#!/usr/bin/env bash
set -euo pipefail

NEW_SPIKE="${1:-}"

if [ -z "$NEW_SPIKE" ]; then
    echo "Usage: $(basename "$0") <new-spike-name>" >&2
    exit 1
fi

# Get the current branch name as the old spike
OLD_SPIKE=$(git branch --show-current)

if [ -z "$OLD_SPIKE" ]; then
    echo "Error: not on a branch (detached HEAD)" >&2
    exit 1
fi

if [[ ! "$OLD_SPIKE" =~ ^spike- ]]; then
    echo "Error: current branch '$OLD_SPIKE' does not begin with 'spike-'" >&2
    echo "Run from a spike branch, e.g. git checkout -b spike-13" >&2
    exit 1
fi

FINAL_TAG="${OLD_SPIKE}-final"

echo "Old spike: $OLD_SPIKE"
echo "Tagging final state: $FINAL_TAG"
git tag -a "$FINAL_TAG" -m "$OLD_SPIKE complete"

echo "Creating and switching to new spike: $NEW_SPIKE"
git checkout -b "$NEW_SPIKE"

echo "Pushing old tag and new branch..."
git push origin "$FINAL_TAG" "$NEW_SPIKE"

echo "Done. You are now on branch '$NEW_SPIKE'."
