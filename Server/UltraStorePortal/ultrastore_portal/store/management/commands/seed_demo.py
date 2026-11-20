"""ultrastore-portal seed_demo: a store to look at on a developer's machine
- an operator, a publisher with its owner, a handful of published apps with
drawn icons and screenshots and small packages, one release waiting for
review and one publisher application. Refused outside DEBUG: the accounts
have a known password."""

from __future__ import annotations

import io
import zipfile

from django.conf import settings
from django.contrib.auth import get_user_model
from django.core.files.base import ContentFile
from django.core.files.uploadedfile import SimpleUploadedFile
from django.core.management.base import BaseCommand, CommandError
from PIL import Image, ImageDraw

from ultrastore_portal.store import catalog, services
from ultrastore_portal.store.models import App, Package, Publisher, Release, Screenshot, TermsVersion
from ultrastore_portal.store.packages import store_upload

PASSWORD = "ultrastore-demo-password"
APPS = [
    # name, category, summary, colour, price, featured
    ("UltraVideo", "video", "The video editor: cut, grade and export on every desktop", "#7C3AED", "29.90", True),
    ("UltraPaint", "graphics", "Paint and retouch with layers, brushes and filters", "#DB2777", None, True),
    ("UltraMail", "communication", "Mail, calendars and contacts in one calm window", "#2563EB", None, True),
    ("UltraFiler", "system", "The file manager with tabs, previews and archives", "#0891B2", None, False),
    ("UltraTexter", "office", "A fast text editor for notes, code and long documents", "#059669", None, False),
    ("UltraPassword", "security", "The password vault that stays on your machine", "#B45309", "9.90", False),
    ("ChatCube", "communication", "The ULTRA OS messenger, end to end encrypted", "#4F46E5", None, False),
    ("UltraSnip", "utilities", "Screenshots and screen recordings with annotations", "#DC2626", None, False),
]
DESCRIPTION = """{name} is part of the ULTRA OS application family, built on UltraCanvas.

{summary}. It starts fast, looks the same on Linux, Windows and macOS, and keeps your data on your own machine.

What you get:
- a native window on every system, no browser inside
- updates through UltraStore, with what changed in plain words
- your settings and documents where you expect them"""


def png(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, "PNG")
    return out.getvalue()


def draw_icon(colour: str, letter: str) -> bytes:
    image = Image.new("RGB", (256, 256), colour)
    draw = ImageDraw.Draw(image)
    draw.ellipse((48, 48, 208, 208), fill="#FFFFFF")
    draw.ellipse((88, 88, 168, 168), fill=colour)
    return png(image)


def draw_screenshot(colour: str, variant: int) -> bytes:
    image = Image.new("RGB", (1120, 700), "#0F172A")
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 1120, 44), fill="#1E293B")
    for i, dot in enumerate(("#EF4444", "#F59E0B", "#22C55E")):
        draw.ellipse((18 + i * 22, 15, 32 + i * 22, 29), fill=dot)
    draw.rectangle((0, 44, 220, 700), fill="#111C33")
    for i in range(7):
        draw.rounded_rectangle((20, 70 + i * 44, 200, 100 + i * 44), 8, fill=colour if i == variant else "#1E293B")
    for row in range(3):
        for col in range(3):
            x, y = 250 + col * 285, 76 + row * 205
            draw.rounded_rectangle((x, y, x + 260, y + 180), 14, fill="#1E293B" if (row + col + variant) % 3 else colour)
    return png(image)


def tiny_package(name: str, system: str) -> SimpleUploadedFile:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        archive.writestr(f"{name}/{name}", f"#!/bin/sh\necho {name} for {system}\n")
        archive.writestr(f"{name}/README.txt", "A demo package, not the real application.\n")
    return SimpleUploadedFile(f"{name.lower()}-{system}.zip", out.getvalue())


class Command(BaseCommand):
    help = "Fill a developer's database with a demo store (DEBUG only)."

    def handle(self, *args, **options):
        if not settings.DEBUG:
            raise CommandError("seed_demo runs only with ULTRASTORE_PORTAL_DEBUG=1: its accounts have a known password.")
        User = get_user_model()
        operator = User.objects.filter(email="operator@example.org").first() or User.objects.create_superuser(
            "operator@example.org", PASSWORD, name="Olivia Operator")
        owner = User.objects.filter(email="dev@example.org").first() or User.objects.create_user(
            "dev@example.org", PASSWORD, name="Dana Developer", email_verified=True)
        applicant = User.objects.filter(email="newcomer@example.org").first() or User.objects.create_user(
            "newcomer@example.org", PASSWORD, name="Noel Newcomer", email_verified=True)
        if not TermsVersion.objects.exists():
            TermsVersion.objects.create(version=1, text="1. You publish only software you have the right to publish.\n\n"
                                        "2. A release is reviewed before it is listed; the operator may ask for changes or refuse it.\n\n"
                                        "3. Malware, misleading listings and software that harms its user are removed, and the publisher suspended.\n\n"
                                        "(Sample terms for a developer's machine.)")
        publisher = Publisher.objects.filter(slug="ultra-os-project").first()
        if publisher is None:
            publisher = services.apply_for_publisher(owner, Publisher(
                slug="ultra-os-project", name="ULTRA OS Project", website="https://ultraos.eu", support_email="dev@example.org",
                description="The makers of ULTRA OS and the UltraCanvas framework."))
            services.decide_publisher(publisher, operator, "approve", "Welcome to UltraStore.")
        if not Publisher.objects.filter(slug="newcomer-labs").exists():
            services.apply_for_publisher(applicant, Publisher(slug="newcomer-labs", name="Newcomer Labs", support_email="newcomer@example.org",
                                                              description="Two people making small tools."))
        icons = settings.BASE_DIR.parent.parent / "media" / "appicon"
        for index, (name, category, summary, colour, price, featured) in enumerate(APPS):
            app_id = f"org.ultraos.{name.lower()}"
            if App.objects.filter(app_id=app_id).exists():
                continue
            app = App(app_id=app_id, publisher=publisher, name=name, summary=summary, category=category,
                      description=DESCRIPTION.format(name=name, summary=summary), license="MIT" if not price else "Proprietary",
                      open_source=not price, source_url="" if price else "https://github.com/ULTRA-OS-Project/UltraCanvas",
                      homepage="https://ultraos.eu", tags=f"{category}, ultracanvas", featured=featured,
                      pricing_model="one-time" if price else "free", price=price, trial_days=30 if price else 0)
            real_icon = icons / f"{name}.png"
            app.icon.save("icon.png", ContentFile(real_icon.read_bytes() if real_icon.is_file() else draw_icon(colour, name[5:6])), save=False)
            app.save()
            for shot in range(3):
                Screenshot(app=app, position=shot).image.save("shot.png", ContentFile(draw_screenshot(colour, shot + index % 3)), save=True)
            release = Release.objects.create(app=app, version=f"1.{index}.0", notes="The first release in UltraStore.\n- Faster start\n- A new settings page")
            for system, arch, min_os in (("linux", "x86_64", ""), ("windows", "x86_64", "10.0.17763"), ("macos", "universal", "14.0")):
                stored = store_upload(tiny_package(name, system))
                services.add_package(release, Package(os=system, arch=arch, min_os=min_os, entry=f"{name}/{name}", variant="glibc-2.31" if system == "linux" else "",
                                                      sha256=stored.sha256, size=stored.size, format=stored.format,
                                                      original_name=f"{name.lower()}-{system}.zip"), stored.names, owner)
            services.submit_release(release, owner)
            if name != "UltraSnip":   # one stays in the review queue
                services.decide_release(release, operator, "approve", "")
        snapshot, _ = catalog.publish_catalog(operator)
        self.stdout.write(self.style.SUCCESS(f"Demo store ready, catalog {snapshot.sequence}."))
        self.stdout.write(f"Operator:  operator@example.org / {PASSWORD}")
        self.stdout.write(f"Publisher: dev@example.org / {PASSWORD}")
        self.stdout.write(f"Applicant: newcomer@example.org / {PASSWORD}")

