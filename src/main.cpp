/*
 * Rhythm Practice Bar  (luchx.barra-ritmo-final)
 * Barra horizontal inferior estilo juego de ritmo para practicar el timing de clics
 * a partir de una macro de Mega Hack / .gdr2.
 *
 * - Las notas viajan de izquierda a derecha hacia una linea receptora.
 * - Al presionar el salto (P1) el receptor destella en verde; al soltar, en celeste.
 * - Cada clic/soltar se juzga contra la macro: Perfecto / Muy Bien / Bien / Temprano / Tarde,
 *   y una nota que se pasa sin clic cuenta como Fallo.
 * - Ajustes "bar-width", "bar-height" y "bg-opacity" del mod.json (o desde el boton
 *   "Ritmo" del menu de pausa, en tiempo real).
 * - Biblioteca de macros guardada en <save dir>/macro-library.txt (una ruta por linea).
 *
 * Formatos que lee parseMacro():
 *   - GDR2 binario (.gdr2, formato de maxnut/GDReplayFormat): se lee directo de los bytes
 *   - JSON de Mega Hack (.mhr.json, "Advanced Options > Export JSON")
 *   - JSON de otros bots que usan la extension .gdr2 (eventos con frame/down/button)
 *   - Texto plano: una linea por evento, "frame/tiempo + estado" (ver parseTextEvents)
 *
 * El formato se detecta por el contenido, no por la extension del archivo.
 * Cabeceras, metadatos y lineas que no sean eventos de clic se ignoran.
 */
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

// === PARSER BEGIN (solo usa la libreria estandar) ===
namespace rpb {

struct ParserSettings {
    double fps = 0.0;            // 0 = automatico (fps del archivo, o 240 si no lo trae)
    double tapThreshold = 0.12;  // segundos: soltar antes de esto = clic unico
};

struct Hold {
    double start = 0.0;
    double end = 0.0;
};

struct Macro {
    std::vector<double> presses;   // notas verdes: inicio de cada clic (segundos)
    std::vector<double> releases;  // notas celestes: soltar de un hold (segundos)
    std::vector<Hold> holds;       // barras translucidas
};

struct ParseInfo {
    std::string format;
    size_t events = 0;
    double fps = 0.0;
    std::string error;   // no vacio = no se pudo leer la macro (mensaje corto para el jugador)
    std::string detail;  // detalle tecnico para el log de Geode
};

enum class Unit { Frames, Seconds, Millis };

struct RawEvent {
    double value;
    Unit unit;
    bool down;
};

struct ClickEvent {
    double time;
    bool down;
};

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------- JSON ----

// Busca "key": valor dentro de un objeto JSON plano y devuelve el valor como texto.
static bool jsonValue(const std::string& obj, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    size_t p = obj.find(needle);
    while (p != std::string::npos) {
        size_t q = p + needle.size();
        while (q < obj.size() && std::isspace(static_cast<unsigned char>(obj[q]))) ++q;
        if (q < obj.size() && obj[q] == ':') {
            ++q;
            while (q < obj.size() && std::isspace(static_cast<unsigned char>(obj[q]))) ++q;
            size_t e = q;
            while (e < obj.size() && obj[e] != ',' && obj[e] != '}' && obj[e] != ']') ++e;
            out = trim(obj.substr(q, e - q));
            if (out.size() >= 2 && out.front() == '"' && out.back() == '"') {
                out = out.substr(1, out.size() - 2);
            }
            return true;
        }
        p = obj.find(needle, p + 1);
    }
    return false;
}

static void handleJsonLeaf(const std::string& obj, std::vector<RawEvent>& out, double& fps) {
    std::string frameStr, timeStr, downStr, v;
    const bool hasFrame = jsonValue(obj, "frame", frameStr);
    const bool hasTime = jsonValue(obj, "time", timeStr);
    const bool hasDown = jsonValue(obj, "down", downStr);

    // Objeto de metadatos (sin frame ni down): puede traer los fps.
    if (!hasFrame && !hasTime) {
        if (jsonValue(obj, "fps", v) || jsonValue(obj, "tps", v)) {
            double f = std::strtod(v.c_str(), nullptr);
            if (f > 0.0) fps = f;
        }
        return;
    }
    if (!hasDown) return;

    // Solo jugador 1.
    for (const char* key : {"p2", "player2", "2p"}) {
        if (jsonValue(obj, key, v) && lower(v) == "true") return;
    }
    // Solo el boton de salto (en plataformer 2 y 3 son izquierda/derecha).
    if ((jsonValue(obj, "btn", v) || jsonValue(obj, "button", v)) && std::atoi(v.c_str()) != 1) {
        return;
    }

    RawEvent r{};
    if (hasFrame) {
        r.value = std::strtod(frameStr.c_str(), nullptr);
        r.unit = Unit::Frames;
    } else {
        r.value = std::strtod(timeStr.c_str(), nullptr);
        r.unit = Unit::Seconds;
    }
    const std::string d = lower(downStr);
    r.down = (d == "true" || d == "1");
    out.push_back(r);
}

// Recorre el JSON y procesa cada objeto "hoja" (sin objetos anidados).
static void parseJsonEvents(const std::string& text, std::vector<RawEvent>& out, double& fps) {
    std::vector<size_t> starts;
    std::vector<bool> hasChild;
    bool inString = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            if (!hasChild.empty()) hasChild.back() = true;
            starts.push_back(i);
            hasChild.push_back(false);
        } else if (c == '}' && !starts.empty()) {
            const size_t s = starts.back();
            const bool child = hasChild.back();
            starts.pop_back();
            hasChild.pop_back();
            if (!child) handleJsonLeaf(text.substr(s, i - s + 1), out, fps);
        }
    }

    if (fps <= 0.0) {
        std::string v;
        if (jsonValue(text, "fps", v) || jsonValue(text, "tps", v)) {
            double f = std::strtod(v.c_str(), nullptr);
            if (f > 0.0) fps = f;
        }
    }
}

// ---------------------------------------------------------- texto plano ----

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    };
    for (char c : line) {
        if (std::isspace(static_cast<unsigned char>(c)) || std::strchr(",;|:=[](){}\"'", c)) {
            flush();
        } else {
            cur += c;
        }
    }
    flush();
    return out;
}

// Un numero es un frame si es entero, y segundos si tiene decimales o sufijo "s".
static bool parseTime(const std::string& tok, double& value, Unit& unit) {
    std::string t = lower(tok);
    if (t.empty()) return false;
    unit = Unit::Frames;
    if (t.size() > 2 && t.compare(t.size() - 2, 2, "ms") == 0) {
        unit = Unit::Millis;
        t.resize(t.size() - 2);
    } else if (t.back() == 's') {
        unit = Unit::Seconds;
        t.pop_back();
    } else if (t.back() == 'f') {
        t.pop_back();
    }
    if (t.empty()) return false;
    char* end = nullptr;
    value = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return false;
    if (!std::isfinite(value) || value < 0.0) return false;
    if (unit == Unit::Frames && t.find_first_of(".eE") != std::string::npos) unit = Unit::Seconds;
    return true;
}

// 1 = presionado, 0 = soltado, -1 = no es un estado.
static int stateOf(const std::string& w) {
    static const char* pressed[] = {"1", "true", "down", "press", "pressed", "click", "clicked",
                                    "on", "hold", "holding", "push", "pushed"};
    static const char* released[] = {"0", "false", "up", "release", "released", "off",
                                     "unpress", "unpressed", "unclick"};
    for (auto s : pressed) if (w == s) return 1;
    for (auto s : released) if (w == s) return 0;
    return -1;
}

static bool isTimePrefix(const std::string& w) {
    return w == "frame" || w == "frames" || w == "f" || w == "time" || w == "t" || w == "tick" ||
           w == "step";
}

static bool isStateKey(const std::string& w) {
    return w == "down" || w == "press" || w == "pressed" || w == "click" || w == "hold" ||
           w == "state";
}

/*
 * Una linea de evento tiene la forma:   <tiempo> <estado> [...]
 * Ejemplos validos:
 *   120 1            120,down          frame 120 down        frame: 120 down: true
 *   0.5 press        1.25s release     120 1 0   (columnas P1 P2: se usa P1)
 * "fps: 240" (o tps) en la cabecera fija los fps. Todo lo demas se ignora.
 */
static void parseTextEvents(const std::string& text, std::vector<RawEvent>& out, double& fps) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line.compare(0, 2, "//") == 0) {
            continue;
        }
        const auto tk = tokenize(line);
        if (tk.empty()) continue;
        const std::string first = lower(tk[0]);

        if (first.find("fps") != std::string::npos || first.find("tps") != std::string::npos ||
            first.find("framerate") != std::string::npos) {
            for (size_t j = 1; j < tk.size(); ++j) {
                double v;
                Unit u;
                if (parseTime(tk[j], v, u) && v > 0.0) {
                    fps = v;
                    break;
                }
            }
            continue;
        }

        const size_t ti = isTimePrefix(first) ? 1 : 0;
        if (ti >= tk.size()) continue;
        double value = 0.0;
        Unit unit = Unit::Frames;
        if (!parseTime(tk[ti], value, unit)) continue;

        int state = -1;
        bool player2 = false;
        for (size_t j = ti + 1; j < tk.size(); ++j) {
            const std::string w = lower(tk[j]);
            if (w == "p2" || w == "2p" || w == "player2") {
                player2 = true;
                break;
            }
            if (w == "btn" || w == "button") {
                ++j;  // salta el numero de boton
                continue;
            }
            if (isStateKey(w) && j + 1 < tk.size()) {
                const std::string n = lower(tk[j + 1]);
                if (n == "true" || n == "false" || n == "1" || n == "0") {
                    state = (n == "true" || n == "1") ? 1 : 0;
                    break;
                }
            }
            const int s = stateOf(w);
            if (s != -1) {
                state = s;
                break;
            }
        }
        if (player2 || state < 0) continue;
        out.push_back({value, unit, state == 1});
    }
}

// ---------------------------------------------------------- GDR2 binario ----
//
// Estructura (README de maxnut/GDReplayFormat, rama gdr2):
//   "GDR" | version(varint) | input tag(cstring)
//   author(cstr) | description(cstr) | duracion(varint) | version del juego(varint)
//   framerate(varint) | seed(varint) | monedas(varint) | ldm(u8) | platformer(u8)
//   bot name(cstr) | bot version(varint) | level id(varint) | level name(cstr)
//   tamano extension(varint) | extension(bytes)
//   cantidad de muertes(varint) | frames de muerte(varint[], delta)
//   cantidad de inputs(varint) | inputs del P1(varint) | inputs (primero todos los de P1, luego P2)
// Cada input es un varint empaquetado: bit "down" + delta de frames respecto al input anterior
// (en platformer ademas el id de boton). El README no deja claro que bit hace de continuacion
// en ese varint, asi que se prueban las dos variantes y se acepta la que consume el archivo
// exactamente. Si la cabecera no se puede recorrer, se busca la seccion de inputs al final.

struct ByteReader {
    const unsigned char* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;

    explicit ByteReader(const std::string& s)
        : data(reinterpret_cast<const unsigned char*>(s.data())), size(s.size()) {}

    unsigned char u8() {
        if (pos >= size) {
            ok = false;
            return 0;
        }
        return data[pos++];
    }

    // LEB128 estandar: bit 7 = continuacion.
    uint64_t varint() {
        uint64_t v = 0;
        for (int k = 0; k < 10; ++k) {
            const unsigned char b = u8();
            if (!ok) return 0;
            v |= static_cast<uint64_t>(b & 0x7F) << (7 * k);
            if (!(b & 0x80)) return v;
        }
        ok = false;
        return 0;
    }

    // Variante literal del README: bit 0 = continuacion, bits 1-7 = datos.
    uint64_t varintLsb() {
        uint64_t v = 0;
        for (int k = 0; k < 10; ++k) {
            const unsigned char b = u8();
            if (!ok) return 0;
            v |= static_cast<uint64_t>(b >> 1) << (7 * k);
            if (!(b & 1)) return v;
        }
        ok = false;
        return 0;
    }

    void cstr() {
        for (size_t n = 0; n < 65536; ++n) {
            if (u8() == 0 || !ok) return;
        }
        ok = false;
    }

    void skip(uint64_t n) {
        if (n > size - pos) {
            ok = false;
            pos = size;
        } else {
            pos += static_cast<size_t>(n);
        }
    }
};

enum class PackScheme { Leb128, LsbContinuation };

// Lee `count` inputs desde la posicion actual. Devuelve true si no se salio del archivo.
static bool readGdr2Inputs(ByteReader& r, uint64_t count, uint64_t p1Count, bool platformer,
                           PackScheme scheme, bool extensionPerInput, std::vector<RawEvent>& out) {
    out.clear();
    uint64_t frame = 0;
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t v = scheme == PackScheme::Leb128 ? r.varint() : r.varintLsb();
        if (!r.ok) return false;
        if (extensionPerInput) {
            r.skip(r.varint());
            if (!r.ok) return false;
        }
        if (i == p1Count) frame = 0;  // empieza el bloque del jugador 2
        const bool down = (v & 1) != 0;
        uint64_t button = 1;
        uint64_t delta = v >> 1;
        if (platformer) {
            button = (v >> 1) & 3;  // 1 = saltar, 2 = izquierda, 3 = derecha
            delta = v >> 3;
        }
        frame += delta;
        if (i < p1Count && button == 1) out.push_back({static_cast<double>(frame), Unit::Frames, down});
    }
    return r.ok;
}

struct Gdr2Result {
    std::vector<RawEvent> events;
    double fps = 0.0;
    bool heuristic = false;
};

static bool parseGdr2Structured(const std::string& text, Gdr2Result& res) {
    ByteReader r(text);
    r.skip(3);   // "GDR"
    r.varint();  // version
    r.cstr();    // input tag
    r.cstr();    // autor
    r.cstr();    // descripcion
    r.varint();  // duracion
    r.varint();  // version del juego
    const uint64_t fps = r.varint();
    r.varint();  // seed
    r.varint();  // monedas
    r.u8();      // ldm
    const bool platformer = r.u8() != 0;
    r.cstr();    // bot name
    r.varint();  // bot version
    r.varint();  // level id
    r.cstr();    // level name
    r.skip(r.varint());  // extension
    const uint64_t deaths = r.varint();
    if (!r.ok || deaths > text.size()) return false;
    for (uint64_t d = 0; d < deaths && r.ok; ++d) r.varint();
    const uint64_t count = r.varint();
    const uint64_t p1 = r.varint();
    if (!r.ok || p1 > count || count > text.size()) return false;

    const size_t inputsPos = r.pos;
    const PackScheme schemes[] = {PackScheme::Leb128, PackScheme::LsbContinuation};
    for (bool ext : {false, true}) {
        for (PackScheme sch : schemes) {
            ByteReader rr(text);
            rr.pos = inputsPos;
            std::vector<RawEvent> ev;
            if (readGdr2Inputs(rr, count, p1, platformer, sch, ext, ev) && rr.pos == rr.size) {
                res.events = std::move(ev);
                if (fps >= 1 && fps <= 10000) res.fps = static_cast<double>(fps);
                return true;
            }
        }
    }
    return false;
}

// Plan B: si la cabecera no coincide con el README, busca desde el principio el primer
// punto donde "cantidad, cantidad P1, inputs..." consume el archivo hasta el final.
static bool parseGdr2Scan(const std::string& text, Gdr2Result& res) {
    const PackScheme schemes[] = {PackScheme::Leb128, PackScheme::LsbContinuation};
    for (size_t o = 3; o < text.size(); ++o) {
        for (PackScheme sch : schemes) {
            ByteReader r(text);
            r.pos = o;
            const uint64_t count = r.varint();
            const uint64_t p1 = r.varint();
            if (!r.ok || count == 0 || p1 > count || count > text.size() - r.pos) continue;
            std::vector<RawEvent> ev;
            if (readGdr2Inputs(r, count, p1, false, sch, false, ev) && r.pos == r.size && !ev.empty()) {
                res.events = std::move(ev);
                res.heuristic = true;
                return true;
            }
        }
    }
    return false;
}

static std::string hexHead(const std::string& s, size_t n = 24) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < s.size() && i < n; ++i) {
        std::snprintf(buf, sizeof(buf), "%02X ", static_cast<unsigned char>(s[i]));
        out += buf;
    }
    return out;
}

static bool looksLikeGdr2(const std::string& text) {
    if (text.size() < 8 || text.compare(0, 3, "GDR") != 0) return false;
    // la cabecera tiene un string terminado en 0 enseguida (el input tag)
    return text.find('\0', 3) < std::min<size_t>(text.size(), 256);
}

// --------------------------------------------------- eventos -> notas ----

static Macro buildMacro(std::vector<ClickEvent> events, double tapThreshold) {
    std::stable_sort(events.begin(), events.end(),
                     [](const ClickEvent& a, const ClickEvent& b) { return a.time < b.time; });

    Macro m;
    auto finish = [&](double t0, double t1) {
        m.presses.push_back(t0);
        if (t1 - t0 >= tapThreshold) {  // hold: barra + nota celeste al soltar
            m.holds.push_back({t0, t1});
            m.releases.push_back(t1);
        }
    };

    bool held = false;
    double t0 = 0.0;
    for (const auto& e : events) {
        if (e.down && !held) {
            held = true;
            t0 = e.time;
        } else if (!e.down && held) {
            held = false;
            finish(t0, e.time);
        }
    }
    if (held) finish(t0, t0);  // la macro termina con el boton apretado: clic simple
    return m;
}

static Macro parseMacro(const std::string& text, const ParserSettings& st, ParseInfo& info) {
    std::vector<RawEvent> raw;
    double headerFps = 0.0;

    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    const bool isJson = i < text.size() && (text[i] == '{' || text[i] == '[');
    const auto byte0 = text.empty() ? 0 : static_cast<unsigned char>(text[0]);
    const auto byte1 = text.size() < 2 ? 0 : static_cast<unsigned char>(text[1]);

    if (looksLikeGdr2(text)) {
        Gdr2Result g;
        if (parseGdr2Structured(text, g)) {
            info.format = "GDR2 (binario)";
        } else if (parseGdr2Scan(text, g)) {
            info.format = "GDR2 (binario, lectura heuristica)";
        } else {
            info.format = "GDR2 (binario)";
            info.error = "no se pudo interpretar el .gdr2";
            info.detail = "primeros bytes: " + hexHead(text);
            return Macro{};
        }
        raw = std::move(g.events);
        headerFps = g.fps;
    } else if (byte0 == 0x1F && byte1 == 0x8B) {
        info.error = "el archivo esta comprimido (gzip); descomprimilo primero";
        info.detail = "primeros bytes: " + hexHead(text);
        return Macro{};
    } else if (byte0 == 0x28 && byte1 == 0xB5) {
        info.error = "el archivo esta comprimido (zstd); descomprimilo primero";
        info.detail = "primeros bytes: " + hexHead(text);
        return Macro{};
    } else if (!isJson && text.find('\0') != std::string::npos) {
        info.error = "formato binario no reconocido";
        info.detail = "primeros bytes: " + hexHead(text);
        return Macro{};
    } else if (isJson) {
        info.format = "JSON (.mhr.json / .gdr2 json)";
        parseJsonEvents(text, raw, headerFps);
    } else {
        info.format = "texto plano";
        parseTextEvents(text, raw, headerFps);
    }

    double fps = st.fps > 0.0 ? st.fps : (headerFps > 0.0 ? headerFps : 240.0);

    std::vector<ClickEvent> events;
    events.reserve(raw.size());
    for (const auto& r : raw) {
        double t = r.value;
        if (r.unit == Unit::Frames) t = r.value / fps;
        else if (r.unit == Unit::Millis) t = r.value / 1000.0;
        events.push_back({t, r.down});
    }

    info.events = events.size();
    info.fps = fps;
    return buildMacro(std::move(events), st.tapThreshold);
}

}  // namespace rpb
// === PARSER END ===

namespace fs = std::filesystem;

// ------------------------------------------------------------ config ----

namespace {

// Ventanas de juicio en segundos (error entre el clic del jugador y la nota de la macro).
constexpr double kPerfectWin = 0.040;
constexpr double kGreatWin = 0.080;
constexpr double kGoodWin = 0.130;
constexpr double kOuterWin = 0.250;  // fuera de esto el clic no cuenta y la nota se da por perdida

constexpr float kReceptorFrac = 0.85f;  // posicion del receptor dentro de la barra (0..1)
constexpr float kNoteWidth = 6.f;
constexpr double kLinger = 0.06;
constexpr size_t kMaxSlots = 384;
constexpr size_t kMaxTrail = 64;

// bigFont.fnt de GD no trae el signo de apertura de exclamacion. Ponelo en 1 si tu fuente lo incluye.
#ifndef RPB_INVERTED_MARKS
#define RPB_INVERTED_MARKS 0
#endif
#if RPB_INVERTED_MARKS
constexpr char const* kTxtPerfect = "\xC2\xA1" "Perfecto!";
constexpr char const* kTxtMiss = "\xC2\xA1" "Fallo!";
#else
constexpr char const* kTxtPerfect = "Perfecto!";
constexpr char const* kTxtMiss = "Fallo!";
#endif

struct Config {
    bool enabled = true;
    std::string macroFile = "macro.txt";
    float barWidthPct = 60.f;   // "bar-width": largo de la barra, % del ancho de pantalla
    float barThickness = 44.f;  // "bar-height": grosor de la barra en px
    double lookahead = 1.5;     // segundos
    double offset = 0.0;        // segundos
    float bgOpacity = 0.35f;    // "bg-opacity": 0..1
    rpb::ParserSettings parser;
};

Config loadConfig() {
    auto mod = Mod::get();
    Config c;
    c.enabled = mod->getSettingValue<bool>("enabled");
    c.macroFile = mod->getSettingValue<std::string>("macro-file");
    c.barWidthPct = static_cast<float>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("bar-width"), 10, 100));
    c.barThickness = static_cast<float>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("bar-height"), 16, 200));
    c.bgOpacity = static_cast<float>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("bg-opacity"), 0, 100)) / 100.f;
    c.lookahead = std::max(0.1, mod->getSettingValue<double>("lookahead"));
    c.offset = mod->getSettingValue<int64_t>("offset") / 1000.0;
    c.parser.tapThreshold = mod->getSettingValue<int64_t>("tap-threshold") / 1000.0;
    c.parser.fps = mod->getSettingValue<double>("macro-fps");
    return c;
}

// Filas del popup: cada una enlaza con un ajuste del mod.json.
struct SettingRow {
    char const* label;
    char const* key;
    int64_t lo;
    int64_t hi;
    int64_t step;
    int64_t def;
    char const* suffix;
};

constexpr SettingRow kRows[] = {
    {"Largo", "bar-width", 10, 100, 5, 60, "%"},
    {"Grosor", "bar-height", 16, 120, 4, 44, "px"},
    {"Fondo", "bg-opacity", 0, 100, 5, 35, "%"},
};

// ------------------------------------------------------------ archivos ----

std::string pathToUtf8(fs::path const& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<char const*>(u.data()), u.size());
}

fs::path utf8ToPath(std::string const& s) {
    return fs::path(std::u8string(reinterpret_cast<char8_t const*>(s.data()), s.size()));
}

fs::path resolveMacroPath(std::string const& name) {
    fs::path p = utf8ToPath(name);
    if (p.is_relative()) p = Mod::get()->getSaveDir() / p;  // carpeta de guardado del mod
    return p;
}

bool readFile(fs::path const& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);  // BOM UTF-8
    return true;
}

// Las fuentes de GD no tienen todos los caracteres: se deja solo ASCII imprimible.
std::string asciiOnly(std::string const& s, size_t maxLen) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out += (c >= 32 && c < 127) ? static_cast<char>(c) : '_';
    if (out.size() > maxLen) {
        out.resize(maxLen > 3 ? maxLen - 3 : maxLen);
        out += "...";
    }
    return out;
}

std::string macroKey(Config const& c) {
    return c.macroFile + "|" + std::to_string(c.parser.fps) + "|" + std::to_string(c.parser.tapThreshold);
}

struct LoadResult {
    rpb::Macro macro;
    bool ok = false;
    std::string error;
    NotificationIcon icon = NotificationIcon::Error;
};

LoadResult loadMacro(Config const& cfg) {
    LoadResult r;
    const auto path = resolveMacroPath(cfg.macroFile);
    const auto shown = pathToUtf8(path);

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        log::warn("Rhythm Bar: no se encontro la macro en {}", shown);
        r.error = "no se encontro el archivo de macro";
        return r;
    }

    std::string text;
    if (!readFile(path, text)) {
        log::warn("Rhythm Bar: no se pudo leer {}", shown);
        r.error = "no se pudo leer el archivo de macro";
        return r;
    }

    rpb::ParseInfo info;
    auto macro = rpb::parseMacro(text, cfg.parser, info);
    if (!info.error.empty()) {
        log::warn("Rhythm Bar: {} -> {} ({})", shown, info.error, info.detail);
        r.error = info.error;
        return r;
    }
    log::info("Rhythm Bar: {} -> formato {}, {} eventos, {} clics, {} holds, {} fps", shown,
              info.format, info.events, macro.presses.size(), macro.holds.size(), info.fps);

    if (macro.presses.empty()) {
        r.error = "no se encontraron clics en la macro";
        r.icon = NotificationIcon::Warning;
        return r;
    }
    r.macro = std::move(macro);
    r.ok = true;
    return r;
}

// ------------------------------------------------- biblioteca de macros ----
// Se guarda en <save dir>/macro-library.txt: una ruta (UTF-8) por linea.

fs::path libraryFile() {
    return Mod::get()->getSaveDir() / "macro-library.txt";
}

std::string pathKey(std::string const& s) {
    return rpb::lower(pathToUtf8(utf8ToPath(s).lexically_normal()));
}

std::vector<std::string> loadLibrary() {
    std::vector<std::string> out;
    std::string text;
    if (!readFile(libraryFile(), text)) return out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = rpb::trim(line);
        if (line.empty()) continue;
        const auto key = pathKey(line);
        bool dup = false;
        for (auto const& e : out) {
            if (pathKey(e) == key) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(line);
    }
    return out;
}

bool saveLibrary(std::vector<std::string> const& list) {
    std::error_code ec;
    fs::create_directories(Mod::get()->getSaveDir(), ec);
    std::ofstream f(libraryFile(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (auto const& e : list) f << e << '\n';
    return static_cast<bool>(f);
}

/*
 * Tiempo actual del nivel, en segundos. Se usa el tiempo de fisica del juego
 * (m_levelTime) porque las macros estan en frames, y asi la barra se mantiene
 * sincronizada al reiniciar o al volver a un checkpoint.
 */
double currentLevelTime(PlayLayer* pl) {
    return pl->m_gameState.m_levelTime;
}

// ------------------------------------------------------------ juicio ----

enum class Judge { Perfect, Great, Good, Early, Late, Miss };

struct JudgeStyle {
    char const* text;
    ccColor3B color;
};

JudgeStyle styleOf(Judge j) {
    switch (j) {
        case Judge::Perfect: return {kTxtPerfect, ccColor3B{255, 230, 90}};
        case Judge::Great: return {"Muy Bien", ccColor3B{80, 255, 120}};
        case Judge::Good: return {"Bien", ccColor3B{90, 220, 255}};
        case Judge::Early: return {"Temprano", ccColor3B{255, 170, 50}};
        case Judge::Late: return {"Tarde", ccColor3B{190, 130, 255}};
        case Judge::Miss: break;
    }
    return {kTxtMiss, ccColor3B{255, 70, 70}};
}

// d = tiempo del jugador - tiempo de la nota (negativo = el jugador se adelanto)
Judge classify(double d) {
    const double a = std::fabs(d);
    if (a <= kPerfectWin) return Judge::Perfect;
    if (a <= kGreatWin) return Judge::Great;
    if (a <= kGoodWin) return Judge::Good;
    return d < 0.0 ? Judge::Early : Judge::Late;
}

enum NoteState : uint8_t { NotePending, NoteHit, NoteMissed, NoteSkipped };

// ------------------------------------------------------------ barra ----

class RhythmBar : public CCNode {
protected:
    struct Track {
        std::vector<double> times;
        std::vector<uint8_t> state;
        size_t cursor = 0;  // primer indice que todavia no expiro

        void assign(std::vector<double> t) {
            times = std::move(t);
            state.assign(times.size(), NotePending);
            cursor = 0;
        }
    };

    struct TrailEntry {
        double start;
        double end;
        bool open;
    };

    inline static RhythmBar* s_instance = nullptr;

    rpb::Macro m_macro;
    Track m_press;
    Track m_release;
    Config m_cfg;
    std::string m_macroKey;

    float m_w = 0.f;
    float m_h = 0.f;
    float m_recX = 0.f;  // posicion X del receptor dentro de la barra

    CCNode* m_layer = nullptr;
    CCLayerColor* m_flashG = nullptr;
    CCLayerColor* m_flashB = nullptr;
    CCLabelBMFont* m_label = nullptr;
    std::vector<CCLayerColor*> m_pool;
    size_t m_used = 0;

    std::vector<TrailEntry> m_trail;  // historial de clics del jugador
    bool m_synced = false;
    double m_lastNow = 0.0;
    double m_clock = 0.0;
    double m_labelDieAt = 0.0;
    float m_pollTimer = 0.f;

    bool init(rpb::Macro macro, Config const& cfg) {
        if (!CCNode::init()) return false;
        m_cfg = cfg;
        m_macroKey = macroKey(cfg);
        this->setMacro(std::move(macro));
        this->rebuildLayout();
        this->scheduleUpdate();
        s_instance = this;
        return true;
    }

public:
    static RhythmBar* create(rpb::Macro macro, Config const& cfg) {
        auto ret = new (std::nothrow) RhythmBar();
        if (ret && ret->init(std::move(macro), cfg)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    ~RhythmBar() override {
        if (s_instance == this) s_instance = nullptr;
    }

    static RhythmBar* instance() { return s_instance; }

    // Lo llama el popup despues de cambiar un ajuste: aplica todo al instante.
    static void refreshNow() {
        if (s_instance) s_instance->applyConfig(true);
    }

    // Lee los ajustes y reconstruye lo que haga falta (distancias, macro).
    void applyConfig(bool force) {
        const Config c = loadConfig();
        const bool relayout = force || c.barWidthPct != m_cfg.barWidthPct ||
                              c.barThickness != m_cfg.barThickness ||
                              c.bgOpacity != m_cfg.bgOpacity || c.lookahead != m_cfg.lookahead;
        const auto key = macroKey(c);
        const bool macroChanged = key != m_macroKey;

        m_cfg = c;
        this->setVisible(c.enabled);
        if (relayout) this->rebuildLayout();

        if (macroChanged) {
            m_macroKey = key;
            auto res = loadMacro(c);
            if (res.ok) {
                this->setMacro(std::move(res.macro));
            } else {
                Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
            }
        }
    }

    // Entrada del jugador (P1, salto) tal como la entrega el juego en cada paso de fisica.
    void onInput(bool down, double levelTime) {
        if (!m_cfg.enabled) return;
        const double t = levelTime - m_cfg.offset;
        if (!m_synced) this->resync(t);

        if (down) {
            this->flashDown();
            m_trail.push_back({t, t, true});
            if (m_trail.size() > kMaxTrail) m_trail.erase(m_trail.begin());
            this->judgeInput(m_press, t);
        } else {
            this->flashUp();
            if (!m_trail.empty() && m_trail.back().open) {
                m_trail.back().end = t;
                m_trail.back().open = false;
            }
            this->judgeInput(m_release, t);
        }
    }

    // Se llama cada frame: refresca el tiempo actual, juzga y redibuja.
    void update(float dt) override {
        auto pl = PlayLayer::get();
        if (!pl) return;

        m_clock += dt;
        m_pollTimer += dt;
        if (m_pollTimer >= 0.25f) {  // recoge cambios hechos desde el menu de Geode
            m_pollTimer = 0.f;
            this->applyConfig(false);
        }
        if (!m_cfg.enabled) return;

        // offset positivo = las notas llegan mas tarde
        const double now = currentLevelTime(pl) - m_cfg.offset;
        this->advanceJudge(now);
        this->refresh(now);

        if (m_label && m_clock >= m_labelDieAt) {
            m_label->removeFromParent();
            m_label = nullptr;
        }
    }

private:
    void setMacro(rpb::Macro macro) {
        m_macro = std::move(macro);
        m_press.assign(m_macro.presses);
        m_release.assign(m_macro.releases);
        m_trail.clear();
        m_synced = false;
    }

    // ---- layout ----

    void rebuildLayout() {
        this->removeAllChildren();
        m_pool.clear();
        m_used = 0;
        m_label = nullptr;

        const auto win = CCDirector::get()->getWinSize();
        const float margin = 8.f;
        m_w = std::min(win.width * m_cfg.barWidthPct / 100.f, win.width - 2.f * margin);
        m_h = m_cfg.barThickness;
        m_recX = m_w * kReceptorFrac;

        this->setContentSize({m_w, m_h});
        this->setPosition({(win.width - m_w) / 2.f, margin});  // abajo, centrada

        // fondo translucido
        const auto bgAlpha = static_cast<GLubyte>(std::clamp(m_cfg.bgOpacity, 0.f, 1.f) * 255.f);
        auto bg = CCLayerColor::create(ccColor4B{0, 0, 0, bgAlpha}, m_w, m_h);
        this->addChild(bg, 0);

        // zona de acierto (ventana "Bien") alrededor del receptor
        const float half = static_cast<float>(kGoodWin * m_recX / m_cfg.lookahead);
        auto zone = CCLayerColor::create(ccColor4B{255, 255, 255, 22}, half * 2.f, m_h);
        zone->setPosition({m_recX - half, 0.f});
        this->addChild(zone, 0);

        m_layer = CCNode::create();
        this->addChild(m_layer, 1);

        // destellos del receptor: verde (exterior, al presionar) y celeste (interior, al soltar)
        m_flashG = CCLayerColor::create(ccColor4B{60, 255, 100, 0}, 30.f, m_h);
        m_flashG->setPosition({m_recX - 15.f, 0.f});
        this->addChild(m_flashG, 2);

        m_flashB = CCLayerColor::create(ccColor4B{80, 220, 255, 0}, 14.f, m_h);
        m_flashB->setPosition({m_recX - 7.f, 0.f});
        this->addChild(m_flashB, 3);

        // linea receptora (timing line)
        auto line = CCLayerColor::create(ccColor4B{255, 255, 255, 230}, 3.f, m_h);
        line->setPosition({m_recX - 1.5f, 0.f});
        this->addChild(line, 4);
    }

    // ---- destellos ----

    void flashDown() {
        if (!m_flashG) return;
        m_flashG->stopAllActions();
        m_flashG->setOpacity(255);
        m_flashG->runAction(CCFadeTo::create(0.10f, 110));  // queda un brillo mientras se mantiene
    }

    void flashUp() {
        if (m_flashG) {
            m_flashG->stopAllActions();
            m_flashG->runAction(CCFadeTo::create(0.22f, 0));
        }
        if (m_flashB) {
            m_flashB->stopAllActions();
            m_flashB->setOpacity(255);
            m_flashB->runAction(CCFadeTo::create(0.22f, 0));
        }
    }

    // ---- juicio ----

    void spawnJudgment(Judge j) {
        const auto st = styleOf(j);
        if (m_label) {
            m_label->removeFromParent();
            m_label = nullptr;
        }
        m_label = CCLabelBMFont::create(st.text, "bigFont.fnt");
        m_label->setColor(st.color);
        m_label->setScale(0.3f);
        m_label->setPosition({m_recX, m_h + 16.f});
        this->addChild(m_label, 10);

        CCFiniteTimeAction* grow = CCEaseBackOut::create(CCScaleTo::create(0.14f, 0.55f));
        CCFiniteTimeAction* rise = CCEaseOut::create(CCMoveBy::create(0.70f, {0.f, 26.f}), 2.f);
        CCFiniteTimeAction* fade = CCSequence::create(CCDelayTime::create(0.40f),
                                                      CCFadeOut::create(0.30f), nullptr);
        m_label->runAction(CCSpawn::create(grow, rise, fade, nullptr));
        m_labelDieAt = m_clock + 0.75;
    }

    // Busca la nota pendiente mas cercana al clic dentro de la ventana exterior.
    void judgeInput(Track& tr, double t) {
        auto it = std::lower_bound(tr.times.begin(), tr.times.end(), t - kOuterWin);
        size_t best = tr.times.size();
        double bestAbs = 1e9;
        for (size_t i = static_cast<size_t>(it - tr.times.begin());
             i < tr.times.size() && tr.times[i] <= t + kOuterWin; ++i) {
            if (tr.state[i] != NotePending) continue;
            const double a = std::fabs(t - tr.times[i]);
            if (a < bestAbs) {
                bestAbs = a;
                best = i;
            }
        }
        if (best == tr.times.size()) return;  // clic suelto: no hay nota cerca
        tr.state[best] = NoteHit;
        this->spawnJudgment(classify(t - tr.times[best]));
    }

    // Notas que ya pasaron la ventana exterior sin clic = fallo.
    int expire(Track& tr, double now) {
        int missed = 0;
        while (tr.cursor < tr.times.size() && tr.times[tr.cursor] + kOuterWin < now) {
            if (tr.state[tr.cursor] == NotePending) {
                tr.state[tr.cursor] = NoteMissed;
                ++missed;
            }
            ++tr.cursor;
        }
        return missed;
    }

    // Tras un reinicio, un checkpoint o un salto grande de tiempo: se vuelve a empezar
    // desde el tiempo actual sin castigar las notas que quedaron atras.
    void resync(double now) {
        for (Track* tr : {&m_press, &m_release}) {
            const auto it = std::lower_bound(tr->times.begin(), tr->times.end(), now - kOuterWin);
            tr->cursor = static_cast<size_t>(it - tr->times.begin());
            for (size_t i = 0; i < tr->state.size(); ++i) {
                tr->state[i] = i < tr->cursor ? NoteSkipped : NotePending;
            }
        }
        m_trail.clear();
        if (m_flashG) {
            m_flashG->stopAllActions();
            m_flashG->setOpacity(0);
        }
        m_synced = true;
        m_lastNow = now;
    }

    void advanceJudge(double now) {
        if (!m_synced || now < m_lastNow - 1e-6 || now - m_lastNow > 0.5) this->resync(now);
        m_lastNow = now;

        const int missed = this->expire(m_press, now) + this->expire(m_release, now);
        if (missed > 0) this->spawnJudgment(Judge::Miss);  // un solo cartel por frame
    }

    // ---- dibujo ----

    // X dentro de la barra: receptor en m_recX, las notas futuras entran por la izquierda.
    float xAt(double t, double now) const {
        const double speed = m_recX / m_cfg.lookahead;  // px por segundo
        return static_cast<float>(m_recX - (t - now) * speed);
    }

    // Segundos que tarda una nota en salir por el borde derecho despues de cruzar el receptor.
    double tailSeconds() const {
        return (m_w - m_recX) * m_cfg.lookahead / std::max(m_recX, 1.f);
    }

    void slot(ccColor3B color, GLubyte alpha, float x, float y, float w, float h) {
        if (m_used >= kMaxSlots || !m_layer) return;
        CCLayerColor* node;
        if (m_used < m_pool.size()) {
            node = m_pool[m_used];
        } else {
            node = CCLayerColor::create(ccColor4B{255, 255, 255, 255}, 1.f, 1.f);
            m_layer->addChild(node);
            m_pool.push_back(node);
        }
        ++m_used;
        node->setColor(color);
        node->setOpacity(alpha);
        node->setContentSize({std::max(w, 1.f), std::max(h, 1.f)});
        node->setPosition({x, y});
        node->setVisible(true);
    }

    void drawNotes(Track const& tr, double now, ccColor3B color) {
        const double from = now - this->tailSeconds();
        const auto it = std::lower_bound(tr.times.begin(), tr.times.end(), from);
        for (size_t i = static_cast<size_t>(it - tr.times.begin());
             i < tr.times.size() && tr.times[i] <= now + m_cfg.lookahead; ++i) {
            const auto st = tr.state[i];
            if (st == NoteHit) continue;  // acertada: desaparece
            ccColor3B c = color;
            GLubyte a = 255;
            if (st == NoteMissed) {
                c = ccColor3B{255, 70, 70};
                a = 200;
            } else if (st == NoteSkipped) {
                a = 110;
            }
            const float x = this->xAt(tr.times[i], now);
            const float left = std::clamp(x - kNoteWidth / 2.f, 0.f, std::max(m_w - kNoteWidth, 0.f));
            slot(c, a, left, 4.f, kNoteWidth, m_h - 8.f);
        }
    }

    // Barra translucida continua entre el press y el release de la macro.
    void drawHold(rpb::Hold const& h, double now) {
        const double a = std::max(h.start, now);
        const double b = std::min(h.end, now + m_cfg.lookahead);
        if (b <= a) return;
        const float xa = this->xAt(a, now);  // mas cerca del receptor (derecha)
        const float xb = this->xAt(b, now);  // mas lejos (izquierda)
        const float lo = std::clamp(xb, 0.f, m_w);
        const float hi = std::clamp(xa, 0.f, m_w);
        const float bh = m_h * 0.5f;
        slot(ccColor3B{60, 255, 100}, 110, lo, (m_h - bh) / 2.f, hi - lo, bh);
    }

    // Huella de lo que el jugador esta haciendo: franja delgada abajo, translucida,
    // continua desde el press hasta el release (crece mientras se mantiene el boton).
    void drawTrail(double now) {
        const double tail = this->tailSeconds();
        while (!m_trail.empty() && !m_trail.front().open && m_trail.front().end < now - tail - 0.1) {
            m_trail.erase(m_trail.begin());
        }
        const float sh = std::clamp(m_h * 0.18f, 4.f, 10.f);
        for (auto const& e : m_trail) {
            const double tb = e.open ? now : e.end;
            const float xr = std::min(this->xAt(e.start, now), m_w);
            const float xl = std::max(this->xAt(tb, now), 0.f);
            if (xr <= 0.f || xl >= m_w) continue;
            slot(ccColor3B{150, 255, 170}, 150, xl, 1.f, std::max(xr - xl, 3.f), sh);
        }
    }

    void refresh(double now) {
        m_used = 0;

        // Los holds no se solapan (un solo boton), asi que estan ordenados por inicio y por fin.
        auto hit = std::lower_bound(m_macro.holds.begin(), m_macro.holds.end(), now - kLinger,
                                    [](rpb::Hold const& h, double t) { return h.end < t; });
        for (; hit != m_macro.holds.end() && hit->start <= now + m_cfg.lookahead; ++hit) {
            this->drawHold(*hit, now);
        }

        // Las notas van despues para quedar dibujadas encima de las barras.
        this->drawNotes(m_press, now, ccColor3B{60, 255, 100});     // verde: clic / inicio de hold
        this->drawNotes(m_release, now, ccColor3B{80, 220, 255});   // celeste: soltar
        this->drawTrail(now);

        for (size_t i = m_used; i < m_pool.size(); ++i) m_pool[i]->setVisible(false);
    }
};

// ------------------------------------------------------------ popup ----

class RhythmPopup : public geode::Popup {
protected:
    static constexpr size_t kPerPage = 3;

    CCLabelBMFont* m_values[3] = {nullptr, nullptr, nullptr};
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_activeLabel = nullptr;
    std::vector<CCNode*> m_listNodes;
    std::vector<std::string> m_library;
    size_t m_page = 0;

    // Punto (dx, dy) respecto al centro del fondo del popup, en coordenadas de `parent`.
    // Asi no dependemos de como este colocado m_mainLayer / m_buttonMenu en cada version.
    CCPoint at(CCNode* parent, float dx, float dy) const {
        const auto world = m_bgSprite->getParent()->convertToWorldSpace(m_bgSprite->getPosition());
        return parent->convertToNodeSpace(ccp(world.x + dx, world.y + dy));
    }

    CCLabelBMFont* addLabel(std::string const& text, float dx, float dy, float scale,
                            CCPoint anchor = CCPoint{0.5f, 0.5f},
                            ccColor3B color = ccColor3B{255, 255, 255}) {
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(scale);
        label->setAnchorPoint(anchor);
        label->setColor(color);
        label->setPosition(this->at(m_mainLayer, dx, dy));
        m_mainLayer->addChild(label, 5);
        return label;
    }

    CCMenuItemSpriteExtra* addButton(char const* caption, int width, bool green, float dx, float dy,
                                     SEL_MenuHandler cb, int tag, float height = 24.f,
                                     float scale = 0.6f) {
        auto spr = ButtonSprite::create(caption, width, true, "bigFont.fnt",
                                        green ? "GJ_button_01.png" : "GJ_button_02.png", height, scale);
        auto btn = CCMenuItemSpriteExtra::create(spr, nullptr, this, cb);
        btn->setTag(tag);
        btn->setPosition(this->at(m_buttonMenu, dx, dy));
        m_buttonMenu->addChild(btn);
        return btn;
    }

    bool init() {
        if (!Popup::init(460.f, 260.f)) return false;
        this->setTitle("Barra de Ritmo");

        // columna izquierda: dimensiones en tiempo real
        const ccColor3B gold{255, 220, 90};
        this->addLabel("Dimensiones", -125.f, 82.f, 0.4f, CCPoint{0.5f, 0.5f}, gold);
        for (int i = 0; i < 3; ++i) {
            const float y = 52.f - 32.f * static_cast<float>(i);
            this->addLabel(kRows[i].label, -215.f, y, 0.4f, CCPoint{0.f, 0.5f});
            this->addButton("-", 26, true, -120.f, y, menu_selector(RhythmPopup::onStep), i * 2);
            m_values[i] = this->addLabel("", -80.f, y, 0.4f);
            this->addButton("+", 26, true, -40.f, y, menu_selector(RhythmPopup::onStep), i * 2 + 1);
        }
        this->addButton("Restablecer", 110, false, -125.f, -45.f, menu_selector(RhythmPopup::onReset), 0);

        // columna derecha: biblioteca de macros
        this->addLabel("Biblioteca de Macros", 115.f, 82.f, 0.4f, CCPoint{0.5f, 0.5f}, gold);
        this->addButton("<", 28, false, 55.f, -30.f, menu_selector(RhythmPopup::onPage), 0, 22.f);
        m_pageLabel = this->addLabel("1/1", 115.f, -30.f, 0.4f);
        this->addButton(">", 28, false, 175.f, -30.f, menu_selector(RhythmPopup::onPage), 1, 22.f);
        this->addButton("Guardar Macro", 170, true, 115.f, -64.f, menu_selector(RhythmPopup::onSave), 0, 26.f);
        m_activeLabel = this->addLabel("", 0.f, -100.f, 0.35f);

        this->refreshValues();
        this->rebuildList();
        return true;
    }

    void refreshValues() {
        auto mod = Mod::get();
        for (int i = 0; i < 3; ++i) {
            const auto v = mod->getSettingValue<int64_t>(kRows[i].key);
            m_values[i]->setString((std::to_string(v) + kRows[i].suffix).c_str());
        }
    }

    void refreshActive() {
        const auto name = asciiOnly(pathToUtf8(resolveMacroPath(loadConfig().macroFile).filename()), 40);
        m_activeLabel->setString(("Activa: " + name).c_str());
    }

    void rebuildList() {
        for (auto n : m_listNodes) {
            n->retain();  // el boton pulsado puede ser uno de estos: se libera al final del frame
            n->removeFromParent();
            n->autorelease();
        }
        m_listNodes.clear();
        m_library = loadLibrary();

        const size_t pages = std::max<size_t>(1, (m_library.size() + kPerPage - 1) / kPerPage);
        m_page = std::min(m_page, pages - 1);
        const auto activeKey = pathKey(pathToUtf8(resolveMacroPath(loadConfig().macroFile)));

        if (m_library.empty()) {
            m_listNodes.push_back(this->addLabel("Sin macros guardadas", 115.f, 26.f, 0.35f));
        }
        for (size_t k = 0; k < kPerPage; ++k) {
            const size_t i = m_page * kPerPage + k;
            if (i >= m_library.size()) break;
            const auto name = asciiOnly(pathToUtf8(utf8ToPath(m_library[i]).filename()), 22);
            const bool active = pathKey(m_library[i]) == activeKey;  // verde = macro activa
            const float y = 54.f - 28.f * static_cast<float>(k);
            m_listNodes.push_back(this->addButton(name.c_str(), 170, active, 115.f, y,
                                                  menu_selector(RhythmPopup::onPick),
                                                  static_cast<int>(i), 24.f, 0.5f));
        }
        m_pageLabel->setString((std::to_string(m_page + 1) + "/" + std::to_string(pages)).c_str());
        this->refreshActive();
    }

    void afterSettingsChanged() {
        this->refreshValues();
        RhythmBar::refreshNow();  // la barra cambia de tamano mientras el popup esta abierto
    }

    void onStep(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag < 0 || tag >= 6) return;
        const auto& row = kRows[tag / 2];
        const int64_t dir = (tag % 2) ? 1 : -1;
        auto mod = Mod::get();
        const int64_t v = std::clamp<int64_t>(mod->getSettingValue<int64_t>(row.key) + dir * row.step,
                                              row.lo, row.hi);
        (void)mod->setSettingValue<int64_t>(row.key, v);
        this->afterSettingsChanged();
    }

    void onReset(CCObject*) {
        auto mod = Mod::get();
        for (auto const& row : kRows) (void)mod->setSettingValue<int64_t>(row.key, row.def);
        this->afterSettingsChanged();
    }

    void onPage(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 0 && m_page > 0) --m_page;
        else if (tag == 1) ++m_page;  // rebuildList lo limita a la ultima pagina
        this->rebuildList();
    }

    // Registra la macro activa (ajuste "macro-file") en la biblioteca.
    void onSave(CCObject*) {
        const auto cfg = loadConfig();
        const auto path = resolveMacroPath(cfg.macroFile);

        std::error_code ec;
        std::string text;
        if (!fs::exists(path, ec) || !readFile(path, text)) {
            Notification::create("Rhythm Bar: no se encontro la macro activa", NotificationIcon::Error)->show();
            return;
        }
        rpb::ParseInfo info;
        const auto macro = rpb::parseMacro(text, cfg.parser, info);
        if (!info.error.empty() || macro.presses.empty()) {
            Notification::create("Rhythm Bar: ese archivo no es una macro valida", NotificationIcon::Warning)->show();
            return;
        }

        auto list = loadLibrary();
        const auto stored = pathToUtf8(path);
        const auto key = pathKey(stored);
        for (auto const& e : list) {
            if (pathKey(e) == key) {
                Notification::create("Rhythm Bar: la macro ya esta en la biblioteca", NotificationIcon::Info)->show();
                return;
            }
        }
        list.push_back(stored);
        if (!saveLibrary(list)) {
            Notification::create("Rhythm Bar: no se pudo guardar la biblioteca", NotificationIcon::Error)->show();
            return;
        }
        m_page = (list.size() - 1) / kPerPage;
        Notification::create("Rhythm Bar: macro guardada", NotificationIcon::Success)->show();
        this->rebuildList();
    }

    // Un clic en una macro de la lista la activa (y la barra la carga al instante).
    void onPick(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag < 0 || static_cast<size_t>(tag) >= m_library.size()) return;
        const auto& stored = m_library[static_cast<size_t>(tag)];

        std::error_code ec;
        if (!fs::exists(utf8ToPath(stored), ec)) {
            Notification::create("Rhythm Bar: el archivo ya no existe", NotificationIcon::Error)->show();
            return;
        }
        (void)Mod::get()->setSettingValue<std::string>("macro-file", stored);
        RhythmBar::refreshNow();
        this->rebuildList();
    }

public:
    static RhythmPopup* create() {
        auto ret = new (std::nothrow) RhythmPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

}  // namespace

// ------------------------------------------------------------- hooks ----

class $modify(RhythmPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        const auto cfg = loadConfig();
        if (!cfg.enabled) return true;

        // La barra se crea aunque la macro falle: asi se puede elegir otra desde el popup.
        auto res = loadMacro(cfg);
        if (!res.ok) {
            Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
        }

        auto bar = RhythmBar::create(std::move(res.macro), cfg);
        if (!bar) return true;
        bar->setID("rhythm-bar"_spr);
        m_uiLayer->addChild(bar, 100);
        return true;
    }
};

/*
 * Entradas del jugador. pushButton/releaseButton de GJBaseGameLayer solo encolan el clic;
 * el juego lo aplica de verdad en handleButton, dentro del paso de fisica que corresponde,
 * asi que ahi el tiempo (m_levelTime) coincide con el de las macros. PlayLayer hereda este
 * metodo, por eso el hook va sobre GJBaseGameLayer.
 */
class $modify(RhythmBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (!isPlayer1 || button != 1) return;  // solo P1 y solo salto
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != static_cast<GJBaseGameLayer*>(this)) return;
        if (auto bar = RhythmBar::instance()) bar->onInput(down, currentLevelTime(pl));
    }
};

/*
 * Boton en el menu de pausa. Con geode.node-ids se engancha al menu lateral de la pausa;
 * si no lo encuentra, crea un menu propio en la esquina inferior derecha.
 */
class $modify(RhythmPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto spr = ButtonSprite::create("Ritmo", 56, true, "bigFont.fnt", "GJ_button_01.png", 26.f, 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, nullptr, this,
                                                 menu_selector(RhythmPauseLayer::onRhythmMenu));
        btn->setID("rhythm-menu-button"_spr);

        CCMenu* host = nullptr;
        for (char const* id : {"right-button-menu", "left-button-menu"}) {
            if (auto node = this->getChildByID(id)) host = typeinfo_cast<CCMenu*>(node);
            if (host) break;
        }

        if (host) {
            host->addChild(btn);
            host->updateLayout();
        } else {
            const auto win = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create();
            menu->setID("rhythm-menu"_spr);
            menu->setPosition({0.f, 0.f});
            btn->setPosition({win.width - 50.f, 28.f});
            menu->addChild(btn);
            this->addChild(menu, 10);
        }
    }

    void onRhythmMenu(CCObject*) {
        if (auto popup = RhythmPopup::create()) popup->show();
    }
};
