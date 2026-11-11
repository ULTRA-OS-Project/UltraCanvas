// ChatCube/include/ChatBubbleDelegate.h
// ListView item delegate that renders chat message bubbles
// Handles all ChatMessageType variants: text, image, file, sticker,
// audio, video, date separator, system event, forwarded, reply
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "ChatCubeTypes.h"
#include "ChatMessageModel.h"
#include <string>
#include <memory>

namespace UltraCanvas {
namespace ChatCube {

// ============================================================================
// BUBBLE STYLE
// All visual constants in one struct — no magic numbers in rendering code
// ============================================================================

struct ChatBubbleStyle {
    // ── Fonts ─────────────────────────────────────────────────────────────
    std::string FontFamily      = "sans-serif";
    float       FontSizeBody    = 13.0f;    // Main message text
    float       FontSizeMeta    = 11.0f;    // Timestamp, filename, file size
    float       FontSizeSender  = 12.0f;    // Sender name above other-person bubble
    float       FontSizeSep     = 11.0f;    // Date separator label
    float       FontSizeReply   = 11.0f;    // Quoted reply preview text

    // ── Bubble geometry ───────────────────────────────────────────────────
    float       BubbleRadius    = 10.0f;    // Rounded corner radius
    int         BubblePadH      = 10;       // Horizontal inner padding
    int         BubblePadV      = 7;        // Vertical inner padding
    int         BubbleMaxWidth  = 360;      // Max bubble width in pixels
    int         BubbleMinWidth  = 80;       // Min bubble width (for timestamp fit)
    int         RowPadTop       = 4;        // Space above each row
    int         RowPadBottom    = 4;        // Space below each row
    int         RowPadLeft      = 8;        // Left margin of message area
    int         RowPadRight     = 8;        // Right margin

    // ── Avatar ────────────────────────────────────────────────────────────
    int         AvatarSize      = 36;       // Avatar circle diameter
    int         AvatarGap       = 8;        // Gap between avatar and bubble
    // Avatar colours used when no image is available (initials fallback)
    Color       AvatarFallbackBg  = Color(100, 120, 180, 255);
    Color       AvatarFallbackTxt = Color(255, 255, 255, 255);

    // ── Own message (right-aligned, green) ────────────────────────────────
    Color       OwnBubbleBg     = Color(200, 230, 160, 255);   // Light green
    Color       OwnBubbleBorder = Color(170, 205, 130, 255);
    Color       OwnTextColor    = Color(20,  20,  20,  255);
    Color       OwnMetaColor    = Color(80,  120, 60,  255);   // Timestamp muted

    // ── Other message (left-aligned, blue/grey) ───────────────────────────
    Color       OtherBubbleBg   = Color(210, 225, 245, 255);   // Light blue-grey
    Color       OtherBubbleBorder = Color(180, 195, 220, 255);
    Color       OtherTextColor  = Color(20,  20,  20,  255);
    Color       OtherMetaColor  = Color(80,  90,  110, 255);
    Color       SenderNameColor = Color(60,  80,  160, 255);

    // ── Reply preview strip (inside bubble) ───────────────────────────────
    Color       ReplyStripColor = Color(120, 140, 180, 255);   // Left border accent
    Color       ReplyBgColor    = Color(0,   0,   0,  20);     // Semi-transparent overlay
    Color       ReplyTextColor  = Color(60,  70,  100, 255);
    int         ReplyStripWidth = 3;
    int         ReplyPadH       = 6;
    int         ReplyPadV       = 4;
    int         ReplyHeight     = 38;      // Fixed height of reply preview block

    // ── File attachment card (inside bubble) ──────────────────────────────
    int         FileCardHeight  = 52;      // Height of file attachment card
    int         FileIconSize    = 32;      // File type icon size
    int         FileIconGap     = 8;       // Gap between icon and filename text
    Color       FileCardBg      = Color(0, 0, 0, 15);          // Slightly darker than bubble
    Color       FileNameColor   = Color(20,  20,  20,  255);
    Color       FileSizeColor   = Color(100, 100, 100, 255);

    // ── Image / sticker / video ───────────────────────────────────────────
    int         ImageMaxHeight  = 220;     // Max rendered image height
    int         StickerSize     = 160;     // Sticker is square; no bubble background
    int         VideoThumbHeight= 160;

    // ── Audio voice note ──────────────────────────────────────────────────
    int         AudioBarHeight  = 48;      // Height of voice note row
    Color       AudioWaveColor  = Color(80, 120, 200, 255);

    // ── Delivery ticks ────────────────────────────────────────────────────
    Color       TickSending     = Color(150, 150, 150, 180);   // Faint grey
    Color       TickSent        = Color(150, 150, 150, 255);   // Grey ✓
    Color       TickDelivered   = Color(130, 130, 130, 255);   // Grey ✓✓
    Color       TickRead        = Color(60,  160, 60,  255);   // Green ✓✓
    Color       TickFailed      = Color(200, 50,  50,  255);   // Red !
    int         TickSize        = 10;

    // ── Presence dot (used in contact list, not bubble — kept here for sharing)
    Color       PresenceOnline  = Color(60,  190, 60,  255);
    Color       PresenceAway    = Color(230, 180, 40,  255);
    Color       PresenceDND     = Color(210, 60,  60,  255);
    Color       PresenceOffline = Color(160, 160, 160, 255);
    int         PresenceDotSize = 10;

    // ── Date separator ────────────────────────────────────────────────────
    Color       SepLineColor    = Color(180, 180, 180, 200);
    Color       SepTextColor    = Color(120, 120, 120, 255);
    Color       SepBgColor      = Color(230, 230, 230, 200);
    int         SepHeight       = 28;
    float       SepBgRadius     = 8.0f;

    // ── System event ──────────────────────────────────────────────────────
    Color       SysEventTextColor = Color(100, 100, 100, 255);
    int         SysEventHeight  = 24;
    float       SysEventFontSize = 11.5f;
};

// ============================================================================
// ChatBubbleDelegate
// Implements IItemDelegate for the chat message ListView
// ============================================================================

class ChatBubbleDelegate : public IItemDelegate {
public:
    explicit ChatBubbleDelegate(const ChatBubbleStyle& style = ChatBubbleStyle{});
    ~ChatBubbleDelegate() override = default;

    // ===== IItemDelegate interface =====

    // Main entry point — called by ListView for every visible row
    void RenderItem(IRenderContext* ctx,
                    const IListModel* model,
                    int row,
                    int column,
                    const ListItemStyleOption& option) override;

    // Variable height: each message type and content length has its own height
    int GetRowHeight(const IListModel* model, int row) const override;

    // ===== Style access =====

    void SetStyle(const ChatBubbleStyle& newStyle);
    const ChatBubbleStyle& GetStyle() const;

    // Set the pixel width available for bubble layout (call when view is resized)
    void SetViewWidth(int width);

private:
    ChatBubbleStyle style_;
    int             viewWidth_ = 600;   // Updated by SetViewWidth

    // ── Per-type render methods ───────────────────────────────────────────

    void RenderTextBubble(IRenderContext* ctx,
                          const ListItemStyleOption& option,
                          const IListModel* model, int row,
                          bool isOwn) const;

    void RenderImageBubble(IRenderContext* ctx,
                           const ListItemStyleOption& option,
                           const IListModel* model, int row,
                           bool isOwn) const;

    void RenderFileBubble(IRenderContext* ctx,
                          const ListItemStyleOption& option,
                          const IListModel* model, int row,
                          bool isOwn) const;

    void RenderStickerRow(IRenderContext* ctx,
                          const ListItemStyleOption& option,
                          const IListModel* model, int row,
                          bool isOwn) const;

    void RenderAudioBubble(IRenderContext* ctx,
                           const ListItemStyleOption& option,
                           const IListModel* model, int row,
                           bool isOwn) const;

    void RenderVideoBubble(IRenderContext* ctx,
                           const ListItemStyleOption& option,
                           const IListModel* model, int row,
                           bool isOwn) const;

    void RenderReplyTextBubble(IRenderContext* ctx,
                               const ListItemStyleOption& option,
                               const IListModel* model, int row,
                               bool isOwn) const;

    void RenderForwardedTextBubble(IRenderContext* ctx,
                                   const ListItemStyleOption& option,
                                   const IListModel* model, int row,
                                   bool isOwn) const;

    void RenderDateSeparator(IRenderContext* ctx,
                             const ListItemStyleOption& option,
                             const IListModel* model, int row) const;

    void RenderSystemEvent(IRenderContext* ctx,
                           const ListItemStyleOption& option,
                           const IListModel* model, int row) const;

    // ── Shared sub-components ─────────────────────────────────────────────

    // Draws the circular avatar (image or initials fallback)
    // Returns the x position after the avatar + gap
    int RenderAvatar(IRenderContext* ctx,
                     const std::string& avatarPath,
                     const std::string& senderName,
                     int x, int y) const;

    // Draws the bubble background rounded rect
    // Returns the Rect2Df of the inner content area (after padding)
    Rect2Df RenderBubbleBackground(IRenderContext* ctx,
                                   const Rect2Df& bubbleRect,
                                   bool isOwn) const;

    // Draws the reply preview strip inside a bubble (top of content area)
    void RenderReplyPreview(IRenderContext* ctx,
                            const Rect2Df& contentRect,
                            const std::string& senderName,
                            const std::string& snippet) const;

    // Draws HH:MM timestamp + delivery ticks at bottom-right of content area
    void RenderTimestampAndTicks(IRenderContext* ctx,
                                 const Rect2Df& contentRect,
                                 const std::string& timestamp,
                                 ChatDeliveryStatus status,
                                 bool isOwn) const;

    // Draws the file attachment card inside a bubble content area
    void RenderFileCard(IRenderContext* ctx,
                        const Rect2Df& contentRect,
                        const std::string& fileName,
                        const std::string& fileSize,
                        const std::string& attachPath) const;

    // ── Height calculation helpers ────────────────────────────────────────

    int CalcTextBubbleHeight(const std::string& text, int bubbleWidth) const;
    int CalcFileBubbleHeight() const;
    int CalcImageBubbleHeight(int imgW, int imgH, int bubbleWidth) const;
    int CalcReplyTextBubbleHeight(const std::string& text, int bubbleWidth) const;

    // Pixel width available for a bubble on one side
    int GetBubbleMaxWidth() const;
};

} // namespace ChatCube
} // namespace UltraCanvas
