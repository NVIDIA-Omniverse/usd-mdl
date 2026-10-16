# SPDX-License-Identifier: Apache-2.0

"""Exercise SDK loading from a copied runtime, independently of its build prefix."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--sdk-prefix", type=Path, required=True)
    args = parser.parse_args()
    windows = sys.platform == "win32"
    extension = ".dll" if windows else (".dylib" if sys.platform == "darwin" else ".so")
    original = (args.sdk_prefix / ("bin" if windows else "lib")).resolve()
    relocated = Path("relocated SDK/runtime").resolve()
    relocated.mkdir(parents=True)
    dependencies = relocated / "dependencies"
    dependencies.mkdir()
    sdk_libraries = {name + extension for name in ("libmdl_sdk", "nv_openimageio", "dds")}
    # Copy runtime libraries, including SDK dependencies, but no headers or
    # package metadata. The wrapper owns this test's temporary directory.
    for source in original.iterdir():
        if source.is_file() and (source.suffix == extension or ".so." in source.name):
            destination = relocated if source.name in sdk_libraries else dependencies
            shutil.copy2(source, destination / source.name)

    env = os.environ.copy()
    loader_variables = ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH")
    for name in loader_variables:
        paths = env.get(name, "").split(os.pathsep)
        env[name] = os.pathsep.join(
            path for path in paths if path and Path(path).resolve() != original
        )
    loader_variable = "PATH" if windows else (
        "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
    )
    env[loader_variable] = str(dependencies) + os.pathsep + env[loader_variable]

    # The original SDK still exists on disk. A hidden build-prefix fallback
    # would make this negative control initialize successfully and fail.
    subprocess.run(
        [str(args.executable), "owned-unavailable", "mdl"], env=env, check=True,
    )

    def run(sdk_path, use_search_path):
        child_env = env.copy()
        if use_search_path:
            child_env[loader_variable] = str(relocated) + os.pathsep + env[loader_variable]
        result = subprocess.run(
            [str(args.executable), "owned", "mdl", sdk_path, str(relocated)],
            env=child_env, capture_output=True, text=True,
        )
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode:
            raise RuntimeError(f"relocated SDK initialization failed: {sdk_path!r}")
        if "failed to load image plugin" in result.stderr.lower():
            raise RuntimeError("relocated image plugin failed to load")

    run("", use_search_path=True)
    # Only the explicit path can select this nonstandard library filename.
    # The SDK and image plugins are still adjacent; the original is not on
    # the child's library search path. Neither are the relocated image plugins:
    # finding them must use the actual SDK location, not a search-path fallback.
    renamed = relocated / ("application_mdl_sdk" + extension)
    (relocated / ("libmdl_sdk" + extension)).rename(renamed)
    run(str(renamed), use_search_path=False)

    # Make both image plugins available to the OS loader, but not beside the
    # selected SDK. Each mode runs in a fresh process to avoid loaded-library
    # reuse. Host loading is the positive control for the available fallback.
    for name in ("nv_openimageio", "dds"):
        library = name + extension
        (relocated / library).rename(dependencies / library)
    subprocess.run(
        [str(args.executable), "external-started", "mdl", str(renamed)],
        env=env, check=True,
    )
    result = subprocess.run(
        [str(args.executable), "owned-no-image-plugins", "mdl", str(renamed)],
        env=env, capture_output=True, text=True,
    )
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    result.check_returncode()
    for name in ("nv_openimageio", "dds"):
        if f"failed to load image plugin '{name}{extension}'" not in result.stderr:
            raise RuntimeError(f"missing adjacent {name} plugin was not reported")


if __name__ == "__main__":
    main()
