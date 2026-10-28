// Apps/UltraStore/core/Catalog.cpp
// catalog.json in and out (Catalog.h), through UltraCanvasJSON.
// Version: 0.5.0 - payment_url, licence_key
// Version: 0.4.0 - codes
// Version: 0.3.0 - trusted_keys
// Version: 0.2.0 - ParseComponent / SerializeComponent for the admin backend
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Catalog.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>
#include <cctype>

namespace UltraStore {

using UltraCanvas::JSONValue;

namespace {

bool IsHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Digits and dots, at least one digit: "14.0", "10.0.19041", "2.31".
bool IsVersionLike(const std::string& s) {
    if (s.empty()) return false;
    bool digit = false;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
        else if (c != '.') return false;
    }
    return digit;
}

// "2027-12-31".
bool IsIsoDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

// "29.90", "5", ".5" is not: digits, at most one point with digits after it.
bool IsDecimal(const std::string& s) {
    if (s.empty() || s[0] == '.') return false;
    bool point = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '.') { if (point || i + 1 == s.size()) return false; point = true; continue; }
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

bool IsReverseDns(const std::string& id) {
    if (id.empty() || id.find('.') == std::string::npos) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) return false;
    }
    return id.front() != '.' && id.back() != '.';
}

LocalizedText ReadText(const JSONValue& v) {
    LocalizedText text;
    if (v.IsString()) {
        text.byLanguage["en"] = v.GetString();
    } else if (v.IsObject()) {
        for (const auto& [key, value] : v.GetMembers()) text.byLanguage[key] = value.GetString();
    }
    return text;
}

std::vector<std::string> ReadStrings(const JSONValue& v) {
    std::vector<std::string> out;
    if (v.IsString()) { out.push_back(v.GetString()); return out; }
    for (const JSONValue& e : v.GetElements()) if (e.IsString()) out.push_back(e.GetString());
    return out;
}

JSONValue WriteText(const LocalizedText& text) {
    JSONValue o = JSONValue::MakeObject();
    for (const auto& [lang, value] : text.byLanguage) o.Set(lang, value);
    return o;
}

JSONValue WriteStrings(const std::vector<std::string>& items) {
    JSONValue a = JSONValue::MakeArray();
    for (const std::string& s : items) a.Append(s);
    return a;
}

// where: "apps[2].releases[0].packages[1]" for the error detail.
Result<Package> ReadPackage(const JSONValue& v, const std::string& where, bool requireUrl) {
    if (!v.IsObject()) return Result<Package>::Failure(ErrorCode::ParseError, "A package entry is not an object", where);
    Package p;
    p.target.os = ParseOS(v.Get("os").GetString());
    p.target.arch = ParseArch(v.Get("arch").GetString());
    if (p.target.os == OS::Unknown || p.target.arch == Arch::Unknown) {
        return Result<Package>::Failure(ErrorCode::ParseError, "A package names no known os / arch",
                                        where + ": os=" + v.Get("os").GetString() + " arch=" + v.Get("arch").GetString());
    }
    p.target.variants = ReadStrings(v.Get("variant"));
    p.minOs = v.Get("min_os").GetString();
    if (!p.minOs.empty() && !IsVersionLike(p.minOs)) {
        return Result<Package>::Failure(ErrorCode::ParseError, "A package's min_os is not a version", where + ": " + p.minOs);
    }
    p.format = v.Get("format").GetString();
    p.url = v.Get("url").GetString();
    p.sha256 = v.Get("sha256").GetString();
    for (char& c : p.sha256) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    p.entry = v.Get("entry").GetString();
    p.size = static_cast<uint64_t>(std::max<int64_t>(0, v.Get("size").GetInteger()));
    if (p.url.rfind("https://", 0) != 0 && p.url.rfind("http://", 0) != 0) {
        if (requireUrl) return Result<Package>::Failure(ErrorCode::ParseError, "A package has no absolute http(s) url", where + ": " + p.url);
    }
    if (!IsHex64(p.sha256)) {
        return Result<Package>::Failure(ErrorCode::ParseError, "A package has no SHA-256 of 64 hex characters", where);
    }
    if (p.format.empty()) {
        return Result<Package>::Failure(ErrorCode::ParseError, "A package names no format", where);
    }
    return Result<Package>::Succeed(std::move(p));
}

Result<Release> ReadRelease(const JSONValue& v, const std::string& where, bool requireUrls) {
    if (!v.IsObject()) return Result<Release>::Failure(ErrorCode::ParseError, "A release entry is not an object", where);
    Release r;
    r.version = v.Get("version").GetString();
    if (r.version.empty()) return Result<Release>::Failure(ErrorCode::ParseError, "A release has no version", where);
    r.channel = v.Get("channel").GetString("stable");
    if (r.channel != "stable" && r.channel != "beta") {
        return Result<Release>::Failure(ErrorCode::ParseError, "A release's channel is neither stable nor beta", where + ": " + r.channel);
    }
    r.date = v.Get("date").GetString();
    r.notes = ReadText(v.Get("notes"));
    for (const JSONValue& d : v.Get("depends").GetElements()) {
        Dependency dep;
        dep.id = d.Get("id").GetString();
        dep.version = d.Get("version").GetString();
        if (dep.id.empty() || dep.version.empty()) {
            return Result<Release>::Failure(ErrorCode::ParseError, "A dependency has no id or no version", where);
        }
        r.depends.push_back(std::move(dep));
    }
    const JSONValue& packages = v.Get("packages");
    for (size_t i = 0; i < packages.GetSize(); ++i) {
        auto p = ReadPackage(packages.At(i), where + ".packages[" + std::to_string(i) + "]", requireUrls);
        if (!p) return Result<Release>::Failure(p.error.code, p.error.message, p.error.detail);
        r.packages.push_back(std::move(p.Value()));
    }
    return Result<Release>::Succeed(std::move(r));
}

Result<Component> ReadComponent(const JSONValue& v, ComponentKind defaultKind, const std::string& where, bool requireUrls) {
    if (!v.IsObject()) return Result<Component>::Failure(ErrorCode::ParseError, "An entry is not an object", where);
    Component c;
    c.id = v.Get("id").GetString();
    if (!IsReverseDns(c.id)) {
        return Result<Component>::Failure(ErrorCode::ParseError, "An entry has no reverse-DNS id", where + ": \"" + c.id + "\"");
    }
    c.kind = v.Contains("kind") ? ParseComponentKind(v.Get("kind").GetString()) : defaultKind;
    c.publisher = v.Get("publisher").GetString();
    c.name = v.Get("name").GetString(c.id);
    c.license = v.Get("license").GetString();
    c.homepage = v.Get("homepage").GetString();
    c.icon = v.Get("icon").GetString();
    c.summary = ReadText(v.Get("summary"));
    c.description = ReadText(v.Get("description"));
    c.sourceLanguage = v.Get("source_language").GetString();
    c.machineTranslated = ReadStrings(v.Get("machine_translated"));
    c.categories = ReadStrings(v.Get("categories"));
    c.screenshots = ReadStrings(v.Get("screenshots"));
    c.tags = ReadStrings(v.Get("tags"));
    if (v.Contains("pricing")) {
        const JSONValue& pr = v.Get("pricing");
        const std::string model = pr.Get("model").GetString("free");
        if (model != "free" && model != "one-time" && model != "monthly" && model != "yearly") {
            return Result<Component>::Failure(ErrorCode::ParseError, "An entry's pricing model is unknown", where + ": " + model);
        }
        c.pricing.model = ParsePricingModel(model);
        c.pricing.price = pr.Get("price").GetString();
        c.pricing.currency = pr.Get("currency").GetString();
        c.pricing.trialDays = pr.Get("trial_days").GetInteger(0);
        if (c.pricing.model != PricingModel::Free && (c.pricing.price.empty() || c.pricing.currency.size() != 3)) {
            return Result<Component>::Failure(ErrorCode::ParseError, "A paid entry has no price or no 3-letter currency", where);
        }
    }
    if (v.Contains("updates")) {
        const JSONValue& up = v.Get("updates");
        const std::string period = up.Get("period").GetString("always");
        if (period != "always" && period != "never" && period != "until") {
            return Result<Component>::Failure(ErrorCode::ParseError, "An entry's update period is unknown", where + ": " + period);
        }
        c.updates.period = ParseUpdatePeriod(period);
        c.updates.until = up.Get("until").GetString();
        if (c.updates.period == UpdatePeriod::Until && !IsIsoDate(c.updates.until)) {
            return Result<Component>::Failure(ErrorCode::ParseError, "An entry's updates run until no date (YYYY-MM-DD)", where);
        }
    }
    {
        const std::string vis = v.Get("visibility").GetString("listed");
        if (vis != "listed" && vis != "unlisted" && vis != "hidden") {
            return Result<Component>::Failure(ErrorCode::ParseError, "An entry's visibility is unknown", where + ": " + vis);
        }
        c.visibility = ParseVisibility(vis);
        const std::string life = v.Get("lifecycle").GetString("active");
        if (life != "active" && life != "deprecated" && life != "withdrawn") {
            return Result<Component>::Failure(ErrorCode::ParseError, "An entry's lifecycle is unknown", where + ": " + life);
        }
        c.lifecycle = ParseLifecycle(life);
    }
    c.successor = v.Get("successor").GetString();
    c.ageRating = v.Get("age_rating").GetString();
    c.requiresAccount = v.Get("requires_account").GetBoolean(false);
    c.openSource = v.Get("open_source").GetBoolean(false);
    c.sourceUrl = v.Get("source_url").GetString();
    c.supportUrl = v.Get("support_url").GetString();
    c.privacyUrl = v.Get("privacy_url").GetString();
    const JSONValue& releases = v.Get("releases");
    for (size_t i = 0; i < releases.GetSize(); ++i) {
        auto r = ReadRelease(releases.At(i), where + ".releases[" + std::to_string(i) + "]", requireUrls);
        if (!r) return Result<Component>::Failure(r.error.code, r.error.message, r.error.detail);
        c.releases.push_back(std::move(r.Value()));
    }
    std::stable_sort(c.releases.begin(), c.releases.end(),
                     [](const Release& a, const Release& b) { return VersionLess(b.version, a.version); });
    return Result<Component>::Succeed(std::move(c));
}

JSONValue WritePackage(const Package& p) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("os", OSName(p.target.os));
    o.Set("arch", ArchName(p.target.arch));
    if (!p.target.variants.empty()) o.Set("variant", WriteStrings(p.target.variants));
    if (!p.minOs.empty()) o.Set("min_os", p.minOs);
    o.Set("format", p.format);
    o.Set("url", p.url);
    o.Set("size", static_cast<int64_t>(p.size));
    o.Set("sha256", p.sha256);
    if (!p.entry.empty()) o.Set("entry", p.entry);
    return o;
}

JSONValue WriteRelease(const Release& r) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("version", r.version);
    o.Set("channel", r.channel.empty() ? std::string("stable") : r.channel);
    if (!r.date.empty()) o.Set("date", r.date);
    if (!r.notes.Empty()) o.Set("notes", WriteText(r.notes));
    if (!r.depends.empty()) {
        JSONValue deps = JSONValue::MakeArray();
        for (const Dependency& d : r.depends) {
            JSONValue dep = JSONValue::MakeObject();
            dep.Set("id", d.id);
            dep.Set("version", d.version);
            deps.Append(std::move(dep));
        }
        o.Set("depends", std::move(deps));
    }
    JSONValue packages = JSONValue::MakeArray();
    for (const Package& p : r.packages) packages.Append(WritePackage(p));
    o.Set("packages", std::move(packages));
    return o;
}

JSONValue WriteComponent(const Component& c) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("id", c.id);
    o.Set("kind", ComponentKindName(c.kind));
    if (!c.publisher.empty()) o.Set("publisher", c.publisher);
    o.Set("name", c.name);
    if (!c.summary.Empty()) o.Set("summary", WriteText(c.summary));
    if (!c.description.Empty()) o.Set("description", WriteText(c.description));
    if (!c.sourceLanguage.empty()) o.Set("source_language", c.sourceLanguage);
    if (!c.machineTranslated.empty()) o.Set("machine_translated", WriteStrings(c.machineTranslated));
    if (!c.categories.empty()) o.Set("categories", WriteStrings(c.categories));
    if (!c.license.empty()) o.Set("license", c.license);
    if (!c.homepage.empty()) o.Set("homepage", c.homepage);
    if (!c.icon.empty()) o.Set("icon", c.icon);
    if (!c.screenshots.empty()) o.Set("screenshots", WriteStrings(c.screenshots));
    if (!c.tags.empty()) o.Set("tags", WriteStrings(c.tags));
    if (c.pricing.model != PricingModel::Free || c.pricing.trialDays > 0) {
        JSONValue pr = JSONValue::MakeObject();
        pr.Set("model", PricingModelName(c.pricing.model));
        if (!c.pricing.price.empty()) pr.Set("price", c.pricing.price);
        if (!c.pricing.currency.empty()) pr.Set("currency", c.pricing.currency);
        if (c.pricing.trialDays > 0) pr.Set("trial_days", static_cast<int64_t>(c.pricing.trialDays));
        o.Set("pricing", std::move(pr));
    }
    if (c.updates.period != UpdatePeriod::Unlimited) {
        JSONValue up = JSONValue::MakeObject();
        up.Set("period", UpdatePeriodName(c.updates.period));
        if (!c.updates.until.empty()) up.Set("until", c.updates.until);
        o.Set("updates", std::move(up));
    }
    if (c.visibility != Visibility::Listed) o.Set("visibility", VisibilityName(c.visibility));
    if (c.lifecycle != Lifecycle::Active) o.Set("lifecycle", LifecycleName(c.lifecycle));
    if (!c.successor.empty()) o.Set("successor", c.successor);
    if (!c.ageRating.empty()) o.Set("age_rating", c.ageRating);
    if (c.requiresAccount) o.Set("requires_account", true);
    if (c.openSource) o.Set("open_source", true);
    if (!c.sourceUrl.empty()) o.Set("source_url", c.sourceUrl);
    if (!c.supportUrl.empty()) o.Set("support_url", c.supportUrl);
    if (!c.privacyUrl.empty()) o.Set("privacy_url", c.privacyUrl);
    std::vector<const Release*> releases;
    for (const Release& r : c.releases) releases.push_back(&r);
    std::stable_sort(releases.begin(), releases.end(),
                     [](const Release* a, const Release* b) { return VersionLess(b->version, a->version); });
    JSONValue list = JSONValue::MakeArray();
    for (const Release* r : releases) list.Append(WriteRelease(*r));
    o.Set("releases", std::move(list));
    return o;
}

} // namespace

ComponentKind ParseComponentKind(std::string_view text) {
    if (text == "runtime") return ComponentKind::Runtime;
    if (text == "library") return ComponentKind::Library;
    return ComponentKind::App;
}

PricingModel ParsePricingModel(std::string_view text) {
    if (text == "one-time") return PricingModel::OneTime;
    if (text == "monthly")  return PricingModel::Monthly;
    if (text == "yearly")   return PricingModel::Yearly;
    return PricingModel::Free;
}

std::string PricingModelName(PricingModel model) {
    switch (model) {
        case PricingModel::OneTime: return "one-time";
        case PricingModel::Monthly: return "monthly";
        case PricingModel::Yearly:  return "yearly";
        case PricingModel::Free: break;
    }
    return "free";
}

UpdatePeriod ParseUpdatePeriod(std::string_view text) {
    if (text == "never") return UpdatePeriod::Never;
    if (text == "until") return UpdatePeriod::Until;
    return UpdatePeriod::Unlimited;
}

std::string UpdatePeriodName(UpdatePeriod period) {
    switch (period) {
        case UpdatePeriod::Never: return "never";
        case UpdatePeriod::Until: return "until";
        case UpdatePeriod::Unlimited: break;
    }
    return "always";
}

Visibility ParseVisibility(std::string_view text) {
    if (text == "unlisted") return Visibility::Unlisted;
    if (text == "hidden")   return Visibility::Hidden;
    return Visibility::Listed;
}

std::string VisibilityName(Visibility visibility) {
    switch (visibility) {
        case Visibility::Unlisted: return "unlisted";
        case Visibility::Hidden:   return "hidden";
        case Visibility::Listed: break;
    }
    return "listed";
}

Lifecycle ParseLifecycle(std::string_view text) {
    if (text == "deprecated") return Lifecycle::Deprecated;
    if (text == "withdrawn")  return Lifecycle::Withdrawn;
    return Lifecycle::Active;
}

std::string LifecycleName(Lifecycle lifecycle) {
    switch (lifecycle) {
        case Lifecycle::Deprecated: return "deprecated";
        case Lifecycle::Withdrawn:  return "withdrawn";
        case Lifecycle::Active: break;
    }
    return "active";
}

std::string ComponentKindName(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Runtime: return "runtime";
        case ComponentKind::Library: return "library";
        case ComponentKind::App: break;
    }
    return "app";
}

const Release* Component::Newest(std::string_view channel) const {
    for (const Release& r : releases) {
        if (r.channel == "stable" || r.channel == channel) return &r;
    }
    return nullptr;
}

const Release* Component::Find(std::string_view version) const {
    for (const Release& r : releases) if (VersionEqual(r.version, version)) return &r;
    return nullptr;
}

const Component* Catalog::FindApp(std::string_view id) const {
    for (const Component& c : apps) if (c.id == id) return &c;
    return nullptr;
}

const Component* Catalog::FindComponent(std::string_view id) const {
    for (const Component& c : components) if (c.id == id) return &c;
    return nullptr;
}

const Component* Catalog::FindAny(std::string_view id) const {
    if (const Component* c = FindApp(id)) return c;
    return FindComponent(id);
}

Result<Component> ParseComponent(const std::string& jsonText, ComponentKind defaultKind, bool requirePackageUrls) {
    UltraCanvas::JSONParseResult parse;
    const JSONValue root = UltraCanvas::JSON::Parse(jsonText, &parse);
    if (!parse.success || !root.IsObject()) {
        return Result<Component>::Failure(ErrorCode::ParseError, "The component is not valid JSON",
                                          parse.errorMessage + " at line " + std::to_string(parse.errorLine));
    }
    return ReadComponent(root, defaultKind, "component", requirePackageUrls);
}

std::string SerializeComponent(const Component& component) {
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    return UltraCanvas::JSON::Serialize(WriteComponent(component), options);
}

// ===== codes =====
CodeKind ParseCodeKind(std::string_view text) {
    return text == "affiliate" ? CodeKind::Affiliate : CodeKind::Discount;
}

std::string CodeKindName(CodeKind kind) {
    return kind == CodeKind::Affiliate ? "affiliate" : "discount";
}

std::string NormalizeCode(std::string_view code) {
    std::string out;
    for (char c : code) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

bool PromoCode::AppliesTo(std::string_view appId) const {
    if (apps.empty()) return true;
    return std::find(apps.begin(), apps.end(), std::string(appId)) != apps.end();
}

bool PromoCode::ValidOn(std::string_view date) const {
    // ISO dates compare as text.
    if (!validFrom.empty() && date < validFrom) return false;
    if (!validUntil.empty() && date > validUntil) return false;
    return true;
}

const PromoCode* Catalog::FindCode(std::string_view code) const {
    const std::string wanted = NormalizeCode(code);
    if (wanted.empty()) return nullptr;
    for (const PromoCode& c : codes) if (NormalizeCode(c.code) == wanted) return &c;
    return nullptr;
}

namespace {

Result<PromoCode> ReadCode(const JSONValue& v, const std::string& where) {
    using R = Result<PromoCode>;
    PromoCode c;
    c.code = v.Get("code").GetString();
    if (NormalizeCode(c.code).empty()) return R::Failure(ErrorCode::ParseError, "A code has no text", where);
    for (char ch : c.code) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') {
            return R::Failure(ErrorCode::ParseError, "A code is letters, digits, '-' and '_'", where + ": " + c.code);
        }
    }
    c.kind = ParseCodeKind(v.Get("kind").GetString("discount"));
    c.discountPercent = static_cast<int>(v.Get("discount_percent").GetInteger(0));
    if (c.discountPercent < 0 || c.discountPercent > 100) return R::Failure(ErrorCode::ParseError, "A discount is 0 to 100 percent", where + ": " + c.code);
    c.discountAmount = v.Get("discount_amount").GetString();
    if (!c.discountAmount.empty() && !IsDecimal(c.discountAmount)) return R::Failure(ErrorCode::ParseError, "A discount amount is a decimal", where + ": " + c.discountAmount);
    c.currency = v.Get("currency").GetString();
    for (const JSONValue& a : v.Get("apps").GetElements()) if (!a.GetString().empty()) c.apps.push_back(a.GetString());
    c.validFrom = v.Get("valid_from").GetString();
    c.validUntil = v.Get("valid_until").GetString();
    if ((!c.validFrom.empty() && !IsIsoDate(c.validFrom)) || (!c.validUntil.empty() && !IsIsoDate(c.validUntil))) {
        return R::Failure(ErrorCode::ParseError, "A code's validity is YYYY-MM-DD", where + ": " + c.code);
    }
    c.affiliate = v.Get("affiliate").GetString();
    if (c.kind == CodeKind::Affiliate && c.affiliate.empty()) return R::Failure(ErrorCode::ParseError, "An affiliate code names its affiliate", where + ": " + c.code);
    return R::Succeed(std::move(c));
}

JSONValue WriteCode(const PromoCode& c) {
    JSONValue o = JSONValue::MakeObject();
    o.Set("code", c.code);
    o.Set("kind", CodeKindName(c.kind));
    if (c.discountPercent > 0) o.Set("discount_percent", static_cast<int64_t>(c.discountPercent));
    if (!c.discountAmount.empty()) o.Set("discount_amount", c.discountAmount);
    if (!c.currency.empty()) o.Set("currency", c.currency);
    if (!c.apps.empty()) {
        JSONValue apps = JSONValue::MakeArray();
        for (const std::string& a : c.apps) apps.Append(a);
        o.Set("apps", std::move(apps));
    }
    if (!c.validFrom.empty()) o.Set("valid_from", c.validFrom);
    if (!c.validUntil.empty()) o.Set("valid_until", c.validUntil);
    if (!c.affiliate.empty()) o.Set("affiliate", c.affiliate);
    return o;
}

} // namespace

Result<Catalog> ParseCatalog(const std::string& jsonText) {
    UltraCanvas::JSONParseResult parse;
    const JSONValue root = UltraCanvas::JSON::Parse(jsonText, &parse);
    if (!parse.success || !root.IsObject()) {
        return Result<Catalog>::Failure(ErrorCode::ParseError, "The catalog is not valid JSON",
                                        parse.errorMessage + " at line " + std::to_string(parse.errorLine));
    }
    Catalog catalog;
    catalog.schema = static_cast<int>(root.Get("schema").GetInteger(0));
    if (catalog.schema < 1) {
        return Result<Catalog>::Failure(ErrorCode::ParseError, "The catalog names no schema version");
    }
    if (catalog.schema > kSupportedSchema) {
        return Result<Catalog>::Failure(ErrorCode::SchemaTooNew,
                                        "This catalog needs a newer UltraStore",
                                        "schema " + std::to_string(catalog.schema) + ", this build reads " +
                                            std::to_string(kSupportedSchema));
    }
    const int64_t sequence = root.Get("sequence").GetInteger(-1);
    if (sequence < 0) return Result<Catalog>::Failure(ErrorCode::ParseError, "The catalog has no sequence number");
    catalog.sequence = static_cast<uint64_t>(sequence);
    catalog.generated = root.Get("generated").GetString();
    for (const JSONValue& k : root.Get("trusted_keys").GetElements()) {
        std::string hex = k.GetString();
        std::vector<uint8_t> bytes;
        if (!FromHex(hex, bytes) || bytes.size() != 32) {
            return Result<Catalog>::Failure(ErrorCode::ParseError, "A trusted key is not a 32-byte hex public key", hex);
        }
        hex = ToHex(bytes);   // lower case, as the store compares them
        if (std::find(catalog.trustedKeys.begin(), catalog.trustedKeys.end(), hex) == catalog.trustedKeys.end()) {
            catalog.trustedKeys.push_back(std::move(hex));
        }
    }
    catalog.paymentUrl = root.Get("payment_url").GetString();
    if (!catalog.paymentUrl.empty() && catalog.paymentUrl.rfind("https://", 0) != 0 && catalog.paymentUrl.rfind("http://", 0) != 0) {
        return Result<Catalog>::Failure(ErrorCode::ParseError, "The payment service is an http(s) URL", catalog.paymentUrl);
    }
    catalog.licenceKey = root.Get("licence_key").GetString();
    if (!catalog.licenceKey.empty()) {
        std::vector<uint8_t> bytes;
        if (!FromHex(catalog.licenceKey, bytes) || bytes.size() != 32) return Result<Catalog>::Failure(ErrorCode::ParseError, "The licence key is not a 32-byte hex public key", catalog.licenceKey);
        catalog.licenceKey = ToHex(bytes);
    }
    const JSONValue& codes = root.Get("codes");
    for (size_t i = 0; i < codes.GetSize(); ++i) {
        auto c = ReadCode(codes.At(i), "codes[" + std::to_string(i) + "]");
        if (!c) return Result<Catalog>::Failure(c.error.code, c.error.message, c.error.detail);
        if (catalog.FindCode(c.Value().code)) return Result<Catalog>::Failure(ErrorCode::ParseError, "Two codes read the same", c.Value().code);
        catalog.codes.push_back(std::move(c.Value()));
    }
    for (const JSONValue& p : root.Get("publishers").GetElements()) {
        Publisher pub;
        pub.id = p.Get("id").GetString();
        pub.name = p.Get("name").GetString();
        pub.url = p.Get("url").GetString();
        if (!pub.id.empty()) catalog.publishers.push_back(std::move(pub));
    }
    const JSONValue& apps = root.Get("apps");
    for (size_t i = 0; i < apps.GetSize(); ++i) {
        auto c = ReadComponent(apps.At(i), ComponentKind::App, "apps[" + std::to_string(i) + "]", true);
        if (!c) return Result<Catalog>::Failure(c.error.code, c.error.message, c.error.detail);
        if (c.Value().kind != ComponentKind::App) {
            return Result<Catalog>::Failure(ErrorCode::ParseError, "A component is listed among the apps",
                                            "apps[" + std::to_string(i) + "]: " + c.Value().id);
        }
        catalog.apps.push_back(std::move(c.Value()));
    }
    const JSONValue& components = root.Get("components");
    for (size_t i = 0; i < components.GetSize(); ++i) {
        auto c = ReadComponent(components.At(i), ComponentKind::Runtime, "components[" + std::to_string(i) + "]", true);
        if (!c) return Result<Catalog>::Failure(c.error.code, c.error.message, c.error.detail);
        if (c.Value().kind == ComponentKind::App) {
            return Result<Catalog>::Failure(ErrorCode::ParseError, "An app is listed among the components",
                                            "components[" + std::to_string(i) + "]: " + c.Value().id);
        }
        catalog.components.push_back(std::move(c.Value()));
    }
    // Ids are unique across both lists.
    std::vector<std::string> ids;
    for (const Component& c : catalog.apps) ids.push_back(c.id);
    for (const Component& c : catalog.components) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end());
    const auto dup = std::adjacent_find(ids.begin(), ids.end());
    if (dup != ids.end()) {
        return Result<Catalog>::Failure(ErrorCode::ParseError, "Two entries share an id", *dup);
    }
    return Result<Catalog>::Succeed(std::move(catalog));
}

std::string SerializeCatalog(const Catalog& catalog) {
    JSONValue root = JSONValue::MakeObject();
    // trusted_keys is a schema 2 member: a catalog that carries them says so.
    const int schema = std::max(catalog.schema > 0 ? catalog.schema : kSupportedSchema, catalog.trustedKeys.empty() ? 1 : 2);
    root.Set("schema", schema);
    if (!catalog.generated.empty()) root.Set("generated", catalog.generated);
    root.Set("sequence", static_cast<int64_t>(catalog.sequence));
    if (!catalog.trustedKeys.empty()) {
        JSONValue keys = JSONValue::MakeArray();
        for (const std::string& k : catalog.trustedKeys) keys.Append(k);
        root.Set("trusted_keys", std::move(keys));
    }
    JSONValue publishers = JSONValue::MakeArray();
    for (const Publisher& p : catalog.publishers) {
        JSONValue o = JSONValue::MakeObject();
        o.Set("id", p.id);
        o.Set("name", p.name);
        if (!p.url.empty()) o.Set("url", p.url);
        publishers.Append(std::move(o));
    }
    root.Set("publishers", std::move(publishers));
    if (!catalog.paymentUrl.empty()) root.Set("payment_url", catalog.paymentUrl);
    if (!catalog.licenceKey.empty()) root.Set("licence_key", catalog.licenceKey);
    if (!catalog.codes.empty()) {
        JSONValue codes = JSONValue::MakeArray();
        for (const PromoCode& c : catalog.codes) codes.Append(WriteCode(c));
        root.Set("codes", std::move(codes));
    }
    JSONValue components = JSONValue::MakeArray();
    for (const Component& c : catalog.components) components.Append(WriteComponent(c));
    root.Set("components", std::move(components));
    JSONValue apps = JSONValue::MakeArray();
    for (const Component& c : catalog.apps) apps.Append(WriteComponent(c));
    root.Set("apps", std::move(apps));
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    return UltraCanvas::JSON::Serialize(root, options);
}

} // namespace UltraStore
