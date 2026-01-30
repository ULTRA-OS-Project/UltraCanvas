// UCStringTest.cpp
// Test file for UCString UTF-8 aware string class
// Version: 1.0.0
// Last Modified: 2026-01-30
// Author: UltraCanvas Framework

#include "UltraCanvasString.h"
#include <iostream>
#include <cassert>
#include <iomanip>

using namespace UltraCanvas;
using namespace UltraCanvas::StringLiterals;

// Helper to print test results
#define TEST(name, condition) \
    do { \
        bool passed = (condition); \
        std::cout << (passed ? "✓ PASS" : "✗ FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++; \
        testCount++; \
    } while(0)

int main() {
    int testCount = 0;
    int failCount = 0;

    std::cout << "========================================" << std::endl;
    std::cout << "   UCString UTF-8 Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // ===== CONSTRUCTOR TESTS =====
    std::cout << "--- Constructor Tests ---" << std::endl;

    {
        UCString empty;
        TEST("Default constructor creates empty string", empty.Empty());
        TEST("Empty string has zero length", empty.Length() == 0);
        TEST("Empty string has zero byte length", empty.ByteLength() == 0);
    }

    {
        UCString fromLiteral("Hello, World!");
        TEST("Construct from C-string literal", fromLiteral == "Hello, World!");
        TEST("ASCII length matches byte length", fromLiteral.Length() == 13);
    }

    {
        std::string stdStr = "Test String";
        UCString fromStd(stdStr);
        TEST("Construct from std::string", fromStd == stdStr);
    }

    {
        UCString fromCodepoint(U'€');
        TEST("Construct from single codepoint", fromCodepoint == "€");
        TEST("Euro sign is 1 grapheme", fromCodepoint.Length() == 1);
        TEST("Euro sign is 3 bytes", fromCodepoint.ByteLength() == 3);
    }

    std::cout << std::endl;

    // ===== UTF-8 LENGTH TESTS =====
    std::cout << "--- UTF-8 Length Tests ---" << std::endl;

    {
        UCString ascii("Hello");
        TEST("ASCII: 5 chars = 5 bytes = 5 graphemes", 
             ascii.Length() == 5 && ascii.ByteLength() == 5);
    }

    {
        UCString german("Größe");  // German word with ö and ß
        TEST("German 'Größe': 5 graphemes", german.Length() == 5);
        TEST("German 'Größe': 7 bytes (ö and ß are 2 bytes each)", german.ByteLength() == 7);
    }

    {
        UCString japanese("日本語");  // Japanese: 3 characters
        TEST("Japanese '日本語': 3 graphemes", japanese.Length() == 3);
        TEST("Japanese '日本語': 9 bytes (3 bytes each)", japanese.ByteLength() == 9);
    }

    {
        UCString emoji("Hello 👋!");  // Hello with waving hand emoji
        TEST("'Hello 👋!': 8 graphemes", emoji.Length() == 8);
        // 5 (Hello) + 1 (space) + 4 (emoji) + 1 (!) = 11 bytes
        TEST("'Hello 👋!': 11 bytes", emoji.ByteLength() == 11);
    }

    std::cout << std::endl;

    // ===== GRAPHEME CLUSTER TESTS =====
    std::cout << "--- Grapheme Cluster Tests ---" << std::endl;

    {
        // Family emoji: 👨�👩�👦 (man + ZWJ + woman + ZWJ + boy)
        UCString family("👨�👩�👦");
        TEST("Family emoji is 1 grapheme cluster", family.Length() == 1);
        std::cout << "   (Family emoji bytes: " << family.ByteLength() << ")" << std::endl;
    }

    {
        // Flag emoji: 🇺🇸 (regional indicators U + S)
        UCString flag("🇺🇸");
        TEST("US flag is 1 grapheme cluster", flag.Length() == 1);
        TEST("US flag is 8 bytes", flag.ByteLength() == 8);
    }

    {
        // Accented character: é composed as e + combining acute
        UCString accented("e\xCC\x81");  // e + combining acute accent
        TEST("'e' + combining accent is 1 grapheme", accented.Length() == 1);
        TEST("'e' + combining accent is 3 bytes", accented.ByteLength() == 3);
    }

    {
        // Devanagari: नी (na + vowel sign ii)
        UCString hindi("नी");
        TEST("Hindi 'नी' is 1 grapheme", hindi.Length() == 1);
    }

    std::cout << std::endl;

    // ===== COMPARISON OPERATORS =====
    std::cout << "--- Comparison Operator Tests ---" << std::endl;

    {
        UCString a("Hello");
        UCString b("Hello");
        UCString c("World");
        std::string stdHello = "Hello";
        const char* cHello = "Hello";

        TEST("UCString == UCString (equal)", a == b);
        TEST("UCString != UCString (not equal)", a != c);
        TEST("UCString == std::string", a == stdHello);
        TEST("std::string == UCString", stdHello == a);
        TEST("UCString == const char*", a == cHello);
        TEST("const char* == UCString", cHello == a);
        TEST("UCString < UCString", a < c);
        TEST("UCString > UCString", c > a);
    }

    std::cout << std::endl;

    // ===== CONCATENATION TESTS =====
    std::cout << "--- Concatenation Tests ---" << std::endl;

    {
        UCString hello("Hello");
        UCString world(" World");
        UCString result = hello + world;
        TEST("UCString + UCString", result == "Hello World");
    }

    {
        UCString str("Hello");
        str += " World";
        TEST("UCString += const char*", str == "Hello World");
    }

    {
        UCString str("Test");
        str += std::string(" String");
        TEST("UCString += std::string", str == "Test String");
    }

    {
        UCString str("A");
        str += U'→';  // Right arrow
        str += "B";
        TEST("UCString += char32_t", str == "A→B");
    }

    std::cout << std::endl;

    // ===== ELEMENT ACCESS TESTS =====
    std::cout << "--- Element Access Tests ---" << std::endl;

    {
        UCString mixed("Héllo🌍!");  // H, é, l, l, o, 🌍, !
        TEST("operator[] returns correct grapheme [0]", mixed[0].ToString() == "H");
        TEST("operator[] on accented char [1]", mixed[1].ToString() == "é");
        TEST("operator[] on emoji [5]", mixed[5].ToString() == "🌍");
        TEST("operator[] on last char [6]", mixed[6].ToString() == "!");
    }

    {
        UCString str("Test");
        str[0] = "X";
        TEST("GraphemeRef assignment", str == "Xest");
    }

    {
        UCString str("ABC");
        TEST("Front() returns first grapheme", str.Front() == "A");
        TEST("Back() returns last grapheme", str.Back() == "C");
    }

    std::cout << std::endl;

    // ===== MODIFICATION TESTS =====
    std::cout << "--- Modification Tests ---" << std::endl;

    {
        UCString str("Hello World");
        str.Insert(5, "😀");
        TEST("Insert emoji at grapheme position", str == "Hello😀 World");
    }

    {
        UCString str("Hello🌍World");
        str.Erase(5, 1);  // Erase the globe emoji
        TEST("Erase single grapheme (emoji)", str == "HelloWorld");
    }

    {
        UCString str("Hello World");
        str.Replace(6, 5, "Universe");
        TEST("Replace graphemes", str == "Hello Universe");
    }

    {
        UCString str("Test");
        str.PopBack();
        TEST("PopBack removes last grapheme", str == "Tes");
    }

    {
        UCString str("Emoji🌍End");
        str.PopBack();
        str.PopBack();
        str.PopBack();
        str.PopBack();  // Remove "End" and emoji
        TEST("PopBack on mixed content", str == "Emoji");
    }

    std::cout << std::endl;

    // ===== SUBSTRING TESTS =====
    std::cout << "--- Substring Tests ---" << std::endl;

    {
        UCString str("Hello🌍World");
        UCString sub = str.Substr(0, 5);
        TEST("Substr first 5 graphemes", sub == "Hello");
    }

    {
        UCString str("Hello🌍World");
        UCString sub = str.Substr(5, 1);
        TEST("Substr emoji only", sub == "🌍");
    }

    {
        UCString str("日本語テスト");  // Japanese text
        UCString sub = str.Substr(0, 3);
        TEST("Substr Japanese characters", sub == "日本語");
    }

    std::cout << std::endl;

    // ===== SEARCH TESTS =====
    std::cout << "--- Search Tests ---" << std::endl;

    {
        UCString str("Hello World Hello");
        TEST("Find existing substring", str.Find("World") == 6);
        TEST("Find returns npos for missing", str.Find("xyz") == UCString::npos);
    }

    {
        UCString str("🌍Hello🌍World🌍");
        TEST("Find emoji at start", str.Find("🌍") == 0);
        TEST("Find from position", str.Find("🌍", 1) == 6);
    }

    {
        UCString str("Test");
        TEST("Contains (true)", str.Contains("es"));
        TEST("Contains (false)", !str.Contains("xyz"));
    }

    {
        UCString str("Hello World");
        TEST("StartsWith (true)", str.StartsWith("Hello"));
        TEST("StartsWith (false)", !str.StartsWith("World"));
        TEST("EndsWith (true)", str.EndsWith("World"));
        TEST("EndsWith (false)", !str.EndsWith("Hello"));
    }

    std::cout << std::endl;

    // ===== CURSOR NAVIGATION TESTS =====
    std::cout << "--- Cursor Navigation Tests ---" << std::endl;

    {
        UCString str("A👨�👩�👦B");  // A + family emoji + B
        
        size_t pos = 0;
        pos = str.NextGraphemePosition(pos);  // After 'A'
        TEST("Next grapheme after 'A'", pos == 1);
        
        pos = str.NextGraphemePosition(pos);  // After family emoji
        size_t afterFamily = pos;
        
        pos = str.NextGraphemePosition(pos);  // After 'B'
        TEST("Navigation reaches end", pos == str.ByteLength());
        
        // Navigate backwards
        pos = str.PrevGraphemePosition(pos);  // Before 'B'
        TEST("Prev grapheme before 'B'", pos == afterFamily);
    }

    {
        UCString str("Test🌍");
        size_t pos = str.ByteLength();
        pos = str.PrevGraphemePosition(pos);  // Before emoji
        TEST("PrevGrapheme before emoji", pos == 4);
        
        pos = str.PrevGraphemePosition(pos);  // Before 't'
        TEST("PrevGrapheme before 't'", pos == 3);
    }

    std::cout << std::endl;

    // ===== POSITION CONVERSION TESTS =====
    std::cout << "--- Position Conversion Tests ---" << std::endl;

    {
        UCString str("A🌍B");  // A(1) + emoji(4) + B(1) = 6 bytes, 3 graphemes
        
        TEST("GraphemeToByteOffset(0)", str.GraphemeToByteOffset(0) == 0);
        TEST("GraphemeToByteOffset(1)", str.GraphemeToByteOffset(1) == 1);
        TEST("GraphemeToByteOffset(2)", str.GraphemeToByteOffset(2) == 5);
        
        TEST("ByteToGraphemeIndex(0)", str.ByteToGraphemeIndex(0) == 0);
        TEST("ByteToGraphemeIndex(1)", str.ByteToGraphemeIndex(1) == 1);
        TEST("ByteToGraphemeIndex(5)", str.ByteToGraphemeIndex(5) == 2);
    }

    std::cout << std::endl;

    // ===== ITERATOR TESTS =====
    std::cout << "--- Iterator Tests ---" << std::endl;

    {
        UCString str("A🌍B");
        std::vector<std::string> graphemes;
        for (auto it = str.begin(); it != str.end(); ++it) {
            graphemes.push_back((*it).ToString());
        }
        TEST("Iterator count", graphemes.size() == 3);
        TEST("Iterator grapheme 0", graphemes[0] == "A");
        TEST("Iterator grapheme 1", graphemes[1] == "🌍");
        TEST("Iterator grapheme 2", graphemes[2] == "B");
    }

    {
        UCString str("Hello");
        std::string collected;
        for (const auto& g : str) {
            collected += g.ToString();
        }
        TEST("Range-based for loop", collected == "Hello");
    }

    std::cout << std::endl;

    // ===== UTILITY TESTS =====
    std::cout << "--- Utility Tests ---" << std::endl;

    {
        UCString str("  Hello World  ");
        TEST("Trim", str.Trimmed() == "Hello World");
        TEST("TrimLeft", str.TrimmedLeft() == "Hello World  ");
        TEST("TrimRight", str.TrimmedRight() == "  Hello World");
    }

    {
        UCString str("Hello World");
        auto parts = str.Split(" ");
        TEST("Split count", parts.size() == 2);
        TEST("Split part 0", parts[0] == "Hello");
        TEST("Split part 1", parts[1] == "World");
    }

    {
        std::vector<UCString> parts = {"A", "B", "C"};
        UCString joined = UCString::Join(parts, "-");
        TEST("Join with separator", joined == "A-B-C");
    }

    {
        UCString str("Hello");
        TEST("ToLower", str.ToLower() == "hello");
        TEST("ToUpper", str.ToUpper() == "HELLO");
    }

    {
        UCString str("A🌍B");
        UCString reversed = str.Reversed();
        TEST("Reversed preserves graphemes", reversed == "B🌍A");
    }

    std::cout << std::endl;

    // ===== CONVERSION TESTS =====
    std::cout << "--- Conversion Tests ---" << std::endl;

    {
        UCString str("Hello🌍");
        std::string stdStr = str.ToString();
        TEST("ToString()", stdStr == "Hello🌍");
        
        std::string implicit = str;  // Implicit conversion
        TEST("Implicit conversion to std::string", implicit == "Hello🌍");
    }

    {
        UCString str("Test");
        const char* cstr = str.CStr();
        TEST("CStr() returns valid C-string", std::string(cstr) == "Test");
    }

    {
        UCString str("Hello");
        std::u32string u32 = str.ToUTF32();
        TEST("ToUTF32 length", u32.length() == 5);
        TEST("ToUTF32 content", u32 == U"Hello");
    }

    {
        std::u32string u32 = U"日本語";
        UCString fromU32 = UCString::FromUTF32(u32);
        TEST("FromUTF32", fromU32 == "日本語");
    }

    std::cout << std::endl;

    // ===== VALIDATION TESTS =====
    std::cout << "--- Validation Tests ---" << std::endl;

    {
        UCString valid("Hello 世界 🌍");
        TEST("Valid UTF-8 string", valid.IsValidUTF8());
    }

    {
        // Create string with invalid UTF-8 sequence
        std::string invalid = "Hello\xFF\xFEWorld";
        UCString sanitized = UCString::Sanitized(invalid);
        TEST("Sanitized replaces invalid bytes", sanitized.IsValidUTF8());
    }

    std::cout << std::endl;

    // ===== STRING LITERAL TESTS =====
    std::cout << "--- String Literal Tests ---" << std::endl;

    {
        UCString str = "Hello"_uc;
        TEST("User-defined literal _uc", str == "Hello");
    }

    std::cout << std::endl;

    // ===== STREAM OPERATOR TESTS =====
    std::cout << "--- Stream Operator Tests ---" << std::endl;

    {
        UCString str("Hello 🌍");
        std::ostringstream oss;
        oss << str;
        TEST("Stream output operator", oss.str() == "Hello 🌍");
    }

    std::cout << std::endl;

    // ===== REAL-WORLD TEXT EDITOR SCENARIO =====
    std::cout << "--- Text Editor Simulation ---" << std::endl;

    {
        UCString text("Hello 👨�👩�👦 World!");
        
        // Simulate cursor at position 0
        size_t cursorByte = 0;
        size_t cursorGrapheme = 0;
        
        // Move cursor right 6 times (H-e-l-l-o-space)
        for (int i = 0; i < 6; i++) {
            cursorByte = text.NextGraphemePosition(cursorByte);
            cursorGrapheme++;
        }
        
        // Now cursor should be right before the family emoji
        TEST("Cursor before family emoji (byte)", cursorByte == 6);
        TEST("Cursor before family emoji (grapheme)", cursorGrapheme == 6);
        
        // Move right once more - should skip entire family emoji
        cursorByte = text.NextGraphemePosition(cursorByte);
        cursorGrapheme++;
        
        // Should now be at the space after emoji
        TEST("Cursor after family emoji", text.ByteToGraphemeIndex(cursorByte) == 7);
        
        // Insert text at cursor position
        UCString newText = text.Substr(0, cursorGrapheme) + "!" + text.Substr(cursorGrapheme);
        TEST("Insert at cursor", newText.Contains("👨�👩�👦!"));
        
        // Delete the family emoji (simulate backspace)
        UCString afterDelete = text;
        afterDelete.Erase(6, 1);  // Erase the family emoji at grapheme position 6
        TEST("Delete family emoji", afterDelete == "Hello  World!");
    }

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "   Test Results: " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    if (failCount == 0) {
        std::cout << "   All tests PASSED! ✓" << std::endl;
    } else {
        std::cout << "   " << failCount << " tests FAILED! ✗" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return failCount > 0 ? 1 : 0;
}