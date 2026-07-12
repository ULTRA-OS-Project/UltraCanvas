// Tests/UltraNet/test_headers.cpp
// UltraNetHttpHeaders case-insensitive ops. No network.
#include "test_framework.h"

#include <UltraNet/UltraNetHttp.h>

#include <string>

TEST(headers_empty_by_default) {
    UltraNetHttpHeaders h;
    CHECK(h.Empty());
    REQUIRE_EQ(h.Get("X-Anything"), std::string{""});
    CHECK(!h.Has("X-Anything"));
}

TEST(headers_set_get_case_insensitive) {
    UltraNetHttpHeaders h;
    h.Set("Content-Type", "application/json");
    REQUIRE_EQ(h.Get("content-type"), std::string{"application/json"});
    REQUIRE_EQ(h.Get("CONTENT-TYPE"), std::string{"application/json"});
    REQUIRE_EQ(h.Get("Content-Type"), std::string{"application/json"});
    CHECK(h.Has("CoNtEnT-tYpE"));
}

TEST(headers_set_replaces_existing) {
    UltraNetHttpHeaders h;
    h.Add("Accept", "first");
    h.Set("Accept", "second");
    auto all = h.GetAll("Accept");
    REQUIRE_EQ(all.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(h.Get("Accept"), std::string{"second"});
}

TEST(headers_add_preserves_duplicates) {
    UltraNetHttpHeaders h;
    h.Add("Cookie", "a=1");
    h.Add("Cookie", "b=2");
    auto all = h.GetAll("Cookie");
    REQUIRE_EQ(all.size(), static_cast<std::size_t>(2));
    CHECK(all[0] == "a=1" || all[0] == "b=2");
}

TEST(headers_remove_is_case_insensitive) {
    UltraNetHttpHeaders h;
    h.Set("X-Foo", "1");
    h.Remove("x-foo");
    CHECK(!h.Has("X-Foo"));
}

TEST(headers_clear_drops_everything) {
    UltraNetHttpHeaders h;
    h.Set("A", "1"); h.Set("B", "2"); h.Add("A", "3");
    h.Clear();
    CHECK(h.Empty());
    REQUIRE_EQ(h.GetAll("A").size(), static_cast<std::size_t>(0));
}

TEST(headers_entries_preserves_insertion_order) {
    UltraNetHttpHeaders h;
    h.Add("first",  "1");
    h.Add("second", "2");
    h.Add("third",  "3");
    const auto& entries = h.Entries();
    REQUIRE_EQ(entries.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(entries[0].first,  std::string{"first"});
    REQUIRE_EQ(entries[1].first,  std::string{"second"});
    REQUIRE_EQ(entries[2].first,  std::string{"third"});
}
