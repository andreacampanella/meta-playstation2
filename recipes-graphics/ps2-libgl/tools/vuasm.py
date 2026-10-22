#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assembler for the vector unit 1 (VU1) programs of ps2gl.

One line holds one instruction pair: the upper instruction (float unit),
a vertical bar, and the lower instruction (integer unit, loads, branches).
A label is a name with a colon on its own line. A comment starts with ';'.
A symbol is "NAME = expression" on its own line. Immediates can use symbols.

    loop:
        MULAx.xyzw ACC, vf1, vf5     | LQ.xyzw vf5, 0(vi2)
        [E] NOP                      | NOP

[E] on the upper instruction sets the end bit: the program stops after the
next instruction. A float immediate goes in the lower slot as LOI 1.0 with
an "i" instruction in the upper slot (MULi, ADDi, ...).

Output: a C array of 32-bit words, lower word first, as ps2gl.c includes it.
The encoding follows the PCSX2 decode tables (VUops.cpp).

usage: vuasm.py program.vsm > program.h
"""
import re
import struct
import sys

DEST_BITS = {'x': 8, 'y': 4, 'z': 2, 'w': 1}

# Upper: opcode in the low 6 bits, three registers
UPPER_3 = {
    'ADD': 0x28, 'MADD': 0x29, 'MUL': 0x2A, 'MAX': 0x2B,
    'SUB': 0x2C, 'MSUB': 0x2D, 'OPMSUB': 0x2E, 'MINI': 0x2F,
}
# Broadcast forms: base + 0..3 for x, y, z, w
UPPER_BC = {
    'ADD': 0x00, 'SUB': 0x04, 'MADD': 0x08, 'MSUB': 0x0C,
    'MAX': 0x10, 'MINI': 0x14, 'MUL': 0x18,
}
UPPER_Q = {'MUL': 0x1C, 'ADD': 0x20, 'MADD': 0x21, 'SUB': 0x24, 'MSUB': 0x25}
UPPER_I = {'MAX': 0x1D, 'MUL': 0x1E, 'MINI': 0x1F, 'ADD': 0x22, 'MADD': 0x23,
           'SUB': 0x26, 'MSUB': 0x27}
# Special forms: (xx, row) -> opcode 0x3C + xx, row in bits 10-6
UPPER_SPECIAL = {
    'ADDAx': (0, 0), 'SUBAx': (0, 1), 'MADDAx': (0, 2), 'MSUBAx': (0, 3),
    'ITOF0': (0, 4), 'FTOI0': (0, 5), 'MULAx': (0, 6), 'MULAq': (0, 7),
    'ADDAq': (0, 8), 'SUBAq': (0, 9), 'ADDA': (0, 10), 'SUBA': (0, 11),
    'ADDAy': (1, 0), 'SUBAy': (1, 1), 'MADDAy': (1, 2), 'MSUBAy': (1, 3),
    'ITOF4': (1, 4), 'FTOI4': (1, 5), 'MULAy': (1, 6), 'ABS': (1, 7),
    'MADDAq': (1, 8), 'MSUBAq': (1, 9), 'MADDA': (1, 10), 'MSUBA': (1, 11),
    'ADDAz': (2, 0), 'SUBAz': (2, 1), 'MADDAz': (2, 2), 'MSUBAz': (2, 3),
    'ITOF12': (2, 4), 'FTOI12': (2, 5), 'MULAz': (2, 6), 'MULAi': (2, 7),
    'ADDAi': (2, 8), 'SUBAi': (2, 9), 'MULA': (2, 10), 'OPMULA': (2, 11),
    'ADDAw': (3, 0), 'SUBAw': (3, 1), 'MADDAw': (3, 2), 'MSUBAw': (3, 3),
    'ITOF15': (3, 4), 'FTOI15': (3, 5), 'MULAw': (3, 6), 'CLIP': (3, 7),
    'MADDAi': (3, 8), 'MSUBAi': (3, 9), 'NOP': (3, 11),
}

# Lower: opcode in bits 31-25
LOWER_TOP = {
    'LQ': 0x00, 'SQ': 0x01, 'ILW': 0x04, 'ISW': 0x05, 'IADDIU': 0x08, 'ISUBIU': 0x09,
    'FCEQ': 0x10, 'FCSET': 0x11, 'FCAND': 0x12, 'FCOR': 0x13,
    'FSEQ': 0x14, 'FSSET': 0x15, 'FSAND': 0x16, 'FSOR': 0x17,
    'FMEQ': 0x18, 'FMAND': 0x1A, 'FMOR': 0x1B, 'FCGET': 0x1C,
    'B': 0x20, 'BAL': 0x21, 'JR': 0x24, 'JALR': 0x25,
    'IBEQ': 0x28, 'IBNE': 0x29, 'IBLTZ': 0x2C, 'IBGTZ': 0x2D, 'IBLEZ': 0x2E, 'IBGEZ': 0x2F,
}
LOWER_OP = {'IADD': 0x30, 'ISUB': 0x31, 'IADDI': 0x32, 'IAND': 0x34, 'IOR': 0x35}
# (xx, row) -> opcode 0x3C + xx, row in bits 10-6
LOWER_T3 = {
    'MOVE': (0, 0xC), 'LQI': (0, 0xD), 'DIV': (0, 0xE), 'MTIR': (0, 0xF),
    'RNEXT': (0, 0x10), 'MFP': (0, 0x19), 'XTOP': (0, 0x1A), 'XGKICK': (0, 0x1B),
    'ESADD': (0, 0x1C), 'EATANxy': (0, 0x1D), 'ESQRT': (0, 0x1E), 'ESIN': (0, 0x1F),
    'MR32': (1, 0xC), 'SQI': (1, 0xD), 'SQRT': (1, 0xE), 'MFIR': (1, 0xF),
    'RGET': (1, 0x10), 'XITOP': (1, 0x1A), 'ERSADD': (1, 0x1C), 'EATANxz': (1, 0x1D),
    'ERSQRT': (1, 0x1E), 'EATAN': (1, 0x1F),
    'LQD': (2, 0xD), 'RSQRT': (2, 0xE), 'ILWR': (2, 0xF), 'RINIT': (2, 0x10),
    'ELENG': (2, 0x1C), 'ESUM': (2, 0x1D), 'ERCPR': (2, 0x1E), 'EEXP': (2, 0x1F),
    'SQD': (3, 0xD), 'WAITQ': (3, 0xE), 'ISWR': (3, 0xF), 'RXOR': (3, 0x10),
    'ERLENG': (3, 0x1C), 'WAITP': (3, 0x1E),
}
LOWER_NOP = 0x8000033C
UPPER_NOP = 0x000002FF


class Error(Exception):
    pass


def dest_mask(s):
    m = 0
    for c in s:
        if c not in DEST_BITS or m & DEST_BITS[c]:
            raise Error('bad destination .%s' % s)
        m |= DEST_BITS[c]
    return m


def vf(s):
    m = re.fullmatch(r'vf(\d+)', s)
    if not m or int(m.group(1)) > 31:
        raise Error('expected a vf register, got %s' % s)
    return int(m.group(1))


def vf_field(s):
    """vf6w -> (6, 3)"""
    m = re.fullmatch(r'vf(\d+)([xyzw])', s)
    if not m:
        raise Error('expected a vf register with a field, got %s' % s)
    return int(m.group(1)), 'xyzw'.index(m.group(2))


def vi(s):
    m = re.fullmatch(r'vi(\d+)', s)
    if not m or int(m.group(1)) > 15:
        raise Error('expected a vi register, got %s' % s)
    return int(m.group(1))


SYMBOLS = {}


def number(s):
    """An integer, or an expression of integers and symbols (+ - * / ( ))."""
    s = s.strip()
    try:
        return int(s, 0)
    except ValueError:
        pass
    if not re.fullmatch(r'[\w\s+\-*/()]+', s):
        raise Error('bad number %s' % s)
    try:
        return int(eval(s, {'__builtins__': {}}, dict(SYMBOLS)))
    except Exception:
        raise Error('bad number %s' % s)


def split_mnemonic(text):
    """'MULAx.xyzw ACC, vf1, vf5' -> ('MULAx', 'xyzw', ['ACC', 'vf1', 'vf5'])"""
    parts = text.strip().split(None, 1)
    head = parts[0]
    args = [a.strip() for a in parts[1].split(',')] if len(parts) > 1 else []
    if '.' in head:
        name, dest = head.split('.', 1)
    else:
        name, dest = head, ''
    return name, dest, args


def assemble_upper(text):
    """Return (word, i_bit_needed)."""
    e_bit = 0
    text = text.strip()
    if text.startswith('[E]'):
        e_bit = 1 << 30
        text = text[3:].strip()
    name, dest, args = split_mnemonic(text)
    if name == 'CLIPw':
        name = 'CLIP'
    d = dest_mask(dest) if dest else 0
    uses_i = name.endswith('i') and name not in ('MINI',)
    word = None

    if name == 'NOP':
        word = UPPER_NOP
    elif name in UPPER_SPECIAL and name not in ('NOP',):
        xx, row = UPPER_SPECIAL[name]
        base = 0x3C + xx | (row << 6)
        if name in ('ITOF0', 'ITOF4', 'ITOF12', 'ITOF15', 'FTOI0', 'FTOI4', 'FTOI12', 'FTOI15', 'ABS'):
            # ft = destination, fs = source
            word = base | (d << 21) | (vf(args[0]) << 16) | (vf(args[1]) << 11)
        elif name == 'CLIP':
            # CLIPw.xyz fs, ft: flags of fs.xyz against ft.w
            word = base | (0xE << 21) | (vf(args[1]) << 16) | (vf(args[0]) << 11)
        else:
            # ACC forms: ACC, fs, ft (ft may carry a field for the bc forms)
            if args[0] != 'ACC':
                raise Error('%s needs ACC as the first operand' % name)
            fs = vf(args[1])
            if name[-1] in 'xyzw' and name not in ('ADDA', 'SUBA', 'MADDA', 'MSUBA', 'MULA', 'OPMULA'):
                ft, f = vf_field(args[2])
                if 'xyzw'[f] != name[-1]:
                    raise Error('%s: the field of %s must be %s' % (name, args[2], name[-1]))
            elif name.endswith('q') or name.endswith('i'):
                ft = 0
            else:
                ft = vf(args[2])
            word = base | (d << 21) | (ft << 16) | (fs << 11)
    else:
        # fd, fs, ft forms
        fd = vf(args[0])
        fs = vf(args[1])
        if name in UPPER_3 and len(args) == 3 and re.fullmatch(r'vf\d+', args[2]):
            word = UPPER_3[name] | (d << 21) | (vf(args[2]) << 16) | (fs << 11) | (fd << 6)
        elif name[:-1] in UPPER_BC and name[-1] in 'xyzw':
            ft, f = vf_field(args[2])
            if 'xyzw'[f] != name[-1]:
                raise Error('%s: the field of %s must be %s' % (name, args[2], name[-1]))
            word = (UPPER_BC[name[:-1]] + f) | (d << 21) | (ft << 16) | (fs << 11) | (fd << 6)
        elif name[:-1] in UPPER_Q and name[-1] == 'q':
            word = UPPER_Q[name[:-1]] | (d << 21) | (fs << 11) | (fd << 6)
        elif name[:-1] in UPPER_I and name[-1] == 'i':
            word = UPPER_I[name[:-1]] | (d << 21) | (fs << 11) | (fd << 6)
        else:
            raise Error('unknown upper instruction %s' % name)
    return word | e_bit, uses_i


def assemble_lower(text, pc, labels):
    text = text.strip()
    name, dest, args = split_mnemonic(text)
    d = dest_mask(dest) if dest else 0

    def mem(arg):
        """'3(vi2)' -> (3, 2); '(vi2)' -> (0, 2)"""
        m = re.fullmatch(r'(.*)\((vi\d+)\)', arg)
        if not m:
            raise Error('expected imm(vi), got %s' % arg)
        return (number(m.group(1)) if m.group(1) else 0), vi(m.group(2))

    def imm11(v):
        if v < -1024 or v > 1023:
            raise Error('immediate %d does not fit 11 bits' % v)
        return v & 0x7FF

    def branch_target(arg):
        if arg not in labels:
            raise Error('unknown label %s' % arg)
        return imm11(labels[arg] - (pc + 1))

    if name == 'NOP':
        return LOWER_NOP, None
    if name == 'LOI':
        return struct.unpack('<I', struct.pack('<f', float(args[0])))[0], 'LOI'
    if name == 'LQ':
        off, base = mem(args[1])
        return (LOWER_TOP[name] << 25) | (d << 21) | (vf(args[0]) << 16) | (base << 11) | imm11(off), None
    if name == 'SQ':
        # The store form: the data register is fs, the base is it
        off, base = mem(args[1])
        return (LOWER_TOP[name] << 25) | (d << 21) | (base << 16) | (vf(args[0]) << 11) | imm11(off), None
    if name in ('ILW', 'ISW'):
        off, base = mem(args[1])
        return (LOWER_TOP[name] << 25) | (d << 21) | (vi(args[0]) << 16) | (base << 11) | imm11(off), None
    if name in ('IADDIU', 'ISUBIU'):
        v = number(args[2])
        if v < 0 or v > 0x7FFF:
            raise Error('immediate %d does not fit 15 bits' % v)
        return (LOWER_TOP[name] << 25) | ((v >> 11) << 21) | (vi(args[0]) << 16) | (vi(args[1]) << 11) | (v & 0x7FF), None
    if name in ('FCAND', 'FCOR', 'FCEQ', 'FCSET'):
        v = number(args[-1])
        return (LOWER_TOP[name] << 25) | (v & 0xFFFFFF), None
    if name == 'FCGET':
        return (LOWER_TOP[name] << 25) | (vi(args[0]) << 16), None
    if name in ('FMAND', 'FMOR', 'FMEQ', 'FSAND', 'FSOR', 'FSEQ'):
        return (LOWER_TOP[name] << 25) | (vi(args[0]) << 16) | (vi(args[1]) << 11), None
    if name in ('B', 'BAL'):
        word = (LOWER_TOP[name] << 25) | branch_target(args[-1])
        if name == 'BAL':
            word |= vi(args[0]) << 16
        return word, None
    if name in ('JR', 'JALR'):
        word = (LOWER_TOP[name] << 25) | (vi(args[-1]) << 11)
        if name == 'JALR':
            word |= vi(args[0]) << 16
        return word, None
    if name in ('IBEQ', 'IBNE'):
        return (LOWER_TOP[name] << 25) | (vi(args[0]) << 16) | (vi(args[1]) << 11) | branch_target(args[2]), None
    if name in ('IBLTZ', 'IBGTZ', 'IBLEZ', 'IBGEZ'):
        return (LOWER_TOP[name] << 25) | (vi(args[0]) << 11) | branch_target(args[1]), None
    base = 0x40 << 25
    if name in ('IADD', 'ISUB', 'IAND', 'IOR'):
        return base | LOWER_OP[name] | (vi(args[2]) << 16) | (vi(args[1]) << 11) | (vi(args[0]) << 6), None
    if name == 'IADDI':
        v = number(args[2])
        if v < -16 or v > 15:
            raise Error('immediate %d does not fit 5 bits' % v)
        return base | LOWER_OP[name] | (vi(args[1]) << 11) | (vi(args[0]) << 16) | ((v & 0x1F) << 6), None
    if name in LOWER_T3:
        xx, row = LOWER_T3[name]
        word = base | (0x3C + xx) | (row << 6)
        if name in ('MOVE', 'MR32'):
            return word | (d << 21) | (vf(args[0]) << 16) | (vf(args[1]) << 11), None
        if name in ('LQI', 'LQD'):
            m = re.fullmatch(r'\((vi\d+)(\+\+|--)?\)', args[1])
            if not m:
                raise Error('expected (vi++) or (vi--), got %s' % args[1])
            return word | (d << 21) | (vf(args[0]) << 16) | (vi(m.group(1)) << 11), None
        if name in ('SQI', 'SQD'):
            m = re.fullmatch(r'\((vi\d+)(\+\+|--)?\)', args[1])
            if not m:
                raise Error('expected (vi++) or (vi--), got %s' % args[1])
            return word | (d << 21) | (vf(args[0]) << 11) | (vi(m.group(1)) << 16), None
        if name in ('DIV', 'SQRT', 'RSQRT'):
            # DIV Q, vf0w, vf6w. SQRT Q, vf6w. RSQRT Q, vf0w, vf6w
            if name == 'SQRT':
                ft, ftf = vf_field(args[1])
                return word | (ftf << 23) | (ft << 16), None
            fs, fsf = vf_field(args[1])
            ft, ftf = vf_field(args[2])
            return word | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11), None
        if name in ('EEXP', 'ESQRT', 'ERSQRT', 'ERCPR', 'ESIN', 'EATAN', 'ESADD', 'ERSADD',
                    'ELENG', 'ERLENG', 'ESUM', 'EATANxy', 'EATANxz'):
            # P, vf1x (the field is ignored by the vector forms)
            m = re.fullmatch(r'vf(\d+)([xyzw]?)', args[1])
            fs = int(m.group(1))
            fsf = 'xyzw'.index(m.group(2)) if m.group(2) else 0
            return word | (fsf << 21) | (fs << 11), None
        if name == 'MFP':
            return word | (d << 21) | (vf(args[0]) << 16), None
        if name == 'MTIR':
            fs, fsf = vf_field(args[1])
            return word | (fsf << 21) | (vi(args[0]) << 16) | (fs << 11), None
        if name == 'MFIR':
            return word | (d << 21) | (vf(args[0]) << 16) | (vi(args[1]) << 11), None
        if name == 'XGKICK':
            return word | (vi(args[0]) << 11), None
        if name in ('XTOP', 'XITOP'):
            return word | (vi(args[0]) << 16), None
        if name in ('ILWR', 'ISWR'):
            m = re.fullmatch(r'\((vi\d+)\)', args[1])
            return word | (d << 21) | (vi(args[0]) << 16) | (vi(m.group(1)) << 11), None
        return word, None
    raise Error('unknown lower instruction %s' % name)


def assemble(source):
    lines = []
    labels = {}
    pc = 0
    for lineno, raw in enumerate(source.splitlines(), 1):
        line = raw.split(';', 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r'(\w+):', line)
        if m:
            labels[m.group(1)] = pc
            continue
        m = re.fullmatch(r'(\w+)\s*=\s*(.+)', line)
        if m and '|' not in line:
            try:
                SYMBOLS[m.group(1)] = number(m.group(2))
            except Error as e:
                raise Error('line %d: %s' % (lineno, e))
            continue
        if '|' not in line:
            raise Error('line %d: no "|" between the upper and the lower instruction' % lineno)
        upper, lower = line.split('|', 1)
        lines.append((lineno, upper, lower, pc))
        pc += 1

    words = []
    for lineno, upper, lower, pc in lines:
        try:
            u, uses_i = assemble_upper(upper)
            l, kind = assemble_lower(lower, pc, labels)
            if kind == 'LOI':
                u |= 1 << 31
            elif uses_i:
                raise Error('%s needs LOI in the lower slot' % upper.strip())
        except Error as e:
            raise Error('line %d: %s' % (lineno, e))
        words.append((l, u, lineno, upper.strip(), lower.strip()))
    return words, labels


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    source = open(sys.argv[1]).read()
    try:
        words, labels = assemble(source)
    except Error as e:
        print('vuasm: %s' % e, file=sys.stderr)
        sys.exit(1)
    print('/* SPDX-License-Identifier: MIT */')
    print('/* Generated by vuasm.py from %s. Do not edit. */' % sys.argv[1])
    print('/* %d instructions. Labels:%s */' % (
        len(words), ''.join(' %s=%d' % (k, v) for k, v in sorted(labels.items(), key=lambda kv: kv[1]))))
    for k, v in sorted(labels.items()):
        print('#define VU_LABEL_%s %d' % (k.upper(), v))
    for k, v in sorted(SYMBOLS.items()):
        print('#define VU_%s %d' % (k.upper(), v))
    print('#define VU_PROGRAM_WORDS %d' % (2 * len(words)))
    print('static const uint32_t vu_program_words[%d] = {' % (2 * len(words)))
    for i, (l, u, lineno, us, ls) in enumerate(words):
        print('\t0x%08x, 0x%08x,\t/* %3d: %-34s | %s */' % (l, u, i, us, ls))
    print('};')


if __name__ == '__main__':
    main()
