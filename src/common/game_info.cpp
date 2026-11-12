#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "audio/output.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "core/log.h"

namespace input {
void SetPadMap(const uint8_t* map);  // native/input.cpp: the controller's buttons remapped, 16 slots
}

namespace game {

uint32_t ConsoleLanguageValue();  // defined below, after BootLanguage

namespace {

// the console's language as the Xbox numbers it (XC_LANGUAGE): 1 English, 2 Japanese, 3 German, 4 French, 5 Spanish,
// 6 Italian. The SDK's ExQueryNonVolatileSetting asks for it through CodConsoleLanguage, so the title boots in it
std::atomic<uint32_t> g_console_language{1};  // unused now: kept so old code that calls SetConsoleLanguage still builds

// the console_language the launcher left last time; a value different from it is a new choice made in the launcher
long g_launcher_language = 0;

[[maybe_unused]] void SetConsoleLanguage(long v) {
    if (v >= 1 && v <= 6) g_console_language = uint32_t(v);
}

// the controller's button map the launcher chose: 16 numbers 0-15 separated by commas, slot i of the pad reads the
// controller's slot map[i] (see native/input.cpp); empty is the pad as it is. Kept in cod.toml as pad_map
std::string g_pad_map;

void ApplyPadMap() {
    uint8_t map[16];
    for (int i = 0; i < 16; i++) map[i] = uint8_t(i);
    if (!g_pad_map.empty()) {
        const char* p = g_pad_map.c_str();
        uint8_t parsed[16];
        int n = 0;
        while (n < 16 && *p) {
            char* end = nullptr;
            long v = std::strtol(p, &end, 10);
            if (end == p || v < 0 || v > 15) break;
            parsed[n++] = uint8_t(v);
            p = *end == ',' ? end + 1 : end;
        }
        if (n == 16) std::copy(std::begin(parsed), std::end(parsed), map);
        else LOGR("Controller: pad_map '%s' ignored, it needs 16 numbers from 0 to 15", g_pad_map.c_str());
    }
    input::SetPadMap(map);
}

// the options' language code (kLanguages) as the console's XC_LANGUAGE; Indonesian only translates the port's own
// text, the game itself stays in English
uint32_t ConsoleValueOf(const std::string& code) {
    if (code == "fra") return 4;
    if (code == "deu") return 3;
    if (code == "ita") return 6;
    if (code == "esp") return 5;
    return 1;
}

// the reverse, for a launcher that only knows the console's number
const char* CodeOf(long xbox) {
    switch (xbox) {
        case 3: return "deu";
        case 4: return "fra";
        case 5: return "esp";
        case 6: return "ita";
        default: return "int";
    }
}

// port 0 has the touch controls, the keyboard and mouse; the game sees no input while the panel is open
void Pad(int port, input::Pad& pad) {
    input::SeenPad(pad);
    if (port == 0) pad.connected = true;
    if (app::MenuOpen()) pad = input::Pad{.connected = pad.connected};
    else if (port == 0) input::Keyboard(pad);
}

// no vibration while the keyboard and mouse are in use
void Rumble(int port, uint16_t& left, uint16_t& right) {
    if (port != 0 || !input::KeyboardActive()) return;
    left = 0;
    right = 0;
}

// what the options list cannot hold
void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    Options& o = Opt();
    o.mouse_sensitivity = float(t["mouse_sensitivity"].value_or(double(o.mouse_sensitivity)));
    // the touch screen followed the mouse's before it had its own: a player's choice carries over
    o.touch_sensitivity = float(t["touch_sensitivity"].value_or(double(o.mouse_sensitivity)));
    o.left_sensitivity = float(t["left_stick_sensitivity"].value_or(double(o.left_sensitivity)));
    o.right_sensitivity = float(t["right_stick_sensitivity"].value_or(double(o.right_sensitivity)));
    o.game_path = t["game_path"].value_or(o.game_path);
    // the LANGUAGE row of the options menu only changes o.language, so that is the one that counts; the old
    // console_language key is read only by a file that has no language
    g_launcher_language = long(t["launcher_language"].value_or(int64_t(0)));
    g_pad_map = t["pad_map"].value_or(std::string());
    std::string language = t["language"].value_or(std::string(CodeOf(t["console_language"].value_or(int64_t(1)))));
    bool known = std::find(std::begin(kLanguages), std::end(kLanguages), language) != std::end(kLanguages);
    if (known) o.language = language;
    if (const toml::table* keys = t["keys"].as_table())
        for (auto&& [id, value] : *keys)
            if (auto v = value.value<std::string>()) o.keys[std::string(id.str())] = *v;
}


// the Android launcher (LauncherActivity) leaves launcher.txt beside the settings: game_path, fps_counter and
// show_resolution, one key=value a line; taken once, into the options, then deleted (a copy stays as launcher_last.txt for the launcher to show again)
void ApplyLauncher() {
    namespace fs = std::filesystem;
    const fs::path file = app::ExeDir() / "launcher.txt";
    std::ifstream in(file);
    if (!in) return;
    Options& o = Opt();
    for (std::string line; std::getline(in, line);) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        if (key == "game_path") o.game_path = value;
        else if (key == "fps_counter") o.fps_counter = value == "1";
        else if (key == "show_resolution") o.show_resolution = value == "1";
        else if (key == "pad_map") g_pad_map = value;
        else if (key == "console_language") {
            // the launcher repeats its last choice on every start: only a different one replaces what the options
            // menu chose, and cod.toml remembers which one it was
            long v = std::strtol(value.c_str(), nullptr, 10);
            if (v >= 1 && v <= 6 && v != g_launcher_language) {
                o.language = CodeOf(v);
                g_launcher_language = v;
                LOGR("Language: the launcher chose console language %ld (%s)", v, o.language.c_str());
            }
        }
    }
    in.close();
    std::error_code ec;
    // el launcher vuelve a mostrar lo ultimo que se eligio: launcher.txt se borra tras leerlo, asi que se deja una copia
    fs::copy_file(file, app::ExeDir() / "launcher_last.txt", fs::copy_options::overwrite_existing, ec);
    fs::remove(file, ec);
    app::Save();  // so the choice outlasts this run
}

// after the settings load, before boot
void Init() {
    ApplyLauncher();
    ApplyPadMap();
    BootLanguage();  // latched before the options can change the setting
    LOGR("Language: boots in '%s', XC_LANGUAGE %u", BootLanguage().c_str(), unsigned(ConsoleLanguageValue()));
    input::LoadBindings();
    audio::SetOutputMode(Opt().audio_output);
    // diagnostico: el log solo se vacia a disco con un aviso o error, y si el app se cierra por la fuerza se pierden
    // las ultimas lineas; este hilo lo vacia cada 3 segundos. Quitar cuando ya no haga falta.
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // cada vez que vacia, el log queda bloqueado un instante: menos veces
            logx::Flush();
        }
    }).detach();
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    const Options& o = Opt();
    t.insert("mouse_sensitivity", double(o.mouse_sensitivity));
    t.insert("touch_sensitivity", double(o.touch_sensitivity));
    t.insert("left_stick_sensitivity", double(o.left_sensitivity));
    t.insert("right_stick_sensitivity", double(o.right_sensitivity));
    t.insert("language", o.language);
    t.insert("console_language", int64_t(ConsoleValueOf(o.language)));
    t.insert("launcher_language", int64_t(g_launcher_language));
    if (!g_pad_map.empty()) t.insert("pad_map", g_pad_map);
    if (!o.game_path.empty()) t.insert("game_path", o.game_path);
    toml::table keys;
    for (const auto& [id, value] : o.keys) keys.insert(id, value);
    t.insert("keys", keys);
}

}  // namespace

Options& Opt() {
    static Options o;
    return o;
}

const std::string& BootLanguage() {
    static const std::string language = Opt().language;
    return language;
}

// what ExQueryNonVolatileSetting reports: latched at boot (BootLanguage), the game reads it once at start, so a change
// in the menu shows as (RESTART) and takes effect on the next launch
uint32_t ConsoleLanguageValue() { return ConsoleValueOf(BootLanguage()); }

}  // namespace game

const app::GameInfo app::kGame = {
    .title = L"Castlevania: Curse of Darkness",
    .short_title = L"Curse of Darkness",
    .name = L"Curse of Darkness",
    .log = "cod",
    .settings = "cod.toml",
    .splash = nullptr,
    .missing = L"Put the game in the Castlevania folder on the phone's storage: its disc image (.iso), or the European "
               L"default.xbe with the rest of the game's files beside it.",
    .images = {{"default.xbe", "Curse of Darkness"}},
    .options = {{"skip_intros", &game::Opt().skip_intros, nullptr},
                {"fps_counter", &game::Opt().fps_counter, nullptr},
                {"show_resolution", &game::Opt().show_resolution, nullptr},
                {"bloom", &game::Opt().bloom, nullptr},
                {"fog", &game::Opt().fog, nullptr},
                {"anti_aliasing", nullptr, &game::Opt().anti_aliasing},
                {"hdr", &game::Opt().hdr, nullptr},
                {"fmv_scale", nullptr, &game::Opt().fmv_scale},
                {"screen_filter", nullptr, &game::Opt().screen_filter},
                {"left_stick_deadzone", nullptr, &game::Opt().left_deadzone},
                {"right_stick_deadzone", nullptr, &game::Opt().right_deadzone},
                {"dynamic_resolution", &game::Opt().dynamic_resolution, nullptr},
                {"dynamic_target_fps", nullptr, &game::Opt().dynamic_target_fps},
                {"dynamic_min_percent", nullptr, &game::Opt().dynamic_min_percent},
                {"dynamic_max_percent", nullptr, &game::Opt().dynamic_max_percent},
                {"dynamic_hold", nullptr, &game::Opt().dynamic_hold},
                {"audio_output", nullptr, &game::Opt().audio_output}},
    .load = game::Load,
    .save = game::Save,
    .init = game::Init,
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = app::DrawUi,
    .pad = game::Pad,
    .rumble = game::Rumble,
};

// read by the SDK's ExQueryNonVolatileSetting (XC_LANGUAGE): the console's language, 1 to 6
extern "C" uint32_t CodConsoleLanguage() {
    static std::atomic<int> asked{0};
    uint32_t v = game::ConsoleLanguageValue();
    if (asked.fetch_add(1) < 3) LOGR("Language: the game asked for XC_LANGUAGE, answered %u", unsigned(v));
    return v;
}
