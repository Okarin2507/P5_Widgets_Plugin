#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <systemd/sd-bus.h>

namespace {

std::filesystem::path config_home() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg);
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config";
    }
    return "/tmp";
}

void write_profile_files(std::string_view profile) {
    std::error_code ec;
    auto noctalia_p = config_home() / "noctalia" / "p5-profile";
    std::filesystem::create_directories(noctalia_p.parent_path(), ec);
    std::ofstream(noctalia_p) << profile << "\n";

    auto dms_p = config_home() / "dms" / "p5notify-profile";
    std::filesystem::create_directories(dms_p.parent_path(), ec);
    std::ofstream(dms_p) << profile << "\n";
}

void print_help() {
    std::cout << "Usage: p5-rice-ctl <command> [args...]\n\n"
              << "Commands:\n"
              << "  profile [slug]                           Get or set active profile (persona, saber, elden-ring)\n"
              << "  notify <summary> [body] [urgency] [ms]   Show a Persona 5 notification banner\n"
              << "  dismiss | dismiss-all                    Clear all active Persona 5 banners\n"
              << "  music <on|off>                           Show or hide the Futaba desktop music widget\n"
              << "  toggle-music                             Toggle the Futaba desktop music widget\n"
              << "  play-pause                               Toggle play/pause on the active media player\n"
              << "  next                                     Skip to next track on the active media player\n"
              << "  prev                                     Return to previous track on the active media player\n"
              << "  status                                   Query daemon status and active profile\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    std::string_view cmd(argv[1]);
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_help();
        return 0;
    }

    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        std::cerr << "error: failed to open user D-Bus session\n";
        return 1;
    }

    int exit_code = 0;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    if (cmd == "profile") {
        if (argc == 2) {
            int r = sd_bus_call_method(
                bus,
                "dev.noctalia.PersonaRice",
                "/dev/noctalia/PersonaRice",
                "dev.noctalia.PersonaRice",
                "GetProfile",
                &err,
                &reply,
                ""
            );
            if (r >= 0 && reply) {
                const char* prof = nullptr;
                if (sd_bus_message_read(reply, "s", &prof) >= 0 && prof) {
                    std::cout << prof << "\n";
                }
            } else {
                auto p = config_home() / "noctalia" / "p5-profile";
                std::ifstream in(p);
                std::string s = "persona";
                if (in.is_open()) std::getline(in, s);
                std::cout << s << "\n";
            }
        } else {
            const char* slug = argv[2];
            write_profile_files(slug);
            sd_bus_call_method(
                bus,
                "dev.noctalia.PersonaRice",
                "/dev/noctalia/PersonaRice",
                "dev.noctalia.PersonaRice",
                "SetProfile",
                &err,
                &reply,
                "s",
                slug
            );
            std::cout << "Profile set to: " << slug << "\n";
        }
    } else if (cmd == "notify") {
        if (argc < 3) {
            std::cerr << "Usage: p5-rice-ctl notify <summary> [body] [urgency:0|1|2] [timeout_ms]\n";
            exit_code = 1;
        } else {
            const char* summary = argv[2];
            const char* body = (argc >= 4) ? argv[3] : "";
            uint32_t urgency = (argc >= 5) ? static_cast<uint32_t>(std::atoi(argv[4])) : 1u;
            int32_t timeout_ms = (argc >= 6) ? std::atoi(argv[5]) : 6000;
            int r = sd_bus_call_method(
                bus,
                "dev.noctalia.PersonaRice",
                "/dev/noctalia/PersonaRice",
                "dev.noctalia.PersonaRice",
                "Notify",
                &err,
                &reply,
                "sssui",
                summary,
                body,
                "p5-rice-ctl",
                urgency,
                timeout_ms
            );
            if (r < 0) {
                std::cerr << "error: p5-rice-daemon is not running (" << (err.message ? err.message : "no reply") << ")\n";
                exit_code = 1;
            }
        }
    } else if (cmd == "dismiss-all" || cmd == "dismiss") {
        sd_bus_call_method(
            bus,
            "dev.noctalia.PersonaRice",
            "/dev/noctalia/PersonaRice",
            "dev.noctalia.PersonaRice",
            "DismissAll",
            &err,
            &reply,
            ""
        );
    } else if (cmd == "music") {
        if (argc < 3) {
            std::cerr << "Usage: p5-rice-ctl music <on|off>\n";
            exit_code = 1;
        } else {
            std::string_view arg(argv[2]);
            int vis = (arg == "on" || arg == "true" || arg == "1" || arg == "show") ? 1 : 0;
            sd_bus_call_method(
                bus,
                "dev.noctalia.PersonaRice",
                "/dev/noctalia/PersonaRice",
                "dev.noctalia.PersonaRice",
                "SetMusicVisible",
                &err,
                &reply,
                "b",
                vis
            );
        }
    } else if (cmd == "toggle-music") {
        sd_bus_call_method(
            bus,
            "dev.noctalia.PersonaRice",
            "/dev/noctalia/PersonaRice",
            "dev.noctalia.PersonaRice",
            "ToggleMusic",
            &err,
            &reply,
            ""
        );
    } else if (cmd == "play-pause") {
        sd_bus_call_method(
            bus,
            "dev.noctalia.PersonaRice",
            "/dev/noctalia/PersonaRice",
            "dev.noctalia.PersonaRice",
            "PlayPause",
            &err,
            &reply,
            ""
        );
    } else if (cmd == "next") {
        sd_bus_call_method(
            bus,
            "dev.noctalia.PersonaRice",
            "/dev/noctalia/PersonaRice",
            "dev.noctalia.PersonaRice",
            "Next",
            &err,
            &reply,
            ""
        );
    } else if (cmd == "prev") {
        sd_bus_call_method(
            bus,
            "dev.noctalia.PersonaRice",
            "/dev/noctalia/PersonaRice",
            "dev.noctalia.PersonaRice",
            "Prev",
            &err,
            &reply,
            ""
        );
    } else if (cmd == "status") {
        int r = sd_bus_call_method(
            bus,
            "dev.noctalia.PersonaRice",
            "/dev/noctalia/PersonaRice",
            "dev.noctalia.PersonaRice",
            "GetProfile",
            &err,
            &reply,
            ""
        );
        if (r >= 0 && reply) {
            const char* prof = nullptr;
            sd_bus_message_read(reply, "s", &prof);
            std::cout << "{\"running\":true,\"profile\":\"" << (prof ? prof : "persona") << "\"}\n";
        } else {
            std::cout << "{\"running\":false}\n";
            exit_code = 1;
        }
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        print_help();
        exit_code = 1;
    }

    sd_bus_error_free(&err);
    if (reply) sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return exit_code;
}
