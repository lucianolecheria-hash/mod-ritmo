// Rhythm Practice Bar
// Barra de ritmo vertical (en un costado) con receptor horizontal delgado.
//   - Verde  : clic único / momento de presionar
//   - Celeste: momento exacto de soltar (solo en clics mantenidos)
//   - Los clics únicos NO muestran barra de retención.
//
// Formato de la macro (texto plano, tiempos en SEGUNDOS):
//   # comentario
//   1.250            <- clic único (solo presionar)
//   2.000 2.800      <- mantener: presiona en 2.000, suelta en 2.800
//   4.100 4.150      <- si dura menos que el umbral (por defecto 120 ms) = clic único
// Separadores válidos: espacios, tabs, coma o punto y coma.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace rhythm {

	struct Note {
		double press = 0.0;
		double release = 0.0;
		bool hold = false;
	};

	// ---------------------------------------------------------------
	// Lectura de la macro
	// ---------------------------------------------------------------
	static std::vector<Note> parseMacro(std::string const& text, double tapThreshold) {
		std::vector<Note> notes;
		std::istringstream in(text);
		std::string line;

		while (std::getline(in, line)) {
			// Quitar comentarios
			for (const char* marker : {"#", "//"}) {
				auto pos = line.find(marker);
				if (pos != std::string::npos) line.erase(pos);
			}
			// Normalizar separadores
			std::replace_if(line.begin(), line.end(), [](char c) {
				return c == ',' || c == ';';
			}, ' ');

			std::istringstream ls(line);
			std::string a, b;
			if (!(ls >> a)) continue;
			ls >> b;

			double press = 0.0;
			try { press = std::stod(a); } catch (...) { continue; } // cabeceras/líneas inválidas

			double release = press;
			if (!b.empty()) {
				try { release = std::stod(b); } catch (...) { release = press; }
			}

			Note n;
			n.press = press;
			if (release - press > tapThreshold) {
				n.hold = true;
				n.release = release;
			} else {
				n.hold = false;      // clic único: sin barra de retención
				n.release = press;
			}
			notes.push_back(n);
		}

		std::sort(notes.begin(), notes.end(), [](Note const& x, Note const& y) {
			return x.press < y.press;
		});
		return notes;
	}

	// ---------------------------------------------------------------
	// Nodo de la barra
	// ---------------------------------------------------------------
	class RhythmBar : public CCNode {
	protected:
		CCDrawNode* m_draw = nullptr;
		std::vector<Note> m_notes;
		size_t m_start = 0;
		double m_lastTime = 0.0;

		float m_w = 34.f;
		float m_h = 224.f;
		bool m_down = true;
		float m_lookahead = 1.5f;
		double m_offset = 0.0;
		float m_bgAlpha = 0.35f;

		static constexpr float kNoteH = 8.f;      // alto de cada nota
		static constexpr float kReceptorH = 2.f;  // grosor del receptor

		void fillRect(float x0, float y0, float x1, float y1, ccColor4F c) {
			// Recorte vertical al área de la barra
			y0 = std::max(y0, 0.f);
			y1 = std::min(y1, m_h);
			if (y1 <= y0) return;
			CCPoint v[4] = { ccp(x0, y0), ccp(x1, y0), ccp(x1, y1), ccp(x0, y1) };
			m_draw->drawPolygon(v, 4, c, 0.f, ccc4f(0.f, 0.f, 0.f, 0.f));
		}

		bool setup(std::vector<Note> notes) {
			if (!CCNode::init()) return false;

			m_notes = std::move(notes);

			auto mod = Mod::get();
			m_w = static_cast<float>(mod->getSettingValue<int64_t>("bar-width"));
			float heightPct = static_cast<float>(mod->getSettingValue<int64_t>("bar-height"));
			m_lookahead = static_cast<float>(mod->getSettingValue<double>("lookahead"));
			m_offset = static_cast<double>(mod->getSettingValue<int64_t>("offset")) / 1000.0;
			m_bgAlpha = static_cast<float>(mod->getSettingValue<int64_t>("bg-opacity")) / 100.f;
			m_down = mod->getSettingValue<std::string>("direction") == "Bajan";
			bool left = mod->getSettingValue<std::string>("side") == "Izquierda";

			auto win = CCDirector::get()->getWinSize();
			m_h = win.height * heightPct / 100.f;

			constexpr float margin = 8.f;
			this->setContentSize({ m_w, m_h });
			this->setPosition(
				left ? margin : win.width - margin - m_w,
				(win.height - m_h) / 2.f
			);
			this->setID("rhythm-bar"_spr);

			m_draw = CCDrawNode::create();
			this->addChild(m_draw);

			this->refresh(0.0);
			return true;
		}

	public:
		static RhythmBar* create(std::vector<Note> notes) {
			auto ret = new RhythmBar();
			if (ret && ret->setup(std::move(notes))) {
				ret->autorelease();
				return ret;
			}
			CC_SAFE_DELETE(ret);
			return nullptr;
		}

		// Se llama cada frame con el tiempo actual del nivel (segundos)
		void refresh(double levelTime) {
			double now = levelTime + m_offset;

			// Si el tiempo retrocede (reinicio / checkpoint), reiniciar el índice
			if (now < m_lastTime - 1e-6) m_start = 0;
			m_lastTime = now;

			m_draw->clear();

			// Fondo translúcido
			fillRect(0.f, 0.f, m_w, m_h, ccc4f(0.f, 0.f, 0.f, m_bgAlpha));

			// Geometría: receptor al 20% (bajan) o al 80% (suben)
			float ry = m_down ? m_h * 0.20f : m_h * 0.80f;
			float dir = m_down ? 1.f : -1.f;                // hacia dónde está el futuro
			float farPx = m_down ? (m_h - ry) : ry;         // espacio para el futuro
			float pxPerSec = farPx / m_lookahead;
			double pastSpan = (m_h - farPx) / pxPerSec;     // segundos visibles tras el receptor

			auto yAt = [&](double t) {
				return ry + dir * static_cast<float>(t - now) * pxPerSec;
			};
			auto fade = [&](double t) {
				double d = t - now;
				if (d >= 0.0) return 1.f;
				return static_cast<float>(std::clamp(1.0 + d / pastSpan, 0.0, 1.0));
			};

			// Saltar notas que ya salieron de la barra
			while (m_start < m_notes.size() && m_notes[m_start].release + pastSpan < now) {
				++m_start;
			}

			auto const green = [](float a) { return ccc4f(0.18f, 0.90f, 0.30f, a); };
			auto const cyan  = [](float a) { return ccc4f(0.25f, 0.85f, 1.00f, a); };
			auto const hold  = [](float a) { return ccc4f(0.20f, 0.88f, 0.65f, 0.40f * a); };

			constexpr float pad = 2.f;

			for (size_t i = m_start; i < m_notes.size(); ++i) {
				auto const& n = m_notes[i];
				if (n.press - now > m_lookahead) break;

				// Barra de retención: SOLO para clics mantenidos
				if (n.hold) {
					float ya = yAt(n.press);
					float yb = yAt(n.release);
					fillRect(m_w * 0.30f, std::min(ya, yb), m_w * 0.70f, std::max(ya, yb), hold(fade(n.release)));
				}

				// Nota verde: presionar / clic único
				float yp = yAt(n.press);
				float ap = fade(n.press);
				fillRect(pad, yp - kNoteH / 2.f, m_w - pad, yp + kNoteH / 2.f, green(ap));

				// Nota celeste: soltar (solo si es mantenido)
				if (n.hold) {
					float yr = yAt(n.release);
					float ar = fade(n.release);
					fillRect(pad, yr - kNoteH / 2.f, m_w - pad, yr + kNoteH / 2.f, cyan(ar));
				}
			}

			// Receptor: barrita horizontal muy delgada
			fillRect(-4.f, ry - kReceptorH / 2.f, m_w + 4.f, ry + kReceptorH / 2.f, ccc4f(1.f, 1.f, 1.f, 1.f));
		}
	};

} // namespace rhythm

// -------------------------------------------------------------------
// Hook de PlayLayer
// -------------------------------------------------------------------
class $modify(RhythmPlayLayer, PlayLayer) {
	struct Fields {
		rhythm::RhythmBar* bar = nullptr;
	};

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("enabled")) return true;

		// Resolver la ruta de la macro
		std::filesystem::path path = mod->getSettingValue<std::string>("macro-file");
		if (path.is_relative()) path = mod->getSaveDir() / path;

		auto res = utils::file::readString(path);
		if (res.isErr()) {
			log::warn("No se pudo leer la macro '{}': {}", path.string(), res.unwrapErr());
			Notification::create("Rhythm Bar: no se pudo leer la macro", NotificationIcon::Warning)->show();
			return true;
		}

		double tapThreshold = static_cast<double>(mod->getSettingValue<int64_t>("tap-threshold")) / 1000.0;
		auto notes = rhythm::parseMacro(res.unwrap(), tapThreshold);
		if (notes.empty()) {
			log::warn("La macro '{}' no contiene notas válidas", path.string());
			return true;
		}

		auto bar = rhythm::RhythmBar::create(std::move(notes));
		if (!bar) return true;

		m_uiLayer->addChild(bar, 100); // capa de UI: no se mueve con la cámara
		m_fields->bar = bar;
		return true;
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (m_fields->bar) {
			// Verifica que m_levelTime exista en los bindings de tu versión de GD
			m_fields->bar->refresh(m_gameState.m_levelTime);
		}
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		if (m_fields->bar) {
			m_fields->bar->refresh(m_gameState.m_levelTime);
		}
	}
};
