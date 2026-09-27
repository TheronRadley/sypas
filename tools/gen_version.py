#!/usr/bin/env python3
"""Generate the one C header that exposes SYPAS's source-tree version."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

VERSION_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version-file", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    version = args.version_file.read_text(encoding="ascii").strip()
    if not VERSION_RE.fullmatch(version):
        raise SystemExit(f"invalid SYPAS version in {args.version_file}: {version!r}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "/* Generated from the repository-root VERSION file.  Do not edit. */\n"
        "#ifndef SYPAS_VERSION_H\n"
        "#define SYPAS_VERSION_H\n"
        f"#define SYPAS_VERSION \"{version}\"\n"
        f"#define SYPAS_VERSION_W L\"{version}\"\n"
        "#endif /* SYPAS_VERSION_H */\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
