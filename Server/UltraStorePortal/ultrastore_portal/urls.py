"""Every address of the store, all under the one root.

Version: 0.1.0
"""

from django.conf import settings
from django.contrib import admin
from django.urls import include, path, register_converter
from django.views.static import serve

from .api import views as api
from .web import account, console, operator, storefront


class AppId:
    regex = r"[a-z0-9][a-z0-9_-]*(?:\.[a-z0-9][a-z0-9_-]*)+"

    def to_python(self, value: str) -> str:
        return value

    def to_url(self, value: str) -> str:
        return value


register_converter(AppId, "appid")

store_patterns = ([
    path("", storefront.home, name="home"),
    path("apps/", storefront.browse, name="browse"),
    path("category/<slug:category>/", storefront.browse, name="category"),
    path("apps/<appid:app_id>/", storefront.app_detail, name="app"),
    path("apps/<appid:app_id>/download/<int:pk>/", storefront.download, name="download"),
    path("apps/<appid:app_id>/report/", storefront.report, name="report"),
    path("reports/<str:public_id>/", storefront.report_outcome, name="report_outcome"),
    path("publisher/<slug:slug>/", storefront.publisher_page, name="publisher"),
    path("terms/", storefront.terms, name="terms"),
    path("robots.txt", storefront.robots, name="robots"),
    path("sitemap.xml", storefront.sitemap, name="sitemap"),
], "store")

account_patterns = ([
    path("signup/", account.signup, name="signup"),
    path("verify/<str:token>/", account.verify, name="verify"),
    path("resend/", account.resend, name="resend"),
    path("signin/", account.SignIn.as_view(), name="signin"),
    path("signout/", account.auth_views.LogoutView.as_view(), name="signout"),
    path("password/reset/", account.PasswordReset.as_view(), name="password_reset"),
    path("password/reset/sent/", account.auth_views.PasswordResetDoneView.as_view(template_name="account/reset_sent.html"), name="password_reset_done"),
    path("password/reset/<uidb64>/<token>/", account.PasswordResetConfirm.as_view(), name="password_reset_confirm"),
    path("password/", account.PasswordChange.as_view(), name="password_change"),
    path("", account.profile, name="profile"),
], "account")

console_patterns = ([
    path("", console.home, name="home"),
    path("apply/", console.apply, name="apply"),
    path("publisher/", console.publisher_profile, name="publisher"),
    path("terms/accept/", console.accept_terms, name="accept_terms"),
    path("apps/new/", console.app_new, name="app_new"),
    path("apps/<appid:app_id>/", console.app_overview, name="app"),
    path("apps/<appid:app_id>/listing/", console.app_listing, name="listing"),
    path("apps/<appid:app_id>/screenshots/", console.screenshot_add, name="screenshot_add"),
    path("apps/<appid:app_id>/screenshots/<int:pk>/delete/", console.screenshot_delete, name="screenshot_delete"),
    path("apps/<appid:app_id>/releases/new/", console.release_new, name="release_new"),
    path("apps/<appid:app_id>/releases/<int:pk>/", console.release_detail, name="release"),
    path("apps/<appid:app_id>/releases/<int:pk>/packages/", console.package_add, name="package_add"),
    path("apps/<appid:app_id>/releases/<int:pk>/packages/<int:package_pk>/delete/", console.package_delete, name="package_delete"),
    path("apps/<appid:app_id>/releases/<int:pk>/<slug:action>/", console.release_action, name="release_action"),
], "console")

operator_patterns = ([
    path("", operator.home, name="home"),
    path("publishers/", operator.publishers, name="publishers"),
    path("publishers/<slug:slug>/", operator.publisher, name="publisher"),
    path("releases/<int:pk>/", operator.release, name="release"),
    path("packages/<int:pk>/", operator.package, name="package"),
    path("apps/", operator.apps, name="apps"),
    path("apps/<appid:app_id>/<slug:action>/", operator.app_action, name="app_action"),
    path("reports/<int:pk>/", operator.report, name="report"),
    path("publish/", operator.publish, name="publish"),
    path("audit/", operator.audit, name="audit"),
], "operator")

api_patterns = ([
    path("catalog.json", api.catalog_json, name="catalog"),
    path("catalog.json.sig", api.catalog_signature, name="catalog_signature"),
    path("apps", api.apps, name="apps"),
    path("apps/<appid:app_id>", api.app, name="app"),
    path("categories", api.categories, name="categories"),
    path("publishers/<slug:slug>", api.publisher, name="publisher"),
    path("publish/key", api.publish_key, name="publish_key"),
    path("terms/current", api.terms_current, name="terms_current"),
    path("reports", api.reports, name="reports"),
    path("reports/<str:public_id>/outcome", api.report_outcome, name="report_outcome"),
], "api")

urlpatterns = [
    path("", include(store_patterns)),
    path("account/", include(account_patterns)),
    path("console/", include(console_patterns)),
    path("operator/", include(operator_patterns)),
    path("v1/", include(api_patterns)),
    path("healthz", api.healthz, name="healthz"),
    path("admin/", admin.site.urls),
]

if settings.DEBUG:
    # A developer's machine has no web server in front: the pictures and the
    # published packages from the folders nginx serves in production
    # (deploy/nginx.conf). restricted/ is served by nobody here either.
    urlpatterns += [
        path("media/<path:path>", serve, {"document_root": settings.MEDIA_ROOT}),
        path("v1/packages/<path:path>", serve, {"document_root": settings.PUBLISH_DIR / "v1" / "packages"}),
    ]
