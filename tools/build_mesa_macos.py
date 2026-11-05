#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Build a supplied Mesa source tree with EGL, Zink and KosmicKrisp on macOS."""

import argparse
import json
import os
import platform
import subprocess
from pathlib import Path


def commands(source, build, prefix, jobs, vulkan_loader=None):
    plan = [
        [
            "meson",
            "setup",
            str(build),
            str(source),
            "--buildtype=release",
            f"--prefix={prefix}",
            "--libdir=lib",
            "--prefer-static",
            "-Dplatforms=macos",
            "-Dgallium-drivers=zink",
            "-Dvulkan-drivers=kosmickrisp",
            "-Dglx=disabled",
            "-Degl=enabled",
            "-Dopengl=true",
            "-Dgles1=disabled",
            "-Dgles2=disabled",
            "-Dglvnd=disabled",
            "-Dzstd=disabled",
        ],
        ["meson", "compile", "-C", str(build), "-j", str(jobs)],
        ["meson", "install", "-C", str(build)],
    ]
    if vulkan_loader:
        # Zink's dlopen originates in libgallium, so an application's rpath
        # cannot supply this dependency. Record it on Mesa's own libraries.
        rpath = f"-Wl,-rpath,{Path(vulkan_loader).parent}"
        plan[0].extend([f"-Dc_link_args={rpath}", f"-Dcpp_link_args={rpath}"])
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--vulkan-loader", type=Path, help="Vulkan loader dylib (otherwise found with pkg-config)"
    )
    args = parser.parse_args()
    source, build, prefix = args.source.resolve(), args.build.resolve(), args.prefix.resolve()
    if args.jobs < 1 or build == source or prefix == source or prefix == build:
        parser.error("use positive jobs and separate source, build and install directories")
    loader = args.vulkan_loader.resolve() if args.vulkan_loader else None
    plan = commands(source, build, prefix, args.jobs, loader)
    if args.dry_run:
        print(json.dumps(plan, indent=2))
        return
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("KosmicKrisp requires an Apple Silicon macOS host")
    if not loader:
        result = subprocess.run(
            ["pkg-config", "--variable=libdir", "vulkan"], capture_output=True, text=True
        )
        if result.returncode or not result.stdout.strip():
            parser.error(
                "install vulkan-loader or specify --vulkan-loader /path/to/libvulkan.1.dylib"
            )
        loader = Path(result.stdout.strip()) / "libvulkan.1.dylib"
    if not loader.is_file():
        parser.error(f"Vulkan loader does not exist: {loader}")
    plan = commands(source, build, prefix, args.jobs, loader)
    version = platform.mac_ver()[0]
    if not version or int(version.split(".")[0]) < 26:
        parser.error("KosmicKrisp requires macOS 26 or newer")
    for required in (
        "meson.build",
        "src/gallium/drivers/zink/meson.build",
        "src/kosmickrisp/meson.build",
    ):
        if not (source / required).is_file():
            parser.error(f"Mesa source tree is missing {required}")
    # A fresh build avoids silently retaining a software or MoltenVK provider.
    if build.exists():
        parser.error("--build must be a new directory")
    revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"], capture_output=True, text=True, check=True
    ).stdout.strip()
    dirty = subprocess.run(
        ["git", "-C", str(source), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    if dirty:
        parser.error("use a clean Mesa checkout so its revision identifies the build input")
    build.parent.mkdir(parents=True, exist_ok=True)
    for command in plan:
        subprocess.run(command, check=True)
    manifests = sorted((prefix / "share/vulkan/icd.d").glob("*kosmickrisp*.json"))
    if len(manifests) != 1 or not (prefix / "lib/libEGL.dylib").is_file():
        raise RuntimeError("Mesa installation is missing EGL or a unique KosmicKrisp ICD")
    record = {
        "mesa_revision": revision,
        "macos": version,
        "architecture": platform.machine(),
        "commands": plan,
        "icd": str(manifests[0]),
        "vulkan_loader": str(loader),
    }
    (build / "twoine-mesa-build.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"TWOINE_MESA_ROOT={prefix}\nTWOINE_KOSMICKRISP_ICD={manifests[0]}")


if __name__ == "__main__":
    main()
