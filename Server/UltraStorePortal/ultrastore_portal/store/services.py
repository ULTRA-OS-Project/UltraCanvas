"""What changes the store's state, each in one place so the pages, the
admin site and the command line never disagree: a publisher applied for and
decided, a release submitted and decided, an app suspended, a report
handled. Every one of them is a row in the audit log, and every one that
can change what is public asks for the catalog again (catalog.py, which
publishes only when the content changed).

Version: 0.3.0 - edit_package: a package corrected while its release is the publisher's
Version: 0.1.0
"""

from __future__ import annotations

import logging

from django.conf import settings
from django.core.mail import send_mail
from django.db import transaction
from django.utils import timezone

from . import catalog
from .models import App, AuditEntry, Package, Publisher, PublisherMember, Release, Report, TermsVersion
from .packages import entry_in_archive, remove_if_unused, stored_names

log = logging.getLogger(__name__)


class Refused(Exception):
    """A change the state does not allow; the message is for the person."""


def audit(actor, action: str, target_kind: str, target: str, **detail) -> AuditEntry:
    return AuditEntry.objects.create(
        actor=actor if getattr(actor, "pk", None) else None, actor_name=str(actor) if actor else "portal",
        action=action, target_kind=target_kind, target=target, detail=detail)


def republish(actor=None) -> None:
    """The catalog asked for again, once the change is committed. A publish
    that fails does not undo the decision behind it: it is logged, and the
    operator's *Publish the catalog now* or the next change makes it up."""
    def publish() -> None:
        try:
            catalog.publish_catalog(actor)
        except Exception:   # noqa: BLE001 - the disk, the key, the database
            log.exception("The catalog could not be published")

    transaction.on_commit(publish)


def notify(publisher: Publisher, subject: str, text: str) -> None:
    """A mail to the publisher's people; a mail that cannot go out does not
    undo the decision it reports."""
    recipients = {publisher.support_email} | {m.user.email for m in publisher.members.select_related("user")}
    body = f"{text}\n\n{settings.BASE_URL}/console/\n"
    try:
        send_mail(f"UltraStore: {subject}", body, None, sorted(r for r in recipients if r), fail_silently=False)
    except Exception as error:   # noqa: BLE001 - SMTP fails in many ways
        log.warning("Mail to %s not sent: %s", publisher.slug, error)


# ---------------------------------------------------------------- publishers

def membership_of(user) -> PublisherMember | None:
    if not getattr(user, "is_authenticated", False):
        return None
    return PublisherMember.objects.filter(user=user).select_related("publisher").order_by("id").first()


def publisher_of(user) -> Publisher | None:
    membership = membership_of(user)
    return membership.publisher if membership else None


@transaction.atomic
def apply_for_publisher(user, publisher: Publisher) -> Publisher:
    """A person's application: the publisher waits for the operator, the
    person is its owner, and the terms in force are accepted with it."""
    if publisher_of(user):
        raise Refused("This account already has a publisher.")
    terms = TermsVersion.current()
    publisher.state = Publisher.APPLIED
    if terms:
        publisher.terms_version = terms.version
        publisher.terms_accepted_at = timezone.now()
        publisher.terms_accepted_by = user
    publisher.save()
    PublisherMember.objects.create(publisher=publisher, user=user, role=PublisherMember.OWNER)
    audit(user, "publisher.apply", "publisher", publisher.slug, name=publisher.name, terms=publisher.terms_version)
    return publisher


@transaction.atomic
def accept_terms(publisher: Publisher, user) -> None:
    terms = TermsVersion.current()
    if not terms:
        return
    publisher.terms_version = terms.version
    publisher.terms_accepted_at = timezone.now()
    publisher.terms_accepted_by = user
    publisher.save(update_fields=["terms_version", "terms_accepted_at", "terms_accepted_by", "updated"])
    audit(user, "terms.accept", "publisher", publisher.slug, version=terms.version)


PUBLISHER_DECISIONS = {
    # action: (states it applies to, the state after, needs a message)
    "approve": ((Publisher.APPLIED, Publisher.REJECTED), Publisher.APPROVED, False),
    "reject": ((Publisher.APPLIED,), Publisher.REJECTED, True),
    "suspend": ((Publisher.APPROVED,), Publisher.SUSPENDED, True),
    "reinstate": ((Publisher.SUSPENDED,), Publisher.APPROVED, False),
}


@transaction.atomic
def decide_publisher(publisher: Publisher, actor, action: str, message: str = "") -> Publisher:
    if action not in PUBLISHER_DECISIONS:
        raise Refused("Unknown decision.")
    allowed, after, needs_message = PUBLISHER_DECISIONS[action]
    if publisher.state not in allowed:
        raise Refused(f"A publisher that is {publisher.get_state_display().lower()} cannot be decided this way.")
    if needs_message and not message.strip():
        raise Refused("Say why: the publisher reads it.")
    before = publisher.state
    publisher.state = after
    publisher.decision_message = message.strip()
    publisher.decided_by = actor
    publisher.decided_at = timezone.now()
    publisher.save()
    audit(actor, f"publisher.{action}", "publisher", publisher.slug, before=before, after=after, message=message.strip())
    subjects = {"approve": "your publisher account is approved", "reject": "your publisher application was not accepted",
                "suspend": "your publisher account is suspended", "reinstate": "your publisher account is reinstated"}
    notify(publisher, subjects[action], message.strip() or f"{publisher.name}: {subjects[action]}.")
    republish(actor)
    return publisher


# ---------------------------------------------------------------------- apps

def listing_problems(app: App) -> list[str]:
    """What the listing still lacks before a release can go to review."""
    problems = []
    if not app.summary:
        problems.append("The listing has no summary.")
    if app.kind == "app":
        if not app.description:
            problems.append("The listing has no description.")
        if not app.category:
            problems.append("The listing names no category.")
        if not app.icon:
            problems.append("The listing has no icon.")
        if not app.screenshots.exists():
            problems.append("The listing has no screenshot.")
    return problems


def release_problems(release: Release) -> list[str]:
    """What keeps this release from going to review."""
    problems = []
    packages = list(release.packages.all())
    if not packages:
        problems.append("The release has no package.")
    if release.app.kind == "app":
        for package in packages:
            if not package.entry and package.format != "appimage":
                problems.append(f"The {package.os}/{package.arch} package names no entry (what the store starts).")
    return problems


def missing_dependencies(release: Release) -> list[str]:
    """The dependencies no published release provides."""
    missing = []
    for dep in release.depends or []:
        found = Release.objects.filter(app__app_id=dep.get("id"), version=dep.get("version"), state=Release.PUBLISHED,
                                       app__suspended=False, app__publisher__state=Publisher.APPROVED).exists()
        if not found:
            missing.append(f"{dep.get('id')}@{dep.get('version')}")
    return missing


@transaction.atomic
def submit_release(release: Release, user) -> Release:
    publisher = release.app.publisher
    if not publisher.approved:
        raise Refused("The publisher account is not approved.")
    if publisher.terms_due():
        raise Refused("Accept the publisher terms in force first.")
    if not release.editable:
        raise Refused(f"A release that is {release.get_state_display().lower()} cannot be sent for review.")
    problems = listing_problems(release.app) + release_problems(release)
    if problems:
        raise Refused(" ".join(problems))
    release.state = Release.SUBMITTED
    release.submitted_at = timezone.now()
    release.submitted_by = user
    release.save()
    audit(user, "release.submit", "release", str(release), packages=[p.sha256 for p in release.packages.all()])
    return release


@transaction.atomic
def withdraw_release(release: Release, user) -> Release:
    """The publisher's way back: a release in review returns to a draft, a
    published one leaves the store."""
    if release.state == Release.SUBMITTED:
        release.state = Release.DRAFT
    elif release.state == Release.PUBLISHED:
        release.state = Release.WITHDRAWN
    else:
        raise Refused(f"A release that is {release.get_state_display().lower()} cannot be withdrawn.")
    release.save()
    audit(user, "release.withdraw", "release", str(release), after=release.state)
    republish(user)
    return release


@transaction.atomic
def decide_release(release: Release, actor, action: str, message: str = "") -> Release:
    """The review's decision: approve publishes the release."""
    if release.state != Release.SUBMITTED:
        raise Refused(f"A release that is {release.get_state_display().lower()} is not in review.")
    app = release.app
    message = message.strip()
    now = timezone.now()
    if action == "approve":
        if not app.publisher.approved:
            raise Refused("The publisher is not in good standing.")
        missing = missing_dependencies(release)
        if missing:
            raise Refused("Publish what it depends on first: " + ", ".join(missing))
        release.state = Release.PUBLISHED
        release.published_at = now
        app.first_published = app.first_published or now
        app.last_published = now
        app.save(update_fields=["first_published", "last_published", "updated"])
        subject = f"{app.name} {release.version} is published"
    elif action == "request_changes":
        if not message:
            raise Refused("Say what to change: the publisher reads it.")
        release.state = Release.CHANGES
        subject = f"{app.name} {release.version}: changes requested"
    elif action == "reject":
        if not message:
            raise Refused("Say why: the publisher reads it.")
        release.state = Release.REJECTED
        subject = f"{app.name} {release.version} was rejected"
    else:
        raise Refused("Unknown decision.")
    release.review_message = message
    release.reviewed_by = actor
    release.reviewed_at = now
    release.save()
    audit(actor, f"release.{action}", "release", str(release), message=message)
    notify(app.publisher, subject, message or subject + ".")
    if action == "approve":
        republish(actor)
    return release


@transaction.atomic
def delete_release(release: Release, user) -> None:
    if not release.editable and release.state not in (Release.REJECTED, Release.WITHDRAWN):
        raise Refused(f"A release that is {release.get_state_display().lower()} cannot be deleted.")
    files = [(p.sha256, p.format) for p in release.packages.all()]
    audit(user, "release.delete", "release", str(release))
    release.delete()
    for sha256, fmt in files:
        remove_if_unused(sha256, fmt)


def add_package(release: Release, package: Package, names: list[str] | None, user) -> Package:
    if not release.editable:
        raise Refused("A release in review or published takes no more packages: make a new version.")
    if not entry_in_archive(package.entry, names):
        remove_if_unused(package.sha256, package.format)
        raise Refused(f"The archive has no \"{package.entry}\" to start.")
    package.release = release
    package.save()
    audit(user, "package.add", "release", str(release), sha256=package.sha256, os=package.os, arch=package.arch, size=package.size)
    return package


def edit_package(package: Package, user, fields: list[str]) -> Package:
    """What a package says about its archive, corrected while the release is
    still the publisher's: the system, the processor, what the machine must
    satisfy, the entry - looked up in the stored archive again. The bytes and
    their hash stay as uploaded."""
    if not package.release.editable:
        raise Refused("A release in review or published keeps its packages as they are.")
    if not entry_in_archive(package.entry, stored_names(package.sha256, package.format)):
        raise Refused(f"The archive has no \"{package.entry}\" to start.")
    package.save()
    audit(user, "package.edit", "release", str(package.release), sha256=package.sha256, os=package.os, arch=package.arch, fields=fields)
    return package


def delete_package(package: Package, user) -> None:
    if not package.release.editable:
        raise Refused("A release in review or published keeps its packages.")
    sha256, fmt = package.sha256, package.format
    audit(user, "package.delete", "release", str(package.release), sha256=sha256)
    package.delete()
    remove_if_unused(sha256, fmt)


@transaction.atomic
def set_app_suspended(app: App, actor, suspended: bool, reason: str = "") -> App:
    if suspended and not reason.strip():
        raise Refused("Say why: the publisher reads it.")
    app.suspended = suspended
    app.suspend_reason = reason.strip() if suspended else ""
    app.save(update_fields=["suspended", "suspend_reason", "updated"])
    audit(actor, "app.suspend" if suspended else "app.reinstate", "app", app.app_id, reason=reason.strip())
    notify(app.publisher, f"{app.name} is {'suspended' if suspended else 'back in the store'}", reason.strip() or f"{app.name} is listed again.")
    republish(actor)
    return app


@transaction.atomic
def set_app_featured(app: App, actor, featured: bool) -> App:
    app.featured = featured
    app.save(update_fields=["featured", "updated"])
    audit(actor, "app.feature" if featured else "app.unfeature", "app", app.app_id)
    republish(actor)
    return app


@transaction.atomic
def handle_report(report: Report, actor, action: str, findings: str, outcome: str) -> Report:
    if action not in ("resolve", "dismiss"):
        raise Refused("Unknown decision.")
    if not outcome.strip():
        raise Refused("Write the line the reporter reads.")
    report.state = Report.RESOLVED if action == "resolve" else Report.DISMISSED
    report.findings = findings.strip()
    report.outcome = outcome.strip()
    report.handled_by = actor
    report.handled_at = timezone.now()
    report.save()
    audit(actor, f"report.{action}", "report", report.public_id, about=report.target, outcome=report.outcome)
    return report
