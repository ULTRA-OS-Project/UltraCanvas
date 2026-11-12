// Apps/UltraStoreOperator/cli/Commands.cpp
// ultrastoreop <command>: the operator's marketplace from a prompt, on the
// same OperatorClient the window uses, so the two never disagree.
//
//   ultrastoreop settings [set url|token|name <value>]
//   ultrastoreop health
//   ultrastoreop merchant list | show <id> | add <name> --key <hex> [--id <id>] [--contact <c>]
//   ultrastoreop merchant set <id> [--name <n>] [--contact <c>] [--key <hex>]
//   ultrastoreop merchant suspend <id> [--clause <c>] [--reason <r>] [--days <n>] | reinstate <id> [--reason <r>]
//   ultrastoreop app suspend <app-id> [--clause <c>] [--reason <r>] [--days <n>] | reinstate <app-id> [--reason <r>]
//   ultrastoreop review list [--state <s>] [--merchant <id>] [--app <id>] | show <submission>
//   ultrastoreop review start|release|changes|reject <submission> [--message <m>] [--sequence <n>]
//   ultrastoreop released | publish | key
//   ultrastoreop report list [--state <s>] | show <id> | open <app|release|merchant> <target> <kind> <text> [--reporter <r>]
//   ultrastoreop report assign <id> <assignee> | resolve <id> [--findings <f>] [--outcome <o>] | dismiss <id> [--findings <f>] [--outcome <o>]
//   ultrastoreop notice list | send <kind> <subject> --to <merchant|*> ... [--text <t>] [--clause <c>]
//   ultrastoreop terms [add <in-force-from> <text|@file> [--changes <c>] [--version <n>]]
//   ultrastoreop policy [set <key> <value>]
//   ultrastoreop audit [--since <id>] [--limit <n>] | verify
//
// The command line stays English: it runs before any language is loaded.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Commands.h"

#include "Operator.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ULTRASTOREOPERATOR_VERSION
#error "ULTRASTOREOPERATOR_VERSION must be passed by CMake (from Docs/UltraStoreOperator/CHANGELOG.md)"
#endif

using namespace UltraStoreOperator;
using UltraStore::Error;

int UltraStoreOperator::Cli::Usage(FILE* to) {
    std::fprintf(to,
        "ultrastoreop %s - the UltraStore operator's window and command line\n"
        "\n"
        "  ultrastoreop [--settings <file>] [--page <id>]         the window (on the page: dashboard merchants review reports terms audit settings)\n"
        "  ultrastoreop [--settings <file>] [--url <base>] [--token <t>] <command>\n"
        "\n"
        "  settings [set url|token|name <value>]\n"
        "  health\n"
        "  merchant list | show <id> | add <name> --key <hex> [--id <id>] [--contact <c>]\n"
        "  merchant set <id> [--name <n>] [--contact <c>] [--key <hex>]\n"
        "  merchant suspend <id> [--clause <c>] [--reason <r>] [--days <n>] | reinstate <id> [--reason <r>]\n"
        "  app suspend <app-id> [--clause <c>] [--reason <r>] [--days <n>] | reinstate <app-id> [--reason <r>]\n"
        "  review list [--state <s>] [--merchant <id>] [--app <id>] | show <submission>\n"
        "  review start | release | changes | reject <submission> [--message <m>] [--sequence <n>]\n"
        "  released | publish | key\n"
        "  report list [--state <s>] | show <id> | open app|release|merchant <target> <kind> <text> [--reporter <r>]\n"
        "  report assign <id> <assignee> | resolve <id> [--findings <f>] [--outcome <o>] | dismiss <id> [--findings <f>] [--outcome <o>]\n"
        "  notice list | send <kind> <subject> --to <merchant|*> [--to ...] [--text <t>] [--clause <c>]\n"
        "  terms [add <in-force-from> <text|@file> [--changes <c>] [--version <n>]]\n"
        "  policy [set strike_expiry_days|strikes_before_suspension|suspension_days|answer_deadline_days|clauses_that_suspend <value>]\n"
        "  audit [--since <id>] [--limit <n>] | audit verify\n"
        "  --version | --help\n"
        "\n"
        "The settings file (settings.json under the data folder) keeps the service's URL, the operator token and your name;\n"
        "ULTRASTORE_OPERATOR_URL and ULTRASTORE_OPERATOR_TOKEN override it. Notice kinds: announcement reminder information_request warning suspension.\n",
        ULTRASTOREOPERATOR_VERSION);
    return to == stdout ? 0 : 2;
}

namespace {

int Usage(FILE* to) { return UltraStoreOperator::Cli::Usage(to); }

int Fail(const Error& e) {
    std::fprintf(stderr, "ultrastoreop: %s", e.message.c_str());
    if (!e.detail.empty()) std::fprintf(stderr, " (%s)", e.detail.c_str());
    std::fprintf(stderr, " [%s]\n", UltraStore::ErrorCodeName(e.code).c_str());
    return 1;
}

int Fail(const std::string& message) {
    std::fprintf(stderr, "ultrastoreop: %s\n", message.c_str());
    return 1;
}

std::string Option(const std::vector<std::string>& args, const std::string& name, const std::string& fallback = {}) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return args[i + 1];
    return fallback;
}

std::vector<std::string> Options(const std::vector<std::string>& args, const std::string& name) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) out.push_back(args[i + 1]);
    return out;
}

std::vector<std::string> Positional(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].size() > 1 && args[i][0] == '-') { ++i; continue; }
        out.push_back(args[i]);
    }
    return out;
}

int Number(const std::string& text, int fallback = 0) {
    if (text.empty()) return fallback;
    return std::atoi(text.c_str());   // locale-ok: digits only
}

// A text given as such, or as @file.
std::string TextOrFile(const std::string& arg) {
    if (arg.rfind("@", 0) != 0) return arg;
    std::ifstream in(UltraCanvas::PathFromUtf8(arg.substr(1)), std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

const char* Or(const std::string& s, const char* fallback) { return s.empty() ? fallback : s.c_str(); }

void PrintMerchant(const Merchant& m) {
    std::printf("%-12s %-32s %-10s %d strike(s)  terms in force %d, accepted %d  %s\n", m.id.c_str(), m.name.c_str(), m.standing.c_str(), m.strikes,
                m.termsInForce, m.termsAccepted, m.contact.c_str());
}

void PrintSubmission(const Submission& s) {
    std::printf("%s  %-32s %-10s %-18s %-12s %s", s.id.c_str(), s.app.c_str(), s.version.c_str(), s.state.c_str(), s.merchant.c_str(), s.createdAt.c_str());
    if (!s.decisions.empty()) {
        const Decision& last = s.decisions.back();
        std::printf("  %s%s", last.action.c_str(), last.message.empty() ? "" : (": " + last.message).c_str());
    }
    std::printf("\n");
}

void PrintReport(const Report& r) {
    std::printf("%s  %-10s %-12s %s %s  %s  by %s%s%s\n", r.id.c_str(), r.kind.c_str(), r.state.c_str(), r.targetKind.c_str(), r.target.c_str(),
                r.createdAt.c_str(), r.source.c_str(), r.reporter.empty() ? "" : (" " + r.reporter).c_str(),
                r.assignee.empty() ? "" : ("  assigned to " + r.assignee).c_str());
}

// ----- settings -----
int CmdSettings(const std::string& path, const std::vector<std::string>& args) {
    auto loaded = OperatorSettings::Load(path);
    if (!loaded) return Fail(loaded.error);
    OperatorSettings s = loaded.Value();
    const auto pos = Positional(args);
    if (pos.size() >= 4 && pos[1] == "set") {
        if (pos[2] == "url") s.url = pos[3];
        else if (pos[2] == "token") s.token = pos[3];
        else if (pos[2] == "name") s.name = pos[3];
        else return Fail("The settings are url, token and name");
        if (const auto saved = s.Save(path); !saved) return Fail(saved.error);
        std::printf("%s set in %s\n", pos[2].c_str(), path.c_str());
        return 0;
    }
    if (pos.size() != 1) return Usage(stderr);
    std::printf("settings %s\nurl    %s\ntoken  %s\nname   %s\n", path.c_str(), Or(s.url, "(not set)"), s.token.empty() ? "(not set)" : "(set)", Or(s.name, "(not set)"));
    return 0;
}

// ----- merchants -----
int CmdMerchant(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    if (pos[1] == "list") {
        const auto list = client.Merchants();
        if (!list) return Fail(list.error);
        if (list.Value().empty()) { std::printf("no merchants registered\n"); return 0; }
        for (const Merchant& m : list.Value()) PrintMerchant(m);
        return 0;
    }
    if (pos[1] == "show") {
        if (pos.size() < 3) return Usage(stderr);
        const auto file = client.GetMerchant(pos[2]);
        if (!file) return Fail(file.error);
        const MerchantFile& f = file.Value();
        PrintMerchant(f.merchant);
        std::printf("  key %s\n  since %s\n", f.merchant.key.c_str(), f.merchant.createdAt.c_str());
        if (!f.notes.empty()) std::printf("  notes: %s\n", f.notes.c_str());
        for (const Acceptance& a : f.acceptances) std::printf("  terms %d accepted by %s at %s\n", a.version, a.acceptedBy.c_str(), a.acceptedAt.c_str());
        for (const Suspension& s : f.suspensions)
            std::printf("  suspension #%lld %s from %s%s%s%s by %s%s\n", static_cast<long long>(s.id), s.clause.empty() ? "" : ("clause " + s.clause).c_str(), s.start.c_str(),
                        s.end.empty() ? " until reinstated" : (" to " + s.end).c_str(), s.reason.empty() ? "" : ": ", s.reason.c_str(), s.createdBy.c_str(),
                        s.Lifted() ? ("  lifted " + s.liftedAt).c_str() : "");
        if (!f.submissions.empty()) std::printf("  submissions:\n");
        for (const Submission& s : f.submissions) { std::printf("  "); PrintSubmission(s); }
        return 0;
    }
    if (pos[1] == "add") {
        if (pos.size() < 3) return Usage(stderr);
        const std::string key = Option(args, "--key");
        if (key.empty()) return Fail("The merchant's key: --key <64 hex characters> (the public half of the key that signs its submissions)");
        const auto m = client.RegisterMerchant(Option(args, "--id"), pos[2], Option(args, "--contact"), key);
        if (!m) return Fail(m.error);
        std::printf("registered %s as %s\n", m.Value().name.c_str(), m.Value().id.c_str());
        return 0;
    }
    if (pos[1] == "set") {
        if (pos.size() < 3) return Usage(stderr);
        const auto m = client.UpdateMerchant(pos[2], Option(args, "--name"), Option(args, "--contact"), Option(args, "--key"));
        if (!m) return Fail(m.error);
        PrintMerchant(m.Value());
        return 0;
    }
    if (pos[1] == "suspend") {
        if (pos.size() < 3) return Usage(stderr);
        const auto m = client.SuspendMerchant(pos[2], Option(args, "--clause"), Option(args, "--reason"), Number(Option(args, "--days")));
        if (!m) return Fail(m.error);
        std::printf("suspended %s: %s\n", pos[2].c_str(), m.Value().standing.c_str());
        return 0;
    }
    if (pos[1] == "reinstate") {
        if (pos.size() < 3) return Usage(stderr);
        const auto m = client.ReinstateMerchant(pos[2], Option(args, "--reason"));
        if (!m) return Fail(m.error);
        std::printf("reinstated %s: %s\n", pos[2].c_str(), m.Value().standing.c_str());
        return 0;
    }
    return Usage(stderr);
}

int CmdApp(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    if (pos[1] == "suspend") {
        const auto s = client.SuspendApp(pos[2], Option(args, "--clause"), Option(args, "--reason"), Number(Option(args, "--days")));
        if (!s) return Fail(s.error);
        std::printf("suspended %s%s: it leaves the next catalog\n", pos[2].c_str(), s.Value().end.empty() ? " until reinstated" : (" until " + s.Value().end).c_str());
        return 0;
    }
    if (pos[1] == "reinstate") {
        const auto n = client.ReinstateApp(pos[2], Option(args, "--reason"));
        if (!n) return Fail(n.error);
        std::printf("reinstated %s (%d suspension(s) lifted)\n", pos[2].c_str(), n.Value());
        return 0;
    }
    return Usage(stderr);
}

// ----- the review -----
int CmdReview(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    if (pos[1] == "list") {
        const auto list = client.Submissions(Option(args, "--state"), Option(args, "--merchant"), Option(args, "--app"));
        if (!list) return Fail(list.error);
        if (list.Value().empty()) { std::printf("no submissions\n"); return 0; }
        for (const Submission& s : list.Value()) PrintSubmission(s);
        return 0;
    }
    if (pos[1] == "show") {
        if (pos.size() < 3) return Usage(stderr);
        const auto sub = client.GetSubmission(pos[2]);
        if (!sub) return Fail(sub.error);
        const Submission& s = sub.Value();
        PrintSubmission(s);
        std::printf("  %s by %s%s\n", s.Name().c_str(), s.merchant.c_str(), s.answers.empty() ? "" : ("  answers " + s.answers).c_str());
        for (const auto& [name, outcome] : s.checks) std::printf("  %-18s %s\n", name.c_str(), outcome.c_str());
        for (const UltraCanvas::JSONValue& p : s.packages.GetElements())
            std::printf("  package %s %s %lld bytes\n", p.Get("file").GetString(p.Get("url").GetString()).c_str(), p.Get("sha256").GetString().c_str(),
                        static_cast<long long>(p.Get("size").GetInteger(0)));
        for (const Decision& d : s.decisions)
            std::printf("  %s  %s by %s%s%s\n", d.at.c_str(), d.action.c_str(), d.actor.c_str(), d.sequence > 0 ? ("  sequence " + std::to_string(d.sequence)).c_str() : "",
                        d.message.empty() ? "" : ("\n    " + d.message).c_str());
        return 0;
    }
    const char* action = pos[1] == "start" ? "start_review" : pos[1] == "release" ? "release" : pos[1] == "changes" ? "request_changes" : pos[1] == "reject" ? "reject" : nullptr;
    if (!action || pos.size() < 3) return Usage(stderr);
    const auto s = client.Decide(pos[2], action, Option(args, "--message"), Number(Option(args, "--sequence")));
    if (!s) return Fail(s.error);
    std::printf("%s: %s%s\n", pos[2].c_str(), s.Value().state.c_str(), s.Value().releasedSequence > 0 ? ("  sequence " + std::to_string(s.Value().releasedSequence)).c_str() : "");
    return 0;
}

int CmdReleased(const OperatorClient& client) {
    const auto r = client.GetReleased();
    if (!r) return Fail(r.error);
    std::printf("last publish: sequence %d; last release: sequence %d; %zu app(s) go into the next publish\n", r.Value().published, r.Value().sequence, r.Value().entries.size());
    for (const ReleasedEntry& e : r.Value().entries)
        std::printf("  %-32s %-12s %s  sequence %d\n", e.app.c_str(), e.merchant.c_str(), e.submission.c_str(), e.sequence);
    return 0;
}

int CmdPublish(const OperatorClient& client) {
    const auto p = client.Publish();
    if (!p) return Fail(p.error);
    const PublishReport& r = p.Value();
    std::printf("published sequence %d: %d app(s), %d component(s), %d publisher(s), signed with %s\n", r.sequence, r.apps, r.components, r.publishers, r.publicKey.c_str());
    for (const std::string& f : r.files) std::printf("  %s\n", f.c_str());
    for (const std::string& l : r.leftOut) std::printf("  left out: %s\n", l.c_str());
    return 0;
}

int CmdKey(const OperatorClient& client) {
    const auto k = client.GetPublishKey();
    if (!k) return Fail(k.error);
    std::printf("public key %s\n", k.Value().publicKey.c_str());
    for (const std::string& t : k.Value().trustedKeys) std::printf("  trusted %s\n", t.c_str());
    return 0;
}

// ----- reports -----
int CmdReport(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    if (pos[1] == "list") {
        const auto list = client.Reports(Option(args, "--state"));
        if (!list) return Fail(list.error);
        if (list.Value().empty()) { std::printf("no reports\n"); return 0; }
        for (const Report& r : list.Value()) PrintReport(r);
        return 0;
    }
    if (pos[1] == "show") {
        if (pos.size() < 3) return Usage(stderr);
        const auto rep = client.GetReport(pos[2]);
        if (!rep) return Fail(rep.error);
        const Report& r = rep.Value();
        PrintReport(r);
        std::printf("  %s\n", r.text.c_str());
        for (const std::string& a : r.attachments) std::printf("  attachment %s\n", a.c_str());
        if (!r.findings.empty()) std::printf("  findings: %s\n", r.findings.c_str());
        if (!r.outcome.empty()) std::printf("  outcome: %s (%s)\n", r.outcome.c_str(), r.resolvedAt.c_str());
        return 0;
    }
    if (pos[1] == "open") {
        if (pos.size() < 6) return Usage(stderr);
        const auto r = client.OpenReport(pos[2], pos[3], pos[4], TextOrFile(pos[5]), Option(args, "--reporter"));
        if (!r) return Fail(r.error);
        std::printf("opened %s\n", r.Value().id.c_str());
        return 0;
    }
    if (pos[1] == "assign") {
        if (pos.size() < 4) return Usage(stderr);
        const auto r = client.AssignReport(pos[2], pos[3]);
        if (!r) return Fail(r.error);
        std::printf("%s assigned to %s\n", pos[2].c_str(), r.Value().assignee.c_str());
        return 0;
    }
    if (pos[1] == "resolve" || pos[1] == "dismiss") {
        if (pos.size() < 3) return Usage(stderr);
        const auto r = client.CloseReport(pos[2], pos[1] == "resolve", TextOrFile(Option(args, "--findings")), Option(args, "--outcome"));
        if (!r) return Fail(r.error);
        std::printf("%s %s\n", pos[2].c_str(), r.Value().state.c_str());
        return 0;
    }
    return Usage(stderr);
}

// ----- notices -----
int CmdNotice(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    if (pos[1] == "list") {
        const auto list = client.Notices();
        if (!list) return Fail(list.error);
        if (list.Value().empty()) { std::printf("no notices sent\n"); return 0; }
        for (const Notice& n : list.Value()) {
            std::string to;
            for (const std::string& r : n.recipients) to += (to.empty() ? "" : ", ") + (r == "*" ? std::string("everyone") : r);
            std::printf("%s  %s  %-20s to %s: %s%s%s\n", n.id.c_str(), n.createdAt.c_str(), n.kind.c_str(), to.c_str(), n.subject.c_str(),
                        n.clause.empty() ? "" : ("  (clause " + n.clause + ")").c_str(), n.deadline.empty() ? "" : ("  answer by " + n.deadline).c_str());
        }
        return 0;
    }
    if (pos[1] == "send") {
        if (pos.size() < 4) return Usage(stderr);
        const auto r = client.SendNotice(pos[2], pos[3], TextOrFile(Option(args, "--text")), Option(args, "--clause"), Options(args, "--to"));
        if (!r) return Fail(r.error);
        std::printf("sent %s\n", r.Value().notice.id.c_str());
        for (const Suspension& s : r.Value().suspended) std::printf("  the policy suspended %s%s\n", s.target.c_str(), s.end.empty() ? "" : (" until " + s.end).c_str());
        return 0;
    }
    return Usage(stderr);
}

// ----- terms and policy -----
int CmdTerms(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() >= 4 && pos[1] == "add") {
        const std::string text = TextOrFile(pos[3]);
        if (text.empty()) return Fail("The terms' text, or @file naming it");
        const auto t = client.AddTerms(text, Option(args, "--changes"), pos[2], Number(Option(args, "--version")));
        if (!t) return Fail(t.error);
        std::printf("terms version %d, in force from %s\n", t.Value().version, t.Value().inForceFrom.c_str());
        return 0;
    }
    if (pos.size() != 1) return Usage(stderr);
    const auto t = client.Terms();
    if (!t) return Fail(t.error);
    if (t.Value().versions.empty()) { std::printf("no terms yet: terms add <in-force-from> <text|@file>\n"); return 0; }
    for (const TermsVersion& v : t.Value().versions)
        std::printf("version %d  in force from %s%s%s\n", v.version, v.inForceFrom.c_str(), v.version == t.Value().inForce ? "  (current)" : "",
                    v.changes.empty() ? "" : ("  " + v.changes).c_str());
    return 0;
}

int CmdPolicy(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    auto print = [](const Policy& p) {
        std::string clauses;
        for (const std::string& c : p.clausesThatSuspend) clauses += (clauses.empty() ? "" : " ") + c;
        std::printf("strike_expiry_days         %d\nstrikes_before_suspension  %d\nsuspension_days            %d\nanswer_deadline_days       %d\nclauses_that_suspend       %s\n",
                    p.strikeExpiryDays, p.strikesBeforeSuspension, p.suspensionDays, p.answerDeadlineDays, clauses.empty() ? "(none)" : clauses.c_str());
    };
    if (pos.size() >= 4 && pos[1] == "set") {
        const auto current = client.GetPolicy();
        if (!current) return Fail(current.error);
        Policy p = current.Value();
        const std::string& key = pos[2];
        if (key == "strike_expiry_days") p.strikeExpiryDays = Number(pos[3]);
        else if (key == "strikes_before_suspension") p.strikesBeforeSuspension = Number(pos[3]);
        else if (key == "suspension_days") p.suspensionDays = Number(pos[3]);
        else if (key == "answer_deadline_days") p.answerDeadlineDays = Number(pos[3]);
        else if (key == "clauses_that_suspend") {
            p.clausesThatSuspend.clear();
            std::istringstream in(pos[3]);
            for (std::string c; in >> c;) if (c != "-") p.clausesThatSuspend.push_back(c);
        } else return Fail("The policy's keys: strike_expiry_days strikes_before_suspension suspension_days answer_deadline_days clauses_that_suspend");
        const auto saved = client.SetPolicy(p);
        if (!saved) return Fail(saved.error);
        print(saved.Value());
        return 0;
    }
    if (pos.size() != 1) return Usage(stderr);
    const auto p = client.GetPolicy();
    if (!p) return Fail(p.error);
    print(p.Value());
    return 0;
}

int CmdAudit(const OperatorClient& client, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() >= 2 && pos[1] == "verify") {
        const auto c = client.VerifyAudit();
        if (!c) return Fail(c.error);
        if (c.Value().ok) { std::printf("the audit log verifies: %lld row(s), last hash %s\n", static_cast<long long>(c.Value().rows), c.Value().lastHash.c_str()); return 0; }
        std::printf("the audit log is BROKEN at row %lld (of %lld)\n", static_cast<long long>(c.Value().brokenAt), static_cast<long long>(c.Value().rows));
        return 1;
    }
    if (pos.size() != 1) return Usage(stderr);
    const auto page = client.Audit(Number(Option(args, "--since")), Number(Option(args, "--limit"), 500));
    if (!page) return Fail(page.error);
    if (page.Value().rows.empty()) { std::printf("nothing in the audit log\n"); return 0; }
    for (const AuditRow& r : page.Value().rows) std::printf("%6lld  %s\n", static_cast<long long>(r.id), r.Line().c_str());
    std::printf("next: --since %lld\n", static_cast<long long>(page.Value().next));
    return 0;
}

} // namespace

int UltraStoreOperator::Cli::Run(const std::vector<std::string>& argsIn) {
    std::vector<std::string> args = argsIn;
    std::string settingsPath = OperatorSettings::DefaultPath();
    std::string urlOverride, tokenOverride;
    for (size_t i = 0; i + 1 < args.size();) {
        if (args[i] == "--settings") { settingsPath = args[i + 1]; }
        else if (args[i] == "--url") { urlOverride = args[i + 1]; }
        else if (args[i] == "--token") { tokenOverride = args[i + 1]; }
        else { ++i; continue; }
        args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
    }
    if (args.empty()) return Usage(stderr);
    const std::string& cmd = args[0];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") return Usage(stdout);
    if (cmd == "--version" || cmd == "-V") { std::printf("ultrastoreop %s\n", ULTRASTOREOPERATOR_VERSION); return 0; }
    if (cmd == "settings") return CmdSettings(settingsPath, args);

    const auto loaded = OperatorSettings::Load(settingsPath);
    if (!loaded) return Fail(loaded.error);
    OperatorSettings settings = loaded.Value().WithEnvironment();
    if (!urlOverride.empty()) settings.url = urlOverride;
    if (!tokenOverride.empty()) settings.token = tokenOverride;
    const OperatorClient client(settings);
    if (cmd == "health") {
        const auto h = client.Health();
        if (!h) return Fail(h.error);
        std::printf("%s\n", h.Value().c_str());
        return 0;
    }
    if (cmd == "merchant") return CmdMerchant(client, args);
    if (cmd == "app")      return CmdApp(client, args);
    if (cmd == "review")   return CmdReview(client, args);
    if (cmd == "released") return CmdReleased(client);
    if (cmd == "publish")  return CmdPublish(client);
    if (cmd == "key")      return CmdKey(client);
    if (cmd == "report")   return CmdReport(client, args);
    if (cmd == "notice")   return CmdNotice(client, args);
    if (cmd == "terms")    return CmdTerms(client, args);
    if (cmd == "policy")   return CmdPolicy(client, args);
    if (cmd == "audit")    return CmdAudit(client, args);
    std::fprintf(stderr, "ultrastoreop: unknown command \"%s\"\n", cmd.c_str());
    return Usage(stderr);
}
