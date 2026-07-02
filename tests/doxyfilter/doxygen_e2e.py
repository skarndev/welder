#!/usr/bin/env python3
"""End-to-end check of the welder Doxygen filter: run doxygen over the corpus
with INPUT_FILTER set, then assert that every expected doc text actually landed
in the generated XML (i.e. Doxygen *attached* the translated comments, not just
that the filter emitted them — placement is the fragile part).

usage: doxygen_e2e.py <doxygen> <filter.py> <corpus.hpp> <workdir>
"""
import glob
import pathlib
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
    missing = [m for m in MUST if m not in xml]
    for m in missing:
        print('MISSING from Doxygen XML:', repr(m))
    print(f'{len(MUST) - len(missing)}/{len(MUST)} doc texts attached')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
