#!/usr/bin/env python3
"""welder-doxygen-filter — Doxygen INPUT_FILTER translating welder's C++26
annotations into Doxygen comments, on the fly.

Doxygen has no plugin system; INPUT_FILTER is its extension point — a program
run per input file whose stdout is what Doxygen parses. The sources on disk are
never modified. Configure:

    INPUT_FILTER    = "python3 /path/to/welder-doxygen-filter.py"
    # or per extension:
    FILTER_PATTERNS = *.hpp="python3 /path/to/welder-doxygen-filter.py"

How it recognizes annotations (the robustness rules)
-----------------------------------------------------
* [[ ... ]] blocks are located by a scanner that skips // and /* */ comments,
  string literals (with escapes), raw strings (R"delim( ... )delim") and char
  literals — an annotation-shaped sequence inside a string or a commented-out
  line is never touched.
* A block's extent is found bracket-aware and literal-aware, so doc text
  containing "]]", quotes or brackets cannot end it early.
* Within a block, the top-level comma-separated attribute list is split
  (commas inside parens/brackets/strings don't split) and each element is
  classified:
    =welder::doc("text")          -> comment summary
    =welder::returns("text")      -> @return text
    =welder::tparam("T", "text")  -> @tparam T text
    any other =welder::...        -> stripped (weld/policy/mark/trust are
                                     binding controls; doc *scope* control is
                                     Doxygen-native — use EXCLUDE_SYMBOLS)
    everything else               -> kept: standard attributes ([[nodiscard]],
                                     [[deprecated("...")]]) and non-welder
                                     annotations ([[=other::thing]]) are
                                     re-emitted in place in a rebuilt block.
  A block with no welder elements passes through byte-identical.
* Annotations must be spelled welder::-qualified (no namespace alias); text
  must be inline string literals — which the annotation design already forces
  (fixed_string; a const char* is not a permitted annotation constant).

Comment placement (Doxygen attaches comments positionally; probed rules)
------------------------------------------------------------------------
* keyword position — `struct [[...]] Name` (also class/union/enum [class|
  struct]/namespace): the comment hoists BEFORE the keyword, and before the
  `template <...>` head(s) if the declaration is a template — inline comments
  between keyword and name do not attach.
* parameter position — `f([[...]] std::map<int,int> m = {})`: a trailing
  `/**< ... */` is placed before the next top-level `,` or `)`; template
  angle brackets, nested parens, braces (default args) and strings are
  tracked so their commas don't split the parameter.
* trailing position — `Enumerator [[...]]` followed by `,` `}` or `;`:
  a trailing `/**< ... */` in place.
* otherwise (own-line annotation before a member/function/variable): a
  `/** ... */` block in place, indentation preserved.

Known limits: annotations spelled through a namespace alias are not
recognized; a `<`-containing *expression* in a default argument (e.g.
`int n = a < b`) can confuse the parameter scan; both are documented
conventions rather than expected shapes.
"""

import sys

DOC_KIND = {'doc', 'returns', 'tparam'}
PREFIX = 'welder::'
KEYWORD_RE = None  # built lazily (keep import cost minimal for Doxygen)


# --- lexical helpers ---------------------------------------------------------

def skip_string(text, i):
    """i at opening '"' (raw strings handled by caller); return index past the
    closing quote."""
    n = len(text)
    i += 1
    while i < n:
        c = text[i]
        if c == '\\':
            i += 2
        elif c == '"':
            return i + 1
        else:
            i += 1
    return n


def skip_raw_string(text, i):
    """i at the '"' of R"delim( ; return index past the closing )delim"."""
    n = len(text)
    j = text.find('(', i + 1)
    if j < 0:
        return n
    delim = text[i + 1:j]
    end = text.find(')' + delim + '"', j + 1)
    return n if end < 0 else end + len(delim) + 2


def skip_char(text, i):
    """i at "'". Distinguishes a char literal from a digit separator (1'000):
    a quote sandwiched between alphanumerics is a separator, skip just it."""
    n = len(text)
    prev = text[i - 1] if i > 0 else ''
    nxt = text[i + 1] if i + 1 < n else ''
    if (prev.isalnum() or prev == '_') and (nxt.isalnum() or nxt == '_'):
        return i + 1
    i += 1
    while i < n:
        c = text[i]
        if c == '\\':
            i += 2
        elif c == "'":
            return i + 1
        else:
            i += 1
    return n


def is_raw_prefix(text, i):
    """Is the '"' at i the start of a raw string (R / uR / u8R / LR prefix)?"""
    return i > 0 and text[i - 1] == 'R' and \
        (i == 1 or not (text[i - 2].isalnum() or text[i - 2] == '_') or
         text[max(0, i - 3):i - 1] in ('u8', ' uR', ' LR') or
         text[i - 2] in 'uUL')


# --- pass 1: locate annotation-bearing [[ ... ]] blocks in code ---------------

def find_blocks(text):
    """Yield (start, end, paren_depth) for every [[ ... ]] block that sits in
    code (not in a comment or literal). end is one past the closing ]]."""
    n = len(text)
    i = 0
    paren = 0
    out = []
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ''
        if c == '/' and nxt == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and nxt == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"':
            i = skip_raw_string(text, i) if is_raw_prefix(text, i) \
                else skip_string(text, i)
        elif c == "'":
            i = skip_char(text, i)
        elif c == '(':
            paren += 1
            i += 1
        elif c == ')':
            paren = max(0, paren - 1)
            i += 1
        elif c == '[' and nxt == '[':
            end = block_end(text, i)
            out.append((i, end, paren))
            i = end
        else:
            i += 1
    return out


def block_end(text, i):
    """i at '[['; return one past the matching ']]', literal- and
    bracket-aware (a ']]' inside a string argument does not close it)."""
    n = len(text)
    j = i + 2
    depth = 0
    while j < n:
        c = text[j]
        if c == '"':
            j = skip_raw_string(text, j) if is_raw_prefix(text, j) \
                else skip_string(text, j)
        elif c == "'":
            j = skip_char(text, j)
        elif c == '[':
            depth += 1
            j += 1
        elif c == ']':
            if depth:
                depth -= 1
                j += 1
            elif j + 1 < n and text[j + 1] == ']':
                return j + 2
            else:
                j += 1
        else:
            j += 1
    return n


# --- pass 2: parse a block's attribute list -----------------------------------

def split_top_level(s):
    """Split the inside of a [[ ... ]] block on top-level commas."""
    parts = []
    depth = 0
    start = 0
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '"':
            i = skip_raw_string(s, i) if is_raw_prefix(s, i) else skip_string(s, i)
            continue
        if c == "'":
            i = skip_char(s, i)
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth = max(0, depth - 1)
        elif c == ',' and depth == 0:
            parts.append(s[start:i])
            start = i + 1
        i += 1
    parts.append(s[start:])
    return parts


def string_args(s):
    """The string-literal arguments inside an element, unescaped."""
    out = []
    i = 0
    n = len(s)
    while i < n:
        if s[i] == '"' and not is_raw_prefix(s, i):
            j = skip_string(s, i)
            raw = s[i + 1:j - 1]
            out.append(raw.replace('\\n', '\n').replace('\\t', '\t')
                          .replace('\\"', '"').replace('\\\\', '\\'))
            i = j
        else:
            i += 1
    return out


def classify(element):
    """-> ('keep', text) | ('strip', None) | ('doc'|'returns'|'tparam', args)"""
    e = element.strip()
    if not e.startswith('='):
        return ('keep', e)  # standard attribute
    expr = e[1:].lstrip()
    if not expr.startswith(PREFIX):
        return ('keep', e)  # foreign annotation
    body = expr[len(PREFIX):]
    kind = ''
    for ch in body:
        if ch.isalnum() or ch == '_':
            kind += ch
        else:
            break
    if kind in DOC_KIND:
        return (kind, string_args(body))
    return ('strip', None)  # weld / policy / mark / trust_bindable / future


# --- comment rendering ---------------------------------------------------------

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


# --- placement -----------------------------------------------------------------

def line_indent(text, pos):
    """The whitespace prefix of pos's line if pos sits in it, else ''."""
    ls = text.rfind('\n', 0, pos) + 1
    prefix = text[ls:pos]
    return prefix if prefix.strip() == '' else ''


def keyword_start(text, s):
    """If the annotation at s sits right after struct/class/union/enum
    [class|struct]/namespace, the hoist point: the keyword's start, extended
    back over `template <...>` head(s). None otherwise."""
    import re
    global KEYWORD_RE
    if KEYWORD_RE is None:
        KEYWORD_RE = re.compile(
            r'\b(enum\s+(?:class|struct)|struct|class|union|enum|namespace)\s*$')
    m = KEYWORD_RE.search(text, 0, s)
    if not m:
        return None
    ins = m.start(1)
    while True:  # `template <...>` (possibly several) directly above
        head = text[:ins].rstrip()
        if not head.endswith('>'):
            return ins
        k = angle_open(text, len(head) - 1)
        if k is None:
            return ins
        t = text[:k].rstrip()
        if not t.endswith('template'):
            return ins
        ins = len(t) - len('template')


def angle_open(text, j):
    """text[j] == '>'; index of the matching '<', or None."""
    depth = 1
    j -= 1
    while j >= 0:
        c = text[j]
        if c == '>' and text[j - 1] != '-':
            depth += 1
        elif c == '<':
            depth -= 1
            if depth == 0:
                return j
        j -= 1
    return None


def param_end(text, i):
    """From i (just past the annotation, inside a parameter list): the index of
    the parameter's ending top-level ',' or ')'. Angle brackets, nested parens,
    braces/brackets (default arguments) and literals are tracked."""
    n = len(text)
    paren = angle = brace = square = 0
    while i < n:
        c = text[i]
        if c == '"':
            i = skip_raw_string(text, i) if is_raw_prefix(text, i) \
                else skip_string(text, i)
            continue
        if c == "'":
            i = skip_char(text, i)
            continue
        if c == '(':
            paren += 1
        elif c == ')':
            if paren == 0:
                return i
            paren -= 1
        elif c == ',' and paren == angle == brace == square == 0:
            return i
        elif c == '<':
            prev = text[i - 1] if i else ''
            if prev.isalnum() or prev == '_' or prev in '>:':
                angle += 1
        elif c == '>' and angle and text[i - 1] != '-':
            angle -= 1
        elif c == '{':
            brace += 1
        elif c == '}':
            brace = max(0, brace - 1)
        elif c == '[':
            square += 1
        elif c == ']':
            square = max(0, square - 1)
        i += 1
    return n


def next_sig(text, i):
    while i < len(text) and text[i].isspace():
        i += 1
    return text[i] if i < len(text) else ''


def whole_lines(text, s, e):
    """Widen (s, e) to swallow the full line(s) when the annotation is alone on
    them (deleting it should not leave blank lines)."""
    ls = text.rfind('\n', 0, s) + 1
    if text[ls:s].strip():
        return s, e
    le = text.find('\n', e)
    if le < 0:
        return ls, e
    if text[e:le].strip():
        return s, e
    return ls, le + 1


# --- the transform ---------------------------------------------------------------

def transform(text):
    if 'welder::' not in text or '[[' not in text:
        return text
    edits = []  # (start, end, replacement)
    for s, e, paren_depth in find_blocks(text):
        inner = text[s + 2:e - 2]
        if PREFIX not in inner:
            continue
        kept = []
        parts = {'doc': [], 'tparam': [], 'returns': []}
        saw_welder = False
        for el in split_top_level(inner):
            kind, val = classify(el)
            if kind == 'keep':
                kept.append(val)
            else:
                saw_welder = True
                if kind != 'strip':
                    parts[kind].append(val)
        if not saw_welder:
            continue
        kept_block = '[[' + ', '.join(kept) + ']]' if kept else ''
        lines = comment_lines(parts)

        kw = keyword_start(text, s)
        prev_ch = text[:s].rstrip()[-1:]

        if kw is not None:
            if lines:
                indent = line_indent(text, kw)
                edits.append((kw, kw, block_comment(lines, indent) + '\n' + indent))
            if kept_block:
                edits.append((s, e, kept_block))
            else:
                edits.append(whole_lines(text, s, e) + ('',))
        elif paren_depth > 0:
            repl = kept_block
            edits.append((s, e, repl))
            if lines:
                stop = param_end(text, e)
                seg = text[e:stop]
                ins = e + len(seg) - len(seg.lstrip()) + len(seg.strip())
                edits.append((ins, ins, ' ' + inline_comment(lines)))
        elif (prev_ch.isalnum() or prev_ch == '_') and next_sig(text, e) in ',};':
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

    for start, end, repl in sorted(edits, key=lambda t: t[0], reverse=True):
        text = text[:start] + repl + text[end:]
    return text


def main():
    if len(sys.argv) != 2:
        sys.stderr.write('usage: welder-doxygen-filter.py <source-file>\n')
        return 2
    with open(sys.argv[1], encoding='utf-8', errors='surrogateescape') as f:
        sys.stdout.write(transform(f.read()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
