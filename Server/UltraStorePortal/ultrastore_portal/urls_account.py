"""The addresses of the account's own host (ULTRASTORE_PORTAL_ACCOUNT_URL,
https://account.ultraos.eu): the account's pages at its root and the OpenID
Connect endpoints, and nothing of the store. A request to that host is
answered from here (identity/hosts.py, AccountHostMiddleware).

Version: 0.4.0
"""

from django.urls import include, path

from .api import views as api
from .urls import account_patterns, oidc_patterns

urlpatterns = [
    path("", include(oidc_patterns)),
    path("", include(account_patterns)),
    path("healthz", api.healthz, name="healthz"),
]
