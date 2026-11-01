# Public domain. No rights reserved. Wholly AI-authored.
"""Relocate Mach-O dependency closures, sign inside-out, and notarize an app."""

import hashlib
import json
import shutil
import struct
import subprocess
import time
from pathlib import Path

MACH_MAGICS = {b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"}


def run(argv, **kwargs):
    return subprocess.run([str(a) for a in argv], check=True, **kwargs)


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as file:
        return file.read(4) in MACH_MAGICS


def dependencies(path):
    text = run(["otool", "-L", path], capture_output=True, text=True).stdout
    return [line.strip().split(" (compatibility version", 1)[0] for line in text.splitlines()[1:]]


def rpaths(path):
    lines = run(["otool", "-l", path], capture_output=True, text=True).stdout.splitlines()
    return [
        lines[i + 2].strip().split(" ", 2)[1]
        for i, line in enumerate(lines)
        if line.strip() == "cmd LC_RPATH"
    ]


def system_library(name):
    return name.startswith(("/usr/lib/", "/System/Library/"))


def resolve(name, binary, roots):
    if name.startswith("@loader_path/"):
        return (binary.parent / name.removeprefix("@loader_path/")).resolve(strict=True)
    if name.startswith("@rpath/"):
        suffix = name.removeprefix("@rpath/")
        for root in [
            *roots,
            *[Path(p.replace("@loader_path", str(binary.parent))) for p in rpaths(binary)],
        ]:
            candidate = root / suffix
            if candidate.is_file():
                return candidate.resolve()
        raise ValueError(f"Cannot resolve {name} for {binary}")
    if name.startswith("@"):
        raise ValueError(f"Unsupported dependency {name} for {binary}")
    return Path(name).resolve(strict=True)


def bundle_runtime(build, destination, mesa, vulkan_loader):
    destination.mkdir(parents=True)
    sources = [
        build / "lx_loader",
        build / "2ine-ball",
        *sorted(build.glob("lib*.so")),
        *sorted(build.glob("lib*.dylib")),
    ]
    sources.extend(sorted((mesa / "lib").glob("*.dylib")))
    sources.append(vulkan_loader)
    copied = {}
    queue = []

    def include(source):
        source = source.resolve(strict=True)
        if source in copied:
            return copied[source]
        target = destination / source.name
        if target.exists():
            if (
                hashlib.sha256(target.read_bytes()).digest()
                != hashlib.sha256(source.read_bytes()).digest()
            ):
                raise ValueError(f"Conflicting dependency filename: {source.name}")
        else:
            shutil.copy2(source, target)
        copied[source] = target
        queue.append(source)
        return target

    for source in sources:
        include(source)
    for source in queue:
        target = copied[source]
        for name in dependencies(source):
            if system_library(name):
                continue
            dependency = resolve(name, source, [build, mesa / "lib"])
            linked = include(dependency)
            run(
                ["install_name_tool", "-change", name, "@loader_path/" + linked.name, target],
                capture_output=True,
            )
        if source.suffix in (".so", ".dylib"):
            run(["install_name_tool", "-id", "@rpath/" + target.name, target], capture_output=True)
        for path in rpaths(target):
            if path.startswith("/") and not system_library(path):
                run(["install_name_tool", "-delete_rpath", path, target], capture_output=True)
        if "@loader_path" not in rpaths(target):
            run(["install_name_tool", "-add_rpath", "@loader_path", target], capture_output=True)
    (destination / "libvulkan.1.dylib").symlink_to(copied[vulkan_loader.resolve()].name)
    for directory in ("fonts", "soundfonts"):
        shutil.copytree(build / directory, destination / directory)
    icd = destination / "vulkan/icd.d/kosmickrisp.json"
    icd.parent.mkdir(parents=True)
    original = next((mesa / "share/vulkan/icd.d").glob("*kosmickrisp*.json"))
    data = json.loads(original.read_text())
    data["ICD"]["library_path"] = "../../libvulkan_kosmickrisp.dylib"
    icd.write_text(json.dumps(data, indent=2) + "\n")
    return copied


def audit(app):
    """Fail if any binary refers to a host installation or missing relative dylib."""
    failures = []
    for binary in app.rglob("*"):
        if not macho(binary):
            continue
        for name in dependencies(binary):
            if system_library(name):
                continue
            if Path(name).name == binary.name:
                continue  # LC_ID_DYLIB is identity, not a linked dependency.
            if name.startswith("@executable_path/") and binary.parent.name == "bin":
                if not (binary.parent / name.removeprefix("@executable_path/")).is_file():
                    failures.append(f"{binary}: missing {name}")
                continue
            if name.startswith("@loader_path/"):
                if not (binary.parent / name.removeprefix("@loader_path/")).is_file():
                    failures.append(f"{binary}: missing {name}")
            elif name.startswith("@rpath/"):
                if not name.startswith("@rpath/libswift"):
                    suffix = name.removeprefix("@rpath/")
                    if binary.name != suffix and not any(
                        (Path(p.replace("@loader_path", str(binary.parent))) / suffix).is_file()
                        for p in rpaths(binary)
                    ):
                        failures.append(f"{binary}: unresolved {name}")
            else:
                failures.append(f"{binary}: external dependency {name}")
    if failures:
        raise ValueError("\n".join(failures))


def sign(app, identity, entitlements):
    timestamp = ["--timestamp"] if identity != "-" else []
    for binary in sorted(app.rglob("*")):
        if not macho(binary):
            continue
        # Only lx_loader needs the exception for Unicorn's MAP_JIT allocation.
        with binary.open("rb") as file:
            header = file.read(16)
        executable = (
            header[:4] == b"\xcf\xfa\xed\xfe" and struct.unpack_from("<I", header, 12)[0] == 2
        )
        # Ad-hoc signatures have no Team ID, so library validation cannot link
        # their separately signed helpers. Hardened runtime is for Developer ID.
        options = ["--options", "runtime"] if executable and identity != "-" else []
        exception = ["--entitlements", entitlements] if binary.name == "lx_loader" else []
        run(
            ["codesign", "--force", "--sign", identity, *timestamp, *options, *exception, binary],
            capture_output=True,
        )
    options = ["--options", "runtime"] if identity != "-" else []
    run(
        ["codesign", "--force", "--sign", identity, *timestamp, *options, app],
        capture_output=True,
    )
    run(["codesign", "--verify", "--deep", "--strict", app])


def notarize(app, profile, output):
    archive = output / "2ine-Balls-notary.zip"
    run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive])
    submitted = run(
        [
            "xcrun",
            "notarytool",
            "submit",
            archive,
            "--keychain-profile",
            profile,
            "--output-format",
            "json",
        ],
        capture_output=True,
        text=True,
    )
    submission = json.loads(submitted.stdout)
    identifier = submission["id"]
    (output / "notary-submission.json").write_text(json.dumps(submission, indent=2) + "\n")
    while True:
        info = run(
            [
                "xcrun",
                "notarytool",
                "info",
                identifier,
                "--keychain-profile",
                profile,
                "--output-format",
                "json",
            ],
            capture_output=True,
            text=True,
        )
        status = json.loads(info.stdout)
        (output / "notary-status.json").write_text(info.stdout)
        if status["status"] != "In Progress":
            break
        print(f"Notarization {identifier}: In Progress", flush=True)
        time.sleep(15)
    run(
        [
            "xcrun",
            "notarytool",
            "log",
            identifier,
            "--keychain-profile",
            profile,
            output / "notary-log.json",
        ]
    )
    if status["status"] != "Accepted":
        raise ValueError(f"Notarization {status['status']}; see {output / 'notary-log.json'}")
    run(["xcrun", "stapler", "staple", app])
    run(["xcrun", "stapler", "validate", app])
    run(["spctl", "--assess", "--type", "execute", "--verbose=2", app])
    run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, output / "2ine-Balls.zip"])
