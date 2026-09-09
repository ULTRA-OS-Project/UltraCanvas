// Apps/EmailCleaner/engine/EmailCleanerAnalytics.cpp
// Shaping the store's aggregates into the map / timetable / timeline view
// models, plus the shared category palette and summary strings.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerAnalytics.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace EmailCleaner {

namespace {

// Thousands separators, so "1204" reads as "1,204" in the summaries.
std::string WithSeparators(long long value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && count % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    if (value < 0) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

MapColor Blend(const MapColor& from, const MapColor& to, double t) {
    t = std::clamp(t, 0.0, 1.0);
    MapColor out;
    out.r = static_cast<uint8_t>(from.r + (to.r - from.r) * t);
    out.g = static_cast<uint8_t>(from.g + (to.g - from.g) * t);
    out.b = static_cast<uint8_t>(from.b + (to.b - from.b) * t);
    return out;
}

// Aggregate a domain's children into the parent's numbers.
void FoldChildIntoParent(const SenderBlock& child, SenderBlock& parent) {
    parent.messageCount    += child.messageCount;
    parent.unwantedCount   += child.unwantedCount;
    parent.attachmentCount += child.attachmentCount;
    parent.totalBytes      += child.totalBytes;
    parent.attachmentBytes += child.attachmentBytes;
    if (parent.firstSeen == 0 || (child.firstSeen != 0 && child.firstSeen < parent.firstSeen))
        parent.firstSeen = child.firstSeen;
    if (child.lastSeen > parent.lastSeen) parent.lastSeen = child.lastSeen;
    // Message-count weighted mean, so a one-message sender cannot dominate.
    const int total = parent.messageCount;
    if (total > 0) {
        parent.averageScore =
            (parent.averageScore * (total - child.messageCount) +
             child.averageScore * child.messageCount) / total;
    }
}

// The category a group of blocks reads as: the one with the most messages,
// with unwanted families winning ties.
MessageCategory DominantCategory(const std::vector<SenderBlock>& blocks) {
    std::map<MessageCategory, int> counts;
    for (const SenderBlock& b : blocks) counts[b.topCategory] += b.messageCount;

    MessageCategory best = MessageCategory::Unclassified;
    int bestCount = -1;
    for (const auto& [category, count] : counts) {
        if (count > bestCount ||
            (count == bestCount && IsUnwanted(category) && !IsUnwanted(best))) {
            best = category;
            bestCount = count;
        }
    }
    return best;
}

MapNode MakeSenderNode(const SenderBlock& block, SenderMetric metric) {
    MapNode node;
    node.key      = block.senderAddr;
    node.label    = block.displayName.empty() ? block.senderAddr : block.displayName;
    node.value    = MetricValue(block, metric);
    node.category = block.topCategory;
    node.color    = Analytics::BlockColor(block);
    node.tooltip  = Analytics::DescribeBlock(block);
    node.block    = block;
    return node;
}

} // namespace

// ---- Palette ---------------------------------------------------------------

MapColor Analytics::CategoryColor(MessageCategory category) {
    switch (category) {
        // Legitimate traffic: cool, low-attention colours.
        case MessageCategory::Personal:      return MapColor{  52, 152, 219 };  // blue
        case MessageCategory::Newsletter:    return MapColor{  26, 188, 156 };  // teal
        case MessageCategory::Notification:  return MapColor{ 149, 165, 166 };  // grey
        // The unwanted families: warm, and increasingly alarming.
        case MessageCategory::ProductSpam:   return MapColor{ 241, 156,  15 };  // amber
        case MessageCategory::AdultContent:  return MapColor{ 155,  89, 182 };  // purple
        case MessageCategory::DatingScam:    return MapColor{ 233,  95, 150 };  // pink
        case MessageCategory::PhishingScam:  return MapColor{ 230, 126,  34 };  // orange
        case MessageCategory::FinancialScam: return MapColor{ 214,  69,  65 };  // red
        case MessageCategory::MalwareRisk:   return MapColor{ 155,  25,  25 };  // dark red
        case MessageCategory::Unclassified:  break;
    }
    return MapColor{ 189, 195, 199 };   // light grey
}

MapColor Analytics::BlockColor(const SenderBlock& block) {
    const MapColor base = CategoryColor(block.topCategory);
    if (IsUnwanted(block.topCategory)) {
        // Already a warning colour; deepen it with the unwanted share.
        return Blend(base, MapColor{ 120, 20, 20 }, block.UnwantedRatio() * 0.45);
    }
    // A "clean" category that nonetheless carries spam should not look clean.
    return Blend(base, CategoryColor(MessageCategory::ProductSpam),
                 block.UnwantedRatio());
}

// ---- Map -------------------------------------------------------------------

bool Analytics::BuildSenderMap(const MessageFilter& filter, SenderMetric metric,
                               const MapShape& shape, MapNode& out) const {
    out = MapNode{};
    out.key   = "all";
    out.label = "All senders";

    std::vector<SenderBlock> senders;
    if (!store_.ListSenderBlocks(filter, metric, /*limit=*/0, senders))
        return false;
    if (senders.empty()) return true;

    double totalValue = 0.0;
    for (const SenderBlock& b : senders) totalValue += MetricValue(b, metric);

    if (!shape.groupByDomain) {
        const int keep = shape.maxSendersPerDomain > 0
                       ? std::min<int>(shape.maxSendersPerDomain, static_cast<int>(senders.size()))
                       : static_cast<int>(senders.size());
        for (int i = 0; i < keep; ++i)
            out.children.push_back(MakeSenderNode(senders[i], metric));

        if (keep < static_cast<int>(senders.size())) {
            MapNode other;
            other.key   = "other";
            other.label = "Other (" + std::to_string(senders.size() - keep) + " senders)";
            other.isAggregate = true;
            other.color = CategoryColor(MessageCategory::Unclassified);
            for (size_t i = keep; i < senders.size(); ++i) {
                other.value += MetricValue(senders[i], metric);
                FoldChildIntoParent(senders[i], other.block);
            }
            other.tooltip = DescribeBlock(other.block);
            if (other.value > 0) out.children.push_back(std::move(other));
        }
        out.value = totalValue;
        return true;
    }

    // Group senders under their domain, keeping the store's metric ordering
    // inside each group.
    std::map<std::string, std::vector<SenderBlock>> byDomain;
    for (const SenderBlock& b : senders) {
        const std::string domain = b.domain.empty() ? "(no domain)" : b.domain;
        byDomain[domain].push_back(b);
    }

    struct DomainEntry {
        std::string domain;
        double value = 0.0;
        std::vector<SenderBlock> senders;
    };
    std::vector<DomainEntry> domains;
    domains.reserve(byDomain.size());
    for (auto& [domain, blocks] : byDomain) {
        DomainEntry entry;
        entry.domain = domain;
        for (const SenderBlock& b : blocks) entry.value += MetricValue(b, metric);
        entry.senders = std::move(blocks);
        domains.push_back(std::move(entry));
    }
    std::sort(domains.begin(), domains.end(),
              [](const DomainEntry& a, const DomainEntry& b) {
                  if (a.value != b.value) return a.value > b.value;
                  return a.domain < b.domain;
              });

    MapNode other;
    other.key   = "other";
    other.isAggregate = true;
    other.color = CategoryColor(MessageCategory::Unclassified);
    int pooledDomains = 0;

    for (size_t i = 0; i < domains.size(); ++i) {
        const DomainEntry& entry = domains[i];
        const bool overCap = shape.maxDomains > 0 &&
                             static_cast<int>(out.children.size()) >= shape.maxDomains;
        const bool tooSmall = totalValue > 0.0 && shape.minShare > 0.0 &&
                              (entry.value / totalValue) < shape.minShare;
        if (overCap || tooSmall) {
            other.value += entry.value;
            for (const SenderBlock& b : entry.senders) FoldChildIntoParent(b, other.block);
            ++pooledDomains;
            continue;
        }

        MapNode domainNode;
        domainNode.key   = entry.domain;
        domainNode.label = entry.domain;
        domainNode.value = entry.value;

        const int keep = shape.maxSendersPerDomain > 0
                       ? std::min<int>(shape.maxSendersPerDomain,
                                       static_cast<int>(entry.senders.size()))
                       : static_cast<int>(entry.senders.size());
        for (int s = 0; s < keep; ++s) {
            domainNode.children.push_back(MakeSenderNode(entry.senders[s], metric));
            FoldChildIntoParent(entry.senders[s], domainNode.block);
        }
        if (keep < static_cast<int>(entry.senders.size())) {
            MapNode tail;
            tail.key   = entry.domain + "/other";
            tail.label = "Other (" + std::to_string(entry.senders.size() - keep) + ")";
            tail.isAggregate = true;
            for (size_t s = keep; s < entry.senders.size(); ++s) {
                tail.value += MetricValue(entry.senders[s], metric);
                FoldChildIntoParent(entry.senders[s], tail.block);
                FoldChildIntoParent(entry.senders[s], domainNode.block);
            }
            tail.category = DominantCategory(
                std::vector<SenderBlock>(entry.senders.begin() + keep, entry.senders.end()));
            tail.color   = CategoryColor(tail.category);
            tail.tooltip = DescribeBlock(tail.block);
            domainNode.children.push_back(std::move(tail));
        }

        domainNode.block.senderAddr  = entry.domain;
        domainNode.block.displayName = entry.domain;
        domainNode.block.domain      = entry.domain;
        domainNode.category = DominantCategory(entry.senders);
        domainNode.block.topCategory = domainNode.category;
        domainNode.color    = BlockColor(domainNode.block);
        domainNode.tooltip  = DescribeBlock(domainNode.block);
        out.children.push_back(std::move(domainNode));
    }

    if (pooledDomains > 0 && other.value > 0.0) {
        other.label   = "Other (" + std::to_string(pooledDomains) + " domains)";
        other.tooltip = DescribeBlock(other.block);
        out.children.push_back(std::move(other));
    }

    out.value = totalValue;
    return true;
}

// ---- Timetable and timeline ------------------------------------------------

bool Analytics::BuildTimetable(const MessageFilter& filter, Timetable& out) const {
    return static_cast<bool>(store_.GetTimetable(filter, out));
}

bool Analytics::BuildTimeline(const MessageFilter& filter, TimeBucket bucket,
                              std::vector<TimelinePoint>& out) const {
    return static_cast<bool>(store_.GetTimeline(filter, bucket, out));
}

TimeBucket Analytics::ChooseBucket(int64_t firstDate, int64_t lastDate) {
    if (firstDate <= 0 || lastDate <= firstDate) return TimeBucket::Month;
    const int64_t days = (lastDate - firstDate) / 86400;
    if (days <= 60)    return TimeBucket::Day;     // up to ~60 columns
    if (days <= 400)   return TimeBucket::Week;    // up to ~57 columns
    if (days <= 4000)  return TimeBucket::Month;   // up to ~131 columns
    return TimeBucket::Year;
}

bool Analytics::BuildOverview(const MessageFilter& filter, StoreOverview& out) const {
    return static_cast<bool>(store_.GetOverview(filter, out));
}

bool Analytics::BuildCategoryLegend(const MessageFilter& filter,
                                    std::vector<CategoryTotal>& out) const {
    std::vector<CategoryTotal> totals;
    if (!store_.GetCategoryTotals(filter, totals)) return false;

    // Present them in taxonomy order rather than by count, so the legend does
    // not reshuffle every time the filter changes.
    out.clear();
    for (MessageCategory category : AllCategories()) {
        for (const CategoryTotal& t : totals) {
            if (t.category == category && t.messageCount > 0) {
                out.push_back(t);
                break;
            }
        }
    }
    return true;
}

// ---- Summaries -------------------------------------------------------------

std::string Analytics::DescribeBlock(const SenderBlock& block) {
    std::string out = WithSeparators(block.messageCount) +
                      (block.messageCount == 1 ? " message" : " messages");
    if (block.unwantedCount > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " · %d%% unwanted",
                      static_cast<int>(block.UnwantedRatio() * 100.0 + 0.5));
        out += buf;
    }
    if (block.totalBytes > 0) out += " · " + FormatBytes(block.totalBytes);
    if (block.attachmentCount > 0) {
        out += " · " + WithSeparators(block.attachmentCount) + " attachments (" +
               FormatBytes(block.attachmentBytes) + ")";
    }
    if (block.firstSeen > 0)
        out += " · " + FormatDate(block.firstSeen) + " - " + FormatDate(block.lastSeen);
    if (block.topCategory != MessageCategory::Unclassified)
        out += " · mostly " + CategoryLabel(block.topCategory);
    return out;
}

std::string Analytics::DescribeTimetable(const Timetable& table) {
    if (table.total == 0) return "No dated messages";
    if (table.peakDay < 0) return WithSeparators(table.total) + " messages";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Busiest on %s %02d:00 (%d %s) · %s total",
                  WeekdayName(table.peakDay).c_str(), table.peakHour, table.peakCount,
                  table.peakCount == 1 ? "message" : "messages",
                  WithSeparators(table.total).c_str());
    return buf;
}

std::string Analytics::DescribeOverview(const StoreOverview& overview) {
    if (overview.messages == 0) return "No messages analysed yet";
    std::string out = WithSeparators(overview.messages) + " messages from " +
                      WithSeparators(overview.senders) + " senders";
    if (overview.unwanted > 0) {
        const int percent =
            static_cast<int>(100.0 * overview.unwanted / overview.messages + 0.5);
        out += " · " + WithSeparators(overview.unwanted) + " unwanted (" +
               std::to_string(percent) + "%)";
    }
    if (overview.attachments > 0) {
        out += " · " + WithSeparators(overview.attachments) + " attachments (" +
               FormatBytes(overview.attachmentBytes) + ")";
    }
    if (overview.firstDate > 0)
        out += " · " + FormatDate(overview.firstDate) + " - " + FormatDate(overview.lastDate);
    return out;
}

} // namespace EmailCleaner
