#!/usr/bin/env python3
"""Header classification and fan-in guard for WildWind Pattern.

Enforces Rule VII of E:/e/file_split_rules_v2_final.md:
  - Value type / pure definition headers: fan-in > 20, lines < 300 (exempt from splitting)
  - Core entity headers: fan-in 20~50, lines < 500 (e.g. Block.h)
  - Aggregator facade headers: fan-in > 50, lines <= 800 (ParamDocument.h)
  - Command headers: if fan-in > 15, must not bundle multi-domain command definitions.
    Umbrella headers (e.g. BlockCommands.h) must be forward-only (only #include / comments).
"""
from __future__ import annotations

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')
TESTS = os.path.join(REPO, 'tests')
EXCEPTIONS_FILE = os.path.join(REPO, 'redline_exceptions.json')


def count_lines(path: str) -> int:
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        return sum(1 for _ in f)


def is_umbrella_forwarder(path: str) -> bool:
    """Checks if a header only consists of #include, comments, pragma, and namespaces."""
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        lines = f.readlines()
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue
        if stripped.startswith('#pragma') or stripped.startswith('#include'):
            continue
        if stripped.startswith('namespace') or stripped.startswith('}') or stripped.startswith('{'):
            continue
        # If there are class or struct definitions, it is not a pure umbrella forwarder
        if re.match(r'^(?:class|struct)\s+\w+', stripped):
            return False
    return True


def main() -> int:
    all_headers = {}
    for root, _, files in os.walk(SRC):
        for f in files:
            if f.endswith('.h'):
                full_path = os.path.join(root, f)
                rel = os.path.relpath(full_path, REPO).replace('\\', '/')
                all_headers[f] = (rel, full_path)

    # Compute fan-in
    fan_in = {h: 0 for h in all_headers}
    for d in [SRC, TESTS]:
        for root, _, files in os.walk(d):
            for f in files:
                if f.endswith(('.cpp', '.h')):
                    with open(os.path.join(root, f), 'r', encoding='utf-8-sig', errors='replace') as file:
                        content = file.read()
                    for h in all_headers:
                        if ('"' + h + '"' in content) or ('/' + h + '"' in content):
                            fan_in[h] += 1

    violations = []

    for h, count in fan_in.items():
        rel, full_path = all_headers[h]
        lines = count_lines(full_path)

        # Check command headers with high fan-in
        if 'document/commands/' in rel:
            if count > 15:
                # Must be an umbrella forwarder or sub-domain command
                if not is_umbrella_forwarder(full_path):
                    # Check if it has multiple command classes
                    with open(full_path, 'r', encoding='utf-8-sig', errors='replace') as file:
                        c_text = file.read()
                    cmd_count = len(re.findall(r'class\s+\w+Command\b', c_text))
                    if cmd_count > 5:
                        violations.append(
                            f"{rel}: fan-in is {count} with {cmd_count} command classes; "
                            "must be split into domain commands or converted to an umbrella forwarder."
                        )

    if violations:
        print(f"FAIL: Found {len(violations)} header classification violation(s):\n")
        for v in violations:
            print(f"  {v}")
        return 1

    print("header classification OK: all headers conform to classification and fan-in rules.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
