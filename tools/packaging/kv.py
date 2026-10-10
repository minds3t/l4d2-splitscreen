"""KeyValues (.res/.txt) parser that keeps Valve platform conditionals ([$X360], [!$WIN32 && $DECK] ...).

parse(text)            -> list of nodes: (key, value_or_children, cond_or_None)
evaluate(nodes, defs)  -> same tree with conditionals resolved for the given define set (cond dropped)
dump(nodes)            -> text
"""
import re

TOKEN = re.compile(r'''
    (?P<ws>\s+) |
    (?P<comment>//[^\n]*) |
    (?P<cond>\[[^\]\n]*\]) |
    (?P<open>\{) | (?P<close>\}) |
    (?P<qstr>"(?:[^"\\\n]|\\.)*"?) |
    (?P<word>[^\s{}"\[\]]+)
''', re.X)


def tokenize(text):
    out = []
    for m in TOKEN.finditer(text.lstrip("\ufeff")):
        k = m.lastgroup
        if k in ("ws", "comment"):
            continue
        v = m.group()
        if k == "qstr":
            v = v[1:-1] if v.endswith('"') and len(v) > 1 else v[1:]
            out.append(("str", v))
        elif k == "word":
            out.append(("str", v))
        else:
            out.append((k, v))
    return out


def parse(text):
    toks = tokenize(text)
    pos = 0

    def block():
        nonlocal pos
        nodes = []
        while pos < len(toks):
            t, v = toks[pos]
            if t == "close":
                pos += 1
                return nodes
            if t == "cond":  # stray conditional
                pos += 1
                continue
            if t != "str":
                pos += 1
                continue
            key = v
            pos += 1
            cond = None
            if pos < len(toks) and toks[pos][0] == "cond":
                cond = toks[pos][1][1:-1].strip(); pos += 1
            if pos >= len(toks):
                break
            t2, v2 = toks[pos]
            if t2 == "open":
                pos += 1
                children = block()
                if pos < len(toks) and toks[pos][0] == "cond" and cond is None:
                    cond = toks[pos][1][1:-1].strip(); pos += 1
                nodes.append((key, children, cond))
            elif t2 == "str":
                pos += 1
                if pos < len(toks) and toks[pos][0] == "cond":
                    cond = toks[pos][1][1:-1].strip(); pos += 1
                nodes.append((key, v2, cond))
            else:
                pos += 1
        return nodes

    return block()


COND_TOKEN = re.compile(r"\s*(\$\w+|&&|\|\||!|\(|\)|[01])")


def _condition(cond):
    """Parse only Valve's boolean condition grammar, without Python eval."""
    tokens = []
    pos = 0
    while pos < len(cond.rstrip()):
        match = COND_TOKEN.match(cond, pos)
        if match is None:
            raise ValueError(f"bad conditional {cond!r}")
        tokens.append(match.group(1))
        pos = match.end()
    pos = 0

    def primary():
        nonlocal pos
        if pos >= len(tokens):
            raise ValueError(f"bad conditional {cond!r}")
        token = tokens[pos]
        pos += 1
        if token == "!":
            return ("!", primary())
        if token == "(":
            value = expression()
            if pos >= len(tokens) or tokens[pos] != ")":
                raise ValueError(f"bad conditional {cond!r}")
            pos += 1
            return value
        if token.startswith("$"):
            return token.upper()
        if token in ("0", "1"):
            return token == "1"
        raise ValueError(f"bad conditional {cond!r}")

    def conjunction():
        nonlocal pos
        value = primary()
        while pos < len(tokens) and tokens[pos] == "&&":
            pos += 1
            value = ("&&", value, primary())
        return value

    def expression():
        nonlocal pos
        value = conjunction()
        while pos < len(tokens) and tokens[pos] == "||":
            pos += 1
            value = ("||", value, conjunction())
        return value

    result = expression()
    if pos != len(tokens):
        raise ValueError(f"bad conditional {cond!r}")
    return result


def _resolve(expr, defines, missing=False):
    if isinstance(expr, bool):
        return expr
    if isinstance(expr, str):
        return defines.get(expr[1:], expr if missing is None else missing)
    op = expr[0]
    left = _resolve(expr[1], defines, missing)
    if op == "!":
        return not left if isinstance(left, bool) else (op, left)
    right = _resolve(expr[2], defines, missing)
    if op == "&&":
        if left is False or right is False:
            return False
        if left is True:
            return right
        if right is True:
            return left
    else:
        if left is True or right is True:
            return True
        if left is False:
            return right
        if right is False:
            return left
    return (op, left, right)


def _format_condition(expr):
    if isinstance(expr, str):
        return expr
    if expr[0] == "!":
        return "!" + _format_condition(expr[1])
    return f"({_format_condition(expr[1])} {expr[0]} {_format_condition(expr[2])})"


def eval_cond(cond, defs):
    if not cond:
        return True
    return _resolve(_condition(cond), {name.upper(): True for name in defs})


def specialize(nodes, defines):
    """Resolve specified symbols, preserving runtime conditions such as language.

    Unlike evaluate(), symbols absent from the boolean mapping remain in the
    output; they are not silently treated as false.
    """
    defines = {name.upper(): value for name, value in defines.items()}
    out = []
    for key, val, cond in nodes:
        result = _resolve(_condition(cond), defines, None) if cond else True
        if result is False:
            continue
        remaining = None if result is True else _format_condition(result)
        if isinstance(val, list):
            val = specialize(val, defines)
        out.append((key, val, remaining))
    return out


def evaluate(nodes, defs):
    out = []
    for key, val, cond in nodes:
        if not eval_cond(cond, defs):
            continue
        if isinstance(val, list):
            out.append((key, evaluate(val, defs), None))
        else:
            out.append((key, val, None))
    return out


def q(s):
    # Parsed strings retain Source's raw escape sequences. Do not escape an
    # already escaped quote again: doing so would terminate the emitted value.
    return '"' + re.sub(r'\\.|"', lambda m: m[0] if m[0].startswith('\\') else '\\"', s) + '"'


def dump(nodes, indent=0):
    lines = []
    pad = "\t" * indent
    for key, val, cond in nodes:
        c = f" [{cond}]" if cond else ""
        if isinstance(val, list):
            lines.append(f"{pad}{q(key)}{c}")
            lines.append(pad + "{")
            lines.append(dump(val, indent + 1))
            lines.append(pad + "}")
        else:
            name = key if key.lower() in ("#base", "#include") else q(key)
            lines.append(f"{pad}{name}\t{q(val)}{c}")
    return "\n".join(l for l in lines if l != "")


def to_dict(nodes):
    """Lossy diagnostic view: first scalars win and repeated blocks merge.

    Repeated children are meaningful in KeyValues (fonts, carousel modes, etc.).
    Never use this view to decide whether two resources are equivalent.
    """
    d = {}
    for key, val, _ in nodes:
        k = key.lower()
        if isinstance(val, list):
            sub = to_dict(val)
            if isinstance(d.get(k), dict):
                d[k].update(sub)
            else:
                d[k] = sub
        else:
            d.setdefault(k, val)
    return d


X360 = {"X360", "X360WIDE", "ENGLISH"}
WIN32 = {"WIN32", "WIN32WIDE", "WINDOWS", "ENGLISH"}
