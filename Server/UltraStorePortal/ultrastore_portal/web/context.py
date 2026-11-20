"""What every page knows: the store's categories and the signed-in person's
publisher."""

from django.conf import settings
from django.utils.functional import SimpleLazyObject

from ..store import constants, services


def portal(request):
    return {
        "store_categories": constants.CATEGORIES,
        "store_download_url": settings.STORE_DOWNLOAD_URL,
        "base_url": settings.BASE_URL,
        "my_publisher": SimpleLazyObject(lambda: services.publisher_of(request.user)),
    }
