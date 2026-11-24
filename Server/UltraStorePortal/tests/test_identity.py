"""The account as an OpenID Connect provider, end to end through its
endpoints: a service sends a person to the account, gets a code back,
exchanges it, and learns who signed in - on one host, and with the account
at an address of its own, where the store itself signs in that way."""

from __future__ import annotations

import base64
import hashlib
import io
import json
import time
from urllib.parse import parse_qs, urlencode, urlsplit

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.asymmetric.rsa import RSAPublicNumbers
from django.contrib.auth import get_user_model
from django.core import mail
from django.core.cache import cache
from django.core.management import call_command
from django.test import Client as Browser
from django.test import TestCase, override_settings
from django.utils import timezone

from ultrastore_portal.identity import keys, provider
from ultrastore_portal.identity.models import Client, Code, Consent, Grant, SignIn, Token
from ultrastore_portal.store.models import AuditEntry, TermsVersion

User = get_user_model()
PASSWORD = "a-long-enough-passphrase"
STORE = "https://store.ultraos.eu"
ACCOUNT = "https://account.ultraos.eu"
CALLBACK = "https://cloud.example/callback"
VERIFIER = "v" * 64


def challenge(verifier: str = VERIFIER) -> str:
    return base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).decode().rstrip("=")


def unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def query_of(response) -> dict:
    return {key: values[0] for key, values in parse_qs(urlsplit(response["Location"]).query).items()}


def basic(client_id: str, secret: str) -> dict:
    return {"Authorization": "Basic " + base64.b64encode(f"{client_id}:{secret}".encode()).decode()}


class IdentityCase(TestCase):
    """One host: the issuer is the store's root, the account's pages are
    under /account/."""

    def setUp(self):
        override = override_settings(BASE_URL=STORE, ACCOUNT_URL="", OIDC_KEY_FILE="", OIDC_SUBJECT_SECRET="")
        override.enable()
        self.addCleanup(override.disable)
        cache.clear()
        self.user = User.objects.create_user("dana@example.org", PASSWORD, name="Dana", email_verified=True)
        self.cloud, self.secret = self.make_client("ultracloud-test", "Some Cloud")

    # -- helpers ---------------------------------------------------------
    def make_client(self, client_id, name, **fields):
        fields.setdefault("redirect_uris", CALLBACK)
        client = Client(client_id=client_id, name=name, **fields)
        secret = client.new_secret() if client.kind == Client.CONFIDENTIAL else ""
        client.full_clean()
        client.save()
        return client, secret

    def request(self, client=None, **extra) -> dict:
        client = client or self.cloud
        params = {"response_type": "code", "client_id": client.client_id, "redirect_uri": CALLBACK, "scope": "openid profile email",
                  "state": "st-1", "nonce": "n-1", "code_challenge": challenge(), "code_challenge_method": "S256"}
        params.update(extra)
        return {key: value for key, value in params.items() if value is not None}

    def sign_in(self):
        self.assertTrue(self.client.login(email=self.user.email, password=PASSWORD))

    def code_for(self, client=None, **extra) -> str:
        """A code, the person signed in and agreeing where they are asked."""
        response = self.client.get("/oauth/authorize", self.request(client, **extra))
        if response.status_code == 200:
            response = self.client.post("/oauth/authorize/decide", {"request": response.context["key"], "decision": "allow"})
        self.assertEqual(response.status_code, 302, response.content[:300])
        answer = query_of(response)
        self.assertNotIn("error", answer, answer)
        return answer["code"]

    def exchange(self, code, client=None, secret=None, **extra):
        client = client or self.cloud
        data = {"grant_type": "authorization_code", "code": code, "redirect_uri": CALLBACK, "code_verifier": VERIFIER, **extra}
        headers = {}
        if client.kind == Client.CONFIDENTIAL:
            headers = basic(client.client_id, self.secret if secret is None else secret)
        else:
            data["client_id"] = client.client_id
        return self.client.post("/oauth/token", data, headers=headers)

    def tokens(self, client=None, **extra) -> dict:
        response = self.exchange(self.code_for(client, **extra), client)
        self.assertEqual(response.status_code, 200, response.content)
        return response.json()

    def claims(self, id_token: str) -> dict:
        """The token's claims, its signature checked as a service would:
        with the key the account publishes under the token's kid."""
        header_b64, claims_b64, signature_b64 = id_token.split(".")
        header = json.loads(unb64(header_b64))
        self.assertEqual(header["alg"], "RS256")
        jwk = {key["kid"]: key for key in self.client.get("/oauth/jwks").json()["keys"]}[header["kid"]]
        public = RSAPublicNumbers(int.from_bytes(unb64(jwk["e"]), "big"), int.from_bytes(unb64(jwk["n"]), "big")).public_key()
        public.verify(unb64(signature_b64), f"{header_b64}.{claims_b64}".encode(), padding.PKCS1v15(), hashes.SHA256())
        return json.loads(unb64(claims_b64))

    # -- the documents ---------------------------------------------------
    def test_the_discovery_document_names_the_issuer_and_its_endpoints(self):
        response = self.client.get("/.well-known/openid-configuration")
        document = response.json()
        self.assertEqual(document["issuer"], STORE)
        self.assertEqual(document["authorization_endpoint"], STORE + "/oauth/authorize")
        self.assertEqual(document["token_endpoint"], STORE + "/oauth/token")
        self.assertEqual(document["jwks_uri"], STORE + "/oauth/jwks")
        self.assertEqual(document["end_session_endpoint"], STORE + "/oauth/logout")
        self.assertEqual(document["response_types_supported"], ["code"])
        self.assertEqual(document["code_challenge_methods_supported"], ["S256"])
        self.assertEqual(response["Access-Control-Allow-Origin"], "*")
        keys_ = self.client.get("/oauth/jwks").json()["keys"]
        self.assertEqual([(key["kty"], key["use"], key["alg"]) for key in keys_], [("RSA", "sig", "RS256")])
        self.assertNotIn("d", keys_[0])   # the public half and nothing else

    # -- the flow --------------------------------------------------------
    def test_a_service_signs_a_person_in(self):
        # Not signed in: to the account's sign-in page, and from there back to the request.
        response = self.client.get("/oauth/authorize", self.request(login_hint="dana@example.org"))
        self.assertEqual(response.status_code, 302)
        self.assertTrue(response["Location"].startswith("/account/signin/?next="))
        page = self.client.get(response["Location"])
        self.assertContains(page, 'value="dana@example.org"')   # the hint, filled in
        back = self.client.post(response["Location"], {"username": "dana@example.org", "password": PASSWORD})
        self.assertTrue(back["Location"].startswith("/oauth/authorize?"))
        # A service that is not ours: the person is asked, and told what it learns.
        consent = self.client.get(back["Location"])
        self.assertContains(consent, "Sign in to Some Cloud?")
        self.assertContains(consent, "cloud.example")
        self.assertContains(consent, "Your email address")
        self.assertEqual(Grant.objects.count(), 0)
        response = self.client.post("/oauth/authorize/decide", {"request": consent.context["key"], "decision": "allow"})
        answer = query_of(response)
        self.assertTrue(response["Location"].startswith(CALLBACK + "?"))
        self.assertEqual((answer["state"], answer["iss"]), ("st-1", STORE))
        self.assertIn("no-store", response["Cache-Control"])

        # The code for the tokens, the service naming itself with its secret.
        response = self.exchange(answer["code"])
        self.assertEqual(response.status_code, 200, response.content)
        self.assertEqual(response["Cache-Control"], "no-store")
        tokens = response.json()
        self.assertEqual((tokens["token_type"], tokens["expires_in"], tokens["scope"]), ("Bearer", 3600, "openid profile email"))
        self.assertNotIn("refresh_token", tokens)   # not asked for
        claims = self.claims(tokens["id_token"])
        self.assertEqual((claims["iss"], claims["aud"], claims["nonce"]), (STORE, "ultracloud-test", "n-1"))
        self.assertEqual((claims["name"], claims["email"], claims["email_verified"]), ("Dana", "dana@example.org", True))
        self.assertLessEqual(abs(claims["iat"] - time.time()), 5)
        self.assertEqual(claims["exp"] - claims["iat"], 3600)
        self.assertLessEqual(abs(claims["auth_time"] - time.time()), 5)
        self.assertEqual(claims["sid"], SignIn.objects.get().sid)
        half = hashlib.sha256(tokens["access_token"].encode()).digest()[:16]
        self.assertEqual(claims["at_hash"], base64.urlsafe_b64encode(half).decode().rstrip("="))
        self.assertNotIn(str(self.user.pk), (claims["sub"],))   # never the account's own number

        # The same person, asked for with the access token.
        info = self.client.get("/oauth/userinfo", headers={"Authorization": "Bearer " + tokens["access_token"]})
        self.assertEqual(info.json(), {"sub": claims["sub"], "name": "Dana", "email": "dana@example.org", "email_verified": True})
        refused = self.client.get("/oauth/userinfo", headers={"Authorization": "Bearer nonsense"})
        self.assertEqual((refused.status_code, refused.json()["error"]), (401, "invalid_token"))
        self.assertIn("Bearer", refused["WWW-Authenticate"])

        # Nothing that was handed out is in the database as it was handed out.
        stored = set(Code.objects.values_list("code_hash", flat=True)) | set(Token.objects.values_list("token_hash", flat=True))
        self.assertFalse({answer["code"], tokens["access_token"]} & stored)
        self.assertNotIn(self.secret, Client.objects.get(pk=self.cloud.pk).secret_hash)

    def test_consent_is_asked_once_and_only_for_what_is_new(self):
        self.sign_in()
        self.code_for(scope="openid email")
        self.assertEqual(Consent.objects.get().scope, "email openid")
        # The same again: no page.
        self.assertEqual(self.client.get("/oauth/authorize", self.request(scope="openid email")).status_code, 302)
        # More than was allowed: asked again; prompt=none is told so instead of being shown a page.
        self.assertEqual(self.client.get("/oauth/authorize", self.request()).status_code, 200)
        silent = self.client.get("/oauth/authorize", self.request(prompt="none"))
        self.assertEqual(query_of(silent)["error"], "consent_required")
        # prompt=consent asks even for what was allowed.
        self.assertEqual(self.client.get("/oauth/authorize", self.request(scope="openid email", prompt="consent")).status_code, 200)

    def test_the_person_says_no(self):
        self.sign_in()
        page = self.client.get("/oauth/authorize", self.request())
        response = self.client.post("/oauth/authorize/decide", {"request": page.context["key"], "decision": "deny"})
        self.assertEqual((query_of(response)["error"], query_of(response)["state"]), ("access_denied", "st-1"))
        self.assertEqual((Grant.objects.count(), Consent.objects.count()), (0, 0))
        # The page's answer is good once.
        again = self.client.post("/oauth/authorize/decide", {"request": page.context["key"], "decision": "allow"})
        self.assertEqual(again.status_code, 400)
        # And the answer is a form of the account's own: another site cannot post it.
        strict = Browser(enforce_csrf_checks=True)
        strict.login(email=self.user.email, password=PASSWORD)
        page = strict.get("/oauth/authorize", self.request())
        self.assertEqual(strict.post("/oauth/authorize/decide", {"request": page.context["key"], "decision": "allow"}).status_code, 403)

    def test_a_service_of_ours_needs_no_consent_and_sees_one_subject(self):
        ours, _ = self.make_client("ultracloud", "UltraCloud", trusted=True, kind=Client.PUBLIC)
        also_ours, _ = self.make_client("chatcube", "ChatCube", trusted=True, kind=Client.PUBLIC)
        other, _ = self.make_client("other-shop", "Other Shop", kind=Client.PUBLIC, redirect_uris="https://other.example/cb\n" + CALLBACK)
        self.sign_in()
        self.assertEqual(self.client.get("/oauth/authorize", self.request(ours)).status_code, 302)   # no page
        subjects = {client.client_id: self.claims(self.tokens(client)["id_token"])["sub"] for client in (ours, also_ours, other, self.cloud)}
        self.assertEqual(subjects["ultracloud"], subjects["chatcube"])
        self.assertNotEqual(subjects["other-shop"], subjects["ultracloud"])
        self.assertNotEqual(subjects["other-shop"], subjects["ultracloud-test"])   # two strangers cannot compare
        self.assertEqual(subjects["other-shop"], self.claims(self.tokens(other)["id_token"])["sub"])   # and it is the same every time

    def test_silent_requests_and_signing_in_again(self):
        # Nobody signed in: prompt=none is an error at the service, not a page here.
        self.assertEqual(query_of(self.client.get("/oauth/authorize", self.request(prompt="none")))["error"], "login_required")
        self.sign_in()
        self.code_for()
        self.assertIn("code", query_of(self.client.get("/oauth/authorize", self.request(prompt="none"))))
        # prompt=login: the password again, although signed in; then on, without the prompt.
        response = self.client.get("/oauth/authorize", self.request(prompt="login"))
        self.assertIn("reauth=1", response["Location"])
        self.assertContains(self.client.get(response["Location"]), "Sign in")   # the page, not a redirect past it
        back = self.client.post(response["Location"], {"username": "dana@example.org", "password": PASSWORD})
        self.assertNotIn("prompt", back["Location"])
        self.assertIn("code", query_of(self.client.get(back["Location"])))
        # max_age: a sign-in older than that is not enough.
        session = self.client.session
        session["auth_time"] = int(time.time()) - 4000
        session.save()
        self.assertIn("code", query_of(self.client.get("/oauth/authorize", self.request(max_age="7200"))))
        self.assertIn("reauth=1", self.client.get("/oauth/authorize", self.request(max_age="600"))["Location"])
        self.assertEqual(query_of(self.client.get("/oauth/authorize", self.request(max_age="600", prompt="none")))["error"], "login_required")

    # -- what is refused -------------------------------------------------
    def test_an_unknown_client_or_address_is_told_to_the_person_and_sent_nowhere(self):
        self.sign_in()
        for params in (self.request(client_id="nobody"), self.request(redirect_uri="https://evil.example/cb"),
                       self.request(redirect_uri=CALLBACK + "/more"), self.request(redirect_uri=None)):
            response = self.client.get("/oauth/authorize", params)
            self.assertEqual(response.status_code, 400)
            self.assertNotIn("Location", response)
        self.cloud.active = False
        self.cloud.save()
        self.assertEqual(self.client.get("/oauth/authorize", self.request()).status_code, 400)

    def test_a_request_that_is_wrong_is_told_to_the_service(self):
        self.sign_in()
        cases = [({"response_type": "token"}, "unsupported_response_type"), ({"response_type": "code id_token"}, "unsupported_response_type"),
                 ({"scope": "profile"}, "invalid_scope"), ({"code_challenge_method": "plain"}, "invalid_request"),
                 ({"code_challenge": None}, "invalid_request"), ({"code_challenge": "short"}, "invalid_request"),
                 ({"response_mode": "fragment"}, "invalid_request"), ({"prompt": "none login"}, "invalid_request"),
                 ({"max_age": "soon"}, "invalid_request"), ({"request": "x.y.z"}, "request_not_supported")]
        for extra, error in cases:
            answer = query_of(self.client.get("/oauth/authorize", self.request(**extra)))
            self.assertEqual((answer["error"], answer["state"], answer["iss"]), (error, "st-1", STORE), extra)
        self.assertEqual(Grant.objects.count(), 0)
        # A scope the client may not have is left out, not refused.
        self.assertEqual(self.tokens(scope="openid email offline_access")["scope"], "openid email")

    def test_the_code_is_bound_to_its_request(self):
        self.sign_in()
        other, other_secret = self.make_client("other", "Other")
        cases = [dict(code_verifier="w" * 64), dict(code_verifier=""), dict(redirect_uri=CALLBACK + "?x=1")]
        for extra in cases:
            response = self.exchange(self.code_for(), **extra)
            self.assertEqual((response.status_code, response.json()["error"]), (400, "invalid_grant"), extra)
        self.assertEqual(self.exchange("not-a-code").json()["error"], "invalid_grant")
        # Another client with its own secret cannot use this client's code - and does not spend it.
        code = self.code_for()
        self.assertEqual(self.exchange(code, other, other_secret).json()["error"], "invalid_grant")
        self.assertEqual(self.exchange(code).status_code, 200)
        # A code runs out.
        code = self.code_for()
        Code.objects.update(expires=timezone.now())
        self.assertEqual(self.exchange(code).json()["error"], "invalid_grant")

    def test_a_code_shown_twice_takes_back_what_the_first_showing_got(self):
        self.sign_in()
        code = self.code_for()
        tokens = self.exchange(code).json()
        bearer = {"Authorization": "Bearer " + tokens["access_token"]}
        self.assertEqual(self.client.get("/oauth/userinfo", headers=bearer).status_code, 200)
        self.assertEqual(self.exchange(code).json()["error"], "invalid_grant")
        self.assertEqual(self.client.get("/oauth/userinfo", headers=bearer).status_code, 401)

    def test_the_client_proves_itself(self):
        self.sign_in()
        wrong = self.exchange(self.code_for(), secret="wrong")
        self.assertEqual((wrong.status_code, wrong.json()["error"]), (401, "invalid_client"))
        self.assertIn("Basic", wrong["WWW-Authenticate"])
        data = {"grant_type": "authorization_code", "code": self.code_for(), "redirect_uri": CALLBACK, "code_verifier": VERIFIER}
        self.assertEqual(self.client.post("/oauth/token", {**data, "client_id": "ultracloud-test"}).status_code, 401)   # no secret at all
        self.assertEqual(self.client.post("/oauth/token", {**data, "client_id": "nobody"}).json()["error"], "invalid_client")
        both = self.client.post("/oauth/token", {**data, "client_secret": self.secret}, headers=basic("ultracloud-test", self.secret))
        self.assertEqual(both.json()["error"], "invalid_request")
        # The secret in the form works as well as in the header.
        data["code"] = self.code_for()
        self.assertEqual(self.client.post("/oauth/token", {**data, "client_id": "ultracloud-test", "client_secret": self.secret}).status_code, 200)
        self.assertEqual(self.client.post("/oauth/token", {"grant_type": "password", "client_id": "ultracloud-test", "client_secret": self.secret}).json()["error"], "unsupported_grant_type")
        self.assertEqual(self.client.get("/oauth/token").status_code, 405)

    def test_guessing_is_slowed_down(self):
        for _ in range(30):
            self.assertEqual(self.exchange("guess", secret="guess").status_code, 401)
        self.assertEqual(self.exchange("guess", secret=self.secret).status_code, 429)

    def test_an_application_on_the_persons_machine(self):
        """A public client: no secret, the code verifier is its proof, and it
        listens on whatever port it got."""
        app, _ = self.make_client("chatcube", "ChatCube", kind=Client.PUBLIC, trusted=True, scopes="openid profile offline_access",
                                  redirect_uris="http://127.0.0.1/callback", require_pkce=False)
        self.assertTrue(Client.objects.get(pk=app.pk).require_pkce)   # a public client cannot switch it off
        self.sign_in()
        params = self.request(app, redirect_uri="http://127.0.0.1:49152/callback", scope="openid profile offline_access")
        response = self.client.get("/oauth/authorize", params)
        self.assertTrue(response["Location"].startswith("http://127.0.0.1:49152/callback?code="))
        self.assertEqual(self.client.get("/oauth/authorize", {**params, "redirect_uri": "http://127.0.0.1:49152/other"}).status_code, 400)
        self.assertEqual(self.client.get("/oauth/authorize", {**params, "redirect_uri": "http://10.0.0.5:49152/callback"}).status_code, 400)
        data = {"grant_type": "authorization_code", "client_id": "chatcube", "code": query_of(response)["code"],
                "redirect_uri": "http://127.0.0.1:49152/callback", "code_verifier": VERIFIER}
        tokens = self.client.post("/oauth/token", data).json()
        self.assertIn("refresh_token", tokens)

    # -- refresh, revoke, introspect --------------------------------------
    def offline(self):
        client, secret = self.make_client("offline", "Offline", scopes="openid email offline_access", trusted=True)
        self.secret = secret
        self.sign_in()
        return client, self.tokens(client, scope="openid email offline_access")

    def refresh(self, client, token, **extra):
        return self.client.post("/oauth/token", {"grant_type": "refresh_token", "refresh_token": token, **extra}, headers=basic(client.client_id, self.secret))

    def test_a_refresh_token_is_spent_by_its_use(self):
        client, first = self.offline()
        second = self.refresh(client, first["refresh_token"]).json()
        self.assertNotEqual(second["refresh_token"], first["refresh_token"])
        claims = self.claims(second["id_token"])
        self.assertNotIn("nonce", claims)
        self.assertEqual(claims["sub"], self.claims(first["id_token"])["sub"])
        # Less may be asked for, never more.
        narrower = self.refresh(client, second["refresh_token"], scope="openid").json()
        self.assertEqual(narrower["scope"], "openid")
        self.assertNotIn("refresh_token", narrower)
        third = self.tokens(client, scope="openid email offline_access")
        self.assertEqual(self.refresh(client, third["refresh_token"], scope="openid email profile").json()["error"], "invalid_scope")

    def test_a_refresh_token_shown_twice_ends_the_grant(self):
        client, first = self.offline()
        second = self.refresh(client, first["refresh_token"]).json()
        self.assertEqual(self.refresh(client, first["refresh_token"]).json()["error"], "invalid_grant")   # the copy
        self.assertEqual(self.refresh(client, second["refresh_token"]).json()["error"], "invalid_grant")   # and the original with it
        self.assertEqual(self.client.get("/oauth/userinfo", headers={"Authorization": "Bearer " + second["access_token"]}).status_code, 401)

    def test_a_token_given_back(self):
        client, tokens = self.offline()
        auth = basic(client.client_id, self.secret)
        # Someone else's token, or none at all: the same answer, nothing changed.
        self.assertEqual(self.client.post("/oauth/revoke", {"token": "nonsense"}, headers=auth).status_code, 200)
        self.assertEqual(self.client.post("/oauth/revoke", {"token": tokens["access_token"]}, headers=basic("ultracloud-test", "x")).status_code, 401)
        self.assertEqual(self.client.post("/oauth/revoke", {"token": tokens["refresh_token"]}, headers=auth).status_code, 200)
        self.assertEqual(self.refresh(client, tokens["refresh_token"]).json()["error"], "invalid_grant")
        self.assertEqual(self.client.get("/oauth/userinfo", headers={"Authorization": "Bearer " + tokens["access_token"]}).status_code, 401)

    def test_a_resource_server_asks_whether_a_token_is_good(self):
        client, tokens = self.offline()
        relay, relay_secret = self.make_client("chat-relay", "Chat relay", resource_server=True)
        answer = self.client.post("/oauth/introspect", {"token": tokens["access_token"]}, headers=basic("chat-relay", relay_secret)).json()
        self.assertEqual((answer["active"], answer["client_id"], answer["scope"], answer["iss"]), (True, "offline", "openid email offline_access", STORE))
        self.assertEqual(answer["sub"], self.claims(tokens["id_token"])["sub"])
        self.assertEqual(self.client.post("/oauth/introspect", {"token": "nonsense"}, headers=basic("chat-relay", relay_secret)).json(), {"active": False})
        # A client that is not one may not ask.
        refused = self.client.post("/oauth/introspect", {"token": tokens["access_token"]}, headers=basic(client.client_id, self.secret))
        self.assertEqual((refused.status_code, refused.json()["error"]), (403, "unauthorized_client"))
        # An account that was switched off has no good tokens.
        User.objects.filter(pk=self.user.pk).update(is_active=False)
        self.assertEqual(self.client.post("/oauth/introspect", {"token": tokens["access_token"]}, headers=basic("chat-relay", relay_secret)).json(), {"active": False})

    # -- signing out, and taking access back -------------------------------
    def test_a_service_signs_the_person_out(self):
        self.cloud.post_logout_redirect_uris = "https://cloud.example/bye"
        self.cloud.save()
        self.sign_in()
        tokens = self.tokens()
        # Without proof of which sign-in is meant, the person is asked: a link cannot sign anyone out.
        page = self.client.get("/oauth/logout", {"post_logout_redirect_uri": "https://cloud.example/bye", "client_id": "ultracloud-test"})
        self.assertContains(page, "Sign out of your account?")
        self.assertIn("_auth_user_id", self.client.session)
        # With the token the account issued for this sign-in: out, and back to where the service registered.
        response = self.client.get("/oauth/logout", {"id_token_hint": tokens["id_token"], "post_logout_redirect_uri": "https://cloud.example/bye", "state": "s9"})
        self.assertEqual(response["Location"], "https://cloud.example/bye?state=s9")
        self.assertNotIn("_auth_user_id", self.client.session)
        self.assertIsNotNone(SignIn.objects.get().ended)
        # An address that was not registered is not gone to.
        self.sign_in()
        tokens = self.tokens()
        response = self.client.get("/oauth/logout", {"id_token_hint": tokens["id_token"], "post_logout_redirect_uri": "https://evil.example/"})
        self.assertContains(response, "You are signed out")
        # Another sign-in's token is no proof for this one.
        self.sign_in()
        self.assertContains(self.client.get("/oauth/logout", {"id_token_hint": tokens["id_token"]}), "Sign out of your account?")
        page = self.client.get("/oauth/logout", {"post_logout_redirect_uri": "https://cloud.example/bye", "client_id": "ultracloud-test"})
        response = self.client.post("/oauth/logout/confirm", {"request": page.context["key"]})
        self.assertEqual(response["Location"], "https://cloud.example/bye")
        self.assertNotIn("_auth_user_id", self.client.session)

    def test_the_person_takes_a_services_access_back(self):
        self.cloud.scopes = "openid profile email offline_access"
        self.cloud.save()
        self.sign_in()
        tokens = self.tokens(scope="openid email offline_access")
        page = self.client.get("/account/")
        self.assertContains(page, "Some Cloud")
        self.assertContains(page, "Your email address")
        self.client.post("/account/services/ultracloud-test/forget/")
        self.assertEqual(Consent.objects.count(), 0)
        self.assertEqual(self.client.get("/oauth/userinfo", headers={"Authorization": "Bearer " + tokens["access_token"]}).status_code, 401)
        refreshed = self.client.post("/oauth/token", {"grant_type": "refresh_token", "refresh_token": tokens["refresh_token"]}, headers=basic("ultracloud-test", self.secret))
        self.assertEqual(refreshed.json()["error"], "invalid_grant")
        self.assertEqual(self.client.get("/oauth/authorize", self.request()).status_code, 200)   # and it is asked again

    def test_a_new_password_ends_what_the_old_one_started(self):
        client, tokens = self.offline()
        other_machine = Browser()
        other_machine.login(email=self.user.email, password=PASSWORD)
        new = "another-long-passphrase"
        self.client.post("/account/password/", {"old_password": PASSWORD, "new_password1": new, "new_password2": new})
        self.assertIn("_auth_user_id", self.client.session)   # the machine that changed it stays signed in
        self.assertFalse(other_machine.get("/account/").wsgi_request.user.is_authenticated)
        self.assertEqual(self.refresh(client, tokens["refresh_token"]).json()["error"], "invalid_grant")
        self.assertEqual(self.client.get("/oauth/userinfo", headers={"Authorization": "Bearer " + tokens["access_token"]}).status_code, 401)
        self.assertEqual(SignIn.objects.filter(ended__isnull=True).count(), 1)

    # -- keys, the registry -----------------------------------------------
    def test_a_new_key_signs_and_the_old_one_still_verifies(self):
        self.sign_in()
        before = self.tokens()["id_token"]
        old_kid = json.loads(unb64(before.split(".")[0]))["kid"]
        new_kid = keys.rotate()
        after = self.tokens()["id_token"]
        self.assertEqual(json.loads(unb64(after.split(".")[0]))["kid"], new_kid)
        self.assertEqual({key["kid"] for key in self.client.get("/oauth/jwks").json()["keys"]}, {old_kid, new_kid})
        self.claims(before)
        self.claims(after)
        self.assertTrue(keys.retire(old_kid))
        self.assertEqual({key["kid"] for key in self.client.get("/oauth/jwks").json()["keys"]}, {new_kid})
        self.assertIsNone(keys.verify(before))
        # A token tampered with, or signed "none", is nobody's.
        header, claims, signature = after.split(".")
        forged = base64.urlsafe_b64encode(json.dumps({**json.loads(unb64(claims)), "sub": "someone-else"}).encode()).decode().rstrip("=")
        self.assertIsNone(keys.verify(f"{header}.{forged}.{signature}"))
        none = base64.urlsafe_b64encode(json.dumps({"alg": "none", "kid": new_kid}).encode()).decode().rstrip("=")
        self.assertIsNone(keys.verify(f"{none}.{claims}."))

    def test_a_client_is_registered_from_the_command_line(self):
        out = io.StringIO()
        call_command("oidc", "client", "ultraads", "--name", "UltraAds", "--redirect-uri", "https://ads.example/cb", "--trusted", stdout=out)
        secret = [line for line in out.getvalue().splitlines() if line.startswith("client_secret")][0].split(": ", 1)[1]
        row = Client.objects.get(client_id="ultraads")
        self.assertTrue(row.trusted and row.check_secret(secret))
        self.assertIn("issuer: " + STORE, out.getvalue())
        self.assertTrue(AuditEntry.objects.filter(action="oidc.client.add", target="ultraads").exists())
        out = io.StringIO()
        call_command("oidc", "client", "ultraads", "--name", "UltraAds 2", stdout=out)
        self.assertNotIn("client_secret", out.getvalue())   # shown once, not again
        self.assertTrue(Client.objects.get(client_id="ultraads").check_secret(secret))
        from django.core.management.base import CommandError
        for arguments in (["bad", "--name", "Bad", "--redirect-uri", "http://shop.example/cb"], ["bad", "--name", "Bad", "--redirect-uri", "https://shop.example/cb#x"],
                          ["bad", "--name", "Bad"], ["bad", "--name", "Bad", "--redirect-uri", "https://shop.example/cb", "--scopes", "openid everything"]):
            with self.assertRaises(CommandError):
                call_command("oidc", "client", *arguments, stdout=io.StringIO())
        self.assertFalse(Client.objects.filter(client_id="bad").exists())

    def test_what_ran_out_is_removed(self):
        client, tokens = self.offline()
        self.refresh(client, tokens["refresh_token"])
        long_ago = timezone.now() - timezone.timedelta(days=3)
        Token.objects.update(expires=long_ago)
        Code.objects.update(expires=long_ago)
        Grant.objects.update(created=long_ago)
        out = io.StringIO()
        call_command("oidc", "cleanup", stdout=out)
        self.assertEqual((Token.objects.count(), Grant.objects.count()), (0, 0))


@override_settings(ALLOWED_HOSTS=["store.ultraos.eu", "account.ultraos.eu", "testserver"])
class TwoHostsCase(TestCase):
    """The account at an address of its own: one service, two hosts, and a
    browser that keeps each host's cookies apart."""

    def setUp(self):
        override = override_settings(BASE_URL=STORE, ACCOUNT_URL=ACCOUNT, OIDC_KEY_FILE="", OIDC_SUBJECT_SECRET="")
        override.enable()
        self.addCleanup(override.disable)
        cache.clear()
        self.user = User.objects.create_user("dana@example.org", PASSWORD, name="Dana", email_verified=True)
        TermsVersion.objects.create(version=1, text="Be good.")
        self.store = Browser(HTTP_HOST="store.ultraos.eu")
        self.account = Browser(HTTP_HOST="account.ultraos.eu")

    def follow(self, browser, response, stop_at: str = ""):
        """Redirects followed, each to the host it names, until a page - or
        until the address named."""
        for _ in range(12):
            if response.status_code not in (301, 302):
                return browser, response
            location = response["Location"]
            if stop_at and stop_at in location:
                return browser, response
            host = urlsplit(location).netloc
            if host:
                browser = self.account if host == "account.ultraos.eu" else self.store
                self.assertIn(host, ("account.ultraos.eu", "store.ultraos.eu"), location)
            response = browser.get(location)
        self.fail("redirects without an end")

    def sign_in_from_the_store(self, path="/console/"):
        browser, response = self.follow(self.store, self.store.get(path))
        self.assertIs(browser, self.account)
        self.assertContains(response, "Sign in")
        posted = self.account.post(response.wsgi_request.get_full_path(), {"username": "dana@example.org", "password": PASSWORD})
        return self.follow(self.account, posted)

    def test_each_host_has_its_own_addresses(self):
        # The account's host: its pages at the root, the documents, nothing of the store.
        self.assertContains(self.account.get("/signin/"), "Sign in")
        self.assertContains(self.account.get("/signin/"), "ULTRA OS account")
        self.assertNotContains(self.account.get("/signin/"), "Developer console")
        self.assertEqual(self.account.get("/apps/").status_code, 404)
        self.assertEqual(self.account.get("/console/").status_code, 404)
        self.assertEqual(self.account.get("/v1/catalog.json").status_code, 404)
        self.assertEqual(self.account.get("/admin/").status_code, 404)
        self.assertEqual(self.account.get("/healthz").status_code, 200)
        document = self.account.get("/.well-known/openid-configuration").json()
        self.assertEqual(document["issuer"], ACCOUNT)
        self.assertEqual(document["authorization_endpoint"], ACCOUNT + "/oauth/authorize")
        self.assertEqual(document["jwks_uri"], ACCOUNT + "/oauth/jwks")
        # The store's host: no issuer there, and its account pages are across the road.
        self.assertEqual(self.store.get("/.well-known/openid-configuration").status_code, 404)
        self.assertEqual(self.store.get("/oauth/jwks").status_code, 404)
        self.assertEqual(self.store.post("/oauth/token", {}).status_code, 404)
        self.assertEqual(self.store.get("/account/signup/")["Location"], ACCOUNT + "/signup/")
        self.assertEqual(self.store.get("/account/password/reset/?x=1")["Location"], ACCOUNT + "/password/reset/?x=1")
        self.assertEqual(self.store.post("/account/signup/", {"email": "x@example.org"}).status_code, 404)
        self.assertEqual(self.store.get("/").status_code, 200)

    def test_the_mails_name_the_account(self):
        self.account.post("/signup/", {"name": "Eve", "email": "eve@example.org", "password": PASSWORD, "password_again": PASSWORD})
        self.assertIn(ACCOUNT + "/verify/", mail.outbox[0].body)
        self.assertTrue(mail.outbox[0].subject.startswith("ULTRA OS account"))
        self.account.post("/password/reset/", {"email": "dana@example.org"})
        self.assertIn(ACCOUNT + "/password/reset/", mail.outbox[1].body)
        # The confirmed address signs in at the account and goes on to the store, signed in there too.
        link = [line for line in mail.outbox[0].body.splitlines() if "/verify/" in line][0]
        browser, response = self.follow(self.account, self.account.get(link.replace(ACCOUNT, "")))
        self.assertIs(browser, self.store)
        self.assertEqual(response.wsgi_request.user.email, "eve@example.org")

    def test_the_store_signs_in_through_the_account(self):
        first = self.store.get("/console/")
        self.assertEqual(first["Location"], "/account/signin/?next=/console/")
        off = self.store.get(first["Location"])
        target = urlsplit(off["Location"])
        asked = {key: values[0] for key, values in parse_qs(target.query).items()}
        self.assertEqual((target.scheme + "://" + target.netloc + target.path), ACCOUNT + "/oauth/authorize")
        self.assertEqual((asked["client_id"], asked["redirect_uri"], asked["code_challenge_method"]), ("ultrastore", STORE + "/sso/callback", "S256"))

        browser, response = self.sign_in_from_the_store()
        self.assertIs(browser, self.store)
        self.assertEqual(response.wsgi_request.get_full_path(), "/console/apply/")   # the console, for someone with no publisher yet
        self.assertEqual(response.wsgi_request.user, self.user)
        self.assertEqual(self.account.get("/").wsgi_request.user, self.user)
        # Two sessions, one sign-in: the store's is tied to the account's.
        self.assertNotEqual(self.store.cookies["sessionid"].value, self.account.cookies["sessionid"].value)
        self.assertEqual(self.store.session["sso_sid"], SignIn.objects.get().sid)
        # A second visit, once signed in at the account, needs no password.
        other_tab = Browser(HTTP_HOST="store.ultraos.eu")
        self.store, kept = other_tab, self.store
        browser, response = self.follow(self.store, self.store.get("/console/"))
        self.assertEqual((browser is self.store, response.wsgi_request.user), (True, self.user))
        self.store = kept

        # Signing out in the store ends its session, then the account's, and comes back.
        out = self.store.post("/account/signout/")
        self.assertTrue(out["Location"].startswith(ACCOUNT + "/oauth/logout?"))
        self.assertFalse(self.store.get("/").wsgi_request.user.is_authenticated)
        browser, response = self.follow(self.store, out, stop_at=STORE + "/")
        self.assertEqual(response["Location"], STORE + "/")
        self.assertFalse(self.account.get("/signin/").wsgi_request.user.is_authenticated)
        self.assertIsNotNone(SignIn.objects.get().ended)
        # And the other tab, which never signed out itself, is signed out with it.
        self.assertFalse(other_tab.get("/").wsgi_request.user.is_authenticated)

    def test_signing_out_at_the_account_signs_the_store_out(self):
        self.sign_in_from_the_store()
        self.assertTrue(self.store.get("/").wsgi_request.user.is_authenticated)
        self.assertEqual(self.account.post("/signout/")["Location"], "/signin/")
        self.assertFalse(self.store.get("/").wsgi_request.user.is_authenticated)

    def test_a_callback_nobody_started_signs_nobody_in(self):
        self.assertEqual(self.account.get("/sso/callback").status_code, 404)
        # A code brought to a browser that did not start the sign-in (a link sent to a victim).
        self.account.post("/signin/", {"username": "dana@example.org", "password": PASSWORD})
        attacker = Browser(HTTP_HOST="store.ultraos.eu")
        off = attacker.get("/account/signin/?next=/console/")
        code = self.account.get(off["Location"].replace(ACCOUNT, ""))
        self.assertIn("code=", code["Location"])
        victim = self.store.get(code["Location"].replace(STORE, ""))
        self.assertEqual(victim["Location"], "/")
        self.assertFalse(self.store.get("/").wsgi_request.user.is_authenticated)
        # A next that leads elsewhere is not followed.
        off = self.store.get("/account/signin/?next=https://evil.example/")
        browser, response = self.follow(self.store, off)
        self.assertEqual((browser is self.store, urlsplit(response.wsgi_request.build_absolute_uri()).netloc), (True, "store.ultraos.eu"))

    def test_another_service_signs_in_at_the_account(self):
        client = Client(client_id="ultracloud", name="UltraCloud", kind=Client.PUBLIC, trusted=True, redirect_uris=CALLBACK)
        client.full_clean()
        client.save()
        self.account.post("/signin/", {"username": "dana@example.org", "password": PASSWORD})
        params = {"response_type": "code", "client_id": "ultracloud", "redirect_uri": CALLBACK, "scope": "openid email", "state": "s",
                  "code_challenge": challenge(), "code_challenge_method": "S256"}
        response = self.account.get("/oauth/authorize?" + urlencode(params))
        answer = query_of(response)
        self.assertEqual(answer["iss"], ACCOUNT)
        tokens = self.account.post("/oauth/token", {"grant_type": "authorization_code", "client_id": "ultracloud", "code": answer["code"],
                                                    "redirect_uri": CALLBACK, "code_verifier": VERIFIER}).json()
        claims = keys.verify(tokens["id_token"])
        self.assertEqual((claims["iss"], claims["aud"], claims["email"]), (ACCOUNT, "ultracloud", "dana@example.org"))
        # The store, being ours as well, knows the person by the same subject.
        from ultrastore_portal.identity import sso
        self.assertEqual(claims["sub"], provider.subject(self.user, sso.client()))
