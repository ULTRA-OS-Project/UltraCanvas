"""The portal end to end, through its pages and its JSON service: a person
signs up, applies for a publisher, the operator approves, an app is listed,
a release uploaded, reviewed and published, and the signed catalog carries
it."""

from __future__ import annotations

import io
import json
import re
import shutil
import tempfile
import zipfile
from pathlib import Path

from django.conf import settings
from django.contrib.auth import get_user_model
from django.core import mail
from django.core.cache import cache
from django.core.files.uploadedfile import SimpleUploadedFile
from django.core.management import call_command
from django.test import TestCase, override_settings
from PIL import Image

from ultrastore_portal.store import catalog, services
from ultrastore_portal.store.models import App, AuditEntry, CatalogSnapshot, Package, Publisher, Release, Report, TermsVersion

User = get_user_model()
PASSWORD = "a-long-enough-passphrase"
ROOT = "https://store.ultraos.eu"
# What the page's script sends with a form it draws the answer of itself (static/portal/portal.js).
IN_PLACE = {"X-Requested-With": "XMLHttpRequest"}


def picture(width: int = 256, height: int = 256, fmt: str = "PNG", name: str = "icon.png") -> SimpleUploadedFile:
    out = io.BytesIO()
    Image.new("RGB", (width, height), "#2563EB").save(out, fmt)
    return SimpleUploadedFile(name, out.getvalue(), content_type="image/" + fmt.lower())


def archive(entry: str = "MyApp/MyApp", name: str = "myapp-linux.zip", salt: str = "") -> SimpleUploadedFile:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr(entry, "#!/bin/sh\necho hello " + salt + "\n")
    return SimpleUploadedFile(name, out.getvalue(), content_type="application/zip")


class PortalCase(TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp(prefix="ultrastore-portal-test-"))
        self.addCleanup(shutil.rmtree, self.folder, True)
        override = override_settings(
            MEDIA_ROOT=self.folder / "media", PACKAGE_DIR=self.folder / "packages", PUBLISH_DIR=self.folder / "public",
            BASE_URL=ROOT, CATALOG_URL=ROOT + "/v1", MARKET_URL=ROOT, SIGNING_SEED="", SEQUENCE_FLOOR=0)
        override.enable()
        self.addCleanup(override.disable)
        cache.clear()
        self.operator = User.objects.create_superuser("operator@example.org", PASSWORD, name="Olivia")
        TermsVersion.objects.create(version=1, text="Be good.")

    # -- helpers ---------------------------------------------------------
    def post(self, url, data=None, **kwargs):
        """A POST whose after-commit work (the catalog's publish) runs."""
        with self.captureOnCommitCallbacks(execute=True):
            return self.client.post(url, data or {}, **kwargs)

    def sign_in(self, email):
        self.client.logout()
        self.assertTrue(self.client.login(email=email, password=PASSWORD))

    def make_publisher(self, email="dev@example.org", slug="acme", approve=True) -> Publisher:
        user = User.objects.create_user(email, PASSWORD, name="Dana", email_verified=True)
        publisher = services.apply_for_publisher(user, Publisher(slug=slug, name=slug.title(), support_email=email))
        if approve:
            with self.captureOnCommitCallbacks(execute=True):
                services.decide_publisher(publisher, self.operator, "approve")
        return publisher

    def make_app(self, publisher, app_id="org.acme.myapp", **fields) -> App:
        fields.setdefault("category", "utilities")
        app = App(app_id=app_id, publisher=publisher, name=fields.pop("name", "MyApp"), summary="A fine app", description="It does things.", **fields)
        app.icon.save("icon.png", picture(), save=False)
        app.save()
        shot = app.screenshots.create(position=1)
        shot.image.save("shot.png", picture(640, 400), save=True)
        return app

    def make_release(self, app, version="1.0.0", *, publish=True, salt="", **fields) -> Release:
        owner = app.publisher.members.first().user
        release = Release.objects.create(app=app, version=version, notes="First.", **fields)
        from ultrastore_portal.store.packages import store_upload
        stored = store_upload(archive(salt=salt or f"{app.app_id}-{version}"))
        services.add_package(release, Package(os="linux", arch="x86_64", entry="MyApp/MyApp", sha256=stored.sha256, size=stored.size,
                                              format=stored.format), stored.names, owner)
        if publish:
            with self.captureOnCommitCallbacks(execute=True):
                services.submit_release(release, owner)
                services.decide_release(release, self.operator, "approve")
        return release

    def catalog(self) -> dict:
        return json.loads(self.client.get("/v1/catalog.json").content)


class AccountTests(PortalCase):
    def test_sign_up_proves_the_address_before_the_first_sign_in(self):
        response = self.client.post("/account/signup/", {"name": "Dana", "email": "Dana@Example.org", "password": PASSWORD, "password_again": PASSWORD})
        self.assertContains(response, "Check your mail")
        user = User.objects.get(email="dana@example.org")
        self.assertFalse(user.email_verified)
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn(ROOT + "/account/verify/", mail.outbox[0].body)
        # Not before the link was followed.
        response = self.client.post("/account/signin/", {"username": "dana@example.org", "password": PASSWORD})
        self.assertContains(response, "not confirmed yet")
        link = [line for line in mail.outbox[0].body.splitlines() if "/account/verify/" in line][0].replace(ROOT, "")
        response = self.client.get(link)
        self.assertRedirects(response, "/console/", fetch_redirect_response=False)
        user.refresh_from_db()
        self.assertTrue(user.email_verified)

    def test_sign_up_tells_a_stranger_nothing_about_an_existing_account(self):
        User.objects.create_user("taken@example.org", PASSWORD, email_verified=True)
        response = self.client.post("/account/signup/", {"name": "X", "email": "taken@example.org", "password": PASSWORD, "password_again": PASSWORD})
        self.assertContains(response, "Check your mail")
        self.assertEqual(User.objects.filter(email="taken@example.org").count(), 1)
        self.assertIn("already has one", mail.outbox[0].body)

    def test_a_short_password_and_a_bad_link_are_refused(self):
        response = self.client.post("/account/signup/", {"name": "X", "email": "x@example.org", "password": "short", "password_again": "short"})
        self.assertContains(response, "too short")
        self.assertEqual(self.client.get("/account/verify/not-a-token/").status_code, 400)

    def test_a_password_is_eight_to_sixty_four_characters(self):
        def sign_up(name, password):
            return self.client.post("/account/signup/", {"name": "X", "email": f"{name}@example.org", "password": password, "password_again": password})

        self.assertContains(self.client.get("/account/signup/"), "8 to 64 characters.")
        self.assertContains(sign_up("seven", "kq7#Wz!"), "too short")
        self.assertContains(sign_up("eight", "kq7#Wz!p"), "Check your mail")
        # Characters, not bytes: 64 of them are a password although they are 133 bytes of UTF-8.
        longest = "kq7#Wz!p-" + "ä€" * 23 + "-Zr4$mLx9"
        self.assertEqual((len(longest), len(longest.encode())), (64, 133))
        self.assertContains(sign_up("longest", longest), "Check your mail")
        self.assertTrue(User.objects.get(email="longest@example.org").check_password(longest))
        self.assertContains(sign_up("longer", longest + "x"), "at most 64 characters")
        self.assertFalse(User.objects.filter(email="longer@example.org").exists())
        # The rule is for choosing a password: one older than the rule still signs in, and is changed under it.
        old = "an-older-and-much-longer-passphrase-" * 3
        User.objects.create_user("old@example.org", old, email_verified=True)
        self.assertRedirects(self.client.post("/account/signin/", {"username": "old@example.org", "password": old}), "/console/", fetch_redirect_response=False)
        change = {"old_password": old, "new_password1": longest + "x", "new_password2": longest + "x"}
        self.assertContains(self.client.post("/account/password/", change), "at most 64 characters")
        self.assertContains(self.client.get("/account/password/"), "at most 64 characters")   # said before it is refused
        self.client.post("/account/password/", {**change, "new_password1": longest, "new_password2": longest})
        self.assertTrue(User.objects.get(email="old@example.org").check_password(longest))

    def test_password_reset_mails_a_link_under_the_root(self):
        User.objects.create_user("dana@example.org", PASSWORD, email_verified=True)
        self.client.post("/account/password/reset/", {"email": "dana@example.org"})
        self.assertIn(ROOT + "/account/password/reset/", mail.outbox[0].body)


class StaticFilesTests(PortalCase):
    def test_a_page_names_its_static_files_by_their_content(self):
        """As the portal runs in production: collectstatic lays every static
        file out under a name with its content's hash, and the pages link
        those - never the bare name, which a browser may hold from the
        release before (a new page drawn with the old portal.css)."""
        static_root = self.folder / "static"
        hashed = {**settings.STORAGES, "staticfiles": {"BACKEND": settings.HASHED_STATIC_FILES}}
        with override_settings(STATIC_ROOT=static_root, STORAGES=hashed):
            call_command("collectstatic", interactive=False, verbosity=0)   # refuses a file that names one it cannot find
            self.make_publisher()
            self.sign_in("dev@example.org")
            self.post("/console/apps/new/", {"name": "MyApp", "app_id": "org.acme.myapp", "kind": "app", "category": "utilities"})
            page = self.client.get("/console/apps/org.acme.myapp/listing/").content.decode()
            for name, ending in (("portal/portal", "css"), ("portal/portal", "js"), ("portal/vendor/bootstrap/bootstrap.min", "css"),
                                 ("portal/vendor/bootstrap/bootstrap.bundle.min", "js"), ("portal/vendor/dropzone/dropzone.min", "js")):
                self.assertNotIn(f"/static/{name}.{ending}", page)
                linked = re.search(rf"/static/({re.escape(name)}\.[0-9a-f]{{12}}\.{ending})\"", page)
                self.assertIsNotNone(linked, name)
                source = Path(settings.STATICFILES_DIRS[0]) / f"{name}.{ending}"
                self.assertEqual((static_root / linked.group(1)).read_bytes(), source.read_bytes())

    def test_a_developers_machine_and_the_tests_need_no_manifest(self):
        self.assertEqual(settings.STORAGES["staticfiles"]["BACKEND"], settings.PLAIN_STATIC_FILES)
        self.assertContains(self.client.get("/"), "/static/portal/portal.css")


class PublisherTests(PortalCase):
    def test_a_person_applies_and_the_operator_decides(self):
        User.objects.create_user("dev@example.org", PASSWORD, email_verified=True)
        self.sign_in("dev@example.org")
        self.assertRedirects(self.client.get("/console/"), "/console/apply/")
        data = {"name": "Acme Software", "slug": "acme", "website": "https://acme.example", "support_email": "dev@example.org", "description": "We make tools."}
        self.assertContains(self.client.post("/console/apply/", data), "This field is required")   # the terms
        self.assertRedirects(self.post("/console/apply/", {**data, "accept_terms": "on"}), "/console/")
        publisher = Publisher.objects.get(slug="acme")
        self.assertEqual((publisher.state, publisher.terms_version), (Publisher.APPLIED, 1))
        self.assertContains(self.client.get("/console/"), "with the operator")
        self.assertRedirects(self.client.get("/console/apps/new/"), "/console/")   # nothing to publish before the decision
        self.assertEqual(self.client.get("/operator/").status_code, 403)

        self.sign_in("operator@example.org")
        self.assertContains(self.client.get("/operator/"), "Acme Software")
        mail.outbox.clear()
        self.post("/operator/publishers/acme/", {"action": "reject", "message": ""})
        self.assertEqual(Publisher.objects.get(slug="acme").state, Publisher.APPLIED)   # a rejection needs its reason
        self.post("/operator/publishers/acme/", {"action": "approve", "message": "Welcome."})
        self.assertEqual(Publisher.objects.get(slug="acme").state, Publisher.APPROVED)
        self.assertIn("dev@example.org", mail.outbox[0].to)
        self.assertTrue(AuditEntry.objects.filter(action="publisher.approve", target="acme", actor=self.operator).exists())

    def test_one_publisher_per_account_and_ids_are_checked(self):
        self.make_publisher()
        self.sign_in("dev@example.org")
        self.assertRedirects(self.client.get("/console/apply/"), "/console/")
        User.objects.create_user("other@example.org", PASSWORD, email_verified=True)
        self.sign_in("other@example.org")
        response = self.client.post("/console/apply/", {"name": "Copy", "slug": "acme", "support_email": "o@example.org", "accept_terms": "on"})
        self.assertContains(response, "already exists")
        response = self.client.post("/console/apply/", {"name": "Copy", "slug": "Not A Slug", "support_email": "o@example.org", "accept_terms": "on"})
        self.assertContains(response, "Lower-case letters")


class PublishingTests(PortalCase):
    def test_from_a_new_app_to_the_store_through_the_pages(self):
        self.make_publisher()
        self.sign_in("dev@example.org")
        self.assertRedirects(self.client.post("/console/apps/new/", {"name": "MyApp", "app_id": "org.acme.myapp", "kind": "app", "category": "utilities"}),
                             "/console/apps/org.acme.myapp/")
        self.assertContains(self.client.get("/console/apps/org.acme.myapp/"), "no icon")
        listing = {"name": "MyApp", "summary": "A fine app", "description": "It does things.", "source_language": "en", "category": "utilities",
                   "tags": "tools, fine", "pricing_model": "free", "currency": "EUR", "trial_days": "0", "updates_period": "always",
                   "visibility": "listed", "lifecycle": "active", "age_rating": ""}
        self.assertRedirects(self.post("/console/apps/org.acme.myapp/listing/", listing), "/console/apps/org.acme.myapp/listing/")
        # The pictures have addresses of their own; without the page's script each answers with the listing page.
        response = self.post("/console/apps/org.acme.myapp/icon/", {"icon": picture(300, 200)}, follow=True)
        self.assertContains(response, "An icon is square")
        self.assertRedirects(self.post("/console/apps/org.acme.myapp/icon/", {"icon": picture()}), "/console/apps/org.acme.myapp/listing/")
        self.post("/console/apps/org.acme.myapp/screenshots/", {"image": picture(640, 400, name="shot.png")})
        app = App.objects.get(app_id="org.acme.myapp")
        self.assertTrue(app.icon.name.endswith(".png"))
        self.assertEqual(app.screenshots.count(), 1)

        response = self.client.post("/console/apps/org.acme.myapp/releases/new/", {"version": "1.0.0", "channel": "stable", "notes": "First.", "depends_text": ""})
        release = Release.objects.get(app=app)
        self.assertRedirects(response, f"/console/apps/org.acme.myapp/releases/{release.pk}/")
        base = f"/console/apps/org.acme.myapp/releases/{release.pk}/"
        # Nothing to review without a package.
        self.post(base + "submit/")
        self.assertEqual(Release.objects.get(pk=release.pk).state, Release.DRAFT)
        # A file that is not what its name says, an entry the archive lacks, a format the store cannot unpack.
        self.post(base + "packages/", {"file": SimpleUploadedFile("myapp.zip", b"not a zip"), "os": "linux", "arch": "x86_64", "entry": "MyApp/MyApp"})
        self.post(base + "packages/", {"file": archive(), "os": "linux", "arch": "x86_64", "entry": "Other/Thing"})
        self.post(base + "packages/", {"file": SimpleUploadedFile("myapp.rar", b"Rar!"), "os": "linux", "arch": "x86_64", "entry": "MyApp/MyApp"})
        self.assertEqual(release.packages.count(), 0)
        self.assertEqual(list((self.folder / "packages").glob("*")), [])
        self.post(base + "packages/", {"file": archive(), "os": "linux", "arch": "x86_64", "entry": "MyApp/MyApp", "variant": "glibc-2.31", "min_os": ""})
        package = release.packages.get()
        self.assertEqual((package.format, len(package.sha256)), ("zip", 64))
        self.assertTrue((self.folder / "packages" / package.file_name).is_file())

        self.post(base + "submit/")
        self.assertEqual(Release.objects.get(pk=release.pk).state, Release.SUBMITTED)
        # In review: not in the store, the bytes not public, the release closed to edits.
        self.assertEqual(self.client.get("/apps/org.acme.myapp/").status_code, 404)
        self.assertEqual(self.catalog()["apps"], [])
        self.assertFalse((self.folder / "public" / "v1" / "packages" / package.file_name).exists())
        self.post(base + "packages/", {"file": archive(name="myapp-win.zip", salt="w"), "os": "windows", "arch": "x86_64", "entry": "MyApp/MyApp"})
        self.assertEqual(release.packages.count(), 1)
        self.assertEqual(self.client.get(f"/operator/releases/{release.pk}/").status_code, 403)

        self.sign_in("operator@example.org")
        self.assertContains(self.client.get(f"/operator/releases/{release.pk}/"), "Approve and publish")
        build = self.client.get(f"/operator/packages/{package.pk}/")
        self.assertEqual(len(b"".join(build.streaming_content)), package.size)
        mail.outbox.clear()
        self.post(f"/operator/releases/{release.pk}/", {"action": "approve", "message": "Looks good."})
        self.assertEqual(Release.objects.get(pk=release.pk).state, Release.PUBLISHED)
        self.assertIn("is published", mail.outbox[0].subject)

        self.client.logout()
        self.assertContains(self.client.get("/apps/org.acme.myapp/"), "ultrastore://app/org.acme.myapp")
        self.assertContains(self.client.get("/"), "MyApp")
        self.assertContains(self.client.get("/apps/?q=fine"), "MyApp")
        self.assertNotContains(self.client.get("/apps/?q=nothing-like-it"), "A fine app")
        self.assertContains(self.client.get("/category/utilities/"), "MyApp")
        self.assertContains(self.client.get("/publisher/acme/"), "MyApp")
        self.assertIn(ROOT + "/apps/org.acme.myapp/", self.client.get("/sitemap.xml").content.decode())
        entry = self.catalog()["apps"][0]
        self.assertEqual(entry["id"], "org.acme.myapp")
        self.assertEqual(entry["releases"][0]["packages"][0]["url"], f"{ROOT}/v1/packages/{package.file_name}")
        self.assertTrue((self.folder / "public" / "v1" / "packages" / package.file_name).is_file())
        response = self.client.get(f"/apps/org.acme.myapp/download/{package.pk}/")
        self.assertRedirects(response, f"/v1/packages/{package.file_name}", fetch_redirect_response=False)
        self.assertEqual(App.objects.get(pk=app.pk).downloads, 1)

    def test_the_review_can_ask_for_changes_or_reject_with_a_reason(self):
        publisher = self.make_publisher()
        app = self.make_app(publisher)
        release = self.make_release(app, publish=False)
        owner = publisher.members.first().user
        services.submit_release(release, owner)
        with self.assertRaises(services.Refused):
            services.decide_release(release, self.operator, "request_changes", "")
        services.decide_release(release, self.operator, "request_changes", "The entry does not start.")
        release.refresh_from_db()
        self.assertEqual((release.state, release.editable), (Release.CHANGES, True))
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get(f"/console/apps/{app.app_id}/releases/{release.pk}/"), "The entry does not start.")
        services.submit_release(release, owner)
        services.decide_release(release, self.operator, "reject", "Not this one.")
        release.refresh_from_db()
        self.assertEqual(release.state, Release.REJECTED)
        with self.assertRaises(services.Refused):
            services.decide_release(release, self.operator, "approve")
        sha256 = release.packages.get().sha256
        services.delete_release(release, owner)
        self.assertFalse((self.folder / "packages" / f"{sha256}.zip").exists())

    def test_a_dependency_must_be_published_first(self):
        publisher = self.make_publisher()
        runtime = self.make_app(publisher, "org.acme.runtime", kind="runtime", name="Runtime")
        app = self.make_app(publisher)
        release = self.make_release(app, publish=False, depends=[{"id": "org.acme.runtime", "version": "0.9.1"}])
        owner = publisher.members.first().user
        services.submit_release(release, owner)
        with self.assertRaisesMessage(services.Refused, "org.acme.runtime@0.9.1"):
            services.decide_release(release, self.operator, "approve")
        self.make_release(runtime, "0.9.1")
        with self.captureOnCommitCallbacks(execute=True):
            services.decide_release(release, self.operator, "approve")
        body = self.catalog()
        self.assertEqual([c["id"] for c in body["components"]], ["org.acme.runtime"])
        self.assertEqual(body["apps"][0]["releases"][0]["depends"], [{"id": "org.acme.runtime", "version": "0.9.1"}])
        # A component is in the catalog, not on the shelves.
        self.assertNotContains(self.client.get("/apps/"), "Runtime")

    def test_a_publisher_sees_only_its_own_apps(self):
        app = self.make_app(self.make_publisher())
        release = self.make_release(app, publish=False)
        self.make_publisher("other@example.org", "other")
        self.sign_in("other@example.org")
        for url in (f"/console/apps/{app.app_id}/", f"/console/apps/{app.app_id}/listing/", f"/console/apps/{app.app_id}/releases/{release.pk}/"):
            self.assertEqual(self.client.get(url).status_code, 404)
        self.assertEqual(self.client.post(f"/console/apps/{app.app_id}/releases/{release.pk}/delete/").status_code, 404)
        response = self.client.post("/console/apps/new/", {"name": "Copy", "app_id": app.app_id, "kind": "app", "category": "utilities"})
        self.assertContains(response, "already exists")
        self.client.logout()
        self.assertRedirects(self.client.get("/console/apps/new/"), "/account/signin/?next=/console/apps/new/")

    def test_new_terms_must_be_accepted_before_the_next_submission(self):
        publisher = self.make_publisher()
        app = self.make_app(publisher)
        release = self.make_release(app, publish=False)
        TermsVersion.objects.create(version=2, text="Be better.")
        owner = publisher.members.first().user
        with self.assertRaisesMessage(services.Refused, "terms"):
            services.submit_release(release, owner)
        self.sign_in("dev@example.org")
        self.assertContains(self.client.get("/console/"), "Accept the terms")
        self.post("/console/terms/accept/")
        services.submit_release(Release.objects.get(pk=release.pk), owner)


class ConsoleTests(PortalCase):
    """The console's pages as a publisher works on them: nothing to release
    before the listing is filled in, pictures and packages changed in place,
    fields that apply only now and then."""

    def setUp(self):
        super().setUp()
        self.publisher = self.make_publisher()
        self.sign_in("dev@example.org")

    def in_place(self, url, data=None):
        return self.post(url, data, headers=IN_PLACE)

    def test_no_release_before_the_listing_has_what_a_review_needs(self):
        self.post("/console/apps/new/", {"name": "MyApp", "app_id": "org.acme.myapp", "kind": "app", "category": "utilities"})
        new_release = "/console/apps/org.acme.myapp/releases/new/"
        overview = self.client.get("/console/apps/org.acme.myapp/")
        self.assertNotContains(overview, new_release)
        self.assertContains(overview, "The listing has no summary.")
        self.assertContains(overview, "/console/apps/org.acme.myapp/listing/")
        # The address itself sends back to the listing, and makes nothing.
        self.assertRedirects(self.client.get(new_release), "/console/apps/org.acme.myapp/listing/")
        self.assertRedirects(self.client.post(new_release, {"version": "1.0.0", "channel": "stable"}), "/console/apps/org.acme.myapp/listing/")
        self.assertEqual(Release.objects.count(), 0)

        app = App.objects.get(app_id="org.acme.myapp")
        app.summary, app.description = "A fine app", "It does things."
        app.icon.save("icon.png", picture(), save=True)
        self.assertContains(self.client.get("/console/apps/org.acme.myapp/listing/"), "The listing has no screenshot.")
        self.assertNotContains(self.client.get("/console/apps/org.acme.myapp/"), new_release)   # one thing missing is enough
        shot = app.screenshots.create(position=1)
        shot.image.save("shot.png", picture(640, 400), save=True)
        self.assertContains(self.client.get("/console/apps/org.acme.myapp/"), new_release)
        self.assertContains(self.client.get("/console/apps/org.acme.myapp/listing/"), "Create the first release")
        self.assertEqual(self.client.get(new_release).status_code, 200)

    def test_a_runtime_needs_only_its_summary_and_no_category(self):
        self.post("/console/apps/new/", {"name": "Runtime", "app_id": "org.acme.runtime", "kind": "runtime", "category": "games"})
        runtime = App.objects.get(app_id="org.acme.runtime")
        self.assertEqual(runtime.category, "")   # asked of an app only
        self.assertNotContains(self.client.get("/console/apps/org.acme.runtime/"), "releases/new/")
        runtime.summary = "What the apps run on"
        runtime.save()
        self.assertContains(self.client.get("/console/apps/org.acme.runtime/"), "releases/new/")

    def test_pictures_are_saved_where_they_are_chosen(self):
        self.post("/console/apps/new/", {"name": "MyApp", "app_id": "org.acme.myapp", "kind": "app", "category": "utilities"})
        base = "/console/apps/org.acme.myapp/"
        page = self.client.get(base + "listing/")
        self.assertContains(page, f'data-upload="{base}icon/"')
        self.assertContains(page, f'data-upload="{base}screenshots/"')

        # The icon: refused with the reason, saved with the page's parts to draw again.
        refused = self.in_place(base + "icon/", {"icon": picture(300, 200)})
        self.assertEqual((refused.status_code, refused.json()["ok"]), (400, False))
        self.assertIn("An icon is square", refused.json()["error"])
        self.assertIn("PNG, a JPEG or a WebP", self.in_place(base + "icon/", {"icon": picture(256, 256, "GIF", "icon.gif")}).json()["error"])
        self.assertFalse(App.objects.get(app_id="org.acme.myapp").icon)
        saved = self.in_place(base + "icon/", {"icon": picture()})
        self.assertEqual((saved.status_code, saved.json()["ok"]), (200, True))
        app = App.objects.get(app_id="org.acme.myapp")
        self.assertIn(app.icon.url, saved.json()["fragments"]["#icon"])
        self.assertNotIn("no icon", saved.json()["fragments"]["#listing-problems"])
        self.assertIn("no screenshot", saved.json()["fragments"]["#listing-problems"])
        self.assertTrue(AuditEntry.objects.filter(action="app.edit", target="org.acme.myapp", detail__fields=["icon"]).exists())
        self.assertIn("Choose a picture", self.in_place(base + "icon/", {}).json()["error"])   # the icon it has is no upload

        # Screenshots: each its own request, up to the limit, removed the same way.
        with override_settings(MAX_SCREENSHOTS=2):
            self.assertEqual(self.in_place(base + "screenshots/", {"image": picture(200, 200)}).status_code, 400)   # too small
            first = self.in_place(base + "screenshots/", {"image": picture(640, 400)})
            shot = app.screenshots.get()
            self.assertIn(shot.image.url, first.json()["fragments"]["#shots"])
            self.assertIn('data-count="1" data-max="2"', first.json()["fragments"]["#shots"])
            self.assertNotIn("no screenshot", first.json()["fragments"]["#listing-problems"])
            self.in_place(base + "screenshots/", {"image": picture(800, 500, "JPEG", "two.jpg")})
            third = self.in_place(base + "screenshots/", {"image": picture(640, 400)})
            self.assertEqual(third.status_code, 400)
            self.assertIn("at most 2", third.json()["error"])
            self.assertEqual([s.position for s in app.screenshots.all()], [1, 2])
            removed = self.in_place(f"{base}screenshots/{shot.pk}/delete/")
            self.assertNotIn(shot.image.url, removed.json()["fragments"]["#shots"])
            self.assertEqual(app.screenshots.count(), 1)
        # Nothing of this left a message for the next page.
        self.assertNotContains(self.client.get(base + "listing/"), "The icon is saved.")

    def test_a_picture_change_of_a_public_app_is_in_the_next_catalog(self):
        app = self.make_app(self.publisher)
        self.make_release(app)
        before = self.catalog()["apps"][0]["icon"]
        self.in_place(f"/console/apps/{app.app_id}/icon/", {"icon": picture(512, 512)})
        self.assertNotEqual(self.catalog()["apps"][0]["icon"], before)

    LISTING = {"name": "MyApp", "summary": "A fine app", "description": "It does things.", "source_language": "en", "category": "utilities",
               "tags": "", "license": "", "age_rating": "", "pricing_model": "free", "currency": "EUR", "trial_days": "0",
               "updates_period": "always", "visibility": "listed", "lifecycle": "active"}

    def test_a_field_that_does_not_apply_is_neither_checked_nor_kept(self):
        app = self.make_app(self.publisher, pricing_model="one-time", price="19.90", currency="USD", trial_days=14,
                            updates_period="until", updates_until="2030-01-01", open_source=True, source_url="https://acme.example/src",
                            lifecycle="deprecated", successor="org.acme.next")
        url = f"/console/apps/{app.app_id}/listing/"
        page = self.client.get(url)
        for field, controller in (("price", "pricing_model"), ("currency", "pricing_model"), ("trial_days", "pricing_model"),
                                  ("updates_period", "pricing_model"), ("updates_until", "updates_period"),
                                  ("source_url", "open_source"), ("successor", "lifecycle")):
            self.assertContains(page, f"&quot;field&quot;: &quot;{field}&quot;, &quot;on&quot;: &quot;{controller}&quot;")
            self.assertContains(page, f'data-field="{field}"')
        # Free: whatever the hidden price fields still hold - nonsense included - does not matter.
        response = self.post(url, {**self.LISTING, "price": "not a number", "currency": "dollars", "trial_days": "-3",
                                   "updates_period": "until", "updates_until": "soon", "source_url": "not a link", "successor": "org.acme.next"})
        self.assertRedirects(response, url)
        app.refresh_from_db()
        self.assertEqual((app.pricing_model, app.price, app.currency, app.trial_days, app.updates_period, app.updates_until),
                         ("free", None, "EUR", 0, "always", None))
        self.assertEqual((app.open_source, app.source_url, app.lifecycle, app.successor), (False, "", "active", ""))

        # Paid: the fields are back and are checked.
        paid = {**self.LISTING, "pricing_model": "monthly", "updates_period": "until"}
        response = self.post(url, {**paid, "price": "", "updates_until": ""})
        self.assertContains(response, "A paid app needs a price.")
        self.assertContains(response, "Name the date the free updates end.")
        response = self.post(url, {**paid, "price": "4.00", "currency": "usd", "trial_days": "7", "updates_until": "2031-05-01",
                                   "open_source": "on", "source_url": "https://acme.example/src", "lifecycle": "deprecated", "successor": "org.acme.next"})
        self.assertRedirects(response, url)
        app.refresh_from_db()
        self.assertEqual((app.pricing_model, str(app.price), app.currency, app.trial_days, app.updates_period, str(app.updates_until)),
                         ("monthly", "4.00", "USD", 7, "until", "2031-05-01"))
        self.assertEqual((app.source_url, app.successor), ("https://acme.example/src", "org.acme.next"))
        # The date goes with "until a date" alone.
        self.post(url, {**paid, "price": "4.00", "updates_period": "never", "updates_until": "2031-05-01"})
        app.refresh_from_db()
        self.assertEqual((app.updates_period, app.updates_until), ("never", None))

    def test_a_package_is_added_and_removed_in_place(self):
        app = self.make_app(self.publisher)
        release = Release.objects.create(app=app, version="1.0.0")
        base = f"/console/apps/{app.app_id}/releases/{release.pk}/"
        page = self.client.get(base)
        self.assertContains(page, "The release has no package.")
        self.assertContains(page, "disabled")   # Send for review
        # What is wrong is named at its field, so the page can leave the chosen file where it is.
        wrong = self.in_place(base + "packages/", {"file": archive(), "os": "linux", "arch": "x86_64", "entry": "Other/Thing"})
        self.assertEqual((wrong.status_code, list(wrong.json()["errors"])), (400, ["entry"]))
        wrong = self.in_place(base + "packages/", {"file": SimpleUploadedFile("myapp.zip", b"not a zip"), "os": "linux", "arch": "x86_64"})
        self.assertEqual(list(wrong.json()["errors"]), ["file"])
        wrong = self.in_place(base + "packages/", {"file": archive(), "os": "", "arch": "x86_64"})
        self.assertEqual(list(wrong.json()["errors"]), ["os"])
        wrong = self.in_place(base + "packages/", {"file": archive(), "os": "linux", "arch": "universal"})
        self.assertEqual(list(wrong.json()["errors"]), ["arch"])   # a macOS fat binary
        self.assertEqual(self.in_place(base + "packages/", {"file": archive(), "os": "wasm", "arch": "x86_64"}).status_code, 400)
        self.assertEqual(release.packages.count(), 0)
        self.assertEqual(list((self.folder / "packages").glob("*")), [])

        added = self.in_place(base + "packages/", {"file": archive(), "os": "linux", "arch": "x86_64", "entry": "MyApp/MyApp"})
        body = added.json()
        self.assertEqual((added.status_code, body["ok"], body["reset"]), (200, True, True))
        package = release.packages.get()
        self.assertIn(package.sha256, body["fragments"]["#packages"])
        self.assertIn(f"{base}packages/{package.pk}/edit/", body["fragments"]["#packages"])
        self.assertEqual(body["fragments"]["#release-problems"].strip(), "")
        self.assertNotIn("disabled", body["fragments"]["#release-actions"])
        again = self.in_place(base + "packages/", {"file": archive(salt="b"), "os": "linux", "arch": "x86_64", "entry": "MyApp/MyApp"})
        self.assertIn("already has a package", again.json()["errors"][""][0])

        removed = self.in_place(f"{base}packages/{package.pk}/delete/")
        self.assertIn("No package yet", removed.json()["fragments"]["#packages"])
        self.assertIn("The release has no package.", removed.json()["fragments"]["#release-problems"])
        self.assertIn("disabled", removed.json()["fragments"]["#release-actions"])
        self.assertFalse((self.folder / "packages" / package.file_name).exists())

    def test_a_package_is_corrected_until_the_release_is_in_review(self):
        app = self.make_app(self.publisher)
        release = self.make_release(app, publish=False)
        package = release.packages.get()
        before = (package.sha256, package.size, package.format)
        base = f"/console/apps/{app.app_id}/releases/{release.pk}/"
        edit = f"{base}packages/{package.pk}/edit/"
        self.assertContains(self.client.get(base), edit)
        # The dialog's form alone for the page's script, a page of its own without it.
        form = self.client.get(edit, headers=IN_PLACE)
        self.assertContains(form, 'value="MyApp/MyApp"')
        self.assertNotContains(form, "<html")
        self.assertContains(self.client.get(edit), "<html")

        fields = {"os": "windows", "arch": "arm64", "entry": "MyApp/MyApp", "min_os": "10.0.17763", "variant": " vc-redist , avx2 "}
        saved = self.in_place(edit, fields)
        self.assertEqual((saved.status_code, saved.json()["ok"], saved.json()["close"]), (200, True, True))
        self.assertIn("10.0.17763+", saved.json()["fragments"]["#packages"])
        package.refresh_from_db()
        self.assertEqual((package.os, package.arch, package.min_os, package.variant), ("windows", "arm64", "10.0.17763", "vc-redist,avx2"))
        self.assertEqual((package.sha256, package.size, package.format), before)   # the archive is not touched
        self.assertTrue((self.folder / "packages" / package.file_name).is_file())
        entry = AuditEntry.objects.filter(action="package.edit").get()
        self.assertEqual(sorted(entry.detail["fields"]), ["arch", "min_os", "os", "variant"])

        # The entry is looked up in the stored archive again; nothing is saved when it is not there.
        wrong = self.in_place(edit, {**fields, "entry": "Other/Thing"})
        self.assertEqual((wrong.status_code, list(wrong.json()["errors"])), (400, ["entry"]))
        self.assertContains(self.client.post(edit, {**fields, "entry": "Other/Thing"}), "The archive has no")
        self.assertEqual(self.in_place(edit, {**fields, "min_os": "ten"}).json()["errors"]["min_os"], ["A version of digits and dots: 14.0, 10.0.17763"])
        self.assertEqual(list(self.in_place(edit, {**fields, "arch": "wasm32"}).json()["errors"]), ["arch"])
        package.refresh_from_db()
        self.assertEqual(package.entry, "MyApp/MyApp")
        # An entry taken away is what keeps the release from review, and is said so at once.
        cleared = self.in_place(edit, {**fields, "entry": ""})
        self.assertIn("names no entry", cleared.json()["fragments"]["#release-problems"])
        self.assertIn("disabled", cleared.json()["fragments"]["#release-actions"])
        self.assertRedirects(self.post(edit, fields), base)   # without the script: back to the release

        # Two packages of one release are never for the same machine.
        from ultrastore_portal.store.packages import store_upload
        stored = store_upload(archive(salt="linux"))
        other = services.add_package(release, Package(os="linux", arch="x86_64", entry="MyApp/MyApp", sha256=stored.sha256, size=stored.size,
                                                      format=stored.format), stored.names, self.publisher.members.first().user)
        clash = self.in_place(f"{base}packages/{other.pk}/edit/", fields)
        self.assertIn("already has a package", clash.json()["errors"][""][0])
        self.assertEqual(self.in_place(f"{base}packages/{other.pk}/edit/", {"os": "linux", "arch": "x86_64", "entry": "MyApp"}).status_code, 200)   # itself, unchanged target

        # In review the packages are closed, as the release is; changes requested open them again.
        owner = self.publisher.members.first().user
        services.submit_release(release, owner)
        self.assertNotContains(self.client.get(base), edit)
        closed = self.in_place(edit, {**fields, "arch": "x86_64"})
        self.assertEqual(closed.status_code, 400)
        self.assertRedirects(self.client.get(edit), base)
        package.refresh_from_db()
        self.assertEqual(package.arch, "arm64")
        services.decide_release(release, self.operator, "request_changes", "Wrong processor.")
        self.assertEqual(self.in_place(edit, {**fields, "arch": "x86_64"}).status_code, 200)
        package.refresh_from_db()
        self.assertEqual(package.arch, "x86_64")

        # Never another publisher's.
        self.make_publisher("other@example.org", "other")
        self.sign_in("other@example.org")
        self.assertEqual(self.client.get(edit).status_code, 404)
        self.assertEqual(self.in_place(edit, fields).status_code, 404)


class CatalogTests(PortalCase):
    def test_the_catalog_is_signed_counted_and_published_only_on_a_change(self):
        first = self.client.get("/v1/catalog.json")
        body = json.loads(first.content)
        self.assertEqual((body["schema"], body["sequence"], body["apps"], body["market_url"]), (2, 1, [], ROOT))
        signature = self.client.get("/v1/catalog.json.sig").content.decode().strip()
        key = self.client.get("/v1/publish/key").json()["public_key"]
        self.assertEqual(body["trusted_keys"], [key])
        self.assertTrue(catalog.verify(first.content, signature, key))
        self.assertFalse(catalog.verify(first.content + b" ", signature, key))
        # The same content is the same catalog; a store that has it is told so.
        self.assertEqual(catalog.publish_catalog()[1], False)
        self.assertEqual(self.client.get("/v1/catalog.json", headers={"If-None-Match": first["ETag"]}).status_code, 304)

        app = self.make_app(self.make_publisher())
        self.make_release(app)
        second = self.client.get("/v1/catalog.json")
        body = json.loads(second.content)
        self.assertEqual(body["sequence"], 2)
        self.assertNotEqual(second["ETag"], first["ETag"])
        self.assertEqual(body["publishers"], [{"id": "acme", "name": "Acme"}])
        entry = body["apps"][0]
        self.assertEqual((entry["summary"], entry["categories"], entry["pricing"], entry["visibility"]),
                         ({"en": "A fine app"}, ["utilities"], {"model": "free"}, "listed"))
        self.assertTrue(entry["icon"].startswith(ROOT + "/media/apps/org.acme.myapp/"))
        package = entry["releases"][0]["packages"][0]
        self.assertEqual((package["os"], package["arch"], package["format"], package["entry"]), ("linux", "x86_64", "zip", "MyApp/MyApp"))
        self.assertRegex(package["sha256"], "^[0-9a-f]{64}$")
        folder = self.folder / "public" / "v1"
        self.assertEqual((folder / "catalog.json").read_bytes(), second.content)
        signature = (folder / "catalog.json.sig").read_text().strip()
        self.assertTrue(catalog.verify(second.content, signature, key))
        self.assertEqual(CatalogSnapshot.objects.count(), 2)

    def test_the_sequence_goes_on_from_a_floor_and_the_seed_can_be_configured(self):
        seed = "11" * 32
        with override_settings(SEQUENCE_FLOOR=500, SIGNING_SEED=seed, TRUSTED_KEYS=["AB" * 32]):
            snapshot, made = catalog.publish_catalog()
            self.assertEqual((snapshot.sequence, made), (501, True))
            self.assertEqual(snapshot.public_key, catalog.SigningKey(bytes.fromhex(seed)).public_hex)
            self.assertEqual(json.loads(bytes(snapshot.body))["trusted_keys"], [snapshot.public_key, "ab" * 32])

    def test_a_suspension_takes_an_app_and_its_packages_out(self):
        publisher = self.make_publisher()
        app = self.make_app(publisher)
        package = self.make_release(app).packages.get()
        public = self.folder / "public" / "v1" / "packages" / package.file_name
        self.assertTrue(public.is_file())
        self.sign_in("operator@example.org")
        self.post(f"/operator/apps/{app.app_id}/suspend/", {"reason": "Malware."})
        self.assertEqual(self.catalog()["apps"], [])
        self.assertFalse(public.exists())
        self.assertTrue((self.folder / "packages" / package.file_name).is_file())   # the evidence stays
        self.assertEqual(self.client.get(f"/apps/{app.app_id}/").status_code, 404)
        self.post(f"/operator/apps/{app.app_id}/reinstate/")
        self.assertEqual(len(self.catalog()["apps"]), 1)
        self.assertTrue(public.is_file())
        # The publisher suspended takes everything of theirs along.
        self.post("/operator/publishers/acme/", {"action": "suspend", "message": "Terms 3."})
        self.assertEqual(self.catalog()["apps"], [])
        self.assertEqual(self.client.get("/publisher/acme/").status_code, 404)

    def test_a_paid_app_without_a_trial_is_never_a_public_file(self):
        publisher = self.make_publisher()
        paid = self.make_app(publisher, "org.acme.paid", name="Paid", pricing_model="one-time", price="19.90", currency="EUR")
        package = self.make_release(paid).packages.get()
        entry = self.catalog()["apps"][0]
        self.assertEqual(entry["pricing"], {"model": "one-time", "price": "19.90", "currency": "EUR"})
        self.assertEqual(entry["releases"][0]["packages"][0]["url"], f"{ROOT}/v1/restricted/{package.file_name}")
        folder = self.folder / "public" / "v1"
        self.assertTrue((folder / "restricted" / package.file_name).is_file())
        self.assertFalse((folder / "packages" / package.file_name).exists())
        self.assertEqual(self.client.get(f"/apps/org.acme.paid/download/{package.pk}/").status_code, 404)
        self.assertNotContains(self.client.get("/apps/org.acme.paid/"), "Direct downloads")
        self.assertContains(self.client.get("/apps/org.acme.paid/"), "Buy 19.90 EUR")

    def test_withdrawing_and_unlisting(self):
        publisher = self.make_publisher()
        app = self.make_app(publisher)
        first = self.make_release(app, "1.0.0")
        self.make_release(app, "1.1.0", channel="beta")
        versions = [r["version"] for r in self.catalog()["apps"][0]["releases"]]
        self.assertEqual(versions, ["1.1.0", "1.0.0"])
        self.assertContains(self.client.get(f"/apps/{app.app_id}/"), "Version 1.0.0")   # the stable one is the store page's
        self.sign_in("dev@example.org")
        self.post(f"/console/apps/{app.app_id}/releases/{first.pk}/withdraw/")
        self.assertEqual([r["version"] for r in self.catalog()["apps"][0]["releases"]], ["1.1.0"])
        app.visibility = "unlisted"
        app.save()
        self.client.logout()
        self.assertNotContains(self.client.get("/apps/"), "A fine app")
        self.assertEqual(self.client.get(f"/apps/{app.app_id}/").status_code, 200)   # by its link


class ApiTests(PortalCase):
    def setUp(self):
        super().setUp()
        publisher = self.make_publisher()
        self.app = self.make_app(publisher, tags="tools")
        self.make_release(self.app)
        paid = self.make_app(publisher, "org.acme.video", name="Cutter", category="video", pricing_model="monthly", price="4.00", currency="EUR", trial_days=14)
        self.make_release(paid)
        self.make_app(publisher, "org.acme.draft", name="Draft")   # never published

    def test_the_list_filters_and_pages(self):
        body = self.client.get("/v1/apps").json()
        self.assertEqual((body["count"], body["pages"]), (2, 1))
        self.assertEqual({a["id"] for a in body["results"]}, {"org.acme.myapp", "org.acme.video"})
        card = next(a for a in body["results"] if a["id"] == "org.acme.myapp")
        self.assertEqual((card["publisher"], card["version"], card["systems"], card["pricing"]),
                         ({"id": "acme", "name": "Acme"}, "1.0.0", ["linux"], {"model": "free"}))
        self.assertEqual(card["url"], ROOT + "/v1/apps/org.acme.myapp")
        self.assertEqual(card["web_url"], ROOT + "/apps/org.acme.myapp/")
        ids = lambda query: [a["id"] for a in self.client.get("/v1/apps" + query).json()["results"]]   # noqa: E731
        self.assertEqual(ids("?category=video"), ["org.acme.video"])
        self.assertEqual(ids("?price=free"), ["org.acme.myapp"])
        self.assertEqual(ids("?price=paid"), ["org.acme.video"])
        self.assertEqual(ids("?q=cutter"), ["org.acme.video"])
        self.assertEqual(ids("?q=tools"), ["org.acme.myapp"])
        self.assertEqual(ids("?os=windows"), [])
        self.assertEqual(ids("?os=linux&sort=name"), ["org.acme.video", "org.acme.myapp"])
        self.assertEqual(ids("?publisher=nobody"), [])
        self.assertEqual(len(ids("?per_page=1")), 1)
        self.assertEqual(self.client.get("/v1/apps?per_page=1").json()["pages"], 2)
        self.assertEqual(self.client.get("/v1/apps?page=9").json()["results"], [])
        self.assertEqual(self.client.get("/v1/apps?page=x").status_code, 400)
        self.assertEqual(self.client.get("/v1/apps")["Access-Control-Allow-Origin"], "*")

    def test_one_app_the_categories_and_a_publisher(self):
        body = self.client.get("/v1/apps/org.acme.video").json()
        self.assertEqual((body["name"], body["publisher"], body["publisher_name"]), ("Cutter", "acme", "Acme"))
        self.assertEqual(body["pricing"], {"model": "monthly", "price": "4.00", "currency": "EUR", "trial_days": 14})
        self.assertEqual(body, {**self.catalog()["apps"][1], "publisher_name": "Acme", "web_url": ROOT + "/apps/org.acme.video/"})
        missing = self.client.get("/v1/apps/org.acme.draft")
        self.assertEqual((missing.status_code, missing.json()["error"]), (404, "no_such_app"))
        categories = {c["id"]: c["apps"] for c in self.client.get("/v1/categories").json()}
        self.assertEqual((len(categories), categories["video"], categories["utilities"], categories["games"]), (14, 1, 1, 0))
        publisher = self.client.get("/v1/publishers/acme").json()
        self.assertEqual((publisher["name"], len(publisher["apps"])), ("Acme", 2))
        self.assertEqual(self.client.get("/v1/publishers/nobody").status_code, 404)
        self.assertEqual(self.client.get("/v1/terms/current").json()["version"], 1)
        self.assertEqual(self.client.get("/healthz").json()["status"], "ok")

    def test_a_report_from_a_store_and_its_outcome(self):
        sent = self.client.post("/v1/reports", json.dumps({"target_kind": "app", "target": "org.acme.myapp", "kind": "broken", "text": "It does not start.",
                                                           "reporter": "ULIC1.abc"}), content_type="application/json")
        self.assertEqual(sent.status_code, 201)
        receipt = sent.json()
        self.assertEqual(self.client.get(f"/v1/reports/{receipt['id']}/outcome?secret=wrong").status_code, 404)
        self.assertEqual(self.client.get(f"/v1/reports/{receipt['id']}/outcome?secret={receipt['secret']}").json()["state"], "open")
        self.assertEqual(self.client.post("/v1/reports", "{}", content_type="application/json").status_code, 400)
        self.assertEqual(self.client.post("/v1/reports", "nonsense", content_type="application/json").status_code, 400)

        report = Report.objects.get(public_id=receipt["id"])
        self.sign_in("operator@example.org")
        self.assertContains(self.client.get(f"/operator/reports/{report.pk}/"), "ULIC1.abc")
        self.post(f"/operator/reports/{report.pk}/", {"action": "resolve", "findings": "Reproduced on glibc 2.31.", "outcome": "The publisher shipped a fix in 1.0.1."})
        outcome = self.client.get(f"/v1/reports/{receipt['id']}/outcome?secret={receipt['secret']}").json()
        self.assertEqual((outcome["state"], outcome["outcome"]), ("resolved", "The publisher shipped a fix in 1.0.1."))
        self.assertNotIn("findings", outcome)

    def test_the_web_report_form(self):
        response = self.client.post("/apps/org.acme.myapp/report/", {"kind": "misleading", "text": "The screenshots are another app's.", "reporter": ""})
        self.assertContains(response, "the report is with the operator")
        report = Report.objects.get()
        self.assertEqual((report.target, report.kind), ("org.acme.myapp", "misleading"))
        self.assertContains(self.client.get(f"/reports/{report.public_id}/?secret={report.secret}"), "has not answered yet")
        self.assertEqual(self.client.get(f"/reports/{report.public_id}/?secret=no").status_code, 404)
