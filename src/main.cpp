/*
 * Rhythm Practice Bar  (luchx.barra-ritmo-final)
 * Barra vertical estilo Guitar Hero para practicar el timing de clics
 * a partir de una macro de Mega Hack.
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

// ------------------------------------------------------------ config ----

namespace {

struct Config {
    bool enabled = true;
    std::string macroFile = "macro.txt";
    bool right = true;       // lado de la pantalla
    bool falling = true;     // true: las notas bajan; false: suben
    float barWidth = 34.f;
    float barHeightPct = 70.f;
    double lookahead = 1.5;  // segundos
    double offset = 0.0;     // segundos
    float bgOpacity = 0.35f;
    rpb::ParserSettings parser;
};

Config loadConfig() {
    auto mod = Mod::get();
    Config c;
    c.enabled = mod->getSettingValue<bool>("enabled");
    c.macroFile = mod->getSettingValue<std::string>("macro-file");
    c.right = mod->getSettingValue<std::string>("side") != "Izquierda";
    c.falling = mod->getSettingValue<std::string>("direction") != "Suben";
    c.barWidth = static_cast<float>(mod->getSettingValue<int64_t>("bar-width"));
    c.barHeightPct = static_cast<float>(mod->getSettingValue<int64_t>("bar-height"));
    c.lookahead = std::max(0.1, mod->getSettingValue<double>("lookahead"));
    c.offset = mod->getSettingValue<int64_t>("offset") / 1000.0;
    c.bgOpacity = static_cast<float>(mod->getSettingValue<int64_t>("bg-opacity")) / 100.f;
    c.parser.tapThreshold = mod->getSettingValue<int64_t>("tap-threshold") / 1000.0;
    c.parser.fps = mod->getSettingValue<double>("macro-fps");
    return c;
}

std::filesystem::path resolveMacroPath(const std::string& name) {
    std::filesystem::path p(name);
    if (p.is_relative()) p = Mod::get()->getSaveDir() / p;  // carpeta de guardado del mod
    return p;
}

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);  // BOM UTF-8
    return true;
}

/*
 * Tiempo actual del nivel, en segundos. Se usa el tiempo de fisica del juego
 * (m_levelTime) porque las macros de Mega Hack estan en frames, y asi la barra
 * se mantiene sincronizada al reiniciar o al volver a un checkpoint.
 * Si preferis el tiempo de la musica, cambialo por:
 *     FMODAudioEngine::get()->getMusicTimeMS(0) / 1000.0
 */
double currentLevelTime(PlayLayer* pl) {
    return pl->m_gameState.m_levelTime;
}

// ------------------------------------------------------------ barra ----

class RhythmBar : public CCNode {
protected:
    static constexpr size_t kMaxSlots = 256;
    static constexpr double kLinger = 0.06;  // las notas se quedan un instante bajo el receptor
    static constexpr float kNoteHeight = 8.f;

    rpb::Macro m_macro;
    Config m_cfg;
    float m_w = 0.f;
    float m_h = 0.f;
    float m_recY = 0.f;  // posicion del receptor dentro de la barra
    CCNode* m_layer = nullptr;
    std::vector<CCLayerColor*> m_pool;
    size_t m_used = 0;

    bool init(rpb::Macro macro, Config const& cfg) {
        if (!CCNode::init()) return false;
        m_macro = std::move(macro);
        m_cfg = cfg;

        auto win = CCDirector::get()->getWinSize();
        m_w = cfg.barWidth;
        m_h = win.height * cfg.barHeightPct / 100.f;
        const float margin = 12.f;
        this->setContentSize({m_w, m_h});
        this->setPosition({cfg.right ? win.width - m_w - margin : margin, (win.height - m_h) / 2.f});

        // fondo translucido
        auto bgAlpha = static_cast<GLubyte>(std::clamp(cfg.bgOpacity, 0.f, 1.f) * 255.f);
        auto bg = CCLayerColor::create(ccColor4B{0, 0, 0, bgAlpha}, m_w, m_h);
        this->addChild(bg, 0);

        // receptor: abajo si las notas bajan, arriba si suben
        m_recY = cfg.falling ? m_h * 0.12f : m_h * 0.88f;

        m_layer = CCNode::create();
        this->addChild(m_layer, 1);

        auto line = CCLayerColor::create(ccColor4B{255, 255, 255, 220}, m_w, 3.f);
        line->setPosition({0.f, m_recY - 1.5f});
        this->addChild(line, 2);

        this->scheduleUpdate();
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

    // Se llama cada frame: refresca el tiempo actual y redibuja las notas.
    void update(float) override {
        auto pl = PlayLayer::get();
        if (!pl) return;
        // offset positivo = las notas llegan mas tarde
        this->refresh(currentLevelTime(pl) - m_cfg.offset);
    }

private:
    // 0 en el receptor, 1 en el borde por donde aparecen las notas
    float yAt(double t, double now) const {
        const double p = (t - now) / m_cfg.lookahead;
        const double span = m_cfg.falling ? (m_h - m_recY) : m_recY;
        const double dir = m_cfg.falling ? 1.0 : -1.0;
        return static_cast<float>(m_recY + dir * p * span);
    }

    void slot(ccColor3B color, GLubyte alpha, float x, float y, float w, float h) {
        if (m_used >= kMaxSlots) return;
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
        node->setContentSize({w, std::max(h, 1.f)});
        node->setPosition({x, y});
        node->setVisible(true);
    }

    void drawNotes(std::vector<double> const& times, double now, ccColor3B color) {
        auto it = std::lower_bound(times.begin(), times.end(), now - kLinger);
        for (; it != times.end() && *it <= now + m_cfg.lookahead; ++it) {
            const float y = yAt(*it, now);
            const float bottom = std::clamp(y - kNoteHeight / 2.f, 0.f, m_h - kNoteHeight);
            slot(color, 255, 3.f, bottom, m_w - 6.f, kNoteHeight);
        }
    }

    void drawHold(rpb::Hold const& h, double now) {
        const double a = std::max(h.start, now);
        const double b = std::min(h.end, now + m_cfg.lookahead);
        if (b <= a) return;
        const float ya = yAt(a, now);
        const float yb = yAt(b, now);
        const float lo = std::clamp(std::min(ya, yb), 0.f, m_h);
        const float hi = std::clamp(std::max(ya, yb), 0.f, m_h);
        const float w = m_w * 0.5f;
        slot(ccColor3B{60, 255, 100}, 110, (m_w - w) / 2.f, lo, w, hi - lo);
    }

    void refresh(double now) {
        m_used = 0;

        // Los holds no se solapan (un solo boton), asi que estan ordenados por inicio y por fin.
        auto hit = std::lower_bound(
            m_macro.holds.begin(), m_macro.holds.end(), now - kLinger,
            [](rpb::Hold const& h, double t) { return h.end < t; });
        for (; hit != m_macro.holds.end() && hit->start <= now + m_cfg.lookahead; ++hit) {
            drawHold(*hit, now);
        }

        // Las notas van despues para quedar dibujadas encima de las barras.
        drawNotes(m_macro.presses, now, ccColor3B{60, 255, 100});    // verde: clic / inicio de hold
        drawNotes(m_macro.releases, now, ccColor3B{80, 220, 255});   // celeste: soltar

        for (size_t i = m_used; i < m_pool.size(); ++i) m_pool[i]->setVisible(false);
    }
};

}  // namespace

// ------------------------------------------------------------- hook ----

class $modify(RhythmPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        const auto cfg = loadConfig();
        if (!cfg.enabled) return true;

        const auto path = resolveMacroPath(cfg.macroFile);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            log::warn("Rhythm Bar: no se encontro la macro en {}", path.string());
            Notification::create("Rhythm Bar: no se encontro el archivo de macro",
                                 NotificationIcon::Error)->show();
            return true;
        }

        std::string text;
        if (!readFile(path, text)) {
            log::warn("Rhythm Bar: no se pudo leer {}", path.string());
            Notification::create("Rhythm Bar: no se pudo leer el archivo de macro",
                                 NotificationIcon::Error)->show();
            return true;
        }

        rpb::ParseInfo info;
        auto macro = rpb::parseMacro(text, cfg.parser, info);
        if (!info.error.empty()) {
            log::warn("Rhythm Bar: {} -> {} ({})", path.string(), info.error, info.detail);
            Notification::create("Rhythm Bar: " + info.error, NotificationIcon::Error)->show();
            return true;
        }
        log::info("Rhythm Bar: {} -> formato {}, {} eventos, {} clics, {} holds, {} fps",
                  path.string(), info.format, info.events, macro.presses.size(),
                  macro.holds.size(), info.fps);

        if (macro.presses.empty()) {
            Notification::create("Rhythm Bar: no se encontraron clics en la macro",
                                 NotificationIcon::Warning)->show();
            return true;
        }

        auto bar = RhythmBar::create(std::move(macro), cfg);
        if (!bar) return true;
        bar->setID("rhythm-bar"_spr);
        m_uiLayer->addChild(bar, 100);
        return true;
    }
};
