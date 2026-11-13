#include "app.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "layout.h"
#include "prefs.h"
#include "wp/mii.h"
#include "wp/padmap.h"

namespace {

namespace fs = std::filesystem;

int pref_int(const std::string& key, int fallback) {
    const std::string text = pref(key.c_str());
    if (text.empty()) return fallback;
    char* end = nullptr;
    long parsed = std::strtol(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : static_cast<int>(parsed);
}

std::string indexed_key(const char* prefix, int index, const char* suffix = nullptr) {
    std::string key = prefix;
    key += std::to_string(index);
    if (suffix) {
        key += ".";
        key += suffix;
    }
    return key;
}

std::string profile_name(int index) {
    std::string name = pref(indexed_key("profiles.name.", index).c_str());
    return name.empty() ? "Player " + std::to_string(index + 1) : name;
}

int profile_count() {
    return std::clamp(pref_int("profiles.count", 1), 1, 8);
}

void ensure_profiles() {
    int count = pref_int("profiles.count", 0);
    if (count < 1) {
        count = 1;
        set_pref("profiles.count", "1");
    }
    count = std::clamp(count, 1, 8);
    if (pref("profiles.name.0").empty()) set_pref("profiles.name.0", "Player 1");
    for (int i = 1; i < count; ++i) {
        const std::string key = indexed_key("profiles.name.", i);
        if (pref(key.c_str()).empty()) set_pref(key.c_str(), "Player " + std::to_string(i + 1));
    }
    if (pref("profiles.active").empty()) set_pref("profiles.active", "0");
    const std::string stored_active = pref("profiles.active");
    int active = std::clamp(pref_int("profiles.active", 0), 0, count - 1);
    // Do not rewrite launcher.ini every frame; persist only if clamping corrected
    // a stale or invalid active-profile index.
    if (stored_active != std::to_string(active)) set_pref("profiles.active", std::to_string(active));
    if (pref("players.players").empty()) set_pref("players.players", "4");
}

std::string clean_name(const char* value) {
    std::string name = value ? value : "";
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    for (char& ch : name) {
        if (ch == '=' || ch == '\n' || ch == '\r') ch = ' ';
    }
    if (name.size() > 36) name.resize(36);
    return name;
}

std::string session_time(const std::string& stored) {
    if (stored.empty()) return "Not played yet";
    char* end = nullptr;
    const long long raw = std::strtoll(stored.c_str(), &end, 10);
    if (end == stored.c_str() || raw <= 0) return "Not played yet";
    const std::time_t stamp = static_cast<std::time_t>(raw);
    const std::tm* local = std::localtime(&stamp);
    if (!local) return "Previously launched";
    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), "%b %d, %Y %H:%M", local) == 0) return "Previously launched";
    return buffer;
}

std::string player_profile_key(int player_slot) {
    return "players.profile." + std::to_string(player_slot);
}

std::string pad_key(const char* name, int ordinal) {
    return wp::padmap::device_key(name ? name : "?", ordinal);
}

std::string calibration_value(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
    return buffer;
}

} // namespace

void App::graphics_presets(float width) {
    const ui::Palette& p = ui::palette();
    const std::string current = pref("graphics.preset").empty() ? "Manual / existing settings" : pref("graphics.preset");
    ui::text("Graphics Presets & Automatic Optimization", Font::Semibold, ui::size::kLead, p.text);
    ui::gap(6);
    ui::text(("Current preset: " + current).c_str(), Font::Regular, ui::size::kDetail, p.secondary, width);
    ui::gap(10);

    auto apply = [&](const std::string& preset) {
        int scale = 1;
        if (preset == "balanced") {
            bool slowed = false;
            scale = recommended_scale(slowed);
        } else if (preset == "quality") {
            SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display ? display : SDL_GetPrimaryDisplay());
            const int height = mode ? mode->h : 1080;
            scale = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) / 528.0)), 1, 6);
        }
        applying_graphics_preset_ = true;
        store("video.scale", std::to_string(scale));
        store("video.antialiasing", preset == "performance" ? "0" : "1");
        store("video.enhancements", preset == "performance" ? "0" : "1");
        store("video.filtering", preset == "performance" ? "nearest" : "linear");
        store("video.vsync", "1");
        applying_graphics_preset_ = false;
        set_pref("graphics.preset", preset);
    };

    Card card(width);
    row(width, "Performance", "Native internal resolution and lighter post-processing for weaker GPUs.", 312.0f, 36.0f, [&] {
        if (ui::button("Apply Performance", Kind::Secondary, 140.0f)) apply("performance");
    });
    inset_separator(width);
    row(width, "Balanced", "Recommended scale based on display size, GPU classification and prior frame-rate logs.", 312.0f, 36.0f, [&] {
        if (ui::button("Apply Balanced", Kind::Primary, 140.0f)) apply("balanced");
    });
    inset_separator(width);
    row(width, "Quality", "Raises internal resolution for capable systems; can reduce frame rate on integrated GPUs.", 312.0f, 36.0f, [&] {
        if (ui::button("Apply Quality", Kind::Secondary, 140.0f)) apply("quality");
    });
    inset_separator(width);
    row(width, "Automatic optimization", "Recalculates the recommended internal scale from local system/session data.", 180.0f, 36.0f, [&] {
        if (ui::button("Optimize", Kind::Secondary, 120.0f)) apply("balanced");
    });
    card.end();
}

void App::calibration_section(float width) {
    const ui::Palette& p = ui::palette();
    ui::text("Controller Calibration Lab", Font::Semibold, ui::size::kLead, p.text);
    ui::gap(6);
    ui::text("Live stick/trigger readings, motion sensor availability, rumble testing and persistent input calibration.", Font::Regular, ui::size::kDetail, p.secondary, width);
    ui::gap(10);

    int count = 0;
    SDL_JoystickID* ids = gamepads(count);
    static int selected_device = 0;
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(ids[i]);
        int ordinal = 0;
        for (int before = 0; before < i; ++before) {
            const char* earlier = SDL_GetGamepadNameForID(ids[before]);
            if (name && earlier && std::strcmp(name, earlier) == 0) ++ordinal;
        }
        names.push_back(std::string(name ? name : "Unknown controller") + " (" + std::to_string(ordinal + 1) + ")");
    }
    if (count > 0) selected_device = std::clamp(selected_device, 0, count - 1);

    Card card(width);
    if (count == 0) {
        row(width, "No gamepad detected", "Connect a controller for live analog and sensor readings. Keyboard and Wii Remote setup are available in Controls.", 0.0f, 0.0f, [] {});
    } else {
        row(width, "Controller under test", "", 250.0f, 36.0f, [&] {
            ui::dropdown("calibration-device", names, selected_device, 250.0f);
        });
        SDL_Gamepad* pad = nullptr;
        auto found = tester_pads_.find(ids[selected_device]);
        if (found != tester_pads_.end()) pad = found->second;
        if (!pad) {
            pad = SDL_OpenGamepad(ids[selected_device]);
            if (pad) tester_pads_[ids[selected_device]] = pad;
        }
        if (pad) {
            auto axis = [&](SDL_GamepadAxis which) { return std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f, -1.0f, 1.0f); };
            auto show_axis = [&](const char* label, float value) {
                char buffer[32]{};
                std::snprintf(buffer, sizeof(buffer), "%+.2f", static_cast<double>(value));
                row(width, label, buffer, 220.0f, 18.0f, [&] {
                    ImGui::ProgressBar((value + 1.0f) * 0.5f, ImVec2(px(200), px(14)), buffer);
                });
            };
            auto show_trigger = [&](const char* label, SDL_GamepadAxis which) {
                const float value = std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f, 0.0f, 1.0f);
                char buffer[32]{};
                std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(value * 100.0f));
                row(width, label, buffer, 220.0f, 18.0f, [&] {
                    ImGui::ProgressBar(value, ImVec2(px(200), px(14)), buffer);
                });
            };
            inset_separator(width);
            show_axis("Left stick — horizontal", axis(SDL_GAMEPAD_AXIS_LEFTX));
            show_axis("Left stick — vertical", axis(SDL_GAMEPAD_AXIS_LEFTY));
            show_axis("Right stick — horizontal", axis(SDL_GAMEPAD_AXIS_RIGHTX));
            show_axis("Right stick — vertical", axis(SDL_GAMEPAD_AXIS_RIGHTY));
            show_trigger("Left trigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            show_trigger("Right trigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            inset_separator(width);
            const bool gyro = SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO);
            const bool accel = SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL);
            bool gyro_read = false;
            bool accel_read = false;
            float gyro_data[3] = {};
            float accel_data[3] = {};
            if (gyro) {
                SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
                gyro_read = SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro_data, 3);
            }
            if (accel) {
                SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true);
                accel_read = SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, accel_data, 3);
            }
            char sensor_buffer[160]{};
            if (gyro_read && accel_read) {
                std::snprintf(sensor_buffer, sizeof(sensor_buffer), "Gyro %.2f / %.2f / %.2f · Accel %.2f / %.2f / %.2f",
                              static_cast<double>(gyro_data[0]), static_cast<double>(gyro_data[1]), static_cast<double>(gyro_data[2]),
                              static_cast<double>(accel_data[0]), static_cast<double>(accel_data[1]), static_cast<double>(accel_data[2]));
            } else if (gyro || accel) {
                std::snprintf(sensor_buffer, sizeof(sensor_buffer), "%s%s%s",
                              gyro ? (gyro_read ? "Gyroscope data available" : "Gyroscope supported, waiting for data") : "",
                              gyro && accel ? " · " : "",
                              accel ? (accel_read ? "Accelerometer data available" : "Accelerometer supported, waiting for data") : "");
            } else {
                std::snprintf(sensor_buffer, sizeof(sensor_buffer), "%s",
                              "No motion sensors exposed by this gamepad; ordinary controls still work.");
            }
            row(width, "Motion sensor readings", sensor_buffer, 0.0f, 0.0f, [] {});
            row(width, "Rumble test", "Short, moderate vibration pulse; only available if the controller supports rumble.", 120.0f, 36.0f, [&] {
                if (ui::button("Test rumble", Kind::Secondary, 120.0f)) SDL_RumbleGamepad(pad, 0x8000, 0x8000, 400);
            });
        } else {
            row(width, "Could not open controller", SDL_GetError(), 0.0f, 0.0f, [] {});
        }
    }
    SDL_free(ids);

    ui::gap(10);
    float deadzone = std::clamp(std::strtof(value("input.stick_deadzone").c_str(), nullptr), 0.01f, 0.50f);
    float threshold = std::clamp(std::strtof(value("input.button_threshold").c_str(), nullptr), 0.10f, 0.95f);
    row(width, "Pointer stick dead zone", "Lower values react to smaller movement; higher values reduce drift. Applies to supported gamepad right-stick pointer input.", 270.0f, 28.0f, [&] {
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::SliderFloat("##stick-deadzone", &deadzone, 0.01f, 0.50f, "%.2f")) store("input.stick_deadzone", calibration_value(deadzone));
    });
    row(width, "Digital activation threshold", "Sets how far a stick or trigger must move before it activates a digital Wii Remote action.", 270.0f, 28.0f, [&] {
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::SliderFloat("##button-threshold", &threshold, 0.10f, 0.95f, "%.2f")) store("input.button_threshold", calibration_value(threshold));
    });
    row(width, "Reset calibration", "Restore the default dead zone and activation threshold.", 120.0f, 36.0f, [&] {
        if (ui::button("Reset defaults", Kind::Secondary, 120.0f)) {
            store("input.stick_deadzone", "0.15");
            store("input.button_threshold", "0.50");
        }
    });
    card.end();
    ui::gap(22);
}

void App::profiles_section(float width) {
    ensure_profiles();
    const ui::Palette& p = ui::palette();
    int count = profile_count();
    int active = std::clamp(pref_int("profiles.active", 0), 0, count - 1);
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) names.push_back(profile_name(i));

    ui::text("Player Profiles & Statistics", Font::Semibold, ui::size::kLead, p.text);
    ui::gap(6);
    ui::text("Profiles keep preferred Miis, players assignments and local launcher statistics separate.", Font::Regular, ui::size::kDetail, p.secondary, width);
    ui::gap(10);
    Card card(width);
    row(width, "Active profile", "Used for new players assignments and launch statistics.", 250.0f, 36.0f, [&] {
        int selected = active;
        if (ui::dropdown("active-profile", names, selected, 250.0f)) {
            set_pref("profiles.active", std::to_string(selected));
            active = selected;
        }
    });

    static char new_name[64] = {};
    static char rename_value[64] = {};
    static int rename_profile = -1;
    if (rename_profile != active) {
        std::snprintf(rename_value, sizeof(rename_value), "%s", profile_name(active).c_str());
        rename_profile = active;
    }
    inset_separator(width);
    row(width, "Create profile", "Up to eight local profiles.", 340.0f, 34.0f, [&] {
        ImGui::SetNextItemWidth(px(210));
        ImGui::InputText("##new-profile-name", new_name, sizeof(new_name));
        ImGui::SameLine(0, px(8));
        if (ui::button("Add", Kind::Secondary, 92.0f, count < 8)) {
            std::string name = clean_name(new_name);
            if (!name.empty() && count < 8) {
                set_pref(indexed_key("profiles.name.", count).c_str(), name);
                set_pref(indexed_key("profiles.sessions.", count).c_str(), "0");
                set_pref(indexed_key("profiles.mii.", count).c_str(), "");
                set_pref("profiles.count", std::to_string(count + 1));
                std::snprintf(new_name, sizeof(new_name), "%s", "");
            }
        }
    });
    inset_separator(width);
    row(width, "Rename active profile", "Names are stored locally in launcher settings.", 340.0f, 34.0f, [&] {
        ImGui::SetNextItemWidth(px(210));
        ImGui::InputText("##rename-profile", rename_value, sizeof(rename_value));
        ImGui::SameLine(0, px(8));
        if (ui::button("Rename", Kind::Secondary, 92.0f)) {
            std::string name = clean_name(rename_value);
            if (!name.empty()) {
                set_pref(indexed_key("profiles.name.", active).c_str(), name);
                names[static_cast<size_t>(active)] = name;
            }
        }
    });
    inset_separator(width);
    row(width, "Delete active profile", count > 1 ? "Remove this profile and compact the list." : "Keep at least one profile.", 112.0f, 36.0f, [&] {
        if (ui::button("Delete", Kind::Secondary, 112.0f, count > 1)) {
            const char* fields[] = {"name", "sessions", "last_played", "mii", "controller"};
            for (const char* field : fields) {
                for (int i = active; i < count - 1; ++i) {
                    const std::string dst = "profiles." + std::string(field) + "." + std::to_string(i);
                    const std::string src = "profiles." + std::string(field) + "." + std::to_string(i + 1);
                    set_pref(dst.c_str(), pref(src.c_str()));
                }
                const std::string last = "profiles." + std::string(field) + "." + std::to_string(count - 1);
                set_pref(last.c_str(), "");
            }
            set_pref("profiles.count", std::to_string(count - 1));
            active = std::min(active, count - 2);
            set_pref("profiles.active", std::to_string(active));
            names.clear();
            for (int i = 0; i < count - 1; ++i) names.push_back(profile_name(i));
            rename_profile = -1;
        }
    });

    const std::string sessions_key = indexed_key("profiles.sessions.", active);
    const std::string played_key = indexed_key("profiles.last_played.", active);
    inset_separator(width);
    row(width, "Sessions launched", pref(sessions_key.c_str()).empty() ? "0" : pref(sessions_key.c_str()).c_str(), 0.0f, 0.0f, [] {});
    const std::string last = session_time(pref(played_key.c_str()));
    row(width, "Last launch", last.c_str(), 0.0f, 0.0f, [] {});


    fs::path mii_file = project_.game_folder() / "nand" / "shared2" / "menu" / "FaceLib" / "RFL_DB.dat";
    std::error_code error;
    if (fs::is_regular_file(mii_file, error)) {
        wp::mii::Database database;
        if (database.load(mii_file) && database.valid()) {
            std::vector<std::string> choices = {"No preferred Mii"};
            std::vector<int> slots = {-1};
            for (size_t slot : database.used()) {
                choices.push_back(wp::mii::name(database.at(slot)) + "  (slot " + std::to_string(slot + 1) + ")");
                slots.push_back(static_cast<int>(slot));
            }
            const std::string mii_key = indexed_key("profiles.mii.", active);
            const int stored = pref_int(mii_key, -1);
            int selected = 0;
            for (size_t i = 0; i < slots.size(); ++i) if (slots[i] == stored) selected = static_cast<int>(i);
            inset_separator(width);
            row(width, "Preferred Mii", "A profile preference for players planning; it does not override a Mii already selected inside the game.", 250.0f, 36.0f, [&] {
                if (ui::dropdown("preferred-mii", choices, selected, 250.0f)) {
                    set_pref(mii_key.c_str(), selected == 0 ? "" : std::to_string(slots[static_cast<size_t>(selected)]));
                }
            });
        }
    } else {
        inset_separator(width);
        row(width, "Preferred Mii", "Import or create Miis on the Miis page first to link one to this profile.", 0.0f, 0.0f, [] {});
    }
    card.end();
    ui::gap(18);
}

void App::players_page(float width) {
    ensure_profiles();
    const ui::Palette& p = ui::palette();
    heading("Players & Controllers", "Choose player profiles and assign real input devices before launching Wii Sports.", width);
    profiles_section(width);

    int count = profile_count();
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) names.push_back(profile_name(i));
    const std::vector<std::string> player_counts = {"1 player", "2 players", "3 players", "4 players"};
    int players = std::clamp(pref_int("players.players", 4), 1, 4);

    ui::text("Player lineup", Font::Semibold, ui::size::kLead, p.text);
    ui::gap(8);
    Card lineup(width);
    row(width, "Players in this players", "Set the local player count for this setup preset.", 220.0f, 36.0f, [&] {
        int selected = players - 1;
        if (ui::dropdown("players-count", player_counts, selected, 220.0f)) {
            players = selected + 1;
            set_pref("players.players", std::to_string(players));
        }
    });
    for (int slot = 1; slot <= players; ++slot) {
        inset_separator(width);
        const std::string key = player_profile_key(slot);
        int default_profile = slot == 1 ? std::clamp(pref_int("profiles.active", 0), 0, count - 1) : std::min(slot - 1, count - 1);
        if (pref(key.c_str()).empty()) set_pref(key.c_str(), std::to_string(default_profile));
        int selected = std::clamp(pref_int(key, default_profile), 0, count - 1);
        ImGui::PushID(slot);
        const std::string label = "Player " + std::to_string(slot) + " profile";
        row(width, label.c_str(), names[static_cast<size_t>(selected)].c_str(), 250.0f, 36.0f, [&] {
            if (ui::dropdown("slot-profile", names, selected, 250.0f)) set_pref(key.c_str(), std::to_string(selected));
        });
        ImGui::PopID();
    }
    lineup.end();

    ui::gap(18);
    ui::text("Controller assignments", Font::Semibold, ui::size::kLead, p.text);
    ui::gap(6);
    ui::text("Assignments here write the same device-to-player mapping used by the game engine.", Font::Regular, ui::size::kDetail, p.secondary, width);
    ui::gap(8);
    Card devices(width);
    bool keyboard_p1 = value("input.player1_keyboard_only") == "1";
    row(width, "Keyboard and mouse are Player 1", "Keeps gamepads from taking Player 1 when a keyboard player is preferred.", 42.0f, 24.0f, [&] {
        if (ui::toggle("keyboard-player-one", keyboard_p1)) store("input.player1_keyboard_only", keyboard_p1 ? "0" : "1");
    });

    int pad_count = 0;
    SDL_JoystickID* ids = gamepads(pad_count);
    std::string ignored = value("input.ignored_gamepads");
    std::string assignments = value("input.gamepad_players");
    const std::vector<std::string> choices = {"Automatic", "Player 1", "Player 2", "Player 3", "Player 4"};
    if (pad_count == 0) {
        inset_separator(width);
        row(width, "No SDL gamepads detected", "Bluetooth Wii Remotes can be paired on the Controls page; they are not necessarily listed as gamepads here.", 0.0f, 0.0f, [] {});
    }
    std::vector<std::string> keys;
    for (int i = 0; i < pad_count && i < 8; ++i) {
        const char* name = SDL_GetGamepadNameForID(ids[i]);
        int ordinal = 0;
        for (int j = 0; j < i; ++j) {
            const char* earlier = SDL_GetGamepadNameForID(ids[j]);
            if (name && earlier && std::strcmp(name, earlier) == 0) ++ordinal;
        }
        keys.push_back(pad_key(name, ordinal));
    }
    for (int i = 0; i < pad_count && i < 8; ++i) {
        inset_separator(width);
        const std::string key = keys[static_cast<size_t>(i)];
        const char* name = SDL_GetGamepadNameForID(ids[i]);
        const bool enabled = !wp::padmap::listed(ignored, key);
        const int assigned = wp::padmap::assigned(assignments, key);
        ImGui::PushID(i + 100);
        row(width, name ? name : "Unknown controller", enabled ? "Connected" : "Ignored by Wii Sports Recomp", 250.0f, 34.0f, [&] {
            int selection = assigned;
            if (ui::dropdown("device-player", choices, selection, 160.0f) && enabled) {
                store("input.gamepad_players", wp::padmap::assign(value("input.gamepad_players"), key, selection));
                if (wp::padmap::listed(value("input.ignored_gamepads"), key)) store("input.ignored_gamepads", wp::padmap::toggled(value("input.ignored_gamepads"), key));
            }
            ImGui::SameLine(0.0f, px(8));
            bool use = enabled;
            if (ui::toggle("use", use)) store("input.ignored_gamepads", wp::padmap::toggled(value("input.ignored_gamepads"), key));
        });
        ImGui::PopID();
    }
    SDL_free(ids);
    devices.end();

    ui::gap(18);
    int preset_count = std::clamp(pref_int("players.presets.count", 0), 0, 12);
    std::vector<std::string> preset_names;
    for (int i = 0; i < preset_count; ++i) {
        const std::string name = pref(indexed_key("players.preset.name.", i).c_str());
        preset_names.push_back(name.empty() ? "Players " + std::to_string(i + 1) : name);
    }
    static int selected_preset = 0;
    if (!preset_names.empty()) selected_preset = std::clamp(selected_preset, 0, static_cast<int>(preset_names.size()) - 1);
    else selected_preset = 0;
    static char preset_name[64] = "Game night";

    ui::text("Saved players presets", Font::Semibold, ui::size::kLead, p.text);
    ui::gap(8);
    Card presets(width);
    row(width, "Load a preset", preset_names.empty() ? "Save a lineup below to create your first preset." : "Loads player count, profile order and controller routing.", 280.0f, 36.0f, [&] {
        if (!preset_names.empty()) {
            ImGui::SetNextItemWidth(px(170));
            ui::dropdown("saved-players", preset_names, selected_preset, 170.0f);
            ImGui::SameLine(0.0f, px(8));
            if (ui::button("Load", Kind::Secondary, 84.0f)) {
                const int i = selected_preset;
                set_pref("players.players", pref(indexed_key("players.preset.players.", i).c_str()));
                for (int slot = 1; slot <= 4; ++slot) {
                    const std::string dst = player_profile_key(slot);
                    const std::string src = "players.preset.profile." + std::to_string(i) + "." + std::to_string(slot);
                    set_pref(dst.c_str(), pref(src.c_str()));
                }
                const std::string pads = pref(indexed_key("players.preset.gamepad_players.", i).c_str());
                const std::string ignored_pads = pref(indexed_key("players.preset.ignored_gamepads.", i).c_str());
                const std::string keyboard = pref(indexed_key("players.preset.keyboard_p1.", i).c_str());
                store("input.gamepad_players", pads);
                store("input.ignored_gamepads", ignored_pads);
                store("input.player1_keyboard_only", keyboard);
            }
        } else {
            ImGui::TextDisabled("No presets saved");
        }
    });
    inset_separator(width);
    row(width, "Save current setup", "Keep this lineup and the currently connected controller routing.", 350.0f, 34.0f, [&] {
        ImGui::SetNextItemWidth(px(220));
        ImGui::InputText("##players-preset-name", preset_name, sizeof(preset_name));
        ImGui::SameLine(0.0f, px(8));
        if (ui::button("Save preset", Kind::Primary, 112.0f, preset_count < 12)) {
            std::string name = clean_name(preset_name);
            if (!name.empty() && preset_count < 12) {
                const int i = preset_count;
                set_pref(indexed_key("players.preset.name.", i).c_str(), name);
                set_pref(indexed_key("players.preset.players.", i).c_str(), std::to_string(players));
                for (int slot = 1; slot <= 4; ++slot) {
                    const std::string key = "players.preset.profile." + std::to_string(i) + "." + std::to_string(slot);
                    const std::string value = pref(player_profile_key(slot).c_str());
                    set_pref(key.c_str(), value);
                }
                set_pref(indexed_key("players.preset.gamepad_players.", i).c_str(), value("input.gamepad_players"));
                set_pref(indexed_key("players.preset.ignored_gamepads.", i).c_str(), value("input.ignored_gamepads"));
                set_pref(indexed_key("players.preset.keyboard_p1.", i).c_str(), value("input.player1_keyboard_only"));
                set_pref("players.presets.count", std::to_string(i + 1));
                selected_preset = i;
                std::snprintf(preset_name, sizeof(preset_name), "%s", "");
            }
        }
    });
    presets.end();

    ui::gap(18);
    const std::string ready_summary = std::to_string(pad_count) + " SDL gamepad(s) currently detected. Pair real Wii Remotes separately and confirm player controls before starting.";
    row(width, "Ready check", ready_summary.c_str(), 0.0f, 0.0f, [] {});
    if (project_.built() && project_.extracted()) {
        if (ui::button("Launch Wii Sports", Kind::Primary, 180.0f)) play();
    } else {
        ui::text("Install or build Wii Sports on the Home page before launching.", Font::Regular, ui::size::kDetail, p.secondary, width);
    }
    ui::gap(8);
    ui::text("The wizard saves profiles and input routing. Choose Tennis, Baseball, Bowling, Golf or Boxing from inside Wii Sports; there is not yet a reliable in-game mode-launch hook.", Font::Regular, ui::size::kDetail, p.secondary, width);
}
