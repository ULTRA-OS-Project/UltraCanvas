// Apps/UltraOffice/UltraOfficeTemplates.cpp
// The templates UltraOffice offers and the Start tab's gallery of them.
//
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeTemplates.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasCard.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasFilePreviews.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

constexpr int kTilesPerRow = 4;
constexpr int kTileGap = 12;
constexpr int kTileHeight = 200;
constexpr int kPictureHeight = 120;

// The shipped templates by file stem, with the names a person reads.
std::string ShippedName(const std::string& stem) {
    if (stem == "letter") return Tr("Letter");
    if (stem == "report") return Tr("Report");
    if (stem == "memo") return Tr("Memo");
    if (stem == "budget") return Tr("Budget");
    if (stem == "invoice") return Tr("Invoice");
    if (stem == "list") return Tr("List");
    if (stem == "plain-deck") return Tr("Plain");
    if (stem == "dark-deck") return Tr("Dark");
    return stem;
}

std::string KindLabel(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return Tr("Spreadsheet");
        case OfficeKind::Presentation: return Tr("Presentation");
        default: return Tr("Document");
    }
}

const char* KindIcon(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return "office/spreadsheet.svg";
        case OfficeKind::Presentation: return "office/presentation.svg";
        default: return "office/document.svg";
    }
}

int KindOrder(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Document: return 0;
        case OfficeKind::Spreadsheet: return 1;
        default: return 2;
    }
}

void CollectFrom(const std::filesystem::path& folder, bool own, std::vector<OfficeTemplate>& out) {
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec)) return;
    std::vector<OfficeTemplate> found;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string path = PathToUtf8(entry.path());
        if (!UltraCanvasFileLoader::IsOfficeTemplateExtension(path)) continue;
        OfficeTemplate t;
        t.path = path;
        t.kind = UltraCanvasFileLoader::DetectOfficeDocumentKind(path);
        if (t.kind == OfficeKind::Unknown) continue;
        const std::string stem = PathToUtf8(entry.path().stem());
        t.name = own ? stem : ShippedName(stem);
        t.own = own;
        found.push_back(std::move(t));
    }
    std::sort(found.begin(), found.end(), [](const OfficeTemplate& a, const OfficeTemplate& b) {
        return std::make_tuple(KindOrder(a.kind), a.name) < std::make_tuple(KindOrder(b.kind), b.name);
    });
    out.insert(out.end(), found.begin(), found.end());
}

// Pictures drawn once a run, by path and the file's time: the Start tab is
// built again after a restore or a "Save as template".
struct CachedPicture {
    std::filesystem::file_time_type written;
    std::shared_ptr<UCPixmap> picture;   // null: none could be drawn
};
std::map<std::string, CachedPicture>& PictureCache() {
    static std::map<std::string, CachedPicture> cache;
    return cache;
}

std::shared_ptr<UCPixmap> PictureOf(const std::string& path, int width, int height, bool& known) {
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(PathFromUtf8(path), ec);
    auto& cache = PictureCache();
    const auto hit = cache.find(path);
    if (hit != cache.end() && hit->second.written == written) { known = true; return hit->second.picture; }
    known = false;
    FileThumbnailRequest request;
    request.width = width;
    request.height = height;
    std::shared_ptr<UCPixmap> picture = RenderFileThumbnailFromProvider(path, request);
    cache[path] = {written, picture};
    return picture;
}

// Draws the pictures one per tick, so the window stays responsive while a
// dozen templates are read.
void DrawPicturesLater(std::vector<std::pair<std::string, std::weak_ptr<UltraCanvasImageElement>>> pending,
                       size_t next, int width, int height) {
    if (next >= pending.size()) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    app->StartTimer(1, false, [pending = std::move(pending), next, width, height](TimerId) mutable {
        if (auto image = pending[next].second.lock()) {
            bool known = false;
            if (auto picture = PictureOf(pending[next].first, width, height, known)) image->LoadFromPixmap(picture);
        }
        DrawPicturesLater(std::move(pending), next + 1, width, height);
    });
}

int TileWidth(int width) { return (width - (kTilesPerRow - 1) * kTileGap) / kTilesPerRow; }

} // namespace

std::filesystem::path ShippedTemplatesFolder() {
    return PathFromUtf8(GetResourcesDir()) / "media" / "templates" / "office";
}

std::vector<OfficeTemplate> ListTemplates(const std::filesystem::path& ownFolder) {
    std::vector<OfficeTemplate> out;
    CollectFrom(ShippedTemplatesFolder(), false, out);
    CollectFrom(ownFolder, true, out);
    return out;
}

std::string TemplateExtension(OfficeKind kind, SaveFormat format) {
    const bool odf = format == SaveFormat::OpenDocument;
    switch (kind) {
        case OfficeKind::Spreadsheet: return odf ? "ots" : "xltx";
        case OfficeKind::Presentation: return odf ? "otp" : "potx";
        default: return odf ? "ott" : "dotx";
    }
}

int TemplateGalleryHeight(size_t count, int width) {
    (void)width;
    if (count == 0) return 0;
    const int rows = static_cast<int>((count + kTilesPerRow - 1) / kTilesPerRow);
    return rows * kTileHeight + (rows - 1) * kTileGap;
}

std::shared_ptr<UltraCanvasContainer> BuildTemplateGallery(const std::vector<OfficeTemplate>& templates, int width,
                                                           std::function<void(const std::string& path)> onPick) {
    auto grid = CreateContainer("office-templates", 0, 0, static_cast<float>(width),
                                static_cast<float>(TemplateGalleryHeight(templates.size(), width)));
    grid->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(kTileGap);
    const int tileWidth = TileWidth(width);
    const int pictureWidth = tileWidth - 24;   // inside the card's padding
    std::vector<std::pair<std::string, std::weak_ptr<UltraCanvasImageElement>>> pending;
    int index = 0;
    for (const OfficeTemplate& t : templates) {
        const std::string id = "office-template-" + std::to_string(index++);
        auto card = CreateCard(id, t.name, t.own ? Tr("Your template") : KindLabel(t.kind));
        card->SetElementSize(CSSLayout::Dimension::Px(static_cast<float>(tileWidth)),
                             CSSLayout::Dimension::Px(static_cast<float>(kTileHeight)));
        card->SetTooltip(t.path);
        auto picture = std::make_shared<UltraCanvasImageElement>(id + "-picture", static_cast<float>(pictureWidth),
                                                                 static_cast<float>(kPictureHeight));
        picture->SetFitMode(ImageFitMode::Contain);
        picture->SetAccessibleName(TrF("A picture of {0}", t.name));
        bool known = false;
        std::shared_ptr<UCPixmap> cached;
        if (PictureCache().count(t.path)) cached = PictureOf(t.path, pictureWidth, kPictureHeight, known);
        if (known && cached) {
            picture->LoadFromPixmap(cached);
        } else {
            // The kind's sign until the picture is drawn (or for good, when
            // none can be).
            picture->LoadFromFile(IconPath(KindIcon(t.kind)));
            if (!known) pending.emplace_back(t.path, picture);
        }
        card->AddContent(picture);
        const std::string path = t.path;
        card->onClick = [onPick, path]() { if (onPick) onPick(path); };
        // The picture takes the press for itself: a click on it picks the
        // template as one on the rest of the card does.
        picture->SetClickable(true);
        picture->onClick = card->onClick;
        grid->AddChild(card);
    }
    DrawPicturesLater(std::move(pending), 0, pictureWidth, kPictureHeight);
    return grid;
}

} // namespace UltraOffice
