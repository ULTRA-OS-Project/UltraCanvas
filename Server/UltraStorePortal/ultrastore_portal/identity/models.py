"""What the account keeps so that other services can sign a person in
through it (OpenID Connect, identity/provider.py).

    Client      a service that may ask: its id, where it may send a person
                back to, whether it can keep a secret, whether it is ours
    SignIn      one sign-in at the account, from the password to the
                sign-out: what every service signed in through it hangs on
    Consent     what a person allowed a service that is not ours to learn
    Grant       one authorization: this person, this service, these scopes
    Code        the authorization code of a grant: good once, for two minutes
    Token       an access or a refresh token of a grant
    SigningKey  the keys the identity tokens are signed with

No secret is kept as it was handed out: a client's secret, a code and a
token are stored as their SHA-256, which is enough for values that are
256 random bits and lets a request find its row by one indexed lookup.

Version: 0.4.0
"""

from __future__ import annotations

import hashlib
import secrets
from datetime import timedelta
from urllib.parse import urlsplit

from django.conf import settings
from django.core.exceptions import ValidationError
from django.db import models
from django.utils import timezone

SCOPES = {
    "openid": "Sign you in",
    "profile": "Your name",
    "email": "Your email address",
    "offline_access": "Stay signed in while you are away",
}
LOOPBACK_HOSTS = {"127.0.0.1", "::1", "localhost"}


def digest(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def new_secret() -> str:
    return secrets.token_urlsafe(32)


def is_loopback(uri: str) -> bool:
    parts = urlsplit(uri)
    return parts.scheme == "http" and (parts.hostname or "") in LOOPBACK_HOSTS


def check_redirect_uri(uri: str) -> None:
    """Where a person may be sent back to: an https address, or http on this
    machine for an application that listens on a port of its own (RFC 8252).
    Never with a fragment, never anything a browser would not open as a page."""
    parts = urlsplit(uri)
    if parts.fragment or "#" in uri:
        raise ValidationError(f"A redirect address has no fragment: {uri}")
    if not parts.hostname:
        raise ValidationError(f"A redirect address is a whole address (https://service.example/callback): {uri}")
    if parts.scheme == "https" or is_loopback(uri):
        return
    raise ValidationError(f"A redirect address is https, or http on 127.0.0.1 for an application on the person's machine: {uri}")


def lines(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines() if line.strip()]


class Client(models.Model):
    """A service that signs people in through the account."""

    CONFIDENTIAL, PUBLIC = "confidential", "public"
    KINDS = [(CONFIDENTIAL, "A server that keeps a secret"), (PUBLIC, "An application or a page that cannot (PKCE instead)")]

    client_id = models.SlugField(max_length=64, unique=True, help_text="What the service names itself by: ultracloud, chatcube. Fixed once in use.")
    name = models.CharField(max_length=120, help_text="What a person reads on the consent page.")
    kind = models.CharField(max_length=16, choices=KINDS, default=CONFIDENTIAL)
    secret_hash = models.CharField(max_length=64, blank=True, editable=False)
    redirect_uris = models.TextField(help_text="One address per line, matched exactly. http://127.0.0.1/... stands for any port on the person's machine.")
    post_logout_redirect_uris = models.TextField(blank=True, help_text="Where a person may be sent after signing out, one per line.")
    scopes = models.CharField(max_length=200, default="openid profile email", help_text="What the service may ask for: openid profile email offline_access.")
    trusted = models.BooleanField("ours", default=False, help_text="One of our own services: no consent page, and the same subject as our other services see.")
    require_pkce = models.BooleanField(default=True, help_text="Refuse a request without a code challenge. Always on for a public client.")
    resource_server = models.BooleanField(default=False, help_text="A service that is handed other services' tokens and may ask whether one is good (introspection).")
    sector = models.CharField(max_length=200, blank=True, help_text="What the person's subject is made for. Empty: the host of the first redirect address.")
    website = models.URLField(blank=True)
    active = models.BooleanField(default=True)
    created = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["client_id"]

    def __str__(self) -> str:
        return self.client_id

    def clean(self) -> None:
        uris = lines(self.redirect_uris)
        if not uris:
            raise ValidationError({"redirect_uris": "A client needs at least one redirect address."})
        try:
            for uri in uris + lines(self.post_logout_redirect_uris):
                check_redirect_uri(uri)
        except ValidationError as error:
            raise ValidationError({"redirect_uris": error.messages}) from error
        unknown = [scope for scope in self.scopes.split() if scope not in SCOPES]
        if unknown or "openid" not in self.scopes.split():
            raise ValidationError({"scopes": "The scopes are openid and any of profile, email, offline_access."})
        if self.kind == self.PUBLIC:
            self.require_pkce = True

    def new_secret(self) -> str:
        """A new secret, shown once: only its hash is kept."""
        secret = new_secret()
        self.secret_hash = digest(secret)
        return secret

    def check_secret(self, secret: str) -> bool:
        return bool(self.secret_hash) and secrets.compare_digest(self.secret_hash, digest(secret or ""))

    def allows_redirect(self, uri: str, post_logout: bool = False) -> bool:
        """Exactly a registered address - or, for one on this machine, the
        registered one on whatever port the application got."""
        for registered in lines(self.post_logout_redirect_uris if post_logout else self.redirect_uris):
            if uri == registered:
                return True
            if is_loopback(registered) and is_loopback(uri):
                a, b = urlsplit(registered), urlsplit(uri)
                if (a.hostname, a.path, a.query) == (b.hostname, b.path, b.query) and not b.fragment and not b.username:
                    return True
        return False

    def allowed_scopes(self) -> set[str]:
        return set(self.scopes.split())

    def subject_sector(self) -> str:
        """Our own services share one subject for a person; every other
        service gets one of its own, so two of them cannot compare."""
        if self.trusted:
            return ""
        return self.sector or (urlsplit(lines(self.redirect_uris)[0]).hostname or self.client_id)


class SignIn(models.Model):
    """One sign-in at the account. A service that signed a person in through
    it is told its id (sid), and the store's own session ends with it."""

    sid = models.CharField(max_length=64, unique=True, default=new_secret, editable=False)
    user = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name="sign_ins")
    created = models.DateTimeField(default=timezone.now)
    ended = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return f"{self.user} {self.created:%Y-%m-%d %H:%M}"

    @property
    def over(self) -> bool:
        return self.ended is not None or self.created + timedelta(seconds=settings.SESSION_COOKIE_AGE) < timezone.now()


class Consent(models.Model):
    """What a person allowed a service to learn; asked once, kept until
    taken back on the account's page."""

    user = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name="consents")
    client = models.ForeignKey(Client, on_delete=models.CASCADE, related_name="consents")
    scope = models.CharField(max_length=200)
    granted = models.DateTimeField(default=timezone.now)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["user", "client"], name="one_consent_per_client")]

    def __str__(self) -> str:
        return f"{self.user} -> {self.client}: {self.scope}"

    def covers(self, scopes: set[str]) -> bool:
        return scopes <= set(self.scope.split())

    @property
    def scope_labels(self) -> list[str]:
        """What the service was told, in a person's words."""
        return [SCOPES[scope] for scope in self.scope.split() if scope in SCOPES and scope != "openid"] or ["That it is you"]


class Grant(models.Model):
    """One authorization. Its code and every token made from it belong to
    it, so taking the grant back takes all of them back."""

    user = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name="grants")
    client = models.ForeignKey(Client, on_delete=models.CASCADE, related_name="grants")
    scope = models.CharField(max_length=200)
    nonce = models.CharField(max_length=255, blank=True)
    sid = models.CharField(max_length=64, blank=True, db_index=True)
    auth_time = models.DateTimeField()
    created = models.DateTimeField(default=timezone.now)
    revoked = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return f"{self.user} -> {self.client} ({self.scope})"

    def revoke(self) -> None:
        if self.revoked is None:
            self.revoked = timezone.now()
            self.save(update_fields=["revoked"])


class Code(models.Model):
    grant = models.OneToOneField(Grant, on_delete=models.CASCADE, related_name="code")
    code_hash = models.CharField(max_length=64, unique=True)
    redirect_uri = models.CharField(max_length=2000)
    code_challenge = models.CharField(max_length=128, blank=True)
    expires = models.DateTimeField()
    used = models.DateTimeField(null=True, blank=True)


class Token(models.Model):
    ACCESS, REFRESH = "access", "refresh"
    KINDS = [(ACCESS, "Access"), (REFRESH, "Refresh")]

    grant = models.ForeignKey(Grant, on_delete=models.CASCADE, related_name="tokens")
    kind = models.CharField(max_length=8, choices=KINDS)
    token_hash = models.CharField(max_length=64, unique=True)
    scope = models.CharField(max_length=200)
    created = models.DateTimeField(default=timezone.now)
    expires = models.DateTimeField(db_index=True)
    used = models.DateTimeField(null=True, blank=True, help_text="A refresh token: when it was exchanged for the next one.")
    revoked = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return f"{self.kind} of {self.grant}"

    @property
    def live(self) -> bool:
        return self.revoked is None and self.used is None and self.expires > timezone.now() and self.grant.revoked is None


class SigningKey(models.Model):
    """An RSA key the identity tokens are signed with. The newest one that is
    not retired signs; every one not retired is published (the JWKS), so a
    token signed before a rotation still verifies."""

    kid = models.CharField(max_length=64, unique=True)
    pem = models.TextField()
    created = models.DateTimeField(default=timezone.now)
    retired = models.DateTimeField(null=True, blank=True)

    class Meta:
        ordering = ["-id"]

    def __str__(self) -> str:
        return self.kid
