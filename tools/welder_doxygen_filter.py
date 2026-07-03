#!/usr/bin/env python3
"""Doxygen INPUT_FILTER translating welder's C++26 annotations into Doxygen
comments, on the fly.

Doxygen has no plugin system; INPUT_FILTER is its extension point — a program
run per input file whose stdout is what Doxygen parses. The sources on disk
are never modified. Requires the ``lark`` package (``pip install lark``).
Configure::

    INPUT_FILTER    = "python3 /path/to/welder_doxygen_filter.py"
    # or per extension:
    FILTER_PATTERNS = *.hpp="python3 /path/to/welder_doxygen_filter.py"

How it works:
    All lexing and parsing lives in the grammar next to this script
    (welder_doxygen_filter.lark, see its header comment). Layer 1 lexes the
    file into a total token stream whose comments and string/char/raw
    literals are single atomic tokens — annotation-shaped text inside them
    is invisible — and whose maximal-munch operators (``<<``, ``>>``,
    ``<=``, ``>=``, ``<=>``, ``->``) can never be mistaken for template
    angle brackets. Layer 2 parses one ``[[ ... ]]`` block's content into
    comma-separated elements with nested balanced groups. This script is
    only the driver:

    * find each block's extent in the token stream (``]]`` is
      context-dependent in C++ — see the grammar header — so this is a
      small bracket-depth scan, not grammar work);
    * classify each parsed element::

        =welder::doc("text")          -> comment summary
        =welder::returns("text")      -> @return text
        =welder::tparam("T", "text")  -> @tparam T text
        any other =welder::...        -> stripped (weld/policy/mark/trust
                                         are binding controls; doc *scope*
                                         control is Doxygen-native —
                                         EXCLUDE_SYMBOLS)
        everything else               -> kept: standard attributes
                                         ([[nodiscard]],
                                         [[deprecated("...")]]) and foreign
                                         annotations ([[=other::thing]])
                                         are re-emitted in place.

      A block with no welder elements passes through byte-identical;
      ``=::welder::…`` (root-qualified) is recognized too; adjacent string
      literals concatenate as in C++ (``doc("a" "b")`` is one text);
    * place the Doxygen comment (probed rules — Doxygen attaches comments
      positionally, see below);
    * splice the edits and write the result byte-exactly.

Comment placement (probed against Doxygen):
    keyword position:
        ``struct [[...]] Name`` (also class/union/enum [class|struct]/
        namespace) hoists the comment BEFORE the keyword, before any
        ``template <...>`` head(s), and before a requires-CLAUSE between
        head and keyword — a comment anywhere between those and the name
        does not attach.
    parameter position (inside parens):
        a trailing ``/**< ... */`` before the parameter's ending top-level
        ``,`` or ``)``. Template angles are tracked with *tentative
        matching* (see angle_probe), so ``f(std::map<int,int> m = {})``
        survives — and so do comparisons and shifts in default arguments.
    trailing position:
        ``Enumerator [[...]]`` / ``int x [[...]]`` before ``,`` ``}`` ``;``
        or ``=``: a trailing ``/**< ... */`` — placed after the initializer
        when one follows (before the ``=`` Doxygen mis-parses it for data
        members).
    otherwise:
        a ``/** ... */`` block in place, indentation preserved.

Fail-safety contract:
    The filter must never break a doc build, whatever the input:

    * lexing is total (grammar layer 1) — arbitrary bytes, unterminated
      literals and non-UTF-8 (surrogateescape in, byte-exact stdout) lex;
    * each block is handled in its own try/except — one the grammar cannot
      parse is left verbatim (Doxygen ignores [[...]] blocks natively);
    * a last-resort try/except emits the whole file unchanged on any error
      (missing lark included), with a note on stderr: the file is still
      documented, only its welder annotations are lost;
    * exit status is 0 in all of these cases; 2 only for wrong usage.

Known limits:
    Annotations must be spelled welder::-qualified (``::welder::…`` also
    works; a namespace *alias* does not). Doc text must be inline string
    literals — which the annotation design already forces (fixed_string; a
    ``const char*`` is not a permitted annotation constant). The filter is
    preprocessor-blind: annotations inside macro definitions transform
    textually, and a parameter list split across ``#if`` branches can
    misplace a parameter doc. A backslash-newline splice is honored inside
    line comments and string/char literals (and correctly *not* inside raw
    strings), but not mid-token elsewhere.
"""

import pathlib
import re
import sys
from typing import NamedTuple

# The annotation kinds that translate into Doxygen text; every other
# =welder:: spec is a binding control and is stripped from the doc build.
DOC_KINDS = frozenset({'doc', 'returns', 'tparam'})

# Keywords whose declarations Doxygen only documents when the comment sits
# before the keyword (hence the hoist in keyword_hoist()).
CLASS_KEYWORDS = frozenset({'struct', 'class', 'union', 'enum', 'namespace'})

# Token types that never carry meaning for placement decisions.
SKIP = frozenset({'WS', 'BLOCK_COMMENT', 'LINE_COMMENT'})

PREFIX = 'welder::'

# Simple-escape-sequence values for unescape(); unknown escapes (\x41, \u…,
# octal) are kept verbatim rather than guessed at.
_ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', 'a': '\a', 'b': '\b',
            'f': '\f', 'v': '\v', '0': '\0',
            '"': '"', "'": "'", '\\': '\\', '?': '?'}

_parser = None


def parser():
    """Returns the process-wide Lark instance, building it on first use.

    The grammar is loaded from welder_doxygen_filter.lark next to this
    script. ``lexer='basic'`` so the layer-1 terminals can be used lex-only
    via ``.lex()``; ``keep_all_tokens``/``propagate_positions`` so layer-2
    parse trees carry every token and source offsets.

    Returns:
        lark.Lark: the parser/lexer for both grammar layers.

    Raises:
        ImportError: if lark is not installed (caught by main()'s fail-safe).
        OSError: if the grammar file is missing (ditto).
    """
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
    """One located ``[[ ... ]]`` attribute block.

    Attributes:
        open_ti: Token index of the ``[[``.
        close_ti: One past the block's last token (the second ``]``).
        start: Char offset of the ``[[``.
        end: Char offset one past the ``]]``.
        paren_depth: ``( )`` nesting at the block; >0 means it sits in a
            parameter list.
    """
    open_ti: int
    close_ti: int
    start: int
    end: int
    paren_depth: int


def find_blocks(toks):
    """Locates every terminated ``[[ ... ]]`` block, in order.

    Comments and literals are atomic tokens, so blocks inside them are never
    seen. An unterminated ``[[`` (broken input) yields no block — it must
    stay verbatim, an edit around it could swallow code — and ends the scan,
    since everything after it sits inside the dangling block anyway.

    Args:
        toks: The layer-1 token list for the whole file.

    Returns:
        list[Block]: the blocks found, in source order.
    """
    blocks = []
    paren = 0
    i = 0
    while i < len(toks):
        t = toks[i]
        if t.type == 'ATTR_OPEN':
            closed = block_close(toks, i)
            if closed is None:
                break
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
    """Finds the ``]]`` matching the ``[[`` at token index i.

    The closer is two *adjacent* ``]`` at bracket depth 0 — a ``]`` inside a
    nested group or a string argument cannot close the block.

    Args:
        toks: The layer-1 token list.
        i: Token index of the ``[[``.

    Returns:
        A ``(close_ti, end)`` tuple — one past the closing ``]``'s token
        index and one past its char offset — or None for an unterminated
        block.
    """
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
    """Classifies one parsed attribute element.

    Args:
        element: A layer-2 ``element`` parse tree.
        inner: The text the block's content was parsed from, for slicing a
            kept element's source form.

    Returns:
        One of ``('keep', source_text)`` — standard attribute or foreign
        annotation, re-emitted verbatim; ``('strip', None)`` — a welder
        binding control; ``(kind, args)`` — a doc annotation, with ``kind``
        in DOC_KINDS and ``args`` its decoded string arguments.
    """
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
    return (kind, string_args(element))


def string_args(element):
    """Collects an element's decoded string-literal arguments, in order.

    Adjacent string literals concatenate, as in C++ translation phase 6
    (``doc("a" "b")`` is one argument); any other token in between —
    typically the ``,`` separating ``tparam``'s arguments — starts a new
    one. Raw strings are not collected (annotation text is fixed_string —
    always an ordinary literal).

    Args:
        element: A layer-2 ``element`` parse tree.

    Returns:
        list[str]: the decoded arguments.
    """
    args = []
    joining = False
    for t in _flat_tokens(element):
        if t.type == 'STRING':
            piece = unescape(t)
            if joining:
                args[-1] += piece
            else:
                args.append(piece)
            joining = True
        else:
            joining = False
    return args


def _flat_tokens(node):
    """Yields every token under a parse-tree node, in source order."""
    from lark import Token
    for child in node.children:
        if isinstance(child, Token):
            yield child
        else:
            yield from _flat_tokens(child)


def unescape(string_token):
    """Decodes a STRING token into its text value.

    Single pass: simple escapes become their characters, a backslash-newline
    line splice disappears, unknown escapes (``\\x41``, ``\\u…``, octal) are
    kept verbatim rather than guessed at.

    Args:
        string_token: A layer-1 STRING token (quotes and optional encoding
            prefix included).

    Returns:
        str: the decoded text.
    """
    body = str(string_token)
    body = body[body.index('"') + 1:-1]  # drop quotes and encoding prefix

    def one(match):
        c = match.group(1)
        if c.endswith('\n'):
            return ''
        return _ESCAPES.get(c, '\\' + c)

    return re.sub(r'\\(\r?\n|.)', one, body)


# =============================================================================
# Comment rendering
# =============================================================================

def comment_lines(parts):
    """Renders collected doc annotations into a comment's logical lines.

    Args:
        parts: Dict with keys 'doc', 'tparam' and 'returns'; each value is a
            list of string-argument lists as produced by classify().

    Returns:
        list[str]: the lines; empty for e.g. a weld-only block.
    """
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
    """Renders lines as a ``/** ... */`` block comment.

    Args:
        lines: Non-empty list of comment lines.
        indent: The target line's leading whitespace, prefixed to
            continuation lines.

    Returns:
        str: the comment text, without a trailing newline.
    """
    if len(lines) == 1:
        return '/** ' + lines[0] + ' */'
    body = ('\n' + indent + ' * ').join(lines)
    return '/**\n' + indent + ' * ' + body + '\n' + indent + ' */'


def inline_comment(lines):
    """Renders lines as one trailing ``/**< ... */`` comment.

    Args:
        lines: Non-empty list of comment lines (joined with spaces).

    Returns:
        str: the comment text.
    """
    return '/**< ' + ' '.join(lines) + ' */'


# =============================================================================
# Placement
# =============================================================================

def prev_sig(toks, i):
    """Finds the significant token before index i.

    Args:
        toks: The layer-1 token list.
        i: A token index.

    Returns:
        int | None: the nearest preceding index whose token is neither
        whitespace nor a comment, or None at the beginning.
    """
    i -= 1
    while i >= 0 and toks[i].type in SKIP:
        i -= 1
    return i if i >= 0 else None


def keyword_hoist(toks, bi):
    """Finds the hoist point for a block in keyword position, if any.

    A block directly after struct/class/union/enum [class|struct]/namespace
    documents that declaration; Doxygen only attaches the comment before the
    keyword — and before any ``template <...>`` head(s) and
    requires-clause(s) above it.

    Args:
        toks: The layer-1 token list.
        bi: Token index of the block's ``[[``.

    Returns:
        int | None: the char offset to insert the comment at, or None when
        the block is not in keyword position.
    """
    p = prev_sig(toks, bi)
    if p is None or toks[p] not in CLASS_KEYWORDS:
        return None
    if toks[p] in ('class', 'struct'):  # enum class / enum struct
        q = prev_sig(toks, p)
        if q is not None and toks[q] == 'enum':
            p = q
    while True:  # `template <...>` head(s) — and requires-clauses — above
        tm = head_start_above(toks, p)
        if tm is None:
            return toks[p].start_pos
        p = tm


def head_start_above(toks, p):
    """Finds the ``template`` introducing the head directly above token p.

    Handles both a plain head (``template <...>``) and one with a trailing
    requires-CLAUSE (``template <...> requires C<T> && (...)``) — the
    comment must hoist above the whole thing to attach.

    Args:
        toks: The layer-1 token list.
        p: Token index the head would end before (a keyword, or a
            previously found ``template``).

    Returns:
        int | None: the ``template`` keyword's token index, or None.
    """
    q = prev_sig(toks, p)
    if q is None:
        return None
    if str(toks[q]) in ('>', '>>'):  # plain `template <...>` directly above?
        tm = template_of_angle(toks, q)
        if tm is not None:
            return tm
    # Otherwise q may end a requires-clause; verify the full shape
    # `template <...> requires <constraint>` before trusting it.
    rq = requires_clause_start(toks, q)
    if rq is None:
        return None
    q = prev_sig(toks, rq)
    if q is None or str(toks[q]) not in ('>', '>>'):
        return None
    return template_of_angle(toks, q)


def template_of_angle(toks, q):
    """Resolves a closing angle to its ``template`` keyword, if it has one.

    Args:
        toks: The layer-1 token list.
        q: Token index of a ``>`` or ``>>`` ending a candidate head.

    Returns:
        int | None: the ``template`` keyword's token index, or None.
    """
    lt = angle_open(toks, q)
    if lt is None:
        return None
    tm = prev_sig(toks, lt)
    return tm if tm is not None and toks[tm] == 'template' else None


def requires_clause_start(toks, j, limit=512):
    """Walks back over a candidate requires-clause to its keyword.

    Skips balanced ``(...)``/``{...}``/``<...>`` groups and the tokens a
    constraint-expression may contain; anything else bails out — the caller
    then falls back to not hoisting, which is never worse than before.

    Args:
        toks: The layer-1 token list.
        j: Token index of the clause's candidate last token.
        limit: Walk budget, bounding pathological inputs.

    Returns:
        int | None: the ``requires`` keyword's token index, or None when
        the shape isn't conservatively constraint-like.
    """
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
        elif str(t) in ('>', '>>'):
            k = angle_open(toks, j)
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
    """Finds the opener matching the ``)`` or ``}`` at token index j.

    Args:
        toks: The layer-1 token list.
        j: Token index of the closer.

    Returns:
        int | None: the opener's token index, or None if unbalanced.
    """
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


def angle_open(toks, j):
    """Finds the ``<`` matching the closing angle at token index j.

    A ``>>`` token counts as two closers — the C++11 rule: in template
    context ``vector<vector<int>>`` closes both lists. Shifts, arrows and
    comparisons cannot interfere; they are single OP tokens.

    Args:
        toks: The layer-1 token list.
        j: Token index of a ``>`` or ``>>``.

    Returns:
        int | None: the matching ``<``'s token index, or None.
    """
    depth = 2 if str(toks[j]) == '>>' else 1
    j -= 1
    while j >= 0:
        v = str(toks[j])
        if v == '>':
            depth += 1
        elif v == '>>':
            depth += 2
        elif v == '<':
            depth -= 1
            if depth <= 0:
                return j
        elif v == '<<':
            depth -= 2
            if depth <= 0:
                return j
        j -= 1
    return None


def angle_probe(toks, i, limit=256):
    """Tentatively matches a candidate template-argument ``<`` forward.

    C++'s classic ambiguity — ``<`` after a name is template arguments or
    less-than, resolvable only with a symbol table — is decided the way
    fuzzy parsers decide it: commit to angles only when a matching ``>``
    plausibly closes the list. The probe walks forward tracking nesting and
    rejects on anything a template-argument-list cannot contain at its top
    level: a ``;`` outside braces, a top-level ``=`` (initializers and
    assignments; NB ``A<B{.x = 1}>`` and ``A<decltype(x = 1)>`` keep theirs
    nested), or the enclosing construct ending first (``)`` ``}`` ``]``
    underflow).

    Args:
        toks: The layer-1 token list.
        i: Token index of the candidate ``<``.
        limit: Probe budget; running out means "not angles".

    Returns:
        bool: True when the ``<`` should be treated as template angles.
    """
    depth = 1
    paren = brace = square = 0
    for j in range(i + 1, min(len(toks), i + 1 + limit)):
        t = toks[j]
        if t.type in SKIP:
            continue
        if t.type == 'ATTR_OPEN':
            square += 2
            continue
        v = str(t)
        if v == '(':
            paren += 1
        elif v == ')':
            if paren == 0:
                return False
            paren -= 1
        elif v == '{':
            brace += 1
        elif v == '}':
            if brace == 0:
                return False
            brace -= 1
        elif v == '[':
            square += 1
        elif v == ']':
            if square == 0:
                return False
            square -= 1
        elif v == ';' and brace == 0:
            return False
        elif paren == brace == square == 0:
            if v == '=':
                return False
            if v == '<':
                depth += 1
            elif v == '>':
                depth -= 1
                if depth == 0:
                    return True
            elif v == '>>':
                depth -= 2
                if depth <= 0:
                    return True
    return False


def decl_end(toks, i, text_len):
    """Scans forward to the end of the current parameter or initializer.

    From token i (just past an annotation block), finds the char offset of
    the ending top-level ``,`` or ``;``, or of the ``)``/``}`` closing the
    enclosing construct. Nested parens, braces and brackets (default
    arguments, lambdas) are tracked; literals are atomic tokens, so their
    contents never count. A template angle opens only for a ``<`` that
    follows an identifier AND passes angle_probe(), so comparisons and
    shifts cannot eat the terminator.

    Args:
        toks: The layer-1 token list.
        i: Token index to scan from.
        text_len: The file length, returned when nothing terminates.

    Returns:
        int: the char offset of the terminating token.
    """
    paren = angle = brace = square = 0
    prev = None  # last significant token, for the `<` candidate test
    while i < len(toks):
        t = toks[i]
        if t.type in SKIP:
            i += 1
            continue
        v = str(t)
        if t.type == 'ATTR_OPEN':
            square += 2
        elif v == '(':
            paren += 1
        elif v == ')':
            if paren == 0:
                return t.start_pos
            paren -= 1
        elif v in (',', ';') and paren == angle == brace == square == 0:
            return t.start_pos
        elif v == '<':
            if (prev is not None and prev.type in ('WORD', 'WELDER')
                    and angle_probe(toks, i)):
                angle += 1
        elif v == '>' and angle:
            angle -= 1
        elif v == '>>' and angle:
            angle = max(0, angle - 2)
        elif v == '{':
            brace += 1
        elif v == '}':
            if brace == 0:
                return t.start_pos
            brace -= 1
        elif v == '[':
            square += 1
        elif v == ']':
            square = max(0, square - 1)
        prev = t
        i += 1
    return text_len


def line_indent(text, pos):
    """Returns the whitespace prefix of pos's line, when pos sits inside it.

    Args:
        text: The whole file text.
        pos: A char offset.

    Returns:
        str: the prefix, or '' when non-whitespace precedes pos on its line.
    """
    ls = text.rfind('\n', 0, pos) + 1
    prefix = text[ls:pos]
    return prefix if prefix.strip() == '' else ''


def whole_lines(text, s, e):
    """Widens (s, e) to whole lines when the annotation is alone on them.

    Deleting an own-line annotation should not leave blank lines behind.

    Args:
        text: The whole file text.
        s: The annotation block's start offset.
        e: The block's end offset.

    Returns:
        tuple[int, int]: the possibly widened (start, end).
    """
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
    """Returns the first non-whitespace character at or after offset i.

    Args:
        text: The whole file text.
        i: A char offset.

    Returns:
        str: the character, or '' at end of input.
    """
    while i < len(text) and text[i].isspace():
        i += 1
    return text[i] if i < len(text) else ''


# =============================================================================
# The transform
# =============================================================================

def block_edits(text, toks, blk):
    """Computes the text edits for one annotation block.

    Args:
        text: The whole file text.
        toks: The layer-1 token list.
        blk: The Block to transform.

    Returns:
        list[tuple[int, int, str]]: ``(start, end, replacement)`` edits;
        empty when the block contains no welder elements.

    Raises:
        Exception: (from Lark) when the block's content does not parse as
            an attribute list — caught by transform()'s per-block fail-safe,
            which leaves the block verbatim.
    """
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

    kw = keyword_hoist(toks, blk.open_ti)
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
            stop = decl_end(toks, blk.close_ti, len(text))
            ins = e + len(text[e:stop].rstrip())
            edits.append((ins, ins, ' ' + inline_comment(lines)))
    elif (prev_ch.isalnum() or prev_ch == '_') and \
            next_code_char(text, e) in (',', '}', ';', '='):
        trailing = inline_comment(lines) if lines else ''
        if trailing and next_code_char(text, e) == '=':
            # Initialized enumerator/member: the trailing comment attaches
            # after the initializer (before the '=', Doxygen mis-parses it
            # for data members).
            stop = decl_end(toks, blk.close_ti, len(text))
            ins = e + len(text[e:stop].rstrip())
            edits.append((ins, ins, ' ' + trailing))
            trailing = ''
        repl = kept_block + (' ' if kept_block and trailing else '') + trailing
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
    """Translates welder annotations in a C++ source into Doxygen comments.

    Args:
        text: The file content (surrogateescape-decoded UTF-8).

    Returns:
        str: the filtered content; input passes through unchanged where
        there is nothing to do or a block cannot be handled (fail-safe,
        noted on stderr).
    """
    if PREFIX not in text or '[[' not in text:
        return text
    toks = list(parser().lex(text, dont_ignore=True))
    edits = []
    for blk in find_blocks(toks):
        try:
            edits.extend(block_edits(text, toks, blk))
        except Exception as exc:  # fail-safe: an unhandled block stays verbatim
            sys.stderr.write('welder_doxygen_filter: [[...]] block at offset '
                             '%d left verbatim: %r\n' % (blk.start, exc))
            continue
    for start, end, repl in sorted(edits, key=lambda t: t[0], reverse=True):
        text = text[:start] + repl + text[end:]
    return text


def main(argv):
    """Runs the filter as Doxygen invokes it.

    Args:
        argv: ``sys.argv`` — expects exactly one argument, the source file
            path.

    Returns:
        int: the process exit status — 0 also on fail-safe pass-through,
        2 only for wrong usage.
    """
    if len(argv) != 2:
        sys.stderr.write('usage: welder_doxygen_filter.py <source-file>\n')
        return 2
    with open(argv[1], 'rb') as f:
        text = f.read().decode('utf-8', errors='surrogateescape')
    try:
        out = transform(text)
    except Exception as exc:  # fail-safe: never break the doc build
        hint = ' (pip install lark?)' if isinstance(exc, ImportError) else ''
        sys.stderr.write('welder_doxygen_filter: %s: %r%s — passing the file '
                         'through unfiltered\n' % (argv[1], exc, hint))
        out = text
    sys.stdout.buffer.write(out.encode('utf-8', errors='surrogateescape'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
