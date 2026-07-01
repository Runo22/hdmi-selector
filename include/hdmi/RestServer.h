#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "hdmi/DisplayManager.h"

namespace hdmi {

struct RestConfig {
    // Address to bind. Use "127.0.0.1" for local-only, or "0.0.0.0" to accept
    // connections from the LAN (recommended together with a token).
    std::string host = "127.0.0.1";
    int port = 8420;

    // If non-empty, requests must present this value in the "X-Auth-Token"
    // header (or "?token=" query parameter). Strongly recommended when the
    // server is bound to the LAN.
    std::string token;
};

// Small HTTP/REST front-end over a DisplayManager. Runs the listener on a
// background thread so it can sit alongside the GUI event loop.
//
// Endpoints (all JSON):
//   GET  /api/health              -> { "status": "ok", "backend": "..." }
//   GET  /api/displays            -> { "displays": [ ... ] }
//   POST /api/switch              body { "mode": "exclusive|extend|duplicate",
//                                        "ids": ["..."] }
//   POST /api/displays/{id}/activate  -> exclusive-activate that display
class RestServer {
public:
    RestServer(DisplayManager& manager, RestConfig config);
    ~RestServer();

    RestServer(const RestServer&) = delete;
    RestServer& operator=(const RestServer&) = delete;

    // Starts listening on a background thread. Returns false if the socket
    // could not be bound.
    bool start();

    // Stops the listener and joins the thread. Safe to call multiple times.
    void stop();

    bool running() const { return running_.load(); }
    int port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace hdmi
