#!/usr/bin/env python3
"""Inject the doxygen-awesome-css extensions into a generated Doxygen header.

doxygen-awesome's *base* theme needs no header changes (just HTML_EXTRA_STYLESHEET),
but the nice-to-have extensions — dark-mode toggle, fragment copy buttons,
paragraph links, interactive ToC, tabs — are JavaScript that must be wired into the
HTML <head>. Doxygen generates a default header (`doxygen -w html …`); this patches
it in place.

Usage: patch_doxygen_header.py <generated-header.html> <patched-header.html>

Fail-safe: if the marker can't be found the header is copied through unchanged (the
base theme still applies), and we exit 0 — a docs build must never break on this.
"""
import sys

SCRIPTS = """\
<!-- doxygen-awesome-css extensions (injected by welder docs build) -->
<script type="text/javascript" src="$relpath^doxygen-awesome-darkmode-toggle.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-fragment-copy-button.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-paragraph-link.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-interactive-toc.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-tabs.js"></script>
<script type="text/javascript">
  DoxygenAwesomeDarkModeToggle.init();
  DoxygenAwesomeFragmentCopyButton.init();
  DoxygenAwesomeParagraphLink.init();
  DoxygenAwesomeInteractiveToc.init();
  DoxygenAwesomeTabs.init();
</script>
"""


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    try:
        with open(src, "r", encoding="utf-8") as f:
            html = f.read()
        marker = "</head>"
        if marker in html and "doxygen-awesome-darkmode-toggle.js" not in html:
            html = html.replace(marker, SCRIPTS + marker, 1)
        with open(dst, "w", encoding="utf-8") as f:
            f.write(html)
    except OSError as e:
        # Last resort: emit nothing special; the caller falls back to the default
        # header. Never fail the docs build over the theme extensions.
        sys.stderr.write(f"patch_doxygen_header: {e}\n")
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
