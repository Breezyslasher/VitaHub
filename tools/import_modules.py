#!/usr/bin/env python3
"""Import the four standalone apps into VitaHub as modules.

VitaHub is built from four upstream apps that keep being developed on their
own. Rather than fork them by hand, this script re-derives
modules/<name>/{include,src} from a checkout of each upstream repo, so pulling
in upstream work is: check the repos out, run this, rebuild.

What it does to each app, in order:

  1. Copies include/ and src/ (minus the pieces the hub owns: main.cpp, the
     Vita pthread/stdio stubs, the standalone self-updater executables and
     dead code).
  2. Nests namespaces that the apps declare at global scope (platform, vita,
     ps4, app_update) inside the app's own namespace. ABS, Suwayomi and Plex
     each ship a different `::platform` / `::vita` / `::ps4`, which would
     silently merge (ODR) or collide at link time in one binary. Code inside
     the app namespace keeps calling `platform::path(...)` unchanged, since
     lookup finds the nested namespace first.
  3. Wraps headers that define free functions at global scope (paths.hpp and
     friends) in the app namespace for the same reason.
  4. Points XML layout loads at the module's own copy:
     "activity/main.xml" -> "<module>/activity/main.xml".
  5. Applies tools/patches/<module>.patch: the small, hand-written edits that
     hook the app into the hub (splitting Application::run() so the hub owns
     the main loop, a way back to the hub, ...). Keep those edits in the patch
     so they survive the next import.

Resources are merged into resources/ (the four trees are identical wherever
they overlap, apart from the XML layouts, which go to resources/xml/<module>/).

Usage:
  tools/import_modules.py --src plex=../Vita_plex --src abs=../Vita_abs \\
                          --src suwayomi=../Vita_Suwayomi \\
                          --src music=../Vita-Music-Assistant
  tools/import_modules.py --src plex=../Vita_plex --only plex
  tools/import_modules.py ... --no-patch     # stop before step 5

The Vita_plex checkout also provides hub/core/: VitaHub's platform layer and
self-updater, renamed from Vita_plex's (see import_core()).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MODULES = {
    "plex": {
        "repo": "Vita_plex",
        "ns": "vitaplex",
        "exclude": [
            "src/main.cpp",
            "src/app.cpp",            # dead code, never compiled upstream
            "include/app.h",
            "src/updater_stub",
            "src/updater_ps4",
            "src/utils/vita_stubs.c",  # hub/src/vita_stubs.c
            "src/utils/switch_stubs.c",
        ],
        "wrap_headers": {
            # file: regex of the first line to wrap from
            "include/platform/paths.hpp": r"^#if defined\(__vita__\)",
            "include/platform/android_assets.hpp": r"^#if defined\(__ANDROID__\)",
            "src/platform/android_assets.cpp": r"^#include <borealis/core/logger.hpp>",
        },
    },
    "abs": {
        "repo": "Vita_abs",
        "ns": "vitaabs",
        "exclude": [
            "src/main.cpp",
            "src/updater_stub",
            "src/updater_ps4",
            "src/utils/vita_stubs.c",
                    "include/player/mpv_player.h",  # unused duplicate of mpv_player.hpp
        ],
        "wrap_headers": {
            "include/platform/paths.hpp": r"^// Legacy constant",
            "include/platform/android_assets.hpp": r"^#if defined\(__ANDROID__\)",
            "src/platform/android_assets.cpp": r"^#include <borealis/core/logger.hpp>",
        },
    },
    "suwayomi": {
        "repo": "Vita_Suwayomi",
        "ns": "vitasuwayomi",
        "exclude": [
            "src/main.cpp",
            "src/updater_stub",
            "src/updater_ps4",
            "src/utils/vita_stubs.c",
        ],
        "wrap_headers": {
            "include/platform/paths.hpp": r"^// Legacy constant",
            "include/platform/android_assets.hpp": r"^#if defined\(__ANDROID__\)",
            "src/platform/android_assets.cpp": r"^#include <borealis/core/logger.hpp>",
            "include/utils/button_icons.hpp": r"^#if defined\(ANDROID\)",
        },
    },
    "music": {
        "repo": "Vita-Music-Assistant",
        "ns": "vita_ma",
        "exclude": [
            "src/main.cpp",
            "src/utils/vita_stubs.c",
        ],
        "wrap_headers": {},
    },
}

# Namespaces the apps open at global scope that must become <ns>::<name>.
GLOBAL_NAMESPACES = ["platform", "vita", "ps4", "app_update"]

SOURCE_EXT = (".cpp", ".hpp", ".h", ".c", ".mm")


# ---------------------------------------------------------------------------
# A small C/C++ scanner: enough to find top-level `namespace X {` blocks and
# their matching close brace while skipping comments, strings and char
# literals. Preprocessor lines are skipped too (a brace in a macro body must
# not count).
# ---------------------------------------------------------------------------
def scan_braces(text):
    """Yield (index, char) for every '{' / '}' that is real code."""
    i, n = 0, len(text)
    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start:
            j = i
            while j < n and text[j] in " \t":
                j += 1
            if j < n and text[j] == "#":
                # preprocessor directive, honour line continuations
                while j < n:
                    if text[j] == "\n" and text[j - 1] != "\\":
                        break
                    j += 1
                i = j
                continue
            at_line_start = False
        if c == "\n":
            at_line_start = True
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if m:
                end = text.find(")" + m.group(1) + '"', i)
                i = n if end < 0 else end + len(m.group(1)) + 2
            else:
                i += 1
        elif c == '"' or c == "'":
            # skip digit separators like 1'000'000
            if c == "'" and i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum() \
                    and not text[i - 1].isalpha():
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n":
                    break
                j += 1
            i = j + 1
        elif c in "{}":
            yield i, c
            i += 1
        else:
            i += 1


def nest_global_namespaces(text, ns, names):
    """Rewrite top-level `namespace NAME {...}` to `namespace ns { namespace NAME {...} }`."""
    pattern = re.compile(r"\bnamespace\s+(" + "|".join(names) + r")\s*\{")
    depth = 0
    opens = []  # stack of (depth-at-open, is_target)
    edits = []  # (pos, insert_text)
    brace_iter = list(scan_braces(text))
    for pos, ch in brace_iter:
        if ch == "{":
            # Is this the brace of a top-level target namespace?
            line_start = text.rfind("\n", 0, pos) + 1
            head = text[line_start:pos + 1]
            m = pattern.search(head)
            is_target = depth == 0 and m is not None and head.rstrip().endswith("{")
            if is_target:
                edits.append((line_start + m.start(), "namespace %s { " % ns))
            opens.append(is_target)
            depth += 1
        else:
            depth -= 1
            if depth < 0:
                raise RuntimeError("unbalanced braces")
            if opens.pop():
                edits.append((pos + 1, " }  // namespace %s" % ns))
    if depth != 0:
        raise RuntimeError("unbalanced braces (depth %d at EOF)" % depth)
    for pos, ins in sorted(edits, reverse=True):
        text = text[:pos] + ins + text[pos:]
    return text, len(edits) // 2


def wrap_header(text, ns, start_regex):
    lines = text.split("\n")
    start = None
    for idx, line in enumerate(lines):
        if re.search(start_regex, line):
            start = idx
            break
    if start is None:
        raise RuntimeError("wrap start marker %r not found" % start_regex)
    end = len(lines)
    # Leave a trailing include guard / #endif matching an opening #if on
    # line 1 (android_assets.cpp style) outside the namespace.
    first = next((l for l in lines if l.strip()), "")
    if first.startswith("#if"):
        while end > 0 and not lines[end - 1].strip():
            end -= 1
        if lines[end - 1].startswith("#endif"):
            end -= 1
    pre = []
    # Headers that pull system headers inside the wrapped region: hoist them
    # (guarded the same way) so they are not included inside our namespace.
    region = "\n".join(lines[start:end])
    if "<SDL2/SDL.h>" in region:
        pre = ["#if defined(__ANDROID__)", "#include <SDL2/SDL.h>", "#endif"]
    return "\n".join(lines[:start] + pre + ["namespace %s {" % ns] + lines[start:end]
                     + ["}  // namespace %s" % ns] + lines[end:])


XML_LOAD = re.compile(r'"activity/([A-Za-z0-9_]+\.xml)"')


def import_module(name, src_repo, apply_patch=True):
    cfg = MODULES[name]
    ns = cfg["ns"]
    dst = os.path.join(ROOT, "modules", name)
    for sub in ("include", "src"):
        path = os.path.join(dst, sub)
        if os.path.isdir(path):
            shutil.rmtree(path)
    excluded = [os.path.normpath(e) for e in cfg["exclude"]]
    nested_total = 0
    for sub in ("include", "src"):
        for dirpath, _, files in os.walk(os.path.join(src_repo, sub)):
            for fn in files:
                full = os.path.join(dirpath, fn)
                rel = os.path.normpath(os.path.relpath(full, src_repo))
                if any(rel == e or rel.startswith(e + os.sep) for e in excluded):
                    continue
                out = os.path.join(dst, rel)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                if not fn.endswith(SOURCE_EXT):
                    shutil.copy2(full, out)
                    continue
                with open(full, encoding="utf-8", errors="surrogateescape") as f:
                    text = f.read()
                if rel in cfg["wrap_headers"]:
                    text = wrap_header(text, ns, cfg["wrap_headers"][rel])
                text, count = nest_global_namespaces(text, ns, GLOBAL_NAMESPACES)
                nested_total += count
                text = XML_LOAD.sub(lambda m: '"%s/activity/%s"' % (name, m.group(1)), text)
                with open(out, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(text)
    print("[%s] imported, %d global namespace blocks nested into %s::" % (name, nested_total, ns))

    # Upstream version (Settings > About, API user agents).
    version = os.path.join(src_repo, "VERSION")
    if os.path.exists(version):
        shutil.copy2(version, os.path.join(dst, "VERSION"))

    # XML layouts -> resources/xml/<module>/
    xml_dst = os.path.join(ROOT, "resources", "xml", name)
    if os.path.isdir(xml_dst):
        shutil.rmtree(xml_dst)
    shutil.copytree(os.path.join(src_repo, "resources", "xml"), xml_dst)

    patch = os.path.join(ROOT, "tools", "patches", name + ".patch")
    if apply_patch and os.path.exists(patch) and os.path.getsize(patch) > 0:
        # Plain apply first; --3way (needs the pre-patch files in git's index)
        # only as the fallback for an upstream change near a hunk.
        if subprocess.call(["git", "apply", "--whitespace=nowarn", patch], cwd=ROOT) != 0:
            subprocess.check_call(["git", "apply", "--3way", "--whitespace=nowarn", patch], cwd=ROOT)
        print("[%s] applied %s" % (name, os.path.relpath(patch, ROOT)))


# ---------------------------------------------------------------------------
# Hub core: VitaHub's own platform layer and self-updater.
#
# Vita_plex has the most complete platform layer of the four apps (every
# platform VitaHub builds for) and a signed, multi-platform in-app updater.
# The hub gets its own copy of those files, renamed to VitaHub: namespace
# vitahub, the VitaHub GitHub repo, VitaHub title IDs and install paths.
# ---------------------------------------------------------------------------
CORE_FILES = [
    "include/platform/platform.hpp",
    "include/platform/paths.hpp",
    "include/platform/android_assets.hpp",
    "include/utils/app_update.hpp",
    "include/utils/async.hpp",
    "include/utils/http_client.hpp",
    "include/utils/ps4_install.hpp",
    "include/utils/update_verify.hpp",
    "include/utils/vita_install.hpp",
    "src/platform/android_assets.cpp",
    "src/platform/platform_android.cpp",
    "src/platform/platform_common.cpp",
    "src/platform/platform_desktop.cpp",
    "src/platform/platform_ios.mm",
    "src/platform/platform_ps4.cpp",
    "src/platform/platform_psv.cpp",
    "src/platform/platform_switch.cpp",
    "src/utils/app_update.cpp",
    "src/utils/http_client.cpp",
    "src/utils/ps4_install.cpp",
    "src/utils/update_verify.cpp",
    "src/utils/vita_head_bin.h",
    "src/utils/vita_install.cpp",
    "src/utils/switch_stubs.c",
    "src/updater_stub/main.cpp",
    "src/updater_ps4/main.cpp",
]

# Names that must NOT be renamed: the Android Java classes. VitaHub's Android
# project keeps Vita_plex's Java sources (the Plex module's JNI code looks
# them up by name), so the hub's native code has to use the same names.
CORE_KEEP = [
    "org/VitaPlex/app/VitaPlexActivity", "org_VitaPlex_app_VitaPlexActivity",
    "org.VitaPlex.app.VitaPlexActivity",
    "org/VitaPlex/app", "org_VitaPlex_app", "org.VitaPlex.app",
    "org/vitaPlex/app", "org_vitaPlex_app", "org.vitaPlex.app",
    # Exported by patches/borealis/psv_platform.cpp under this name.
    "vitaplex_set_audio_playback_active", "vitaplex_set_video_render_hook",
    "vitaplex_signal_video_frame",
]

CORE_RENAMES = [
    ("Breezyslasher/Vita_plex", "Breezyslasher/VitaHub"),
    ("VitaPlexMainEntry", "VitaHubMainEntry"),
    ("VITA_PLEX_DISPLAY_VERSION", "VITAHUB_DISPLAY_VERSION"),
    ("VITA_PLEX_VERSION", "VITAHUB_VERSION"),
    ("VITAPLEX_", "VITAHUB_"),
    ("VPLX00002", "VHUB00002"),   # PS4 app
    ("VPLX00003", "VHUB00003"),   # PS4 updater helper
    ("VPLXUPD01", "VHUBUPD01"),   # Vita updater stub
    ("VPLEX0001", "VHUB00001"),   # Vita app
    ("Vita_plex", "VitaHub"),
    ("VitaPlex", "VitaHub"),
    ("vitaplex", "vitahub"),
    ("VITAPLEX", "VITAHUB"),
]


def rename_for_core(text):
    keep = {}
    for i, k in enumerate(CORE_KEEP):
        token = "\x00KEEP%d\x00" % i
        keep[token] = k
        text = text.replace(k, token)
    for old, new in CORE_RENAMES:
        text = text.replace(old, new)
    for token, k in keep.items():
        text = text.replace(token, k)
    return text


def import_core(plex_repo, apply_patch=True):
    dst = os.path.join(ROOT, "hub", "core")
    for sub in ("include", "src"):
        path = os.path.join(dst, sub)
        if os.path.isdir(path):
            shutil.rmtree(path)
    for rel in CORE_FILES:
        with open(os.path.join(plex_repo, rel), encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        if rel == "include/platform/paths.hpp":
            text = wrap_header(text, "vitaplex", r"^#if defined\(__vita__\)")
        elif rel in ("include/platform/android_assets.hpp",):
            text = wrap_header(text, "vitaplex", r"^#if defined\(__ANDROID__\)")
        elif rel == "src/platform/android_assets.cpp":
            text = wrap_header(text, "vitaplex", r"^#include <borealis/core/logger.hpp>")
        if rel.endswith((".cpp", ".hpp", ".h", ".mm")) and "updater_" not in rel:
            text, _ = nest_global_namespaces(text, "vitaplex", GLOBAL_NAMESPACES)
        text = rename_for_core(text)
        if rel == "src/updater_stub/main.cpp":
            # The stub is its own tiny program; give it back the global
            # vita:: name that the import nested into vitahub::.
            text = text.replace('#include "utils/vita_install.hpp"\n',
                                '#include "utils/vita_install.hpp"\n\nnamespace vita = vitahub::vita;\n', 1)
        out = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
    print("[core] imported %d files from Vita_plex into hub/core (namespace vitahub)" % len(CORE_FILES))
    patch = os.path.join(ROOT, "tools", "patches", "core.patch")
    if apply_patch and os.path.exists(patch) and os.path.getsize(patch) > 0:
        if subprocess.call(["git", "apply", "--whitespace=nowarn", patch], cwd=ROOT) != 0:
            subprocess.check_call(["git", "apply", "--3way", "--whitespace=nowarn", patch], cwd=ROOT)
        print("[core] applied tools/patches/core.patch")


# Resource files that legitimately differ between the apps; the hub decides.
RESOURCE_OWNED_BY_HUB = {
    os.path.normpath(p) for p in (
        "i18n/en-US/main.json",   # unused by all four apps; hub ships its own
        "images/logo.png",
        "images/logo-small.png",
    )
}


def merge_resources(srcs):
    res_dst = os.path.join(ROOT, "resources")
    conflicts = []
    order = ["plex", "suwayomi", "abs", "music"]  # first wins (Plex is newest)
    for name in order:
        if name not in srcs:
            continue
        base = os.path.join(srcs[name], "resources")
        for dirpath, _, files in os.walk(base):
            rel_dir = os.path.relpath(dirpath, base)
            if rel_dir.split(os.sep)[0] == "xml":
                continue
            for fn in files:
                rel = os.path.normpath(os.path.join(rel_dir, fn))
                if rel in RESOURCE_OWNED_BY_HUB:
                    continue
                out = os.path.join(res_dst, rel)
                if os.path.exists(out):
                    with open(out, "rb") as a, open(os.path.join(dirpath, fn), "rb") as b:
                        if a.read() != b.read():
                            conflicts.append((rel, name))
                    continue
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copy2(os.path.join(dirpath, fn), out)
    for rel, name in conflicts:
        print("resource differs, kept existing copy: %s (from %s)" % (rel, name))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", action="append", default=[], metavar="MODULE=PATH",
                    help="checkout of an upstream app")
    ap.add_argument("--only", action="append", help="import only these modules")
    ap.add_argument("--no-patch", action="store_true", help="skip tools/patches/<module>.patch")
    ap.add_argument("--no-resources", action="store_true")
    args = ap.parse_args()

    srcs = {}
    for item in args.src:
        name, _, path = item.partition("=")
        if name not in MODULES or not path:
            ap.error("bad --src %r (modules: %s)" % (item, ", ".join(MODULES)))
        srcs[name] = os.path.abspath(path)
    if not srcs:
        ap.error("no --src given")

    for name, path in srcs.items():
        if args.only and name not in args.only:
            continue
        import_module(name, path, apply_patch=not args.no_patch)
    if "plex" in srcs and (not args.only or "core" in args.only or "plex" in args.only):
        import_core(srcs["plex"], apply_patch=not args.no_patch)
    if not args.no_resources:
        merge_resources(srcs)


if __name__ == "__main__":
    sys.exit(main())
