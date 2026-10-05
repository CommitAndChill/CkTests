#!/usr/bin/env python3
"""Check authored XML IDs/classes, simple CSS selectors, and contract node identity.

This is a source consistency check, not the CkSlateLayout parser or a runtime gate.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


CLASS_NAME = re.compile(r"^[a-z][a-z0-9-]*$")
CSS_RULE = re.compile(r"\.([a-z][a-z0-9-]*)\s*\{")


def fail(message):
    raise ValueError(message)


def read_text(path):
    try:
        return path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as exc:
        fail(f"cannot read {path}: {exc}")


def declared_css_classes(stylesheet):
    """Preflight top-level rules with the runtime's single .class selector shape.

    Declaration properties and values are left to the framework parser.
    """
    css = stylesheet
    comment_start = css.find("/*")
    while comment_start != -1:
        comment_end = css.find("*/", comment_start + 2)
        if comment_end == -1:
            fail("unterminated CSS comment")
        css = css[:comment_start] + css[comment_end + 2:]
        comment_start = css.find("/*")
    declared = set()
    position = 0
    while position < len(css):
        while position < len(css) and css[position].isspace():
            position += 1
        if position == len(css):
            break
        rule = CSS_RULE.match(css, position)
        if rule is None:
            snippet = css[position:position + 40].splitlines()[0]
            fail(f"CSS selector must be a single .class: {snippet!r}")
        declared.add(rule.group(1))
        end = css.find("}", rule.end())
        if end == -1:
            fail(f"unterminated CSS rule for .{rule.group(1)}")
        position = end + 1
    return declared


def authored_nodes(region):
    records = []
    seen = set()
    used_classes = set()

    def visit(node, parent_id, scope, in_repeat):
        if not isinstance(node.tag, str):
            return
        node_id = node.get("id")
        if not node_id and node.tag not in {"slot"}:
            fail(f"<{node.tag}> is missing an id")
        if node_id:
            key = (scope, node_id)
            if key in seen:
                fail(f"duplicate id {node_id!r} in scope {'/'.join(scope)}")
            seen.add(key)
            records.append({
                "id": node_id,
                "tag": node.tag,
                "parentId": parent_id,
                "scope": "repeat-item-local" if in_repeat else "main-region",
            })
        for token in node.get("class", "").split():
            if not CLASS_NAME.fullmatch(token):
                fail(f"invalid class token {token!r} on {node_id or node.tag}")
            used_classes.add(token)
        child_scope = scope
        if node.tag == "repeat":
            child_scope += (f"repeat:{node_id}",)
        elif node.tag == "slot":
            child_scope += (f"slot:{node.get('name', '')}",)
        for child in node:
            visit(child, node_id or parent_id, child_scope,
                  in_repeat or node.tag == "repeat")

    for child in region:
        visit(child, None, ("main",), False)
    return records, used_classes


def check_contract(path, records):
    try:
        contract = json.loads(read_text(path))
    except json.JSONDecodeError as exc:
        fail(f"invalid contract JSON {path}: {exc}")
    if not isinstance(contract, dict) or contract.get("schemaVersion") != 1:
        fail("contract must be a schemaVersion 1 JSON object")
    nodes = contract.get("nodes")
    if not isinstance(nodes, list) or any(not isinstance(item, dict) for item in nodes):
        fail("contract nodes must be an array of objects")
    for index, item in enumerate(nodes):
        if (not isinstance(item.get("id"), str) or not item["id"]
                or not isinstance(item.get("tag"), str) or not item["tag"]
                or item.get("parentId") is not None and not isinstance(item["parentId"], str)
                or not isinstance(item.get("scope"), str)
                or item["scope"] not in {"main-region", "repeat-item-local"}):
            fail(f"contract node {index} has invalid id, tag, parentId or scope")
    fields = ("id", "tag", "parentId", "scope")
    def identity(item):
        return tuple(item.get(field) for field in fields)
    expected = Counter(identity(item) for item in records)
    actual = Counter(identity(item) for item in nodes)
    missing = expected - actual
    extra = actual - expected
    if missing or extra:
        detail = []
        if missing:
            detail.append(f"missing {list(missing.elements())[:3]}")
        if extra:
            detail.append(f"extra {list(extra.elements())[:3]}")
        fail("contract node identity mismatch: " + "; ".join(detail))
    if contract.get("nodeCount", len(nodes)) != len(nodes):
        fail("contract nodeCount does not match nodes array length")
    return len(nodes)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("markup", type=Path, help="authored .ui.html file")
    parser.add_argument("stylesheet", type=Path, help="authored .ui.css file")
    parser.add_argument("--contract", type=Path, help="optional schemaVersion 1 design-contract.json")
    args = parser.parse_args(argv)
    try:
        try:
            root = ET.fromstring(read_text(args.markup))
        except ET.ParseError as exc:
            fail(f"invalid markup XML {args.markup}: {exc}")
        if root.tag != "ui" or root.get("version") != "1":
            fail("markup must have <ui version=\"1\"> root")
        regions = list(root)
        if len(regions) != 1 or regions[0].tag != "region" or regions[0].get("name") != "main":
            fail("markup must have exactly one <region name=\"main\">")
        if len(regions[0]) != 1:
            fail("main region must contain exactly one authored root")
        records, used = authored_nodes(regions[0])
        declared = declared_css_classes(read_text(args.stylesheet))
        undefined = sorted(used - declared)
        if undefined:
            fail("undefined CSS classes: " + ", ".join(undefined))
        contract_count = check_contract(args.contract, records) if args.contract else None
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    suffix = f"; contract {contract_count} nodes matched" if contract_count is not None else ""
    print(f"OK: {len(records)} authored IDs, {len(used)} CSS classes referenced, "
          f"{len(declared)} declared selectors{suffix}.")
    print("Limits: XML structure, scoped IDs, top-level single .class CSS rules and "
          "contract identity only; no declaration/value, binding or runtime validation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
