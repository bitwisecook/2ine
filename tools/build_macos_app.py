#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Build a complete native macOS 27+ app with the shared C ball engine."""

import argparse
import json
import os
import platform
import plistlib
import shutil
import subprocess
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING or __package__:
    from tools import macos_bundle
else:
    import macos_bundle

ROOT = Path(__file__).resolve().parents[1]


def command(argv, **kwargs):
    return subprocess.run([str(a) for a in argv], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build-macos")
    parser.add_argument("--runtime-build", type=Path, required=True)
    parser.add_argument("--mesa-prefix", type=Path, required=True)
    parser.add_argument("--vulkan-loader", type=Path, required=True)
    parser.add_argument("--sign-identity", default="-")
    parser.add_argument("--notary-profile")
    parser.add_argument("--notarize", action="store_true")
    args = parser.parse_args()
    if args.notarize and (args.sign_identity == "-" or not args.notary_profile):
        parser.error(
            "notarization requires --sign-identity (Developer ID Application) and --notary-profile"
        )
    if platform.system() != "Darwin" or int(platform.mac_ver()[0].split(".")[0]) < 27:
        parser.error("macOS 27+ and Xcode 27 with Swift 6.4 are required")
    swift = command(["swift", "--version"], capture_output=True, text=True).stdout
    if "Swift version 6.4" not in swift:
        parser.error("Swift 6.4 is required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(
        os.environ,
        CLANG_MODULE_CACHE_PATH=str(output / "clang-cache"),
        SWIFTPM_MODULECACHE_OVERRIDE=str(output / "swift-cache"),
    )
    build_log = output / "swift-build.log"
    with build_log.open("w") as log:
        try:
            command(
                [
                    "swift",
                    "build",
                    "--package-path",
                    ROOT / "macos",
                    "-c",
                    "release",
                    "--scratch-path",
                    output / "swift",
                    "--cache-path",
                    output / "spm-cache",
                    "--config-path",
                    output / "spm-config",
                    "--security-path",
                    output / "spm-security",
                    "--disable-sandbox",
                ],
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
        except subprocess.CalledProcessError:
            print(build_log.read_text())
            raise
    app = output / "2ine Balls.app"
    if app.exists():
        shutil.rmtree(app)
    contents = app / "Contents"
    resources = contents / "Resources"
    (contents / "MacOS").mkdir(parents=True)
    resources.mkdir(parents=True)
    shutil.copy2(output / "swift/release/TwoineBalls", contents / "MacOS/TwoineBalls")
    copied = macos_bundle.bundle_runtime(
        args.runtime_build.resolve(),
        resources / "Runtime",
        args.mesa_prefix.resolve(),
        args.vulkan_loader.resolve(),
    )
    shutil.copy2(ROOT / "LICENSE.txt", resources / "LICENSE.txt")
    shutil.copytree(ROOT / "docs/licenses", resources / "Licenses")
    shutil.copy2(ROOT / "ball/vendor/cjson/LICENSE", resources / "Licenses/cJSON-MIT.txt")
    shutil.copy2(
        ROOT / "ball/vendor/sha256/README.md", resources / "Licenses/SHA256-public-domain.txt"
    )
    for source in copied:
        for parent in source.parents:
            if (parent / "INSTALL_RECEIPT.json").is_file():
                for pattern in ("LICENSE*", "COPYING*", "NOTICE*"):
                    for license_file in parent.glob(pattern):
                        if license_file.is_file():
                            target = resources / "Licenses" / parent.parent.name
                            target.mkdir(exist_ok=True)
                            shutil.copy2(license_file, target / license_file.name)
                break
    mesa_license = ROOT / "build-inputs/mesa-26.2.6/docs/license.rst"
    if mesa_license.is_file():
        shutil.copy2(mesa_license, resources / "Licenses/Mesa-license.rst")
    developer = Path(command(["xcode-select", "-p"], capture_output=True, text=True).stdout.strip())
    ictool = developer.parent / "Applications/Icon Composer.app/Contents/Executables/ictool"
    exports = output / "icon-renders"
    exports.mkdir(exist_ok=True)
    for rendition in ("Default", "Dark", "TintedLight", "TintedDark"):
        command(
            [
                ictool,
                ROOT / "macos/Twoine.icon",
                "--export-image",
                "--output-file",
                exports / f"{rendition}.png",
                "--platform",
                "macOS",
                "--rendition",
                rendition,
                "--width",
                "1024",
                "--height",
                "1024",
                "--scale",
                "1",
            ]
        )
    iconset = output / "Twoine.iconset"
    iconset.mkdir(exist_ok=True)
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            suffix = "@2x" if scale == 2 else ""
            command(
                [
                    "sips",
                    "-z",
                    str(size * scale),
                    str(size * scale),
                    exports / "Default.png",
                    "--out",
                    iconset / f"icon_{size}x{size}{suffix}.png",
                ],
                stdout=subprocess.DEVNULL,
            )
    command(["iconutil", "-c", "icns", iconset, "-o", resources / "Twoine.icns"])
    info = {
        "CFBundleExecutable": "TwoineBalls",
        "CFBundleIdentifier": "io.github.bitwisecook.2ine.Balls",
        "CFBundleName": "2ine Balls",
        "CFBundleDisplayName": "2ine Balls",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "1.0",
        "CFBundleVersion": "1",
        "LSMinimumSystemVersion": "27.0",
        "NSHighResolutionCapable": True,
        "CFBundleIconFile": "Twoine",
        "NSPrincipalClass": "NSApplication",
        "CFBundleDocumentTypes": [
            {
                "CFBundleTypeName": "2ine Ball",
                "CFBundleTypeRole": "Editor",
                "LSHandlerRank": "Owner",
                "LSItemContentTypes": ["io.github.bitwisecook.2ine.ball"],
            }
        ],
        "UTExportedTypeDeclarations": [
            {
                "UTTypeIdentifier": "io.github.bitwisecook.2ine.ball",
                "UTTypeDescription": "2ine Ball",
                "UTTypeConformsTo": ["com.apple.package"],
                "UTTypeTagSpecification": {"public.filename-extension": ["2ineball"]},
            }
        ],
    }
    with (contents / "Info.plist").open("wb") as file:
        plistlib.dump(info, file)
    macos_bundle.audit(app)
    macos_bundle.sign(app, args.sign_identity, ROOT / "macos/runtime.entitlements")
    if args.notarize:
        macos_bundle.notarize(app, args.notary_profile, output)
    (output / "build-info.json").write_text(
        json.dumps(
            {"swift": swift.strip(), "architecture": platform.machine()},
            indent=2,
        )
        + "\n"
    )
    print(app)


if __name__ == "__main__":
    main()
