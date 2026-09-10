#!/bin/bash
grep -oE ' [<>]L.*' | LC_ALL=C sort

