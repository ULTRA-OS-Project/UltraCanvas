// Apps/UltraStoreAdmin/ui/AdminWindow.h
// The admin backend's window: a sidebar with the store's pages (Overview,
// Publish, Keys, Settings) and one entry per app and per component; on the
// right the page, for an app or component the form with every setting the
// catalog carries, its texts per language, its releases and packages. Every
// operation goes through UltraStoreAdminCore - the same AdminDatabase,
// KeyVault, PackageStore, CatalogPublisher and Translation the commands
// use - so the window and the command line never disagree. The slow ones
// (a new key's Argon2id seal, a publish, a translation) run on a worker
// thread and report through UltraCanvasApplication::PostToUIThread.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminDatabase.h"
#include "PackageStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSidebarNavigation.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace UltraStoreAdmin::Ui {

class AdminWindow {
public:
    AdminWindow(UltraCanvas::UltraCanvasApplication& app, std::string storeDirUtf8);
    ~AdminWindow();
    AdminWindow(const AdminWindow&) = delete;
    AdminWindow& operator=(const AdminWindow&) = delete;

    bool Create();
    void Show();

private:
    using Container = UltraCanvas::UltraCanvasContainer;
    using Label = UltraCanvas::UltraCanvasLabel;
    using Button = UltraCanvas::UltraCanvasButton;
    using TextInput = UltraCanvas::UltraCanvasTextInput;
    using TextArea = UltraCanvas::UltraCanvasTextArea;
    using Dropdown = UltraCanvas::UltraCanvasDropdown;
    using Checkbox = UltraCanvas::UltraCanvasCheckbox;

    // ----- shell (AdminWindow.cpp) -----
    void BuildShell();
    std::shared_ptr<Container> BuildPage(const std::string& id, const std::string& title,
                                         std::shared_ptr<Container>& header, std::shared_ptr<Container>& body);
    void ShowPage(const std::string& id);
    void LayoutForSize(float width, float height);
    void SetStatus(const std::string& text, bool isError = false);
    void Alert(const std::string& message, bool isError = true);
    bool OnClosing();
    void RebuildSidebar();                 // the apps and components as items
    void ReloadComponents();               // from the database

    // ----- the store pages -----
    void BuildOverviewPage();
    void RefreshOverview();
    void BuildSettingsPage();
    void LoadSettingsPage();
    void SaveSettingsPage();
    void BuildKeysPage();
    void RefreshKeysPage();
    void BuildPublishPage();
    void CheckCatalog();
    void PublishNow();

    // ----- the component form (AdminComponentPage.cpp) -----
    void BuildComponentPage();
    void ShowComponent(const std::string& id);      // loads the draft and fills the form
    void NewComponent(UltraStore::ComponentKind kind);
    void FillForm();                                  // draft -> controls
    bool ReadForm(std::string& error);                // controls -> draft
    void SaveComponent();
    void RevertComponent();
    void DeleteComponent();
    void RebuildTextsSection();
    void RebuildReleasesSection();
    void PickIcon();
    void PickScreenshot();
    void AddRelease();
    void RemoveRelease(const std::string& version);
    void AddPackage(const std::string& version);
    void RemovePackage(const std::string& version, size_t index);
    void TranslateDraft();
    bool EnsureDraftSaved(std::function<void()> then);   // asks when the draft has unsaved edits

    // ----- jobs -----
    struct JobOutcome {
        bool ok = true;
        std::string message;
        std::vector<std::string> lines;   // warnings, a report
    };
    void RunJob(const std::string& title, std::function<JobOutcome()> work, std::function<void(const JobOutcome&)> done);
    bool Busy() const { return busy_; }

    // ----- members -----
    UltraCanvas::UltraCanvasApplication& app_;
    AdminStore store_;
    AdminDatabase db_;
    std::string storeError_;
    std::vector<UltraStore::Component> components_;
    std::vector<UltraStore::Publisher> publishers_;
    std::vector<KeyRecord> keys_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(1);
    std::thread worker_;
    std::atomic<bool> busy_{false};

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<Container> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasSidebarNavigation> sidebar_;
    std::shared_ptr<Container> pageHost_;
    std::shared_ptr<Label> statusLabel_;
    std::map<std::string, std::shared_ptr<Container>> pages_;
    std::string currentPage_;
    int appsSection_ = -1, componentsSection_ = -1;

    // overview
    std::shared_ptr<Label> overviewText_;
    std::shared_ptr<TextInput> newId_, newName_;
    std::shared_ptr<Dropdown> newKind_;
    // settings
    std::map<std::string, std::shared_ptr<TextInput>> settingInputs_;
    std::shared_ptr<Label> settingsInfo_;
    // keys
    std::shared_ptr<Container> keysList_;
    std::shared_ptr<TextInput> keyName_, keyPass_, keyPassAgain_;
    // publish
    std::shared_ptr<TextInput> publishDir_, publishPass_;
    std::shared_ptr<Dropdown> publishKey_;
    std::shared_ptr<Checkbox> publishCopy_;
    std::shared_ptr<Label> publishReport_;
    // the component form
    std::shared_ptr<Container> formBody_;
    std::shared_ptr<Label> formTitle_;
    UltraStore::Component draft_;
    bool draftIsNew_ = false;
    bool draftDirty_ = false;
    std::string formLanguage_ = "en";
    std::shared_ptr<TextInput> fId_, fName_, fLicense_, fHomepage_, fSupport_, fPrivacy_, fSource_, fSuccessor_,
                               fCategories_, fTags_, fSourceLanguage_, fPrice_, fCurrency_, fTrialDays_, fUntil_, fIcon_,
                               fSummary_, fNewLanguage_;
    std::shared_ptr<TextArea> fDescription_;
    std::shared_ptr<Dropdown> fKind_, fPublisher_, fVisibility_, fLifecycle_, fAgeRating_, fPricingModel_, fUpdatePeriod_, fLanguage_;
    std::shared_ptr<Checkbox> fOpenSource_, fRequiresAccount_;
    std::shared_ptr<Container> fScreenshots_, fTexts_, fReleases_;
    std::shared_ptr<Label> fMachineNote_;
    // the add-release and add-package editors
    std::shared_ptr<Container> releaseEditor_;
    std::shared_ptr<TextInput> rVersion_, rDate_, rDepends_, rNotes_;
    std::shared_ptr<Dropdown> rChannel_;
    std::shared_ptr<Container> packageEditor_;
    std::string packageEditorVersion_;
    std::string packageFile_;
    std::shared_ptr<Label> pFile_;
    std::shared_ptr<Dropdown> pOs_, pArch_, pAccess_;
    std::shared_ptr<TextInput> pVariants_, pMinOs_, pEntry_;

    static constexpr int kMinWidth = 900;
    static constexpr int kMinHeight = 560;
};

} // namespace UltraStoreAdmin::Ui
