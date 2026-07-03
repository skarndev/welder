#!/usr/bin/env python3
"""End-to-end check of the welder Doxygen filter: run doxygen over the corpus
with INPUT_FILTER set, then assert that every expected doc text actually landed
in the generated XML (i.e. Doxygen *attached* the translated comments, not just
that the filter emitted them — placement is the fragile part).

usage: doxygen_e2e.py <doxygen> <filter.py> <corpus.hpp> <workdir>
"""
import glob
import html
import pathlib
import re
import subprocess
import sys

MUST = [
    # namespace / keyword positions (hoisted comments)
    "Workshop namespace: the filter test corpus.",
    "Mixed block: nodiscard stays, weld goes.",
    "Single-line struct.",
    "Nested helpers.",
    "Task state.",
    # members, incl. ones sharing a rebuilt attribute block
    "legacy flag",
    "foreign annotation kept",
    "number of live entries",
    # the flagship template case: tparams + interior docs
    "An ordered dictionary.",
    "the key type",
    "the mapped type",
    "Look a key up.",
    "the mapped value, default-constructed if absent",
    "the key to search",
    "fallbacks, keyed like the dict",
    # function template with tparam/returns + comment between template head
    # and declaration (attachment verified here)
    "Clamp a value to a closed range.",
    "any totally ordered type",
    "v, lo or hi",
    "lower bound",
    # enumerator trailing docs
    "nothing queued",
    # parameters with template-arg commas and brace default arguments
    "Merge two maps.",
    "the merged map",
    "the base map",
    "overrides, win on clash",
    # C++20/26 shapes: the comment hoists over `template <...> requires ...`
    # whatever the constraint ends with; Doxygen must still attach it
    "Requires-paren struct.",
    "Requires-concept struct.",
    "Requires-chain struct.",
    "Requires-requires struct.",
    "Constrained-parameter struct.",
    "Function with a trailing requires.",
    "input value",
    "Root-qualified spelling.",
    # reflection operator (^^) in initializers / default template arguments,
    # splice ([: :]) as a member type — Doxygen parses past both
    "A reflection constant.",
    "Reflection-parameterized.",
    "Splice-typed member holder.",
    "spliced-type member",
    # the `<` ambiguity: shifts/comparisons in default arguments must not
    # swallow the parameter's comma (each doc attaches to ITS parameter)
    "Angles, comparisons and shifts, one signature.",
    "shift, not angles",
    "comparison, not angles",
    "real template arguments",
    "angles closed by >>",
    "Requires nested-angle struct.",
    # adjacent string literals concatenate (phase 6)
    "Concatenated doc text.",
    # trailing docs on initialized enumerators/members go after the initializer
    "Levels.",
    "lowest level",
    "Defaults holder.",
    "post-initializer doc",
    # the bare-comparison function's own brief still attaches...
    "Bare comparison function.",
]

# ...but texts here are ones DOXYGEN ITSELF loses (probed, 1.16): a bare,
# unparenthesized `<` comparison in a default argument derails Doxygen's own
# parameter parsing — it drops that parameter's doc and the rest of the list
# (parenthesizing repairs it; see clash() in the corpus). The filter's output
# for these is still correct and golden-locked; attachment is reported here
# but not asserted either way, so a Doxygen version that fixes it doesn't
# break the test — just watch for these flipping to attached.
DOXYGEN_LOSES = [
    "bare comparison param",
]


def main():
    doxygen, filter_py, corpus, workdir = sys.argv[1:5]
    wd = pathlib.Path(workdir)
    wd.mkdir(parents=True, exist_ok=True)
    (wd / 'Doxyfile').write_text(f'''
INPUT = {corpus}
INPUT_FILTER = "{sys.executable} {filter_py}"
EXTRACT_ALL = YES
GENERATE_HTML = NO
GENERATE_LATEX = NO
GENERATE_XML = YES
QUIET = YES
''')
    subprocess.run([doxygen, 'Doxyfile'], cwd=wd, check=True)
    xml = "".join(open(f, encoding='utf-8').read()
                  for f in glob.glob(str(wd / 'xml' / '*.xml')))
    # Compare against a tag-stripped, entity-decoded view: Doxygen auto-links
    # words matching entity names (<ref>Defaults</ref> holder.) and escapes
    # &/</> in doc text, so the raw XML never contains such texts verbatim.
    plain = html.unescape(re.sub(r'<[^>]+>', '', xml))
    missing = [m for m in MUST if m not in plain]
    for m in missing:
        print('MISSING from Doxygen XML:', repr(m))
    print(f'{len(MUST) - len(missing)}/{len(MUST)} doc texts attached')
    for m in DOXYGEN_LOSES:  # informational: known Doxygen-side losses
        state = 'attached (!)' if m in plain else 'lost, as expected'
        print(f'known Doxygen limit: {m!r} — {state}')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
