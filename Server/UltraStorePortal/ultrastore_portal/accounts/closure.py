"""Closing an account, deleting one, and coming back.

Two ways to leave, because they mean different things:

- **Closed.** The person closes the account: it is signed out everywhere,
  every service's access ends, and nothing signs in to it - but it is kept,
  whole, for ACCOUNT_CLOSED_DAYS (365). Signing in with the right password
  in that time offers to **reactivate** it, and it is as it was. After that
  it is erased.
- **Deleting.** The person asks for the account to be deleted: it stays
  usable for ACCOUNT_DELETION_GRACE_DAYS (90), with the day it goes shown on
  the account's page, and *Keep my account* takes the request back. After
  that it is erased.

**Erased** is the end of both: the row stays under its id, so what names it
still names something, with the address replaced (and free again), the name
and the password gone, every grant, token, sign-in and consent removed, the
memberships removed, and the audit rows it acted in naming "account <id>"
in place of the address. Orders are not the account's (a purchase is
reached by its order's secret) and stay as the law asks.

An operator's account and the owner of a publisher cannot leave this way:
the first is removed by another operator, the second hands the publisher
over first - an app must not be left with nobody behind it.

`maintain()` erases what is due; a daily timer runs it
(`ultrastore-portal accounts maintain`).

Version: 0.8.0
"""

from __future__ import annotations

import datetime as dt

from django.conf import settings
from django.core import signing
from django.core.mail import send_mail
from django.db import transaction
from django.template.loader import render_to_string
from django.utils import timezone

from .models import User

REACTIVATE_SALT = "ultrastore-portal.reactivate-account"
REACTIVATE_MAX_AGE = 15 * 60


def refusal(user) -> str:
    """Why this account cannot be closed or deleted from its own page; ""
    when it can."""
    from ..store.models import PublisherMember

    if user.is_staff:
        return "An operator's account is removed by another operator, not from this page."
    if PublisherMember.objects.filter(user=user, role=PublisherMember.OWNER).exists():
        return "This account owns a publisher. Hand the publisher over, or ask the store to close it, before the account goes."
    return ""


def _end_everything(user) -> None:
    """Whoever held a sign-in or a service's access holds nothing any more."""
    from ..identity import provider

    provider.password_changed(user)


def _mail(user, template: str, subject: str) -> None:
    from ..identity import hosts

    body = render_to_string(f"mail/{template}", {"user": user, "brand": hosts.brand(), "erase_after": user.erase_after,
                                                  "signin": hosts.account_url("signin"), "account": hosts.account_url("profile")})
    send_mail(f"{hosts.brand()}: {subject}", body, None, [user.email])


def _affiliates():
    """The affiliate programme follows the account's states: a closed
    account's profile is closed with it, comes back with it, and is
    emptied when the account is erased."""
    from ..affiliates import services

    return services


def _audit(user, action: str, **detail) -> None:
    from ..store.services import audit

    audit(user, action, "account", str(user.pk), **detail)


@transaction.atomic
def close(user) -> User:
    """Closed: signed out everywhere and kept, to be reactivated or, after
    ACCOUNT_CLOSED_DAYS, erased."""
    user.closed_at = timezone.now()
    user.deletion_requested_at = None
    user.erase_after = timezone.localdate() + dt.timedelta(days=settings.ACCOUNT_CLOSED_DAYS)
    user.is_active = False
    user.save(update_fields=["closed_at", "deletion_requested_at", "erase_after", "is_active"])
    _end_everything(user)
    _affiliates().account_closed(user)
    _audit(user, "account.close", erase_after=user.erase_after.isoformat())
    transaction.on_commit(lambda: _mail(user, "account_closed.txt", "your account is closed"))
    return user


@transaction.atomic
def reactivate(user) -> User:
    if user.state != User.CLOSED:
        return user
    user.closed_at = None
    user.erase_after = None
    user.is_active = True
    user.save(update_fields=["closed_at", "erase_after", "is_active"])
    _affiliates().account_reactivated(user)
    _audit(user, "account.reactivate")
    return user


@transaction.atomic
def request_deletion(user) -> User:
    """Deleting: usable for the grace, then erased."""
    user.deletion_requested_at = timezone.now()
    user.erase_after = timezone.localdate() + dt.timedelta(days=settings.ACCOUNT_DELETION_GRACE_DAYS)
    user.save(update_fields=["deletion_requested_at", "erase_after"])
    _affiliates().account_closed(user)   # the customer's binding is suspended from the request on
    _audit(user, "account.delete_requested", erase_after=user.erase_after.isoformat())
    transaction.on_commit(lambda: _mail(user, "account_deletion.txt", "your account will be deleted"))
    return user


@transaction.atomic
def cancel_deletion(user) -> User:
    if user.state != User.DELETING:
        return user
    user.deletion_requested_at = None
    user.erase_after = None
    user.save(update_fields=["deletion_requested_at", "erase_after"])
    _affiliates().account_reactivated(user)
    _audit(user, "account.delete_cancelled")
    return user


@transaction.atomic
def erase(user) -> User:
    """The end of a closed account and of a deletion: nothing of the person
    is left but the row's id."""
    from ..identity.models import Consent, Grant, SignIn
    from ..store.models import AuditEntry, PublisherMember
    from ..store.services import audit

    if user.state == User.ERASED:
        return user
    was = user.state
    Grant.objects.filter(user=user).delete()      # with their codes and tokens
    SignIn.objects.filter(user=user).delete()
    Consent.objects.filter(user=user).delete()
    PublisherMember.objects.filter(user=user).delete()
    _affiliates().account_erased(user)
    AuditEntry.objects.filter(actor=user).update(actor_name=f"account {user.pk}")
    user.email = f"erased-{user.pk}@erased.invalid"
    user.name = ""
    user.email_verified = False
    user.is_active = False
    user.set_unusable_password()
    user.closed_at = None
    user.deletion_requested_at = None
    user.erase_after = None
    user.erased_at = timezone.now()
    user.save()
    audit(None, "account.erased", "account", str(user.pk), was=was)
    return user


def maintain(today: dt.date | None = None) -> dict:
    """Every account whose time is up, erased. Safe to run again."""
    today = today or timezone.localdate()
    counts = {"closed": 0, "deleting": 0}
    for user in User.objects.filter(erased_at__isnull=True, erase_after__isnull=False, erase_after__lte=today):
        counts[user.state] = counts.get(user.state, 0) + 1
        erase(user)
    return counts


def reactivation_token(user) -> str:
    return signing.dumps({"u": user.pk, "c": user.closed_at.isoformat() if user.closed_at else ""}, salt=REACTIVATE_SALT)


def from_reactivation_token(token: str) -> User | None:
    """The closed account a sign-in page handed this token for, in the last
    quarter of an hour, and closed still."""
    try:
        data = signing.loads(token, salt=REACTIVATE_SALT, max_age=REACTIVATE_MAX_AGE)
        user = User.objects.get(pk=data["u"])
    except (signing.BadSignature, User.DoesNotExist, KeyError, TypeError):
        return None
    if user.state != User.CLOSED or (user.closed_at.isoformat() if user.closed_at else "") != data.get("c"):
        return None
    return user


def closed_account(email: str, password: str) -> User | None:
    """The closed account this address and password are the right ones for."""
    user = User.objects.filter(email=(email or "").strip().lower(), closed_at__isnull=False, erased_at__isnull=True).first()
    return user if user is not None and password and user.check_password(password) else None
