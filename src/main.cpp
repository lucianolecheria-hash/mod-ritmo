/*
 * Rhythm Practice Bar  v1.610  (luchx.barra-ritmo-final)
 * Barra estilo juego de ritmo para practicar el timing de clics a partir de una macro
 * de Mega Hack / .gdr2. Todo el dibujo de la barra usa rectangulos solidos (CCLayerColor).
 * Unica excepcion: el check verde del filtro noclip usa GJ_completes_001.png (si el sprite no
 * existe se dibuja un check con CCDrawNode). Sin reaparicion automatica ni StartPos virtual
 * (eso lo maneja el jugador a mano): el mod solo dibuja, juzga y muestra resultados.
 *
 * NOVEDADES v1.610 (Chain-Survival Link; reemplaza al "Filtro Noclip Inteligente" de v1.600)
 *  1) El veredicto de un clic ya NO es inmediato. El cartel y la mini-barra salen al instante,
 *     pero combo y precision solo se aplican cuando se cierra la "cadena" del clic: los cuadros de
 *     fisica desde que arranca (primer cuadro entre el pulsar del jugador y el de la macro) hasta el
 *     INICIO EXACTO del siguiente clic de la macro (el ultimo clic usa kChainTailSec).
 *  2) Si la hitbox real choco (PlayLayer::destroyPlayer) dentro de esa cadena, el clic se invalida y
 *     cuenta como un "Fallo!" real de muerte, incluso si habia salido "Excelente!". Un solo Fallo
 *     por clic (un hold no se castiga dos veces: pulsar + soltar).
 *  3) Temprano / Tarde sin choque: se muestra el texto con un tilde verde (GJ_completes_001.png),
 *     SUMAN a la racha del combo (no la reinician) y entran a la precision con su peso normal.
 *  4) "Fallo con tilde" (Fallo! sin choque en su cadena): el combo se mantiene y la precision no
 *     cambia; se cuenta aparte en "Salvados" de la pantalla de resultados.
 *  5) evaluarPrecisionFrame() existe ahora como funcion propia (RhythmBar::evaluarPrecisionFrame).
 *     Si el cartel original ya desaparecio cuando se cierra la cadena, el tilde o el "Fallo!" salen
 *     en un cartel secundario al costado (kEchoTag). Combo y estadisticas van un clic atrasados.
 *
 * NOVEDADES v1.600
 *  1) Interfaz del popup "Ritmo": etiquetas y flechas (-, +, <, >) mas chicas y alineadas en una
 *     sola grilla (columna de etiquetas / boton - / valor / boton +), "Direccion" incluida, con
 *     nombres de direccion cortos (Izq>Der...) para que no se pisen con las flechas. El boton de
 *     cerrar (X) se escala y se coloca DENTRO del marco marron (esquina superior izquierda, con
 *     margen) en el popup de ajustes y en el de resultados: ya no sobresale ni tapa nada.
 *  2) Calibracion frame-perfect. Causa raiz del "Perfecto+" de los bots: el cuadro del jugador se
 *     sacaba de llround(m_levelTime * fps) leido DENTRO de handleButton, y m_levelTime puede ir un
 *     paso adelantado respecto al contador que los bots escriben en la macro. Ese desfase
 *     constante de +1 cuadro afectaba solo al PULSAR; el SOLTAR se comparaba por duracion (resta de
 *     dos cuadros con el mismo error) y por eso el release ya salia "Excelente!". Ahora el cuadro
 *     sale del MISMO contador que usan los bots (GJGameState::m_currentProgress, ver frameNow()),
 *     con respaldo al tiempo del nivel si ese contador no concuerda (+-kStepTolerance). El
 *     juicio sigue siendo classifyFrames(): 0 cuadros = "Excelente!".
 *     Si en tu build un clic exacto de bot aun sale +-1, el unico ajuste es kInputFrameBias.
 *     En el log de Geode (nivel debug) cada pulsar escribe "cuadro / macro / dif".
 *  3) Macro Toggle: un segundo clic sobre la macro activa de la biblioteca la DESACTIVA al
 *     instante (se corta la lluvia de notas, los holds en vivo y las estadisticas). Borrar el
 *     texto del cuadro de macro tambien la desactiva, y "Quitar" desactiva la macro que saca de
 *     la biblioteca. Se guarda como valor "macro-active" (no hace falta tocar mod.json).
 *  4) Filtro Noclip Inteligente: REEMPLAZADO en v1.610 por el Chain-Survival Link (ver arriba).
 *     Se mantiene el enganche de PlayLayer::destroyPlayer con prioridad alta para verlo antes que
 *     un noclip que lo salte. Si tu noclip bloquea la colision antes de llegar ahi, ningun clic
 *     se vera como chocado y todos los Temprano / Tarde / Fallo! saldran con tilde.
 *
 * JUICIO POR FRAMES EXACTOS (usa los fps de la macro; 240 si el archivo no los trae):
 *   - Pulsar: frame del clic del jugador contra frame de la macro (juicio principal).
 *   - Soltar: SOLO en holds largos explicitos (duracion >= tap-threshold, 120 ms por defecto):
 *     se compara la DURACION del hold del jugador (en frames) con la de la macro.
 *
 *       0 frames de diferencia  -> "Excelente!"   (cian brillante)
 *       +-1 frame               -> "Perfecto+!"   (verde lima)
 *       +-2 a 3 frames          -> "Perfecto!"    (verde)
 *       mas de 3 frames         -> "Temprano" / "Tarde"  (con tilde verde si sobrevivio la cadena)
 *       sin pulsar / sin soltar -> "Fallo!"       (un solo Fallo por clic)
 *
 * - Combo ACUMULATIVO (RunStats::combo / RunStats::maxCombo): "Excelente!", "Perfecto+!" y
 *   "Perfecto!" suman; "Temprano" / "Tarde" CON tilde tambien suman; solo "Temprano", "Tarde" o
 *   "Fallo!" que chocaron (muerte en la cadena) lo reinician a 0. El "Fallo con tilde" lo deja igual.
 * - Pantalla de resultados estilo osu! (RhythmResultsPopup) con el mapa de calor / grafica de
 *   precision por tramo dibujada con CCDrawNode.
 * - Popup "Ritmo" (boton del menu de pausa) con ajustes en tiempo real: Largo, Grosor,
 *   Altura Barra, Fondo, Velocidad, Desfase (ms), Direccion, Modo Experimento, Ocultar Barra,
 *   Mini-Barra Superior y biblioteca de macros.
 *
 * Equivalencias de nombres (por si se buscan por el nombre del pedido):
 *   evaluarPrecisionFrame      -> RhythmBar::evaluarPrecisionFrame() (la llaman judgePress() /
 *                                 judgeRelease(); clasifica con classifyFrames() y encola el
 *                                 veredicto hasta que se cierra la cadena, ver resolveJudge())
 *   m_audioOffsetMs            -> ajuste "offset" (kOffsetDef / Config::offset, solo visual)
 *   m_currentCombo, m_maxCombo -> RunStats::combo, RunStats::maxCombo
 *   desplegarPantallaResultadosOsu -> RhythmResultsPopup (lo abre RhythmBar::openResults)
 *
 * Persistencia: cada ajuste se guarda con Mod::get()->setSettingValue cuando esta declarado en
 * mod.json (bar-width, bar-height, bg-opacity, offset, ...). Los que no esten declarados
 * (scroll-speed, bar-y, hide-bar, bar-dir, show-minibar, macro-active) se guardan como valor
 * guardado del mod, que tambien persiste.
 *
 * Formatos que lee parseMacro():
 *   - GDR2 binario (.gdr2, formato de maxnut/GDReplayFormat): se lee directo de los bytes
 *   - JSON de Mega Hack (.mhr.json, "Advanced Options > Export JSON")
 *   - JSON de otros bots que usan la extension .gdr2 (eventos con frame/down/button)
 *   - Texto plano: una linea por evento, "frame/tiempo + estado" (ver parseTextEvents)
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
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
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
    double tapThreshold = 0.12;  // segundos: soltar antes de esto = clic comun (sin juicio al soltar)
};

struct Hold {
    double start = 0.0;
    double end = 0.0;
    bool hold = false;  // true = hold largo explicito (duracion >= tapThreshold): se juzga tambien al soltar
};

struct Macro {
    std::vector<double> presses;  // inicio de cada clic de P1 (segundos)
    std::vector<Hold> holds;      // holds largos de P1
    std::vector<Hold> clicks;     // TODOS los clics de P1 (inicio, fin, si es hold largo): juicio por frames
    std::vector<Hold> clicks2;    // TODOS los clics de P2: solo se dibujan (morado claro), no se juzgan
};

struct ParseInfo {
    std::string format;
    size_t events = 0;    // eventos de P1
    size_t eventsP2 = 0;  // eventos de P2
    double fps = 0.0;
    std::string error;   // no vacio = no se pudo leer la macro (mensaje corto para el jugador)
    std::string detail;  // detalle tecnico para el log de Geode
};

enum class Unit { Frames, Seconds, Millis };

struct RawEvent {
    double value;
    Unit unit;
    bool down;
    bool p2;
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

    // Jugador 2: se conserva (marcado) para dibujarlo en morado claro.
    bool player2 = false;
    for (const char* key : {"p2", "player2", "2p"}) {
        if (jsonValue(obj, key, v) && lower(v) == "true") player2 = true;
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
    r.p2 = player2;
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
 *   150 down p2      (cualquier linea con "p2" / "2p" / "player2" es del jugador 2)
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

        bool player2 = false;
        for (size_t j = ti + 1; j < tk.size(); ++j) {
            const std::string w = lower(tk[j]);
            if (w == "p2" || w == "2p" || w == "player2") player2 = true;
        }

        int state = -1;
        for (size_t j = ti + 1; j < tk.size(); ++j) {
            const std::string w = lower(tk[j]);
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
        if (state < 0) continue;
        out.push_back({value, unit, state == 1, player2});
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
// Los inputs del bloque del jugador 2 (indice >= p1Count) se marcan con p2 = true.
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
        if (button == 1) {
            out.push_back({static_cast<double>(frame), Unit::Frames, down, i >= p1Count});
        }
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

// Empareja bajada / subida de un solo jugador en clics (inicio, fin, si es hold largo).
static void pairClicks(std::vector<ClickEvent> events, double tapThreshold, std::vector<Hold>& out) {
    std::stable_sort(events.begin(), events.end(),
                     [](const ClickEvent& a, const ClickEvent& b) { return a.time < b.time; });

    // Hold largo explicito: solo estos se juzgan tambien al soltar. Un clic comun (corto)
    // se juzga UNICAMENTE por el frame exacto de impacto (el Press).
    auto finish = [&](double t0, double t1) { out.push_back({t0, t1, (t1 - t0) >= tapThreshold}); };

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
}

static Macro buildMacro(std::vector<ClickEvent> p1, std::vector<ClickEvent> p2, double tapThreshold) {
    Macro m;
    pairClicks(std::move(p1), tapThreshold, m.clicks);
    pairClicks(std::move(p2), tapThreshold, m.clicks2);
    for (const auto& c : m.clicks) {
        m.presses.push_back(c.start);
        if (c.hold) m.holds.push_back(c);
    }
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

    std::vector<ClickEvent> ev1, ev2;
    ev1.reserve(raw.size());
    for (const auto& r : raw) {
        double t = r.value;
        if (r.unit == Unit::Frames) t = r.value / fps;
        else if (r.unit == Unit::Millis) t = r.value / 1000.0;
        (r.p2 ? ev2 : ev1).push_back({t, r.down});
    }

    info.events = ev1.size();
    info.eventsP2 = ev2.size();
    info.fps = fps;
    return buildMacro(std::move(ev1), std::move(ev2), st.tapThreshold);
}

}  // namespace rpb
// === PARSER END ===

namespace fs = std::filesystem;

// ------------------------------------------------------------ config ----

namespace {

// Ventana exterior: fuera de esto el clic no cuenta y la nota se da por perdida.
// Dentro de la ventana el juicio se hace SIEMPRE en frames enteros (ver classifyFrames).
constexpr double kOuterWin = 0.250;
constexpr double kZoneWin = 0.050;  // solo visual: zona sombreada alrededor del receptor

// Escala de frames de diferencia.
constexpr int kPerfectPlusFrames = 1;  // +-1 -> Perfecto+!
constexpr int kPerfectFrames = 3;      // +-2..3 -> Perfecto!

// Ajuste fino (en cuadros) del cuadro del jugador, DESPUES de usar el contador de pasos de los
// bots (ver RhythmBar::frameNow). 0 = un clic exacto de bot da 0 cuadros de diferencia. Si en tu
// build siempre sale +1 / -1, pon -1 / +1 aqui (es el unico punto donde se toca el cuadro).
constexpr int kInputFrameBias = 0;

// frameNow(): se usa el contador de pasos del juego si difiere del cuadro por tiempo en menos
// de esto; si no (otro tps, checkpoint raro) se usa el cuadro por tiempo.
constexpr int kStepTolerance = 2;

// Filtro noclip / Chain-Survival Link.
constexpr int kLethalIgnoreFrames = 3;    // ignora colisiones de los primeros cuadros (anticheat)
constexpr int32_t kLethalHookPriority = -100000;  // bajo = el hook corre antes que un noclip

// Chain-Survival Link: la cadena de un clic va desde su pulsar hasta el INICIO EXACTO del
// siguiente clic de la macro.
constexpr double kChainTailSec = 1.0;  // ultimo clic de la macro (no hay siguiente): cuanto se espera
constexpr int kChainMinFrames = 2;     // la ventana nunca mide menos que esto
constexpr int kEchoTag = 7732;         // cartel secundario (tilde / invalidacion tardia)

constexpr int kMicroRange = 8;       // la mini-barra muestra +-8 frames
constexpr float kMicroH = 10.f;      // alto de la mini-barra
constexpr float kTopExtra = 84.f;    // espacio sobre la barra para mini-barra, combo y cartel
constexpr float kTopExtraLite = 48.f;  // idem con la mini-barra oculta (solo cartel y combo)

constexpr float kReceptorFrac = 0.85f;  // posicion del receptor dentro de la barra (0..1)
constexpr float kNoteWidth = 6.f;       // grosor de un contenedor (nota) a lo largo del recorrido
constexpr size_t kMaxSlots = 512;
constexpr size_t kMaxTrail = 64;
constexpr int kResultsTag = 7731;
constexpr float kResultsDelay = 2.6f;  // segundos hasta mostrar los resultados (sobre la pantalla de fin)

// Velocidad de desplazamiento de las notas (en %). 100% = el "lookahead" del mod.json.
constexpr int64_t kSpeedMin = 20;
constexpr int64_t kSpeedMax = 400;
constexpr int64_t kSpeedDef = 100;

// Desfase VISUAL por defecto, en ms. Solo mueve el dibujo de las notas: NO entra al juicio.
constexpr int64_t kOffsetDef = 0;

// Direccion en la que corren las notas.
enum class BarDir : int { LeftToRight = 0, RightToLeft = 1, TopToBottom = 2, BottomToTop = 3 };

// Nombres cortos (sin espacios) para que quepan entre las flechas del popup.
char const* const kDirNames[4] = {"Izq>Der", "Der>Izq", "Arr>Aba", "Aba>Arr"};

const ccColor3B kColWhite{255, 255, 255};
const ccColor3B kColGreen{60, 255, 100};
const ccColor3B kColRed{255, 70, 70};
const ccColor3B kColLime{180, 255, 40};
const ccColor3B kColBrightCyan{40, 240, 255};
const ccColor3B kColGold{255, 220, 90};
const ccColor3B kColOrange{255, 170, 50};
const ccColor3B kColPurple{190, 130, 255};
const ccColor3B kColLightPurple{205, 165, 255};  // notas y holds del 2do jugador
const ccColor3B kColGray{190, 190, 200};

// Modo Experimento: solo vive en esta sesion del juego (se apaga al reiniciar GD).
bool g_experiment = false;

// Ultimo estado de la creacion de la barra ("ok" o el motivo por el que no existe).
std::string g_barStatus = "sin iniciar";

struct Config {
    bool enabled = true;
    bool hidden = false;        // "hide-bar": Ocultar Barra
    bool showMiniBar = true;    // "show-minibar": mini-barra de micro-frames
    bool macroActive = true;    // "macro-active": false = macro desactivada (toggle de la biblioteca)
    int dir = 0;                // "bar-dir": ver BarDir
    std::string macroFile = "macro.txt";
    float barWidthPct = 60.f;   // "bar-width": largo de la barra, % del ancho (o del alto si es vertical)
    float barThickness = 44.f;  // "bar-height": grosor de la barra en px
    float barY = 0.f;           // "bar-y": altura de la barra sobre el borde inferior, px
    double lookahead = 1.5;     // segundos que tarda una nota en llegar al receptor (ya con la velocidad aplicada)
    double offset = static_cast<double>(kOffsetDef) / 1000.0;  // "offset": desfase VISUAL en segundos
    float bgOpacity = 0.35f;    // "bg-opacity": 0..1
    rpb::ParserSettings parser;
};

// Lectura tolerante de ajustes. getSettingValue<T>() de Geode devuelve en silencio 0 / false / ""
// cuando el ajuste NO esta declarado en mod.json: aqui, si no existe, se usa el valor guardado
// del mod (o un valor por defecto). Escribir usa setSettingValue si el ajuste existe.
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
    if (mod->hasSetting(key)) return mod->getSettingValue<bool>(key);
    return mod->getSavedValue<bool>(key, def);
}

void setBoolSetting(char const* key, bool v) {
    auto mod = Mod::get();
    if (mod->hasSetting(key)) {
        (void)mod->setSettingValue<bool>(key, v);
    } else {
        (void)mod->setSavedValue<bool>(key, v);
    }
}

// Migracion unica a v1.50: el desfase por defecto de versiones anteriores era -5 ms. Como ahora
// es solo visual, si el jugador nunca lo cambio (sigue en -5) se pasa al nuevo valor por defecto.
void ensureDefaultOffset() {
    auto mod = Mod::get();
    if (mod->getSavedValue<bool>("offset-v150-migrated", false)) return;
    (void)mod->setSavedValue<bool>("offset-v150-migrated", true);
    if (intSetting("offset", kOffsetDef) == -5) setIntSetting("offset", kOffsetDef);
}

bool hideBarSetting() {
    return boolSetting("hide-bar", false);
}

void setHideBarSetting(bool v) {
    setBoolSetting("hide-bar", v);
}

bool miniBarSetting() {
    return boolSetting("show-minibar", true);
}

void setMiniBarSetting(bool v) {
    setBoolSetting("show-minibar", v);
}

// Macro Toggle: false = hay una macro elegida pero esta desactivada (la barra no dibuja ni juzga).
bool macroActiveSetting() {
    return boolSetting("macro-active", true);
}

void setMacroActiveSetting(bool v) {
    setBoolSetting("macro-active", v);
}

int dirSetting() {
    return static_cast<int>(std::clamp<int64_t>(intSetting("bar-dir", 0), 0, 3));
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
    c.hidden = hideBarSetting();
    c.showMiniBar = miniBarSetting();
    c.macroActive = macroActiveSetting();
    c.dir = dirSetting();
    c.macroFile = macroFileSetting();
    c.barWidthPct = static_cast<float>(std::clamp<int64_t>(intSetting("bar-width", 60), 10, 100));
    c.barThickness = static_cast<float>(std::clamp<int64_t>(intSetting("bar-height", 44), 16, 200));
    c.barY = static_cast<float>(std::clamp<int64_t>(intSetting("bar-y", 0), 0, 400));
    c.bgOpacity = static_cast<float>(std::clamp<int64_t>(intSetting("bg-opacity", 35), 0, 100)) / 100.f;
    // Velocidad: mas % = la nota recorre la barra en menos tiempo = notas mas separadas en pantalla.
    const double speed = static_cast<double>(std::clamp<int64_t>(
                             intSetting("scroll-speed", kSpeedDef), kSpeedMin, kSpeedMax)) /
                         100.0;
    double base = doubleSetting("lookahead", 1.5);
    if (base <= 0.0) base = 1.5;
    const double baseLookahead = std::max(0.1, base);
    c.lookahead = std::max(0.05, baseLookahead / speed);
    // Desfase en ms (se edita desde el popup). SOLO VISUAL: no entra al juicio por frames.
    c.offset = static_cast<double>(std::clamp<int64_t>(intSetting("offset", kOffsetDef), -1000, 1000)) / 1000.0;
    c.parser.tapThreshold =
        static_cast<double>(std::clamp<int64_t>(intSetting("tap-threshold", 120), 10, 1000)) / 1000.0;
    c.parser.fps = std::max(0.0, doubleSetting("macro-fps", 0.0));
    return c;
}

// Filas del popup con botones "-" / "+". Si la clave esta en mod.json se usa ese ajuste
// (setSettingValue); si no, se guarda como valor guardado del mod. Ambos persisten.
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
    {"Altura Barra", "bar-y", 0, 260, 5, 0, "px"},
    {"Fondo", "bg-opacity", 0, 100, 5, 35, "%"},
    {"Velocidad", "scroll-speed", kSpeedMin, kSpeedMax, 10, kSpeedDef, "%"},
    {"Desfase", "offset", -200, 200, 5, kOffsetDef, "ms"},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

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

// La clave incluye si la macro esta activa: activar / desactivar cuenta como "macro cambiada".
std::string macroKey(Config const& c) {
    const std::string file = c.macroActive ? c.macroFile : std::string("<ninguna>");
    return file + "|" + std::to_string(c.parser.fps) + "|" + std::to_string(c.parser.tapThreshold);
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
    double fps = 240.0;  // fps de la macro: base del juicio por frames
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
    log::info("Rhythm Bar: {} -> formato {}, {} eventos P1, {} eventos P2, {} clics, {} holds largos, {} fps",
              shown, info.format, info.events, info.eventsP2, macro.presses.size(), macro.holds.size(),
              info.fps);

    if (macro.presses.empty()) {
        r.error = "no se encontraron clics en la macro";
        r.icon = NotificationIcon::Warning;
        return r;
    }
    r.macro = std::move(macro);
    r.fps = info.fps > 0.0 ? info.fps : 240.0;
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

// Clave de la macro elegida en los ajustes (sin recorrer carpetas). Sirve para marcarla en la
// biblioteca, para el toggle del segundo clic y para "Quitar".
std::string activeKeyOf(Config const& cfg) {
    const auto p = findMacro(cfg.macroFile, false);
    return p ? pathKey(pathToUtf8(*p)) : pathKey(cfg.macroFile);
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

// Contador de pasos de fisica del juego: es el "frame" que los bots escriben en la macro.
// Si tu version de los bindings lo llama distinto, este es el unico lugar donde cambiarlo.
int physicsStep(PlayLayer* pl) {
    return static_cast<int>(pl->m_gameState.m_currentProgress);
}

// Porcentaje actual del nivel (0..100): es el eje X de la grafica de la pantalla de resultados.
float currentLevelPercent(PlayLayer* pl) {
    if (!pl) return 0.f;
    const float p = pl->getCurrentPercent();
    if (!std::isfinite(p)) return 0.f;
    return std::clamp(p, 0.f, 100.f);
}

// ------------------------------------------------------------ juicio ----

enum class Judge : uint8_t { Excellent, PerfectPlus, Perfect, Early, Late, Miss };

constexpr size_t kJudgeCount = 6;

constexpr size_t idx(Judge j) {
    return static_cast<size_t>(j);
}

// Peso de cada juicio en el porcentaje de precision (en %).
constexpr double kJudgeWeight[kJudgeCount] = {100.0, 98.0, 90.0, 50.0, 50.0, 0.0};

struct JudgeStyle {
    char const* text;
    ccColor3B color;
};

JudgeStyle styleOf(Judge j) {
    switch (j) {
        case Judge::Excellent: return {"Excelente!", kColBrightCyan};
        case Judge::PerfectPlus: return {"Perfecto+!", kColLime};
        case Judge::Perfect: return {"Perfecto!", kColGreen};
        case Judge::Early: return {"Temprano", kColOrange};
        case Judge::Late: return {"Tarde", kColPurple};
        case Judge::Miss: break;
    }
    return {"Fallo!", kColRed};
}

// d = frames del jugador - frames de la macro (negativo = el jugador se adelanto).
Judge classifyFrames(int d) {
    const int a = std::abs(d);
    if (a == 0) return Judge::Excellent;
    if (a <= kPerfectPlusFrames) return Judge::PerfectPlus;
    if (a <= kPerfectFrames) return Judge::Perfect;
    return d < 0 ? Judge::Early : Judge::Late;
}

// Acierto limpio: suma al combo y NO lo rompe. Temprano / Tarde / Fallo! lo cortan
// (salvo que sobrevivan la cadena: ver RunStats::addSaved / addSavedMiss).
constexpr bool isCleanJudge(Judge j) {
    return j == Judge::Excellent || j == Judge::PerfectPlus || j == Judge::Perfect;
}

// Un juicio con el porcentaje del nivel donde ocurrio (datos de la grafica de resultados).
struct JudgeSample {
    float pct;  // 0..100
    Judge j;
};

constexpr size_t kMaxSamples = 20000;

// Estadisticas de la run actual.
struct RunStats {
    uint32_t counts[kJudgeCount] = {};
    uint32_t combo = 0;     // aciertos consecutivos (limpios + Temprano / Tarde con tilde)
    uint32_t maxCombo = 0;  // pico mas alto de esa racha
    uint32_t savedEarly = 0;  // Temprano con tilde: YA esta dentro de counts[] (cuenta en precision)
    uint32_t savedLate = 0;   // Tarde con tilde: idem
    uint32_t savedMiss = 0;   // Fallo con tilde: NO esta en counts[] (no toca la precision)
    std::vector<JudgeSample> samples;

    void reset() { *this = RunStats{}; }

    // Juicios que cuentan para la precision.
    uint32_t total() const {
        uint32_t n = 0;
        for (auto c : counts) n += c;
        return n;
    }

    uint32_t survived() const { return savedEarly + savedLate + savedMiss; }

    // Todo lo que paso en la run (el Fallo con tilde no esta en counts[]).
    uint32_t shown() const { return total() + savedMiss; }

    // Porcentaje de precision 0..100. El Fallo con tilde queda FUERA del promedio.
    double accuracy() const {
        const uint32_t n = total();
        if (n == 0) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < kJudgeCount; ++i) sum += static_cast<double>(counts[i]) * kJudgeWeight[i];
        return sum / static_cast<double>(n);
    }

    void add(Judge j, float pct) {
        ++counts[idx(j)];
        if (isCleanJudge(j)) {
            ++combo;  // el combo se acumula entre categorias limpias
            maxCombo = std::max(maxCombo, combo);
        } else {
            combo = 0;  // Temprano, Tarde y Fallo! (con muerte en la cadena) lo cortan por completo
        }
        if (samples.size() < kMaxSamples) samples.push_back({pct, j});
    }

    // Temprano / Tarde que sobrevivio hasta el siguiente clic: SUMA a la racha (no la rompe)
    // y entra a la precision con su peso normal.
    void addSaved(Judge j, float pct) {
        ++counts[idx(j)];
        if (j == Judge::Early) ++savedEarly;
        else ++savedLate;
        ++combo;
        maxCombo = std::max(maxCombo, combo);
        if (samples.size() < kMaxSamples) samples.push_back({pct, j});
    }

    // Fallo con tilde: el combo queda igual y la precision no se altera.
    void addSavedMiss() { ++savedMiss; }
};

struct GradeInfo {
    char const* text;
    ccColor3B color;
};

GradeInfo gradeOf(RunStats const& s) {
    if (s.total() == 0) return {"-", kColGray};
    const double a = s.accuracy();
    if (a >= 100.0) return {"SS", kColBrightCyan};
    if (a >= 97.0) return {"S", kColGold};
    if (a >= 93.0) return {"A", kColGreen};
    if (a >= 85.0) return {"B", ccColor3B{90, 160, 255}};
    if (a >= 70.0) return {"C", kColOrange};
    return {"D", kColRed};
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

// Ajusta la escala de un texto para que no pase de maxWidth (en px).
void fitLabel(CCLabelBMFont* label, float baseScale, float maxWidth) {
    if (!label) return;
    label->setScale(baseScale);
    const float w = label->getContentSize().width * baseScale;
    if (w > maxWidth && w > 0.f) label->setScale(baseScale * maxWidth / w);
}

// ------------------------------------------------------ base de popups ----
// Ayudantes comunes a los popups del mod. Todas las posiciones son (dx, dy) respecto al centro
// del fondo del popup, sin depender de como este colocado m_mainLayer / m_buttonMenu.

class RhythmPopupBase : public geode::Popup {
protected:
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

    // Boton de cerrar (X): mas chico y DENTRO del marco marron, en la esquina superior izquierda,
    // con margen. Por defecto Geode lo deja asomando por fuera de la esquina. Se llama justo
    // despues de Popup::init con el tamano del popup.
    void tidyCloseButton(float w, float h) {
        if (!m_closeBtn) return;
        m_closeBtn->setScale(0.62f);
        m_closeBtn->setPosition(this->at(m_buttonMenu, -w / 2.f + 16.f, h / 2.f - 16.f));
    }
};

// ------------------------------------------- grafica de precision por tramo ----
// El nivel (0..100%) se divide en kGraphBins tramos. La precision de cada tramo es el promedio
// ponderado (kJudgeWeight) de los juicios que ocurrieron dentro de ese tramo.

constexpr size_t kGraphBins = 50;

struct GraphBin {
    uint32_t n = 0;       // juicios en este tramo
    uint32_t misses = 0;  // cuantos fueron Fallo!
    double sum = 0.0;     // suma de pesos

    double acc() const { return n ? sum / static_cast<double>(n) : 0.0; }
};

struct GraphData {
    GraphBin bins[kGraphBins];
};

GraphData buildGraph(RunStats const& s) {
    GraphData g;
    for (auto const& sm : s.samples) {
        const float p = std::clamp(sm.pct, 0.f, 100.f);
        size_t b = static_cast<size_t>(p / 100.f * static_cast<float>(kGraphBins));
        if (b >= kGraphBins) b = kGraphBins - 1;
        auto& bin = g.bins[b];
        ++bin.n;
        bin.sum += kJudgeWeight[idx(sm.j)];
        if (sm.j == Judge::Miss) ++bin.misses;
    }
    return g;
}

ccColor3B lerpColor(ccColor3B a, ccColor3B b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return ccColor3B{static_cast<GLubyte>(static_cast<float>(a.r) + (static_cast<float>(b.r) - a.r) * t),
                     static_cast<GLubyte>(static_cast<float>(a.g) + (static_cast<float>(b.g) - a.g) * t),
                     static_cast<GLubyte>(static_cast<float>(a.b) + (static_cast<float>(b.b) - a.b) * t)};
}

// Color segun la precision: 0 rojo -> 55 naranja -> 90 verde -> 100 cian.
ccColor3B heatColor(double acc) {
    const float a = static_cast<float>(std::clamp(acc, 0.0, 100.0));
    if (a >= 90.f) return lerpColor(kColGreen, kColBrightCyan, (a - 90.f) / 10.f);
    if (a >= 55.f) return lerpColor(kColOrange, kColGreen, (a - 55.f) / 35.f);
    return lerpColor(kColRed, kColOrange, a / 55.f);
}

// ccColor4F con alpha premultiplicado (CCDrawNode mezcla con GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
ccColor4F drawColor(ccColor3B c, float a) {
    return ccc4f(static_cast<float>(c.r) / 255.f * a, static_cast<float>(c.g) / 255.f * a,
                 static_cast<float>(c.b) / 255.f * a, a);
}

// Check verde de los aciertos "salvados" (filtro noclip). Usa GJ_completes_001.png; si el sprite
// no existe (o Geode devolvio su textura de reemplazo) se dibuja un check con CCDrawNode.
// Devuelve un contenedor centrado en (0, 0) que mide aprox. `size` px.
CCNode* makeCheckMark(float size) {
    auto holder = CCNode::create();
    if (auto spr = CCSprite::createWithSpriteFrameName("GJ_completes_001.png")) {
        const auto cs = spr->getContentSize();
        if (!spr->getUserObject("geode.texture-loader/fallback") && cs.width > 1.f && cs.height > 1.f) {
            spr->setScale(size / std::max(cs.width, cs.height));
            holder->addChild(spr);
            return holder;
        }
    }
    auto draw = CCDrawNode::create();
    const float r = size * 0.09f;
    const ccColor4F col = drawColor(kColGreen, 1.f);
    draw->drawSegment(ccp(-0.50f * size, 0.00f * size), ccp(-0.15f * size, -0.40f * size), r, col);
    draw->drawSegment(ccp(-0.15f * size, -0.40f * size), ccp(0.50f * size, 0.45f * size), r, col);
    holder->addChild(draw);
    return holder;
}

// ------------------------------------------- pantalla de resultados (osu!) ----
// Popup 440 x 300 (cabe tambien en pantallas 4:3). Posiciones (dx, dy) desde el centro:
//   cabecera:  nota + precision total (izquierda), combo maximo (derecha)
//   fila:      conteo exacto de cada juicio (6 columnas)
//   grafica:   precision por tramo del nivel + tira de calor
//   pie:       total / salvados / macro + boton OK

class RhythmResultsPopup : public RhythmPopupBase {
protected:
    static constexpr float kW = 440.f;
    static constexpr float kH = 300.f;

    void addStat(Judge j, uint32_t count, float cx) {
        const auto st = styleOf(j);
        auto name = this->addLabel(st.text, cx, 60.f, 0.3f, CCPoint{0.5f, 0.5f}, st.color);
        fitLabel(name, 0.3f, 66.f);
        auto value = this->addLabel(std::to_string(count), cx, 46.f, 0.5f, CCPoint{0.5f, 0.5f}, kColWhite);
        fitLabel(value, 0.5f, 60.f);
    }

    // Grafica de linea horizontal con CCDrawNode:
    //   X = porcentaje del nivel (0..100), Y = precision de los clics en ese tramo (0..100).
    //   Tramo limpio (puros Excelente!) -> linea arriba en cian/verde.
    //   Fallo! o imprecisiones -> la curva se desploma y vira a naranja / rojo.
    void addGraph(RunStats const& s) {
        constexpr float panelL = -204.f, panelR = 204.f, panelT = 30.f, panelB = -72.f;
        constexpr float plotL = -178.f, plotR = 190.f, plotT = 14.f, plotB = -46.f;
        constexpr float plotW = plotR - plotL;
        constexpr float plotH = plotT - plotB;
        constexpr float stripT = -52.f, stripB = -58.f;  // tira de calor bajo la grafica

        auto panel = makeSolid(ccColor4B{0, 0, 0, 110}, panelR - panelL, panelT - panelB);
        panel->setPosition(this->at(m_mainLayer, panelL, panelB));
        m_mainLayer->addChild(panel, 2);

        // todos los puntos del CCDrawNode se expresan en el espacio de m_mainLayer
        const CCPoint o = this->at(m_mainLayer, 0.f, 0.f);
        auto P = [o](float dx, float dy) { return ccp(o.x + dx, o.y + dy); };

        auto draw = CCDrawNode::create();
        m_mainLayer->addChild(draw, 4);

        // cuadricula: Y en 0 / 50 / 100, X cada 25% del nivel
        for (float f : {0.f, 0.5f, 1.f}) {
            const float y = plotB + plotH * f;
            draw->drawSegment(P(plotL, y), P(plotR, y), 0.5f, drawColor(kColWhite, 0.18f));
        }
        for (int k = 0; k <= 4; ++k) {
            const float x = plotL + plotW * static_cast<float>(k) / 4.f;
            draw->drawSegment(P(x, plotB), P(x, plotT), 0.4f, drawColor(kColWhite, 0.10f));
        }

        const GraphData g = buildGraph(s);
        const float cell = plotW / static_cast<float>(kGraphBins);
        auto binX = [&](size_t i) { return plotL + cell * (static_cast<float>(i) + 0.5f); };
        auto binY = [&](double acc) { return plotB + plotH * static_cast<float>(acc / 100.0); };

        // tira de calor: un bloque de color por tramo (gris tenue = sin clics en ese tramo)
        for (size_t i = 0; i < kGraphBins; ++i) {
            const float x0 = plotL + cell * static_cast<float>(i);
            const float x1 = x0 + cell;
            CCPoint v[4] = {P(x0, stripB), P(x1, stripB), P(x1, stripT), P(x0, stripT)};
            auto const& b = g.bins[i];
            const ccColor4F col = b.n ? drawColor(heatColor(b.acc()), 0.9f) : drawColor(kColGray, 0.15f);
            draw->drawPolygon(v, 4, col, 0.f, ccc4f(0.f, 0.f, 0.f, 0.f));
        }

        // marcas rojas abajo donde hubo al menos un Fallo!
        for (size_t i = 0; i < kGraphBins; ++i) {
            if (g.bins[i].misses == 0) continue;
            draw->drawSegment(P(binX(i), plotB), P(binX(i), plotB + 7.f), 0.9f, drawColor(kColRed, 0.9f));
        }

        // la linea: segmentos entre tramos con datos, con degradado de color segun la precision.
        // Si hay tramos vacios entre dos puntos (sin clics) el segmento se dibuja mas fino y tenue.
        size_t prev = kGraphBins;
        constexpr int kSteps = 6;
        for (size_t i = 0; i < kGraphBins; ++i) {
            auto const& b = g.bins[i];
            if (b.n == 0) continue;
            if (prev < kGraphBins) {
                auto const& a = g.bins[prev];
                const bool gap = prev + 1 != i;
                const float x0 = binX(prev), y0 = binY(a.acc());
                const float x1 = binX(i), y1 = binY(b.acc());
                for (int k = 0; k < kSteps; ++k) {
                    const float t0 = static_cast<float>(k) / static_cast<float>(kSteps);
                    const float t1 = static_cast<float>(k + 1) / static_cast<float>(kSteps);
                    const double v = a.acc() + (b.acc() - a.acc()) * static_cast<double>((t0 + t1) * 0.5f);
                    draw->drawSegment(P(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0),
                                      P(x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1),
                                      gap ? 0.7f : 1.3f, drawColor(heatColor(v), gap ? 0.45f : 1.f));
                }
            }
            prev = i;
        }
        for (size_t i = 0; i < kGraphBins; ++i) {
            auto const& b = g.bins[i];
            if (b.n == 0) continue;
            draw->drawDot(P(binX(i), binY(b.acc())), 1.8f, drawColor(heatColor(b.acc()), 1.f));
        }

        // textos: titulo, peor tramo y ejes
        const CCPoint center{0.5f, 0.5f};
        fitLabel(this->addLabel("Precision por tramo del nivel", panelL + 6.f, 24.f, 0.26f,
                                CCPoint{0.f, 0.5f}, kColGold),
                 0.26f, 200.f);

        size_t worst = kGraphBins;
        double worstAcc = 101.0;
        for (size_t i = 0; i < kGraphBins; ++i) {
            if (g.bins[i].n > 0 && g.bins[i].acc() < worstAcc) {
                worst = i;
                worstAcc = g.bins[i].acc();
            }
        }
        if (worst < kGraphBins && worstAcc < 99.95) {
            char wb[48];
            std::snprintf(wb, sizeof(wb), "Peor tramo: %d-%d%%",
                          static_cast<int>(worst * 100 / kGraphBins),
                          static_cast<int>((worst + 1) * 100 / kGraphBins));
            fitLabel(this->addLabel(wb, panelR - 6.f, 24.f, 0.26f, CCPoint{1.f, 0.5f}, heatColor(worstAcc)),
                     0.26f, 150.f);
        }

        const CCPoint right{1.f, 0.5f};
        this->addLabel("100", plotL - 3.f, plotT, 0.22f, right, kColGray);
        this->addLabel("50", plotL - 3.f, plotB + plotH * 0.5f, 0.22f, right, kColGray);
        this->addLabel("0", plotL - 3.f, plotB, 0.22f, right, kColGray);

        static char const* const kXLabels[] = {"0%", "25%", "50%", "75%", "100%"};
        for (int k = 0; k <= 4; ++k) {
            this->addLabel(kXLabels[k], plotL + plotW * static_cast<float>(k) / 4.f, -66.f, 0.22f, center,
                           kColGray);
        }
    }

    bool init(RunStats const& s, std::string const& macroName) {
        if (!Popup::init(kW, kH)) return false;
        this->setTitle("Resultados");
        this->tidyCloseButton(kW, kH);

        const CCPoint center{0.5f, 0.5f};

        // cabecera: nota (SS / S / A / B / C / D) + precision total
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f%%", s.accuracy());
        const auto grade = gradeOf(s);
        auto gl = this->addLabel(grade.text, -172.f, 92.f, 1.3f, center, grade.color);
        fitLabel(gl, 1.3f, 64.f);
        auto acc = this->addLabel(buf, -64.f, 98.f, 0.95f, center, kColWhite);
        fitLabel(acc, 0.95f, 150.f);
        this->addLabel("Precision total", -64.f, 76.f, 0.3f, center, kColGold);

        // combo maximo de aciertos limpios (Excelente! / Perfecto+! / Perfecto!)
        this->addLabel("Combo Max", 150.f, 102.f, 0.3f, center, kColGold);
        auto cm = this->addLabel(std::to_string(s.maxCombo), 150.f, 84.f, 0.8f, center, kColBrightCyan);
        fitLabel(cm, 0.8f, 90.f);

        // conteo exacto de cada juicio
        const Judge order[] = {Judge::Excellent, Judge::PerfectPlus, Judge::Perfect,
                               Judge::Early,     Judge::Late,        Judge::Miss};
        for (size_t k = 0; k < kJudgeCount; ++k) {
            this->addStat(order[k], s.counts[idx(order[k])], -175.f + 70.f * static_cast<float>(k));
        }

        // grafica de precision por tramo del nivel
        this->addGraph(s);

        // pie: los salvados (Temprano / Tarde / Fallo con tilde) se cuentan aparte
        std::string line = "Total: " + std::to_string(s.total()) + " juicios";
        if (s.survived() > 0) line += "   Salvados: " + std::to_string(s.survived());
        line += "   Macro: " + macroName;
        auto info = this->addLabel(line, 0.f, -88.f, 0.28f, center, kColGray);
        fitLabel(info, 0.28f, 390.f);

        this->addButton("OK", 80, true, 0.f, -116.f, menu_selector(RhythmResultsPopup::onOk), 0, 28.f, 0.7f);
        return true;
    }

    void onOk(CCObject*) {
        this->setKeypadEnabled(false);
        this->removeFromParentAndCleanup(true);
    }

public:
    static RhythmResultsPopup* create(RunStats const& s, std::string const& macroName) {
        auto ret = new (std::nothrow) RhythmResultsPopup();
        if (ret && ret->init(s, macroName)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ------------------------------------------------------------ barra ----
//
// Geometria: la barra se calcula SIEMPRE en un eje "de recorrido" s (0 = borde por donde entran
// las notas, m_len = borde de salida) y un eje transversal c (0..m_thick). box() convierte ese
// par de rangos a un rectangulo (x, y, w, h) segun la direccion elegida, asi el dibujo es el
// mismo para las cuatro direcciones.

class RhythmBar : public CCNode {
protected:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    // Un clic de la macro (P1): pulsar y, SOLO si es un hold largo explicito, soltar.
    //   pulsar -> cuadro del clic contra cuadro de la macro (siempre)
    //   soltar -> duracion del hold del jugador (cuadros) contra la de la macro
    //             (solo holds largos; en un clic comun el soltar no se juzga)
    struct Click {
        double start = 0.0;  // segundos (solo para dibujar)
        double end = 0.0;
        int sF = 0;          // cuadro de bajada de la macro
        int eF = 0;          // cuadro de subida de la macro
        int pF = 0;          // cuadro en que el jugador pulso (si acerto)
        bool hasRel = false; // true = hold largo explicito: el soltar tambien se juzga
        uint8_t press = NotePending;
        uint8_t release = NoteSkipped;
        bool dead = false;   // la cadena de este clic ya murio (un solo Fallo por clic)
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

    struct Box {
        float x, y, w, h;
    };

    // Un juicio esperando su turno para entrar a las estadisticas (Chain-Survival Link). Los
    // juicios se aplican SIEMPRE en orden y solo cuando se cierra la cadena de su clic (hasta el
    // inicio exacto del siguiente clic de la macro), o antes si ya se vio una colision letal.
    struct QueuedJudge {
        Judge j = Judge::Miss;
        float pct = 0.f;
        size_t click = kNone;   // clic de la macro al que pertenece
        int lo = 0;             // cadena: [lo, hi] en cuadros
        int hi = 0;
        bool lethal = false;    // ya se vio una colision letal dentro de la cadena
        Ref<CCNode> box;        // cartel en pantalla (para el tilde / la invalidacion)
        Ref<CCLabelBMFont> label;
    };

    // Como se recorta / ancla una barra de hold respecto al receptor.
    enum class BarMode { Free, Clip, Pin };

    inline static RhythmBar* s_instance = nullptr;

    rpb::Macro m_macro;
    std::vector<Click> m_clicks;
    double m_fps = 240.0;
    int m_outer = 60;           // ventana exterior en cuadros
    int m_lethalKeep = 600;     // cuadros de historial de colisiones que se conservan
    size_t m_pCursor = 0;       // primer clic cuyo "pulsar" aun no expiro
    size_t m_rCursor = 0;       // primer clic con algo sin resolver
    size_t m_active = kNone;    // clic de la macro que el jugador esta sosteniendo (acertado)
    RunStats m_stats;
    bool m_macroOn = false;     // hay una macro cargada y activa (false = desactivada / sin clics)
    bool m_finished = false;    // nivel completado: se congelan juicios y estadisticas
    Config m_cfg;
    std::string m_macroKey;

    // filtro noclip / Chain-Survival Link
    std::deque<QueuedJudge> m_queue;  // juicios pendientes de entrar a m_stats
    std::vector<int> m_lethal;        // cuadros (ascendentes) con colision letal de la hitbox real

    // geometria actual
    BarDir m_dir = BarDir::LeftToRight;
    bool m_vertical = false;
    bool m_miniOn = true;
    float m_len = 0.f;      // largo a lo largo del recorrido
    float m_thick = 0.f;    // grosor
    float m_w = 0.f;        // tamano real del contenedor
    float m_h = 0.f;
    float m_recS = 0.f;     // posicion del receptor sobre el eje de recorrido
    float m_hudX = 0.f;     // X (local) donde se centran mini-barra y cartel de juicio
    float m_textY = 0.f;    // Y (local) del cartel de juicio
    float m_cell = 8.f;     // ancho de 1 frame en la mini-barra

    // Rectangulos solidos fijos: se crean una sola vez (buildStatic) y se redimensionan (applyLayout).
    CCLayerColor* m_bg = nullptr;      // fondo de la barra
    CCLayerColor* m_zone = nullptr;    // zona sombreada alrededor del receptor
    CCLayerColor* m_edge[4] = {};      // borde: abajo, arriba, izquierda, derecha
    CCLayerColor* m_flashG = nullptr;  // destello verde (presionar)
    CCLayerColor* m_line = nullptr;    // linea receptora

    // Mini-barra de micro-frames (sobre la barra)
    CCLayerColor* m_microBg = nullptr;
    CCLayerColor* m_microG = nullptr;  // +-3 frames
    CCLayerColor* m_microL = nullptr;  // +-1 frame
    CCLayerColor* m_microC = nullptr;  // 0 frames (exacto)
    CCLayerColor* m_markP = nullptr;   // marcador del frame del clic (mitad superior)
    CCLayerColor* m_markH = nullptr;   // marcador del error del hold (mitad inferior)

    CCNode* m_layer = nullptr;         // contenedor de notas / holds / huella
    CCNode* m_box = nullptr;           // contenedor del cartel de juicio (sube) + check de salvado
    CCLabelBMFont* m_label = nullptr;  // cartel de juicio (hijo de m_box)
    CCLabelBMFont* m_combo = nullptr;  // contador de combo de aciertos limpios
    std::vector<CCLayerColor*> m_pool;
    size_t m_used = 0;

    std::vector<TrailEntry> m_trail;   // historial de clics del jugador (tiempo visual)
    bool m_synced = false;
    int m_lastFrame = 0;               // ultimo cuadro visto por el juicio
    double m_lastVis = 0.0;            // ultimo tiempo visual dibujado
    double m_clock = 0.0;
    double m_labelDieAt = 0.0;
    float m_pollTimer = 0.f;

    // Modo Experimento
    CCLayerColor* m_blackout = nullptr;  // pantalla negra detras de la UI (la posee m_uiLayer)
    std::vector<HiddenNode> m_hidden;    // nodos del nivel ocultados, con su visibilidad original
    bool m_levelHidden = false;

    bool init(rpb::Macro macro, double fps, Config const& cfg) {
        if (!CCNode::init()) return false;
        m_cfg = cfg;
        m_macroKey = macroKey(cfg);
        this->buildStatic();
        this->setMacro(std::move(macro), fps);
        this->applyLayout();
        this->setVisible(cfg.enabled && !cfg.hidden);
        this->scheduleUpdate();
        s_instance = this;
        return true;
    }

public:
    static RhythmBar* create(rpb::Macro macro, double fps, Config const& cfg) {
        auto ret = new (std::nothrow) RhythmBar();
        if (ret && ret->init(std::move(macro), fps, cfg)) {
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
                              c.barThickness != m_cfg.barThickness || c.barY != m_cfg.barY ||
                              c.bgOpacity != m_cfg.bgOpacity || c.lookahead != m_cfg.lookahead ||
                              c.dir != m_cfg.dir || c.showMiniBar != m_cfg.showMiniBar;
        const bool redraw = relayout || c.offset != m_cfg.offset;
        const auto key = macroKey(c);
        const bool macroChanged = key != m_macroKey;

        m_cfg = c;
        this->setVisible(c.enabled && !c.hidden);  // "Ocultar Barra"
        if (relayout) this->applyLayout();         // solo setContentSize / setPosition de los rectangulos

        if (macroChanged) {
            m_macroKey = key;
            if (!c.macroActive) {
                // Macro Toggle: desactivada -> se corta la lluvia de notas y se limpian los holds en vivo
                this->clearMacro();
            } else {
                auto res = loadMacro(c);
                if (res.ok) {
                    this->setMacro(std::move(res.macro), res.fps);
                } else {
                    Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
                }
            }
        }

        // Redibuja ya mismo (el juego puede estar en pausa y no llamar a update).
        if (c.enabled && (redraw || macroChanged)) this->refresh(this->currentNow());
        this->syncLevelVisibility();
    }

    // Entrada del jugador (P1, salto) tal como la entrega el juego en cada paso de fisica.
    //
    // PASO DE TIEMPO: el juicio usa el cuadro entero de fisica en el que el juego APLICA el clic
    // (frameNow: el mismo contador que usan los bots al escribir la macro), sin Desfase ni
    // ninguna correccion en milisegundos. El Desfase visual solo entra al dibujo (rastro del
    // jugador), nunca al cuadro que se compara con la macro.
    void onInput(bool down, PlayLayer* pl) {
        if (!m_cfg.enabled || m_finished || !m_macroOn || !pl) return;
        const int frame = this->frameNow(pl);                      // juicio
        const double tv = currentLevelTime(pl) - m_cfg.offset;     // dibujo
        if (!m_synced) this->resync(frame, true, false);

        if (down) {
            this->flashDown();
            m_trail.push_back({tv, tv, true});
            if (m_trail.size() > kMaxTrail) m_trail.erase(m_trail.begin());
            this->judgePress(frame);
        } else {
            this->flashUp();
            if (!m_trail.empty() && m_trail.back().open) {
                m_trail.back().end = tv;
                m_trail.back().open = false;
            }
            this->judgeRelease(frame);
        }
    }

    // Colision letal de la hitbox real (PlayLayer::destroyPlayer), la haya bloqueado un noclip o no.
    // Se guarda el cuadro y se marcan como "muertos" los juicios en vuelo cuya cadena lo contiene:
    // un clic con una colision dentro de su cadena se invalida (Fallo! real).
    void onLethal(PlayLayer* pl) {
        if (!m_cfg.enabled || !m_macroOn || m_finished || !pl) return;
        const int f = this->frameNow(pl);
        if (f < kLethalIgnoreFrames) return;
        if (!m_lethal.empty() && f < m_lethal.back()) m_lethal.clear();  // el tiempo retrocedio
        if (m_lethal.empty() || m_lethal.back() != f) m_lethal.push_back(f);

        // historial acotado por antiguedad
        const auto old = std::lower_bound(m_lethal.begin(), m_lethal.end(), f - m_lethalKeep);
        m_lethal.erase(m_lethal.begin(), old);

        // cadenas en vuelo: si el cuadro cae dentro de su tramo, el veredicto ya es muerte
        for (auto& q : m_queue) {
            if (f >= q.lo && f <= q.hi) q.lethal = true;
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

        if (m_box && m_clock >= m_labelDieAt) this->clearJudgmentBox();
        if (!m_cfg.enabled || !m_macroOn) return;

        const double levelTime = currentLevelTime(pl);
        // juicio: cuadros enteros, sin Desfase
        if (!m_finished) this->advanceJudge(this->frameNow(pl));
        // dibujo: con Desfase visual (positivo = las notas llegan mas tarde)
        const double nowV = levelTime - m_cfg.offset;
        m_lastVis = nowV;
        if (this->isVisible()) this->refresh(nowV);
    }

    // Nuevo intento (muerte / reinicio / checkpoint): se descongela el juicio.
    void onLevelReset() {
        m_finished = false;
        m_active = kNone;
        this->stopActionByTag(kResultsTag);
    }

    // PlayLayer::levelComplete: congela las estadisticas y abre los resultados por encima
    // de la pantalla de nivel completado.
    void onLevelComplete() {
        if (!m_cfg.enabled || m_finished || !m_macroOn) return;
        m_finished = true;
        m_active = kNone;
        this->drainQueue(m_lastFrame, true);  // las cadenas pendientes se resuelven ya
        if (m_stats.shown() == 0) return;

        this->stopActionByTag(kResultsTag);
        auto seq = CCSequence::create(CCDelayTime::create(kResultsDelay),
                                      CCCallFunc::create(this, callfunc_selector(RhythmBar::openResults)),
                                      nullptr);
        seq->setTag(kResultsTag);
        this->runAction(seq);
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
        return pl ? currentLevelTime(pl) - m_cfg.offset : m_lastVis;
    }

    // Porcentaje del nivel en este momento (eje X de la grafica de resultados).
    float percentNow() const {
        return currentLevelPercent(PlayLayer::get());
    }

    // Cuadro entero actual. Es el UNICO punto de conversion del juicio.
    //
    // Los bots escriben en la macro el contador de pasos de fisica (m_currentProgress). Leer en
    // cambio m_levelTime * fps dentro de handleButton daba un cuadro de mas en los pulsares
    // (el tiempo ya esta adelantado un paso), y por eso los bots salian "Perfecto+!". Se usa el
    // contador de pasos cuando concuerda con el tiempo del nivel; si no (otro tps, checkpoint,
    // fps de macro distintos) se vuelve al cuadro por tiempo.
    int frameNow(PlayLayer* pl) const {
        const int byTime = static_cast<int>(std::llround(currentLevelTime(pl) * m_fps));
        const int step = physicsStep(pl);
        const int f = std::abs(step - byTime) <= kStepTolerance ? step : byTime;
        return f + kInputFrameBias;
    }

    void setMacro(rpb::Macro macro, double fps) {
        m_macro = std::move(macro);
        m_fps = fps > 0.0 ? fps : 240.0;
        m_outer = std::max(1, static_cast<int>(std::llround(kOuterWin * m_fps)));

        m_clicks.clear();
        m_clicks.reserve(m_macro.clicks.size());
        for (auto const& h : m_macro.clicks) {
            Click c;
            c.start = h.start;
            c.end = h.end;
            c.sF = static_cast<int>(std::llround(h.start * m_fps));
            c.eF = static_cast<int>(std::llround(h.end * m_fps));
            c.hasRel = h.hold && c.eF > c.sF;  // solo holds largos explicitos se juzgan al soltar
            c.release = c.hasRel ? NotePending : NoteSkipped;
            m_clicks.push_back(c);
        }
        int longest = 0;
        for (auto const& c : m_clicks) longest = std::max(longest, c.eF - c.sF);
        m_lethalKeep = longest + 2 * m_outer + 8;  // cubre holds largos + ventana exterior
        m_macroOn = !m_clicks.empty();
        m_pCursor = 0;
        m_rCursor = 0;
        m_active = kNone;
        m_finished = false;
        m_queue.clear();
        m_lethal.clear();
        m_trail.clear();
        m_synced = false;
    }

    // Macro Toggle: deja la barra sin macro. Se corta la lluvia de notas, se limpian los holds en
    // vivo (rastro, destellos, cartel, combo, marcadores) y se borran las estadisticas.
    void clearMacro() {
        m_macro = rpb::Macro{};
        m_clicks.clear();
        m_pCursor = 0;
        m_rCursor = 0;
        m_active = kNone;
        m_macroOn = false;
        m_finished = false;
        m_synced = false;
        m_stats.reset();
        m_queue.clear();
        m_lethal.clear();
        m_trail.clear();
        this->stopActionByTag(kResultsTag);
        this->clearJudgmentBox();
        this->removeChildByTag(kEchoTag, true);

        for (auto n : m_pool) n->setVisible(false);
        m_used = 0;

        if (m_flashG) {
            m_flashG->stopAllActions();
            m_flashG->setOpacity(0);
        }
        if (m_line) {
            m_line->stopAllActions();
            m_line->setColor(kColWhite);
        }
        for (auto mk : {m_markP, m_markH}) {
            if (mk) {
                mk->stopAllActions();
                mk->setOpacity(0);
            }
        }
        this->updateComboLabel(false);  // combo en 0: se oculta
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

    // ---- geometria ----

    // Rango [s0, s1] sobre el eje de recorrido y [c0, c1] sobre el transversal -> rectangulo local.
    Box box(float s0, float s1, float c0, float c1) const {
        if (s1 < s0) std::swap(s0, s1);
        if (c1 < c0) std::swap(c0, c1);
        const float ds = s1 - s0;
        const float dc = c1 - c0;
        switch (m_dir) {
            case BarDir::RightToLeft: return {m_len - s1, c0, ds, dc};
            case BarDir::BottomToTop: return {c0, s0, dc, ds};
            case BarDir::TopToBottom: return {c0, m_len - s1, dc, ds};
            case BarDir::LeftToRight: break;
        }
        return {s0, c0, ds, dc};
    }

    void place(CCLayerColor* node, Box const& b) {
        resizeSolid(node, b.w, b.h, b.x, b.y);
    }

    // Posicion s de un instante t cuando "ahora" es `now`: el receptor esta en m_recS y las notas
    // futuras entran desde s = 0. px por segundo = m_recS / lookahead, o sea que la velocidad del
    // popup multiplica directamente cuanto se desplazan las notas por segundo.
    float sAt(double t, double now) const {
        const double speed = m_recS / m_cfg.lookahead;  // px por segundo
        return static_cast<float>(m_recS - (t - now) * speed);
    }

    // Segundos que tarda una nota en salir por el borde de salida despues de cruzar el receptor.
    double tailSeconds() const {
        return (m_len - m_recS) * m_cfg.lookahead / std::max(m_recS, 1.f);
    }

    // Crea los rectangulos solidos UNA sola vez. El tamano real lo pone applyLayout().
    void buildStatic() {
        m_bg = makeSolid(ccColor4B{14, 16, 30, 90}, 1.f, 1.f);
        this->addChild(m_bg, 0);

        m_zone = makeSolid(ccColor4B{255, 255, 255, 28}, 1.f, 1.f);
        this->addChild(m_zone, 0);

        m_layer = CCNode::create();
        this->addChild(m_layer, 1);

        // destello verde del receptor (al presionar). Ya no hay destello celeste.
        m_flashG = makeSolid(ccColor4B{60, 255, 100, 0}, 1.f, 1.f);
        this->addChild(m_flashG, 2);

        // mini-barra de micro-frames: fondo + zonas (+-3 verde, +-1 lima, 0 cian) + 2 marcadores
        m_microBg = makeSolid(ccColor4B{10, 12, 22, 175}, 1.f, 1.f);
        this->addChild(m_microBg, 4);
        m_microG = makeSolid(ccColor4B{60, 255, 100, 110}, 1.f, 1.f);
        this->addChild(m_microG, 4);
        m_microL = makeSolid(ccColor4B{180, 255, 40, 160}, 1.f, 1.f);
        this->addChild(m_microL, 4);
        m_microC = makeSolid(ccColor4B{40, 240, 255, 235}, 1.f, 1.f);
        this->addChild(m_microC, 4);
        m_markP = makeSolid(ccColor4B{255, 255, 255, 0}, 1.f, 1.f);
        this->addChild(m_markP, 7);
        m_markH = makeSolid(ccColor4B{255, 230, 90, 0}, 1.f, 1.f);
        this->addChild(m_markH, 7);

        // borde de la barra: 4 franjas finas, siempre visibles aunque el fondo sea transparente
        for (auto& e : m_edge) {
            e = makeSolid(ccColor4B{140, 210, 255, 220}, 1.f, 1.f);
            this->addChild(e, 5);
        }

        // linea receptora (timing line): sobresale un poco por los dos lados
        m_line = makeSolid(ccColor4B{255, 255, 255, 255}, 1.f, 1.f);
        this->addChild(m_line, 6);

        // contador de combo de aciertos limpios
        m_combo = CCLabelBMFont::create("Combo x0", "bigFont.fnt");
        m_combo->setAnchorPoint({0.f, 0.5f});
        m_combo->setColor(kColBrightCyan);
        m_combo->setScale(0.5f);
        m_combo->setVisible(false);
        this->addChild(m_combo, 9);
    }

    // Aplica largo / grosor / altura / fondo / velocidad / direccion actuales con
    // setContentSize + setPosition.
    void applyLayout() {
        const auto win = CCDirector::get()->getWinSize();
        const float margin = 10.f;

        m_dir = static_cast<BarDir>(std::clamp(m_cfg.dir, 0, 3));
        m_vertical = m_dir == BarDir::TopToBottom || m_dir == BarDir::BottomToTop;
        m_miniOn = m_cfg.showMiniBar;
        const float topExtra = m_miniOn ? kTopExtra : kTopExtraLite;

        m_thick = std::max(16.f, m_cfg.barThickness);
        if (m_vertical) {
            // en vertical el "Largo" es un % del ALTO de la pantalla (sin pasarse por arriba)
            const float maxLen = std::max(40.f, win.height - 2.f * margin - topExtra);
            m_len = std::max(40.f, std::min(win.height * m_cfg.barWidthPct / 100.f, maxLen));
            m_w = m_thick;
            m_h = m_len;
        } else {
            m_len = std::max(40.f, std::min(win.width * m_cfg.barWidthPct / 100.f, win.width - 2.f * margin));
            m_w = m_len;
            m_h = m_thick;
        }
        m_recS = m_len * kReceptorFrac;

        // Altura Barra: sube toda la estructura, sin dejar que se salga por arriba.
        const float maxY = std::max(0.f, win.height - margin - m_h - topExtra);
        const float y = margin + std::clamp(m_cfg.barY, 0.f, maxY);

        this->setContentSize({m_w, m_h});
        this->setPosition({(win.width - m_w) / 2.f, y});

        // fondo solido
        if (m_bg) {
            m_bg->setOpacity(static_cast<GLubyte>(std::clamp(m_cfg.bgOpacity, 0.f, 1.f) * 255.f));
        }
        resizeSolid(m_bg, m_w, m_h, 0.f, 0.f);

        // zona sombreada alrededor del receptor (referencia visual), sin salirse de la barra
        const float half = static_cast<float>(kZoneWin * m_recS / m_cfg.lookahead);
        this->place(m_zone, this->box(std::max(0.f, m_recS - half), std::min(m_len, m_recS + half), 0.f, m_thick));

        this->place(m_flashG, this->box(std::max(0.f, m_recS - 17.f), std::min(m_len, m_recS + 17.f), 0.f, m_thick));

        const float b = 2.f;
        resizeSolid(m_edge[0], m_w, b, 0.f, 0.f);          // abajo
        resizeSolid(m_edge[1], m_w, b, 0.f, m_h - b);      // arriba
        resizeSolid(m_edge[2], b, m_h, 0.f, 0.f);          // izquierda
        resizeSolid(m_edge[3], b, m_h, m_w - b, 0.f);      // derecha

        this->place(m_line, this->box(m_recS - 2.f, m_recS + 2.f, -5.f, m_thick + 5.f));

        // centro horizontal del HUD (mini-barra + cartel): el receptor si la barra es horizontal,
        // el centro de la barra si es vertical
        const Box rb = this->box(m_recS - 1.f, m_recS + 1.f, 0.f, m_thick);
        m_hudX = rb.x + rb.w * 0.5f;

        // mini-barra de micro-frames, centrada sobre m_hudX y por encima de la barra
        m_cell = std::clamp(m_len / 24.f, 3.f, 8.f);
        const float my = m_h + 4.f;
        const float halfMicro = (static_cast<float>(kMicroRange) + 0.5f) * m_cell;
        resizeSolid(m_microBg, 2.f * halfMicro, kMicroH, m_hudX - halfMicro, my);
        const float g = (static_cast<float>(kPerfectFrames) + 0.5f) * m_cell;
        resizeSolid(m_microG, 2.f * g, kMicroH, m_hudX - g, my);
        const float l = (static_cast<float>(kPerfectPlusFrames) + 0.5f) * m_cell;
        resizeSolid(m_microL, 2.f * l, kMicroH, m_hudX - l, my);
        resizeSolid(m_microC, m_cell, kMicroH, m_hudX - m_cell / 2.f, my);
        for (auto mk : {m_markP, m_markH}) {
            if (mk) {
                mk->stopAllActions();
                mk->setOpacity(0);
            }
        }
        // casilla "Mini-Barra Superior": setVisible(false) limpia por completo la mini-barra
        for (auto n : {m_microBg, m_microG, m_microL, m_microC, m_markP, m_markH}) {
            if (n) n->setVisible(m_miniOn);
        }

        // cartel de juicio y combo (suben o bajan segun haya mini-barra)
        m_textY = my + (m_miniOn ? kMicroH + 14.f : 10.f);
        if (m_combo) {
            if (m_vertical) {
                // al costado de la barra, a la altura del receptor
                m_combo->setAnchorPoint({0.f, 0.5f});
                m_combo->setPosition({m_w + 10.f, rb.y + rb.h * 0.5f});
            } else if (m_dir == BarDir::RightToLeft) {
                // el receptor queda a la izquierda: el combo va al otro lado
                m_combo->setAnchorPoint({1.f, 0.5f});
                m_combo->setPosition({m_w - 4.f, m_textY - 2.f});
            } else {
                m_combo->setAnchorPoint({0.f, 0.5f});
                m_combo->setPosition({4.f, m_textY - 2.f});
            }
        }
    }

    // ---- destellos ----

    void flashDown() {
        if (m_flashG) {
            m_flashG->stopAllActions();
            m_flashG->setOpacity(255);
            m_flashG->runAction(CCFadeTo::create(0.10f, 110));  // queda un brillo mientras se mantiene
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
        if (m_line) {
            m_line->stopAllActions();
            m_line->runAction(CCTintTo::create(0.25f, 255, 255, 255));  // vuelve a blanco (sin celeste)
        }
    }

    // ---- mini-barra y combo ----

    // Marca en la mini-barra cuantos frames de diferencia hubo (negativo = temprano, a la izquierda;
    // en "Der > Izq" se refleja para que "temprano" quede del lado de donde vienen las notas).
    // upper = true: frame del clic (blanco); false: error del hold al soltar (dorado).
    void placeMarker(CCLayerColor* mark, int frames, bool upper) {
        if (!mark) return;
        const float sign = m_dir == BarDir::RightToLeft ? -1.f : 1.f;
        const int e = std::clamp(frames, -kMicroRange, kMicroRange);
        const float cx = m_hudX + sign * static_cast<float>(e) * m_cell;
        const float mw = 3.f;
        const float halfH = kMicroH / 2.f;
        const float my = m_h + 4.f;
        const float y = upper ? my + halfH : my - 3.f;
        resizeSolid(mark, mw, halfH + 3.f, cx - mw / 2.f, y);
        mark->stopAllActions();
        mark->setOpacity(255);
        mark->runAction(CCSequence::create(CCDelayTime::create(0.7f), CCFadeTo::create(0.4f, 90), nullptr));
    }

    void updateComboLabel(bool pulse) {
        if (!m_combo) return;
        if (m_stats.combo == 0) {
            m_combo->setVisible(false);
            return;
        }
        char buf[40];
        std::snprintf(buf, sizeof(buf), "Combo x%u", static_cast<unsigned>(m_stats.combo));
        m_combo->setString(buf);
        m_combo->setVisible(true);
        if (pulse) {
            m_combo->stopAllActions();
            m_combo->setScale(0.66f);
            m_combo->runAction(CCEaseOut::create(CCScaleTo::create(0.14f, 0.5f), 2.f));
        }
    }

    // ---- juicio ----

    void clearJudgmentBox() {
        if (m_box) m_box->removeFromParent();
        m_box = nullptr;
        m_label = nullptr;
    }

    // El cartel vive dentro de un contenedor (m_box) que es el que sube: asi el check de un
    // acierto salvado, que se agrega unos cuadros despues, sube pegado al texto.
    void spawnJudgment(Judge j) {
        const auto st = styleOf(j);
        this->clearJudgmentBox();

        m_box = CCNode::create();
        m_box->setPosition({m_hudX, m_textY});
        this->addChild(m_box, 10);

        m_label = CCLabelBMFont::create(st.text, "bigFont.fnt");
        m_label->setColor(st.color);
        m_label->setScale(0.3f);
        m_box->addChild(m_label);

        CCFiniteTimeAction* grow = CCEaseBackOut::create(CCScaleTo::create(0.14f, 0.55f));
        CCFiniteTimeAction* fade = CCSequence::create(CCDelayTime::create(0.40f),
                                                      CCFadeOut::create(0.30f), nullptr);
        m_label->runAction(CCSpawn::create(grow, fade, nullptr));
        m_box->runAction(CCEaseOut::create(CCMoveBy::create(0.70f, {0.f, 26.f}), 2.f));
        m_labelDieAt = m_clock + 0.75;
    }

    // Check verde al lado de un Temprano / Tarde salvado. Solo si ese cartel sigue en pantalla.
    void attachCheck(CCNode* box, CCLabelBMFont* label) {
        if (!box || !label || box != m_box || !box->getParent()) return;

        auto holder = CCNode::create();
        holder->addChild(makeCheckMark(15.f));
        const float halfW = label->getContentSize().width * 0.55f * 0.5f;  // escala final del cartel
        holder->setPosition({halfW + 12.f, 0.f});
        holder->setScale(0.f);
        const float life = static_cast<float>(std::max(0.0, m_labelDieAt - m_clock - 0.25));
        holder->runAction(CCSequence::create(CCEaseBackOut::create(CCScaleTo::create(0.12f, 1.f)),
                                             CCDelayTime::create(life), CCScaleTo::create(0.2f, 0.f),
                                             nullptr));
        box->addChild(holder, 2);
    }

    // Una colision letal de la hitbox real cae dentro de [lo, hi]?
    bool lethalInWindow(int lo, int hi) const {
        const auto it = std::lower_bound(m_lethal.begin(), m_lethal.end(), lo);
        return it != m_lethal.end() && *it <= hi;
    }

    // ---- Chain-Survival Link ----

    // Cadena de un clic: cuadros de fisica desde que arranca hasta el inicio EXACTO del siguiente clic
    // de la macro (el ultimo clic usa kChainTailSec). Arranca en el primer cuadro entre el pulsar del
    // jugador y el de la macro: un "Tarde" choca ANTES de que termine el clic y se perderia.
    void chainWindow(size_t ci, int& lo, int& hi) const {
        auto const& c = m_clicks[ci];
        lo = (c.press == NoteHit) ? std::min(c.pF, c.sF) : c.sF;
        hi = (ci + 1 < m_clicks.size())
                 ? m_clicks[ci + 1].sF
                 : std::max(c.sF, c.eF) + static_cast<int>(std::llround(kChainTailSec * m_fps));
        hi = std::max(hi, lo + kChainMinFrames);
    }

    void onEchoDone(CCNode* node) {
        if (node) node->removeFromParentAndCleanup(true);
    }

    // Cartel secundario, para cuando el cartel original ya desaparecio al resolverse la cadena.
    void spawnEcho(Judge j, bool check) {
        const auto st = styleOf(j);
        this->removeChildByTag(kEchoTag, true);

        auto holder = CCNode::create();
        holder->setTag(kEchoTag);
        const float side = m_dir == BarDir::RightToLeft ? 1.f : -1.f;  // lado opuesto al combo
        holder->setPosition({m_hudX + side * 72.f, m_textY});
        holder->setScale(0.f);
        this->addChild(holder, 10);

        auto label = CCLabelBMFont::create(st.text, "bigFont.fnt");
        label->setColor(st.color);
        label->setScale(0.42f);
        holder->addChild(label);
        if (check) {
            auto mark = CCNode::create();
            mark->addChild(makeCheckMark(14.f));  // GJ_completes_001.png (o el check de CCDrawNode)
            mark->setPosition({label->getContentSize().width * 0.42f * 0.5f + 12.f, 0.f});
            holder->addChild(mark);
        }

        CCFiniteTimeAction* pop = CCEaseBackOut::create(CCScaleTo::create(0.12f, 1.f));
        CCFiniteTimeAction* rise = CCEaseOut::create(CCMoveBy::create(0.60f, {0.f, 14.f}), 2.f);
        holder->runAction(CCSequence::create(
            CCSpawn::create(pop, rise, nullptr), CCDelayTime::create(0.30f), CCScaleTo::create(0.15f, 0.f),
            CCCallFuncN::create(this, callfuncN_selector(RhythmBar::onEchoDone)), nullptr));
    }

    bool boxStillShown(QueuedJudge const& q) {
        return q.box.data() && q.box.data() == m_box && m_box->getParent() && q.label.data();
    }

    // Tilde verde: junto al cartel original si sigue en pantalla; si no, en un cartel secundario.
    void showCheck(QueuedJudge& q) {
        if (this->boxStillShown(q)) this->attachCheck(m_box, q.label.data());
        else this->spawnEcho(q.j, true);
    }

    // Clic invalidado por muerte: el cartel pasa a "Fallo!" en rojo.
    void markDead(QueuedJudge& q) {
        if (this->boxStillShown(q)) {
            const auto st = styleOf(Judge::Miss);
            q.label->setString(st.text);
            q.label->setColor(st.color);
        } else {
            this->spawnEcho(Judge::Miss, false);
        }
    }

    // Veredicto definitivo de un juicio cuando su cadena se cerro (o ya se vio la muerte).
    //   muerte en la cadena                -> Fallo! real (invalida incluso un Excelente!)
    //   Temprano / Tarde sin muerte        -> tilde, SUMA al combo, cuenta en precision
    //   Fallo! sin muerte (Fallo con tilde)-> combo y precision intactos, con tilde
    //   limpio sin muerte                  -> normal
    void resolveJudge(QueuedJudge& q, bool& pulse) {
        const uint32_t before = m_stats.combo;

        // un solo veredicto de muerte por clic: lo que llegue despues se descarta
        if (q.click < m_clicks.size()) {
            auto& c = m_clicks[q.click];
            if (c.dead) return;
            if (q.lethal) c.dead = true;
        }

        if (q.lethal) {
            if (q.j != Judge::Miss) this->markDead(q);
            m_stats.add(Judge::Miss, q.pct);
        } else if (q.j == Judge::Miss) {
            m_stats.addSavedMiss();
            this->showCheck(q);
        } else if (q.j == Judge::Early || q.j == Judge::Late) {
            m_stats.addSaved(q.j, q.pct);
            this->showCheck(q);
        } else {
            m_stats.add(q.j, q.pct);
        }
        if (m_stats.combo > before) pulse = true;
    }

    // Aplica EN ORDEN lo que ya se puede resolver. Un juicio vivo espera a que pase su tramo
    // (nowF > hi); uno con muerte vista no espera. force = resolver todo con lo visto hasta ahora.
    void drainQueue(int nowF, bool force) {
        bool changed = false;
        bool pulse = false;
        while (!m_queue.empty()) {
            auto& q = m_queue.front();
            if (!q.lethal && !force && nowF <= q.hi) break;
            this->resolveJudge(q, pulse);
            changed = true;
            m_queue.pop_front();
        }
        if (changed) this->updateComboLabel(pulse);
    }

    // Fallo por expiracion. No drena: advanceJudge le liga el cartel y luego drena.
    void queueMiss(size_t ci, float pct) {
        QueuedJudge q;
        q.j = Judge::Miss;
        q.pct = pct;
        q.click = ci;
        this->chainWindow(ci, q.lo, q.hi);
        q.lethal = this->lethalInWindow(q.lo, q.hi);
        m_queue.push_back(std::move(q));
    }

    // evaluarPrecisionFrame: clasifica por cuadros (d = jugador - macro) y ENCOLA el veredicto.
    // Cartel y mini-barra salen al instante, pero nada toca combo ni precision hasta que se
    // cierra la cadena del clic (resolveJudge).
    void evaluarPrecisionFrame(size_t ci, int d, bool isPress, int curFrame) {
        const Judge j = classifyFrames(d);
        this->spawnJudgment(j);
        this->placeMarker(isPress ? m_markP : m_markH, d, isPress);

        QueuedJudge q;
        q.j = j;
        q.pct = this->percentNow();
        q.click = ci;
        this->chainWindow(ci, q.lo, q.hi);
        q.lethal = this->lethalInWindow(q.lo, q.hi);  // un Tarde puede haber chocado antes de pulsar
        q.box = Ref<CCNode>(m_box);
        q.label = Ref<CCLabelBMFont>(m_label);
        m_queue.push_back(std::move(q));
        this->drainQueue(curFrame, false);
    }

    // Pulsar: busca el clic pendiente mas cercano dentro de la ventana exterior y compara CUADROS
    // ENTEROS (cuadro del jugador - cuadro de la macro). Es el juicio principal.
    void judgePress(int f) {
        m_active = kNone;
        auto it = std::lower_bound(m_clicks.begin(), m_clicks.end(), f - m_outer,
                                   [](Click const& c, int v) { return c.sF < v; });
        size_t best = kNone;
        int bestAbs = 1 << 30;
        for (size_t i = static_cast<size_t>(it - m_clicks.begin());
             i < m_clicks.size() && m_clicks[i].sF <= f + m_outer; ++i) {
            if (m_clicks[i].press != NotePending) continue;
            const int a = std::abs(f - m_clicks[i].sF);
            if (a < bestAbs) {
                bestAbs = a;
                best = i;
            }
        }
        if (best == kNone) return;  // clic suelto: no hay nota cerca

        auto& c = m_clicks[best];
        c.press = NoteHit;
        c.pF = f;
        m_active = best;

        const int d = f - c.sF;
        log::debug("Rhythm Bar: pulsar cuadro={} macro={} dif={}", f, c.sF, d);
        this->evaluarPrecisionFrame(best, d, true, f);
    }

    // Soltar: SOLO para holds largos explicitos de la macro. Compara la DURACION del hold del
    // jugador (cuadros) con la del hold de la macro. En un clic comun no se evalua nada al soltar.
    void judgeRelease(int f) {
        if (m_active == kNone || m_active >= m_clicks.size()) {
            m_active = kNone;
            return;
        }
        const size_t ci = m_active;
        auto& c = m_clicks[ci];
        m_active = kNone;
        if (!c.hasRel || c.release != NotePending) return;  // clic comun, o ya expiro (Fallo)

        const int d = (f - c.pF) - (c.eF - c.sF);  // duracion del jugador - duracion de la macro
        c.release = NoteHit;
        this->evaluarPrecisionFrame(ci, d, false, f);
    }

    // Tiempo expirado sin accion = Fallo. Devuelve cuantos fallos nuevos hubo.
    //  - pulsar: el clic no se pulso dentro de la ventana; su "soltar" ya no cuenta (un Fallo por clic)
    //  - soltar: solo holds largos: el jugador pulso pero no solto a tiempo
    //            (cuadro de pulsacion + duracion del hold + ventana)
    int expire(int nowF) {
        int missed = 0;
        const float pct = this->percentNow();

        while (m_pCursor < m_clicks.size() && m_clicks[m_pCursor].sF + m_outer < nowF) {
            auto& c = m_clicks[m_pCursor];
            if (c.press == NotePending) {
                c.press = NoteMissed;
                if (c.hasRel) c.release = NoteSkipped;
                this->queueMiss(m_pCursor, pct);
                ++missed;
            }
            ++m_pCursor;
        }

        for (size_t i = m_rCursor; i < m_clicks.size(); ++i) {
            auto& c = m_clicks[i];
            if (c.sF > nowF) break;
            if (c.press == NoteHit && c.hasRel && c.release == NotePending) {
                const int deadline = c.pF + (c.eF - c.sF) + m_outer;
                if (nowF > deadline) {
                    c.release = NoteMissed;
                    this->queueMiss(i, pct);
                    ++missed;
                }
            }
        }

        while (m_rCursor < m_clicks.size()) {
            auto const& c = m_clicks[m_rCursor];
            const bool pressDone = c.press != NotePending;
            const bool relDone = !c.hasRel || c.release != NotePending;
            if (pressDone && relDone) ++m_rCursor;
            else break;
        }
        return missed;
    }

    // Tras un reinicio, un checkpoint o un salto grande de tiempo: se vuelve a empezar
    // desde el cuadro actual sin castigar las notas que quedaron atras.
    //  fresh   = primera sincronizacion con esta macro: estadisticas a cero
    //  rewound = el tiempo retrocedio: a cero salvo en modo practica (checkpoints)
    void resync(int nowF, bool fresh, bool rewound) {
        bool resetStats = fresh;
        if (rewound) {
            auto pl = PlayLayer::get();
            const bool practice = pl && pl->m_isPracticeMode;
            resetStats = !practice;
        }
        if (resetStats) {
            m_queue.clear();
            m_stats.reset();
        } else {
            // se conservan las estadisticas: las cadenas pendientes se resuelven con lo que se
            // alcanzo a ver (una muerte real deja su colision letal registrada)
            this->drainQueue(m_lastFrame, true);
            m_stats.combo = 0;
        }
        m_lethal.clear();
        this->updateComboLabel(false);

        const auto it = std::lower_bound(m_clicks.begin(), m_clicks.end(), nowF - m_outer,
                                         [](Click const& c, int v) { return c.sF < v; });
        m_pCursor = static_cast<size_t>(it - m_clicks.begin());
        m_rCursor = m_pCursor;
        m_active = kNone;
        for (size_t i = 0; i < m_clicks.size(); ++i) {
            auto& c = m_clicks[i];
            c.pF = 0;
            c.dead = false;
            if (i < m_pCursor) {
                c.press = NoteSkipped;
                c.release = NoteSkipped;
            } else {
                c.press = NotePending;
                c.release = c.hasRel ? NotePending : NoteSkipped;
            }
        }

        m_trail.clear();
        if (m_flashG) {
            m_flashG->stopAllActions();
            m_flashG->setOpacity(0);
        }
        if (m_line) {
            m_line->stopAllActions();
            m_line->setColor(kColWhite);
        }
        m_synced = true;
        m_lastFrame = nowF;
    }

    void advanceJudge(int nowF) {
        const bool fresh = !m_synced;
        const bool rewound = m_synced && nowF < m_lastFrame;
        const bool jumped = m_synced && nowF - m_lastFrame > static_cast<int>(std::llround(0.5 * m_fps));
        if (fresh || rewound || jumped) this->resync(nowF, fresh, rewound);
        m_lastFrame = nowF;

        const int missed = this->expire(nowF);
        if (missed > 0) {
            this->spawnJudgment(Judge::Miss);  // un solo cartel por frame
            // el cartel nuevo queda ligado a los Fallos de este frame (tilde / invalidacion)
            for (auto& q : m_queue) {
                if (q.j == Judge::Miss && !q.box.data()) {
                    q.box = Ref<CCNode>(m_box);
                    q.label = Ref<CCLabelBMFont>(m_label);
                }
            }
        }
        this->drainQueue(nowF, false);  // cierra las cadenas cuyo siguiente clic ya empezo
    }

    // Resultados (se abre unos segundos despues de levelComplete, encima de la pantalla de fin).
    void openResults() {
        if (!m_finished || m_stats.shown() == 0) return;
        const auto name = asciiOnly(pathToUtf8(utf8ToPath(m_cfg.macroFile).filename()), 30);
        if (auto popup = RhythmResultsPopup::create(m_stats, name)) {
            popup->show();
            popup->setZOrder(20000);
        }
    }

    // ---- dibujo ----

    void slot(ccColor3B color, GLubyte alpha, Box const& b) {
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
        node->setContentSize({std::max(b.w, 1.f), std::max(b.h, 1.f)});
        node->setPosition({b.x, b.y});
        node->setVisible(true);
    }

    // Contenedor (nota) de `along` px a lo largo del recorrido, ocupando [c0, c1] del grosor.
    // Una nota acertada desaparece; una perdida se pone roja; una saltada, tenue.
    void drawContainer(double t, uint8_t state, ccColor3B color, float c0, float c1, float along,
                       double now) {
        if (state == NoteHit) return;
        const float s = this->sAt(t, now);
        if (s - along / 2.f > m_len || s + along / 2.f < 0.f) return;  // fuera de la barra
        ccColor3B col = color;
        GLubyte a = 255;
        if (state == NoteMissed) {
            col = kColRed;
            a = 200;
        } else if (state == NoteSkipped) {
            a = 110;
        }
        const float s0 = std::clamp(s - along / 2.f, 0.f, std::max(m_len - along, 0.f));
        this->slot(col, a, this->box(s0, s0 + along, c0, c1));
    }

    // Cuerpo del hold entre el contenedor de bajada (t0) y el de subida (t1).
    //   Free: tal cual.  Clip: se corta en el receptor (lo que ya paso desaparece).
    //   Pin: la cabeza queda anclada al receptor y el resto se va consumiendo (hold sostenido).
    void drawBar(double t0, double t1, ccColor3B color, GLubyte alpha, float c0, float c1,
                 double now, BarMode mode) {
        float sHead = this->sAt(t0, now);  // lado del receptor
        const float sTail = this->sAt(t1, now);  // lado del origen
        if (mode == BarMode::Pin) sHead = m_recS;
        else if (mode == BarMode::Clip) sHead = std::min(sHead, m_recS);
        const float lo = std::clamp(sTail, 0.f, m_len);
        const float hi = std::clamp(sHead, 0.f, m_len);
        if (hi - lo < 1.f) return;
        this->slot(color, alpha, this->box(lo, hi, c0, c1));
    }

    // Barra de hold de P1 (verde), segun el estado del clic.
    void drawHoldP1(Click const& c, double now) {
        if (!c.hasRel) return;
        const float c0 = m_thick * 0.25f;
        const float c1 = m_thick * 0.75f;
        if (c.press == NoteHit) {
            // el jugador lo esta sosteniendo: cabeza anclada al receptor, el hold se consume
            if (c.release == NotePending) {
                this->drawBar(c.start, c.end, kColGreen, 215, c0, c1, now, BarMode::Pin);
            }
            return;
        }
        if (c.press == NoteMissed) {
            this->drawBar(c.start, c.end, kColRed, 120, c0, c1, now, BarMode::Free);
        } else if (c.press == NoteSkipped) {
            this->drawBar(c.start, c.end, kColGreen, 70, c0, c1, now, BarMode::Free);
        } else {
            this->drawBar(c.start, c.end, kColGreen, 150, c0, c1, now, BarMode::Free);
        }
    }

    // Barra de hold de P2 (morado claro): solo visual, se corta en el receptor.
    void drawHoldP2(rpb::Hold const& h, double now) {
        if (!h.hold) return;
        this->drawBar(h.start, h.end, kColLightPurple, 205, m_thick * 0.36f, m_thick * 0.64f, now,
                      BarMode::Clip);
    }

    // Contenedores verdes de P1: bajada (cabeza) y, en holds largos, subida (cola).
    void drawEndsP1(Click const& c, double from, double now) {
        const float c0 = 4.f;
        const float c1 = std::max(m_thick - 4.f, 5.f);
        if (c.start >= from) this->drawContainer(c.start, c.press, kColGreen, c0, c1, kNoteWidth, now);
        if (c.hasRel && c.end >= from) {
            uint8_t tailState = c.release;
            if (c.press == NoteMissed) tailState = NoteMissed;
            else if (c.press == NoteSkipped) tailState = NoteSkipped;
            this->drawContainer(c.end, tailState, kColGreen, c0, c1, kNoteWidth, now);
        }
    }

    // Contenedores morados claros de P2 (solo los que aun no llegaron al receptor).
    void drawEndsP2(rpb::Hold const& h, double now) {
        const float c0 = m_thick * 0.28f;
        const float c1 = m_thick * 0.72f;
        if (h.start >= now) {
            this->drawContainer(h.start, NotePending, kColLightPurple, c0, c1, kNoteWidth, now);
        }
        if (h.hold && h.end >= now) {
            this->drawContainer(h.end, NotePending, kColLightPurple, c0, c1, kNoteWidth, now);
        }
    }

    // Huella de lo que el jugador esta haciendo: franja delgada en el borde transversal inicial,
    // translucida, continua desde el press hasta el release (crece mientras se mantiene el boton).
    void drawTrail(double now) {
        const double tail = this->tailSeconds();
        while (!m_trail.empty() && !m_trail.front().open && m_trail.front().end < now - tail - 0.1) {
            m_trail.erase(m_trail.begin());
        }
        const float sh = std::clamp(m_thick * 0.18f, 4.f, 10.f);
        for (auto const& e : m_trail) {
            const double tb = e.open ? now : e.end;
            const float sa = std::min(this->sAt(e.start, now), m_len);  // extremo del press (lado salida)
            const float sb = std::max(this->sAt(tb, now), 0.f);         // extremo del release (lado entrada)
            if (sa <= 0.f || sb >= m_len) continue;
            const float len = std::max(sa - sb, 3.f);
            this->slot(ccColor3B{150, 255, 170}, 150, this->box(sb, sb + len, 1.f, 1.f + sh));
        }
    }

    void refresh(double now) {
        m_used = 0;

        if (m_macroOn) {
            const double from = now - this->tailSeconds();
            const double to = now + m_cfg.lookahead;

            // clics de P1 visibles (ordenados por inicio y por fin: un solo boton, no se solapan)
            const auto it1 = std::lower_bound(m_clicks.begin(), m_clicks.end(), from,
                                              [](Click const& c, double v) { return c.end < v; });
            const size_t a1 = static_cast<size_t>(it1 - m_clicks.begin());
            // clics de P2 visibles (los que todavia no terminaron)
            auto const& c2 = m_macro.clicks2;
            const auto it2 = std::lower_bound(c2.begin(), c2.end(), now,
                                              [](rpb::Hold const& h, double v) { return h.end < v; });
            const size_t a2 = static_cast<size_t>(it2 - c2.begin());

            // 1) cuerpos de hold P1  2) cuerpos de hold P2 (encima)  3) contenedores P1  4) contenedores P2
            for (size_t i = a1; i < m_clicks.size() && m_clicks[i].start <= to; ++i) {
                this->drawHoldP1(m_clicks[i], now);
            }
            for (size_t i = a2; i < c2.size() && c2[i].start <= to; ++i) this->drawHoldP2(c2[i], now);
            for (size_t i = a1; i < m_clicks.size() && m_clicks[i].start <= to; ++i) {
                this->drawEndsP1(m_clicks[i], from, now);
            }
            for (size_t i = a2; i < c2.size() && c2[i].start <= to; ++i) this->drawEndsP2(c2[i], now);

            this->drawTrail(now);
        }

        for (size_t i = m_used; i < m_pool.size(); ++i) m_pool[i]->setVisible(false);
    }
};

// ------------------------------------------------- crear la barra (seguro) ----
// Unico punto donde se crea la barra. Lo llaman PlayLayer::init (forceNew), el menu de pausa y el
// Modo Experimento. Si no se puede crear, deja el motivo en g_barStatus (se ve en el popup) en
// vez de fallar en silencio.
RhythmBar* ensureBar(PlayLayer* pl, bool forceNew = false) {
    if (!forceNew) {
        if (auto existing = RhythmBar::instance()) return existing;
    }

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

    // La barra se crea aunque la macro falle (o este desactivada): asi se puede elegir otra
    // desde el popup. Con la macro desactivada no se carga nada ni se avisa.
    LoadResult res;
    if (cfg.macroActive) {
        res = loadMacro(cfg);
        if (!res.ok) {
            Notification::create("Rhythm Bar: " + res.error, res.icon)->show();
        }
    }

    auto bar = RhythmBar::create(std::move(res.macro), res.fps, cfg);
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
    log::info("Rhythm Bar: barra creada y agregada a la UI ({}x{} en {},{}; visible={}; z={}; fps macro={})",
              size.width, size.height, pos.x, pos.y, bar->isVisible(), bar->getZOrder(), res.fps);
    return bar;
}

// ------------------------------------------------------------ popup ----
//
// Tamano 420 x 300 (la pantalla de GD mide 320 de alto). Posiciones (dx, dy) desde el centro.
//
//   boton de cerrar:    dentro del marco, esquina superior izquierda (-194, +134), escala 0.62
//   columna izquierda:  x de -196 a -5  (6 ajustes + Direccion, una sola grilla):
//                         etiqueta -196..-116 | boton - en -96 | valor en -56 | boton + en -16
//   separador vertical: x = +9
//   columna derecha:    x de  +20 a +196   (biblioteca de macros, centro en +108; casilla Mini-Barra)
//   fila de abajo:      Restablecer, Modo Experimento, Ocultar Barra
//   al final:           cuadro de macro (borrar = desactivar) + macro activa

class RhythmPopup : public RhythmPopupBase {
protected:
    static constexpr size_t kPerPage = 3;
    static constexpr float kPopupW = 420.f;
    static constexpr float kPopupH = 300.f;

    // filas de ajustes (columna izquierda) y de la lista (columna derecha)
    static constexpr float kRowY0 = 88.f;
    static constexpr float kRowDY = 20.f;
    static constexpr float kRightCX = 108.f;

    // grilla de la columna izquierda
    static constexpr float kLabelX = -196.f;   // inicio (ancla izquierda) de las etiquetas
    static constexpr float kLabelW = 80.f;     // ancho maximo de una etiqueta
    static constexpr float kMinusX = -96.f;    // centro del boton "-" / "<"
    static constexpr float kValueX = -56.f;    // centro del valor
    static constexpr float kValueW = 40.f;     // ancho maximo del valor
    static constexpr float kPlusX = -16.f;     // centro del boton "+" / ">"

    CCLabelBMFont* m_values[kRowCount] = {};
    CCLabelBMFont* m_dirValue = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_activeLabel = nullptr;
    CCMenuItemToggler* m_expToggle = nullptr;
    CCMenuItemToggler* m_hideToggle = nullptr;
    CCMenuItemToggler* m_miniToggle = nullptr;
    TextInput* m_input = nullptr;
    bool m_inputSilent = false;  // true = el cambio del texto lo hace el popup (no desactiva la macro)
    std::vector<CCNode*> m_listNodes;
    std::vector<std::string> m_library;
    size_t m_page = 0;

    // Casilla (toggle) con su texto a la derecha.
    CCMenuItemToggler* addToggle(SEL_MenuHandler cb, float dx, float dy, bool on, char const* text,
                                 float labelDx, float maxW) {
        auto toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.6f);
        toggle->setPosition(this->at(m_buttonMenu, dx, dy));
        toggle->toggle(on);
        m_buttonMenu->addChild(toggle);
        fitLabel(this->addLabel(text, labelDx, dy, 0.3f, CCPoint{0.f, 0.5f}), 0.3f, maxW);
        return toggle;
    }

    // Flecha chica (-, +, <, >) de la grilla de ajustes.
    CCMenuItemSpriteExtra* addArrow(char const* caption, float dx, float dy, SEL_MenuHandler cb, int tag) {
        return this->addButton(caption, 22, true, dx, dy, cb, tag, 18.f, 0.5f);
    }

    // Cambia el texto del cuadro de macro sin que cuente como "el jugador lo borro".
    void setInputSilently(std::string const& text) {
        if (!m_input) return;
        m_inputSilent = true;
        m_input->setString(text);
        m_inputSilent = false;
    }

    bool init() {
        if (!Popup::init(kPopupW, kPopupH)) return false;
        this->setTitle("Barra de Ritmo");
        this->tidyCloseButton(kPopupW, kPopupH);

        const CCPoint center{0.5f, 0.5f};
        const CCPoint leftAnchor{0.f, 0.5f};

        // separador vertical entre las dos columnas
        {
            auto divider = makeSolid(ccColor4B{0, 0, 0, 70}, 2.f, 160.f);
            divider->setPosition(this->at(m_mainLayer, 9.f, -48.f));
            m_mainLayer->addChild(divider, 3);
        }

        // ---- columna izquierda: ajustes en tiempo real (+ / - cambian la barra al instante) ----
        this->addLabel("Ajustes (tiempo real)", -100.f, 108.f, 0.35f, center, kColGold);
        for (size_t i = 0; i < kRowCount; ++i) {
            const float y = kRowY0 - kRowDY * static_cast<float>(i);
            fitLabel(this->addLabel(kRows[i].label, kLabelX, y, 0.3f, leftAnchor), 0.3f, kLabelW);
            this->addArrow("-", kMinusX, y, menu_selector(RhythmPopup::onStep), static_cast<int>(i) * 2);
            m_values[i] = this->addLabel("", kValueX, y, 0.34f);
            this->addArrow("+", kPlusX, y, menu_selector(RhythmPopup::onStep), static_cast<int>(i) * 2 + 1);
        }
        // direccion en la que corren las notas: misma grilla que el resto de las filas
        {
            const float y = kRowY0 - kRowDY * static_cast<float>(kRowCount);
            fitLabel(this->addLabel("Direccion", kLabelX, y, 0.3f, leftAnchor), 0.3f, kLabelW);
            this->addArrow("<", kMinusX, y, menu_selector(RhythmPopup::onDir), 0);
            m_dirValue = this->addLabel("", kValueX, y, 0.3f);
            this->addArrow(">", kPlusX, y, menu_selector(RhythmPopup::onDir), 1);
        }

        // ---- columna derecha: biblioteca de macros ----
        this->addLabel("Biblioteca de Macros", kRightCX, 108.f, 0.35f, center, kColGold);
        this->addButton("<", 28, false, 44.f, 22.f, menu_selector(RhythmPopup::onPage), 0, 20.f);
        m_pageLabel = this->addLabel("1/1", kRightCX, 22.f, 0.4f);
        this->addButton(">", 28, false, 172.f, 22.f, menu_selector(RhythmPopup::onPage), 1, 20.f);
        this->addButton("Quitar", 70, false, kRightCX, 0.f, menu_selector(RhythmPopup::onRemove), 0,
                        20.f, 0.5f);

        // casilla de la mini-barra segmentada superior
        m_miniToggle = this->addToggle(menu_selector(RhythmPopup::onMiniBar), 36.f, -30.f,
                                       miniBarSetting(), "Mini-Barra Superior", 52.f, 140.f);

        // ---- fila de abajo: Restablecer + dos casillas ----
        this->addButton("Restablecer", 80, false, -148.f, -62.f, menu_selector(RhythmPopup::onReset), 0,
                        22.f, 0.5f);
        m_expToggle = this->addToggle(menu_selector(RhythmPopup::onExperiment), -82.f, -62.f,
                                      g_experiment, "Modo Experimento", -66.f, 104.f);
        m_hideToggle = this->addToggle(menu_selector(RhythmPopup::onHideBar), 58.f, -62.f,
                                       hideBarSetting(), "Ocultar Barra", 74.f, 110.f);

        // ---- macro por nombre: el cuadro muestra la macro activa; borrar el texto la desactiva ----
        this->addLabel("Macro (borrar el texto = desactivar)", 0.f, -86.f, 0.3f, center, kColGold);
        m_input = TextInput::create(260.f, "ej: macro.gdr2", "bigFont.fnt");
        m_input->setFilter(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-()[]+,'&!#@%=/\\:");
        m_input->setMaxCharCount(120);
        m_input->setPosition(this->at(m_mainLayer, -62.f, -108.f));
        m_input->setCallback([this](std::string const& text) { this->onInputChanged(text); });
        m_mainLayer->addChild(m_input, 5);
        this->addButton("Agregar", 90, true, 140.f, -108.f, menu_selector(RhythmPopup::onAdd), 0, 26.f);

        m_activeLabel = this->addLabel("", 0.f, -134.f, 0.32f);

        // el cuadro arranca con el nombre de la macro activa (si hay una)
        {
            const auto cfg = loadConfig();
            if (cfg.macroActive) {
                this->setInputSilently(pathToUtf8(utf8ToPath(cfg.macroFile).filename()));
            }
        }

        this->refreshValues();
        this->rebuildList();
        return true;
    }

    void refreshValues() {
        for (size_t i = 0; i < kRowCount; ++i) {
            const auto v = rowValue(kRows[i]);
            m_values[i]->setString((std::to_string(v) + kRows[i].suffix).c_str());
            fitLabel(m_values[i], 0.34f, kValueW);
        }
        if (m_dirValue) {
            m_dirValue->setString(kDirNames[dirSetting()]);
            fitLabel(m_dirValue, 0.3f, kValueW);
        }
    }

    // Linea inferior del popup. Si la barra no existe, aqui se ve el motivo (en rojo).
    void refreshActive() {
        if (!RhythmBar::instance()) {
            m_activeLabel->setString(("Barra no creada: " + asciiOnly(g_barStatus, 38)).c_str());
            m_activeLabel->setColor(kColRed);
            fitLabel(m_activeLabel, 0.32f, 390.f);
            return;
        }
        const auto cfg = loadConfig();
        if (!cfg.macroActive) {
            m_activeLabel->setString("Macro: ninguna (desactivada)");
            m_activeLabel->setColor(kColGray);
        } else {
            const auto name = asciiOnly(pathToUtf8(utf8ToPath(cfg.macroFile).filename()), 40);
            m_activeLabel->setString(("Activa: " + name).c_str());
            m_activeLabel->setColor(kColWhite);
        }
        fitLabel(m_activeLabel, 0.32f, 390.f);
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
        const auto activeKey = activeKeyOf(cfg);  // sin recorrer carpetas

        if (m_library.empty()) {
            m_listNodes.push_back(this->addLabel("Sin macros guardadas", kRightCX, kRowY0 - kRowDY, 0.35f));
        }
        for (size_t k = 0; k < kPerPage; ++k) {
            const size_t i = m_page * kPerPage + k;
            if (i >= m_library.size()) break;
            // 17 caracteres entran dentro del boton (se recorta con "..." si es mas largo)
            const auto name = asciiOnly(pathToUtf8(utf8ToPath(m_library[i]).filename()), 17);
            // verde = macro activa (si esta desactivada, ninguna se marca)
            const bool active = cfg.macroActive && pathKey(m_library[i]) == activeKey;
            const float y = kRowY0 - kRowDY * static_cast<float>(k);
            m_listNodes.push_back(this->addButton(name.c_str(), 160, active, kRightCX, y,
                                                  menu_selector(RhythmPopup::onPick),
                                                  static_cast<int>(i), 20.f, 0.5f));
        }
        m_pageLabel->setString((std::to_string(m_page + 1) + "/" + std::to_string(pages)).c_str());
        this->refreshActive();
    }

    void afterSettingsChanged() {
        this->refreshValues();
        RhythmBar::refreshNow();  // la barra cambia mientras el popup esta abierto
    }

    void onStep(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag < 0 || tag >= static_cast<int>(kRowCount) * 2) return;
        const auto& row = kRows[tag / 2];
        const int64_t dir = (tag % 2) ? 1 : -1;
        const int64_t v = std::clamp<int64_t>(rowValue(row) + dir * row.step, row.lo, row.hi);
        setRowValue(row, v);  // setSettingValue (o valor guardado): persiste en Geode
        this->afterSettingsChanged();
    }

    // Direccion de las notas: tag 0 = anterior, tag 1 = siguiente (circular entre las 4).
    void onDir(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        const int cur = dirSetting();
        const int next = (cur + (tag == 1 ? 1 : 3)) % 4;
        setIntSetting("bar-dir", next);
        this->afterSettingsChanged();
    }

    void onReset(CCObject*) {
        for (auto const& row : kRows) setRowValue(row, row.def);  // Desfase vuelve a 0 ms
        setIntSetting("bar-dir", 0);
        setMiniBarSetting(true);
        this->afterSettingsChanged();
        this->syncToggles();
    }

    void onPage(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 0 && m_page > 0) --m_page;
        else if (tag == 1) ++m_page;  // rebuildList lo limita a la ultima pagina
        this->rebuildList();
    }

    // Los togglers cambian su aspecto solos, en un orden que no controlamos respecto al callback:
    // un instante despues se vuelven a poner iguales al estado real.
    void scheduleToggleSync() {
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.05f),
            CCCallFunc::create(this, callfunc_selector(RhythmPopup::syncToggles)), nullptr));
    }

    void syncToggles() {
        if (m_expToggle) m_expToggle->toggle(g_experiment);
        if (m_hideToggle) m_hideToggle->toggle(hideBarSetting());
        if (m_miniToggle) m_miniToggle->toggle(miniBarSetting());
    }

    // Modo Experimento. Si la barra todavia no existe se intenta crear ahora; si no se puede,
    // el modo queda apagado y se avisa el motivo.
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
        this->scheduleToggleSync();
    }

    // Ocultar Barra: setVisible(false) sobre todo el contenedor de ritmo (el juicio sigue corriendo
    // por debajo, asi las estadisticas y la pantalla de resultados siguen funcionando).
    void onHideBar(CCObject*) {
        const bool want = !hideBarSetting();
        setHideBarSetting(want);
        RhythmBar::refreshNow();
        this->scheduleToggleSync();
    }

    // Mini-Barra Superior: marcada = visible; desmarcada = setVisible(false) sobre toda la
    // mini-barra segmentada (el juicio sigue igual, solo se limpia la interfaz).
    void onMiniBar(CCObject*) {
        setMiniBarSetting(!miniBarSetting());
        RhythmBar::refreshNow();
        this->scheduleToggleSync();
    }

    // Macro Toggle: apaga la macro al instante. La barra deja de dibujar notas, se limpian los
    // holds en vivo y se borran las estadisticas de esa run.
    void deactivateMacro(std::string const& message) {
        setMacroActiveSetting(false);
        RhythmBar::refreshNow();
        Notification::create(message, NotificationIcon::Info)->show();
        this->rebuildList();
    }

    // Texto del cuadro de macro: si el jugador lo deja vacio (la borra), la macro se desactiva.
    void onInputChanged(std::string const& text) {
        if (m_inputSilent) return;
        if (!rpb::trim(text).empty()) return;
        if (!macroActiveSetting()) return;
        this->deactivateMacro("Rhythm Bar: macro desactivada (texto borrado)");
    }

    // Busca la macro por nombre, la valida, la agrega a la biblioteca y la activa al instante.
    void onAdd(CCObject*) {
        const std::string name = m_input ? rpb::trim(m_input->getString()) : std::string();
        const auto cfg = loadConfig();
        if (name.empty()) {
            Notification::create("Rhythm Bar: escribe el nombre de una macro", NotificationIcon::Info)->show();
            return;
        }

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
        setMacroActiveSetting(true);
        RhythmBar::refreshNow();
        this->setInputSilently(pathToUtf8(found->filename()));
        Notification::create("Rhythm Bar: macro cargada (" +
                                 asciiOnly(pathToUtf8(found->filename()), 30) + ")",
                             NotificationIcon::Success)->show();
        this->rebuildList();
    }

    // Saca la macro activa de la biblioteca (no borra el archivo) y la desactiva.
    void onRemove(CCObject*) {
        const auto cfg = loadConfig();
        if (!cfg.macroActive) {
            Notification::create("Rhythm Bar: no hay una macro activa", NotificationIcon::Info)->show();
            return;
        }
        const auto activeKey = activeKeyOf(cfg);

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
        setMacroActiveSetting(false);  // quitada de la biblioteca = desactivada al instante
        RhythmBar::refreshNow();
        this->setInputSilently("");
        Notification::create("Rhythm Bar: macro quitada y desactivada", NotificationIcon::Success)->show();
        this->rebuildList();
    }

    // Un clic en una macro de la lista la activa (y la barra la carga al instante).
    // Macro Toggle: un SEGUNDO clic sobre la macro que ya esta activa la desactiva.
    void onPick(CCObject* sender) {
        const int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag < 0 || static_cast<size_t>(tag) >= m_library.size()) return;
        const std::string stored = m_library[static_cast<size_t>(tag)];  // copia: rebuildList cambia m_library

        const auto cfg = loadConfig();
        if (cfg.macroActive && pathKey(stored) == activeKeyOf(cfg)) {
            this->setInputSilently("");
            this->deactivateMacro("Rhythm Bar: macro desactivada");
            return;
        }

        std::error_code ec;
        if (!fs::exists(utf8ToPath(stored), ec)) {
            Notification::create("Rhythm Bar: el archivo ya no existe", NotificationIcon::Error)->show();
            return;
        }
        setMacroFile(stored);
        setMacroActiveSetting(true);
        RhythmBar::refreshNow();
        this->setInputSilently(pathToUtf8(utf8ToPath(stored).filename()));
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
    // El hook de destroyPlayer va con prioridad alta (numero bajo = corre antes) para ver la
    // colision letal aunque un noclip despues decida no llamar al original.
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", kLethalHookPriority);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        ensureDefaultOffset();  // migracion unica del desfase a v1.50
        ensureBar(this, true);  // siempre una barra nueva por nivel
        return true;
    }

    // Nuevo intento: se descongela el juicio y se cancela una pantalla de resultados pendiente.
    void resetLevel() {
        PlayLayer::resetLevel();
        if (auto bar = RhythmBar::instance()) bar->onLevelReset();
    }

    // Fin de nivel: congela las estadisticas y abre la pantalla de resultados estilo osu!.
    void levelComplete() {
        PlayLayer::levelComplete();
        if (auto bar = RhythmBar::instance()) bar->onLevelComplete();
    }

    // Chain-Survival Link: cada colision letal de la hitbox real (se bloquee o no) queda
    // registrada. El pico anticheat del nivel no cuenta.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (object != m_anticheatSpike) {
            if (auto bar = RhythmBar::instance()) bar->onLethal(this);
        }
        PlayLayer::destroyPlayer(player, object);
    }
};

/*
 * Entradas del jugador. pushButton/releaseButton de GJBaseGameLayer solo encolan el clic;
 * el juego lo aplica de verdad en handleButton, dentro del paso de fisica que corresponde,
 * asi que ahi el contador de pasos coincide con el de las macros. PlayLayer hereda este
 * metodo, por eso el hook va sobre GJBaseGameLayer. Se llama primero al original (el clic ya
 * queda aplicado) y luego se lee el cuadro del MISMO paso (RhythmBar::frameNow): no hay ningun
 * retardo intermedio.
 */
class $modify(RhythmBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (!isPlayer1 || button != 1) return;  // solo P1 y solo salto
        auto bar = RhythmBar::instance();
        if (!bar) return;
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != static_cast<GJBaseGameLayer*>(this)) return;
        bar->onInput(down, pl);
    }
};

/*
 * Menu de pausa: UN solo boton, "Ritmo" (abre el popup con todos los ajustes).
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
