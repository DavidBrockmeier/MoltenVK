# Development layout

## Entry point

Open `MoltenVK.xcworkspace` at the repository root and select the shared
**MoltenVK** scheme. It brings together the existing native Xcode projects:

| Location | Role |
|---|---|
| `MoltenVK/MoltenVK.xcodeproj` | Core static library, dynamic framework, and dylib |
| `MoltenVKShaderConverter/MoltenVKShaderConverter.xcodeproj` | Shader conversion library and tool |
| `ExternalDependencies.xcodeproj` | Native SPIRV-Cross and SPIRV-Tools targets |
| `MoltenVKPackaging.xcodeproj` | Packaged outputs and headers |
| `Development/MoltenVKProbe.xcodeproj` | Runnable native Vulkan test host |
| `Config/MoltenVK.xcconfig` | Shared macOS development settings |

The macOS shader converter depends directly on the external library targets.
It does not require their XCFrameworks to have been built in a separate command.
The source checkouts and generated SPIRV-Tools headers must still be prepared
before the first build.

## Dependencies

```sh
python3 Scripts/prepare_dependencies.py
```

The revisions are defined by the existing files in `ExternalRevisions/`. The
helper fetches missing repositories at those revisions and prepares the generated
headers. It does not force-reset modified dependency checkouts.

For package managers that have already staged the source resources:

```sh
python3 Scripts/prepare_dependencies.py --offline
```

To check preparation without changing files or using the network:

```sh
python3 Scripts/prepare_dependencies.py --check
```

`make` performs this check and builds the Release workspace. `make run` builds
Debug and runs the native probe. Preparation is explicit; an Xcode build does
not unexpectedly download or overwrite dependencies.

## Settings

`Config/MoltenVK.xcconfig` is attached to the macOS target configurations. Existing
upstream namespace, version, Debug, and Release definitions are inherited.
The default enables private Metal interfaces and targets ARM64/macOS 26.
Build products are local development outputs and require no Apple developer team.

The primary scheme follows the packaging scripts' sequential target requirement.
The probe dynamically opens the exact selected dylib and issues a bounded GPU
buffer operation. Its nonzero exit indicates a failed check; output appears in
Xcode's console.

The old local `m5` and `m5-spi` make targets remain aliases for compatibility.
They are not separate products. Use `make`, the `macos` target, or the shared
MoltenVK Xcode scheme for new work.

## Source identity

Keep the source revision when producing packages. The Homebrew formula pins a
commit of this repository and its dependency resources, so each package's source
is reproducible. User-specific Xcode state and generated build outputs are ignored.

The installed Homebrew library is independent of Xcode build outputs. Building
or running the probe does not replace an installed driver. Homebrew handles that
replacement through its formula; `make install` is an upstream legacy target
and is not needed for this development workflow.
