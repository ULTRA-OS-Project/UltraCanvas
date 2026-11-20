"""Signing up, proving the address, signing in.

Version: 0.1.0
"""

from __future__ import annotations

from django.conf import settings
from django.contrib import messages
from django.contrib.auth import get_user_model, login, views as auth_views
from django.contrib.auth.decorators import login_required
from django.core.mail import send_mail
from django.shortcuts import redirect, render
from django.template.loader import render_to_string
from django.urls import reverse, reverse_lazy

from .forms import EmailForm, ProfileForm, SignInForm, SignUpForm
from .utils import too_many

User = get_user_model()
BACKEND = "django.contrib.auth.backends.ModelBackend"


def send_verification(user) -> None:
    link = settings.BASE_URL + reverse("account:verify", args=[user.verification_token()])
    send_mail("UltraStore: confirm your email address", render_to_string("mail/verify_email.txt", {"user": user, "link": link}), None, [user.email])


def send_existing_account(user) -> None:
    link = settings.BASE_URL + reverse("account:password_reset")
    send_mail("UltraStore: you already have an account", render_to_string("mail/existing_account.txt", {"user": user, "link": link}), None, [user.email])


def signup(request):
    if request.user.is_authenticated:
        return redirect("console:home")
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
        "lead": "One account for the developer console. Publishing an app starts here.",
        "below": [("Already have an account?", "Sign in", reverse("account:signin"))]})


def verify(request, token: str):
    user = User.from_verification_token(token)
    if user is None or not user.is_active:
        return render(request, "account/verify_failed.html", status=400)
    if not user.email_verified:
        user.email_verified = True
        user.save(update_fields=["email_verified"])
    login(request, user, backend=BACKEND)
    messages.success(request, "Your email address is confirmed.")
    return redirect("console:home")


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

    def post(self, request, *args, **kwargs):
        if too_many(request, "signin", 10, 900, key=request.POST.get("username", "").strip().lower()):
            form = self.get_form()
            form.is_valid()
            form.add_error(None, "Too many attempts. Wait fifteen minutes and try again.")
            return self.form_invalid(form)
        return super().post(request, *args, **kwargs)


class PasswordReset(auth_views.PasswordResetView):
    template_name = "form_page.html"
    email_template_name = "mail/password_reset.txt"
    subject_template_name = "mail/password_reset_subject.txt"
    success_url = reverse_lazy("account:password_reset_done")
    extra_context = {"title": "Reset the password", "submit": "Mail me the link",
                     "lead": "We mail a link to the address, good for two days."}

    def form_valid(self, form):
        if too_many(self.request, "reset", 5, 3600):
            return redirect(self.success_url)
        self.extra_email_context = {"base_url": settings.BASE_URL}   # the link under the store's root, whatever host was asked
        return super().form_valid(form)


class PasswordResetConfirm(auth_views.PasswordResetConfirmView):
    template_name = "form_page.html"
    success_url = reverse_lazy("account:signin")
    extra_context = {"title": "Choose a new password", "submit": "Set the password"}

    def form_valid(self, form):
        messages.success(self.request, "The password is changed. Sign in with the new one.")
        return super().form_valid(form)


class PasswordChange(auth_views.PasswordChangeView):
    template_name = "form_page.html"
    success_url = reverse_lazy("account:profile")
    extra_context = {"title": "Change the password", "submit": "Change the password"}

    def form_valid(self, form):
        messages.success(self.request, "The password is changed.")
        return super().form_valid(form)


@login_required
def profile(request):
    form = ProfileForm(request.POST or None, instance=request.user)
    if request.method == "POST" and form.is_valid():
        form.save()
        messages.success(request, "Saved.")
        return redirect("account:profile")
    return render(request, "account/profile.html", {"form": form})
