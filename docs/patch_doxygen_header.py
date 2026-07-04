#!/usr/bin/env python3
"""Inject welder's header customizations into a generated Doxygen header.

Doxygen 1.17 ships **no jQuery**, so doxygen-awesome's `init()`-based extensions
(dark-mode toggle, copy buttons, …) silently fail. And its dark-mode *class* is a
top-level `class` in a classic script — a global lexical binding, not a `window`
property — which makes it awkward and brittle to drive from another script.

So we do it ourselves, fully self-contained: doxygen-awesome's CSS keys dark mode
purely on `html.dark-mode` (+ `prefers-color-scheme`), so we just manage that class
directly and persist the choice in localStorage — no doxygen-awesome JS at all.
We also place a light/dark toggle button next to the search box and link the
project title back to the mkdocs guide.

Usage: patch_doxygen_header.py <generated-header.html> <patched-header.html>

Fail-safe: on any error the header is copied through unchanged and we exit 0 — a
docs build must never break here.
"""
import sys

# `$relpath^../index.html` resolves from any api/ page to the guide home. The saved
# preference is applied immediately (this runs in <head>, before first paint); the
# button + title link are built on DOMContentLoaded.
CONTROLS = """\
<!-- welder: self-contained dark-mode toggle + guide backlink (no jQuery / no awesome JS) -->
<script type="text/javascript">
(function () {
  var GUIDE_URL = "$relpath^../index.html";
  var KEY = "welder-color-scheme";
  var SUN = '<svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M12 7a5 5 0 100 10 5 5 0 000-10zm0-5a1 1 0 011 1v1a1 1 0 11-2 0V3a1 1 0 011-1zm0 17a1 1 0 011 1v1a1 1 0 11-2 0v-1a1 1 0 011-1zM4 12a1 1 0 01-1 1H2a1 1 0 110-2h1a1 1 0 011 1zm18 0a1 1 0 01-1 1h-1a1 1 0 110-2h1a1 1 0 011 1zM5.99 5.99a1 1 0 01-1.41 0l-.71-.71a1 1 0 011.41-1.41l.71.71a1 1 0 010 1.41zm14.02 14.02a1 1 0 01-1.41 0l-.71-.71a1 1 0 011.41-1.41l.71.71a1 1 0 010 1.41zM18.01 5.99a1 1 0 010-1.41l.71-.71a1 1 0 011.41 1.41l-.71.71a1 1 0 01-1.41 0zM3.99 20.01a1 1 0 010-1.41l.71-.71a1 1 0 011.41 1.41l-.71.71a1 1 0 01-1.41 0z"/></svg>';
  var MOON = '<svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M12 3a9 9 0 108.99 9.36A7.002 7.002 0 0112 3z"/></svg>';
  function prefersDark() {
    return !!(window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches);
  }
  function saved() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }
  function isDark() {
    var c = document.documentElement.classList;
    if (c.contains("dark-mode")) { return true; }
    if (c.contains("light-mode")) { return false; }
    return prefersDark();
  }
  function apply(dark) {
    var c = document.documentElement.classList;
    c.toggle("dark-mode", dark);
    c.toggle("light-mode", !dark);
  }
  // Apply the saved choice as early as possible (this runs in <head>); with no
  // saved choice we leave the class off and let prefers-color-scheme decide.
  var s = saved();
  if (s === "dark") { apply(true); }
  else if (s === "light") { apply(false); }
  function setIcon() {
    var b = document.getElementById("welder-dark-toggle");
    if (b) { b.innerHTML = isDark() ? MOON : SUN; }
  }
  function toggle() {
    var dark = !isDark();
    apply(dark);
    try { localStorage.setItem(KEY, dark ? "dark" : "light"); } catch (e) {}
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
                html = html.replace(marker, CONTROLS + marker, 1)
        with open(dst, "w", encoding="utf-8") as f:
            f.write(html)
    except OSError as e:
        sys.stderr.write(f"patch_doxygen_header: {e}\n")
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
