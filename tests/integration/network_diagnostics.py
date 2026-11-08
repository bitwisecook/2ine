#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Guest resolver ABI, managed configuration and optional unchanged IBM network tools."""

import argparse
import hashlib
import json
import os
import re
import shutil
import socket
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


def resolver_program():
    g = FlatGuest()
    localhost = g.field(b"localhost\0")
    loopback = g.field(socket.inet_aton("127.0.0.1"))
    icmp, udp = g.field(b"icmp\0"), g.field(b"udp\0")
    domain = g.field(b"domain\0")
    error_pointer = g.field(bytes(4))
    file_name = g.field(b"C:\\MPTN\\ETC\\resolv2.\0")
    opened, action = g.field(bytes(4)), g.field(bytes(4))
    g.call(1, 273, file_name, opened, action, 0, 0, 1, 0x40, 0)
    g.result(0, 13)
    g.call_handle(1, 257, opened)
    g.result(0, 14)
    query = g.field(struct.pack("<6i", 4, 39, 0, 2, 3, 0))
    required = g.field(bytes(4))
    g.call(2, 207, query, 6, 0, required, 0, 0)
    g.result(0, 15)
    g.load(required)
    nonzero(g, 16)
    g.call(2, 139)
    g.result(0, 10)
    g.code.extend(b"\xb8" + bytes(4))
    g.fixups.append(struct.pack("<BBhBH", 7, 1, len(g.code) - 4, 2, 143))
    g.require(b"\x81\x38" + struct.pack("<I", 5), 11)
    g.require(b"\x81\x78\x08" + struct.pack("<I", 0x2C1), 12)
    for module, forward, reverse, proto, service, error in [
        (2, 111, 112, 121, 124, 151),
        (3, 11, 12, 21, 24, 51),
    ]:
        g.call(module, forward, localhost)
        nonzero(g, 20)
        g.require(b"\x83\x78\x08\x02", 21)
        g.require(b"\x83\x78\x0c\x04", 22)
        g.code.extend(b"\x8b\x40\x10\x8b\x00")
        g.require(b"\x81\x38" + socket.inet_aton("127.0.0.1"), 23)
        g.call(module, reverse, loopback, 4, 2)
        nonzero(g, 24)
        g.call(module, error)
        nonzero(g, 25)
        store(g, error_pointer)
        g.require(b"\x83\x38\x00", 26)
        g.call(module, reverse, loopback, 4, 3)
        g.result(0, 31)
        g.load(error_pointer)
        g.require(b"\x83\x38\x03", 32)
        g.call(module, forward, localhost)
        nonzero(g, 33)
        g.load(error_pointer)
        g.require(b"\x83\x38\x00", 34)
        g.call(module, proto, icmp)
        nonzero(g, 27)
        g.require(b"\x83\x78\x08\x01", 28)
        g.call(module, service, domain, udp)
        nonzero(g, 29)
        g.require(b"\x81\x78\x08" + struct.pack("<I", socket.htons(53)), 30)
    g.exit(0)
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "TCPIP32", "TCP32DLL"], "RESOLVER", data=g.data
    )


def socket_program(port):
    g = FlatGuest()
    receiver, idle = g.field(bytes(4)), g.field(bytes(4))
    target = g.field(
        b"\x10\x02" + struct.pack("!H", port) + socket.inet_aton("127.0.0.1") + bytes(8)
    )
    buffer = g.field(bytes(256))
    message = g.field(b"guest datagram")
    descriptors = g.field(struct.pack("<HH", 2, 0) + bytes(64 * 4))
    timeout = g.field(struct.pack("<ii", 2, 0))
    length = g.field(struct.pack("<I", 16))
    source = g.field(bytes(16))
    for fd in [receiver, idle]:
        g.call(2, 16, 2, 2, 17)
        store(g, fd)
    g.call_handle(2, 14, receiver, message, 14, 0, target, 16)
    g.result(14, 40)
    g.load(idle)
    store(g, descriptors + 4)
    g.load(receiver)
    store(g, descriptors + 8)
    g.call(2, 32, 0, descriptors, 0, 0, timeout)
    g.result(1, 41)
    g.word(descriptors, 1, 42)
    g.load(receiver)
    g.require(b"\x3b\x05" + struct.pack("<I", descriptors + 4), 43)
    g.call_handle(2, 11, receiver, buffer, 32, 0, source, length)
    g.result(14, 44)
    g.word(buffer, int.from_bytes(b"gues", "little"), 45)
    g.word(length, 16, 46)
    g.word(source, 0x210 | socket.htons(port) << 16, 47)
    # Both fd_set and the older descriptor-array API must remove non-ready sockets.
    g.call(2, 32, 0, descriptors, 0, 0, g.field(bytes(8)))
    g.result(0, 48)
    g.word(descriptors, 0, 49)
    g.call(2, 12, receiver, 1, 0, 0, 0)
    g.result(0, 50)
    g.word(receiver, 0xFFFFFFFF, 51)
    g.load(descriptors + 4)
    store(g, receiver)
    g.call_handle(2, 17, receiver)
    g.result(0, 52)
    g.call_handle(2, 17, idle)
    g.result(0, 53)
    g.call_handle(2, 13, receiver, 0xFFFFFFFF, 4, 0)
    g.result(0xFFFFFFFF, 54)
    g.call(2, 20)
    g.result(10014, 55)
    g.call(2, 144, buffer, 256)
    g.result(0, 56)
    g.call(2, 20)
    g.result(10014, 57)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "TCPIP32"], "SOCKETS", data=g.data)


def dns_name(value):
    return b"".join(bytes([len(part)]) + part.encode("ascii") for part in value.split(".")) + b"\0"


class DNSFixture:
    def __init__(self):
        self.events, self.failures, self.stop = [], [], threading.Event()
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", 0))
        self.udp.settimeout(0.1)
        self.port = self.udp.getsockname()[1]
        self.tcp = socket.socket()
        self.tcp.bind(("127.0.0.1", self.port))
        self.tcp.listen(4)
        self.tcp.settimeout(0.1)
        self.threads = [
            threading.Thread(target=self.serve_udp),
            threading.Thread(target=self.serve_tcp),
        ]
        for thread in self.threads:
            thread.start()

    def answer(self, data, transport):
        p, parts = 12, []
        while data[p]:
            n = data[p]
            parts.append(data[p + 1 : p + 1 + n].decode("ascii"))
            p += n + 1
        p += 1
        kind, cls = struct.unpack_from("!HH", data, p)
        p += 4
        name = ".".join(parts).lower()
        self.events.append((transport, name, kind))
        flags, payload = 0x8180, b""
        if name == "missing.fixture.test":
            flags = 0x8183
        elif name == "truncated.fixture.test" and transport == "udp":
            flags = 0x8380
        elif name == "timeout.fixture.test":
            return None
        elif kind == 12:
            payload = dns_name(
                "resolver.fixture.test" if name == "1.0.0.127.in-addr.arpa" else "fixture.test"
            )
        elif kind == 1:
            payload = socket.inet_aton("192.0.2.42")
        elif kind == 15:
            payload = struct.pack("!H", 10) + dns_name("mail.fixture.test")
        body = (
            b"\xc0\x0c" + struct.pack("!HHIH", kind, cls, 30, len(payload)) + payload
            if payload
            else b""
        )
        return data[:2] + struct.pack("!HHHHH", flags, 1, bool(payload), 0, 0) + data[12:p] + body

    def serve_udp(self):
        try:
            while not self.stop.is_set():
                try:
                    data, peer = self.udp.recvfrom(4096)
                    response = self.answer(data, "udp")
                    if response is not None:
                        self.udp.sendto(response, peer)
                except TimeoutError:
                    pass
        except BaseException as error:
            self.failures.append(error)

    @staticmethod
    def read_exact(stream, size):
        data = b""
        while len(data) < size:
            chunk = stream.recv(size - len(data))
            if not chunk:
                raise EOFError("incomplete DNS TCP message")
            data += chunk
        return data

    def serve_tcp(self):
        try:
            while not self.stop.is_set():
                try:
                    stream, _ = self.tcp.accept()
                except TimeoutError:
                    continue
                with stream:
                    stream.settimeout(2)
                    size = struct.unpack("!H", self.read_exact(stream, 2))[0]
                    response = self.answer(self.read_exact(stream, size), "tcp")
                    if response is not None:
                        stream.sendall(struct.pack("!H", len(response)) + response)
        except BaseException as error:
            self.failures.append(error)

    def close(self):
        self.stop.set()
        for thread in self.threads:
            thread.join(3)
            assert not thread.is_alive(), "DNS fixture did not stop"
        self.udp.close()
        self.tcp.close()
        assert not self.failures, self.failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", default="unicorn", choices=["unicorn", "native"])
    parser.add_argument(
        "--os2-tools", type=Path, help="Private MPTN directory containing bin and dll"
    )
    args = parser.parse_args()
    build = args.build.resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix="twoine-diagnostics-") as directory:
        work = Path(directory).resolve()
        config = work / "2ine.cfg"
        config.write_text(f"mountpoint.C={work}\n")
        env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(("TWOINE_", "TRACE_", "DYLD_", "LD_"))
            and k not in ("ETC", "LIBPATH")
        }
        env.update(
            TWOINE_BACKEND=args.backend,
            TWOINE_CONFIG=str(config),
            TWOINE_LIBPATH=str(build),
            LIBPATH="C:\\MPTN\\DLL",
            LANG="en_US",
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
        )

        def run(path, *arguments, success=True):
            result = subprocess.run(
                [str(build / "lx_loader"), str(path), *arguments],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            text = result.stdout.decode("latin1") + result.stderr.decode("latin1")
            assert b"Missing import" not in result.stderr, text
            if success:
                assert result.returncode == 0, (path.name, arguments, result.returncode, text)
            results.append({"tool": path.name, "arguments": arguments, "exit": result.returncode})
            return text

        resolver = work / "RESOLVER.EXE"
        resolver.write_bytes(resolver_program())
        run(resolver)
        etc = work / "MPTN" / "ETC"
        marker = b"# Generated by 2ine: host network configuration\r\n"
        for name in ["RESOLV", "RESOLV2", "HOSTS"]:
            assert (etc / name).read_bytes().startswith(marker), name
        # Case-insensitive manually configured files survive startup; owned files refresh.
        (etc / "RESOLV2").unlink()
        manual = etc / "resolv2"
        manual.write_bytes(b"nameserver 127.0.0.1\r\n")
        (etc / "RESOLV").write_bytes(marker + b"obsolete configuration\r\n")
        run(resolver)
        assert manual.read_bytes() == b"nameserver 127.0.0.1\r\n"
        assert "RESOLV2" not in [entry.name for entry in etc.iterdir()]
        assert b"obsolete configuration" not in (etc / "RESOLV").read_bytes()
        endpoint = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        endpoint.bind(("127.0.0.1", 0))
        endpoint.settimeout(10)
        failures = []

        def echo():
            try:
                data, peer = endpoint.recvfrom(4096)
                endpoint.sendto(data, peer)
            except BaseException as error:
                failures.append(error)

        thread = threading.Thread(target=echo)
        thread.start()
        try:
            program = work / "SOCKETS.EXE"
            program.write_bytes(socket_program(endpoint.getsockname()[1]))
            run(program)
        finally:
            thread.join(11)
            endpoint.close()
        assert not thread.is_alive() and not failures, failures
        if args.os2_tools:
            source = args.os2_tools.resolve()
            binaries = work / "mptn-bin"
            binaries.mkdir()
            libraries = work / "MPTN" / "DLL"
            libraries.mkdir()
            for name in [
                "ping.exe",
                "tracerte.exe",
                "host.exe",
                "nslookup.exe",
                "netstat.exe",
                "ifconfig.exe",
            ]:
                original = source / "bin" / name
                shutil.copy2(original, binaries / name)
                results.append(
                    {"fixture": name, "sha256": hashlib.sha256(original.read_bytes()).hexdigest()}
                )
            shutil.copy2(source / "dll" / "ddnsres.dll", libraries / "ddnsres.dll")
            assert re.search(
                r"(?<!\d)0% packet loss", run(binaries / "ping.exe", "localhost", "32", "2")
            )
            assert "127.0.0.1" in run(
                binaries / "tracerte.exe", "-n", "-m", "2", "-q", "1", "-w", "2", "127.0.0.1"
            )
            assert "127.0.0.1" in run(binaries / "host.exe", "localhost")
            assert "localhost" in run(binaries / "host.exe", "127.0.0.1").lower()
            assert "127.0.0.1" in run(binaries / "netstat.exe", "-rn")
            loopback_name = "lo0" if os.uname().sysname == "Darwin" else "lo"
            assert "127.0.0.1" in run(binaries / "ifconfig.exe", loopback_name)
            dns = DNSFixture()
            try:
                options = [f"-port={dns.port}", "-timeout=1", "-retry=1"]
                for name, extra, expected in [
                    ("fixture.test.", [], "192.0.2.42"),
                    ("fixture.test.", ["-vc"], "192.0.2.42"),
                    ("192.0.2.42", [], "fixture.test"),
                    ("fixture.test.", ["-type=MX"], "mail.fixture.test"),
                    ("truncated.fixture.test.", [], "192.0.2.42"),
                    ("missing.fixture.test.", [], "non-existent"),
                    ("timeout.fixture.test.", [], "timed-out"),
                ]:
                    text = run(
                        binaries / "nslookup.exe",
                        *options,
                        *extra,
                        name,
                        "127.0.0.1",
                        success=False,
                    )
                    assert expected in text.lower(), (name, text)
                assert ("udp", "truncated.fixture.test", 1) in dns.events
                assert ("tcp", "truncated.fixture.test", 1) in dns.events
                assert ("udp", "42.2.0.192.in-addr.arpa", 12) in dns.events
            finally:
                dns.close()
            results.append({"dns_queries": dns.events})
    print(json.dumps(results, indent=2))
    print("Guest resolver ABI, socket readiness and resolver configuration checks passed")


if __name__ == "__main__":
    main()
