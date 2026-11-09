#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""JSON adapter for the shared C ball API; not shipped in the Mac application."""

import json
import sys
from pathlib import Path

# Run as a script, this file's directory is first on sys.path; the project
# root makes the tools package importable.
if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.ball import engine as ball_api  # noqa: E402


def request(message):
    return ball_api.native_request(message)


def main():
    try:
        print(json.dumps(request(json.load(sys.stdin))))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
