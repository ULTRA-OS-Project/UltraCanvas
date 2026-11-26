"""What every page knows: the store's categories and the signed-in person's
publisher."""

from django.conf import settings
from django.utils.functional import SimpleLazyObject

from ..identity import hosts
from ..store import constants, services


def portal(request):
    return {
        "store_categories": constants.CATEGORIES,
        "store_download_url": settings.STORE_DOWNLOAD_URL,
        "base_url": settings.BASE_URL,
        # The account's own host has its own top and bottom of the page
        # (templates/base.html), with nothing of the store in them.
        "account_site": hosts.split() and hosts.on_account_host(request),
        "brand": hosts.brand(),
        "store_root": hosts.store_root(),
        "promote_on": settings.PROMOTE,
        "affiliates_on": settings.AFFILIATES,
        "my_publisher": SimpleLazyObject(lambda: services.publisher_of(request.user)),
        "my_role": SimpleLazyObject(lambda: getattr(services.membership_of(request.user), "role", "")),
    }
