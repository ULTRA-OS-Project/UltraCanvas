// ChatCube/core/ChatBubbleDelegate.cpp
// Implementation of the chat message bubble item delegate
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "ChatBubbleDelegate.h"
#include <algorithm>
#include <string>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ChatBubbleDelegate::ChatBubbleDelegate(const ChatBubbleStyle& style)
    : style_(style) {
}

// ============================================================================
// STYLE / WIDTH
// ============================================================================

void ChatBubbleDelegate::SetStyle(const ChatBubbleStyle& newStyle) {
    style_ = newStyle;
}

const ChatBubbleStyle& ChatBubbleDelegate::GetStyle() const {
    return style_;
}

void ChatBubbleDelegate::SetViewWidth(int width) {
    viewWidth_ = width;
}

int ChatBubbleDelegate::GetBubbleMaxWidth() const {
    // Never wider than BubbleMaxWidth, never wider than 70% of the view
    return std::min(style_.BubbleMaxWidth,
                    static_cast<int>(viewWidth_ * 0.70f));
}

// ============================================================================
// IItemDelegate — GetRowHeight
// Called for every row during layout; must be fast (no rendering).
// Returns the pixel height that RenderItem will occupy.
// ============================================================================

int ChatBubbleDelegate::GetRowHeight(const IListModel* model, int row) const {
    if (!model) return 40;

    // Read message type from model
    auto typeVal = model->GetData({row, 0}, ChatMsgRole_MessageType);
    const int* typeInt = std::get_if<int>(&typeVal);
    if (!typeInt) return 40;
    const auto msgType = static_cast<ChatMessageType>(*typeInt);

    // Non-bubble rows have fixed heights
    if (msgType == ChatMessageType::DateSeparator) return style_.SepHeight + style_.RowPadTop + style_.RowPadBottom;
    if (msgType == ChatMessageType::SystemEvent)   return style_.SysEventHeight + style_.RowPadTop + style_.RowPadBottom;
    if (msgType == ChatMessageType::Sticker)       return style_.StickerSize + style_.RowPadTop + style_.RowPadBottom + style_.AvatarSize / 2;
    if (msgType == ChatMessageType::Audio)         return style_.AudioBarHeight + style_.RowPadTop + style_.RowPadBottom + style_.FontSizeMeta + 4;

    const int bubbleWidth = GetBubbleMaxWidth() - style_.BubblePadH * 2;

    if (msgType == ChatMessageType::Image || msgType == ChatMessageType::Video) {
        auto wVal = model->GetData({row, 0}, ChatMsgRole_AttachWidth);
        auto hVal = model->GetData({row, 0}, ChatMsgRole_AttachHeight);
        const int* w = std::get_if<int>(&wVal);
        const int* h = std::get_if<int>(&hVal);
        int imgH = CalcImageBubbleHeight(w ? *w : 320, h ? *h : 240, bubbleWidth);
        // Add meta line height + padding + avatar gap
        return imgH
             + static_cast<int>(style_.FontSizeMeta) + 4
             + style_.RowPadTop + style_.RowPadBottom
             + style_.BubblePadV * 2;
    }

    if (msgType == ChatMessageType::File) {
        return CalcFileBubbleHeight()
             + style_.RowPadTop + style_.RowPadBottom;
    }

    // Text-based: Text, ForwardedText, ReplyText
    auto textVal = model->GetData({row, 0}, ListDataRole::DisplayRole);
    const std::string* text = std::get_if<std::string>(&textVal);
    const std::string empty;
    const std::string& msgText = text ? *text : empty;

    int height = 0;
    if (msgType == ChatMessageType::ReplyText) {
        height = CalcReplyTextBubbleHeight(msgText, bubbleWidth);
    } else {
        height = CalcTextBubbleHeight(msgText, bubbleWidth);
    }

    // ForwardedText adds a small "Forwarded from X" header
    if (msgType == ChatMessageType::ForwardedText) {
        height += static_cast<int>(style_.FontSizeMeta) + 4;
    }

    return height + style_.RowPadTop + style_.RowPadBottom;
}

// ============================================================================
// IItemDelegate — RenderItem
// Dispatches to per-type render methods based on ChatMessageType
// ============================================================================

void ChatBubbleDelegate::RenderItem(IRenderContext* ctx,
                                     const IListModel* model,
                                     int row,
                                     int /*column*/,
                                     const ListItemStyleOption& option) const {
    if (!ctx || !model) return;

    // ── Read message type ─────────────────────────────────────────────────
    auto typeVal = model->GetData({row, 0}, ChatMsgRole_MessageType);
    const int* typeInt = std::get_if<int>(&typeVal);
    if (!typeInt) return;
    const auto msgType = static_cast<ChatMessageType>(*typeInt);

    // ── Non-bubble rows ───────────────────────────────────────────────────
    if (msgType == ChatMessageType::DateSeparator) {
        RenderDateSeparator(ctx, option, model, row);
        return;
    }
    if (msgType == ChatMessageType::SystemEvent) {
        RenderSystemEvent(ctx, option, model, row);
        return;
    }

    // ── Bubble rows — determine own vs other ──────────────────────────────
    auto ownVal = model->GetData({row, 0}, ChatMsgRole_IsOwn);
    const int* ownInt = std::get_if<int>(&ownVal);
    const bool isOwn = (ownInt && *ownInt != 0);

    ctx->PushState();

    switch (msgType) {
        case ChatMessageType::Text:
            RenderTextBubble(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::Image:
            RenderImageBubble(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::File:
            RenderFileBubble(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::Sticker:
            RenderStickerRow(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::Audio:
            RenderAudioBubble(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::Video:
            RenderVideoBubble(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::ReplyText:
            RenderReplyTextBubble(ctx, option, model, row, isOwn);
            break;
        case ChatMessageType::ForwardedText:
            RenderForwardedTextBubble(ctx, option, model, row, isOwn);
            break;
        default:
            break;
    }

    ctx->PopState();
}

// ============================================================================
// SHARED SUB-COMPONENTS
// ============================================================================

int ChatBubbleDelegate::RenderAvatar(IRenderContext* ctx,
                                      const std::string& avatarPath,
                                      const std::string& senderName,
                                      int x, int y) const {
    const float r = static_cast<float>(style_.AvatarSize) / 2.0f;
    const Point2Df centre(static_cast<float>(x) + r,
                          static_cast<float>(y) + r);

    ctx->PushState();

    if (!avatarPath.empty()) {
        // Clip to circle then draw image
        ctx->ClipRoundedRectangle(
            Rect2Df(static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(style_.AvatarSize),
                    static_cast<float>(style_.AvatarSize)),
            r, r, r, r);
        ctx->DrawImage(avatarPath,
                       Rect2Df(static_cast<float>(x),
                               static_cast<float>(y),
                               static_cast<float>(style_.AvatarSize),
                               static_cast<float>(style_.AvatarSize)),
                       ImageFitMode::Cover);
    } else {
        // Initials fallback
        ctx->DrawFilledCircle(centre, r, style_.AvatarFallbackBg);
        if (!senderName.empty()) {
            ctx->SetFontSize(static_cast<double>(r * 0.8f));
            ctx->SetFontWeight(FontWeight::Medium);
            ctx->SetTextPaint(style_.AvatarFallbackTxt);
            ctx->SetTextAlignment(TextAlignment::Center);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            // Single initial
            std::string initial(1, senderName[0]);
            ctx->DrawTextInRect(initial,
                                Rect2Df(static_cast<float>(x),
                                        static_cast<float>(y),
                                        static_cast<float>(style_.AvatarSize),
                                        static_cast<float>(style_.AvatarSize)));
        }
    }

    ctx->PopState();
    return x + style_.AvatarSize + style_.AvatarGap;
}

Rect2Df ChatBubbleDelegate::RenderBubbleBackground(IRenderContext* ctx,
                                                     const Rect2Df& bubbleRect,
                                                     bool isOwn) const {
    const Color& bg     = isOwn ? style_.OwnBubbleBg     : style_.OtherBubbleBg;
    const Color& border = isOwn ? style_.OwnBubbleBorder : style_.OtherBubbleBorder;

    ctx->DrawFilledRectangle(bubbleRect, bg, 1.0f, border, style_.BubbleRadius);

    // Return inner content rect (padded)
    return Rect2Df(bubbleRect.x + style_.BubblePadH,
                   bubbleRect.y + style_.BubblePadV,
                   bubbleRect.width  - style_.BubblePadH * 2,
                   bubbleRect.height - style_.BubblePadV * 2);
}

void ChatBubbleDelegate::RenderReplyPreview(IRenderContext* ctx,
                                             const Rect2Df& contentRect,
                                             const std::string& senderName,
                                             const std::string& snippet) const {
    Rect2Df previewRect(contentRect.x,
                        contentRect.y,
                        contentRect.width,
                        static_cast<float>(style_.ReplyHeight));

    // Translucent background
    ctx->DrawFilledRectangle(previewRect, style_.ReplyBgColor, 0.0f,
                             Colors::Transparent, 4.0f);

    // Left accent strip
    ctx->DrawFilledRectangle(
        Rect2Df(previewRect.x,
                previewRect.y,
                static_cast<float>(style_.ReplyStripWidth),
                previewRect.height),
        style_.ReplyStripColor, 0.0f, Colors::Transparent, 0.0f);

    // Sender name
    const float nameX = previewRect.x + style_.ReplyStripWidth + style_.ReplyPadH;
    const float textW = previewRect.width - style_.ReplyStripWidth - style_.ReplyPadH * 2;

    ctx->SetFontSize(style_.FontSizeReply);
    ctx->SetFontWeight(FontWeight::Bold);
    ctx->SetTextPaint(style_.SenderNameColor);
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    ctx->DrawTextInRect(senderName,
                        Rect2Df(nameX,
                                previewRect.y + style_.ReplyPadV,
                                textW,
                                static_cast<float>(style_.FontSizeReply) + 2));

    // Snippet (one line, truncated)
    ctx->SetFontSize(style_.FontSizeReply);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(style_.ReplyTextColor);
    ctx->DrawTextInRect(snippet,
                        Rect2Df(nameX,
                                previewRect.y + style_.ReplyPadV + style_.FontSizeReply + 3,
                                textW,
                                static_cast<float>(style_.FontSizeReply) + 2));
}

void ChatBubbleDelegate::RenderTimestampAndTicks(IRenderContext* ctx,
                                                  const Rect2Df& contentRect,
                                                  const std::string& timestamp,
                                                  ChatDeliveryStatus status,
                                                  bool isOwn) const {
    ctx->SetFontSize(style_.FontSizeMeta);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetFontSlant(FontSlant::Normal);
    const Color& metaColor = isOwn ? style_.OwnMetaColor : style_.OtherMetaColor;
    ctx->SetTextPaint(metaColor);

    // Measure timestamp width to right-align it
    int tsW = 0, tsH = 0;
    ctx->GetTextLineDimensions(timestamp, tsW, tsH);

    const float metaY = contentRect.y + contentRect.height - static_cast<float>(tsH) - 2;
    float metaX = contentRect.x + contentRect.width - static_cast<float>(tsW) - 2;

    // Delivery ticks (own messages only) — drawn to the right of timestamp
    if (isOwn) {
        Color tickColor;
        std::string tickStr;
        switch (status) {
            case ChatDeliveryStatus::Sending:
                tickColor = style_.TickSending;
                tickStr   = "o";   // Clock placeholder — replace with icon in v2
                break;
            case ChatDeliveryStatus::Sent:
                tickColor = style_.TickSent;
                tickStr   = "\u2713";       // ✓
                break;
            case ChatDeliveryStatus::Delivered:
                tickColor = style_.TickDelivered;
                tickStr   = "\u2713\u2713"; // ✓✓
                break;
            case ChatDeliveryStatus::Read:
                tickColor = style_.TickRead;
                tickStr   = "\u2713\u2713"; // ✓✓ green
                break;
            case ChatDeliveryStatus::Failed:
                tickColor = style_.TickFailed;
                tickStr   = "!";
                break;
        }

        int tickW = 0, tickH = 0;
        ctx->GetTextLineDimensions(tickStr, tickW, tickH);

        ctx->SetTextPaint(tickColor);
        ctx->DrawTextInRect(tickStr,
                            Rect2Df(contentRect.x + contentRect.width - static_cast<float>(tickW) - 2,
                                    metaY,
                                    static_cast<float>(tickW),
                                    static_cast<float>(tickH)));
        metaX -= static_cast<float>(tickW) + 4;
    }

    ctx->SetTextPaint(metaColor);
    ctx->DrawTextInRect(timestamp,
                        Rect2Df(metaX, metaY,
                                static_cast<float>(tsW),
                                static_cast<float>(tsH)));
}

void ChatBubbleDelegate::RenderFileCard(IRenderContext* ctx,
                                         const Rect2Df& contentRect,
                                         const std::string& fileName,
                                         const std::string& fileSize,
                                         const std::string& attachPath) const {
    // Card background (slightly darker tint over bubble)
    Rect2Df cardRect(contentRect.x,
                     contentRect.y,
                     contentRect.width,
                     static_cast<float>(style_.FileCardHeight));
    ctx->DrawFilledRectangle(cardRect, style_.FileCardBg, 0.0f,
                             Colors::Transparent, 6.0f);

    // File icon (from attachPath if image; otherwise generic icon)
    const int iconX = static_cast<int>(contentRect.x) + 4;
    const int iconY = static_cast<int>(contentRect.y) + (style_.FileCardHeight - style_.FileIconSize) / 2;

    if (!attachPath.empty()) {
        ctx->DrawImage(attachPath,
                       Rect2Df(static_cast<float>(iconX),
                               static_cast<float>(iconY),
                               static_cast<float>(style_.FileIconSize),
                               static_cast<float>(style_.FileIconSize)),
                       ImageFitMode::Contain);
    } else {
        // Generic file placeholder circle
        ctx->DrawFilledCircle(
            Point2Df(static_cast<float>(iconX) + style_.FileIconSize / 2.0f,
                     static_cast<float>(iconY) + style_.FileIconSize / 2.0f),
            style_.FileIconSize / 2.0f,
            Color(120, 130, 160, 255));
    }

    // Filename (bold, one line)
    const float textX = static_cast<float>(iconX + style_.FileIconSize + style_.FileIconGap);
    const float textW = contentRect.width - (textX - contentRect.x) - 4;

    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetFontWeight(FontWeight::Bold);
    ctx->SetTextPaint(style_.FileNameColor);
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(fileName,
                        Rect2Df(textX,
                                contentRect.y + 6,
                                textW,
                                static_cast<float>(style_.FontSizeBody) + 2));

    // File size (muted, smaller)
    ctx->SetFontSize(style_.FontSizeMeta);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(style_.FileSizeColor);
    ctx->DrawTextInRect(fileSize,
                        Rect2Df(textX,
                                contentRect.y + 6 + style_.FontSizeBody + 4,
                                textW,
                                static_cast<float>(style_.FontSizeMeta) + 2));
}

// ============================================================================
// TEXT BUBBLE
// ============================================================================

void ChatBubbleDelegate::RenderTextBubble(IRenderContext* ctx,
                                           const ListItemStyleOption& option,
                                           const IListModel* model,
                                           int row,
                                           bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();

    // Read fields
    auto textVal     = model->GetData({row, 0}, ListDataRole::DisplayRole);
    auto avatarVal   = model->GetData({row, 0}, ListDataRole::DecorationRole);
    auto nameVal     = model->GetData({row, 0}, ChatMsgRole_SenderName);
    auto tsVal       = model->GetData({row, 0}, ChatMsgRole_TimestampDisplay);
    auto statusVal   = model->GetData({row, 0}, ChatMsgRole_DeliveryStatus);

    const std::string& msgText   = GetStringValue(textVal);
    const std::string& avatar    = GetStringValue(avatarVal);
    const std::string& sender    = GetStringValue(nameVal);
    const std::string& timestamp = GetStringValue(tsVal);
    const int* statusInt = std::get_if<int>(&statusVal);
    const ChatDeliveryStatus status = statusInt
        ? static_cast<ChatDeliveryStatus>(*statusInt)
        : ChatDeliveryStatus::Sent;

    const int rowTop  = rowRect.y + style_.RowPadTop;
    const int rowLeft = rowRect.x + style_.RowPadLeft;

    // ── Layout ───────────────────────────────────────────────────────────
    int avatarX, bubbleLeft;
    if (isOwn) {
        // Right side: no avatar shown for own messages
        avatarX    = 0;
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        // Left side: avatar then bubble
        avatarX    = rowLeft;
        bubbleLeft = avatarX + style_.AvatarSize + style_.AvatarGap;
        RenderAvatar(ctx, avatar, sender, avatarX, rowTop);

        // Sender name above bubble (other's messages only)
        ctx->SetFontSize(style_.FontSizeSender);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(style_.SenderNameColor);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->DrawTextInRect(sender,
                            Rect2Df(static_cast<float>(bubbleLeft),
                                    static_cast<float>(rowTop),
                                    static_cast<float>(bubbleMaxW),
                                    static_cast<float>(style_.FontSizeSender) + 2));
    }

    // ── Measure text to size bubble ───────────────────────────────────────
    const int innerW = bubbleMaxW - style_.BubblePadH * 2;
    int textW = 0, textH = 0;
    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->GetTextDimensions(msgText, innerW, 0, textW, textH);

    const int metaLineH = static_cast<int>(style_.FontSizeMeta) + 6;
    const int bubbleH   = textH + style_.BubblePadV * 2 + metaLineH;
    const int nameOffset = isOwn ? 0 : static_cast<int>(style_.FontSizeSender) + 4;
    const int bubbleTop  = rowTop + nameOffset;

    Rect2Df bubbleRect(static_cast<float>(bubbleLeft),
                       static_cast<float>(bubbleTop),
                       static_cast<float>(bubbleMaxW),
                       static_cast<float>(bubbleH));

    Rect2Df content = RenderBubbleBackground(ctx, bubbleRect, isOwn);

    // ── Message text ─────────────────────────────────────────────────────
    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(isOwn ? style_.OwnTextColor : style_.OtherTextColor);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    ctx->DrawTextInRect(msgText,
                        Rect2Df(content.x, content.y, content.width,
                                static_cast<float>(textH)));

    // ── Timestamp + ticks ────────────────────────────────────────────────
    RenderTimestampAndTicks(ctx, content, timestamp, status, isOwn);
}

// ============================================================================
// FILE BUBBLE
// ============================================================================

void ChatBubbleDelegate::RenderFileBubble(IRenderContext* ctx,
                                           const ListItemStyleOption& option,
                                           const IListModel* model,
                                           int row,
                                           bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();

    auto avatarVal  = model->GetData({row, 0}, ListDataRole::DecorationRole);
    auto nameVal    = model->GetData({row, 0}, ChatMsgRole_SenderName);
    auto fileNmVal  = model->GetData({row, 0}, ChatMsgRole_AttachFileName);
    auto fileSzVal  = model->GetData({row, 0}, ChatMsgRole_AttachSize);
    auto filePtVal  = model->GetData({row, 0}, ChatMsgRole_AttachPath);
    auto tsVal      = model->GetData({row, 0}, ChatMsgRole_TimestampDisplay);
    auto statusVal  = model->GetData({row, 0}, ChatMsgRole_DeliveryStatus);

    const std::string& avatar    = GetStringValue(avatarVal);
    const std::string& sender    = GetStringValue(nameVal);
    const std::string& fileName  = GetStringValue(fileNmVal);
    const std::string& fileSize  = GetStringValue(fileSzVal);
    const std::string& filePath  = GetStringValue(filePtVal);
    const std::string& timestamp = GetStringValue(tsVal);
    const int* statusInt = std::get_if<int>(&statusVal);
    const ChatDeliveryStatus status = statusInt
        ? static_cast<ChatDeliveryStatus>(*statusInt)
        : ChatDeliveryStatus::Sent;

    const int rowTop = rowRect.y + style_.RowPadTop;

    int bubbleLeft;
    if (isOwn) {
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        const int avatarX = rowRect.x + style_.RowPadLeft;
        RenderAvatar(ctx, avatar, sender, avatarX, rowTop);
        bubbleLeft = avatarX + style_.AvatarSize + style_.AvatarGap;
    }

    const int bubbleH  = style_.FileCardHeight + style_.BubblePadV * 2
                       + static_cast<int>(style_.FontSizeMeta) + 6;
    Rect2Df bubbleRect(static_cast<float>(bubbleLeft),
                       static_cast<float>(rowTop),
                       static_cast<float>(bubbleMaxW),
                       static_cast<float>(bubbleH));

    Rect2Df content = RenderBubbleBackground(ctx, bubbleRect, isOwn);

    RenderFileCard(ctx, content, fileName, fileSize, filePath);
    RenderTimestampAndTicks(ctx, content, timestamp, status, isOwn);
}

// ============================================================================
// IMAGE BUBBLE
// ============================================================================

void ChatBubbleDelegate::RenderImageBubble(IRenderContext* ctx,
                                            const ListItemStyleOption& option,
                                            const IListModel* model,
                                            int row,
                                            bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();
    const int innerW       = bubbleMaxW - style_.BubblePadH * 2;

    auto avatarVal  = model->GetData({row, 0}, ListDataRole::DecorationRole);
    auto nameVal    = model->GetData({row, 0}, ChatMsgRole_SenderName);
    auto imgPtVal   = model->GetData({row, 0}, ChatMsgRole_AttachPath);
    auto imgWVal    = model->GetData({row, 0}, ChatMsgRole_AttachWidth);
    auto imgHVal    = model->GetData({row, 0}, ChatMsgRole_AttachHeight);
    auto tsVal      = model->GetData({row, 0}, ChatMsgRole_TimestampDisplay);
    auto statusVal  = model->GetData({row, 0}, ChatMsgRole_DeliveryStatus);
    auto captionVal = model->GetData({row, 0}, ListDataRole::DisplayRole);

    const std::string& avatar    = GetStringValue(avatarVal);
    const std::string& sender    = GetStringValue(nameVal);
    const std::string& imgPath   = GetStringValue(imgPtVal);
    const std::string& timestamp = GetStringValue(tsVal);
    const std::string& caption   = GetStringValue(captionVal);
    const int* imgWPtr  = std::get_if<int>(&imgWVal);
    const int* imgHPtr  = std::get_if<int>(&imgHVal);
    const int* statusInt = std::get_if<int>(&statusVal);
    const ChatDeliveryStatus status = statusInt
        ? static_cast<ChatDeliveryStatus>(*statusInt)
        : ChatDeliveryStatus::Sent;

    const int srcW = imgWPtr ? *imgWPtr : 320;
    const int srcH = imgHPtr ? *imgHPtr : 240;
    const int dispH = CalcImageBubbleHeight(srcW, srcH, innerW);

    const int rowTop = rowRect.y + style_.RowPadTop;
    int bubbleLeft;
    if (isOwn) {
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        const int avatarX = rowRect.x + style_.RowPadLeft;
        RenderAvatar(ctx, avatar, sender, avatarX, rowTop);
        bubbleLeft = avatarX + style_.AvatarSize + style_.AvatarGap;
    }

    const int metaH   = static_cast<int>(style_.FontSizeMeta) + 6;
    const int bubbleH = dispH + style_.BubblePadV * 2 + metaH;

    Rect2Df bubbleRect(static_cast<float>(bubbleLeft),
                       static_cast<float>(rowTop),
                       static_cast<float>(bubbleMaxW),
                       static_cast<float>(bubbleH));

    Rect2Df content = RenderBubbleBackground(ctx, bubbleRect, isOwn);

    // Image (clipped to rounded rect for clean look)
    Rect2Df imgRect(content.x, content.y,
                    content.width, static_cast<float>(dispH));
    if (!imgPath.empty()) {
        ctx->PushState();
        ctx->ClipRoundedRectangle(imgRect,
                                  style_.BubbleRadius - 2,
                                  style_.BubbleRadius - 2,
                                  2.0, 2.0);
        ctx->DrawImage(imgPath, imgRect, ImageFitMode::Cover);
        ctx->PopState();
    } else {
        // Placeholder
        ctx->DrawFilledRectangle(imgRect,
                                 Color(200, 200, 200, 255),
                                 0.0f, Colors::Transparent, 4.0f);
    }

    // Caption (optional)
    if (!caption.empty()) {
        ctx->SetFontSize(style_.FontSizeBody);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(isOwn ? style_.OwnTextColor : style_.OtherTextColor);
        ctx->SetTextWrap(TextWrap::WrapWordChar);
        ctx->DrawTextInRect(caption,
                            Rect2Df(content.x,
                                    content.y + static_cast<float>(dispH) + 2,
                                    content.width,
                                    static_cast<float>(style_.FontSizeBody) + 2));
    }

    RenderTimestampAndTicks(ctx, content, timestamp, status, isOwn);
}

// ============================================================================
// STICKER ROW (no bubble background)
// ============================================================================

void ChatBubbleDelegate::RenderStickerRow(IRenderContext* ctx,
                                           const ListItemStyleOption& option,
                                           const IListModel* model,
                                           int row,
                                           bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    auto imgPtVal = model->GetData({row, 0}, ChatMsgRole_AttachPath);
    const std::string& stickerPath = GetStringValue(imgPtVal);

    const int rowTop = rowRect.y + style_.RowPadTop;
    int stickerX;
    if (isOwn) {
        stickerX = rowRect.x + rowRect.width - style_.RowPadRight - style_.StickerSize;
    } else {
        stickerX = rowRect.x + style_.RowPadLeft + style_.AvatarSize + style_.AvatarGap;
    }

    if (!stickerPath.empty()) {
        ctx->DrawImage(stickerPath,
                       Rect2Df(static_cast<float>(stickerX),
                               static_cast<float>(rowTop),
                               static_cast<float>(style_.StickerSize),
                               static_cast<float>(style_.StickerSize)),
                       ImageFitMode::Contain);
    }
}

// ============================================================================
// AUDIO BUBBLE
// ============================================================================

void ChatBubbleDelegate::RenderAudioBubble(IRenderContext* ctx,
                                            const ListItemStyleOption& option,
                                            const IListModel* model,
                                            int row,
                                            bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();

    auto avatarVal  = model->GetData({row, 0}, ListDataRole::DecorationRole);
    auto nameVal    = model->GetData({row, 0}, ChatMsgRole_SenderName);
    auto tsVal      = model->GetData({row, 0}, ChatMsgRole_TimestampDisplay);
    auto statusVal  = model->GetData({row, 0}, ChatMsgRole_DeliveryStatus);

    const std::string& avatar    = GetStringValue(avatarVal);
    const std::string& sender    = GetStringValue(nameVal);
    const std::string& timestamp = GetStringValue(tsVal);
    const int* statusInt = std::get_if<int>(&statusVal);
    const ChatDeliveryStatus status = statusInt
        ? static_cast<ChatDeliveryStatus>(*statusInt)
        : ChatDeliveryStatus::Sent;

    const int rowTop = rowRect.y + style_.RowPadTop;
    int bubbleLeft;
    if (isOwn) {
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        const int avatarX = rowRect.x + style_.RowPadLeft;
        RenderAvatar(ctx, avatar, sender, avatarX, rowTop);
        bubbleLeft = avatarX + style_.AvatarSize + style_.AvatarGap;
    }

    const int bubbleH = style_.AudioBarHeight + style_.BubblePadV * 2;
    Rect2Df bubbleRect(static_cast<float>(bubbleLeft),
                       static_cast<float>(rowTop),
                       static_cast<float>(bubbleMaxW),
                       static_cast<float>(bubbleH));
    Rect2Df content = RenderBubbleBackground(ctx, bubbleRect, isOwn);

    // Simple waveform placeholder — evenly spaced bars
    const int barCount  = 20;
    const float barGap  = 3.0f;
    const float barW    = (content.width - barGap * (barCount - 1)) / barCount;
    const float barMaxH = content.height - static_cast<float>(style_.FontSizeMeta) - 8;

    for (int i = 0; i < barCount; i++) {
        // Pseudo-random height variation (deterministic by index)
        const float h = barMaxH * (0.3f + 0.7f * std::abs(std::sin(static_cast<float>(i) * 1.3f)));
        const float bx = content.x + static_cast<float>(i) * (barW + barGap);
        const float by = content.y + (barMaxH - h) / 2.0f;
        ctx->DrawFilledRectangle(
            Rect2Df(bx, by, barW, h),
            style_.AudioWaveColor, 0.0f, Colors::Transparent, 2.0f);
    }

    RenderTimestampAndTicks(ctx, content, timestamp, status, isOwn);
}

// ============================================================================
// VIDEO BUBBLE (thumbnail + play indicator)
// ============================================================================

void ChatBubbleDelegate::RenderVideoBubble(IRenderContext* ctx,
                                            const ListItemStyleOption& option,
                                            const IListModel* model,
                                            int row,
                                            bool isOwn) const {
    // Video shares the same layout as Image for now (thumbnail as image)
    // Play-button overlay is drawn on top
    RenderImageBubble(ctx, option, model, row, isOwn);

    // Play button overlay — centred circle + triangle
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();
    const int innerW       = bubbleMaxW - style_.BubblePadH * 2;
    auto imgWVal = model->GetData({row, 0}, ChatMsgRole_AttachWidth);
    auto imgHVal = model->GetData({row, 0}, ChatMsgRole_AttachHeight);
    const int* imgWPtr = std::get_if<int>(&imgWVal);
    const int* imgHPtr = std::get_if<int>(&imgHVal);
    const int dispH = CalcImageBubbleHeight(imgWPtr ? *imgWPtr : 320,
                                            imgHPtr ? *imgHPtr : 240,
                                            innerW);

    const int rowTop = rowRect.y + style_.RowPadTop;
    int bubbleLeft;
    if (isOwn) {
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        bubbleLeft = rowRect.x + style_.RowPadLeft + style_.AvatarSize + style_.AvatarGap;
    }

    const float imgCentreX = static_cast<float>(bubbleLeft) + style_.BubblePadH + static_cast<float>(innerW) / 2.0f;
    const float imgCentreY = static_cast<float>(rowTop) + style_.BubblePadV + static_cast<float>(dispH) / 2.0f;
    const float playR = 20.0f;

    ctx->DrawFilledCircle(Point2Df(imgCentreX, imgCentreY),
                          playR, Color(0, 0, 0, 120));
}

// ============================================================================
// REPLY TEXT BUBBLE
// ============================================================================

void ChatBubbleDelegate::RenderReplyTextBubble(IRenderContext* ctx,
                                                const ListItemStyleOption& option,
                                                const IListModel* model,
                                                int row,
                                                bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();

    auto textVal      = model->GetData({row, 0}, ListDataRole::DisplayRole);
    auto avatarVal    = model->GetData({row, 0}, ListDataRole::DecorationRole);
    auto nameVal      = model->GetData({row, 0}, ChatMsgRole_SenderName);
    auto tsVal        = model->GetData({row, 0}, ChatMsgRole_TimestampDisplay);
    auto statusVal    = model->GetData({row, 0}, ChatMsgRole_DeliveryStatus);
    auto repSnipVal   = model->GetData({row, 0}, ChatMsgRole_ReplySnippet);
    auto repNameVal   = model->GetData({row, 0}, ChatMsgRole_ReplySenderName);

    const std::string& msgText    = GetStringValue(textVal);
    const std::string& avatar     = GetStringValue(avatarVal);
    const std::string& sender     = GetStringValue(nameVal);
    const std::string& timestamp  = GetStringValue(tsVal);
    const std::string& repSnip    = GetStringValue(repSnipVal);
    const std::string& repName    = GetStringValue(repNameVal);
    const int* statusInt = std::get_if<int>(&statusVal);
    const ChatDeliveryStatus status = statusInt
        ? static_cast<ChatDeliveryStatus>(*statusInt)
        : ChatDeliveryStatus::Sent;

    const int rowTop  = rowRect.y + style_.RowPadTop;
    int bubbleLeft;
    if (isOwn) {
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        const int avatarX = rowRect.x + style_.RowPadLeft;
        RenderAvatar(ctx, avatar, sender, avatarX, rowTop);
        bubbleLeft = avatarX + style_.AvatarSize + style_.AvatarGap;
    }

    const int innerW = bubbleMaxW - style_.BubblePadH * 2;
    int textW = 0, textH = 0;
    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->GetTextDimensions(msgText, innerW, 0, textW, textH);

    const int metaH   = static_cast<int>(style_.FontSizeMeta) + 6;
    const int bubbleH = style_.ReplyHeight + style_.BubblePadV
                      + textH + metaH + style_.BubblePadV;

    Rect2Df bubbleRect(static_cast<float>(bubbleLeft),
                       static_cast<float>(rowTop),
                       static_cast<float>(bubbleMaxW),
                       static_cast<float>(bubbleH));
    Rect2Df content = RenderBubbleBackground(ctx, bubbleRect, isOwn);

    RenderReplyPreview(ctx, content, repName, repSnip);

    // Main text below reply block
    Rect2Df textArea(content.x,
                     content.y + static_cast<float>(style_.ReplyHeight) + 4,
                     content.width,
                     static_cast<float>(textH));
    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(isOwn ? style_.OwnTextColor : style_.OtherTextColor);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->DrawTextInRect(msgText, textArea);

    RenderTimestampAndTicks(ctx, content, timestamp, status, isOwn);
}

// ============================================================================
// FORWARDED TEXT BUBBLE
// ============================================================================

void ChatBubbleDelegate::RenderForwardedTextBubble(IRenderContext* ctx,
                                                    const ListItemStyleOption& option,
                                                    const IListModel* model,
                                                    int row,
                                                    bool isOwn) const {
    const Rect2Di& rowRect = option.rect;
    const int bubbleMaxW   = GetBubbleMaxWidth();

    auto textVal    = model->GetData({row, 0}, ListDataRole::DisplayRole);
    auto avatarVal  = model->GetData({row, 0}, ListDataRole::DecorationRole);
    auto nameVal    = model->GetData({row, 0}, ChatMsgRole_SenderName);
    auto tsVal      = model->GetData({row, 0}, ChatMsgRole_TimestampDisplay);
    auto statusVal  = model->GetData({row, 0}, ChatMsgRole_DeliveryStatus);
    auto fwdVal     = model->GetData({row, 0}, ChatMsgRole_ForwardedFrom);

    const std::string& msgText    = GetStringValue(textVal);
    const std::string& avatar     = GetStringValue(avatarVal);
    const std::string& sender     = GetStringValue(nameVal);
    const std::string& timestamp  = GetStringValue(tsVal);
    const std::string& fwdFrom    = GetStringValue(fwdVal);
    const int* statusInt = std::get_if<int>(&statusVal);
    const ChatDeliveryStatus status = statusInt
        ? static_cast<ChatDeliveryStatus>(*statusInt)
        : ChatDeliveryStatus::Sent;

    const int rowTop = rowRect.y + style_.RowPadTop;
    int bubbleLeft;
    if (isOwn) {
        bubbleLeft = rowRect.x + rowRect.width - style_.RowPadRight - bubbleMaxW;
    } else {
        const int avatarX = rowRect.x + style_.RowPadLeft;
        RenderAvatar(ctx, avatar, sender, avatarX, rowTop);
        bubbleLeft = avatarX + style_.AvatarSize + style_.AvatarGap;
    }

    const int innerW = bubbleMaxW - style_.BubblePadH * 2;
    const int fwdLineH = static_cast<int>(style_.FontSizeMeta) + 4;
    int textW = 0, textH = 0;
    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->GetTextDimensions(msgText, innerW, 0, textW, textH);

    const int metaH   = static_cast<int>(style_.FontSizeMeta) + 6;
    const int bubbleH = style_.BubblePadV + fwdLineH + textH + metaH + style_.BubblePadV;

    Rect2Df bubbleRect(static_cast<float>(bubbleLeft),
                       static_cast<float>(rowTop),
                       static_cast<float>(bubbleMaxW),
                       static_cast<float>(bubbleH));
    Rect2Df content = RenderBubbleBackground(ctx, bubbleRect, isOwn);

    // "Forwarded from X" header
    std::string fwdLabel = "Forwarded from " + fwdFrom;
    ctx->SetFontSize(style_.FontSizeMeta);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetFontSlant(FontSlant::Italic);
    ctx->SetTextPaint(style_.OtherMetaColor);
    ctx->SetTextWrap(TextWrap::WrapNone);
    ctx->DrawTextInRect(fwdLabel,
                        Rect2Df(content.x, content.y,
                                content.width,
                                static_cast<float>(fwdLineH)));
    ctx->SetFontSlant(FontSlant::Normal);

    // Message text
    ctx->SetFontSize(style_.FontSizeBody);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(isOwn ? style_.OwnTextColor : style_.OtherTextColor);
    ctx->SetTextWrap(TextWrap::WrapWordChar);
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->DrawTextInRect(msgText,
                        Rect2Df(content.x,
                                content.y + static_cast<float>(fwdLineH) + 2,
                                content.width,
                                static_cast<float>(textH)));

    RenderTimestampAndTicks(ctx, content, timestamp, status, isOwn);
}

// ============================================================================
// DATE SEPARATOR
// ============================================================================

void ChatBubbleDelegate::RenderDateSeparator(IRenderContext* ctx,
                                              const ListItemStyleOption& option,
                                              const IListModel* model,
                                              int row) const {
    const Rect2Di& rowRect = option.rect;
    auto labelVal = model->GetData({row, 0}, ChatMsgRole_DateLabel);
    const std::string& label = GetStringValue(labelVal);

    const float centreY = static_cast<float>(rowRect.y + rowRect.height / 2);
    const float left    = static_cast<float>(rowRect.x + style_.RowPadLeft);
    const float right   = static_cast<float>(rowRect.x + rowRect.width - style_.RowPadRight);

    // Horizontal lines either side
    ctx->SetStrokePaint(style_.SepLineColor);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Df(left,  centreY), Point2Df(right, centreY));

    // Label pill in the centre
    if (!label.empty()) {
        ctx->SetFontSize(style_.FontSizeSep);
        ctx->SetFontWeight(FontWeight::Normal);
        int lw = 0, lh = 0;
        ctx->GetTextLineDimensions(label, lw, lh);

        const float pillW = static_cast<float>(lw) + 20;
        const float pillH = static_cast<float>(lh) + 6;
        const float pillX = static_cast<float>(rowRect.x + rowRect.width / 2) - pillW / 2;
        const float pillY = centreY - pillH / 2;

        ctx->DrawFilledRectangle(Rect2Df(pillX, pillY, pillW, pillH),
                                 style_.SepBgColor, 0.0f, Colors::Transparent,
                                 style_.SepBgRadius);

        ctx->SetTextPaint(style_.SepTextColor);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(label, Rect2Df(pillX, pillY, pillW, pillH));
    }
}

// ============================================================================
// SYSTEM EVENT
// ============================================================================

void ChatBubbleDelegate::RenderSystemEvent(IRenderContext* ctx,
                                            const ListItemStyleOption& option,
                                            const IListModel* model,
                                            int row) const {
    const Rect2Di& rowRect = option.rect;
    auto evtVal = model->GetData({row, 0}, ChatMsgRole_SystemEventText);
    const std::string& evtText = GetStringValue(evtVal);

    ctx->SetFontSize(style_.SysEventFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetFontSlant(FontSlant::Italic);
    ctx->SetTextPaint(style_.SysEventTextColor);
    ctx->SetTextAlignment(TextAlignment::Center);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
    ctx->DrawTextInRect(evtText,
                        Rect2Df(static_cast<float>(rowRect.x),
                                static_cast<float>(rowRect.y),
                                static_cast<float>(rowRect.width),
                                static_cast<float>(rowRect.height)));
    ctx->SetFontSlant(FontSlant::Normal);
}

// ============================================================================
// HEIGHT CALCULATORS
// ============================================================================

int ChatBubbleDelegate::CalcTextBubbleHeight(const std::string& text,
                                              int bubbleWidth) const {
    // We cannot call GetTextDimensions here (no ctx), so estimate via line count
    // This is the offline path used by GetRowHeight before rendering.
    // Conservative estimate: avg ~70 chars per line at bubbleWidth px
    if (text.empty()) {
        return style_.BubblePadV * 2
             + static_cast<int>(style_.FontSizeBody) + 4
             + static_cast<int>(style_.FontSizeMeta) + 6;
    }
    const int charsPerLine = std::max(1, bubbleWidth / 7);
    const int lineCount    = std::max(1, (static_cast<int>(text.size()) + charsPerLine - 1) / charsPerLine);
    const int lineH        = static_cast<int>(style_.FontSizeBody * 1.5f);
    return style_.BubblePadV * 2
         + lineCount * lineH
         + static_cast<int>(style_.FontSizeMeta) + 6;
}

int ChatBubbleDelegate::CalcFileBubbleHeight() const {
    return style_.BubblePadV * 2
         + style_.FileCardHeight
         + static_cast<int>(style_.FontSizeMeta) + 6;
}

int ChatBubbleDelegate::CalcImageBubbleHeight(int imgW, int imgH,
                                               int bubbleWidth) const {
    if (imgW <= 0 || imgH <= 0) return style_.ImageMaxHeight;
    const float aspect  = static_cast<float>(imgH) / static_cast<float>(imgW);
    const int   scaledH = static_cast<int>(static_cast<float>(bubbleWidth) * aspect);
    return std::min(scaledH, style_.ImageMaxHeight);
}

int ChatBubbleDelegate::CalcReplyTextBubbleHeight(const std::string& text,
                                                   int bubbleWidth) const {
    return style_.ReplyHeight + 4 + CalcTextBubbleHeight(text, bubbleWidth);
}

} // namespace ChatCube
} // namespace UltraCanvas
