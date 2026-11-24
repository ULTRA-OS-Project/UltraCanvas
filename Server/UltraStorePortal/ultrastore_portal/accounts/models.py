"""A person at the portal: the email is the sign-in name, proven by a mailed
link before the first sign-in. A publisher's people and the operator's
staff are the same kind of account; what one may do comes from the
publisher memberships (store.PublisherMember) and from is_staff.

Version: 0.4.0 - a new password ends every grant and sign-in (save)
Version: 0.1.0
"""

from __future__ import annotations

from django.contrib.auth.models import AbstractBaseUser, BaseUserManager, PermissionsMixin
from django.core import signing
from django.db import models
from django.utils import timezone

VERIFY_SALT = "ultrastore-portal.verify-email"
VERIFY_MAX_AGE = 2 * 24 * 3600


class UserManager(BaseUserManager):
    use_in_migrations = True

    def _create(self, email: str, password: str | None, **extra):
        if not email:
            raise ValueError("An account needs an email address")
        user = self.model(email=self.normalize_email(email).lower(), **extra)
        user.set_password(password)
        user.save(using=self._db)
        return user

    def create_user(self, email: str, password: str | None = None, **extra):
        extra.setdefault("is_staff", False)
        extra.setdefault("is_superuser", False)
        return self._create(email, password, **extra)

    def create_superuser(self, email: str, password: str | None = None, **extra):
        extra["is_staff"] = True
        extra["is_superuser"] = True
        extra.setdefault("email_verified", True)
        return self._create(email, password, **extra)

    def get_by_natural_key(self, email: str):
        return self.get(email=(email or "").strip().lower())


class User(AbstractBaseUser, PermissionsMixin):
    email = models.EmailField(unique=True)
    name = models.CharField(max_length=120, blank=True)
    email_verified = models.BooleanField(default=False)
    is_staff = models.BooleanField("operator", default=False, help_text="May open the operator's pages and the admin site.")
    is_active = models.BooleanField(default=True)
    date_joined = models.DateTimeField(default=timezone.now)

    objects = UserManager()

    USERNAME_FIELD = "email"
    EMAIL_FIELD = "email"
    REQUIRED_FIELDS: list[str] = []

    class Meta:
        ordering = ["email"]

    def __str__(self) -> str:
        return self.email

    def save(self, *args, **kwargs):
        """A new password ends what the old one started, wherever it was
        set - the account's pages, the admin site, the command line: every
        service's grant with its tokens, and every sign-in (but the one the
        change is made in, which a page names in `keep_sign_in`)."""
        changed = self._password is not None and self.pk is not None
        super().save(*args, **kwargs)
        if changed:
            from ..identity import provider   # the identity app's models name this one
            provider.password_changed(self, keep_sid=getattr(self, "keep_sign_in", ""))

    @property
    def display_name(self) -> str:
        return self.name or self.email.split("@", 1)[0]

    def get_full_name(self) -> str:
        return self.display_name

    def get_short_name(self) -> str:
        return self.display_name

    def verification_token(self) -> str:
        return signing.dumps({"u": self.pk, "e": self.email}, salt=VERIFY_SALT)

    @classmethod
    def from_verification_token(cls, token: str) -> "User | None":
        try:
            data = signing.loads(token, salt=VERIFY_SALT, max_age=VERIFY_MAX_AGE)
            return cls.objects.get(pk=data["u"], email=data["e"])
        except (signing.BadSignature, cls.DoesNotExist, KeyError, TypeError):
            return None
