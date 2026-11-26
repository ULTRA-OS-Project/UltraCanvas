"""OpenID Connect, the provider's side: what a request may ask, what a code
and a token are worth. The pages and the JSON around it are views.py; this
module answers without knowing about either.

One flow and no other: the authorization code, with PKCE (S256). No implicit
or hybrid flow, no password grant, no request objects.

    read_request     an authorization request, checked
    authorize        the person agreed: a grant and its code
    redeem_code      the code, once, for its grant
    issue_tokens     the access token, the identity token, the refresh token
    refresh          a refresh token for the next set, the old one spent
    userinfo, introspect, revoke
    forget           a person takes one service's access back
    password_changed every grant and every other sign-in ends

What is refused, and how:

    Refused         the client or where to send the person back is not known:
                    the person is told, and sent nowhere
    RedirectError   anything else about an authorization request: the
                    service is told, at its own address
    OAuthError      the token endpoint and its neighbours: a JSON error

Version: 0.4.0
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import re
import secrets
import time
from dataclasses import dataclass, field
from datetime import timedelta
from urllib.parse import unquote_plus

from django.conf import settings
from django.utils import timezone

from . import hosts, keys
from .models import Client, Code, Consent, Grant, SignIn, Token, digest, new_secret

PKCE_VALUE = re.compile(r"[A-Za-z0-9._~-]{43,128}")   # used with fullmatch: no trailing newline slips through
PROMPTS = {"none", "login", "consent", "select_account"}


class Refused(Exception):
    """Nothing can be sent back: the message is for the person."""


class RedirectError(Exception):
    def __init__(self, redirect_uri: str, error: str, description: str = "", state: str = "") -> None:
        super().__init__(description or error)
        self.redirect_uri, self.error, self.description, self.state = redirect_uri, error, description, state


class OAuthError(Exception):
    def __init__(self, error: str, description: str = "", status: int = 400) -> None:
        super().__init__(description or error)
        self.error, self.description, self.status = error, description, status


@dataclass
class AuthRequest:
    client: Client
    redirect_uri: str
    scope: list[str]
    state: str = ""
    nonce: str = ""
    code_challenge: str = ""
    prompt: set[str] = field(default_factory=set)
    max_age: int | None = None
    login_hint: str = ""

    def fail(self, error: str, description: str = "") -> RedirectError:
        return RedirectError(self.redirect_uri, error, description, self.state)


def issuer() -> str:
    return hosts.account_root()


# -- the authorization request ------------------------------------------------

def read_request(params) -> AuthRequest:
    if any("\x00" in str(value) for value in params.values()):
        raise Refused("This request is not one a service would send.")
    client = Client.objects.filter(client_id=params.get("client_id", ""), active=True).first()
    if client is None:
        raise Refused("This service is not known to the account.")
    redirect_uri = params.get("redirect_uri", "")
    if not redirect_uri or not client.allows_redirect(redirect_uri):
        raise Refused("The address this service asked to return to is not one it registered.")
    request = AuthRequest(client=client, redirect_uri=redirect_uri, scope=[], state=params.get("state", ""))

    if params.get("request"):
        raise request.fail("request_not_supported")
    if params.get("request_uri"):
        raise request.fail("request_uri_not_supported")
    if params.get("response_type") != "code":
        raise request.fail("unsupported_response_type", "The account answers with a code and nothing else (response_type=code).")
    if params.get("response_mode", "query") != "query":
        raise request.fail("invalid_request", "The code comes back in the query (response_mode=query).")

    asked = params.get("scope", "").split()
    if "openid" not in asked:
        raise request.fail("invalid_scope", "The scope must name openid.")
    allowed = client.allowed_scopes()
    request.scope = [scope for scope in dict.fromkeys(asked) if scope in allowed]   # what it may not have is left out

    challenge = params.get("code_challenge", "")
    if challenge:
        if params.get("code_challenge_method") != "S256":
            raise request.fail("invalid_request", "The code challenge method is S256.")
        if not PKCE_VALUE.fullmatch(challenge):
            raise request.fail("invalid_request", "The code challenge is 43 to 128 characters of base64url.")
        request.code_challenge = challenge
    elif client.require_pkce or client.kind == Client.PUBLIC:
        raise request.fail("invalid_request", "This client must send a code challenge (PKCE, S256).")

    request.nonce = params.get("nonce", "")
    if len(request.nonce) > 255:
        raise request.fail("invalid_request", "The nonce is at most 255 characters.")
    request.prompt = set(params.get("prompt", "").split())
    if request.prompt - PROMPTS or ("none" in request.prompt and len(request.prompt) > 1):
        raise request.fail("invalid_request", "prompt is none, or any of login, consent, select_account.")
    if params.get("max_age", "") != "":
        try:
            request.max_age = int(params["max_age"])
            if request.max_age < 0:
                raise ValueError
        except ValueError:
            raise request.fail("invalid_request", "max_age is a number of seconds.") from None
    request.login_hint = params.get("login_hint", "")[:254]
    return request


def authorize(request: AuthRequest, user, sid: str, auth_time) -> str:
    """The person agreed (or need not be asked): a grant, and the code the
    service exchanges for its tokens."""
    grant = Grant.objects.create(user=user, client=request.client, scope=" ".join(request.scope), nonce=request.nonce,
                                 sid=sid, auth_time=auth_time)
    code = new_secret()
    Code.objects.create(grant=grant, code_hash=digest(code), redirect_uri=request.redirect_uri, code_challenge=request.code_challenge,
                        expires=timezone.now() + timedelta(seconds=settings.OIDC_CODE_TTL))
    return code


# -- the client at the token endpoint ----------------------------------------

def presented_client_id(authorization: str, form) -> str:
    """The client a request names, proven or not: what guessing is counted by."""
    if authorization.lower().startswith("basic "):
        try:
            return unquote_plus(base64.b64decode(authorization[6:].strip(), validate=True).decode().partition(":")[0])[:64]
        except ValueError:
            return ""
    return str(form.get("client_id", ""))[:64]


def authenticate_client(authorization: str, form) -> Client:
    """Who is asking: the secret in the Authorization header or in the
    form, never both; a public client names itself and proves nothing here
    (its proof is the code verifier)."""
    if any("\x00" in str(value) for value in form.values()):
        raise OAuthError("invalid_request", "A value holds a character no value has.")
    client_id, secret, basic = form.get("client_id", ""), form.get("client_secret"), False
    if authorization.lower().startswith("basic "):
        if secret is not None:
            raise OAuthError("invalid_request", "The client's secret is sent once: in the header or in the form.")
        try:
            user, _, password = base64.b64decode(authorization[6:].strip(), validate=True).decode().partition(":")
        except ValueError:   # not base64, not ASCII, not UTF-8
            raise OAuthError("invalid_client", "The Authorization header is not Basic credentials.", 401) from None
        if client_id and client_id != unquote_plus(user):
            raise OAuthError("invalid_request", "Two different clients are named.")
        client_id, secret, basic = unquote_plus(user), unquote_plus(password), True
    client = Client.objects.filter(client_id=client_id, active=True).first()
    status = 401 if basic else 400
    if client is None:
        raise OAuthError("invalid_client", "Unknown client.", status)
    if client.kind == Client.CONFIDENTIAL and not client.check_secret(secret or ""):
        raise OAuthError("invalid_client", "The client's secret is wrong or missing.", 401)
    return client


# -- codes and tokens ----------------------------------------------------------

def _pkce_matches(challenge: str, verifier: str) -> bool:
    if not PKCE_VALUE.fullmatch(verifier or ""):
        return False
    made = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).decode().rstrip("=")
    return secrets.compare_digest(made, challenge)


def redeem_code(client: Client, code: str, redirect_uri: str, verifier: str) -> Grant:
    """The grant a code stands for. A code is good once: shown a second time
    it takes back everything the first exchange was given, because one of
    the two who showed it should not have had it."""
    invalid = OAuthError("invalid_grant", "The code is wrong, was used, ran out, or was made for another request.")
    row = Code.objects.select_related("grant__client", "grant__user").filter(code_hash=digest(code or "")).first()
    if row is None or row.grant.client_id != client.pk:
        raise invalid
    grant = row.grant
    if Code.objects.filter(pk=row.pk, used__isnull=True).update(used=timezone.now()) != 1:
        grant.revoke()
        raise invalid
    if row.expires < timezone.now() or grant.revoked is not None or not grant.user.is_active:
        raise invalid
    if redirect_uri != row.redirect_uri:
        raise invalid
    if row.code_challenge and not _pkce_matches(row.code_challenge, verifier):
        raise invalid
    if verifier and not row.code_challenge:
        # A verifier for a code that was asked for without a challenge: the
        # challenge was taken out of the request on its way here.
        raise invalid
    return grant


def subject(user, client: Client) -> str:
    """Who the person is to this service: the same value every time, and a
    different one for every service that is not ours, so that two of them
    cannot tell they know the same person. Never the account's own number."""
    message = f"{client.subject_sector()}|{user.pk}".encode()
    return keys.b64(hmac.new(keys.subject_secret(), message, hashlib.sha256).digest())


def claims_for(user, scopes) -> dict:
    claims = {}
    if "profile" in scopes:
        claims["name"] = user.display_name
    if "email" in scopes:
        claims["email"] = user.email
        claims["email_verified"] = bool(user.email_verified)
    return claims


def id_token(grant: Grant, access_token: str = "", scope: str | None = None, with_nonce: bool = True) -> str:
    now = int(time.time())
    scopes = (grant.scope if scope is None else scope).split()
    claims = {
        "iss": issuer(), "sub": subject(grant.user, grant.client), "aud": grant.client.client_id,
        "iat": now, "exp": now + settings.OIDC_ID_TOKEN_TTL, "auth_time": int(grant.auth_time.timestamp()),
    }
    if with_nonce and grant.nonce:
        claims["nonce"] = grant.nonce
    if grant.sid:
        claims["sid"] = grant.sid
    if access_token:
        half = hashlib.sha256(access_token.encode()).digest()[:16]
        claims["at_hash"] = keys.b64(half)
    claims.update(claims_for(grant.user, scopes))
    return keys.sign(claims)


def _new_token(grant: Grant, kind: str, scope: str, seconds: int) -> str:
    value = new_secret()
    Token.objects.create(grant=grant, kind=kind, token_hash=digest(value), scope=scope, expires=timezone.now() + timedelta(seconds=seconds))
    return value


def issue_tokens(grant: Grant, scope: str | None = None, with_nonce: bool = True) -> dict:
    """The tokens of a grant. `scope` is what this access token is for - a
    refresh may ask for less than was granted - while the refresh token
    always stands for the whole grant, so asking for less once does not
    lose the rest (RFC 6749, 6)."""
    scope = grant.scope if scope is None else scope
    access = _new_token(grant, Token.ACCESS, scope, settings.OIDC_ACCESS_TOKEN_TTL)
    answer = {"access_token": access, "token_type": "Bearer", "expires_in": settings.OIDC_ACCESS_TOKEN_TTL, "scope": scope}
    if "openid" in scope.split():
        answer["id_token"] = id_token(grant, access, scope, with_nonce)
    if "offline_access" in grant.scope.split():
        answer["refresh_token"] = _new_token(grant, Token.REFRESH, grant.scope, settings.OIDC_REFRESH_TOKEN_TTL)
    return answer


def refresh(client: Client, token: str, scope: str = "") -> dict:
    """The next set of tokens for a refresh token, which is spent by it. One
    that was already spent and is shown again was copied: the whole grant
    goes, and both holders have to sign in anew."""
    invalid = OAuthError("invalid_grant", "The refresh token is wrong, was used, ran out, or was taken back.")
    row = Token.objects.select_related("grant__client", "grant__user").filter(token_hash=digest(token or ""), kind=Token.REFRESH).first()
    if row is None or row.grant.client_id != client.pk:
        raise invalid
    grant = row.grant
    if row.used is not None:
        grant.revoke()
        raise invalid
    if not row.live or not grant.user.is_active:
        raise invalid
    wanted = scope.split() if scope else row.scope.split()
    if not set(wanted) <= set(row.scope.split()):
        raise OAuthError("invalid_scope", "A refresh cannot ask for more than was granted.")
    if Token.objects.filter(pk=row.pk, used__isnull=True).update(used=timezone.now()) != 1:
        grant.revoke()
        raise invalid
    return issue_tokens(grant, " ".join(wanted), with_nonce=False)


def find_token(value: str, kind: str | None = None) -> Token | None:
    rows = Token.objects.select_related("grant__client", "grant__user").filter(token_hash=digest(value or ""))
    if kind:
        rows = rows.filter(kind=kind)
    return rows.first()


def userinfo(access_token: str) -> dict:
    row = find_token(access_token, Token.ACCESS)
    if row is None or not row.live or not row.grant.user.is_active or "openid" not in row.scope.split():
        raise OAuthError("invalid_token", "The access token is wrong, ran out, or was taken back.", 401)
    return {"sub": subject(row.grant.user, row.grant.client), **claims_for(row.grant.user, row.scope.split())}


def person_of(access_token: str):
    """The person an access token speaks for, or None: for the portal's own
    JSON service, which takes the account's token to know who is buying."""
    row = find_token(access_token, Token.ACCESS) if access_token else None
    if row is None or not row.live or not row.grant.user.is_active:
        return None
    return row.grant.user


def introspect(client: Client, value: str) -> dict:
    """Whether a token is good, for a service that is handed other
    services' tokens (RFC 7662). Only a client marked as such may ask."""
    if not client.resource_server or client.kind != Client.CONFIDENTIAL:
        raise OAuthError("unauthorized_client", "This client may not ask about tokens.", 403)
    row = find_token(value)
    if row is None or not row.live or not row.grant.user.is_active:
        return {"active": False}
    grant = row.grant
    return {
        "active": True, "scope": row.scope, "client_id": grant.client.client_id,
        "sub": subject(grant.user, grant.client), "iss": issuer(), "iat": int(row.created.timestamp()), "exp": int(row.expires.timestamp()),
        **({"token_type": "Bearer"} if row.kind == Token.ACCESS else {}), **({"sid": grant.sid} if grant.sid else {}),
    }


def revoke(client: Client, value: str) -> None:
    """A token given back (RFC 7009). A refresh token takes its grant with
    it; an access token goes alone. A token that is not this client's, or
    not a token, is answered the same way."""
    row = find_token(value)
    if row is None or row.grant.client_id != client.pk:
        return
    if row.kind == Token.REFRESH:
        row.grant.revoke()
    elif row.revoked is None:
        row.revoked = timezone.now()
        row.save(update_fields=["revoked"])


def forget(user, client: Client) -> None:
    """The person takes a service's access back: the consent and every
    grant, with all their tokens."""
    client.consents.filter(user=user).delete()
    Grant.objects.filter(user=user, client=client, revoked__isnull=True).update(revoked=timezone.now())


def services_of(user) -> list[dict]:
    """The services that hold something of this person: the ones they
    allowed on a consent page, and ours that still hold a refresh token -
    which were never asked for, and must be as easy to sign out of."""
    found: dict[int, dict] = {}
    for consent in Consent.objects.filter(user=user, client__active=True).select_related("client"):
        found[consent.client_id] = {"client": consent.client, "told": consent.scope_labels, "since": consent.granted}
    held = Token.objects.filter(grant__user=user, grant__revoked__isnull=True, grant__client__active=True, kind=Token.REFRESH,
                                used__isnull=True, revoked__isnull=True, expires__gt=timezone.now()).select_related("grant__client")
    for token in held:
        grant = token.grant
        found.setdefault(grant.client_id, {"client": grant.client, "told": ["Stays signed in while you are away"], "since": grant.created})
    return sorted(found.values(), key=lambda row: row["client"].name.lower())


def password_changed(user, keep_sid: str = "") -> None:
    """A new password: whoever held the old one holds nothing any more.
    Every grant goes with its tokens, and every sign-in but the one the
    password was changed in ends - which signs the store out where it
    followed one of them."""
    now = timezone.now()
    Grant.objects.filter(user=user, revoked__isnull=True).update(revoked=now)
    SignIn.objects.filter(user=user, ended__isnull=True).exclude(sid=keep_sid).update(ended=now)


def cleanup() -> dict:
    """What has run out and is of no more use, removed."""
    now = timezone.now()
    tokens, _ = Token.objects.filter(expires__lt=now - timedelta(days=1)).delete()
    grants, _ = Grant.objects.filter(created__lt=now - timedelta(days=1), tokens__isnull=True).filter(code__expires__lt=now).delete()
    return {"tokens": tokens, "grants": grants}
