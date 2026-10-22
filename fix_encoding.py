#!/usr/bin/env python3
"""
Clean up broken characters in the C sources so GitHub shows them correctly.

What it does, for every .c/.h under include/ and src/:
  1. Files saved as Windows-1252 (em-dashes etc. as single bytes) -> converted to UTF-8.
  2. Literal replacement characters (U+FFFD, shown as a black diamond with ?) -> replaced by "-".
  3. Any fix that lands in CODE or a STRING (not a comment) is listed so you can review it
     (in this project they are #error / _Static_assert messages).
Line endings (CRLF/LF) are preserved. Run from the repo root:   python fix_encoding.py
Use --dry-run to only report.
"""
import os, sys

DRY = '--dry-run' in sys.argv
ROOTS = ['include', 'src']
EXTS = ('.c', '.h')
ASCII_FALLBACK = {'\u2014': '-', '\u2013': '-', '\u2192': '->', '\u2190': '<-', '\u2265': '>=',
                  '\u2264': '<=', '\u2018': "'", '\u2019': "'", '\u201c': '"', '\u201d': '"',
                  '\u2026': '...', '\ufffd': '-'}

def decode(b: bytes) -> str:
    try:
        return b.decode('utf-8')
    except UnicodeDecodeError:
        out = []
        for ch in b:
            if ch < 0x80: out.append(chr(ch))
            else:
                try: out.append(bytes([ch]).decode('cp1252'))
                except UnicodeDecodeError: out.append(bytes([ch]).decode('latin-1'))
        return ''.join(out)

def fix_text(t: str):
    """Return (new_text, [(line, old_line)] for fixes made outside comments)."""
    out, code_fixes = [], []
    i, n, line = 0, len(t), 1
    state, line_start = 'code', 0
    while i < n:
        c = t[i]
        if c == '\n':
            line += 1; line_start = i + 1
        if state == 'code':
            if t.startswith('/*', i): state = 'block'; out.append('/*'); i += 2; continue
            if t.startswith('//', i): state = 'line'; out.append('//'); i += 2; continue
            if c == '"': state = 'str'
            elif c == "'": state = 'chr'
        elif state == 'block':
            if t.startswith('*/', i): state = 'code'; out.append('*/'); i += 2; continue
        elif state == 'line':
            if c == '\n': state = 'code'
        elif state in ('str', 'chr'):
            if c == '\\' and i + 1 < n: out.append(c); out.append(t[i+1]); i += 2; continue
            if (state == 'str' and c == '"') or (state == 'chr' and c == "'"): state = 'code'
        # fix characters
        if c == '\ufffd' or (ord(c) > 0x7f and state in ('code', 'str', 'chr')):
            if state in ('code', 'str', 'chr'):
                end = t.find('\n', i); end = n if end < 0 else end
                code_fixes.append((line, t[line_start:end].strip()[:100]))
                out.append(ASCII_FALLBACK.get(c, '-'))
            elif c == '\ufffd':
                out.append('-')
            else:
                out.append(c)
        else:
            out.append(c)
        i += 1
    return ''.join(out), code_fixes

changed = 0
reviews = []
for root in ROOTS:
    for d, _, files in os.walk(root):
        for f in files:
            if not f.endswith(EXTS): continue
            p = os.path.join(d, f)
            raw = open(p, 'rb').read()
            text = decode(raw)
            new, cf = fix_text(text)
            new_b = new.encode('utf-8')
            if new_b != raw:
                changed += 1
                if not DRY: open(p, 'wb').write(new_b)
                print(('would fix: ' if DRY else 'fixed: ') + p)
            for ln, src in cf:
                reviews.append((p, ln, src))
print(f'\nDone. files changed: {changed}' + ('  (dry run)' if DRY else ''))
if reviews:
    print('\nReview these (fix landed in code/string, not a comment):')
    for p, ln, src in reviews: print(f'  {p}:{ln}: {src}')
