#pragma once

/// @file ISimulationModule.hpp
/// Common interface implemented by every dynamically loaded simulation
/// module (Qt plugin).
///
/// Load sequence per plugin:
///   1. QPluginLoader::instance() loads the shared library. At that moment
///      the generated RTTR_PLUGIN_REGISTRATION blocks run automatically —
///      RTTR types and their metadata become visible process-wide with no
///      further calls.
///   2. The host qobject_casts the root object to ISimulationModule and
///      calls registerComponents(world), which invokes the generated Flecs
///      registrars for the module's code-generation units.

#include <QString>
#include <QtPlugin>

#include <flecs.h>

class ISimulationModule
{
public:
    virtual ~ISimulationModule() = default;

    /// Human-readable module identifier (diagnostics, logging).
    virtual QString moduleName() const = 0;

    /// Registers this module's compile-time component types with the given
    /// Flecs world by delegating to the generated registrar functions.
    /// Returns false if any component failed the is_valid() check.
    virtual bool registerComponents(flecs::world& world) = 0;
};

#define SimMeta_ISimulationModule_iid "com.simmeta.ISimulationModule/1.0"
Q_DECLARE_INTERFACE(ISimulationModule, SimMeta_ISimulationModule_iid)
