from django.contrib import admin
from django.contrib.auth.admin import UserAdmin as DjangoUserAdmin

from .models import User


@admin.register(User)
class UserAdmin(DjangoUserAdmin):
    ordering = ["email"]
    list_display = ["email", "name", "email_verified", "is_staff", "is_active", "state", "erase_after", "date_joined"]
    list_filter = ["is_staff", "is_active", "email_verified"]
    search_fields = ["email", "name"]
    fieldsets = [
        (None, {"fields": ["email", "password"]}),
        ("Person", {"fields": ["name", "email_verified"]}),
        ("Permissions", {"fields": ["is_active", "is_staff", "is_superuser", "groups", "user_permissions"]}),
        ("Dates", {"fields": ["last_login", "date_joined"]}),
        ("Leaving", {"fields": ["closed_at", "deletion_requested_at", "erase_after", "erased_at"]}),
    ]
    # Leaving is the person's own doing (accounts/closure.py) or the daily
    # maintenance's: the admin site shows it and does not set it.
    readonly_fields = ["closed_at", "deletion_requested_at", "erase_after", "erased_at"]
    add_fieldsets = [(None, {"classes": ["wide"], "fields": ["email", "name", "password1", "password2", "email_verified", "is_staff"]})]
