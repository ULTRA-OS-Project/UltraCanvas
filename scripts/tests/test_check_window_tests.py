#!/usr/bin/env python3
"""What scripts/check_window_tests.py reports: a window test that no
virtual-display step runs, a step that runs one without
ULTRACANVAS_TEST_DISPLAY_REQUIRED, a window test whose skip never asks
DisplayTest::DisplayRequired(), and a name a step lists that nothing
registers - and nothing for the shapes build.yml uses, nor for an entry the
source exempts. Run with

    python3 -m unittest discover -s scripts/tests -v
"""
import importlib.util
import sys
import unittest
from pathlib import Path

import yaml

SCRIPT = Path(__file__).resolve().parent.parent / "check_window_tests.py"
spec = importlib.util.spec_from_file_location("check_window_tests", SCRIPT)
check = importlib.util.module_from_spec(spec)
sys.modules["check_window_tests"] = check
spec.loader.exec_module(check)

WINDOW = '''
#include "DisplayTestSupport.h"
int main() {
    if (!std::getenv("DISPLAY")) {
        if (UltraCanvas::DisplayTest::DisplayRequired()) return 1;
        return 0;
    }
}
'''
HEADLESS = 'int main() { return 0; }\n'


def steps_of(text):
    return check.display_steps("build.yml", yaml.safe_load(text))


def workflow(run, step_env="", job_env=""):
    step_env = f"        env:\n{step_env}" if step_env else ""
    job_env = f"    env:\n{job_env}" if job_env else ""
    run = "\n".join("          " + line for line in run.splitlines())
    return (f"name: Build\non: push\njobs:\n  build:\n    runs-on: ubuntu-22.04\n{job_env}"
            f"    steps:\n      - name: Window tests\n{step_env}        run: |\n{run}\n")


REQUIRED = "          ULTRACANVAS_TEST_DISPLAY_REQUIRED: 1\n"


class CMake(unittest.TestCase):
    def test_comments_are_dropped_but_not_inside_quotes(self):
        text = 'add_test(NAME A COMMAND A) # add_test(NAME B COMMAND B)\nset(X "a#b")'
        self.assertEqual(check.strip_cmake_comments(text), 'add_test(NAME A COMMAND A) \nset(X "a#b")')

    def test_a_variable_folder_is_the_cmake_files_own(self):
        tracked = {"Tests/FooTest.cpp", "Other/FooTest.cpp"}
        self.assertEqual(check.resolve_source("${CMAKE_CURRENT_SOURCE_DIR}/FooTest.cpp", "Tests", tracked),
                         "Tests/FooTest.cpp")
        self.assertEqual(check.resolve_source("${_FT_DIR}/FooTest.cpp", "Tests", tracked), "Tests/FooTest.cpp")

    def test_a_path_found_elsewhere_must_be_unique(self):
        tracked = {"SmartHome/tests/WidgetTest.cpp"}
        self.assertEqual(check.resolve_source("tests/WidgetTest.cpp", "SmartHome", tracked),
                         "SmartHome/tests/WidgetTest.cpp")
        self.assertIsNone(check.resolve_source("WidgetTest.cpp", "Elsewhere", tracked | {"B/WidgetTest.cpp"}))
        self.assertIsNone(check.resolve_source("${GENERATED}", "Tests", tracked))

    def test_each_ctest_entry_finds_its_sources(self):
        cmake = {"Tests/CMakeLists.txt": '''
add_executable(FooTest
    ${CMAKE_CURRENT_SOURCE_DIR}/FooTest.cpp
)
add_test(
    NAME FooTest COMMAND FooTest
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_executable(BarTest ${CMAKE_CURRENT_SOURCE_DIR}/BarTest.cpp)
add_test(NAME BarTest COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:BarTest>)
add_test(NAME BarTrayTest COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:BarTest> --tray)
add_test(NAME ${name}Smoke COMMAND ${name})
'''}
        tests = check.test_sources(cmake, {"Tests/FooTest.cpp", "Tests/BarTest.cpp"})
        self.assertEqual(tests, {"FooTest": ["Tests/FooTest.cpp"], "BarTest": ["Tests/BarTest.cpp"],
                                 "BarTrayTest": ["Tests/BarTest.cpp"]})


class WindowTests(unittest.TestCase):
    def test_what_counts_as_a_window_test(self):
        files = {"Tests/Window.cpp": WINDOW,
                 "Tests/X.cpp": "Display* d = XOpenDisplay(nullptr);",
                 "Tests/Headless.cpp": HEADLESS,
                 "Apps/Foo/Foo.cpp": 'getenv("DISPLAY");'}
        tests = {"Window": ["Tests/Window.cpp"], "X": ["Tests/X.cpp"], "Headless": ["Tests/Headless.cpp"],
                 "App": ["Apps/Foo/Foo.cpp"]}
        self.assertEqual(check.window_tests(tests, files.get), {"Window": "Tests/Window.cpp", "X": "Tests/X.cpp"})

    def test_wayland_display_alone_is_not_one(self):
        self.assertEqual(check.window_tests({"W": ["Tests/W.cpp"]}, {"Tests/W.cpp": 'getenv("WAYLAND_DISPLAY")'}.get),
                         {})


class Steps(unittest.TestCase):
    def test_a_regex_and_the_variable_on_the_step(self):
        (step,) = steps_of(workflow('xvfb-run -a ctest -R "^(FooTest|BarTest)$"', step_env=REQUIRED))
        self.assertEqual(step["regexes"], ["^(FooTest|BarTest)$"])
        self.assertTrue(step["required"])
        self.assertTrue(check.selects(step, "FooTest"))
        self.assertFalse(check.selects(step, "FooTestToo"))

    def test_a_bash_array_and_the_variable_on_the_job(self):
        run = 'tests=(\n  FooTest BarTest\n)\nregex="^($(IFS=\'|\'; echo "${tests[*]}"))\\$"\nxvfb-run -a ctest -R "$regex"'
        (step,) = steps_of(workflow(run, job_env="      ULTRACANVAS_TEST_DISPLAY_REQUIRED: '1'\n"))
        self.assertEqual(step["names"], ["FooTest", "BarTest"])
        self.assertEqual(step["regexes"], [])
        self.assertTrue(step["required"])

    def test_the_variable_on_the_command_line_and_set_to_0(self):
        (on,) = steps_of(workflow('ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 xvfb-run -a ctest -R FooTest'))
        self.assertTrue(on["required"])
        self.assertEqual(on["regexes"], ["FooTest"])
        (off,) = steps_of(workflow('xvfb-run -a ctest -R FooTest', step_env="          ULTRACANVAS_TEST_DISPLAY_REQUIRED: 0\n"))
        self.assertFalse(off["required"])

    def test_steps_without_xvfb_run_or_ctest_are_not_display_steps(self):
        self.assertEqual(steps_of(workflow('ctest -R FooTest')), [])
        self.assertEqual(steps_of(workflow('xvfb-run -a ./UltraCanvasDemo --audit-layout')), [])


class Check(unittest.TestCase):
    def findings(self, steps, files, windows=None, registered=None):
        windows = windows if windows is not None else {"FooTest": "Tests/FooTest.cpp"}
        registered = registered if registered is not None else set(windows)
        return [m for _, m in check.check(windows, steps, registered, files.get)]

    def test_clean(self):
        steps = steps_of(workflow('xvfb-run -a ctest -R "^FooTest$"', step_env=REQUIRED))
        self.assertEqual(self.findings(steps, {"Tests/FooTest.cpp": WINDOW}), [])

    def test_a_window_test_no_step_runs(self):
        steps = steps_of(workflow('xvfb-run -a ctest -R "^BarTest$"', step_env=REQUIRED))
        (message,) = self.findings(steps, {"Tests/FooTest.cpp": WINDOW})
        self.assertIn("FooTest needs a display, and no step runs it", message)

    def test_a_step_without_the_variable(self):
        steps = steps_of(workflow('xvfb-run -a ctest -R "^FooTest$"'))
        (message,) = self.findings(steps, {"Tests/FooTest.cpp": WINDOW})
        self.assertIn("without ULTRACANVAS_TEST_DISPLAY_REQUIRED", message)

    def test_a_skip_that_never_asks(self):
        steps = steps_of(workflow('xvfb-run -a ctest -R "^FooTest$"', step_env=REQUIRED))
        source = 'int main() { if (!std::getenv("DISPLAY")) return 0; }'
        (message,) = self.findings(steps, {"Tests/FooTest.cpp": source})
        self.assertIn("never asks DisplayTest::DisplayRequired()", message)

    def test_a_listed_name_nothing_registers(self):
        steps = steps_of(workflow('tests=( FooTest FoTest )\nxvfb-run -a ctest -R "$regex"', step_env=REQUIRED))
        (message,) = self.findings(steps, {"Tests/FooTest.cpp": WINDOW})
        self.assertIn("lists FoTest, which no add_test registers", message)

    def test_the_whole_source_exempt(self):
        source = '// window-test-exempt: reads DISPLAY only to name it in a log\n' + 'getenv("DISPLAY");'
        self.assertEqual(self.findings([], {"Tests/FooTest.cpp": source}), [])

    def test_one_entry_exempt_and_the_others_still_checked(self):
        source = WINDOW + '// window-test-exempt(FooTrayTest): D-Bus only\n'
        windows = {"FooTest": "Tests/FooTest.cpp", "FooTrayTest": "Tests/FooTest.cpp"}
        (message,) = self.findings([], {"Tests/FooTest.cpp": source}, windows)
        self.assertIn("FooTest needs a display", message)

    def test_an_exemption_needs_a_reason(self):
        source = WINDOW + '// window-test-exempt:\n'
        (message,) = self.findings([], {"Tests/FooTest.cpp": source})
        self.assertIn("no step runs it", message)


if __name__ == "__main__":
    unittest.main()
