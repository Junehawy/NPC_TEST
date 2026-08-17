#include "demo_config.h"

#include <array>
#include <string_view>
#include <utility>

namespace npc_agent::adapter::godot_demo {

namespace {

using json = nlohmann::json;

// 字段路径拼接（错误定位用）。
std::string path_of(const std::string& base, std::string_view key) {
    return base.empty() ? std::string(key) : base + "." + std::string(key);
}

// ---- 类型化取值助手（fail-fast：类型不符即报错，不静默用默认值） ----
bool get_bool(const json& obj, const std::string& path, bool& out, std::string& err) {
    if (!obj.is_boolean()) {
        err = "demo 配置字段 " + path + " 应为布尔值";
        return false;
    }
    out = obj.get<bool>();
    return true;
}

bool get_number(const json& obj, const std::string& path, double& out, std::string& err) {
    if (!obj.is_number()) {
        err = "demo 配置字段 " + path + " 应为数值";
        return false;
    }
    out = obj.get<double>();
    return true;
}

bool get_string(const json& obj, const std::string& path, std::string& out, std::string& err) {
    if (!obj.is_string()) {
        err = "demo 配置字段 " + path + " 应为字符串";
        return false;
    }
    out = obj.get<std::string>();
    return true;
}

bool get_vec3(const json& obj, const std::string& path, Vec3& out, std::string& err) {
    if (!obj.is_array() || obj.size() != 3) {
        err = "demo 配置字段 " + path + " 应为 [x, y, z] 数组";
        return false;
    }
    for (const auto& v : obj) {
        if (!v.is_number()) {
            err = "demo 配置字段 " + path + " 元素应为数值";
            return false;
        }
    }
    out = Vec3{obj[0].get<float>(), obj[1].get<float>(), obj[2].get<float>()};
    return true;
}

bool get_waypoints(const json& obj, const std::string& path, std::vector<Vec3>& out,
                   std::string& err) {
    if (!obj.is_array() || obj.empty()) {
        err = "demo 配置字段 " + path + " 应为非空数组";
        return false;
    }
    std::vector<Vec3> points;
    points.reserve(obj.size());
    for (std::size_t i = 0; i < obj.size(); ++i) {
        Vec3 p;
        if (!get_vec3(obj[i], path + "[" + std::to_string(i) + "]", p, err))
            return false;
        points.push_back(p);
    }
    out = std::move(points);
    return true;
}

// 检查对象中是否出现未知键（fail-fast，防拼写错误静默失效）。
bool check_known_keys(const json& obj, const std::string& path,
                      const std::array<std::string_view, 8>& known, std::string& err) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool found = false;
        for (const auto k : known)
            found = found || (k == it.key());
        if (!found) {
            err = "demo 配置含未知键: " + path_of(path, it.key());
            return false;
        }
    }
    return true;
}

bool parse_patrol(const json& obj, PatrolParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {
        "waypoints", "walk_seconds", "rest_seconds", "speed", "yield_on_player_seen"};
    if (!check_known_keys(obj, "patrol", kKeys, err))
        return false;
    if (auto it = obj.find("waypoints"); it != obj.end()) {
        if (!get_waypoints(*it, "patrol.waypoints", out.waypoints, err))
            return false;
    }
    if (auto it = obj.find("walk_seconds"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "patrol.walk_seconds", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 patrol.walk_seconds 必须为正数";
            return false;
        }
        out.walk_seconds = v;
    }
    if (auto it = obj.find("rest_seconds"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "patrol.rest_seconds", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 patrol.rest_seconds 必须为正数";
            return false;
        }
        out.rest_seconds = v;
    }
    if (auto it = obj.find("speed"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "patrol.speed", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 patrol.speed 必须为正数";
            return false;
        }
        out.speed = static_cast<float>(v);
    }
    if (auto it = obj.find("yield_on_player_seen"); it != obj.end()) {
        if (!get_bool(*it, "patrol.yield_on_player_seen", out.yield_on_player_seen, err))
            return false;
    }
    return true;
}

bool parse_greet(const json& obj, GreetParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {"enabled", "text", "tone", "priority",
                                                              "max_distance"};
    if (!check_known_keys(obj, "greet", kKeys, err))
        return false;
    if (auto it = obj.find("enabled"); it != obj.end()) {
        if (!get_bool(*it, "greet.enabled", out.enabled, err))
            return false;
    }
    if (auto it = obj.find("text"); it != obj.end()) {
        if (!get_string(*it, "greet.text", out.text, err))
            return false;
    }
    if (auto it = obj.find("tone"); it != obj.end()) {
        if (!get_string(*it, "greet.tone", out.tone, err))
            return false;
    }
    if (auto it = obj.find("priority"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "greet.priority", v, err))
            return false;
        out.priority = static_cast<float>(v);
    }
    if (auto it = obj.find("max_distance"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "greet.max_distance", v, err) || v < 0.0) {
            if (err.empty())
                err = "demo 配置字段 greet.max_distance 不能为负";
            return false;
        }
        out.max_distance = static_cast<float>(v);
    }
    return true;
}

bool parse_startle(const json& obj, StartleParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {"stimulus_type", "emote", "priority"};
    if (!check_known_keys(obj, "startle", kKeys, err))
        return false;
    if (auto it = obj.find("stimulus_type"); it != obj.end()) {
        if (!get_string(*it, "startle.stimulus_type", out.stimulus_type, err))
            return false;
    }
    if (auto it = obj.find("emote"); it != obj.end()) {
        if (!get_string(*it, "startle.emote", out.emote, err))
            return false;
    }
    if (auto it = obj.find("priority"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "startle.priority", v, err))
            return false;
        out.priority = static_cast<float>(v);
    }
    return true;
}

bool parse_alert(const json& obj, AlertParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {"emote", "priority"};
    if (!check_known_keys(obj, "alert", kKeys, err))
        return false;
    if (auto it = obj.find("emote"); it != obj.end()) {
        if (!get_string(*it, "alert.emote", out.emote, err))
            return false;
    }
    if (auto it = obj.find("priority"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "alert.priority", v, err))
            return false;
        out.priority = static_cast<float>(v);
    }
    return true;
}

bool parse_body(const json& obj, BodyParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {"say_seconds", "emote_seconds",
                                                              "arrive_epsilon"};
    if (!check_known_keys(obj, "body", kKeys, err))
        return false;
    if (auto it = obj.find("say_seconds"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "body.say_seconds", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 body.say_seconds 必须为正数";
            return false;
        }
        out.say_seconds = v;
    }
    if (auto it = obj.find("emote_seconds"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "body.emote_seconds", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 body.emote_seconds 必须为正数";
            return false;
        }
        out.emote_seconds = v;
    }
    if (auto it = obj.find("arrive_epsilon"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "body.arrive_epsilon", v, err) || v < 0.0) {
            if (err.empty())
                err = "demo 配置字段 body.arrive_epsilon 不能为负";
            return false;
        }
        out.arrive_epsilon = v;
    }
    return true;
}

bool parse_player(const json& obj, PlayerParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {"speed", "clamp_margin"};
    if (!check_known_keys(obj, "player", kKeys, err))
        return false;
    if (auto it = obj.find("speed"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "player.speed", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 player.speed 必须为正数";
            return false;
        }
        out.speed = static_cast<float>(v);
    }
    if (auto it = obj.find("clamp_margin"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "player.clamp_margin", v, err) || v < 0.0) {
            if (err.empty())
                err = "demo 配置字段 player.clamp_margin 不能为负";
            return false;
        }
        out.clamp_margin = static_cast<float>(v);
    }
    return true;
}

bool parse_scene(const json& obj, SceneParams& out, std::string& err) {
    static constexpr std::array<std::string_view, 8> kKeys = {
        "npc_spawn",     "player_spawn", "scale",    "window_width",
        "window_height", "show_debug",   "show_hint"};
    if (!check_known_keys(obj, "scene", kKeys, err))
        return false;
    if (auto it = obj.find("npc_spawn"); it != obj.end()) {
        if (!get_vec3(*it, "scene.npc_spawn", out.npc_spawn, err))
            return false;
    }
    if (auto it = obj.find("player_spawn"); it != obj.end()) {
        if (!get_vec3(*it, "scene.player_spawn", out.player_spawn, err))
            return false;
    }
    if (auto it = obj.find("scale"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "scene.scale", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 scene.scale 必须为正数";
            return false;
        }
        out.scale = static_cast<float>(v);
    }
    if (auto it = obj.find("window_width"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "scene.window_width", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 scene.window_width 必须为正整数";
            return false;
        }
        out.window_width = static_cast<int>(v);
    }
    if (auto it = obj.find("window_height"); it != obj.end()) {
        double v = 0.0;
        if (!get_number(*it, "scene.window_height", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 scene.window_height 必须为正整数";
            return false;
        }
        out.window_height = static_cast<int>(v);
    }
    if (auto it = obj.find("show_debug"); it != obj.end()) {
        if (!get_bool(*it, "scene.show_debug", out.show_debug, err))
            return false;
    }
    if (auto it = obj.find("show_hint"); it != obj.end()) {
        if (!get_bool(*it, "scene.show_hint", out.show_hint, err))
            return false;
    }
    return true;
}

} // namespace

std::optional<std::string> parse_demo_config(const json& extra, DemoConfig& out) {
    DemoConfig cfg;
    std::string err;
    if (!extra.is_object())
        return std::string("NPC 配置 extra 段应为对象");
    const auto demo_it = extra.find("demo");
    if (demo_it == extra.end()) {
        out = cfg; // 无 demo 段：全默认值
        return std::nullopt;
    }
    if (!demo_it->is_object())
        return std::string("extra.demo 应为对象");
    const json& d = *demo_it;

    static constexpr std::array<std::string_view, 8> kTopKeys = {
        "patrol", "alarm_seconds", "greet", "startle", "alert", "body", "player", "scene"};
    if (!check_known_keys(d, "demo", kTopKeys, err))
        return err;

    if (auto it = d.find("patrol"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.patrol 应为对象");
        if (!parse_patrol(*it, cfg.patrol, err))
            return err;
    }
    if (auto it = d.find("alarm_seconds"); it != d.end()) {
        double v = 0.0;
        if (!get_number(*it, "demo.alarm_seconds", v, err) || v <= 0.0) {
            if (err.empty())
                err = "demo 配置字段 demo.alarm_seconds 必须为正数";
            return err;
        }
        cfg.alarm_seconds = v;
    }
    if (auto it = d.find("greet"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.greet 应为对象");
        if (!parse_greet(*it, cfg.greet, err))
            return err;
    }
    if (auto it = d.find("startle"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.startle 应为对象");
        if (!parse_startle(*it, cfg.startle, err))
            return err;
    }
    if (auto it = d.find("alert"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.alert 应为对象");
        if (!parse_alert(*it, cfg.alert, err))
            return err;
    }
    if (auto it = d.find("body"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.body 应为对象");
        if (!parse_body(*it, cfg.body, err))
            return err;
    }
    if (auto it = d.find("player"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.player 应为对象");
        if (!parse_player(*it, cfg.player, err))
            return err;
    }
    if (auto it = d.find("scene"); it != d.end()) {
        if (!it->is_object())
            return std::string("extra.demo.scene 应为对象");
        if (!parse_scene(*it, cfg.scene, err))
            return err;
    }

    out = std::move(cfg);
    return std::nullopt;
}

} // namespace npc_agent::adapter::godot_demo
