"""ultrastore-portal oidc ...: the account's clients and keys from the
command line.

    oidc client <id> --name NAME --redirect-uri URI [--redirect-uri ...]
                     [--post-logout-redirect-uri URI] [--public] [--trusted]
                     [--resource-server] [--scopes "openid profile email"]
                     [--new-secret]
        register a service, or change one; a confidential client's secret
        is printed once, when it is made or with --new-secret
    oidc clients     the registered services
    oidc issuer      the issuer and where its documents are
    oidc rotate-key  a new signing key; the earlier ones stay published
    oidc retire-key <kid>
    oidc cleanup     tokens and grants that ran out, removed (a daily timer)
"""

from django.core.exceptions import ValidationError
from django.core.management.base import BaseCommand, CommandError

from ultrastore_portal.identity import hosts, keys, provider
from ultrastore_portal.identity.models import Client
from ultrastore_portal.store.services import audit


class Command(BaseCommand):
    help = "The OpenID Connect provider: clients, keys, cleanup."

    def add_arguments(self, parser):
        sub = parser.add_subparsers(dest="what", required=True)
        client = sub.add_parser("client")
        client.add_argument("client_id")
        client.add_argument("--name")
        client.add_argument("--redirect-uri", action="append", default=[])
        client.add_argument("--post-logout-redirect-uri", action="append", default=[])
        client.add_argument("--scopes")
        client.add_argument("--public", action="store_true")
        client.add_argument("--trusted", action="store_true")
        client.add_argument("--resource-server", action="store_true")
        client.add_argument("--new-secret", action="store_true")
        sub.add_parser("clients")
        sub.add_parser("issuer")
        sub.add_parser("rotate-key")
        sub.add_parser("retire-key").add_argument("kid")
        sub.add_parser("cleanup")

    def handle(self, *args, what: str, **options):
        getattr(self, what.replace("-", "_"))(**options)

    def client(self, client_id, name, redirect_uri, post_logout_redirect_uri, scopes, public, trusted, resource_server, new_secret, **_):
        row = Client.objects.filter(client_id=client_id).first()
        made = row is None
        if made:
            if not name or not redirect_uri:
                raise CommandError("A new client needs --name and at least one --redirect-uri.")
            row = Client(client_id=client_id, kind=Client.PUBLIC if public else Client.CONFIDENTIAL)
        if name:
            row.name = name
        if redirect_uri:
            row.redirect_uris = "\n".join(redirect_uri)
        if post_logout_redirect_uri:
            row.post_logout_redirect_uris = "\n".join(post_logout_redirect_uri)
        if scopes:
            row.scopes = scopes
        if made or trusted:
            row.trusted = trusted
        if made or resource_server:
            row.resource_server = resource_server
        secret = row.new_secret() if row.kind == Client.CONFIDENTIAL and (made or new_secret) else ""
        try:
            row.full_clean()
        except ValidationError as error:
            raise CommandError("; ".join(message for messages in error.message_dict.values() for message in messages)) from error
        row.save()
        audit(None, "oidc.client.add" if made else "oidc.client.change", "oidc_client", row.client_id, kind=row.kind, trusted=row.trusted)
        self.stdout.write(f"{'registered' if made else 'changed'}: {row.client_id} ({row.kind}{', ours' if row.trusted else ''})")
        self.stdout.write(f"issuer: {provider.issuer()}")
        if secret:
            self.stdout.write(f"client_secret (shown this once): {secret}")

    def clients(self, **_):
        for row in Client.objects.all():
            flags = ", ".join(flag for flag, on in [("ours", row.trusted), ("resource server", row.resource_server), ("off", not row.active)] if on)
            self.stdout.write(f"{row.client_id}\t{row.kind}\t{row.name}\t{flags}")

    def issuer(self, **_):
        self.stdout.write(provider.issuer())
        self.stdout.write(hosts.account_url("oidc:discovery"))
        self.stdout.write(hosts.account_url("oidc:jwks"))

    def rotate_key(self, **_):
        self.stdout.write(f"signing with {keys.rotate()}")

    def retire_key(self, kid, **_):
        if not keys.retire(kid):
            raise CommandError(f"No published key {kid}.")
        self.stdout.write(f"retired {kid}")

    def cleanup(self, **_):
        removed = provider.cleanup()
        self.stdout.write(f"removed {removed['tokens']} tokens, {removed['grants']} grants")
