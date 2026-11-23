// Apps/ChatCube/ui/ChatCubeEmoji.cpp
// See the header.
// Version: 0.1.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeEmoji.h"

#include "ChatCubeControls.h"
#include "ChatCubeTheme.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int kEmojiCell = 34;        // one smiley's square in the grid
constexpr int kEmojiColumns = 8;
constexpr int kStickerCell = 72;      // one sticker's square
constexpr int kStickerColumns = 4;
constexpr int kGridPadding = 8;
constexpr int kGridTitle = 22;        // the set's name over the grid

std::vector<std::string> Split(const char* list) {
    // The emoji are separated by spaces: one may be several code points
    // (a heart with its variation selector), which a split by code point
    // would break apart
    std::vector<std::string> out;
    std::string current;
    for (const char* p = list; *p; ++p) {
        if (*p == ' ') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += *p;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::vector<EmojiSet> MakeSets() {
    std::vector<EmojiSet> sets;
    sets.push_back({"smileys", Tr("Smileys"), "\xF0\x9F\x98\x80", false,
                    Split("😀 😃 😄 😁 😆 😅 😂 🤣 😊 😇 🙂 🙃 😉 😌 😍 🥰 😘 😋 😛 😜 🤪 😎 🤩 🥳 "
                          "😏 😒 😞 😔 😟 😕 🙁 😢 😭 😤 😠 😡 🤯 😳 🥺 😱 😴 🤔 🤗 🤫 🙄 😬 🤐 😷")});
    sets.push_back({"gestures", Tr("Hands"), "\xF0\x9F\x91\x8D", false,
                    Split("👍 👎 👌 ✌️ 🤞 🤟 🤘 🤙 👋 🙌 👏 🙏 💪 👊 ✊ 🤝 👆 👇 👉 👈 ☝️ ✋ 🤚 🖐️")});
    sets.push_back({"hearts", Tr("Hearts"), "\xE2\x9D\xA4\xEF\xB8\x8F", false,
                    Split("❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❣️ 💕 💞 💓 💗 💖 💘 💝 💟 😍 🥰 😘 💋 🌹")});
    sets.push_back({"animals", Tr("Animals"), "\xF0\x9F\x90\xB1", false,
                    Split("🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯 🦁 🐮 🐷 🐸 🐵 🐔 🐧 🐦 🐤 🦆 🦉 🐴 🦄 🐝 "
                          "🦋 🐌 🐞 🐢 🐍 🐙 🐬 🐳")});
    sets.push_back({"food", Tr("Food & drink"), "\xF0\x9F\x8D\x95", false,
                    Split("🍏 🍎 🍐 🍊 🍋 🍌 🍉 🍇 🍓 🍒 🍑 🍍 🥥 🥝 🍅 🥑 🥦 🥕 🌽 🍞 🧀 🥚 🍳 🥞 "
                          "🍔 🍟 🍕 🌭 🌮 🍣 🍰 🎂 🍪 🍫 🍿 ☕ 🍵 🍺 🍷 🥂")});
    sets.push_back({"things", Tr("Celebrations & things"), "\xF0\x9F\x8E\x89", false,
                    Split("🎉 🎊 🎁 🎈 🎂 🎄 🎃 ⭐ 🌟 ✨ 🔥 💯 ✅ ❌ ❓ ❗ 💡 📌 📎 🎵 🎶 📷 🎮 🏆 "
                          "⚽ 🏀 🎾 🚗 ✈️ 🏠 ☀️ 🌧️")});
    sets.push_back({"cubes", Tr("Stickers: Cubes"), "", true,
                    {"cube-hello", "cube-thumbs", "cube-laugh", "cube-love", "cube-sad", "cube-party"}});
    return sets;
}

// A flat square button in the grid: no border, the hover tint of the look
std::shared_ptr<UltraCanvasButton> MakeCell(const std::string& id, int side, const std::string& text) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, side, side, text);
    ButtonStyle st = b->GetStyle();
    st.cornerRadius = 8.0f;
    st.borderWidth = 0.0f;
    st.fontSize = 17.0f;
    st.normalColor = Colors::Transparent;
    st.hoverColor = L().hover;
    st.pressedColor = L().pressed;
    st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().ink;
    b->SetStyle(st);
    // Exactly one cell: a button sizes itself to its text and padding
    // otherwise, and the grid's columns would not line up
    b->SetPadding(0);
    b->size.width = CSSLayout::Dimension::Px(static_cast<float>(side));
    b->size.height = CSSLayout::Dimension::Px(static_cast<float>(side));
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

} // namespace

const std::vector<EmojiSet>& EmojiSets() {
    // Made on first use: the names are translated, which needs the
    // catalogue loaded
    static const std::vector<EmojiSet> sets = MakeSets();
    return sets;
}

const EmojiSet& FindEmojiSet(const std::string& key) {
    for (const EmojiSet& set : EmojiSets()) {
        if (set.key == key) return set;
    }
    return EmojiSets().front();
}

std::string StickerPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/chatcube/stickers/" + name + ".svg");
}

EmojiPicker::EmojiPicker() = default;

EmojiPicker::~EmojiPicker() {
    Close();
}

void EmojiPicker::Close() {
    if (window_ && grid_) window_->ClosePopup(*grid_);
    grid_.reset();
    list_.reset();
}

void EmojiPicker::OpenGrid(UltraCanvasWindow& window, const Rect2Di& anchor, const std::string& setKey) {
    Close();
    window_ = &window;
    const EmojiSet& set = FindEmojiSet(setKey);
    const int cell = set.stickers ? kStickerCell : kEmojiCell;
    const int columns = set.stickers ? kStickerColumns : kEmojiColumns;
    const int rows = (static_cast<int>(set.items.size()) + columns - 1) / columns;
    const int width = columns * cell + 2 * kGridPadding;
    const int height = rows * cell + 2 * kGridPadding + kGridTitle;

    grid_ = std::make_shared<UltraCanvasContainer>("cc-emoji-grid", 0, 0, width, height);
    grid_->layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    grid_->SetPadding(kGridPadding, kGridPadding, kGridPadding, kGridPadding);
    grid_->SetBackgroundColor(L().panel);
    grid_->SetBorders(1.0f, L().line, 12.0f);
    auto title = MakeLabel("cc-emoji-grid-title", set.name, 9.5f, &Look::inkMuted);
    title->size.height = CSSLayout::Dimension::Px(static_cast<float>(kGridTitle));
    grid_->AddChild(title);
    auto cells = std::make_shared<UltraCanvasContainer>("cc-emoji-cells");
    cells->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(0);
    cells->size.width = CSSLayout::Dimension::Px(static_cast<float>(columns * cell));
    cells->size.height = CSSLayout::Dimension::Px(static_cast<float>(rows * cell));
    cells->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    int n = 0;
    for (const std::string& item : set.items) {
        const std::string id = "cc-emoji-" + set.key + "-" + std::to_string(n++);
        if (set.stickers) {
            auto b = MakeCell(id, cell, "");
            const std::string path = StickerPath(item);
            b->SetIcon(path);
            b->SetIconSize(cell - 10, cell - 10);
            b->SetOnClick([this, path]() {
                // A sticker is a message of its own: the grid closes
                auto pick = onSticker;
                Close();
                if (pick) pick(path);
            });
            cells->AddChild(b);
        } else {
            auto b = MakeCell(id, cell, item);
            b->SetOnClick([this, item]() {
                if (onEmoji) onEmoji(item);
            });
            cells->AddChild(b);
        }
    }
    grid_->AddChild(cells);

    // Above the button, its left edge at the button's, inside the window
    int x = anchor.x;
    int y = anchor.y - height - 6;
    x = std::clamp(x, 4, std::max(4, static_cast<int>(window.GetWidth()) - width - 4));
    if (y < 4) y = anchor.y + anchor.height + 6;
    PopupElementSettings settings;
    window.OpenPopup(Point2Di(x, y), *grid_, settings);
}

void EmojiPicker::OpenSetList(UltraCanvasWindow& window, const Rect2Di& anchor) {
    Close();
    window_ = &window;
    list_ = std::make_shared<UltraCanvasMenu>("cc-emoji-sets", 0, 0, 240, 0);
    list_->SetMenuType(MenuType::PopupMenu);
    list_->AddItem(MenuItemData::Header(Tr("Smileys & stickers")));
    for (const EmojiSet& set : EmojiSets()) {
        const std::string key = set.key;
        auto choose = [this, key, &window, anchor]() {
            if (onSetChosen) onSetChosen(key);
            // After the menu has gone
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                app->PostToUIThread([this, key, &window, anchor]() { OpenGrid(window, anchor, key); });
            }
        };
        if (set.stickers) {
            // The set's first sticker is its example
            list_->AddItem(MenuItemData::Action(set.name, StickerPath(set.items.front()), choose));
        } else {
            list_->AddItem(MenuItemData::Action(set.example + "   " + set.name, choose));
        }
    }
    list_->OpenMenuAnchored(anchor, window, PopupElementSettings());
}

} // namespace ChatCube
