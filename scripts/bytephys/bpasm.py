#!/usr/bin/env python3
"""bpasm.py -- assembler for the BytePhys spike (srcs/shared/include/BytePhys.h).

Reads *.bp sources and writes srcs/shared/include/BytePrograms.h, the
byte arrays the EWP runs for the bytecode physics types (fb1.bp ->
BP_PROG_FB1, ...).  The opcode and field numbers are parsed from
BytePhys.h itself, so the header stays the one source of truth.

Source syntax: one instruction per line, `;` starts a comment, `NAME:`
defines a label.  Instructions are the Op names without OP_, lower case
(push, dup, add, jz, getf, ...).  `push N` assembles as PUSH8 when N <
256, else PUSH32.  Jumps and calls take a label (absolute u16 target).
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
  bpasm.py --list fb1.bp   print the assembled listing
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


def assemble(path, lines, ops, fields, decls):
    """lines: path's non-declaration lines.  -> (bytes, listing lines)."""

    def size(words):
        op = words[0]
        if op == "push":
            return 2 if int(words[1], 0) < 256 else 5
        if op in JUMPS or op in MEMBER_OPS:
            return 3
        if op in ("getf", "setf", "loadl", "storel"):
            return 2
        if op == "enter":
            return 3
        return 1

    labels = {}
    pc = 0
    items = []
    slots = {}                  # the current func's local names
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
            if name in labels:
                raise SystemExit(f"{path}:{n}: label {name} twice")
            labels[name] = pc
            slots = {x: i for i, x in enumerate(names)}
            w = ["enter", str(len(args)), str(len(names))]
            items.append((n, s, w, pc))
            pc += size(w)
            continue
        while ":" in s:
            lab, s = s.split(":", 1)
            lab = lab.strip()
            if lab in labels:
                raise SystemExit(f"{path}:{n}: label {lab} twice")
            labels[lab] = pc
            s = s.strip()
        if not s:
            continue
        words = s.split()
        if words[0] not in ops and words[0] != "push":
            raise SystemExit(f"{path}:{n}: unknown op {words[0]}")
        if words[0] in ("loadl", "storel") and len(words) == 2 and \
           words[1] in slots:
            words = [words[0], str(slots[words[1]])]
        items.append((n, s, words, pc))
        pc += size(words)
    code = []
    listing = []
    for n, s, w, at in items:
        op = w[0]
        if op == "push":
            v = int(w[1], 0)
            if not 0 <= v < (1 << 32):
                raise SystemExit(f"{path}:{n}: push out of range")
            if v < 256:
                b = [ops["push8"], v]
            else:
                b = [ops["push32"]] + [(v >> k) & 0xff for k in (0, 8, 16, 24)]
        elif op in JUMPS:
            if len(w) != 2 or w[1] not in labels:
                raise SystemExit(f"{path}:{n}: {op} needs a known label")
            t = labels[w[1]]
            b = [ops[op], t & 0xff, t >> 8]
        elif op in ("getf", "setf"):
            if len(w) != 2 or w[1] not in fields:
                raise SystemExit(f"{path}:{n}: {op} needs a field "
                                 f"({', '.join(fields)})")
            b = [ops[op], fields[w[1]]]
        elif op in ("loadl", "storel", "enter"):
            want = 3 if op == "enter" else 2
            try:
                vals = [int(x, 0) for x in w[1:]]
            except ValueError:
                raise SystemExit(f"{path}:{n}: {op}: unknown local {w[1:]}")
            if len(w) != want or not all(0 <= v < 256 for v in vals):
                raise SystemExit(f"{path}:{n}: {op} takes "
                                 f"{want - 1} number(s) 0..255")
            b = [ops[op]] + vals
        elif op in MEMBER_OPS:
            if len(w) != 2:
                raise SystemExit(f"{path}:{n}: {op} needs a member path")
            i = decls.member_id(w[1], f"{path}:{n}")
            b = [ops[op], i & 0xff, i >> 8]
        else:
            if len(w) != 1:
                raise SystemExit(f"{path}:{n}: {op} takes no operand")
            b = [ops[op]]
        code += b
        listing.append(f"{at:4d}: {' '.join(f'{x:02x}' for x in b):<16} {s}")
    if len(code) >= 1 << 16:
        raise SystemExit(f"{path}: program too long")
    return bytes(code), listing


def render(progs, decls, srcdir=HERE, outpath=OUT, inner=None):
    """inner: a namespace nested in BytePhys (the test physics)."""
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
        refs.append(f"{{{len(ats)}u, {len(per)}u}}")
        ats += [f"{{{t}u, {p}u, {n}u}}" for t, p, n in per]
    n = max(len(refs), 1)
    out.append(f"    static constexpr MemberRef BP_MEMBER_REFS[{n}] = {{")
    out += [f"      {r}," for r in refs] or ["      {0u, 0u},"]
    out.append("    };")
    out.append(f"    static constexpr MemberAt BP_MEMBER_ATS"
               f"[{max(len(ats), 1)}] = {{")
    out += [f"      {a}," for a in ats] or ["      {0u, 0u, 0u},"]
    out.append("    };")
    out.append(f"    static constexpr Layout BP_LAYOUT = "
               f"{{BP_MEMBER_REFS, {len(refs)}u, BP_MEMBER_ATS}};")
    for name, code in progs:
        up = name.upper()
        out.append("")
        out.append(f"    static constexpr u8 BP_CODE_{up}[{len(code)}] = {{")
        for i in range(0, len(code), 12):
            chunk = ", ".join(f"0x{b:02x}" for b in code[i:i + 12])
            out.append(f"      {chunk},")
        out.append("    };")
        out.append(f"    static constexpr Program BP_PROG_{up} = "
                   f"{{BP_CODE_{up}, {len(code)}u, &BP_LAYOUT}};")
        # Machine::run trusts its program: check it here, once, for free
        out.append(f"    static_assert(verify(BP_PROG_{up}) == 0u, "
                   f"\"{name}.bp fails BytePhys::verify\");")
    # every program in source order, with its name (the microbenchmark
    # set, ByteBench.h, runs them all; harmless elsewhere)
    out.append("")
    out.append(f"    static constexpr u16 BP_PROG_COUNT = {len(progs)}u;")
    out.append(f"    static const Program * const "
               f"BP_PROG_LIST[{max(len(progs), 1)}] = {{")
    out += [f"      &BP_PROG_{n.upper()}," for n, _ in progs] or \
        ["      nullptr,"]
    out.append("    };")
    out.append(f"    static const char * const "
               f"BP_PROG_NAMES[{max(len(progs), 1)}] = {{")
    out += [f'      "{n}",' for n, _ in progs] or ['      "",']
    out.append("    };")
    # type number -> program: an element's program is the .bp file named
    # after it in lower case (ulamlite.py writes them that way)
    names = {n for n, _ in progs}
    types = {t: n for n, (t, _) in decls.elements.items()}
    first = min(types) if types else 0
    count = max(types) - first + 1 if types else 0
    out.append("")
    out.append("    // type number -> program (ewp Physics.cpp dispatches "
               "through programFor)")
    out.append(f"    static constexpr u16 BP_FIRST_TYPE = {first}u;")
    out.append(f"    static constexpr u16 BP_TYPE_COUNT = {count}u;")
    out.append(f"    static const Program * const "
               f"BP_PROGRAMS[{max(count, 1)}] = {{")
    for t in range(first, first + count):
        n = types.get(t)
        prog = (f"&BP_PROG_{n.upper()}" if n and n.lower() in names
                else "nullptr")
        out.append(f"      {prog},{'' if n is None else '  // ' + n}")
    if count == 0:
        out.append("      nullptr,")
    out.append("    };")
    out += ["",
            "    inline const Program * programFor(u32 type) {",
            "      if (type < BP_FIRST_TYPE) return nullptr;",
            "      const u32 i = type - BP_FIRST_TYPE;",
            "      return i < BP_TYPE_COUNT ? BP_PROGRAMS[i] : nullptr;",
            "    }"]
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


def build(srcdir=HERE, outpath=OUT, inner=None):
    ops, fields = load_tables()
    decls, rest = load_decls(sources(srcdir))
    progs = []
    for path, lines in rest.items():
        if lines:
            code, _ = assemble(path, lines, ops, fields, decls)
            progs.append((os.path.basename(path)[:-3], code))
    return render(progs, decls, srcdir, outpath, inner)


def main(argv):
    if argv[:1] == ["--list"]:
        ops, fields = load_tables()
        paths = sources()
        extra = [os.path.abspath(f) for f in argv[1:]
                 if os.path.abspath(f) not in paths]
        decls, rest = load_decls(paths + extra)
        for path in paths:      # hand out member ids as a build would
            if rest[path]:
                assemble(path, rest[path], ops, fields, decls)
        for f in argv[1:]:
            lines = rest[os.path.abspath(f)]
            code, listing = assemble(f, lines, ops, fields, decls)
            print(f"{f}: {len(code)} bytes")
            print("\n".join(listing))
        print("\n".join(decls.describe()))
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
