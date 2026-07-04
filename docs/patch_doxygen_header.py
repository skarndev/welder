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

# Loaded in <head>. We deliberately do NOT call DoxygenAwesomeDarkModeToggle.init()
# — it auto-inserts the toggle button next to #MSearchBox, which the sidebar-only
# layout hides. The script's static constructor still runs on load (applying dark
# mode from the OS/localStorage preference); we place a *visible* toggle ourselves
# in the top bar below (TOPBAR) and drive its icon there.
# The interactive-toc extension is deliberately NOT enabled: it turns a page's
# table of contents into a full-height floating panel on the right, which occupies
# the whole right edge (colliding with any other right-side control) and isn't
# dark-themed. Left off, the TOC renders as a normal inline block (themed by
# doxygen-extra.css).
SCRIPTS = """\
<!-- doxygen-awesome-css extensions (injected by welder docs build) -->
<script type="text/javascript" src="$relpath^doxygen-awesome-darkmode-toggle.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-fragment-copy-button.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-paragraph-link.js"></script>
<script type="text/javascript" src="$relpath^doxygen-awesome-tabs.js"></script>
<script type="text/javascript">
  DoxygenAwesomeFragmentCopyButton.init();
  DoxygenAwesomeParagraphLink.init();
  DoxygenAwesomeTabs.init();
</script>
"""

# A top bar injected at the end of the header: a backlink to the mkdocs guide and a
# reliably-placed light/dark toggle. `$relpath^../index.html` resolves from any
# api/ page up to the site root (the guide home). The toggle element upgrades from
# doxygen-awesome-darkmode-toggle.js; we call updateIcon() ourselves since we don't
# use its auto-inserting init().
TOPBAR = """\
<div id="welder-topbar">
  <a id="welder-guide-link" href="$relpath^../index.html" title="Back to the welder documentation">&#8592;&#160;welder&#160;docs</a>
  <doxygen-awesome-dark-mode-toggle id="welder-dark-toggle" title="Toggle light / dark mode"></doxygen-awesome-dark-mode-toggle>
</div>
<script type="text/javascript">
  (function () {
    function upd() {
      var t = document.getElementById("welder-dark-toggle");
      if (t && t.updateIcon) { t.updateIcon(); }
    }
    if (document.readyState !== "loading") { upd(); }
    else { document.addEventListener("DOMContentLoaded", upd); }
    try {
      window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", upd);
    } catch (e) {}
    document.addEventListener("visibilitychange", function () {
      if (document.visibilityState === "visible") { upd(); }
    });
  })();
</script>
"""


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    try:
        with open(src, "r", encoding="utf-8") as f:
            html = f.read()
        if "doxygen-awesome-darkmode-toggle.js" not in html:
            head_marker = "</head>"
            if head_marker in html:
                html = html.replace(head_marker, SCRIPTS + head_marker, 1)
            # Place the top bar as a <body> child *before* #top. In the sidebar-only
            # layout #top is a narrow, fixed-height, overflow:hidden sidebar header
            # (it holds the search box); injecting into it clips the bar and crowds
            # search out. As a sibling before #top the bar can float (position:fixed)
            # without disturbing that layout.
            bar_marker = '<div id="top">'
            if bar_marker in html:
                html = html.replace(bar_marker, TOPBAR + bar_marker, 1)
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
