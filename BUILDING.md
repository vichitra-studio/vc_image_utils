# Building vc_image_utils

## Prerequisites

### Building

| Tool | Min version | macOS (Homebrew) | Ubuntu / Debian | Windows (winget) | Notes |
|------|-------------|------------------|-----------------|------------------|-------|
| C++17 compiler | clang ≥ 7 / gcc ≥ 8 / MSVC ≥ 19.14 | `xcode-select --install` | `sudo apt install clang` | Visual Studio 2019+ Build Tools¹ | |
| CMake | 3.20 | `brew install cmake` | `sudo apt install cmake` | `winget install Kitware.CMake` | |
| Ninja | any | `brew install ninja` | `sudo apt install ninja-build` | `winget install Ninja-build.Ninja` | required (enforced by presets)² |
| ccache | any | `brew install ccache` | `sudo apt install ccache` | `winget install ccache.ccache` | optional, faster rebuilds |

### Contributing (pre-commit hooks)

These are only needed if you intend to commit code. The `addlicense` hook uses
`language: golang`, which requires Go on the system PATH — pre-commit does **not**
install Go automatically.

| Tool | Min version | macOS (Homebrew) | Ubuntu / Debian | Windows | Notes |
|------|-------------|------------------|-----------------|---------|-------|
| Go | 1.21 | `brew install go` | `sudo apt install golang-go` | `winget install GoLang.Go` | required for `addlicense` pre-commit hook³ |
| pre-commit | 3.0 | `brew install pre-commit` | `pip install pre-commit` | `pip install pre-commit` | runs all hooks on commit |

After installing both, register the hooks once:

```bash
pre-commit install
```

¹ Install via [Visual Studio Installer](https://visualstudio.microsoft.com/downloads/) →
  **Build Tools for Visual Studio** → select the **Desktop development with C++** workload.
  Alternatively, install [LLVM for Windows](https://releases.llvm.org/) to use clang-cl.

² Ninja is **required on all platforms** — the base CMake preset enforces it so that
  `compile_commands.json` is always produced (the Visual Studio generator ignores
  `CMAKE_EXPORT_COMPILE_COMMANDS`). Install it before running `cmake --preset`.

³ Go is also used in `tools/go.mod` to track the `addlicense` version for Dependabot.
  `tools/go.sum` is committed; no extra steps on a fresh clone. Re-run `go mod tidy`
  inside `tools/` only after Dependabot bumps the version in `go.mod`.

## Build (command line)

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

For a release build:

```bash
cmake --preset release
cmake --build --preset release
```

## Build (VS Code)

1. Install the workspace-recommended extensions when VS Code prompts you, or run
   **Extensions: Show Recommended Extensions** from the command palette.
2. Open the command palette (`Cmd+Shift+P` on macOS / `Ctrl+Shift+P` on Linux & Windows) → **CMake: Configure**,
   select the **debug** preset.
3. Click **Build** in the status bar or run **CMake: Build**.

> **Note:** clangd reads `compile_commands.json` from `build/debug/` (configured via `.clangd`).
> Run the debug build at least once before expecting accurate autocomplete and error checking.

## Local overrides (CMakeUserPresets.json)

`CMakeUserPresets.json` is gitignored — use it for machine-local settings such as
a custom compiler path or toolchain file. Example:

```json
{
  "version": 2,
  "configurePresets": [
    {
      "name": "my-local",
      "inherits": "debug",
      "cacheVariables": {
        "CMAKE_CXX_COMPILER": "/usr/local/bin/clang++"
      }
    }
  ]
}
```
