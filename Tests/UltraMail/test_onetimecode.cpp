// Tests/UltraMail/test_onetimecode.cpp
// FindOneTimeCodes: the sign-in and confirmation codes the reading pane puts a
// copy button on - in any language - and the numbers it must leave alone.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailOneTimeCode.h"

#include <string>
#include <vector>

using namespace UltraMail;

namespace {

// The one code found in a single block of text, or "" (none, or several).
std::string OnlyCode(const std::string& text, const std::string& subject = "") {
    const std::vector<OneTimeCode> codes = FindOneTimeCodes(subject, {text});
    return codes.size() == 1 ? codes.front().code : std::string();
}

} // namespace

// Papierkram's sign-in mail (2026-10-08), as the HTML page's paragraphs: the
// code is a paragraph of its own, below "...mit dem folgenden Code:".
TEST(one_time_code_papierkram_html_paragraphs) {
    const std::vector<std::string> blocks = {
        "Liebe(r) Stefan Fr\xC3\xB6hling,",
        "du hast dich gerade versucht von folgendem Ger\xC3\xA4t einzuloggen:",
        "Ger\xC3\xA4t",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:157.0) Gecko/20100101 Firefox/157.0",
        "IP",
        "171.5.163.48",
        "Bitte best\xC3\xA4tige die Anmeldung mit dem folgenden Code:",
        "649082",
        "Der Code ist 25 Minuten g\xC3\xBCltig.",
        "Solltest du keine Anmeldung durchgef\xC3\xBChrt haben, \xC3\xA4ndere bitte dein Passwort "
        "so schnell es geht: Passwort zur\xC3\xBC" "cksetzen.  Diese Ma\xC3\x9Fnahme dient zur "
        "Sicherheit deines Papierkram-Accounts.",
        "Viele Gr\xC3\xBC\xC3\x9F" "e\nDein Papierkram-Team",
        "\xC2\xA9 Papierkram.de ist ein Produkt der odacer finanzsoftware GmbH\n"
        "odacer finanzsoftware GmbH, Konrad-Adenauer-Ring 13, 65187 Wiesbaden",
        "Impressum und Datenschutz",
    };
    const std::vector<OneTimeCode> codes = FindOneTimeCodes("Dein Anmelde-Code", blocks);
    REQUIRE_EQ(codes.size(), static_cast<size_t>(1));
    REQUIRE_EQ(codes[0].code, std::string("649082"));
    REQUIRE_EQ(codes[0].block, 7);
    REQUIRE(codes[0].standalone);
}

// The same mail's plain-text part, a line at a time: the code between
// Markdown fences.
TEST(one_time_code_papierkram_plain_text_lines) {
    const std::vector<std::string> lines = {
        "Liebe(r) Stefan Fr\xC3\xB6hling,", "",
        "du hast dich gerade versucht von folgendem Ger\xC3\xA4t einzuloggen:", "",
        "```",
        "Ger\xC3\xA4t: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:157.0) Gecko/20100101 Firefox/157.0",
        "IP: 171.5.163.48",
        "```", "",
        "Bitte best\xC3\xA4tige die Anmeldung mit dem folgenden Code:", "",
        "```",
        "649082",
        "```", "",
        "Der Code ist 25 Minuten g\xC3\xBCltig.", "",
        "odacer finanzsoftware GmbH",
        "Konrad-Adenauer-Ring 13",
        "65187 Wiesbaden",
    };
    const std::vector<OneTimeCode> codes = FindOneTimeCodes("Dein Anmelde-Code", lines);
    REQUIRE_EQ(codes.size(), static_cast<size_t>(1));
    REQUIRE_EQ(codes[0].code, std::string("649082"));
    REQUIRE_EQ(codes[0].block, 12);
    REQUIRE(codes[0].standalone);
}

// Near the word, in many languages and scripts.
TEST(one_time_code_found_next_to_the_word_in_any_language) {
    REQUIRE_EQ(OnlyCode("Your verification code is 482913."), std::string("482913"));
    REQUIRE_EQ(OnlyCode("G-123456 is your Google verification code."), std::string("G-123456"));
    REQUIRE_EQ(OnlyCode("Ihre TAN lautet 553201"), std::string("553201"));
    REQUIRE_EQ(OnlyCode("Tu c\xC3\xB3" "digo de verificaci\xC3\xB3n es 120394"), std::string("120394"));
    REQUIRE_EQ(OnlyCode("Votre code de v\xC3\xA9rification\xC2\xA0: 220031"), std::string("220031"));
    REQUIRE_EQ(OnlyCode("Il tuo codice \xC3\xA8 760012"), std::string("760012"));
    REQUIRE_EQ(OnlyCode("Je verificatiecode is 908172"), std::string("908172"));
    REQUIRE_EQ(OnlyCode("Din engångskod är 302010"), std::string("302010"));
    REQUIRE_EQ(OnlyCode("Vahvistuskoodisi on 908170"), std::string("908170"));
    REQUIRE_EQ(OnlyCode("Tw\xC3\xB3j kod weryfikacyjny to 112233"), std::string("112233"));
    REQUIRE_EQ(OnlyCode("V\xC3\xA1\xC5\xA1 ov\xC4\x9B\xC5\x99ovac\xC3\xAD k\xC3\xB3" "d je 445566"),
               std::string("445566"));
    REQUIRE_EQ(OnlyCode("Do\xC4\x9Frulama kodunuz: 448812"), std::string("448812"));
    REQUIRE_EQ(OnlyCode("Codul de verificare este 667788"), std::string("667788"));
    REQUIRE_EQ(OnlyCode("M\xC3\xA3 x\xC3\xA1" "c minh c\xE1\xBB\xA7" "a b\xE1\xBA\xA1n l\xC3\xA0 135790"),
               std::string("135790"));
    REQUIRE_EQ(OnlyCode("Kode verifikasi Anda adalah 246810"), std::string("246810"));
    // Cyrillic, Greek
    REQUIRE_EQ(OnlyCode("\xD0\x92\xD0\xB0\xD1\x88 \xD0\x9A\xD0\x9E\xD0\x94: 739201"), std::string("739201"));   // Ваш КОД
    REQUIRE_EQ(OnlyCode("\xCE\x9F \xCE\xBA\xCF\x89\xCE\xB4\xCE\xB9\xCE\xBA\xCF\x8C\xCF\x82 "
                        "\xCF\x83\xCE\xB1\xCF\x82 \xCE\xB5\xCE\xAF\xCE\xBD\xCE\xB1\xCE\xB9 778899"),
               std::string("778899"));                                                                    // Ο κωδικός σας είναι
    // Chinese, Japanese, Korean
    REQUIRE_EQ(OnlyCode("\xE6\x82\xA8\xE7\x9A\x84\xE9\xAA\x8C\xE8\xAF\x81\xE7\xA0\x81\xE6\x98\xAF"
                        "\xEF\xBC\x9A" "649082\xEF\xBC\x8C" "5\xE5\x88\x86\xE9\x92\x9F\xE5\x86\x85"
                        "\xE6\x9C\x89\xE6\x95\x88\xE3\x80\x82"),
               std::string("649082"));                                   // 您的验证码是：649082，5分钟内有效。
    REQUIRE_EQ(OnlyCode("\xE8\xAA\x8D\xE8\xA8\xBC\xE3\x82\xB3\xE3\x83\xBC\xE3\x83\x89: 123456"),
               std::string("123456"));                                   // 認証コード: 123456
    REQUIRE_EQ(OnlyCode("\xEC\x9D\xB8\xEC\xA6\x9D\xEB\xB2\x88\xED\x98\xB8 [654321]\xEC\x9D\x84 "
                        "\xEC\x9E\x85\xEB\xA0\xA5\xED\x95\xB4\xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"),
               std::string("654321"));                                   // 인증번호 [654321]을 입력해주세요
    // Thai, Arabic, Hebrew, Hindi
    REQUIRE_EQ(OnlyCode("\xE0\xB8\xA3\xE0\xB8\xAB\xE0\xB8\xB1\xE0\xB8\xAA OTP 552817"),
               std::string("552817"));                                   // รหัส OTP
    REQUIRE_EQ(OnlyCode("\xD8\xB1\xD9\x85\xD8\xB2 \xD8\xA7\xD9\x84\xD8\xAA\xD8\xAD\xD9\x82\xD9\x82 "
                        "\xD8\xA7\xD9\x84\xD8\xAE\xD8\xA7\xD8\xB5 \xD8\xA8\xD9\x83 \xD9\x87\xD9\x88 918273"),
               std::string("918273"));                                   // رمز التحقق الخاص بك هو
    REQUIRE_EQ(OnlyCode("\xD7\x94\xD7\xA7\xD7\x95\xD7\x93 \xD7\xA9\xD7\x9C\xD7\x9A "
                        "\xD7\x94\xD7\x95\xD7\x90 564738"),
               std::string("564738"));                                   // הקוד שלך הוא
    REQUIRE_EQ(OnlyCode("\xE0\xA4\x86\xE0\xA4\xAA\xE0\xA4\x95\xE0\xA4\xBE OTP 341256 "
                        "\xE0\xA4\xB9\xE0\xA5\x88"),
               std::string("341256"));                                   // आपका OTP 341256 है
}

TEST(one_time_code_shapes) {
    // Groups of digits: typed in without the space.
    const std::vector<OneTimeCode> spaced = FindOneTimeCodes("", {"Your code: 649 082"});
    REQUIRE_EQ(spaced.size(), static_cast<size_t>(1));
    REQUIRE_EQ(spaced[0].code, std::string("649082"));
    REQUIRE_EQ(spaced[0].shown, std::string("649 082"));
    // Capitals and digits.
    REQUIRE_EQ(OnlyCode("Your code is X7K-9PQ2"), std::string("X7K-9PQ2"));
    // In the subject only.
    const std::vector<OneTimeCode> subject = FindOneTimeCodes("482913 is your Instagram code", {});
    REQUIRE_EQ(subject.size(), static_cast<size_t>(1));
    REQUIRE_EQ(subject[0].code, std::string("482913"));
    REQUIRE_EQ(subject[0].block, -1);
    // A paragraph of its own, in brackets, under a subject that says "code".
    const std::vector<OneTimeCode> alone =
        FindOneTimeCodes("Ihr Sicherheitscode", {"Hallo,", "[ 120934 ]", "Danke"});
    REQUIRE_EQ(alone.size(), static_cast<size_t>(1));
    REQUIRE_EQ(alone[0].code, std::string("120934"));
    REQUIRE(alone[0].standalone);
}

// Numbers that look like codes but are not.
TEST(one_time_code_leaves_other_numbers_alone) {
    // No word for "code" anywhere: a number on its own is just a number.
    REQUIRE(FindOneTimeCodes("Weekly digest", {"Top story", "123456", "More"}).empty());
    // A year on its own line.
    REQUIRE(FindOneTimeCodes("Your code", {"2026"}).empty());
    // Postal codes and other codes that are not typed in.
    REQUIRE(OnlyCode("Postal code 65187 Wiesbaden").empty());
    REQUIRE(OnlyCode("Postcode: 65187").empty());
    REQUIRE(OnlyCode("Error code 404512 occurred").empty());
    // Order numbers, prices, times, IP addresses, versions, links.
    REQUIRE(OnlyCode("Your code for order #123456").empty());
    REQUIRE(OnlyCode("Code valid until 10:30").empty());
    REQUIRE(OnlyCode("IP 171.5.163.48 asked for a code").empty());
    REQUIRE(OnlyCode("Code \xE2\x82\xAC" "1999").empty());
    REQUIRE(OnlyCode("code https://example.com/r/123456").empty());
    REQUIRE(OnlyCode("code at user123456@example.com").empty());
    // A phone number beside the word.
    REQUIRE(OnlyCode("Did not ask for a code? Call +49 611 1234567").empty());
    // Lower-case words with digits are names, not codes.
    REQUIRE(OnlyCode("code for Win64 x64").empty());
    // Another number between the word and this one.
    REQUIRE(OnlyCode("The code expires in 10 minutes, ticket 445566").empty());
}

TEST(one_time_code_mentions) {
    REQUIRE(MentionsOneTimeCode("Dein Anmelde-Code"));
    REQUIRE(MentionsOneTimeCode("Your one-time passcode"));
    REQUIRE(MentionsOneTimeCode("\xE9\xAA\x8C\xE8\xAF\x81\xE7\xA0\x81"));   // 验证码
    REQUIRE(MentionsOneTimeCode("\xD0\x9A\xD0\xBE\xD0\xB4"));               // Код
    REQUIRE(!MentionsOneTimeCode("Weekly digest"));
    REQUIRE(!MentionsOneTimeCode("Postcode"));
    REQUIRE(!MentionsOneTimeCode("Your postal code"));
    REQUIRE(!MentionsOneTimeCode("Spinning wheel"));   // "pin" inside a word
}
