# Minecraft SS Tool

Programa de revisión (SS) para Minecraft Java + panel web para el staff.

- **`MinecraftSSTool.exe`**: el jugador lo abre, escribe la licencia y se escanea su PC.
- **Panel web** (`docs/`): crear licencias, ver los escaneos y agregar mods o páginas prohibidas.
- **Base de datos**: Supabase (gratis). GitHub Pages solo sirve páginas estáticas, así que los datos se guardan en Supabase.

## Qué revisa el programa

| Paso | Qué detecta |
|---|---|
| Sistema y sesiones | Reinicios, cierres o cambios de sesión recientes, registros de eventos borrados |
| Minecraft y procesos | Cuánto tiempo lleva abierto Minecraft, si lo reabrió hace poco, java agents, DLL inyectadas, programas cerrados en los últimos 30 min |
| Mods | Mods prohibidos revisando **el contenido del .jar** (detecta archivos renombrados), .jar disfrazados con otra extensión, versiones de clientes de hacks |
| Borrados y renombres | Cuántos archivos borró (24 h y última hora), papelera enviada o vaciada, Prefetch borrado, archivos renombrados o con extensión cambiada (registro USN del disco) |
| Archivos recientes | .exe/.jar/.dll/.bat/.ahk nuevos y .jar escondidos en Descargas, Escritorio, Documentos, AppData y Temp |
| Historial web | Páginas de hacks visitadas o buscadas (Chrome, Edge, Brave, Opera, Opera GX, Vivaldi, Firefox). Solo busca esas páginas, no copia el historial |
| Mouse y macros | Logitech G HUB, Razer, Corsair, SteelSeries, Bloody, Glorious, AutoHotkey… si están abiertos, si cambió su configuración hace poco o si borró archivos de sus carpetas |
| Prefetch | Programas de hacks ejecutados, Prefetch desactivado o limpiado |

> Una alerta es un **indicio**, no una prueba. El staff debe revisar cada una.

## Configuración (una sola vez)

### 1. Crear la base de datos en Supabase
1. Entra a https://supabase.com, crea una cuenta y un **New project** (gratis).
2. Ve a **SQL Editor → New query**, pega todo el archivo `supabase/schema.sql` y dale a **Run**.
3. Ve a **Authentication → Sign In / Providers** y **desactiva** "Allow new users to sign up". Así nadie más puede crearse una cuenta en tu panel.
4. Ve a **Authentication → Users → Add user** y crea tu usuario (correo + contraseña). Repite para cada staff.
5. Ve a **Project Settings → API** (o **API Keys**) y copia:
   - la **Project URL** (`https://xxxx.supabase.co`)
   - la clave **anon public** o **publishable**

> ⚠️ Nunca uses la clave **service_role / secret** en el programa ni en la página.

### 2. Conectar el panel y el programa
- En `docs/config.js` pega la URL y la clave.
- En `config.h` pega las mismas dos cosas (`SS_SUPABASE_URL` y `SS_SUPABASE_KEY`).
- Vuelve a compilar con `compilar.bat`.

Si `config.h` queda vacío, el programa funciona en **modo local**: no pide licencia y solo guarda el `.txt`.

### 3. Subir el panel a GitHub Pages
1. Crea un repositorio en GitHub y sube esta carpeta (el `.gitignore` ya evita subir el `.exe` y los archivos de compilación).
2. En el repositorio: **Settings → Pages → Build and deployment → Deploy from a branch**, rama `main`, carpeta **`/docs`** → Save.
3. En 1–2 minutos tu panel estará en `https://TU_USUARIO.github.io/NOMBRE_DEL_REPO/`.

## Uso
1. En el panel, pestaña **Licencias**, crea una licencia (por ejemplo de 1 uso para cada SS).
2. Pásale al jugador el `.exe` y el código de licencia.
3. El jugador abre el programa (pide permisos de administrador), escribe la licencia y pulsa **Iniciar escaneo**.
4. El resultado aparece en la pestaña **Escaneos** del panel.

Para agregar mods o páginas prohibidas sin recompilar: pestaña **Mods y webs prohibidos**.

## Compilar
Doble clic en `compilar.bat` (necesita Visual Studio con "Desarrollo para el escritorio con C++").
