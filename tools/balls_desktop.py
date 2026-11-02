#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""JSON adapter for the shared C ball API; not shipped in the Mac application."""

import json
import sys
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING or __package__:
    from tools import balls
else:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import balls


def request(message):
    return balls.native_request(message)


def main():
    try:
        print(json.dumps(request(json.load(sys.stdin))))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
