// Headless entry point: runs the DisplayManager + REST server with no GUI.
//
// On Windows it drives the real display hardware; on other platforms it falls
// back to the Mock backend so the API can be exercised anywhere. Useful as a
// lightweight service and as the target used for automated testing.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "hdmi/DisplayManager.h"
#include "hdmi/ModeStore.h"
#include "hdmi/RestServer.h"
#include "hdmi/backend_factory.h"

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop.store(true); }
}  // namespace

int main(int argc, char** argv) {
    hdmi::RestConfig cfg;
    cfg.host = "127.0.0.1";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (arg == "--host") cfg.host = next();
        else if (arg == "--port") cfg.port = std::atoi(next().c_str());
        else if (arg == "--token") cfg.token = next();
        else if (arg == "--lan") cfg.host = "0.0.0.0";
        else if (arg == "--help") {
            std::cout << "Usage: hdmi_server [--host H] [--port N] [--token T] [--lan]\n";
            return 0;
        }
    }

    auto store = std::make_shared<hdmi::ModeStore>(hdmi::ModeStore::defaultPath());
    auto manager =
        std::make_unique<hdmi::DisplayManager>(hdmi::makeDefaultBackend(), store);
    manager->applySavedModes();  // restore per-monitor selections
    hdmi::RestServer server(*manager, cfg);
    if (!server.start()) {
        std::cerr << "error: could not bind " << cfg.host << ":" << cfg.port << "\n";
        return 1;
    }
    std::cout << "hdmi-selector server (backend=" << manager->backendName() << ") listening on "
              << cfg.host << ":" << cfg.port << (cfg.token.empty() ? " [no auth]" : " [token]")
              << std::endl;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "shutting down..." << std::endl;
    server.stop();
    return 0;
}
