#include "PropulsionModule.hpp"

#include <simmeta/Annotations.hpp>

// One declaration per code-generation unit (header) of this module. The
// definitions live in the generated <unit>.simmeta.cpp files that
// simmeta_attach_codegen() adds to this target.
SIMMETA_DECLARE_FLECS_REGISTRAR(TurbofanComponents)

QString PropulsionModule::moduleName() const
{
    return QStringLiteral("Propulsion");
}

bool PropulsionModule::registerComponents(flecs::world& world)
{
    bool ok = true;
    ok &= SIMMETA_FLECS_REGISTRAR(TurbofanComponents)(world);
    return ok;
}
