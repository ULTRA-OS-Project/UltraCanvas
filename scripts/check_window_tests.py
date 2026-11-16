#!/usr/bin/env python3
"""Every window test runs on a display in CI, and fails there rather than
skip.

A window test opens a real window and reads it back, so it skips itself
without a display - and a skip passes. The Linux row's plain `ctest` has no
display: a window test that no virtual-display step selects passes there by
checking nothing. `FileDialogTest` failed on every display from 0.9.541
until 2026-11-15 that way, with 33 more window tests in the same position.
A window test is a test source - a file in a `Tests/` or `tests/` folder,
built into an executable that `add_test` registers - that reads the
`DISPLAY` variable or opens the X display itself (`XOpenDisplay`, as
DesktopShellTest does). For each, three things must hold:

1. A step of a workflow under .github/workflows that runs ctest under
   `xvfb-run` selects its ctest entry: by an `-R "<regex>"` written in the
   step, or by a name in a bash array (`tests=( ... )`) that the step builds
   its regex from.
2. That step sets `ULTRACANVAS_TEST_DISPLAY_REQUIRED` (in the step's, the
   job's or the workflow's `env`, or on the command line), to anything but
   empty or `0`: there a skip fails, so a broken display cannot pass.
3. The source asks `DisplayTest::DisplayRequired()` (Tests/DisplayRequired.h)
   - its skip honours that variable.

And a name listed in a step's array is one that `add_test` registers, so a
renamed test is caught before the step runs.

A source that reads `DISPLAY` and is no window test, or one that cannot run
on Xvfb, says so with `// window-test-exempt: <why>`. One ctest entry of
several built from a source - a mode of the binary that never opens a
window, as DesktopShellTest's `--tray` - says so with
`// window-test-exempt(<ctest name>): <why>`; the source's other entries
are still checked.

    python3 scripts/check_window_tests.py

Lists each finding and exits 1 when there are any. The analysis (the
functions below main) is pure and tested in
scripts/tests/test_check_window_tests.py.
"""
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

try:
    import yaml
except ImportError:  # pragma: no cover - the one dependency, named plainly
    sys.exit("check_window_tests: needs PyYAML (pip install pyyaml) to read the workflows")

ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"
VARIABLE = "ULTRACANVAS_TEST_DISPLAY_REQUIRED"
EXEMPT = "window-test-exempt"

_READS_DISPLAY = re.compile(r'\b(?:(?:std::)?(?:getenv|secure_getenv|GetEnvUtf8)\s*\(\s*"DISPLAY"\s*\)'
                            r'|XOpenDisplay\s*\()')
_ASKS_REQUIRED = re.compile(r"\bDisplayRequired\s*\(")
_EXEMPTION = re.compile(r"//\s*" + re.escape(EXEMPT) + r"(?:\(\s*([A-Za-z0-9_.+-]+)\s*\))?:\s*(\S.*)")
_SOURCE = re.compile(r"\.(?:c|cc|cpp|cxx|mm)$")
_ADD_EXECUTABLE = re.compile(r"\badd_executable\s*\(\s*([A-Za-z0-9_.+-]+)\b([^)]*)\)", re.S)
_ADD_TEST = re.compile(r"\badd_test\s*\(\s*NAME\s+([A-Za-z0-9_.+-]+)\s+COMMAND\s+([^)]*)\)", re.S)
_TARGET_FILE = re.compile(r"\$<TARGET_FILE:([A-Za-z0-9_.+-]+)>")
_SHELL_EXPANSION = re.compile(r"\$[A-Za-z_{(]")
_ARRAY = re.compile(r"\b[A-Za-z_]\w*=\(\s*([^)]*)\)")
_REGEX_ARG = re.compile(r"""(?:^|\s)(?:-R|--tests-regex)\s+(?:"([^"]*)"|'([^']*)'|([^\s"'$]\S*))""")


def strip_cmake_comments(text):
    """The CMake text without its # comments (a # inside quotes is kept)."""
    out = []
    for line in text.splitlines():
        quoted = False
        for i, ch in enumerate(line):
            if ch == '"':
                quoted = not quoted
            elif ch == "#" and not quoted:
                line = line[:i]
                break
        out.append(line)
    return "\n".join(out)


def resolve_source(token, cmake_dir, tracked):
    """The tracked file an add_executable source token names, or None.
    `${VAR}/` prefixes are dropped (every test folder's variables point at
    the folder the CMake file is in), then the rest is looked up next to the
    CMake file and, failing that, as the unique tracked path it ends."""
    rest = re.sub(r"^(?:\$\{[^}]+\}/)+", "", token)
    if "$" in rest or not _SOURCE.search(rest):
        return None
    near = str(PurePosixPath(cmake_dir) / rest)
    if near in tracked:
        return near
    ends = [p for p in tracked if p == rest or p.endswith("/" + rest)]
    return ends[0] if len(ends) == 1 else None


def test_sources(cmake_files, tracked):
    """{ctest name: [source paths]} for every add_test whose command runs an
    executable built here. cmake_files is {path: text}; tracked the set of
    tracked file paths."""
    executables = {}
    registrations = []
    for path, text in cmake_files.items():
        text = strip_cmake_comments(text)
        cmake_dir = str(PurePosixPath(path).parent)
        for m in _ADD_EXECUTABLE.finditer(text):
            sources = [resolve_source(t, cmake_dir, tracked) for t in m.group(2).split()]
            executables.setdefault(m.group(1), []).extend(s for s in sources if s)
        registrations.extend(_ADD_TEST.findall(text))
    tests = {}
    for name, command in registrations:
        if "$" in name:
            continue
        words = command.split()
        targets = _TARGET_FILE.findall(command) + [w for w in words if w in executables]
        for target in targets:
            tests.setdefault(name, []).extend(executables.get(target, []))
    return tests


def is_test_source(path):
    return any(part in ("Tests", "tests") for part in PurePosixPath(path).parts[:-1])


def window_tests(tests, read):
    """{ctest name: source} for every test built from a test source that
    reads DISPLAY or opens the X display. read(path) returns a file's text."""
    found = {}
    for name, sources in sorted(tests.items()):
        for source in sources:
            if is_test_source(source) and _READS_DISPLAY.search(read(source)):
                found[name] = source
                break
    return found


def _required(env):
    value = (env or {}).get(VARIABLE)
    return value is not None and str(value).strip() not in ("", "0")


def display_steps(workflow_path, workflow):
    """Each step that runs ctest under xvfb-run, as a dict: file, job, name,
    regexes (the -R patterns written in it), names (from its bash arrays) and
    required (whether it sets the variable)."""
    steps = []
    if not isinstance(workflow, dict):
        return steps
    top_env = workflow.get("env") or {}
    for job_id, job in (workflow.get("jobs") or {}).items():
        if not isinstance(job, dict):
            continue
        job_env = job.get("env") or {}
        for step in job.get("steps") or []:
            run = step.get("run") if isinstance(step, dict) else None
            if not isinstance(run, str) or "xvfb-run" not in run or "ctest" not in run:
                continue
            # A pattern the shell expands ("$regex") is the array's, read below.
            regexes = [p for p in (next(g for g in m.groups() if g is not None) for m in _REGEX_ARG.finditer(run))
                       if not _SHELL_EXPANSION.search(p)]
            names = [w for m in _ARRAY.finditer(run) for w in m.group(1).split()]
            inline = re.search(r"\b" + VARIABLE + r"=(\S+)", run)
            required = (_required(step.get("env")) or _required(job_env) or _required(top_env)
                        or bool(inline and inline.group(1).strip("'\"") not in ("", "0")))
            steps.append({"file": workflow_path, "job": job_id, "name": step.get("name", "(unnamed)"),
                          "regexes": regexes, "names": names, "required": required})
    return steps


def selects(step, test):
    if test in step["names"]:
        return True
    for pattern in step["regexes"]:
        try:
            if re.search(pattern, test):
                return True
        except re.error:
            continue
    return False


def exempt(test, text):
    """Whether the source's text exempts this ctest entry: a
    window-test-exempt comment with a reason, for the whole source or naming
    this entry."""
    return any(m.group(1) in (None, test) for m in _EXEMPTION.finditer(text))


def check(windows, steps, registered, read):
    """The findings, as (where, message) pairs. windows is {ctest name:
    source}; steps from display_steps; registered every ctest name add_test
    registers; read(path) a file's text."""
    findings = []
    for test, source in sorted(windows.items()):
        text = read(source)
        if exempt(test, text):
            continue
        chosen = [s for s in steps if selects(s, test)]
        if not chosen:
            findings.append((source, f"{test} needs a display, and no step runs it under xvfb-run: add it to "
                                     f"\"Run the framework window tests on a virtual display (Linux)\" in "
                                     f".github/workflows/build.yml (or say why not: // {EXEMPT}({test}): <why>)"))
        for step in chosen:
            if not step["required"]:
                findings.append((step["file"], f"step \"{step['name']}\" runs {test} under xvfb-run without "
                                               f"{VARIABLE}: a skip there passes"))
        if not _ASKS_REQUIRED.search(text):
            findings.append((source, f"{test} needs a display, and its skip never asks "
                                     f"DisplayTest::DisplayRequired() (Tests/DisplayRequired.h): under "
                                     f"{VARIABLE} it has to fail, not skip"))
    for step in steps:
        for name in step["names"]:
            if name not in registered:
                findings.append((step["file"], f"step \"{step['name']}\" lists {name}, which no add_test registers"))
    return findings


def main():
    tracked = set(subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True,
                                 check=True).stdout.splitlines())
    cmake_files = {p: (ROOT / p).read_text(errors="replace") for p in sorted(tracked)
                   if (p.endswith("CMakeLists.txt") or p.endswith(".cmake"))
                   and "third_party/" not in p and "3rdparty/" not in p}

    def read(path):
        return (ROOT / path).read_text(errors="replace")

    tests = test_sources(cmake_files, tracked)
    windows = window_tests(tests, read)
    steps = []
    for path in sorted(WORKFLOWS.glob("*.yml")):
        with open(path) as f:
            steps += display_steps(str(path.relative_to(ROOT)), yaml.safe_load(f))
    findings = check(windows, steps, set(tests), read)
    for where, message in findings:
        print(f"{where}: {message}")
    if findings:
        print(f"check_window_tests: {len(findings)} finding(s) - see the rule at the top of "
              f"scripts/check_window_tests.py")
        return 1
    exempted = sum(1 for test, source in windows.items() if exempt(test, read(source)))
    print(f"check_window_tests: clean ({len(windows) - exempted} window tests, {exempted} exempt, "
          f"{len(steps)} virtual-display steps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
