// Tests/UltraStoreAdmin/test_translation.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "Translation.h"

#include <algorithm>

using namespace UltraStoreAdmin;
using namespace admin_test;

namespace {

// "<lang>:<text>", so the test sees what went where.
class FakeTranslator final : public ITextTranslator {
public:
    int calls = 0;
    std::string failFor;
    Result<std::vector<std::string>> Translate(const std::vector<std::string>& texts, const std::string& source,
                                               const std::string& target) override {
        ++calls;
        if (target == failFor) return Result<std::vector<std::string>>::Failure(UltraStore::ErrorCode::NetworkError, "the model is away");
        std::vector<std::string> out;
        for (const std::string& t : texts) out.push_back(target + "(" + source + "):" + t);
        return Result<std::vector<std::string>>::Succeed(std::move(out));
    }
};

bool Has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

} // namespace

TEST(official_languages_are_listed) {
    REQUIRE(OfficialLanguages().size() >= 24);
    REQUIRE_EQ(LanguageName("de"), std::string("German"));
    REQUIRE_EQ(LanguageName("xx"), std::string("xx"));
    REQUIRE(Has(OfficialLanguageCodes(), "mt"));
    REQUIRE(Has(OfficialLanguageCodes(), "th"));
}

TEST(a_component_is_filled_in_the_languages_it_lacks) {
    UltraStore::Component c = HelloApp();   // en source; de summary (machine), no de description or notes
    FakeTranslator fake;
    const std::vector<std::string> targets = {"en", "de", "fr"};
    const auto fill = LanguagesToFill(c, targets);
    REQUIRE_EQ(fill.size(), size_t(2));   // de (description, notes missing) and fr; never the source
    const auto report = TranslateComponent(c, fake, targets);
    REQUIRE(report.Ok());
    REQUIRE_EQ(report.Value().languages.size(), size_t(2));
    REQUIRE_EQ(report.Value().texts, size_t(2 + 3));   // de: description + notes; fr: summary, description, notes
    REQUIRE_EQ(fake.calls, 2);
    REQUIRE_EQ(c.summary.Text("de"), std::string("Sagt Hallo"));        // what was there stays
    REQUIRE_EQ(c.description.Text("de"), std::string("de(en):A tiny program that prints a greeting."));
    REQUIRE_EQ(c.summary.Text("fr"), std::string("fr(en):Says hello"));
    REQUIRE_EQ(c.releases[0].notes.Text("fr"), std::string("fr(en):First."));
    REQUIRE(Has(c.machineTranslated, "de") && Has(c.machineTranslated, "fr"));
    REQUIRE_EQ(c.machineTranslated.size(), size_t(2));
    // Nothing left to do.
    REQUIRE(LanguagesToFill(c, targets).empty());
    const auto again = TranslateComponent(c, fake, targets);
    REQUIRE(again.Ok());
    REQUIRE_EQ(again.Value().texts, size_t(0));
    REQUIRE_EQ(again.Value().skipped.size(), size_t(2));
    // Redo translates the machine ones again, but not a human one.
    c.summary.byLanguage["fr"] = "Dit bonjour";
    c.machineTranslated.erase(std::find(c.machineTranslated.begin(), c.machineTranslated.end(), "fr"));
    const auto redo = TranslateComponent(c, fake, targets, true);
    REQUIRE(redo.Ok());
    REQUIRE_EQ(c.summary.Text("fr"), std::string("Dit bonjour"));
    REQUIRE_EQ(c.summary.Text("de"), std::string("de(en):Says hello"));
}

TEST(a_failing_language_keeps_what_the_component_had) {
    UltraStore::Component c = HelloApp();
    FakeTranslator fake;
    fake.failFor = "fr";
    const auto report = TranslateComponent(c, fake, {"de", "fr", "it"});
    REQUIRE(report.Ok());
    REQUIRE_EQ(report.Value().errors.size(), size_t(1));
    REQUIRE(report.Value().errors[0].rfind("fr:", 0) == 0);
    REQUIRE(!Has(c.machineTranslated, "fr"));
    REQUIRE(Has(c.machineTranslated, "it"));
    REQUIRE(c.summary.byLanguage.count("fr") == 0);
    // No source text at all: nothing to translate from.
    UltraStore::Component bare;
    bare.id = "org.ultraos.bare";
    REQUIRE(!TranslateComponent(bare, fake, {"de"}).Ok());
    // Without the UltraAI module the factory says so rather than crashing.
    std::string why;
    auto t = CreateUltraAITranslator("mock", "", "", &why);
    REQUIRE(t != nullptr || !why.empty());
}
