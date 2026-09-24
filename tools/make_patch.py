#!/usr/bin/env python3
"""Regenerate tools/patches/<name>.patch from the edits in the working tree.

  tools/make_patch.py --src plex=../Vita_plex plex     # modules/plex edits
  tools/make_patch.py --src plex=../Vita_plex core     # hub/core edits

Re-imports <name> without its patch into a scratch copy of the repo, diffs
the pristine import against your working tree, and writes the result to
tools/patches/<name>.patch. Your working tree is not modified.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DIRS = {
    "core": ["hub/core/include", "hub/core/src"],
}


def dirs_for(name):
    return DIRS.get(name, ["modules/%s/include" % name, "modules/%s/src" % name])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", action="append", required=True, metavar="MODULE=PATH")
    ap.add_argument("name", help="plex, abs, suwayomi, music or core")
    args = ap.parse_args()

    srcs = [s if os.path.isabs(s.partition("=")[2]) else
            s.partition("=")[0] + "=" + os.path.abspath(s.partition("=")[2]) for s in args.src]
    only = "plex" if args.name == "core" else args.name

    with tempfile.TemporaryDirectory() as tmp:
        scratch = os.path.join(tmp, "repo")
        # A throwaway copy of just what the importer touches.
        shutil.copytree(os.path.join(ROOT, "tools"), os.path.join(scratch, "tools"))
        for d in ("hub/core", "modules/" + only):
            if os.path.isdir(os.path.join(ROOT, d)):
                shutil.copytree(os.path.join(ROOT, d), os.path.join(scratch, d))
        subprocess.check_call(["git", "init", "-q"], cwd=scratch)
        cmd = [sys.executable, "tools/import_modules.py", "--no-patch", "--no-resources", "--only", only]
        if args.name == "core":
            cmd += ["--only", "core"]
        for s in srcs:
            cmd += ["--src", s]
        subprocess.check_call(cmd, cwd=scratch, stdout=subprocess.DEVNULL)

        patch = ""
        for d in dirs_for(args.name):
            out = subprocess.run(
                ["git", "diff", "--no-index", "--no-prefix", os.path.join(scratch, d), os.path.join(ROOT, d)],
                capture_output=True, text=True).stdout
            # Rewrite the absolute paths to repo-relative a/ b/ paths.
            # (--no-prefix drops the leading "/" of both sides.)
            out = out.replace(os.path.join(scratch, "").lstrip("/"), "a/")
            out = out.replace(os.path.join(ROOT, "").lstrip("/"), "b/")
            patch += out
        dest = os.path.join(ROOT, "tools", "patches", args.name + ".patch")
        with open(dest, "w") as f:
            f.write(patch)
        files = patch.count("\ndiff --git ") + (1 if patch.startswith("diff --git ") else 0)
        print("wrote %s (%d files)" % (os.path.relpath(dest, ROOT), files))


if __name__ == "__main__":
    sys.exit(main())
