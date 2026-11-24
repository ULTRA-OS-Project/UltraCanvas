"""A sign-in at the account, as a record.

Signing in at the account starts a SignIn; signing out there ends it. A
service that signs a person in through the account is told the sign-in's id
(the `sid` claim), and the store - which with two hosts is such a service
itself (sso.py) - checks on every request that the sign-in its session came
from has not ended: signing out at the account signs the store out too.

Version: 0.4.0
"""

from __future__ import annotations

import time
from datetime import datetime, timezone as utc

from django.contrib.auth import logout
from django.contrib.auth.signals import user_logged_in, user_logged_out
from django.dispatch import receiver
from django.utils import timezone

from . import hosts
from .models import SignIn

SID, AUTH_TIME = "sso_sid", "auth_time"


def _at_the_account(request) -> bool:
    try:
        return request is not None and hosts.on_account_host(request)
    except KeyError:   # a request made up without a host (a test's login())
        return False


@receiver(user_logged_in)
def started(sender, request, user, **kwargs) -> None:
    """A sign-in at the account. The same person giving the password again
    in a session that is signed in (a service asked for it) is the same
    sign-in, a moment later - not a second one, which would leave the first
    without an end and everything hanging on it signed in for good."""
    if not _at_the_account(request):
        return
    sid = request.session.get(SID)
    if not (sid and SignIn.objects.filter(sid=sid, user=user, ended__isnull=True).exists()):
        request.session[SID] = SignIn.objects.create(user=user).sid
    request.session[AUTH_TIME] = time.time()   # to the fraction: "after the account asked" is decided by it


def end(sid: str) -> None:
    if sid:
        SignIn.objects.filter(sid=sid, ended__isnull=True).update(ended=timezone.now())


@receiver(user_logged_out)
def ended(sender, request, user, **kwargs) -> None:
    if _at_the_account(request):
        end(request.session.get(SID, ""))


def current(request) -> tuple[str, datetime]:
    """(sid, when the person signed in) of this session at the account. A
    session older than this record gets one now, dated by the last sign-in."""
    sid, auth_time = request.session.get(SID), request.session.get(AUTH_TIME)
    if not sid or not auth_time or not SignIn.objects.filter(sid=sid, user=request.user, ended__isnull=True).exists():
        last = request.user.last_login or timezone.now()
        sid = SignIn.objects.create(user=request.user, created=last).sid
        auth_time = last.timestamp()
        request.session[SID], request.session[AUTH_TIME] = sid, auth_time
    return sid, datetime.fromtimestamp(auth_time, utc.utc)


class SignInMiddleware:
    """On the store's host, with two hosts: a session that came from a
    sign-in at the account ends when that sign-in does."""

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        if hosts.split() and not hosts.on_account_host(request) and request.user.is_authenticated:
            sid = request.session.get(SID)
            if sid:
                sign_in = SignIn.objects.filter(sid=sid).first()
                if sign_in is None or sign_in.over:
                    logout(request)
        return self.get_response(request)
