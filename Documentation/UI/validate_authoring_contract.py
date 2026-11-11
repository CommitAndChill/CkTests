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
SOURCE_NAME = re.compile(r"^[A-Za-z_-][A-Za-z0-9_-]*$")
CSS_RULE = re.compile(r"\.([a-z][a-z0-9-]*)\s*\{")
PARAMETER_KINDS = {
    "text", "number", "bool", "color", "text-binding", "image-binding",
    "number-binding", "integer-binding", "bool-binding", "string-binding",
    "color-binding", "search-binding", "native-binding", "collection-binding",
    "float-series-binding", "action", "text-changed", "text-committed",
    "bool-changed", "number-changed", "number-committed", "integer-committed",
    "color-committed", "number-interaction", "string-changed",
}
BUILTIN_BIND_KINDS = {
    "text": "text-binding", "button": "text-binding", "native": "native-binding",
    "search": "search-binding", "image": "image-binding",
    "repeat": "collection-binding", "table": "collection-binding",
    "tree": "collection-binding",
}


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


def authored_templates(root, region):
    """Check template signatures, local IDs, references and use arguments.

    Native admission remains responsible for widget-specific property types and values.
    """
    templates = {}
    for child in root:
        if child is region:
            continue
        if child.tag != "template" or set(child.attrib) != {"name"}:
            fail("ui may contain only named templates and one main region")
        name = child.get("name", "").strip()
        if not SOURCE_NAME.fullmatch(name) or name in templates:
            fail(f"invalid or duplicate template {name!r}")
        params = {}
        reserved_attributes = {"id", "template", "class", "visible", "text"}
        body = None
        for part in child:
            if part.tag == "param":
                param_name = part.get("name", "").strip()
                kind = part.get("type")
                if (body is not None or set(part.attrib) != {"name", "type"}
                        or not SOURCE_NAME.fullmatch(param_name)
                        or param_name.endswith("-param") or param_name in params
                        or kind not in PARAMETER_KINDS or len(params) >= 64
                        or len(part) or (part.text or "").strip()):
                    fail(f"invalid parameter in template {name!r}")
                attribute = param_name + ("-bind" if kind.endswith("-binding") else "")
                if attribute in reserved_attributes or attribute + "-param" in reserved_attributes:
                    fail(f"template {name!r} has colliding parameter attribute")
                reserved_attributes.update({attribute, attribute + "-param"})
                params[param_name] = kind
            elif body is None:
                body = part
            else:
                fail(f"template {name!r} requires exactly one body root")
        if body is None or (child.text or "").strip():
            fail(f"template {name!r} requires exactly one body root")
        local_nodes, classes = authored_nodes([body])
        templates[name] = {"parameters": params, "nodes": local_nodes,
                           "classes": classes, "body": body, "instances": []}

    calls = {name: set() for name in templates}

    def reference_matches(node, target, kind):
        if target == "visible" or target == "visible-field":
            return kind == "bool-binding"
        if target == "bind":
            return kind == BUILTIN_BIND_KINDS.get(node.tag)
        if target.endswith("-bind") or target.endswith("-field"):
            return kind.endswith("-binding")
        return not kind.endswith("-binding")

    def visit(node, current_template=None):
        if node.tag == "use":
            name = node.get("template")
            if name not in templates:
                fail(f"unknown template {name!r}")
            params = templates[name]["parameters"]
            expected = {key + ("-bind" if kind.endswith("-binding") else ""): kind
                        for key, kind in params.items()}
            supplied = {key: value for key, value in node.attrib.items()
                        if key not in {"id", "template"}}
            targets = [key[:-6] if key.endswith("-param") else key for key in supplied]
            if (set(targets) != set(expected) or len(targets) != len(set(targets))
                    or len(node) or (node.text or "").strip()):
                fail(f"template use {node.get('id')!r} has missing or unsupported arguments")
            if current_template is None:
                templates[name]["instances"].append(node.get("id"))
            else:
                calls[current_template].add(name)
            for argument, value in supplied.items():
                target = argument[:-6] if argument.endswith("-param") else argument
                if not value.strip():
                    fail(f"template use {node.get('id')!r} has empty {argument!r}")
                if argument.endswith("-param"):
                    source = templates[current_template]["parameters"].get(value) if current_template else None
                    if source != expected[target]:
                        fail(f"type-mismatched template parameter {value!r}")
        else:
            for attr, value in node.attrib.items():
                if not attr.endswith("-param"):
                    continue
                target = attr[:-6]
                kind = templates[current_template]["parameters"].get(value) if current_template else None
                if kind is None:
                    fail(f"unknown template parameter {value!r}")
                if not reference_matches(node, target, kind):
                    fail(f"type-mismatched template parameter {value!r}")
        for child in node:
            visit(child, current_template)

    for name, template in templates.items():
        visit(template["body"], name)
    for child in region:
        visit(child)
    visiting = set()
    visited = set()

    def check_cycle(name):
        if name in visiting:
            fail(f"recursive template {name!r}")
        if name in visited:
            return
        visiting.add(name)
        for called in calls[name]:
            check_cycle(called)
        visiting.remove(name)
        visited.add(name)

    for name in templates:
        check_cycle(name)
    return templates


def check_contract(path, records, templates=None):
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
    if templates:
        declared = contract.get("templates")
        if not isinstance(declared, dict) or set(declared) != set(templates):
            fail("contract template names do not match source")
        for name, template in templates.items():
            item = declared[name]
            if not isinstance(item, dict):
                fail(f"contract template {name!r} must be an object")
            parameters = item.get("parameters")
            if (not isinstance(parameters, dict)
                    or any(not isinstance(value, str) for value in parameters.values())
                    or {
                    key: re.sub(r"(?<!^)(?=[A-Z])", "-", value).lower()
                    for key, value in parameters.items()
                    } != template["parameters"]):
                fail(f"contract template {name!r} parameters do not match source")
            local = item.get("localNodes")
            expected_local = template["nodes"]
            if (not isinstance(local, list) or any(not isinstance(node, dict) for node in local)
                    or [
                    (node.get("id"), node.get("tag"), node.get("parentId"))
                    for node in local
                    ] != [(node["id"], node["tag"], node["parentId"]) for node in expected_local]):
                fail(f"contract template {name!r} local IDs do not match source")
            if item.get("instances") != template["instances"]:
                fail(f"contract template {name!r} instances do not match source")
    elif contract.get("templates"):
        fail("contract declares templates absent from source")
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
        regions = [child for child in root if child.tag == "region"]
        if len(regions) != 1 or regions[0].get("name") != "main":
            fail("markup must have exactly one <region name=\"main\">")
        if len(regions[0]) != 1:
            fail("main region must contain exactly one authored root")
        records, used = authored_nodes(regions[0])
        templates = authored_templates(root, regions[0])
        for template in templates.values():
            used.update(template["classes"])
        declared = declared_css_classes(read_text(args.stylesheet))
        undefined = sorted(used - declared)
        if undefined:
            fail("undefined CSS classes: " + ", ".join(undefined))
        contract_count = check_contract(args.contract, records, templates) if args.contract else None
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    suffix = f"; contract {contract_count} nodes matched" if contract_count is not None else ""
    print(f"OK: {len(records)} authored IDs, {len(used)} CSS classes referenced, "
          f"{len(declared)} declared selectors{suffix}.")
    print("Limits: XML structure, scoped IDs, template signatures/uses, top-level single .class "
          "CSS rules and contract identity only; no widget property/value or runtime validation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
