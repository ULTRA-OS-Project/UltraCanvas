"""Two hosts, one service.

The account - sign-up, sign-in, the profile, and the OpenID Connect
endpoints other services sign in through - can live at an address of its own
(ULTRASTORE_PORTAL_ACCOUNT_URL, https://account.ultraos.eu), beside the
store's (BASE_URL, https://store.ultraos.eu). Both are this one Django
service: the web server sends both hosts here, and the host of a request
decides which addresses exist on it.

    one host (ACCOUNT_URL unset or the store's)   the account pages under <root>/account/,
                                                  the OpenID Connect endpoints under <root>/
    two hosts                                     the account host has the account pages at
                                                  its root and the OpenID Connect endpoints;
                                                  the store host has neither, and signs a
                                                  person in through the account host like
                                                  any other service (identity/sso.py)

Everything here is read from the settings when asked, not once at import, so
a test can run the same code as one host and as two.

Version: 0.4.0
"""

from __future__ import annotations

from functools import wraps
from urllib.parse import urlsplit

from django.conf import settings
from django.http import Http404
from django.http.request import split_domain_port
from django.shortcuts import redirect
from django.urls import reverse

ACCOUNT_URLCONF = "ultrastore_portal.urls_account"


def store_root() -> str:
    return settings.BASE_URL.rstrip("/")


def account_root() -> str:
    """The account's own root, which is also the issuer every token names."""
    return (settings.ACCOUNT_URL or settings.BASE_URL).rstrip("/")


def split() -> bool:
    """Whether the account has a host of its own."""
    return urlsplit(account_root()).netloc.lower() != urlsplit(store_root()).netloc.lower()


def _host_and_port(host: str, scheme: str) -> tuple[str, str]:
    """A host as it is compared: lower case, without the dot a full name
    may end in, and with the port its scheme has when it names none - so
    account.ultraos.eu, account.ultraos.eu:443 and account.ultraos.eu. are
    one host, as they are to the web server."""
    domain, port = split_domain_port(host.lower())
    return domain.rstrip("."), port or ("443" if scheme == "https" else "80")


def on_account_host(request) -> bool:
    """Whether this request is one the account answers: always with one
    host, and with two only when it came to the account's."""
    if not split():
        return True
    account = urlsplit(account_root())
    return _host_and_port(request.get_host(), account.scheme) == _host_and_port(account.netloc, account.scheme)


def account_url(name: str, *args, **kwargs) -> str:
    """The whole address of an account page ("signin", "verify") or of an
    OpenID Connect endpoint ("oidc:token"), for a mail or a link from the
    other host."""
    view = name if ":" in name else "account:" + name
    urlconf = ACCOUNT_URLCONF if split() else settings.ROOT_URLCONF
    return account_root() + reverse(view, args=args, kwargs=kwargs, urlconf=urlconf)


def store_url(name: str, *args, **kwargs) -> str:
    """The whole address of one of the store's pages ("console:home"); with
    one host just its path, as before."""
    path = reverse(name, args=args, kwargs=kwargs, urlconf=settings.ROOT_URLCONF)
    return store_root() + path if split() else path


def brand() -> str:
    """What the account calls itself: the store's with one host, its own
    name once it serves more than the store."""
    return settings.ACCOUNT_NAME if split() else "UltraStore"


class AccountHostMiddleware:
    """A request to the account's host is answered from the account's
    addresses (urls_account.py) and from nothing else."""

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        if split() and on_account_host(request):
            request.urlconf = ACCOUNT_URLCONF
        return self.get_response(request)


def account_page(view, on_store=None):
    """An account page. With two hosts it exists on the account's only: on
    the store's host a GET is sent across to it, and anything else is not
    found - unless `on_store` says what the store does there instead (its
    own sign-in and sign-out)."""
    @wraps(view)
    def wrapper(request, *args, **kwargs):
        if not on_account_host(request):
            if on_store is not None:
                return on_store(request)
            if request.method not in ("GET", "HEAD"):
                raise Http404
            target = account_root() + reverse(request.resolver_match.view_name, args=args, kwargs=kwargs, urlconf=ACCOUNT_URLCONF)
            query = request.META.get("QUERY_STRING", "")
            return redirect(target + ("?" + query if query else ""))
        return view(request, *args, **kwargs)
    return wrapper


def issuer_only(view):
    """An OpenID Connect endpoint: it exists where the issuer is."""
    @wraps(view)
    def wrapper(request, *args, **kwargs):
        if not on_account_host(request):
            raise Http404
        return view(request, *args, **kwargs)
    return wrapper
