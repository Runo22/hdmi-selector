/// SimMeta example host.
///
/// Loads simulation modules (Qt plugins) given on the command line, lets
/// their generated code register everything, then demonstrates that both
/// reflection systems see the types *without the host knowing them at
/// compile time*:
///
///   * RTTR:  enumerates properties and their metadata (Min/Max/Step/Units,
///            path semantics, policies) — this is what a generic property
///            editor or scenario serializer would consume.
///   * Flecs: looks the component up by name, attaches it to an entity and
///            prints the meta-serialized JSON.
///
/// Usage:  simmeta_host <path/to/module> [more modules...]

#include <simcore/ISimulationModule.hpp>
#include <simmeta/MetaKeys.hpp>

#include <QCoreApplication>
#include <QPluginLoader>

#include <flecs.h>
#include <rttr/type>

#include <iostream>
#include <string>

namespace
{

void printMetadataIfPresent(const rttr::property& property, const char* key)
{
    const rttr::variant value = property.get_metadata(key);
    if (value.is_valid()) {
        std::cout << "        " << key << " = "
                  << value.to_string().c_str() << "\n";
    }
}

void dumpReflectedType(const std::string& typeName)
{
    const rttr::type type = rttr::type::get_by_name(typeName);
    if (!type.is_valid()) {
        std::cout << "  (RTTR type not found: " << typeName << ")\n";
        return;
    }

    std::cout << "  " << typeName << "\n";
    for (const rttr::property& property : type.get_properties()) {
        std::cout << "    ." << property.get_name().to_string() << " : "
                  << property.get_type().get_name().to_string() << "\n";
        using namespace simmeta::keys;
        for (const char* key : {DisplayName, Description, Min, Max, Step,
                                Units, FilePath, DirectoryPath, Transient,
                                ReadOnly}) {
            printMetadataIfPresent(property, key);
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <module.so|module.dll> ...\n";
        return 2;
    }

    flecs::world world;

    for (int i = 1; i < argc; ++i) {
        QPluginLoader loader(QString::fromLocal8Bit(argv[i]));

        // Loading the library runs the generated RTTR_PLUGIN_REGISTRATION
        // blocks; RTTR types are available from this point on.
        QObject* rootObject = loader.instance();
        if (rootObject == nullptr) {
            std::cerr << "failed to load " << argv[i] << ": "
                      << loader.errorString().toStdString() << "\n";
            return 1;
        }

        auto* module = qobject_cast<ISimulationModule*>(rootObject);
        if (module == nullptr) {
            std::cerr << argv[i]
                      << " does not implement ISimulationModule\n";
            return 1;
        }

        std::cout << "loaded module: "
                  << module->moduleName().toStdString() << "\n";

        if (!module->registerComponents(world)) {
            std::cerr << "component registration reported failures for "
                      << module->moduleName().toStdString() << "\n";
            return 1;
        }
    }

    std::cout << "\n=== RTTR view (metadata-driven tooling) ===\n";
    dumpReflectedType("sim::propulsion::TurbofanEngine");
    dumpReflectedType("sim::propulsion::TurbofanEngine::SpoolState");
    dumpReflectedType("sim::propulsion::FuelSystem");

    std::cout << "\n=== Flecs view (runtime component access) ===\n";
    const flecs::entity engineComponent =
        world.lookup("sim::propulsion::TurbofanEngine");
    if (engineComponent.is_valid()) {
        flecs::entity demo = world.entity("DemoEngine");
        demo.add(engineComponent);
        std::cout << demo.to_json() << "\n";
    } else {
        std::cout << "  (TurbofanEngine component not found)\n";
    }

    return 0;
}
