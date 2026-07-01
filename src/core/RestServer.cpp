#include "hdmi/RestServer.h"

#include <nlohmann/json.hpp>

#include <httplib.h>

namespace hdmi {

using nlohmann::json;

namespace {

json displayToJson(const DisplayInfo& d) {
    json modes = json::array();
    for (const auto& m : d.modes) {
        modes.push_back(json{{"width", m.width}, {"height", m.height}, {"hz", m.hz}});
    }
    return json{
        {"id", d.id},
        {"name", d.name},
        {"connector", d.connector},
        {"active", d.active},
        {"primary", d.primary},
        {"width", d.width},
        {"height", d.height},
        {"refreshHz", d.refreshHz},
        {"posX", d.posX},
        {"posY", d.posY},
        {"modes", modes},
    };
}

}  // namespace

struct RestServer::Impl {
    Impl(DisplayManager& mgr, RestConfig cfg) : manager(mgr), config(std::move(cfg)) {}

    DisplayManager& manager;
    RestConfig config;
    httplib::Server server;

    // Returns true if the request is authorised for the configured token.
    bool authorised(const httplib::Request& req) const {
        if (config.token.empty()) return true;  // auth disabled
        if (req.get_header_value("X-Auth-Token") == config.token) return true;
        if (req.has_param("token") && req.get_param_value("token") == config.token) return true;
        return false;
    }

    static void sendError(httplib::Response& res, int status, const std::string& msg) {
        res.status = status;
        res.set_content(json{{"error", msg}}.dump(), "application/json");
    }

    void setupRoutes() {
        // Reject unauthorised requests before any handler runs.
        server.set_pre_routing_handler(
            [this](const httplib::Request& req, httplib::Response& res) {
                if (!authorised(req)) {
                    sendError(res, 401, "invalid or missing token");
                    return httplib::Server::HandlerResponse::Handled;
                }
                return httplib::Server::HandlerResponse::Unhandled;
            });

        server.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(
                json{{"status", "ok"}, {"backend", manager.backendName()}}.dump(),
                "application/json");
        });

        server.Get("/api/displays", [this](const httplib::Request&, httplib::Response& res) {
            json arr = json::array();
            for (const auto& d : manager.displays()) arr.push_back(displayToJson(d));
            res.set_content(json{{"displays", arr}}.dump(), "application/json");
        });

        server.Post("/api/switch", [this](const httplib::Request& req, httplib::Response& res) {
            json body;
            try {
                body = json::parse(req.body);
            } catch (const std::exception&) {
                return sendError(res, 400, "request body is not valid JSON");
            }

            SwitchRequest sr;
            std::string modeStr = body.value("mode", "exclusive");
            if (!topologyFromString(modeStr, sr.topology)) {
                return sendError(res, 400, "unknown mode: " + modeStr);
            }
            if (!body.contains("ids") || !body["ids"].is_array()) {
                return sendError(res, 400, "missing 'ids' array");
            }
            for (const auto& id : body["ids"]) {
                if (!id.is_string()) return sendError(res, 400, "'ids' must be strings");
                sr.activeIds.push_back(id.get<std::string>());
            }

            std::string err;
            if (!manager.apply(sr, &err)) {
                return sendError(res, 400, err);
            }
            json arr = json::array();
            for (const auto& d : manager.displays()) arr.push_back(displayToJson(d));
            res.set_content(json{{"ok", true}, {"displays", arr}}.dump(), "application/json");
        });

        server.Post(R"(/api/displays/([^/]+)/activate)",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        const std::string id = req.matches[1];
                        std::string err;
                        if (!manager.activateExclusive(id, &err)) {
                            return sendError(res, 400, err);
                        }
                        json arr = json::array();
                        for (const auto& d : manager.displays()) arr.push_back(displayToJson(d));
                        res.set_content(json{{"ok", true}, {"displays", arr}}.dump(),
                                        "application/json");
                    });

        // Set a display's resolution (+ optional refresh). hz omitted/<=0 picks
        // the highest rate at that resolution. Selection is persisted.
        server.Post(R"(/api/displays/([^/]+)/mode)",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        const std::string id = req.matches[1];
                        json body;
                        try {
                            body = json::parse(req.body);
                        } catch (const std::exception&) {
                            return sendError(res, 400, "request body is not valid JSON");
                        }
                        if (!body.contains("width") || !body.contains("height")) {
                            return sendError(res, 400, "missing 'width'/'height'");
                        }
                        const int w = body.value("width", 0);
                        const int h = body.value("height", 0);
                        const int hz = body.value("hz", 0);
                        std::string err;
                        if (!manager.setMode(id, w, h, hz, &err)) {
                            return sendError(res, 400, err);
                        }
                        res.set_content(json{{"ok", true}}.dump(), "application/json");
                    });

        // Raise a display to the highest refresh rate at its current resolution.
        server.Post(R"(/api/displays/([^/]+)/maxhz)",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        const std::string id = req.matches[1];
                        std::string err;
                        if (!manager.setMaxRefresh(id, &err)) {
                            return sendError(res, 400, err);
                        }
                        res.set_content(json{{"ok", true}}.dump(), "application/json");
                    });
    }
};

RestServer::RestServer(DisplayManager& manager, RestConfig config)
    : impl_(std::make_unique<Impl>(manager, std::move(config))) {
    impl_->setupRoutes();
}

RestServer::~RestServer() {
    stop();
}

bool RestServer::start() {
    if (running_.load()) return true;

    // Bind synchronously so we can report failure to the caller, then serve on
    // the background thread.
    if (!impl_->server.bind_to_port(impl_->config.host.c_str(), impl_->config.port)) {
        return false;
    }
    running_.store(true);
    thread_ = std::thread([this]() {
        impl_->server.listen_after_bind();
        running_.store(false);
    });
    return true;
}

void RestServer::stop() {
    if (impl_) impl_->server.stop();
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

int RestServer::port() const {
    return impl_->config.port;
}

}  // namespace hdmi
