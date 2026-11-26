"""Leaving an account: closing it and coming back, asking for its deletion
and taking that back, and what is left when it is erased."""

from __future__ import annotations

import datetime as dt
import io

from django.core import mail
from django.core.management import call_command
from django.test import override_settings
from django.utils import timezone

from ultrastore_portal.accounts import closure
from ultrastore_portal.accounts.models import User
from ultrastore_portal.identity import provider
from ultrastore_portal.identity.models import Client, Grant, SignIn
from ultrastore_portal.store.models import AuditEntry, PublisherMember

from .test_portal import PortalCase

PASSWORD = "a long password"


class ClosureCase(PortalCase):
    def setUp(self):
        super().setUp()
        self.user = User.objects.create_user("buyer@example.org", PASSWORD, name="Bea Buyer", email_verified=True)

    def sign_in_with_password(self, email="buyer@example.org", password=PASSWORD, **kwargs):
        return self.client.post("/account/signin/", {"username": email, "password": password}, **kwargs)

    def grant(self) -> Grant:
        client = Client.objects.create(client_id="shop", name="Shop", redirect_uris="https://shop.example/cb")
        return Grant.objects.create(user=self.user, client=client, scope="openid", auth_time=timezone.now())


class ClosingTests(ClosureCase):
    def test_a_closed_account_is_signed_out_everywhere_and_kept_for_a_year(self):
        self.sign_in_with_password()
        grant = self.grant()
        self.assertContains(self.client.get("/account/"), "Close or delete the account")
        page = self.client.get("/account/leave/")
        self.assertContains(page, "kept for 365 days")
        self.assertContains(page, "usable for 90 days")
        # Not without the password: a browser somebody walked away from is not enough.
        wrong = self.client.post("/account/leave/", {"what": "close", "password": "not it"})
        self.assertContains(wrong, "The password is wrong.")
        self.assertEqual(User.objects.get(pk=self.user.pk).state, "active")

        done = self.post("/account/leave/", {"what": "close", "password": PASSWORD})   # with what follows the commit: the mail
        self.assertRedirects(done, "/account/signin/", fetch_redirect_response=False)
        user = User.objects.get(pk=self.user.pk)
        self.assertEqual((user.state, user.is_active, user.erase_after), ("closed", False, timezone.localdate() + dt.timedelta(days=365)))
        grant.refresh_from_db()
        self.assertIsNotNone(grant.revoked)                                             # every service's access ended
        self.assertFalse(SignIn.objects.filter(user=user, ended__isnull=True).exists())  # and every sign-in
        self.assertEqual(self.client.get("/account/").status_code, 302)                 # this browser's too
        self.assertEqual(AuditEntry.objects.filter(action="account.close", target=str(user.pk)).count(), 1)
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn("your account is closed", mail.outbox[0].subject)
        self.assertIn("/account/signin/", mail.outbox[0].body)

    def test_the_right_password_offers_a_closed_account_back(self):
        closure.close(self.user)
        # A wrong password says what it always says, and tells nobody the account is closed.
        self.assertContains(self.sign_in_with_password(password="not it at all"), "The email or the password is wrong.")
        offered = self.sign_in_with_password()
        self.assertContains(offered, "This account is closed")
        self.assertEqual(self.client.get("/account/").status_code, 302)   # offered, not signed in
        token = offered.context["token"]
        self.assertRedirects(self.client.post("/account/reactivate/", {"token": token}), "/account/", fetch_redirect_response=False)
        user = User.objects.get(pk=self.user.pk)
        self.assertEqual((user.state, user.is_active, user.erase_after, user.name), ("active", True, None, "Bea Buyer"))
        self.assertContains(self.client.get("/account/"), "Signed in as buyer@example.org")
        # The token is for that closing, once: it brings nothing back a second time.
        closure.close(user)
        self.client.post("/account/signout/")
        self.assertRedirects(self.client.post("/account/reactivate/", {"token": token}), "/account/signin/", fetch_redirect_response=False)
        self.assertEqual(User.objects.get(pk=self.user.pk).state, "closed")
        self.assertRedirects(self.client.post("/account/reactivate/", {"token": "nonsense"}), "/account/signin/", fetch_redirect_response=False)

    def test_a_forgotten_password_does_not_lock_a_closed_account_out(self):
        closure.close(self.user)
        mail.outbox.clear()
        self.client.post("/account/password/reset/", {"email": "buyer@example.org"})
        self.assertEqual(len(mail.outbox), 1)
        link = next(word for word in mail.outbox[0].body.split() if "/password/reset/" in word)
        page = self.client.get(link, follow=True)
        self.client.post(page.redirect_chain[-1][0], {"new_password1": "another long one", "new_password2": "another long one"})
        self.assertEqual(User.objects.get(pk=self.user.pk).state, "closed")   # a new password alone brings nothing back
        self.assertContains(self.sign_in_with_password(password="another long one"), "This account is closed")

    def test_an_operator_and_a_publishers_owner_do_not_leave_this_way(self):
        publisher = self.make_publisher()
        owner = PublisherMember.objects.get(publisher=publisher).user
        self.assertIn("owns a publisher", closure.refusal(owner))
        self.sign_in("dev@example.org")
        page = self.client.post("/account/leave/", {"what": "close", "password": "whatever"})
        self.assertContains(page, "owns a publisher")
        self.assertEqual(User.objects.get(pk=owner.pk).state, "active")
        self.assertIn("another operator", closure.refusal(User.objects.get(email="operator@example.org")))
        self.assertEqual(closure.refusal(self.user), "")


class DeletionTests(ClosureCase):
    def test_a_deletion_waits_ninety_days_and_can_be_taken_back(self):
        self.sign_in_with_password()
        done = self.post("/account/leave/", {"what": "delete", "password": PASSWORD})
        self.assertRedirects(done, "/account/", fetch_redirect_response=False)
        user = User.objects.get(pk=self.user.pk)
        self.assertEqual((user.state, user.is_active, user.erase_after), ("deleting", True, timezone.localdate() + dt.timedelta(days=90)))
        page = self.client.get("/account/")                       # usable still, and it says when it goes
        self.assertContains(page, "This account will be deleted on")
        self.assertContains(page, "Keep my account")
        self.assertNotContains(page, "Close or delete the account")
        self.assertIn("will be deleted", mail.outbox[0].subject)
        self.assertRedirects(self.client.post("/account/leave/keep/"), "/account/", fetch_redirect_response=False)
        user.refresh_from_db()
        self.assertEqual((user.state, user.erase_after), ("active", None))
        self.assertEqual([e.action for e in AuditEntry.objects.filter(target_kind="account").order_by("id")],
                         ["account.delete_requested", "account.delete_cancelled"])

    @override_settings(ACCOUNT_CLOSED_DAYS=30, ACCOUNT_DELETION_GRACE_DAYS=7)
    def test_the_days_are_the_settings(self):
        closure.request_deletion(self.user)
        self.assertEqual(self.user.erase_after, timezone.localdate() + dt.timedelta(days=7))
        closure.cancel_deletion(self.user)
        closure.close(self.user)
        self.assertEqual(self.user.erase_after, timezone.localdate() + dt.timedelta(days=30))


class ErasureTests(ClosureCase):
    def test_the_maintenance_erases_what_is_due_and_nothing_else(self):
        other = User.objects.create_user("other@example.org", PASSWORD, email_verified=True)
        waiting = User.objects.create_user("waiting@example.org", PASSWORD, email_verified=True)
        closure.close(self.user)
        closure.request_deletion(other)
        closure.request_deletion(waiting)
        today = timezone.localdate()
        self.assertEqual(closure.maintain(today), {"closed": 0, "deleting": 0})            # nothing is due yet
        self.assertEqual(closure.maintain(today + dt.timedelta(days=89)), {"closed": 0, "deleting": 0})
        User.objects.filter(pk=waiting.pk).update(erase_after=today + dt.timedelta(days=200))
        self.assertEqual(closure.maintain(today + dt.timedelta(days=90)), {"closed": 0, "deleting": 1})
        self.assertEqual(User.objects.get(pk=other.pk).state, "erased")
        self.assertEqual(User.objects.get(pk=self.user.pk).state, "closed")                # its year is not over
        self.assertEqual(closure.maintain(today + dt.timedelta(days=365)), {"closed": 1, "deleting": 1})
        self.assertEqual(closure.maintain(today + dt.timedelta(days=365)), {"closed": 0, "deleting": 0})   # and again: nothing

    def test_an_erased_account_leaves_its_id_and_nothing_of_the_person(self):
        self.sign_in_with_password()
        grant = self.grant()
        publisher = self.make_publisher()
        PublisherMember.objects.create(publisher=publisher, user=self.user, role=PublisherMember.ANALYST)
        closure.request_deletion(self.user)
        pk = self.user.pk
        closure.erase(User.objects.get(pk=pk))
        user = User.objects.get(pk=pk)                                # the row is still there, under its id
        self.assertEqual((user.state, user.email, user.name, user.is_active, user.email_verified, user.has_usable_password(), user.erase_after),
                         ("erased", f"erased-{pk}@erased.invalid", "", False, False, False, None))
        self.assertFalse(Grant.objects.filter(pk=grant.pk).exists())
        self.assertFalse(SignIn.objects.filter(user=user).exists())
        self.assertFalse(PublisherMember.objects.filter(user=user).exists())
        self.assertFalse(AuditEntry.objects.filter(actor_name="buyer@example.org").exists())   # the address is nowhere in the log
        self.assertTrue(AuditEntry.objects.filter(actor_name=f"account {pk}", action="account.delete_requested").exists())
        self.assertTrue(AuditEntry.objects.filter(action="account.erased", target=str(pk)).exists())
        # Nothing signs in to it, nothing offers it back, and no mail goes to it.
        self.client.post("/account/signout/")
        self.assertContains(self.sign_in_with_password(), "The email or the password is wrong.")
        mail.outbox.clear()
        self.client.post("/account/password/reset/", {"email": f"erased-{pk}@erased.invalid"})
        self.assertEqual(mail.outbox, [])
        # The address is free again: the same person signs up as somebody new.
        self.client.post("/account/signup/", {"email": "buyer@example.org", "name": "Bea", "password": PASSWORD, "password_again": PASSWORD})
        again = User.objects.filter(email="buyer@example.org").first()
        self.assertIsNotNone(again)
        self.assertNotEqual(again.pk, pk)
        closure.erase(user)   # erased twice is erased once
        self.assertEqual(AuditEntry.objects.filter(action="account.erased", target=str(pk)).count(), 1)

    def test_a_closed_account_holds_no_service(self):
        grant = self.grant()
        closure.close(self.user)
        grant.refresh_from_db()
        self.assertIsNotNone(grant.revoked)
        self.assertEqual(provider.services_of(User.objects.get(pk=self.user.pk)), [])

    def test_the_command_says_what_it_did(self):
        closure.request_deletion(self.user)
        out = io.StringIO()
        call_command("accounts", "leaving", stdout=out)
        self.assertIn(f"{self.user.pk}\tdeleting\terased after", out.getvalue())
        self.assertNotIn("buyer@example.org", out.getvalue())   # an id and a day, not an address
        User.objects.filter(pk=self.user.pk).update(erase_after=timezone.localdate())
        out = io.StringIO()
        call_command("accounts", "maintain", stdout=out)
        self.assertIn("0 closed account(s) past their time, 1 deletion(s) past their grace", out.getvalue())
        out = io.StringIO()
        call_command("accounts", "leaving", stdout=out)
        self.assertIn("nobody is leaving", out.getvalue())
