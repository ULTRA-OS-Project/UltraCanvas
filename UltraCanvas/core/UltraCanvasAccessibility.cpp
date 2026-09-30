// core/UltraCanvasAccessibility.cpp
// The platform-neutral accessibility layer: listeners and text helpers.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasAccessibility.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace UltraCanvas {

namespace {

std::mutex& ListenerMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<int, UltraCanvasAccessibility::Listener>& Listeners() {
    static std::map<int, UltraCanvasAccessibility::Listener> listeners;
    return listeners;
}

int& NextListenerId() {
    static int id = 0;
    return id;
}

bool IsContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

} // namespace

int UltraCanvasAccessibility::AddListener(Listener listener) {
    std::lock_guard<std::mutex> lock(ListenerMutex());
    const int id = ++NextListenerId();
    Listeners()[id] = std::move(listener);
    return id;
}

void UltraCanvasAccessibility::RemoveListener(int id) {
    std::lock_guard<std::mutex> lock(ListenerMutex());
    Listeners().erase(id);
}

bool UltraCanvasAccessibility::HasListeners() {
    std::lock_guard<std::mutex> lock(ListenerMutex());
    return !Listeners().empty();
}

void UltraCanvasAccessibility::Notify(const AccessibilityEvent& event) {
    std::vector<Listener> listeners;
    {
        std::lock_guard<std::mutex> lock(ListenerMutex());
        for (const auto& [id, listener] : Listeners()) listeners.push_back(listener);
    }
    // Called outside the lock: a listener may add or remove listeners.
    for (const Listener& listener : listeners) listener(event);
}

int UltraCanvasAccessibility::CharacterCount(const std::string& utf8) {
    int count = 0;
    for (unsigned char c : utf8) count += IsContinuation(c) ? 0 : 1;
    return count;
}

size_t UltraCanvasAccessibility::ByteOffsetOfCharacter(const std::string& utf8, int character) {
    if (character <= 0) return 0;
    int seen = 0;
    for (size_t i = 0; i < utf8.size(); i++) {
        if (IsContinuation(static_cast<unsigned char>(utf8[i]))) continue;
        if (seen == character) return i;
        seen++;
    }
    return utf8.size();
}

int UltraCanvasAccessibility::CharacterOffsetOfByte(const std::string& utf8, size_t byte) {
    byte = std::min(byte, utf8.size());
    int count = 0;
    for (size_t i = 0; i < byte; i++) count += IsContinuation(static_cast<unsigned char>(utf8[i])) ? 0 : 1;
    return count;
}

std::string UltraCanvasAccessibility::TextUnitAt(const std::string& text, int offset, AccessibleTextBoundary boundary,
                                                 int& start, int& end) {
    // Worked in code points.
    std::vector<std::string> characters;
    for (size_t i = 0; i < text.size();) {
        size_t next = i + 1;
        while (next < text.size() && IsContinuation(static_cast<unsigned char>(text[next]))) next++;
        characters.push_back(text.substr(i, next - i));
        i = next;
    }
    const int count = static_cast<int>(characters.size());
    offset = std::clamp(offset, 0, std::max(0, count - 1));
    auto isSpace = [&](int i) { return characters[static_cast<size_t>(i)] == " " || characters[static_cast<size_t>(i)] == "\t"; };
    auto isNewline = [&](int i) { return characters[static_cast<size_t>(i)] == "\n"; };
    auto isWordCharacter = [&](int i) {
        const std::string& c = characters[static_cast<size_t>(i)];
        if (c.size() > 1) return true;             // letters of other scripts
        return std::isalnum(static_cast<unsigned char>(c[0])) != 0 || c[0] == '_' || c[0] == '\'';
    };
    auto isSentenceEnd = [&](int i) {
        const std::string& c = characters[static_cast<size_t>(i)];
        return c == "." || c == "!" || c == "?" || c == "\xE3\x80\x82";
    };
    start = end = offset;
    if (count == 0) {
        start = end = 0;
        return "";
    }
    switch (boundary) {
        case AccessibleTextBoundary::Character:
            end = offset + 1;
            break;
        case AccessibleTextBoundary::Word: {
            // The word and the spaces after it, as the platform APIs count.
            const bool word = isWordCharacter(offset);
            while (start > 0 && isWordCharacter(start - 1) == word && !isNewline(start - 1)) start--;
            end = offset;
            while (end < count && isWordCharacter(end) == word && !isNewline(end)) end++;
            while (end < count && isSpace(end)) end++;
            if (end == start) end = start + 1;
            break;
        }
        case AccessibleTextBoundary::Sentence:
            while (start > 0 && !isNewline(start - 1)
                   && !(isSentenceEnd(start - 1) && start < count && isSpace(start))) start--;
            while (start < count && isSpace(start) && start < offset) start++;
            end = offset;
            while (end < count && !isNewline(end) && !isSentenceEnd(end)) end++;
            if (end < count && isSentenceEnd(end)) end++;
            while (end < count && isSpace(end)) end++;
            break;
        case AccessibleTextBoundary::Line:
        case AccessibleTextBoundary::Paragraph:
            while (start > 0 && !isNewline(start - 1)) start--;
            end = offset;
            while (end < count && !isNewline(end)) end++;
            if (end < count) end++;                // with its line feed
            break;
    }
    std::string unit;
    for (int i = start; i < end && i < count; i++) unit += characters[static_cast<size_t>(i)];
    return unit;
}

std::string IAccessibleText::GetTextAtOffset(int offset, AccessibleTextBoundary boundary, int& start, int& end) const {
    return UltraCanvasAccessibility::TextUnitAt(GetAccessibleText(), offset, boundary, start, end);
}

} // namespace UltraCanvas
