# ReXGlue (macOS Fork)

A fork of [ReXGlue](https://github.com/rexglue/rexglue-sdk) with **macOS support** and **basic Metal rendering**.

ReXGlue is a static recompilation runtime and SDK for Xbox 360 games. It converts Xbox 360 PowerPC executables into portable C++ that runs natively on modern hardware — no emulation at runtime.

Built on the foundations of [Xenia](https://github.com/xenia-project), with codegen inspired by [XenonRecomp](https://github.com/hedge-dev/XenonRecomp).

## What this fork adds

- **macOS / Apple Silicon support** (arm64-osx)
- **Metal graphics backend** with basic rendering support
- **Headless mode** for running recompiled titles without a display

## Building

Requires Clang, CMake, Ninja, and [vcpkg](https://github.com/microsoft/vcpkg).

```bash
git clone --recursive <this repo>
cd rexglue-sdk
cmake --preset macos-arm64
cmake --build out/build/macos-arm64 --config Release
```

## Usage

See the upstream [ReXGlue README](https://github.com/rexglue/rexglue-sdk) for full documentation on project setup, codegen configuration, and the TOML config format.
