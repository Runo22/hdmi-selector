#!/usr/bin/env python3
"""Self-contained smoke test for metagen.py.

Fabricates a build directory with a compile_commands.json entry (standing in
for what CMake exports), runs the generator against the example propulsion
header, and asserts that the emitted translation unit contains the expected
RTTR and Flecs registration code.

Run:  python3 tools/metagen/smoke_test.py
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
METAGEN = os.path.join(HERE, "metagen.py")
HEADER = os.path.join(
    ROOT, "plugins", "propulsion", "include", "propulsion",
    "TurbofanComponents.hpp")

EXPECTED_SNIPPETS = [
    # RTTR: structs, nested structs, enums, metadata, plugin registration.
    "RTTR_PLUGIN_REGISTRATION",
    "registration::class_<sim::propulsion::TurbofanEngine>",
    "registration::class_<sim::propulsion::TurbofanEngine::SpoolState>",
    "registration::class_<sim::propulsion::FuelSystem::TankState>",
    "registration::enumeration<sim::propulsion::IgniterMode>",
    "registration::enumeration<sim::propulsion::FuelSystem::TankState::Status>",
    'rttr::value("Continuous", sim::propulsion::IgniterMode::Continuous)',
    'rttr::metadata("Min", 0.0)',
    'rttr::metadata("Units", "kN")',
    'rttr::metadata("FilePath", true)',
    'rttr::metadata("Policy", "Replicated")',
    ".property(\"egtProbes\", &sim::propulsion::TurbofanEngine::egtProbes)",
    # Flecs: registrar, safety check, members, arrays, enum constants.
    "bool RegisterFlecsTypes_TurbofanComponents(flecs::world& world)",
    "is_valid()",
    "comp.member(\"n1\", &sim::propulsion::TurbofanEngine::SpoolState::n1)",
    "offsetof(sim::propulsion::TurbofanEngine, egtProbes)",
    "offsetof(sim::propulsion::FuelSystem, tanks)",
    'comp.constant("Continuous", sim::propulsion::IgniterMode::Continuous);',
    # Dynamic containers are documented as skipped for Flecs.
    "'pumpDutyCycles'",
]

ORDERING_PAIRS = [
    # Nested types must be registered before the type embedding them.
    ("component<sim::propulsion::TurbofanEngine::SpoolState>",
     "component<sim::propulsion::TurbofanEngine>("),
    ("component<sim::propulsion::FuelSystem::TankState>",
     "component<sim::propulsion::FuelSystem>("),
    ("component<sim::propulsion::FuelSystem::TankState::Status>",
     "component<sim::propulsion::FuelSystem::TankState>("),
]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="simmeta-smoke-") as build_dir:
        anchor = os.path.join(build_dir, "anchor.cpp")
        with open(anchor, "w", encoding="utf-8") as fh:
            fh.write("int main() { return 0; }\n")

        runtime_include = os.path.join(ROOT, "runtime", "include")
        plugin_include = os.path.join(
            ROOT, "plugins", "propulsion", "include")
        compile_commands = [{
            "directory": build_dir,
            "file": anchor,
            "arguments": [
                "clang++", "-std=c++17",
                f"-I{runtime_include}",
                f"-I{plugin_include}",
                "-c", anchor, "-o", "anchor.o",
            ],
        }]
        with open(os.path.join(build_dir, "compile_commands.json"), "w",
                  encoding="utf-8") as fh:
            json.dump(compile_commands, fh)

        output = os.path.join(build_dir, "TurbofanComponents.simmeta.cpp")
        model = os.path.join(build_dir, "model.json")
        result = subprocess.run(
            [sys.executable, METAGEN, HEADER,
             "--compile-commands", build_dir,
             "--anchor", anchor,
             "--output", output,
             "--header-include", "propulsion/TurbofanComponents.hpp",
             "--emit-model", model],
            capture_output=True, text=True)

        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            print("FAIL: metagen.py exited with", result.returncode)
            return 1

        with open(output, encoding="utf-8") as fh:
            text = fh.read()

        failures = [s for s in EXPECTED_SNIPPETS if s not in text]
        for snippet in failures:
            print(f"FAIL: missing snippet: {snippet}")

        for first, second in ORDERING_PAIRS:
            if first in text and second in text:
                if text.index(first) > text.index(second):
                    failures.append((first, second))
                    print(f"FAIL: {first!r} must precede {second!r}")

        if failures:
            print(f"\n--- generated output ---\n{text}")
            return 1

        print(f"PASS: generated {len(text.splitlines())} lines; "
              "all snippets and ordering constraints satisfied")
        return 0


if __name__ == "__main__":
    sys.exit(main())
