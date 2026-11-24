from django.apps import AppConfig


class IdentityConfig(AppConfig):
    name = "ultrastore_portal.identity"
    label = "identity"
    verbose_name = "Sign-in for other services (OpenID Connect)"

    def ready(self) -> None:
        from . import sessions   # noqa: F401 - the sign-in and sign-out signals
