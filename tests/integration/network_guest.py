#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Real LX calls exercise nested ioctl pointers, TLS handles and provider policy."""

import argparse
import os
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
from pathlib import Path

from images import FlatGuest, linear_image


def store(g, address):
    g.code.extend(b"\xa3" + struct.pack("<I", address))


def nonzero(g, status):
    g.code.extend(b"\x85\xc0\x75\x12")
    g.exit(status)


def interfaces_program():
    g = FlatGuest()
    fd = g.field(bytes(4))
    entries = g.field(bytes([0xAB]) * 512 + struct.pack("<I", 0xFEEDBEEF))
    config = g.field(struct.pack("<II", 512, entries))
    g.call(2, 16, 2, 2, 0)
    store(g, fd)
    g.call_handle(2, 8, fd, 0x6914, config, 8)
    g.result(0, 10)
    g.load(config)
    nonzero(g, 11)
    g.word(entries + 16, 2, 12)
    g.word(entries + 512, 0xFEEDBEEF, 13)
    # Toolkit 4.1+ ioctl: length/family sockaddr layout and a nested 32-bit pointer.
    g.call_handle(3, 8, fd, 0xC0086924, config)
    g.result(0, 14)
    g.word(entries + 16, 0x210, 15)
    g.call_handle(2, 8, fd, 0x6914, config, 4)
    g.result(0xFFFFFFFF, 16)
    g.word(entries + 512, 0xFEEDBEEF, 17)
    g.call_handle(2, 17, fd)
    g.result(0, 18)
    g.exit(0)
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "SO32DLL", "TCPIP32"], "NETGUEST", data=g.data
    )


def tls_program(port):
    g = FlatGuest()
    fd, method, context, session = (g.field(bytes(4)) for _ in range(4))
    address = g.field(
        struct.pack("<H", 2) + struct.pack("!H", port) + socket.inet_aton("127.0.0.1") + bytes(8)
    )
    peer = g.field(b"localhost\0")
    message = b"guest TLS over host TCP"
    data, output = g.field(message), g.field(bytes(len(message)))
    g.call(2, 16, 2, 1, 6)
    store(g, fd)
    g.call_handle(2, 3, fd, address, 16)
    g.result(0, 20)
    g.call(3, 172)  # TLSv1_client_method: upgrade is enabled by default.
    store(g, method)
    g.call_handle(3, 12, method)
    nonzero(g, 21)
    store(g, context)
    g.call_handle(3, 75, context)
    nonzero(g, 22)
    store(g, session)
    g.call_handle(3, 8, context)
    g.call_handle(3, 87, session, (fd,))
    g.result(1, 23)
    g.call_handle(3, 45, session, 55, 0, peer)
    g.result(1, 24)
    g.call_handle(3, 43, session)
    g.result(1, 25)
    g.call_handle(3, 157, session)
    g.result(0, 26)
    g.call_handle(3, 71, session)
    nonzero(g, 27)
    g.require(b"\x81\x78\x04" + struct.pack("<I", int.from_bytes(b"1.3\0", "little")), 28)
    g.call_handle(3, 108, session, data, len(message))
    g.result(len(message), 29)
    g.call_handle(3, 78, session, output, len(message))
    g.result(len(message), 30)
    g.word(output, int.from_bytes(message[:4], "little"), 31)
    g.call_handle(3, 96, session)
    g.call_handle(3, 48, session)
    g.call_handle(2, 17, fd)
    g.exit(0)
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "SO32DLL", "SSLEAY32"], "TLSGUEST", data=g.data
    )


def provider_program(expected):
    g = FlatGuest()
    g.call(2, 183)
    g.result(expected, 40)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "SSLEAY32"], "TLSCHOICE", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-network-guest-") as directory:
        work = Path(directory).resolve()
        cert, key = work / "cert.pem", work / "key.pem"
        subprocess.run(
            [
                args.openssl,
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-noenc",
                "-days",
                "1",
                "-subj",
                "/CN=localhost",
                "-addext",
                "subjectAltName=DNS:localhost",
                "-keyout",
                str(key),
                "-out",
                str(cert),
            ],
            check=True,
            capture_output=True,
            timeout=15,
        )
        config = work / "2ine.cfg"
        env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(("TWOINE_", "TRACE_", "DYLD_", "LD_"))
        }
        env.update(
            TWOINE_BACKEND="unicorn",
            TWOINE_DRIVE_C=str(work),
            TWOINE_CONFIG=str(config),
            TWOINE_LIBPATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
            SSL_CERT_FILE=str(cert),
            SSL_CERT_DIR=str(work / "empty-cas"),
        )

        def run(name, program, settings=""):
            (work / name).write_bytes(program)
            config.write_text(f"mountpoint.C={work}\n{settings}")
            p = subprocess.run(
                [str(build / "lx_loader"), str(work / name)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            assert p.returncode == 0, (
                name,
                settings,
                p.returncode,
                p.stdout.decode(errors="replace"),
                p.stderr.decode(errors="replace"),
            )

        run("NETGUEST.EXE", interfaces_program())
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        failures = []

        def serve():
            try:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_3
                context.load_cert_chain(cert, key)
                connection, _ = listener.accept()
                with context.wrap_socket(connection, server_side=True) as stream:
                    assert stream.version() == "TLSv1.3"
                    stream.sendall(stream.recv(1024))
                    stream.unwrap().close()
            except BaseException as error:
                failures.append(error)

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            run("TLSGUEST.EXE", tls_program(listener.getsockname()[1]))
        finally:
            thread.join(15)
            listener.close()
        assert not thread.is_alive() and not failures, failures
        # A real guest DLL wins when upgrades are off; default/on selects the host shim.
        table = bytes([182, 0, 1, 3]) + struct.pack("<H", 1) + b"\x01" + struct.pack("<I", 0)
        (work / "SSLEAY32.DLL").write_bytes(
            linear_image(b"\xb8\x2a\0\0\0\xc3", [], [], "SSLEAY32", library=True, entry_table=table)
        )
        run("TLSCHOICE.EXE", provider_program(42), "system.tls_upgrade=false\n")
        run("TLSCHOICE.EXE", provider_program(1))
        run("TLSCHOICE.EXE", provider_program(1), "system.tls_upgrade=true\n")
    print("LX network ioctl pointers, encrypted TLS 1.3 and upgrade provider selection passed")


if __name__ == "__main__":
    main()
