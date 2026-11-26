"""Signing up, proving the address, signing in.

With two hosts (identity/hosts.py) these pages are the account's own, at
its address; what they link to and send people on to in the store is named
by its whole address then.

Version: 0.8.0 - closing the account, deleting it, taking a deletion back,
                 and reactivating a closed one from the sign-in page
                 (accounts/closure.py)
Version: 0.4.0 - the account at an address of its own; signing in again when
                 a service asks for it; the services a person let in
Version: 0.1.0
"""

from __future__ import annotations

from django.contrib import messages
from django.contrib.auth import get_user_model, login, views as auth_views
from django.contrib.auth.decorators import login_required
from django.core.mail import send_mail
from django.shortcuts import redirect, render
from django.template.loader import render_to_string
from django.urls import reverse, reverse_lazy
from django.utils.formats import date_format

from django.views.decorators.http import require_POST

from django.conf import settings

from ..accounts import closure
from ..identity import hosts, provider, sessions
from ..identity.models import Client
from .forms import ClosedAccountResetForm, EmailForm, LeaveForm, ProfileForm, SignInForm, SignUpForm
from .utils import too_many

User = get_user_model()
BACKEND = "django.contrib.auth.backends.ModelBackend"


def send_verification(user) -> None:
    link = hosts.account_url("verify", user.verification_token())
    send_mail(f"{hosts.brand()}: confirm your email address", render_to_string("mail/verify_email.txt", {"user": user, "link": link, "brand": hosts.brand()}), None, [user.email])


def send_existing_account(user) -> None:
    link = hosts.account_url("password_reset")
    send_mail(f"{hosts.brand()}: you already have an account", render_to_string("mail/existing_account.txt", {"user": user, "link": link, "brand": hosts.brand()}), None, [user.email])


def signup(request):
    if request.user.is_authenticated:
        return redirect(hosts.store_url("console:home"))
    form = SignUpForm(request.POST or None)
    if request.method == "POST":
        if too_many(request, "signup", 10, 3600):
            form.add_error(None, "Too many sign-ups from this address. Try again in an hour.")
        elif form.is_valid():
            email = form.cleaned_data["email"]
            existing = User.objects.filter(email=email).first()
            # The answer is the same whether the address has an account or
            # not: only its owner learns which, by mail.
            if existing is None:
                user = User.objects.create_user(email, form.cleaned_data["password"], name=form.cleaned_data["name"])
                send_verification(user)
            elif existing.email_verified:
                send_existing_account(existing)
            else:
                send_verification(existing)
            return render(request, "account/check_mail.html", {"email": email})
    return render(request, "form_page.html", {
        "form": form, "title": "Create an account", "submit": "Create account",
        "lead": "One account for every ULTRA OS shop and service." if hosts.split() else "One account for the developer console. Publishing an app starts here.",
        "below": [("Already have an account?", "Sign in", reverse("account:signin"))]})


def verify(request, token: str):
    """The mailed link proves the address, once. It signs the person in
    only as it does so - the first time, and when nobody else is signed in
    here: a link that kept signing in would be a second password with two
    days of life, and one that replaced whoever is signed in would let its
    sender put a person into the sender's account without their noticing."""
    user = User.from_verification_token(token)
    if user is None or not user.is_active:
        return render(request, "account/verify_failed.html", status=400)
    first = not user.email_verified
    if first:
        user.email_verified = True
        user.save(update_fields=["email_verified"])
    if first and not request.user.is_authenticated:
        login(request, user, backend=BACKEND)
        messages.success(request, "Your email address is confirmed.")
        return redirect(hosts.store_url("console:home"))
    if request.user.is_authenticated and request.user.pk == user.pk:
        return redirect(hosts.store_url("console:home"))
    messages.success(request, "Your email address is confirmed. Sign in to go on." if first else "This address is already confirmed. Sign in to go on.")
    return redirect("account:signin")


def resend(request):
    form = EmailForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        if not too_many(request, "resend", 5, 3600):
            user = User.objects.filter(email=form.cleaned_data["email"], email_verified=False, is_active=True).first()
            if user:
                send_verification(user)
        return render(request, "account/check_mail.html", {"email": form.cleaned_data["email"]})
    return render(request, "form_page.html", {"form": form, "title": "Send the confirmation link again", "submit": "Send the link"})


class SignIn(auth_views.LoginView):
    template_name = "form_page.html"
    authentication_form = SignInForm
    redirect_authenticated_user = True
    extra_context = {
        "title": "Sign in", "submit": "Sign in",
        "below": [("New here?", "Create an account", reverse_lazy("account:signup")),
                  ("Forgot the password?", "Reset it", reverse_lazy("account:password_reset")),
                  ("No confirmation mail?", "Send it again", reverse_lazy("account:resend"))]}

    def dispatch(self, request, *args, **kwargs):
        # A service asked for the password again (prompt=login, max_age): the
        # page is shown to someone who is signed in, too.
        if request.GET.get("reauth"):
            self.redirect_authenticated_user = False
        return super().dispatch(request, *args, **kwargs)

    def get_initial(self):
        return {**super().get_initial(), "username": self.request.GET.get("login_hint", "")}

    def get_default_redirect_url(self):
        return reverse("account:profile") if hosts.split() else super().get_default_redirect_url()

    def form_valid(self, form):
        # Someone else signing in where a person is signed in: the first
        # one's sign-in ends here, and with it what followed it elsewhere.
        before = self.request.user
        earlier = self.request.session.get(sessions.SID, "") if before.is_authenticated and before.pk != form.get_user().pk else ""
        response = super().form_valid(form)
        sessions.end(earlier)
        return response

    def post(self, request, *args, **kwargs):
        if too_many(request, "signin", 10, 900, key=request.POST.get("username", "").strip().lower()):
            form = self.get_form()
            form.is_valid()
            form.add_error(None, "Too many attempts. Wait fifteen minutes and try again.")
            return self.form_invalid(form)
        # A closed account does not sign in; its owner, with the right
        # password, is offered it back.
        closed = closure.closed_account(request.POST.get("username", ""), request.POST.get("password", ""))
        if closed is not None:
            return render(request, "account/reactivate.html", {"token": closure.reactivation_token(closed), "erase_after": closed.erase_after})
        return super().post(request, *args, **kwargs)


class PasswordReset(auth_views.PasswordResetView):
    template_name = "form_page.html"
    form_class = ClosedAccountResetForm
    email_template_name = "mail/password_reset.txt"
    subject_template_name = "mail/password_reset_subject.txt"
    success_url = reverse_lazy("account:password_reset_done")
    extra_context = {"title": "Reset the password", "submit": "Mail me the link",
                     "lead": "We mail a link to the address, good for two days."}

    def form_valid(self, form):
        if too_many(self.request, "reset", 5, 3600):
            return redirect(self.success_url)
        self.extra_email_context = {"base_url": hosts.account_root(), "brand": hosts.brand()}   # the link under the account's root, whatever host was asked
        return super().form_valid(form)


class PasswordResetConfirm(auth_views.PasswordResetConfirmView):
    template_name = "form_page.html"
    success_url = reverse_lazy("account:signin")
    extra_context = {"title": "Choose a new password", "submit": "Set the password"}

    def form_valid(self, form):
        messages.success(self.request, "The password is changed. Sign in with the new one.")
        return super().form_valid(form)   # every grant and sign-in ends with it (accounts.User.save)


class PasswordChange(auth_views.PasswordChangeView):
    template_name = "form_page.html"
    success_url = reverse_lazy("account:profile")
    extra_context = {"title": "Change the password", "submit": "Change the password"}

    def form_valid(self, form):
        messages.success(self.request, "The password is changed.")
        # Everything the old password started ends (accounts.User.save) -
        # but for the sign-in it is being changed in.
        form.user.keep_sign_in = self.request.session.get(sessions.SID, "")
        return super().form_valid(form)


class SignOut(auth_views.LogoutView):
    def get_default_redirect_url(self):
        return reverse("account:signin") if hosts.split() else super().get_default_redirect_url()


@login_required
def profile(request):
    form = ProfileForm(request.POST or None, instance=request.user)
    if request.method == "POST" and form.is_valid():
        form.save()
        messages.success(request, "Saved.")
        return redirect("account:profile")
    return render(request, "account/profile.html", {
        "form": form, "console_url": hosts.store_url("console:home"), "services": provider.services_of(request.user)})


@login_required
def leave(request):
    """Close the account or ask for its deletion, with the password."""
    user = request.user
    refused = closure.refusal(user)
    form = LeaveForm(request.POST or None, user=user, closed_days=settings.ACCOUNT_CLOSED_DAYS, grace_days=settings.ACCOUNT_DELETION_GRACE_DAYS)
    if request.method == "POST" and not refused:
        if too_many(request, "leave", 5, 900, key=str(user.pk)):
            form.is_valid()
            form.add_error(None, "Too many attempts. Wait fifteen minutes and try again.")
        elif form.is_valid():
            if form.cleaned_data["what"] == LeaveForm.CLOSE:
                closure.close(user)
                messages.success(request, f"The account is closed. Sign in again before {date_format(user.erase_after, 'j F Y')} to bring it back; after that it is removed.")
                return redirect("account:signin")   # the sign-in ended with the account
            closure.request_deletion(user)
            messages.success(request, f"The account will be deleted on {date_format(user.erase_after, 'j F Y')}. Until then you can keep it, from this page.")
            return redirect("account:profile")
    return render(request, "account/leave.html", {"form": form, "refused": refused, "state": user.state, "erase_after": user.erase_after})


@login_required
@require_POST
def keep(request):
    """A deletion that was asked for, taken back."""
    if request.user.state == request.user.DELETING:
        closure.cancel_deletion(request.user)
        messages.success(request, "The deletion is cancelled. The account stays.")
    return redirect("account:profile")


@require_POST
def reactivate(request):
    """A closed account, brought back by whoever just gave its password on
    the sign-in page (the token says so, for a quarter of an hour)."""
    user = closure.from_reactivation_token(request.POST.get("token", ""))
    if user is None:
        messages.error(request, "That took too long, or the account is no longer closed. Sign in again.")
        return redirect("account:signin")
    closure.reactivate(user)
    login(request, user, backend=BACKEND)
    messages.success(request, "Welcome back. The account is as you left it; the services it was signed in to need signing in again.")
    return redirect("account:profile")


@login_required
@require_POST
def forget_service(request, client_id: str):
    """A service's access taken back: the consent and every token it holds."""
    client = Client.objects.filter(client_id=client_id).first()
    if client is not None:
        provider.forget(request.user, client)
        messages.success(request, f"{client.name} no longer has access to your account.")
    return redirect("account:profile")
