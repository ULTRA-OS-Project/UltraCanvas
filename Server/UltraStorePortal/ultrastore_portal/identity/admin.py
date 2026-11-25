"""The clients of the account in the admin site: a service is registered
here (or with `ultrastore-portal oidc client`), and a confidential one's
secret is shown once, when it is made."""

from django.contrib import admin, messages

from ..store.services import audit
from .models import Client, Consent, Grant, SignIn, SigningKey


@admin.register(Client)
class ClientAdmin(admin.ModelAdmin):
    list_display = ["client_id", "name", "kind", "trusted", "resource_server", "active", "created"]
    list_filter = ["kind", "trusted", "active"]
    search_fields = ["client_id", "name"]
    actions = ["new_secret"]

    def get_readonly_fields(self, request, obj=None):
        return ["client_id", "created"] if obj else ["created"]

    def save_model(self, request, obj, form, change):
        secret = ""
        if obj.kind == Client.CONFIDENTIAL and not obj.secret_hash:
            secret = obj.new_secret()
        if obj.kind == Client.PUBLIC:
            obj.secret_hash = ""
        super().save_model(request, obj, form, change)
        audit(request.user, "oidc.client.change" if change else "oidc.client.add", "oidc_client", obj.client_id, kind=obj.kind, trusted=obj.trusted)
        if secret:
            messages.warning(request, f"The secret of {obj.client_id}, shown this once: {secret}")

    @admin.action(description="Make a new secret (the old one stops working)")
    def new_secret(self, request, queryset):
        for client in queryset.filter(kind=Client.CONFIDENTIAL):
            secret = client.new_secret()
            client.save(update_fields=["secret_hash"])
            audit(request.user, "oidc.client.secret", "oidc_client", client.client_id)
            messages.warning(request, f"The secret of {client.client_id}, shown this once: {secret}")


class ReadOnly(admin.ModelAdmin):
    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(Consent)
class ConsentAdmin(ReadOnly):
    list_display = ["user", "client", "scope", "granted"]
    search_fields = ["user__email", "client__client_id"]


@admin.register(Grant)
class GrantAdmin(ReadOnly):
    list_display = ["user", "client", "scope", "created", "revoked"]
    list_filter = ["client"]
    search_fields = ["user__email"]
    exclude = ["nonce", "sid"]


@admin.register(SignIn)
class SignInAdmin(ReadOnly):
    list_display = ["user", "created", "ended"]
    search_fields = ["user__email"]
    exclude = ["sid"]


@admin.register(SigningKey)
class SigningKeyAdmin(ReadOnly):
    list_display = ["kid", "created", "retired"]
    exclude = ["pem"]

    def has_delete_permission(self, request, obj=None):
        return False
