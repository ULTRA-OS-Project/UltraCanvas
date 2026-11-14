// Apps/UltraStoreAdmin/ui/AdminComponentPage.cpp
// The form for one app or component: every setting of the catalog's table,
// the texts per language, the pictures, the releases with their packages.
// The form edits a draft; Save writes it whole (AdminDatabase::PutComponent,
// which reads it back the way the store does before storing it).
// Version: 0.1.1 - categories from the store's fixed list only (UltraStore::StoreCategories)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminWindow.h"

#include "AdminTheme.h"
#include "Translation.h"
#include "StoreQuery.h"   // CategoryName

#include "UltraCanvasAlert.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

using namespace UltraCanvas;
using UltraStore::Component;
using UltraStore::ComponentKind;
using UltraStore::Package;
using UltraStore::Release;

namespace UltraStoreAdmin::Ui {

namespace {

constexpr const char* kComponent = "component";
constexpr const char* kItemPrefix = "c:";

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string JoinWords(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& w : words) out += (out.empty() ? "" : " ") + w;
    return out;
}

std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    return s.substr(b);
}

int IndexOfValue(const std::vector<std::string>& values, const std::string& value) {
    const auto it = std::find(values.begin(), values.end(), value);
    return it == values.end() ? 0 : static_cast<int>(it - values.begin());
}

const std::vector<std::string> kKinds = {"app", "runtime", "library"};
const std::vector<std::string> kVisibilities = {"listed", "unlisted", "hidden"};
const std::vector<std::string> kLifecycles = {"active", "deprecated", "withdrawn"};
const std::vector<std::string> kAges = {"", "3", "7", "12", "16", "18"};
const std::vector<std::string> kPricing = {"free", "one-time", "monthly", "yearly"};
const std::vector<std::string> kPeriods = {"always", "never", "until"};
const std::vector<std::string> kOses = {"linux", "windows", "macos", "ultraos", "ios", "android", "wasm"};
const std::vector<std::string> kArchs = {"x86_64", "arm64", "armv7", "riscv64", "wasm32", "universal", "any"};

std::shared_ptr<UltraCanvasDropdown> Choice(const std::string& id, const std::vector<std::string>& values,
                                            const std::vector<std::string>& labels, float width = 200) {
    auto d = Theme::Pin(CreateDropdown(id, 0, 0, width, Theme::kControlHeight));
    for (size_t i = 0; i < values.size(); ++i) d->AddItem(labels.empty() ? values[i] : labels[i], values[i]);
    d->SetSelectedIndex(0, false);
    return d;
}

std::string LanguageLabel(const std::string& code) {
    const std::string name = LanguageName(code);
    return name == code ? code : code + " - " + name;
}

} // namespace

// ===========================================================================
// Building the form
// ===========================================================================
void AdminWindow::BuildComponentPage() {
    auto page = Theme::Column(kComponent, Theme::kGap);
    page->SetPadding(Theme::kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto header = Theme::Row("uaC.header", Theme::kGap);
    formTitle_ = Theme::Heading("uaC.title", Tr("App"));
    formTitle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header->AddChild(formTitle_);
    auto save = Theme::Button("uaC.save", Tr("Save"), 90, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveComponent(); };
    header->AddChild(save);
    auto revert = Theme::Button("uaC.revert", Tr("Revert"), 90);
    revert->onClick = [this]() { RevertComponent(); };
    header->AddChild(revert);
    auto remove = Theme::Button("uaC.delete", Tr("Delete"), 90, Theme::ButtonKind::Danger);
    remove->onClick = [this]() { DeleteComponent(); };
    header->AddChild(remove);
    page->AddChild(header);

    auto body = CreateScrollableContainer("uaC.body", 0, 0, 0, 0);
    ContainerStyle style = body->GetContainerStyle();
    style.forceShowVerticalScrollbar = false;
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    body->SetContainerStyle(style);
    body->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    body->SetPadding(2, 16, 2, 2);
    formBody_ = body;

    auto input = [](const std::string& id, float w = 420) { return Theme::Pin(CreateTextInput(id, 0, 0, w, Theme::kControlHeight)); };

    // ----- identity -----
    auto identity = Theme::Surface("uaC.identity", Theme::kInnerGap);
    identity->AddChild(Theme::Text("uaC.identity.title", Tr("Identity"), 14.0f, FontWeight::Bold));
    fId_ = input("uaC.id");
    identity->AddChild(Theme::Field("uaC.idRow", Tr("Id (reverse DNS)"), fId_));
    fKind_ = Choice("uaC.kind", kKinds, {Tr("App"), Tr("Runtime"), Tr("Library")});
    identity->AddChild(Theme::Field("uaC.kindRow", Tr("Kind"), fKind_));
    fName_ = input("uaC.name");
    identity->AddChild(Theme::Field("uaC.nameRow", Tr("Name"), fName_));
    fPublisher_ = Choice("uaC.publisher", {""}, {Tr("(none)")}, 300);
    identity->AddChild(Theme::Field("uaC.publisherRow", Tr("Publisher"), fPublisher_));
    fLicense_ = input("uaC.license", 200);
    identity->AddChild(Theme::Field("uaC.licenseRow", Tr("License"), fLicense_));
    fHomepage_ = input("uaC.homepage");
    identity->AddChild(Theme::Field("uaC.homepageRow", Tr("Homepage"), fHomepage_));
    fSupport_ = input("uaC.support");
    identity->AddChild(Theme::Field("uaC.supportRow", Tr("Support URL"), fSupport_));
    fPrivacy_ = input("uaC.privacy");
    identity->AddChild(Theme::Field("uaC.privacyRow", Tr("Privacy URL"), fPrivacy_));
    fOpenSource_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaC.oss", 0, 0, 300, Theme::kControlHeight, Tr("Open source"), false));
    identity->AddChild(fOpenSource_);
    fSource_ = input("uaC.source");
    identity->AddChild(Theme::Field("uaC.sourceRow", Tr("Source URL"), fSource_));
    // The store's list, not free words: it names each in the buyer's language.
    fCategories_ = Theme::Pin(CreateChipGroup("uaC.categories", 0, 0, 0, 0));
    fCategories_->SetSelectionMode(ChipGroupSelectionMode::Multiple);
    fCategories_->SetSpacing(6.0f);
    for (const std::string& id : UltraStore::StoreCategories()) fCategories_->AddChip(UltraStore::CategoryName(id));
    fCategories_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    fCategories_->onSelectedIndicesChanged = [this](const std::vector<int>&) { draftDirty_ = true; };
    identity->AddChild(Theme::Field("uaC.categoriesRow", Tr("Categories"), fCategories_, true));
    fTags_ = input("uaC.tags");
    fTags_->SetPlaceholder(Tr("free-form words for the search"));
    identity->AddChild(Theme::Field("uaC.tagsRow", Tr("Tags"), fTags_));
    body->AddChild(identity);

    // ----- the admin backend's settings -----
    auto settings = Theme::Surface("uaC.settings", Theme::kInnerGap);
    settings->AddChild(Theme::Text("uaC.settings.title", Tr("Store settings"), 14.0f, FontWeight::Bold));
    fVisibility_ = Choice("uaC.visibility", kVisibilities, {Tr("Listed"), Tr("Unlisted (by id or link)"), Tr("Hidden (updates only)")}, 260);
    settings->AddChild(Theme::Field("uaC.visibilityRow", Tr("Visibility"), fVisibility_));
    fLifecycle_ = Choice("uaC.lifecycle", kLifecycles, {Tr("Active"), Tr("Deprecated (names a successor)"), Tr("Withdrawn (no new installs)")}, 260);
    settings->AddChild(Theme::Field("uaC.lifecycleRow", Tr("Lifecycle"), fLifecycle_));
    fSuccessor_ = input("uaC.successor");
    fSuccessor_->SetPlaceholder(Tr("the id to install instead, when deprecated or withdrawn"));
    settings->AddChild(Theme::Field("uaC.successorRow", Tr("Successor"), fSuccessor_));
    fAgeRating_ = Choice("uaC.age", kAges, {Tr("Everyone"), "3+", "7+", "12+", "16+", "18+"}, 160);
    settings->AddChild(Theme::Field("uaC.ageRow", Tr("Age rating"), fAgeRating_));
    fRequiresAccount_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaC.account", 0, 0, 420, Theme::kControlHeight, Tr("Needs an account of its own to be useful"), false));
    settings->AddChild(fRequiresAccount_);
    fFeatured_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaC.featured", 0, 0, 420, Theme::kControlHeight, Tr("Featured: the store's hero and its Editor's picks"), false));
    settings->AddChild(fFeatured_);
    fPricingModel_ = Choice("uaC.pricing", kPricing, {Tr("Free"), Tr("One-time purchase"), Tr("Monthly fee"), Tr("Yearly fee")}, 200);
    settings->AddChild(Theme::Field("uaC.pricingRow", Tr("Monetary"), fPricingModel_));
    auto priceRow = Theme::Row("uaC.priceRow", Theme::kInnerGap);
    fPrice_ = input("uaC.price", 100);
    fPrice_->SetPlaceholder("29.90");
    priceRow->AddChild(fPrice_);
    fCurrency_ = input("uaC.currency", 70);
    fCurrency_->SetPlaceholder("EUR");
    priceRow->AddChild(fCurrency_);
    priceRow->AddChild(Theme::Caption("uaC.trialLabel", Tr("trial days")));
    fTrialDays_ = input("uaC.trial", 60);
    fTrialDays_->SetPlaceholder("0");
    priceRow->AddChild(fTrialDays_);
    settings->AddChild(Theme::Field("uaC.priceField", Tr("Price, currency, trial"), priceRow));
    fUpdatePeriod_ = Choice("uaC.period", kPeriods, {Tr("Always free"), Tr("Never (the bought version only)"), Tr("Until a date")}, 260);
    settings->AddChild(Theme::Field("uaC.periodRow", Tr("Free update period"), fUpdatePeriod_));
    fUntil_ = input("uaC.until", 140);
    fUntil_->SetPlaceholder("2027-12-31");
    settings->AddChild(Theme::Field("uaC.untilRow", Tr("Free updates until"), fUntil_));
    body->AddChild(settings);

    // ----- texts -----
    auto texts = Theme::Surface("uaC.texts", Theme::kInnerGap);
    texts->AddChild(Theme::Text("uaC.texts.title", Tr("Texts"), 14.0f, FontWeight::Bold));
    fSourceLanguage_ = input("uaC.sourceLanguage", 80);
    fSourceLanguage_->SetPlaceholder("en");
    texts->AddChild(Theme::Field("uaC.sourceLanguageRow", Tr("Written in (source language)"), fSourceLanguage_));
    auto langRow = Theme::Row("uaC.langRow", Theme::kInnerGap, true);
    fLanguage_ = Theme::Pin(CreateDropdown("uaC.language", 0, 0, 240, Theme::kControlHeight));
    fLanguage_->onSelectionChanged = [this](int, const DropdownItem& item) {
        ReadForm(*std::make_unique<std::string>());   // keep the texts of the language shown
        formLanguage_ = item.value;
        RebuildTextsSection();
    };
    langRow->AddChild(fLanguage_);
    fNewLanguage_ = input("uaC.newLanguage", 70);
    fNewLanguage_->SetPlaceholder("de");
    langRow->AddChild(fNewLanguage_);
    auto addLanguage = Theme::Button("uaC.addLanguage", Tr("Add language"), 110);
    addLanguage->onClick = [this]() {
        const std::string code = Trim(fNewLanguage_->GetText());
        if (code.size() < 2) { SetStatus(Tr("A language is a code such as de"), true); return; }
        std::string error;
        ReadForm(error);
        formLanguage_ = code;
        fNewLanguage_->SetText("");
        RebuildTextsSection();
    };
    langRow->AddChild(addLanguage);
    auto translate = Theme::Button("uaC.translate", Tr("Translate the missing languages"), 220);
    translate->SetTooltip(Tr("UltraAI fills every language of the settings the texts lack, from the source language"));
    translate->onClick = [this]() { TranslateDraft(); };
    langRow->AddChild(translate);
    texts->AddChild(Theme::Field("uaC.langField", Tr("Language shown"), langRow));
    fMachineNote_ = Theme::Caption("uaC.machineNote", "");
    texts->AddChild(fMachineNote_);
    fSummary_ = input("uaC.summary", 600);
    texts->AddChild(Theme::Field("uaC.summaryRow", Tr("Summary (one line)"), fSummary_));
    fDescription_ = Theme::Pin(std::make_shared<UltraCanvasTextArea>("uaC.description", 0.0f, 140.0f));
    fDescription_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(140));
    fDescription_->SetWordWrap(true);
    fDescription_->SetShowLineNumbers(false);
    fDescription_->SetHighlightSyntax(false);
    fDescription_->SetFontSize(12.0f);
    fDescription_->SetBackgroundColor(Theme::kPageBackground);
    fDescription_->SetBorders(1.0f, Theme::kBorder, 4.0f);
    texts->AddChild(Theme::Text("uaC.descriptionLabel", Tr("Description"), 12.0f));
    texts->AddChild(fDescription_);
    fTexts_ = texts;
    body->AddChild(texts);

    // ----- pictures -----
    auto media = Theme::Surface("uaC.media", Theme::kInnerGap);
    media->AddChild(Theme::Text("uaC.media.title", Tr("Pictures"), 14.0f, FontWeight::Bold));
    auto iconRow = Theme::Row("uaC.iconRow", Theme::kInnerGap);
    fIcon_ = input("uaC.icon");
    fIcon_->SetPlaceholder(Tr("media/<id>/icon.png, or a URL"));
    iconRow->AddChild(fIcon_);
    auto pickIcon = Theme::Button("uaC.pickIcon", Tr("Choose"), 90);
    pickIcon->onClick = [this]() { PickIcon(); };
    iconRow->AddChild(pickIcon);
    media->AddChild(Theme::Field("uaC.iconField", Tr("Icon"), iconRow));
    fScreenshots_ = Theme::Column("uaC.screenshots", 4.0f);
    media->AddChild(Theme::Field("uaC.screenshotsField", Tr("Screenshots"), fScreenshots_, true));
    auto addShot = Theme::Button("uaC.addShot", Tr("Add screenshot"), 140);
    addShot->onClick = [this]() { PickScreenshot(); };
    auto shotRow = Theme::Row("uaC.shotRow", Theme::kInnerGap);
    shotRow->AddChild(addShot);
    media->AddChild(shotRow);
    body->AddChild(media);

    // ----- releases -----
    auto releases = Theme::Surface("uaC.releases", Theme::kInnerGap);
    releases->AddChild(Theme::Text("uaC.releases.title", Tr("Releases and packages"), 14.0f, FontWeight::Bold));
    releases->AddChild(Theme::Caption("uaC.releases.hint",
        Tr("A release per version, newest first, with the exact components it was built against; a package per target "
        "with the build's file, which goes into the package store under its hash.")));
    fReleases_ = Theme::Column("uaC.releaseList", Theme::kInnerGap);
    releases->AddChild(fReleases_);
    // The add-release editor.
    releaseEditor_ = Theme::Column("uaC.releaseEditor", Theme::kInnerGap);
    releaseEditor_->SetBackgroundColor(Theme::kPageBackground);
    releaseEditor_->SetBorders(1.0f, Theme::kBorder, 6.0f);
    releaseEditor_->SetPadding(8.0f);
    rVersion_ = input("uaC.rVersion", 140);
    rVersion_->SetPlaceholder("1.2.0");
    releaseEditor_->AddChild(Theme::Field("uaC.rVersionRow", Tr("Version"), rVersion_));
    rChannel_ = Choice("uaC.rChannel", {"stable", "beta"}, {Tr("Stable"), Tr("Beta")}, 140);
    releaseEditor_->AddChild(Theme::Field("uaC.rChannelRow", Tr("Channel"), rChannel_));
    rDate_ = input("uaC.rDate", 140);
    rDate_->SetPlaceholder("2026-10-27");
    releaseEditor_->AddChild(Theme::Field("uaC.rDateRow", Tr("Date"), rDate_));
    rDepends_ = input("uaC.rDepends");
    rDepends_->SetPlaceholder(Tr("org.ultraos.runtime@0.9.410 (separated by spaces)"));
    releaseEditor_->AddChild(Theme::Field("uaC.rDependsRow", Tr("Needs (id@version)"), rDepends_));
    rNotes_ = input("uaC.rNotes", 600);
    rNotes_->SetPlaceholder(Tr("what changed, in the source language"));
    releaseEditor_->AddChild(Theme::Field("uaC.rNotesRow", Tr("Release notes"), rNotes_));
    auto rButtons = Theme::Row("uaC.rButtons", Theme::kInnerGap);
    auto rAdd = Theme::Button("uaC.rAdd", Tr("Add the release"), 130, Theme::ButtonKind::Primary);
    rAdd->onClick = [this]() { AddRelease(); };
    rButtons->AddChild(rAdd);
    auto rCancel = Theme::Button("uaC.rCancel", Tr("Cancel"), 80);
    rCancel->onClick = [this]() { releaseEditor_->SetVisible(false); formBody_->InvalidateLayout(); };
    rButtons->AddChild(rCancel);
    releaseEditor_->AddChild(rButtons);
    releaseEditor_->SetVisible(false);
    releases->AddChild(releaseEditor_);
    // The add-package editor.
    packageEditor_ = Theme::Column("uaC.packageEditor", Theme::kInnerGap);
    packageEditor_->SetBackgroundColor(Theme::kPageBackground);
    packageEditor_->SetBorders(1.0f, Theme::kBorder, 6.0f);
    packageEditor_->SetPadding(8.0f);
    pFile_ = Theme::Text("uaC.pFile", "", 12.0f);
    packageEditor_->AddChild(pFile_);
    pOs_ = Choice("uaC.pOs", kOses, {}, 160);
    packageEditor_->AddChild(Theme::Field("uaC.pOsRow", Tr("Operating system"), pOs_));
    pArch_ = Choice("uaC.pArch", kArchs, {}, 160);
    packageEditor_->AddChild(Theme::Field("uaC.pArchRow", Tr("Processor"), pArch_));
    pVariants_ = input("uaC.pVariants");
    pVariants_->SetPlaceholder(Tr("glibc-2.31 rpi (separated by spaces)"));
    packageEditor_->AddChild(Theme::Field("uaC.pVariantsRow", Tr("Variants"), pVariants_));
    pMinOs_ = input("uaC.pMinOs", 160);
    pMinOs_->SetPlaceholder("14.0 / 10.0.17763");
    packageEditor_->AddChild(Theme::Field("uaC.pMinOsRow", Tr("Minimum OS version"), pMinOs_));
    pEntry_ = input("uaC.pEntry");
    pEntry_->SetPlaceholder(Tr("bin/app (suggested from the archive)"));
    packageEditor_->AddChild(Theme::Field("uaC.pEntryRow", Tr("Entry to start"), pEntry_));
    pAccess_ = Choice("uaC.pAccess", {"public", "restricted"}, {Tr("Public (free)"), Tr("Restricted (buyers only)")}, 220);
    packageEditor_->AddChild(Theme::Field("uaC.pAccessRow", Tr("Access"), pAccess_));
    auto pButtons = Theme::Row("uaC.pButtons", Theme::kInnerGap);
    auto pAdd = Theme::Button("uaC.pAdd", Tr("Add the package"), 130, Theme::ButtonKind::Primary);
    pAdd->onClick = [this]() { AddPackage(packageEditorVersion_); };
    pButtons->AddChild(pAdd);
    auto pCancel = Theme::Button("uaC.pCancel", Tr("Cancel"), 80);
    pCancel->onClick = [this]() { packageEditor_->SetVisible(false); formBody_->InvalidateLayout(); };
    pButtons->AddChild(pCancel);
    packageEditor_->AddChild(pButtons);
    packageEditor_->SetVisible(false);
    releases->AddChild(packageEditor_);
    auto newRelease = Theme::Button("uaC.newRelease", Tr("Add release"), 120);
    newRelease->onClick = [this]() {
        packageEditor_->SetVisible(false);
        releaseEditor_->SetVisible(true);
        rVersion_->SetText("");
        rDate_->SetText(NowIso8601().substr(0, 10));
        formBody_->InvalidateLayout();
    };
    auto relRow = Theme::Row("uaC.relRow", Theme::kInnerGap);
    relRow->AddChild(newRelease);
    releases->AddChild(relRow);
    body->AddChild(releases);

    page->AddChild(body);
    pages_[kComponent] = page;
    pageHost_->AddChild(page);
    page->SetVisible(false);

    // Every edit marks the draft.
    for (auto* in : {fId_.get(), fName_.get(), fLicense_.get(), fHomepage_.get(), fSupport_.get(), fPrivacy_.get(), fSource_.get(),
                     fSuccessor_.get(), fTags_.get(), fSourceLanguage_.get(), fPrice_.get(), fCurrency_.get(),
                     fTrialDays_.get(), fUntil_.get(), fIcon_.get(), fSummary_.get()}) {
        in->onTextChanged = [this](const std::string&) { draftDirty_ = true; };
    }
    fDescription_->SetOnTextChanged([this](const std::string&) { draftDirty_ = true; });
}

// ===========================================================================
// Loading and reading the form
// ===========================================================================
void AdminWindow::ShowComponent(const std::string& id) {
    const auto found = std::find_if(components_.begin(), components_.end(), [&](const Component& c) { return c.id == id; });
    if (found == components_.end()) return;
    draft_ = *found;
    draftIsNew_ = false;
    draftDirty_ = false;
    formLanguage_ = draft_.sourceLanguage.empty() ? "en" : draft_.sourceLanguage;
    FillForm();
    ShowPage(kComponent);
    if (sidebar_->GetSelectedId() != kItemPrefix + id) sidebar_->Select(kItemPrefix + id, false);
}

void AdminWindow::NewComponent(ComponentKind kind) {
    const std::string id = Trim(newId_->GetText()), name = Trim(newName_->GetText());
    if (id.empty() || id.find('.') == std::string::npos) { SetStatus(Tr("The id is reverse DNS, such as org.ultraos.myapp"), true); return; }
    for (const Component& c : components_) if (c.id == id) { SetStatus(TrF("{0} exists already", id), true); return; }
    Component c;
    c.id = id;
    c.name = name.empty() ? id : name;
    c.kind = kind;
    c.sourceLanguage = "en";
    if (const auto r = db_.PutComponent(c); !r) { SetStatus(r.error.message, true); return; }
    newId_->SetText("");
    newName_->SetText("");
    ReloadComponents();
    ShowComponent(id);
    SetStatus(TrF("{0} created: fill in the form and add a release", id));
}

void AdminWindow::FillForm() {
    const Component& c = draft_;
    formTitle_->SetText(c.kind == ComponentKind::App ? TrF("App {0}", c.id)
                        : c.kind == ComponentKind::Runtime ? TrF("Runtime {0}", c.id) : TrF("Library {0}", c.id));
    fId_->SetText(c.id);
    fId_->SetDisabled(!draftIsNew_);
    fKind_->SetSelectedIndex(IndexOfValue(kKinds, UltraStore::ComponentKindName(c.kind)), false);
    fName_->SetText(c.name);
    LoadSettingsPage();   // the publisher list
    int pubIndex = 0;
    for (size_t i = 0; i < publishers_.size(); ++i) if (publishers_[i].id == c.publisher) pubIndex = static_cast<int>(i) + 1;
    fPublisher_->SetSelectedIndex(pubIndex, false);
    fLicense_->SetText(c.license);
    fHomepage_->SetText(c.homepage);
    fSupport_->SetText(c.supportUrl);
    fPrivacy_->SetText(c.privacyUrl);
    fOpenSource_->SetChecked(c.openSource);
    fSource_->SetText(c.sourceUrl);
    {
        // A category from before the list, or a later list's, is not offered:
        // Save keeps only the list's.
        std::vector<int> picked;
        const auto& all = UltraStore::StoreCategories();
        for (const std::string& written : c.categories) {
            const auto at = std::find(all.begin(), all.end(), UltraStore::CategoryId(written));
            if (at != all.end()) picked.push_back(static_cast<int>(at - all.begin()));
        }
        fCategories_->SetSelectedIndices(picked);
    }
    fTags_->SetText(JoinWords(c.tags));
    fVisibility_->SetSelectedIndex(IndexOfValue(kVisibilities, UltraStore::VisibilityName(c.visibility)), false);
    fLifecycle_->SetSelectedIndex(IndexOfValue(kLifecycles, UltraStore::LifecycleName(c.lifecycle)), false);
    fSuccessor_->SetText(c.successor);
    fAgeRating_->SetSelectedIndex(IndexOfValue(kAges, c.ageRating), false);
    fRequiresAccount_->SetChecked(c.requiresAccount);
    fFeatured_->SetChecked(c.featured);
    fPricingModel_->SetSelectedIndex(IndexOfValue(kPricing, UltraStore::PricingModelName(c.pricing.model)), false);
    fPrice_->SetText(c.pricing.price);
    fCurrency_->SetText(c.pricing.currency);
    fTrialDays_->SetText(c.pricing.trialDays ? std::to_string(c.pricing.trialDays) : "");
    fUpdatePeriod_->SetSelectedIndex(IndexOfValue(kPeriods, UltraStore::UpdatePeriodName(c.updates.period)), false);
    fUntil_->SetText(c.updates.until);
    fSourceLanguage_->SetText(c.sourceLanguage);
    fIcon_->SetText(c.icon);
    RebuildTextsSection();
    // Screenshots.
    fScreenshots_->ClearChildren();
    int n = 0;
    for (const std::string& shot : c.screenshots) {
        auto row = Theme::Row("uaC.shot" + std::to_string(n), Theme::kInnerGap);
        auto text = Theme::Caption("uaC.shot" + std::to_string(n) + ".path", shot);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(text);
        auto remove = Theme::Button("uaC.shot" + std::to_string(n) + ".remove", Tr("Remove"), 72);
        remove->onClick = [this, shot]() {
            std::string error;
            ReadForm(error);
            // The variants run beside the screenshots: the removed one's go too.
            draft_.screenshotVariants.resize(draft_.screenshots.size());
            for (size_t i = draft_.screenshots.size(); i-- > 0;) {
                if (draft_.screenshots[i] != shot) continue;
                draft_.screenshots.erase(draft_.screenshots.begin() + static_cast<long>(i));
                draft_.screenshotVariants.erase(draft_.screenshotVariants.begin() + static_cast<long>(i));
            }
            draftDirty_ = true;
            FillForm();
        };
        row->AddChild(remove);
        fScreenshots_->AddChild(row);
        ++n;
    }
    if (c.screenshots.empty()) fScreenshots_->AddChild(Theme::Caption("uaC.noShots", "none"));
    RebuildReleasesSection();
    releaseEditor_->SetVisible(false);
    packageEditor_->SetVisible(false);
    draftDirty_ = false;
    formBody_->ScrollToVertical(0);
    formBody_->InvalidateLayout();
    formBody_->RequestRedraw();
}

void AdminWindow::RebuildTextsSection() {
    // The languages the draft has, the source first, and the one shown.
    std::vector<std::string> languages;
    auto add = [&](const std::string& l) { if (!l.empty() && std::find(languages.begin(), languages.end(), l) == languages.end()) languages.push_back(l); };
    add(draft_.sourceLanguage.empty() ? std::string("en") : draft_.sourceLanguage);
    add(formLanguage_);
    for (const auto& [l, t] : draft_.summary.byLanguage) add(l);
    for (const auto& [l, t] : draft_.description.byLanguage) add(l);
    for (const Release& r : draft_.releases) for (const auto& [l, t] : r.notes.byLanguage) add(l);
    fLanguage_->ClearItems();
    int selected = 0;
    for (size_t i = 0; i < languages.size(); ++i) {
        const bool machine = std::find(draft_.machineTranslated.begin(), draft_.machineTranslated.end(), languages[i]) != draft_.machineTranslated.end();
        fLanguage_->AddItem(machine ? TrF("{0} (machine)", LanguageLabel(languages[i])) : LanguageLabel(languages[i]), languages[i]);
        if (languages[i] == formLanguage_) selected = static_cast<int>(i);
    }
    fLanguage_->SetSelectedIndex(selected, false);
    const bool machine = std::find(draft_.machineTranslated.begin(), draft_.machineTranslated.end(), formLanguage_) != draft_.machineTranslated.end();
    fMachineNote_->SetText(machine ? Tr("This language was translated automatically; editing it here makes it a human text.")
                                   : (formLanguage_ == (draft_.sourceLanguage.empty() ? "en" : draft_.sourceLanguage) ? Tr("The source language.") : std::string()));
    fSummary_->SetText(draft_.summary.byLanguage.count(formLanguage_) ? draft_.summary.byLanguage.at(formLanguage_) : "");
    fDescription_->SetText(draft_.description.byLanguage.count(formLanguage_) ? draft_.description.byLanguage.at(formLanguage_) : "", false);
}

bool AdminWindow::ReadForm(std::string& error) {
    Component& c = draft_;
    if (draftIsNew_) c.id = Trim(fId_->GetText());
    c.kind = UltraStore::ParseComponentKind(kKinds[std::max(0, fKind_->GetSelectedIndex())]);
    c.name = Trim(fName_->GetText());
    const int pub = fPublisher_->GetSelectedIndex();
    c.publisher = pub > 0 && pub <= static_cast<int>(publishers_.size()) ? publishers_[pub - 1].id : std::string();
    c.license = Trim(fLicense_->GetText());
    c.homepage = Trim(fHomepage_->GetText());
    c.supportUrl = Trim(fSupport_->GetText());
    c.privacyUrl = Trim(fPrivacy_->GetText());
    c.openSource = fOpenSource_->IsChecked();
    c.sourceUrl = Trim(fSource_->GetText());
    c.categories.clear();
    for (int index : fCategories_->GetSelectedIndices()) {
        if (index >= 0 && index < static_cast<int>(UltraStore::StoreCategories().size())) c.categories.push_back(UltraStore::StoreCategories()[index]);
    }
    c.tags = SplitWords(fTags_->GetText());
    c.visibility = UltraStore::ParseVisibility(kVisibilities[std::max(0, fVisibility_->GetSelectedIndex())]);
    c.lifecycle = UltraStore::ParseLifecycle(kLifecycles[std::max(0, fLifecycle_->GetSelectedIndex())]);
    c.successor = Trim(fSuccessor_->GetText());
    c.ageRating = kAges[std::max(0, fAgeRating_->GetSelectedIndex())];
    c.requiresAccount = fRequiresAccount_->IsChecked();
    c.featured = fFeatured_->IsChecked();
    c.pricing.model = UltraStore::ParsePricingModel(kPricing[std::max(0, fPricingModel_->GetSelectedIndex())]);
    c.pricing.price = Trim(fPrice_->GetText());
    c.pricing.currency = Trim(fCurrency_->GetText());
    const std::string trial = Trim(fTrialDays_->GetText());
    c.pricing.trialDays = trial.empty() ? 0 : std::atoi(trial.c_str());   // locale-ok: digits only
    c.updates.period = UltraStore::ParseUpdatePeriod(kPeriods[std::max(0, fUpdatePeriod_->GetSelectedIndex())]);
    c.updates.until = Trim(fUntil_->GetText());
    c.sourceLanguage = Trim(fSourceLanguage_->GetText());
    if (const std::string icon = Trim(fIcon_->GetText()); icon != c.icon) {
        c.icon = icon;
        c.iconVariants.clear();   // the variants were made from the picture named before
    }
    // The texts of the language shown.
    const std::string summary = fSummary_->GetText(), description = fDescription_->GetText();
    const bool hadSummary = c.summary.byLanguage.count(formLanguage_) > 0;
    const bool hadDescription = c.description.byLanguage.count(formLanguage_) > 0;
    if (!summary.empty()) {
        if (!hadSummary || c.summary.byLanguage[formLanguage_] != summary) {
            c.summary.byLanguage[formLanguage_] = summary;
            c.machineTranslated.erase(std::remove(c.machineTranslated.begin(), c.machineTranslated.end(), formLanguage_), c.machineTranslated.end());
        }
    } else if (hadSummary) c.summary.byLanguage.erase(formLanguage_);
    if (!description.empty()) {
        if (!hadDescription || c.description.byLanguage[formLanguage_] != description) {
            c.description.byLanguage[formLanguage_] = description;
            c.machineTranslated.erase(std::remove(c.machineTranslated.begin(), c.machineTranslated.end(), formLanguage_), c.machineTranslated.end());
        }
    } else if (hadDescription) c.description.byLanguage.erase(formLanguage_);
    if (c.sourceLanguage.empty() && !c.summary.byLanguage.empty()) c.sourceLanguage = c.summary.byLanguage.begin()->first;
    if (c.id.empty() || c.id.find('.') == std::string::npos) { error = Tr("The id is reverse DNS, such as org.ultraos.myapp"); return false; }
    if (c.name.empty()) { error = Tr("The app needs a name"); return false; }
    if (c.pricing.model != UltraStore::PricingModel::Free && (c.pricing.price.empty() || c.pricing.currency.empty())) {
        error = Tr("A paid app needs a price and a currency");
        return false;
    }
    if (c.updates.period == UltraStore::UpdatePeriod::Until && c.updates.until.empty()) { error = Tr("Free updates until needs a date"); return false; }
    return true;
}

// ===========================================================================
// Save, revert, delete
// ===========================================================================
void AdminWindow::SaveComponent() {
    std::string error;
    if (!ReadForm(error)) { SetStatus(error, true); return; }
    if (const auto r = db_.PutComponent(draft_); !r) { SetStatus(r.error.message + (r.error.detail.empty() ? "" : " (" + r.error.detail + ")"), true); return; }
    draftDirty_ = false;
    draftIsNew_ = false;
    const std::string id = draft_.id;
    ReloadComponents();
    ShowComponent(id);
    SetStatus(TrF("{0} saved", id));
}

void AdminWindow::RevertComponent() {
    if (draftIsNew_) { ShowPage("overview"); return; }
    ShowComponent(draft_.id);
    SetStatus(Tr("Reverted to what is stored"));
}

void AdminWindow::DeleteComponent() {
    const std::string id = draft_.id;
    UltraCanvasDialogManager::ShowConfirmation(TrF("Delete {0} from the database? Its package files stay in the package store.", id), TrF("Delete {0}", id),
        [this, id](bool confirmed) {
            if (!confirmed) return;
            if (const auto r = db_.RemoveComponent(id); !r) { SetStatus(r.error.message, true); return; }
            draftDirty_ = false;
            ReloadComponents();
            ShowPage("overview");
            SetStatus(TrF("{0} deleted", id));
        }, window_.get());
}

bool AdminWindow::EnsureDraftSaved(std::function<void()> then) {
    if (currentPage_ != kComponent || !draftDirty_) { then(); return true; }
    UltraCanvasDialogManager::ShowConfirmation(Tr("The form has unsaved changes. Leave and lose them?"), Tr("Unsaved changes"),
        [this, then](bool confirmed) {
            if (!confirmed) { sidebar_->Select(kItemPrefix + draft_.id, false); return; }
            draftDirty_ = false;
            then();
        }, window_.get());
    return false;
}

// ===========================================================================
// Pictures
// ===========================================================================
namespace {
// "Icon copied into the media store as media/x/icon.png, 6 sizes for the
// web store", with the import's warnings after it.
std::string MediaStatus(const std::string& format, const ImportedMedia& media) {
    std::string text = TrF(format, media.original);
    if (!media.variants.empty()) text += " - " + TrF("{0} web sizes", std::to_string(media.variants.size()));
    for (const std::string& w : media.warnings) text += "; " + w;
    return text;
}
} // namespace

void AdminWindow::PickIcon() {
    FileDialogOptions opts;
    opts.SetTitle(Tr("The app's icon")).AddFilter(Tr("Pictures"), "png;svg;jpg;jpeg;webp").SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const auto imported = ImportMediaVariants(path, store_.dir, draft_.id, MediaKind::Icon);
        if (!imported) { SetStatus(imported.error.message, true); return; }
        std::string error;
        ReadForm(error);
        draft_.icon = imported.Value().original;
        draft_.iconVariants = imported.Value().variants;
        fIcon_->SetText(draft_.icon);
        draftDirty_ = true;
        SetStatus(MediaStatus(Tr("Icon copied into the media store as {0}"), imported.Value()), !imported.Value().warnings.empty());
    });
}

void AdminWindow::PickScreenshot() {
    FileDialogOptions opts;
    opts.SetTitle(Tr("A screenshot")).AddFilter(Tr("Pictures"), "png;jpg;jpeg;webp").SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const auto imported = ImportMediaVariants(path, store_.dir, draft_.id, MediaKind::Screenshot);
        if (!imported) { SetStatus(imported.error.message, true); return; }
        std::string error;
        ReadForm(error);
        const ImportedMedia& media = imported.Value();
        draft_.screenshotVariants.resize(draft_.screenshots.size());
        const auto at = std::find(draft_.screenshots.begin(), draft_.screenshots.end(), media.original);
        if (at == draft_.screenshots.end()) {
            draft_.screenshots.push_back(media.original);
            draft_.screenshotVariants.push_back(media.variants);
        } else {
            draft_.screenshotVariants[static_cast<size_t>(at - draft_.screenshots.begin())] = media.variants;
        }
        FillForm();
        draftDirty_ = true;
        SetStatus(MediaStatus(Tr("Screenshot copied into the media store as {0}"), media), !media.warnings.empty());
    });
}

// ===========================================================================
// Releases and packages
// ===========================================================================
void AdminWindow::RebuildReleasesSection() {
    fReleases_->ClearChildren();
    if (draft_.releases.empty()) fReleases_->AddChild(Theme::Caption("uaC.noReleases", Tr("No release yet: the store lists nothing until one has a package.")));
    int n = 0;
    for (const Release& r : draft_.releases) {
        const std::string id = "uaC.rel" + std::to_string(n++);
        auto surface = Theme::Column(id, 4.0f);
        surface->SetBackgroundColor(Theme::kPageBackground);
        surface->SetBorders(1.0f, Theme::kBorder, 6.0f);
        surface->SetPadding(8.0f);
        std::string title = r.version + " · " + (r.channel.empty() ? "stable" : r.channel);
        if (!r.date.empty()) title += " · " + r.date;
        auto head = Theme::Row(id + ".head", Theme::kInnerGap);
        auto titleLabel = Theme::Text(id + ".title", title, 13.0f, FontWeight::Bold);
        titleLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        head->AddChild(titleLabel);
        auto addPackage = Theme::Button(id + ".addPackage", Tr("Add package"), 120);
        const std::string version = r.version;
        addPackage->onClick = [this, version]() {
            FileDialogOptions opts;
            opts.SetTitle(TrF("The build for {0}", version)).AddFilter(Tr("Packages"), "zip;7z;zst;gz;xz;tgz;AppImage;appimage").SetParentWindow(window_.get());
            UltraCanvasFileLoader::OpenFileDialog(opts, [this, version](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                if (FormatOf(path).empty()) { SetStatus(Tr("The store installs zip, 7z, tar.zst, tar.gz, tar.xz and AppImage files"), true); return; }
                packageFile_ = path;
                packageEditorVersion_ = version;
                pFile_->SetText(TrF("File: {0}  ({1})", path, FormatOf(path)));
                pEntry_->SetText("");
                if (const auto entries = ListPackageEntries(path)) {
                    pEntry_->SetText(SuggestEntry(entries.Value(), UltraStore::ParseOS(kOses[std::max(0, pOs_->GetSelectedIndex())])));
                }
                pAccess_->SetSelectedIndex(draft_.pricing.model == UltraStore::PricingModel::Free ? 0 : 1, false);
                releaseEditor_->SetVisible(false);
                packageEditor_->SetVisible(true);
                formBody_->InvalidateLayout();
                formBody_->RequestRedraw();
            });
        };
        head->AddChild(addPackage);
        auto removeRelease = Theme::Button(id + ".remove", Tr("Remove release"), 120, Theme::ButtonKind::Danger);
        removeRelease->onClick = [this, version]() { RemoveRelease(version); };
        head->AddChild(removeRelease);
        surface->AddChild(head);
        if (!r.depends.empty()) {
            std::string list;
            for (const auto& d : r.depends) list += " " + d.id + "@" + d.version;
            const std::string needs = TrF("Needs{0}", list);
            surface->AddChild(Theme::Caption(id + ".needs", needs));
        }
        const std::string notes = r.notes.Text(formLanguage_);
        if (!notes.empty()) surface->AddChild(Theme::Caption(id + ".notes", notes));
        if (r.packages.empty()) surface->AddChild(Theme::Caption(id + ".noPackages", Tr("No package yet.")));
        for (size_t i = 0; i < r.packages.size(); ++i) {
            const Package& p = r.packages[i];
            auto row = Theme::Row(id + ".p" + std::to_string(i), Theme::kInnerGap);
            std::string line = p.target.Describe() + " · " + p.format;
            if (p.size) line += " · " + TrF("{0} bytes", p.size);
            if (!p.minOs.empty()) line += " · " + TrF("min OS {0}", p.minOs);
            if (!p.entry.empty()) line += " · " + TrF("starts {0}", p.entry);
            line += " · " + p.sha256.substr(0, 12) + "…";
            if (const auto file = db_.GetPackageFile(p.sha256)) line += " · " + file.Value().access;
            else if (p.url.empty()) line += " · " + Tr("no file!");
            auto text = Theme::Caption(id + ".p" + std::to_string(i) + ".text", line);
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(text);
            auto remove = Theme::Button(id + ".p" + std::to_string(i) + ".remove", Tr("Remove"), 72);
            remove->onClick = [this, version, i]() { RemovePackage(version, i); };
            row->AddChild(remove);
            surface->AddChild(row);
        }
        fReleases_->AddChild(surface);
    }
    fReleases_->InvalidateLayout();
}

void AdminWindow::AddRelease() {
    std::string error;
    ReadForm(error);
    Release r;
    r.version = Trim(rVersion_->GetText());
    if (r.version.empty()) { SetStatus(Tr("A release needs a version"), true); return; }
    for (const Release& existing : draft_.releases) if (existing.version == r.version) { SetStatus(TrF("Release {0} exists", r.version), true); return; }
    r.channel = rChannel_->GetSelectedIndex() == 1 ? "beta" : "stable";
    r.date = Trim(rDate_->GetText());
    for (const std::string& dep : SplitWords(rDepends_->GetText())) {
        const size_t at = dep.find('@');
        if (at == std::string::npos) { SetStatus(TrF("Needs wants id@version, not {0}", dep), true); return; }
        r.depends.push_back({dep.substr(0, at), dep.substr(at + 1)});
    }
    const std::string notes = Trim(rNotes_->GetText());
    if (!notes.empty()) r.notes.byLanguage[draft_.sourceLanguage.empty() ? "en" : draft_.sourceLanguage] = notes;
    draft_.releases.push_back(r);
    std::sort(draft_.releases.begin(), draft_.releases.end(), [](const Release& a, const Release& b) { return UltraStore::VersionLess(b.version, a.version); });
    rNotes_->SetText("");
    rDepends_->SetText("");
    draftDirty_ = true;
    FillForm();
    draftDirty_ = true;
    SetStatus(TrF("Release {0} added to the draft: add its packages, then Save", r.version));
}

void AdminWindow::RemoveRelease(const std::string& version) {
    std::string error;
    ReadForm(error);
    draft_.releases.erase(std::remove_if(draft_.releases.begin(), draft_.releases.end(), [&](const Release& r) { return r.version == version; }), draft_.releases.end());
    FillForm();
    draftDirty_ = true;
}

void AdminWindow::AddPackage(const std::string& version) {
    std::string error;
    ReadForm(error);
    Release* release = nullptr;
    for (Release& r : draft_.releases) if (r.version == version) release = &r;
    if (!release || packageFile_.empty()) { SetStatus(Tr("Choose the release's build first"), true); return; }
    Package p;
    p.target.os = UltraStore::ParseOS(kOses[std::max(0, pOs_->GetSelectedIndex())]);
    p.target.arch = UltraStore::ParseArch(kArchs[std::max(0, pArch_->GetSelectedIndex())]);
    p.target.variants = SplitWords(pVariants_->GetText());
    p.minOs = Trim(pMinOs_->GetText());
    p.entry = Trim(pEntry_->GetText());
    const auto imported = ImportPackage(packageFile_, store_.PackagesDir());
    if (!imported) { SetStatus(imported.error.message, true); return; }
    p.format = imported.Value().format;
    p.sha256 = imported.Value().sha256;
    p.size = imported.Value().size;
    PackageFile file;
    file.sha256 = p.sha256;
    file.file = imported.Value().file;
    file.size = p.size;
    file.format = p.format;
    file.access = pAccess_->GetSelectedIndex() == 1 ? "restricted" : "public";
    if (const auto r = db_.PutPackageFile(file); !r) { SetStatus(r.error.message, true); return; }
    release->packages.erase(std::remove_if(release->packages.begin(), release->packages.end(), [&](const Package& q) {
        return q.target.os == p.target.os && q.target.arch == p.target.arch && q.target.variants == p.target.variants;
    }), release->packages.end());
    release->packages.push_back(p);
    packageFile_.clear();
    FillForm();
    draftDirty_ = true;
    SetStatus(TrF("Package {0} added to {1} ({2} bytes, in the package store): Save to keep it", p.target.Describe(), version, p.size));
}

void AdminWindow::RemovePackage(const std::string& version, size_t index) {
    std::string error;
    ReadForm(error);
    for (Release& r : draft_.releases) {
        if (r.version != version || index >= r.packages.size()) continue;
        r.packages.erase(r.packages.begin() + static_cast<long>(index));
    }
    FillForm();
    draftDirty_ = true;
}

// ===========================================================================
// Translation
// ===========================================================================
void AdminWindow::TranslateDraft() {
    std::string error;
    if (!ReadForm(error)) { SetStatus(error, true); return; }
    std::vector<std::string> languages = SplitWords(db_.Setting("texts.languages"));
    if (languages.empty()) languages = OfficialLanguageCodes();
    std::string why;
    std::shared_ptr<ITextTranslator> translator = CreateUltraAITranslator(db_.Setting("ai.provider"), db_.Setting("ai.base_url"), db_.Setting("ai.model"), &why);
    if (!translator) { SetStatus(TrF("No translator: {0}", why), true); return; }
    Component work = draft_;
    RunJob(Tr("Translating with UltraAI"), [work, translator, languages]() mutable {
        JobOutcome out;
        const auto report = TranslateComponent(work, *translator, languages);
        if (!report) { out.ok = false; out.message = report.error.message; return out; }
        out.message = TrF("{0} texts translated into {1} language(s)", report.Value().texts, report.Value().languages.size());
        for (const std::string& e : report.Value().errors) out.lines.push_back(e);
        out.lines.push_back(UltraStore::SerializeComponent(work));
        return out;
    }, [this](const JobOutcome& out) {
        if (!out.ok || out.lines.empty()) return;
        const auto parsed = UltraStore::ParseComponent(out.lines.back(), draft_.kind, false);
        if (!parsed) { SetStatus(Tr("The translated texts did not read back"), true); return; }
        draft_ = parsed.Value();
        FillForm();
        draftDirty_ = true;
        std::string message = TrF("{0} - Save to keep them", out.message);
        for (size_t i = 0; i + 1 < out.lines.size(); ++i) message += "; " + out.lines[i];
        SetStatus(message, out.lines.size() > 1);
    });
}

} // namespace UltraStoreAdmin::Ui
