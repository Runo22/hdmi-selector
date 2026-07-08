#pragma once

/// @file Annotations.hpp
/// SimMeta annotation macros — the authoring surface of the reflection
/// pipeline.
///
/// In a normal compile every macro below expands to *nothing*, so annotated
/// headers carry zero runtime, binary, or dependency cost. When the
/// code-generation pass runs (tools/metagen/metagen.py re-parses the header
/// with -DSIMMETA_CODEGEN=1), the macros expand to Clang `annotate`
/// attributes instead. Those attributes surface in the AST as ANNOTATE_ATTR
/// nodes whose string payload is the stringized macro argument list.
///
/// This is deliberately how metadata extraction stays robust: the generator
/// never scans raw source tokens or guesses at macro boundaries — the
/// compiler itself performs macro expansion, string concatenation, and
/// attachment of the payload to the exact declaration it decorates.
///
/// Metadata payload grammar (inside the macro parentheses):
///
///     Key = Value, Key = "quoted string", BareFlag, ...
///
///   * numbers   ->  Min = 0.0, Max = 120.0, Step = 0.5
///   * strings   ->  Units = "kN", DisplayName = "Net Thrust"
///   * flags     ->  Transient, ReadOnly, FilePath   (parsed as `true`)
///
/// The key vocabulary is open; canonical keys used by the tooling live in
/// simmeta/MetaKeys.hpp.

#if defined(SIMMETA_CODEGEN)
#    define SIMMETA_DETAIL_ANNOTATE(kind, payload)                            \
        __attribute__((annotate("simmeta:" kind ":" payload)))
#else
#    define SIMMETA_DETAIL_ANNOTATE(kind, payload)
#endif

/// Marks a struct/class as a reflected simulation component.
/// Placement: between the class-key and the type name.
///
///     struct SIM_COMPONENT(Category = "Propulsion") TurbofanEngine { ... };
#define SIM_COMPONENT(...) SIMMETA_DETAIL_ANNOTATE("component", #__VA_ARGS__)

/// Marks a (public) data member as a reflected property.
/// Placement: immediately before the member declaration.
///
///     SIM_PROPERTY(Min = 0.0, Max = 400.0, Units = "kN")
///     double thrust = 0.0;
#define SIM_PROPERTY(...) SIMMETA_DETAIL_ANNOTATE("property", #__VA_ARGS__)

/// Marks an enum (scoped or unscoped, at any nesting depth) as reflected.
/// Placement: between `enum` / `enum class` and the enum name.
///
///     enum class SIM_ENUM(DisplayName = "Igniter Mode") IgniterMode { ... };
#define SIM_ENUM(...) SIMMETA_DETAIL_ANNOTATE("enum", #__VA_ARGS__)

/// Attaches metadata to an individual enumerator.
/// Placement: after the enumerator name, before `=` / `,`.
///
///     Continuous SIM_ENUM_VALUE(DisplayName = "Continuous Relight") = 5,
#define SIM_ENUM_VALUE(...) SIMMETA_DETAIL_ANNOTATE("enumerator", #__VA_ARGS__)

// ---------------------------------------------------------------------------
// Access to the generated Flecs registrar.
//
// For every processed header <UnitName>.hpp the generator emits
//
//     bool simmeta::generated::RegisterFlecsTypes_<UnitName>(flecs::world&);
//
// The macros below let a module forward-declare and invoke that function
// without needing any generated header to exist.
// ---------------------------------------------------------------------------

/// Expands to the fully qualified name of the generated Flecs registrar.
#define SIMMETA_FLECS_REGISTRAR(UnitName)                                     \
    ::simmeta::generated::RegisterFlecsTypes_##UnitName

/// Forward-declares the generated Flecs registrar for a code-gen unit.
#define SIMMETA_DECLARE_FLECS_REGISTRAR(UnitName)                             \
    namespace flecs {                                                         \
    struct world;                                                             \
    }                                                                         \
    namespace simmeta { namespace generated {                                 \
    bool RegisterFlecsTypes_##UnitName(::flecs::world& world);                \
    }}
