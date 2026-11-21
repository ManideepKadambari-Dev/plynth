import json
import os
import re

SCHEMA_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "schema")


# Loads one of the project's schema files by name, for example "blender-result".
def load_schema(name):
    with open(os.path.join(SCHEMA_FOLDER, name + ".schema.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


# Returns true when value has the JSON type named by name.
def has_type(value, name):
    if name == "null":
        return value is None
    if name == "boolean":
        return isinstance(value, bool)
    if name == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if name == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if name == "string":
        return isinstance(value, str)
    if name == "array":
        return isinstance(value, list)
    if name == "object":
        return isinstance(value, dict)
    return False


# Follows a local reference such as #/definitions/size.
def resolve(root, reference):
    node = root
    for part in reference[2:].split("/"):
        node = node[part]
    return node


# Checks a value against the subset of JSON Schema the Plynth schemas use. Returns a list of problems.
def validate(value, schema, root=None, path="$"):
    root = root if root is not None else schema
    problems = []
    if "$ref" in schema:
        return validate(value, resolve(root, schema["$ref"]), root, path)
    if "const" in schema and (value != schema["const"] or type(value) is not type(schema["const"])):
        problems.append("%s: expected %r" % (path, schema["const"]))
    if "enum" in schema and value not in schema["enum"]:
        problems.append("%s: %r is not one of %r" % (path, value, schema["enum"]))
    if "type" in schema:
        names = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(has_type(value, name) for name in names):
            problems.append("%s: expected type %r" % (path, schema["type"]))
            return problems
    if "oneOf" in schema:
        matches = sum(1 for option in schema["oneOf"] if not validate(value, option, root, path))
        if matches != 1:
            problems.append("%s: matches %d of the oneOf options" % (path, matches))
    for part in schema.get("allOf", []):
        problems += validate(value, part, root, path)
    if "if" in schema:
        branch = "then" if not validate(value, schema["if"], root, path) else "else"
        if branch in schema:
            problems += validate(value, schema[branch], root, path)
    if isinstance(value, str):
        if "pattern" in schema and not re.search(schema["pattern"], value):
            problems.append("%s: does not match %s" % (path, schema["pattern"]))
        if "minLength" in schema and len(value) < schema["minLength"]:
            problems.append("%s: shorter than %d" % (path, schema["minLength"]))
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            problems.append("%s: longer than %d" % (path, schema["maxLength"]))
    if has_type(value, "number"):
        if "minimum" in schema and value < schema["minimum"]:
            problems.append("%s: below %r" % (path, schema["minimum"]))
        if "maximum" in schema and value > schema["maximum"]:
            problems.append("%s: above %r" % (path, schema["maximum"]))
        if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
            problems.append("%s: not above %r" % (path, schema["exclusiveMinimum"]))
    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            problems.append("%s: fewer than %d items" % (path, schema["minItems"]))
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            problems.append("%s: more than %d items" % (path, schema["maxItems"]))
        if "items" in schema:
            for index, item in enumerate(value):
                problems += validate(item, schema["items"], root, "%s[%d]" % (path, index))
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        for key in schema.get("required", []):
            if key not in value:
                problems.append("%s: missing %s" % (path, key))
        if schema.get("additionalProperties") is False:
            for key in value:
                if key not in properties:
                    problems.append("%s: unknown field %s" % (path, key))
        for key, sub in properties.items():
            if key in value:
                problems += validate(value[key], sub, root, "%s.%s" % (path, key))
    return problems
