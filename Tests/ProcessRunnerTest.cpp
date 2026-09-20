// Tests/ProcessRunnerTest.cpp
// RunProcessCaptured: running a program, feeding it, and keeping what it says.
//
// This is small and easy to get wrong in ways that do not show up until the
// data is big. Three failures in particular are worth a permanent guard:
//
//   - Writing to a child that has stopped reading raises SIGPIPE, whose
//     default action kills the process. A print job whose filter rejects the
//     page would take the application down with it.
//   - Writing with a blocking descriptor while the child is blocked writing
//     output nobody is draining deadlocks both sides. It appears only once
//     the data outgrows a pipe buffer - so never in a small test, and always
//     on a real page.
//   - Building a command line instead of passing a list hands the arguments
//     to a shell, and a device path or a file name becomes syntax.
//
// POSIX-only: the checks run real programs (/bin/true, /bin/cat), and the
// point is to exercise the pipe behaviour rather than to mock it.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasUtils.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// Comfortably past any pipe buffer on any platform, which is the whole point:
// the interesting behaviour starts once the kernel stops absorbing the data.
std::vector<unsigned char> BigInput() {
    return std::vector<unsigned char>(64u * 1024u * 1024u, 'x');
}

void TestAChildThatNeverReads() {
    std::cout << "\n-- A child that exits without reading --\n";

    const std::vector<unsigned char> big = BigInput();
    const ProcessOutput result = RunProcessCaptured({"/bin/true"}, big);

    // If SIGPIPE were not blocked, this process would have died on the first
    // write and nothing below would run - the test would not fail, it would
    // vanish.
    Check(result.started, "the child runs");
    Check(result.exitCode == 0, "and its exit status is reported");
    std::cout << "         (wrote " << (big.size() / (1024 * 1024))
              << " MB into a closed pipe and lived)\n";
}

void TestAFilterThatReadsAndWritesAtOnce() {
    std::cout << "\n-- A filter reading and writing at the same time --\n";

    const std::vector<unsigned char> big = BigInput();
    const ProcessOutput result = RunProcessCaptured({"/bin/cat"}, big);

    Check(result.Succeeded(), "it completes rather than deadlocking");
    Check(result.standardOutput.size() == big.size(), "and every byte comes back");
}

void TestTheTwoOutputStreams() {
    std::cout << "\n-- Standard output and standard error --\n";

    const std::vector<unsigned char> text{'h', 'e', 'l', 'l', 'o', '\n'};
    const ProcessOutput result =
        RunProcessCaptured({"/bin/sh", "-c", "cat; echo diagnostic >&2"}, text);

    Check(result.Succeeded(), "a child writing to both streams completes");
    Check(result.standardOutput.size() == text.size(), "stdout carries the data");
    Check(result.standardError.find("diagnostic") != std::string::npos,
          "and stderr is kept apart from it");
}

void TestAProgramThatIsNotThere() {
    std::cout << "\n-- A program that does not exist --\n";

    const ProcessOutput result = RunProcessCaptured({"/nonexistent/program"}, {});

    // The fork succeeds and the exec fails, so the failure has to be carried
    // back from the child rather than observed directly.
    Check(!result.started, "is reported as not started");
    Check(!result.error.empty(), "with something to tell the user");
    Check(!result.Succeeded(), "and never reads as success");

    const ProcessOutput empty = RunProcessCaptured({}, {});
    Check(!empty.started && !empty.error.empty(), "and so is no program at all");
}

void TestArgumentsAreNotShellSyntax() {
    std::cout << "\n-- Arguments are arguments, not shell syntax --\n";

    // The prototype this replaced pasted a device path into a string and gave
    // it to popen(), which runs a shell. Everything in this argument would
    // have been syntax there; here it is text.
    const std::string hostile = "a b; rm -rf /tmp/nothing && $(whoami) | tee /dev/null";
    const ProcessOutput result = RunProcessCaptured({"/bin/echo", hostile}, {});

    const std::string out(result.standardOutput.begin(), result.standardOutput.end());
    Check(result.Succeeded(), "the program runs");
    Check(out.find(hostile) != std::string::npos,
          "and receives the metacharacters verbatim, unexecuted");
}

void TestTheEnvironmentAddition() {
    std::cout << "\n-- Adding to the child's environment --\n";

    const ProcessOutput result = RunProcessCaptured(
        {"/bin/sh", "-c", "printf %s \"$ULTRACANVAS_TEST_VALUE\""}, {},
        {{"ULTRACANVAS_TEST_VALUE", "carried"}});

    const std::string out(result.standardOutput.begin(), result.standardOutput.end());
    Check(result.Succeeded(), "the child runs");
    Check(out == "carried", "and sees the variable it was given");

    // The rest of the environment is inherited, not replaced.
    const ProcessOutput inherited =
        RunProcessCaptured({"/bin/sh", "-c", "printf %s \"$PATH\""}, {},
                           {{"ULTRACANVAS_TEST_VALUE", "carried"}});
    Check(!inherited.standardOutput.empty(),
          "while the environment it already had survives");
}

}  // namespace

int main() {
    std::cout << "RunProcessCaptured tests\n";
    std::cout << "========================\n";

    TestAChildThatNeverReads();
    TestAFilterThatReadsAndWritesAtOnce();
    TestTheTwoOutputStreams();
    TestAProgramThatIsNotThere();
    TestArgumentsAreNotShellSyntax();
    TestTheEnvironmentAddition();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All process-runner tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " process-runner test(s) FAILED.\n";
    return 1;
}
