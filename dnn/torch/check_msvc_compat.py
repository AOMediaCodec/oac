#!/usr/bin/env python3
"""Simple linter to check generated dnn C sources for MSVC compatibility.

Checks performed:
 - No designated initializers (".field = ...")
 - No obvious compound literals like "(type[]){...}"
 - Arrays are declared with an allowed dtype (prefer oac_ types)
 - `WEIGHT_TYPE_uint8` exists in dnn/nnet.h

Exit code is non-zero if issues are found.
"""

import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
DNN_DIR = os.path.join(ROOT, 'dnn')
NNET_H = os.path.join(DNN_DIR, 'nnet.h')

ALLOWED_DTYPES = set([
    'oac_uint8', 'oac_int8', 'oac_uint16', 'oac_int16',
    'float', 'int', 'qweight'
])

# regexes
RE_DESIGNATED = re.compile(r"\.[A-Za-z_][A-Za-z0-9_]*\s*=")
RE_COMPOUND = re.compile(r"\([A-Za-z_][A-Za-z0-9_]*\s*\[\s*\]\)\s*\{")
RE_ARRAY_DECL = re.compile(r"\b(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[.*\]\s*=")


def check_nnet_has_uint8():
    if not os.path.exists(NNET_H):
        print(f"ERROR: {NNET_H} not found")
        return False
    with open(NNET_H, 'r', encoding='utf-8') as f:
        data = f.read()
    if 'WEIGHT_TYPE_uint8' not in data:
        print('ERROR: WEIGHT_TYPE_uint8 not found in dnn/nnet.h')
        return False
    return True


def check_file(path, rel):
    issues = []
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    # Only inspect files that look like generated weight files
    if '#define WEIGHTS_' not in text and not rel.endswith('_data.c') and not rel.endswith('_data.h'):
        return issues

    if RE_DESIGNATED.search(text):
        issues.append('designated initializer (e.g., ".field = value") found')
    if RE_COMPOUND.search(text):
        issues.append('compound literal pattern (e.g., "(type[]){...") found')

    for m in RE_ARRAY_DECL.finditer(text):
        dtype = m.group(1)
        varname = m.group(2)
        # Only enforce dtypes on arrays that look like weight arrays
        if 'weights' not in varname and not varname.endswith('_int8') and not varname.endswith('_float') and '_quant_scales_' not in varname:
            continue
        if dtype not in ALLOWED_DTYPES:
            issues.append(f'unexpected array dtype "{dtype}" for "{varname}"; prefer oac_* types or float/int/qweight')

    return issues


def main():
    ok = True

    if not check_nnet_has_uint8():
        ok = False

    # Inspect generated dnn files
    for root, dirs, files in os.walk(DNN_DIR):
        # skip python and model files
        for fn in files:
            if not fn.endswith(('.c', '.h')):
                continue
            path = os.path.join(root, fn)
            # skip the generator source files themselves
            if path.endswith('export_rdovae_weights.py'):
                continue
            rel = os.path.relpath(path, ROOT)
            issues = check_file(path, rel)
            if issues:
                ok = False
                print(f'File: {rel}')
                for iss in issues:
                    print(f'  - {iss}')

    if not ok:
        print('\nMSVC compatibility linter failed. See messages above.')
        sys.exit(1)
    else:
        print('MSVC compatibility linter passed.')


if __name__ == '__main__':
    main()
