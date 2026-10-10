// Tests/UltraCanvasStart/test_plan.cpp
// The plan, the SDK names and the report.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "StartPlan.h"
#include "StartSdk.h"

using namespace UltraCanvasStart;

namespace {
SystemProfile UbuntuProfile() {
    SystemProfile p;
    p.platform = Platform::Linux;
    p.osName = "Ubuntu 24.04 LTS";
    p.architecture = "x86_64";
    p.distributionId = "ubuntu";
    p.packageManager = PackageManager::Apt;
    p.packageManagerPath = "/usr/bin/apt-get";
    p.homeDirectory = "/home/dev";
    return p;
}
} // namespace

TEST(SdkArtifactName_matches_the_workflow) {
    REQUIRE_EQ(SdkArtifactName(Platform::Linux, "0.9.147", "x86_64"),
               std::string("UltraCanvas-SDK-Linux-0.9.147-x86_64"));
    REQUIRE_EQ(SdkArchiveName(Platform::MacOS, "0.9.147", "arm64"),
               std::string("UltraCanvas-SDK-MacOS-0.9.147-arm64.tar.gz"));
    REQUIRE_EQ(SdkArchiveName(Platform::Windows, "0.9.147", "x86_64"),
               std::string("UltraCanvas-SDK-Windows-0.9.147-x86_64.zip"));
    REQUIRE_EQ(SdkArchiveName(Platform::Linux, "0.9.147", "x86_64"),
               std::string("UltraCanvas-SDK-Linux-0.9.147-x86_64.tar.xz"));
    REQUIRE(SdkReleaseAssetUrl(Platform::Linux, "1.0.0", "arm64").find(
        "/releases/download/v1.0.0/UltraCanvas-SDK-Linux-1.0.0-arm64.tar.xz") != std::string::npos);
    REQUIRE_EQ(SdkReleasePage("0.9.211"),
               std::string("https://github.com/ULTRA-OS-Project/UltraCanvas/releases/tag/v0.9.211"));
    REQUIRE(!FrameworkVersion().empty());
}

TEST(BuildPlan_installs_only_what_the_checks_miss) {
    const SystemProfile profile = UbuntuProfile();
    Choices choices;
    choices.platform = Platform::Linux;
    std::vector<CheckResult> checks(3);
    checks[0].dependencyId = "cmake"; checks[0].checked = true; checks[0].present = true;
    checks[0].packageName = "cmake";
    checks[1].dependencyId = "cairo"; checks[1].checked = true; checks[1].present = false;
    checks[1].packageName = "libcairo2-dev";
    checks[2].dependencyId = "pango"; checks[2].checked = true; checks[2].present = true;
    checks[2].versionOk = false; checks[2].packageName = "libpango1.0-dev";

    const Plan plan = BuildPlan(profile, choices, checks);
    REQUIRE_EQ(plan.MissingCount(), size_t(2));
    REQUIRE(!plan.steps.empty());
    const PlanStep& install = plan.steps.front();
    REQUIRE(install.kind == StepKind::Install);
    REQUIRE_EQ(install.argv[0], std::string("/usr/bin/apt-get"));
    bool cairo = false, pango = false, cmake = false;
    for (const auto& a : install.argv) {
        cairo |= a == "libcairo2-dev";
        pango |= a == "libpango1.0-dev";
        cmake |= a == "cmake";
    }
    REQUIRE(cairo);
    REQUIRE(pango);
    REQUIRE(!cmake);
}

TEST(BuildPlan_for_another_platform_lists_everything) {
    const SystemProfile profile = UbuntuProfile();
    Choices choices;
    choices.platform = Platform::Windows;
    const Plan plan = BuildPlan(profile, choices, {});
    REQUIRE(plan.steps.front().kind == StepKind::Install);
    REQUIRE_EQ(plan.steps.front().argv[0], std::string("pacman"));
    bool clang = false;
    for (const auto& a : plan.steps.front().argv) clang |= a == "mingw-w64-clang-x86_64-clang";
    REQUIRE(clang);
    bool msysNote = false;
    for (const auto& n : plan.notes) msysNote |= n.find("MSYS2") != std::string::npos;
    REQUIRE(msysNote);
    // The SDK step names the Windows archive.
    bool sdk = false;
    for (const auto& s : plan.steps) sdk |= s.kind == StepKind::Download && s.title.find("SDK-Windows") != std::string::npos;
    REQUIRE(sdk);
}

TEST(BuildPlan_follows_the_framework_and_ai_choices) {
    const SystemProfile profile = UbuntuProfile();
    Choices choices;
    choices.platform = Platform::Linux;
    choices.useSdk = false;
    choices.useAi = true;
    choices.cloudOnly = true;
    choices.projectFolder = "/home/dev/MyApp";
    const Plan plan = BuildPlan(profile, choices, {});
    bool clone = false, download = false, scaffold = false, ai = false;
    for (const auto& s : plan.steps) {
        clone |= s.kind == StepKind::Clone;
        download |= s.kind == StepKind::Download;
        scaffold |= s.kind == StepKind::Scaffold;
        ai |= s.kind == StepKind::Manual && s.title.find("GitHub") != std::string::npos;
    }
    REQUIRE(clone);
    REQUIRE(!download);
    REQUIRE(scaffold);
    REQUIRE(ai);
}

TEST(RenderReport_and_DisplayCommand) {
    const SystemProfile profile = UbuntuProfile();
    Choices choices;
    choices.platform = Platform::Linux;
    const Plan plan = BuildPlan(profile, choices, {});
    const std::string report = RenderReport(plan);
    REQUIRE(report.find("Ubuntu 24.04 LTS") != std::string::npos);
    REQUIRE(report.find("\nPlan\n") != std::string::npos);
    REQUIRE(report.find("sudo /usr/bin/apt-get install -y") != std::string::npos);
    REQUIRE_EQ(DisplayCommand({ "tar", "-xf", "my archive.tar.gz" }, false),
               std::string("tar -xf \"my archive.tar.gz\""));
}

TEST(UnpackStep_uses_tar) {
    const PlanStep step = UnpackStep("/tmp/sdk.tar.gz", "/tmp/out");
    REQUIRE(step.kind == StepKind::Unpack);
    REQUIRE_EQ(step.argv[0], std::string("tar"));
    REQUIRE_EQ(step.argv.back(), std::string("/tmp/out"));
}
