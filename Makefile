XC_PROJ := MoltenVKPackaging.xcodeproj
XC_SCHEME := MoltenVK Package

SHELL := /bin/bash
# This local build targets the M5 host. Use `make all` for all upstream platforms.
.DEFAULT_GOAL := m5

XCODEBUILD_BIN ?= $(shell command -v xcodebuild)
XCRUN ?= xcrun
XCODEBUILD := set -o pipefail && "$(XCODEBUILD_BIN)"
M5_DEPLOYMENT_TARGET ?= 26.0
# Used to determine if xcpretty is available
XCPRETTY_PATH := $(shell command -v xcpretty 2> /dev/null)

OUTPUT_FMT_CMD =
ifdef XCPRETTY_PATH
	# Pipe output to xcpretty, while preserving full log as xcodebuild.log
	OUTPUT_FMT_CMD = | tee "xcodebuild.log" | xcpretty -c
else
	# Use xcodebuild -quiet parameter
	OUTPUT_FMT_CMD = -quiet
endif

# Collect command-line preprocessor settings (eg: MVK_HIDE_VULKAN_SYMBOLS=1).
# Build controls are not C macros. In particular, DEVELOPER_DIR may contain spaces.
MAKE_CONTROL_VARIABLES := M5_% XCODEBUILD XCODEBUILD_BIN XCRUN DEVELOPER_DIR \
	ARCHS ONLY_ACTIVE_ARCH EXCLUDED_ARCHS SDKROOT MACOSX_DEPLOYMENT_TARGET
MAKEARGS := $(strip \
  $(foreach v,$(filter-out $(MAKE_CONTROL_VARIABLES),$(.VARIABLES)),\
    $(if $(filter command\ line,$(origin $(v))),\
      $(v)=$(value $(v)) ,)))

# A recent SDK enables MoltenVK's existing Metal 4 feature guards. It does not
# implement new TensorOps kernels or enable unmerged Vulkan extensions.
.PHONY: check-m5-toolchain
check-m5-toolchain:
	@"$(XCODEBUILD_BIN)" -version >/dev/null || { \
		printf '%s\n' 'M5 build requires a working full Xcode toolchain; check developer-directory selection and setup.' >&2; \
		exit 1; \
	}
	@sdk_version="$$("$(XCRUN)" --sdk macosx --show-sdk-version)" || exit 1; \
	 sdk_major="$${sdk_version%%.*}"; \
	 case "$$sdk_major" in ''|*[!0-9]*) \
		printf 'Cannot determine macOS SDK version: %s\n' "$$sdk_version" >&2; exit 1;; esac; \
	 if [ "$$sdk_major" -lt 26 ]; then \
		printf 'M5 build requires macOS SDK 26 or newer; selected SDK is %s.\n' "$$sdk_version" >&2; exit 1; \
	 fi

.PHONY: m5
m5: check-m5-toolchain
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (macOS only)" \
		-destination "generic/platform=macOS" -configuration Release -sdk macosx \
		ARCHS=arm64 ONLY_ACTIVE_ARCH=NO EXCLUDED_ARCHS= \
		MACOSX_DEPLOYMENT_TARGET="$(M5_DEPLOYMENT_TARGET)" \
		GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

# Optional graphics SPIs; separate from M5 neural-accelerator support.
.PHONY: m5-spi
m5-spi:
	$(MAKE) m5 MVK_USE_METAL_PRIVATE_API=1

# Specify individually (not as dependencies) so the sub-targets don't run in parallel
# maccat is currently excluded from `all` because of unresolved build issues on Mac Catalyst platform.
.PHONY: all
all:
	@$(MAKE) macos
	@$(MAKE) ios
	@$(MAKE) iossim
#	@$(MAKE) maccat
	@$(MAKE) tvos
	@$(MAKE) tvossim
	@$(MAKE) visionos       # Requires Xcode 15+
	@$(MAKE) visionossim    # Requires Xcode 15+

.PHONY: all-debug
all-debug:
	@$(MAKE) macos-debug
	@$(MAKE) ios-debug
	@$(MAKE) iossim-debug
#	@$(MAKE) maccat-debug
	@$(MAKE) tvos-debug
	@$(MAKE) tvossim-debug
	@$(MAKE) visionos-debug       # Requires Xcode 15+
	@$(MAKE) visionossim-debug    # Requires Xcode 15+

.PHONY: macos
macos:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (macOS only)" -destination "generic/platform=macOS" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: macos-debug
macos-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (macOS only)" -destination "generic/platform=macOS" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: ios
ios:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (iOS only)" -destination "generic/platform=iOS" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: ios-debug
ios-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (iOS only)" -destination "generic/platform=iOS" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: iossim
iossim:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (iOS only)" -destination "generic/platform=iOS Simulator" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: iossim-debug
iossim-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (iOS only)" -destination "generic/platform=iOS Simulator" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: maccat
maccat:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (MacCat only)" -destination "generic/platform=macOS,variant=Mac Catalyst" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: maccat-debug
maccat-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (MacCat only)" -destination "generic/platform=macOS,variant=Mac Catalyst" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: tvos
tvos:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (tvOS only)" -destination "generic/platform=tvOS" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: tvos-debug
tvos-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (tvOS only)" -destination "generic/platform=tvOS" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: tvossim
tvossim:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (tvOS only)" -destination "generic/platform=tvOS Simulator" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: tvossim-debug
tvossim-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (tvOS only)" -destination "generic/platform=tvOS Simulator" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: visionos
visionos:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (visionOS only)" -destination "generic/platform=xrOS" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: visionos-debug
visionos-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (visionOS only)" -destination "generic/platform=xrOS" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: visionossim
visionossim:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (visionOS only)" -destination "generic/platform=xrOS Simulator" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: visionossim-debug
visionossim-debug:
	$(XCODEBUILD) build -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (visionOS only)" -destination "generic/platform=xrOS Simulator" -configuration "Debug" GCC_PREPROCESSOR_DEFINITIONS='$${inherited} $(MAKEARGS)' $(OUTPUT_FMT_CMD)

.PHONY: clean
clean:
	$(XCODEBUILD) clean -project "$(XC_PROJ)" -scheme "$(XC_SCHEME) (macOS only)" -destination "generic/platform=macOS" $(OUTPUT_FMT_CMD)
	rm -rf Package

# Usually requires 'sudo make install'
.PHONY: install
install:
	$ rm -f /usr/local/lib/libMoltenVK.dylib
	$ cp -p Package/Latest/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib /usr/local/lib
