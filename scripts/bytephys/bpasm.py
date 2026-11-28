#!/usr/bin/env python3
"""bpasm.py -- assembler for the BytePhys spike (srcs/shared/include/BytePhys.h).

Reads *.bp sources and writes srcs/shared/include/BytePrograms.h: ONE
image (BytePhys.h 'IMAGES, FUNCTIONS, CHUNKS') holding every file's
functions cut into chunks of at most 256 bytes, and per file a Program
(entry functions: fb1.bp -> BP_FUNC_FB1, ...; the image is one BCP blob, BP_BCP).  The opcode and field numbers are parsed from
BytePhys.h itself, so the header stays the one source of truth.

Source syntax: one instruction per line, `;` starts a comment, `NAME:`
defines a label.  Instructions are the Op names without OP_, lower case
(push, dup, add, jz, getf, ...).  `push N` assembles as PUSH8 when N <
256, else PUSH32.  Jumps and calls take a label: a jump to a label in
the same chunk is jmp/jz/jnz OFFSET8, else xjmp/xjz/xjnz CHUNK16 OFFSET8;
a call is CALL FUNCTION16 (its target label starts a function).
getf/setf take a raw field: type, data0, stg0, stg1.

Declarations (global: every .bp file sees every other file's):

  quark NAME                start a quark
  element NAME TYPE         start an element of atom type TYPE
    unsigned NAME BITS      a member, 1..32 bits
    bool NAME               a 1-bit member
    QUARKNAME NAME          a member that is a whole quark

Members pack in order from state bit 0 (BytePhys.h: bits 0..31 are
mStg[0], 32..63 mStg[1], 64..79 mData0); an element must fit in 80 bits.
A declaration ends at the first line that is not a member line.

Frames and calls:

  func NAME ARG... [| LOCAL...]   label NAME, then `enter nargs nlocals`;
                                  the names stand for slots 0, 1, ...
                                  until the next func
  enter NARGS NLOCALS             the raw form
  loadl SLOT / storel SLOT        SLOT is a number or a func name
  call NAME / ret                 results come back on the operand stack

The program starts at offset 0, so its first func (or enter) is the
entry; a program needing no locals needs no enter.

getm/setm take a member path: ELEMENT.member[.member...] (that element
only) or QUARK.member[...] (any element holding that quark, wherever it
sits in it).  Each distinct path gets a member id; the ids and where each
element type keeps them go into one Layout shared by all programs.  A .bp
file with only declarations makes no program.

Usage:
  bpasm.py                 regenerate BytePrograms.h from every *.bp here
  bpasm.py --check         exit 1 if BytePrograms.h is out of date
  bpasm.py --list [tests|bench]   print a set's image listing
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
HDR = os.path.join(ROOT, "srcs", "shared", "include", "BytePhys.h")
OUT = os.path.join(ROOT, "srcs", "shared", "include", "BytePrograms.h")


def parse_enum(text, enum_name, prefix):
    """Sequential enum values (first may be '= 0') of `enum NAME : u8`."""
    m = re.search(r"enum\s+" + enum_name + r"\s*:\s*u8\s*\{(.*?)\};", text,
                  re.S)
    if not m:
        raise SystemExit(f"bpasm: enum {enum_name} not found in {HDR}")
    out = {}
    n = 0
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        mm = re.match(r"(" + prefix + r"\w+)\s*(?:=\s*(\d+))?$", line)
        if not mm:
            continue
        if mm.group(2) is not None:
            n = int(mm.group(2))
        out[mm.group(1)[len(prefix):].lower()] = n
        n += 1
    return out


def load_tables():
    text = open(HDR).read()
    ops = parse_enum(text, "Op", "OP_")
    fields = parse_enum(text, "Field", "F_")
    ops.pop("count", None)
    fields.pop("count", None)
    return ops, fields


JUMPS = ("jmp", "jz", "jnz", "call")
MEMBER_OPS = ("getm", "setm")
STATE_BITS = 80
MAX_MEMBER_BITS = 32


def strip(raw):
    return raw.split(";")[0].strip()


class Decls:
    """Every quark and element declared in a set of .bp files, plus the
    member ids handed out to getm/setm paths (in first-use order)."""

    def __init__(self):
        self.quarks = {}        # name -> [(member, kind, bits|quark)]
        self.elements = {}      # name -> (type, [(member, kind, arg)])
        self.where = {}         # name -> "file:line"
        self.ids = {}           # path -> member id
        self.ats = []           # per id: [(type, pos, len)]

    def parse(self, path):
        """Pull the declarations out of path; return its other lines as
        [(line number, text)]."""
        rest = []
        cur = None              # member list being filled
        for n, raw in enumerate(open(path), 1):
            s = strip(raw)
            if not s:
                continue
            w = s.split()
            if w[0] in ("quark", "element"):
                want = 2 if w[0] == "quark" else 3
                if len(w) != want:
                    raise SystemExit(f"{path}:{n}: '{w[0]} NAME"
                                     f"{'' if want == 2 else ' TYPE'}'")
                name = w[1]
                if name in self.quarks or name in self.elements:
                    raise SystemExit(f"{path}:{n}: {name} declared twice "
                                     f"(also {self.where[name]})")
                self.where[name] = f"{path}:{n}"
                cur = []
                if w[0] == "quark":
                    self.quarks[name] = cur
                else:
                    t = int(w[2], 0)
                    if not 0 < t < (1 << 15):
                        raise SystemExit(f"{path}:{n}: bad type {t}")
                    self.elements[name] = (t, cur)
                continue
            if cur is not None and self._member(path, n, w, cur):
                continue
            cur = None
            rest.append((n, s))
        return rest

    def _member(self, path, n, w, cur):
        """Add a member line to cur; False if w is not a member line."""
        if w[0] == "unsigned" and len(w) == 3:
            bits = int(w[2], 0)
            if not 1 <= bits <= MAX_MEMBER_BITS:
                raise SystemExit(f"{path}:{n}: unsigned width 1.."
                                 f"{MAX_MEMBER_BITS}")
            item = (w[1], "u", bits)
        elif w[0] == "bool" and len(w) == 2:
            item = (w[1], "u", 1)
        elif (len(w) == 2 and w[0][:1].isupper() and
              not w[0].endswith(":") and not w[1].endswith(":")):
            item = (w[1], "q", w[0])
        else:
            return False
        if any(m[0] == item[0] for m in cur):
            raise SystemExit(f"{path}:{n}: member {item[0]} twice")
        cur.append(item)
        return True

    def size(self, quark, seen=()):
        if quark not in self.quarks:
            raise SystemExit(f"bpasm: no quark {quark}")
        if quark in seen:
            raise SystemExit(f"bpasm: quark {quark} contains itself")
        return sum(a if k == "u" else self.size(a, seen + (quark,))
                   for _, k, a in self.quarks[quark])

    def check(self):
        for name, (t, members) in self.elements.items():
            bits = sum(a if k == "u" else self.size(a)
                       for _, k, a in members)
            if bits > STATE_BITS:
                raise SystemExit(f"{self.where[name]}: element {name} is "
                                 f"{bits} bits (max {STATE_BITS})")
        types = [t for t, _ in self.elements.values()]
        if len(set(types)) != len(types):
            raise SystemExit("bpasm: two elements share a type number")
        for q in self.quarks:
            self.size(q)

    def _walk(self, members, names, pos):
        """(pos, len) of the scalar member path names in members."""
        at = pos
        for m, k, a in members:
            if m == names[0]:
                if k == "u":
                    if len(names) != 1:
                        return None
                    return (at, a)
                if len(names) == 1:
                    return None         # a whole quark is not a scalar
                return self._walk(self.quarks[a], names[1:], at)
            at += a if k == "u" else self.size(a)
        return None

    def _quark_at(self, members, quark, pos, out):
        """Collect every start bit where quark sits inside members."""
        at = pos
        for _, k, a in members:
            if k == "q":
                if a == quark:
                    out.append(at)
                self._quark_at(self.quarks[a], quark, at, out)
            at += a if k == "u" else self.size(a)

    def member_id(self, path, where):
        if path in self.ids:
            return self.ids[path]
        names = path.split(".")
        if len(names) < 2:
            raise SystemExit(f"{where}: member path TYPE.member, not {path}")
        root, rest = names[0], names[1:]
        ats = []
        if root in self.elements:
            t, members = self.elements[root]
            r = self._walk(members, rest, 0)
            if r is None:
                raise SystemExit(f"{where}: {root} has no member "
                                 f"{'.'.join(rest)}")
            ats.append((t, r[0], r[1]))
        elif root in self.quarks:
            r = self._walk(self.quarks[root], rest, 0)
            if r is None:
                raise SystemExit(f"{where}: quark {root} has no member "
                                 f"{'.'.join(rest)}")
            for ename, (t, members) in self.elements.items():
                starts = []
                self._quark_at(members, root, 0, starts)
                if len(starts) > 1:
                    raise SystemExit(f"{where}: {ename} holds {root} "
                                     f"{len(starts)} times: name it "
                                     f"through {ename}")
                if starts:
                    ats.append((t, starts[0] + r[0], r[1]))
            if not ats:
                raise SystemExit(f"{where}: no element holds quark {root}")
        else:
            raise SystemExit(f"{where}: no element or quark {root}")
        self.ids[path] = len(self.ats)
        self.ats.append(sorted(ats))
        if len(self.ats) >= 1 << 16:
            raise SystemExit("bpasm: too many members")
        return self.ids[path]

    def describe(self):
        out = []
        for name, (t, members) in self.elements.items():
            out.append(f"element {name} (type {t}):")
            self._describe(members, 0, "  ", out)
        for path, i in self.ids.items():
            out.append(f"member {i} {path}: " + ", ".join(
                f"type {t} bits {p}..{p + n - 1}" for t, p, n in self.ats[i]))
        return out

    def _describe(self, members, pos, ind, out):
        at = pos
        for m, k, a in members:
            if k == "u":
                out.append(f"{ind}{m}: bits {at}..{at + a - 1}")
                at += a
            else:
                out.append(f"{ind}{m} ({a}): bits {at}..{at + self.size(a) - 1}")
                self._describe(self.quarks[a], at, ind + "  ", out)
                at += self.size(a)


def load_decls(paths):
    """Parse every path's declarations; -> (Decls, {path: other lines})."""
    d = Decls()
    rest = {p: d.parse(p) for p in paths}
    d.check()
    return d, rest


CHUNK_MAX = 256                 # BytePhys.h CHUNK_MAX
LINK_BYTES = 4                  # an appended xjmp: op, u16 chunk, u8 off
TERMINAL = ("halt0", "halt1", "jmp", "ret")


def parse_items(path, lines, ops):
    """path's instruction lines -> ([(n, src, words, labels)], {label:
    index}, [function start labels]).  `func` becomes its label plus
    enter; loadl/storel local names become slot numbers."""
    items = []
    labels = {}
    funcs = []
    slots = {}                  # the current func's local names
    pending = []                # labels waiting for their instruction
    for n, s in lines:
        words = s.split()
        if words[0] == "func":
            if len(words) < 2 or words[1].endswith(":"):
                raise SystemExit(f"{path}:{n}: func NAME ARG... [| LOCAL...]")
            name = words[1]
            rest = words[2:]
            args = rest[:rest.index("|")] if "|" in rest else rest
            locs = rest[rest.index("|") + 1:] if "|" in rest else []
            names = args + locs
            if len(set(names)) != len(names) or len(names) > 255:
                raise SystemExit(f"{path}:{n}: func local names must be "
                                 f"distinct and at most 255")
            if name in labels or name in pending:
                raise SystemExit(f"{path}:{n}: label {name} twice")
            pending.append(name)
            funcs.append(name)
            slots = {x: i for i, x in enumerate(names)}
            words = ["enter", str(len(args)), str(len(names))]
            for lab in pending:
                labels[lab] = len(items)
            items.append((n, s, words, pending))
            pending = []
            continue
        while ":" in s:
            lab, s = s.split(":", 1)
            lab = lab.strip()
            if lab in labels or lab in pending:
                raise SystemExit(f"{path}:{n}: label {lab} twice")
            pending.append(lab)
            s = s.strip()
        if not s:
            continue
        words = s.split()
        if words[0] not in ops and words[0] != "push":
            raise SystemExit(f"{path}:{n}: unknown op {words[0]}")
        if words[0] in ("loadl", "storel") and len(words) == 2 and \
           words[1] in slots:
            words = [words[0], str(slots[words[1]])]
        for lab in pending:
            labels[lab] = len(items)
        items.append((n, s, words, pending))
        pending = []
    if pending:
        raise SystemExit(f"{path}: label(s) {', '.join(pending)} at the end "
                         "label nothing")
    return items, labels, funcs


def insn_size(words, short=False):
    """Bytes of one instruction; jumps as the short (in-chunk) form if
    short, else the cross-chunk form."""
    op = words[0]
    if op == "push":
        return 2 if int(words[1], 0) < 256 else 5
    if op in ("jmp", "jz", "jnz"):
        return 2 if short else 4
    if op == "call" or op in MEMBER_OPS or op == "enter":
        return 3
    if op in ("getf", "setf", "loadl", "storel"):
        return 2
    return 1


def assemble_image(files, ops, fields, decls):
    """files: [(path, lines)] of one set.  -> (code bytes, chunk starts,
    function first chunks, function names, {program name: function},
    listing lines).

    Every file's code goes into ONE image.  Its functions are its first
    instruction (the program's entry), every call target and every
    `func`; each runs to the next.  A function is cut into chunks of at
    most CHUNK_MAX bytes, preferring a cut at a label (a basic block
    start), with room left for an xjmp to the next chunk where control
    would fall through.  A jump to a label in the same chunk assembles
    short (jmp/jz/jnz OFF8), else as xjmp/xjz/xjnz CHUNK16 OFF8."""
    chunks = []                 # [(path, [item index, ...])]
    funcs = []                  # [(name, first chunk)]
    progs = {}
    perfile = []
    for path, lines in files:
        items, labels, fnames = parse_items(path, lines, ops)
        if not items:
            continue
        starts = {0}
        for _, _, w, _ in items:
            if w[0] == "call":
                if len(w) != 2 or w[1] not in labels:
                    raise SystemExit(f"{path}: call needs a known label "
                                     f"({' '.join(w)})")
                starts.add(labels[w[1]])
        for f in fnames:
            starts.add(labels[f])
        starts = sorted(starts)
        fidx = {}               # item index -> function number
        chunk_of = {}           # item index -> chunk number
        file_chunks = []
        for k, st in enumerate(starts):
            end = starts[k + 1] if k + 1 < len(starts) else len(items)
            fidx[st] = len(funcs)
            name = next((l for l in items[st][3]), None) or \
                os.path.basename(path)[:-3]
            funcs.append((f"{os.path.basename(path)[:-3]}:{name}",
                          len(chunks)))
            cur = []
            size = 0
            for i in range(st, end):
                sz = insn_size(items[i][2])
                if cur and size + sz + LINK_BYTES > CHUNK_MAX:
                    # cut at the last label in cur, if not at its start
                    cut = max((j for j, x in enumerate(cur)
                               if j > 0 and items[x][3]), default=None)
                    keep = cur if cut is None else cur[:cut]
                    carry = [] if cut is None else cur[cut:]
                    for x in keep:
                        chunk_of[x] = len(chunks)
                    chunks.append((path, keep))
                    file_chunks.append(len(chunks) - 1)
                    cur = carry
                    size = sum(insn_size(items[x][2]) for x in cur)
                cur.append(i)
                size += sz
            for x in cur:
                chunk_of[x] = len(chunks)
            chunks.append((path, cur))
            file_chunks.append(len(chunks) - 1)
        progs[os.path.basename(path)[:-3]] = fidx[0]
        perfile.append((path, items, labels, fidx, chunk_of, starts))
    # encode every chunk
    code = []
    cstarts = []
    listing = []
    for path, items, labels, fidx, chunk_of, starts in perfile:
        mine = [c for c in range(len(chunks)) if chunks[c][0] == path]
        # pass 1: every instruction's offset in its chunk (final forms),
        # and each chunk's fall-through link
        off = {}
        links = {}
        ends = {}
        for c in mine:
            idxs = chunks[c][1]
            last = items[idxs[-1]][2][0]
            link = None
            if last not in TERMINAL:
                nxt = idxs[-1] + 1
                if nxt >= len(items):
                    raise SystemExit(f"{path}:{items[idxs[-1]][0]}: control "
                                     "runs off the end of the program")
                link = chunk_of[nxt]
            pc = 0
            for x in idxs:
                off[x] = pc
                w = items[x][2]
                short = w[0] in ("jmp", "jz", "jnz") and \
                    w[1] in labels and chunk_of[labels[w[1]]] == c
                pc += insn_size(w, short)
            if pc + (LINK_BYTES if link is not None else 0) > CHUNK_MAX:
                raise SystemExit(f"{path}: chunk {c} is over {CHUNK_MAX} "
                                 "bytes")
            links[c] = link
            ends[c] = pc
        # pass 2: the bytes
        for c in mine:
            cstarts.append(len(code))
            listing.append(f"-- chunk {c} ({os.path.basename(path)}) --")
            for x in chunks[c][1]:
                n, src, w, labs = items[x]
                b = encode(path, n, w, ops, fields, decls, labels,
                           chunk_of, off, c, fidx)
                listing.append(f"{off[x]:4d}: "
                               f"{' '.join(f'{v:02x}' for v in b):<16} {src}")
                code += b
            link = links[c]
            if link is not None:
                b = [ops["xjmp"], link & 0xff, link >> 8, 0]
                listing.append(f"{ends[c]:4d}: "
                               f"{' '.join(f'{v:02x}' for v in b):<16} "
                               f"(xjmp chunk {link}: falls through)")
                code += b
    cstarts.append(len(code))
    if len(code) >= 1 << 16:
        raise SystemExit("bpasm: image over 64 KB")
    return code, cstarts, funcs, progs, listing


def encode(path, n, w, ops, fields, decls, labels, chunk_of, off, c, fidx):
    op = w[0]
    if op == "push":
        v = int(w[1], 0)
        if not 0 <= v < (1 << 32):
            raise SystemExit(f"{path}:{n}: push out of range")
        if v < 256:
            return [ops["push8"], v]
        return [ops["push32"]] + [(v >> k) & 0xff for k in (0, 8, 16, 24)]
    if op in ("jmp", "jz", "jnz"):
        if len(w) != 2 or w[1] not in labels:
            raise SystemExit(f"{path}:{n}: {op} needs a known label")
        t = labels[w[1]]
        tc = chunk_of[t]
        if tc == c:
            return [ops[op], off[t]]
        # the target starts its chunk or sits inside it: its offset there
        return [ops["x" + op], tc & 0xff, tc >> 8, off[t]]
    if op == "call":
        f = fidx[labels[w[1]]]
        return [ops["call"], f & 0xff, f >> 8]
    if op in ("getf", "setf"):
        if len(w) != 2 or w[1] not in fields:
            raise SystemExit(f"{path}:{n}: {op} needs a field "
                             f"({', '.join(fields)})")
        return [ops[op], fields[w[1]]]
    if op in ("loadl", "storel", "enter"):
        want = 3 if op == "enter" else 2
        try:
            vals = [int(x, 0) for x in w[1:]]
        except ValueError:
            raise SystemExit(f"{path}:{n}: {op}: unknown local {w[1:]}")
        if len(w) != want or not all(0 <= v < 256 for v in vals):
            raise SystemExit(f"{path}:{n}: {op} takes "
                             f"{want - 1} number(s) 0..255")
        return [ops[op]] + vals
    if op in MEMBER_OPS:
        if len(w) != 2:
            raise SystemExit(f"{path}:{n}: {op} needs a member path")
        i = decls.member_id(w[1], f"{path}:{n}")
        return [ops[op], i & 0xff, i >> 8]
    if len(w) != 1:
        raise SystemExit(f"{path}:{n}: {op} takes no operand")
    return [ops[op]]


NO_FUNC = 0xffff
BCP_MAGIC = b"BCP1"
BCP_VERSION = 1            # BytePhys.h BCP_VERSION (verifyBCP checks it)
BCP_HEADER_BYTES = 64


def ops_count():
    """OP_COUNT from BytePhys.h (the blob header records it)."""
    ops = parse_enum(open(HDR).read(), "Op", "OP_")
    return ops["count"]


def bcp_checksum(blob):
    """FNV-1a over the blob, bytes 12..15 (the checksum) read as 0
    (BytePhys.h bcpChecksum)."""
    h = 2166136261
    for i, b in enumerate(blob):
        h = ((h ^ (0 if 12 <= i < 16 else b)) * 16777619) & 0xffffffff
    return h


def pack_bcp(code, cstarts, fchunks, refs, ats, first, tfunc, opcount):
    """One BCP blob (BytePhys.h 'BCP'): header, then the code and the
    tables, each padded to 4 bytes."""
    import struct
    body = bytearray()
    offs = []

    def table(data):
        offs.append(BCP_HEADER_BYTES + len(body))
        body.extend(data)
        while len(body) % 4:
            body.append(0)
    table(bytes(code))
    table(struct.pack(f"<{len(cstarts)}H", *cstarts))
    table(struct.pack(f"<{len(fchunks)}H", *fchunks))
    table(b"".join(struct.pack("<HH", a, n) for a, n in refs))
    table(b"".join(struct.pack("<HBB", t, p, n) for t, p, n in ats))
    table(struct.pack(f"<{len(tfunc)}H", *tfunc))
    size = BCP_HEADER_BYTES + len(body)
    hdr = BCP_MAGIC + struct.pack(
        "<HHII6H6I3I", BCP_VERSION, opcount, size, 0,
        len(cstarts) - 1, len(fchunks), len(refs), len(ats), first,
        len(tfunc), *offs, 0, 0, 0)
    assert len(hdr) == BCP_HEADER_BYTES
    blob = bytearray(hdr + body)
    blob[12:16] = struct.pack("<I", bcp_checksum(blob))
    return bytes(blob)


def render(progs, decls, srcdir=HERE, outpath=OUT, inner=None,
           image=None):
    """progs: [(name, entry function)]; image: assemble_image's (code,
    chunk starts, functions, listing) or None; inner: a namespace nested
    in BytePhys (the test physics)."""
    out = ["#pragma once  /* -*- C++ -*- */",
           f"/* {os.path.basename(outpath)} -- GENERATED by "
           f"scripts/bytephys/bpasm.py from",
           f"   the .bp files in {os.path.relpath(srcdir, ROOT)}; do not "
           f"edit (bpasm.py --check tells",
           "   whether it is current).  Programs for BytePhys.h. */",
           '#include "BytePhys.h"',
           "",
           "namespace MFM {",
           "  namespace BytePhys {"]
    if inner:
        out.append(f"  namespace {inner} {{")
    out.append("")
    for name, (t, _) in decls.elements.items():
        out.append(f"    static constexpr u16 BP_TYPE_{name.upper()} = {t}u;")
    out.append("")
    for line in decls.describe():
        out.append(f"    // {line}")
    refs, ats = [], []
    for per in decls.ats:
        refs.append((len(ats), len(per)))
        ats += list(per)
    # type number -> entry function: an element's program is the .bp
    # file named after it in lower case (ulamlite.py writes them that way)
    entry = {n.lower(): f for n, f in progs}
    types = {t: n for n, (t, _) in decls.elements.items()}
    first = min(types) if types else 0
    count = max(types) - first + 1 if types else 0
    tfunc = [entry.get(types.get(t, "").lower(), NO_FUNC)
             for t in range(first, first + count)]
    out.append("")
    out.append(f"    static constexpr u16 BP_FIRST_TYPE = {first}u;")
    out.append(f"    static constexpr u16 BP_TYPE_COUNT = {count}u;")
    # entry functions in source order, with names (the microbenchmark
    # set, ByteBench.h, runs them all)
    out.append("")
    for name, f in progs:
        out.append(f"    static constexpr u16 BP_FUNC_{name.upper()} = {f}u;")
    out.append(f"    static constexpr u16 BP_PROG_COUNT = {len(progs)}u;")
    out.append(f"    static constexpr u16 BP_PROG_FUNCS"
               f"[{max(len(progs), 1)}] = {{")
    out += [f"      BP_FUNC_{n.upper()}," for n, _ in progs] or ["      0u,"]
    out.append("    };")
    out.append(f"    static const char * const "
               f"BP_PROG_NAMES[{max(len(progs), 1)}] = {{")
    out += [f'      "{n}",' for n, _ in progs] or ['      "",']
    out.append("    };")
    if image is not None:
        code, cstarts, funcs, _ = image
        blob = pack_bcp(code, cstarts, [ch for _, ch in funcs], refs, ats,
                        first, tfunc, ops_count())
        out.append("")
        out.append(f"    // one image: {len(code)} bytes of code, "
                   f"{len(cstarts) - 1} chunks, {len(funcs)} functions; "
                   f"BCP blob {len(blob)} bytes")
        for i, (fname, ch) in enumerate(funcs):
            out.append(f"    //   function {i} {fname}: chunk {ch}")
        for k, t in enumerate(range(first, first + count)):
            if tfunc[k] != NO_FUNC:
                out.append(f"    //   type {t} {types[t]}: function "
                           f"{tfunc[k]}")
        out.append(f"    alignas(4) static constexpr u8 BP_BCP[{len(blob)}]"
                   " = {")
        for i in range(0, len(blob), 12):
            part = ", ".join(f"0x{b:02x}" for b in blob[i:i + 12])
            out.append(f"      {part},")
        out.append("    };")
        # Machine::run trusts its image: check it here, once, for free
        out.append("    static_assert(verifyBCP(BP_BCP, sizeof(BP_BCP)) == "
                   "0u, \"the blob fails BytePhys::verifyBCP\");")
    if inner:
        out.append("  }")
    out += ["  }", "}", ""]
    return "\n".join(out)


# (source dir, output header, nested namespace): the EWP's physics, the
# test-only physics compiled from scripts/ulamlite/tests (only
# srcs/host/tools/bytephys_test.cpp includes its header), and the
# per-opcode microbenchmark (bench/*.bp, written by benchgen.py; the EWP
# runs it on a Bench atom)
SETS = [(HERE, OUT, None),
        (os.path.join(HERE, "tests"),
         os.path.join(ROOT, "srcs", "host", "tools", "BytePhysTests.h"),
         "Tests"),
        (os.path.join(HERE, "bench"),
         os.path.join(ROOT, "srcs", "shared", "include", "ByteBench.h"),
         "Bench")]


def sources(srcdir=HERE):
    if not os.path.isdir(srcdir):
        return []
    return [os.path.join(srcdir, f) for f in sorted(os.listdir(srcdir))
            if f.endswith(".bp")]


def build(srcdir=HERE, outpath=OUT, inner=None, listing=False):
    ops, fields = load_tables()
    decls, rest = load_decls(sources(srcdir))
    files = [(p, l) for p, l in rest.items() if l]
    image = None
    progs = []
    if files:
        code, cstarts, funcs, entries, lst = assemble_image(files, ops,
                                                            fields, decls)
        image = (code, cstarts, funcs, lst)
        progs = list(entries.items())
    if listing:
        return image
    return render(progs, decls, srcdir, outpath, inner, image)


def main(argv):
    if argv[:1] == ["--list"]:
        # the listing of a whole set: --list [tests|bench]
        which = argv[1] if len(argv) > 1 else ""
        st = {"tests": SETS[1], "bench": SETS[2]}.get(which, SETS[0])
        image = build(st[0], st[1], st[2], listing=True)
        if image is None:
            print("no code")
            return 0
        code, cstarts, funcs, lst = image
        print(f"{len(code)} bytes, {len(cstarts) - 1} chunks, "
              f"{len(funcs)} functions")
        print("\n".join(lst))
        return 0
    texts = [(build(*st), st[1]) for st in SETS]
    if argv[:1] == ["--check"]:
        bad = [os.path.basename(o) for t, o in texts
               if (open(o).read() if os.path.exists(o) else "") != t]
        if bad:
            print(f"bpasm --check: {', '.join(bad)} OUT OF DATE "
                  "(run scripts/bytephys/bpasm.py)")
            return 1
        print("bpasm --check: "
              + ", ".join(os.path.basename(o) for _, o in texts)
              + " current")
        return 0
    for text, o in texts:
        open(o, "w").write(text)
        print(f"bpasm: wrote {os.path.relpath(o, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
