"""mkdocs hook: auto-link inline code spans to the Doxygen C++ reference.

Doxygen emits a **tag file** (``GENERATE_TAGFILE`` in the Doxyfile) next to the
``api/`` HTML — its machine-readable symbol -> page#anchor map. This hook loads it
and, on every rendered page, wraps inline ``<code>`` spans whose text names a welder
API entity (``welder::welder``, ``weld_type<T>(m)``, ``mark::exclude``,
``WELDER_MODULE(ns, rod)``, …) in a link to that entity's reference page. The guide
never hand-writes a mangled Doxygen URL, and a renamed/removed symbol simply stops
linking instead of 404ing.

Resolution is deliberately conservative, in this order:

1. the exact (normalized) span text as a fully qualified name;
2. the span with an implicit ``welder::`` prefix (``naming::none``, ``policy::opt_in``);
3. for a bare identifier, a ``welder::welder::`` member (the entry points:
   ``weld_type``, ``weld_namespace``, …);
4. for a bare identifier of >= 4 chars, a *unique* short name anywhere in the API
   (``pep8``, ``google_style``, ``cleandoc``) — skipped when ambiguous (``rod``).

Normalization strips the annotation wrapper (``[[=…]]``), template argument lists
(``rod<>``, ``welder<Rod, Style, Carriage>``) and trailing call parentheses
(``doc("text")``), so the common ways the guide spells an entity all resolve.

Like ``inject_reference.py``, the tag file is found via the ``WELDER_DOXYGEN_API``
environment variable (it sits *next to* the ``api/`` dir, so it is never published):
without it — e.g. a bare ``mkdocs build`` — the hook is a silent no-op and the guide
builds unlinked.
"""

import html
import os
import re
import xml.etree.ElementTree as ET

# Fully qualified name (or bare define) -> URL relative to <site>/api/.
_symbols: dict[str, str] = {}
# Last path segment -> distinct target URLs, for the unique-short-name fallback.
_short: dict[str, set[str]] = {}

# Compound kinds worth linking to (files/dirs/pages are not API entities).
_COMPOUND_KINDS = {"class", "struct", "union", "namespace", "concept"}
# Member kinds worth linking to (enumvalues resolve via their enumeration page).
_MEMBER_KINDS = {"function", "variable", "typedef", "enumeration", "define"}

# Bare span texts never linked: names that happen to exist in welder's API but
# usually refer to the *reader's* code or a framework's API when spelled bare
# (`detail`/`impl` namespaces, pybind11's `export_values()`). Qualified spellings
# (`welder::detail::…`) still resolve.
_DENY = {"detail", "impl", "export_values", "name", "value", "type", "module", "main"}

_QUALIFIED = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*")
_CODE_SPAN = re.compile(r"<code>([^<>]+)</code>")
# Regions that must stay untouched: existing links and fenced/mermaid blocks.
_SKIP_REGION = re.compile(r"<a\b.*?</a>|<pre\b.*?</pre>", re.S)


def _register(name: str, url: str) -> None:
    # First registration wins (overloads share a page; any anchor of the set is fine).
    _symbols.setdefault(name, url)
    _short.setdefault(name.rsplit("::", 1)[-1], set()).add(_symbols[name])


def _load_tagfile(path: str) -> None:
    root = ET.parse(path).getroot()
    for compound in root.findall("compound"):
        kind = compound.get("kind")
        cname = compound.findtext("name") or ""
        cfile = compound.findtext("filename") or ""
        if kind in _COMPOUND_KINDS and cfile:
            if not cfile.endswith(".html"):
                cfile += ".html"
            _register(cname, cfile)
        if kind not in _COMPOUND_KINDS and kind != "file":
            continue
        for member in compound.findall("member"):
            if member.get("kind") not in _MEMBER_KINDS:
                continue
            mname = member.findtext("name") or ""
            anchorfile = member.findtext("anchorfile") or ""
            anchor = member.findtext("anchor") or ""
            if not (mname and anchorfile):
                continue
            url = anchorfile + ("#" + anchor if anchor else "")
            if member.get("kind") == "define":
                _register(mname, url)  # macros are global; bare name
            elif kind in _COMPOUND_KINDS:
                _register(cname + "::" + mname, url)


def _strip_trailing_parens(text: str) -> str:
    """Drop one balanced ``(...)`` group at the very end (a call's arguments)."""
    if not text.endswith(")"):
        return text
    depth = 0
    for i in range(len(text) - 1, -1, -1):
        if text[i] == ")":
            depth += 1
        elif text[i] == "(":
            depth -= 1
            if depth == 0:
                return text[:i].rstrip()
    return text


def _strip_angle_groups(text: str) -> str:
    """Remove balanced ``<...>`` groups (template arguments) anywhere."""
    out: list[str] = []
    depth = 0
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth = max(depth - 1, 0)
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def _normalize(span_text: str) -> str | None:
    """Reduce an inline-code span to a lookupable name, or None if it isn't one."""
    t = html.unescape(span_text).strip()
    if t.startswith("[[") and t.endswith("]]"):
        t = t[2:-2].strip()
    t = t.removeprefix("=").lstrip()
    t = _strip_trailing_parens(t)
    t = _strip_angle_groups(t)
    t = _strip_trailing_parens(t)  # weld_type<T>(m): parens follow the template args
    t = t.strip().rstrip("&*").removesuffix("::").removeprefix("::")
    return t if _QUALIFIED.fullmatch(t) else None


def _resolve(name: str) -> tuple[str, str] | None:
    """Return (qualified name, api-relative URL), or None."""
    if name in _DENY:
        return None
    for candidate in (name, "welder::" + name):
        if candidate in _symbols:
            return candidate, _symbols[candidate]
    if "::" not in name:
        entry = "welder::welder::" + name
        if entry in _symbols:
            return entry, _symbols[entry]
        if len(name) >= 4 and len(_short.get(name, ())) == 1:
            (url,) = _short[name]
            return name, url
    return None


def on_config(config, **kwargs):
    _symbols.clear()
    _short.clear()
    api = os.environ.get("WELDER_DOXYGEN_API")
    if not api:
        return
    tagfile = os.path.join(os.path.dirname(api), "welder.tag")
    if os.path.isfile(tagfile):
        _load_tagfile(tagfile)


def on_page_content(content, page, config, files):
    if not _symbols:
        return content
    to_root = "../" * page.url.count("/")

    def link_span(match: re.Match) -> str:
        name = _normalize(match.group(1))
        resolved = _resolve(name) if name else None
        if not resolved:
            return match.group(0)
        qualified, url = resolved
        return (
            f'<a class="welder-apilink" href="{to_root}api/{url}" '
            f'title="{html.escape(qualified)} — C++ reference">{match.group(0)}</a>'
        )

    # Leave existing links and code blocks alone; transform only the gaps between.
    out: list[str] = []
    pos = 0
    for region in _SKIP_REGION.finditer(content):
        out.append(_CODE_SPAN.sub(link_span, content[pos : region.start()]))
        out.append(region.group(0))
        pos = region.end()
    out.append(_CODE_SPAN.sub(link_span, content[pos:]))
    return "".join(out)