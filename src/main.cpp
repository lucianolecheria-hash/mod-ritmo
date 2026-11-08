/*
 * Rhythm Practice Bar  (luchx.barra-ritmo-final)
 * Barra horizontal inferior estilo juego de ritmo para practicar el timing de clics
 * a partir de una macro de Mega Hack / .gdr2.
 *
 * - Las notas viajan de izquierda a derecha hacia una linea receptora.
 * - Al presionar el salto (P1) el receptor destella en verde; al soltar, en celeste.
 * - Cada clic/soltar se juzga contra la macro: Perfecto / Muy Bien / Bien / Temprano / Tarde,
 *   y una nota que se pasa sin clic cuenta como Fallo.
 * - Todo el dibujo de la barra usa rectangulos solidos (CCLayerColor): CERO texturas del juego,
 *   asi que no puede quedar "vacia" ni con tamano 0. Los rectangulos se crean UNA vez y los
 *   botones +/- del popup les cambian el setContentSize en vivo.
 * - Ajustes "bar-width", "bar-height" y "bg-opacity" del mod.json (o desde el boton
 *   "Ritmo" del menu de pausa, en tiempo real).
 * - "Velocidad" (scroll speed) en el popup "Ritmo", de 20% a 400% y en vivo: mas rapido = las
 *   notas viajan mas rapido y quedan mas separadas; mas lento = quedan mas pegadas. Se guarda
 *   como valor guardado del mod (no hace falta tocar mod.json); el ajuste "lookahead" del
 *   mod.json sigue siendo la base (100%).
 * - Macros por NOMBRE: escribis "macro.gdr2" y el mod lo busca solo (carpeta del mod,
 *   megahack/, Geode/mods/*, y el resto de la carpeta del juego). Tambien podes dar una ruta completa.
 * - Biblioteca de macros guardada en <save dir>/macro-library.txt (una ruta por linea).
 *   Se agregan desde el popup "Ritmo" (campo de texto + boton "Agregar").
 * - "Modo Experimento": oculta TODO el nivel (fondo, cubo, obstaculos) y deja solo la barra.
 *   El nivel sigue corriendo y la musica sigue sonando. Se activa desde el popup "Ritmo"
 *   (casilla "Modo Experimento"). Se reinicia a "apagado" cada vez que abris el juego.
 * - El menu de pausa solo agrega UN boton ("Ritmo"); se coloca en una esquina libre
 *   comprobando que no se superponga con ningun boton o texto del propio menu de pausa.
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
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
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

// Velocidad de desplazamiento de las notas (en %). 100% = el "lookahead" del mod.json.
constexpr int64_t kSpeedMin = 20;
constexpr int64_t kSpeedMax = 400;
constexpr int64_t kSpeedDef = 100;

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

const ccColor3B kColWhite{255, 255, 255};
const ccColor3B kColGreen{60, 255, 100};
const ccColor3B kColCyan{80, 220, 255};
const ccColor3B kColRed{255, 70, 70};

// Modo Experimento: solo vive en esta sesion del juego (se apaga al reiniciar GD).
bool g_experiment = false;

// Ultimo estado de la creacion de la barra ("ok" o el motivo por el que no existe).
// Se muestra en el popup y en los avisos, asi se ve en pantalla que pasa sin abrir logs.
std::string g_barStatus = "sin iniciar";

struct Config {
    bool enabled = true;
    std::string macroFile = "macro.txt";
    float barWidthPct = 60.f;   // "bar-width": largo de la barra, % del ancho de pantalla
    float barThickness = 44.f;  // "bar-height": grosor de la barra en px
    double lookahead = 1.5;     // segundos que tarda una nota en llegar al receptor (ya con la velocidad aplicada)
    double offset = 0.0;        // segundos
    float bgOpacity = 0.35f;    // "bg-opacity": 0..1
    rpb::ParserSettings parser;
};

// Lectura tolerante de ajustes. getSettingValue<T>() de Geode devuelve en silencio 0 / false / ""
// cuando el ajuste NO esta declarado en mod.json (o tiene otro tipo): un "enabled" ausente
// apagaba la barra entera sin avisar, y por eso ni se creaba ni respondia el Modo Experimento.
// Aqui, si el ajuste no existe, se usa un valor por defecto (o el valor guardado del mod).
int64_t intSetting(char const* key, int64_t def) {
    auto mod = Mod::get();
    if (mod->hasSetting(key)) return mod->getSettingValue<int64_t>(key);
    return mod->getSavedValue<int64_t>(key, def);
}

void setIntSetting(char const* key, int64_t v) {
    auto mod = Mod::get();
    if (mod->hasSetting(key)) {
        (void)mod->setSettingValue<int64_t>(key, v);
    } else {
        (void)mod->setSavedValue<int64_t>(key, v);
    }
}

double doubleSetting(char const* key, double def) {
    auto mod = Mod::get();
    return mod->hasSetting(key) ? mod->getSettingValue<double>(key) : def;
}

bool boolSetting(char const* key, bool def) {
    auto mod = Mod::get();
    return mod->hasSetting(key) ? mod->getSettingValue<bool>(key) : def;
}

std::string macroFileSetting() {
    auto mod = Mod::get();
    std::string v = mod->hasSetting("macro-file")
                        ? mod->getSettingValue<std::string>("macro-file")
                        : mod->getSavedValue<std::string>("macro-file", std::string("macro.txt"));
    return v.empty() ? std::string("macro.txt") : v;
}

void setMacroFile(std::string const& v) {
    auto mod = Mod::get();
    if (mod->hasSetting("macro-file")) {
        (void)mod->setSettingValue<std::string>("macro-file", v);
    } else {
        (void)mod->setSavedValue<std::string>("macro-file", v);
    }
}

Config loadConfig() {
    Config c;
    c.enabled = boolSetting("enabled", true);
    c.macroFile = macroFileSetting();
    c.barWidthPct = static_cast<float>(std::clamp<int64_t>(intSetting("bar-width", 60), 10, 100));
    c.barThickness = static_cast<float>(std::clamp<int64_t>(intSetting("bar-height", 44), 16, 200));
    c.bgOpacity = static_cast<float>(std::clamp<int64_t>(intSetting("bg-opacity", 35), 0, 100)) / 100.f;
    // Velocidad: mas % = la nota recorre la barra en menos tiempo = notas mas separadas en pantalla.
    // Esto equivale a multiplicar los pixeles por segundo de las notas por la velocidad, y mantiene
    // la linea receptora alineada con el momento real del clic.
    const double speed = static_cast<double>(std::clamp<int64_t>(
                             intSetting("scroll-speed", kSpeedDef), kSpeedMin, kSpeedMax)) /
                         100.0;
    double base = doubleSetting("lookahead", 1.5);
    if (base <= 0.0) base = 1.5;
    const double baseLookahead = std::max(0.1, base);
    c.lookahead = std::max(0.05, baseLookahead / speed);
    c.offset = static_cast<double>(intSetting("offset", 0)) / 1000.0;
    c.parser.tapThreshold =
        static_cast<double>(std::clamp<int64_t>(intSetting("tap-threshold", 120), 10, 1000)) / 1000.0;
    c.parser.fps = std::max(0.0, doubleSetting("macro-fps", 0.0));
    return c;
}

// Filas del popup con botones "-" / "+". `saved` = false: enlaza con un ajuste del mod.json;
// `saved` = true: se guarda como valor guardado del mod (no necesita entrada en mod.json).
struct SettingRow {
    char const* label;
    char const* key;
    int64_t lo;
    int64_t hi;
    int64_t step;
    int64_t def;
    char const* suffix;
    bool saved;
};

constexpr SettingRow kRows[] = {
    {"Largo", "bar-width", 10, 100, 5, 60, "%", false},
    {"Grosor", "bar-height", 16, 120, 4, 44, "px", false},
    {"Fondo", "bg-opacity", 0, 100, 5, 60, "%", false},
    {"Velocidad", "scroll-speed", kSpeedMin, kSpeedMax, 10, kSpeedDef, "%", true},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

// Usa el ajuste de mod.json si existe y, si no, el valor guardado del mod (ver intSetting).
int64_t rowValue(SettingRow const& row) {
    return intSetting(row.key, row.def);
}

void setRowValue(SettingRow const& row, int64_t v) {
    setIntSetting(row.key, v);
}

// ------------------------------------------------------------ archivos ----

std::string pathToUtf8(fs::path const& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<char const*>(u.data()), u.size());
}

fs::path utf8ToPath(std::string const& s) {
    return fs::path(std::u8string(reinterpret_cast<char8_t const*>(s.data()), s.size()));
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

// ------------------------------------------------- buscador de macros ----
// El jugador escribe solo el nombre ("macro.gdr2") y se busca en este orden:
//   1. ruta completa (si la escribio entera)
//   2. acceso directo <raiz>/<nombre> en cada raiz
//   3. busqueda recursiva por nombre de archivo en cada raiz:
//        carpeta de guardado del mod -> <juego>/megahack -> Geode/mods (otros bots) -> todo <juego>
// Sin extension ("macro") tambien prueba macro.gdr2, macro.mhr.json, macro.json y macro.txt.
// El resultado se guarda en cache para no volver a recorrer carpetas.

constexpr char const* kMacroExts[] = {".gdr2", ".mhr.json", ".json", ".txt"};

bool hasMacroExt(std::string const& lowerName) {
    for (auto e : kMacroExts) {
        const size_t n = std::strlen(e);
        if (lowerName.size() > n && lowerName.compare(lowerName.size() - n, n, e) == 0) return true;
    }
    return false;
}

bool nameMatches(std::string const& fileLower, std::string const& wantLower, bool wantHasExt) {
    if (fileLower == wantLower) return true;
    if (wantHasExt) return false;
    if (fileLower.size() <= wantLower.size() ||
        fileLower.compare(0, wantLower.size(), wantLower) != 0) {
        return false;
    }
    const std::string rest = fileLower.substr(wantLower.size());
    for (auto e : kMacroExts) {
        if (rest == e) return true;
    }
    return false;
}

// Carpetas pesadas o inutiles que no vale la pena recorrer.
bool skipDirName(std::string const& lowerName) {
    static constexpr char const* kSkip[] = {"resources", "unzipped", "temp", "crashlogs",
                                            "logs",      "index",    "update", "cache", ".git"};
    for (auto s : kSkip) {
        if (lowerName == s) return true;
    }
    return false;
}

std::optional<fs::path> searchTree(fs::path const& root, std::string const& wantLower,
                                   bool wantHasExt, int maxDepth, size_t& budget) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return std::nullopt;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (budget == 0) return std::nullopt;
        --budget;

        std::error_code e2;
        const auto& entry = *it;
        if (entry.is_directory(e2)) {
            if (it.depth() + 1 >= maxDepth ||
                skipDirName(rpb::lower(pathToUtf8(entry.path().filename())))) {
                it.disable_recursion_pending();
            }
        } else if (entry.is_regular_file(e2)) {
            if (nameMatches(rpb::lower(pathToUtf8(entry.path().filename())), wantLower, wantHasExt)) {
                return entry.path();
            }
        }
        it.increment(ec);
    }
    return std::nullopt;
}

struct SearchRoot {
    fs::path path;
    int depth;
};

std::vector<SearchRoot> searchRoots() {
    std::vector<SearchRoot> roots;
    const fs::path save = Mod::get()->getSaveDir();
    const fs::path game = dirs::getGameDir();
    roots.push_back({save, 4});                 // carpeta de guardado de este mod
    roots.push_back({game / "megahack", 8});    // macros de Mega Hack (megahack/replays, ...)
    roots.push_back({save.parent_path(), 4});   // Geode/mods/*: macros de otros bots
    roots.push_back({game, 6});                 // el resto de la carpeta del juego
    return roots;
}

std::unordered_map<std::string, fs::path>& macroCache() {
    static std::unordered_map<std::string, fs::path> cache;
    return cache;
}

// allowScan = false: solo resuelve rapido (ruta completa, acceso directo o cache), sin recorrer carpetas.
// forceRescan = true: ignora la cache.
std::optional<fs::path> findMacro(std::string const& rawName, bool allowScan = true,
                                  bool forceRescan = false) {
    const std::string name = rpb::trim(rawName);
    if (name.empty()) return std::nullopt;

    auto& cache = macroCache();
    const std::string cacheKey = rpb::lower(name);
    std::error_code ec;

    if (forceRescan) {
        cache.erase(cacheKey);
    } else if (auto c = cache.find(cacheKey); c != cache.end()) {
        if (fs::is_regular_file(c->second, ec)) return c->second;
        cache.erase(c);
    }

    auto remember = [&](fs::path p) -> std::optional<fs::path> {
        cache[cacheKey] = p;
        return p;
    };

    const fs::path wanted = utf8ToPath(name);
    const std::string wantedFile = rpb::lower(pathToUtf8(wanted.filename()));
    if (wantedFile.empty()) return std::nullopt;
    const bool hasExt = hasMacroExt(wantedFile);

    // 1) ruta completa
    if (wanted.is_absolute() && fs::is_regular_file(wanted, ec)) return remember(wanted);

    const auto roots = searchRoots();

    // 2) acceso directo (el nombre puede traer subcarpetas: "mis macros/macro.gdr2")
    for (auto const& root : roots) {
        const fs::path base = wanted.is_absolute() ? wanted : root.path / wanted;
        if (fs::is_regular_file(base, ec)) return remember(base);
        if (!hasExt) {
            for (auto ext : kMacroExts) {
                fs::path withExt = base;
                withExt += ext;
                if (fs::is_regular_file(withExt, ec)) return remember(withExt);
            }
        }
    }
    if (!allowScan) return std::nullopt;

    // 3) busqueda por nombre de archivo, sin importar la subcarpeta
    size_t budget = 80000;
    for (auto const& root : roots) {
        if (auto p = searchTree(root.path, wantedFile, hasExt, root.depth, budget)) {
            return remember(*p);
        }
        if (budget == 0) break;
    }
    return std::nullopt;
}

struct LoadResult {
    rpb::Macro macro;
    bool ok = false;
    std::string error;
    NotificationIcon icon = NotificationIcon::Error;
};

LoadResult loadMacro(Config const& cfg) {
    LoadResult r;
    const auto found = findMacro(cfg.macroFile);
    if (!found) {
        log::warn("Rhythm Bar: no se encontro la macro \"{}\"", cfg.macroFile);
        r.error = "no se encontro \"" + asciiOnly(cfg.macroFile, 40) + "\"";
        return r;
    }
    const auto path = *found;
    const auto shown = pathToUtf8(path);

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
    return {kTxtMiss, kColRed};
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

// Rectangulo solido (sin texturas): (x, y) = esquina inferior izquierda dentro del padre.
// El tamano minimo es 1x1 para que nunca quede "invisible" por tener tamano 0.
CCLayerColor* makeSolid(ccColor4B color, float w, float h, float x = 0.f, float y = 0.f) {
    auto node = CCLayerColor::create(color, std::max(w, 1.f), std::max(h, 1.f));
    node->setPosition({x, y});
    return node;
}

// Cambia tamano y posicion de un rectangulo solido ya creado (esto es lo que usan los +/- del popup).
void resizeSolid(CCLayerColor* node, float w, float h, float x, float y) {
    if (!node) return;
    node->setContentSize({std::max(w, 1.f), std::max(h, 1.f)});
    node->setPosition({x, y});
}

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

    struct HiddenNode {
        Ref<CCNode> node;
        bool wasVisible;
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

    // Rectangulos solidos fijos: se crean una sola vez (buildStatic) y se redimensionan (applyLayout).
    CCLayerColor* m_bg = nullptr;     // fondo de la barra
    CCLayerColor* m_zone = nullptr;   // zona de acierto alrededor del receptor
    CCLayerColor* m_edge[4] = {};     // borde: abajo, arriba, izquierda, derecha
    CCLayerColor* m_flashG = nullptr; // destello verde (presionar)
    CCLayerColor* m_flashB = nullptr; // destello celeste (soltar)
    CCLayerColor* m_line = nullptr;   // linea receptora

    CCNode* m_layer = nullptr;        // contenedor de notas / holds / huella
    CCLabelBMFont* m_label = nullptr;
    std::vector<CCLayerColor*> m_pool;
    size_t m_used = 0;

    std::vector<TrailEntry> m_trail;  // historial de clics del jugador
    bool m_synced = false;
    double m_lastNow = 0.0;
    double m_clock = 0.0;
    double m_labelDieAt = 0.0;
    float m_pollTimer = 0.f;

    // Modo Experimento
    CCLayerColor* m_blackout = nullptr;  // pantalla negra detras de la UI (la posee m_uiLayer)
    std::vector<HiddenNode> m_hidden;    // nodos del nivel ocultados, con su visibilidad original
    bool m_levelHidden = false;

    bool init(rpb::Macro macro, Config const& cfg) {
        if (!CCNode::init()) return false;
        m_cfg = cfg;
        m_macroKey = macroKey(cfg);
        this->buildStatic();
        this->setMacro(std::move(macro));
        this->applyLayout();
        this->setVisible(cfg.enabled);
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

    // Lo llama la casilla "Modo Experimento" del popup.
    static void applyExperimentNow() {
        if (s_instance) s_instance->syncLevelVisibility();
    }

    // Para el log de diagnostico.
    CCSize barSize() const { return {m_w, m_h}; }

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
        if (relayout) this->applyLayout();  // solo setContentSize / setPosition de los rectangulos

        if (macroChanged) {
            m_macroKey = key;
            auto res = loadMacro(c);
            if (res.ok) {
                this->setMacro(std::move(res.macro));
            } else {
                Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
            }
        }

        // Redibuja ya mismo (el juego puede estar en pausa y no llamar a update).
        if (c.enabled && (relayout || macroChanged)) this->refresh(this->currentNow());
        this->syncLevelVisibility();
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
        this->syncLevelVisibility();
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

    // ---- Modo Experimento ----
    // Dos capas de seguridad: (1) una pantalla negra opaca detras de la UI y por encima del nivel,
    // (2) todos los hijos del PlayLayer (fondo, suelo, objetos, cubo...) se ponen invisibles.
    // El nivel sigue corriendo y la musica sigue sonando: solo se deja de dibujar.
    void syncLevelVisibility() {
        const bool want = m_cfg.enabled && g_experiment;
        if (!want && !m_levelHidden) return;

        auto pl = PlayLayer::get();
        if (!pl) return;

        if (want) {
            this->showBlackout(true);
            this->hideLevelNodes(pl);
            m_levelHidden = true;
        } else {
            this->showBlackout(false);
            this->restoreLevelNodes();
            m_levelHidden = false;
        }
    }

private:
    double currentNow() {
        auto pl = PlayLayer::get();
        return pl ? currentLevelTime(pl) - m_cfg.offset : m_lastNow;
    }

    void setMacro(rpb::Macro macro) {
        m_macro = std::move(macro);
        m_press.assign(m_macro.presses);
        m_release.assign(m_macro.releases);
        m_trail.clear();
        m_synced = false;
    }

    // ---- modo experimento (detalle) ----

    void showBlackout(bool on) {
        if (on) {
            if (!m_blackout) {
                auto parent = this->getParent();
                if (!parent) return;
                const auto win = CCDirector::get()->getWinSize();
                m_blackout = makeSolid(ccColor4B{0, 0, 0, 255}, win.width, win.height);
                parent->addChild(m_blackout, -100);  // detras de todo lo demas de la UI
            }
            m_blackout->setVisible(true);
        } else if (m_blackout) {
            m_blackout->setVisible(false);
        }
    }

    // OJO: no puede ser const, getParent() de Cocos2d no es const.
    bool keepsVisible(PlayLayer* pl, CCNode* n) {
        CCNode* ui = static_cast<CCNode*>(pl->m_uiLayer);
        CCNode* parent = this->getParent();
        CCNode* black = static_cast<CCNode*>(m_blackout);
        return n == ui || n == parent || n == black ||
               typeinfo_cast<FLAlertLayer*>(n) != nullptr ||
               typeinfo_cast<GJDropDownLayer*>(n) != nullptr;  // pantalla de nivel completado, etc.
    }

    void hideLevelNodes(PlayLayer* pl) {
        auto children = pl->getChildren();
        if (!children) return;
        for (auto child : CCArrayExt<CCNode*>(children)) {
            if (!child || this->keepsVisible(pl, child)) continue;

            bool known = false;
            for (auto const& h : m_hidden) {
                if (h.node.data() == child) {
                    known = true;
                    break;
                }
            }
            if (!known) m_hidden.push_back({Ref<CCNode>(child), child->isVisible()});
            if (child->isVisible()) child->setVisible(false);
        }
    }

    void restoreLevelNodes() {
        for (auto& h : m_hidden) {
            if (h.node.data()) h.node->setVisible(h.wasVisible);
        }
        m_hidden.clear();
    }

    // ---- layout ----

    // Crea los rectangulos solidos UNA sola vez. El tamano real lo pone applyLayout().
    void buildStatic() {
        m_bg = makeSolid(ccColor4B{14, 16, 30, 90}, 1.f, 1.f);
        this->addChild(m_bg, 0);

        m_zone = makeSolid(ccColor4B{255, 255, 255, 28}, 1.f, 1.f);
        this->addChild(m_zone, 0);

        m_layer = CCNode::create();
        this->addChild(m_layer, 1);

        // destellos del receptor: verde (exterior, al presionar) y celeste (interior, al soltar)
        m_flashG = makeSolid(ccColor4B{60, 255, 100, 0}, 1.f, 1.f);
        this->addChild(m_flashG, 2);

        m_flashB = makeSolid(ccColor4B{80, 220, 255, 0}, 1.f, 1.f);
        this->addChild(m_flashB, 3);

        // borde de la barra: 4 franjas finas, siempre visibles aunque el fondo sea transparente
        for (auto& e : m_edge) {
            e = makeSolid(ccColor4B{140, 210, 255, 220}, 1.f, 1.f);
            this->addChild(e, 5);
        }

        // linea receptora (timing line): sobresale un poco por arriba y por abajo
        m_line = makeSolid(ccColor4B{255, 255, 255, 255}, 1.f, 1.f);
        this->addChild(m_line, 6);
    }

    // Aplica largo / grosor / fondo / velocidad actuales con setContentSize + setPosition.
    // Se llama al crear la barra y cada vez que un boton +/- del popup cambia un valor.
    void applyLayout() {
        const auto win = CCDirector::get()->getWinSize();
        const float margin = 10.f;
        m_w = std::max(40.f, std::min(win.width * m_cfg.barWidthPct / 100.f, win.width - 2.f * margin));
        m_h = std::max(16.f, m_cfg.barThickness);
        m_recX = m_w * kReceptorFrac;

        this->setContentSize({m_w, m_h});
        this->setPosition({(win.width - m_w) / 2.f, margin});  // abajo, centrada

        // fondo solido
        if (m_bg) {
            m_bg->setOpacity(static_cast<GLubyte>(std::clamp(m_cfg.bgOpacity, 0.f, 1.f) * 255.f));
        }
        resizeSolid(m_bg, m_w, m_h, 0.f, 0.f);

        // zona de acierto (ventana "Bien") alrededor del receptor, sin salirse de la barra
        const float half = static_cast<float>(kGoodWin * m_recX / m_cfg.lookahead);
        const float zl = std::max(0.f, m_recX - half);
        const float zr = std::min(m_w, m_recX + half);
        resizeSolid(m_zone, zr - zl, m_h, zl, 0.f);

        resizeSolid(m_flashG, 34.f, m_h, m_recX - 17.f, 0.f);
        resizeSolid(m_flashB, 16.f, m_h, m_recX - 8.f, 0.f);

        const float b = 2.f;
        resizeSolid(m_edge[0], m_w, b, 0.f, 0.f);          // abajo
        resizeSolid(m_edge[1], m_w, b, 0.f, m_h - b);      // arriba
        resizeSolid(m_edge[2], b, m_h, 0.f, 0.f);          // izquierda
        resizeSolid(m_edge[3], b, m_h, m_w - b, 0.f);      // derecha

        resizeSolid(m_line, 4.f, m_h + 10.f, m_recX - 2.f, -5.f);
    }

    // ---- destellos ----

    void flashDown() {
        if (m_flashG) {
            m_flashG->stopAllActions();
            m_flashG->setOpacity(255);
            m_flashG->runAction(CCFadeTo::create(0.10f, 110));  // queda un brillo mientras se mantiene
        }
        if (m_flashB) {
            m_flashB->stopAllActions();
            m_flashB->setOpacity(0);
        }
        if (m_line) {
            m_line->stopAllActions();
            m_line->setColor(kColGreen);  // verde brillante mientras el boton esta apretado
        }
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
        if (m_line) {
            m_line->stopAllActions();
            m_line->setColor(kColCyan);  // celeste al soltar, vuelve a blanco
            m_line->runAction(CCTintTo::create(0.25f, 255, 255, 255));
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
        if (m_flashB) {
            m_flashB->stopAllActions();
            m_flashB->setOpacity(0);
        }
        if (m_line) {
            m_line->stopAllActions();
            m_line->setColor(kColWhite);
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

    // X dentro de la barra: receptor en m_recX, las notas futuras entran por la izquierda
    // y viajan hacia la derecha. px por segundo = m_recX / lookahead, o sea que la velocidad
    // del popup multiplica directamente cuanto se desplazan las notas por segundo.
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
                c = kColRed;
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
        slot(kColGreen, 110, lo, (m_h - bh) / 2.f, hi - lo, bh);
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
        this->drawNotes(m_press, now, kColGreen);    // verde: clic / inicio de hold
        this->drawNotes(m_release, now, kColCyan);   // celeste: soltar
        this->drawTrail(now);

        for (size_t i = m_used; i < m_pool.size(); ++i) m_pool[i]->setVisible(false);
    }
};

// ------------------------------------------------- crear la barra (seguro) ----
// Unico punto donde se crea la barra. Lo llaman PlayLayer::init, PlayLayer::resetLevel (red de
// seguridad), el menu de pausa y el Modo Experimento. Si no se puede crear, deja el motivo en
// g_barStatus (se ve en el popup) en vez de fallar en silencio.
RhythmBar* ensureBar(PlayLayer* pl) {
    if (auto existing = RhythmBar::instance()) return existing;

    if (!pl || !pl->m_uiLayer) {
        g_barStatus = "no hay un nivel en juego";
        return nullptr;
    }

    const auto cfg = loadConfig();
    if (!cfg.enabled) {
        g_barStatus = "ajuste enabled apagado";
        log::warn("Rhythm Bar: no se crea la barra: el ajuste \"enabled\" esta apagado");
        return nullptr;
    }

    // La barra se crea aunque la macro falle: asi se puede elegir otra desde el popup.
    auto res = loadMacro(cfg);
    if (!res.ok) {
        Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
    }

    auto bar = RhythmBar::create(std::move(res.macro), cfg);
    if (!bar) {
        g_barStatus = "fallo al crear la barra";
        log::error("Rhythm Bar: RhythmBar::create devolvio null");
        return nullptr;
    }
    bar->setID("rhythm-bar"_spr);
    pl->m_uiLayer->addChild(bar, 1000);
    g_barStatus = "ok";

    // Diagnostico: si la barra no se ve, mira esta linea en el log de Geode.
    const auto size = bar->barSize();
    const auto pos = bar->getPosition();
    log::info("Rhythm Bar: barra creada y agregada a la UI ({}x{} en {},{}; visible={}; z={})",
              size.width, size.height, pos.x, pos.y, bar->isVisible(), bar->getZOrder());
    return bar;
}

// ------------------------------------------------------------ popup ----
//
// Tamano 420 x 300: la pantalla de GD mide 320 de alto, asi que el popup entra completo
// (el anterior de 330 se salia por arriba y por abajo). Todas las posiciones son (dx, dy)
// respecto al centro del popup. Anchos reales de ButtonSprite: ~ ancho pedido + 15.
//
//   columna izquierda:  x de -198 a  0     (dimensiones y velocidad)
//   separador vertical: x = +7
//   columna derecha:    x de  +20 a +196   (biblioteca de macros, centro en +108)

class RhythmPopup : public geode::Popup {
protected:
    static constexpr size_t kPerPage = 3;
    static constexpr float kPopupW = 420.f;
    static constexpr float kPopupH = 300.f;

    // filas de ajustes (columna izquierda) y de la lista (columna derecha)
    static constexpr float kRowY0 = 76.f;
    static constexpr float kRowDY = 27.f;
    static constexpr float kRightCX = 108.f;

    CCLabelBMFont* m_values[kRowCount] = {};
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_activeLabel = nullptr;
    CCMenuItemToggler* m_expToggle = nullptr;
    TextInput* m_input = nullptr;
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
        if (!Popup::init(kPopupW, kPopupH)) return false;
        this->setTitle("Barra de Ritmo");

        const ccColor3B gold{255, 220, 90};
        const CCPoint center{0.5f, 0.5f};
        const CCPoint leftAnchor{0.f, 0.5f};

        // separador vertical entre las dos columnas
        {
            auto divider = makeSolid(ccColor4B{0, 0, 0, 70}, 2.f, 138.f);
            divider->setPosition(this->at(m_mainLayer, 7.f, -46.f));
            m_mainLayer->addChild(divider, 3);
        }

        // ---- columna izquierda: dimensiones en tiempo real (+ / - estiran la barra solida) ----
        this->addLabel("Dimensiones y Velocidad", -100.f, 104.f, 0.35f, center, gold);
        for (size_t i = 0; i < kRowCount; ++i) {
            const float y = kRowY0 - kRowDY * static_cast<float>(i);
            this->addLabel(kRows[i].label, -198.f, y, 0.36f, leftAnchor);
            this->addButton("-", 26, true, -108.f, y, menu_selector(RhythmPopup::onStep),
                            static_cast<int>(i) * 2);
            m_values[i] = this->addLabel("", -64.f, y, 0.4f);
            this->addButton("+", 26, true, -20.f, y, menu_selector(RhythmPopup::onStep),
                            static_cast<int>(i) * 2 + 1);
        }
        this->addButton("Restablecer", 100, false, -100.f, -33.f, menu_selector(RhythmPopup::onReset), 0);

        // ---- columna derecha: biblioteca de macros ----
        this->addLabel("Biblioteca de Macros", kRightCX, 104.f, 0.35f, center, gold);
        this->addButton("<", 28, false, 44.f, -8.f, menu_selector(RhythmPopup::onPage), 0, 22.f);
        m_pageLabel = this->addLabel("1/1", kRightCX, -8.f, 0.4f);
        this->addButton(">", 28, false, 172.f, -8.f, menu_selector(RhythmPopup::onPage), 1, 22.f);
        this->addButton("Quitar", 70, false, kRightCX, -33.f, menu_selector(RhythmPopup::onRemove), 0,
                        22.f, 0.5f);

        // ---- Modo Experimento (casilla), ocupa su propia fila para no pisar nada ----
        {
            auto toggle = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(RhythmPopup::onExperiment), 0.7f);
            toggle->setPosition(this->at(m_buttonMenu, -95.f, -61.f));
            toggle->toggle(g_experiment);
            m_buttonMenu->addChild(toggle);
            m_expToggle = toggle;
            this->addLabel("Modo Experimento (oculta el nivel)", -78.f, -61.f, 0.38f, leftAnchor);
        }

        // ---- fila inferior: agregar una macro escribiendo solo su nombre ----
        this->addLabel("Agregar macro por nombre (vacio = la activa)", 0.f, -83.f, 0.32f, center, gold);
        m_input = TextInput::create(260.f, "ej: macro.gdr2", "bigFont.fnt");
        m_input->setFilter(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-()[]+,'&!#@%=/\\:");
        m_input->setMaxCharCount(120);
        m_input->setPosition(this->at(m_mainLayer, -62.f, -105.f));
        m_mainLayer->addChild(m_input, 5);
        this->addButton("Agregar", 90, true, 140.f, -105.f, menu_selector(RhythmPopup::onAdd), 0, 26.f);

        m_activeLabel = this->addLabel("", 0.f, -132.f, 0.32f);

        this->refreshValues();
        this->rebuildList();
        return true;
    }

    void refreshValues() {
        for (size_t i = 0; i < kRowCount; ++i) {
            const auto v = rowValue(kRows[i]);
            m_values[i]->setString((std::to_string(v) + kRows[i].suffix).c_str());
        }
    }

    // Linea inferior del popup. Si la barra no existe, aqui se ve el motivo (en rojo) en vez de
    // quedarse sin barra y sin saber por que.
    void refreshActive() {
        if (!RhythmBar::instance()) {
            m_activeLabel->setString(("Barra no creada: " + asciiOnly(g_barStatus, 38)).c_str());
            m_activeLabel->setColor(kColRed);
            return;
        }
        const auto name = asciiOnly(pathToUtf8(utf8ToPath(loadConfig().macroFile).filename()), 40);
        m_activeLabel->setString(("Activa: " + name).c_str());
        m_activeLabel->setColor(kColWhite);
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

        const auto cfg = loadConfig();
        const auto activePath = findMacro(cfg.macroFile, false);  // sin recorrer carpetas
        const auto activeKey = activePath ? pathKey(pathToUtf8(*activePath)) : pathKey(cfg.macroFile);

        if (m_library.empty()) {
            m_listNodes.push_back(this->addLabel("Sin macros guardadas", kRightCX, kRowY0 - kRowDY, 0.35f));
        }
        for (size_t k = 0; k < kPerPage; ++k) {
            const size_t i = m_page * kPerPage + k;
            if (i >= m_library.size()) break;
            // 17 caracteres entran dentro del boton (se recorta con "..." si es mas largo)
            const auto name = asciiOnly(pathToUtf8(utf8ToPath(m_library[i]).filename()), 17);
            const bool active = pathKey(m_library[i]) == activeKey;  // verde = macro activa
            const float y = kRowY0 - kRowDY * static_cast<float>(k);
            m_listNodes.push_back(this->addButton(name.c_str(), 160, active, kRightCX, y,
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
        if (tag < 0 || tag >= static_cast<int>(kRowCount) * 2) return;
        const auto& row = kRows[tag / 2];
        const int64_t dir = (tag % 2) ? 1 : -1;
        const int64_t v = std::clamp<int64_t>(rowValue(row) + dir * row.step, row.lo, row.hi);
        setRowValue(row, v);
        this->afterSettingsChanged();  // la barra se redibuja ya con el nuevo largo / grosor / velocidad
    }

    void onReset(CCObject*) {
        for (auto const& row : kRows) setRowValue(row, row.def);
        this->afterSettingsChanged();
    }

    void onPage(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 0 && m_page > 0) --m_page;
        else if (tag == 1) ++m_page;  // rebuildList lo limita a la ultima pagina
        this->rebuildList();
    }

    // Modo Experimento. Si la barra todavia no existe se intenta crear ahora; si no se puede,
    // el modo queda apagado y se avisa el motivo (antes no pasaba nada y no se sabia por que).
    void onExperiment(CCObject*) {
        const bool want = !g_experiment;
        if (want && !ensureBar(PlayLayer::get())) {
            g_experiment = false;
            Notification::create("Modo Experimento: la barra no esta activa (" + asciiOnly(g_barStatus, 40) + ")",
                                 NotificationIcon::Error)->show();
        } else {
            g_experiment = want;
            RhythmBar::applyExperimentNow();
            Notification::create(g_experiment ? "Modo Experimento: ACTIVADO" : "Modo Experimento: DESACTIVADO",
                                 g_experiment ? NotificationIcon::Success : NotificationIcon::Info)->show();
        }
        this->refreshActive();

        // El toggler cambia su aspecto solo, y lo hace en un orden que no controlamos respecto a este
        // callback: un instante despues se vuelve a poner igual al estado real del modo.
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.05f),
            CCCallFunc::create(this, callfunc_selector(RhythmPopup::syncExperimentToggle)), nullptr));
    }

    void syncExperimentToggle() {
        if (m_expToggle) m_expToggle->toggle(g_experiment);
    }

    // Busca la macro por nombre, la valida, la agrega a la biblioteca y la activa al instante.
    // Con el campo vacio registra la macro que esta activa ahora.
    void onAdd(CCObject*) {
        std::string name = m_input ? rpb::trim(m_input->getString()) : std::string();
        const auto cfg = loadConfig();
        if (name.empty()) name = cfg.macroFile;

        const auto found = findMacro(name, true, true);
        if (!found) {
            Notification::create("Rhythm Bar: no se encontro \"" + asciiOnly(name, 32) + "\"",
                                 NotificationIcon::Error)->show();
            return;
        }

        std::string text;
        if (!readFile(*found, text)) {
            Notification::create("Rhythm Bar: no se pudo leer el archivo", NotificationIcon::Error)->show();
            return;
        }
        rpb::ParseInfo info;
        const auto macro = rpb::parseMacro(text, cfg.parser, info);
        if (!info.error.empty() || macro.presses.empty()) {
            const std::string why = info.error.empty() ? std::string("la macro no tiene clics") : info.error;
            Notification::create("Rhythm Bar: " + why, NotificationIcon::Warning)->show();
            return;
        }

        auto list = loadLibrary();
        const auto stored = pathToUtf8(*found);
        const auto key = pathKey(stored);
        size_t index = list.size();
        for (size_t i = 0; i < list.size(); ++i) {
            if (pathKey(list[i]) == key) {
                index = i;
                break;
            }
        }
        if (index == list.size()) {
            list.push_back(stored);
            if (!saveLibrary(list)) {
                Notification::create("Rhythm Bar: no se pudo guardar la biblioteca",
                                     NotificationIcon::Error)->show();
                return;
            }
        }
        m_page = index / kPerPage;

        setMacroFile(stored);
        RhythmBar::refreshNow();
        if (m_input) m_input->setString("");
        Notification::create("Rhythm Bar: macro cargada (" +
                                 asciiOnly(pathToUtf8(found->filename()), 30) + ")",
                             NotificationIcon::Success)->show();
        this->rebuildList();
    }

    // Saca la macro activa de la biblioteca (no borra el archivo).
    void onRemove(CCObject*) {
        const auto cfg = loadConfig();
        const auto activePath = findMacro(cfg.macroFile, false);
        const auto activeKey = activePath ? pathKey(pathToUtf8(*activePath)) : pathKey(cfg.macroFile);

        auto list = loadLibrary();
        const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](std::string const& e) { return pathKey(e) == activeKey; }),
                   list.end());
        if (list.size() == before) {
            Notification::create("Rhythm Bar: la macro activa no esta en la biblioteca",
                                 NotificationIcon::Info)->show();
            return;
        }
        if (!saveLibrary(list)) {
            Notification::create("Rhythm Bar: no se pudo guardar la biblioteca", NotificationIcon::Error)->show();
            return;
        }
        Notification::create("Rhythm Bar: macro quitada de la biblioteca", NotificationIcon::Success)->show();
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
        setMacroFile(stored);
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

// ------------------------------------------- colocacion en el menu de pausa ----
// Rectangulo de un nodo expresado en las coordenadas de `space`.
CCRect rectIn(CCNode* space, CCNode* n) {
    const CCRect bb = n->boundingBox();  // en el espacio del padre
    CCNode* parent = n->getParent();
    if (!parent) return bb;
    const CCPoint a = space->convertToNodeSpace(
        parent->convertToWorldSpace(ccp(bb.origin.x, bb.origin.y)));
    const CCPoint b = space->convertToNodeSpace(
        parent->convertToWorldSpace(ccp(bb.origin.x + bb.size.width, bb.origin.y + bb.size.height)));
    return CCRect(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
}

bool rectsOverlap(CCRect const& a, CCRect const& b) {
    return a.origin.x < b.origin.x + b.size.width && b.origin.x < a.origin.x + a.size.width &&
           a.origin.y < b.origin.y + b.size.height && b.origin.y < a.origin.y + a.size.height;
}

// Junta los rectangulos ocupados por botones / textos / barras del menu de pausa.
// Los menus se recorren por dentro (su propio rectangulo ocupa toda la pantalla) y los
// nodos gigantes (fondos) se ignoran.
void collectBusyRects(CCNode* space, CCNode* root, CCSize const& win, std::vector<CCRect>& out,
                      int depth = 0) {
    auto children = root->getChildren();
    if (!children || depth > 2) return;
    for (auto child : CCArrayExt<CCNode*>(children)) {
        if (!child || !child->isVisible()) continue;
        if (typeinfo_cast<CCMenu*>(child) != nullptr) {
            collectBusyRects(space, child, win, out, depth + 1);
            continue;
        }
        const CCRect r = rectIn(space, child);
        if (r.size.width <= 1.f || r.size.height <= 1.f) continue;
        if (r.size.width >= win.width * 0.8f || r.size.height >= win.height * 0.8f) continue;
        out.push_back(r);
    }
}

}  // namespace

// ------------------------------------------------------------- hooks ----

class $modify(RhythmPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        const auto cfg = loadConfig();
        if (!cfg.enabled) {
            log::info("Rhythm Bar: desactivado (ajuste \"enabled\"), no se crea la barra");
            return true;
        }

        // La barra se crea aunque la macro falle: asi se puede elegir otra desde el popup.
        auto res = loadMacro(cfg);
        if (!res.ok) {
            Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
        }

        auto bar = RhythmBar::create(std::move(res.macro), cfg);
        if (!bar) {
            log::error("Rhythm Bar: no se pudo crear la barra");
            return true;
        }
        bar->setID("rhythm-bar"_spr);
        m_uiLayer->addChild(bar, 1000);

        // Diagnostico: si la barra no se ve, mira estas lineas en el log de Geode.
        const auto size = bar->barSize();
        const auto pos = bar->getPosition();
        log::info("Rhythm Bar: barra agregada a la UI ({}x{} en {},{}; visible={}; z={})", size.width,
                  size.height, pos.x, pos.y, bar->isVisible(), bar->getZOrder());
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
 * Menu de pausa: UN solo boton, "Ritmo" (abre el popup, donde tambien esta el Modo Experimento).
 * Se coloca en la primera esquina que no se superponga con nada del menu de pausa
 * (abajo-izquierda, abajo-derecha, arriba-derecha, arriba-izquierda), siempre dentro de la pantalla.
 */
class $modify(RhythmPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        const auto win = CCDirector::get()->getWinSize();

        auto spr = ButtonSprite::create("Ritmo", 54, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, nullptr, this,
                                                 menu_selector(RhythmPauseLayer::onRhythmMenu));
        btn->setID("rhythm-menu-button"_spr);

        const auto size = spr->getContentSize();
        const float margin = 8.f;
        const float w = size.width;
        const float h = size.height;

        const CCPoint candidates[] = {
            ccp(margin + w / 2.f, margin + h / 2.f),                              // abajo-izquierda
            ccp(win.width - margin - w / 2.f, margin + h / 2.f),                  // abajo-derecha
            ccp(win.width - margin - w / 2.f, win.height - margin - h / 2.f),     // arriba-derecha
            ccp(margin + w / 2.f, win.height - margin - h / 2.f),                 // arriba-izquierda
        };

        std::vector<CCRect> busy;
        collectBusyRects(this, this, win, busy);

        CCPoint chosen = candidates[0];
        for (auto const& c : candidates) {
            const float pad = 4.f;
            const CCRect mine(c.x - w / 2.f - pad, c.y - h / 2.f - pad, w + 2.f * pad, h + 2.f * pad);
            bool free = true;
            for (auto const& r : busy) {
                if (rectsOverlap(mine, r)) {
                    free = false;
                    break;
                }
            }
            if (free) {
                chosen = c;
                break;
            }
        }
        btn->setPosition(chosen);

        auto menu = CCMenu::create();
        menu->setID("rhythm-menu"_spr);
        menu->setPosition({0.f, 0.f});
        menu->addChild(btn);
        this->addChild(menu, 10);
    }

    void onRhythmMenu(CCObject*) {
        if (auto popup = RhythmPopup::create()) popup->show();
    }
};
