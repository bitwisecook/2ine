# Public domain. No rights reserved. Wholly AI-authored.
"""Golden contract of the JSON tb_request facade: every action and command.

One scripted session runs every action and ball command, including failures,
against balls holding folder, image, bundled and missing references. The
normalized responses, error messages and resulting ball files must match
fixtures/ball_contract.json. Set TWOINE_UPDATE_BALL_CONTRACT=1 to rewrite it
after an intended change.
"""

import base64
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools import ball as ball_api

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from disk_images import fat_image, iso_image

GOLDEN = Path(__file__).resolve().parent / "fixtures/ball_contract.json"

# Values that depend on the clock, the host or the build rather than on the
# request: the key is kept and its value replaced.
VOLATILE_KEYS = {
    "modified",
    "mtime",
    "recorded",
    "started",
    "finished",
    "exportedAt",
    "compiledAt",
    "compiler",
    "sourceVersion",
    "sourceRevision",
    "runtime_id",
    "identitySHA256",
    "loaderSHA256",
    "TWOINE_RUNTIME_ID",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",
    "buildInformation",
    "fingerprint",
    "mediaFingerprint",
    "generated",
}
TIMESTAMP = re.compile(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?(?:Z|[+-]\d\d:\d\d)?")
# Report directories are named after the Unix time they were written.
REPORT_TIME = re.compile(r"(dependency-audit-)\d+-\d+")
RUNTIME_DIRECTORY = re.compile(r"(OS2/DLL/2INE/[a-z]+/)[0-9a-f]{16}")
OPERATION_ID = re.compile(r"\b[0-9a-f]{32}\b")
# Virtual roots name their host source in hexadecimal.
VIRTUAL_ROOT = re.compile(r"/@2ine-(device|media|image)/([0-9a-f]+)")
LX_PROGRAM = b"MZ" + bytes(58) + struct.pack("<I", 64) + b"LX" + bytes(34)
PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
)


def profile(entries):
    """A 2ine profile (2INEPRF1) holding application/key/data entries."""
    out = bytearray(b"2INEPRF1" + struct.pack("<I", len(entries)))
    for application, key, data in entries:
        a, k = application.encode() + b"\0", key.encode() + b"\0"
        out += struct.pack("<III", len(a), len(k), len(data)) + a + k + data
    return bytes(out)


def sound_bank():
    body = b"sfbk" + b"LIST" + struct.pack("<I", 4) + b"INFO"
    return b"RIFF" + struct.pack("<I", len(body)) + body


# Top-level manifest groups a version 1 engine reads. Newer engines add keys
# that version 1 engines ignore, but never change the meaning of these.
VERSION_1_GROUPS = {
    "version",
    "drives",
    "currentDrive",
    "workingDirectories",
    "environment",
    "system",
    "runtime",
    "applications",
    "wpsHidden",
    "startupPrograms",
    "audio",
    "soundBanks",
}


def version_1_problems(manifest):
    """Reasons a version 1 engine (the version 1 manifest loader) would reject this manifest."""
    problems = []
    if manifest.get("version") != 1:
        problems.append("version is not 1")
    drives = manifest.get("drives")
    if not isinstance(drives, dict) or "C" not in drives:
        problems.append("missing private C: drive")
        return problems
    for letter, entry in drives.items():
        if len(letter) != 1 or not "A" <= letter <= "Z":
            problems.append("invalid drive letter " + letter)
        elif isinstance(entry, str):
            continue
        elif not isinstance(entry, dict) or not (
            isinstance(entry.get("image"), str) or isinstance(entry.get("device"), str)
        ):
            problems.append(f"drive {letter} is neither a folder nor a device")
        elif entry.get("device") not in (None, "cdrom", "floppy", "disk"):
            problems.append(f"drive {letter} has an unknown device")
    return problems


class ContractSession:
    def __init__(self, root):
        self.root = root
        self.build = root / "runtime"
        self.records = []
        self.replacements = []
        self.version_1_problems = []

    def normalize(self, value, key=None):
        if key in VOLATILE_KEYS:
            return "<volatile>"
        if isinstance(value, dict):
            return {k: self.normalize(v, k) for k, v in value.items()}
        if isinstance(value, list):
            return [self.normalize(v) for v in value]
        if isinstance(value, str):
            value = VIRTUAL_ROOT.sub(
                lambda m: "/@2ine-{}/{{{}}}".format(
                    m.group(1), bytes.fromhex(m.group(2)).decode("utf-8", "replace")
                ),
                value,
            )
            for old, new in self.replacements:
                value = value.replace(old, new)
            value = TIMESTAMP.sub("<time>", value)
            value = REPORT_TIME.sub(r"\1<time>", value)
            value = RUNTIME_DIRECTORY.sub(r"\1<runtime>", value)
            return OPERATION_ID.sub("<id>", value)
        return value

    def request(self, label, message):
        try:
            result = {"result": ball_api.native_request(message)}
        except ValueError as error:
            result = {"error": str(error)}
        self.records.append({"step": label, **self.normalize(result)})
        return result.get("result")

    def files(self, label, ball):
        """Record the manifest and the derived files the runtime reads."""
        state = {"manifest": json.loads((ball / "ball.json").read_text())}
        self.version_1_problems += [
            label + ": " + problem for problem in version_1_problems(state["manifest"])
        ]
        config = ball / "2ine.cfg"
        if config.is_file():
            state["2ine.cfg"] = config.read_text().splitlines()
        devices = ball / "devices"
        if devices.is_dir():
            for control in sorted(devices.iterdir()):
                data = control.read_bytes()
                state["devices/" + control.name] = {
                    "magic": data[:8].decode("latin-1"),
                    "kind": data[8],
                    "readonly": data[9],
                    "path": data[16 : 16 + struct.unpack_from("<I", data, 12)[0]].decode(),
                }
        state["tree"] = sorted(
            path.relative_to(ball).as_posix()
            for path in ball.rglob("*")
            if "OS2/DLL/2INE" not in path.as_posix() and "/cache/" not in "/" + path.as_posix()
        )
        self.records.append({"step": label, **self.normalize(state)})


class BallContractTests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-contract-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        for name in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "TWOINE_BACKEND"):
            previous = os.environ.get(name)
            self.addCleanup(
                lambda n=name, p=previous: (
                    os.environ.pop(n, None) if p is None else os.environ.__setitem__(n, p)
                )
            )
        os.environ["XDG_DATA_HOME"] = str(self.root / "data")
        os.environ["XDG_CONFIG_HOME"] = str(self.root / "config")
        os.environ.pop("TWOINE_BACKEND", None)
        try:
            ball_api.native_request({"action": "defaults"})
        except ValueError as error:
            self.skipTest(str(error))

    def session(self):
        s = ContractSession(self.root)
        s.replacements = [(str(self.root), "<ROOT>")]
        host = os.environ.get("LD_LIBRARY_PATH")
        if host:
            s.replacements.insert(0, (host, "<host>"))
        return s

    def make_runtime(self, build):
        build.mkdir()
        loader = build / "lx_loader"
        shutil.copy2(sys.executable, loader)
        loader.chmod(0o755)
        (build / "libdoscalls.so").write_bytes(b"\x7fELF placeholder")
        (build / "runtime-capabilities.json").write_text(
            json.dumps(
                {
                    "schema": 1,
                    "operationSupervisor": True,
                    "backends": ["native", "unicorn"],
                    "defaultBackend": "native",
                    "defaultOpenGL": "mesa",
                    "features": {"mesa": False, "video": True, "pcm": True, "midi": True},
                }
            )
        )

    def test_every_action_matches_the_golden_contract(self):
        s = self.session()
        root, build = self.root, s.build
        self.make_runtime(build)
        media = root / "media"
        media.mkdir()
        iso = media / "install.iso"
        iso.write_bytes(iso_image(payload=LX_PROGRAM, filename="SETUP.EXE"))
        floppy_source = media / "disk.img"
        floppy_source.write_bytes(fat_image())
        folder = root / "shared"
        (folder / "sub").mkdir(parents=True)
        (folder / "readme.txt").write_text("hello")
        icon = root / "icon.png"
        icon.write_bytes(PNG)
        bank = root / "piano.sf2"
        bank.write_bytes(sound_bank())
        ball = root / "data/2ine/ball/Contract.2ineball"
        b = str(ball)

        def run(label, action, **fields):
            return s.request(label, {"action": action, **fields})

        def command(label, *arguments, **fields):
            return run(label, "command", ball=b, arguments=list(arguments), **fields)

        # Requests that need no ball.
        run("missing action", None)
        run("unknown action", "no-such-action", ball=b)
        run("effective-runtime", "effective-runtime", path=str(build))
        run("effective-runtime none", "effective-runtime", path=str(root))
        run("effective-backend", "effective-backend", build=str(build), path="native")
        run("effective-backend missing", "effective-backend")
        run("format-arguments", "format-arguments", arguments=["a b", 'say "hi"', ""])
        run("parse-arguments", "parse-arguments", text='one \'two three\' "x""y" ')
        run("parse-arguments open quote", "parse-arguments", text='"open')
        run("image-preview", "image-preview", path=str(icon))
        run("image-preview missing", "image-preview")
        project = run("project-sources", "project-sources")
        # The catalogue is generated from the build host's packages; keep its shape.
        s.records[-1]["result"] = {
            key: sorted(sorted(entry) for entry in value) if isinstance(value, list) else value
            for key, value in (project or {}).items()
        }
        run("defaults", "defaults")
        run("unset-environment", "unset-environment")
        run("runtime-files", "runtime-files", build=str(build))
        run("sync-runtime", "sync-runtime", build=str(build), directory=str(root / "snapshot"))
        run("discover empty", "discover")

        # A new ball and its private drive.
        command("init", "init")
        run("discover", "discover")
        command("init again", "init")
        command("map C refused", "map", "C", str(folder))
        command("unknown command", "frobnicate")
        command("map folder", "map", "E", str(folder))
        command("map missing folder", "map", "F", str(root / "missing"))
        command("map-image cdrom", "map-image", "D", str(iso))
        command("create-floppy", "create-floppy", "A", str(media / "new.img"), "1440")
        command("create-floppy bad size", "create-floppy", "B", str(media / "bad.img"), "1000")
        command("create-disk", "create-disk", "H", str(media / "hpfs.img"), "hpfs", "8")
        command("create-disk bad", "create-disk", "I", str(media / "x.img"), "fat32", "8")
        command("device", "device", "F", "cdrom")
        command("device again", "device", "F", "floppy")
        command("map-image floppy into cdrom", "map-image", "F", str(floppy_source))
        command("device-lock no image", "device-lock", "F", "on")
        command("device-lock", "device-lock", "D", "on")
        command("eject locked", "eject", "D")
        command("device-lock off", "device-lock", "D", "off")
        command("media-readonly", "media-readonly", "A", "on")
        command("media-readonly cdrom off", "media-readonly", "D", "off")
        command("eject", "eject", "F")
        command("restore-drive", "restore-drive", "G", json.dumps("relative-folder"))
        command("restore-drive bad", "restore-drive", "G", "{}")
        command("unmap", "unmap", "G")
        command("config env", "config", "env.GREETING", "hello %LANG%")
        command("config system", "config", "system.display_dpi", "120")
        command("config system bad", "config", "system.display_dpi", "12")
        command("config runtime", "config", "runtime.backend", "unicorn", build=str(build))
        command("config unknown group", "config", "other.x", "1")
        command("config reserved", "config", "env.TWOINE_BALL", "x")
        command("unset", "unset", "env.GREETING")
        (ball / "drive_c/APP").mkdir()
        (ball / "drive_c/APP/APP.EXE").write_bytes(LX_PROGRAM)
        (ball / "drive_c/APP/RUN.CMD").write_bytes(b"@echo off\r\n")
        command("add-app", "add-app", "--cwd", "C:\\APP", "App", "C:\\APP\\APP.EXE", "-x")
        command("add-app missing", "add-app", "Nope", "C:\\NOPE.EXE")
        record = json.dumps({"program": "C:\\APP\\APP.EXE", "arguments": []})
        command("restore-app", "restore-app", "Restored", record)
        command("restore-app exists", "restore-app", "Restored", record)
        command("remove-app", "remove-app", "Restored")
        s.files("files after commands", ball)

        # Settings, files and applications.
        run("snapshot", "snapshot", ball=b)
        run("write-config", "write-config", ball=b)
        config = run("config-sys", "config-sys", ball=b)
        run(
            "save-config-sys stale",
            "save-config-sys",
            ball=b,
            expected="stale",
            text="SET X=1\r\n",
        )
        run(
            "save-config-sys",
            "save-config-sys",
            ball=b,
            expected=config,
            text=(config or "") + "SET EXTRA=é\r\n",
        )
        (ball / "drive_c/OS2/OS2.INI").write_bytes(
            profile([("PM_Colors", "Background", b"0 0 0\0"), ("App", "Key", b"\x01\x02")])
        )
        run("configuration-files", "configuration-files", ball=b)
        run("configuration-file-read", "configuration-file-read", ball=b, path="C:\\CONFIG.SYS")
        ini = run(
            "configuration-read",
            "configuration-read",
            ball=b,
            name="file",
            file="C:\\OS2\\OS2.INI",
        )
        run(
            "configuration-write",
            "configuration-write",
            ball=b,
            name="file",
            file="C:\\OS2\\OS2.INI",
            expected=(ini or {}).get("revision"),
            path="App",
            key="Key",
            text="0a0b0c",
        )
        run(
            "configuration-write stale",
            "configuration-write",
            ball=b,
            name="file",
            file="C:\\OS2\\OS2.INI",
            expected="0" * 64,
            path="App",
            key="Key",
            remove=True,
        )
        run("configuration-read user", "configuration-read", ball=b, name="user")
        read = run(
            "configuration-file-read ini",
            "configuration-file-read",
            ball=b,
            path="C:\\OS2\\OS2.INI",
        )
        if read:
            run(
                "configuration-file-write",
                "configuration-file-write",
                ball=b,
                path="C:\\CONFIG.SYS",
                expected=(
                    run(
                        "configuration-file-read again",
                        "configuration-file-read",
                        ball=b,
                        path="C:\\CONFIG.SYS",
                    )
                    or {}
                ).get("revision"),
                text="SET ONLY=1\r\n",
            )
        run("browse", "browse", ball=b)
        run("browse folder", "browse", ball=b, path="E:\\")
        run("browse image", "browse", ball=b, path="D:\\")
        run("browse unmapped", "browse", ball=b, path="Q:\\")
        run("import-files", "import-files", ball=b, path="C:\\TEMP", sources=[str(folder)])
        run("import-files conflict", "import-files", ball=b, path="C:\\TEMP", sources=[str(folder)])
        run(
            "import-files read-only image", "import-files", ball=b, path="D:\\", sources=[str(icon)]
        )
        run("guest-environment", "guest-environment", ball=b, environment={"EXTRA2": "%PATH%;X"})
        run(
            "save-application",
            "save-application",
            ball=b,
            name="Saved",
            program="C:\\APP\\APP.EXE",
            arguments=["--flag"],
            cwd="C:\\APP",
            iconSource=str(icon),
            environment={"MODE": "1"},
            workingDirectories={"E": "E:\\sub"},
        )
        run(
            "save-application rename",
            "save-application",
            ball=b,
            name="Renamed",
            originalName="Saved",
            program="C:\\APP\\APP.EXE",
            arguments=[],
        )
        run("save-application invalid", "save-application", ball=b, name="", arguments=[])
        run("application-icon custom", "application-icon", ball=b, name="Renamed")
        run("application-icon program", "application-icon", ball=b, program="C:\\APP\\APP.EXE")
        run("set-working-directory", "set-working-directory", ball=b, path="E:\\sub")
        run("set-working-directory bad", "set-working-directory", ball=b, path="E:")
        run("runtime-build", "runtime-build", ball=b, build=str(build))
        run("runtime-build invalid", "runtime-build", ball=b, build=str(root))
        run("restore-runtime", "restore-runtime", ball=b, build=str(build))
        run("host-path", "host-path", ball=b, path="E:\\readme.txt")
        run("host-path outside", "host-path", ball=b, path="Q:\\x")
        run("guest-path", "guest-path", ball=b, path=str(folder / "readme.txt"))
        run("environment", "environment", ball=b, build=str(build))
        run("audio-snapshot", "audio-snapshot", ball=b)
        run("import-soundfont", "import-soundfont", ball=b, path=str(bank))
        audio = run("audio-snapshot again", "audio-snapshot", ball=b)
        if audio:
            settings = dict(audio["settings"])
            settings["profile"] = "gs"
            run("save-audio", "save-audio", ball=b, settings=settings)
        run("save-audio invalid", "save-audio", ball=b, settings={"profile": "nope"})

        # Installation manager and launch planning.
        run("os2-support", "os2-support", ball=b)
        run("remember-os2-media", "remember-os2-media", ball=b, sources=[str(iso)])
        run("browse-media", "browse-media", ball=b, path=str(iso))
        run("browse-media member", "browse-media", ball=b, path=str(iso) + "/SETUP.EXE")
        run("open-os2-source", "open-os2-source", ball=b, sources=[str(iso)], build=str(build))
        run("validate-os2-media", "validate-os2-media", ball=b, sources=[str(iso)])
        run(
            "plan-os2-extraction",
            "plan-os2-extraction",
            ball=b,
            sources=[str(iso)],
            build=str(build),
        )
        run(
            "import-os2-support",
            "import-os2-support",
            ball=b,
            sources=[str(iso)],
            build=str(build),
        )
        run("os2-component-state", "os2-component-state", ball=b)
        run("remove-os2-components", "remove-os2-components", ball=b, paths=[])
        run(
            "cancel-os2-component-operation",
            "cancel-os2-component-operation",
            ball=b,
            operationID="0" * 32,
        )
        run("runtime-features", "runtime-features", ball=b, build=str(build))
        run(
            "launch-prerequisites",
            "launch-prerequisites",
            ball=b,
            build=str(build),
            arguments=["run", "C:\\APP\\APP.EXE"],
        )
        run("ball-navigation-state", "ball-navigation-state", ball=b, build=str(build))
        launch = {"build": str(build), "arguments": ["launch", "App"]}
        run("ensure-launch-dependencies", "ensure-launch-dependencies", ball=b, **launch)
        run("launch-dependency-status", "launch-dependency-status", ball=b, **launch)
        plan = run("prepare-launch", "prepare-launch", ball=b, **launch)
        run(
            "prepare-launch run",
            "prepare-launch",
            ball=b,
            build=str(build),
            arguments=["run", "--cwd", "E:\\", "C:\\APP\\APP.EXE", "a"],
        )
        run(
            "prepare-launch command file",
            "prepare-launch",
            ball=b,
            build=str(build),
            arguments=["run", "C:\\APP\\RUN.CMD"],
        )
        run("prepare-launch bad", "prepare-launch", ball=b, build=str(build), arguments=["x"])
        run("ensure-dependencies", "ensure-dependencies", ball=b, build=str(build))
        run("audit-dependencies", "audit-dependencies", ball=b, build=str(build), resolve=False)
        run(
            "record-install",
            "record-install",
            ball=b,
            build=str(build),
            receipt=(plan or {}).get("receipt", {}),
            exitCode=0,
            programHost=str(ball / "drive_c/APP/APP.EXE"),
        )
        run("record-install invalid", "record-install", ball=b)
        s.files("files after launch", ball)

        # References to images, folders and media outside the ball.
        run("reference-status", "reference-status", ball=b)
        run("reference-status one", "reference-status", ball=b, id="drive-D", verify=True)
        run("reference-status unknown", "reference-status", ball=b, id="drive-Q")
        run("search-references", "search-references", ball=b, searchRoots=[str(media)])
        run("search-references without roots", "search-references", ball=b)
        run(
            "set-reference-host-data",
            "set-reference-host-data",
            ball=b,
            id="drive-E",
            host="contract-host",
            data={"bookmark": "AAAA"},
        )
        run("relink-reference unknown", "relink-reference", ball=b, id="drive-Q", path=str(iso))
        run(
            "relink-reference media",
            "relink-reference",
            ball=b,
            id="media-1",
            path=str(iso),
            hash=True,
        )
        run("bundle-reference folder", "bundle-reference", ball=b, id="drive-E")
        run("unbundle-reference external", "unbundle-reference", ball=b, id="drive-D", path="x")
        run("reference-remaps", "reference-remaps")
        run(
            "reference-remaps add",
            "reference-remaps",
            **{"from": str(root / "old-media"), "to": str(media)},
        )
        run(
            "reference-remaps remove",
            "reference-remaps",
            **{"from": str(root / "old-media"), "remove": True},
        )
        run("reference-remaps relative", "reference-remaps", **{"from": "x", "to": "y"})

        # Packages: export, import and rename.
        package = root / "Contract.zip"
        run("export-ball", "export-ball", ball=b, path=str(package), build=str(build))
        if package.is_file():
            with zipfile.ZipFile(package) as archive:
                names = sorted(archive.namelist())
                exported = json.loads(archive.read("ball.json"))
            s.records.append(
                {"step": "export contents", **s.normalize({"names": names, "ball": exported})}
            )
        imported = root / "data/2ine/ball/Imported.2ineball"
        run(
            "import-ball",
            "import-ball",
            path=str(package),
            ball=str(imported),
            build=str(build),
        )
        bundled = root / "Bundled.zip"
        run(
            "export-ball images",
            "export-ball",
            ball=b,
            path=str(bundled),
            includeImages=True,
            build=str(build),
        )
        with_images = root / "data/2ine/ball/WithImages.2ineball"
        run(
            "import-ball images",
            "import-ball",
            path=str(bundled),
            ball=str(with_images),
            build=str(build),
        )
        if with_images.is_dir():
            s.files("files after bundled import", with_images)
        if imported.is_dir():
            s.files("files after import", imported)
        iso.rename(media / "moved.iso")
        run("snapshot with missing image", "snapshot", ball=b)
        run("host-path missing image", "host-path", ball=b, path="D:\\SETUP.EXE")
        iso_moved = media / "moved.iso"
        iso_moved.rename(iso)
        run("rename-ball", "rename-ball", ball=b, text="Renamed Contract")
        run("rename-ball exists", "rename-ball", ball=str(imported), text="Renamed Contract")
        renamed = root / "data/2ine/ball/Renamed Contract.2ineball"
        if renamed.is_dir():
            s.files("files after rename", renamed)

        self.assertEqual([], s.version_1_problems)
        actual = json.dumps(s.records, indent=1, ensure_ascii=False, sort_keys=True) + "\n"
        if os.environ.get("TWOINE_UPDATE_BALL_CONTRACT") == "1":
            GOLDEN.write_text(actual)
        expected = GOLDEN.read_text()
        if actual != expected:
            expected_records = {r["step"]: r for r in json.loads(expected)}
            for record in s.records:
                old = expected_records.get(record["step"])
                self.assertEqual(
                    json.dumps(old, indent=1, ensure_ascii=False, sort_keys=True),
                    json.dumps(record, indent=1, ensure_ascii=False, sort_keys=True),
                    "contract step differs: " + record["step"],
                )
            self.assertEqual(expected, actual)


if __name__ == "__main__":
    unittest.main()
