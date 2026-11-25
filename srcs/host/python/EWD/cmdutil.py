"""Shared pieces of the mfmxctl command modules (cmd_*.py)."""
from shell import Shell


class Ctx:
    """What a command needs, built once by mfmxctl.main and passed to every
    command function as its first argument."""

    def __init__(self, *, sh, m, cyc, args, MFMx, mock, state):
        self.sh = sh            #< the Shell (say, run_for, ticks, command)
        self.m = m              #< the Machine
        self.cyc = cyc          #< the SuperCycle
        self.args = args        #< the parsed command line
        self.MFMx = MFMx        #< the MFMx module (real or mock)
        self.mock = mock        #< the mock's control object, or None
        self.state = state      #< {"close": bool}, set by 'quit close'


def need(args_, n, usage):
    """ValueError with a usage line unless at least n arguments."""
    if len(args_) < n:
        raise ValueError(f"usage: {usage}")


def kw(word, choices, what):
    """A keyword argument, abbreviated like a command: exact match, else the
    unique choice `word` is a prefix of (case blind); AbbrevError otherwise."""
    return Shell.resolve(word.lower(), choices, what)
