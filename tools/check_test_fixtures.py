#!/usr/bin/env python3
"""Test-fixture hygiene check (P2-2).

A regression baseline that lives OUTSIDE the repository is not a baseline: it
is whatever the user happens to have on disk. This used to be the case for
several ctest targets, which loaded `E:/3.gcad` (a real pattern-making save
file), `e:/存档/1.gcad` and even `build/out/Debug/1.gcad` (a build artifact
that was never committed, so those tests had been silently QSKIPping for
months). Whenever the user edited those documents the tests went red with
messages like "交点偏移 29.17mm / 缺变量 后长补正" — noise that hid real
regressions.

Rule
----
Tests must build their documents with the model API (or load a fixture that is
committed under `tests/fixtures/`). They must NOT reference:

  * absolute paths outside the repo (`C:/...`, `E:/...`, `/home/...`),
  * build artifacts (`build/...`),
  * the user's home (`~`, `%USERPROFILE%`).

Exceptions are declared explicitly with a comment on the same line:

      const QString path = QStringLiteral("E:/x.gcad");  // fixture-allow: manual probe

`test_realdoc_*` targets are exempt: they are manual performance probes driven
by the GCAD_DOC environment variable and are deliberately not in ctest.

Usage:  python tools/check_test_fixtures.py
Exit code: 1 when an undeclared external path is found.
"""
from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(REPO, 'tests')

EXEMPT_FILES = ('test_realdoc_perf.cpp', 'test_realdoc_full.cpp')
ALLOW_MARK = 'fixture-allow'

# Absolute-ish path literals handed to file APIs.
PATH_LITERAL = re.compile(
    r'"((?:[A-Za-z]:[\\/]|\\\\|/|~/)[^"]{0,200})"')

# Any string that names a document we care about, even without a drive letter.
DOC_SUFFIX = re.compile(r'"[^"]*\.(?:gcad|json)"')

BUILD_ARTIFACT = re.compile(r'\bbuild[\\/]')


def is_external(path: str) -> bool:
    # Only flag things that look like FILE paths (a name with an extension);
    # bare fragments such as "/" or "/6" are string noise, not fixtures.
    if not re.search(r'\.[A-Za-z0-9]{2,5}$', path):
        return False
    if BUILD_ARTIFACT.search(path):
        return True
    if re.match(r'^[A-Za-z]:[\\/]', path):          # C:/ E:/ ...
        return True
    if path.startswith('\\\\'):
        return True
    if path.startswith('/'):
        # "/tmp/x.gcad" is external; "/x.gcad" is just a filename fragment.
        return path.count('/') >= 2
    if path.startswith('~'):
        return True
    return False


def main() -> int:
    violations: list[tuple[str, int, str]] = []
    for dirpath, _, files in os.walk(TESTS):
        for name in sorted(files):
            if not name.endswith(('.cpp', '.h')) or name in EXEMPT_FILES:
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), REPO)
            for lineno, line in enumerate(
                    open(os.path.join(dirpath, name), encoding='utf-8-sig',
                         errors='replace'), start=1):
                if ALLOW_MARK in line:
                    continue
                for m in PATH_LITERAL.finditer(line):
                    if is_external(m.group(1)):
                        violations.append((rel, lineno, m.group(1)))
    if not violations:
        print('test fixtures OK: no external document paths (tests build their '
              'own documents)')
        return 0

    print(f'{len(violations)} external document path(s) in tests:\n')
    for rel, lineno, path in violations:
        print(f'  {rel}:{lineno}: "{path}"')
    print('\nBuild the document with the ParamDocument API instead (see '
          'test_intersection_update.cpp / test_p612_colinearity.cpp for the\n'
          'synthetic-fixture pattern), or commit it under tests/fixtures/. '
          'Genuine one-off probes may opt out\nwith a `// fixture-allow: '
          '<reason>` comment on the same line.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
