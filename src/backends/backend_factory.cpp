#include "hdmi/backend_factory.h"

#include "MockBackend.h"

#ifdef _WIN32
#include "WindowsBackend.h"
#endif

namespace hdmi {

std::unique_ptr<IDisplayBackend> makeDefaultBackend() {
#ifdef _WIN32
    return std::make_unique<WindowsBackend>();
#else
    return std::make_unique<MockBackend>();
#endif
}

}  // namespace hdmi
