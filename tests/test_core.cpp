// Lightweight assertion-based tests for the core logic and Mock backend.
// Exercised on every platform; no external test framework required.

#include <iostream>
#include <memory>
#include <string>

#include <cstdio>

#include "../src/backends/MockBackend.h"
#include "hdmi/DisplayManager.h"
#include "hdmi/ModeStore.h"

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    } else {
        std::cout << "ok: " << what << "\n";
    }
}

const hdmi::DisplayInfo* find(const std::vector<hdmi::DisplayInfo>& v, const std::string& id) {
    for (const auto& d : v) if (d.id == id) return &d;
    return nullptr;
}

std::unique_ptr<hdmi::DisplayManager> makeManager() {
    return std::make_unique<hdmi::DisplayManager>(std::make_unique<hdmi::MockBackend>());
}

void testDefaultState() {
    auto mgr = makeManager();
    auto d = mgr->displays();
    check(d.size() == 2, "default setup has two displays");
    check(find(d, "monitor") && find(d, "monitor")->active, "monitor active by default");
    check(find(d, "tv") && !find(d, "tv")->active, "tv inactive by default");
}

void testExclusiveSwitch() {
    auto mgr = makeManager();
    std::string err;
    check(mgr->activateExclusive("tv", &err), "activate tv exclusively succeeds");
    auto d = mgr->displays();
    check(find(d, "tv")->active && find(d, "tv")->primary, "tv is now active + primary");
    check(!find(d, "monitor")->active, "monitor turned off in exclusive mode");
}

void testExtend() {
    auto mgr = makeManager();
    hdmi::SwitchRequest req;
    req.topology = hdmi::Topology::Extend;
    req.activeIds = {"monitor", "tv"};
    std::string err;
    check(mgr->apply(req, &err), "extend across both succeeds");
    auto d = mgr->displays();
    check(find(d, "monitor")->active && find(d, "tv")->active, "both active when extended");
    check(find(d, "monitor")->posX != find(d, "tv")->posX, "extended displays are tiled apart");
}

void testDuplicate() {
    auto mgr = makeManager();
    hdmi::SwitchRequest req;
    req.topology = hdmi::Topology::Duplicate;
    req.activeIds = {"monitor", "tv"};
    std::string err;
    check(mgr->apply(req, &err), "duplicate across both succeeds");
    auto d = mgr->displays();
    check(find(d, "monitor")->posX == 0 && find(d, "tv")->posX == 0,
          "duplicated displays share the origin");
}

void testValidation() {
    auto mgr = makeManager();
    std::string err;

    hdmi::SwitchRequest empty;
    check(!mgr->apply(empty, &err), "empty request rejected");

    hdmi::SwitchRequest exclTwo;
    exclTwo.topology = hdmi::Topology::Exclusive;
    exclTwo.activeIds = {"monitor", "tv"};
    check(!mgr->apply(exclTwo, &err), "exclusive with two ids rejected");

    hdmi::SwitchRequest unknown;
    unknown.topology = hdmi::Topology::Exclusive;
    unknown.activeIds = {"projector"};
    check(!mgr->apply(unknown, &err), "unknown id rejected");

    hdmi::SwitchRequest dup;
    dup.topology = hdmi::Topology::Extend;
    dup.activeIds = {"monitor", "monitor"};
    check(!mgr->apply(dup, &err), "duplicate id in request rejected");

    hdmi::SwitchRequest extendOne;
    extendOne.topology = hdmi::Topology::Extend;
    extendOne.activeIds = {"monitor"};
    check(!mgr->apply(extendOne, &err), "extend with a single id rejected");
}

void testSetMode() {
    auto mgr = makeManager();
    std::string err;
    check(mgr->setMode("monitor", 2560, 1440, 0, &err), "set monitor to 2K (best hz)");
    auto d = mgr->displays();
    check(find(d, "monitor")->width == 2560 && find(d, "monitor")->height == 1440,
          "monitor now at 2560x1440");
    check(find(d, "monitor")->refreshHz == 120, "2K picked highest hz (120)");

    check(!mgr->setMode("monitor", 1234, 567, 0, &err), "unsupported resolution rejected");
    check(mgr->setMaxRefresh("monitor", &err), "max refresh at current res succeeds");
}

void testPersistence() {
    const std::string path = "/tmp/hdmi_test_modes.json";
    std::remove(path.c_str());

    // First session: choose 4K on the tv and 2K on the monitor.
    {
        auto store = std::make_shared<hdmi::ModeStore>(path);
        hdmi::DisplayManager mgr(std::make_unique<hdmi::MockBackend>(), store);
        std::string err;
        check(mgr.setMode("monitor", 2560, 1440, 120, &err), "session1: set monitor 2K@120");
    }

    // Second session with the same store: the saved mode is restored.
    {
        auto store = std::make_shared<hdmi::ModeStore>(path);
        hdmi::DisplayManager mgr(std::make_unique<hdmi::MockBackend>(), store);
        mgr.applySavedModes();
        auto d = mgr.displays();
        check(find(d, "monitor")->width == 2560 && find(d, "monitor")->refreshHz == 120,
              "session2: monitor restored to saved 2K@120");
        // The tv had no saved entry, so it keeps its current (default) state.
        check(find(d, "tv") != nullptr, "tv still present with its current mode");
    }
    std::remove(path.c_str());
}

void testTopologyStrings() {
    hdmi::Topology t;
    check(hdmi::topologyFromString("extend", t) && t == hdmi::Topology::Extend,
          "parse 'extend'");
    check(!hdmi::topologyFromString("bogus", t), "reject unknown topology string");
    check(std::string(hdmi::topologyToString(hdmi::Topology::Duplicate)) == "duplicate",
          "stringify duplicate");
}

}  // namespace

int main() {
    testDefaultState();
    testExclusiveSwitch();
    testExtend();
    testDuplicate();
    testValidation();
    testSetMode();
    testPersistence();
    testTopologyStrings();

    if (g_failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cerr << "\n" << g_failures << " test(s) failed.\n";
    return 1;
}
