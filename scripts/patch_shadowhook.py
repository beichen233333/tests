#!/usr/bin/env python3
"""Patch pinned ShadowHook v2.0.1 for direct-address-only Android hooks.

This build intentionally skips ShadowHook's private linker soinfo monitor. All
AstraeOffline hooks target functions in an already-loaded ELF by absolute
address, so sh_task_do() obtains normal ELF metadata through xDL's address API.
"""
from __future__ import annotations
import argparse
import subprocess
from pathlib import Path

INIT_MARKER = "ASTRAE_SHADOWHOOK_DIRECT_ADDRESS_ONLY"
INIT_REPLACEMENT = '''#if defined(ASTRAE_SHADOWHOOK_DIRECT_ADDRESS_ONLY)
  SH_LOG_ALWAYS_SHOW("AstraeOffline: direct-address-only profile; skipping private linker monitor initialization");
#else
  if (__predict_false(0 != sh_linker_init())) GOTO_END(SHADOWHOOK_ERRNO_INIT_LINKER);
#endif'''
DIRECT_ORIGINAL = '''  } else {
    addr_info.is_sym_addr = self->is_sym_addr;
    addr_info.is_proc_start = self->is_proc_start;
  }'''
DIRECT_REPLACEMENT = '''  } else {
    // Direct-address hooks do not need ShadowHook's linker monitor. Resolve
    // the containing ELF's load bias and program headers directly through xDL.
    if (0 != (r = sh_linker_get_addr_info_by_addr(&addr_info, (void *)self->target_addr,
                                                   self->is_sym_addr, self->is_proc_start,
                                                   !self->is_sym_addr)))
      goto end;
  }'''
TASK_INIT_MARKER = "AstraeOffline: direct-address-only profile; linker callbacks disabled"


def patch_init(source: Path) -> bool:
    text = source.read_text(encoding="utf-8")
    if INIT_MARKER in text:
        if "sh_linker_init()" not in text or "SHADOWHOOK_ERRNO_INIT_LINKER" not in text:
            raise SystemExit(f"ERROR: partial direct-address-only init patch in {source}")
        if "private linker monitor initialization" not in text or "#else" not in text:
            raise SystemExit(f"ERROR: unexpected direct-address-only init patch in {source}")
        return False
    import re
    pattern = re.compile(r"(?m)^(?P<indent>[ \t]*)if \(__predict_false\(0 != sh_linker_init\(\)\)\) GOTO_END\(SHADOWHOOK_ERRNO_INIT_LINKER\);$")
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise SystemExit(f"ERROR: expected one sh_linker_init failure gate in {source}, found {len(matches)}")
    indent = matches[0].group("indent")
    replacement = "\n".join(indent + line if line else line for line in INIT_REPLACEMENT.splitlines())
    text = text[:matches[0].start()] + replacement + text[matches[0].end():]
    source.write_text(text, encoding="utf-8")
    return True


def patch_task_init(text: str, source: Path) -> tuple[str, bool]:
    start = text.find("int sh_task_init(void) {")
    end = text.find("\nint sh_task_do(", start + 1) if start >= 0 else -1
    if start < 0 or end < 0:
        raise SystemExit(f"ERROR: cannot locate sh_task_init() in {source}")
    function = text[start:end]
    if TASK_INIT_MARKER in function:
        if "return 0;" not in function or "#else" not in function:
            raise SystemExit(f"ERROR: partial direct-address-only task init patch in {source}")
        return text, False
    open_brace = function.find("{")
    close_brace = function.rfind("}")
    if open_brace < 0 or close_brace <= open_brace:
        raise SystemExit(f"ERROR: malformed sh_task_init() in {source}")
    original_body = function[open_brace + 1:close_brace]
    new_function = (
        function[:open_brace + 1]
        + "\n#ifdef ASTRAE_SHADOWHOOK_DIRECT_ADDRESS_ONLY\n"
        + f'  SH_LOG_ALWAYS_SHOW("{TASK_INIT_MARKER}");\n  return 0;\n'
        + "#else\n"
        + original_body
        + "\n#endif\n}"
    )
    return text[:start] + new_function + text[end:], True


def patch_task(source: Path) -> bool:
    text = source.read_text(encoding="utf-8")
    changed = False
    if TASK_INIT_MARKER not in text:
        text, did_change = patch_task_init(text, source)
        changed |= did_change
    direct_marker = "sh_linker_get_addr_info_by_addr(&addr_info, (void *)self->target_addr"
    if direct_marker not in text:
        count = text.count(DIRECT_ORIGINAL)
        if count != 1:
            raise SystemExit(f"ERROR: expected one direct-address metadata branch in {source}, found {count}")
        text = text.replace(DIRECT_ORIGINAL, DIRECT_REPLACEMENT, 1)
        changed = True
    elif DIRECT_REPLACEMENT not in text:
        raise SystemExit(f"ERROR: unexpected direct-address metadata patch in {source}")
    if changed:
        source.write_text(text, encoding="utf-8")
    return changed


def restore_legacy_linker_patch(source_root: Path, linker_source: Path) -> bool:
    """Discard only the known probe-bridge patch from earlier Astrae builds."""
    text = linker_source.read_text(encoding="utf-8")
    legacy_markers = ("astrae_shadowhook_dlopen_nothing", "astrae_shadowhook_linker_log", "sh_linker_find_probe_info")
    if not any(marker in text for marker in legacy_markers):
        return False
    check = subprocess.run(
        ["git", "-C", str(source_root), "rev-parse", "--is-inside-work-tree"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if check.returncode != 0:
        raise SystemExit(
            f"ERROR: {linker_source} contains a legacy Astrae probe patch but is not in a Git checkout. "
            "Remove this ShadowHook source directory and re-fetch the clean v2.0.1 tag."
        )
    relative = linker_source.relative_to(source_root)
    restore = subprocess.run(
        ["git", "-C", str(source_root), "checkout", "--", str(relative)],
        check=False,
    )
    if restore.returncode != 0:
        raise SystemExit(f"ERROR: could not restore legacy-modified ShadowHook source: {linker_source}")
    restored = linker_source.read_text(encoding="utf-8")
    if any(marker in restored for marker in legacy_markers):
        raise SystemExit(f"ERROR: legacy ShadowHook linker bridge remains after restore: {linker_source}")
    print("Restored legacy probe-bridge modifications in ShadowHook sh_linker.c")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shadowhook_source_dir", type=Path)
    args = parser.parse_args()
    cpp = args.shadowhook_source_dir / "shadowhook" / "src" / "main" / "cpp"
    init_source = cpp / "shadowhook.c"
    task_source = cpp / "sh_task.c"
    linker_source = cpp / "sh_linker.c"
    for required in (init_source, task_source, linker_source, cpp / "sh_linker.h"):
        if not required.is_file():
            raise SystemExit(f"ERROR: incomplete ShadowHook v2.0.1 source tree: missing {required}")
    restore_legacy_linker_patch(args.shadowhook_source_dir, linker_source)
    changed_init = patch_init(init_source)
    changed_task = patch_task(task_source)
    print(
        "ShadowHook direct-address-only patch: "
        f"shadowhook.c={'updated' if changed_init else 'already patched'}, "
        f"sh_task.c={'updated' if changed_task else 'already patched'}"
    )
    print("Linker probe / soinfo monitor is disabled; direct targets resolve ELF metadata via xDL.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
