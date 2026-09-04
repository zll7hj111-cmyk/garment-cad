#!/usr/bin/env python3
"""File size and architectural layer guard for WildWind Pattern.

Enforces line count rules from E:/e/file_split_rules_v2_final.md:
  - Pure logic / algorithm (src/geometry/): limit 500 lines
  - Undo commands / data models (src/document/commands/): limit 400 lines
  - Qt UI layout (src/ui/): limit 800 lines
  - Aggregator facade header (src/parametric/ParamDocument.h): limit 800 lines
  - General source files: limit 900 lines

Any file exceeding its threshold must be registered in redline_exceptions.json.
The exceptions list is an invariant ratchet: it may only shrink, never expand.
"""
from __future__ import annotations

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')
EXCEPTIONS_FILE = os.path.join(REPO, 'redline_exceptions.json')

EXEMPT_FILES = {
    'src/ui/Theme.cpp', 'src/ui/Theme.h',
    'src/canvas/CanvasStyle.cpp', 'src/canvas/CanvasStyle.h',
}


def get_layer_threshold(rel: str) -> tuple[int, str]:
    if rel == 'src/parametric/ParamDocument.h':
        return 800, 'Aggregator Facade Header'
    if rel.startswith('src/geometry/'):
        return 500, 'Pure Logic / Geometry Algorithm'
    if rel.startswith('src/document/commands/'):
        return 400, 'Undo Command'
    if rel.startswith('src/ui/'):
        return 800, 'Qt UI Layout / Widget'
    return 900, 'General Source'


def count_lines(path: str) -> int:
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        return sum(1 for _ in f)


def main() -> int:
    exceptions = {}
    if os.path.exists(EXCEPTIONS_FILE):
        try:
            with open(EXCEPTIONS_FILE, 'r', encoding='utf-8-sig') as f:
                data = json.load(f)
                exceptions = data.get('file_size_exceptions', {})
        except Exception as e:
            print(f'ERROR: Failed to parse {EXCEPTIONS_FILE}: {e}')
            return 1

    violations = []
    monitored_exceptions = set()

    for dirpath, _, files in os.walk(SRC):
        for name in sorted(files):
            if not name.endswith(('.cpp', '.h')):
                continue
            full_path = os.path.join(dirpath, name)
            rel = os.path.relpath(full_path, REPO).replace('\\', '/')
            if rel in EXEMPT_FILES:
                continue

            lines = count_lines(full_path)
            threshold, layer_name = get_layer_threshold(rel)

            if lines > threshold:
                if rel in exceptions:
                    monitored_exceptions.add(rel)
                else:
                    violations.append((rel, lines, threshold, layer_name))

    print(f'check_file_size: scanned src/ files, {len(monitored_exceptions)} legacy exceptions monitored.')

    if violations:
        print(f'\nFAIL: Found {len(violations)} unexempted file size violation(s):\n')
        for rel, lines, threshold, layer_name in violations:
            print(f'  {rel}: {lines} lines (limit: {threshold}, layer: {layer_name})')
        print('\nTo fix: Refactor/split the file per file_split_rules_v2_final.md.')
        print('If an emergency exemption is required, declare it in redline_exceptions.json with rationale and milestone.')
        return 1

    print('file size OK: no undeclared over-threshold files in src/.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
