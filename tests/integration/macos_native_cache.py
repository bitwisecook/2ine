# Public domain. No rights reserved. Wholly AI-authored.
"""Source-style Mach-O RPATHs must not duplicate cached PM native module state."""

import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import macos_bundle  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=args.work.resolve()))
    mirror = work / "Frozen source build"
    mirror.mkdir()
    sources = [
        build / "lx_loader",
        *sorted(build.glob("lib*.so")),
        *sorted(build.glob("lib*.dylib")),
    ]
    # Freeze the actual Mach-O closure before removing development RPATHs.
    # Source-provider dependencies such as SDL live below _deps rather than
    # beside lx_loader; a root-only glob produces an incomplete test provider.
    queue = list(sources)
    copied = {}
    links = {}
    while queue:
        source = queue.pop(0).resolve(strict=True)
        if source in copied:
            continue
        target = mirror / source.name
        if target.exists():
            assert (
                hashlib.sha256(target.read_bytes()).digest()
                == hashlib.sha256(source.read_bytes()).digest()
            ), f"Conflicting frozen dependency filename: {source.name}"
        else:
            shutil.copy2(source, target)
            target.chmod(target.stat().st_mode | stat.S_IWUSR)
        copied[source] = target
        links[source] = []
        # otool -L includes LC_ID_DYLIB as well as linked libraries. An identity
        # is not a dependency and may have no independently resolvable RPATH.
        identities = set(
            macos_bundle.run(
                ["otool", "-D", source], capture_output=True, text=True
            ).stdout.splitlines()[1:]
        )
        for name in macos_bundle.dependencies(source):
            if name in identities or macos_bundle.system_library(name):
                continue
            dependency = macos_bundle.resolve(name, source, [build])
            links[source].append((name, dependency))
            queue.append(dependency)
    for source, target in copied.items():
        # Reproduce a development build, even when testing a relocated provider.
        for name, dependency in links[source]:
            macos_bundle.run(
                ["install_name_tool", "-change", name, "@rpath/" + copied[dependency].name, target],
                capture_output=True,
            )
        if source.suffix in (".so", ".dylib"):
            macos_bundle.run(
                ["install_name_tool", "-id", "@rpath/" + target.name, target], capture_output=True
            )
        for path in macos_bundle.rpaths(target):
            macos_bundle.run(
                ["install_name_tool", "-delete_rpath", path, target], capture_output=True
            )
        macos_bundle.run(
            ["install_name_tool", "-add_rpath", str(mirror), target], capture_output=True
        )
        exception = (
            ["--entitlements", ROOT / "macos/runtime.entitlements"]
            if target.name == "lx_loader"
            else []
        )
        macos_bundle.run(
            ["codesign", "--force", "--sign", "-", *exception, target], capture_output=True
        )
    (work / "frozen-closure.json").write_text(
        json.dumps({str(source): target.name for source, target in copied.items()}, indent=2) + "\n"
    )
    capabilities = build / "runtime-capabilities.json"
    if capabilities.exists():
        shutil.copy2(capabilities, mirror / capabilities.name)
    ball = work / "Native state.2ineball"
    clean = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("TWOINE_", "DYLD_", "LD_", "TRACE_", "SDL_"))
    }

    def request(action, **fields):
        p = subprocess.run(
            [str(build / "2ine-ball")],
            input=json.dumps({"action": action, "ball": str(ball), **fields}).encode(),
            env=clean,
            capture_output=True,
            check=True,
        )
        return json.loads(p.stdout)

    request("command", arguments=["init"])
    g = FlatGuest()
    g.exit(0)
    # PMWINX's Mach-O dependencies (PMMERGE among them) load before the loader
    # resolves PMWIN's forwarders to PMMERGE.
    guest = ball / "drive_c/STATE.EXE"
    guest.write_bytes(
        linear_image(
            g.code,
            g.fixups,
            ["DOSCALLS", "PMWINX", "PMWIN", "PMGPI", "VIOCALLS"],
            "STATE",
            data=g.data,
        )
    )
    plan = request("prepare-launch", build=str(mirror), arguments=["run", "C:\\STATE.EXE"])
    (work / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    cache = Path(plan["command"][0]).parent
    assert Path(plan["environment"]["DYLD_LIBRARY_PATH"].split(os.pathsep)[0]) == cache
    env = {
        **clean,
        **plan["environment"],
        "DYLD_PRINT_LIBRARIES": "1",
        "SDL_VIDEO_DRIVER": "dummy",
        "SDL_AUDIO_DRIVER": "dummy",
    }
    p = subprocess.run(plan["command"], cwd=plan["cwd"], env=env, capture_output=True, timeout=30)
    (work / "native-loads.log").write_bytes(p.stdout + p.stderr)
    assert p.returncode == 0, (p.returncode, p.stderr.decode(errors="replace")[-2000:])
    paths = collections.defaultdict(list)
    wanted = {
        "lib2ine.dylib",
        "libpmwin.so",
        "libpmmerge.so",
        "libpmgpi.so",
        "libpmwinx.so",
        "libviocalls.so",
    }
    for line in p.stderr.decode(errors="replace").splitlines():
        m = re.match(r"^dyld\[\d+\]:.*?\s(/.*)$", line)
        if m and Path(m[1]).name in wanted:
            paths[Path(m[1]).name].append(str(Path(m[1]).resolve()))
    for name in wanted:
        assert len(paths[name]) == 1 and Path(paths[name][0]).parent == cache, (name, paths[name])
    (work / "native-inventory.json").write_text(json.dumps(dict(paths), indent=2) + "\n")
    print(
        "Source-style RPATH control: six native modules loaded exactly once, all from the ball cache"
    )


if __name__ == "__main__":
    main()
