# Fallback shim so Config.py can run where the real 'tomlikey' is not
# installed (e.g. the mock/cloud environment).  mfmxctl.py --mock appends this
# directory to the END of sys.path, so a real tomlikey always wins.
#
# The project's dtoml files use UNQUOTED bare-word string values
# (activeLayout=ewpandhub, image.all.all = HUB, blocks = [BC_LOGBLOCK]) which
# plain tomllib rejects.  This shim quotes bare words in values and hands the
# result to tomllib.  Limitation: values must be on one line (true of the
# current config files); the real tomlikey may accept more.
import re
import tomllib

_NUM = re.compile(r"^[+-]?(0x[0-9a-fA-F_]+|0o[0-7_]+|0b[01_]+|\d[\d_]*(\.\d[\d_]*)?([eE][+-]?\d+)?|inf|nan)$")
_BARE = re.compile(r"^[A-Za-z_][\w.\-]*$")
_WORDS = {"true", "false"}


def _fix_value(v):
    out, i, n = [], 0, len(v)
    while i < n:
        c = v[i]
        if c in "\"'":                      # copy a quoted string verbatim
            j = i + 1
            while j < n and v[j] != c:
                j += 2 if (c == '"' and v[j] == "\\") else 1
            out.append(v[i:j + 1])
            i = j + 1
        elif c == "#":                      # comment: copy the rest
            out.append(v[i:])
            break
        elif c in "[],{}= \t":
            out.append(c)
            i += 1
        else:                               # a bare token
            j = i
            while j < n and v[j] not in "[],{}= \t#":
                j += 1
            tok = v[i:j]
            if tok in _WORDS or _NUM.match(tok) or not _BARE.match(tok):
                out.append(tok)
            else:
                out.append(f'"{tok}"')
            i = j
    return "".join(out)


def _fix_line(line):
    s = line.lstrip()
    if not s or s.startswith("#") or s.startswith("["):
        return line
    # first '=' outside quotes splits key from value
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in "\"'":
            q = c
        elif c == "=":
            return line[:i + 1] + _fix_value(line[i + 1:])
    return line


def loads(text):
    return tomllib.loads("\n".join(_fix_line(l) for l in text.split("\n")))


def load(fp):
    return loads(fp.read().decode() if hasattr(fp, "read") else fp)
