"""What the store is made of: the publishers and their people, the apps with
their listings, the releases a review decides on, the packages a release
carries, the reports from the store's pages, the audit of every decision,
and the signed catalogs published from all of it.

One store, one catalog: there is no private or per-publisher catalog here.
A publisher owns listings and releases; the operator's review decides what
of it is public, and the one catalog.json is generated from that
(catalog.py).

Version: 0.1.0
"""

from __future__ import annotations

import secrets
import uuid

from django.conf import settings
from django.core.exceptions import ValidationError
from django.core.validators import MinValueValidator, RegexValidator
from django.db import models
from django.urls import reverse
from django.utils import timezone

from . import constants

SLUG_VALIDATOR = RegexValidator(r"^[a-z0-9]+(-[a-z0-9]+)*$", "Lower-case letters, digits and single hyphens: ultra-os-project")
APP_ID_VALIDATOR = RegexValidator(constants.APP_ID_PATTERN, "A reverse-DNS id in lower case: org.example.myapp")
VERSION_VALIDATOR = RegexValidator(constants.VERSION_PATTERN, "A version: 1.2.3")
MIN_OS_VALIDATOR = RegexValidator(constants.MIN_OS_PATTERN, "A version of digits and dots: 14.0, 10.0.17763")


class TermsVersion(models.Model):
    """One version of the publisher terms (the developer agreement)."""

    version = models.PositiveIntegerField(unique=True)
    text = models.TextField()
    changes = models.TextField(blank=True, help_text="What changed against the version before.")
    in_force_from = models.DateField(default=timezone.localdate)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-version"]

    def __str__(self) -> str:
        return f"Terms {self.version}"

    @classmethod
    def current(cls) -> "TermsVersion | None":
        return cls.objects.filter(in_force_from__lte=timezone.localdate()).order_by("-version").first()


class Publisher(models.Model):
    """A publisher (a developer account): applied for by a person, decided
    by the operator."""

    APPLIED, APPROVED, REJECTED, SUSPENDED = "applied", "approved", "rejected", "suspended"
    STATES = [(APPLIED, "Waiting for the operator"), (APPROVED, "Approved"), (REJECTED, "Rejected"), (SUSPENDED, "Suspended")]

    slug = models.CharField("publisher id", max_length=64, unique=True, validators=[SLUG_VALIDATOR],
                            help_text="The id the catalog names the publisher by. It cannot be changed later.")
    name = models.CharField(max_length=120, help_text="The name buyers see.")
    website = models.URLField(blank=True)
    support_email = models.EmailField(help_text="Where buyers and the operator reach you.")
    description = models.TextField(blank=True, max_length=2000, help_text="A few lines about who you are.")
    state = models.CharField(max_length=16, choices=STATES, default=APPLIED, db_index=True)
    decision_message = models.TextField(blank=True)
    decided_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    decided_at = models.DateTimeField(null=True, blank=True)
    terms_version = models.PositiveIntegerField(null=True, blank=True)
    terms_accepted_at = models.DateTimeField(null=True, blank=True)
    terms_accepted_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    created = models.DateTimeField(auto_now_add=True)
    updated = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ["name"]

    def __str__(self) -> str:
        return self.name

    def get_absolute_url(self) -> str:
        return reverse("store:publisher", args=[self.slug])

    @property
    def approved(self) -> bool:
        return self.state == self.APPROVED

    def terms_due(self) -> "TermsVersion | None":
        """The terms version in force this publisher has not accepted yet."""
        current = TermsVersion.current()
        if current and (self.terms_version or 0) < current.version:
            return current
        return None


class PublisherMember(models.Model):
    """Who may act for a publisher. The first member is the applicant, as
    the owner; more people per publisher come later."""

    OWNER, DEVELOPER = "owner", "developer"
    MARKETING, BILLING, ANALYST = "marketing", "billing", "analyst"
    # Owner: everything. Developer: the apps, their listings and releases.
    # The other three are advertising's (promote/): Marketing runs the
    # campaigns, Billing pays into the balance, Analyst reads and changes
    # nothing.
    ROLES = [(OWNER, "Owner"), (DEVELOPER, "Developer"), (MARKETING, "Marketing"), (BILLING, "Billing"), (ANALYST, "Analyst")]
    APPS = (OWNER, DEVELOPER)

    publisher = models.ForeignKey(Publisher, on_delete=models.CASCADE, related_name="members")
    user = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name="memberships")
    role = models.CharField(max_length=16, choices=ROLES, default=OWNER)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["publisher", "user"], name="one_membership_per_person")]

    def __str__(self) -> str:
        return f"{self.user} - {self.publisher} ({self.role})"


def icon_path(app: "App", file_name: str) -> str:
    return f"apps/{app.app_id}/icon-{secrets.token_hex(4)}.{file_name.rsplit('.', 1)[-1].lower()}"


def screenshot_path(shot: "Screenshot", file_name: str) -> str:
    return f"apps/{shot.app.app_id}/shot-{secrets.token_hex(4)}.{file_name.rsplit('.', 1)[-1].lower()}"


class AppQuerySet(models.QuerySet):
    def public(self):
        """What the catalog carries: an approved publisher's apps that are
        not suspended and have a published release."""
        return self.filter(publisher__state=Publisher.APPROVED, suspended=False,
                           releases__state=Release.PUBLISHED).distinct()

    def listed(self):
        """What the store's pages list (an unlisted or hidden app is
        reached by its link or its id only)."""
        return self.public().filter(kind="app", visibility="listed").exclude(lifecycle="withdrawn")


class App(models.Model):
    """An app, a runtime or a library: the listing and the settings the
    catalog carries (Docs/UltraStore/README.md, *The admin backend's
    settings*)."""

    app_id = models.CharField("id", max_length=120, unique=True, validators=[APP_ID_VALIDATOR],
                              help_text="Reverse-DNS, lower case: org.example.myapp. It cannot be changed later.")
    publisher = models.ForeignKey(Publisher, on_delete=models.PROTECT, related_name="apps")
    kind = models.CharField(max_length=16, choices=constants.KINDS, default="app",
                            help_text="A runtime or a library is a shared layer other apps depend on.")
    name = models.CharField(max_length=60)
    summary = models.CharField(max_length=120, blank=True, help_text="One line under the name.")
    description = models.TextField(blank=True, max_length=8000)
    source_language = models.CharField(max_length=8, default="en", help_text="The language the texts are written in.")
    category = models.CharField(max_length=32, choices=constants.CATEGORIES, blank=True, db_index=True)
    tags = models.CharField(max_length=200, blank=True, help_text="Search words, separated by commas.")
    license = models.CharField(max_length=60, blank=True, help_text="MIT, GPL-3.0-or-later, Proprietary")
    icon = models.ImageField(upload_to=icon_path, blank=True, help_text="A square PNG, JPEG or WebP, 256 px or more.")
    homepage = models.URLField(blank=True)
    support_url = models.URLField(blank=True)
    privacy_url = models.URLField("privacy policy", blank=True)
    open_source = models.BooleanField(default=False)
    source_url = models.URLField(blank=True)
    requires_account = models.BooleanField(default=False, help_text="The app is useless without its own user account.")
    age_rating = models.CharField(max_length=4, choices=constants.AGE_RATINGS, blank=True)

    pricing_model = models.CharField(max_length=16, choices=constants.PRICING_MODELS, default="free")
    price = models.DecimalField(max_digits=9, decimal_places=2, null=True, blank=True, validators=[MinValueValidator(0)])
    currency = models.CharField(max_length=3, blank=True, default="EUR")
    trial_days = models.PositiveIntegerField(default=0)
    updates_period = models.CharField("free updates", max_length=8, choices=constants.UPDATE_PERIODS, default="always")
    updates_until = models.DateField(null=True, blank=True)

    visibility = models.CharField(max_length=16, choices=constants.VISIBILITIES, default="listed")
    lifecycle = models.CharField(max_length=16, choices=constants.LIFECYCLES, default="active")
    successor = models.CharField(max_length=120, blank=True, help_text="The id of the app that replaces a deprecated one.")

    # The operator's.
    featured = models.BooleanField(default=False, help_text="An editor's pick on the store's front page.")
    suspended = models.BooleanField(default=False, db_index=True)
    suspend_reason = models.TextField(blank=True)

    downloads = models.PositiveBigIntegerField(default=0, editable=False)
    first_published = models.DateTimeField(null=True, blank=True, editable=False)
    last_published = models.DateTimeField(null=True, blank=True, editable=False, db_index=True)
    created = models.DateTimeField(auto_now_add=True)
    updated = models.DateTimeField(auto_now=True)

    objects = AppQuerySet.as_manager()

    class Meta:
        ordering = ["name"]

    def __str__(self) -> str:
        return f"{self.name} ({self.app_id})"

    def get_absolute_url(self) -> str:
        return reverse("store:app", args=[self.app_id])

    def clean(self) -> None:
        errors = {}
        if self.pricing_model != "free":
            if self.price is None or self.price <= 0:
                errors["price"] = "A paid app needs a price."
            if len(self.currency or "") != 3 or not self.currency.isalpha():
                errors["currency"] = "A three-letter currency: EUR"
        if self.updates_period == "until" and not self.updates_until:
            errors["updates_until"] = "Name the date the free updates end."
        if self.lifecycle != "deprecated" and self.successor:
            errors["successor"] = "Only a deprecated app names a successor."
        if errors:
            raise ValidationError(errors)

    @property
    def is_free(self) -> bool:
        return self.pricing_model == "free"

    @property
    def restricted_packages(self) -> bool:
        """A paid app's packages without a trial are served to buyers only
        (the publish folder's restricted/)."""
        return self.kind == "app" and not self.is_free and self.trial_days == 0

    @property
    def tag_list(self) -> list[str]:
        return [t.strip() for t in self.tags.split(",") if t.strip()]

    @property
    def category_name(self) -> str:
        return constants.CATEGORY_NAMES.get(self.category, "")

    def published_releases(self):
        return [r for r in self.releases.all() if r.state == Release.PUBLISHED]

    def latest_release(self) -> "Release | None":
        """The newest published release, a stable one before a beta."""
        published = sorted(self.published_releases(), key=lambda r: (r.channel == "stable", r.published_at or r.created), reverse=True)
        return published[0] if published else None

    @property
    def is_public(self) -> bool:
        return self.publisher.approved and not self.suspended and bool(self.published_releases())

    def price_label(self) -> str:
        if self.is_free:
            return "Free"
        suffix = {"monthly": " / month", "yearly": " / year"}.get(self.pricing_model, "")
        return f"{self.price:.2f} {self.currency}{suffix}"


class Screenshot(models.Model):
    app = models.ForeignKey(App, on_delete=models.CASCADE, related_name="screenshots")
    image = models.ImageField(upload_to=screenshot_path)
    position = models.PositiveIntegerField(default=0)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["position", "id"]

    def __str__(self) -> str:
        return f"{self.app.app_id} screenshot {self.position}"


class Release(models.Model):
    """One version of an app, and the row the review works on: drafted by
    the publisher, submitted, decided by the operator. A published release
    is never edited; what changes is a new version."""

    DRAFT, SUBMITTED, CHANGES, REJECTED, PUBLISHED, WITHDRAWN = "draft", "submitted", "changes_requested", "rejected", "published", "withdrawn"
    STATES = [
        (DRAFT, "Draft"),
        (SUBMITTED, "In review"),
        (CHANGES, "Changes requested"),
        (REJECTED, "Rejected"),
        (PUBLISHED, "Published"),
        (WITHDRAWN, "Withdrawn"),
    ]
    EDITABLE = (DRAFT, CHANGES)

    app = models.ForeignKey(App, on_delete=models.CASCADE, related_name="releases")
    version = models.CharField(max_length=40, validators=[VERSION_VALIDATOR])
    channel = models.CharField(max_length=8, choices=constants.CHANNELS, default="stable")
    notes = models.TextField("what's new", blank=True, max_length=4000)
    depends = models.JSONField(default=list, blank=True,
                               help_text="The exact component versions this build runs on: [{id, version}].")
    state = models.CharField(max_length=20, choices=STATES, default=DRAFT, db_index=True)
    submitted_at = models.DateTimeField(null=True, blank=True)
    submitted_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    review_message = models.TextField(blank=True, help_text="What the reviewer tells the publisher.")
    reviewed_at = models.DateTimeField(null=True, blank=True)
    reviewed_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    published_at = models.DateTimeField(null=True, blank=True)
    created = models.DateTimeField(auto_now_add=True)
    updated = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ["-created"]
        constraints = [models.UniqueConstraint(fields=["app", "version"], name="one_release_per_version")]

    def __str__(self) -> str:
        return f"{self.app.app_id} {self.version}"

    @property
    def editable(self) -> bool:
        return self.state in self.EDITABLE

    @property
    def depends_text(self) -> str:
        return ", ".join(f"{d.get('id')}@{d.get('version')}" for d in self.depends or [])

    def total_size(self) -> int:
        return sum(p.size for p in self.packages.all())


class Package(models.Model):
    """One archive of a release for one system and processor. The bytes
    live in the package store under their SHA-256 (packages.py); a package
    is what its hash in the signed catalog says it is."""

    release = models.ForeignKey(Release, on_delete=models.CASCADE, related_name="packages")
    os = models.CharField("system", max_length=16, choices=constants.OPERATING_SYSTEMS)
    arch = models.CharField("processor", max_length=16, choices=constants.ARCHITECTURES)
    variant = models.CharField(max_length=120, blank=True, help_text="What the machine must satisfy, separated by commas: glibc-2.31")
    min_os = models.CharField("minimum system version", max_length=32, blank=True, validators=[MIN_OS_VALIDATOR],
                              help_text="14.0 (macOS), 10.0.17763 (Windows)")
    format = models.CharField(max_length=16)
    entry = models.CharField(max_length=200, blank=True, help_text="What the store starts, inside the archive: MyApp/MyApp, MyApp.app")
    sha256 = models.CharField(max_length=64, db_index=True)
    size = models.PositiveBigIntegerField()
    original_name = models.CharField(max_length=255, blank=True)
    downloads = models.PositiveBigIntegerField(default=0, editable=False)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["os", "arch", "id"]
        constraints = [models.UniqueConstraint(fields=["release", "os", "arch", "variant"], name="one_package_per_target")]

    def __str__(self) -> str:
        return f"{self.release} {self.os}/{self.arch}"

    @property
    def file_name(self) -> str:
        return f"{self.sha256}.{self.format}"

    @property
    def variant_list(self) -> list[str]:
        return [v.strip() for v in self.variant.split(",") if v.strip()]

    @property
    def os_name(self) -> str:
        return constants.OS_NAMES.get(self.os, self.os)


def _report_id() -> str:
    return uuid.uuid4().hex


def _report_secret() -> str:
    return secrets.token_urlsafe(24)


class Report(models.Model):
    """A problem reported from the store's app page (the native store's
    *Report a problem* or the web page's). The reporter is the operator's
    to see, never the publisher's; the reporter learns one outcome line."""

    OPEN, RESOLVED, DISMISSED = "open", "resolved", "dismissed"
    STATES = [(OPEN, "Open"), (RESOLVED, "Resolved"), (DISMISSED, "Dismissed")]
    TARGET_KINDS = [("app", "App"), ("release", "Release"), ("merchant", "Publisher"), ("ad", "Advertisement")]

    public_id = models.CharField(max_length=32, unique=True, default=_report_id, editable=False)
    secret = models.CharField(max_length=64, default=_report_secret, editable=False)
    target_kind = models.CharField(max_length=16, choices=TARGET_KINDS, default="app")
    target = models.CharField(max_length=160)
    kind = models.CharField(max_length=32, default="other")
    text = models.TextField(max_length=4000)
    reporter = models.CharField(max_length=255, blank=True)
    state = models.CharField(max_length=16, choices=STATES, default=OPEN, db_index=True)
    findings = models.TextField(blank=True, help_text="For the operator only.")
    outcome = models.CharField(max_length=300, blank=True, help_text="The one line the reporter learns.")
    handled_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    handled_at = models.DateTimeField(null=True, blank=True)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-created"]

    def __str__(self) -> str:
        return f"{self.kind}: {self.target}"


class AuditEntry(models.Model):
    """Every decision and every publish, one row each, never edited."""

    actor = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name="+")
    actor_name = models.CharField(max_length=254)
    action = models.CharField(max_length=64, db_index=True)
    target_kind = models.CharField(max_length=32)
    target = models.CharField(max_length=200, db_index=True)
    detail = models.JSONField(default=dict, blank=True)
    created = models.DateTimeField(auto_now_add=True, db_index=True)

    class Meta:
        ordering = ["-id"]
        verbose_name_plural = "audit entries"

    def __str__(self) -> str:
        return f"{self.actor_name} {self.action} {self.target}"


class CatalogSnapshot(models.Model):
    """A published catalog: the exact bytes that were signed, so what the
    store fetched can always be shown again."""

    sequence = models.PositiveBigIntegerField(unique=True)
    body = models.BinaryField()
    signature = models.CharField(max_length=128)
    public_key = models.CharField(max_length=64)
    digest = models.CharField(max_length=64, help_text="SHA-256 of the content without the sequence and the date.")
    apps = models.PositiveIntegerField(default=0)
    components = models.PositiveIntegerField(default=0)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-sequence"]
        get_latest_by = "sequence"

    def __str__(self) -> str:
        return f"Catalog {self.sequence}"


class Setting(models.Model):
    """What the portal keeps for itself (the signing seed made on the first
    publish)."""

    key = models.CharField(max_length=64, unique=True)
    value = models.TextField(blank=True)

    def __str__(self) -> str:
        return self.key
