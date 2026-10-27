// Apps/ChatCube/ui/ChatCubeTheme.cpp
// See ChatCubeTheme.h.
// Version: 0.1.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework / ULTRA OS
#include "ChatCubeTheme.h"

#include "UltraCanvasUtils.h"

#include <cctype>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

const Color kGreen(34, 139, 70, 255);           // the ChatCube green
const Color kGreenDeep(27, 116, 58, 255);
const Color kGreenOnDark(76, 187, 110, 255);
const Color kGreenOnDarkHover(96, 205, 128, 255);
const Color kWhite(255, 255, 255, 255);

// The avatar palette: eight hues that read on white and on the dark panel.
const std::array<Color, 8> kAvatarsLight = {
    Color(228, 93, 86, 255),  Color(224, 128, 44, 255),  Color(196, 160, 20, 255),  Color(52, 160, 96, 255),
    Color(40, 150, 170, 255), Color(64, 118, 220, 255),  Color(132, 94, 210, 255),  Color(200, 82, 150, 255)};
const std::array<Color, 8> kAvatarsDark = {
    Color(236, 112, 104, 255), Color(236, 148, 70, 255), Color(214, 180, 48, 255), Color(76, 184, 118, 255),
    Color(66, 176, 196, 255),  Color(96, 146, 236, 255), Color(156, 122, 228, 255), Color(220, 108, 172, 255)};

Color Mix(const Color& a, const Color& b, float t) {
    auto lerp = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * t); };
    return Color(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), 255);
}

} // namespace

UltraCanvasTheme ThemeFor(bool dark) {
    UltraCanvasTheme t = dark ? UltraCanvasTheme::Dark() : UltraCanvasTheme::Light();
    t.name = dark ? "ChatCube dark" : "ChatCube light";
    ThemeRoles& r = t.roles;
    if (dark) {
        r.ground = Color(24, 26, 31, 255);
        r.panel = Color(32, 35, 42, 255);
        r.well = Color(40, 44, 52, 255);
        r.raised = Color(38, 42, 50, 255);
        r.hover = Color(48, 53, 63, 255);
        r.pressed = Color(58, 64, 76, 255);
        r.line = Color(52, 57, 68, 255);
        r.lineStrong = Color(80, 86, 100, 255);
        r.ink = Color(232, 234, 238, 255);
        r.inkMuted = Color(160, 166, 178, 255);
        r.inkFaint = Color(110, 116, 128, 255);
        r.accent = kGreenOnDark;
        r.accentHover = kGreenOnDarkHover;
        r.accentSoft = Color(34, 64, 44, 255);
        r.onAccent = Color(12, 28, 18, 255);
        r.focus = kGreenOnDark;
        r.danger = Color(240, 96, 96, 255);
        t.SetRole("chatcube.sidebar", Color(28, 31, 37, 255));
        t.SetRole("chatcube.bubble-own", Color(38, 84, 52, 255));
        t.SetRole("chatcube.bubble-own-ink", Color(222, 245, 228, 255));
        t.SetRole("chatcube.bubble-other", Color(44, 48, 58, 255));
        t.SetRole("chatcube.bubble-other-line", Color(56, 61, 72, 255));
        t.SetRole("chatcube.call-bar", Color(30, 52, 38, 255));
        for (size_t i = 0; i < kAvatarsDark.size(); ++i) t.SetRole("chatcube.avatar." + std::to_string(i), kAvatarsDark[i]);
    } else {
        r.ground = Color(246, 247, 249, 255);
        r.panel = kWhite;
        r.well = Color(241, 243, 246, 255);
        r.raised = kWhite;
        r.hover = Color(236, 238, 242, 255);
        r.pressed = Color(226, 229, 235, 255);
        r.line = Color(226, 229, 235, 255);
        r.lineStrong = Color(200, 204, 212, 255);
        r.ink = Color(28, 30, 36, 255);
        r.inkMuted = Color(104, 110, 122, 255);
        r.inkFaint = Color(150, 156, 166, 255);
        r.accent = kGreen;
        r.accentHover = kGreenDeep;
        r.accentSoft = Color(226, 243, 231, 255);
        r.onAccent = kWhite;
        r.focus = kGreen;
        r.danger = Color(206, 56, 56, 255);
        t.SetRole("chatcube.sidebar", Color(238, 241, 245, 255));
        t.SetRole("chatcube.bubble-own", Color(214, 240, 221, 255));
        t.SetRole("chatcube.bubble-own-ink", Color(22, 54, 32, 255));
        t.SetRole("chatcube.bubble-other", kWhite);
        t.SetRole("chatcube.bubble-other-line", Color(226, 229, 235, 255));
        t.SetRole("chatcube.call-bar", Color(228, 243, 232, 255));
        for (size_t i = 0; i < kAvatarsLight.size(); ++i) t.SetRole("chatcube.avatar." + std::to_string(i), kAvatarsLight[i]);
    }
    t.SetRole("chatcube.presence-online", Color(52, 199, 89, 255));
    t.SetRole("chatcube.presence-away", Color(255, 184, 0, 255));
    t.SetRole("chatcube.presence-busy", Color(229, 57, 53, 255));
    t.SetRole("chatcube.presence-offline", dark ? Color(96, 102, 114, 255) : Color(176, 182, 192, 255));
    return t;
}

Look LookOf(const UltraCanvasTheme& theme) {
    const ThemeRoles& r = theme.roles;
    Look l;
    l.ground = r.ground;
    l.panel = r.panel;
    l.well = r.well;
    l.hover = r.hover;
    l.pressed = r.pressed;
    l.line = r.line;
    l.lineStrong = r.lineStrong;
    l.ink = r.ink;
    l.inkMuted = r.inkMuted;
    l.inkFaint = r.inkFaint;
    l.accent = r.accent;
    l.accentHover = r.accentHover;
    l.accentSoft = r.accentSoft;
    l.onAccent = r.onAccent;
    l.danger = r.danger;
    l.sidebar = theme.Role("chatcube.sidebar", Mix(r.ground, r.panel, 0.5f));
    l.bubbleOwn = theme.Role("chatcube.bubble-own", r.accentSoft);
    l.bubbleOwnInk = theme.Role("chatcube.bubble-own-ink", r.ink);
    l.bubbleOther = theme.Role("chatcube.bubble-other", r.panel);
    l.bubbleOtherLine = theme.Role("chatcube.bubble-other-line", r.line);
    l.presenceOnline = theme.Role("chatcube.presence-online", Color(52, 199, 89, 255));
    l.presenceAway = theme.Role("chatcube.presence-away", Color(255, 184, 0, 255));
    l.presenceBusy = theme.Role("chatcube.presence-busy", r.danger);
    l.presenceOffline = theme.Role("chatcube.presence-offline", r.inkFaint);
    l.callBar = theme.Role("chatcube.call-bar", r.accentSoft);
    const std::array<Color, 8>& fallback = theme.dark ? kAvatarsDark : kAvatarsLight;
    for (size_t i = 0; i < l.avatars.size(); ++i)
        l.avatars[i] = theme.Role("chatcube.avatar." + std::to_string(i), fallback[i]);
    return l;
}

Look CurrentLook() {
    return LookOf(UltraCanvasTheme::Current());
}

std::string IconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/chatcube/" + name + ".svg");
}

std::string Initials(const std::string& title) {
    std::string out;
    int words = 0;
    size_t i = 0;
    while (i < title.size() && words < 2) {
        while (i < title.size() && (title[i] == ' ' || title[i] == '\t')) ++i;
        if (i >= title.size()) break;
        // Punctuation a name may start with ("#team", "@bob", "+44") is not a letter.
        if (title[i] == '#' || title[i] == '@' || title[i] == '+') { ++i; continue; }
        // One UTF-8 code point: the lead byte says how many follow.
        const unsigned char lead = static_cast<unsigned char>(title[i]);
        size_t n = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (i + n > title.size()) n = title.size() - i;
        std::string cp = title.substr(i, n);
        if (n == 1) cp[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cp[0])));
        out += cp;
        ++words;
        while (i < title.size() && title[i] != ' ' && title[i] != '\t') ++i;
    }
    return out;
}

Color AvatarColor(const std::string& key, const Look& look) {
    uint32_t h = 2166136261u;
    for (unsigned char c : key) h = (h ^ c) * 16777619u;
    return look.avatars[h % look.avatars.size()];
}

void PaintAvatar(IRenderContext* ctx, const Point2Dd& centre, double radius, const std::string& title,
                 const std::string& key, bool isGroup, const Look& look) {
    if (!ctx) return;
    const Color fill = isGroup ? look.well : AvatarColor(key.empty() ? title : key, look);
    ctx->DrawFilledCircle(centre, static_cast<float>(radius), fill, isGroup ? look.lineStrong : Colors::Transparent,
                          isGroup ? 1.0f : 0.0f);
    const std::string text = isGroup ? "#" : Initials(title);
    ctx->SetFontSize(static_cast<float>(radius * (text.size() > 2 ? 0.72 : 0.82)));
    ctx->SetFontWeight(FontWeight::Bold);
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->SetTextPaint(isGroup ? look.inkMuted : kWhite);
    ctx->DrawTextInRect(text, Rect2Dd(centre.x - radius, centre.y - radius, 2 * radius, 2 * radius));
}

void PaintPresence(IRenderContext* ctx, const Point2Dd& avatarCentre, double avatarRadius, const Color& presence,
                   const Color& ring) {
    if (!ctx) return;
    const double r = std::max(3.5, avatarRadius * 0.26);
    const Point2Dd at(avatarCentre.x + avatarRadius * 0.68, avatarCentre.y + avatarRadius * 0.68);
    ctx->DrawFilledCircle(at, static_cast<float>(r + 2.0), ring);
    ctx->DrawFilledCircle(at, static_cast<float>(r), presence);
}

void FollowTheme(const std::shared_ptr<UltraCanvasLabel>& label, LabelVariant variant) {
    label->SetThemeVariant(variant);
    label->SetFollowTheme(true);
}

void FollowTheme(const std::shared_ptr<UltraCanvasButton>& button, ButtonVariant variant) {
    button->SetThemeVariant(variant);
    button->SetFollowTheme(true);
}

} // namespace ChatCube
