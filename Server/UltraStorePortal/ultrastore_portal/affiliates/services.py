"""UltraAffiliates, phase 1: product links.

The rules, from Docs/Research/UltraAffiliatesDesignProposal.md and the
owner's decisions in its section 9:

- **Free for the merchant, and every product is in it.** Any active
  affiliate may take a link to any public paid app. There is nothing to
  join and nothing to opt out of.
- **A link to a product wins that product** for AFFILIATE_LINK_DAYS (90):
  the last such link an order carries, by an affiliate who may earn on it -
  active, not a member of the publisher selling it, not the buyer.
- **The commission is a base and a boost.** The base, AFFILIATE_BASE_PERCENT
  (5) of the net price, is the operator's cost, out of the store's fee. The
  boost is what the merchant set for the product (1 to 45 % of the net),
  taken from the merchant's proceeds: the payment's `boost_amount`, which
  the publisher's share and payout leave out.
- **The superaffiliate's share**, AFFILIATE_SUPER_PERCENT (10) of the
  affiliate's whole commission unless the operator set another for that
  affiliate, on top and at the operator's cost. One level up, no further.
- **Pending, then approved** after AFFILIATE_APPROVAL_DAYS (30). A refund
  or a chargeback takes the commission back in the proportion of the money,
  as a line below zero - the affiliate's, the superaffiliate's and the
  merchant's boost alike.
- **Identified.** A profile is active once its phone number is proven and
  the operator has checked an identity document and written down what it
  says - one profile per person.
- **Blocked or banned** affiliates earn nothing; each has a record.

Not in phase 1 (the proposal's phases 2 and 3): the binding of a customer
to an affiliate, which needs the buyer's account on an order; the 5 %
holdback; statements and self-billed credit notes; fixed boosts for
hardware; codes; the native store's links.

Version: 0.10.0
"""

from __future__ import annotations

import datetime as dt
import hashlib
import hmac
import secrets
from decimal import ROUND_HALF_UP, Decimal

from django.conf import settings
from django.db import transaction
from django.db.models import Q
from django.utils import timezone

from ..billing import split
from ..billing.models import Payment
from ..store.models import App, PublisherMember
from ..store.services import audit
from . import texts
from .models import AffiliatePayout, AffiliateProfile, Attribution, Boost, Click, ClickDay, Commission, Sanction

CENT = Decimal("0.01")
ZERO = Decimal("0.00")
BOOST_MIN, BOOST_MAX = Decimal("1"), Decimal("45")
CODE_MINUTES = 10
CODE_TRIES = 5


class AffiliateError(Exception):
    pass


def enabled() -> bool:
    return settings.AFFILIATES


def part(amount: Decimal, percent: Decimal) -> Decimal:
    return (amount * percent / 100).quantize(CENT, rounding=ROUND_HALF_UP)


# ---------------------------------------------------------------- profiles

def profile_of(user) -> AffiliateProfile | None:
    if not getattr(user, "is_authenticated", False):
        return None
    return AffiliateProfile.objects.filter(user=user).first()


@transaction.atomic
def apply(user, *, handle: str, phone: str, website: str = "", channels: str = "", legal_name: str = "", vat_id: str = "",
          payout_details: str = "", recruiter: str = "") -> AffiliateProfile:
    """A person asks to be an affiliate. Whoever's recruiting link brought
    them is their superaffiliate, for good."""
    if profile_of(user) is not None:
        raise AffiliateError("This account has an affiliate profile already")
    superaffiliate = AffiliateProfile.objects.filter(handle__iexact=recruiter, state=AffiliateProfile.ACTIVE).exclude(user=user).first() if recruiter else None
    profile = AffiliateProfile.objects.create(
        user=user, handle=handle, phone=phone, website=website, channels=channels, legal_name=legal_name, vat_id=vat_id,
        payout_details=payout_details, terms_accepted_at=timezone.now(), superaffiliate=superaffiliate)
    audit(user, "affiliate.apply", "affiliate", profile.handle, recruited_by=superaffiliate.handle if superaffiliate else "")
    return profile


def _hash(code: str) -> str:
    return hmac.new(settings.SECRET_KEY.encode(), code.encode(), hashlib.sha256).hexdigest()


def send_phone_code(profile: AffiliateProfile) -> None:
    """A six-digit code to the profile's number, good for ten minutes."""
    if not profile.phone:
        raise AffiliateError("Give a phone number first")
    code = f"{secrets.randbelow(1_000_000):06d}"
    texts.send_text(profile.phone, f"{code} is your code for UltraStore affiliates. It is good for {CODE_MINUTES} minutes.")
    profile.phone_code_hash = _hash(code)
    profile.phone_code_expires = timezone.now() + dt.timedelta(minutes=CODE_MINUTES)
    profile.phone_code_tries = 0
    profile.save(update_fields=["phone_code_hash", "phone_code_expires", "phone_code_tries"])


def confirm_phone(profile: AffiliateProfile, code: str) -> bool:
    """Whether the code is the one sent, in time and within five tries."""
    if not profile.phone_code_hash or not profile.phone_code_expires or profile.phone_code_expires < timezone.now():
        return False
    if profile.phone_code_tries >= CODE_TRIES:
        return False
    profile.phone_code_tries += 1
    good = hmac.compare_digest(profile.phone_code_hash, _hash((code or "").strip()))
    if good:
        profile.phone_verified_at = timezone.now()
        profile.phone_code_hash = ""
    profile.save(update_fields=["phone_code_tries", "phone_verified_at", "phone_code_hash"])
    return good


def same_person(profile: AffiliateProfile, name: str, birth_date: dt.date, country: str) -> AffiliateProfile | None:
    """Another profile whose identity check says the same person."""
    return (AffiliateProfile.objects.filter(id_name__iexact=name.strip(), id_birth_date=birth_date, id_country__iexact=country.strip())
            .exclude(pk=profile.pk).first())


@transaction.atomic
def activate(profile: AffiliateProfile, operator, *, id_name: str, id_birth_date: dt.date, id_document: str, id_country: str,
             id_expires: dt.date | None, phone_confirmed: bool = False) -> AffiliateProfile:
    """The review's end: the operator has seen an identity document and
    writes down what it says. One profile per person, of age, with a
    proven phone number."""
    if profile.state not in (AffiliateProfile.APPLIED, AffiliateProfile.PAUSED):
        raise AffiliateError(f"The profile is {profile.get_state_display().lower()}, not waiting for a review")
    today = timezone.localdate()
    if id_expires and id_expires < today:
        raise AffiliateError("The document is no longer valid")
    eighteen = id_birth_date.replace(year=id_birth_date.year + 18) if (id_birth_date.month, id_birth_date.day) != (2, 29) \
        else dt.date(id_birth_date.year + 18, 3, 1)
    if eighteen > today:
        raise AffiliateError("An affiliate is eighteen or older")
    other = same_person(profile, id_name, id_birth_date, id_country)
    if other is not None:
        raise AffiliateError(f"This person has an affiliate profile already: {other.handle}")
    if not profile.phone_verified_at:
        if not phone_confirmed:
            raise AffiliateError("The phone number is not proven: have the code entered, or confirm the number yourself")
        profile.phone_verified_at = timezone.now()
    profile.id_name, profile.id_birth_date, profile.id_document = id_name.strip(), id_birth_date, id_document
    profile.id_country, profile.id_expires = id_country.strip().upper(), id_expires
    profile.id_checked_at, profile.id_checked_by = timezone.now(), operator
    profile.state, profile.activated_at = AffiliateProfile.ACTIVE, timezone.now()
    profile.save()
    audit(operator, "affiliate.activate", "affiliate", profile.handle, document=id_document, country=profile.id_country)
    return profile


@transaction.atomic
def sanction(profile: AffiliateProfile, operator, kind: str, *, rule: str, facts: str, evidence: str = "", until: dt.date | None = None,
             money: str = "") -> Sanction:
    """A block or a ban, with its record. A ban ends the superaffiliate
    relations in both directions; a block leaves them."""
    if kind not in (Sanction.BLOCK, Sanction.BAN):
        raise AffiliateError("A sanction is a block or a ban")
    if not rule.strip() or not facts.strip():
        raise AffiliateError("A block or a ban names the rule and says what happened")
    if profile.state == AffiliateProfile.BANNED:
        raise AffiliateError("The profile is banned already")
    row = Sanction.objects.create(affiliate=profile, kind=kind, rule=rule.strip(), facts=facts.strip(), evidence=evidence.strip(),
                                  until=until if kind == Sanction.BLOCK else None, money=money.strip(), decided_by=operator)
    if kind == Sanction.BAN:
        AffiliateProfile.objects.filter(superaffiliate=profile).update(superaffiliate=None)
        profile.superaffiliate = None
        profile.state = AffiliateProfile.BANNED
    else:
        if profile.state != AffiliateProfile.BLOCKED:
            profile.state_before = profile.state
        profile.state = AffiliateProfile.BLOCKED
    profile.save()
    audit(operator, f"affiliate.{kind}", "affiliate", profile.handle, rule=row.rule, until=until.isoformat() if until else "")
    return row


@transaction.atomic
def lift(profile: AffiliateProfile, operator, note: str = "") -> AffiliateProfile:
    """A block ended: the profile is what it was before."""
    if profile.state != AffiliateProfile.BLOCKED:
        raise AffiliateError("Only a block can be lifted")
    Sanction.objects.create(affiliate=profile, kind=Sanction.LIFT, rule="-", facts=note.strip() or "The block ended.", decided_by=operator)
    profile.state = profile.state_before or AffiliateProfile.ACTIVE
    profile.state_before = ""
    profile.save(update_fields=["state", "state_before"])
    audit(operator, "affiliate.lift", "affiliate", profile.handle)
    return profile


def add_statement(row: Sanction, text: str) -> Sanction:
    """What the affiliate said to it: added to the record, never replacing."""
    stamp = timezone.localdate().isoformat()
    Sanction.objects.filter(pk=row.pk).update(statement=(row.statement + "\n\n" if row.statement else "") + f"{stamp}: {text.strip()}")
    row.refresh_from_db()
    return row


# ------------------------------------------------- the account's own states

def account_closed(user) -> None:
    profile = AffiliateProfile.objects.filter(user=user).first()
    if profile is not None and profile.state not in (AffiliateProfile.CLOSED, AffiliateProfile.BANNED):
        profile.state_before, profile.state = profile.state, AffiliateProfile.CLOSED
        profile.save(update_fields=["state", "state_before"])


def account_reactivated(user) -> None:
    profile = AffiliateProfile.objects.filter(user=user, state=AffiliateProfile.CLOSED).first()
    if profile is not None:
        profile.state, profile.state_before = profile.state_before or AffiliateProfile.PAUSED, ""
        profile.save(update_fields=["state", "state_before"])


def account_erased(user) -> None:
    """Nothing of the person is left on the profile; the row stays, so the
    commissions it earned and a ban's record still name something."""
    profile = AffiliateProfile.objects.filter(user=user).first()
    if profile is None:
        return
    AffiliateProfile.objects.filter(superaffiliate=profile).update(superaffiliate=None)
    Click.objects.filter(affiliate=profile).delete()
    banned = profile.state == AffiliateProfile.BANNED
    for name in ("website", "channels", "legal_name", "vat_id", "payout_details", "phone", "phone_code_hash"):
        setattr(profile, name, "")
    if not banned:   # a banned person stays known by what the check said, so a second profile is refused
        profile.id_name, profile.id_birth_date, profile.id_country, profile.id_document, profile.id_expires = "", None, "", "", None
    profile.handle = f"erased.{profile.pk}"
    profile.superaffiliate = None
    profile.state = AffiliateProfile.BANNED if banned else AffiliateProfile.CLOSED
    profile.save()


# -------------------------------------------------------------- the boosts

def boost_for(app: App, day: dt.date, *, renewal: bool = False) -> Boost | None:
    """The merchant's boost in force for this app on this day: the app's
    own before its category's before the one for everything."""
    rows = Boost.objects.filter(publisher_id=app.publisher_id).filter(
        Q(valid_from__isnull=True) | Q(valid_from__lte=day), Q(valid_until__isnull=True) | Q(valid_until__gte=day))
    if renewal:
        rows = rows.filter(renewals=True)
    found = {Boost.APP: None, Boost.CATEGORY: None, Boost.ALL: None}
    for row in rows.order_by("-id"):
        fits = (row.scope == Boost.APP and row.app_id == app.pk) or (row.scope == Boost.CATEGORY and row.category and row.category == app.category) \
            or row.scope == Boost.ALL
        if fits and found[row.scope] is None:
            found[row.scope] = row
    return found[Boost.APP] or found[Boost.CATEGORY] or found[Boost.ALL]


def percent_for(app: App, day: dt.date | None = None) -> tuple[Decimal, Decimal]:
    """What a link to this app earns today: the base, and the boost."""
    boost = boost_for(app, day or timezone.localdate())
    return settings.AFFILIATE_BASE_PERCENT, boost.percent if boost else ZERO


@transaction.atomic
def save_boost(publisher, actor, *, scope: str, percent: Decimal, app: App | None = None, category: str = "", renewals: bool = False,
               valid_from: dt.date | None = None, valid_until: dt.date | None = None) -> Boost:
    """A boost, saved by the publisher's owner and nobody else."""
    if not PublisherMember.objects.filter(publisher=publisher, user=actor, role=PublisherMember.OWNER).exists():
        raise AffiliateError("Only the publisher's owner sets a boost")
    if not BOOST_MIN <= percent <= BOOST_MAX:
        raise AffiliateError(f"A boost is from {BOOST_MIN} to {BOOST_MAX} percent")
    if scope == Boost.APP and (app is None or app.publisher_id != publisher.pk):
        raise AffiliateError("Choose one of your apps")
    if scope == Boost.CATEGORY and not category:
        raise AffiliateError("Choose a category")
    if valid_from and valid_until and valid_until < valid_from:
        raise AffiliateError("The boost ends before it begins")
    row = Boost.objects.create(publisher=publisher, scope=scope, app=app if scope == Boost.APP else None,
                               category=category if scope == Boost.CATEGORY else "", percent=percent, renewals=renewals,
                               valid_from=valid_from, valid_until=valid_until, saved_by=actor)
    audit(actor, "affiliate.boost", "publisher", publisher.slug, scope=scope, percent=str(percent), app=app.app_id if row.app_id else "",
          category=row.category)
    return row


# --------------------------------------------------------------- the links

def may_earn(profile: AffiliateProfile, app: App, buyer_email: str = "") -> bool:
    """Whether a sale of this app can credit this affiliate: active, not
    one of the seller's own people, and not the buyer."""
    if profile.state != AffiliateProfile.ACTIVE or not profile.user.is_active:
        return False
    if PublisherMember.objects.filter(publisher_id=app.publisher_id, user_id=profile.user_id).exists():
        return False
    return not (buyer_email and buyer_email.strip().lower() == profile.user.email.lower())


def record_click(profile: AffiliateProfile, app: App | None, label: str = "") -> Click:
    return Click.objects.create(affiliate=profile, app=app, label=(label or "")[:32])


def order_opened(order, ref: str) -> None:
    """An order that came with the token of an affiliate's link: kept,
    when the link was to this product, in time, by an affiliate who may
    earn on it."""
    if not enabled() or not ref:
        return
    click = Click.objects.select_related("affiliate__user").filter(token=ref[:40]).first()
    if click is None or click.app_id != order.app_id:
        return
    if click.at < timezone.now() - dt.timedelta(days=settings.AFFILIATE_LINK_DAYS):
        return
    if not may_earn(click.affiliate, order.app, order.payer_email):
        return
    Attribution.objects.get_or_create(order=order, defaults={"affiliate": click.affiliate, "label": click.label, "clicked_at": click.at})


def _line(payment: Payment, affiliate: AffiliateProfile, basis: str, **fields) -> Commission:
    return Commission.objects.create(payment=payment, order=payment.order, affiliate=affiliate, basis=basis, currency=payment.currency, **fields)


def _earned(payment: Payment, row: Attribution) -> None:
    order = payment.order
    affiliate = row.affiliate
    if not may_earn(affiliate, order.app, order.payer_email):
        return
    today = timezone.localdate()
    net = payment.net_amount
    base_percent = settings.AFFILIATE_BASE_PERCENT
    boost = boost_for(order.app, today, renewal=payment.kind == Payment.RENEWAL)
    boost_percent = boost.percent if boost else ZERO
    operator_part, merchant_part = part(net, base_percent), part(net, boost_percent)
    # Never more than the publisher has left of this payment.
    merchant_part = max(min(merchant_part, payment.amount - payment.vat_amount - payment.store_fee), ZERO)
    after = today + dt.timedelta(days=settings.AFFILIATE_APPROVAL_DAYS)
    line = _line(payment, affiliate, Commission.PRODUCT_LINK, label=row.label, net=net, base_percent=base_percent, boost_percent=boost_percent,
                 operator_part=operator_part, merchant_part=merchant_part, amount=operator_part + merchant_part, approve_after=after)
    if merchant_part:
        Payment.objects.filter(pk=payment.pk).update(boost_amount=merchant_part)
        payment.boost_amount = merchant_part
    above = affiliate.superaffiliate
    if above is not None and above.state == AffiliateProfile.ACTIVE and above.user.is_active:
        share_percent = affiliate.super_percent if affiliate.super_percent is not None else settings.AFFILIATE_SUPER_PERCENT
        share = part(line.amount, share_percent)
        if share > 0:
            _line(payment, above, Commission.SUPER, share_of=line, net=line.amount, base_percent=share_percent, operator_part=share,
                  amount=share, approve_after=after)


def _given_back(payment: Payment) -> None:
    """A refund or a chargeback: every line of the payment it takes back,
    again below zero, in the proportion of the money - and the last cent of
    a payment takes whatever of a line is left."""
    order = payment.order
    paid = [p for p in order.payments.all() if not p.is_reversal]
    original = next((p for p in paid if payment.reverses and p.provider_transaction == payment.reverses), paid[-1] if paid else None)
    if original is None:
        return
    earlier = [p for p in order.payments.all()
               if p.is_reversal and p.pk != payment.pk and original.provider_transaction and p.reverses == original.provider_transaction]
    before = sum((p.amount for p in earlier), ZERO)
    earlier_ids = [p.pk for p in earlier]
    boost_back = ZERO
    made: dict[int, Commission] = {}
    for line in Commission.objects.filter(payment=original).exclude(state=Commission.REVERSAL).order_by("id"):
        took = Commission.objects.filter(payment_id__in=earlier_ids, affiliate=line.affiliate, basis=line.basis, state=Commission.REVERSAL)
        def back(whole: Decimal, name: str) -> Decimal:
            return split.reversal_fee(original.amount, whole, payment.amount, before, -sum((getattr(t, name) for t in took), ZERO))
        amount, operator_part, merchant_part = back(line.amount, "amount"), back(line.operator_part, "operator_part"), back(line.merchant_part, "merchant_part")
        if not amount:
            continue
        made[line.pk] = _line(payment, line.affiliate, line.basis, share_of=made.get(line.share_of_id), label=line.label,
                              net=-back(line.net, "net"),
                              base_percent=line.base_percent, boost_percent=line.boost_percent, operator_part=-operator_part,
                              merchant_part=-merchant_part, amount=-amount, state=Commission.REVERSAL)
        boost_back += merchant_part
    if boost_back:
        Payment.objects.filter(pk=payment.pk).update(boost_amount=boost_back)
        payment.boost_amount = boost_back


def payment_recorded(payment: Payment) -> None:
    """Called for every payment the portal writes (billing/services.py)."""
    if not enabled():
        return
    if payment.is_reversal:
        _given_back(payment)
        return
    row = Attribution.objects.select_related("affiliate__user", "affiliate__superaffiliate__user").filter(order=payment.order).first()
    if row is not None:
        _earned(payment, row)


# ------------------------------------------------- approval, sums, payouts

def approve_due(today: dt.date | None = None) -> int:
    """Pending lines whose days are over become approved. A blocked
    affiliate's wait: a block holds what it earned."""
    today = today or timezone.localdate()
    return Commission.objects.filter(state=Commission.PENDING, approve_after__lte=today).exclude(
        affiliate__state__in=[AffiliateProfile.BLOCKED, AffiliateProfile.BANNED]).update(state=Commission.APPROVED)


def end_blocks(today: dt.date | None = None) -> int:
    """Blocks whose last day has passed, lifted."""
    today = today or timezone.localdate()
    lifted = 0
    for profile in AffiliateProfile.objects.filter(state=AffiliateProfile.BLOCKED):
        last = profile.sanctions.filter(kind__in=[Sanction.BLOCK, Sanction.LIFT]).first()
        if last is not None and last.kind == Sanction.BLOCK and last.until and last.until < today:
            lift(profile, None, "The block's time is over.")
            lifted += 1
    return lifted


def tidy_clicks(today: dt.date | None = None) -> int:
    """Clicks past the days a link counts: counted by day, then removed."""
    today = today or timezone.localdate()
    old = Click.objects.filter(at__date__lt=today - dt.timedelta(days=settings.AFFILIATE_LINK_DAYS))
    count = 0
    for click in old:
        row, _ = ClickDay.objects.get_or_create(affiliate_id=click.affiliate_id, app_id=click.app_id, label=click.label,
                                                day=timezone.localtime(click.at).date())
        ClickDay.objects.filter(pk=row.pk).update(clicks=row.clicks + 1)
        count += 1
    old.delete()
    return count


def maintain(today: dt.date | None = None) -> dict:
    return {"approved": approve_due(today), "blocks_ended": end_blocks(today), "clicks_counted": tidy_clicks(today)}


def sums(profile: AffiliateProfile) -> list[dict]:
    """Per currency: pending, the balance (approved, less what was given
    back, not yet paid) and paid."""
    rows: dict[str, dict] = {}
    for line in Commission.objects.filter(affiliate=profile):
        row = rows.setdefault(line.currency, {"currency": line.currency, "pending": ZERO, "balance": ZERO, "paid": ZERO})
        if line.payout_id:
            row["paid"] += line.amount
        elif line.state == Commission.PENDING:
            row["pending"] += line.amount
        else:
            row["balance"] += line.amount
    return [rows[currency] for currency in sorted(rows)]


@transaction.atomic
def pay(profile: AffiliateProfile, currency: str, operator, reference: str, note: str = "") -> AffiliatePayout:
    """The operator has paid the affiliate's balance in a currency: written
    down with the transfer's reference. The portal moves no money."""
    if profile.state in (AffiliateProfile.BLOCKED, AffiliateProfile.BANNED):
        raise AffiliateError("A blocked or banned affiliate's money is held; the ban's record says what is paid")
    reference = (reference or "").strip()
    if not reference:
        raise AffiliateError("Give the transfer's reference: it is what proves the payout was paid")
    lines = list(Commission.objects.select_for_update().filter(affiliate=profile, currency=currency, payout__isnull=True)
                 .exclude(state=Commission.PENDING))
    amount = sum((line.amount for line in lines), ZERO)
    if amount <= 0:
        raise AffiliateError(f"Nothing is owed in {currency}")
    payout = AffiliatePayout.objects.create(affiliate=profile, currency=currency, amount=amount, details=profile.payout_details,
                                            reference=reference[:120], note=(note or "").strip()[:300], paid_by=operator)
    Commission.objects.filter(pk__in=[line.pk for line in lines]).update(payout=payout)
    audit(operator, "affiliate.paid", "affiliate", profile.handle, amount=str(amount), currency=currency, reference=payout.reference)
    return payout


def cost(since: dt.date | None = None) -> list[dict]:
    """What the programme cost the operator and the merchants, per currency."""
    lines = Commission.objects.all()
    if since:
        lines = lines.filter(created__date__gte=since)
    rows: dict[str, dict] = {}
    for line in lines:
        row = rows.setdefault(line.currency, {"currency": line.currency, "operator": ZERO, "merchants": ZERO, "total": ZERO})
        row["operator"] += line.operator_part
        row["merchants"] += line.merchant_part
        row["total"] += line.amount
    return [rows[currency] for currency in sorted(rows)]
