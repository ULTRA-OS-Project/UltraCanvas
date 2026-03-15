// core/UltraCanvasTextArea_Hex.cpp
// Hex editor mode for UltraCanvasTextArea
// Provides a three-panel hex view: address | hex bytes | ASCII characters

#include "UltraCanvasTextArea.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasClipboard.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {
    void UltraCanvasTextArea::SetRawBytes(const std::vector<uint8_t>& bytes) {
        hexBuffer = bytes;
        hexCursorByteOffset = 0;
        hexCursorInAsciiPanel = false;
        hexCursorNibble = 0;
        hexSelectionStart = -1;
        hexSelectionEnd = -1;
        hexFirstVisibleRow = 0;
        hexUndoStack.clear();
        hexRedoStack.clear();

        if (editingMode == TextAreaEditingMode::Hex) {
            isNeedRecalculateVisibleArea = true;
            RequestRedraw();
        }
    }

    std::vector<uint8_t> UltraCanvasTextArea::GetRawBytes() const {
        if (editingMode == TextAreaEditingMode::Hex) {
            return hexBuffer;
        }
        return std::vector<uint8_t>(textContent.begin(), textContent.end());
    }

// ===== HEX HELPERS =====

    std::string UltraCanvasTextArea::HexFormatAddress(int offset) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08Xh:", offset);
        return std::string(buf);
    }

    std::string UltraCanvasTextArea::HexFormatByte(uint8_t byte) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X", byte);
        return std::string(buf);
    }

    char UltraCanvasTextArea::HexPrintableChar(uint8_t byte) {
        return (byte >= 0x20 && byte <= 0x7E) ? static_cast<char>(byte) : '.';
    }

    int UltraCanvasTextArea::HexGetRowForByte(int byteOffset) const {
        if (hexBytesPerRow <= 0) return 0;
        return byteOffset / hexBytesPerRow;
    }

    int UltraCanvasTextArea::HexGetColumnForByte(int byteOffset) const {
        if (hexBytesPerRow <= 0) return 0;
        return byteOffset % hexBytesPerRow;
    }

// ===== HEX LAYOUT =====

    void UltraCanvasTextArea::CalculateHexLayout() {
        auto ctx = GetRenderContext();
        if (!ctx) return;

        auto bounds = GetBounds();

        ctx->PushState();
        ctx->SetFontStyle(style.fixedFontStyle);
        ctx->SetFontWeight(FontWeight::Normal);

        computedLineHeight = static_cast<int>(
            static_cast<float>(ctx->GetTextLineHeight("M")) * style.lineHeight);
        hexRowHeight = computedLineHeight;

        // Measure character width using monospace font
        int charW = ctx->GetTextLineWidth("0");

        ctx->PopState();

        // Address column: "00000000h: " = 11 chars
        hexAddressWidth = charW * 11;

        // Each hex byte: "XX " = 3 chars
        hexByteWidth = charW * 3;

        // Each ASCII char = 1 char width
        hexAsciiCharWidth = charW;

        // Gap between hex and ASCII panels
        int gapWidth = charW * 2;

        // Available width for hex + ASCII content
        int availWidth = bounds.width - style.padding * 2 - hexAddressWidth - gapWidth;

        // Account for vertical scrollbar
        if (hexTotalRows > 0) {
            int estMaxVisible = std::max(1, (bounds.height - style.padding * 2) / hexRowHeight);
            if (hexTotalRows > estMaxVisible) {
                availWidth -= 15; // scrollbar width
            }
        }

        // Solve: bytesPerRow * (hexByteWidth + hexAsciiCharWidth) <= availWidth
        int perByte = hexByteWidth + hexAsciiCharWidth;
        if (perByte <= 0) perByte = 1;

        int bpr = availWidth / perByte;
        bpr = (bpr / 4) * 4; // Round down to multiple of 4
        hexBytesPerRow = std::max(4, bpr);

        // Compute panel positions
        hexPanelStartX = bounds.x + style.padding + hexAddressWidth;
        hexAsciiPanelStartX = hexPanelStartX + hexBytesPerRow * hexByteWidth + gapWidth;

        // Compute total rows
        int bufSize = static_cast<int>(hexBuffer.size());
        hexTotalRows = (bufSize > 0) ? ((bufSize + hexBytesPerRow - 1) / hexBytesPerRow) : 1;

        // Visible area
        hexVisibleArea = bounds;
        hexVisibleArea.x += style.padding;
        hexVisibleArea.y += style.padding;
        hexVisibleArea.width -= style.padding * 2;
        hexVisibleArea.height -= style.padding * 2;

        // Visible rows
        hexMaxVisibleRows = std::max(1, hexVisibleArea.height / hexRowHeight);

        // Clamp scroll
        int maxFirstRow = std::max(0, hexTotalRows - hexMaxVisibleRows);
        hexFirstVisibleRow = std::max(0, std::min(hexFirstVisibleRow, maxFirstRow));

        isNeedRecalculateVisibleArea = false;
    }

// ===== HEX RENDERING =====

    void UltraCanvasTextArea::DrawHexView(IRenderContext* ctx) {
        ctx->PushState();
        ctx->ClipRect(hexVisibleArea);
        ctx->SetFontStyle(style.fixedFontStyle);
        ctx->SetFontWeight(FontWeight::Normal);

        DrawHexCurrentRowHighlight(ctx);
        DrawHexAddresses(ctx);
        DrawHexBytes(ctx);
        DrawHexAscii(ctx);

        if (hexSelectionStart >= 0 && hexSelectionEnd >= 0 && hexSelectionStart != hexSelectionEnd) {
            DrawHexSelection(ctx);
        }
        
        DrawHexCrossHighlight(ctx); 

        if (IsFocused() && cursorVisible && !isReadOnly) {
            DrawHexCursor(ctx);
        }

        ctx->PopState();
    }

    void UltraCanvasTextArea::DrawHexCurrentRowHighlight(IRenderContext* ctx) {
        int cursorRow = HexGetRowForByte(hexCursorByteOffset);
        int displayRow = cursorRow - hexFirstVisibleRow;

        if (displayRow >= 0 && displayRow < hexMaxVisibleRows) {
            int y = hexVisibleArea.y + displayRow * hexRowHeight;
            ctx->SetFillPaint(style.currentLineHighlightColor);
            ctx->FillRectangle(hexVisibleArea.x, y, hexVisibleArea.width, hexRowHeight);
        }
    }

    void UltraCanvasTextArea::DrawHexAddresses(IRenderContext* ctx) {
        auto bounds = GetBounds();

        // Draw address gutter background
        ctx->SetFillPaint(style.lineNumbersBackgroundColor);
        ctx->FillRectangle(bounds.x, hexVisibleArea.y, hexAddressWidth + style.padding, hexVisibleArea.height);

        // Draw separator line
        int separatorX = bounds.x + style.padding + hexAddressWidth - 4;
        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(1);
        ctx->DrawLine(separatorX, hexVisibleArea.y, separatorX, hexVisibleArea.y + hexVisibleArea.height);

        ctx->SetTextPaint(style.lineNumbersColor);

        int endRow = std::min(hexTotalRows, hexFirstVisibleRow + hexMaxVisibleRows + 1);
        for (int row = hexFirstVisibleRow; row < endRow; row++) {
            int y = hexVisibleArea.y + (row - hexFirstVisibleRow) * hexRowHeight;
            int address = row * hexBytesPerRow;
            std::string addrStr = HexFormatAddress(address);
            ctx->DrawText(addrStr, hexVisibleArea.x, y);
        }
    }

    void UltraCanvasTextArea::DrawHexBytes(IRenderContext* ctx) {
        int bufSize = static_cast<int>(hexBuffer.size());
        int endRow = std::min(hexTotalRows, hexFirstVisibleRow + hexMaxVisibleRows + 1);

        ctx->SetTextPaint(style.fontColor);

        for (int row = hexFirstVisibleRow; row < endRow; row++) {
            int y = hexVisibleArea.y + (row - hexFirstVisibleRow) * hexRowHeight;
            int rowStart = row * hexBytesPerRow;
            int rowEnd = std::min(rowStart + hexBytesPerRow, bufSize);

            for (int i = rowStart; i < rowEnd; i++) {
                int col = i - rowStart;
                int x = hexPanelStartX + col * hexByteWidth;
                std::string byteStr = HexFormatByte(hexBuffer[i]);
                ctx->DrawText(byteStr, x, y);
            }
        }
    }

    void UltraCanvasTextArea::DrawHexAscii(IRenderContext* ctx) {
        int bufSize = static_cast<int>(hexBuffer.size());
        int endRow = std::min(hexTotalRows, hexFirstVisibleRow + hexMaxVisibleRows + 1);

        // Draw separator line before ASCII panel
        int separatorX = hexAsciiPanelStartX - hexAsciiCharWidth;
        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(1);
        ctx->DrawLine(separatorX, hexVisibleArea.y, separatorX, hexVisibleArea.y + hexVisibleArea.height);

        ctx->SetTextPaint(style.fontColor);

        for (int row = hexFirstVisibleRow; row < endRow; row++) {
            int y = hexVisibleArea.y + (row - hexFirstVisibleRow) * hexRowHeight;
            int rowStart = row * hexBytesPerRow;
            int rowEnd = std::min(rowStart + hexBytesPerRow, bufSize);

            // Build ASCII string for the row
            std::string asciiStr;
            asciiStr.reserve(rowEnd - rowStart);
            for (int i = rowStart; i < rowEnd; i++) {
                asciiStr += HexPrintableChar(hexBuffer[i]);
            }
            ctx->DrawText(asciiStr, hexAsciiPanelStartX, y);
        }
    }

    void UltraCanvasTextArea::DrawHexSelection(IRenderContext* ctx) {
        if (hexSelectionStart < 0 || hexSelectionEnd < 0 || hexSelectionStart == hexSelectionEnd) return;

        int selStart = std::min(hexSelectionStart, hexSelectionEnd);
        int selEnd = std::max(hexSelectionStart, hexSelectionEnd);
        int bufSize = static_cast<int>(hexBuffer.size());

        selStart = std::max(0, selStart);
        selEnd = std::min(selEnd, bufSize);

        ctx->SetFillPaint(style.selectionColor);

        int endRow = std::min(hexTotalRows, hexFirstVisibleRow + hexMaxVisibleRows + 1);
        for (int row = hexFirstVisibleRow; row < endRow; row++) {
            int rowStart = row * hexBytesPerRow;
            int rowEnd = std::min(rowStart + hexBytesPerRow, bufSize);

            int overlapStart = std::max(selStart, rowStart);
            int overlapEnd = std::min(selEnd, rowEnd);

            if (overlapStart >= overlapEnd) continue;

            int y = hexVisibleArea.y + (row - hexFirstVisibleRow) * hexRowHeight;
            int startCol = overlapStart - rowStart;
            int endCol = overlapEnd - rowStart;

            // Highlight in hex panel
            int hexX = hexPanelStartX + startCol * hexByteWidth;
            int hexW = (endCol - startCol) * hexByteWidth;
            ctx->FillRectangle(hexX, y, hexW, hexRowHeight);

            // Highlight in ASCII panel
            int asciiX = hexAsciiPanelStartX + startCol * hexAsciiCharWidth;
            int asciiW = (endCol - startCol) * hexAsciiCharWidth;
            ctx->FillRectangle(asciiX, y, asciiW, hexRowHeight);
        }
    }

    void UltraCanvasTextArea::DrawHexCursor(IRenderContext* ctx) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0 && hexCursorByteOffset == 0) {
            // Empty buffer — show cursor at start
        } else if (hexCursorByteOffset >= bufSize) {
            return;
        }

        int cursorRow = HexGetRowForByte(hexCursorByteOffset);
        int displayRow = cursorRow - hexFirstVisibleRow;

        if (displayRow < 0 || displayRow >= hexMaxVisibleRows) return;

        int y = hexVisibleArea.y + displayRow * hexRowHeight;
        int col = HexGetColumnForByte(hexCursorByteOffset);

        ctx->PushState();

        if (hexCursorInAsciiPanel) {
            // Line cursor in ASCII panel
            int x = hexAsciiPanelStartX + col * hexAsciiCharWidth;
            ctx->SetStrokeWidth(2);
            ctx->DrawLine(x, y, x, y + hexRowHeight, style.cursorColor);
        } else {
            // Block cursor in hex panel on current nibble
            int charW = hexByteWidth / 3; // Width of one hex digit
            int x = hexPanelStartX + col * hexByteWidth + hexCursorNibble * charW;
            ctx->SetFillPaint(Color(style.cursorColor.r, style.cursorColor.g, style.cursorColor.b, 80));
            ctx->FillRectangle(x, y, charW, hexRowHeight);
            // Also draw line cursor
            ctx->SetStrokeWidth(2);
            ctx->DrawLine(x, y, x, y + hexRowHeight, style.cursorColor);
        }

        ctx->PopState();
    }

// ===== HEX HIT TESTING =====

    // Returns {byteOffset, panel} where panel: 0=address, 1=hex, 2=ascii
    std::pair<int,int> UltraCanvasTextArea::HexHitTestPoint(int mouseX, int mouseY) const {
        int relY = mouseY - hexVisibleArea.y;
        int row = hexFirstVisibleRow + relY / hexRowHeight;

        if (row < 0) row = 0;
        if (row >= hexTotalRows) row = hexTotalRows - 1;
        if (row < 0) return {0, 0};

        int rowStart = row * hexBytesPerRow;
        int bufSize = static_cast<int>(hexBuffer.size());
        int rowEnd = std::min(rowStart + hexBytesPerRow, bufSize);

        // Determine which panel was clicked
        if (mouseX >= hexAsciiPanelStartX) {
            // ASCII panel
            int col = (mouseX - hexAsciiPanelStartX) / std::max(1, hexAsciiCharWidth);
            col = std::max(0, std::min(col, rowEnd - rowStart - 1));
            int byteOff = rowStart + col;
            byteOff = std::max(0, std::min(byteOff, bufSize - 1));
            return {byteOff, 2};
        } else if (mouseX >= hexPanelStartX) {
            // Hex panel
            int col = (mouseX - hexPanelStartX) / std::max(1, hexByteWidth);
            col = std::max(0, std::min(col, rowEnd - rowStart - 1));
            int byteOff = rowStart + col;
            byteOff = std::max(0, std::min(byteOff, bufSize - 1));
            return {byteOff, 1};
        }

        // Address panel — map to first byte of row
        int byteOff = std::min(rowStart, std::max(0, bufSize - 1));
        return {byteOff, 0};
    }

// ===== HEX EVENT HANDLING =====

    bool UltraCanvasTextArea::HandleHexMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        // Scrollbar thumb dragging takes priority
        if (IsNeedVerticalScrollbar() && verticalScrollThumb.Contains(event.x, event.y)) {
            isDraggingVerticalThumb = true;
            dragStartOffset.y = event.globalY - verticalScrollThumb.y;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        SetFocus(true);

        if (hexBuffer.empty()) return true;

        auto [byteOff, panel] = HexHitTestPoint(event.x, event.y);

        if (event.shift && hexSelectionStart >= 0) {
            // Shift-click: extend selection
            hexSelectionEnd = byteOff;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
            hexSelectionAnchor = byteOff;
        }

        hexCursorByteOffset = byteOff;
        hexCursorNibble = 0;
        hexCursorInAsciiPanel = (panel == 2);

        hexIsSelectingWithMouse = true;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);

        RequestRedraw();

        if (onCursorPositionChanged) {
            onCursorPositionChanged(HexGetRowForByte(hexCursorByteOffset),
                                    HexGetColumnForByte(hexCursorByteOffset));
        }
        return true;
    }

    bool UltraCanvasTextArea::HandleHexMouseMove(const UCEvent& event) {
        // Scrollbar thumb dragging
        if (isDraggingVerticalThumb) {
            auto bounds = GetBounds();
            int scrollbarHeight = bounds.height;
            int thumbHeight = verticalScrollThumb.height;
            int maxThumbY = scrollbarHeight - thumbHeight;

            int newThumbY = event.globalY - dragStartOffset.y - bounds.y;
            newThumbY = std::max(0, std::min(newThumbY, maxThumbY));

            if (maxThumbY > 0 && hexTotalRows > hexMaxVisibleRows) {
                hexFirstVisibleRow = (newThumbY * (hexTotalRows - hexMaxVisibleRows)) / maxThumbY;
                hexFirstVisibleRow = std::max(0, std::min(hexFirstVisibleRow, hexTotalRows - hexMaxVisibleRows));
            }

            RequestRedraw();
            return true;
        }

        if (hexIsSelectingWithMouse && hexSelectionAnchor >= 0 && !hexBuffer.empty()) {
            auto [byteOff, panel] = HexHitTestPoint(event.x, event.y);
            hexSelectionStart = hexSelectionAnchor;
            hexSelectionEnd = byteOff;
            hexCursorByteOffset = byteOff;

            // Auto-scroll near edges
            if (event.y < hexVisibleArea.y) {
                hexFirstVisibleRow = std::max(0, hexFirstVisibleRow - 1);
            } else if (event.y > hexVisibleArea.y + hexVisibleArea.height) {
                int maxFirstRow = std::max(0, hexTotalRows - hexMaxVisibleRows);
                hexFirstVisibleRow = std::min(maxFirstRow, hexFirstVisibleRow + 1);
            }

            RequestRedraw();

            if (onSelectionChanged) {
                onSelectionChanged(hexSelectionStart, hexSelectionEnd);
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasTextArea::HandleHexKeyDown(const UCEvent& event) {
        if (hexBuffer.empty() && event.virtualKey != UCKeys::Escape) {
            // Allow typing in empty buffer only for paste
            if (event.ctrl && event.virtualKey == UCKeys::V) {
                // Handle paste below
            } else {
                return false;
            }
        }

        bool handled = true;

        switch (event.virtualKey) {
            case UCKeys::Tab:
                // Toggle between hex and ASCII panels
                hexCursorInAsciiPanel = !hexCursorInAsciiPanel;
                hexCursorNibble = 0;
                RequestRedraw();
                break;

            case UCKeys::Left:
                HexMoveCursorLeft(event.shift);
                break;

            case UCKeys::Right:
                HexMoveCursorRight(event.shift);
                break;

            case UCKeys::Up:
                HexMoveCursorUp(event.shift);
                break;

            case UCKeys::Down:
                HexMoveCursorDown(event.shift);
                break;

            case UCKeys::Home:
                if (event.ctrl) {
                    // Ctrl+Home: go to start
                    int oldPos = hexCursorByteOffset;
                    hexCursorByteOffset = 0;
                    hexCursorNibble = 0;
                    if (event.shift) {
                        if (hexSelectionStart < 0) hexSelectionStart = oldPos;
                        hexSelectionEnd = 0;
                    } else {
                        hexSelectionStart = -1;
                        hexSelectionEnd = -1;
                    }
                    HexEnsureCursorVisible();
                    RequestRedraw();
                } else {
                    HexMoveCursorToRowStart(event.shift);
                }
                break;

            case UCKeys::End:
                if (event.ctrl) {
                    // Ctrl+End: go to end
                    int oldPos = hexCursorByteOffset;
                    hexCursorByteOffset = std::max(0, static_cast<int>(hexBuffer.size()) - 1);
                    hexCursorNibble = 0;
                    if (event.shift) {
                        if (hexSelectionStart < 0) hexSelectionStart = oldPos;
                        hexSelectionEnd = hexCursorByteOffset;
                    } else {
                        hexSelectionStart = -1;
                        hexSelectionEnd = -1;
                    }
                    HexEnsureCursorVisible();
                    RequestRedraw();
                } else {
                    HexMoveCursorToRowEnd(event.shift);
                }
                break;

            case UCKeys::PageUp:
                HexMoveCursorPageUp(event.shift);
                break;

            case UCKeys::PageDown:
                HexMoveCursorPageDown(event.shift);
                break;

            case UCKeys::Delete:
                if (!isReadOnly) {
                    if (hexSelectionStart >= 0 && hexSelectionEnd >= 0 && hexSelectionStart != hexSelectionEnd) {
                        HexSaveState();
                        int s = std::min(hexSelectionStart, hexSelectionEnd);
                        int e = std::max(hexSelectionStart, hexSelectionEnd);
                        hexBuffer.erase(hexBuffer.begin() + s, hexBuffer.begin() + e);
                        hexCursorByteOffset = s;
                        hexSelectionStart = -1;
                        hexSelectionEnd = -1;
                    } else {
                        HexDeleteByte();
                    }
                    isNeedRecalculateVisibleArea = true;
                    RequestRedraw();
                }
                break;

            case UCKeys::Backspace:
                if (!isReadOnly) {
                    if (hexSelectionStart >= 0 && hexSelectionEnd >= 0 && hexSelectionStart != hexSelectionEnd) {
                        HexSaveState();
                        int s = std::min(hexSelectionStart, hexSelectionEnd);
                        int e = std::max(hexSelectionStart, hexSelectionEnd);
                        hexBuffer.erase(hexBuffer.begin() + s, hexBuffer.begin() + e);
                        hexCursorByteOffset = s;
                        hexSelectionStart = -1;
                        hexSelectionEnd = -1;
                    } else {
                        HexDeleteByteBackward();
                    }
                    isNeedRecalculateVisibleArea = true;
                    RequestRedraw();
                }
                break;

            case UCKeys::A:
                if (event.ctrl && !event.alt) {
                    // Select all
                    hexSelectionStart = 0;
                    hexSelectionEnd = static_cast<int>(hexBuffer.size());
                    hexCursorByteOffset = static_cast<int>(hexBuffer.size()) - 1;
                    RequestRedraw();
                } else {
                    handled = false;
                }
                break;

            case UCKeys::C:
                if (event.ctrl && !event.alt) {
                    // Copy
                    if (hexSelectionStart >= 0 && hexSelectionEnd >= 0 && hexSelectionStart != hexSelectionEnd) {
                        int s = std::min(hexSelectionStart, hexSelectionEnd);
                        int e = std::max(hexSelectionStart, hexSelectionEnd);
                        std::string clipStr;
                        for (int i = s; i < e && i < static_cast<int>(hexBuffer.size()); i++) {
                            if (!clipStr.empty()) clipStr += ' ';
                            clipStr += HexFormatByte(hexBuffer[i]);
                        }
                        SetClipboardText(clipStr);
                    }
                } else {
                    handled = false;
                }
                break;

            case UCKeys::V:
                if (event.ctrl && !event.alt && !isReadOnly) {
                    // Paste hex bytes
                    std::string clipText;
                    if (GetClipboardText(clipText) && !clipText.empty()) {
                        HexSaveState();
                        // Try to parse as hex bytes (space-separated)
                        std::istringstream iss(clipText);
                        std::string token;
                        std::vector<uint8_t> pasteBytes;
                        bool isHex = true;
                        while (iss >> token) {
                            if (token.size() <= 2) {
                                unsigned int val;
                                if (std::sscanf(token.c_str(), "%x", &val) == 1 && val <= 0xFF) {
                                    pasteBytes.push_back(static_cast<uint8_t>(val));
                                    continue;
                                }
                            }
                            isHex = false;
                            break;
                        }

                        if (!isHex || pasteBytes.empty()) {
                            // Paste as raw ASCII bytes
                            pasteBytes.assign(clipText.begin(), clipText.end());
                        }

                        // Delete selection first if any
                        if (hexSelectionStart >= 0 && hexSelectionEnd >= 0 && hexSelectionStart != hexSelectionEnd) {
                            int s = std::min(hexSelectionStart, hexSelectionEnd);
                            int e = std::max(hexSelectionStart, hexSelectionEnd);
                            hexBuffer.erase(hexBuffer.begin() + s, hexBuffer.begin() + e);
                            hexCursorByteOffset = s;
                            hexSelectionStart = -1;
                            hexSelectionEnd = -1;
                        }

                        int pos = std::min(hexCursorByteOffset, static_cast<int>(hexBuffer.size()));
                        hexBuffer.insert(hexBuffer.begin() + pos, pasteBytes.begin(), pasteBytes.end());
                        hexCursorByteOffset = pos + static_cast<int>(pasteBytes.size());
                        if (hexCursorByteOffset > 0) hexCursorByteOffset--;

                        isNeedRecalculateVisibleArea = true;
                        HexEnsureCursorVisible();
                        RequestRedraw();

                        if (onTextChanged) {
                            onTextChanged(std::string(hexBuffer.begin(), hexBuffer.end()));
                        }
                    }
                } else {
                    handled = false;
                }
                break;

            case UCKeys::Z:
                if (event.ctrl) {
                    if (event.shift) HexRedo();
                    else HexUndo();
                } else {
                    handled = false;
                }
                break;

            case UCKeys::Y:
                if (event.ctrl) {
                    HexRedo();
                } else {
                    handled = false;
                }
                break;

            default:
                handled = false;
                break;
        }

        // Handle hex digit input in hex panel
        if (!handled && !event.ctrl && !event.alt && !isReadOnly) {
            if (!hexCursorInAsciiPanel) {
                // Hex panel: accept 0-9, a-f, A-F
                int nibbleVal = -1;
                if (!event.text.empty()) {
                    char ch = event.text[0];
                    if (ch >= '0' && ch <= '9') nibbleVal = ch - '0';
                    else if (ch >= 'a' && ch <= 'f') nibbleVal = ch - 'a' + 10;
                    else if (ch >= 'A' && ch <= 'F') nibbleVal = ch - 'A' + 10;
                }

                if (nibbleVal >= 0) {
                    HexOverwriteNibble(nibbleVal);
                    handled = true;
                }
            } else {
                // ASCII panel: accept printable characters
                if (!event.text.empty()) {
                    char ch = event.text[0];
                    if (ch >= 0x20 && ch <= 0x7E) {
                        HexOverwriteAscii(ch);
                        handled = true;
                    }
                } else if (event.character != 0 && event.character >= 0x20 && event.character <= 0x7E) {
                    HexOverwriteAscii(static_cast<char>(event.character));
                    handled = true;
                }
            }
        }

        if (handled && onCursorPositionChanged) {
            onCursorPositionChanged(HexGetRowForByte(hexCursorByteOffset),
                                    HexGetColumnForByte(hexCursorByteOffset));
        }

        return handled;
    }

// ===== HEX CURSOR MOVEMENT =====

    void UltraCanvasTextArea::HexMoveCursorLeft(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;

        if (!hexCursorInAsciiPanel && hexCursorNibble == 1) {
            // Move from low nibble to high nibble
            hexCursorNibble = 0;
        } else if (hexCursorByteOffset > 0) {
            hexCursorByteOffset--;
            hexCursorNibble = hexCursorInAsciiPanel ? 0 : 1;
        }

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorRight(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;

        if (!hexCursorInAsciiPanel && hexCursorNibble == 0) {
            // Move from high nibble to low nibble
            hexCursorNibble = 1;
        } else if (hexCursorByteOffset < bufSize - 1) {
            hexCursorByteOffset++;
            hexCursorNibble = 0;
        }

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorUp(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;

        if (hexCursorByteOffset >= hexBytesPerRow) {
            hexCursorByteOffset -= hexBytesPerRow;
        }
        hexCursorNibble = 0;

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorDown(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;

        if (hexCursorByteOffset + hexBytesPerRow < bufSize) {
            hexCursorByteOffset += hexBytesPerRow;
        } else {
            hexCursorByteOffset = bufSize - 1;
        }
        hexCursorNibble = 0;

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorPageUp(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;
        int pageBytes = hexMaxVisibleRows * hexBytesPerRow;
        hexCursorByteOffset = std::max(0, hexCursorByteOffset - pageBytes);
        hexCursorNibble = 0;

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorPageDown(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;
        int pageBytes = hexMaxVisibleRows * hexBytesPerRow;
        hexCursorByteOffset = std::min(bufSize - 1, hexCursorByteOffset + pageBytes);
        hexCursorNibble = 0;

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorToRowStart(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;
        int row = HexGetRowForByte(hexCursorByteOffset);
        hexCursorByteOffset = row * hexBytesPerRow;
        hexCursorNibble = 0;

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexMoveCursorToRowEnd(bool selecting) {
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0) return;

        int oldPos = hexCursorByteOffset;
        int row = HexGetRowForByte(hexCursorByteOffset);
        hexCursorByteOffset = std::min((row + 1) * hexBytesPerRow - 1, bufSize - 1);
        hexCursorNibble = 0;

        if (selecting) {
            if (hexSelectionStart < 0) hexSelectionStart = oldPos;
            hexSelectionEnd = hexCursorByteOffset;
        } else {
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
        }

        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexEnsureCursorVisible() {
        int cursorRow = HexGetRowForByte(hexCursorByteOffset);

        if (cursorRow < hexFirstVisibleRow) {
            hexFirstVisibleRow = cursorRow;
        } else if (cursorRow >= hexFirstVisibleRow + hexMaxVisibleRows) {
            hexFirstVisibleRow = cursorRow - hexMaxVisibleRows + 1;
        }

        int maxFirstRow = std::max(0, hexTotalRows - hexMaxVisibleRows);
        hexFirstVisibleRow = std::max(0, std::min(hexFirstVisibleRow, maxFirstRow));
    }

// ===== HEX EDITING =====

    void UltraCanvasTextArea::HexOverwriteNibble(int nibbleValue) {
        if (isReadOnly) return;
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0 || hexCursorByteOffset >= bufSize) return;

        HexSaveState();

        uint8_t& byte = hexBuffer[hexCursorByteOffset];
        if (hexCursorNibble == 0) {
            // High nibble
            byte = (byte & 0x0F) | (static_cast<uint8_t>(nibbleValue) << 4);
            hexCursorNibble = 1;
        } else {
            // Low nibble
            byte = (byte & 0xF0) | static_cast<uint8_t>(nibbleValue);
            hexCursorNibble = 0;
            // Advance to next byte
            if (hexCursorByteOffset < bufSize - 1) {
                hexCursorByteOffset++;
            }
        }

        hexSelectionStart = -1;
        hexSelectionEnd = -1;

        HexEnsureCursorVisible();
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(std::string(hexBuffer.begin(), hexBuffer.end()));
        }
    }

    void UltraCanvasTextArea::HexOverwriteAscii(char ch) {
        if (isReadOnly) return;
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0 || hexCursorByteOffset >= bufSize) return;

        HexSaveState();

        hexBuffer[hexCursorByteOffset] = static_cast<uint8_t>(ch);

        // Advance to next byte
        if (hexCursorByteOffset < bufSize - 1) {
            hexCursorByteOffset++;
        }

        hexSelectionStart = -1;
        hexSelectionEnd = -1;

        HexEnsureCursorVisible();
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(std::string(hexBuffer.begin(), hexBuffer.end()));
        }
    }

    void UltraCanvasTextArea::HexDeleteByte() {
        if (isReadOnly) return;
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0 || hexCursorByteOffset >= bufSize) return;

        HexSaveState();
        hexBuffer.erase(hexBuffer.begin() + hexCursorByteOffset);

        if (hexCursorByteOffset >= static_cast<int>(hexBuffer.size()) && hexCursorByteOffset > 0) {
            hexCursorByteOffset--;
        }
        hexCursorNibble = 0;

        isNeedRecalculateVisibleArea = true;
        HexEnsureCursorVisible();
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(std::string(hexBuffer.begin(), hexBuffer.end()));
        }
    }

    void UltraCanvasTextArea::HexDeleteByteBackward() {
        if (isReadOnly || hexCursorByteOffset == 0) return;

        HexSaveState();
        hexCursorByteOffset--;
        hexBuffer.erase(hexBuffer.begin() + hexCursorByteOffset);

        if (hexCursorByteOffset >= static_cast<int>(hexBuffer.size()) && hexCursorByteOffset > 0) {
            hexCursorByteOffset--;
        }
        hexCursorNibble = 0;

        isNeedRecalculateVisibleArea = true;
        HexEnsureCursorVisible();
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(std::string(hexBuffer.begin(), hexBuffer.end()));
        }
    }

// ===== HEX UNDO/REDO =====

    void UltraCanvasTextArea::HexSaveState() {
        HexState state;
        state.data = hexBuffer;
        state.cursorByteOffset = hexCursorByteOffset;
        state.cursorInAsciiPanel = hexCursorInAsciiPanel;
        state.selectionStart = hexSelectionStart;
        state.selectionEnd = hexSelectionEnd;

        hexUndoStack.push_back(std::move(state));
        if (hexUndoStack.size() > maxUndoStackSize) {
            hexUndoStack.erase(hexUndoStack.begin());
        }
        hexRedoStack.clear();
    }

    void UltraCanvasTextArea::HexUndo() {
        if (hexUndoStack.empty()) return;

        HexState currentState;
        currentState.data = hexBuffer;
        currentState.cursorByteOffset = hexCursorByteOffset;
        currentState.cursorInAsciiPanel = hexCursorInAsciiPanel;
        currentState.selectionStart = hexSelectionStart;
        currentState.selectionEnd = hexSelectionEnd;
        hexRedoStack.push_back(std::move(currentState));

        HexState prev = std::move(hexUndoStack.back());
        hexUndoStack.pop_back();

        hexBuffer = std::move(prev.data);
        hexCursorByteOffset = prev.cursorByteOffset;
        hexCursorInAsciiPanel = prev.cursorInAsciiPanel;
        hexSelectionStart = prev.selectionStart;
        hexSelectionEnd = prev.selectionEnd;
        hexCursorNibble = 0;

        isNeedRecalculateVisibleArea = true;
        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::HexRedo() {
        if (hexRedoStack.empty()) return;

        HexState currentState;
        currentState.data = hexBuffer;
        currentState.cursorByteOffset = hexCursorByteOffset;
        currentState.cursorInAsciiPanel = hexCursorInAsciiPanel;
        currentState.selectionStart = hexSelectionStart;
        currentState.selectionEnd = hexSelectionEnd;
        hexUndoStack.push_back(std::move(currentState));

        HexState next = std::move(hexRedoStack.back());
        hexRedoStack.pop_back();

        hexBuffer = std::move(next.data);
        hexCursorByteOffset = next.cursorByteOffset;
        hexCursorInAsciiPanel = next.cursorInAsciiPanel;
        hexSelectionStart = next.selectionStart;
        hexSelectionEnd = next.selectionEnd;
        hexCursorNibble = 0;

        isNeedRecalculateVisibleArea = true;
        HexEnsureCursorVisible();
        RequestRedraw();
    }

    void UltraCanvasTextArea::DrawHexCrossHighlight(IRenderContext* ctx) {
        // Nothing to highlight if buffer is empty or cursor is out of range
        int bufSize = static_cast<int>(hexBuffer.size());
        if (bufSize == 0 || hexCursorByteOffset < 0 || hexCursorByteOffset >= bufSize) return;
 
        int cursorRow = HexGetRowForByte(hexCursorByteOffset);
        int displayRow = cursorRow - hexFirstVisibleRow;
 
        // Only draw if the cursor row is visible
        if (displayRow < 0 || displayRow >= hexMaxVisibleRows) return;
 
        int col = HexGetColumnForByte(hexCursorByteOffset);
        int y   = hexVisibleArea.y + displayRow * hexRowHeight;
 
        // Use cursor color at low alpha for a soft echo highlight
        // that is clearly distinct from the selection (which uses selectionColor)
        Color echoColor = Color(
            style.cursorColor.r,
            style.cursorColor.g,
            style.cursorColor.b,
            55   // ~22% opacity — visible but non-intrusive
        );
 
        ctx->PushState();
        ctx->SetFillPaint(echoColor);
 
        if (hexCursorInAsciiPanel) {
            // Cursor is in ASCII panel → highlight the corresponding byte in the HEX panel
            int x = hexPanelStartX + col * hexByteWidth;
            ctx->FillRectangle(x, y, hexByteWidth, hexRowHeight);
        } else {
            // Cursor is in HEX panel → highlight the corresponding char in the ASCII panel
            int x = hexAsciiPanelStartX + col * hexAsciiCharWidth;
            ctx->FillRectangle(x, y, hexAsciiCharWidth, hexRowHeight);
        }
 
        ctx->PopState();
    }
} // namespace UltraCanvas
