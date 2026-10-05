#!/usr/bin/env python3
"""Measure the cost of generating surface over the PocketMage SDK.

Run from the SDK root:
    python3 tools/surface_measure.py [--json out.json]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict

PRIMITIVE_RETURNS = {
    "void", "bool", "char", "int", "unsigned", "long",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "size_t", "float", "double",
}

CONVERTIBLE_PARAM = re.compile(
    r"^(const\s+)?(char|int|unsigned|long|bool|float|double|size_t"
    r"|int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t)"
    r"(\s*\*+)?$"
)

DECL_RE = re.compile(
    r"^\s*(?P<prefix>(?:explicit\s+|static\s+|inline\s+|virtual\s+)*)"
    r"(?P<ret>.*?)\b(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"\((?P<args>[^()]*)\)\s*(?P<tail>.*)$"
)

SKIP_NAMES = {"if", "for", "while", "switch", "return", "sizeof", "catch"}

NOT_A_DECL_RE = re.compile(r"\b(return|if|else|for|while|switch|goto)\b")
COMPARISON_RE = re.compile(r"(<|>|==|!=|<=|>=|&&|\|\||[+\-*/%]\s*=)")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def split_args(args: str) -> list[str]:
    """Split an argument list on top-level commas."""
    out, depth, current = [], 0, []
    for ch in args:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    tail = "".join(current).strip()
    if tail:
        out.append(tail)
    return out


def classify_param(arg: str) -> dict:
    """Describe one parameter's translation cost."""
    arg = arg.strip()
    info = {
        "text": arg,
        "has_default": "=" in arg,
        "is_reference": "&" in arg,
        "is_pointer": "*" in arg,
        "is_array": "[" in arg,
    }
    if "..." in arg:
        info["kind"] = "variadic"
        return info

    if info["has_default"]:
        arg = arg.split("=", 1)[0].strip()
    base = re.sub(r"\[[^\]]*\]", "", arg).strip()
    type_part = re.sub(r"[A-Za-z_][A-Za-z0-9_]*\s*$", "", base).strip()
    if not type_part:
        type_part = base
    info["type"] = " ".join(type_part.split())

    if info["is_reference"]:
        info["kind"] = "reference"
    elif info["is_array"]:
        info["kind"] = "array"
    elif CONVERTIBLE_PARAM.match(info["type"]):
        info["kind"] = "primitive"
    elif info["type"] in ("String", "const String"):
        info["kind"] = "string"
    else:
        info["kind"] = "opaque"
    return info


def parse_header(path: str) -> tuple[list[dict], str]:
    """Return (methods, class_name) for one header."""
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    text = strip_comments(raw)

    methods = []
    class_name = "?"

    for mcls in re.finditer(r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)", text):
        cname = mcls.group(1)
        cstart = mcls.start()
        brace = text.find("{", cstart)
        semi = text.find(";", cstart)
        if semi != -1 and (brace == -1 or semi < brace):
            continue
        if brace == -1:
            continue
        depth = 0
        endpos = len(text)
        for i, ch in enumerate(text[brace:], start=brace):
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
            if depth == 0:
                endpos = i
                break
        class_body = text[brace:endpos + 1]
        cls_methods = []
        for pub in re.finditer(r"\bpublic\s*:", class_body):
            body = class_body[pub.end():]
            stop = re.search(r"\n\s*(private|protected)\s*:", body)
            if stop:
                body = body[: stop.start()]
            for line in body.splitlines():
                stripped = line.strip()
                if not stripped or stripped.startswith(("#", "//", "*", "}")):
                    continue
                if stripped.startswith("#"):
                    continue
                m = DECL_RE.match(line)
                if not m:
                    continue
                name = m.group("name")
                if name in SKIP_NAMES:
                    continue
                if name.startswith("~") or name.startswith("operator") or "~:" in line:
                    continue
                ret = " ".join(m.group("ret").split())
                args_raw = m.group("args")
                tail = m.group("tail")
                if ";" not in tail and "{" not in tail:
                    continue
                if NOT_A_DECL_RE.search(ret) or COMPARISON_RE.search(ret):
                    continue
                if "=" in ret:
                    continue
                if COMPARISON_RE.search(args_raw):
                    continue
                if "=" in tail:
                    continue
                is_ctor = not ret or ret == prefix_text(m)
                args = split_args(args_raw)
                params = [classify_param(a) for a in args] if args and args != ["void"] else []
                cls_methods.append({
                    "class": cname,
                    "ret": ret,
                    "name": name,
                    "params": params,
                    "inline_def": "{" in tail,
                    "operator": name.startswith("operator"),
                    "constructor": is_ctor,
                })
        if cls_methods:
            methods.extend(cls_methods)
            class_name = cname
    return methods, class_name

def prefix_text(m) -> str:
    """Return just the declaration prefix (explicit/static/inline/virtual)."""
    return " ".join(m.group("prefix").split())


def analyze(headers: list[str]) -> dict:
    all_methods: list[dict] = []
    for path in headers:
        methods, _ = parse_header(path)
        for m in methods:
            m["header"] = os.path.relpath(path)
        all_methods.extend(methods)

    real = [m for m in all_methods if not m["operator"] and not m["constructor"]]
    ctors = [m for m in all_methods if m["constructor"]]

    by_name: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for m in real:
        by_name[(m["class"], m["name"])].append(m)

    collisions = []
    for (cls, name), group in by_name.items():
        if len(group) < 2:
            continue
        arities = defaultdict(list)
        for m in group:
            arities[len(m["params"])].append(m)
        for arity, same_arity in arities.items():
            if len(same_arity) < 2:
                continue
            sigs = {
                tuple(p["type"] for p in m["params"]) for m in same_arity
            }
            collisions.append({
                "class": cls,
                "method": name,
                "arity": arity,
                "variants": [
                    f"{m['ret']} {name}(" + ", ".join(
                        p["type"] for p in m["params"]) + ")"
                    for m in same_arity
                ],
                "distinct_types": len(sigs),
            })

    param_types = Counter()
    ret_types = Counter()
    for m in real:
        ret_types[m["ret"]] += 1
        for p in m["params"]:
            param_types[p["type"]] += 1

    kinds = Counter()
    for m in real:
        for p in m["params"]:
            kinds[p["kind"]] += 1

    hard_returns = [
        m for m in real
        if m["ret"] not in PRIMITIVE_RETURNS
        and not m["ret"].endswith("*")
        and "&" not in m["ret"]
    ]

    defaulted = [m for m in real if any(p["has_default"] for p in m["params"])]
    refs = [m for m in real if any(p["is_reference"] for p in m["params"])]
    opaque = [m for m in real if any(p["kind"] == "opaque" for p in m["params"])]

    return {
        "headers": len(headers),
        "methods_declared": len(real),
        "constructors": len(ctors),
        "distinct_class_method_pairs": len(by_name),
        "classes": sorted({m["class"] for m in real}),
        "overload_collision_groups": len(collisions),
        "collisions": collisions,
        "param_kind_counts": dict(kinds),
        "param_type_top": param_types.most_common(20),
        "return_type_top": ret_types.most_common(15),
        "returns_needing_conversion": [
            f"{m['class']}::{m['ret']} {m['name']}("
            + ", ".join(p["type"] for p in m["params"]) + ")"
            for m in hard_returns
        ],
        "methods_with_defaults": len(defaulted),
        "methods_with_references": len(refs),
        "methods_with_opaque_params": len(opaque),
        "opaque_param_examples": [
            f"{m['class']}::{m['name']}("
            + ", ".join(p["type"] for p in m["params"]) + ")"
            for m in opaque[:25]
        ],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", help="also write the report as JSON")
    ap.add_argument("--root", default=".", help="SDK root")
    args = ap.parse_args()

    headers = sorted(
        os.path.join(args.root, name, h)
        for name in os.listdir(args.root)
        if name.startswith("pocketmage_")
        and os.path.isdir(os.path.join(args.root, name))
        for h in os.listdir(os.path.join(args.root, name))
        if h.startswith("pocketmage_") and h.endswith(".h")
    )

    report = analyze(headers)

    print(f"headers scanned        : {report['headers']}")
    print(f"classes with methods   : {len(report['classes'])} -> {', '.join(report['classes'])}")
    print(f"methods declared       : {report['methods_declared']}")
    print(f"class::method pairs    : {report['distinct_class_method_pairs']}")
    print(f"overload collisions    : {report['overload_collision_groups']}")
    print()
    print("parameter kinds:")
    for kind, count in sorted(
        report["param_kind_counts"].items(), key=lambda kv: -kv[1]
    ):
        print(f"  {kind:<12} {count}")
    print()
    print("most common parameter types:")
    for t, c in report["param_type_top"][:15]:
        print(f"  {t:<40} {c}")
    print()
    print(f"returns needing a conversion rule: {len(report['returns_needing_conversion'])}")
    for sig in report["returns_needing_conversion"][:12]:
        print(f"  {sig}")
    print()
    print(f"methods with default args : {report['methods_with_defaults']}")
    print(f"methods with references    : {report['methods_with_references']}")
    print(f"methods with opaque params : {report['methods_with_opaque_params']}")
    if report["opaque_param_examples"]:
        print("  examples:")
        for sig in report["opaque_param_examples"][:10]:
            print(f"  {sig}")
    if report["collisions"]:
        print()
        print("overload collisions needing disambiguation:")
        for c in report["collisions"][:20]:
            print(f"  {c['class']}::{c['method']}/{c['arity']} ({c['distinct_types']} distinct)")
            for v in c["variants"]:
                print(f"      {v}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        print(f"\njson written to {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())