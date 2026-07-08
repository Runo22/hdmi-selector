#pragma once

/// @file MetaKeys.hpp
/// Canonical metadata key names shared by annotated headers, the code
/// generator, and every consumer of the reflected data (property editors,
/// serializers, network replication, ...).
///
/// The generator emits keys verbatim as string literals; this header exists
/// so that *consumers* query metadata through named constants instead of
/// scattering magic strings. The vocabulary is open — plugins may introduce
/// their own keys — but anything used by shared tooling belongs here.

namespace simmeta::keys {

// --- Presentation ----------------------------------------------------------
inline constexpr const char* DisplayName = "DisplayName";
inline constexpr const char* Description = "Description";
inline constexpr const char* Category    = "Category";

// --- Numeric range / editing -----------------------------------------------
inline constexpr const char* Min   = "Min";
inline constexpr const char* Max   = "Max";
inline constexpr const char* Step  = "Step";
inline constexpr const char* Units = "Units";

// --- Path semantics ---------------------------------------------------------
/// Flag: the string property holds a path to a file (editors show a file
/// picker).
inline constexpr const char* FilePath = "FilePath";
/// Flag: the string property holds a path to a directory.
inline constexpr const char* DirectoryPath = "DirectoryPath";

// --- Lifecycle / access -----------------------------------------------------
/// Flag: excluded from persistence (snapshots, scenario files).
inline constexpr const char* Transient = "Transient";
/// Flag: surfaced to UIs as non-editable (simulation output, not input).
inline constexpr const char* ReadOnly = "ReadOnly";

// --- Distribution -----------------------------------------------------------
/// Component-level tag describing distribution behaviour, e.g. "Replicated",
/// "LocalOnly", "Recorded".
inline constexpr const char* Policy = "Policy";

}  // namespace simmeta::keys
