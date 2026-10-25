#!/usr/bin/env python3
# Tests/UltraDBAdmin/e2e/test_oidc_provider.py
# An OpenID Connect provider for the browser test of UltraDBAdmin's relay
# sign-in (run_e2e.sh) - never for anything else: it signs everybody in, at
# once, as the one user it is told to be.
#
#   test_oidc_provider.py --port 9100 --client uda --email anna@example.com
#
# Serves the discovery document, its key set, an authorization endpoint that
# redirects straight back with a code, and a token endpoint that checks the
# code, the redirect URI and the PKCE verifier (S256) and answers with an ID
# token signed RS256 - by the openssl command line, so nothing beyond the
# standard library and openssl is needed. Its key is made at start-up.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
import argparse
import base64
import hashlib
import http.server
import json
import os
import secrets
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


class Provider:
    def __init__(self, base: str, client: str, email: str):
        self.base = base
        self.client = client
        self.email = email
        self.codes = {}
        self.lock = threading.Lock()
        self.folder = tempfile.mkdtemp(prefix="oidc-")
        self.key = os.path.join(self.folder, "key.pem")
        subprocess.run(["openssl", "genrsa", "-out", self.key, "2048"], check=True, capture_output=True)
        modulus = subprocess.run(["openssl", "rsa", "-in", self.key, "-noout", "-modulus"], check=True,
                                 capture_output=True, text=True).stdout.strip().split("=", 1)[1]
        self.jwk = {"kty": "RSA", "use": "sig", "alg": "RS256", "kid": "test-1",
                    "n": b64url(bytes.fromhex(modulus)), "e": b64url((65537).to_bytes(3, "big"))}

    def sign(self, claims: dict) -> str:
        header = {"alg": "RS256", "typ": "JWT", "kid": "test-1"}
        data = b64url(json.dumps(header).encode()) + "." + b64url(json.dumps(claims).encode())
        signature = subprocess.run(["openssl", "dgst", "-sha256", "-sign", self.key], input=data.encode(),
                                   check=True, capture_output=True).stdout
        return data + "." + b64url(signature)


def make_handler(provider: Provider):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            sys.stderr.write("oidc: " + (fmt % args) + "\n")

        def reply(self, status: int, body: dict, headers=None):
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            for name, value in (headers or {}).items():
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            query = dict(urllib.parse.parse_qsl(url.query))
            if url.path == "/.well-known/openid-configuration":
                return self.reply(200, {
                    "issuer": provider.base,
                    "authorization_endpoint": provider.base + "/authorize",
                    "token_endpoint": provider.base + "/token",
                    "jwks_uri": provider.base + "/jwks",
                    "response_types_supported": ["code"],
                    "id_token_signing_alg_values_supported": ["RS256"],
                })
            if url.path == "/jwks":
                return self.reply(200, {"keys": [provider.jwk]})
            if url.path == "/authorize":
                if query.get("client_id") != provider.client or query.get("code_challenge_method") != "S256":
                    return self.reply(400, {"error": "invalid_request"})
                code = secrets.token_urlsafe(16)
                with provider.lock:
                    provider.codes[code] = query
                target = query["redirect_uri"] + "?" + urllib.parse.urlencode(
                    {"code": code, "state": query.get("state", "")})
                self.send_response(302)
                self.send_header("Location", target)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return None
            return self.reply(404, {"error": "not_found"})

        def do_POST(self):
            if urllib.parse.urlsplit(self.path).path != "/token":
                return self.reply(404, {"error": "not_found"})
            length = int(self.headers.get("Content-Length", "0"))
            form = dict(urllib.parse.parse_qsl(self.rfile.read(length).decode()))
            with provider.lock:
                grant = provider.codes.pop(form.get("code", ""), None)
            if grant is None:
                return self.reply(400, {"error": "invalid_grant"})
            challenge = b64url(hashlib.sha256(form.get("code_verifier", "").encode()).digest())
            if challenge != grant.get("code_challenge") or form.get("redirect_uri") != grant.get("redirect_uri"):
                return self.reply(400, {"error": "invalid_grant", "error_description": "PKCE or redirect_uri"})
            now = int(time.time())
            claims = {"iss": provider.base, "sub": "sub-" + provider.email, "aud": provider.client,
                      "iat": now, "exp": now + 600, "nonce": grant.get("nonce", ""),
                      "email": provider.email, "email_verified": True}
            return self.reply(200, {"access_token": secrets.token_urlsafe(16), "token_type": "Bearer",
                                    "expires_in": 600, "id_token": provider.sign(claims)})

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9100)
    parser.add_argument("--client", default="uda")
    parser.add_argument("--email", default="anna@example.com")
    args = parser.parse_args()
    provider = Provider(f"http://127.0.0.1:{args.port}", args.client, args.email)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(provider))
    print(f"test OpenID provider at {provider.base}, signing everyone in as {args.email}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
