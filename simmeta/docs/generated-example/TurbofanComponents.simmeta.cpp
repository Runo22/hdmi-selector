// ============================================================
//  GENERATED FILE — DO NOT EDIT.
//  Source : propulsion/TurbofanComponents.hpp
//  Emitted by tools/metagen/metagen.py (SimMeta pipeline).
// ============================================================

#include "propulsion/TurbofanComponents.hpp"

#include <rttr/registration>
#include <flecs.h>

#include <cstddef>

RTTR_PLUGIN_REGISTRATION
{
    using rttr::registration;

    registration::enumeration<sim::propulsion::IgniterMode>("sim::propulsion::IgniterMode")
    (
        rttr::value("Off", sim::propulsion::IgniterMode::Off),
        rttr::value("Automatic", sim::propulsion::IgniterMode::Automatic),
        rttr::value("Continuous", sim::propulsion::IgniterMode::Continuous),
        rttr::metadata("DisplayName", "Igniter Mode"),
        rttr::metadata("Description", "Exciter box operating mode"),
        rttr::metadata("Off.DisplayName", "Off"),
        rttr::metadata("Automatic.DisplayName", "Automatic (FADEC)"),
        rttr::metadata("Continuous.DisplayName", "Continuous Relight"),
        rttr::metadata("Continuous.Description", "Both exciters energized")
    );

    registration::class_<sim::propulsion::TurbofanEngine::SpoolState>("sim::propulsion::TurbofanEngine::SpoolState")
    (
        rttr::metadata("DisplayName", "Spool Speeds")
    )
        .constructor<>()(rttr::policy::ctor::as_object)
        .property("n1", &sim::propulsion::TurbofanEngine::SpoolState::n1)
        (
            rttr::metadata("DisplayName", "N1"),
            rttr::metadata("Min", 0.0),
            rttr::metadata("Max", 120.0),
            rttr::metadata("Step", 0.1),
            rttr::metadata("Units", "%")
        )
        .property("n2", &sim::propulsion::TurbofanEngine::SpoolState::n2)
        (
            rttr::metadata("DisplayName", "N2"),
            rttr::metadata("Min", 0.0),
            rttr::metadata("Max", 120.0),
            rttr::metadata("Step", 0.1),
            rttr::metadata("Units", "%")
        );

    registration::class_<sim::propulsion::TurbofanEngine>("sim::propulsion::TurbofanEngine")
    (
        rttr::metadata("Category", "Propulsion"),
        rttr::metadata("DisplayName", "Turbofan Engine"),
        rttr::metadata("Policy", "Replicated")
    )
        .constructor<>()(rttr::policy::ctor::as_object)
        .property("thrust", &sim::propulsion::TurbofanEngine::thrust)
        (
            rttr::metadata("DisplayName", "Net Thrust"),
            rttr::metadata("Min", 0.0),
            rttr::metadata("Max", 400.0),
            rttr::metadata("Step", 0.5),
            rttr::metadata("Units", "kN"),
            rttr::metadata("ReadOnly", true)
        )
        .property("throttle", &sim::propulsion::TurbofanEngine::throttle)
        (
            rttr::metadata("DisplayName", "Throttle Lever"),
            rttr::metadata("Min", 0.0),
            rttr::metadata("Max", 1.0),
            rttr::metadata("Step", 0.01)
        )
        .property("egtProbes", &sim::propulsion::TurbofanEngine::egtProbes)
        (
            rttr::metadata("DisplayName", "EGT Probes"),
            rttr::metadata("Min", -50.0),
            rttr::metadata("Max", 1200.0),
            rttr::metadata("Units", "degC"),
            rttr::metadata("ReadOnly", true)
        )
        .property("spools", &sim::propulsion::TurbofanEngine::spools)
        (
            rttr::metadata("DisplayName", "Spools")
        )
        .property("igniter", &sim::propulsion::TurbofanEngine::igniter)
        (
            rttr::metadata("DisplayName", "Igniter")
        )
        .property("performanceDeckPath", &sim::propulsion::TurbofanEngine::performanceDeckPath)
        (
            rttr::metadata("FilePath", true),
            rttr::metadata("DisplayName", "Performance Deck"),
            rttr::metadata("Description", "Engine performance deck (.csv)")
        )
        .property("fuelFlow", &sim::propulsion::TurbofanEngine::fuelFlow)
        (
            rttr::metadata("Transient", true),
            rttr::metadata("ReadOnly", true),
            rttr::metadata("Units", "kg/h")
        );

    registration::enumeration<sim::propulsion::FuelSystem::TankState::Status>("sim::propulsion::FuelSystem::TankState::Status")
    (
        rttr::value("Sealed", sim::propulsion::FuelSystem::TankState::Status::Sealed),
        rttr::value("Feeding", sim::propulsion::FuelSystem::TankState::Status::Feeding),
        rttr::value("Crossfeed", sim::propulsion::FuelSystem::TankState::Status::Crossfeed),
        rttr::metadata("DisplayName", "Tank Status"),
        rttr::metadata("Sealed.DisplayName", "Sealed"),
        rttr::metadata("Feeding.DisplayName", "Feeding"),
        rttr::metadata("Crossfeed.DisplayName", "Crossfeed")
    );

    registration::class_<sim::propulsion::FuelSystem::TankState>("sim::propulsion::FuelSystem::TankState")
    (
        rttr::metadata("DisplayName", "Fuel Tank")
    )
        .constructor<>()(rttr::policy::ctor::as_object)
        .property("quantity", &sim::propulsion::FuelSystem::TankState::quantity)
        (
            rttr::metadata("DisplayName", "Quantity"),
            rttr::metadata("Min", 0.0),
            rttr::metadata("Max", 12000.0),
            rttr::metadata("Step", 1.0),
            rttr::metadata("Units", "kg")
        )
        .property("status", &sim::propulsion::FuelSystem::TankState::status)
        (
            rttr::metadata("DisplayName", "Status")
        );

    registration::class_<sim::propulsion::FuelSystem>("sim::propulsion::FuelSystem")
    (
        rttr::metadata("Category", "Fuel"),
        rttr::metadata("DisplayName", "Fuel System"),
        rttr::metadata("Policy", "Replicated")
    )
        .constructor<>()(rttr::policy::ctor::as_object)
        .property("tanks", &sim::propulsion::FuelSystem::tanks)
        (
            rttr::metadata("DisplayName", "Tanks")
        )
        .property("fuelModelDirectory", &sim::propulsion::FuelSystem::fuelModelDirectory)
        (
            rttr::metadata("DirectoryPath", true),
            rttr::metadata("DisplayName", "Fuel Model Directory"),
            rttr::metadata("Description", "Directory holding fuel density tables")
        )
        .property("pumpDutyCycles", &sim::propulsion::FuelSystem::pumpDutyCycles)
        (
            rttr::metadata("DisplayName", "Pump Duty Cycles"),
            rttr::metadata("Min", 0.0),
            rttr::metadata("Max", 1.0),
            rttr::metadata("ReadOnly", true)
        );

}

namespace simmeta { namespace generated {

bool RegisterFlecsTypes_TurbofanComponents(flecs::world& world)
{
    bool allValid = true;

    {  // enum sim::propulsion::IgniterMode
        auto comp = world.component<sim::propulsion::IgniterMode>("sim::propulsion::IgniterMode");
        if (!comp.is_valid()) {
            allValid = false;
        } else {
            comp.constant("Off", sim::propulsion::IgniterMode::Off);
            comp.constant("Automatic", sim::propulsion::IgniterMode::Automatic);
            comp.constant("Continuous", sim::propulsion::IgniterMode::Continuous);
        }
    }

    {  // struct sim::propulsion::TurbofanEngine::SpoolState
        auto comp = world.component<sim::propulsion::TurbofanEngine::SpoolState>("sim::propulsion::TurbofanEngine::SpoolState");
        if (!comp.is_valid()) {
            allValid = false;
        } else {
            comp.member("n1", &sim::propulsion::TurbofanEngine::SpoolState::n1).range(0.0, 120.0);
            comp.member("n2", &sim::propulsion::TurbofanEngine::SpoolState::n2).range(0.0, 120.0);
        }
    }

    {  // struct sim::propulsion::TurbofanEngine
        auto comp = world.component<sim::propulsion::TurbofanEngine>("sim::propulsion::TurbofanEngine");
        if (!comp.is_valid()) {
            allValid = false;
        } else {
            comp.member("thrust", &sim::propulsion::TurbofanEngine::thrust).range(0.0, 400.0);
            comp.member("throttle", &sim::propulsion::TurbofanEngine::throttle).range(0.0, 1.0);
            comp.member<float>("egtProbes", 4, offsetof(sim::propulsion::TurbofanEngine, egtProbes)).range(-50.0, 1200.0);
            comp.member("spools", &sim::propulsion::TurbofanEngine::spools);
            comp.member("igniter", &sim::propulsion::TurbofanEngine::igniter);
            // 'performanceDeckPath' (std::basic_string<char>) has no Flecs struct-member mapping; it remains accessible through RTTR.
            comp.member("fuelFlow", &sim::propulsion::TurbofanEngine::fuelFlow);
        }
    }

    {  // enum sim::propulsion::FuelSystem::TankState::Status
        auto comp = world.component<sim::propulsion::FuelSystem::TankState::Status>("sim::propulsion::FuelSystem::TankState::Status");
        if (!comp.is_valid()) {
            allValid = false;
        } else {
            comp.constant("Sealed", sim::propulsion::FuelSystem::TankState::Status::Sealed);
            comp.constant("Feeding", sim::propulsion::FuelSystem::TankState::Status::Feeding);
            comp.constant("Crossfeed", sim::propulsion::FuelSystem::TankState::Status::Crossfeed);
        }
    }

    {  // struct sim::propulsion::FuelSystem::TankState
        auto comp = world.component<sim::propulsion::FuelSystem::TankState>("sim::propulsion::FuelSystem::TankState");
        if (!comp.is_valid()) {
            allValid = false;
        } else {
            comp.member("quantity", &sim::propulsion::FuelSystem::TankState::quantity).range(0.0, 12000.0);
            comp.member("status", &sim::propulsion::FuelSystem::TankState::status);
        }
    }

    {  // struct sim::propulsion::FuelSystem
        auto comp = world.component<sim::propulsion::FuelSystem>("sim::propulsion::FuelSystem");
        if (!comp.is_valid()) {
            allValid = false;
        } else {
            comp.member<sim::propulsion::FuelSystem::TankState>("tanks", 3, offsetof(sim::propulsion::FuelSystem, tanks));
            // 'fuelModelDirectory' (std::basic_string<char>) has no Flecs struct-member mapping; it remains accessible through RTTR.
            // 'pumpDutyCycles' (std::vector<float>) has no Flecs struct-member mapping; it remains accessible through RTTR.
        }
    }

    return allValid;
}

}}  // namespace simmeta::generated
