"""The affiliate programme in the admin site: to read. What changes a
profile, a commission or a record is the programme's own pages."""

from django.contrib import admin

from .models import AffiliatePayout, AffiliateProfile, Boost, Commission, Sanction


class ReadOnly(admin.ModelAdmin):
    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


@admin.register(AffiliateProfile)
class AffiliateProfileAdmin(ReadOnly):
    list_display = ["handle", "user", "state", "phone_verified_at", "id_checked_at", "superaffiliate", "created"]
    list_filter = ["state"]
    search_fields = ["handle", "user__email"]
    exclude = ["phone_code_hash"]


@admin.register(Commission)
class CommissionAdmin(ReadOnly):
    list_display = ["created", "affiliate", "basis", "order", "amount", "currency", "state", "payout"]
    list_filter = ["state", "basis", "currency"]


admin.site.register(Boost, ReadOnly)
admin.site.register(AffiliatePayout, ReadOnly)
admin.site.register(Sanction, ReadOnly)
