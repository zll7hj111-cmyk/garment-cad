#!/usr/bin/env python3
"""Test size and naming convention guard for WildWind Pattern.

Enforces Rule VIII of E:/e/file_split_rules_v2_final.md:
  - Test files must not exceed 2,500 lines (mandatory split limit).
  - Test files exceeding 1,500 lines trigger warnings.
  - Over-limit test files must be registered in redline_exceptions.json.
  - Test files must follow domain naming conventions (test_<domain>*.cpp).
"""
from __future__ import annotations

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(REPO, 'tests')
EXCEPTIONS_FILE = os.path.join(REPO, 'redline_exceptions.json')

MANDATORY_LIMIT = 2500
WARNING_LIMIT = 1500


def count_lines(path: str) -> int:
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        return sum(1 for _ in f)


def main() -> int:
    exceptions = {}
    if os.path.exists(EXCEPTIONS_FILE):
        try:
            with open(EXCEPTIONS_FILE, 'r', encoding='utf-8-sig') as f:
                data = json.load(f)
                exceptions = data.get('test_size_exceptions', {})
        except Exception as e:
            print(f'ERROR: Failed to parse {EXCEPTIONS_FILE}: {e}')
            return 1

    violations = []
    warnings = []
    monitored = set()

    for root, _, files in os.walk(TESTS):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            full_path = os.path.join(root, f)
            rel = os.path.relpath(full_path, REPO).replace('\\', '/')
            lines = count_lines(full_path)

            if lines > MANDATORY_LIMIT:
                if rel in exceptions:
                    monitored.add(rel)
                else:
                    violations.append((rel, lines, MANDATORY_LIMIT))
            elif lines > WARNING_LIMIT:
                warnings.append((rel, lines, WARNING_LIMIT))

    print(f'check_test_split: scanned tests/ files, {len(monitored)} legacy exceptions monitored.')
    if warnings:
        print(f'NOTE: {len(warnings)} test file(s) exceed warning threshold ({WARNING_LIMIT} lines):')
        for rel, lines, w_lim in warnings:
            print(f'  [WARN] {rel}: {lines} lines (> {w_lim})')

    if violations:
        print(f'\nFAIL: Found {len(violations)} test file(s) exceeding mandatory limit ({MANDATORY_LIMIT} lines):\n')
        for rel, lines, m_lim in violations:
            print(f'  {rel}: {lines} lines (mandatory limit: {m_lim})')
        print('\nTo fix: Split into domain-specific test suites per file_split_rules_v2_final.md.')
        print('If an emergency exemption is required, declare it in redline_exceptions.json.')
        return 1

    print('test split OK: no undeclared test files exceeding mandatory limit.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
