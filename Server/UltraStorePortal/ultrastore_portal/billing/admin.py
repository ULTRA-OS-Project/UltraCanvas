"""Orders and payments in the admin site: to read. Money moves through the
provider's reports and the operator's refund (/operator/orders/), never by
editing a row."""

from django.contrib import admin

from .models import Order, Payment


class PaymentInline(admin.TabularInline):
    model = Payment
    extra = 0
    can_delete = False

    def has_add_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(Order)
class OrderAdmin(admin.ModelAdmin):
    list_display = ["created", "app_name", "amount", "currency", "status", "provider", "receipt_no", "payer_email"]
    list_filter = ["status", "provider", "cycle", "source"]
    search_fields = ["public_id", "app_name", "payer_email", "licence_id", "provider_transaction"]
    exclude = ["secret", "licence"]
    inlines = [PaymentInline]

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
