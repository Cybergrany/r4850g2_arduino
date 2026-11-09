#!/usr/bin/env python3
"""Reject conversions unsupported by this firmware's default AVR-libc printf.

Run against authored production code, so a permissive desktop libc cannot hide
'*' widths/precisions, long long or float formatting. Arduino Print is separate.
"""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
conversion = re.compile(r'%(?:%|([-+ #0]*)(\*|\d*)(?:\.(\*|\d*))?(hh|ll|[hlLzjt])?([a-zA-Z]))')

def unsupported(fmt):
    for m in conversion.finditer(fmt):
        if m.group() == '%%':
            continue
        flags, width, precision, length, kind = m.groups()
        if width == '*' or precision == '*':
            yield m.group() + ': variable width/precision aborts AVR printf'
        elif length not in (None, 'h', 'l') or kind not in 'diouxXcspS':
            yield m.group() + ': unsupported conversion in default AVR printf'
        elif any(int(n or '0') > 255 for n in (width, precision)):
            yield m.group() + ': AVR width/precision exceeds 255'

def formats(source):
    # Tokenize calls, quoted strings and punctuation; skip comments. This also
    # handles adjacent literal fragments and commas/parentheses inside strings.
    source = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', lambda m: '\n' * m.group().count('\n'), source)
    call = re.compile(r'\b(PSU_SNPRINTF|snprintf_P|snprintf|sprintf|printf)\s*\(')
    token = re.compile(r'"(?:\\.|[^"\\])*"|[(),]|[^(),"]+')
    for found in call.finditer(source):
        target = 0 if found[1] == 'printf' else 1 if found[1] == 'sprintf' else 2
        depth, arg, literal = 1, 0, ''
        for item in token.finditer(source, found.end()):
            v = item.group()
            if v == '(':
                depth += 1
            elif v == ')':
                depth -= 1
                if depth == 0:
                    break
            elif v == ',' and depth == 1:
                if arg == target:
                    break
                arg += 1
            elif arg == target and v.startswith('"'):
                literal += v[1:-1]
        if literal:
            yield source.count('\n', 0, found.start())+1, literal

def main():
    assert list(unsupported('%0*u')) and list(unsupported('%.*s'))
    assert list(unsupported('%llu')) and list(unsupported('%.2f'))
    assert not list(unsupported('%02u %lu %.8s %s %% %c %02X'))
    assert list(formats('int n = PSU_SNPRINTF(a, sizeof(a), "%" "0*u", x, y);')) == [(1, '%0*u')]
    errors = []
    for path in (root/'src').rglob('*'):
        if path.suffix not in ('.cpp', '.h') or 'vendor' in path.parts:
            continue
        for line, fmt in formats(path.read_text()):
            errors.extend(f'{path.relative_to(root)}:{line}: {error}' for error in unsupported(fmt))
    if errors:
        raise SystemExit('\n'.join(errors))
    print('AVR printf format check passed')
if __name__ == '__main__':
    main()
