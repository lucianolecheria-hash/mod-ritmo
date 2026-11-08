# RPB (Rhythm Practice Bar) — v1.60
> **Mod de análisis físico y práctica rítmica por frames para Geometry Dash 2.2081.**  
> Desarrollado para **Geode v5** (Win64) usando C++23 y Cocos2d-x.

**RPB** es una herramienta de entrenamiento diseñada para optimizar las sesiones de práctica en niveles de alta dificultad física (como *Acheron* o *Slaughterhouse*). En lugar de depender únicamente de los intentos a ciegas, el mod lee macros binarias `.gdr2` y mide en segundo plano la precisión exacta de tus dedos en comparación con la macro perfecta, devolviéndote un reporte visual al morir.

---

## 🛠️ ¿Qué hace realmente este mod? (Funciones Reales)

*   **Compensación de Retraso (-1 Frame):** Soluciona de raíz el delay de entrada nativo de Cocos2d-x al presionar las teclas. Esto calibra los clics de bajada para que los impactos perfectos registren el rango máximo de **¡Excelente!** al instante, igual que los releases.
*   **Bypass de Release en Clics Únicos:** Libera los toques rápidos de un solo frame. El mod evalúa estrictamente el momento del impacto para dar fluidez al gameplay y solo se pone estricto con la duración del hold cuando la macro registra holds sostenidos.
*   **Gráfica de Precisión (Mapa de Calor):** Renderiza una línea temporal del 0% al 100% del nivel mediante `CCDrawNode` en la pantalla de resultados estilo *osu!*. Te muestra con exactitud dónde mantuviste el ritmo (curva cian arriba) y dónde perdiste la consistencia, marcando automáticamente el **"PEOR TRAMO"** de la run (ej. 56-58%).
*   **Validación de Supervivencia por Trayectoria:** El mod no te regala un acierto solo por apretar bien el botón de inicio. Si das el clic en el frame ideal pero la trayectoria del salto te hace morir contra un pincho dos bloques más adelante antes de llegar al siguiente salto, el mod invalida el input anterior y te marca un **¡Fallo!** real.
*   **Filtro de Noclip Inteligente:** Si estás en una run de noclip y metés un clic *Temprano* o *Tarde* pero la hitbox del juego igual te perdonaba y sobrevivías, el mod te dibuja un tilde verde (`✔️`) al lado del texto. Este acierto leve de supervivencia mantiene vivo tu combo acumulativo en pantalla en lugar de arruinarte la racha injustamente.
*   **Modo Sombra (Copiloto):** Permite que tu bot de Mega Hack reproduzca la macro perfecta en el fondo para pasar el nivel automáticamente. El mod intercepta tus clics reales del teclado para que vayan **únicamente** dirigidos a la barra de ritmo y al cálculo de precisión, sin enviar el input de salto al cubo real del juego. Podés practicar de pura memoria sobre la macro sin desviar al vehículo.
*   **Direcciones Dinámicas:** Permite rotar y mover la interfaz en 4 direcciones desde el menú de pausa: Izquierda a Derecha, Derecha a Izquierda, Arriba a Abajo (Vertical) y Abajo a Arriba (Vertical). También incluye soporte dual pintando las notas del Player 2 en color **Morado Claro**.

---

## 📥 Instalación

1. Entrá a la pestaña de **Actions** en este repositorio de GitHub.
2. Descargá el compilado en verde más reciente desde la sección de **Artifacts** y extraé el `.zip`.
3. Abrí Geometry Dash, andá a la lista de mods en el menú interno de **Geode** y hacé clic en el botón de la tuerca/ajustes o usá el botón nativo de **"Inyectar mod"** para cargar el archivo `.geode` directamente desde ahí sin abrir carpetas.

---

## 👑 Autoría
Conceptualizado y desarrollado de forma independiente por **LuchX20** (con ayuda de IA para el desarrollo)
