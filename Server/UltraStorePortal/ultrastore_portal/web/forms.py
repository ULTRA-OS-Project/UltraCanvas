"""The portal's forms: what a person types, checked before it reaches the
models.

Version: 0.1.0
"""

from __future__ import annotations

from django import forms
from django.conf import settings
from django.contrib.auth import get_user_model, password_validation
from django.contrib.auth.forms import AuthenticationForm
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


class SignUpForm(forms.Form):
    name = forms.CharField(max_length=120, label="Your name")
    email = forms.EmailField(label="Email", help_text="Your sign-in name. A link is mailed to it to prove it is yours.")
    password = forms.CharField(widget=forms.PasswordInput(attrs={"autocomplete": "new-password"}), help_text="Twelve characters or more.")
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
        fields = ["name", "website", "support_email", "description"]
        widgets = {"description": forms.Textarea(attrs={"rows": 4})}


class AppCreateForm(forms.ModelForm):
    class Meta:
        model = App
        fields = ["name", "app_id", "kind", "category"]

    def clean_app_id(self):
        return self.cleaned_data["app_id"].strip().lower()


class AppListingForm(forms.ModelForm):
    class Meta:
        model = App
        fields = [
            "name", "summary", "description", "source_language", "category", "tags", "icon",
            "license", "open_source", "source_url", "homepage", "support_url", "privacy_url", "age_rating", "requires_account",
            "pricing_model", "price", "currency", "trial_days", "updates_period", "updates_until",
            "visibility", "lifecycle", "successor",
        ]
        widgets = {
            "description": forms.Textarea(attrs={"rows": 10}),
            "updates_until": forms.DateInput(attrs={"type": "date"}),
            "icon": forms.FileInput(attrs={"accept": "image/png,image/jpeg,image/webp"}),
        }

    sections = [
        ("Store listing", ["name", "summary", "description", "source_language", "category", "tags", "icon"]),
        ("Details", ["license", "open_source", "source_url", "homepage", "support_url", "privacy_url", "age_rating", "requires_account"]),
        ("Price", ["pricing_model", "price", "currency", "trial_days", "updates_period", "updates_until"]),
        ("Availability", ["visibility", "lifecycle", "successor"]),
    ]

    def clean_icon(self):
        return check_image(self.cleaned_data.get("icon"), min_side=128, square=True)

    def clean_currency(self):
        return (self.cleaned_data.get("currency") or "").upper()

    def clean_source_language(self):
        return (self.cleaned_data.get("source_language") or "en").strip().lower()

    def grouped(self):
        return [(title, [self[name] for name in names]) for title, names in self.sections]


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


class PackageForm(forms.ModelForm):
    file = forms.FileField(label="Archive", help_text="zip, 7z, tar.gz, tar.xz, tar.zst, tar.bz2 or an AppImage.")

    class Meta:
        model = Package
        fields = ["os", "arch", "variant", "min_os", "entry"]

    field_order = ["file", "os", "arch", "entry", "min_os", "variant"]

    def __init__(self, *args, release: Release, **kwargs):
        super().__init__(*args, **kwargs)
        self.release = release

    def clean_file(self):
        upload = self.cleaned_data["file"]
        if not constants.package_format_of(upload.name or ""):
            raise ValidationError("The store unpacks zip, 7z, tar.gz, tar.xz, tar.zst, tar.bz2 archives and AppImages.")
        if upload.size > settings.MAX_PACKAGE_MB * 1024 * 1024:
            raise ValidationError(f"A package is at most {settings.MAX_PACKAGE_MB} MB.")
        return upload

    def clean_variant(self):
        return ",".join(v.strip() for v in self.cleaned_data.get("variant", "").split(",") if v.strip())

    def clean(self):
        data = super().clean()
        if data.get("os") and data.get("arch") and Package.objects.filter(
                release=self.release, os=data["os"], arch=data["arch"], variant=data.get("variant", "")).exists():
            raise ValidationError("This release already has a package for that system and processor.")
        return data


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
