#!/usr/bin/env python3
"""Inline unit / angle / number-formatting guard for the src/ tree.

Single-source-of-truth conventions (2026-12 audit P1-4 / P1-5 / P1-6, work
order G4):
  * Angles: cad::geo::kPi (src/geometry/Angle.h) is the only pi constant.
    M_PI is banned everywhere; std::numbers::* only inside Angle.h.
  * rad<->deg: cad::geo::degToRad / radToDeg. A bare
    "x * 180.0 / kPi" or "x * kPi / 180.0" is banned outside Angle.h.
  * Number display: Units::formatCm / formatLength / formatNumberTrimmed /
    formatDegTrimmed / formatDegValue / formatPoint (src/geometry/Units.h).
    A bare "'f', N" precision is banned outside Units.h: it picks a precision
    of its own, so the same quantity can render two different strings.
  * mm<->cm: Units::mmToCm / cmToMm; a bare "/ 10.0" is banned.

An inline opt-out comment "// units-allow: <reason>" is honoured for the rare
site where the literal is deliberate (same pattern as check_hardcoded_colors).

Usage:  python tools/check_inline_units.py
Exit code: 1 when a banned literal is found (0 = clean).
"""
from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')
ALLOW_MARK = 'units-allow'

# (label, pattern, source files that may legitimately carry the pattern)
RULES = (
    ('M_PI', re.compile(r'\bM_PI\b'), frozenset()),
    ('std::numbers', re.compile(r'\bstd::numbers::'), frozenset({'geometry/Angle.h'})),
    ('bare rad<->deg',
     re.compile(r'180(?:\.0)?\s*/\s*(?:cad::geo::)?kPi\b'
                r'|\b(?:cad::geo::)?kPi\s*/\s*180(?:\.0)?\b'),
     frozenset({'geometry/Angle.h'})),
    ("bare 'f', N", re.compile(r"""['"]f['"]\s*,\s*\d"""), frozenset({'geometry/Units.h'})),
    ('bare / 10.0 (mm<->cm)', re.compile(r'/\s*10\.0\b'), frozenset()),
)


def main() -> int:
    violations: list[tuple[str, int, str, str]] = []
    for dirpath, _, files in os.walk(SRC):
        for name in sorted(files):
            if not name.endswith(('.cpp', '.h')):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, REPO).replace(os.sep, '/')
            rel_src = rel[4:] if rel.startswith('src/') else rel
            for lineno, line in enumerate(
                    open(full, encoding='utf-8-sig', errors='replace'), start=1):
                if ALLOW_MARK in line:
                    continue
                code = line.split('//', 1)[0]  # trailing doc comments are prose
                for label, pattern, allowed in RULES:
                    if rel_src in allowed:
                        continue
                    m = pattern.search(code)
                    if m:
                        violations.append((rel, lineno, label, m.group(0).strip()))

    if not violations:
        print('inline units OK: no M_PI / bare rad<->deg / bare \'f\', N / bare '
              '/ 10.0 in src/ (angles via geometry/Angle.h, display via '
              'geometry/Units.h)')
        return 0

    print(f'{len(violations)} inline unit/format literal(s) outside the '
          f'central helpers:\n')
    for rel, lineno, label, text in violations:
        print(f'  {rel}:{lineno}: [{label}] {text}')
    print('\nUse cad::geo::kPi / degToRad / radToDeg (geometry/Angle.h) and the '
          'Units::format*\nhelpers (geometry/Units.h) instead. Deliberate '
          'literals need a "// units-allow:\n<reason>" comment on the same line.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
