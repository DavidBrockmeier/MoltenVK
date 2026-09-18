# MoltenVK

Personal experimental MoltenVK development build for Apple Silicon, with private
Metal APIs enabled by default and selected upstream fixes integrated.

## Open in Xcode

Prepare the pinned source dependencies once:

```sh
python3 Scripts/prepare_dependencies.py
```

Open **`MoltenVK.xcworkspace`**, select the **MoltenVK** scheme and **My Mac**.

- **Build (⌘B):** builds the native dependency libraries, shader converter,
  MoltenVK library, package, and probe in the correct order.
- **Run (⌘R):** builds Debug and starts `MoltenVKProbe`, loading the library from
  this build. It reports the GPU and driver, fills a small GPU buffer, and checks
  the result on the CPU.
- **Profile:** uses Release with the same native probe executable.

Use the workspace rather than opening one of the internal projects on its own.
The existing projects remain separate so their source organization and upstream
history stay usable, but their macOS dependencies are connected in Xcode's build
graph. There is one shared development scheme.

## Command line

```sh
make prepare   # one-time dependency checkout / header preparation
make           # Release build through the same workspace and scheme
make run       # Debug build, then execute the probe
```

The equivalent direct build is:

```sh
xcodebuild -workspace MoltenVK.xcworkspace -scheme MoltenVK \
  -configuration Release -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath build/DerivedData build
```

Requirements: Apple Silicon, macOS 26 or newer, and full Xcode 26 or newer.
Shared macOS settings live in **`Config/MoltenVK.xcconfig`**: ARM64, macOS 26,
local unsigned development output, and `MVK_USE_METAL_PRIVATE_API=1`.

Outputs:

```text
build/DerivedData/Build/Products/Release/libMoltenVK.dylib
build/DerivedData/Build/Products/Release/MoltenVKProbe
Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
```

`MoltenVKProbe` uses an explicit library argument, `MOLTENVK_PROBE_LIBRARY`, or
the dylib beside its executable. It does not silently select Homebrew's installed
driver. For example:

```sh
build/DerivedData/Build/Products/Release/MoltenVKProbe \
  "$PWD/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
```

## Homebrew

The same source is packaged as **`molten-vk`** in
[DavidBrockmeier/homebrew-tap](https://github.com/DavidBrockmeier/homebrew-tap).
The package intentionally replaces the Homebrew-core formula under the normal
name and paths. The formula pins source and dependency revisions and builds this
workspace in Release configuration.

```sh
brew reinstall --force --build-from-source DavidBrockmeier/tap/molten-vk
```

## Integrated changes

Starting from upstream commit `4aaf714aa1b3e78e26ecfcefa9c75e9a576c500b`, this
branch includes:

- [#2819](https://github.com/KhronosGroup/MoltenVK/pull/2819): compute/copy execution dependencies.
- [#2827](https://github.com/KhronosGroup/MoltenVK/pull/2827): push-descriptor buffer metadata.
- [#2829](https://github.com/KhronosGroup/MoltenVK/pull/2829): imported Metal texture ownership.
- [#2822](https://github.com/KhronosGroup/MoltenVK/pull/2822): argument-buffer restoration.

The unfinished cooperative-matrix PR is not enabled. The private-API setting
enables existing graphics SPI implementations; it does not create TensorOps
convolution kernels. The probe checks real driver execution, not RIFE throughput.

The original cross-platform documentation is preserved in
[README.upstream.md](README.upstream.md). [Development details](Docs/Development.md)
describe the project layout and how to refresh dependencies.
