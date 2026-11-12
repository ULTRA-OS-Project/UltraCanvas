// Apps/UltraAIServerAdmin/ui/ServerAdminWindow.cpp
// UltraAI ServerAdmin's window (ServerAdminWindow.h).
// Version: 0.1.0
// Author: UltraAI Module

#include "ServerAdminWindow.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#ifndef ULTRAAISERVERADMIN_VERSION
#error "ULTRAAISERVERADMIN_VERSION must come from Docs/UltraAIServerAdmin/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)"
#endif

namespace UltraAIServerAdmin::Ui {

using namespace UltraCanvas;
using Theme::ButtonKind;

namespace {

constexpr const char* kConnection = "connection";
constexpr const char* kModels     = "models";
constexpr const char* kTopics     = "topics";
constexpr const char* kEngines    = "engines";
constexpr const char* kUploads    = "uploads";
constexpr const char* kAdmins     = "admins";
constexpr const char* kAudit      = "audit";

std::string Bytes(int64_t bytes) {
    if (bytes <= 0) return "—";
    char buffer[32];
    if (bytes >= 1024LL * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));   // locale-ok: shown to the user
    } else if (bytes >= 1024LL * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024));   // locale-ok: shown to the user
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld bytes", static_cast<long long>(bytes));
    }
    return buffer;
}

// A count of bytes typed as "8 GB", "512 MB" or a plain number.
bool ParseBytes(const std::string& text, int64_t& out) {
    std::string t;
    for (char c : text) if (c != ' ') t += c;
    if (t.empty()) { out = 0; return true; }
    size_t pos = 0;
    while (pos < t.size() && (std::isdigit(static_cast<unsigned char>(t[pos])) || t[pos] == '.')) ++pos;
    const std::string number = t.substr(0, pos);
    std::string unit = t.substr(pos);
    std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    double value = 0;
    if (!TryParseFloat(number, value)) return false;
    double scale = 1;
    if (unit == "KB" || unit == "K") scale = 1024.0;
    else if (unit == "MB" || unit == "M") scale = 1024.0 * 1024;
    else if (unit == "GB" || unit == "G") scale = 1024.0 * 1024 * 1024;
    else if (!unit.empty() && unit != "B") return false;
    out = static_cast<int64_t>(value * scale);
    return true;
}

// A size for a field the user edits and ParseBytes reads back: GB from
// 1 GB, MB below, exact bytes under 1 MB, to four significant digits - so a
// re-save keeps the figure to within 0.05 %. Dot-decimal on purpose: the
// field is read back with TryParseFloat, which takes only a dot.
std::string GbText(int64_t bytes) {
    if (bytes <= 0) return "";
    if (bytes >= 1024LL * 1024 * 1024) return FormatFloatClassic(static_cast<double>(bytes) / (1024.0 * 1024 * 1024), 4) + " GB";
    if (bytes >= 1024LL * 1024) return FormatFloatClassic(static_cast<double>(bytes) / (1024.0 * 1024), 4) + " MB";
    return std::to_string(bytes);
}

std::vector<std::string> SplitWords(const std::string& text, char separator) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        const size_t a = cur.find_first_not_of(" \t");
        if (a != std::string::npos) out.push_back(cur.substr(a, cur.find_last_not_of(" \t") - a + 1));
        cur.clear();
    };
    for (char c : text) {
        if (c == separator) flush();
        else cur += c;
    }
    flush();
    return out;
}

std::string JoinStrings(const JSONValue& list, const std::string& separator) {
    std::string out;
    for (const JSONValue& v : list.GetElements()) out += (out.empty() ? "" : separator) + v.GetString();
    return out;
}

std::shared_ptr<UltraCanvasTextInput> Input(const std::string& id, const std::string& placeholder = {}) {
    auto in = std::make_shared<UltraCanvasTextInput>(id, 0, 0, 300, Theme::kControlHeight);
    if (!placeholder.empty()) in->SetPlaceholder(placeholder);
    return Theme::Pin(in);
}

std::shared_ptr<UltraCanvasDropdown> Choice(const std::string& id, float width) {
    auto dd = std::make_shared<UltraCanvasDropdown>(id, 0, 0, width, Theme::kControlHeight);
    dd->SetElementSize(CSSLayout::Dimension::Px(width), CSSLayout::Dimension::Px(Theme::kControlHeight));
    return Theme::Pin(dd);
}

std::shared_ptr<UltraCanvasContainer> ScrollList(const std::string& id) {
    auto list = CreateScrollableContainer(id, 0, 0, 0, 0);
    ContainerStyle style = list->GetContainerStyle();
    style.forceShowVerticalScrollbar = false;
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    list->SetContainerStyle(style);
    list->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    list->SetPadding(2, 16, 2, 2);
    return list;
}

std::filesystem::path SettingsPath() {
    return UltraCanvasDataFolder("UltraAIServerAdmin") / "settings.json";
}

} // namespace

ServerAdminWindow::ServerAdminWindow(UltraCanvasApplication& app) : app_(app) {}

ServerAdminWindow::~ServerAdminWindow() {
    alive_->store(false);
}

bool ServerAdminWindow::Create() {
    WindowConfig config;
    config.title = "UltraAI ServerAdmin " ULTRAAISERVERADMIN_VERSION;   // the product's name and version: not translated
    config.width = 1180;
    config.height = 780;
    config.minWidth = kMinWidth;
    config.minHeight = kMinHeight;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);
    if (!window_) return false;

    BuildShell();
    LayoutForSize(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int w, int h) { LayoutForSize(static_cast<float>(w), static_cast<float>(h)); };
    window_->onWindowClosing = [this]() { return OnClosing(); };
    LoadConnectionSettings();
    ShowPage(kConnection);
    // Address and token both given (ULTRAAI_SERVER_TOKEN): sign in at once.
    if (!serverInput_->GetText().empty() && !tokenInput_->GetText().empty()) Connect();
    return true;
}

void ServerAdminWindow::Show() {
    if (window_) window_->Show();
}

// ===========================================================================
// Shell
// ===========================================================================
void ServerAdminWindow::BuildShell() {
    root_ = Theme::Column("saRoot", 0);
    root_->SetBackgroundColor(Theme::kPageBackground);
    auto body = Theme::Row("saBody", 0);
    body->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    sidebar_ = Theme::Pin(CreateSidebarNavigation("saSidebar", 0, 0, 220, 0));
    sidebar_->SetElementSize(CSSLayout::Dimension::Px(220), CSSLayout::Dimension::Auto());
    const int server = sidebar_->AddSection(Tr("Model server"));
    sidebar_->AddItem(server, kConnection, Tr("Connection"), Theme::Icon("settings.svg"));
    sidebar_->AddItem(server, kModels, Tr("Models"), Theme::Icon("list-check.svg"));
    sidebar_->AddItem(server, kTopics, Tr("Topics"), Theme::Icon("home.svg"));
    const int admin = sidebar_->AddSection(Tr("Administration"));
    sidebar_->AddItem(admin, kEngines, Tr("Engines"), Theme::Icon("settings.svg"));
    sidebar_->AddItem(admin, kUploads, Tr("Uploads"), Theme::Icon("folder-download.svg"));
    sidebar_->AddItem(admin, kAdmins, Tr("Administrators"), Theme::Icon("user-plus.svg"));
    sidebar_->AddItem(admin, kAudit, Tr("Audit log"), Theme::Icon("list-check.svg"));
    sidebar_->onSelectionChanged = [this](const std::string& id) {
        if (currentPage_ != id) ShowPage(id);
    };

    pageHost_ = Theme::Column("saPages", 0);
    pageHost_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    BuildConnectionPage();
    BuildModelsPage();
    BuildTopicsPage();
    BuildEnginesPage();
    BuildUploadsPage();
    BuildAdminsPage();
    BuildAuditPage();

    body->AddChild(sidebar_);
    body->AddChild(pageHost_);

    auto statusRow = Theme::Row("saStatusRow", 0);
    statusRow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(Theme::kStatusHeight));
    statusRow->SetPadding(0, Theme::kPagePadding);
    statusRow->SetBackgroundColor(Theme::kSurface);
    statusRow->SetBorders(0.0f, Theme::kBorder, 0.0f);
    statusLabel_ = Theme::Caption("saStatus", Tr("Not connected."));
    statusLabel_->SetWrap(TextWrap::WrapNone);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    statusRow->AddChild(statusLabel_);

    root_->AddChild(body);
    root_->AddChild(statusRow);
    window_->AddChild(root_);
}

std::shared_ptr<ServerAdminWindow::Container> ServerAdminWindow::BuildPage(const std::string& id, const std::string& title,
                                                                           const std::string& caption) {
    auto page = Theme::Column(id, Theme::kGap);
    page->SetPadding(Theme::kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    page->AddChild(Theme::Heading(id + ".title", title));
    if (!caption.empty()) page->AddChild(Theme::Caption(id + ".caption", caption));
    auto body = ScrollList(id + ".body");
    page->AddChild(body);
    pages_[id] = page;
    pageHost_->AddChild(page);
    page->SetVisible(false);
    return body;
}

void ServerAdminWindow::ShowPage(const std::string& id) {
    currentPage_ = id;
    for (auto& [key, page] : pages_) page->SetVisible(key == id);
    if (sidebar_->GetSelectedId() != id) sidebar_->Select(id, false);
    if (Connected()) {
        if (id == kModels && models_.empty()) ReloadModels();
        if (id == kTopics) ReloadTopics(editingTopic_);
        if (id == kEngines) ReloadEngines();
        if (id == kUploads) ReloadUploads();
        if (id == kAdmins && IsSuper()) ReloadAdmins();
        if (id == kAudit) ReloadAudit();
    } else if (id != kConnection) {
        SetStatus(Tr("Connect to a model server first (Connection)."), true);
    }
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

void ServerAdminWindow::LayoutForSize(float width, float height) {
    if (!root_) return;
    root_->SetElementAbsolutePosition(Point2Df(0.0f, 0.0f));
    root_->SetElementSize(Size2Df(std::max(static_cast<float>(kMinWidth), width), std::max(static_cast<float>(kMinHeight), height)));
    root_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

void ServerAdminWindow::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextSecondary);
    statusLabel_->RequestRedraw();
}

bool ServerAdminWindow::OnClosing() {
    if (busy_->load()) {
        SetStatus(Tr("A request is running (an upload?): close the window once it has finished."), true);
        return false;
    }
    app_.RequestExit();
    return true;
}

void ServerAdminWindow::Run(const std::string& what, std::function<Reply(const ModelServerClient&)> work,
                            std::function<void(const Reply&)> done) {
    if (busy_->exchange(true)) {
        SetStatus(Tr("Another request is still running."), true);
        return;
    }
    SetStatus(what + "…");
    const ModelServerClient client(connection_);
    std::thread([client, work = std::move(work), done = std::move(done), alive = alive_, busy = busy_, this]() {
        Reply reply = work(client);
        app_.PostToUIThread([reply = std::move(reply), done, alive, busy, this]() {
            busy->store(false);
            if (!alive->load()) return;
            done(reply);
        });
    }).detach();
}

bool ServerAdminWindow::Check(const Reply& r) {
    if (r.ok) return true;
    SetStatus(r.message + (r.error.empty() || r.error == "network" ? "" : " (" + r.error + ")"), true);
    return false;
}

// ===========================================================================
// Connection
// ===========================================================================
void ServerAdminWindow::BuildConnectionPage() {
    auto body = BuildPage(kConnection, Tr("Connection"),
                          Tr("The UltraAI model server this tool manages, and the token its administrator gave you."));
    auto card = Theme::Surface("saC.card", Theme::kGap);
    serverInput_ = Input("saC.server", "https://models.example.org");
    card->AddChild(Theme::Field("saC.serverRow", Tr("Server"), serverInput_));
    tokenInput_ = Input("saC.token", Tr("The super administrator's or an account's token"));
    tokenInput_->SetInputType(TextInputType::Password);
    card->AddChild(Theme::Field("saC.tokenRow", Tr("Token"), tokenInput_));
    auto row = Theme::Row("saC.actions", Theme::kGap);
    auto connect = Theme::Button("saC.connect", Tr("Connect"), 110, ButtonKind::Primary);
    connect->SetOnClick([this]() { Connect(); });
    row->AddChild(connect);
    rememberServer_ = Theme::Pin(std::make_shared<UltraCanvasCheckbox>("saC.remember", 0, 0, 300, Theme::kControlHeight,
                                                                       Tr("Remember the server's address")));
    rememberServer_->SetChecked(true);
    row->AddChild(rememberServer_);
    card->AddChild(row);
    connectionInfo_ = Theme::Text("saC.info", Tr("Not connected."), 12.0f);
    card->AddChild(connectionInfo_);
    body->AddChild(card);

    auto help = Theme::Surface("saC.help", Theme::kInnerGap);
    help->AddChild(Theme::Text("saC.help.title", Tr("Clients"), 14.0f, FontWeight::Bold));
    help->AddChild(Theme::Caption("saC.help.text",
        Tr("UltraAI clients read this server's gallery at <server>/gallery/index.json: enter that address (or just the "
        "server's) under UltraAI Settings > Model topics > Model server. Only published models reach them; a "
        "withdrawn model stays installed where it is, but is offered no more. The token is never stored by this "
        "tool; ULTRAAI_SERVER_TOKEN in the environment fills it in.")));
    body->AddChild(help);
}

void ServerAdminWindow::LoadConnectionSettings() {
    JSONParseResult result;
    const JSONValue settings = JSON::ParseFile(PathToUtf8(SettingsPath()), &result);
    std::string server = result.success ? settings["server"].GetString() : std::string();
    const std::string envServer = GetEnvUtf8("ULTRAAI_SERVER_URL");
    if (server.empty()) server = envServer;
    if (serverInput_) serverInput_->SetText(server);
    if (tokenInput_) tokenInput_->SetText(GetEnvUtf8("ULTRAAI_SERVER_TOKEN"));
}

void ServerAdminWindow::SaveConnectionSettings() const {
    std::error_code ec;
    std::filesystem::create_directories(SettingsPath().parent_path(), ec);
    JSONValue settings = JSONValue::MakeObject();
    settings.Set("server", rememberServer_ && rememberServer_->IsChecked() ? connection_.baseUrl : std::string());
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    JSON::SerializeToFile(PathToUtf8(SettingsPath()), settings, pretty);
}

void ServerAdminWindow::Connect() {
    std::string why;
    const std::string base = ModelServerClient::NormaliseBaseUrl(serverInput_->GetText(), &why);
    if (base.empty()) {
        SetStatus(why, true);
        return;
    }
    if (tokenInput_->GetText().empty()) {
        SetStatus(Tr("Enter the token."), true);
        return;
    }
    connection_.baseUrl = base;
    connection_.token = tokenInput_->GetText();
    role_.clear();
    Run(TrF("Connecting to {0}", base), [](const ModelServerClient& c) { return c.WhoAmI(); }, [this](const Reply& r) {
        if (!Check(r)) {
            connectionInfo_->SetText(TrF("Not connected: {0}", r.message));
            return;
        }
        role_ = r.body["role"].GetString();
        serverInput_->SetText(connection_.baseUrl);
        SaveConnectionSettings();
        RefreshConnectionInfo(r.body);
        SetStatus(TrF("Connected as {0} ({1}).", r.body["name"].GetString(), role_));
        models_.clear();
        ReloadModels();
    });
}

void ServerAdminWindow::RefreshConnectionInfo(const JSONValue& who) {
    std::string text = (who["role"].GetString() == "super"
                            ? TrF("Signed in as {0}, super administrator.", who["name"].GetString())
                            : TrF("Signed in as {0}, editor (models and topics).", who["name"].GetString())) +
                       "\n" + TrF("Server {0} at {1}; catalogue of {2}, revision {3}.", who["server"].GetString(),
                                  who["baseUrl"].GetString(), who["published"].GetString(),
                                  who["revision"].GetInteger());
    const std::string key = who["signingKey"].GetString();
    if (!key.empty()) {
        text += "\n" + TrF("Its lists are signed with the key {0}. A client pins it on its first refresh; "
                           "give it to users who want to check it.", key);
    }
    connectionInfo_->SetText(text);
}

// ===========================================================================
// Models
// ===========================================================================
void ServerAdminWindow::BuildModelsPage() {
    auto body = BuildPage(kModels, Tr("Models"),
                          Tr("Every model on the server. Clients see the published ones; a draft is prepared here first, "
                          "a withdrawn one is kept but offered no more."));
    auto top = Theme::Row("saM.top", Theme::kGap);
    auto reload = Theme::Button("saM.reload", Tr("Reload"), 90);
    reload->SetOnClick([this]() { ReloadModels(editingModel_); });
    auto add = Theme::Button("saM.new", Tr("New model"), 110, ButtonKind::Primary);
    add->SetOnClick([this]() { NewModel(); });
    top->AddChild(reload);
    top->AddChild(add);
    body->AddChild(top);

    modelsTable_ = Theme::MakeTable("saM.table", {{Tr("Id"), 170}, {Tr("Name"), 200}, {Tr("Service"), 90}, {Tr("State"), 90},
                                                  {Tr("Files"), 60}, {Tr("Size"), 80}, {Tr("Topics"), 160}}, 220);
    modelsTable_.view->onSelectionChanged = [this](const std::vector<int>&) { ShowModel(modelsTable_.SelectedRow()); };
    body->AddChild(modelsTable_.view);

    auto form = Theme::Surface("saM.form", Theme::kInnerGap);
    modelTitle_ = Theme::Text("saM.formTitle", Tr("New model"), 14.0f, FontWeight::Bold);
    form->AddChild(modelTitle_);
    modelState_ = Theme::Caption("saM.state", "");
    form->AddChild(modelState_);
    mId_ = Input("saM.id", Tr("qwen3-14b-q4: a-z, 0-9, '.', '_', '-'"));
    form->AddChild(Theme::Field("saM.idRow", Tr("Id"), mId_));
    mName_ = Input("saM.name", Tr("Qwen3 14B (Q4_K_M)"));
    form->AddChild(Theme::Field("saM.nameRow", Tr("Name"), mName_));
    mCapability_ = Choice("saM.cap", 220);
    for (const std::string& c : Capabilities()) mCapability_->AddItem(CapabilityLabel(c), c);
    mCapability_->SetSelectedIndex(0, false);
    form->AddChild(Theme::Field("saM.capRow", Tr("Service"), mCapability_, false));
    mEngine_ = Input("saM.engine", Tr("llama-server"));
    form->AddChild(Theme::Field("saM.engineRow", Tr("Engine"), mEngine_));
    mLicense_ = Input("saM.license", "Apache-2.0");
    form->AddChild(Theme::Field("saM.licenseRow", Tr("Licence"), mLicense_));
    mDescription_ = Theme::Pin(std::make_shared<UltraCanvasTextArea>("saM.desc", 0.0f, 70.0f));
    mDescription_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(70));
    mDescription_->SetWordWrap(true);
    mDescription_->SetShowLineNumbers(false);
    mDescription_->SetHighlightSyntax(false);
    form->AddChild(Theme::Field("saM.descRow", Tr("Description"), mDescription_));
    mMemory_ = Input("saM.memory", "8 GB");
    form->AddChild(Theme::Field("saM.memRow", Tr("Memory needed"), mMemory_));
    mGpu_ = Input("saM.gpu", Tr("for full GPU offload, e.g. 7 GB"));
    form->AddChild(Theme::Field("saM.gpuRow", Tr("GPU memory"), mGpu_));
    mContext_ = Input("saM.context", "32768");
    form->AddChild(Theme::Field("saM.ctxRow", Tr("Context window"), mContext_));
    mDims_ = Input("saM.dims", Tr("embedding models: 1024"));
    form->AddChild(Theme::Field("saM.dimsRow", Tr("Dimensions"), mDims_));
    auto feat = Theme::Row("saM.feat", Theme::kGap);
    auto featLabel = Theme::Text("saM.feat.lbl", Tr("Features"), 12.0f);
    featLabel->SetElementSize(CSSLayout::Dimension::Px(Theme::kLabelWidth), CSSLayout::Dimension::Auto());
    feat->AddChild(featLabel);
    mTools_ = Theme::Pin(std::make_shared<UltraCanvasCheckbox>("saM.tools", 0, 0, 140, Theme::kControlHeight, Tr("Tool calls")));
    mVision_ = Theme::Pin(std::make_shared<UltraCanvasCheckbox>("saM.vision", 0, 0, 140, Theme::kControlHeight, Tr("Pictures")));
    feat->AddChild(mTools_);
    feat->AddChild(mVision_);
    form->AddChild(feat);
    mTags_ = Input("saM.tags", Tr("chat, tools, multilingual, recommended"));
    form->AddChild(Theme::Field("saM.tagsRow", Tr("Tags"), mTags_));
    mArgs_ = Input("saM.args", "--ctx-size 8192");
    form->AddChild(Theme::Field("saM.argsRow", Tr("Engine arguments"), mArgs_));

    auto actions = Theme::Row("saM.actions", Theme::kGap);
    auto save = Theme::Button("saM.save", Tr("Save"), 90, ButtonKind::Primary);
    save->SetOnClick([this]() { SaveModel(); });
    publishButton_ = Theme::Button("saM.publish", Tr("Publish"), 90);
    publishButton_->SetOnClick([this]() { ChangeModelState("publish"); });
    withdrawButton_ = Theme::Button("saM.withdraw", Tr("Withdraw"), 100);
    withdrawButton_->SetOnClick([this]() { ChangeModelState("withdraw"); });
    auto check = Theme::Button("saM.check", Tr("Check"), 80);
    check->SetOnClick([this]() { CheckModel(); });
    deleteModelButton_ = Theme::Button("saM.delete", Tr("Delete"), 90, ButtonKind::Danger);
    deleteModelButton_->SetOnClick([this]() { DeleteModel(); });
    actions->AddChild(save);
    actions->AddChild(publishButton_);
    actions->AddChild(withdrawButton_);
    actions->AddChild(check);
    actions->AddChild(deleteModelButton_);
    form->AddChild(actions);
    modelProblems_ = Theme::Caption("saM.problems", "");
    form->AddChild(modelProblems_);
    body->AddChild(form);

    auto files = Theme::Surface("saM.files", Theme::kInnerGap);
    files->AddChild(Theme::Text("saM.files.title", Tr("Files"), 14.0f, FontWeight::Bold));
    files->AddChild(Theme::Caption("saM.files.caption",
        Tr("The first file is the one the engine loads; a vision model's projector (mmproj*.gguf) goes beside it. "
        "An upload is hashed here and checked by the server; a file hosted elsewhere needs its https address, its "
        "size and its SHA-256 (a Hugging Face file may leave the hash to the host).")));
    filesTable_ = Theme::MakeTable("saM.filesTable", {{Tr("Name"), 220}, {Tr("Source"), 280}, {Tr("Size"), 90}, {Tr("SHA-256"), 160}}, 130);
    files->AddChild(filesTable_.view);
    auto fileActions = Theme::Row("saM.fileActions", Theme::kGap);
    auto upload = Theme::Button("saM.upload", Tr("Upload file"), 110, ButtonKind::Primary);
    upload->SetOnClick([this]() { UploadFile(); });
    auto url = Theme::Button("saM.url", Tr("Add by address"), 130);
    url->SetOnClick([this]() { AddUrlFile(); });
    auto remove = Theme::Button("saM.removeFile", Tr("Remove file"), 110, ButtonKind::Danger);
    remove->SetOnClick([this]() { RemoveFile(); });
    fileActions->AddChild(upload);
    fileActions->AddChild(url);
    fileActions->AddChild(remove);
    uploadProgress_ = Theme::Caption("saM.progress", "");
    uploadProgress_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    fileActions->AddChild(uploadProgress_);
    files->AddChild(fileActions);
    body->AddChild(files);
    NewModel();
}

void ServerAdminWindow::ReloadModels(const std::string& select) {
    if (!Connected()) return;
    Run(Tr("Loading the models"), [](const ModelServerClient& c) { return c.ListModels(); }, [this, select](const Reply& r) {
        if (!Check(r)) return;
        models_ = r.body["models"].GetElements();
        FillModelsTable();
        SetStatus(TrN("{n} model on the server.", "{n} models on the server.", static_cast<long long>(models_.size())));
        for (size_t i = 0; i < models_.size(); ++i) {
            if (!select.empty() && models_[i]["id"].GetString() == select) {
                modelsTable_.Select(static_cast<int>(i));
                ShowModel(static_cast<int>(i));
            }
        }
    });
}

void ServerAdminWindow::FillModelsTable() {
    std::vector<std::vector<std::string>> rows;
    for (const JSONValue& m : models_) {
        int64_t size = 0;
        for (const JSONValue& f : m["files"].GetElements()) size += f["size"].GetInteger();
        rows.push_back({m["id"].GetString(), m["displayName"].GetString(), CapabilityLabel(m["capability"].GetString()),
                        m["status"].GetString(), std::to_string(m["files"].GetSize()), Bytes(size),
                        JoinStrings(m["topics"], ", ")});
    }
    modelsTable_.SetRows(rows);
}

void ServerAdminWindow::NewModel() {
    editingModel_.clear();
    modelTitle_->SetText(Tr("New model"));
    modelState_->SetText(Tr("A new model is a draft until it has its files and is published."));
    modelProblems_->SetText("");
    mId_->SetReadOnly(false);
    for (auto& in : {mId_, mName_, mLicense_, mMemory_, mGpu_, mContext_, mDims_, mTags_, mArgs_}) in->SetText("");
    mEngine_->SetText(Tr("llama-server"));
    mDescription_->SetText("");
    mCapability_->SetSelectedIndex(0, false);
    mTools_->SetChecked(false);
    mVision_->SetChecked(false);
    files_.clear();
    filesTable_.SetRows({});
    publishButton_->SetDisabled(true);
    withdrawButton_->SetDisabled(true);
    deleteModelButton_->SetDisabled(true);
}

void ServerAdminWindow::ShowModel(int row) {
    if (row < 0 || row >= static_cast<int>(models_.size())) return;
    const JSONValue& m = models_[static_cast<size_t>(row)];
    editingModel_ = m["id"].GetString();
    const std::string status = m["status"].GetString();
    modelTitle_->SetText(m["displayName"].GetString());
    const std::string stateName = status == "published" ? Tr("published") : status == "withdrawn" ? Tr("withdrawn") : Tr("draft");
    modelState_->SetText(m["publishedAt"].GetString().empty()
                             ? TrF("State: {0}; changed {1}.", stateName, m["updatedAt"].GetString())
                             : TrF("State: {0}, published {1}; changed {2}.", stateName, m["publishedAt"].GetString(),
                                   m["updatedAt"].GetString()));
    modelProblems_->SetText("");
    mId_->SetText(editingModel_);
    mId_->SetReadOnly(true);
    mName_->SetText(m["displayName"].GetString());
    for (int i = 0; i < mCapability_->GetItemCount(); ++i) {
        if (mCapability_->GetItem(i)->value == m["capability"].GetString()) mCapability_->SetSelectedIndex(i, false);
    }
    mEngine_->SetText(m["engine"].GetString());
    mLicense_->SetText(m["license"].GetString());
    mDescription_->SetText(m["description"].GetString(), false);
    mMemory_->SetText(GbText(m["requires"]["memoryBytes"].GetInteger()));
    mGpu_->SetText(GbText(m["requires"]["gpuMemoryBytes"].GetInteger()));
    const int64_t ctx = m["features"]["contextWindow"].GetInteger();
    const int64_t dims = m["features"]["embeddingDimensions"].GetInteger();
    mContext_->SetText(ctx ? std::to_string(ctx) : "");
    mDims_->SetText(dims ? std::to_string(dims) : "");
    mTools_->SetChecked(m["features"]["tools"].GetBoolean());
    mVision_->SetChecked(m["features"]["vision"].GetBoolean());
    mTags_->SetText(JoinStrings(m["tags"], ", "));
    mArgs_->SetText(JoinStrings(m["engineArgs"], " "));
    FillFiles(m);
    publishButton_->SetDisabled(status == "published");
    withdrawButton_->SetDisabled(status != "published");
    deleteModelButton_->SetDisabled(!IsSuper());
}

void ServerAdminWindow::FillFiles(const JSONValue& m) {
    files_ = m["files"].GetElements();
    std::vector<std::vector<std::string>> rows;
    for (const JSONValue& f : files_) {
        const std::string sha = f["sha256"].GetString();
        rows.push_back({f["name"].GetString(), f["uploaded"].GetBoolean() ? Tr("uploaded to this server") : f["url"].GetString(),
                        Bytes(f["size"].GetInteger()), sha.empty() ? Tr("from the host") : sha.substr(0, 16) + "…"});
    }
    filesTable_.SetRows(rows);
}

bool ServerAdminWindow::ReadModelForm(std::string& id, JSONValue& model, std::string& error) const {
    id = mId_->GetText();
    if (id.empty()) { error = Tr("The model needs an id."); return false; }
    model = JSONValue::MakeObject();
    model.Set("displayName", mName_->GetText().empty() ? id : mName_->GetText());
    model.Set("description", mDescription_->GetText());
    const DropdownItem* cap = mCapability_->GetSelectedItem();
    model.Set("capability", cap ? cap->value : std::string("textllm"));
    model.Set("engine", mEngine_->GetText().empty() ? std::string("llama-server") : mEngine_->GetText());
    model.Set("license", mLicense_->GetText());
    int64_t memory = 0, gpu = 0;
    if (!ParseBytes(mMemory_->GetText(), memory)) { error = Tr("Memory needed: a size such as 8 GB."); return false; }
    if (!ParseBytes(mGpu_->GetText(), gpu)) { error = Tr("GPU memory: a size such as 7 GB."); return false; }
    JSONValue req = JSONValue::MakeObject();
    if (memory) req.Set("memoryBytes", memory);
    if (gpu) req.Set("gpuMemoryBytes", gpu);
    model.Set("requires", req);
    JSONValue feat = JSONValue::MakeObject();
    int64_t ctx = 0, dims = 0;
    if (!ParseBytes(mContext_->GetText(), ctx)) { error = Tr("Context window: a number of tokens."); return false; }
    if (!ParseBytes(mDims_->GetText(), dims)) { error = Tr("Dimensions: a number."); return false; }
    if (ctx) feat.Set("contextWindow", ctx);
    if (dims) feat.Set("embeddingDimensions", dims);
    if (mTools_->IsChecked()) feat.Set("tools", true);
    if (mVision_->IsChecked()) feat.Set("vision", true);
    model.Set("features", feat);
    JSONValue tags = JSONValue::MakeArray();
    for (const std::string& t : SplitWords(mTags_->GetText(), ',')) tags.Append(JSONValue(t));
    model.Set("tags", tags);
    JSONValue args = JSONValue::MakeArray();
    for (const std::string& a : SplitWords(mArgs_->GetText(), ' ')) args.Append(JSONValue(a));
    model.Set("engineArgs", args);
    return true;
}

void ServerAdminWindow::SaveModel() {
    if (!Connected()) { SetStatus(Tr("Connect first."), true); return; }
    std::string id, error;
    JSONValue model;
    if (!ReadModelForm(id, model, error)) { SetStatus(error, true); return; }
    Run(TrF("Saving {0}", id), [id, model](const ModelServerClient& c) { return c.PutModel(id, model); },
        [this, id](const Reply& r) {
            if (!Check(r)) return;
            SetStatus(TrF("Saved {0}.", id));
            editingModel_ = id;
            ReloadModels(id);
        });
}

void ServerAdminWindow::ChangeModelState(const std::string& action) {
    if (editingModel_.empty()) return;
    const std::string id = editingModel_;
    Run(action == "publish" ? TrF("Publishing {0}", id) : TrF("Withdrawing {0}", id),
        [id, action](const ModelServerClient& c) { return action == "publish" ? c.Publish(id) : c.Withdraw(id); },
        [this, id, action](const Reply& r) {
            if (!r.ok && r.error == "not_publishable") {
                modelProblems_->SetText(TrF("Not yet: {0}", r.message));
                SetStatus(TrF("{0} cannot be published yet.", id), true);
                return;
            }
            if (!Check(r)) return;
            SetStatus(action == "publish" ? TrF("{0} is published: clients see it at their next refresh.", id)
                                       : TrF("{0} is withdrawn.", id));
            ReloadModels(id);
        });
}

void ServerAdminWindow::CheckModel() {
    if (editingModel_.empty()) return;
    const std::string id = editingModel_;
    Run(TrF("Checking {0}", id), [id](const ModelServerClient& c) { return c.CheckModel(id); }, [this, id](const Reply& r) {
        if (!Check(r)) return;
        if (r.body["publishable"].GetBoolean()) {
            modelProblems_->SetText(Tr("Ready to publish."));
        } else {
            std::string text = Tr("Before it can be published:");
            for (const JSONValue& p : r.body["problems"].GetElements()) text += "\n- " + p.GetString();
            modelProblems_->SetText(text);
        }
        SetStatus(TrF("Checked {0}.", id));
    });
}

void ServerAdminWindow::DeleteModel() {
    if (editingModel_.empty() || !IsSuper()) return;
    const std::string id = editingModel_;
    UltraCanvasDialogManager::ShowConfirmation(
        TrF("Delete {0} from the server? It leaves every topic. Clients that installed it keep it. "
            "Withdraw it instead to keep it on record.", id), Tr("Delete model"),
        [this, id, alive = alive_](bool confirmed) {
            if (!confirmed || !alive->load()) return;
            Run(TrF("Deleting {0}", id), [id](const ModelServerClient& c) { return c.DeleteModel(id); }, [this, id](const Reply& r) {
                if (!Check(r)) return;
                SetStatus(TrF("Deleted {0}.", id));
                NewModel();
                ReloadModels();
            });
        }, window_.get());
}

void ServerAdminWindow::UploadFile() {
    if (editingModel_.empty()) { SetStatus(Tr("Save the model first, then add its files."), true); return; }
    FileDialogOptions opts;
    opts.SetTitle(Tr("Upload a model file"))
        .SetParentWindow(window_.get())
        .AddFilter(Tr("GGUF models"), std::vector<std::string>{"gguf"})
        .AddFilter(Tr("All files"), "*");
    UltraCanvasFileLoader::OpenFileDialog(opts, [this, alive = alive_](DialogResult result, const std::string& path) {
        if (!alive->load() || result != DialogResult::OK || path.empty()) return;
        const std::string id = editingModel_;
        const std::string name = PathToUtf8(PathFromUtf8(path).filename());
        auto progress = uploadProgress_.get();
        Run(TrF("Hashing and uploading {0} (a large model takes a while)", name),
            [id, name, path, progress, alive, this](const ModelServerClient& c) {
                int lastPercent = -1;
                return c.UploadFile(id, name, path, [&lastPercent, progress, alive, this](int64_t done, int64_t total) {
                    if (total <= 0) return;
                    const int percent = static_cast<int>(done * 100 / total);
                    if (percent == lastPercent) return;
                    lastPercent = percent;
                    const std::string text = TrF("Uploading: {0}% of {1}", percent, Bytes(total));
                    app_.PostToUIThread([progress, text, alive]() {
                        if (alive->load()) progress->SetText(text);
                    });
                });
            },
            [this, id, name](const Reply& r) {
                uploadProgress_->SetText("");
                if (!Check(r)) return;
                SetStatus(r.body["stored"].GetBoolean()
                              ? TrF("{0} uploaded and verified ({1}).", name, Bytes(r.body["size"].GetInteger()))
                              : TrF("{0} was already on the server ({1}).", name, Bytes(r.body["size"].GetInteger())));
                ReloadModels(id);
            });
    });
}

void ServerAdminWindow::AddUrlFile() {
    if (editingModel_.empty()) { SetStatus(Tr("Save the model first, then add its files."), true); return; }
    UltraCanvasDialogManager::ShowInputDialog(
        Tr("The file's https address, its size and its SHA-256, separated by spaces:\n"
        "https://huggingface.co/org/repo/resolve/main/model.gguf 2497000000 <sha256>\n"
        "A Hugging Face file may leave out the hash; the client then takes it from the host."),
        Tr("Add a file by address"), "", InputType::Text,
        [this, alive = alive_](DialogResult result, const std::string& text) {
            if (!alive->load() || result != DialogResult::OK || text.empty()) return;
            const std::vector<std::string> parts = SplitWords(text, ' ');
            int64_t size = 0;
            if (parts.size() < 2 || !ParseBytes(parts[1], size) || size <= 0) {
                SetStatus(Tr("Give the address and the size in bytes (and the SHA-256)."), true);
                return;
            }
            const std::string url = parts[0];
            const std::string sha = parts.size() > 2 ? parts[2] : std::string();
            std::string name = url.substr(url.find_last_of('/') + 1);
            const size_t query = name.find('?');
            if (query != std::string::npos) name.resize(query);
            const std::string id = editingModel_;
            Run(TrF("Adding {0}", name), [id, name, url, sha, size](const ModelServerClient& c) {
                return c.PutExternalFile(id, name, url, sha, static_cast<uint64_t>(size));
            }, [this, id, name](const Reply& r) {
                if (!Check(r)) return;
                SetStatus(TrF("Added {0}.", name));
                ReloadModels(id);
            });
        }, window_.get());
}

void ServerAdminWindow::RemoveFile() {
    const int row = filesTable_.SelectedRow();
    if (editingModel_.empty() || row < 0 || row >= static_cast<int>(files_.size())) {
        SetStatus(Tr("Select a file first."), true);
        return;
    }
    const std::string id = editingModel_;
    const std::string name = files_[static_cast<size_t>(row)]["name"].GetString();
    Run(TrF("Removing {0}", name), [id, name](const ModelServerClient& c) { return c.RemoveModelFile(id, name); },
        [this, id, name](const Reply& r) {
            if (!Check(r)) return;
            SetStatus(TrF("Removed {0}. An upload no model uses any more is deleted with Uploads > Delete unused.", name));
            ReloadModels(id);
        });
}

// ===========================================================================
// Topics
// ===========================================================================
void ServerAdminWindow::BuildTopicsPage() {
    auto body = BuildPage(kTopics, Tr("Topics"),
                          Tr("What each kind of work is offered. A client uses the topic's default unless the user "
                          "picked another model; when the default changes here, clients that follow the server "
                          "download the new one."));
    auto top = Theme::Row("saT.top", Theme::kGap);
    auto reload = Theme::Button("saT.reload", Tr("Reload"), 90);
    reload->SetOnClick([this]() { ReloadTopics(editingTopic_); });
    auto add = Theme::Button("saT.new", Tr("New topic"), 110, ButtonKind::Primary);
    add->SetOnClick([this]() { NewTopic(); });
    top->AddChild(reload);
    top->AddChild(add);
    body->AddChild(top);

    topicsTable_ = Theme::MakeTable("saT.table", {{Tr("Id"), 130}, {Tr("Name"), 200}, {Tr("Service"), 90}, {Tr("Default"), 200}, {Tr("Models"), 70}}, 190);
    topicsTable_.view->onSelectionChanged = [this](const std::vector<int>&) { ShowTopic(topicsTable_.SelectedRow()); };
    body->AddChild(topicsTable_.view);

    auto form = Theme::Surface("saT.form", Theme::kInnerGap);
    form->AddChild(Theme::Text("saT.formTitle", Tr("Topic"), 14.0f, FontWeight::Bold));
    tId_ = Input("saT.id", Tr("legal: a-z, 0-9, '.', '_', '-'"));
    form->AddChild(Theme::Field("saT.idRow", Tr("Id"), tId_));
    tName_ = Input("saT.name", Tr("Legal texts"));
    form->AddChild(Theme::Field("saT.nameRow", Tr("Name"), tName_));
    tDescription_ = Input("saT.desc", Tr("Contracts, letters to authorities"));
    form->AddChild(Theme::Field("saT.descRow", Tr("Description"), tDescription_));
    tCapability_ = Choice("saT.cap", 220);
    for (const std::string& c : Capabilities()) tCapability_->AddItem(CapabilityLabel(c), c);
    tCapability_->SetSelectedIndex(0, false);
    tCapability_->onSelectionChanged = [this](int, const DropdownItem&) { FillTopicModelChoices(); };
    form->AddChild(Theme::Field("saT.capRow", Tr("Service"), tCapability_, false));

    form->AddChild(Theme::Caption("saT.models.caption", Tr("The models offered, best first. * marks the default.")));
    topicModelsTable_ = Theme::MakeTable("saT.models", {{Tr("Model"), 260}, {Tr("State"), 100}, {Tr("Default"), 80}}, 130);
    form->AddChild(topicModelsTable_.view);
    auto modelActions = Theme::Row("saT.modelActions", Theme::kGap);
    tAddModel_ = Choice("saT.addModel", 260);
    auto addModel = Theme::Button("saT.add", Tr("Add"), 70);
    addModel->SetOnClick([this]() { AddTopicModel(); });
    auto up = Theme::Button("saT.up", Tr("Up"), 60);
    up->SetOnClick([this]() { MoveTopicModel(-1); });
    auto down = Theme::Button("saT.down", Tr("Down"), 70);
    down->SetOnClick([this]() { MoveTopicModel(1); });
    auto makeDefault = Theme::Button("saT.default", Tr("Make default"), 110);
    makeDefault->SetOnClick([this]() { MakeTopicDefault(); });
    auto removeModel = Theme::Button("saT.remove", Tr("Remove"), 90);
    removeModel->SetOnClick([this]() { RemoveTopicModel(); });
    modelActions->AddChild(tAddModel_);
    modelActions->AddChild(addModel);
    modelActions->AddChild(up);
    modelActions->AddChild(down);
    modelActions->AddChild(makeDefault);
    modelActions->AddChild(removeModel);
    form->AddChild(modelActions);

    auto actions = Theme::Row("saT.actions", Theme::kGap);
    auto save = Theme::Button("saT.save", Tr("Save topic"), 110, ButtonKind::Primary);
    save->SetOnClick([this]() { SaveTopic(); });
    deleteTopicButton_ = Theme::Button("saT.delete", Tr("Delete topic"), 120, ButtonKind::Danger);
    deleteTopicButton_->SetOnClick([this]() { DeleteTopic(); });
    actions->AddChild(save);
    actions->AddChild(deleteTopicButton_);
    form->AddChild(actions);
    body->AddChild(form);
    NewTopic();
}

void ServerAdminWindow::ReloadTopics(const std::string& select) {
    if (!Connected()) return;
    // The topics and the models (for the names and the choice list).
    Run(Tr("Loading the topics"), [](const ModelServerClient& c) {
        Reply topics = c.ListTopics();
        if (!topics.ok) return topics;
        Reply models = c.ListModels();
        if (!models.ok) return models;
        topics.body.Set("models", models.body["models"]);
        return topics;
    }, [this, select](const Reply& r) {
        if (!Check(r)) return;
        topics_ = r.body["topics"].GetElements();
        models_ = r.body["models"].GetElements();
        FillModelsTable();
        std::vector<std::vector<std::string>> rows;
        for (const JSONValue& t : topics_) {
            std::string def;
            for (const JSONValue& m : t["models"].GetElements()) {
                if (m["default"].GetBoolean()) def = m["id"].GetString();
            }
            rows.push_back({t["id"].GetString(), t["name"].GetString(), CapabilityLabel(t["capability"].GetString()), def,
                            std::to_string(t["models"].GetSize())});
        }
        topicsTable_.SetRows(rows);
        SetStatus(TrN("{n} topic.", "{n} topics.", static_cast<long long>(topics_.size())));
        for (size_t i = 0; i < topics_.size(); ++i) {
            if (!select.empty() && topics_[i]["id"].GetString() == select) {
                topicsTable_.Select(static_cast<int>(i));
                ShowTopic(static_cast<int>(i));
            }
        }
        FillTopicModelChoices();
        FillTopicModels();
    });
}

void ServerAdminWindow::NewTopic() {
    editingTopic_.clear();
    tId_->SetReadOnly(false);
    tId_->SetText("");
    tName_->SetText("");
    tDescription_->SetText("");
    tCapability_->SetSelectedIndex(0, false);
    topicEntries_.clear();
    FillTopicModels();
    FillTopicModelChoices();
    deleteTopicButton_->SetDisabled(true);
}

void ServerAdminWindow::ShowTopic(int row) {
    if (row < 0 || row >= static_cast<int>(topics_.size())) return;
    const JSONValue& t = topics_[static_cast<size_t>(row)];
    editingTopic_ = t["id"].GetString();
    tId_->SetText(editingTopic_);
    tId_->SetReadOnly(true);
    tName_->SetText(t["name"].GetString());
    tDescription_->SetText(t["description"].GetString());
    for (int i = 0; i < tCapability_->GetItemCount(); ++i) {
        if (tCapability_->GetItem(i)->value == t["capability"].GetString()) tCapability_->SetSelectedIndex(i, false);
    }
    topicEntries_.clear();
    for (const JSONValue& m : t["models"].GetElements()) topicEntries_.push_back({m["id"].GetString(), m["default"].GetBoolean()});
    FillTopicModels();
    FillTopicModelChoices();
    deleteTopicButton_->SetDisabled(!IsSuper());
}

void ServerAdminWindow::FillTopicModels() {
    std::vector<std::vector<std::string>> rows;
    for (const TopicEntry& e : topicEntries_) {
        std::string name = e.id, state = Tr("not on the server");
        for (const JSONValue& m : models_) {
            if (m["id"].GetString() == e.id) {
                name = m["displayName"].GetString() + " (" + e.id + ")";
                state = m["status"].GetString();
            }
        }
        rows.push_back({name, state, e.isDefault ? "*" : ""});
    }
    topicModelsTable_.SetRows(rows);
}

void ServerAdminWindow::FillTopicModelChoices() {
    const DropdownItem* cap = tCapability_->GetSelectedItem();
    const std::string capability = cap ? cap->value : std::string();
    tAddModel_->ClearItems();
    for (const JSONValue& m : models_) {
        if (m["capability"].GetString() != capability || m["status"].GetString() == "withdrawn") continue;
        tAddModel_->AddItem(m["displayName"].GetString() + " (" + m["status"].GetString() + ")", m["id"].GetString());
    }
    if (tAddModel_->GetItemCount() > 0) tAddModel_->SetSelectedIndex(0, false);
}

void ServerAdminWindow::AddTopicModel() {
    const DropdownItem* item = tAddModel_->GetSelectedItem();
    if (!item) return;
    for (const TopicEntry& e : topicEntries_) {
        if (e.id == item->value) return;
    }
    topicEntries_.push_back({item->value, topicEntries_.empty()});
    FillTopicModels();
}

void ServerAdminWindow::RemoveTopicModel() {
    const int row = topicModelsTable_.SelectedRow();
    if (row < 0 || row >= static_cast<int>(topicEntries_.size())) return;
    const bool wasDefault = topicEntries_[static_cast<size_t>(row)].isDefault;
    topicEntries_.erase(topicEntries_.begin() + row);
    if (wasDefault && !topicEntries_.empty()) topicEntries_.front().isDefault = true;
    FillTopicModels();
}

void ServerAdminWindow::MoveTopicModel(int delta) {
    const int row = topicModelsTable_.SelectedRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= static_cast<int>(topicEntries_.size())) return;
    std::swap(topicEntries_[static_cast<size_t>(row)], topicEntries_[static_cast<size_t>(to)]);
    FillTopicModels();
    topicModelsTable_.Select(to);
}

void ServerAdminWindow::MakeTopicDefault() {
    const int row = topicModelsTable_.SelectedRow();
    if (row < 0 || row >= static_cast<int>(topicEntries_.size())) return;
    for (size_t i = 0; i < topicEntries_.size(); ++i) topicEntries_[i].isDefault = static_cast<int>(i) == row;
    FillTopicModels();
    topicModelsTable_.Select(row);
}

void ServerAdminWindow::SaveTopic() {
    if (!Connected()) { SetStatus(Tr("Connect first."), true); return; }
    const std::string id = tId_->GetText();
    if (id.empty()) { SetStatus(Tr("The topic needs an id."), true); return; }
    JSONValue topic = JSONValue::MakeObject();
    topic.Set("name", tName_->GetText().empty() ? id : tName_->GetText());
    topic.Set("description", tDescription_->GetText());
    const DropdownItem* cap = tCapability_->GetSelectedItem();
    topic.Set("capability", cap ? cap->value : std::string("textllm"));
    JSONValue models = JSONValue::MakeArray();
    for (const TopicEntry& e : topicEntries_) {
        JSONValue entry = JSONValue::MakeObject();
        entry.Set("id", e.id);
        if (e.isDefault) entry.Set("default", true);
        models.Append(entry);
    }
    topic.Set("models", models);
    Run(TrF("Saving the topic {0}", id), [id, topic](const ModelServerClient& c) { return c.PutTopic(id, topic); },
        [this, id](const Reply& r) {
            if (!Check(r)) return;
            SetStatus(TrF("Saved the topic {0}: clients take it at their next refresh.", id));
            editingTopic_ = id;
            ReloadTopics(id);
        });
}

void ServerAdminWindow::DeleteTopic() {
    if (editingTopic_.empty() || !IsSuper()) return;
    const std::string id = editingTopic_;
    UltraCanvasDialogManager::ShowConfirmation(TrF("Delete the topic {0}? Clients fall back to their own choice.", id),
        Tr("Delete topic"), [this, id, alive = alive_](bool confirmed) {
            if (!confirmed || !alive->load()) return;
            Run(TrF("Deleting the topic {0}", id), [id](const ModelServerClient& c) { return c.DeleteTopic(id); },
                [this, id](const Reply& r) {
                    if (!Check(r)) return;
                    SetStatus(TrF("Deleted the topic {0}.", id));
                    NewTopic();
                    ReloadTopics();
                });
        }, window_.get());
}

// ===========================================================================
// Engines
// ===========================================================================
void ServerAdminWindow::BuildEnginesPage() {
    auto body = BuildPage(kEngines, Tr("Engines"),
                          Tr("The inference engines clients download, one bundle per platform and accelerator, as the "
                          "gallery index carries them. Only a super administrator changes them."));
    auto top = Theme::Row("saE.top", Theme::kGap);
    auto reload = Theme::Button("saE.reload", Tr("Reload"), 90);
    reload->SetOnClick([this]() { ReloadEngines(); });
    top->AddChild(reload);
    body->AddChild(top);
    enginesTable_ = Theme::MakeTable("saE.table", {{Tr("Id"), 160}, {Tr("Version"), 110}, {Tr("Licence"), 90}, {Tr("Serves"), 200}, {Tr("Bundles"), 80}}, 140);
    enginesTable_.view->onSelectionChanged = [this](const std::vector<int>&) { ShowEngine(enginesTable_.SelectedRow()); };
    body->AddChild(enginesTable_.view);

    auto form = Theme::Surface("saE.form", Theme::kInnerGap);
    eId_ = Input("saE.id", Tr("llama-server"));
    form->AddChild(Theme::Field("saE.idRow", Tr("Engine id"), eId_));
    form->AddChild(Theme::Caption("saE.caption",
        Tr("{\"version\", \"license\", \"executable\", \"serves\": [\"textllm\", ...], \"defaultArgs\": [...], "
        "\"bundles\": [{\"platform\": \"linux-x64\", \"accel\": \"vulkan\", \"url\": \"https://...\", \"sha256\": \"...\", "
        "\"size\": 30497889, \"optional\": false, \"minOs\": \"\"}]}")));
    eJson_ = Theme::Pin(std::make_shared<UltraCanvasTextArea>("saE.json", 0.0f, 260.0f));
    eJson_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(260));
    eJson_->SetShowLineNumbers(true);
    eJson_->SetHighlightSyntax(false);
    form->AddChild(eJson_);
    auto actions = Theme::Row("saE.actions", Theme::kGap);
    auto save = Theme::Button("saE.save", Tr("Save engine"), 120, ButtonKind::Primary);
    save->SetOnClick([this]() { SaveEngine(); });
    auto del = Theme::Button("saE.delete", Tr("Delete engine"), 130, ButtonKind::Danger);
    del->SetOnClick([this]() { DeleteEngine(); });
    actions->AddChild(save);
    actions->AddChild(del);
    form->AddChild(actions);
    body->AddChild(form);
}

void ServerAdminWindow::ReloadEngines() {
    if (!Connected()) return;
    Run(Tr("Loading the engines"), [](const ModelServerClient& c) { return c.ListEngines(); }, [this](const Reply& r) {
        if (!Check(r)) return;
        engines_ = r.body["engines"].GetElements();
        std::vector<std::vector<std::string>> rows;
        for (const JSONValue& e : engines_) {
            rows.push_back({e["id"].GetString(), e["version"].GetString(), e["license"].GetString(),
                            JoinStrings(e["serves"], ", "), std::to_string(e["bundles"].GetSize())});
        }
        enginesTable_.SetRows(rows);
        SetStatus(TrN("{n} engine.", "{n} engines.", static_cast<long long>(engines_.size())));
    });
}

void ServerAdminWindow::ShowEngine(int row) {
    if (row < 0 || row >= static_cast<int>(engines_.size())) return;
    JSONValue e = engines_[static_cast<size_t>(row)];
    eId_->SetText(e["id"].GetString());
    e.Remove("id");
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    eJson_->SetText(JSON::Serialize(e, pretty), false);
}

void ServerAdminWindow::SaveEngine() {
    if (!IsSuper()) { SetStatus(Tr("Only a super administrator changes the engines."), true); return; }
    const std::string id = eId_->GetText();
    JSONParseResult parsed;
    const JSONValue engine = JSON::Parse(eJson_->GetText(), &parsed);
    if (id.empty() || !parsed.success || !engine.IsObject()) {
        SetStatus(id.empty() ? Tr("The engine needs an id.")
                             : TrF("The engine is not a JSON object: {0} (line {1})", parsed.errorMessage,
                                   static_cast<long long>(parsed.errorLine)), true);
        return;
    }
    Run(TrF("Saving the engine {0}", id), [id, engine](const ModelServerClient& c) { return c.PutEngine(id, engine); },
        [this, id](const Reply& r) {
            if (!Check(r)) return;
            SetStatus(TrF("Saved the engine {0}.", id));
            ReloadEngines();
        });
}

void ServerAdminWindow::DeleteEngine() {
    if (!IsSuper()) { SetStatus(Tr("Only a super administrator changes the engines."), true); return; }
    const std::string id = eId_->GetText();
    if (id.empty()) return;
    UltraCanvasDialogManager::ShowConfirmation(TrF("Delete the engine {0} from the gallery?", id), Tr("Delete engine"),
        [this, id, alive = alive_](bool confirmed) {
            if (!confirmed || !alive->load()) return;
            Run(TrF("Deleting the engine {0}", id), [id](const ModelServerClient& c) { return c.DeleteEngine(id); },
                [this, id](const Reply& r) {
                    if (!Check(r)) return;
                    SetStatus(TrF("Deleted the engine {0}.", id));
                    eId_->SetText("");
                    eJson_->SetText("", false);
                    ReloadEngines();
                });
        }, window_.get());
}

// ===========================================================================
// Uploads
// ===========================================================================
void ServerAdminWindow::BuildUploadsPage() {
    auto body = BuildPage(kUploads, Tr("Uploads"),
                          Tr("The model files stored on the server. A file no model uses any more (replaced, or its "
                          "model deleted) takes disk until it is deleted here."));
    auto top = Theme::Row("saU.top", Theme::kGap);
    auto reload = Theme::Button("saU.reload", Tr("Reload"), 90);
    reload->SetOnClick([this]() { ReloadUploads(); });
    auto prune = Theme::Button("saU.prune", Tr("Delete unused"), 130, ButtonKind::Danger);
    prune->SetOnClick([this]() { PruneUploads(); });
    top->AddChild(reload);
    top->AddChild(prune);
    body->AddChild(top);
    uploadsInfo_ = Theme::Caption("saU.info", "");
    body->AddChild(uploadsInfo_);
    uploadsTable_ = Theme::MakeTable("saU.table", {{Tr("File"), 220}, {Tr("Size"), 90}, {Tr("Uploaded"), 160}, {Tr("By"), 110}, {Tr("Used by"), 70}, {Tr("SHA-256"), 180}}, 360);
    body->AddChild(uploadsTable_.view);
}

void ServerAdminWindow::ReloadUploads() {
    if (!Connected()) return;
    Run(Tr("Loading the uploads"), [](const ModelServerClient& c) { return c.ListUploads(); }, [this](const Reply& r) {
        if (!Check(r)) return;
        std::vector<std::vector<std::string>> rows;
        int64_t total = 0, unused = 0;
        for (const JSONValue& b : r.body["blobs"].GetElements()) {
            total += b["size"].GetInteger();
            if (b["used"].GetInteger() == 0) unused += b["size"].GetInteger();
            rows.push_back({b["original_name"].GetString(), Bytes(b["size"].GetInteger()), b["uploaded_at"].GetString(),
                            b["uploaded_by"].GetString(), std::to_string(b["used"].GetInteger()),
                            b["sha256"].GetString().substr(0, 20) + "…"});
        }
        uploadsTable_.SetRows(rows);
        uploadsInfo_->SetText(TrF("{0} files, {1}; unused {2}.", rows.size(), Bytes(total), Bytes(unused)));
        SetStatus(Tr("Uploads loaded."));
    });
}

void ServerAdminWindow::PruneUploads() {
    if (!IsSuper()) { SetStatus(Tr("Only a super administrator deletes uploads."), true); return; }
    UltraCanvasDialogManager::ShowConfirmation(Tr("Delete every uploaded file no model uses?"), Tr("Delete unused uploads"),
        [this, alive = alive_](bool confirmed) {
            if (!confirmed || !alive->load()) return;
            Run(Tr("Deleting the unused uploads"), [](const ModelServerClient& c) { return c.PruneUploads(); },
                [this](const Reply& r) {
                    if (!Check(r)) return;
                    SetStatus(TrF("Deleted {0} files, {1}.", r.body["deleted"].GetInteger(),
                                  Bytes(r.body["bytes"].GetInteger())));
                    ReloadUploads();
                });
        }, window_.get());
}

// ===========================================================================
// Administrators
// ===========================================================================
void ServerAdminWindow::BuildAdminsPage() {
    auto body = BuildPage(kAdmins, Tr("Administrators"),
                          Tr("Accounts that may sign in besides the super administrator's token. An editor manages "
                          "models, files and topics; a super administrator also the engines, the accounts and deletions."));
    adminsTable_ = Theme::MakeTable("saA.table", {{Tr("Name"), 180}, {Tr("Role"), 90}, {Tr("Created"), 160}, {Tr("By"), 110}, {Tr("Last used"), 160}}, 200);
    adminsTable_.view->onSelectionChanged = [this](const std::vector<int>&) {
        const int row = adminsTable_.SelectedRow();
        if (row >= 0 && row < static_cast<int>(admins_.size())) aName_->SetText(admins_[static_cast<size_t>(row)]["name"].GetString());
    };
    body->AddChild(adminsTable_.view);
    auto form = Theme::Surface("saA.form", Theme::kInnerGap);
    aName_ = Input("saA.name", "alice");
    form->AddChild(Theme::Field("saA.nameRow", Tr("Account"), aName_));
    aRole_ = Choice("saA.role", 220);
    aRole_->AddItem(Tr("Editor"), "editor");
    aRole_->AddItem(Tr("Super administrator"), "super");
    aRole_->SetSelectedIndex(0, false);
    form->AddChild(Theme::Field("saA.roleRow", Tr("Role"), aRole_, false));
    auto actions = Theme::Row("saA.actions", Theme::kGap);
    auto put = Theme::Button("saA.put", Tr("Create, or give a new token"), 220, ButtonKind::Primary);
    put->SetOnClick([this]() { PutAdmin(); });
    auto del = Theme::Button("saA.delete", Tr("Delete account"), 130, ButtonKind::Danger);
    del->SetOnClick([this]() { DeleteAdmin(); });
    actions->AddChild(put);
    actions->AddChild(del);
    form->AddChild(actions);
    aToken_ = Input("saA.token", Tr("The new token appears here once"));
    aToken_->SetReadOnly(true);
    form->AddChild(Theme::Field("saA.tokenRow", Tr("New token"), aToken_));
    form->AddChild(Theme::Caption("saA.tokenNote",
        Tr("Copy the token and hand it to the person now: the server keeps only its hash and never shows it again. "
        "A new token replaces the old one at once.")));
    body->AddChild(form);
}

void ServerAdminWindow::ReloadAdmins() {
    if (!Connected() || !IsSuper()) return;
    Run(Tr("Loading the accounts"), [](const ModelServerClient& c) { return c.ListAdmins(); }, [this](const Reply& r) {
        if (!Check(r)) return;
        admins_ = r.body["admins"].GetElements();
        std::vector<std::vector<std::string>> rows;
        for (const JSONValue& a : admins_) {
            rows.push_back({a["name"].GetString(), a["role"].GetString(), a["created_at"].GetString(),
                            a["created_by"].GetString(), a["last_used_at"].GetString()});
        }
        adminsTable_.SetRows(rows);
        SetStatus(TrN("{n} account.", "{n} accounts.", static_cast<long long>(admins_.size())));
    });
}

void ServerAdminWindow::PutAdmin() {
    if (!IsSuper()) { SetStatus(Tr("Only a super administrator manages accounts."), true); return; }
    const std::string name = aName_->GetText();
    const DropdownItem* role = aRole_->GetSelectedItem();
    if (name.empty() || !role) { SetStatus(Tr("Enter the account's name."), true); return; }
    const std::string roleId = role->value;
    Run(TrF("Saving the account {0}", name), [name, roleId](const ModelServerClient& c) { return c.PutAdmin(name, roleId); },
        [this, name](const Reply& r) {
            if (!Check(r)) return;
            aToken_->SetText(r.body["token"].GetString());
            SetStatus(TrF("The account {0} has a new token: copy it now.", name));
            ReloadAdmins();
        });
}

void ServerAdminWindow::DeleteAdmin() {
    if (!IsSuper()) { SetStatus(Tr("Only a super administrator manages accounts."), true); return; }
    const std::string name = aName_->GetText();
    if (name.empty()) return;
    UltraCanvasDialogManager::ShowConfirmation(TrF("Delete the account {0}? Its token stops working at once.", name),
        Tr("Delete account"), [this, name, alive = alive_](bool confirmed) {
            if (!confirmed || !alive->load()) return;
            Run(TrF("Deleting the account {0}", name), [name](const ModelServerClient& c) { return c.DeleteAdmin(name); },
                [this, name](const Reply& r) {
                    if (!Check(r)) return;
                    SetStatus(TrF("Deleted the account {0}.", name));
                    aName_->SetText("");
                    ReloadAdmins();
                });
        }, window_.get());
}

// ===========================================================================
// Audit log
// ===========================================================================
void ServerAdminWindow::BuildAuditPage() {
    auto body = BuildPage(kAudit, Tr("Audit log"), Tr("Who changed what on the server, oldest first."));
    auto top = Theme::Row("saL.top", Theme::kGap);
    auto reload = Theme::Button("saL.reload", Tr("Reload"), 90);
    reload->SetOnClick([this]() { ReloadAudit(); });
    top->AddChild(reload);
    body->AddChild(top);
    auditTable_ = Theme::MakeTable("saL.table", {{"#", 50}, {Tr("When"), 160}, {Tr("Who"), 110}, {Tr("What"), 120}, {Tr("On"), 200}, {Tr("Detail"), 260}}, 460);
    body->AddChild(auditTable_.view);
}

void ServerAdminWindow::ReloadAudit() {
    if (!Connected()) return;
    Run(Tr("Loading the audit log"), [](const ModelServerClient& c) { return c.Audit(0); }, [this](const Reply& r) {
        if (!Check(r)) return;
        std::vector<std::vector<std::string>> rows;
        for (const JSONValue& e : r.body["entries"].GetElements()) {
            rows.push_back({std::to_string(e["seq"].GetInteger()), e["at"].GetString(), e["admin"].GetString(),
                            e["action"].GetString(), e["target"].GetString(), e["detail"].GetString()});
        }
        auditTable_.SetRows(rows);
        SetStatus(TrN("{n} entry.", "{n} entries.", static_cast<long long>(rows.size())));
    });
}

} // namespace UltraAIServerAdmin::Ui
