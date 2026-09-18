#!/usr/bin/env python3
"""Stage pinned Xcode dependency sources and headers, without compiling anything.

Normal mode downloads only absent repositories. Existing Git checkouts must be
clean and already at the revision recorded in ExternalRevisions. --offline also
accepts unpacked source archives (for Homebrew resources). --check is a cheap,
read-only input-presence check, intentionally allowing local development edits.
"""

import argparse
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile


# name, upstream owner, location below External, representative build inputs
DEPENDENCIES = (
    ("cereal", "USCiLab", "cereal", ("include/cereal/cereal.hpp",)),
    ("Vulkan-Headers", "KhronosGroup", "Vulkan-Headers", ("include/vulkan/vulkan.h",)),
    ("SPIRV-Cross", "KhronosGroup", "SPIRV-Cross", ("spirv_cross.cpp", "spirv_msl.cpp", "spirv_msl.hpp")),
    ("SPIRV-Tools", "KhronosGroup", "SPIRV-Tools", ("source/spirv_optimizer_options.h", "include/spirv-tools/libspirv.hpp")),
    ("SPIRV-Headers", "KhronosGroup", "SPIRV-Tools/external/spirv-headers", ("include/spirv/unified1/spirv.hpp",)),
    ("Vulkan-Tools", "KhronosGroup", "Vulkan-Tools", ("vulkaninfo/vulkaninfo.cpp",)),
    ("Volk", "zeux", "Volk", ("volk.c", "volk.h")),
)


class PreparationError(Exception):
    pass


def git(path, *args):
    result = subprocess.run(["git", "-C", str(path), *args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise PreparationError(result.stderr.strip() or "Git failed in " + str(path))
    return result.stdout.strip()


def check_inputs(path, inputs):
    missing = [str(path / item) for item in inputs if not (path / item).is_file()]
    if missing:
        raise PreparationError("Missing dependency inputs:\n  " + "\n  ".join(missing))


def validate_existing(path, revision, inputs, offline):
    if not path.is_dir():
        raise PreparationError("Dependency path exists but is not a directory: " + str(path))
    # Checking .git directly avoids accidentally treating an archive directory
    # inside the MoltenVK checkout as part of MoltenVK's own repository.
    if (path / ".git").exists():
        if git(path, "status", "--porcelain", "--untracked-files=normal"):
            raise PreparationError("Refusing to modify dirty dependency checkout: " + str(path))
        actual = git(path, "rev-parse", "HEAD")
        if actual != revision:
            raise PreparationError("Dependency revision mismatch at {}: expected {}, found {}. "
                                   "Preserve or move that checkout before retrying.".format(path, revision, actual))
    elif not offline:
        raise PreparationError("Unversioned dependency sources at {}. Use --offline for deliberately "
                               "staged source archives; their revisions cannot be checked locally.".format(path))
    check_inputs(path, inputs)


def generated_headers(root):
    archive = root / "Templates/spirv-tools/build.zip"
    with zipfile.ZipFile(archive) as template:
        names = []
        for entry in template.infolist():
            if entry.is_dir() or entry.filename.startswith("__MACOSX/"):
                continue
            path = PurePosixPath(entry.filename)
            if path.is_absolute() or ".." in path.parts or path.parts[0] != "build":
                raise PreparationError("Unexpected path in pre-generated header archive: " + entry.filename)
            names.append(entry.filename)
    if not names:
        raise PreparationError("Pre-generated header archive is empty: " + str(archive))
    return archive, names


def prepare(root, offline=False, check=False):
    external = root / "External"
    archive, headers = generated_headers(root)
    if check:
        for _, _, relative, inputs in DEPENDENCIES:
            check_inputs(external / relative, inputs)
        check_inputs(external / "SPIRV-Tools", headers)
        print("Dependency inputs are ready. No network access or changes were made.")
        return

    # Preflight every existing checkout before cloning or staging any inputs.
    pending = []
    for name, owner, relative, inputs in DEPENDENCIES:
        revision = (root / "ExternalRevisions" / (name + "_repo_revision")).read_text().strip()
        if not re.fullmatch(r"[0-9a-fA-F]{40}", revision):
            raise PreparationError("Expected a full pinned Git revision for " + name)
        revision = revision.lower()
        path = external / relative
        source = None
        if not os.path.lexists(path) and name == "SPIRV-Headers":
            candidate = external / name
            if os.path.lexists(candidate):
                validate_existing(candidate, revision, inputs, offline)
                source = candidate
        if os.path.lexists(path):
            validate_existing(path, revision, inputs, offline)
        elif source is not None:
            pending.append((path, source, None, inputs))
        elif offline:
            raise PreparationError("Offline dependency is missing: " + str(path))
        else:
            pending.append((path, "https://github.com/{}/{}.git".format(owner, name), revision, inputs))

    for path, source, revision, inputs in pending:
        path.parent.mkdir(parents=True, exist_ok=True)
        # A failed fetch/copy leaves the intended destination absent. Temporary
        # work is confined to a new directory, never an existing user checkout.
        with tempfile.TemporaryDirectory(prefix=".prepare-", dir=path.parent) as temporary:
            staged = Path(temporary) / "source"
            if revision is None:
                print("Staging {} from {} (original preserved)".format(path.name, source))
                # Keep Git/worktree metadata at its original location. A
                # relative link also works in relocatable Homebrew source trees.
                if os.path.lexists(path):
                    raise PreparationError("Dependency appeared during preparation: " + str(path))
                path.symlink_to(os.path.relpath(source, path.parent), target_is_directory=True)
                continue
            else:
                print("Fetching {} at {}".format(path.name, revision), flush=True)
                staged.mkdir()
                git(staged, "init", "--quiet")
                git(staged, "remote", "add", "origin", source)
                git(staged, "fetch", "--quiet", "--depth=1", "origin", revision)
                git(staged, "checkout", "--quiet", "--detach", "FETCH_HEAD")
                if git(staged, "rev-parse", "HEAD") != revision:
                    raise PreparationError("Fetched revision does not match pin for " + str(path))
            check_inputs(staged, inputs)
            if os.path.lexists(path):
                raise PreparationError("Dependency appeared during preparation; refusing replacement: " + str(path))
            staged.rename(path)

    with zipfile.ZipFile(archive) as template:
        for name in headers:
            target = external / "SPIRV-Tools" / name
            if target.is_file():
                continue  # Preserve existing generated headers and built deps.
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as output:
                output.write(template.read(name))
            print("Prepared " + str(target.relative_to(root)))
    print("Dependency sources and generated headers are ready. Open MoltenVK.xcworkspace to build.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--offline", action="store_true", help="prepare pre-staged sources without network access")
    modes.add_argument("--check", action="store_true", help="check required inputs without modifying files or fetching")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    try:
        prepare(root, offline=args.offline, check=args.check)
    except (PreparationError, OSError, zipfile.BadZipFile) as error:
        print("error: {}\nRun python3 Scripts/prepare_dependencies.py from a prepared source checkout.".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
