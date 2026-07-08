#pragma once

/// @file TurbofanComponents.hpp
/// Propulsion-domain simulation components.
///
/// This header is a code-generation unit: tools/metagen/metagen.py parses it
/// and emits TurbofanComponents.simmeta.cpp containing the RTTR registration
/// block and the Flecs registrar
/// `simmeta::generated::RegisterFlecsTypes_TurbofanComponents`.
///
/// Note that the header itself depends only on the standard library and the
/// (empty in normal builds) annotation macros — no RTTR, no Flecs, no Qt.

#include <simmeta/Annotations.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sim::propulsion {

/// Igniter operating mode. Demonstrates a namespace-level scoped enum with
/// a sparse (non-contiguous) enumerator value.
enum class SIM_ENUM(DisplayName = "Igniter Mode",
                    Description = "Exciter box operating mode")
    IgniterMode : std::uint8_t {
    Off SIM_ENUM_VALUE(DisplayName = "Off") = 0,
    Automatic SIM_ENUM_VALUE(DisplayName = "Automatic (FADEC)") = 1,
    Continuous SIM_ENUM_VALUE(DisplayName = "Continuous Relight",
                              Description = "Both exciters energized") = 5,
};

/// Primary engine state component.
struct SIM_COMPONENT(Category = "Propulsion",
                     DisplayName = "Turbofan Engine",
                     Policy = "Replicated")
    TurbofanEngine {

    /// Inner struct: rotor spool speeds. Registered as its own reflected
    /// type and embedded below as a member.
    struct SIM_COMPONENT(DisplayName = "Spool Speeds") SpoolState {
        SIM_PROPERTY(DisplayName = "N1", Min = 0.0, Max = 120.0, Step = 0.1,
                     Units = "%")
        double n1 = 0.0;

        SIM_PROPERTY(DisplayName = "N2", Min = 0.0, Max = 120.0, Step = 0.1,
                     Units = "%")
        double n2 = 0.0;
    };

    SIM_PROPERTY(DisplayName = "Net Thrust", Min = 0.0, Max = 400.0,
                 Step = 0.5, Units = "kN", ReadOnly)
    double thrust = 0.0;

    SIM_PROPERTY(DisplayName = "Throttle Lever", Min = 0.0, Max = 1.0,
                 Step = 0.01)
    double throttle = 0.0;

    /// Fixed-size array member (mirrored into Flecs as an array member and
    /// into RTTR as a sequential container property).
    SIM_PROPERTY(DisplayName = "EGT Probes", Min = -50.0, Max = 1200.0,
                 Units = "degC", ReadOnly)
    std::array<float, 4> egtProbes{};

    SIM_PROPERTY(DisplayName = "Spools")
    SpoolState spools;

    SIM_PROPERTY(DisplayName = "Igniter")
    IgniterMode igniter = IgniterMode::Off;

    /// String property carrying path semantics for editors.
    SIM_PROPERTY(FilePath, DisplayName = "Performance Deck",
                 Description = "Engine performance deck (.csv)")
    std::string performanceDeckPath;

    SIM_PROPERTY(Transient, ReadOnly, Units = "kg/h")
    double fuelFlow = 0.0;
};

/// Fuel system component. Demonstrates deep nesting (component -> inner
/// struct -> inner enum), arrays of user types, and directory-path metadata.
struct SIM_COMPONENT(Category = "Fuel",
                     DisplayName = "Fuel System",
                     Policy = "Replicated")
    FuelSystem {

    struct SIM_COMPONENT(DisplayName = "Fuel Tank") TankState {
        /// Enum nested two levels deep — the parser reaches it recursively.
        enum class SIM_ENUM(DisplayName = "Tank Status") Status : std::uint8_t {
            Sealed SIM_ENUM_VALUE(DisplayName = "Sealed") = 0,
            Feeding SIM_ENUM_VALUE(DisplayName = "Feeding"),
            Crossfeed SIM_ENUM_VALUE(DisplayName = "Crossfeed"),
        };

        SIM_PROPERTY(DisplayName = "Quantity", Min = 0.0, Max = 12000.0,
                     Step = 1.0, Units = "kg")
        double quantity = 0.0;

        SIM_PROPERTY(DisplayName = "Status")
        Status status = Status::Sealed;
    };

    /// Array of nested reflected structs.
    SIM_PROPERTY(DisplayName = "Tanks")
    std::array<TankState, 3> tanks{};

    SIM_PROPERTY(DirectoryPath, DisplayName = "Fuel Model Directory",
                 Description = "Directory holding fuel density tables")
    std::string fuelModelDirectory;

    /// Dynamically sized container: registered with RTTR (variant sequential
    /// view) but intentionally skipped in the Flecs struct description — the
    /// generator documents that in its output.
    SIM_PROPERTY(DisplayName = "Pump Duty Cycles", Min = 0.0, Max = 1.0,
                 ReadOnly)
    std::vector<float> pumpDutyCycles;
};

}  // namespace sim::propulsion
