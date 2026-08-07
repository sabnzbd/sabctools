#!/usr/bin/env python3
# Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

"""
Shared machinery for the vendoring scripts in this directory.

A vendored tree is a snapshot of an upstream repository at a pinned commit, built by its
own CMake, and carrying local patches only where a CPython extension genuinely needs
something different from a standalone binary.
"""

import argparse
import os
import shutil
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def fetch(repo: str, ref: str, into: str) -> str:
    """Check out any ref - tag, branch or commit - and return the resolved commit."""
    run(["git", "init", "--quiet", into])
    run(["git", "-C", into, "remote", "add", "origin", repo])

    # A shallow fetch of an exact object works on GitHub and is by far the cheapest,
    # but not every host allows it; fall back to fetching everything.
    try:
        run(["git", "-C", into, "fetch", "--quiet", "--depth", "1", "origin", ref])
    except subprocess.CalledProcessError:
        print("==> Shallow fetch rejected, retrying with full history")
        run(["git", "-C", into, "fetch", "--quiet", "origin"])
        run(["git", "-C", into, "fetch", "--quiet", "--tags", "origin"])

    try:
        run(["git", "-C", into, "checkout", "--quiet", "FETCH_HEAD"])
    except subprocess.CalledProcessError:
        run(["git", "-C", into, "checkout", "--quiet", ref])

    return subprocess.run(
        ["git", "-C", into, "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()


def copy_sources(source: str, dest: str, trees, files):
    """Replace dest with the listed subtrees and files from a checkout."""
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)

    for tree in trees:
        shutil.copytree(os.path.join(source, tree), os.path.join(dest, tree))
    for name in files:
        target = os.path.join(dest, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy(os.path.join(source, name), target)


def patch(dest: str, path: str, old: str, new: str, description: str):
    """Apply one local patch, and fail loudly if it no longer applies.

    A patch that stops matching means upstream changed underneath us - either it fixed
    the problem itself, in which case delete the patch here, or it moved the code and
    the patch needs rewriting. Silently carrying on is the one thing we must not do.
    """
    full = os.path.join(dest, path)
    with open(full, encoding="utf-8") as handle:
        content = handle.read()

    if new in content and old not in content:
        raise SystemExit(
            "ERROR: patch '%s' is already present upstream in %s.\n"
            "       Remove it from the vendoring script." % (description, path)
        )
    if old not in content:
        raise SystemExit("ERROR: patch '%s' no longer matches anything in %s." % (description, path))

    with open(full, "w", encoding="utf-8") as handle:
        handle.write(content.replace(old, new))
    print("==> Patched %s: %s" % (path, description))


def parse_args(doc: str, default_ref: str, default_repo: str):
    parser = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ref", default=default_ref, help="tag, branch or commit to vendor (default: %(default)s)")
    parser.add_argument("--repo", default=default_repo, help=argparse.SUPPRESS)
    return parser.parse_args()
