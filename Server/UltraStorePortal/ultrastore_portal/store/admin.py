"""The admin site (/admin/): every record as it is, for the operator's
staff. The decisions - a publisher approved, a release published - are made
on the operator's pages (/operator/), which write the audit log and mail
the publisher; here a state is a read-only field, so nothing is decided
around them.

Version: 0.1.0
"""

from django.contrib import admin

from .models import App, AuditEntry, CatalogSnapshot, Package, Publisher, PublisherMember, Release, Report, Screenshot, TermsVersion

admin.site.site_header = "UltraStore portal"
admin.site.site_title = "UltraStore portal"
admin.site.index_title = "Records"


class ReadOnlyAdmin(admin.ModelAdmin):
    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


class MemberInline(admin.TabularInline):
    model = PublisherMember
    extra = 0
    autocomplete_fields = ["user"]


@admin.register(Publisher)
class PublisherAdmin(admin.ModelAdmin):
    list_display = ["name", "slug", "state", "support_email", "terms_version", "created"]
    list_filter = ["state"]
    search_fields = ["name", "slug", "support_email"]
    readonly_fields = ["state", "decided_by", "decided_at", "terms_version", "terms_accepted_at", "terms_accepted_by", "created", "updated"]
    inlines = [MemberInline]


class ScreenshotInline(admin.TabularInline):
    model = Screenshot
    extra = 0


@admin.register(App)
class AppAdmin(admin.ModelAdmin):
    list_display = ["name", "app_id", "publisher", "kind", "category", "pricing_model", "featured", "suspended", "last_published"]
    list_filter = ["kind", "category", "pricing_model", "featured", "suspended", "visibility", "lifecycle"]
    search_fields = ["name", "app_id", "publisher__name"]
    readonly_fields = ["suspended", "suspend_reason", "downloads", "first_published", "last_published", "created", "updated"]
    autocomplete_fields = ["publisher"]
    inlines = [ScreenshotInline]


class PackageInline(admin.TabularInline):
    model = Package
    extra = 0
    can_delete = False
    readonly_fields = ["os", "arch", "variant", "min_os", "format", "entry", "sha256", "size", "original_name", "downloads"]

    def has_add_permission(self, request, obj=None):
        return False


@admin.register(Release)
class ReleaseAdmin(admin.ModelAdmin):
    list_display = ["app", "version", "channel", "state", "submitted_at", "published_at"]
    list_filter = ["state", "channel"]
    search_fields = ["app__name", "app__app_id", "version"]
    readonly_fields = ["state", "submitted_at", "submitted_by", "reviewed_at", "reviewed_by", "published_at", "created", "updated"]
    autocomplete_fields = ["app"]
    inlines = [PackageInline]


@admin.register(Report)
class ReportAdmin(admin.ModelAdmin):
    list_display = ["target", "kind", "state", "created"]
    list_filter = ["state", "kind", "target_kind"]
    search_fields = ["target", "text"]
    readonly_fields = ["public_id", "target_kind", "target", "kind", "text", "reporter", "state", "handled_by", "handled_at", "created"]


@admin.register(TermsVersion)
class TermsVersionAdmin(admin.ModelAdmin):
    list_display = ["version", "in_force_from", "created"]


@admin.register(AuditEntry)
class AuditEntryAdmin(ReadOnlyAdmin):
    list_display = ["created", "actor_name", "action", "target_kind", "target"]
    list_filter = ["action", "target_kind"]
    search_fields = ["actor_name", "target", "action"]


@admin.register(CatalogSnapshot)
class CatalogSnapshotAdmin(ReadOnlyAdmin):
    list_display = ["sequence", "created", "apps", "components", "public_key"]
    exclude = ["body"]
