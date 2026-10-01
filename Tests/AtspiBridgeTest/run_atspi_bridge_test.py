#!/usr/bin/env python3
"""End-to-end test of the AT-SPI bridge (OS/Linux/UltraCanvasLinuxAccessibility).

On a private D-Bus session it starts the AT-SPI bus launcher (the
accessibility bus and its registry, as a desktop does), turns accessibility on
the way GNOME does when a screen reader starts, runs AtspiBridgeTestApp and
then AtspiBridgeTestClient, a libatspi client that reads the application as
Orca would. The test passes when the client's checks pass.

    run_atspi_bridge_test.py <AtspiBridgeTestApp> <AtspiBridgeTestClient>

Exits 77 (skipped) without a display, dbus-run-session or at-spi2-core.
"""

import os
import shutil
import subprocess
import sys
import time

SKIP = 77
LAUNCHER_PATHS = ["/usr/libexec/at-spi-bus-launcher", "/usr/lib/at-spi2-core/at-spi-bus-launcher",
                  "/usr/lib/at-spi-bus-launcher"]


def gdbus(*args):
    return subprocess.run(["gdbus", "call", "--session", *args], capture_output=True, text=True)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    app_path, client_path = sys.argv[1], sys.argv[2]
    launcher = next((p for p in LAUNCHER_PATHS if os.path.exists(p)), None)
    if not os.environ.get("DISPLAY"):
        print("SKIP: no display")
        return SKIP
    if not launcher or not shutil.which("gdbus"):
        print("SKIP: at-spi2-core (at-spi-bus-launcher) or gdbus is not installed")
        return SKIP

    # Run inside a private session bus, never the user's own.
    if os.environ.get("UC_ATSPI_TEST_PRIVATE_BUS") != "1":
        runner = shutil.which("dbus-run-session")
        if not runner:
            print("SKIP: dbus-run-session is not installed")
            return SKIP
        env = dict(os.environ, UC_ATSPI_TEST_PRIVATE_BUS="1")
        return subprocess.run([runner, "--", sys.executable, os.path.abspath(__file__), app_path, client_path],
                              env=env).returncode

    env = dict(os.environ)
    env.pop("NO_AT_BRIDGE", None)
    bus = subprocess.Popen([launcher, "--launch-immediately"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            if gdbus("--dest", "org.a11y.Bus", "--object-path", "/org/a11y/bus",
                     "--method", "org.a11y.Bus.GetAddress").returncode == 0:
                break
            time.sleep(0.1)
        else:
            print("SKIP: the accessibility bus did not start")
            return SKIP
        gdbus("--dest", "org.a11y.Bus", "--object-path", "/org/a11y/bus",
              "--method", "org.freedesktop.DBus.Properties.Set", "org.a11y.Status", "IsEnabled", "<true>")

        app = subprocess.Popen([app_path], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            client = subprocess.run([client_path], env=env, capture_output=True, text=True, timeout=60)
            print(client.stdout)
            if client.returncode != 0:
                print(client.stderr[-4000:])
        finally:
            app.kill()
            log = app.communicate()[0]
        if client.returncode != 0:
            print("--- application log ---")
            print("\n".join(line for line in log.splitlines()
                            if "ccessib" in line or "AT-SPI" in line or "APP:" in line))
        return client.returncode
    finally:
        bus.terminate()


if __name__ == "__main__":
    sys.exit(main())
