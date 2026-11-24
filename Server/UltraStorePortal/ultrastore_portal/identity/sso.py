"""The store signs a person in through the account.

With two hosts the store's host has no password page: its *Sign in* sends
the person to the account, which sends them back with a code, exactly as
it would for any other service. The store is the account's first client
("ultrastore", one of ours: no consent page), and because both run in this
one process the code is exchanged by a call instead of a request - with the
same checks, the code verifier included.

    /account/signin/?next=    on the store's host: to the account, and back
    /sso/callback             the code for the store's own session
    /account/signout/         the store's session ended, then the account's

With one host none of this is used: the account's session is the store's.

Version: 0.4.0
"""

from __future__ import annotations

import base64
import hashlib
import secrets
from urllib.parse import urlencode

from django.conf import settings
from django.contrib import messages
from django.contrib.auth import login, logout
from django.http import Http404, HttpResponseRedirect
from django.shortcuts import redirect, resolve_url
from django.urls import reverse
from django.utils.http import url_has_allowed_host_and_scheme
from django.views.decorators.cache import never_cache
from django.views.decorators.http import require_POST

from . import hosts, provider, sessions
from .models import Client

CLIENT_ID = "ultrastore"
BACKEND = "django.contrib.auth.backends.ModelBackend"
PENDING, ID_TOKEN = "sso_pending", "sso_id_token"


def callback_uri() -> str:
    return hosts.store_root() + reverse("sso_callback", urlconf=settings.ROOT_URLCONF)


def client() -> Client:
    """The store as the account's client, registered by itself and kept in
    step with where the store is."""
    wanted = {"redirect_uris": callback_uri(), "post_logout_redirect_uris": hosts.store_root() + "/"}
    row, _ = Client.objects.get_or_create(client_id=CLIENT_ID, defaults={
        "name": "UltraStore", "kind": Client.PUBLIC, "trusted": True, "scopes": "openid profile email", **wanted})
    if any(getattr(row, name) != value for name, value in wanted.items()):
        for name, value in wanted.items():
            setattr(row, name, value)
        row.save(update_fields=list(wanted))
    return row


def _next(request) -> str:
    target = request.GET.get("next", "")
    if target and url_has_allowed_host_and_scheme(target, allowed_hosts={request.get_host()}, require_https=request.is_secure()):
        return target
    return resolve_url(settings.LOGIN_REDIRECT_URL)


@never_cache
def start(request):
    """Sign in on the store's host: off to the account."""
    target = _next(request)
    if request.user.is_authenticated:
        return redirect(target)
    state, verifier, nonce = secrets.token_urlsafe(24), secrets.token_urlsafe(48), secrets.token_urlsafe(24)
    request.session[PENDING] = {"state": state, "verifier": verifier, "nonce": nonce, "next": target}
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).decode().rstrip("=")
    query = {"response_type": "code", "client_id": client().client_id, "redirect_uri": callback_uri(), "scope": "openid profile email",
             "state": state, "nonce": nonce, "code_challenge": challenge, "code_challenge_method": "S256"}
    return HttpResponseRedirect(hosts.account_url("oidc:authorize") + "?" + urlencode(query))


@never_cache
def callback(request):
    """Back from the account with the code."""
    if not hosts.split() or hosts.on_account_host(request):
        raise Http404
    pending = request.session.pop(PENDING, None)
    state = request.GET.get("state", "")
    if not pending or not state or not secrets.compare_digest(state, pending["state"]):
        messages.error(request, "That sign-in was not started here, or it was started twice. Try again.")
        return redirect("store:home")
    if request.GET.get("iss") != provider.issuer() or request.GET.get("error"):
        messages.error(request, "The account did not sign you in. Try again.")
        return redirect("store:home")
    try:
        grant = provider.redeem_code(client(), request.GET.get("code", ""), callback_uri(), pending["verifier"])
    except provider.OAuthError:
        messages.error(request, "The sign-in ran out before it was finished. Try again.")
        return redirect("store:home")
    if grant.nonce != pending["nonce"]:
        grant.revoke()
        messages.error(request, "The account answered another request than the one sent. Try again.")
        return redirect("store:home")
    login(request, grant.user, backend=BACKEND)
    request.session[sessions.SID] = grant.sid
    request.session[ID_TOKEN] = provider.id_token(grant)
    return redirect(pending["next"])


@never_cache
@require_POST
def signout(request):
    """Sign out on the store's host: here first, then at the account, which
    is told which sign-in is meant by the token it issued for it."""
    hint = request.session.get(ID_TOKEN, "")
    logout(request)
    query = {"client_id": CLIENT_ID, "post_logout_redirect_uri": hosts.store_root() + "/"}
    if hint:
        query["id_token_hint"] = hint
    return HttpResponseRedirect(hosts.account_url("oidc:logout") + "?" + urlencode(query))
