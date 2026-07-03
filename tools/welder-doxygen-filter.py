#!/usr/bin/env python3
"""welder-doxygen-filter — Doxygen INPUT_FILTER translating welder's C++26
annotations into Doxygen comments, on the fly.

Doxygen has no plugin system; INPUT_FILTER is its extension point — a program
run per input file whose stdout is what Doxygen parses. The sources on disk
are never modified. Requires the `lark` package (pip install lark). Configure:

    INPUT_FILTER    = "python3 /path/to/welder-doxygen-filter.py"
    # or per extension:
    FILTER_PATTERNS = *.hpp="python3 /path/to/welder-doxygen-filter.py"

How it works
------------
All lexing and parsing lives in the grammar next to this script
(welder-doxygen-filter.lark, see its header comment): layer 1 lexes the file
into a total token stream whose comments and string/char/raw literals are
single atomic tokens (annotation-shaped text inside them is invisible);
layer 2 parses one [[ ... ]] block's content into comma-separated elements
with nested balanced groups. This script is only the driver:

  * find each [[ ... ]] block's extent in the token stream (`]]` is
    context-dependent in C++ — see the grammar header — so this is a small
    bracket-depth scan, not grammar work);
  * classify each parsed element:
      =welder::doc("text")          -> comment summary
      =welder::returns("text")      -> @return text
      =welder::tparam("T", "text")  -> @tparam T text
      any other =welder::...        -> stripped (weld/policy/mark/trust are
                                       binding controls; doc *scope* control
                                       is Doxygen-native — EXCLUDE_SYMBOLS)
      everything else               -> kept: standard attributes
                                       ([[nodiscard]], [[deprecated("...")]])
                                       and foreign annotations
                                       ([[=other::thing]]) are re-emitted in
                                       place in a rebuilt block.
    A block with no welder elements passes through byte-identical.
  * place the Doxygen comment (probed rules — Doxygen attaches positionally):
      - keyword position: `struct [[...]] Name` (also class/union/enum
        [class|struct]/namespace) hoists the comment BEFORE the keyword and
        before any `template <...>` head(s) — a comment between keyword and
        name does not attach;
      - parameter position (inside parens): a trailing `/**< ... */` before
        the parameter's ending top-level `,` or `)`, tracking template
        angles, nested parens and brace default arguments;
      - trailing position (`Enumerator [[...]]` before `,` `}` `;`):
        a trailing `/**< ... */` in place;
      - otherwise: a `/** ... */` block in place, indentation preserved.

Fail-safety contract
--------------------
The filter must never break a doc build, whatever the input:
  * lexing is total (grammar layer 1) — arbitrary bytes, unterminated
    literals and non-UTF-8 (surrogateescape in, byte-exact stdout) all lex;
  * each block is handled in its own try/except — one the grammar cannot
    parse is left verbatim (Doxygen ignores [[...]] blocks natively);
  * a last-resort try/except emits the whole file unchanged on any error
    (missing lark included), with a note on stderr: the file is still
    documented, only its welder annotations are lost;
  * exit status is 0 in all of these cases; 2 only for wrong usage.

Known limits: annotations must be spelled welder::-qualified (`::welder::…`
also works; a namespace *alias* does not); doc text must be inline string
literals — which the annotation design
already forces (fixed_string; a const char* is not a permitted annotation
constant); a `<`-containing *expression* in a default argument (e.g.
`int n = a < b`) can confuse the parameter-end scan.
"""

import pathlib
import sys
from typing import NamedTuple

DOC_KINDS = frozenset({'doc', 'returns', 'tparam'})
CLASS_KEYWORDS = frozenset({'struct', 'class', 'union', 'enum', 'namespace'})
SKIP = frozenset({'WS', 'BLOCK_COMMENT', 'LINE_COMMENT'})  # non-significant
PREFIX = 'welder::'

_parser = None


def parser():
    global _parser
    if _parser is None:
        import lark
        grammar = pathlib.Path(__file__).resolve().with_suffix('.lark')
        _parser = lark.Lark(grammar.read_text(encoding='utf-8'),
                            start=['unit', 'attr_list'],
                            parser='earley', lexer='basic',
                            keep_all_tokens=True, propagate_positions=True)
    return _parser


# =============================================================================
# Annotation blocks: locate in the token stream, classify parsed elements
# =============================================================================

class Block(NamedTuple):
    open_ti: int      # token index of the [[
    close_ti: int     # one past the block's last token (the second ])
    start: int        # char offset of the [[
    end: int          # char offset one past the ]]
    paren_depth: int  # ( )-nesting at the block: >0 = in a parameter list


def find_blocks(toks):
    """Every *terminated* [[ ... ]] block, in order. Comments and literals
    are atomic tokens, so blocks inside them are never seen; an unterminated
    [[ (broken input) yields no block — it must stay verbatim, an edit around
    it could swallow code."""
    blocks = []
    paren = 0
    i = 0
    while i < len(toks):
        t = toks[i]
        if t.type == 'ATTR_OPEN':
            closed = block_close(toks, i)
            if closed is None:
                break  # unterminated: nothing after it can be a block either
            j, end = closed
            blocks.append(Block(i, j, t.start_pos, end, paren))
            i = j
        else:
            if t == '(':
                paren += 1
            elif t == ')':
                paren = max(0, paren - 1)
            i += 1
    return blocks


def block_close(toks, i):
    """i at the [[ token; find the matching ]] — two *adjacent* ] at bracket
    depth 0 (a ] inside a nested group or a string argument cannot close the
    block). Returns (one past the ]] tokens, one past the ]] char offset),
    or None for an unterminated block."""
    depth = 0
    j = i + 1
    while j < len(toks):
        t = toks[j]
        if t.type == 'ATTR_OPEN':
            depth += 2
        elif t == '[':
            depth += 1
        elif t == ']':
            if depth:
                depth -= 1
            elif (j + 1 < len(toks) and toks[j + 1] == ']'
                  and toks[j + 1].start_pos == t.end_pos):
                return j + 2, toks[j + 1].end_pos
        j += 1
    return None


def classify(element, inner):
    """One parsed attribute element (a layer-2 `element` tree; `inner` is the
    text it was parsed from) ->
    ('keep', source-text) | ('strip', None) | (doc-kind, [string args])."""
    from lark import Token
    kids = element.children
    if not kids:
        return ('keep', '')
    kept = ('keep', inner[element.meta.start_pos:element.meta.end_pos])
    lead = [k.type if isinstance(k, Token) else 'group' for k in kids[:5]]
    if lead[:1] != ['EQUAL']:
        return kept                      # standard attribute
    w = 2 if lead[1:2] == ['COLONCOLON'] else 1  # =::welder::… also welds
    if lead[w:w + 3] != ['WELDER', 'COLONCOLON', 'WORD']:
        return kept                      # foreign annotation
    kind = str(kids[w + 2])
    if kind not in DOC_KINDS:
        return ('strip', None)  # weld / policy / mark / trust_bindable / ...
    args = [unescape(t) for t in element.scan_values(
        lambda v: isinstance(v, Token) and v.type == 'STRING')]
    return (kind, args)


def unescape(string_token):
    """A STRING token's value with the common escapes decoded."""
    body = str(string_token)
    body = body[body.index('"') + 1:-1]  # drop quotes and encoding prefix
    return (body.replace('\\n', '\n').replace('\\t', '\t')
                .replace('\\"', '"').replace('\\\\', '\\'))


# =============================================================================
# Comment rendering
# =============================================================================

def comment_lines(parts):
    """parts: {'doc': [[text]...], 'tparam': [[name,text]...], 'returns': ...}
    -> the comment's logical lines, or []."""
    lines = []
    for args in parts['doc']:
        if args:
            lines.extend(args[0].split('\n'))
    for args in parts['tparam']:
        if len(args) >= 2:
            lines.append('@tparam ' + args[0] + ' ' + args[1])
    for args in parts['returns']:
        if args:
            lines.append('@return ' + args[0])
    return lines


def block_comment(lines, indent):
    if len(lines) == 1:
        return '/** ' + lines[0] + ' */'
    body = ('\n' + indent + ' * ').join(lines)
    return '/**\n' + indent + ' * ' + body + '\n' + indent + ' */'


def inline_comment(lines):
    return '/**< ' + ' '.join(lines) + ' */'


# =============================================================================
# Placement
# =============================================================================

def prev_sig(toks, i):
    """Index of the significant (non-ws, non-comment) token before i, or None."""
    i -= 1
    while i >= 0 and toks[i].type in SKIP:
        i -= 1
    return i if i >= 0 else None


def keyword_hoist(text, toks, bi):
    """If the block at token bi directly follows struct/class/union/enum
    [class|struct]/namespace, the hoist point: the keyword's char offset,
    extended back over `template <...>` head(s). None otherwise."""
    p = prev_sig(toks, bi)
    if p is None or toks[p] not in CLASS_KEYWORDS:
        return None
    if toks[p] in ('class', 'struct'):  # enum class / enum struct
        q = prev_sig(toks, p)
        if q is not None and toks[q] == 'enum':
            p = q
    while True:  # `template <...>` head(s) — and requires-clauses — above
        tm = head_start_above(text, toks, p)
        if tm is None:
            return toks[p].start_pos
        p = tm


def head_start_above(text, toks, p):
    """Token index of the `template` introducing the head directly above
    token p, or None. Handles both a plain head (`template <...>`) and one
    with a trailing requires-CLAUSE (`template <...> requires C<T> && (...)`)
    — the comment must hoist above the whole thing to attach."""
    q = prev_sig(toks, p)
    if q is None:
        return None
    if toks[q] == '>':  # plain `template <...>` directly above?
        tm = template_of_angle(text, toks, q)
        if tm is not None:
            return tm
    # Otherwise q may end a requires-clause; verify the full shape
    # `template <...> requires <constraint>` before trusting it.
    rq = requires_clause_start(text, toks, q)
    if rq is None:
        return None
    q = prev_sig(toks, rq)
    if q is None or toks[q] != '>':
        return None
    return template_of_angle(text, toks, q)


def template_of_angle(text, toks, q):
    """toks[q] is the '>' of a candidate template head: the index of its
    `template` keyword, or None."""
    lt = angle_open(text, toks, q)
    if lt is None:
        return None
    tm = prev_sig(toks, lt)
    return tm if tm is not None and toks[tm] == 'template' else None


def requires_clause_start(text, toks, j, limit=512):
    """toks[j] ends a candidate requires-clause: the index of its `requires`
    keyword, or None if the shape isn't conservatively constraint-like.
    Walks back over balanced (...)/{...}/<...> groups and the tokens a
    constraint-expression may contain; anything else bails (the caller then
    falls back to not hoisting — never worse than before)."""
    steps = 0
    while j >= 0 and steps < limit:
        steps += 1
        t = toks[j]
        if t.type in SKIP:
            j -= 1
        elif t.type == 'WORD' and t == 'requires':
            # `requires requires(T t) {...}`: the inner keyword is part of
            # the constraint — keep walking to the clause-introducing one.
            p = prev_sig(toks, j)
            if p is not None and toks[p] == 'requires':
                j = p
            else:
                return j
        elif t in (')', '}'):
            k = group_open(toks, j)
            if k is None:
                return None
            j = k - 1
        elif t == '>':
            k = angle_open(text, toks, j)
            if k is None:
                return None
            j = k - 1
        elif t.type in ('WORD', 'WELDER', 'NUMBER', 'COLONCOLON') \
                or t in ('&', '|', '!', ',', '.'):
            j -= 1
        else:
            return None
    return None


def group_open(toks, j):
    """toks[j] is ')' or '}': token index of its matching opener, or None."""
    close = str(toks[j])
    open_ = {')': '(', '}': '{'}[close]
    depth = 1
    j -= 1
    while j >= 0:
        t = toks[j]
        if t == close:
            depth += 1
        elif t == open_:
            depth -= 1
            if depth == 0:
                return j
        j -= 1
    return None


def angle_open(text, toks, j):
    """toks[j] is '>'; token index of the matching '<', or None."""
    depth = 1
    j -= 1
    while j >= 0:
        t = toks[j]
        if t == '>' and (t.start_pos == 0 or text[t.start_pos - 1] != '-'):
            depth += 1
        elif t == '<':
            depth -= 1
            if depth == 0:
                return j
        j -= 1
    return None


def param_end(text, toks, i):
    """From token i (just past the block, inside a parameter list): the char
    offset of the parameter's ending top-level ',' or ')'. Template angles,
    nested parens and brace/bracket default arguments are tracked; literals
    are atomic tokens, so their contents never count."""
    paren = angle = brace = square = 0
    while i < len(toks):
        t = toks[i]
        prev = text[t.start_pos - 1] if t.start_pos else ''
        if t.type == 'ATTR_OPEN':
            square += 2
        elif t == '(':
            paren += 1
        elif t == ')':
            if paren == 0:
                return t.start_pos
            paren -= 1
        elif t == ',' and paren == angle == brace == square == 0:
            return t.start_pos
        elif t == '<' and (prev.isalnum() or prev == '_' or prev in '>:'):
            angle += 1  # heuristically a template angle, not a comparison
        elif t == '>' and angle and prev != '-':
            angle -= 1
        elif t == '{':
            brace += 1
        elif t == '}':
            brace = max(0, brace - 1)
        elif t == '[':
            square += 1
        elif t == ']':
            square = max(0, square - 1)
        i += 1
    return len(text)


def line_indent(text, pos):
    """The whitespace prefix of pos's line if pos sits in it, else ''."""
    ls = text.rfind('\n', 0, pos) + 1
    prefix = text[ls:pos]
    return prefix if prefix.strip() == '' else ''


def whole_lines(text, s, e):
    """Widen (s, e) to swallow the full line(s) when the annotation is alone
    on them (deleting it should not leave blank lines)."""
    ls = text.rfind('\n', 0, s) + 1
    if text[ls:s].strip():
        return s, e
    le = text.find('\n', e)
    if le < 0:
        return ls, e
    if text[e:le].strip():
        return s, e
    return ls, le + 1


def next_code_char(text, i):
    while i < len(text) and text[i].isspace():
        i += 1
    return text[i] if i < len(text) else ''


# =============================================================================
# The transform
# =============================================================================

def block_edits(text, toks, blk):
    """The (start, end, replacement) edits for one annotation block."""
    inner = text[blk.start + 2:blk.end - 2]
    if PREFIX not in inner:
        return []
    kept = []
    parts = {'doc': [], 'tparam': [], 'returns': []}
    saw_welder = False
    tree = parser().parse(inner, start='attr_list')
    for element in tree.find_data('element'):
        kind, val = classify(element, inner)
        if kind == 'keep':
            kept.append(val)
        else:
            saw_welder = True
            if kind != 'strip':
                parts[kind].append(val)
    if not saw_welder:
        return []
    kept_block = '[[' + ', '.join(kept) + ']]' if kept else ''
    lines = comment_lines(parts)
    s, e = blk.start, blk.end
    edits = []

    kw = keyword_hoist(text, toks, blk.open_ti)
    prev_ch = text[:s].rstrip()[-1:]

    if kw is not None:
        if lines:
            indent = line_indent(text, kw)
            edits.append((kw, kw, block_comment(lines, indent) + '\n' + indent))
        if kept_block:
            edits.append((s, e, kept_block))
        else:
            edits.append(whole_lines(text, s, e) + ('',))
    elif blk.paren_depth > 0:
        edits.append((s, e, kept_block))
        if lines:
            stop = param_end(text, toks, blk.close_ti)
            ins = e + len(text[e:stop].rstrip())
            edits.append((ins, ins, ' ' + inline_comment(lines)))
    elif (prev_ch.isalnum() or prev_ch == '_') and \
            next_code_char(text, e) in (',', '}', ';'):
        repl = kept_block + (' ' if kept_block and lines else '') + \
            (inline_comment(lines) if lines else '')
        if repl:
            edits.append((s, e, repl))
        else:  # nothing left: also eat the space before the annotation
            ns = len(text[:s].rstrip())
            edits.append((ns, e, ''))
    else:
        indent = line_indent(text, s)
        repl = block_comment(lines, indent) if lines else ''
        if kept_block:
            repl += ('\n' + indent if repl else '') + kept_block
        if repl:
            edits.append((s, e, repl))
        else:
            edits.append(whole_lines(text, s, e) + ('',))
    return edits


def transform(text):
    if PREFIX not in text or '[[' not in text:
        return text
    toks = list(parser().lex(text, dont_ignore=True))
    edits = []
    for blk in find_blocks(toks):
        try:
            edits.extend(block_edits(text, toks, blk))
        except Exception as exc:  # fail-safe: an unhandled block stays verbatim
            sys.stderr.write('welder-doxygen-filter: [[...]] block at offset '
                             '%d left verbatim: %r\n' % (blk.start, exc))
            continue
    for start, end, repl in sorted(edits, key=lambda t: t[0], reverse=True):
        text = text[:start] + repl + text[end:]
    return text


def main(argv):
    if len(argv) != 2:
        sys.stderr.write('usage: welder-doxygen-filter.py <source-file>\n')
        return 2
    with open(argv[1], 'rb') as f:
        text = f.read().decode('utf-8', errors='surrogateescape')
    try:
        out = transform(text)
    except Exception as exc:  # fail-safe: never break the doc build
        hint = ' (pip install lark?)' if isinstance(exc, ImportError) else ''
        sys.stderr.write('welder-doxygen-filter: %s: %r%s — passing the file '
                         'through unfiltered\n' % (argv[1], exc, hint))
        out = text
    sys.stdout.buffer.write(out.encode('utf-8', errors='surrogateescape'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
