"""The portal end to end, through its pages and its JSON service: a person
signs up, applies for a publisher, the operator approves, an app is listed,
a release uploaded, reviewed and published, and the signed catalog carries
it."""

from __future__ import annotations

import io
import json
import shutil
import tempfile
import zipfile
from pathlib import Path

from django.contrib.auth import get_user_model
from django.core import mail
from django.core.cache import cache
from django.core.files.uploadedfile import SimpleUploadedFile
from django.test import TestCase, override_settings
from PIL import Image

from ultrastore_portal.store import catalog, services
from ultrastore_portal.store.models import App, AuditEntry, CatalogSnapshot, Package, Publisher, Release, Report, TermsVersion

User = get_user_model()
PASSWORD = "a-long-enough-passphrase"
ROOT = "https://store.ultraos.eu"


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

    def test_password_reset_mails_a_link_under_the_root(self):
        User.objects.create_user("dana@example.org", PASSWORD, email_verified=True)
        self.client.post("/account/password/reset/", {"email": "dana@example.org"})
        self.assertIn(ROOT + "/account/password/reset/", mail.outbox[0].body)


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
        self.assertContains(self.client.post("/console/apps/org.acme.myapp/listing/", {**listing, "icon": picture(300, 200)}), "An icon is square")
        self.assertRedirects(self.post("/console/apps/org.acme.myapp/listing/", {**listing, "icon": picture()}), "/console/apps/org.acme.myapp/listing/")
        self.post("/console/apps/org.acme.myapp/screenshots/", {"image": picture(640, 400, name="shot.png")})
        app = App.objects.get(app_id="org.acme.myapp")
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
