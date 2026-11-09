#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify original IBM CMD/REXX execution and shared-service lifetime using supplied media files."""

import argparse
import fcntl
import hashlib
import json
import os
import shutil
import struct
import subprocess
import time
from pathlib import Path


def wait_for(predicate, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise RuntimeError(f"Timed out waiting for {description}")


def queue_owner(ipc):
    for path in ipc.glob("queue-*"):
        if path.name.endswith(".lock"):
            continue
        try:
            data = path.read_bytes()
        except FileNotFoundError:
            continue
        if len(data) >= 20 and struct.unpack_from("<I", data)[0] == 0x324D424C:
            return struct.unpack_from("<I", data, 4)[0], path
    return None


def queue_live(path):
    try:
        with path.with_name(path.name + ".lock").open("rb") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
    except FileNotFoundError:
        pass
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cmd", type=Path, required=True)
    parser.add_argument("--dlls", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--backend", choices=("unicorn", "native"), required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        parser.error("--work must name an empty directory")
    system = work / "OS2"
    dlls = system / "DLL"
    dlls.mkdir(parents=True)
    ipc = work / "ipc"
    ipc.mkdir()
    shutil.copyfile(args.cmd, system / "CMD.EXE")
    for source in args.dlls.iterdir():
        if source.suffix.upper() in (".DLL", ".MSG"):
            shutil.copyfile(source, dlls / source.name.upper())
    for name in ("REXX.DLL", "REXXAPI.DLL", "REXXINIT.DLL", "REXXCRT.DLL", "REXXUTIL.DLL"):
        if not (dlls / name).is_file():
            parser.error(f"Original {name} is missing from --dlls")
    config = work / "2ine.cfg"
    config.write_text(f"mountpoint.C = {work}\n")
    (work / "HOLD.CMD").write_bytes(
        b"/* REXX */\r\nsay 'ORIGINAL-REXX-READY'\r\ncall charin\r\n"
        b"say 'ORIGINAL-REXX-DONE'\r\nexit 0\r\n"
    )
    (work / "SAY.CMD").write_bytes(
        b"/* REXX */\r\nsay 'ORIGINAL-REXX-WORKS'\r\n"
        b"address CMD '@ECHO ORIGINAL-CMD-WORKS'\r\nif rc <> 0 then exit 31\r\n"
        b"call RxFuncAdd 'SysLoadFuncs', 'RexxUtil', 'SysLoadFuncs'\r\n"
        b"if result <> 0 then exit 32\r\ncall SysLoadFuncs\r\n"
        b"do profile = 1 to 2\r\n"
        b"  if profile = 1 then ini = 'USER'\r\n  else ini = 'C:\\fixture.ini'\r\n"
        b"  value = 'roundtrip' || '00'x\r\n"
        b"  result = SysIni(ini, '2ine Original Regression', 'Fixture', value)\r\n"
        b"  if result <> '' then exit 33\r\n"
        b"  if SysIni(ini, '2ine Original Regression', 'Fixture') <> value then exit 34\r\n"
        b"end\r\nsay 'ORIGINAL-REXX-APIS-WORK'\r\nexit 0\r\n"
    )
    environment = dict(os.environ)
    for key in tuple(environment):
        if key.startswith("TWOINE_TRACE") or key in (
            "TRACE_NATIVE",
            "IS_2INE",
            "TWOINE_CHILD_ARGUMENTS",
        ):
            environment.pop(key)
    environment.update(
        TWOINE_LIBPATH=str(args.build.resolve()),
        TWOINE_CONFIG=str(config),
        TWOINE_CURRENT_DRIVE="C",
        TWOINE_BACKEND=args.backend,
        TWOINE_BALL=str(work),
        TWOINE_IPC_DIR=str(ipc),
        OS2PATH="C:\\OS2",
        COMSPEC="C:\\OS2\\CMD.EXE",
        LIBPATH="C:\\OS2\\DLL;C:\\OS2",
    )
    command = [str(args.build.resolve() / "lx_loader"), str(system / "CMD.EXE"), "/C", "CALL"]
    first_out, first_err = work / "holder.stdout", work / "holder.stderr"
    first = None
    try:
        with first_out.open("wb") as stdout, first_err.open("wb") as stderr:
            first = subprocess.Popen(
                [*command, "C:\\HOLD.CMD"],
                cwd=work,
                env=environment,
                stdin=subprocess.PIPE,
                stdout=stdout,
                stderr=stderr,
            )
            wait_for(
                lambda: b"ORIGINAL-REXX-READY" in first_out.read_bytes(),
                15,
                "original REXX input wait",
            )
            service = queue_owner(ipc)
            if not service or service[0] == first.pid or not queue_live(service[1]):
                raise RuntimeError("REXX initialization did not run in a separate live worker")
            with (
                (work / "say.stdout").open("wb") as output,
                (work / "say.stderr").open("wb") as errors,
            ):
                second = subprocess.run(
                    [*command, "C:\\SAY.CMD"],
                    cwd=work,
                    env=environment,
                    stdout=output,
                    stderr=errors,
                    timeout=15,
                )
            markers = (b"ORIGINAL-REXX-WORKS", b"ORIGINAL-CMD-WORKS", b"ORIGINAL-REXX-APIS-WORK")
            if second.returncode or not all(
                marker in (work / "say.stdout").read_bytes() for marker in markers
            ):
                raise RuntimeError(f"Original REXX SAY failed with status {second.returncode}")
            if queue_owner(ipc) != service:
                raise RuntimeError("Concurrent REXX clients did not reuse their service worker")
            time.sleep(1.3)  # Longer than the service's one-second idle grace.
            if first.poll() is not None or not queue_live(service[1]):
                raise RuntimeError(
                    "The service retired while another original REXX client was live"
                )
            first.communicate(b"x", timeout=15)
            if first.returncode or b"ORIGINAL-REXX-DONE" not in first_out.read_bytes():
                raise RuntimeError(
                    f"Original REXX input continuation failed with status {first.returncode}"
                )
            wait_for(
                lambda: not queue_live(service[1]), 5, "service retirement after its last client"
            )
    finally:
        if first and first.poll() is None:
            first.kill()
            first.wait(timeout=5)
    manifest = {
        str(path.relative_to(work)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (system / "CMD.EXE", *sorted(dlls.iterdir()))
    }
    (work / "inputs.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(
        "Original IBM REXX SAY, CMD callbacks, REXXUTIL/profile roundtrips, "
        "concurrent service reuse and last-client retirement passed"
    )


if __name__ == "__main__":
    main()
