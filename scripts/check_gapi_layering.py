#!/usr/bin/env python3
"""Make sure that the shared renderer code does not use a graphics API.

The shared code is code/renderer_gapi/*.c, code/renderer_gapi/*.h, and
code/renderer_gapi/graphics_api/gapi.h. It must use only gapi.h to get to a
graphics API. The script finds these items in the shared code, outside of
comments and string literals:

  - An include of a graphics API header, or of a file in graphics_api/<API>/.
  - A Vulkan or WebGPU name: vk_*, Vk*, VK_*, qvk*, the vk struct, wgpu*,
    WGPU*.

It prints each item as <file>:<line>: <text> and exits with status 1 if it
finds one. Run it from any directory.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAPI_DIR = ROOT / "code" / "renderer_gapi"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
BAD_INCLUDE_RE = re.compile(
    r'(^|/)(vk_[^/]*|VKimpl\.h|vulkan[^/]*|webgpu[^/]*|dawn[^/]*)$'
    r'|(^|/)graphics_api/[^/]+/'
    r'|^(vulkan|webgpu|dawn)/')
BAD_NAME_RE = re.compile(
    r'\b(vk_\w+|Vk[A-Z]\w*|VK_[A-Z0-9_]+|qvk\w+|vk(?=\s*\.)|wgpu[A-Z]\w*|WGPU\w+)')


def strip_comments_and_strings(text):
    """Replace comments and string and character literals with spaces.

    The line breaks stay, so the line numbers do not change.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + c if j - i >= 2 else text[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def check_file(path):
    """Return a list of (line number, text) for each item that is not permitted."""
    text = path.read_text(encoding="latin-1")
    problems = []

    # Includes are read from the original text, because the file name is a
    # string literal.
    for number, line in enumerate(text.splitlines(), start=1):
        match = INCLUDE_RE.match(line)
        if match and match.group(1) != "graphics_api/gapi.h" and BAD_INCLUDE_RE.search(match.group(1)):
            problems.append((number, line.strip()))

    stripped = strip_comments_and_strings(text)
    for number, line in enumerate(stripped.splitlines(), start=1):
        if INCLUDE_RE.match(line):
            continue
        if BAD_NAME_RE.search(line):
            problems.append((number, text.splitlines()[number - 1].strip()))

    return problems


def main():
    files = sorted(GAPI_DIR.glob("*.c")) + sorted(GAPI_DIR.glob("*.h"))
    files.append(GAPI_DIR / "graphics_api" / "gapi.h")

    count = 0
    for path in files:
        for number, line in check_file(path):
            print(f"{path.relative_to(ROOT)}:{number}: {line}")
            count += 1

    if count:
        print(f"{count} graphics API uses in the shared renderer code. Use graphics_api/gapi.h.")
        return 1
    print(f"OK: {len(files)} shared renderer files use no graphics API directly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
