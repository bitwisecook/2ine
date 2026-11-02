#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Stage the native GNOME app and portable runtime; build Debian and RPM packages."""

import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "io.github.bitwisecook.Twoine.Balls"
# Host libc and graphics/desktop drivers remain owned by the distribution.
HOST_LIBRARIES = re.compile(
    r"lib(c|m|dl|pthread|rt|resolv|util|gcc_s|stdc\+\+|GL\w*|EGL|X\w*|xcb\w*|drm\w*|gbm|wayland\w*|expat)\.so|ld-linux"
)


def run(argv, **kwargs):
    return subprocess.run([str(arg) for arg in argv], check=True, **kwargs)


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def elf(path):
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def stage(build, destination):
    """Copy only declared runtime assets, close private ELF dependencies and relocate them."""
    if (destination / "usr/lib/2ine-balls").exists() or (
        destination / "usr/bin/2ine-balls"
    ).exists():
        raise ValueError(
            f"A 2ine installation already exists in the staging directory: {destination}"
        )
    if not (build / "lx_loader").is_file() or not (build / "libtwoine_ball.so").is_file():
        raise ValueError("Build the full portable runtime and shared ball engine first")
    app = destination / "usr/lib/2ine-balls"
    runtime = app / "runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    for name in ("gnome_balls.py", "balls.py"):
        copy(ROOT / "tools" / name, app / "tools" / name)
    for name in ("window.ui", APP_ID + ".svg", APP_ID + "-symbolic.svg"):
        copy(ROOT / "tools/gnome" / name, app / "tools/gnome" / name)
    for directory in ("fonts", "soundfonts"):
        shutil.copytree(build / directory, runtime / directory)
    launcher = destination / "usr/bin/2ine-balls"
    copy(ROOT / "tools/gnome/2ine-balls", launcher)
    launcher.chmod(0o755)
    copy(
        ROOT / "tools/gnome" / (APP_ID + ".desktop"),
        destination / "usr/share/applications" / (APP_ID + ".desktop"),
    )
    copy(
        ROOT / "tools/gnome" / (APP_ID + ".metainfo.xml"),
        destination / "usr/share/metainfo" / (APP_ID + ".metainfo.xml"),
    )
    copy(
        ROOT / "tools/gnome" / (APP_ID + ".mime.xml"),
        destination / "usr/share/mime/packages" / (APP_ID + ".xml"),
    )
    for suffix, size in ((".svg", "scalable"), ("-symbolic.svg", "symbolic")):
        copy(
            ROOT / "tools/gnome" / (APP_ID + suffix),
            destination / f"usr/share/icons/hicolor/{size}/apps" / (APP_ID + suffix),
        )
    licenses = destination / "usr/share/doc/2ine-balls"
    copy(ROOT / "LICENSE.txt", licenses / "copyright")
    shutil.copytree(ROOT / "docs/licenses", licenses / "licenses")
    copy(ROOT / "ball/vendor/cjson/LICENSE", licenses / "licenses/cJSON-MIT.txt")
    copy(ROOT / "ball/vendor/fatfs/LICENSE.txt", licenses / "licenses/FatFs-license.txt")
    copy(ROOT / "ball/vendor/libarchive/COPYING", licenses / "licenses/libarchive-BSD.txt")
    if (build / "2ine-build.json").is_file():
        copy(build / "2ine-build.json", runtime / "2ine-build.json")
    for directory in ("fonts", "soundfonts"):
        for source in (runtime / directory).iterdir():
            if "license" in source.name.lower():
                copy(source, licenses / "licenses" / source.name)
    queue = [build / "lx_loader", build / "2ine-ball", *sorted(build.glob("lib*.so*"))]
    copied = {}
    dependency_paths = []
    while queue:
        source = queue.pop(0)
        name = source.name
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if name in copied:
            if copied[name] != digest:
                raise ValueError(f"Conflicting runtime dependency: {name}")
            continue
        copied[name] = digest
        dependency_paths.append(source.resolve())
        copy(source, runtime / name)
        result = run(["ldd", source], capture_output=True, text=True).stdout
        for line in result.splitlines():
            if "not found" in line:
                raise ValueError(f"Unresolved dependency of {source}: {line.strip()}")
            match = re.search(r"^\s*(\S+)\s+=>\s+(/\S+)", line)
            if match and not HOST_LIBRARIES.match(match[1]):
                dependency = Path(match[2])
                # Preserve the SONAME used by the requesting binary, even when ldd follows a symlink.
                if dependency.name != match[1]:
                    raise ValueError(f"Unexpected dependency name: {line.strip()}")
                queue.append(dependency)
    for binary in runtime.iterdir():
        if binary.is_file() and elf(binary):
            run(["patchelf", "--set-rpath", "$ORIGIN", binary])
    for source in (runtime / "fonts").iterdir():
        if source.suffix.lower() in (".pfb", ".fon") or (
            source.suffix.lower() == ".ttf" and not source.name.startswith("Liberation")
        ):
            raise ValueError(f"Unexpected private font in package inputs: {source.name}")
    # Include version-bound source licenses from the configured dependency inputs.
    cache = (build / "CMakeCache.txt").read_text()
    for key in (
        "TWOINE_FREETYPE_SOURCE_DIR",
        "TWOINE_SDL3_SOURCE_DIR",
        "TWOINE_UNICORN_SOURCE_DIR",
    ):
        match = re.search(rf"^{key}:\w+=(.+)$", cache, re.M)
        if match:
            source = Path(match[1])
            for name in ("LICENSE.txt", "LICENSE.TXT", "LICENSE", "COPYING", "docs/FTL.TXT"):
                if (source / name).is_file():
                    copy(
                        source / name, licenses / "licenses" / (source.name + "-" + Path(name).name)
                    )
    for source in dependency_paths if shutil.which("dpkg-query") else []:
        result = subprocess.run(["dpkg-query", "-S", str(source)], capture_output=True, text=True)
        if result.returncode == 0:
            package = result.stdout.split(": ", 1)[0].split(":", 1)[0]
            copyright_file = Path("/usr/share/doc") / package / "copyright"
            if copyright_file.is_file():
                copy(copyright_file, licenses / "licenses" / (package + "-copyright"))
    (licenses / "runtime-inputs.json").write_text(json.dumps(copied, indent=2) + "\n")
    return destination


def libc_floor(stage_root):
    versions = []
    for binary in (stage_root / "usr/lib/2ine-balls/runtime").iterdir():
        if binary.is_file() and elf(binary):
            symbols = run(["objdump", "-T", binary], capture_output=True, text=True).stdout
            versions.extend(
                tuple(map(int, version.split(".")))
                for version in re.findall(r"GLIBC_(\d+\.\d+)", symbols)
            )
    return ".".join(map(str, max(versions)))


def deb(stage_root, output, version):
    architecture = run(
        ["dpkg", "--print-architecture"], capture_output=True, text=True
    ).stdout.strip()
    control = stage_root / "DEBIAN"
    control.mkdir()
    (control / "control").write_text(f"""Package: 2ine-balls
Version: {version}
Section: otherosfs
Priority: optional
Architecture: {architecture}
Maintainer: 2ine Contributors <noreply@github.com>
Depends: python3 (>= 3.11), python3-gi, gir1.2-gtk-4.0 (>= 4.18), gir1.2-adw-1 (>= 1.9), libc6 (>= {libc_floor(stage_root)}), libgl1, libegl1, libgles2
Homepage: https://github.com/bitwisecook/2ine
Description: Native GNOME manager for OS/2 applications
 Persistent balls of 2ine with private drives, settings and application state.
 Includes the shared ball engine and portable Unicorn runtime.
""")
    # dh_icons/desktop-file-utils compatible triggers, without modifying other packages' files.
    (control / "triggers").write_text(
        "activate-noawait /usr/share/icons/hicolor\nactivate-noawait /usr/share/applications\nactivate-noawait /usr/share/mime\n"
    )
    artifact = output / f"2ine-balls_{version}_{architecture}.deb"
    run(["dpkg-deb", "--root-owner-group", "--build", stage_root, artifact])
    shutil.rmtree(control)
    return artifact


def rpm(stage_root, output, version):
    top = output / "rpmbuild"
    for name in ("BUILD", "BUILDROOT", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (top / name).mkdir(parents=True, exist_ok=True)
    spec = top / "SPECS/2ine-balls.spec"
    # Quote source paths for the shell used by rpmbuild; package version is validated by main().

    private = "|".join(
        p.name.replace(".", "[.]")
        for p in (stage_root / "usr/lib/2ine-balls/runtime").glob("lib*.so*")
    )
    spec.write_text(f"""Name: 2ine-balls
Version: {version}
Release: 1
Summary: Native GNOME manager for OS/2 applications
License: Zlib AND GPL-2.0-only AND MIT AND OFL-1.1
URL: https://github.com/bitwisecook/2ine
Requires: python3 >= 3.11, python3-gobject, gtk4 >= 4.18, libadwaita >= 1.9, mesa-libGL, mesa-libEGL, libglvnd-gles
Recommends: mesa-dri-drivers
# Private runtime modules must not satisfy unrelated system packages.
%global __provides_exclude_from ^/usr/lib/2ine-balls/.*$
%global __requires_exclude ^({private})([(].*)?$
%global debug_package %{{nil}}
%global _build_id_links none
%description
Persistent balls of 2ine with private drives, settings and application state.
Includes the shared C ball engine and portable Unicorn runtime.
%install
mkdir -p %{{buildroot}}
cp -a {shlex.quote(str(stage_root / "usr"))} %{{buildroot}}/
%files
/usr/bin/2ine-balls
/usr/lib/2ine-balls
/usr/share/applications/{APP_ID}.desktop
/usr/share/metainfo/{APP_ID}.metainfo.xml
/usr/share/mime/packages/{APP_ID}.xml
/usr/share/icons/hicolor/scalable/apps/{APP_ID}.svg
/usr/share/icons/hicolor/symbolic/apps/{APP_ID}-symbolic.svg
%doc /usr/share/doc/2ine-balls
""")
    run(["rpmbuild", "-bb", "--define", "_tmppath /tmp", "--define", f"_topdir {top}", spec])
    built = list((top / "RPMS").glob("*/*.rpm"))
    return [Path(shutil.copy2(path, output)) for path in built]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build-gnome/packages")
    parser.add_argument("--stage", type=Path, help="stage only, e.g. for Snapcraft")
    parser.add_argument("--version", default="0.1")
    parser.add_argument("--format", choices=("deb", "rpm", "all"), default="all")
    args = parser.parse_args()
    if not re.fullmatch(r"\d+(?:\.\d+){1,2}(?:[.+~][a-zA-Z0-9.]+)?", args.version):
        parser.error("Use a numeric release version such as 0.1")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage_root = stage(
        args.build.resolve(), args.stage.resolve() if args.stage else output / "stage"
    )
    if args.stage:
        return
    if args.format in ("deb", "all"):
        print(deb(stage_root, output, args.version))
    if args.format in ("rpm", "all"):
        print(rpm(stage_root, output, args.version))


if __name__ == "__main__":
    main()
