from django.contrib import admin

from .models import Attribution, Balance, Campaign, Click, DayCount, LedgerEntry, Placement, TopUp


@admin.register(Placement)
class PlacementAdmin(admin.ModelAdmin):
    list_display = ["key", "area", "page", "name", "slots", "floor", "state", "position"]
    list_filter = ["area", "state"]


@admin.register(Campaign)
class CampaignAdmin(admin.ModelAdmin):
    list_display = ["name", "publisher", "state", "stopped", "daily_budget", "spent", "created"]
    list_filter = ["state", "stopped"]
    search_fields = ["name", "publisher__name", "app__app_id"]
    raw_id_fields = ["publisher", "app", "created_by"]


class ReadOnly(admin.ModelAdmin):
    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


@admin.register(Balance)
class BalanceAdmin(ReadOnly):
    list_display = ["publisher", "amount", "first_funded"]


@admin.register(LedgerEntry)
class LedgerEntryAdmin(ReadOnly):
    list_display = ["created", "publisher", "kind", "amount", "balance_after", "note"]
    list_filter = ["kind"]


@admin.register(TopUp)
class TopUpAdmin(ReadOnly):
    list_display = ["created", "publisher", "amount", "currency", "status", "receipt_no"]
    list_filter = ["status"]


@admin.register(DayCount)
class DayCountAdmin(ReadOnly):
    list_display = ["day", "campaign", "placement", "country", "shown", "clicks", "invalid", "spent", "installs", "orders", "revenue"]
    date_hierarchy = "day"


@admin.register(Click)
class ClickAdmin(ReadOnly):
    list_display = ["at", "campaign", "placement", "price", "valid", "reason"]
    list_filter = ["valid", "reason"]
    exclude = ["jti"]


@admin.register(Attribution)
class AttributionAdmin(ReadOnly):
    list_display = ["created", "campaign", "order", "counted"]


from .models import Keyword, PlacementBid, PolicyAcceptance, PolicyVersion, TermCount  # noqa: E402


@admin.register(PolicyVersion)
class PolicyVersionAdmin(admin.ModelAdmin):
    list_display = ["version", "in_force_from", "created"]


@admin.register(PolicyAcceptance)
class PolicyAcceptanceAdmin(ReadOnly):
    list_display = ["publisher", "version", "accepted_by", "accepted_at"]


@admin.register(Keyword)
class KeywordAdmin(ReadOnly):
    list_display = ["campaign", "text", "match", "negative"]


@admin.register(PlacementBid)
class PlacementBidAdmin(ReadOnly):
    list_display = ["campaign", "placement", "max_click"]


@admin.register(TermCount)
class TermCountAdmin(ReadOnly):
    list_display = ["day", "campaign", "term", "shown", "clicks"]
