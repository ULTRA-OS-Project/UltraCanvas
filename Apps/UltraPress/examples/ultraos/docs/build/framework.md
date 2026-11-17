---
sidebar_position: 1
---

# Building the framework

The framework builds with CMake on Linux, macOS and Windows.

:::tabs
::tab[Linux]
```bash
sudo apt install build-essential cmake libcairo2-dev libpango1.0-dev
```
::tab[macOS]
```bash
brew install cmake cairo pango
```
:::

## Configure and build

```bash
cmake -B build -G Ninja
ninja -C build
```

:::tip
A second build only compiles what changed.
:::
