// Apps/UltraAIServerAdmin/ui/ServerAdminWindow.h
// UltraAI ServerAdmin's window: the super administrator's tool for an UltraAI
// model server (Server/UltraAIModelServer). A sidebar with the pages, on the
// right the page:
//
//   Connection      the server's address and the token; who is signed in, with
//                   which role, and the catalogue's date and revision.
//   Models          every model with its state (draft, published, withdrawn);
//                   the form with everything the catalogue carries; its files,
//                   uploaded from this computer (hashed first, verified by the
//                   server) or hosted elsewhere by URL; what still stands in
//                   the way of publishing; Publish, Withdraw, Delete.
//   Topics          what each kind of work (chat, code, translation, ...) is
//                   offered: the models in order, the default marked.
//   Engines         the gallery's engines and their bundles, as JSON (super).
//   Uploads         the files stored on the server and how often each is
//                   used; delete the unused ones (super).
//   Administrators  the accounts: super or editor, a new token shown once
//                   (super).
//   Audit log       who changed what, when.
//
// Every call goes through ModelServerClient, the same as the command line,
// on a worker thread; the answer comes back through PostToUIThread.
// Version: 0.1.0
// Author: UltraAI Module
#pragma once

#include "ModelServerClient.h"
#include "ServerAdminTheme.h"

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
#include <vector>

namespace UltraAIServerAdmin::Ui {

class ServerAdminWindow {
public:
    explicit ServerAdminWindow(UltraCanvas::UltraCanvasApplication& app);
    ~ServerAdminWindow();
    ServerAdminWindow(const ServerAdminWindow&) = delete;
    ServerAdminWindow& operator=(const ServerAdminWindow&) = delete;

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

    // ----- shell -----
    void BuildShell();
    std::shared_ptr<Container> BuildPage(const std::string& id, const std::string& title, const std::string& caption);
    void ShowPage(const std::string& id);
    void LayoutForSize(float width, float height);
    void SetStatus(const std::string& text, bool isError = false);
    bool OnClosing();
    bool Connected() const { return !role_.empty(); }
    bool IsSuper() const { return role_ == "super"; }

    // Run `work` on a worker thread, then `done` on the UI thread (not after
    // the window closed). One at a time: a second request while one runs is
    // refused with a word in the status line.
    void Run(const std::string& what, std::function<Reply(const ModelServerClient&)> work,
             std::function<void(const Reply&)> done);
    // A failed reply into the status line; true when it was ok.
    bool Check(const Reply& r);

    // ----- connection -----
    void BuildConnectionPage();
    void Connect();
    void RefreshConnectionInfo(const JSONValue& who);
    void LoadConnectionSettings();
    void SaveConnectionSettings() const;

    // ----- models -----
    void BuildModelsPage();
    void ReloadModels(const std::string& select = {});
    void FillModelsTable();
    void ShowModel(int row);
    void NewModel();
    bool ReadModelForm(std::string& id, JSONValue& model, std::string& error) const;
    void SaveModel();
    void ChangeModelState(const std::string& action);
    void DeleteModel();
    void CheckModel();
    void UploadFile();
    void AddUrlFile();
    void RemoveFile();
    void FillFiles(const JSONValue& model);

    // ----- topics -----
    void BuildTopicsPage();
    void ReloadTopics(const std::string& select = {});
    void ShowTopic(int row);
    void NewTopic();
    void FillTopicModels();
    void FillTopicModelChoices();
    void SaveTopic();
    void DeleteTopic();
    void AddTopicModel();
    void RemoveTopicModel();
    void MoveTopicModel(int delta);
    void MakeTopicDefault();

    // ----- engines, uploads, administrators, audit -----
    void BuildEnginesPage();
    void ReloadEngines();
    void ShowEngine(int row);
    void SaveEngine();
    void DeleteEngine();
    void BuildUploadsPage();
    void ReloadUploads();
    void PruneUploads();
    void BuildAdminsPage();
    void ReloadAdmins();
    void PutAdmin();
    void DeleteAdmin();
    void BuildAuditPage();
    void ReloadAudit();

    // ----- members -----
    UltraCanvas::UltraCanvasApplication& app_;
    Connection connection_;
    std::string role_;                       // "" until signed in; "super" or "editor"
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> busy_ = std::make_shared<std::atomic<bool>>(false);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<Container> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasSidebarNavigation> sidebar_;
    std::shared_ptr<Container> pageHost_;
    std::shared_ptr<Label> statusLabel_;
    std::map<std::string, std::shared_ptr<Container>> pages_;
    std::string currentPage_;

    // connection
    std::shared_ptr<TextInput> serverInput_, tokenInput_;
    std::shared_ptr<Checkbox> rememberServer_;
    std::shared_ptr<Label> connectionInfo_;

    // models
    Theme::Table modelsTable_;
    std::vector<JSONValue> models_;          // aligned with the table
    std::string editingModel_;               // "" = a new one
    std::shared_ptr<Label> modelTitle_, modelState_, modelProblems_;
    std::shared_ptr<TextInput> mId_, mName_, mEngine_, mLicense_, mMemory_, mGpu_, mContext_, mDims_, mTags_, mArgs_;
    std::shared_ptr<Dropdown> mCapability_;
    std::shared_ptr<Checkbox> mTools_, mVision_;
    std::shared_ptr<TextArea> mDescription_;
    Theme::Table filesTable_;
    std::vector<JSONValue> files_;           // aligned with the files table
    std::shared_ptr<Button> publishButton_, withdrawButton_, deleteModelButton_;
    std::shared_ptr<Label> uploadProgress_;

    // topics
    Theme::Table topicsTable_;
    std::vector<JSONValue> topics_;
    std::string editingTopic_;
    std::shared_ptr<TextInput> tId_, tName_, tDescription_;
    std::shared_ptr<Dropdown> tCapability_, tAddModel_;
    Theme::Table topicModelsTable_;
    struct TopicEntry { std::string id; bool isDefault = false; };
    std::vector<TopicEntry> topicEntries_;   // aligned with the topic's model table
    std::shared_ptr<Button> deleteTopicButton_;

    // engines
    Theme::Table enginesTable_;
    std::vector<JSONValue> engines_;
    std::shared_ptr<TextInput> eId_;
    std::shared_ptr<TextArea> eJson_;

    // uploads
    Theme::Table uploadsTable_;
    std::shared_ptr<Label> uploadsInfo_;

    // administrators
    Theme::Table adminsTable_;
    std::vector<JSONValue> admins_;
    std::shared_ptr<TextInput> aName_, aToken_;
    std::shared_ptr<Dropdown> aRole_;

    // audit
    Theme::Table auditTable_;

    static constexpr int kMinWidth = 980;
    static constexpr int kMinHeight = 640;
};

} // namespace UltraAIServerAdmin::Ui
