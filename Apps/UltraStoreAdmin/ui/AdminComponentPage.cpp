// Apps/UltraStoreAdmin/ui/AdminComponentPage.cpp
// The form for one app or component: every setting of the catalog's table,
// the texts per language, the pictures, the releases with their packages.
// The form edits a draft; Save writes it whole (AdminDatabase::PutComponent,
// which reads it back the way the store does before storing it).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminWindow.h"

#include "AdminTheme.h"
#include "Translation.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"

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
    formTitle_ = Theme::Heading("uaC.title", "App");
    formTitle_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header->AddChild(formTitle_);
    auto save = Theme::Button("uaC.save", "Save", 90, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveComponent(); };
    header->AddChild(save);
    auto revert = Theme::Button("uaC.revert", "Revert", 90);
    revert->onClick = [this]() { RevertComponent(); };
    header->AddChild(revert);
    auto remove = Theme::Button("uaC.delete", "Delete", 90, Theme::ButtonKind::Danger);
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
    identity->AddChild(Theme::Text("uaC.identity.title", "Identity", 14.0f, FontWeight::Bold));
    fId_ = input("uaC.id");
    identity->AddChild(Theme::Field("uaC.idRow", "Id (reverse DNS)", fId_));
    fKind_ = Choice("uaC.kind", kKinds, {"App", "Runtime", "Library"});
    identity->AddChild(Theme::Field("uaC.kindRow", "Kind", fKind_));
    fName_ = input("uaC.name");
    identity->AddChild(Theme::Field("uaC.nameRow", "Name", fName_));
    fPublisher_ = Choice("uaC.publisher", {""}, {"(none)"}, 300);
    identity->AddChild(Theme::Field("uaC.publisherRow", "Publisher", fPublisher_));
    fLicense_ = input("uaC.license", 200);
    identity->AddChild(Theme::Field("uaC.licenseRow", "License", fLicense_));
    fHomepage_ = input("uaC.homepage");
    identity->AddChild(Theme::Field("uaC.homepageRow", "Homepage", fHomepage_));
    fSupport_ = input("uaC.support");
    identity->AddChild(Theme::Field("uaC.supportRow", "Support URL", fSupport_));
    fPrivacy_ = input("uaC.privacy");
    identity->AddChild(Theme::Field("uaC.privacyRow", "Privacy URL", fPrivacy_));
    fOpenSource_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaC.oss", 0, 0, 300, Theme::kControlHeight, "Open source", false));
    identity->AddChild(fOpenSource_);
    fSource_ = input("uaC.source");
    identity->AddChild(Theme::Field("uaC.sourceRow", "Source URL", fSource_));
    fCategories_ = input("uaC.categories");
    fCategories_->SetPlaceholder("Office Tools (separated by spaces)");
    identity->AddChild(Theme::Field("uaC.categoriesRow", "Categories", fCategories_));
    fTags_ = input("uaC.tags");
    fTags_->SetPlaceholder("free-form words for the search");
    identity->AddChild(Theme::Field("uaC.tagsRow", "Tags", fTags_));
    body->AddChild(identity);

    // ----- the admin backend's settings -----
    auto settings = Theme::Surface("uaC.settings", Theme::kInnerGap);
    settings->AddChild(Theme::Text("uaC.settings.title", "Store settings", 14.0f, FontWeight::Bold));
    fVisibility_ = Choice("uaC.visibility", kVisibilities, {"Listed", "Unlisted (by id or link)", "Hidden (updates only)"}, 260);
    settings->AddChild(Theme::Field("uaC.visibilityRow", "Visibility", fVisibility_));
    fLifecycle_ = Choice("uaC.lifecycle", kLifecycles, {"Active", "Deprecated (names a successor)", "Withdrawn (no new installs)"}, 260);
    settings->AddChild(Theme::Field("uaC.lifecycleRow", "Lifecycle", fLifecycle_));
    fSuccessor_ = input("uaC.successor");
    fSuccessor_->SetPlaceholder("the id to install instead, when deprecated or withdrawn");
    settings->AddChild(Theme::Field("uaC.successorRow", "Successor", fSuccessor_));
    fAgeRating_ = Choice("uaC.age", kAges, {"Everyone", "3+", "7+", "12+", "16+", "18+"}, 160);
    settings->AddChild(Theme::Field("uaC.ageRow", "Age rating", fAgeRating_));
    fRequiresAccount_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaC.account", 0, 0, 420, Theme::kControlHeight, "Needs an account of its own to be useful", false));
    settings->AddChild(fRequiresAccount_);
    fPricingModel_ = Choice("uaC.pricing", kPricing, {"Free", "One-time purchase", "Monthly fee", "Yearly fee"}, 200);
    settings->AddChild(Theme::Field("uaC.pricingRow", "Monetary", fPricingModel_));
    auto priceRow = Theme::Row("uaC.priceRow", Theme::kInnerGap);
    fPrice_ = input("uaC.price", 100);
    fPrice_->SetPlaceholder("29.90");
    priceRow->AddChild(fPrice_);
    fCurrency_ = input("uaC.currency", 70);
    fCurrency_->SetPlaceholder("EUR");
    priceRow->AddChild(fCurrency_);
    priceRow->AddChild(Theme::Caption("uaC.trialLabel", "trial days"));
    fTrialDays_ = input("uaC.trial", 60);
    fTrialDays_->SetPlaceholder("0");
    priceRow->AddChild(fTrialDays_);
    settings->AddChild(Theme::Field("uaC.priceField", "Price, currency, trial", priceRow));
    fUpdatePeriod_ = Choice("uaC.period", kPeriods, {"Always free", "Never (the bought version only)", "Until a date"}, 260);
    settings->AddChild(Theme::Field("uaC.periodRow", "Free update period", fUpdatePeriod_));
    fUntil_ = input("uaC.until", 140);
    fUntil_->SetPlaceholder("2027-12-31");
    settings->AddChild(Theme::Field("uaC.untilRow", "Free updates until", fUntil_));
    body->AddChild(settings);

    // ----- texts -----
    auto texts = Theme::Surface("uaC.texts", Theme::kInnerGap);
    texts->AddChild(Theme::Text("uaC.texts.title", "Texts", 14.0f, FontWeight::Bold));
    fSourceLanguage_ = input("uaC.sourceLanguage", 80);
    fSourceLanguage_->SetPlaceholder("en");
    texts->AddChild(Theme::Field("uaC.sourceLanguageRow", "Written in (source language)", fSourceLanguage_));
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
    auto addLanguage = Theme::Button("uaC.addLanguage", "Add language", 110);
    addLanguage->onClick = [this]() {
        const std::string code = Trim(fNewLanguage_->GetText());
        if (code.size() < 2) { SetStatus("A language is a code such as de", true); return; }
        std::string error;
        ReadForm(error);
        formLanguage_ = code;
        fNewLanguage_->SetText("");
        RebuildTextsSection();
    };
    langRow->AddChild(addLanguage);
    auto translate = Theme::Button("uaC.translate", "Translate the missing languages", 220);
    translate->SetTooltip("UltraAI fills every language of the settings the texts lack, from the source language");
    translate->onClick = [this]() { TranslateDraft(); };
    langRow->AddChild(translate);
    texts->AddChild(Theme::Field("uaC.langField", "Language shown", langRow));
    fMachineNote_ = Theme::Caption("uaC.machineNote", "");
    texts->AddChild(fMachineNote_);
    fSummary_ = input("uaC.summary", 600);
    texts->AddChild(Theme::Field("uaC.summaryRow", "Summary (one line)", fSummary_));
    fDescription_ = Theme::Pin(std::make_shared<UltraCanvasTextArea>("uaC.description", 0.0f, 140.0f));
    fDescription_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(140));
    fDescription_->SetWordWrap(true);
    fDescription_->SetShowLineNumbers(false);
    fDescription_->SetHighlightSyntax(false);
    fDescription_->SetFontSize(12.0f);
    fDescription_->SetBackgroundColor(Theme::kPageBackground);
    fDescription_->SetBorders(1.0f, Theme::kBorder, 4.0f);
    texts->AddChild(Theme::Text("uaC.descriptionLabel", "Description", 12.0f));
    texts->AddChild(fDescription_);
    fTexts_ = texts;
    body->AddChild(texts);

    // ----- pictures -----
    auto media = Theme::Surface("uaC.media", Theme::kInnerGap);
    media->AddChild(Theme::Text("uaC.media.title", "Pictures", 14.0f, FontWeight::Bold));
    auto iconRow = Theme::Row("uaC.iconRow", Theme::kInnerGap);
    fIcon_ = input("uaC.icon");
    fIcon_->SetPlaceholder("media/<id>/icon.png, or a URL");
    iconRow->AddChild(fIcon_);
    auto pickIcon = Theme::Button("uaC.pickIcon", "Choose", 90);
    pickIcon->onClick = [this]() { PickIcon(); };
    iconRow->AddChild(pickIcon);
    media->AddChild(Theme::Field("uaC.iconField", "Icon", iconRow));
    fScreenshots_ = Theme::Column("uaC.screenshots", 4.0f);
    media->AddChild(Theme::Field("uaC.screenshotsField", "Screenshots", fScreenshots_, true));
    auto addShot = Theme::Button("uaC.addShot", "Add screenshot", 140);
    addShot->onClick = [this]() { PickScreenshot(); };
    auto shotRow = Theme::Row("uaC.shotRow", Theme::kInnerGap);
    shotRow->AddChild(addShot);
    media->AddChild(shotRow);
    body->AddChild(media);

    // ----- releases -----
    auto releases = Theme::Surface("uaC.releases", Theme::kInnerGap);
    releases->AddChild(Theme::Text("uaC.releases.title", "Releases and packages", 14.0f, FontWeight::Bold));
    releases->AddChild(Theme::Caption("uaC.releases.hint",
        "A release per version, newest first, with the exact components it was built against; a package per target "
        "with the build's file, which goes into the package store under its hash."));
    fReleases_ = Theme::Column("uaC.releaseList", Theme::kInnerGap);
    releases->AddChild(fReleases_);
    // The add-release editor.
    releaseEditor_ = Theme::Column("uaC.releaseEditor", Theme::kInnerGap);
    releaseEditor_->SetBackgroundColor(Theme::kPageBackground);
    releaseEditor_->SetBorders(1.0f, Theme::kBorder, 6.0f);
    releaseEditor_->SetPadding(8.0f);
    rVersion_ = input("uaC.rVersion", 140);
    rVersion_->SetPlaceholder("1.2.0");
    releaseEditor_->AddChild(Theme::Field("uaC.rVersionRow", "Version", rVersion_));
    rChannel_ = Choice("uaC.rChannel", {"stable", "beta"}, {"Stable", "Beta"}, 140);
    releaseEditor_->AddChild(Theme::Field("uaC.rChannelRow", "Channel", rChannel_));
    rDate_ = input("uaC.rDate", 140);
    rDate_->SetPlaceholder("2026-10-27");
    releaseEditor_->AddChild(Theme::Field("uaC.rDateRow", "Date", rDate_));
    rDepends_ = input("uaC.rDepends");
    rDepends_->SetPlaceholder("org.ultraos.runtime@0.9.410 (separated by spaces)");
    releaseEditor_->AddChild(Theme::Field("uaC.rDependsRow", "Needs (id@version)", rDepends_));
    rNotes_ = input("uaC.rNotes", 600);
    rNotes_->SetPlaceholder("what changed, in the source language");
    releaseEditor_->AddChild(Theme::Field("uaC.rNotesRow", "Release notes", rNotes_));
    auto rButtons = Theme::Row("uaC.rButtons", Theme::kInnerGap);
    auto rAdd = Theme::Button("uaC.rAdd", "Add the release", 130, Theme::ButtonKind::Primary);
    rAdd->onClick = [this]() { AddRelease(); };
    rButtons->AddChild(rAdd);
    auto rCancel = Theme::Button("uaC.rCancel", "Cancel", 80);
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
    packageEditor_->AddChild(Theme::Field("uaC.pOsRow", "Operating system", pOs_));
    pArch_ = Choice("uaC.pArch", kArchs, {}, 160);
    packageEditor_->AddChild(Theme::Field("uaC.pArchRow", "Processor", pArch_));
    pVariants_ = input("uaC.pVariants");
    pVariants_->SetPlaceholder("glibc-2.31 rpi (separated by spaces)");
    packageEditor_->AddChild(Theme::Field("uaC.pVariantsRow", "Variants", pVariants_));
    pMinOs_ = input("uaC.pMinOs", 160);
    pMinOs_->SetPlaceholder("14.0 / 10.0.17763");
    packageEditor_->AddChild(Theme::Field("uaC.pMinOsRow", "Minimum OS version", pMinOs_));
    pEntry_ = input("uaC.pEntry");
    pEntry_->SetPlaceholder("bin/app (suggested from the archive)");
    packageEditor_->AddChild(Theme::Field("uaC.pEntryRow", "Entry to start", pEntry_));
    pAccess_ = Choice("uaC.pAccess", {"public", "restricted"}, {"Public (free)", "Restricted (buyers only)"}, 220);
    packageEditor_->AddChild(Theme::Field("uaC.pAccessRow", "Access", pAccess_));
    auto pButtons = Theme::Row("uaC.pButtons", Theme::kInnerGap);
    auto pAdd = Theme::Button("uaC.pAdd", "Add the package", 130, Theme::ButtonKind::Primary);
    pAdd->onClick = [this]() { AddPackage(packageEditorVersion_); };
    pButtons->AddChild(pAdd);
    auto pCancel = Theme::Button("uaC.pCancel", "Cancel", 80);
    pCancel->onClick = [this]() { packageEditor_->SetVisible(false); formBody_->InvalidateLayout(); };
    pButtons->AddChild(pCancel);
    packageEditor_->AddChild(pButtons);
    packageEditor_->SetVisible(false);
    releases->AddChild(packageEditor_);
    auto newRelease = Theme::Button("uaC.newRelease", "Add release", 120);
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
                     fSuccessor_.get(), fCategories_.get(), fTags_.get(), fSourceLanguage_.get(), fPrice_.get(), fCurrency_.get(),
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
    if (id.empty() || id.find('.') == std::string::npos) { SetStatus("The id is reverse DNS, such as org.ultraos.myapp", true); return; }
    for (const Component& c : components_) if (c.id == id) { SetStatus(id + " exists already", true); return; }
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
    SetStatus(id + " created: fill in the form and add a release");
}

void AdminWindow::FillForm() {
    const Component& c = draft_;
    formTitle_->SetText((c.kind == ComponentKind::App ? "App " : c.kind == ComponentKind::Runtime ? "Runtime " : "Library ") + c.id);
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
    fCategories_->SetText(JoinWords(c.categories));
    fTags_->SetText(JoinWords(c.tags));
    fVisibility_->SetSelectedIndex(IndexOfValue(kVisibilities, UltraStore::VisibilityName(c.visibility)), false);
    fLifecycle_->SetSelectedIndex(IndexOfValue(kLifecycles, UltraStore::LifecycleName(c.lifecycle)), false);
    fSuccessor_->SetText(c.successor);
    fAgeRating_->SetSelectedIndex(IndexOfValue(kAges, c.ageRating), false);
    fRequiresAccount_->SetChecked(c.requiresAccount);
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
        auto remove = Theme::Button("uaC.shot" + std::to_string(n) + ".remove", "Remove", 72);
        remove->onClick = [this, shot]() {
            std::string error;
            ReadForm(error);
            draft_.screenshots.erase(std::remove(draft_.screenshots.begin(), draft_.screenshots.end(), shot), draft_.screenshots.end());
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
        fLanguage_->AddItem(LanguageLabel(languages[i]) + (machine ? " (machine)" : ""), languages[i]);
        if (languages[i] == formLanguage_) selected = static_cast<int>(i);
    }
    fLanguage_->SetSelectedIndex(selected, false);
    const bool machine = std::find(draft_.machineTranslated.begin(), draft_.machineTranslated.end(), formLanguage_) != draft_.machineTranslated.end();
    fMachineNote_->SetText(machine ? "This language was translated automatically; editing it here makes it a human text."
                                   : (formLanguage_ == (draft_.sourceLanguage.empty() ? "en" : draft_.sourceLanguage) ? "The source language." : ""));
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
    c.categories = SplitWords(fCategories_->GetText());
    c.tags = SplitWords(fTags_->GetText());
    c.visibility = UltraStore::ParseVisibility(kVisibilities[std::max(0, fVisibility_->GetSelectedIndex())]);
    c.lifecycle = UltraStore::ParseLifecycle(kLifecycles[std::max(0, fLifecycle_->GetSelectedIndex())]);
    c.successor = Trim(fSuccessor_->GetText());
    c.ageRating = kAges[std::max(0, fAgeRating_->GetSelectedIndex())];
    c.requiresAccount = fRequiresAccount_->IsChecked();
    c.pricing.model = UltraStore::ParsePricingModel(kPricing[std::max(0, fPricingModel_->GetSelectedIndex())]);
    c.pricing.price = Trim(fPrice_->GetText());
    c.pricing.currency = Trim(fCurrency_->GetText());
    const std::string trial = Trim(fTrialDays_->GetText());
    c.pricing.trialDays = trial.empty() ? 0 : std::atoi(trial.c_str());   // locale-ok: digits only
    c.updates.period = UltraStore::ParseUpdatePeriod(kPeriods[std::max(0, fUpdatePeriod_->GetSelectedIndex())]);
    c.updates.until = Trim(fUntil_->GetText());
    c.sourceLanguage = Trim(fSourceLanguage_->GetText());
    c.icon = Trim(fIcon_->GetText());
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
    if (c.id.empty() || c.id.find('.') == std::string::npos) { error = "The id is reverse DNS, such as org.ultraos.myapp"; return false; }
    if (c.name.empty()) { error = "The app needs a name"; return false; }
    if (c.pricing.model != UltraStore::PricingModel::Free && (c.pricing.price.empty() || c.pricing.currency.empty())) {
        error = "A paid app needs a price and a currency";
        return false;
    }
    if (c.updates.period == UltraStore::UpdatePeriod::Until && c.updates.until.empty()) { error = "Free updates until needs a date"; return false; }
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
    SetStatus(id + " saved");
}

void AdminWindow::RevertComponent() {
    if (draftIsNew_) { ShowPage("overview"); return; }
    ShowComponent(draft_.id);
    SetStatus("Reverted to what is stored");
}

void AdminWindow::DeleteComponent() {
    const std::string id = draft_.id;
    UltraCanvasDialogManager::ShowConfirmation("Delete " + id + " from the database? Its package files stay in the package store.", "Delete " + id,
        [this, id](bool confirmed) {
            if (!confirmed) return;
            if (const auto r = db_.RemoveComponent(id); !r) { SetStatus(r.error.message, true); return; }
            draftDirty_ = false;
            ReloadComponents();
            ShowPage("overview");
            SetStatus(id + " deleted");
        }, window_.get());
}

bool AdminWindow::EnsureDraftSaved(std::function<void()> then) {
    if (currentPage_ != kComponent || !draftDirty_) { then(); return true; }
    UltraCanvasDialogManager::ShowConfirmation("The form has unsaved changes. Leave and lose them?", "Unsaved changes",
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
void AdminWindow::PickIcon() {
    FileDialogOptions opts;
    opts.SetTitle("The app's icon").AddFilter("Pictures", "png;svg;jpg;jpeg;webp").SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const auto imported = ImportMedia(path, store_.dir, draft_.id);
        if (!imported) { SetStatus(imported.error.message, true); return; }
        fIcon_->SetText(imported.Value());
        draftDirty_ = true;
        SetStatus("Icon copied into the media store as " + imported.Value());
    });
}

void AdminWindow::PickScreenshot() {
    FileDialogOptions opts;
    opts.SetTitle("A screenshot").AddFilter("Pictures", "png;jpg;jpeg;webp").SetParentWindow(window_.get());
    UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        const auto imported = ImportMedia(path, store_.dir, draft_.id);
        if (!imported) { SetStatus(imported.error.message, true); return; }
        std::string error;
        ReadForm(error);
        if (std::find(draft_.screenshots.begin(), draft_.screenshots.end(), imported.Value()) == draft_.screenshots.end()) draft_.screenshots.push_back(imported.Value());
        FillForm();
        draftDirty_ = true;
        SetStatus("Screenshot copied into the media store as " + imported.Value());
    });
}

// ===========================================================================
// Releases and packages
// ===========================================================================
void AdminWindow::RebuildReleasesSection() {
    fReleases_->ClearChildren();
    if (draft_.releases.empty()) fReleases_->AddChild(Theme::Caption("uaC.noReleases", "No release yet: the store lists nothing until one has a package."));
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
        auto addPackage = Theme::Button(id + ".addPackage", "Add package", 120);
        const std::string version = r.version;
        addPackage->onClick = [this, version]() {
            FileDialogOptions opts;
            opts.SetTitle("The build for " + version).AddFilter("Packages", "zip;7z;zst;gz;xz;tgz;AppImage;appimage").SetParentWindow(window_.get());
            UltraCanvasFileLoader::OpenFileDialog(opts, [this, version](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                if (FormatOf(path).empty()) { SetStatus("The store installs zip, 7z, tar.zst, tar.gz, tar.xz and AppImage files", true); return; }
                packageFile_ = path;
                packageEditorVersion_ = version;
                pFile_->SetText("File: " + path + "  (" + FormatOf(path) + ")");
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
        auto removeRelease = Theme::Button(id + ".remove", "Remove release", 120, Theme::ButtonKind::Danger);
        removeRelease->onClick = [this, version]() { RemoveRelease(version); };
        head->AddChild(removeRelease);
        surface->AddChild(head);
        if (!r.depends.empty()) {
            std::string needs = "Needs";
            for (const auto& d : r.depends) needs += " " + d.id + "@" + d.version;
            surface->AddChild(Theme::Caption(id + ".needs", needs));
        }
        const std::string notes = r.notes.Text(formLanguage_);
        if (!notes.empty()) surface->AddChild(Theme::Caption(id + ".notes", notes));
        if (r.packages.empty()) surface->AddChild(Theme::Caption(id + ".noPackages", "No package yet."));
        for (size_t i = 0; i < r.packages.size(); ++i) {
            const Package& p = r.packages[i];
            auto row = Theme::Row(id + ".p" + std::to_string(i), Theme::kInnerGap);
            std::string line = p.target.Describe() + " · " + p.format;
            if (p.size) line += " · " + std::to_string(p.size) + " bytes";
            if (!p.minOs.empty()) line += " · min OS " + p.minOs;
            if (!p.entry.empty()) line += " · starts " + p.entry;
            line += " · " + p.sha256.substr(0, 12) + "…";
            if (const auto file = db_.GetPackageFile(p.sha256)) line += " · " + file.Value().access;
            else if (p.url.empty()) line += " · no file!";
            auto text = Theme::Caption(id + ".p" + std::to_string(i) + ".text", line);
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(text);
            auto remove = Theme::Button(id + ".p" + std::to_string(i) + ".remove", "Remove", 72);
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
    if (r.version.empty()) { SetStatus("A release needs a version", true); return; }
    for (const Release& existing : draft_.releases) if (existing.version == r.version) { SetStatus("Release " + r.version + " exists", true); return; }
    r.channel = rChannel_->GetSelectedIndex() == 1 ? "beta" : "stable";
    r.date = Trim(rDate_->GetText());
    for (const std::string& dep : SplitWords(rDepends_->GetText())) {
        const size_t at = dep.find('@');
        if (at == std::string::npos) { SetStatus("Needs wants id@version, not " + dep, true); return; }
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
    SetStatus("Release " + r.version + " added to the draft: add its packages, then Save");
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
    if (!release || packageFile_.empty()) { SetStatus("Choose the release's build first", true); return; }
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
    SetStatus("Package " + p.target.Describe() + " added to " + version + " (" + std::to_string(p.size) + " bytes, in the package store): Save to keep it");
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
    if (!translator) { SetStatus("No translator: " + why, true); return; }
    Component work = draft_;
    RunJob("Translating with UltraAI", [work, translator, languages]() mutable {
        JobOutcome out;
        const auto report = TranslateComponent(work, *translator, languages);
        if (!report) { out.ok = false; out.message = report.error.message; return out; }
        out.message = std::to_string(report.Value().texts) + " texts translated into " + std::to_string(report.Value().languages.size()) + " language(s)";
        for (const std::string& e : report.Value().errors) out.lines.push_back(e);
        out.lines.push_back(UltraStore::SerializeComponent(work));
        return out;
    }, [this](const JobOutcome& out) {
        if (!out.ok || out.lines.empty()) return;
        const auto parsed = UltraStore::ParseComponent(out.lines.back(), draft_.kind, false);
        if (!parsed) { SetStatus("The translated texts did not read back", true); return; }
        draft_ = parsed.Value();
        FillForm();
        draftDirty_ = true;
        std::string message = out.message + " - Save to keep them";
        for (size_t i = 0; i + 1 < out.lines.size(); ++i) message += "; " + out.lines[i];
        SetStatus(message, out.lines.size() > 1);
    });
}

} // namespace UltraStoreAdmin::Ui
