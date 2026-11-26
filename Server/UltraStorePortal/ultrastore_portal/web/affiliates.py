"""UltraAffiliates, phases 1 and 2: the affiliate's own pages, the links, the
publisher's boosts and the operator's review, block, ban and payout
(affiliates/services.py has the rules).

Every page here is a 404 while ULTRASTORE_PORTAL_AFFILIATES is not 1.

Version: 0.11.0 - the last link a browser followed (for the binding), and
                  an affiliate's customers by number
Version: 0.10.0
"""

from __future__ import annotations

import datetime as dt
from decimal import ROUND_HALF_UP, Decimal
from functools import wraps

from django import forms
from django.conf import settings
from django.contrib import messages
from django.contrib.auth.decorators import login_required
from django.core.exceptions import ValidationError
from django.db.models import Count, Q, Sum
from django.http import Http404
from django.shortcuts import get_object_or_404, redirect, render
from django.utils import timezone
from django.views.decorators.http import require_GET, require_POST

from ..affiliates import services, texts
from ..affiliates.models import AffiliatePayout, AffiliateProfile, Binding, Boost, Click, Commission, Sanction
from ..billing import tax
from ..store import constants
from ..store.models import App, PublisherMember
from .utils import operator_required, publisher_required, too_many

CENT = Decimal("0.01")


def _cents(rows, *names) -> list[dict]:
    """A database's sums as amounts: SQLite hands a sum back as a float
    with its tail, MySQL with the column's places - two places, always."""
    out = []
    for row in rows:
        row = dict(row)
        for name in names:
            row[name] = Decimal(str(row[name] or 0)).quantize(CENT, rounding=ROUND_HALF_UP)
        out.append(row)
    return out


CLICKS = "affiliate_clicks"       # the session's {app id or "": token}: the links this browser followed
LAST = "affiliate_last"           # the token of the last of them, whatever it led to: what a binding goes by
RECRUITER = "affiliate_recruiter"  # the handle whose recruiting link this browser followed


def affiliates_on(view):
    @wraps(view)
    def wrapper(request, *args, **kwargs):
        if not services.enabled():
            raise Http404
        return view(request, *args, **kwargs)
    return wrapper


# ----------------------------------------------------------------- the links

@require_GET
@affiliates_on
def go(request, handle: str, app_id: str = ""):
    """/r/<handle>/ and /r/<handle>/<app>/: the affiliate is noted, and the
    visitor goes on to the shop or the product. Only a person following a
    link counts: a page fetched as a picture, a script or a frame does not."""
    app = App.objects.public().filter(app_id=app_id).first() if app_id else None
    target = app.get_absolute_url() if app else "/"
    if app_id and app is None:
        raise Http404
    fetched_as = request.headers.get("Sec-Fetch-Dest", "document")
    profile = AffiliateProfile.objects.filter(handle__iexact=handle, state=AffiliateProfile.ACTIVE).first()
    if profile is not None and fetched_as == "document" and not too_many(request, "affiliate-click", 60, 3600):
        click = services.record_click(profile, app, request.GET.get("s", ""))
        kept = request.session.get(CLICKS) or {}
        kept[app.app_id if app else ""] = click.token
        request.session[CLICKS] = kept
        request.session[LAST] = click.token
    return redirect(target)


def ref_for(request, app: App) -> str:
    """The token of the affiliate's link to this product that this browser
    last followed, if any (the buy page sends it with the order)."""
    return (request.session.get(CLICKS) or {}).get(app.app_id, "") if services.enabled() else ""


def last_click(request) -> str:
    """The token of the last affiliate's link this browser followed, to a
    product or to the shop: a new customer is bound by it."""
    return request.session.get(LAST, "") if services.enabled() else ""


@require_GET
@affiliates_on
def join(request, handle: str):
    """A recruiting link: whoever follows it and becomes an affiliate has
    this affiliate as superaffiliate."""
    if AffiliateProfile.objects.filter(handle__iexact=handle, state=AffiliateProfile.ACTIVE).exists():
        request.session[RECRUITER] = handle
    return redirect("affiliate:home")


# ------------------------------------------------------ the affiliate's pages

class ApplyForm(forms.ModelForm):
    accept_terms = forms.BooleanField(
        label="I accept the affiliate terms: I say that it is advertising wherever I put a link, I do not bid on a merchant's name in search "
              "advertising, and I am eighteen or older", required=True)

    class Meta:
        model = AffiliateProfile
        fields = ["handle", "phone", "website", "channels", "legal_name", "vat_id", "payout_details"]
        widgets = {"channels": forms.Textarea(attrs={"rows": 3}), "payout_details": forms.Textarea(attrs={"rows": 3})}

    def clean_handle(self):
        handle = self.cleaned_data["handle"].strip()
        if AffiliateProfile.objects.filter(handle__iexact=handle).exists() or handle.lower().startswith("erased."):
            raise ValidationError("That name is taken.")
        return handle

    def clean_phone(self):
        phone = "".join(self.cleaned_data["phone"].split())
        digits = phone[1:] if phone.startswith("+") else phone
        if not phone.startswith("+") or not digits.isdigit() or not 7 <= len(digits) <= 15:
            raise ValidationError("With the country's code, digits only: +491701234567.")
        return phone


class ProfileEditForm(forms.ModelForm):
    class Meta:
        model = AffiliateProfile
        fields = ["website", "channels", "legal_name", "vat_id", "payout_details"]
        widgets = {"channels": forms.Textarea(attrs={"rows": 3}), "payout_details": forms.Textarea(attrs={"rows": 3})}


def _link(request, profile: AffiliateProfile, app: App | None = None) -> str:
    return f"{settings.BASE_URL}/r/{profile.handle}/" + (f"{app.app_id}/" if app else "")


def _products(q: str = "", limit: int = 200) -> list[dict]:
    apps = App.objects.listed().exclude(pricing_model="free").select_related("publisher").order_by("name")
    if q:
        apps = apps.filter(Q(name__icontains=q) | Q(summary__icontains=q) | Q(publisher__name__icontains=q) | Q(category__iexact=q))
    rows = []
    for app in apps[:limit]:
        base, boost = services.percent_for(app)
        rows.append({"app": app, "base": base, "boost": boost, "total": format((base + boost).normalize(), "f")})
    rows.sort(key=lambda row: (-(row["base"] + row["boost"]), row["app"].name.lower()))
    return rows


@login_required
@affiliates_on
def home(request):
    profile = services.profile_of(request.user)
    if profile is None:
        form = ApplyForm(request.POST or None)
        if request.method == "POST" and form.is_valid():
            data = form.cleaned_data
            try:
                profile = services.apply(request.user, recruiter=request.session.pop(RECRUITER, ""),
                                         **{name: data[name] for name in ApplyForm.Meta.fields})
            except services.AffiliateError as refused:
                form.add_error(None, str(refused))
            else:
                messages.success(request, "Your application is in. Prove your phone number below; the store then checks your identity.")
                return redirect("affiliate:home")
        return render(request, "form_page.html", {
            "form": form, "title": "Become an affiliate", "submit": "Apply",
            "lead": f"Advertise what the store sells and earn {format(settings.AFFILIATE_BASE_PERCENT.normalize(), 'f')} % of every sale your link "
                    "brings, and more where the publisher adds to it. Free, for you and for the publishers. An affiliate is a person the store "
                    "knows: you prove a phone number here, and the store checks an identity document before your links count."})
    recruits = []
    for recruit in profile.recruits.order_by("handle"):
        earned = Commission.objects.filter(affiliate=profile, basis=Commission.SUPER, share_of__affiliate=recruit).values("currency").annotate(sum=Sum("amount"))
        recruits.append({"handle": recruit.handle, "state": recruit.get_state_display(), "earned": _cents(earned, "sum")})
    since = timezone.now() - dt.timedelta(days=settings.AFFILIATE_LINK_DAYS)
    return render(request, "affiliate/home.html", {
        "profile": profile, "sums": services.sums(profile), "lines": profile.commissions.select_related("order__app", "share_of__affiliate")[:100],
        "payouts": profile.payouts.all()[:30], "clicks": Click.objects.filter(affiliate=profile, at__gte=since).count(),
        "by_app": (Click.objects.filter(affiliate=profile, at__gte=since).values("app__name", "app__app_id").annotate(clicks=Count("id")).order_by("-clicks")[:20]),
        "recruits": recruits, "general_link": _link(request, profile), "join_link": f"{settings.BASE_URL}/affiliate/join/{profile.handle}/",
        "products": _products(limit=12) if profile.active else [], "link_days": settings.AFFILIATE_LINK_DAYS,
        "approval_days": settings.AFFILIATE_APPROVAL_DAYS, "super_percent": format(settings.AFFILIATE_SUPER_PERCENT.normalize(), "f"),
        "sanctions": profile.sanctions.all(), "can_text": texts.can_send(), "edit_form": ProfileEditForm(instance=profile),
        "customers": Binding.objects.filter(affiliate=profile, state=Binding.ACTIVE).count(),
        "base_percent": format(settings.AFFILIATE_BASE_PERCENT.normalize(), "f")})


def _own_profile(request) -> AffiliateProfile:
    profile = services.profile_of(request.user)
    if profile is None:
        raise Http404
    return profile


@login_required
@require_POST
@affiliates_on
def phone(request):
    """Send the code to the profile's number, or take the code."""
    profile = _own_profile(request)
    if profile.phone_verified_at:
        return redirect("affiliate:home")
    if "code" in request.POST:
        if too_many(request, "affiliate-code", 10, 900, key=str(profile.pk)):
            messages.error(request, "Too many attempts. Wait fifteen minutes and try again.")
        elif services.confirm_phone(profile, request.POST.get("code", "")):
            messages.success(request, "Your phone number is proven.")
        else:
            messages.error(request, "That is not the code, or it is older than ten minutes. Ask for a new one.")
    elif too_many(request, "affiliate-text", 3, 3600, key=str(profile.pk)):
        messages.error(request, "Three texts an hour are enough. Try again later.")
    else:
        try:
            services.send_phone_code(profile)
            messages.success(request, f"A code is on its way to {profile.phone}.")
        except (services.AffiliateError, texts.TextError) as refused:
            messages.error(request, str(refused))
    return redirect("affiliate:home")


@login_required
@require_POST
@affiliates_on
def edit(request):
    profile = _own_profile(request)
    form = ProfileEditForm(request.POST, instance=profile)
    if form.is_valid():
        form.save()
        messages.success(request, "Saved.")
    else:
        messages.error(request, "Not saved: " + "; ".join(f"{name}: {', '.join(errors)}" for name, errors in form.errors.items()))
    return redirect("affiliate:home")


@login_required
@require_POST
@affiliates_on
def pause(request):
    """An affiliate's own pause, and its end: links earn nothing meanwhile."""
    profile = _own_profile(request)
    if profile.state == AffiliateProfile.ACTIVE:
        profile.state = AffiliateProfile.PAUSED
    elif profile.state == AffiliateProfile.PAUSED and profile.identified:
        profile.state = AffiliateProfile.ACTIVE
    profile.save(update_fields=["state"])
    return redirect("affiliate:home")


@login_required
@require_POST
@affiliates_on
def statement(request, pk: int):
    """What the affiliate says to a block or a ban: added to its record."""
    row = get_object_or_404(Sanction, pk=pk, affiliate=_own_profile(request))
    text = request.POST.get("statement", "").strip()
    if text:
        services.add_statement(row, text[:4000])
        messages.success(request, "Your statement is on the record; the store answers by mail.")
    return redirect("affiliate:home")


@login_required
@affiliates_on
def products(request):
    """Every product an affiliate can advertise, with what it pays."""
    profile = _own_profile(request)
    q = request.GET.get("q", "").strip()
    return render(request, "affiliate/products.html", {"profile": profile, "q": q, "products": _products(q), "link_days": settings.AFFILIATE_LINK_DAYS})


# ------------------------------------------------- the publisher's boosts

class BoostForm(forms.Form):
    scope = forms.ChoiceField(choices=Boost.SCOPES)
    app = forms.ModelChoiceField(queryset=App.objects.none(), required=False)
    category = forms.ChoiceField(choices=[("", "---------")] + list(constants.CATEGORIES), required=False)
    percent = forms.DecimalField(max_digits=5, decimal_places=2, label="Boost, in percent of the net price",
                                 help_text=f"From {services.BOOST_MIN} to {services.BOOST_MAX}. Taken from your proceeds of a sale an affiliate's link brought.")
    renewals = forms.BooleanField(required=False, label="Also on a subscription's renewals")
    valid_from = forms.DateField(required=False, widget=forms.DateInput(attrs={"type": "date"}))
    valid_until = forms.DateField(required=False, widget=forms.DateInput(attrs={"type": "date"}))

    def __init__(self, *args, publisher, **kwargs):
        super().__init__(*args, **kwargs)
        self.fields["app"].queryset = App.objects.filter(publisher=publisher).exclude(pricing_model="free").order_by("name")


@publisher_required(roles=None)
@affiliates_on
def console_affiliates(request):
    """The publisher's side: nothing to switch on - every paid app is in
    the programme - and the boosts its owner may add."""
    publisher = request.publisher
    owner = request.publisher_role == PublisherMember.OWNER
    form = BoostForm(request.POST or None, publisher=publisher)
    if request.method == "POST":
        if "delete" in request.POST:
            if owner:
                row = get_object_or_404(Boost, pk=request.POST["delete"], publisher=publisher)
                services.audit(request.user, "affiliate.boost_removed", "publisher", publisher.slug, percent=str(row.percent), scope=row.scope)
                row.delete()
                messages.success(request, "The boost is removed. Sales already made keep it.")
            return redirect("console:affiliates")
        if form.is_valid():
            data = form.cleaned_data
            try:
                services.save_boost(publisher, request.user, scope=data["scope"], percent=data["percent"], app=data["app"], category=data["category"],
                                    renewals=data["renewals"], valid_from=data["valid_from"], valid_until=data["valid_until"])
                messages.success(request, "The boost is saved, and counts for sales from now on.")
                return redirect("console:affiliates")
            except services.AffiliateError as refused:
                form.add_error(None, str(refused))
    today = timezone.localdate()
    apps = []
    for app in App.objects.filter(publisher=publisher).exclude(pricing_model="free").order_by("name"):
        base, boost = services.percent_for(app, today)
        apps.append({"app": app, "base": base, "boost": boost})
    sold = (Commission.objects.filter(order__app__publisher=publisher, basis=Commission.PRODUCT_LINK)
            .values("affiliate__handle", "currency").annotate(lines=Count("id", filter=Q(amount__gt=0)), boost=Sum("merchant_part"))
            .order_by("affiliate__handle", "currency"))
    return render(request, "console/affiliates.html", {
        "form": form, "owner": owner, "boosts": Boost.objects.filter(publisher=publisher).select_related("app"), "apps": apps,
        "sold": _cents(sold, "boost"), "base_percent": format(settings.AFFILIATE_BASE_PERCENT.normalize(), "f"), "today": today})


# ------------------------------------------------------------ the operator's

class ReviewForm(forms.Form):
    id_name = forms.CharField(label="Name on the document", max_length=200)
    id_birth_date = forms.DateField(label="Date of birth", widget=forms.DateInput(attrs={"type": "date"}))
    id_document = forms.ChoiceField(label="Document", choices=AffiliateProfile.DOCUMENTS)
    id_country = forms.CharField(label="Issuing country", max_length=2, help_text="Two letters, such as DE.")
    id_expires = forms.DateField(label="Valid until", required=False, widget=forms.DateInput(attrs={"type": "date"}))
    phone_confirmed = forms.BooleanField(required=False, label="I confirmed the phone number myself (where no code was entered)")

    def clean_id_country(self):
        country = tax.clean_country(self.cleaned_data["id_country"])
        if not country:
            raise ValidationError("Two letters, such as DE.")
        return country


class SanctionForm(forms.Form):
    kind = forms.ChoiceField(choices=[(Sanction.BLOCK, "Block, for a time or until lifted"), (Sanction.BAN, "Ban, for good")])
    rule = forms.CharField(max_length=200, label="The rule", help_text="The clause of the affiliate terms.")
    facts = forms.CharField(widget=forms.Textarea(attrs={"rows": 3}), label="What happened", help_text="When, and with which merchant's products. The affiliate is told this.")
    evidence = forms.CharField(widget=forms.Textarea(attrs={"rows": 2}), required=False, help_text="What the decision rests on.")
    until = forms.DateField(required=False, label="A block's last day", widget=forms.DateInput(attrs={"type": "date"}))
    money = forms.CharField(widget=forms.Textarea(attrs={"rows": 2}), required=False, label="The money", help_text="Which commissions are taken back, and what is paid.")


@operator_required
@affiliates_on
def operator_affiliates(request):
    state = request.GET.get("state", AffiliateProfile.APPLIED)
    found = AffiliateProfile.objects.select_related("user", "superaffiliate")
    if state in dict(AffiliateProfile.STATES):
        found = found.filter(state=state)
    owed = {}
    for line in _cents(Commission.objects.filter(payout__isnull=True).exclude(state=Commission.PENDING).values("affiliate_id", "currency")
                       .annotate(sum=Sum("amount")), "sum"):
        if line["sum"] > 0:
            owed.setdefault(line["affiliate_id"], []).append(line)
    rows = [{"profile": profile, "owed": owed.get(profile.pk, [])} for profile in found[:300]]
    return render(request, "operator/affiliates.html", {
        "rows": rows, "state": state, "states": AffiliateProfile.STATES, "cost": services.cost(),
        "to_pay": sum(1 for lines in owed.values() if lines), "can_text": texts.can_send()})


@operator_required
@affiliates_on
def operator_affiliate(request, pk: int):
    profile = get_object_or_404(AffiliateProfile.objects.select_related("user", "superaffiliate"), pk=pk)
    review, sanction_form = ReviewForm(prefix="review"), SanctionForm(prefix="sanction")
    if request.method == "POST":
        action = request.POST.get("action", "")
        try:
            if action == "activate":
                review = ReviewForm(request.POST, prefix="review")
                if review.is_valid():
                    services.activate(profile, request.user, **review.cleaned_data)
                    messages.success(request, f"{profile.handle} is an affiliate.")
                    return redirect("operator:affiliate", pk)
            elif action == "sanction":
                sanction_form = SanctionForm(request.POST, prefix="sanction")
                if sanction_form.is_valid():
                    data = sanction_form.cleaned_data
                    services.sanction(profile, request.user, data["kind"], rule=data["rule"], facts=data["facts"], evidence=data["evidence"],
                                      until=data["until"], money=data["money"])
                    messages.success(request, "Done, and on the record. Tell the affiliate the rule and what happened: the page shows them both.")
                    return redirect("operator:affiliate", pk)
            elif action == "lift":
                services.lift(profile, request.user, request.POST.get("note", ""))
                messages.success(request, "The block is lifted.")
                return redirect("operator:affiliate", pk)
            elif action == "super":
                text = request.POST.get("super_percent", "").strip()
                profile.super_percent = None if not text else forms.DecimalField(max_digits=5, decimal_places=2, min_value=0, max_value=50).clean(text)
                profile.save(update_fields=["super_percent"])
                services.audit(request.user, "affiliate.super_percent", "affiliate", profile.handle, percent=text or "default")
                messages.success(request, "The superaffiliate's share is set.")
                return redirect("operator:affiliate", pk)
            elif action == "pay":
                services.pay(profile, request.POST.get("currency", ""), request.user, request.POST.get("reference", ""), request.POST.get("note", ""))
                messages.success(request, "The payout is written down.")
                return redirect("operator:affiliate", pk)
        except (services.AffiliateError, ValidationError) as refused:
            messages.error(request, refused.messages[0] if isinstance(refused, ValidationError) else str(refused))
        profile.refresh_from_db()
    return render(request, "operator/affiliate.html", {
        "profile": profile, "review": review, "sanction_form": sanction_form, "sums": services.sums(profile),
        "lines": profile.commissions.select_related("order__app", "share_of__affiliate")[:100], "payouts": profile.payouts.all()[:30],
        "sanctions": profile.sanctions.select_related("decided_by"), "recruits": profile.recruits.order_by("handle"),
        "default_super": format(settings.AFFILIATE_SUPER_PERCENT.normalize(), "f"), "can_text": texts.can_send(),
        "customers": Binding.objects.filter(affiliate=profile, state=Binding.ACTIVE).count()})
