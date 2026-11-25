"""UltraAds, the pages (Docs/Research/UltraAdsDesignProposal.md, phase 1).

    The publisher's
      /console/apps/<id>/promote/   Promote: the shop's door - where, how much, how long
      /promote/                     every campaign, with what it cost and brought
      /promote/campaigns/<n>/       one campaign: its report, its settings, pause and end
      /promote/balance/             the balance, what went in, what was spent by day, paying in
      /promote/topup/<id>/...       the hand-off to the payment provider, and back

    Everyone's
      /promote/go/<token>/          a click on an advertisement: counted, then the listing
      /promote/why/<token>/         who paid for it and what it matched
      /promote/report/<token>/      Report this advertisement

    The operator's
      /operator/promote/            the placements and their prices, the settings, every
                                    campaign with its stop, clicks to credit, vouchers

Version: 0.5.0
"""

from __future__ import annotations

from decimal import Decimal, InvalidOperation
from functools import wraps

from django import forms
from django.conf import settings
from django.contrib import messages
from django.http import Http404
from django.shortcuts import get_object_or_404, redirect, render
from django.utils import timezone
from django.views.decorators.cache import never_cache
from django.views.decorators.http import require_POST

from ..billing import services as billing
from ..billing import skrill
from ..promote import decide, services
from ..promote.models import AREAS, HOME_AREA, Campaign, Click, LedgerEntry, Placement, TopUp
from ..store.models import App, Publisher, Report
from .forms import ReportForm
from .utils import client_address, operator_required, publisher_required, too_many


def promote_on(view):
    """Advertising is a switch (ULTRASTORE_PORTAL_PROMOTE): off, none of
    its pages exist."""
    @wraps(view)
    def wrapper(request, *args, **kwargs):
        if not services.enabled():
            raise Http404
        return view(request, *args, **kwargs)
    return wrapper


# ------------------------------------------------------------------- the form

class CampaignForm(forms.Form):
    daily_budget = forms.DecimalField(label="Budget per day", min_value=Decimal("1.00"), max_digits=8, decimal_places=2,
                                      help_text="The most this campaign spends in a day. Spread over the day.")
    max_click = forms.DecimalField(label="The most to pay for a click", required=False, min_value=Decimal("0.01"), max_digits=6, decimal_places=2,
                                   help_text="Leave empty and the store bids for you, up to the budget. You never pay more than it took to be shown.")
    ends = forms.DateField(label="Runs until", required=False, widget=forms.DateInput(attrs={"type": "date"}),
                           help_text="Leave empty to run until you stop it.")
    total_budget = forms.DecimalField(label="Budget in all", required=False, min_value=Decimal("1.00"), max_digits=10, decimal_places=2,
                                      help_text="Leave empty for no limit but the daily one.")

    def __init__(self, *args, publisher: Publisher, campaign: Campaign | None = None, **kwargs):
        super().__init__(*args, **kwargs)
        self.publisher, self.campaign = publisher, campaign
        self.fields["daily_budget"].help_text += f" In {settings.PROMOTE_CURRENCY}."
        self.places: list[Placement] = []

    def clean_ends(self):
        ends = self.cleaned_data["ends"]
        if ends and ends < timezone.localdate():
            raise forms.ValidationError("A day that is still to come.")
        return ends

    def clean(self):
        data = super().clean()
        keys = self.data.getlist("places") if hasattr(self.data, "getlist") else list(self.data.get("places", []))
        self.places = list(Placement.objects.filter(key__in=keys))
        if "daily_budget" in data:
            try:
                services.check_campaign(self.publisher, self.places, data["daily_budget"], data.get("max_click"))
            except services.Refused as refused:
                raise forms.ValidationError(str(refused)) from None
        if data.get("total_budget") and data.get("daily_budget") and data["total_budget"] < data["daily_budget"]:
            self.add_error("total_budget", "At least one day's budget.")
        return data


def where_list(ticked: set[str]) -> list[dict]:
    """Every shop and marketplace with its placements, for the Promote
    page: this shop's ticked, the others there to tick - and what is not
    open listed too, with the reason, so the list is the same for everyone."""
    placements = list(Placement.objects.all())
    groups = []
    for area, name in AREAS:
        rows = [p for p in placements if p.area == area]
        if not rows:
            continue
        groups.append({
            "area": area, "name": name, "home": area == HOME_AREA, "open": any(p.is_open for p in rows),
            "rows": [{"placement": p, "ticked": p.key in ticked and p.is_open,
                      "reason": "" if p.is_open else ("This shop is not open yet." if p.state == Placement.PLANNED else "Switched off by the store for now.")}
                     for p in rows]})
    return groups


def _own_app(request, app_id: str) -> App:
    return get_object_or_404(App, app_id=app_id, publisher=request.publisher)


def _own_campaign(request, pk: int) -> Campaign:
    return get_object_or_404(Campaign.objects.select_related("app", "publisher"), pk=pk, publisher=request.publisher)


def _campaign_page(request, app: App, campaign: Campaign | None):
    """The Promote form: a new campaign for a listing, or one being changed."""
    balance = services.balance_of(request.publisher)
    initial = {"daily_budget": Decimal("5.00")}
    if campaign is not None:
        initial = {"daily_budget": campaign.daily_budget, "max_click": campaign.max_click, "ends": campaign.ends, "total_budget": campaign.total_budget}
    form = CampaignForm(request.POST or None, initial=initial, publisher=request.publisher, campaign=campaign)
    if request.method == "POST":
        ticked = set(request.POST.getlist("places"))
    elif campaign is not None:
        ticked = set(campaign.placements.values_list("key", flat=True))
    else:   # this shop, and nothing else: every other place is the publisher's own tick
        ticked = set(Placement.objects.filter(area=HOME_AREA, state=Placement.OPEN).values_list("key", flat=True))
    if request.method == "POST" and form.is_valid():
        target = campaign or Campaign(publisher=request.publisher, app=app, name=app.name, created_by=request.user)
        for name in ("daily_budget", "max_click", "ends", "total_budget"):
            setattr(target, name, form.cleaned_data[name])
        try:
            services.save_campaign(target, form.places, request.user)
        except services.Refused as refused:
            form.add_error(None, str(refused))
        else:
            messages.success(request, "Saved." if campaign else f"{app.name} is being promoted." +
                             ("" if balance.amount > 0 else " It is shown as soon as there is a balance."))
            return redirect("promote:campaign", target.pk)
    return render(request, "promote/campaign_form.html", {
        "app": app, "campaign": campaign, "form": form, "groups": where_list(ticked), "balance": balance,
        "currency": settings.PROMOTE_CURRENCY, "public": app.is_public and app.visibility == "listed" and app.kind == "app",
        "is_new": services.is_new(request.publisher), "new_cap": services.number("new_daily_cap"), "new_days": services.setting("new_days")})


@promote_on
@publisher_required
def promote_app(request, app_id: str):
    """The shop's door: Promote on a listing."""
    return _campaign_page(request, _own_app(request, app_id), None)


@promote_on
@publisher_required
def home(request):
    balance = services.balance_of(request.publisher)
    campaigns = list(Campaign.objects.filter(publisher=request.publisher).select_related("app"))
    rows = []
    for campaign in campaigns:
        word, sentence = campaign.standing(balance.amount)
        rows.append({"campaign": campaign, "word": word, "sentence": sentence, "total": services.report([campaign])["total"]})
    return render(request, "promote/home.html", {
        "rows": rows, "balance": balance, "currency": settings.PROMOTE_CURRENCY, "report": services.report(campaigns),
        "apps": request.publisher.apps.filter(kind="app").order_by("name")})


@promote_on
@publisher_required
def campaign(request, pk: int):
    subject = _own_campaign(request, pk)
    balance = services.balance_of(request.publisher)
    word, sentence = subject.standing(balance.amount)
    return render(request, "promote/campaign.html", {
        "campaign": subject, "app": subject.app, "word": word, "sentence": sentence, "report": services.report([subject]),
        "places": subject.placements.all(), "balance": balance, "currency": settings.PROMOTE_CURRENCY})


@promote_on
@publisher_required
def campaign_edit(request, pk: int):
    subject = _own_campaign(request, pk)
    if subject.state == Campaign.ENDED:
        messages.error(request, "An ended campaign stays ended. Start a new one.")
        return redirect("promote:campaign", subject.pk)
    return _campaign_page(request, subject.app, subject)


@promote_on
@require_POST
@publisher_required
def campaign_state(request, pk: int, state: str):
    subject = _own_campaign(request, pk)
    try:
        services.set_state(subject, {"pause": Campaign.PAUSED, "resume": Campaign.ACTIVE, "end": Campaign.ENDED}.get(state, ""), request.user)
    except services.Refused as refused:
        messages.error(request, str(refused))
    return redirect("promote:campaign", subject.pk)


# -------------------------------------------------------------------- balance

class TopUpForm(forms.Form):
    amount = forms.DecimalField(label="Amount", max_digits=8, decimal_places=2, min_value=Decimal("1.00"))


@promote_on
@publisher_required
def balance(request):
    possible, why_not = services.topups_open()
    form = TopUpForm(request.POST or None, initial={"amount": services.number("min_topup")})
    if request.method == "POST" and form.is_valid():
        try:
            made = services.open_topup(request.publisher, form.cleaned_data["amount"], request.user)
        except services.Refused as refused:
            form.add_error("amount", str(refused))
        else:
            return redirect("promote:topup", made.public_id)
    return render(request, "promote/balance.html", {
        "balance": services.balance_of(request.publisher), "currency": settings.PROMOTE_CURRENCY, "form": form, "possible": possible, "why_not": why_not,
        "entries": LedgerEntry.objects.filter(publisher=request.publisher)[:50], "spent": services.spent_by_day(request.publisher),
        "min_topup": services.number("min_topup")})


def _own_topup(request, public_id: str) -> TopUp:
    return get_object_or_404(TopUp, public_id=public_id, publisher=request.publisher)


@promote_on
@publisher_required
def topup(request, public_id: str):
    """The page that sends the publisher on to the payment provider."""
    made = _own_topup(request, public_id)
    if made.status == TopUp.PAID:
        return redirect("promote:balance")
    context = {"topup": made, "amount": services.money(made.amount)}
    if made.provider == "test":
        return render(request, "promote/topup_test.html", context)
    root = settings.BASE_URL
    fields = {
        "pay_to_email": settings.SKRILL_PAY_TO_EMAIL, "recipient_description": "UltraStore", "transaction_id": made.public_id,
        "return_url": f"{root}/promote/topup/{made.public_id}/return/", "return_url_text": "Back to UltraStore",
        "cancel_url": f"{root}/promote/topup/{made.public_id}/cancel/", "status_url": f"{root}/v1/promote/skrill/status",
        "language": "EN", "amount": services.money(made.amount), "currency": made.currency,
        "detail1_description": "Advertising balance:", "detail1_text": made.publisher.name,
    }
    return render(request, "promote/topup.html", {**context, "pay_url": settings.SKRILL_PAY_URL, "fields": fields})


@promote_on
@require_POST
@publisher_required
def topup_test(request, public_id: str):
    """The test provider's button: the provider's report, signed with the
    test secret, through the code the provider's own goes through."""
    if settings.PAYMENT_PROVIDER != "test":
        raise Http404
    made = _own_topup(request, public_id)
    outcome = request.POST.get("outcome", "paid")
    status = {"paid": skrill.STATUS_PROCESSED, "failed": skrill.STATUS_FAILED}.get(outcome, skrill.STATUS_CANCELLED)
    try:
        services.topup_report(services.test_topup_report(made, status))
    except billing.BillingError as refused:
        messages.error(request, refused.description)
    return redirect("promote:topup_return", public_id)


@promote_on
@publisher_required
def topup_return(request, public_id: str):
    """Where the provider sends the publisher back. It proves nothing: the
    balance grew when the provider's own report said so."""
    made = _own_topup(request, public_id)
    if made.status == TopUp.PAID:
        messages.success(request, f"{services.money(made.amount)} {made.currency} are in your balance. Receipt no. {made.receipt_no}.")
    elif made.status == TopUp.OPEN:
        messages.info(request, "The payment is being confirmed. The balance grows as soon as the confirmation arrives.")
    else:
        messages.warning(request, "Nothing was paid in.")
    return redirect("promote:balance")


@promote_on
@publisher_required
def topup_cancel(request, public_id: str):
    made = _own_topup(request, public_id)
    if made.status == TopUp.OPEN:
        made.status = TopUp.CANCELLED
        made.save(update_fields=["status"])
    messages.info(request, "Nothing was charged.")
    return redirect("promote:balance")


# ------------------------------------------------------------- everyone's pages

CLICK = "promote_click"


@never_cache
def go(request, token: str):
    """A click on an advertisement on one of the store's pages: counted,
    kept in this browser's session for the order that may follow, then the
    listing. A link that is no advertisement's (or an old one) is the
    store's front page."""
    data = services.read_token(token)
    campaign = Campaign.objects.select_related("app", "publisher").filter(pk=data["c"]).first() if data and services.enabled() else None
    if campaign is None:
        return redirect("store:home")
    click = services.clicked(token, address=client_address(request), own=services.is_own(request.user, campaign.publisher))
    if click is not None and click.valid:
        request.session[CLICK] = {"token": token, "app": campaign.app.app_id}
    return redirect(campaign.app)


def clicked_token(request, app: App) -> str:
    """The token of the advertisement for this listing that this browser
    last clicked, if any."""
    kept = request.session.get(CLICK) or {}
    return kept.get("token", "") if kept.get("app") == app.app_id else ""


def _ad(token: str):
    data = services.read_token(token, max_age=30 * 24 * 3600)
    if data is None or not services.enabled():
        raise Http404
    campaign = get_object_or_404(Campaign.objects.select_related("app", "publisher"), pk=data["c"])
    placement = get_object_or_404(Placement, key=data["p"])
    return data, campaign, placement


def why(request, token: str):
    data, campaign, placement = _ad(token)
    return render(request, "promote/why.html", {
        "campaign": campaign, "app": campaign.app, "placement": placement, "token": token, "reasons": decide.explain(placement, data.get("w") or {})})


def report_ad(request, token: str):
    data, campaign, placement = _ad(token)
    form = ReportForm(request.POST or None)
    if request.method == "POST" and form.is_valid():
        if too_many(request, "report", 10, 3600):
            form.add_error(None, "Too many reports from this address. Try again later.")
        else:
            Report.objects.create(target_kind="ad", target=f"campaign {campaign.pk}: {campaign.app.app_id} in {placement.key}", **form.cleaned_data)
            messages.success(request, "Thank you. The report is with the store's operator.")
            return redirect(campaign.app)
    return render(request, "form_page.html", {
        "form": form, "title": f"Report the advertisement for {campaign.app.name}", "submit": "Send the report",
        "lead": "The report goes to the store's operator. The advertiser sees the report, never who sent it."})


# ---------------------------------------------------------------- the operator's

def _decimal(text: str) -> Decimal | None:
    try:
        value = Decimal((text or "").strip())
    except InvalidOperation:
        return None
    return value if value.is_finite() else None


@operator_required
def operator_home(request):
    if request.method == "POST":
        try:
            _operator_action(request)
        except services.Refused as refused:
            messages.error(request, str(refused))
        return redirect("operator:promote")
    return render(request, "promote/operator.html", {
        "enabled": services.enabled(), "topups": services.topups_open(), "currency": settings.PROMOTE_CURRENCY,
        "placements": Placement.objects.all(), "settings_rows": [(name, services.setting(name), default) for name, default in services.DEFAULTS.items()],
        "campaigns": Campaign.objects.select_related("app", "publisher")[:200], "clicks": Click.objects.select_related("campaign__publisher", "placement")[:50],
        "revenue": services.revenue(), "publishers": Publisher.objects.filter(state=Publisher.APPROVED)})


def _operator_action(request) -> None:
    action, user = request.POST.get("action", ""), request.user
    if action == "placement":
        placement = get_object_or_404(Placement, pk=request.POST.get("placement"))
        floor, slots, state = _decimal(request.POST.get("floor", "")), request.POST.get("slots", ""), request.POST.get("state", "")
        if floor is None or floor < Decimal("0.01") or not slots.isdigit() or not 1 <= int(slots) <= 12:
            raise services.Refused("A floor is a price of a cent or more, and a placement holds one to twelve advertisements.")
        if state not in (Placement.OPEN, Placement.CLOSED) or placement.state == Placement.PLANNED:
            state = placement.state   # a shop that is not built is not opened from here
        placement.floor, placement.slots, placement.state = floor, int(slots), state
        placement.save()
        services.audit(user, "promote.placement", "placement", placement.key, floor=services.money(floor), slots=int(slots), state=state)
        messages.success(request, f"{placement}: saved.")
    elif action == "setting":
        services.set_setting(request.POST.get("name", ""), request.POST.get("value", "").strip(), user)
        messages.success(request, "Saved.")
    elif action in ("stop", "release"):
        subject = get_object_or_404(Campaign, pk=request.POST.get("campaign"))
        services.stop(subject, user, action == "stop", request.POST.get("reason", ""))
        messages.success(request, f"{subject.name}: {'stopped' if action == 'stop' else 'released'}.")
    elif action == "credit":
        services.invalidate(get_object_or_404(Click.objects.select_related("campaign__publisher"), pk=request.POST.get("click")), user)
        messages.success(request, "The click's price went back to the balance.")
    elif action == "voucher":
        publisher = get_object_or_404(Publisher, slug=request.POST.get("publisher", ""))
        amount = _decimal(request.POST.get("amount", ""))
        if amount is None or amount <= 0 or amount > 10000:
            raise services.Refused("A voucher is an amount of money.")
        services.credit(publisher, LedgerEntry.VOUCHER, amount, request.POST.get("note", "").strip() or "A voucher from the store", user)
        messages.success(request, f"{services.money(amount)} {settings.PROMOTE_CURRENCY} are in the balance of {publisher.name}.")
    else:
        raise services.Refused("Nothing was chosen.")
