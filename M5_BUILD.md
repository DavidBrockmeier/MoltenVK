# Local M5 build and reviewed PR integration

Prepared and built 2026-09-18. Local branch: `codex/m5-moltenvk`.
Upstream base: `4aaf714aa1b3e78e26ecfcefa9c75e9a576c500b`.
This is a local experimental build; nothing was pushed or merged upstream and the
installed Homebrew/SVP libraries were not replaced.

## Build

From this checkout, with full Xcode and macOS SDK 26 or newer available:

```sh
./fetchDependencies --macos --keep-cache
make m5
```

Bare `make` also selects `m5`. The target explicitly sets:

- macOS SDK and macOS-only package scheme;
- Release configuration, preserving the project's optimization settings;
- `ARCHS=arm64`, `ONLY_ACTIVE_ARCH=NO`, and empty `EXCLUDED_ARCHS`;
- minimum macOS 26.0, configurable through `M5_DEPLOYMENT_TARGET`;
- existing source's SDK/runtime-controlled Metal 4 support.

No invented GPU `-mcpu` flag or forced SDK feature macro is used. Building for M5
does not add a TensorOps implementation that the source does not contain.

For example, deployment-target and diagnostic options are separate from compiler
definitions:

```sh
make m5 M5_DEPLOYMENT_TARGET=27.0 MVK_CONFIG_LOG_LEVEL=1
```

Command-line build controls are excluded from `GCC_PREPROCESSOR_DEFINITIONS`,
while supported definitions such as `MVK_CONFIG_LOG_LEVEL` still pass through.
The Xcode/SDK preflight does not itself certify license or first-run readiness;
the build can still report those toolchain errors.

The optional graphics-SPI variant is available through:

```sh
make m5-spi
# Equivalent: make m5 MVK_USE_METAL_PRIVATE_API=1
```

SPI use is independent of cooperative matrices and M5 Neural Accelerators.
The compiled and initialization-tested artifact from this run is the default
public-API build. The SPI target's argument forwarding was checked with fake
tools; that variant was not separately compiled in this run.

The existing explicit `all`, platform, debug, clean, and install targets remain.
`make install` is **not** part of this workflow: it overwrites a system library
under `/usr/local/lib`.

## Output

```text
Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
```

The packaging target also produces static/dynamic XCFramework outputs. The tested
dylib is ARM64-only and its Mach-O metadata records minimum macOS 26.0 and SDK 27.0.
Its development version reports 1.4.3; this is not a claim of an upstream 1.4.3 release.

## Merged PRs

Each was fetched by its GitHub PR ref, checked against the reviewed head, and
merged separately to preserve history and allow bisection.

| PR | Reviewed head | Purpose |
|---|---|---|
| [#2819](https://github.com/KhronosGroup/MoltenVK/pull/2819) | `f17c4e0d3728b5c7fa18ec276fe5e5fa65577f52` | Preserve compute/copy execution dependencies and fence stages |
| [#2827](https://github.com/KhronosGroup/MoltenVK/pull/2827) | `2962af7a5d3d409a78bdd22a08adb2b5dab6913a` | Refresh implicit buffer metadata after push-descriptor changes |
| [#2829](https://github.com/KhronosGroup/MoltenVK/pull/2829) | `062a23c615a0d82f4d377e72e159d7ef4052cc72` | Preserve imported Metal texture identity and correct ownership/cleanup |
| [#2822](https://github.com/KhronosGroup/MoltenVK/pull/2822) | `fd8ea9dbb1963af3642d912bad651f018434c922` | Restore argument-buffer bindings overwritten by helper commands |

The only merge conflict was overlapping changelog additions in `Docs/Whats_New.md`;
all entries were retained. Implementation changes merged without manual code
conflict resolution. The combined source review found no actionable interaction
issues. These are correctness fixes, not established RIFE speed improvements.

## Excluded from the default build

| PR | Reason |
|---|---|
| [#2753](https://github.com/KhronosGroup/MoltenVK/pull/2753) | Cooperative-matrix WIP can expose a path that the supplied ncnn shaders cannot yet translate because of specialization constants; requires further compatibility work |
| [#2799](https://github.com/KhronosGroup/MoltenVK/pull/2799) | Blanket residency declarations have unresolved design/reproducer objections |
| [#2762](https://github.com/KhronosGroup/MoltenVK/pull/2762) | Deferred lifetime tracking lacks an established valid-Vulkan reproducer and has unresolved review objections |
| [#2813](https://github.com/KhronosGroup/MoltenVK/pull/2813) | Wine-specific imported-memory copy workaround has no demonstrated native SVP need |

Already-merged upstream fixes were not reapplied. SPIRV-Cross was not arbitrarily
advanced independently of the selected MoltenVK revision; dependency pins remain
the upstream base's tested set.

## Validation and limits

- Eight Makefile checks passed using fake Xcode/SDK tools: default target,
  ARM64/Release arguments, paths with spaces, compiler-definition separation,
  deployment override, optional SPI recursion, old/malformed SDK rejection,
  unavailable-Xcode rejection, and preservation of explicit platform targets.
- Dependency XCFramework build completed.
- `make m5` completed using Xcode 27.0 / SDK 27.0.
- `file`, `lipo`, and Mach-O load commands confirmed ARM64 / macOS 26.0 / SDK 27.0.
- An initialization probe loaded the exact output dylib directly, created a Vulkan
  instance, and enumerated **Apple M5 Max**. Driver logging reported **Metal Shading
  Language 4.0**, **GPU Family Metal 4**, and **Apple GPU Family 10**.
- `VK_KHR_cooperative_matrix` was absent, as expected for the selected source.

The probe does not submit an inference workload. The four PRs' full upstream
regression/CTS reproductions, sustained playback, and RIFE performance were not
run. Build output includes existing SDK deprecation/script-phase warnings.

Review reports, exact logs, the initialization probe, and Makefile validation
evidence are retained in `../research/moltenvk-integration/`.
