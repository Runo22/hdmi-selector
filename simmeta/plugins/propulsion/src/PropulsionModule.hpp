#pragma once

#include <simcore/ISimulationModule.hpp>

#include <QObject>

/// Propulsion simulation module.
///
/// RTTR registration for this plugin's types is fully automatic: the
/// generated RTTR_PLUGIN_REGISTRATION block runs when QPluginLoader loads
/// the shared library. Flecs registration is explicit (a world instance is
/// needed) and happens in registerComponents().
class PropulsionModule : public QObject, public ISimulationModule
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SimMeta_ISimulationModule_iid
                      FILE "PropulsionModule.json")
    Q_INTERFACES(ISimulationModule)

public:
    QString moduleName() const override;
    bool registerComponents(flecs::world& world) override;
};
