#include "hdmi/RestServer.h"

#include <nlohmann/json.hpp>

#include <httplib.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace hdmi {

using nlohmann::json;

namespace {

// Same config-directory convention as ModeStore, so both land next to each
// other under %APPDATA%\HdmiSelector (or ~/.config/hdmi-selector).
std::string logPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    std::string base = appdata ? appdata : ".";
    return base + "\\HdmiSelector\\rest.log";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else if (home && *home) base = std::string(home) + "/.config";
    else base = ".";
    return base + "/hdmi-selector/rest.log";
#endif
}

std::string timestamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

json displayToJson(const DisplayInfo& d) {
    json modes = json::array();
    for (const auto& m : d.modes) {
        modes.push_back(json{{"width", m.width}, {"height", m.height}, {"hz", m.hz}});
    }
    return json{
        {"id", d.id},
        {"name", d.name},
        {"connector", d.connector},
        {"connectorLabel", d.connectorLabel},
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

// Short human-readable summary of an activated display, e.g.
// "Selected SONY TV *30 (4K/50Hz)". Meant for clients (like a phone shortcut)
// that just want something to show, without parsing the full display object.
std::string activationMessage(const DisplayInfo& d) {
    return "Selected " + d.name + " (" + resolutionLabel(d.width, d.height) + "/" +
           std::to_string(d.refreshHz) + "Hz)";
}

// Same idea for /api/switch, which can also produce Extend/Duplicate layouts
// that don't have a single "the" display to describe.
std::string switchMessage(Topology topology, const std::vector<DisplayInfo>& displays) {
    if (topology == Topology::Exclusive) {
        for (const auto& d : displays) {
            if (d.active) return activationMessage(d);
        }
        return "";
    }
    int active = 0;
    for (const auto& d : displays) {
        if (d.active) ++active;
    }
    const char* verb = (topology == Topology::Extend) ? "Extended across " : "Duplicated across ";
    return verb + std::to_string(active) + " displays";
}

}  // namespace

struct RestServer::Impl {
    Impl(DisplayManager& mgr, RestConfig cfg) : manager(mgr), config(std::move(cfg)) {}

    DisplayManager& manager;
    RestConfig config;
    httplib::Server server;
    std::mutex logMutex;

    // Appends a line to %APPDATA%\HdmiSelector\rest.log (best effort). This is
    // the main way to tell "request never arrived" (firewall/network/wrong IP)
    // apart from "request arrived but was rejected" (bad token/route) when
    // debugging from another device, since the GUI build has no console.
    void log(const std::string& line) {
        std::lock_guard<std::mutex> lock(logMutex);
        const std::string path = logPath();
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::app);
        if (out) out << timestamp() << "  " << line << "\n";
    }

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
        // Log every request that reaches this process, successful or not -
        // if nothing shows up here while the phone is trying, the request
        // isn't reaching Windows at all (firewall/router/wrong IP), rather
        // than being rejected by the app.
        server.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
            log(req.remote_addr + ":" + std::to_string(req.remote_port) + "  " + req.method +
                " " + req.path + " -> " + std::to_string(res.status));
        });

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
            const auto displays = manager.displays();
            json arr = json::array();
            for (const auto& d : displays) arr.push_back(displayToJson(d));
            res.set_content(json{{"ok", true},
                                 {"message", switchMessage(sr.topology, displays)},
                                 {"displays", arr}}
                                .dump(),
                            "application/json");
        });

        server.Post(R"(/api/displays/([^/]+)/activate)",
                    [this](const httplib::Request& req, httplib::Response& res) {
                        const std::string id = req.matches[1];
                        std::string err;
                        if (!manager.activateExclusive(id, &err)) {
                            return sendError(res, 400, err);
                        }
                        const auto displays = manager.displays();
                        json arr = json::array();
                        for (const auto& d : displays) arr.push_back(displayToJson(d));
                        res.set_content(json{{"ok", true},
                                             {"message", switchMessage(Topology::Exclusive, displays)},
                                             {"displays", arr}}
                                            .dump(),
                                        "application/json");
                    });

        // Stateless toggle to the next display (no id/body needed). Exposed on
        // GET and POST so a plain URL works from simple HTTP clients.
        auto toggleHandler = [this](const httplib::Request&, httplib::Response& res) {
            std::string id, name, err;
            if (!manager.toggle(&id, &name, &err)) {
                return sendError(res, 400, err);
            }
            const auto displays = manager.displays();
            json arr = json::array();
            for (const auto& d : displays) arr.push_back(displayToJson(d));
            res.set_content(
                json{{"ok", true},
                     {"message", switchMessage(Topology::Exclusive, displays)},
                     {"activated", {{"id", id}, {"name", name}}},
                     {"displays", arr}}
                    .dump(),
                "application/json");
        };
        server.Get("/api/toggle", toggleHandler);
        server.Post("/api/toggle", toggleHandler);

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
        impl_->log("FAILED to bind " + impl_->config.host + ":" +
                   std::to_string(impl_->config.port) + " (port already in use?)");
        return false;
    }
    impl_->log("listening on " + impl_->config.host + ":" + std::to_string(impl_->config.port) +
              " (auth " + (impl_->config.token.empty() ? "disabled)" : "enabled)"));
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
