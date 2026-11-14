// Apps/UltraStore/web/WebStore.h
// The UltraStore web store: the shop of the design canvas
// (Docs/UltraStore/design: Main, AppDetail, Categories) as an UltraCanvas
// application a browser runs, on the UltraCanvas runtime (a side module of
// this code only), or on the desktop over a publish folder. It shows the
// catalog the admin backend publishes - Discover, the categories, an app's
// page - and hands installing, trying and buying to the native store
// through its ultrastore:// link, since a licence and an install have to
// land in the store that keeps them (Docs/UltraStore/WebstoreBrief.md).
//
// The catalog comes from the site's own origin and is trusted the way the
// native store trusts it: the signature must check against a key built in
// or announced by a catalog accepted before (trusted_keys, kept with the
// highest sequence seen, so a mirror cannot roll the visitor back). A
// catalog that does not verify is not shown; the page says so.
//
// Pictures are never resized here: the catalog names each picture at the
// web store's sizes and formats (icon_variants, screenshot_variants), and
// the store picks the format the browser decodes and the smallest width at
// or above the slot times the device pixel ratio, falling back to the
// original.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "WebPlatform.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraStore::Web {

class WebStore {
public:
    WebStore(UltraCanvas::UltraCanvasApplication& app, std::shared_ptr<UltraCanvas::UltraCanvasWindow> window,
             std::vector<std::vector<uint8_t>> builtInKeys);

    // Builds the shell, fetches and verifies the catalog, shows the page the
    // address names.
    void Start();

private:
    // ----- the catalog -----
    void LoadCatalog();
    void CatalogArrived(const std::string& bytes, const std::string& signatureText);
    void Accept(Catalog catalog, const std::string& bytes);
    void Refuse(const std::string& title, const std::string& text);
    std::vector<std::vector<uint8_t>> KeysTrustedNow() const;
    void ReadTrustState();
    void WriteTrustState();

    // ----- the pages -----
    // "" Discover, "categories", "category/<id>", "app/<id>".
    void GoTo(const std::string& page, bool addHistory = true);
    void ShowPage(const std::string& page);
    void Rebuild();
    void BuildShell();
    void BuildHeader();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildFooter();
    void BuildDiscover();
    void BuildResults();
    void BuildCategories(const std::string& selected);
    void BuildApp(const Component& app);
    void BuildMessage(const std::string& title, const std::string& text, bool offerStore);

    // ----- the pieces -----
    struct Band {
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> outer, inner;
    };
    Band AddBand(const std::string& id, const UltraCanvas::Color& background, float paddingTop, float paddingBottom);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Grid(const std::string& id, float gap = 20.0f);
    std::shared_ptr<UltraCanvas::UltraCanvasCard> AppCard(const Component& app, const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasCard> AppRow(const Component& app, int number, const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasCard> AppMini(const Component& app, const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasCard> Hero(const Component& app);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> SectionHead(const std::string& id, const std::string& title,
                                                                   const std::string& subtitle, const std::string& linkText = {},
                                                                   std::function<void()> onLink = {});
    std::shared_ptr<UltraCanvas::UltraCanvasButton> ActionButton(const Component& app, const std::string& id, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Badge(const std::string& id, const std::string& text,
                                                             const UltraCanvas::Color& fill, const UltraCanvas::Color& ink);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> InfoLine(const std::string& id, const std::string& key, const std::string& value);
    std::shared_ptr<UltraCanvas::UltraCanvasButton> LinkButton(const std::string& id, const std::string& text, const std::string& url);

    // A picture of the catalog in a slot of w x h CSS pixels: the variant
    // chosen for the browser, the original without one; loaded once and
    // shared between the elements that show it.
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> Picture(const std::string& id, const std::string& original,
                                                                  const std::vector<MediaVariant>& variants,
                                                                  float w, float h, float radius);
    std::string PickUrl(const std::string& original, const std::vector<MediaVariant>& variants, float slotWidth) const;
    void Deliver(const std::string& url, const std::shared_ptr<UltraCanvas::UCImage>& image);

    // ----- texts -----
    std::string Summary(const Component& app) const;
    std::string Description(const Component& app) const;
    std::string PublisherName(const Component& app) const;
    std::string PriceText(const Component& app) const;
    std::string ReleaseLine(const Component& app) const;
    std::string RunsOn(const Component& app) const;
    std::string CategoryOf(const Component& app) const;
    std::string DownloadUrl() const { return downloadUrl_; }
    void OpenInStore(const Component& app);

    // ----- the lists -----
    std::vector<const Component*> Listed() const;                       // every app the store shows, by name
    std::vector<const Component*> Featured() const;                     // the admin's picks, the newest first
    std::vector<const Component*> Newest(size_t count) const;           // by the newest release's date
    std::vector<const Component*> Free(size_t count) const;
    std::vector<const Component*> InCategory(const std::string& id) const;
    std::vector<const Component*> Matching(const std::string& search) const;
    std::vector<std::pair<std::string, size_t>> CategoriesWithCounts() const;

    UltraCanvas::UltraCanvasApplication& app_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::vector<std::vector<uint8_t>> builtInKeys_;

    Catalog catalog_;
    bool loaded_ = false;
    std::string lang_ = "en";
    std::string downloadUrl_;     // where the native store is downloaded (the page's data-store-download)
    std::string contactUrl_;      // the store's contact (data-store-contact)

    struct TrustState {
        uint64_t sequence = 0;
        std::vector<std::string> announcedKeys;   // hex
    } trust_;

    std::string page_;            // the page shown
    std::string search_;          // the header's search text: Discover shows the results while it holds text
    std::string priceFilter_;     // Categories: "" all, "free", "paid"
    std::string sort_ = "newest"; // Categories: "newest", "name", "price"
    std::string refusedTitle_, refusedText_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;     // the scrolling page
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> header_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> body_;     // what the page changes
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> search_input_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasButton>> navButtons_;

    std::map<std::string, std::shared_ptr<UltraCanvas::UCImage>> pictures_;                               // url -> decoded (nullptr: failed)
    std::map<std::string, std::vector<std::weak_ptr<UltraCanvas::UltraCanvasImageElement>>> waiting_;   // url -> the elements that show it
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);                                               // callbacks after the store is gone
    int ids_ = 0;
};

} // namespace UltraStore::Web
