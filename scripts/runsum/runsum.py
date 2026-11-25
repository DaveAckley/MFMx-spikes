#!/usr/bin/env python3
"""Wrapper: the code lives in srcs/host/python/EWD/runsum.py (mfmxctl uses
it too, to write the summary at shutdown).  See that file for usage."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "srcs", "host", "python",
                                "EWD"))
import runsum  # noqa: E402

sys.exit(runsum.main())
