#pragma once

#include <memory>

#include "hdmi/IDisplayBackend.h"

namespace hdmi {

// Returns the appropriate backend for the host platform: the real Windows
// (CCD) backend when compiled for Windows, otherwise the Mock backend. This is
// the single place that decides which implementation to instantiate.
std::unique_ptr<IDisplayBackend> makeDefaultBackend();

}  // namespace hdmi
