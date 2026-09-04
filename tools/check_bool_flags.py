#!/usr/bin/env python3
"""Bool state explosion guard for WildWind Pattern.

Enforces Rule I (二级红线 2. 隐式状态爆炸) of E:/e/file_split_rules_v2_final.md:
  - Any class/struct with > 5 bool member variables is considered at risk
    of state machine explosion and must be refactored or declared in redline_exceptions.json.
"""
from __future__ import annotations

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')
EXCEPTIONS_FILE = os.path.join(REPO, 'redline_exceptions.json')


def count_bool_members(body: str) -> tuple[int, list[str]]:
    # Remove method definitions inside class body to avoid counting local bools
    # A simple approach: strip blocks enclosed in { ... } inside the class body
    # or match lines ending in semicolon that start with bool
    lines = body.splitlines()
    bool_names = []
    in_method = 0
    for line in lines:
        stripped = line.strip()
        # Count brace depth
        in_method += stripped.count('{') - stripped.count('}')
        if in_method > 0:
            continue
        # Check if line declares bool members: e.g. "bool m_flag = false;" or "bool a, b;"
        m = re.match(r'^(?:const\s+|static\s+|mutable\s+)*bool\s+([^;()]+);', stripped)
        if m:
            decls = m.group(1).split(',')
            for d in decls:
                name = d.split('=')[0].strip()
                if name and not name.startswith('('):
                    bool_names.append(name)
    return len(bool_names), bool_names


def main() -> int:
    exceptions = {}
    if os.path.exists(EXCEPTIONS_FILE):
        try:
            with open(EXCEPTIONS_FILE, 'r', encoding='utf-8-sig') as f:
                data = json.load(f)
                exceptions = data.get('bool_flag_exceptions', {})
        except Exception as e:
            print(f'ERROR: Failed to parse {EXCEPTIONS_FILE}: {e}')
            return 1

    violations = []
    monitored = set()

    for root, _, files in os.walk(SRC):
        for f in files:
            if not f.endswith(('.h', '.cpp')):
                continue
            full_path = os.path.join(root, f)
            rel = os.path.relpath(full_path, REPO).replace('\\', '/')
            with open(full_path, 'r', encoding='utf-8-sig', errors='replace') as file:
                content = file.read()

            # Match class / struct
            matches = re.finditer(r'\b(?:class|struct)\s+([A-Za-z0-9_]+)\b[^{;]*\{(.*?)\};', content, re.DOTALL)
            for m in matches:
                cname = m.group(1)
                body = m.group(2)
                cnt, names = count_bool_members(body)
                if cnt > 5:
                    if cname in exceptions:
                        monitored.add(cname)
                    else:
                        violations.append((cname, rel, cnt, names))

    print(f'check_bool_flags: scanned src/ classes, {len(monitored)} legacy exceptions monitored.')

    if violations:
        print(f'\nFAIL: Found {len(violations)} class(es) with > 5 bool flags (implicit state explosion):\n')
        for cname, rel, cnt, names in violations:
            print(f'  {cname} ({rel}): {cnt} bools -> {names}')
        print('\nTo fix: Extract explicit state machine, sub-struct, or session per file_split_rules_v2_final.md.')
        print('If an emergency exemption is required, declare it in redline_exceptions.json.')
        return 1

    print('bool flags OK: no undeclared classes with > 5 bool members.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
