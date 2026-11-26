"""The portal's forms: what a person types, checked before it reaches the
models.

Version: 0.3.0 - fields that show only while another field calls for them
                 (DependentFieldsMixin); the icon is a form of its own,
                 uploaded where it is chosen; a package's fields edited
                 after the upload
Version: 0.1.0
"""

from __future__ import annotations

import json

from django import forms
from django.conf import settings
from django.contrib.auth import get_user_model, password_validation
from django.contrib.auth.forms import AuthenticationForm, PasswordResetForm
from django.core.exceptions import ValidationError

from ..store import constants
from ..store.models import App, Package, Publisher, Release, Screenshot, TermsVersion

User = get_user_model()

IMAGE_FORMATS = {"PNG": "png", "JPEG": "jpg", "WEBP": "webp"}


def check_image(upload, *, min_side: int = 0, square: bool = False):
    """A picture the store's pages can show: PNG, JPEG or WebP, not too
    heavy, and for an icon square enough and big enough."""
    if not upload or not hasattr(upload, "image"):
        return upload   # the picture already stored, unchanged
    if upload.size > settings.MAX_IMAGE_MB * 1024 * 1024:
        raise ValidationError(f"A picture is at most {settings.MAX_IMAGE_MB} MB.")
    kind = (upload.image.format or "").upper()
    if kind not in IMAGE_FORMATS:
        raise ValidationError("A picture is a PNG, a JPEG or a WebP file.")
    # The stored name's ending is what the file is, never what the upload called itself.
    upload.name = "picture." + IMAGE_FORMATS[kind]
    width, height = upload.image.size
    if min(width, height) < min_side:
        raise ValidationError(f"The picture is {width} x {height}; it needs {min_side} px or more on each side.")
    if square and abs(width - height) > max(width, height) * 0.02:
        raise ValidationError(f"An icon is square; this one is {width} x {height}.")
    return upload


class DependentFieldsMixin:
    """Fields that matter only while another field has one of a few values:
    the price of an app that is not free, the date free updates end on.

        depends = [(field, controller, values, value when hidden), ...]

    The page hides such a field (portal.js reads `depends_json` from the
    form's data-depends), and the form does not trust the page: whatever a
    hidden field was sent with is replaced by its value when hidden, and no
    error of a field the person cannot see keeps the form from saving. The
    rules are read in order, so a field can depend on one that is itself
    hidden - which then counts as not having the value."""

    depends: list[tuple[str, str, tuple, object]] = []

    @staticmethod
    def _as_text(value) -> str:
        """A value as the page has it: a ticked box is "on", an unticked one
        and nothing are ""."""
        if value is True:
            return "on"
        if value in (False, None):
            return ""
        return str(value)

    @property
    def depends_json(self) -> str:
        return json.dumps([{"field": field, "on": controller, "values": [self._as_text(v) for v in values]}
                           for field, controller, values, _ in self.depends if field in self.fields])

    def clean(self):
        data = super().clean()
        hidden: set[str] = set()
        for field, controller, values, when_hidden in self.depends:
            if field not in self.fields:
                continue
            shown = controller not in hidden and self._as_text(data.get(controller)) in [self._as_text(v) for v in values]
            if not shown:
                hidden.add(field)
                data[field] = when_hidden
                self.errors.pop(field, None)
        return data


class SignUpForm(forms.Form):
    name = forms.CharField(max_length=120, label="Your name")
    email = forms.EmailField(label="Email", help_text="Your sign-in name. A link is mailed to it to prove it is yours.")
    password = forms.CharField(widget=forms.PasswordInput(attrs={"autocomplete": "new-password"}), help_text=f"{settings.PASSWORD_MIN_LENGTH} to {settings.PASSWORD_MAX_LENGTH} characters.")
    password_again = forms.CharField(widget=forms.PasswordInput(attrs={"autocomplete": "new-password"}), label="The password again")

    def clean_email(self):
        return self.cleaned_data["email"].strip().lower()

    def clean(self):
        data = super().clean()
        password = data.get("password")
        if password and password != data.get("password_again"):
            self.add_error("password_again", "The two passwords differ.")
        if password:
            try:
                password_validation.validate_password(password, User(email=data.get("email", ""), name=data.get("name", "")))
            except ValidationError as error:
                self.add_error("password", error)
        return data


class SignInForm(AuthenticationForm):
    error_messages = {
        **AuthenticationForm.error_messages,
        "invalid_login": "The email or the password is wrong.",
        "unverified": "This address is not confirmed yet. Follow the link in the mail we sent, or ask for a new one below.",
    }

    def clean_username(self):
        return self.cleaned_data["username"].strip().lower()

    def confirm_login_allowed(self, user):
        super().confirm_login_allowed(user)
        if not user.email_verified:
            raise ValidationError(self.error_messages["unverified"], code="unverified")


class LeaveForm(forms.Form):
    """Closing or deleting the account, with the password: a signed-in
    browser somebody walked away from must not be enough."""

    CLOSE, DELETE = "close", "delete"
    what = forms.ChoiceField(label="What should happen", widget=forms.RadioSelect, choices=[])
    password = forms.CharField(label="Your password", widget=forms.PasswordInput(attrs={"autocomplete": "current-password"}))

    def __init__(self, *args, user, closed_days: int, grace_days: int, **kwargs):
        super().__init__(*args, **kwargs)
        self.user = user
        self.fields["what"].choices = [
            (self.CLOSE, f"Close the account. It is signed out everywhere and kept for {closed_days} days; signing in again in that time brings it back as it was."),
            (self.DELETE, f"Delete the account. It stays usable for {grace_days} days, in which you can change your mind, and is then removed for good."),
        ]

    def clean_password(self):
        if not self.user.check_password(self.cleaned_data["password"]):
            raise ValidationError("The password is wrong.")
        return self.cleaned_data["password"]


class ClosedAccountResetForm(PasswordResetForm):
    """A closed account's owner who forgot the password can still set a new
    one and come back; an erased account has no usable password and is
    left out as before."""

    def get_users(self, email):
        users = User._default_manager.filter(email__iexact=email, erased_at__isnull=True)
        return (u for u in users if u.has_usable_password() and (u.is_active or u.closed_at is not None))


class EmailForm(forms.Form):
    email = forms.EmailField()

    def clean_email(self):
        return self.cleaned_data["email"].strip().lower()


class ProfileForm(forms.ModelForm):
    class Meta:
        model = User
        fields = ["name"]


class PublisherApplyForm(forms.ModelForm):
    accept_terms = forms.BooleanField(label="I accept the publisher terms", required=True)

    class Meta:
        model = Publisher
        fields = ["name", "slug", "website", "support_email", "description"]
        widgets = {"description": forms.Textarea(attrs={"rows": 4})}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if not TermsVersion.current():
            del self.fields["accept_terms"]


class PublisherProfileForm(forms.ModelForm):
    class Meta:
        model = Publisher
        fields = ["name", "website", "support_email", "description", "legal_name", "vat_id", "payout_details"]
        widgets = {"description": forms.Textarea(attrs={"rows": 4}), "payout_details": forms.Textarea(attrs={"rows": 3})}

    PAYOUT_FIELDS = ("legal_name", "vat_id", "payout_details")

    def __init__(self, *args, owner: bool = True, **kwargs):
        super().__init__(*args, **kwargs)
        # Where the money goes is the owner's to say, and to see.
        if not owner:
            for name in self.PAYOUT_FIELDS:
                del self.fields[name]


class VatRateForm(forms.Form):
    """A VAT rate as the operator enters it. A plain form, not the model's:
    a rate for a country and a day that has one replaces it (the tax page
    saves with update_or_create), so "already exists" is no error here."""

    country = forms.CharField(help_text="ISO 3166-1, two letters (Greece is GR).")
    percent = forms.DecimalField(max_digits=5, decimal_places=2)
    valid_from = forms.DateField(widget=forms.DateInput(attrs={"type": "date"}))
    note = forms.CharField(required=False, max_length=200)

    def clean_country(self):
        country = self.cleaned_data["country"].strip().upper()
        country = "GR" if country == "EL" else country
        if len(country) != 2 or not country.isalpha():
            raise ValidationError("Two letters, such as DE.")
        return country

    def clean_percent(self):
        percent = self.cleaned_data["percent"]
        if not 0 <= percent < 100:
            raise ValidationError("From 0 to under 100.")
        return percent


PAID = ("one-time", "monthly", "yearly")


class AppCreateForm(DependentFieldsMixin, forms.ModelForm):
    class Meta:
        model = App
        fields = ["name", "app_id", "kind", "category"]

    # A runtime or a library is not on the shelves: it has no category.
    depends = [("category", "kind", ("app",), "")]

    def clean_app_id(self):
        return self.cleaned_data["app_id"].strip().lower()


class AppListingForm(DependentFieldsMixin, forms.ModelForm):
    """The listing's texts and settings. The pictures are not here: the icon
    (IconForm) and the screenshots (ScreenshotForm) are uploaded where they
    are chosen, so choosing one never costs what was typed into this form."""

    class Meta:
        model = App
        fields = [
            "name", "summary", "description", "source_language", "category", "tags",
            "license", "open_source", "source_url", "homepage", "support_url", "privacy_url", "age_rating", "requires_account",
            "pricing_model", "price", "currency", "trial_days", "updates_period", "updates_until",
            "visibility", "lifecycle", "successor",
        ]
        widgets = {
            "description": forms.Textarea(attrs={"rows": 10}),
            "updates_until": forms.DateInput(attrs={"type": "date"}),
        }

    sections = [
        ("Store listing", ["name", "summary", "description", "source_language", "category", "tags"]),
        ("Details", ["license", "open_source", "source_url", "homepage", "support_url", "privacy_url", "age_rating", "requires_account"]),
        ("Price", ["pricing_model", "price", "currency", "trial_days", "updates_period", "updates_until"]),
        ("Availability", ["visibility", "lifecycle", "successor"]),
    ]

    # What a free app has no use for is neither asked nor kept: a price, a
    # trial, and which updates a purchase includes (every update of a free app
    # is free). The source link belongs to an open-source app, the successor to
    # a deprecated one.
    depends = [
        ("source_url", "open_source", (True,), ""),
        ("price", "pricing_model", PAID, None),
        ("currency", "pricing_model", PAID, "EUR"),
        ("trial_days", "pricing_model", PAID, 0),
        ("updates_period", "pricing_model", PAID, "always"),
        ("updates_until", "updates_period", ("until",), None),
        ("successor", "lifecycle", ("deprecated",), ""),
    ]

    def clean_currency(self):
        return (self.cleaned_data.get("currency") or "").upper()

    def clean_source_language(self):
        return (self.cleaned_data.get("source_language") or "en").strip().lower()

    def grouped(self):
        return [(title, [self[name] for name in names]) for title, names in self.sections]


class IconForm(forms.ModelForm):
    """The app's icon alone: uploaded as soon as it is chosen."""

    class Meta:
        model = App
        fields = ["icon"]
        widgets = {"icon": forms.FileInput(attrs={"accept": "image/png,image/jpeg,image/webp"})}

    def clean_icon(self):
        if "icon" not in self.files:   # the icon the app has is not an upload
            raise ValidationError("Choose a picture.")
        return check_image(self.cleaned_data.get("icon"), min_side=128, square=True)


class ScreenshotForm(forms.ModelForm):
    class Meta:
        model = Screenshot
        fields = ["image"]
        widgets = {"image": forms.FileInput(attrs={"accept": "image/png,image/jpeg,image/webp"})}

    def clean_image(self):
        return check_image(self.cleaned_data.get("image"), min_side=320)


def parse_depends(text: str) -> list[dict]:
    """'org.ultraos.runtime@0.9.410, org.ultraos.lib.videofx@0.12.0'."""
    out = []
    for item in text.replace("\n", ",").split(","):
        item = item.strip()
        if not item:
            continue
        app_id, _, version = item.partition("@")
        app_id, version = app_id.strip().lower(), version.strip()
        if not constants.APP_ID_PATTERN.match(app_id) or not constants.VERSION_PATTERN.match(version):
            raise ValidationError(f"\"{item}\" is not an id and its exact version: org.ultraos.runtime@0.9.410")
        out.append({"id": app_id, "version": version})
    return out


class ReleaseForm(forms.ModelForm):
    depends_text = forms.CharField(
        label="Depends on", required=False,
        help_text="The exact runtime and library versions this build was made against, separated by commas: org.ultraos.runtime@0.9.410. Empty for an app that carries everything itself.")

    class Meta:
        model = Release
        fields = ["version", "channel", "notes"]
        widgets = {"notes": forms.Textarea(attrs={"rows": 5})}

    def __init__(self, *args, app: App | None = None, **kwargs):
        super().__init__(*args, **kwargs)
        self.app = app or self.instance.app
        if self.instance.pk:
            self.fields["depends_text"].initial = self.instance.depends_text

    def clean_version(self):
        version = self.cleaned_data["version"].strip()
        if Release.objects.filter(app=self.app, version=version).exclude(pk=self.instance.pk).exists():
            raise ValidationError("This app already has that version.")
        return version

    def clean_depends_text(self):
        return parse_depends(self.cleaned_data.get("depends_text", ""))

    def save(self, commit=True):
        self.instance.app = self.app
        self.instance.depends = self.cleaned_data["depends_text"]
        return super().save(commit)


class PackageFieldsForm(forms.ModelForm):
    """What a package says about its archive - the system, the processor,
    what the machine must satisfy, what the store starts: asked with the
    upload (PackageForm) and corrected after it (PackageEditForm)."""

    class Meta:
        model = Package
        fields = ["os", "arch", "variant", "min_os", "entry"]

    def __init__(self, *args, release: Release, **kwargs):
        super().__init__(*args, **kwargs)
        self.release = release

    def clean_variant(self):
        return ",".join(v.strip() for v in self.cleaned_data.get("variant", "").split(",") if v.strip())

    def clean_entry(self):
        return self.cleaned_data.get("entry", "").strip()

    @property
    def choices_json(self) -> str:
        """Which processors go with which system, for the page to offer
        only those (portal.js, data-choices); clean() refuses the rest."""
        return json.dumps([{"field": "arch", "on": "os", "of": {k: list(v) for k, v in constants.ARCHITECTURES_OF.items()},
                            "only": {k: list(v) for k, v in constants.ARCHITECTURE_ONLY_FOR.items()}}])

    def clean(self):
        data = super().clean()
        system, arch = data.get("os"), data.get("arch")
        if system and arch and not constants.architecture_fits(system, arch):
            self.add_error("arch", f"{constants.OS_NAMES.get(system, system)} has no {arch} package.")
        if data.get("os") and data.get("arch") and Package.objects.filter(
                release=self.release, os=data["os"], arch=data["arch"], variant=data.get("variant", "")).exclude(pk=self.instance.pk).exists():
            raise ValidationError("This release already has a package for that system and processor.")
        return data


class PackageForm(PackageFieldsForm):
    file = forms.FileField(label="Archive", help_text="zip, 7z, tar.gz, tar.xz, tar.zst, tar.bz2 or an AppImage.")

    field_order = ["file", "os", "arch", "entry", "min_os", "variant"]

    def clean_file(self):
        upload = self.cleaned_data["file"]
        if not constants.package_format_of(upload.name or ""):
            raise ValidationError("The store unpacks zip, 7z, tar.gz, tar.xz, tar.zst, tar.bz2 archives and AppImages.")
        if upload.size > settings.MAX_PACKAGE_MB * 1024 * 1024:
            raise ValidationError(f"A package is at most {settings.MAX_PACKAGE_MB} MB.")
        return upload


class PackageEditForm(PackageFieldsForm):
    """A package of a release that is not in review, corrected in place: the
    archive stays as uploaded, what is said about it changes."""

    field_order = ["os", "arch", "entry", "min_os", "variant"]

    def __init__(self, *args, **kwargs):
        super().__init__(*args, release=kwargs["instance"].release, **kwargs)


class ReportForm(forms.Form):
    kind = forms.ChoiceField(choices=constants.REPORT_KINDS, label="What is wrong")
    text = forms.CharField(widget=forms.Textarea(attrs={"rows": 6}), max_length=4000, label="What happened")
    reporter = forms.EmailField(required=False, label="Your email (optional)",
                                help_text="Only the store's operator sees it, never the publisher.")


class DecisionForm(forms.Form):
    action = forms.CharField()
    message = forms.CharField(required=False, widget=forms.Textarea(attrs={"rows": 4}), max_length=4000)


class ReportDecisionForm(forms.Form):
    action = forms.ChoiceField(choices=[("resolve", "Resolve"), ("dismiss", "Dismiss")])
    findings = forms.CharField(required=False, widget=forms.Textarea(attrs={"rows": 4}), max_length=4000)
    outcome = forms.CharField(max_length=300)
