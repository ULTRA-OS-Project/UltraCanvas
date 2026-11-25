"""Small things the pages share.

Version: 0.1.0
"""

from __future__ import annotations

from functools import wraps

from django.contrib.auth.decorators import login_required
from django.core.cache import cache
from django.core.exceptions import PermissionDenied
from django.shortcuts import redirect

from ..store import services
from ..store.models import PublisherMember


def client_address(request) -> str:
    """Who is asking: the address the web server in front of the portal saw
    (it appends it to X-Forwarded-For), else the connection's."""
    remote = request.META.get("REMOTE_ADDR", "")
    forwarded = request.META.get("HTTP_X_FORWARDED_FOR", "")
    if forwarded and remote in ("127.0.0.1", "::1", ""):
        return forwarded.split(",")[-1].strip()
    return remote


def too_many(request, what: str, limit: int, seconds: int, key: str = "") -> bool:
    """Whether this address did `what` more than `limit` times in the
    window. Counted per process, which is enough to slow a guesser down."""
    name = f"throttle:{what}:{client_address(request)}:{key}"
    if cache.add(name, 1, seconds):
        return False
    try:
        return cache.incr(name) > limit
    except ValueError:
        return False


def publisher_required(view=None, *, approved: bool = True, roles: tuple | None = PublisherMember.APPS):
    """The console's pages: a signed-in person with a publisher, approved
    unless the page says otherwise; request.publisher is theirs and
    request.publisher_role what they are there. The apps, their listings
    and releases are an owner's and a developer's; a page for other roles
    names them (`roles`; None: every member)."""
    def decorate(inner):
        @wraps(inner)
        @login_required
        def wrapper(request, *args, **kwargs):
            membership = services.membership_of(request.user)
            if membership is None:
                return redirect("console:apply")
            publisher = membership.publisher
            if approved and not publisher.approved:
                return redirect("console:home")
            if roles is not None and membership.role not in roles:
                raise PermissionDenied
            request.publisher, request.publisher_role = publisher, membership.role
            return inner(request, *args, **kwargs)
        return wrapper
    return decorate(view) if view else decorate


def operator_required(view):
    """The operator's pages: staff only."""
    @wraps(view)
    @login_required
    def wrapper(request, *args, **kwargs):
        if not request.user.is_staff:
            raise PermissionDenied
        return view(request, *args, **kwargs)
    return wrapper
