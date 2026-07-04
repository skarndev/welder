#!/usr/bin/env python3
"""Inject welder's header customizations into a generated Doxygen header.

Doxygen 1.17 ships **no jQuery**, so doxygen-awesome's `init()`-based extensions
(dark-mode toggle, copy buttons, …) silently fail — they call `$(function(){…})`.
The theme's *static* dark-mode loader still runs (no jQuery), so the theme applies
the OS/localStorage color scheme on load; only the visible controls are missing.

So we wire our own, in plain JS:
  * a light/dark toggle button placed next to the search box in the header
    (it reuses doxygen-awesome's DoxygenAwesomeDarkModeToggle for persistence when
    present, else toggles the html class directly),
  * the project title linked back to the mkdocs guide.

Usage: patch_doxygen_header.py <generated-header.html> <patched-header.html>

Fail-safe: on any error the header is copied through unchanged and we exit 0 — a
docs build must never break here.
"""
import sys

# Only the dark-mode script is loaded: its class + static loader are jQuery-free
# and we reuse them. The other awesome extensions need jQuery (absent in 1.17).
HEAD = """\
<!-- welder: doxygen-awesome dark-mode class + static loader (jQuery-free) -->
<script type="text/javascript" src="$relpath^doxygen-awesome-darkmode-toggle.js"></script>
"""

# A sun/moon icon set (fill: currentColor) + the controls builder. Runs on
# DOMContentLoaded; `$relpath^../index.html` resolves from any api/ page to the
# guide home.
CONTROLS = """\
<!-- welder: header controls (dark-mode toggle + guide backlink) -->
<script type="text/javascript">
(function () {
  var GUIDE_URL = "$relpath^../index.html";
  var SUN = '<svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M12 7a5 5 0 100 10 5 5 0 000-10zm0-5a1 1 0 011 1v1a1 1 0 11-2 0V3a1 1 0 011-1zm0 17a1 1 0 011 1v1a1 1 0 11-2 0v-1a1 1 0 011-1zM4 12a1 1 0 01-1 1H2a1 1 0 110-2h1a1 1 0 011 1zm18 0a1 1 0 01-1 1h-1a1 1 0 110-2h1a1 1 0 011 1zM5.99 5.99a1 1 0 01-1.41 0l-.71-.71a1 1 0 011.41-1.41l.71.71a1 1 0 010 1.41zm14.02 14.02a1 1 0 01-1.41 0l-.71-.71a1 1 0 011.41-1.41l.71.71a1 1 0 010 1.41zM18.01 5.99a1 1 0 010-1.41l.71-.71a1 1 0 011.41 1.41l-.71.71a1 1 0 01-1.41 0zM3.99 20.01a1 1 0 010-1.41l.71-.71a1 1 0 011.41 1.41l-.71.71a1 1 0 01-1.41 0z"/></svg>';
  var MOON = '<svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M12 3a9 9 0 108.99 9.36A7.002 7.002 0 0112 3z"/></svg>';
  function isDark() { return document.documentElement.classList.contains("dark-mode"); }
  function setIcon() {
    var b = document.getElementById("welder-dark-toggle");
    if (b) { b.innerHTML = isDark() ? MOON : SUN; }
  }
  function toggle() {
    try {
      if (window.DoxygenAwesomeDarkModeToggle) {
        DoxygenAwesomeDarkModeToggle.userPreference = !DoxygenAwesomeDarkModeToggle.userPreference;
      } else {
        var h = document.documentElement;
        h.classList.toggle("dark-mode");
        h.classList.toggle("light-mode");
      }
    } catch (e) {}
    setIcon();
  }
  function build() {
    var box = document.getElementById("MSearchBox");
    if (box && box.parentNode && !document.getElementById("welder-dark-toggle")) {
      var btn = document.createElement("button");
      btn.id = "welder-dark-toggle";
      btn.type = "button";
      btn.title = "Toggle light / dark mode";
      btn.setAttribute("aria-label", "Toggle light / dark mode");
      btn.addEventListener("click", toggle);
      box.parentNode.appendChild(btn);
    }
    setIcon();
    var pn = document.getElementById("projectname");
    if (pn && !pn.querySelector("a.welder-guide-link")) {
      var a = document.createElement("a");
      a.href = GUIDE_URL;
      a.className = "welder-guide-link";
      a.title = "Back to the welder documentation";
      while (pn.firstChild) { a.appendChild(pn.firstChild); }
      pn.appendChild(a);
    }
  }
  if (document.readyState !== "loading") { build(); }
  else { document.addEventListener("DOMContentLoaded", build); }
  try { window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", setIcon); } catch (e) {}
  document.addEventListener("visibilitychange", function () {
    if (document.visibilityState === "visible") { setIcon(); }
  });
})();
</script>
"""


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    try:
        with open(src, "r", encoding="utf-8") as f:
            html = f.read()
        if "welder-dark-toggle" not in html:
            marker = "</head>"
            if marker in html:
                html = html.replace(marker, HEAD + CONTROLS + marker, 1)
        with open(dst, "w", encoding="utf-8") as f:
            f.write(html)
    except OSError as e:
        sys.stderr.write(f"patch_doxygen_header: {e}\n")
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
