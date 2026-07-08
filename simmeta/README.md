# SimMeta — code-generation reflection pipeline for plugin-based simulators

SimMeta is a UHT-style reflection pipeline for Modern C++ simulation
projects. You decorate structs, inner structs, arrays, and enums with
macros that are **empty in normal builds**, and the build system generates
the registration boilerplate for:

* **[RTTR 0.9.6](https://www.rttr.org/)** — runtime reflection with rich
  per-property metadata (`Min`, `Max`, `Step`, `Units`, `FilePath`,
  `Policy`, …), registered via `RTTR_PLUGIN_REGISTRATION` so it works in
  dynamically loaded libraries.
* **[Flecs 4](https://github.com/SanderMertens/flecs)** — the exact
  compile-time component types mirrored into a `flecs::world`, including
  struct members, fixed-size arrays, enum constants, and member ranges,
  guarded by `is_valid()` checks.

Target stack: Modern C++ (17), CMake ≥ 3.21, Qt 5.15 plugins,
RTTR 0.9.6, Flecs 4, Python 3.8+ with libclang.

```
annotated header (.hpp)                      normal compile: macros vanish,
        │                                    zero cost, no dependencies
        │  cmake configure exports compile_commands.json
        ▼
tools/metagen/metagen.py  ──(libclang, -DSIMMETA_CODEGEN)──►  <unit>.simmeta.cpp
        │                                                          │
        │   ANNOTATE_ATTR nodes carry the stringized metadata      │
        ▼                                                          ▼
   parsed type model                    RTTR_PLUGIN_REGISTRATION { ... }
                                        simmeta::generated::RegisterFlecsTypes_<unit>(world)
                                                                   │
                                        compiled into the Qt plugin (MODULE library)
```

## Repository layout

| Path | Purpose |
| --- | --- |
| `runtime/include/simmeta/Annotations.hpp` | Annotation macros (`SIM_COMPONENT`, `SIM_PROPERTY`, `SIM_ENUM`, `SIM_ENUM_VALUE`) |
| `runtime/include/simmeta/MetaKeys.hpp` | Canonical metadata key constants for consumers |
| `tools/metagen/metagen.py` | libclang parser + code emitter |
| `tools/metagen/smoke_test.py` | Self-contained generator test (no Qt/RTTR/Flecs needed) |
| `cmake/SimMetaCodegen.cmake` | `simmeta_attach_codegen()` build integration |
| `core/include/simcore/ISimulationModule.hpp` | Qt plugin interface (`registerComponents(flecs::world&)`) |
| `plugins/propulsion/` | Example plugin: annotated turbofan/fuel components |
| `host/` | Example host: loads plugins, dumps RTTR metadata + Flecs JSON |
| `docs/generated-example/` | A real, checked-in sample of generator output |

## Quick start

Prerequisites: a C++17 toolchain, Ninja (or Makefiles), Qt 5.15, RTTR
0.9.6 and Flecs 4 discoverable via `find_package`, and Python 3 with the
libclang bindings:

```bash
pip install libclang        # ships the native library too
```

Build and run:

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/host/simmeta_host build/modules/propulsion_module.so
```

Test only the generator (no C++ dependencies required):

```bash
python3 tools/metagen/smoke_test.py
```

## Authoring components

```cpp
#include <simmeta/Annotations.hpp>

namespace sim::propulsion {

enum class SIM_ENUM(DisplayName = "Igniter Mode") IgniterMode : std::uint8_t {
    Off        SIM_ENUM_VALUE(DisplayName = "Off") = 0,
    Continuous SIM_ENUM_VALUE(DisplayName = "Continuous Relight") = 5,
};

struct SIM_COMPONENT(Category = "Propulsion", Policy = "Replicated") TurbofanEngine {

    struct SIM_COMPONENT(DisplayName = "Spool Speeds") SpoolState {
        SIM_PROPERTY(Min = 0.0, Max = 120.0, Step = 0.1, Units = "%")
        double n1 = 0.0;
    };

    SIM_PROPERTY(Min = 0.0, Max = 400.0, Units = "kN", ReadOnly)
    double thrust = 0.0;

    SIM_PROPERTY(DisplayName = "EGT Probes", Units = "degC")
    std::array<float, 4> egtProbes{};

    SIM_PROPERTY(FilePath, Description = "Engine performance deck (.csv)")
    std::string performanceDeckPath;
};

}  // namespace sim::propulsion
```

Payload grammar: `Key = Value` pairs and bare flags, comma-separated.
Values may be numbers, `true`/`false`, quoted strings, or unquoted
identifier tags (`Policy = Replicated`). The key vocabulary is open;
canonical keys live in `simmeta/MetaKeys.hpp`. Only annotated members are
reflected — reflection is opt-in per member, like `UPROPERTY`.

Macro placement:

* `SIM_COMPONENT(...)` / `SIM_ENUM(...)` — between the class-key
  (`struct`, `enum class`) and the type name.
* `SIM_PROPERTY(...)` — immediately before the member declaration.
* `SIM_ENUM_VALUE(...)` — after the enumerator name, before `=`/`,`.

## Hooking a target into the pipeline

```cmake
# top-level CMakeLists.txt
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # before any target
include(SimMetaCodegen)

# plugin CMakeLists.txt
simmeta_attach_codegen(
    TARGET       propulsion_module
    ANCHOR       "${CMAKE_CURRENT_SOURCE_DIR}/src/PropulsionModule.cpp"
    INCLUDE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/include"
    HEADERS      "${CMAKE_CURRENT_SOURCE_DIR}/include/propulsion/TurbofanComponents.hpp")
```

Each header becomes one generated `<stem>.simmeta.cpp` added to the
target. `add_custom_command` re-runs the generator **only when the header
or the generator script changes** — normal incremental-build behaviour,
no glob scans, no always-run steps.

`ANCHOR` is the trick that removes hardcoded include paths: it names any
ordinary source file of the same target, and the generator reuses that
file's entry in `compile_commands.json` (include dirs, defines, `-std`).
If you add a new include directory to the target, it is automatically
visible to the parser at the next build.

Then expose the generated Flecs registrar from your module:

```cpp
SIMMETA_DECLARE_FLECS_REGISTRAR(TurbofanComponents)

bool PropulsionModule::registerComponents(flecs::world& world)
{
    return SIMMETA_FLECS_REGISTRAR(TurbofanComponents)(world);
}
```

## How extraction works (and why it is robust)

The macros are *not* recovered by scanning tokens or regexing source. In
the code-gen pass (`-DSIMMETA_CODEGEN=1`) each macro expands to a Clang
annotate attribute:

```cpp
__attribute__((annotate("simmeta:property:" "Min = 0.0, Units = \"kN\"")))
```

The compiler performs macro expansion, string concatenation, and — most
importantly — attaches the payload to the **exact declaration** in the
AST (`ANNOTATE_ATTR` child of the `FIELD_DECL`/`STRUCT_DECL`/…). The
parser reads it back verbatim. There is no ambiguity about which
declaration a macro belongs to, no comment/#if pitfalls, and nested
parentheses or quoted commas in metadata just work.

Parsing uses `PARSE_SKIP_FUNCTION_BODIES` for speed on long headers, and
traversal is fully recursive, so inner structs and enums are found at any
depth and emitted in dependency order (inner types registered before the
types that embed them).

## What the generator emits

See [`docs/generated-example/TurbofanComponents.simmeta.cpp`](docs/generated-example/TurbofanComponents.simmeta.cpp)
for real output. Highlights:

* **RTTR**: `RTTR_PLUGIN_REGISTRATION` block (plugin-safe: registration
  runs on library load and unregisters on unload);
  `registration::class_` per struct with `policy::ctor::as_object`
  constructors; every metadata pair as `rttr::metadata(key, value)`;
  enumerations with all values. RTTR has no per-enumerator metadata slot,
  so enumerator metadata is flattened into the enum's metadata as
  `"<Enumerator>.<Key>"`.
* **Flecs**: one registrar per header. Every registration is checked with
  `is_valid()` — the fundamental safety check — and the registrar returns
  `false` if anything failed. Scalars and nested structs use the
  type-safe pointer-to-member overload
  (`comp.member("n1", &SpoolState::n1)`); fixed arrays
  (`std::array<T,N>`, `T[N]`) use element type + count + `offsetof`;
  `Min`/`Max` metadata becomes `.range(min, max)`; enums get explicit
  `constant()` calls (works for sparse values).
* Dynamically sized members (`std::string`, `std::vector`) have no
  Flecs struct-member mapping; the generator leaves a comment in the
  output and they remain fully accessible through RTTR.

## Plugin architecture

* Plugins are plain Qt `MODULE` libraries implementing
  `ISimulationModule` (`core/include/simcore/ISimulationModule.hpp`).
* `QPluginLoader::instance()` loads the library → the generated
  `RTTR_PLUGIN_REGISTRATION` blocks run automatically; all RTTR types and
  metadata become visible process-wide with **no explicit init call**.
* The host then calls `module->registerComponents(world)`, which invokes
  the generated Flecs registrars against the host-owned world.

### Shared-library discipline (important)

For one process-wide RTTR registry and one set of Flecs component ids,
the host and all plugins must link the **shared** RTTR and Flecs
libraries. Linking either statically into each plugin would give every
plugin a private registry/world state and break cross-module reflection.

## Extending

* **New metadata keys**: just use them in annotations — the vocabulary is
  open. Add a constant to `MetaKeys.hpp` when shared tooling consumes it.
* **New plugin**: copy `plugins/propulsion`, annotate headers, list them
  in `simmeta_attach_codegen`, declare/invoke the registrars.
* **Debugging the parse**: pass `EXTRA_ARGS --emit-model model.json` to
  dump the parsed type model as JSON, or run `metagen.py` by hand.

## Limitations & notes

* Use a CMake generator that exports `compile_commands.json` (Ninja or
  Makefiles). On Windows, use Ninja with MSVC or clang-cl rather than the
  Visual Studio generator.
* A component whose member type is an annotated type from a *different*
  header is fine for RTTR; for Flecs, register the defining header's unit
  first (call its registrar earlier in `registerComponents`).
* Annotated members must be public (the generator warns and skips
  otherwise).
* `libclang` needs clang builtin headers; the generator auto-detects the
  resource directory via `clang -print-resource-dir`, or accepts an
  explicit `--extra-arg=-resource-dir=...`.
