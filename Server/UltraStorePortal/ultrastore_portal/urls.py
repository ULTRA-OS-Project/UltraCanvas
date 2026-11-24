"""Every address of the store, all under the one root - and the account's,
which are here too while the account has no host of its own, and in
urls_account.py when it has (identity/hosts.py).

Version: 0.4.0 - the OpenID Connect endpoints; the account's pages on the
                 account's host; sso/callback
Version: 0.3.0 - console: icon/, packages/<n>/edit/
Version: 0.1.0
"""

from django.conf import settings
from django.contrib import admin
from django.urls import include, path, register_converter
from django.views.static import serve

from .api import billing as buying
from .api import views as api
from .identity import sso
from .identity import views as oidc
from .identity.hosts import account_page
from .web import account, console, operator, pay, storefront


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
    path("apps/<appid:app_id>/buy/", pay.buy, name="buy"),
    path("reports/<str:public_id>/", storefront.report_outcome, name="report_outcome"),
    path("publisher/<slug:slug>/", storefront.publisher_page, name="publisher"),
    path("terms/", storefront.terms, name="terms"),
    path("robots.txt", storefront.robots, name="robots"),
    path("sitemap.xml", storefront.sitemap, name="sitemap"),
], "store")

account_patterns = ([
    path("signup/", account_page(account.signup), name="signup"),
    path("verify/<str:token>/", account_page(account.verify), name="verify"),
    path("resend/", account_page(account.resend), name="resend"),
    path("signin/", account_page(account.SignIn.as_view(), on_store=sso.start), name="signin"),
    path("signout/", account_page(account.SignOut.as_view(), on_store=sso.signout), name="signout"),
    path("password/reset/", account_page(account.PasswordReset.as_view()), name="password_reset"),
    path("password/reset/sent/", account_page(account.auth_views.PasswordResetDoneView.as_view(template_name="account/reset_sent.html")), name="password_reset_done"),
    path("password/reset/<uidb64>/<token>/", account_page(account.PasswordResetConfirm.as_view()), name="password_reset_confirm"),
    path("password/", account_page(account.PasswordChange.as_view()), name="password_change"),
    path("services/<slug:client_id>/forget/", account_page(account.forget_service), name="forget_service"),
    path("", account_page(account.profile), name="profile"),
], "account")

# OpenID Connect: where another service signs a person in through the
# account (identity/views.py). They exist at the issuer, which is the
# account's host when it has one.
oidc_patterns = ([
    path(".well-known/openid-configuration", oidc.discovery, name="discovery"),
    path("oauth/jwks", oidc.jwks, name="jwks"),
    path("oauth/authorize", oidc.authorize, name="authorize"),
    path("oauth/authorize/decide", oidc.decide, name="decide"),
    path("oauth/token", oidc.token, name="token"),
    path("oauth/userinfo", oidc.userinfo, name="userinfo"),
    path("oauth/introspect", oidc.introspect, name="introspect"),
    path("oauth/revoke", oidc.revoke, name="revoke"),
    path("oauth/logout", oidc.logout, name="logout"),
    path("oauth/logout/confirm", oidc.logout_confirm, name="logout_confirm"),
], "oidc")

pay_patterns = ([
    path("pay/<str:public_id>/", pay.handoff, name="handoff"),
    path("pay/<str:public_id>/test/", pay.test_pay, name="test"),
    path("pay/<str:public_id>/return/", pay.returned, name="return"),
    path("pay/<str:public_id>/cancel/", pay.cancelled, name="cancel"),
    path("orders/<str:public_id>/", pay.order_page, name="order"),
    path("orders/<str:public_id>/licence.ulic", pay.order_licence, name="licence"),
    path("orders/<str:public_id>/download/<int:pk>/", pay.order_download, name="download"),
    path("orders/<str:public_id>/unsubscribe/", pay.order_unsubscribe, name="unsubscribe"),
], "pay")

console_patterns = ([
    path("", console.home, name="home"),
    path("sales/", console.sales, name="sales"),
    path("apply/", console.apply, name="apply"),
    path("publisher/", console.publisher_profile, name="publisher"),
    path("terms/accept/", console.accept_terms, name="accept_terms"),
    path("apps/new/", console.app_new, name="app_new"),
    path("apps/<appid:app_id>/", console.app_overview, name="app"),
    path("apps/<appid:app_id>/listing/", console.app_listing, name="listing"),
    path("apps/<appid:app_id>/icon/", console.icon_upload, name="icon_upload"),
    path("apps/<appid:app_id>/screenshots/", console.screenshot_add, name="screenshot_add"),
    path("apps/<appid:app_id>/screenshots/<int:pk>/delete/", console.screenshot_delete, name="screenshot_delete"),
    path("apps/<appid:app_id>/releases/new/", console.release_new, name="release_new"),
    path("apps/<appid:app_id>/releases/<int:pk>/", console.release_detail, name="release"),
    path("apps/<appid:app_id>/releases/<int:pk>/packages/", console.package_add, name="package_add"),
    path("apps/<appid:app_id>/releases/<int:pk>/packages/<int:package_pk>/edit/", console.package_edit, name="package_edit"),
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
    path("orders/", operator.orders, name="orders"),
    path("orders/<int:pk>/", operator.order, name="order"),
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
    path("checkout", buying.checkout, name="checkout"),
    path("orders/<str:public_id>", buying.order, name="order"),
    path("orders/<str:public_id>/receipt", buying.receipt, name="receipt"),
    path("orders/<str:public_id>/download", buying.download, name="download"),
    path("orders/<str:public_id>/subscription/cancel", buying.subscription_cancel, name="subscription_cancel"),
    path("licences/<str:licence_id>", buying.licence, name="licence"),
    path("keys", buying.keys, name="keys"),
    path("skrill/status", buying.skrill_status, name="skrill_status"),
    path("skrill/recurring", buying.skrill_recurring, name="skrill_recurring"),
], "api")

urlpatterns = [
    path("", include(store_patterns)),
    path("", include(pay_patterns)),
    path("account/", include(account_patterns)),
    path("", include(oidc_patterns)),
    path("sso/callback", sso.callback, name="sso_callback"),
    path("console/", include(console_patterns)),
    path("operator/", include(operator_patterns)),
    path("v1/", include(api_patterns)),
    path("healthz", api.healthz, name="healthz"),
    path("admin/", admin.site.urls),
]

if settings.DEBUG:
    # A developer's machine has no web server in front: the pictures and the
    # published packages from the folders nginx serves in production
    # (deploy/nginx.conf), restricted/ behind the same signed link.
    urlpatterns += [
        path("media/<path:path>", serve, {"document_root": settings.MEDIA_ROOT}),
        path("v1/packages/<path:path>", serve, {"document_root": settings.PUBLISH_DIR / "v1" / "packages"}),
        path("v1/restricted/<path:path>", pay.restricted_file),
    ]
