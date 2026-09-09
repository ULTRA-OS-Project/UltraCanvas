// Tests/UltraCleaner/test_rules.cpp
// Structural checks on the rule table. A rule with a typo in it is the one
// way UltraCleaner could look somewhere it should not, so the table is
// validated as data rather than trusted as source.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraCleanerPaths.h"
#include "UltraCleanerRules.h"

#include <set>

using namespace UltraCleaner;

TEST(RuleIdsAreUniqueAndNonEmpty) {
    std::set<std::string> ids;
    for (const auto& rule : BuiltinRules()) {
        REQUIRE(!rule.id.empty());
        REQUIRE(!rule.title.empty());
        REQUIRE(ids.insert(rule.id).second);
    }
}

TEST(EveryRuleTargetsAtLeastOnePlatform) {
    for (const auto& rule : BuiltinRules()) {
        REQUIRE((rule.platforms & PlatformMask::All) != 0u);
    }
}

TEST(EveryPlatformHasRules) {
    REQUIRE(RulesForPlatform(CleanerPlatform::Linux).size() > 10);
    REQUIRE(RulesForPlatform(CleanerPlatform::MacOS).size() > 10);
    REQUIRE(RulesForPlatform(CleanerPlatform::Windows).size() > 10);
    REQUIRE(RulesForPlatform(CleanerPlatform::Unknown).empty());
}

TEST(EveryRuleHasRootsExceptTheRecycleBin) {
    for (const auto& rule : BuiltinRules()) {
        if (rule.mode == RuleMode::WindowsRecycleBin) {
            REQUIRE(rule.roots.empty());
            continue;
        }
        REQUIRE(!rule.roots.empty());
    }
}

TEST(MatchingModesCarryNamePatterns) {
    for (const auto& rule : BuiltinRules()) {
        if (rule.mode == RuleMode::RemoveMatchingFiles ||
            rule.mode == RuleMode::RemoveMatchingDirectories) {
            REQUIRE(!rule.namePatterns.empty());
        }
    }
}

TEST(RuleRootsUseOnlyKnownTokens) {
    // ExpandTokens returns "" for an unknown token, which would silently
    // disable the rule; every root must either resolve here or resolve on
    // one of the other platforms, never fail because of a typo.
    static const std::set<std::string> known = {
        "HOME", "TEMP", "SYSTEMTEMP", "CACHE", "DATA", "STATE",
        "LOCALAPPDATA", "APPDATA", "DOWNLOADS", "TRASH", "WINDIR"
    };
    for (const auto& rule : BuiltinRules()) {
        for (const auto& root : rule.roots) {
            size_t index = 0;
            while ((index = root.find('{', index)) != std::string::npos) {
                const size_t close = root.find('}', index);
                REQUIRE(close != std::string::npos);
                const std::string token = root.substr(index + 1, close - index - 1);
                REQUIRE(known.count(token) == 1);
                index = close + 1;
            }
        }
    }
}

TEST(RuleRootsAreAbsoluteOnceExpanded) {
    for (const auto& rule : RulesForCurrentPlatform()) {
        for (const auto& root : rule.roots) {
            const std::string expanded = ExpandTokens(root);
            if (expanded.empty()) continue;      // token not meaningful here
            const bool posixAbsolute = expanded[0] == '/';
            const bool windowsAbsolute =
                expanded.size() >= 3 && expanded[1] == ':' && expanded[2] == '/';
            REQUIRE(posixAbsolute || windowsAbsolute);
        }
    }
}

TEST(DestructiveCategoriesAreNotPreTicked) {
    // Emptying the trash is permanent and re-downloading a Maven repository
    // is expensive: neither may start ticked.
    for (const auto& rule : BuiltinRules()) {
        if (rule.category == CleanCategory::TrashBin) {
            REQUIRE(rule.safeByDefault == false);
        }
    }
    const CleanRule* maven = FindRule("package.maven");
    REQUIRE(maven != nullptr);
    REQUIRE(maven->safeByDefault == false);

    const CleanRule* installers = FindRule("installer.downloads");
    REQUIRE(installers != nullptr);
    REQUIRE(installers->safeByDefault == false);
    // …and it must never sweep a Downloads folder that was filled yesterday.
    REQUIRE(installers->minAgeDays >= 30);
}

TEST(SharedTempRulesExcludeSessionSockets) {
    const CleanRule* rule = FindRule("temp.user.posix");
    REQUIRE(rule != nullptr);
    bool guardsX11 = false;
    for (const auto& exclude : rule->excludeNames) {
        if (exclude == ".X11-unix") guardsX11 = true;
    }
    REQUIRE(guardsX11);
    REQUIRE(rule->minAgeDays >= 1);
}

TEST(UnknownRuleLookupReturnsNull) {
    REQUIRE(FindRule("no.such.rule") == nullptr);
}
