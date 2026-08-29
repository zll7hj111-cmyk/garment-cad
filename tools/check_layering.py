#!/usr/bin/env python3
"""Static layering check for the WildWind Pattern source tree (P1-6).

Every library currently exposes the whole `src/` directory through
``target_include_directories(... PUBLIC ${GCAD_SRC})``, so the compiler happily
accepts an include that points UP the layer stack. This script is the missing
guard: it walks every module directory and reports includes whose target module
sits ABOVE the including module.

Layer order (AGENTS.md「架构原则」, low -> high):

    geometry(0) -> parametric(1) -> canvas(2) / document(2) -> ui(3) -> tools(4) -> app(5)

Rules
-----
* An include may target the SAME module or any LOWER one.
* canvas and document are peers (same rank): cross-includes are tolerated
  (canvas pushes document commands; document emits signals the canvas watches).
* Includes of a module's own headers (no module prefix, e.g. "BlockItem.h")
  are always fine.
* tests/ is not checked — test code legitimately touches every layer.

Usage:  python tools/check_layering.py [--strict]
Exit code: 1 when violations are found (0 = clean).
"""
from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, 'src')

# module -> rank (higher = further up the stack)
RANK = {
    'geometry':   0,
    'parametric': 1,
    'canvas':     2,
    'document':   2,
    'ui':         3,
    'tools':      4,
    'app':        5,
}

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def module_of_include(inc: str) -> str | None:
    head = inc.split('/', 1)[0]
    return head if head in RANK else None


def main() -> int:
    violations: list[tuple[str, int, str, str, str]] = []
    for module, rank in sorted(RANK.items(), key=lambda kv: kv[1]):
        root = os.path.join(SRC, module)
        if not os.path.isdir(root):
            continue
        for dirpath, _, files in os.walk(root):
            for name in sorted(files):
                if not name.endswith(('.h', '.cpp')):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, REPO)
                for lineno, line in enumerate(
                        open(path, encoding='utf-8-sig', errors='replace'), start=1):
                    m = INCLUDE.match(line)
                    if not m:
                        continue
                    target = module_of_include(m.group(1))
                    if target is None or target == module:
                        continue
                    if RANK[target] > rank:
                        violations.append((rel, lineno, m.group(1), module, target))

    if not violations:
        print('layering OK: no upward includes (geometry < parametric < '
              'canvas/document < ui < tools < app)')
        return 0

    print(f'{len(violations)} upward include(s) — layering violation:\n')
    for rel, lineno, inc, src, tgt in violations:
        print(f'  {rel}:{lineno}: {src} -> {tgt}   ("{inc}")')
    print('\nFix by moving the shared type into the LOWER layer (interfaces such'
          '\nas canvas/InputDispatcher.h) or by lifting the logic into the'
          '\nUPPER layer and talking through signals.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
