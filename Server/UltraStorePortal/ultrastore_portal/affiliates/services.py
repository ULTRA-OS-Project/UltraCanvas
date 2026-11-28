"""UltraAffiliates, phases 1 to 3: product links, the customer's binding, the money.

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

Phase 2, the binding (0.11.0) - it needs the buyer's account on the order,
so it works for a buyer who is signed in:

- **A new customer is bound to the affiliate whose link brought them**: at
  sign-up, or at the first paid order, when a link of that affiliate's - to
  a product or to the shop - was followed in the days a link counts. New
  means no paid order before, under the account or under its address.
- **A bound customer's purchase earns their affiliate the base**, whatever
  the product and whoever sells it - unless a link to that product wins it
  (above), which earns that link's affiliate base and boost. One commission
  per order; the binding does not move for a link.
- **A customer is won back**: one who has paid for nothing for more than
  AFFILIATE_WINBACK_MONTHS (6) and pays within AFFILIATE_WINBACK_DAYS (14)
  of following another affiliate's link is bound to that affiliate from
  that order on, whether they had one before or not. The earlier binding
  stays as history.
- **Nobody is bound to themselves**, and a binding credits nobody the
  programme would not let earn on that sale.
- **The binding follows the customer's account**: suspended while it is
  closed or being deleted, back when it is, gone when it is erased. An
  affiliate's ban, or the erasure of their account, ends their bindings.
- **A referred customer who becomes an affiliate** has their affiliate as
  superaffiliate, before any recruiting link.

- **Held back** (phase 3): of every approved commission
  AFFILIATE_HOLDBACK_PERCENT (5) for AFFILIATE_HOLDBACK_MONTHS (6) from the
  sale; what is taken back in that time comes out of it first.
- **Paid monthly**, per currency, from AFFILIATE_PAYOUT_MINIMUM (25.00); a
  balance under it is carried, and paid whatever it is when the account is
  being left.

Not built: self-billed credit notes; paying through the provider's
interface; fixed boosts for hardware; codes; the native store's links.

Version: 0.12.0 - phase 3, the money: the holdback and its release, payouts
                  made for a day and marked paid, the statement
Version: 0.11.0 - phase 2: the binding, the win-back, the bound customer's base
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
from .models import (AffiliatePayout, AffiliateProfile, Attribution, Binding, Boost, Click, ClickDay, Commission, Holdback, OrderVisit,
                     Sanction)

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
    # Whoever brought the customer brought the affiliate: the binding goes
    # before a recruiting link.
    bound = binding_of(user)
    if bound is not None and bound.affiliate.state == AffiliateProfile.ACTIVE:
        superaffiliate = bound.affiliate
    else:
        superaffiliate = (AffiliateProfile.objects.filter(handle__iexact=recruiter, state=AffiliateProfile.ACTIVE).exclude(user=user).first()
                          if recruiter else None)
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
        _end_bindings(profile, "affiliate_banned")
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
    """The account is closed, or its deletion asked for: as a customer, its
    binding is suspended; as an affiliate (a closed account), its profile."""
    Binding.objects.filter(customer=user, state=Binding.ACTIVE).update(state=Binding.SUSPENDED)
    if user.is_active:   # a deletion asked for: the account works on, and so does its profile
        return
    profile = AffiliateProfile.objects.filter(user=user).first()
    if profile is not None and profile.state not in (AffiliateProfile.CLOSED, AffiliateProfile.BANNED):
        profile.state_before, profile.state = profile.state, AffiliateProfile.CLOSED
        profile.save(update_fields=["state", "state_before"])


def account_reactivated(user) -> None:
    """The account is back, or its deletion taken back: the binding is in
    force again, to the same affiliate."""
    Binding.objects.filter(customer=user, state=Binding.SUSPENDED).update(state=Binding.ACTIVE)
    profile = AffiliateProfile.objects.filter(user=user, state=AffiliateProfile.CLOSED).first()
    if profile is not None:
        profile.state, profile.state_before = profile.state_before or AffiliateProfile.PAUSED, ""
        profile.save(update_fields=["state", "state_before"])


def account_erased(user) -> None:
    """Nothing of the person is left: as a customer, the bindings are
    removed (what they earned an affiliate stays, with no customer behind
    it); as an affiliate, the profile is emptied and its row stays, so the
    commissions it earned and a ban's record still name something."""
    Binding.objects.filter(customer=user).delete()
    profile = AffiliateProfile.objects.filter(user=user).first()
    if profile is None:
        return
    _end_bindings(profile, "affiliate_erased")
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

def may_earn(profile: AffiliateProfile, app: App, buyer_email: str = "", buyer_id: int | None = None) -> bool:
    """Whether a sale of this app can credit this affiliate: active, not
    one of the seller's own people, and not the buyer."""
    if profile.state != AffiliateProfile.ACTIVE or not profile.user.is_active:
        return False
    if PublisherMember.objects.filter(publisher_id=app.publisher_id, user_id=profile.user_id).exists():
        return False
    if buyer_id is not None and buyer_id == profile.user_id:
        return False
    return not (buyer_email and buyer_email.strip().lower() == profile.user.email.lower())


# ------------------------------------------------------------- the binding

def binding_of(user) -> Binding | None:
    """The customer's binding in force: active, or suspended with the account."""
    if not getattr(user, "pk", None):
        return None
    return Binding.objects.select_related("affiliate__user").filter(customer=user).exclude(state=Binding.ENDED).first()


def _end_bindings(profile: AffiliateProfile, because: str) -> None:
    Binding.objects.filter(affiliate=profile).exclude(state=Binding.ENDED).update(state=Binding.ENDED, ended_because=because, ended=timezone.now())


def _may_bind(profile: AffiliateProfile, user) -> bool:
    return profile.state == AffiliateProfile.ACTIVE and profile.user.is_active and profile.user_id != user.pk


@transaction.atomic
def _bind(user, profile: AffiliateProfile, through: str) -> Binding:
    # One binding in force per customer: the earlier one ends here, in the
    # same transaction, with the customer's row held (the constraint that
    # says so is not one MySQL keeps).
    type(user).objects.select_for_update().filter(pk=user.pk).first()
    Binding.objects.filter(customer=user).exclude(state=Binding.ENDED).update(state=Binding.ENDED, ended_because="won_back", ended=timezone.now())
    row = Binding.objects.create(customer=user, affiliate=profile, through=through)
    audit(None, "affiliate.bound", "account", str(user.pk), affiliate=profile.handle, through=through)
    return row


def last_paid(user, *, apart_from=None):
    """When this person last paid for anything - under the account, or
    under its address before the account was on its orders - or None."""
    paid = Payment.objects.filter(kind__in=[Payment.SALE, Payment.RENEWAL]).filter(Q(order__buyer=user) | Q(order__payer_email__iexact=user.email))
    if apart_from is not None:
        paid = paid.exclude(order=apart_from)
    return paid.order_by("-at").values_list("at", flat=True).first()


def months_after(moment, months: int):
    """The same day `months` later (a date or a time), or the month's last
    where it has no such day."""
    return months_before(moment, -months)


def months_before(moment, months: int):
    """The same day `months` earlier, or the month's last where it has no such day."""
    count = moment.year * 12 + (moment.month - 1) - months
    year, month = count // 12, count % 12 + 1
    for day in range(moment.day, 27, -1):
        try:
            return moment.replace(year=year, month=month, day=day)
        except ValueError:
            continue
    return moment.replace(year=year, month=month, day=min(moment.day, 28))


def signed_up(user, token: str) -> Binding | None:
    """A new account whose owner followed an affiliate's link in the days a
    link counts: bound to that affiliate."""
    if not enabled() or not token:
        return None
    click = Click.objects.select_related("affiliate__user").filter(token=token[:40]).first()
    if click is None or click.at < timezone.now() - dt.timedelta(days=settings.AFFILIATE_LINK_DAYS):
        return None
    if binding_of(user) is not None or last_paid(user) is not None or not _may_bind(click.affiliate, user):
        return None
    return _bind(user, click.affiliate, Binding.SIGNUP)


def _settle_binding(order) -> None:
    """Before an order's first payment is attributed: a new customer who
    came through an affiliate's link is bound, and a customer who stopped
    buying is won back by the affiliate whose link brought them back."""
    buyer = order.buyer
    visit = OrderVisit.objects.select_related("affiliate__user").filter(order=order).first()
    if buyer is None or visit is None or not _may_bind(visit.affiliate, buyer):
        return
    if buyer.state != buyer.ACTIVE:   # on the way out: nobody's new customer
        return
    now = timezone.now()
    current = binding_of(buyer)
    earlier = last_paid(buyer, apart_from=order)
    if earlier is None:
        if current is None and visit.clicked_at >= now - dt.timedelta(days=settings.AFFILIATE_LINK_DAYS):
            _bind(buyer, visit.affiliate, Binding.FIRST_ORDER)
        return
    lapsed = earlier < months_before(now, settings.AFFILIATE_WINBACK_MONTHS)
    in_time = visit.clicked_at >= now - dt.timedelta(days=settings.AFFILIATE_WINBACK_DAYS)
    if lapsed and in_time and (current is None or current.affiliate_id != visit.affiliate_id):
        _bind(buyer, visit.affiliate, Binding.WON_BACK)


def record_click(profile: AffiliateProfile, app: App | None, label: str = "") -> Click:
    return Click.objects.create(affiliate=profile, app=app, label=(label or "")[:32])


def order_opened(order, ref: str, via: str = "") -> None:
    """An order that came with the tokens of affiliates' links. `ref`, a
    link to this product: kept as the order's attribution when it is in
    time and by an affiliate who may earn on it. `via`, the last link the
    buyer followed, whatever it led to: kept for the binding."""
    if not enabled():
        return
    clicks = {c.token: c for c in Click.objects.select_related("affiliate__user").filter(token__in=[t[:40] for t in (ref, via) if t])}
    product, last = clicks.get(ref[:40]) if ref else None, clicks.get(via[:40]) if via else None
    if last is None or (product is not None and product.at > last.at):
        last = product
    if last is not None and last.affiliate.state == AffiliateProfile.ACTIVE:
        OrderVisit.objects.get_or_create(order=order, defaults={"affiliate": last.affiliate, "clicked_at": last.at})
    if product is None or product.app_id != order.app_id:
        return
    if product.at < timezone.now() - dt.timedelta(days=settings.AFFILIATE_LINK_DAYS):
        return
    if not may_earn(product.affiliate, order.app, order.payer_email, order.buyer_id):
        return
    Attribution.objects.get_or_create(order=order, defaults={"affiliate": product.affiliate, "label": product.label, "clicked_at": product.at})


def _line(payment: Payment, affiliate: AffiliateProfile, basis: str, **fields) -> Commission:
    return Commission.objects.create(payment=payment, order=payment.order, affiliate=affiliate, basis=basis, currency=payment.currency, **fields)


def _earned(payment: Payment, row: Attribution) -> None:
    order = payment.order
    affiliate = row.affiliate
    if not may_earn(affiliate, order.app, order.payer_email, order.buyer_id):
        return
    today = timezone.localdate()
    net = payment.net_amount
    base_percent = settings.AFFILIATE_BASE_PERCENT
    # The merchant's boost is for the affiliate whose link to the product
    # brought the sale; a bound customer's purchase earns the base alone.
    linked = row.basis == Attribution.PRODUCT_LINK
    boost = boost_for(order.app, today, renewal=payment.kind == Payment.RENEWAL) if linked else None
    boost_percent = boost.percent if boost else ZERO
    operator_part, merchant_part = part(net, base_percent), part(net, boost_percent)
    # Never more than the publisher has left of this payment.
    merchant_part = max(min(merchant_part, payment.amount - payment.vat_amount - payment.store_fee), ZERO)
    after = today + dt.timedelta(days=settings.AFFILIATE_APPROVAL_DAYS)
    line = _line(payment, affiliate, Commission.PRODUCT_LINK if linked else Commission.BOUND_CUSTOMER, binding=None if linked else row.binding,
                 label=row.label, net=net, base_percent=base_percent, boost_percent=boost_percent,
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


@transaction.atomic
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
        # Out of the line's holdback first, while it is held; the rest
        # comes off the balance.
        covered = ZERO
        held = Holdback.objects.select_for_update().filter(commission=line, released_on__isnull=True).first()
        if held is not None and held.left > 0:
            covered = min(held.left, amount)
            held.taken += covered
            held.save(update_fields=["taken"])
        made[line.pk] = _line(payment, line.affiliate, line.basis, share_of=made.get(line.share_of_id), label=line.label, binding=line.binding,
                              reverses=line, from_holdback=covered, net=-back(line.net, "net"),
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
    order = payment.order
    if payment.kind == Payment.SALE:
        _settle_binding(order)
    row = Attribution.objects.select_related("affiliate__user", "affiliate__superaffiliate__user", "binding").filter(order=order).first()
    if row is None and payment.kind == Payment.SALE and order.buyer_id:
        # No link to the product: the affiliate the buyer is bound to.
        bound = Binding.objects.select_related("affiliate__user", "affiliate__superaffiliate__user").filter(customer_id=order.buyer_id,
                                                                                                         state=Binding.ACTIVE).first()
        if bound is not None and may_earn(bound.affiliate, order.app, order.payer_email, order.buyer_id):
            row = Attribution.objects.create(order=order, affiliate=bound.affiliate, basis=Attribution.BOUND_CUSTOMER, binding=bound)
    elif row is not None and row.basis == Attribution.BOUND_CUSTOMER and payment.kind == Payment.RENEWAL:
        # A renewal follows the customer, not the day the subscription began:
        # it earns whoever the customer is bound to now, and nobody while
        # the binding is suspended or has ended.
        bound = Binding.objects.select_related("affiliate__user", "affiliate__superaffiliate__user").filter(customer_id=order.buyer_id,
                                                                                                         state=Binding.ACTIVE).first()
        if bound is None:
            return
        if bound.pk != row.binding_id:
            row.affiliate, row.binding = bound.affiliate, bound
            row.save(update_fields=["affiliate", "binding"])
    if row is not None:
        _earned(payment, row)


# ------------------------------------------------- approval, sums, payouts

HELD = [AffiliateProfile.BLOCKED, AffiliateProfile.BANNED]   # whose money waits: nothing is approved, released or paid


def approve_due(today: dt.date | None = None) -> int:
    """Pending lines whose days are over become approved, and the holdback
    is set aside: AFFILIATE_HOLDBACK_PERCENT of what is left of the line -
    what a refund took in the meantime is off it - until
    AFFILIATE_HOLDBACK_MONTHS after the sale was paid. A blocked
    affiliate's wait: a block holds what it earned."""
    today = today or timezone.localdate()
    due = Commission.objects.select_related("payment").filter(state=Commission.PENDING, approve_after__lte=today).exclude(affiliate__state__in=HELD)
    count = 0
    for line in due:
        with transaction.atomic():
            if not Commission.objects.filter(pk=line.pk, state=Commission.PENDING).update(state=Commission.APPROVED, approved_on=today):
                continue
            left = line.amount + sum((r.amount for r in line.reversals.all()), ZERO)
            held = part(left, settings.AFFILIATE_HOLDBACK_PERCENT) if left > 0 else ZERO
            if held > 0:
                Holdback.objects.create(commission=line, affiliate_id=line.affiliate_id, currency=line.currency,
                                        percent=settings.AFFILIATE_HOLDBACK_PERCENT, amount=held,
                                        release_on=months_after(timezone.localtime(line.payment.at).date(), settings.AFFILIATE_HOLDBACK_MONTHS))
            count += 1
    return count


def release_due(today: dt.date | None = None) -> int:
    """Holdbacks whose day has come: what is left of each is in the
    affiliate's balance from now on."""
    today = today or timezone.localdate()
    return Holdback.objects.filter(released_on__isnull=True, release_on__lte=today).exclude(affiliate__state__in=HELD).update(released_on=today)


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
    return {"blocks_ended": end_blocks(today), "approved": approve_due(today), "released": release_due(today), "clicks_counted": tidy_clicks(today)}


# The balance is made of three things no payout has taken yet:
# - an approved line, less its holdback;
# - a line that takes back, less what the holdback covered of it - once the
#   line it takes from is approved (before, both are pending together);
# - a holdback that was released, with what is left of it.

def _due(until: dt.date, profile: AffiliateProfile | None = None) -> dict[tuple[int, str], dict]:
    """Per affiliate and currency: what is due on `until` and in no payout."""
    def of(queryset):
        return queryset.filter(affiliate=profile) if profile is not None else queryset
    groups: dict[tuple[int, str], dict] = {}
    def group(affiliate_id: int, currency: str) -> dict:
        return groups.setdefault((affiliate_id, currency), {"lines": [], "releases": [], "amount": ZERO})
    approved = Q(approved_on__isnull=True) | Q(approved_on__lte=until)
    for line in of(Commission.objects.filter(approved, state=Commission.APPROVED, payout__isnull=True)).select_related("holdback"):
        row = group(line.affiliate_id, line.currency)
        row["lines"].append(line)
        row["amount"] += line.payable
    taken_from = Q(reverses__isnull=True) | (Q(reverses__state=Commission.APPROVED) & (Q(reverses__approved_on__isnull=True) | Q(reverses__approved_on__lte=until)))
    for line in of(Commission.objects.filter(taken_from, state=Commission.REVERSAL, payout__isnull=True, created__date__lte=until)):
        row = group(line.affiliate_id, line.currency)
        row["lines"].append(line)
        row["amount"] += line.payable
    for held in of(Holdback.objects.filter(payout__isnull=True, released_on__lte=until)):
        if held.left > 0:
            row = group(held.affiliate_id, held.currency)
            row["releases"].append(held)
            row["amount"] += held.left
    return groups


def leaving(profile: AffiliateProfile) -> bool:
    """The account behind the profile is closed or being deleted: what is
    owed is paid whatever the minimum."""
    return profile.state == AffiliateProfile.CLOSED or profile.user.state in (profile.user.CLOSED, profile.user.DELETING)


def sums(profile: AffiliateProfile, today: dt.date | None = None) -> list[dict]:
    """Per currency, what the affiliate's statistics show: pending; held
    back, with the next release; the balance - due and in no payout yet -
    and whether the next payout takes it; what is in a payout to be paid;
    and what was paid."""
    today = today or timezone.localdate()
    rows: dict[str, dict] = {}
    def row(currency: str) -> dict:
        return rows.setdefault(currency, {"currency": currency, "pending": ZERO, "held": ZERO, "next_release": None, "next_release_amount": ZERO,
                                          "balance": ZERO, "to_pay": ZERO, "paid": ZERO})
    for line in Commission.objects.filter(affiliate=profile, state=Commission.PENDING).prefetch_related("reversals"):
        row(line.currency)["pending"] += line.amount + sum((r.amount for r in line.reversals.all()), ZERO)
    for held in Holdback.objects.filter(affiliate=profile, released_on__isnull=True):
        if held.left > 0:
            entry = row(held.currency)
            entry["held"] += held.left
            if entry["next_release"] is None or held.release_on < entry["next_release"]:
                entry["next_release"], entry["next_release_amount"] = held.release_on, ZERO
            if held.release_on == entry["next_release"]:
                entry["next_release_amount"] += held.left
    for (_, currency), due in _due(today, profile).items():
        row(currency)["balance"] += due["amount"]
    for payout in AffiliatePayout.objects.filter(affiliate=profile):
        row(payout.currency)["paid" if payout.state == AffiliatePayout.PAID else "to_pay"] += payout.amount
    regardless = leaving(profile)
    for entry in rows.values():
        entry["payable"] = entry["balance"] >= settings.AFFILIATE_PAYOUT_MINIMUM or (regardless and entry["balance"] > 0)
    return [rows[currency] for currency in sorted(rows)]


def owed(today: dt.date | None = None) -> dict[int, list[dict]]:
    """Per affiliate: the balances above nothing, per currency, and whether
    the next payout takes each."""
    out: dict[int, list[dict]] = {}
    profiles = {p.pk: p for p in AffiliateProfile.objects.select_related("user")}
    for (affiliate_id, currency), due in sorted(_due(today or timezone.localdate()).items()):
        if due["amount"] > 0:
            profile = profiles[affiliate_id]
            out.setdefault(affiliate_id, []).append({
                "currency": currency, "sum": due["amount"],
                "payable": profile.state not in HELD and (due["amount"] >= settings.AFFILIATE_PAYOUT_MINIMUM or leaving(profile))})
    return out


def default_until(today: dt.date | None = None) -> dt.date:
    """The last day of the month before this one: payouts are monthly."""
    today = today or timezone.localdate()
    return today.replace(day=1) - dt.timedelta(days=1)


@transaction.atomic
def build(until: dt.date | None = None, actor=None, profile: AffiliateProfile | None = None) -> list[AffiliatePayout]:
    """The payouts for everything due on `until`, per affiliate and
    currency. A balance under AFFILIATE_PAYOUT_MINIMUM makes none and is
    carried - unless the affiliate's account is being left, when whatever is
    owed is paid. A blocked or banned affiliate's money waits. Safe to run
    again: what a payout took is taken."""
    until = until or default_until()
    if until > timezone.localdate():
        raise AffiliateError("A payout cannot be made for days that have not come")
    profiles = {p.pk: p for p in AffiliateProfile.objects.select_related("user")}
    made = []
    for (affiliate_id, currency), due in sorted(_due(until, profile).items()):
        owner = profiles[affiliate_id]
        if owner.state in HELD or owner.user.state == owner.user.ERASED:
            continue
        amount, regardless = due["amount"], False
        if amount < settings.AFFILIATE_PAYOUT_MINIMUM:
            if amount <= 0 or not leaving(owner):
                continue   # carried: it counts the next time
            regardless = True
        payout = AffiliatePayout.objects.create(affiliate=owner, currency=currency, until=until, amount=amount, regardless=regardless,
                                                details=owner.payout_details, created_by=actor if getattr(actor, "pk", None) else None)
        Commission.objects.filter(pk__in=[line.pk for line in due["lines"]]).update(payout=payout)
        Holdback.objects.filter(pk__in=[held.pk for held in due["releases"]]).update(payout=payout)
        audit(actor, "affiliate.payout_made", "affiliate", owner.handle, payout=payout.pk, amount=str(amount), currency=currency,
              until=until.isoformat())
        made.append(payout)
    return made


@transaction.atomic
def mark_paid(payout: AffiliatePayout, operator, reference: str, note: str = "") -> AffiliatePayout:
    """The operator has paid it: written down with the transfer's
    reference. The portal moves no money."""
    payout = AffiliatePayout.objects.select_for_update().select_related("affiliate").get(pk=payout.pk)
    if payout.state == AffiliatePayout.PAID:
        raise AffiliateError("This payout is marked paid already")
    if payout.affiliate.state in HELD:
        raise AffiliateError("A blocked or banned affiliate's money is held; the record says what is paid")
    reference = (reference or "").strip()
    if not reference:
        raise AffiliateError("Give the transfer's reference: it is what proves the payout was paid")
    payout.state, payout.reference, payout.note = AffiliatePayout.PAID, reference[:120], (note or "").strip()[:300]
    payout.paid_at, payout.paid_by = timezone.now(), operator if getattr(operator, "pk", None) else None
    payout.save()
    audit(operator, "affiliate.paid", "affiliate", payout.affiliate.handle, payout=payout.pk, amount=str(payout.amount), currency=payout.currency,
          reference=payout.reference)
    return payout


def statement(payout: AffiliatePayout) -> dict:
    """A payout as its statement shows it: the commissions with what was
    held back of each, what was taken back, the holdbacks released, and the
    sums."""
    lines = list(payout.lines.select_related("order", "share_of__affiliate", "holdback", "reverses").order_by("id"))
    earned = [line for line in lines if line.state != Commission.REVERSAL]
    back = [line for line in lines if line.state == Commission.REVERSAL]
    releases = list(payout.releases.select_related("commission__order", "commission__payment").order_by("id"))
    return {"earned": earned, "back": back, "releases": releases,
            "commissions": sum((line.amount for line in earned), ZERO), "held": sum((line.held for line in earned), ZERO),
            "taken_back": sum((line.payable for line in back), ZERO), "released": sum((held.left for held in releases), ZERO)}


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
