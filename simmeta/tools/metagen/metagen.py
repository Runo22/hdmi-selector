#!/usr/bin/env python3
"""SimMeta code generator.

Parses an annotated C++ header with libclang and emits a translation unit
containing:

  * an RTTR_PLUGIN_REGISTRATION block registering every annotated struct
    (including nested inner structs), enum, array, and property together
    with its metadata — suitable for dynamically loaded plugin libraries;
  * a Flecs registrar function
        bool simmeta::generated::RegisterFlecsTypes_<UnitName>(flecs::world&)
    that mirrors the exact compile-time types into a flecs::world, using
    component<T>().is_valid() as the fundamental safety check.

Design notes
------------
* Metadata extraction never touches raw tokens. The macros in
  simmeta/Annotations.hpp expand — only under -DSIMMETA_CODEGEN — to Clang
  `annotate` attributes whose payload is the stringized macro argument list.
  The compiler performs macro expansion and attaches the payload to the
  exact declaration; this script merely reads ANNOTATE_ATTR nodes from the
  AST. This is the most robust extraction strategy available with libclang.

* Compile flags (include paths, defines, language standard) are taken from
  compile_commands.json via an "anchor" source file belonging to the same
  build target, so nothing is ever hard-coded or duplicated.

* Function bodies are skipped (PARSE_SKIP_FUNCTION_BODIES) for speed on
  large headers.

* Traversal is fully recursive: inner structs and enums at any nesting
  depth are discovered and emitted in dependency (post-) order, so a nested
  type is always registered before the type that embeds it.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field

import clang.cindex as cx

ANNOTATION_PREFIX = "simmeta:"
CODEGEN_DEFINE = "SIMMETA_CODEGEN=1"

# Types Flecs pre-registers as primitives (canonical spellings).
FLECS_PRIMITIVES = {
    "bool", "char", "signed char", "unsigned char",
    "short", "unsigned short", "int", "unsigned int",
    "long", "unsigned long", "long long", "unsigned long long",
    "float", "double",
}

STD_ARRAY_RE = re.compile(r"^std::array<(.+),\s*(\d+)\s*>$")
DYNAMIC_CONTAINER_PREFIXES = (
    "std::vector<", "std::basic_string<", "std::deque<", "std::list<",
    "std::map<", "std::unordered_map<", "std::set<", "std::unordered_set<",
)


def log(msg: str) -> None:
    print(f"[simmeta] {msg}", file=sys.stderr)


def fail(msg: str) -> "NoReturn":  # noqa: F821
    log(f"error: {msg}")
    sys.exit(2)


# ---------------------------------------------------------------------------
# Metadata payload parsing
#
# Payload grammar (already stringized by the preprocessor):
#     Key = Value, Key = "quoted string", BareFlag
# ---------------------------------------------------------------------------

def split_top_level(text: str) -> list[str]:
    """Split on commas that are outside quotes and outside (), [], {}."""
    parts: list[str] = []
    buf: list[str] = []
    depth = 0
    in_string = False
    i = 0
    while i < len(text):
        ch = text[i]
        if in_string:
            buf.append(ch)
            if ch == "\\" and i + 1 < len(text):
                buf.append(text[i + 1])
                i += 2
                continue
            if ch == '"':
                in_string = False
        elif ch == '"':
            in_string = True
            buf.append(ch)
        elif ch in "([{":
            depth += 1
            buf.append(ch)
        elif ch in ")]}":
            depth -= 1
            buf.append(ch)
        elif ch == "," and depth == 0:
            part = "".join(buf).strip()
            if part:
                parts.append(part)
            buf = []
        else:
            buf.append(ch)
        i += 1
    tail = "".join(buf).strip()
    if tail:
        parts.append(tail)
    return parts


def unquote(text: str) -> str:
    body = text[1:-1]
    out: list[str] = []
    i = 0
    while i < len(body):
        ch = body[i]
        if ch == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(nxt, nxt))
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def parse_scalar(text: str):
    text = text.strip()
    if len(text) >= 2 and text.startswith('"') and text.endswith('"'):
        return unquote(text)
    if text == "true":
        return True
    if text == "false":
        return False
    if re.fullmatch(r"[+-]?\d+", text):
        return int(text)
    if re.fullmatch(r"[+-]?0[xX][0-9a-fA-F]+", text):
        return int(text, 16)
    numeric = text.rstrip("fF") if re.search(r"[.\deE]f?$", text) else text
    try:
        return float(numeric)
    except ValueError:
        # Unquoted identifier-ish value (e.g. Policy = Replicated): keep as
        # string so authors may omit quotes for simple tags.
        return text


def parse_payload(payload: str) -> dict:
    """Parse `Key = Value, Flag, ...` into an ordered dict.

    Bare flags map to True.
    """
    meta: dict = {}
    for entry in split_top_level(payload):
        # Find the first '=' outside a quoted string.
        eq = -1
        in_string = False
        i = 0
        while i < len(entry):
            ch = entry[i]
            if in_string:
                if ch == "\\":
                    i += 1
                elif ch == '"':
                    in_string = False
            elif ch == '"':
                in_string = True
            elif ch == "=":
                eq = i
                break
            i += 1
        if eq < 0:
            meta[entry.strip()] = True
        else:
            key = entry[:eq].strip()
            value = entry[eq + 1:].strip()
            if not key:
                fail(f"malformed metadata entry: {entry!r}")
            meta[key] = parse_scalar(value)
    return meta


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------

@dataclass
class PropertyModel:
    name: str
    owner: str                      # fully qualified owner type
    declared_type: str              # as written in source
    canonical_type: str             # canonical spelling
    shape: str                      # 'scalar' | 'array' | 'dynamic'
    element_type: str = ""          # canonical element type for arrays
    element_count: int = 1
    metadata: dict = field(default_factory=dict)


@dataclass
class StructModel:
    qualified_name: str
    metadata: dict = field(default_factory=dict)
    properties: list = field(default_factory=list)


@dataclass
class EnumeratorModel:
    name: str
    value: int
    metadata: dict = field(default_factory=dict)


@dataclass
class EnumModel:
    qualified_name: str
    underlying_type: str
    is_scoped: bool
    metadata: dict = field(default_factory=dict)
    enumerators: list = field(default_factory=list)


@dataclass
class UnitModel:
    header_path: str
    header_include: str
    unit_name: str
    # (kind, model) in dependency order: nested types precede their owners.
    types: list = field(default_factory=list)

    def struct_names(self) -> set:
        return {m.qualified_name for k, m in self.types if k == "struct"}

    def enum_names(self) -> set:
        return {m.qualified_name for k, m in self.types if k == "enum"}


# ---------------------------------------------------------------------------
# Compile flags from compile_commands.json
# ---------------------------------------------------------------------------

# Flags that must not leak into the reparse (output/dep-file handling and the
# input file itself).
_SKIP_WITH_ARG = {"-o", "-MF", "-MT", "-MQ", "--output"}
_SKIP_ALONE = {"-c", "-MD", "-MMD", "-MP", "-M", "-MM", "--"}


def sanitize_compile_args(arguments: list[str], source_file: str) -> list[str]:
    source_abs = os.path.abspath(source_file)
    args: list[str] = []
    it = iter(range(1, len(arguments)))  # skip argv[0] (the compiler)
    skip_next = False
    for idx in range(1, len(arguments)):
        if skip_next:
            skip_next = False
            continue
        arg = arguments[idx]
        if arg in _SKIP_WITH_ARG:
            skip_next = True
            continue
        if arg in _SKIP_ALONE:
            continue
        if arg.startswith("-o") and len(arg) > 2 and not arg.startswith("-op"):
            continue
        if os.path.abspath(arg) == source_abs:
            continue
        args.append(arg)
    return args


def detect_resource_dir(compiler: str) -> str | None:
    """Find clang's builtin-header directory (stddef.h & friends).

    libclang does not locate its own resource dir when driven through the
    C API, so headers pulled in via the standard library fail with
    "'stddef.h' file not found" unless we pass -resource-dir explicitly.
    Prefer asking the compiler recorded in the compilation database (when it
    is a clang), then fall back to any clang on PATH.
    """
    candidates = []
    base = os.path.basename(compiler)
    if "clang" in base:
        candidates.append(compiler)
    for name in ("clang++", "clang"):
        if shutil.which(name):
            candidates.append(name)
    for candidate in candidates:
        try:
            result = subprocess.run(
                [candidate, "-print-resource-dir"],
                capture_output=True, text=True, timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            continue
        resource_dir = result.stdout.strip()
        if result.returncode == 0 and os.path.isdir(resource_dir):
            return resource_dir
    return None


def load_compile_args(build_dir: str, anchor: str | None) -> list[str]:
    db_path = os.path.join(build_dir, "compile_commands.json")
    if not os.path.exists(db_path):
        fail(f"compile_commands.json not found in {build_dir} "
             "(set CMAKE_EXPORT_COMPILE_COMMANDS=ON and configure first)")
    db = cx.CompilationDatabase.fromDirectory(build_dir)

    commands = None
    if anchor:
        commands = db.getCompileCommands(os.path.abspath(anchor))
        if not commands:
            log(f"warning: anchor {anchor!r} not found in compilation "
                "database; falling back to the first entry")
    if not commands:
        commands = db.getAllCompileCommands()
    if not commands or len(commands) == 0:
        fail("compilation database is empty")

    cmd = commands[0]
    arguments = list(cmd.arguments)
    args = sanitize_compile_args(arguments, cmd.filename)
    # Relative -I paths in the entry are relative to the entry's directory.
    args.append(f"-working-directory={cmd.directory}")
    args.append(f"-D{CODEGEN_DEFINE}")
    if not any(a.startswith("-resource-dir") for a in args):
        resource_dir = detect_resource_dir(arguments[0] if arguments else "")
        if resource_dir:
            args.append(f"-resource-dir={resource_dir}")
    return args


# ---------------------------------------------------------------------------
# AST traversal
# ---------------------------------------------------------------------------

def annotation_of(cursor) -> tuple[str, str] | None:
    """Return (kind, payload) for the first SimMeta annotation, if any."""
    for child in cursor.get_children():
        if child.kind != cx.CursorKind.ANNOTATE_ATTR:
            continue
        text = child.spelling or ""
        if not text.startswith(ANNOTATION_PREFIX):
            continue
        rest = text[len(ANNOTATION_PREFIX):]
        kind, _, payload = rest.partition(":")
        return kind, payload
    return None


def qualified_name(cursor) -> str:
    parts: list[str] = []
    cur = cursor
    while cur is not None and cur.kind != cx.CursorKind.TRANSLATION_UNIT:
        if cur.spelling:
            parts.append(cur.spelling)
        cur = cur.semantic_parent
    return "::".join(reversed(parts))


def classify_field(cursor) -> tuple[str, str, int]:
    """Return (shape, element_type, element_count) for a field."""
    canonical = cursor.type.get_canonical()
    if canonical.kind == cx.TypeKind.CONSTANTARRAY:
        return ("array",
                canonical.element_type.get_canonical().spelling,
                canonical.element_count)
    spelling = canonical.spelling
    m = STD_ARRAY_RE.match(spelling)
    if m:
        return "array", m.group(1).strip(), int(m.group(2))
    if spelling.startswith(DYNAMIC_CONTAINER_PREFIXES):
        return "dynamic", "", 0
    return "scalar", spelling, 1


def collect_enum(cursor, unit: UnitModel, payload: str) -> None:
    model = EnumModel(
        qualified_name=qualified_name(cursor),
        underlying_type=cursor.enum_type.spelling,
        is_scoped=cursor.is_scoped_enum(),
        metadata=parse_payload(payload),
    )
    for child in cursor.get_children():
        if child.kind != cx.CursorKind.ENUM_CONSTANT_DECL:
            continue
        ann = annotation_of(child)
        meta = parse_payload(ann[1]) if ann and ann[0] == "enumerator" else {}
        model.enumerators.append(
            EnumeratorModel(child.spelling, child.enum_value, meta))
    unit.types.append(("enum", model))


def collect_struct(cursor, unit: UnitModel, payload: str) -> None:
    qname = qualified_name(cursor)

    # Post-order: nested annotated types are collected (and later emitted)
    # before the struct that contains them.
    for child in cursor.get_children():
        visit_type_decl(child, unit)

    model = StructModel(qualified_name=qname, metadata=parse_payload(payload))
    for child in cursor.get_children():
        if child.kind != cx.CursorKind.FIELD_DECL:
            continue
        ann = annotation_of(child)
        if not ann or ann[0] != "property":
            continue
        if child.access_specifier not in (cx.AccessSpecifier.PUBLIC,
                                          cx.AccessSpecifier.INVALID):
            log(f"warning: skipping non-public property "
                f"{qname}::{child.spelling}")
            continue
        shape, element_type, count = classify_field(child)
        model.properties.append(PropertyModel(
            name=child.spelling,
            owner=qname,
            declared_type=child.type.spelling,
            canonical_type=child.type.get_canonical().spelling,
            shape=shape,
            element_type=element_type,
            element_count=count,
            metadata=parse_payload(ann[1]),
        ))
    unit.types.append(("struct", model))


def visit_type_decl(cursor, unit: UnitModel) -> None:
    """Dispatch a single declaration cursor (struct/class/enum)."""
    if cursor.kind in (cx.CursorKind.STRUCT_DECL, cx.CursorKind.CLASS_DECL):
        if not cursor.is_definition():
            return
        ann = annotation_of(cursor)
        if ann and ann[0] == "component":
            collect_struct(cursor, unit, ann[1])
        else:
            # Unannotated record may still contain annotated inner types.
            for child in cursor.get_children():
                visit_type_decl(child, unit)
    elif cursor.kind == cx.CursorKind.ENUM_DECL:
        if not cursor.is_definition():
            return
        ann = annotation_of(cursor)
        if ann and ann[0] == "enum":
            collect_enum(cursor, unit, ann[1])


def collect_unit(tu, header_path: str, header_include: str,
                 unit_name: str) -> UnitModel:
    unit = UnitModel(header_path=header_path,
                     header_include=header_include,
                     unit_name=unit_name)
    target = os.path.realpath(header_path)

    def in_target(cursor) -> bool:
        f = cursor.location.file
        return f is not None and os.path.realpath(f.name) == target

    def walk(cursor) -> None:
        for child in cursor.get_children():
            if not in_target(child):
                continue
            if child.kind == cx.CursorKind.NAMESPACE:
                walk(child)
            else:
                visit_type_decl(child, unit)

    walk(tu.cursor)
    return unit


# ---------------------------------------------------------------------------
# Emission helpers
# ---------------------------------------------------------------------------

def cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def cpp_literal(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        text = repr(value)
        return text if any(c in text for c in ".eE") else text + ".0"
    return cpp_string(str(value))


def metadata_calls(meta: dict) -> list[str]:
    return [f"rttr::metadata({cpp_string(k)}, {cpp_literal(v)})"
            for k, v in meta.items()]


def flecs_type_name(qualified: str) -> str:
    """Entity path used in the Flecs world; '::' is the path separator."""
    return qualified


# ---------------------------------------------------------------------------
# RTTR emission
# ---------------------------------------------------------------------------

def emit_rttr(unit: UnitModel, out: list[str]) -> None:
    out.append("RTTR_PLUGIN_REGISTRATION")
    out.append("{")
    out.append("    using rttr::registration;")
    out.append("")
    for kind, model in unit.types:
        if kind == "enum":
            emit_rttr_enum(model, out)
        else:
            emit_rttr_struct(model, out)
    out.append("}")


def emit_rttr_enum(model: EnumModel, out: list[str]) -> None:
    q = model.qualified_name
    out.append(f"    registration::enumeration<{q}>({cpp_string(q)})")
    out.append("    (")
    entries: list[str] = []
    for e in model.enumerators:
        entries.append(f"        rttr::value({cpp_string(e.name)}, {q}::{e.name})")
    # RTTR has no per-enumerator metadata slot; enumerator metadata is
    # flattened into the enumeration's metadata as "<Enumerator>.<Key>".
    for k, v in model.metadata.items():
        entries.append(f"        rttr::metadata({cpp_string(k)}, {cpp_literal(v)})")
    for e in model.enumerators:
        for k, v in e.metadata.items():
            entries.append(
                f"        rttr::metadata({cpp_string(e.name + '.' + k)}, "
                f"{cpp_literal(v)})")
    out.append(",\n".join(entries))
    out.append("    );")
    out.append("")


def emit_rttr_struct(model: StructModel, out: list[str]) -> None:
    q = model.qualified_name
    out.append(f"    registration::class_<{q}>({cpp_string(q)})")
    class_meta = metadata_calls(model.metadata)
    if class_meta:
        out.append("    (")
        out.append(",\n".join(f"        {m}" for m in class_meta))
        out.append("    )")
    out.append("        .constructor<>()(rttr::policy::ctor::as_object)")
    for i, prop in enumerate(model.properties):
        terminator = ";" if i == len(model.properties) - 1 else ""
        prop_meta = metadata_calls(prop.metadata)
        if prop_meta:
            out.append(f"        .property({cpp_string(prop.name)}, "
                       f"&{q}::{prop.name})")
            out.append("        (")
            out.append(",\n".join(f"            {m}" for m in prop_meta))
            out.append(f"        ){terminator}")
        else:
            out.append(f"        .property({cpp_string(prop.name)}, "
                       f"&{q}::{prop.name}){terminator}")
    if not model.properties:
        out[-1] += ";"
    out.append("")


# ---------------------------------------------------------------------------
# Flecs emission
# ---------------------------------------------------------------------------

def flecs_member_supported(prop: PropertyModel, unit: UnitModel) -> bool:
    known = unit.struct_names() | unit.enum_names() | FLECS_PRIMITIVES
    if prop.shape == "dynamic":
        return False
    if prop.shape == "array":
        return prop.element_type in known
    return prop.canonical_type in known


def emit_flecs(unit: UnitModel, out: list[str]) -> None:
    out.append("namespace simmeta { namespace generated {")
    out.append("")
    out.append(f"bool RegisterFlecsTypes_{unit.unit_name}(flecs::world& world)")
    out.append("{")
    out.append("    bool allValid = true;")
    out.append("")
    for kind, model in unit.types:
        if kind == "enum":
            emit_flecs_enum(model, out)
        else:
            emit_flecs_struct(model, unit, out)
    out.append("    return allValid;")
    out.append("}")
    out.append("")
    out.append("}}  // namespace simmeta::generated")


def emit_flecs_enum(model: EnumModel, out: list[str]) -> None:
    q = model.qualified_name
    out.append(f"    {{  // enum {q}")
    out.append(f"        auto comp = world.component<{q}>("
               f"{cpp_string(flecs_type_name(q))});")
    out.append("        if (!comp.is_valid()) {")
    out.append("            allValid = false;")
    out.append("        } else {")
    for e in model.enumerators:
        out.append(f"            comp.constant({cpp_string(e.name)}, "
                   f"{q}::{e.name});")
    out.append("        }")
    out.append("    }")
    out.append("")


def emit_flecs_struct(model: StructModel, unit: UnitModel,
                      out: list[str]) -> None:
    q = model.qualified_name
    out.append(f"    {{  // struct {q}")
    out.append(f"        auto comp = world.component<{q}>("
               f"{cpp_string(flecs_type_name(q))});")
    out.append("        if (!comp.is_valid()) {")
    out.append("            allValid = false;")
    out.append("        } else {")
    for prop in model.properties:
        if not flecs_member_supported(prop, unit):
            out.append(f"            // '{prop.name}' "
                       f"({prop.canonical_type}) has no Flecs struct-member "
                       "mapping; it remains accessible through RTTR.")
            continue
        min_v = prop.metadata.get("Min")
        max_v = prop.metadata.get("Max")
        has_range = (isinstance(min_v, (int, float))
                     and isinstance(max_v, (int, float))
                     and not isinstance(min_v, bool)
                     and not isinstance(max_v, bool))
        range_call = (f".range({cpp_literal(float(min_v))}, "
                      f"{cpp_literal(float(max_v))})" if has_range else "")
        if prop.shape == "array":
            out.append(
                f"            comp.member<{prop.element_type}>("
                f"{cpp_string(prop.name)}, {prop.element_count}, "
                f"offsetof({q}, {prop.name})){range_call};")
        else:
            out.append(
                f"            comp.member({cpp_string(prop.name)}, "
                f"&{q}::{prop.name}){range_call};")
    out.append("        }")
    out.append("    }")
    out.append("")


# ---------------------------------------------------------------------------
# File emission
# ---------------------------------------------------------------------------

def emit_unit(unit: UnitModel) -> str:
    out: list[str] = []
    out.append("// ============================================================")
    out.append("//  GENERATED FILE — DO NOT EDIT.")
    out.append(f"//  Source : {unit.header_include}")
    out.append("//  Emitted by tools/metagen/metagen.py (SimMeta pipeline).")
    out.append("// ============================================================")
    out.append("")
    out.append(f"#include \"{unit.header_include}\"")
    out.append("")
    out.append("#include <rttr/registration>")
    out.append("#include <flecs.h>")
    out.append("")
    out.append("#include <cstddef>")
    out.append("")
    emit_rttr(unit, out)
    out.append("")
    emit_flecs(unit, out)
    out.append("")
    return "\n".join(out)


def model_as_json(unit: UnitModel) -> str:
    def as_dict(obj):
        if isinstance(obj, (StructModel, EnumModel, PropertyModel,
                            EnumeratorModel)):
            return {k: as_dict(v) for k, v in obj.__dict__.items()}
        if isinstance(obj, list):
            return [as_dict(v) for v in obj]
        return obj

    return json.dumps({
        "header": unit.header_path,
        "unit": unit.unit_name,
        "types": [{"kind": k, **as_dict(m)} for k, m in unit.types],
    }, indent=2)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def sanitize_unit_name(path: str) -> str:
    stem = os.path.splitext(os.path.basename(path))[0]
    name = re.sub(r"\W", "_", stem)
    if re.match(r"^\d", name):
        name = "_" + name
    return name


def main() -> int:
    parser = argparse.ArgumentParser(
        description="SimMeta reflection code generator (libclang).")
    parser.add_argument("header", help="annotated header to process")
    parser.add_argument("--compile-commands", required=True, metavar="DIR",
                        help="directory containing compile_commands.json "
                             "(usually the CMake build directory)")
    parser.add_argument("--anchor", metavar="SOURCE",
                        help="source file of the same target; its entry in "
                             "the compilation database supplies the flags")
    parser.add_argument("--output", required=True, metavar="FILE",
                        help="generated .cpp path")
    parser.add_argument("--header-include", metavar="TEXT",
                        help="path to use in the generated #include "
                             "(defaults to the header's absolute path)")
    parser.add_argument("--unit-name", metavar="ID",
                        help="identifier for the generated registrar "
                             "(defaults to the sanitized header stem)")
    parser.add_argument("--emit-model", metavar="FILE",
                        help="also dump the parsed model as JSON (debugging)")
    parser.add_argument("--clang-library", metavar="PATH",
                        help="explicit libclang shared library path")
    parser.add_argument("--extra-arg", action="append", default=[],
                        help="additional compiler argument (repeatable)")
    parser.add_argument("--permissive", action="store_true",
                        help="continue despite parse errors")
    args = parser.parse_args()

    if args.clang_library:
        cx.Config.set_library_file(args.clang_library)

    header = os.path.abspath(args.header)
    if not os.path.exists(header):
        fail(f"header not found: {header}")

    compile_args = load_compile_args(args.compile_commands, args.anchor)
    compile_args += args.extra_arg

    index = cx.Index.create()
    try:
        tu = index.parse(
            header,
            args=compile_args,
            options=cx.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    except cx.TranslationUnitLoadError as exc:
        fail(f"libclang failed to parse {header}: {exc}")

    errors = [d for d in tu.diagnostics
              if d.severity >= cx.Diagnostic.Error]
    if errors:
        for d in errors:
            log(f"parse error: {d}")
        if not args.permissive:
            fail(f"{len(errors)} error(s) while parsing {header}")

    unit_name = args.unit_name or sanitize_unit_name(header)
    header_include = args.header_include or header
    unit = collect_unit(tu, header, header_include, unit_name)

    if not unit.types:
        log(f"warning: no annotated types found in {header}")

    text = emit_unit(unit)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as fh:
        fh.write(text)

    if args.emit_model:
        with open(args.emit_model, "w", encoding="utf-8") as fh:
            fh.write(model_as_json(unit))

    struct_count = len(unit.struct_names())
    enum_count = len(unit.enum_names())
    log(f"{os.path.basename(header)} -> {args.output} "
        f"({struct_count} struct(s), {enum_count} enum(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
