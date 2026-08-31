#!/usr/bin/env python3
"""Hardcoded color guard for the WildWind Pattern source tree.

The styling contract (AGENTS.md「开发规范」/ Theme.h): ALL widget colors come
from ThemeTokens (cad::ui) / CanvasStyle (canvas twin) — single source of
truth, light/dark aware, theme switching re-bakes instance QSS from tokens.
A hex literal written into a stylesheet or a widget bypasses the theme:
it is invisible in dark mode, and switching themes silently drops it.

This script finds hex color literals (`#RRGGBB`) in `src/` and reports every
one that is NOT declared as an allowed exception.

Rules
-----
* Theme.{h,cpp} and CanvasStyle.{h,cpp} are the token sources themselves —
  exempt (their hex values ARE the theme).
* Trailing `//` comments are skipped: doc comments that cite a reference hex
  (e.g. `QColor(20, 24, 30);  // #14181E night paper`) are documentation,
  not styling code.
* Anything else: flag unless the line carries an inline opt-out comment
  `// color-allow: <reason>` (same pattern as check_test_fixtures.py).
  Prefer mapping the color to a token; use the opt-out only for hues that
  are deliberately fixed across themes (e.g. the cross-layer purple badge)
  or that live in a layer that must not depend on ui tokens
  (e.g. Serial's HTML spans in parametric/).

Usage:  python tools/check_hardcoded_colors.py
Exit code: 1 when undeclared hardcoded colors are found (0 = clean).
"""
from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')

# The token tables themselves — their hex values ARE the theme definition.
EXEMPT_FILES = {
    'ui/Theme.cpp', 'ui/Theme.h',
    'canvas/CanvasStyle.cpp', 'canvas/CanvasStyle.h',
}
ALLOW_MARK = 'color-allow'

HEX_COLOR = re.compile(r'#[0-9A-Fa-f]{6}\b')


def main() -> int:
    violations: list[tuple[str, int, str]] = []
    for dirpath, _, files in os.walk(SRC):
        for name in sorted(files):
            if not name.endswith(('.cpp', '.h')):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), REPO)
            if rel.replace(os.sep, '/').lstrip('src/') in EXEMPT_FILES:
                continue
            for lineno, line in enumerate(
                    open(os.path.join(dirpath, name), encoding='utf-8-sig',
                         errors='replace'), start=1):
                if ALLOW_MARK in line:
                    continue
                code = line.split('//', 1)[0]  # 剥掉行尾注释（文档引用色）；color-allow 在注释里时上面的判断已放行
                for m in HEX_COLOR.finditer(code):
                    violations.append((rel, lineno, m.group(0)))
    if not violations:
        print('hardcoded colors OK: no undeclared hex colors in src/ '
              '(all widget colors come from ThemeTokens / CanvasStyle)')
        return 0

    print(f'{len(violations)} hardcoded color(s) outside the token tables:\n')
    for rel, lineno, hexval in violations:
        print(f'  {rel}:{lineno}: {hexval}')
    print('\nMap the color to a ThemeTokens / CanvasStyle token (light + dark '
          'entries at the same\nplace) instead of a literal. If a hue is '
          'deliberately theme-independent, declare it with a\n`// color-allow: '
          '<reason>` comment on the same line.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
