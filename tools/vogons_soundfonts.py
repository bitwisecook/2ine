#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Inventory a public VOGONS topic, then fetch one explicitly selected link.

Posts and license claims remain source data. Forum text never becomes executable
instructions, a redistribution grant, or part of the pinned SoundFont catalog.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import http.client
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass, field
from email.message import Message
from html.parser import HTMLParser
from pathlib import Path, PurePosixPath
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tools import fetch_soundfonts as sf
else:
    try:
        from tools import fetch_soundfonts as sf
    except ModuleNotFoundError:
        import fetch_soundfonts as sf

USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36"
PAGE_LIMIT = 4 * 1024 * 1024
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "wbr"}


@dataclass(eq=False)
class Node:
    tag: str
    attrs: dict[str, str]
    children: list[Node | str] = field(default_factory=list)
    parent: Node | None = None

    def text(self) -> str:
        return "".join(
            child.text() if isinstance(child, Node) else child for child in self.children
        )

    def all(self):
        yield self
        for child in self.children:
            if isinstance(child, Node):
                yield from child.all()

    def has_class(self, name: str) -> bool:
        return name in self.attrs.get("class", "").split()


class Document(HTMLParser):
    def __init__(self, html: str):
        super().__init__(convert_charrefs=True)
        self.root = Node("root", {})
        self.stack = [self.root]
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        node = Node(tag, {key: value or "" for key, value in attrs}, parent=self.stack[-1])
        self.stack[-1].children.append(node)
        if tag in {"br", "p", "li"}:
            node.children.append("\n")
        if tag not in VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, 0, -1):
            if self.stack[index].tag == tag:
                if tag in {"p", "li"}:
                    self.stack[index].children.append("\n")
                del self.stack[index:]
                break

    def handle_data(self, data):
        if self.stack[-1].tag not in {"script", "style"}:
            self.stack[-1].children.append(data)


def clean(text: str) -> str:
    return "\n".join(line for raw in text.splitlines() if (line := " ".join(raw.split())))


def topic_url(url: str, topic: str | None = None) -> tuple[str, str]:
    parsed = urllib.parse.urlparse(url)
    query = urllib.parse.parse_qs(parsed.query)
    identity = query.get("t", [""])[0]
    if (
        parsed.scheme != "https"
        or parsed.hostname not in {"vogons.org", "www.vogons.org"}
        or parsed.path != "/viewtopic.php"
        or not identity.isdecimal()
        or (topic is not None and identity != topic)
    ):
        raise ValueError("expected a public HTTPS VOGONS viewtopic.php?t=NUMBER URL")
    start = query.get("start", ["0"])[0]
    if not start.isdecimal():
        raise ValueError("invalid topic page offset")
    suffix = "&start=" + str(int(start)) if int(start) else ""
    return "https://www.vogons.org/viewtopic.php?t=" + identity + suffix, identity


def parse_page(html: str, url: str) -> dict:
    canonical, identity = topic_url(url)
    doc = Document(html)
    nodes = list(doc.root.all())
    title_node = next((node for node in nodes if node.attrs.get("id") == "title"), None)
    title = clean(title_node.text()) if title_node else ""
    posts = []
    for node in nodes:
        post_id = node.attrs.get("id", "")
        if not node.has_class("post") or not re.fullmatch(r"p\d+", post_id):
            continue
        descendants = list(node.all())
        body = next((item for item in descendants if item.has_class("post_content")), None)
        if body is None:
            continue
        author = next((item for item in descendants if item.has_class("post_user_username")), None)
        if author:
            author = next((item for item in author.all() if item.has_class("username")), author)
        date = next(
            (item.attrs.get("datetime", "") for item in descendants if item.tag == "time"), ""
        )
        permalink = "https://www.vogons.org/viewtopic.php?p=" + post_id[1:] + "#" + post_id
        post_license = [
            {
                "claim": item.attrs.get("title", clean(item.text())),
                "url": urllib.parse.urljoin(url, item.attrs.get("href", "")),
            }
            for item in descendants
            if item.has_class("post_license_button")
        ]
        body_text = clean(body.text())
        links = []
        seen = set()
        for item in body.all():
            if item.tag != "a" or not item.attrs.get("href"):
                continue
            href = urllib.parse.urljoin(url, item.attrs["href"])
            if urllib.parse.urlparse(href).scheme not in {"http", "https"} or href in seen:
                continue
            seen.add(href)
            context = item.parent
            while context and context is not body and context.tag not in {"p", "figure", "dd"}:
                context = context.parent
            context_text = clean(context.text()) if context else clean(item.text())
            kind = "link"
            if "/download/file.php" in href:
                kind = "attachment"
            elif re.search(r"https://drive\.google\.com/file/d/[^/]+/", href):
                kind = "google-drive-file"
            elif "drive.google.com/drive/folders/" in href:
                kind = "google-drive-folder"
            elif urllib.parse.urlparse(href).path.lower().endswith((".sf2", ".zip", ".7z", ".rar")):
                kind = "file"
            figure = item.parent
            while figure and figure is not body and not figure.tag == "figure":
                figure = figure.parent
            attachment_license = []
            if figure and figure.tag == "figure":
                attachment_license = [
                    clean(value.text())
                    for value in figure.all()
                    if value.has_class("attachment_license")
                ]
            links.append(
                {
                    "id": hashlib.sha256(href.encode()).hexdigest()[:16],
                    "url": href,
                    "label": clean(item.text()),
                    "context": context_text[:2000],
                    "kind": kind,
                    "attachment_license_claims": attachment_license,
                    "bank_license": "unknown; post/attachment licenses do not establish rights for external banks",
                }
            )
        posts.append(
            {
                "id": post_id,
                "author": clean(author.text()) if author else "",
                "date": date,
                "url": permalink,
                "text": body_text,
                "post_license_claims": post_license,
                "body_license_claims": [
                    line
                    for line in body_text.splitlines()
                    if re.search(r"licen[sc]|public domain|CC-BY|GPL", line, re.I)
                ],
                "links": links,
            }
        )
    pages = set()
    for node in nodes:
        if node.tag != "a":
            continue
        href = urllib.parse.urljoin(url, node.attrs.get("href", ""))
        try:
            page, _ = topic_url(href, identity)
            pages.add(page)
        except ValueError:
            pass
    if not title or not posts:
        raise ValueError("response contains no VOGONS topic title/posts; not a usable topic page")
    return {"url": canonical, "title": title, "posts": posts, "pages": sorted(pages)}


def request(url: str):
    return urllib.request.Request(url, headers={"User-Agent": USER_AGENT})


def fetch_page(url: str) -> bytes:
    try:
        with urllib.request.urlopen(request(url), timeout=30) as response:
            data = response.read(PAGE_LIMIT + 1)
    except urllib.error.HTTPError as error:
        error.close()
        # Some public forum servers accept an ordinary curl fetch while denying
        # urllib's TLS/HTTP client. This does not add cookies or authentication.
        if error.code not in {403, 503} or not shutil.which("curl"):
            raise
        result = subprocess.run(
            [
                "curl",
                "--fail",
                "--location",
                "--max-time",
                "30",
                "--max-filesize",
                str(PAGE_LIMIT),
                "--user-agent",
                USER_AGENT,
                "--url",
                url,
            ],
            capture_output=True,
            check=False,
            timeout=35,
        )
        if result.returncode:
            raise ValueError(
                f"public topic fetch failed: {error}; curl: "
                + result.stderr.decode("utf-8", "replace")[:1000]
            ) from error
        data = result.stdout
    if len(data) > PAGE_LIMIT:
        raise ValueError("topic page exceeds limit")
    return data


def discover(url: str, output: Path, max_pages: int = 10, html_path: Path | None = None) -> dict:
    canonical, _ = topic_url(url)
    output.mkdir(parents=True, exist_ok=True)
    queue, visited, posts, pages, errors = [canonical], set(), {}, [], []
    title = ""
    while queue and len(visited) < max_pages:
        page_url = queue.pop(0)
        if page_url in visited:
            continue
        visited.add(page_url)
        try:
            if html_path:
                data = html_path.read_bytes()
                if len(data) > PAGE_LIMIT:
                    raise ValueError("saved topic page exceeds limit")
            else:
                data = fetch_page(page_url)
            parsed = parse_page(data.decode("utf-8", "replace"), page_url)
            title = title or parsed["title"]
            saved = output / f"page-{len(pages):04}.html"
            saved.write_bytes(data)
            pages.append(
                {
                    "url": page_url,
                    "path": str(saved.resolve()),
                    "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                }
            )
            for post in parsed["posts"]:
                posts[post["id"]] = post
            for page in parsed["pages"]:
                if page not in visited and page not in queue:
                    queue.append(page)
            if html_path:
                break
        except (OSError, ValueError, urllib.error.URLError, subprocess.TimeoutExpired) as error:
            errors.append({"url": page_url, "error": str(error)})
    links = {}
    for post in posts.values():
        for link in post["links"]:
            value = links.setdefault(link["id"], {**link, "observations": []})
            value["observations"].append(
                {
                    "post_id": post["id"],
                    "post_url": post["url"],
                    "author": post["author"],
                    "context": link["context"],
                }
            )
    inventory = {
        "schema": 1,
        "topic": canonical,
        "title": title,
        "observed_at": datetime.datetime.now(datetime.UTC).isoformat(),
        "complete": not queue and not errors,
        "unfetched_pages": queue,
        "errors": errors,
        "pages": pages,
        "posts": list(posts.values()),
        "links": list(links.values()),
    }
    sf.atomic_write(
        output / "inventory.json", json.dumps(inventory, indent=2, ensure_ascii=False) + "\n"
    )
    return inventory


def download_url(url: str) -> str:
    match = re.fullmatch(r"https://drive\.google\.com/file/d/([A-Za-z0-9_-]+)/[^#]*", url)
    if match:
        return "https://drive.google.com/uc?" + urllib.parse.urlencode(
            {"export": "download", "id": match[1]}
        )
    if "drive.google.com/drive/folders/" in url:
        raise ValueError("select an individual file; folder downloads are not automatic")
    return url


def drive_confirmation(html: bytes, source_url: str) -> str | None:
    if urllib.parse.urlparse(source_url).hostname not in {
        "drive.google.com",
        "drive.usercontent.google.com",
    }:
        return None
    doc = Document(html.decode("utf-8", "replace"))
    for form in doc.root.all():
        if form.tag != "form":
            continue
        action = urllib.parse.urljoin(source_url, form.attrs.get("action", ""))
        parsed = urllib.parse.urlparse(action)
        if (
            parsed.scheme != "https"
            or parsed.hostname not in {"drive.google.com", "drive.usercontent.google.com"}
            or parsed.path not in {"/download", "/uc"}
        ):
            continue
        fields = {
            node.attrs["name"]: node.attrs.get("value", "")
            for node in form.all()
            if node.tag == "input"
            and node.attrs.get("name") in {"id", "export", "confirm", "uuid", "at"}
        }
        source_id = urllib.parse.parse_qs(urllib.parse.urlparse(source_url).query).get(
            "id", [None]
        )[0]
        if source_id and fields.get("id") != source_id:
            continue
        if fields.get("id") and fields.get("confirm"):
            return action + ("&" if parsed.query else "?") + urllib.parse.urlencode(fields)
    return None


def safe_name(name: str) -> str:
    name = name.replace("\\", "/").rsplit("/", 1)[-1]
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)[:180] or "download.bin"


def inspect_download(path: Path) -> dict:
    with path.open("rb") as stream:
        head = stream.read(12)
    if head[:4] == b"RIFF" and head[8:] == b"sfbk":
        try:
            sf.validate_sf2(path)
            return {"format": "sf2", "complete_gm": True}
        except ValueError as error:
            return {"format": "sf2", "complete_gm": False, "coverage_error": str(error)}
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            if len(entries) > 4096:
                raise ValueError("ZIP has too many members")
            return {
                "format": "zip",
                "members": [
                    {
                        "name": item.filename,
                        "size": item.file_size,
                        "compressed_size": item.compress_size,
                    }
                    for item in entries
                ],
            }
    return {"format": "file"}


def selected_fetch(
    inventory: dict,
    identity: str,
    cache: Path,
    max_bytes: int = 512 * 1024**2,
    expected_sha256: str | None = None,
    opener=urllib.request.urlopen,
) -> dict:
    links = [link for link in inventory["links"] if link["id"] == identity]
    if len(links) != 1:
        raise ValueError("select one exact link id from the inventory")
    link = links[0]
    url = download_url(link["url"])
    cache.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        for _ in range(3):
            with opener(request(url), timeout=60) as response:
                resolved = response.geturl()
                kind = response.headers.get_content_type()
                beginning = response.read(4096)
                if kind in {
                    "text/html",
                    "application/xhtml+xml",
                } or beginning.lstrip().lower().startswith((b"<!doctype html", b"<html")):
                    html = beginning + response.read(PAGE_LIMIT + 1)
                    if len(html) > PAGE_LIMIT:
                        raise ValueError("download landing page exceeds limit")
                    confirmation = drive_confirmation(html, resolved)
                    if not confirmation:
                        raise ValueError("selected link returned a landing/login page, not a file")
                    url = confirmation
                    continue
                disposition = Message()
                disposition["content-disposition"] = response.headers.get("Content-Disposition", "")
                name = safe_name(
                    disposition.get_filename() or Path(urllib.parse.urlparse(resolved).path).name
                )
                sha, size = hashlib.sha256(), 0
                with tempfile.NamedTemporaryFile(
                    prefix=".download-", dir=cache, delete=False
                ) as out:
                    temporary = Path(out.name)
                    chunk = beginning
                    while chunk:
                        size += len(chunk)
                        if size > max_bytes:
                            raise ValueError("selected file exceeds download byte limit")
                        sha.update(chunk)
                        out.write(chunk)
                        chunk = response.read(sf.CHUNK)
                    out.flush()
                    os.fsync(out.fileno())
                content_length = response.headers.get("Content-Length")
                if content_length and content_length.isdecimal() and size != int(content_length):
                    raise ValueError("selected download ended before its advertised byte count")
                value = sha.hexdigest()
                if expected_sha256 and value != expected_sha256:
                    raise ValueError("selected download SHA-256 mismatch")
                inspection = inspect_download(temporary)
                directory = cache / value
                directory.mkdir(exist_ok=True)
                destination = directory / name
                os.replace(temporary, destination)
                temporary = None
                receipt = {
                    "schema": 1,
                    "topic": inventory["topic"],
                    "link": link,
                    "requested_url": link["url"],
                    "resolved_url": resolved,
                    "path": str(destination.resolve()),
                    "size": size,
                    "sha256": value,
                    "expected_sha256": expected_sha256,
                    "bank_license": "unknown; no redistribution grant inferred from forum text",
                    "observed_at": datetime.datetime.now(datetime.UTC).isoformat(),
                    **inspection,
                }
                sf.atomic_write(
                    directory / "receipt.json",
                    json.dumps(receipt, indent=2, ensure_ascii=False) + "\n",
                )
                return receipt
        raise ValueError("public download confirmation did not produce a file")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def extract_member(receipt: dict, member: str, output: Path, max_bytes: int) -> Path:
    pure = PurePosixPath(member)
    if pure.is_absolute() or ".." in pure.parts or "\\" in member:
        raise ValueError("unsafe ZIP member name")
    path = Path(receipt["path"])
    if sf.digest(path) != receipt["sha256"]:
        raise ValueError("cached archive digest changed")
    with zipfile.ZipFile(path) as archive:
        info = archive.getinfo(member)
        if (
            info.is_dir()
            or info.file_size > max_bytes
            or info.file_size > max(1, info.compress_size) * 500
        ):
            raise ValueError("ZIP member exceeds extraction limits")
        output.mkdir(parents=True, exist_ok=True)
        destination = output / safe_name(pure.name)
        with (
            archive.open(info) as source,
            tempfile.NamedTemporaryFile(dir=output, delete=False) as out,
        ):
            temporary = Path(out.name)
            try:
                count = 0
                while chunk := source.read(sf.CHUNK):
                    count += len(chunk)
                    if count > max_bytes:
                        raise ValueError("ZIP member exceeds extraction byte limit")
                    out.write(chunk)
                out.flush()
                os.fsync(out.fileno())
                os.replace(temporary, destination)
            finally:
                temporary.unlink(missing_ok=True)
    sf.atomic_write(
        destination.with_suffix(destination.suffix + ".receipt.json"),
        json.dumps(
            {
                "archive_sha256": receipt["sha256"],
                "member": member,
                "path": str(destination.resolve()),
                "sha256": sf.digest(destination),
                "bank_license": receipt["bank_license"],
            },
            indent=2,
        )
        + "\n",
    )
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    find = commands.add_parser("discover")
    find.add_argument("topic")
    find.add_argument("--out", type=Path, required=True)
    find.add_argument("--max-pages", type=int, default=10)
    find.add_argument("--html", type=Path, help="parse an already retained public page")
    listing = commands.add_parser("list")
    listing.add_argument("inventory", type=Path)
    fetch = commands.add_parser("fetch")
    fetch.add_argument("inventory", type=Path)
    fetch.add_argument("link_id")
    fetch.add_argument("--cache", type=Path, required=True)
    fetch.add_argument("--max-mib", type=int, default=512)
    fetch.add_argument("--sha256")
    fetch.add_argument("--member", help="extract exactly this ZIP member after fetching")
    fetch.add_argument(
        "--use", action="store_true", help="select the downloaded/extracted complete GM SF2"
    )
    fetch.add_argument("--config", type=Path, default=sf.default_config())
    args = parser.parse_args()
    try:
        if args.command == "discover":
            if not 1 <= args.max_pages <= 100:
                raise ValueError("max-pages must be 1..100")
            inventory = discover(args.topic, args.out, args.max_pages, args.html)
            print(
                f"{inventory['title']}: {len(inventory['posts'])} posts, {len(inventory['links'])} links; complete={inventory['complete']}"
            )
            print(args.out / "inventory.json")
            return 0 if inventory["complete"] else 1
        inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
        if args.command == "list":
            print(inventory["title"])
            for link in inventory["links"]:
                print(f"{link['id']}  {link['kind']}  {link['url']}\n  {link['context'][:180]}")
            return 0
        if not 1 <= args.max_mib <= 4096:
            raise ValueError("max-mib must be 1..4096")
        receipt = selected_fetch(
            inventory, args.link_id, args.cache, args.max_mib * 1024**2, args.sha256
        )
        path = Path(receipt["path"])
        if args.member:
            path = extract_member(
                receipt, args.member, path.parent / "selected", args.max_mib * 1024**2
            )
        print(path)
        print(f"SHA-256 {sf.digest(path)}")
        if args.use:
            print(sf.use_file(path, args.cache / "selected-banks", args.config))
        return 0
    except (
        OSError,
        ValueError,
        KeyError,
        urllib.error.URLError,
        http.client.HTTPException,
        zipfile.BadZipFile,
    ) as error:
        print(f"vogons_soundfonts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
