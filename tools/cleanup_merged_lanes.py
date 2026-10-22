#!/usr/bin/env python3
# =============================================================================
# tools/cleanup_merged_lanes.py - remove lane branches that are already merged.
#
# OWNERSHIP: the repository's merge hygiene. A lane branch must not outlive
# its merge: after the lead verifies and merges a lane, both the lane's
# worktree and the lane's branch are removed, so `git branch` shows only
# `master` and the lanes still in flight.
#
# Forgetting the branch is easy - removing the worktree and deleting the
# branch are two commands, and the second one is the one that gets skipped.
# This tool makes the check mechanical: it finds every branch (other than
# `master`) whose tip is already an ancestor of `master`, removes the
# worktree that still holds it, and deletes the branch.
#
# Usage (from the repo root):
#   python tools/cleanup_merged_lanes.py            # report and remove
#   python tools/cleanup_merged_lanes.py --dry-run  # report only
#
# Exit codes: 0 ok, 1 a lane branch is NOT merged and is still in flight
# (informational - it is not an error), 2 usage or git failure.
# =============================================================================
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PROTECTED = {"master"}


def git(*args: str) -> tuple[int, str]:
    proc = subprocess.run(
        ["git", "-C", str(REPO), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    return proc.returncode, proc.stdout + proc.stderr


def branches() -> list[str]:
    code, out = git("branch", "--format=%(refname:short)")
    if code != 0:
        raise RuntimeError(out)
    return [line.strip() for line in out.splitlines() if line.strip()]


def commits_ahead(branch: str) -> int:
    """How many commits the branch has that master does not."""
    code, out = git("rev-list", "--count", f"master..{branch}")
    if code != 0:
        return 0
    try:
        return int(out.strip())
    except ValueError:
        return 0


def worktree_of(branch: str) -> str | None:
    code, out = git("worktree", "list", "--porcelain")
    if code != 0:
        return None
    lines = out.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("branch "):
            continue
        ref = line[len("branch "):].strip()
        if ref == f"refs/heads/{branch}":
            for back in range(index - 1, -1, -1):
                if lines[back].startswith("worktree "):
                    return lines[back][len("worktree "):].strip()
    return None


def has_uncommitted_work(path: str) -> bool:
    """True when the worktree still holds changes or untracked files."""
    code, out = git("-C", path, "status", "--porcelain")
    if code != 0:
        return True  # cannot tell: treat it as busy rather than delete it
    return bool(out.strip())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be removed, remove nothing")
    args = parser.parse_args()

    try:
        lanes = branches()
    except RuntimeError as error:
        print(f"cannot list branches: {error}", file=sys.stderr)
        return 2

    removed: list[str] = []
    in_flight: list[str] = []
    for branch in sorted(lanes):
        if branch in PROTECTED:
            continue
        path = worktree_of(branch)
        if path is not None:
            # The lane still owns a worktree. That is the normal state of a
            # lane in flight, and it is ALSO the state of a lane whose agent
            # has simply not committed yet - the branch then still points at
            # the commit it was created from, which master contains. Nothing
            # here can tell the two apart, so nothing here may remove it.
            in_flight.append(branch)
            continue
        # No worktree holds the branch: this is the leftover class - the lane
        # was merged and its worktree removed, but the branch was left behind.
        # Its tip is an ancestor of master, so the work is safely in master.
        if commits_ahead(branch) > 0:
            in_flight.append(branch)
            continue
        if not args.dry_run:
            code, out = git("branch", "-D", branch)
            if code != 0:
                print(f"cannot delete branch {branch}: {out}", file=sys.stderr)
                return 2
        removed.append(branch)

    print(f"merged lane branches removed: {len(removed)}")
    for branch in removed:
        print(f"  - {branch}")
    print(f"lane branches still in flight: {len(in_flight)}")
    for branch in in_flight:
        print(f"  * {branch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
