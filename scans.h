#pragma once
// =====================================================================
//  scans.h - todas las revisiones del SS
// =====================================================================

struct UsnDelete { std::wstring name, dir; ULONGLONG time; };
struct UsnRename { std::wstring oldName, newName, dir; ULONGLONG time; };
struct PfEntry   { std::wstring exe; fs::path path; ULONGLONG created, written; };

struct ScanContext {
    fs::path appdata, home, desktop, downloads, localApp, temp, programData, programFiles, programFilesX86, windir;
    std::set<std::wstring> running;          // procesos abiertos (minusculas)
    std::vector<UsnDelete> deletes;          // borrados ultimas 24 h
    std::vector<UsnRename> renames;          // renombrados ultimas 24 h
    bool usnOk = false;
    std::vector<PfEntry> prefetch;
    bool prefetchOk = false;
};

static const double kRecentHours = 48.0;   // "archivos recientes"
static const double kUsnHours    = 24.0;   // ventana para borrados/renombres

static bool PathUnder(const std::wstring& pathLower, const fs::path& dir) {
    if (dir.empty()) return false;
    std::wstring d = ToLower(dir.wstring());
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    return pathLower.rfind(d, 0) == 0;
}

static void LoadPrefetch(ScanContext& c) {
    fs::path pf = c.windir / L"Prefetch";
    std::error_code ec;
    fs::directory_iterator it(pf, ec), end;
    if (ec) return;
    c.prefetchOk = true;
    for (; !ec && it != end; it.increment(ec)) {
        std::wstring name = it->path().filename().wstring();
        if (!EndsWith(ToLower(name), L".pf")) continue;
        size_t dash = name.find_last_of(L'-');
        PfEntry e;
        e.exe = ToLower(dash == std::wstring::npos ? name : name.substr(0, dash));
        e.path = it->path();
        if (!FileTimes(e.path, e.created, e.written)) continue;
        c.prefetch.push_back(e);
    }
}

// =================== 1. SISTEMA Y SESIONES ===================
struct EvItem { int id; ULONGLONG time; std::wstring source; };

static void QueryEvents(const wchar_t* channel, const wchar_t* xpath, const wchar_t* source,
                        std::vector<EvItem>& out, size_t maxEvents = 100) {
    EVT_HANDLE q = EvtQuery(nullptr, channel, xpath, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!q) return;
    EVT_HANDLE ctx = EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem);
    if (!ctx) { EvtClose(q); return; }
    EVT_HANDLE evs[16];
    DWORD ret = 0;
    std::vector<BYTE> buf(8192);
    size_t taken = 0;
    while (taken < maxEvents && EvtNext(q, 16, evs, 3000, 0, &ret)) {
        for (DWORD i = 0; i < ret; ++i) {
            DWORD used = 0, props = 0;
            BOOL ok = EvtRender(ctx, evs[i], EvtRenderEventValues, (DWORD)buf.size(), buf.data(), &used, &props);
            if (!ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                buf.resize(used);
                ok = EvtRender(ctx, evs[i], EvtRenderEventValues, (DWORD)buf.size(), buf.data(), &used, &props);
            }
            if (ok && props > EvtSystemTimeCreated) {
                auto* v = (PEVT_VARIANT)buf.data();
                out.push_back({ (int)v[EvtSystemEventID].UInt16Val, v[EvtSystemTimeCreated].FileTimeVal, source });
                ++taken;
            }
            EvtClose(evs[i]);
        }
    }
    EvtClose(ctx);
    EvtClose(q);
}

static const wchar_t* EventText(int id) {
    switch (id) {
    case 21:   return L"Inicio de sesión";
    case 23:   return L"Cierre de sesión";
    case 24:   return L"Sesión desconectada (cambio de usuario)";
    case 25:   return L"Volvió a conectarse a la sesión";
    case 7001: return L"Inicio de sesión (Winlogon)";
    case 7002: return L"Cierre de sesión (Winlogon)";
    case 1074: return L"Apagado o reinicio solicitado";
    case 6005: return L"Windows se encendió";
    case 6006: return L"Windows se apagó";
    case 6008: return L"Apagado inesperado";
    default:   return L"Evento";
    }
}

static void ScanSystem(Report& r, ScanContext& c) {
    r.Section(L"Sistema y sesiones");
    r.Info(L"PC: " + GetPcName() + L"  ·  Usuario de Windows: " + GetWinUser());

    double upHours = GetTickCount64() / 3600000.0;
    ULONGLONG boot = NowFt() - (ULONGLONG)(GetTickCount64() * 10000ULL);
    std::wstring up = L"PC encendida desde " + LocalTimeStr(boot) + L" (" + Ago(upHours) + L")";
    if (upHours < 0.5) r.Flag(up + L"  →  se reinició hace menos de 30 min (un reinicio borra procesos abiertos)");
    else r.Info(up);

    std::vector<EvItem> ev;
    QueryEvents(L"Microsoft-Windows-TerminalServices-LocalSessionManager/Operational",
                L"*[System[(EventID=21 or EventID=23 or EventID=24 or EventID=25) and TimeCreated[timediff(@SystemTime) <= 86400000]]]",
                L"Sesiones", ev);
    QueryEvents(L"System",
                L"*[System[Provider[@Name='Microsoft-Windows-Winlogon'] and (EventID=7001 or EventID=7002) and TimeCreated[timediff(@SystemTime) <= 86400000]]]",
                L"Winlogon", ev);
    QueryEvents(L"System",
                L"*[System[(EventID=1074 or EventID=6005 or EventID=6006 or EventID=6008) and TimeCreated[timediff(@SystemTime) <= 86400000]]]",
                L"Sistema", ev);
    std::sort(ev.begin(), ev.end(), [](const EvItem& a, const EvItem& b) { return a.time > b.time; });

    if (ev.empty()) r.Info(L"Sin inicios/cierres de sesión ni apagados en las últimas 24 h.");
    int shown = 0;
    for (const auto& e : ev) {
        if (shown++ >= 25) break;
        double h = HoursSinceFt(e.time);
        std::wstring line = std::wstring(EventText(e.id)) + L"  ·  " + LocalTimeStr(e.time) + L" (" + Ago(h) + L")";
        bool sus = (e.id == 23 || e.id == 24 || e.id == 7002 || e.id == 1074) && h < 2.0;
        if (sus) r.Flag(line + L"  →  cambio de sesión, cierre de sesión o reinicio reciente");
        else r.Info(line);
    }

    std::vector<EvItem> cleared;
    QueryEvents(L"System", L"*[System[Provider[@Name='Microsoft-Windows-Eventlog'] and EventID=104]]", L"System", cleared, 5);
    QueryEvents(L"Security", L"*[System[EventID=1102]]", L"Security", cleared, 5);
    for (const auto& e : cleared) {
        double h = HoursSinceFt(e.time);
        std::wstring line = L"Se borró el registro de eventos de Windows (" + e.source + L") el " +
                            LocalTimeStr(e.time) + L" (" + Ago(h) + L")";
        if (h < 24 * 7) r.Flag(line + L"  →  alguien limpió los registros");
        else r.Info(line);
    }
    if (cleared.empty()) r.Ok(L"No se detectó que hayan borrado los registros de eventos de Windows.");
}

// =================== 2. MINECRAFT Y PROCESOS ===================
typedef LONG(NTAPI* NtQIP_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
struct UStr { USHORT Length; USHORT MaximumLength; PWSTR Buffer; };

static std::wstring GetCmdLine(HANDLE h) {
    static NtQIP_t fn = reinterpret_cast<NtQIP_t>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!fn) return L"";
    ULONG len = 0;
    fn(h, 60 /*ProcessCommandLineInformation*/, nullptr, 0, &len);
    if (len == 0 || len > (1u << 20)) return L"";
    std::vector<BYTE> b(len);
    if (fn(h, 60, b.data(), len, &len) < 0) return L"";
    auto* us = (UStr*)b.data();
    if (!us->Buffer || us->Length == 0) return L"";
    return std::wstring(us->Buffer, us->Length / 2);
}
static std::wstring ProcessPath(HANDLE h) {
    wchar_t b[MAX_PATH * 2];
    DWORD n = ARRAYSIZE(b);
    return QueryFullProcessImageNameW(h, 0, b, &n) ? std::wstring(b, n) : L"";
}
static ULONGLONG ProcessStart(HANDLE h) {
    FILETIME cr, ex, k, u;
    return GetProcessTimes(h, &cr, &ex, &k, &u) ? FileTimeToU64(cr) : 0;
}
static std::wstring ArgValue(const std::wstring& cmd, const std::wstring& flag) {
    size_t p = cmd.find(flag);
    if (p == std::wstring::npos) return L"";
    p += flag.size();
    while (p < cmd.size() && cmd[p] == L' ') ++p;
    if (p >= cmd.size()) return L"";
    size_t e;
    if (cmd[p] == L'"') { ++p; e = cmd.find(L'"', p); }
    else e = cmd.find(L' ', p);
    return cmd.substr(p, e == std::wstring::npos ? std::wstring::npos : e - p);
}
static bool LooksLikeMinecraft(const std::wstring& cmdLower) {
    return cmdLower.find(L"net.minecraft") != std::wstring::npos || cmdLower.find(L"--gamedir") != std::wstring::npos ||
           cmdLower.find(L"minecraft") != std::wstring::npos || cmdLower.find(L"lunar") != std::wstring::npos ||
           cmdLower.find(L"--accesstoken") != std::wstring::npos;
}

static void AnalyzeMinecraftProcess(Report& r, ScanContext& c, DWORD pid, const std::wstring& exe, HANDLE h,
                                    const std::wstring& cmd) {
    ULONGLONG start = ProcessStart(h);
    double hrs = HoursSinceFt(start);
    std::wstring path = ProcessPath(h);
    std::wstring line = L"Minecraft abierto (" + exe + L", PID " + std::to_wstring(pid) + L") desde las " +
                        LocalTimeStr(start) + L"  ·  lleva " + FormatHours(hrs) + L" abierto";
    if (hrs >= 0 && hrs < 10.0 / 60) r.Flag(line + L"  →  se abrió hace muy poco (pudo cerrarlo y abrirlo antes del SS)");
    else r.Info(line);

    std::wstring lc = ToLower(cmd);
    std::wstring ver = ArgValue(cmd, L"--version");
    if (!ver.empty()) r.Info(L"Versión / perfil: " + ver);
    std::wstring gd = ArgValue(cmd, L"--gameDir");
    if (!gd.empty()) r.Info(L"Carpeta del juego: " + gd);

    size_t p = 0;
    while ((p = lc.find(L"-javaagent:", p)) != std::wstring::npos) {
        size_t s = p + 11;
        size_t e = lc.find_first_of(L" \"", s);
        std::wstring agent = cmd.substr(s, e == std::wstring::npos ? std::wstring::npos : e - s);
        std::wstring al = ToLower(agent);
        bool known = al.find(L"lunar") != std::wstring::npos || al.find(L"feather") != std::wstring::npos ||
                     al.find(L"badlion") != std::wstring::npos || al.find(L"labymod") != std::wstring::npos;
        if (known) r.Info(L"Java agent del launcher: " + agent);
        else r.Flag(L"Java agent cargado en Minecraft: " + agent + L"  →  puede ser un cliente inyectado");
        p = s;
    }
    auto hits = MatchText(Utf8(lc), M_MOD, true);
    if (!hits.empty()) r.Flag(L"La línea de comandos de Minecraft menciona: " + HitsToText(hits));
    r.Info(L"Línea de comandos: " + Truncate(cmd, 600));

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) { r.Info(L"No se pudieron leer las DLL cargadas en Minecraft."); return; }
    std::wstring javaHome;
    if (!path.empty()) javaHome = ToLower(fs::path(path).parent_path().parent_path().wstring());
    static const wchar_t* safe[] = { L"lwjgl", L"jna", L"jinput", L"openal", L"glfw", L"jemalloc", L"sqlite", L"netty",
                                     L"discord", L"zstd", L"lz4", L"snappy", L"oshi", L"jnidispatch", L"imgui",
                                     L"nvidia", L"amd", L"steam", L"overlay", L"graphics-hook" };
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    int other = 0, flagged = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            std::wstring mp = me.szExePath, ml = ToLower(mp), name = ToLower(me.szModule);
            if (PathUnder(ml, c.windir)) continue;
            if (!javaHome.empty() && ml.rfind(javaHome, 0) == 0) continue;
            auto mh = MatchName(name, M_MOD | M_PROC);
            bool suspDir = PathUnder(ml, c.temp) || PathUnder(ml, c.downloads) || PathUnder(ml, c.desktop) ||
                           ml.find(L"\\appdata\\local\\temp\\") != std::wstring::npos;
            bool isSafe = false;
            for (auto s : safe) if (name.find(s) != std::wstring::npos) { isSafe = true; break; }
            if (!mh.empty()) { r.Flag(L"DLL sospechosa dentro de Minecraft: " + mp + L"  →  " + HitsToText(mh)); ++flagged; }
            else if (suspDir && !isSafe) { r.Flag(L"DLL cargada en Minecraft desde Temp/Descargas: " + mp + L"  →  posible inyección"); ++flagged; }
            else if (other < 12) { r.Info(L"DLL externa en Minecraft: " + mp); ++other; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    if (flagged == 0) r.Ok(L"No se detectaron DLL inyectadas en Minecraft.");
}

static void ScanProcesses(Report& r, ScanContext& c) {
    r.Section(L"Minecraft y procesos");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { r.Info(L"No se pudo leer la lista de procesos."); return; }
    std::vector<std::pair<DWORD, std::wstring>> procs;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do { procs.push_back({ pe.th32ProcessID, pe.szExeFile }); } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    int sus = 0, mcCount = 0;
    for (const auto& [pid, name] : procs) {
        std::wstring low = ToLower(name);
        c.running.insert(low);
        if (low == L"javaw.exe" || low == L"java.exe") {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) { r.Info(name + L" (PID " + std::to_wstring(pid) + L"): no se pudo abrir el proceso."); continue; }
            std::wstring cmd = GetCmdLine(h);
            if (cmd.empty() || LooksLikeMinecraft(ToLower(cmd))) {
                ++mcCount;
                AnalyzeMinecraftProcess(r, c, pid, name, h, cmd);
            } else {
                r.Info(L"Otro programa Java abierto (no parece Minecraft): " + name + L" PID " + std::to_wstring(pid));
            }
            CloseHandle(h);
            continue;
        }
        auto hits = MatchName(name, M_PROC | M_MOD);
        if (!hits.empty()) {
            r.Flag(L"Programa sospechoso abierto: " + name + L" (PID " + std::to_wstring(pid) + L")  →  " + HitsToText(hits));
            ++sus;
        }
    }

    if (mcCount == 0) {
        ULONGLONG last = 0;
        for (const auto& e : c.prefetch) if (e.exe == L"javaw.exe" || e.exe == L"java.exe") last = std::max(last, e.written);
        if (last) {
            double h = HoursSinceFt(last);
            std::wstring t = L"Minecraft NO está abierto. Java se usó por última vez " + Ago(h);
            if (h < 0.25) r.Flag(t + L"  →  lo cerró hace muy poco");
            else r.Info(t);
        } else {
            r.Info(L"Minecraft NO está abierto.");
        }
    }

    // Programas usados hace poco que ya estan cerrados
    if (c.prefetchOk) {
        std::vector<const PfEntry*> recent;
        for (const auto& e : c.prefetch)
            if (HoursSinceFt(e.written) <= 0.5 && !c.running.count(e.exe)) recent.push_back(&e);
        std::sort(recent.begin(), recent.end(), [](const PfEntry* a, const PfEntry* b) { return a->written > b->written; });
        if (recent.empty()) r.Ok(L"No hay programas abiertos en los últimos 30 min que ya estén cerrados.");
        else r.Info(L"Programas abiertos en los últimos 30 min que YA ESTÁN CERRADOS:");
        int n = 0;
        for (const auto* e : recent) {
            if (n++ >= 25) break;
            auto hits = MatchName(e->exe, M_PROC | M_MOD);
            std::wstring t = L"Cerrado: " + e->exe + L"  ·  se abrió " + Ago(HoursSinceFt(e->written));
            if (!hits.empty()) { r.Flag(t + L"  →  " + HitsToText(hits)); ++sus; }
            else r.Info(t);
        }
    }
    if (sus == 0) r.Ok(L"Ningún programa sospechoso abierto o cerrado recientemente.");
}

// =================== 3. MODS ===================
// Devuelve true si genero una alerta
static bool ReportJar(Report& r, const fs::path& p, bool verbose) {
    std::wstring name = p.filename().wstring(), lname = ToLower(name);
    bool jarExt = EndsWith(lname, L".jar");
    bool disabled = EndsWith(lname, L".disabled");
    JarResult jr = AnalyzeJar(p);
    if (!jr.isZip) {
        if (verbose && jarExt) r.Info(L"No se pudo leer: " + name);
        return false;
    }
    if (!jarExt && !disabled && jr.classes == 0) return false;  // zip normal
    auto nameHits = MatchName(name, M_MOD);
    std::wstring when = L"  ·  modificado " + Ago(HoursSince(p));
    if (!jarExt && !disabled) {
        r.Flag(L"Archivo .jar DISFRAZADO con otra extensión: " + p.wstring() + when +
               (jr.hits.empty() ? L"" : L"  →  contiene " + HitsToText(jr.hits)));
        return true;
    }
    if (!jr.hits.empty()) {
        std::wstring pre = disabled ? L"Mod prohibido (desactivado): " : L"Mod prohibido: ";
        std::wstring ren = nameHits.empty() ? L"  ·  ¡EL NOMBRE NO LO DICE: posible archivo renombrado!" : L"";
        r.Flag(pre + name + L"  →  " + HitsToText(jr.hits) + ren + when + L"  ·  " + p.parent_path().wstring());
        return true;
    }
    if (!nameHits.empty()) {
        r.Flag(L"Nombre de mod sospechoso: " + name + L"  →  " + HitsToText(nameHits) + when);
        return true;
    }
    if (verbose) r.Info(name + L"  ·  " + std::to_wstring(jr.classes) + L" clases" + when);
    return false;
}

static void FindModsDirs(const fs::path& root, std::vector<fs::path>& out) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    if (ToLower(root.filename().wstring()) == L"mods") { out.push_back(root); return; }
    static const std::set<std::wstring> skip = { L"assets", L"libraries", L"jre", L"runtime", L"natives", L"logs",
        L"saves", L"resourcepacks", L"shaderpacks", L"screenshots", L"cache", L"textures", L"versions",
        L"crash-reports", L"config", L"jre-legacy", L"java-runtime-gamma" };
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        std::wstring n = ToLower(it->path().filename().wstring());
        if (n == L"mods") {
            out.push_back(it->path());
            it.disable_recursion_pending();
            if (out.size() > 80) return;
            continue;
        }
        if (skip.count(n) || it.depth() >= 5) it.disable_recursion_pending();
    }
}

static void ScanMods(Report& r, ScanContext& c) {
    r.Section(L"Mods y clientes");
    std::error_code ec;
    struct Root { fs::path path; const wchar_t* name; };
    std::vector<Root> roots = {
        { c.appdata / L".minecraft",                    L"Minecraft Launcher / SKLauncher" },
        { c.home / L".lunarclient",                     L"Lunar Client" },
        { c.appdata / L".feather",                      L"Feather Client" },
        { c.appdata / L".tlauncher",                    L"TLauncher" },
        { c.appdata / L"PrismLauncher",                 L"Prism Launcher" },
        { c.appdata / L"MultiMC",                       L"MultiMC" },
        { c.appdata / L".badlion",                      L"Badlion Client" },
        { c.home / L"curseforge" / L"minecraft" / L"Instances", L"CurseForge" },
        { c.appdata / L"com.modrinth.theseus",          L"Modrinth App" },
        { c.appdata / L"ModrinthApp",                   L"Modrinth App" },
        { c.appdata / L".technic",                      L"Technic" },
        { c.appdata / L"gdlauncher_next",               L"GDLauncher" },
        { c.appdata / L".labymod",                      L"LabyMod" },
    };
    std::vector<fs::path> modDirs;
    for (const auto& rt : roots) {
        if (!fs::is_directory(rt.path, ec)) continue;
        r.Info(std::wstring(rt.name) + L" detectado: " + rt.path.wstring());
        FindModsDirs(rt.path, modDirs);
    }
    std::sort(modDirs.begin(), modDirs.end());
    modDirs.erase(std::unique(modDirs.begin(), modDirs.end()), modDirs.end());

    int flagged = 0, total = 0;
    for (const auto& dir : modDirs) {
        r.Info(L"Carpeta de mods: " + dir.wstring());
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() >= 1) it.disable_recursion_pending();
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            ++total;
            if (ReportJar(r, it->path(), true)) ++flagged;
        }
    }

    // Versiones instaladas (los clientes de hacks suelen instalarse como "version")
    fs::path versions = c.appdata / L".minecraft" / L"versions";
    if (fs::is_directory(versions, ec)) {
        for (fs::directory_iterator it(versions, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2)) continue;
            std::wstring name = it->path().filename().wstring();
            auto hits = MatchName(name, M_MOD | M_PROC);
            std::wstring when = L"  ·  modificada " + Ago(HoursSince(it->path()));
            if (!hits.empty()) { r.Flag(L"Versión sospechosa instalada: " + name + when + L"  →  " + HitsToText(hits)); ++flagged; }
            else r.Info(L"Versión instalada: " + name + when);
            fs::path jar = it->path() / (name + L".jar");
            if (fs::exists(jar, e2) && ReportJar(r, jar, false)) ++flagged;
        }
    }
    if (flagged == 0)
        r.Ok(L"No se encontraron mods prohibidos (" + std::to_wstring(total) + L" archivos revisados por dentro).");
}

// =================== 4. BORRADOS Y RENOMBRES ===================
static std::wstring PathFromId(HANDLE volRoot, DWORDLONG frn, std::map<DWORDLONG, std::wstring>& cache) {
    auto it = cache.find(frn);
    if (it != cache.end()) return it->second;
    std::wstring res;
    if (volRoot != INVALID_HANDLE_VALUE) {
        FILE_ID_DESCRIPTOR fid{};
        fid.dwSize = sizeof(fid);
        fid.Type = FileIdType;
        fid.FileId.QuadPart = (LONGLONG)frn;
        HANDLE h = OpenFileById(volRoot, &fid, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                FILE_FLAG_BACKUP_SEMANTICS);
        if (h != INVALID_HANDLE_VALUE) {
            wchar_t b[1024];
            DWORD n = GetFinalPathNameByHandleW(h, b, 1024, FILE_NAME_NORMALIZED);
            if (n > 0 && n < 1024) {
                res.assign(b, n);
                if (StartsWith(res, L"\\\\?\\")) res = res.substr(4);
            }
            CloseHandle(h);
        }
    }
    cache[frn] = res;
    return res;
}

// Lee el registro de cambios del disco (USN Journal) de una unidad
static bool ReadUsnVolume(ScanContext& c, wchar_t letter, ULONGLONG windowFt, ULONGLONG& oldest) {
    wchar_t volPath[] = L"\\\\.\\X:";
    volPath[4] = letter;
    HANDLE vol = CreateFileW(volPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (vol == INVALID_HANDLE_VALUE) return false;
    USN_JOURNAL_DATA_V0 jd{};
    DWORD br = 0;
    if (!DeviceIoControl(vol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &jd, sizeof(jd), &br, nullptr)) {
        CloseHandle(vol);
        return false;
    }
    wchar_t rootPath[] = L"X:\\";
    rootPath[0] = letter;
    HANDLE root = CreateFileW(rootPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    std::vector<BYTE> buf(1 << 20);
    std::map<DWORDLONG, std::wstring> dirCache;
    std::map<DWORDLONG, std::wstring> pendingOld;
    READ_USN_JOURNAL_DATA_V0 rd{};
    rd.StartUsn = jd.FirstUsn;
    rd.ReasonMask = 0xFFFFFFFF;
    rd.ReturnOnlyOnClose = FALSE;
    rd.UsnJournalID = jd.UsnJournalID;
    ULONGLONG now = NowFt();
    bool first = true;
    while (DeviceIoControl(vol, FSCTL_READ_USN_JOURNAL, &rd, sizeof(rd), buf.data(), (DWORD)buf.size(), &br, nullptr)) {
        if (br <= sizeof(USN)) break;
        USN next = *(USN*)buf.data();
        BYTE* p = buf.data() + sizeof(USN);
        BYTE* e = buf.data() + br;
        while (p + sizeof(USN_RECORD_V2) <= e) {
            auto* rec = (USN_RECORD_V2*)p;
            if (rec->RecordLength == 0) break;
            if (rec->MajorVersion == 2) {
                ULONGLONG t = (ULONGLONG)rec->TimeStamp.QuadPart;
                if (first) { oldest = t; first = false; }
                DWORD rs = rec->Reason;
                if (t + windowFt >= now &&
                    (rs & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME | USN_REASON_RENAME_NEW_NAME))) {
                    std::wstring name((const WCHAR*)((const BYTE*)rec + rec->FileNameOffset), rec->FileNameLength / 2);
                    if ((rs & USN_REASON_FILE_DELETE) && (rs & USN_REASON_CLOSE)) {
                        if (c.deletes.size() < 400000)
                            c.deletes.push_back({ name, PathFromId(root, rec->ParentFileReferenceNumber, dirCache), t });
                    }
                    if (rs & USN_REASON_RENAME_OLD_NAME) {
                        pendingOld[rec->FileReferenceNumber] = name;
                    } else if (rs & USN_REASON_RENAME_NEW_NAME) {
                        auto it = pendingOld.find(rec->FileReferenceNumber);
                        if (it != pendingOld.end()) {
                            if (it->second != name && c.renames.size() < 200000)
                                c.renames.push_back({ it->second, name,
                                                      PathFromId(root, rec->ParentFileReferenceNumber, dirCache), t });
                            pendingOld.erase(it);
                        }
                    }
                }
            }
            p += rec->RecordLength;
        }
        if (next == rd.StartUsn) break;
        rd.StartUsn = next;
    }
    if (root != INVALID_HANDLE_VALUE) CloseHandle(root);
    CloseHandle(vol);
    return true;
}

static bool IsNoiseDir(const std::wstring& dl) {
    static const wchar_t* noise[] = { L"\\temp", L"cache", L"\\windows\\", L"\\appdata\\local\\microsoft",
        L"\\programdata\\microsoft", L"\\appdata\\local\\packages", L"\\$extend", L"system volume information",
        L"\\appdata\\local\\google", L"\\appdata\\local\\bravesoftware", L"\\appdata\\roaming\\discord",
        L"\\appdata\\roaming\\opera software", L"\\appdata\\local\\nvidia", L"\\.git\\", L"\\node_modules\\",
        L"\\appdata\\local\\d3dscache", L"\\logs" };
    if (EndsWith(dl, L"\\windows")) return true;
    for (auto n : noise) if (dl.find(n) != std::wstring::npos) return true;
    return false;
}
static bool InterestingExt(const std::wstring& ext) {
    static const std::set<std::wstring> s = { L".jar", L".exe", L".dll", L".bat", L".cmd", L".ahk", L".zip",
                                              L".rar", L".7z", L".ps1", L".vbs", L".disabled" };
    return s.count(ext) > 0;
}
static bool IsMouseDir(const std::wstring& dl) {
    static const wchar_t* m[] = { L"lghub", L"logitech", L"razer", L"corsair", L"steelseries", L"bloody",
                                  L"glorious", L"roccat", L"redragon", L"ngenuity", L"masterplus", L"autohotkey" };
    for (auto s : m) if (dl.find(s) != std::wstring::npos) return true;
    return false;
}
static std::wstring CurrentUserSid() {
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return L"";
    DWORD len = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> b(len ? len : 1);
    std::wstring res;
    if (GetTokenInformation(tok, TokenUser, b.data(), len, &len)) {
        LPWSTR s = nullptr;
        if (ConvertSidToStringSidW(((TOKEN_USER*)b.data())->User.Sid, &s)) { res = s; LocalFree(s); }
    }
    CloseHandle(tok);
    return res;
}
static std::vector<wchar_t> FixedNtfsDrives() {
    std::vector<wchar_t> out;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        wchar_t root[] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        wchar_t fsName[16] = {};
        if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fsName, 16)) continue;
        if (_wcsicmp(fsName, L"NTFS") == 0) out.push_back(root[0]);
    }
    return out;
}

static void ScanDeleted(Report& r, ScanContext& c) {
    r.Section(L"Archivos borrados y renombrados");
    ULONGLONG window = (ULONGLONG)(kUsnHours * kFtPerHour);
    wchar_t sysDrive = c.windir.empty() ? L'C' : (wchar_t)towupper(c.windir.wstring()[0]);
    ULONGLONG sysOldest = 0;
    for (wchar_t d : FixedNtfsDrives()) {
        ULONGLONG o = 0;
        if (ReadUsnVolume(c, d, window, o)) {
            c.usnOk = true;
            if (towupper(d) == sysDrive) sysOldest = o;
        }
    }

    if (!c.usnOk) {
        r.Info(L"No se pudo leer el registro de cambios del disco (USN). Abre el programa como administrador.");
    } else {
        if (sysOldest) {
            double cov = HoursSinceFt(sysOldest);
            r.Info(L"El registro de cambios del disco guarda " + FormatHours(cov) + L" de historial.");
            if (cov < 2.0)
                r.Flag(L"El registro de cambios del disco es MUY reciente (" + FormatHours(cov) +
                       L"): pudo ser borrado con \"fsutil usn deletejournal\" para ocultar archivos");
        }

        // ---- Conteos ----
        int del24 = 0, del1h = 0, delUser = 0, emptied = 0, pfDel = 0, recycled24 = 0, recycled1h = 0;
        ULONGLONG lastEmpty = 0;
        for (const auto& d : c.deletes) {
            double h = HoursSinceFt(d.time);
            std::wstring dl = ToLower(d.dir), nl = ToLower(d.name);
            if (dl.find(L"$recycle.bin") != std::wstring::npos && (StartsWith(nl, L"$r") || StartsWith(nl, L"$i"))) {
                if (StartsWith(nl, L"$r")) { ++emptied; lastEmpty = std::max(lastEmpty, d.time); }
                continue;
            }
            ++del24;
            if (h <= 1) ++del1h;
            if (!IsNoiseDir(dl)) ++delUser;
            if (EndsWith(nl, L".pf")) ++pfDel;
        }
        for (const auto& rn : c.renames) {
            if (ToLower(rn.dir).find(L"$recycle.bin") != std::wstring::npos && StartsWith(ToLower(rn.newName), L"$r")) {
                ++recycled24;
                if (HoursSinceFt(rn.time) <= 1) ++recycled1h;
            }
        }
        r.Info(L"Archivos borrados en las últimas 24 h: " + std::to_wstring(del24) + L"  (en carpetas personales, sin temporales: " +
               std::to_wstring(delUser) + L")");
        r.Info(L"Archivos borrados en la última hora: " + std::to_wstring(del1h));
        r.Info(L"Enviados a la papelera: " + std::to_wstring(recycled24) + L" en 24 h  ·  " + std::to_wstring(recycled1h) +
               L" en la última hora");
        if (emptied > 0) {
            double h = HoursSinceFt(lastEmpty);
            std::wstring t = L"Vació la papelera " + Ago(h) + L" (" + std::to_wstring(emptied) + L" archivos eliminados para siempre)";
            if (h < 3) r.Flag(t); else r.Info(t);
        }
        if (pfDel > 0)
            r.Flag(L"Borró " + std::to_wstring(pfDel) + L" archivos de Prefetch (historial de programas) en las últimas 24 h");

        // ---- Detalle de borrados ----
        int infoShown = 0, flagged = 0;
        for (const auto& d : c.deletes) {
            std::wstring dl = ToLower(d.dir), nl = ToLower(d.name);
            if (dl.find(L"$recycle.bin") != std::wstring::npos || EndsWith(nl, L".pf")) continue;
            std::wstring ext = FileExtLower(nl);
            auto hits = MatchName(d.name, M_MOD | M_PROC);
            std::wstring full = (d.dir.empty() ? L"(carpeta ya borrada)" : d.dir) + L"\\" + d.name;
            std::wstring when = L"  ·  " + Ago(HoursSinceFt(d.time));
            bool inMods = EndsWith(dl, L"\\mods") || dl.find(L"\\mods\\") != std::wstring::npos;
            if (!hits.empty()) { r.Flag(L"Borró: " + full + when + L"  →  " + HitsToText(hits)); ++flagged; }
            else if (IsMouseDir(dl) && !IsNoiseDir(dl)) { r.Flag(L"Borró un archivo del software del mouse/macros: " + full + when); ++flagged; }
            else if (inMods || ext == L".ahk" || (ext == L".jar" && !IsNoiseDir(dl))) { r.Flag(L"Borró: " + full + when); ++flagged; }
            else if (InterestingExt(ext) && !IsNoiseDir(dl) && infoShown < 150) { r.Info(L"Borrado: " + full + when); ++infoShown; }
        }

        // ---- Renombres ----
        int renInfo = 0;
        for (const auto& rn : c.renames) {
            std::wstring dl = ToLower(rn.dir), ol = ToLower(rn.oldName), nl = ToLower(rn.newName);
            std::wstring when = L"  ·  " + Ago(HoursSinceFt(rn.time));
            std::wstring where = rn.dir.empty() ? L"" : L"  ·  en " + rn.dir;
            if (dl.find(L"$recycle.bin") != std::wstring::npos && StartsWith(nl, L"$r")) {
                auto hits = MatchName(rn.oldName, M_MOD | M_PROC);
                if (!hits.empty()) { r.Flag(L"Mandó a la papelera: " + rn.oldName + when + L"  →  " + HitsToText(hits)); ++flagged; }
                else if (InterestingExt(FileExtLower(ol)) && renInfo < 150) { r.Info(L"Mandó a la papelera: " + rn.oldName + when); ++renInfo; }
                continue;
            }
            if (EndsWith(ol, L".crdownload") || EndsWith(ol, L".part")) {
                auto hits = MatchName(rn.newName, M_MOD | M_PROC);
                if (!hits.empty()) { r.Flag(L"Descargó: " + rn.newName + where + when + L"  →  " + HitsToText(hits)); ++flagged; }
                else if (InterestingExt(FileExtLower(nl)) && renInfo < 150) { r.Info(L"Descargó: " + rn.newName + where + when); ++renInfo; }
                continue;
            }
            if (EndsWith(ol, L".tmp") || StartsWith(ol, L"~") || IsNoiseDir(dl)) continue;
            std::wstring oe = FileExtLower(ol), ne = FileExtLower(nl);
            auto oh = MatchName(rn.oldName, M_MOD | M_PROC), nh = MatchName(rn.newName, M_MOD | M_PROC);
            bool important = oe == L".jar" || oe == L".exe" || oe == L".dll" || ne == L".jar" || ne == L".exe" || ne == L".dll";
            std::wstring txt = rn.oldName + L"  →  " + rn.newName + where + when;
            if (!oh.empty() && nh.empty()) { r.Flag(L"RENOMBRÓ un archivo sospechoso: " + txt + L"  (antes: " + HitsToText(oh) + L")"); ++flagged; }
            else if (oe != ne && important) { r.Flag(L"Cambió la extensión de un archivo: " + txt); ++flagged; }
            else if (!nh.empty()) { r.Flag(L"Renombró a un nombre sospechoso: " + txt); ++flagged; }
            else if (important && renInfo < 150) { r.Info(L"Renombrado: " + txt); ++renInfo; }
        }
        if (flagged == 0) r.Ok(L"No se detectaron borrados ni renombres sospechosos en las últimas 24 h.");
    }

    // ---- Lo que hay ahora en la papelera ----
    std::wstring sid = CurrentUserSid();
    if (!sid.empty()) {
        int count = 0, recent = 0, shown = 0;
        for (wchar_t d : FixedNtfsDrives()) {
            fs::path dir = fs::path(std::wstring(1, d) + L":\\") / L"$Recycle.Bin" / sid;
            std::error_code ec;
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::wstring n = it->path().filename().wstring();
                if (!StartsWith(n, L"$I")) continue;
                std::string data;
                if (!ReadWholeFile(it->path(), data, 4096) || data.size() < 28) continue;
                const unsigned char* b = (const unsigned char*)data.data();
                uint32_t ver = Rd32(b);
                ULONGLONG delTime = (ULONGLONG)Rd32(b + 16) | ((ULONGLONG)Rd32(b + 20) << 32);
                std::wstring orig;
                if (ver == 2 && data.size() >= 28) {
                    uint32_t chars = Rd32(b + 24);
                    size_t bytes = std::min<size_t>((size_t)chars * 2, data.size() - 28);
                    orig.assign((const wchar_t*)(b + 28), bytes / 2);
                } else if (data.size() >= 24 + 2) {
                    size_t bytes = std::min<size_t>(520, data.size() - 24);
                    orig.assign((const wchar_t*)(b + 24), bytes / 2);
                }
                orig = std::wstring(orig.c_str());  // corta en el primer nulo
                ++count;
                double h = HoursSinceFt(delTime);
                if (h >= 0 && h <= 24) ++recent;
                auto hits = MatchName(FileNameOfPath(orig), M_MOD | M_PROC);
                if (!hits.empty()) r.Flag(L"En la papelera: " + orig + L"  ·  borrado " + Ago(h) + L"  →  " + HitsToText(hits));
                else if (h >= 0 && h <= 24 && InterestingExt(FileExtLower(orig)) && shown < 40) {
                    r.Info(L"En la papelera: " + orig + L"  ·  borrado " + Ago(h));
                    ++shown;
                }
            }
        }
        r.Info(L"Elementos en la papelera ahora: " + std::to_wstring(count) + L"  (" + std::to_wstring(recent) +
               L" borrados en las últimas 24 h)");
    }
}

// =================== 5. ARCHIVOS RECIENTES Y .JAR OCULTOS ===================
static bool CommonZipExt(const std::wstring& ext) {
    static const std::set<std::wstring> s = { L".zip", L".docx", L".xlsx", L".pptx", L".odt", L".ods", L".apk", L".epub",
        L".nupkg", L".whl", L".vsix", L".crx", L".xpi", L".mcpack", L".mcworld", L".mcaddon", L".mrpack", L".xapk",
        L".jar", L".appx", L".msix", L".3mf", L".kmz", L".ora", L".sketch", L".aab" };
    return s.count(ext) > 0;
}

static void ScanRecentFiles(Report& r, ScanContext& c) {
    r.Section(L"Archivos recientes y .jar escondidos");
    std::vector<std::pair<fs::path, std::wstring>> dirs = {
        { c.downloads, L"Descargas" }, { c.desktop, L"Escritorio" }, { c.home / L"Documents", L"Documentos" },
        { c.appdata, L"AppData\\Roaming" }, { c.localApp, L"AppData\\Local" }, { c.temp, L"Temp" } };
    static const std::set<std::wstring> watched = { L".jar", L".exe", L".dll", L".bat", L".ahk", L".cmd", L".ps1", L".vbs" };
    int flags0 = r.Flags(), zipChecks = 0;
    for (const auto& [dir, label] : dirs) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        int shown = 0;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() >= 2) it.disable_recursion_pending();
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            const fs::path& p = it->path();
            std::wstring lname = ToLower(p.filename().wstring());
            std::wstring ext = FileExtLower(lname);
            uintmax_t size = it->file_size(e2);
            if (ext == L".jar" || EndsWith(lname, L".jar.disabled")) {
                if (ReportJar(r, p, false)) continue;
            } else if (!e2 && size > 100 && size < (30u << 20) && zipChecks < 5000 && !CommonZipExt(ext)) {
                ++zipChecks;
                if (StartsWithPK(p) && ReportJar(r, p, false)) continue;
            }
            if (!watched.count(ext)) continue;
            double h = HoursSince(p);
            if (h < 0 || h > kRecentHours) continue;
            auto hits = MatchName(p.filename().wstring(), M_MOD | M_PROC);
            std::wstring msg = p.wstring() + L"  ·  " + Ago(h);
            if (!hits.empty()) r.Flag(msg + L"  →  " + HitsToText(hits));
            else if (shown < 80) { r.Info(msg); ++shown; }
        }
    }
    if (r.Flags() == flags0) r.Ok(L"No hay archivos recientes sospechosos ni .jar disfrazados.");
}

// =================== 6. HISTORIAL WEB ===================
struct BrowserDb { std::wstring browser, profile; std::vector<fs::path> files; };

static void AddChromium(const std::wstring& browser, const fs::path& userData, std::vector<BrowserDb>& out) {
    std::error_code ec;
    if (!fs::is_directory(userData, ec)) return;
    auto add = [&](const fs::path& dir, const std::wstring& profile) {
        if (!fs::exists(dir / L"History", ec)) return;
        BrowserDb db{ browser, profile, { dir / L"History" } };
        if (fs::exists(dir / L"History-journal", ec)) db.files.push_back(dir / L"History-journal");
        out.push_back(db);
    };
    add(userData, L"");
    for (fs::directory_iterator it(userData, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) add(it->path(), it->path().filename().wstring());
    }
}

static void ScanBrowsers(Report& r, ScanContext& c) {
    r.Section(L"Historial web");
    std::vector<BrowserDb> dbs;
    AddChromium(L"Google Chrome", c.localApp / L"Google" / L"Chrome" / L"User Data", dbs);
    AddChromium(L"Microsoft Edge", c.localApp / L"Microsoft" / L"Edge" / L"User Data", dbs);
    AddChromium(L"Brave", c.localApp / L"BraveSoftware" / L"Brave-Browser" / L"User Data", dbs);
    AddChromium(L"Vivaldi", c.localApp / L"Vivaldi" / L"User Data", dbs);
    AddChromium(L"Yandex", c.localApp / L"Yandex" / L"YandexBrowser" / L"User Data", dbs);
    AddChromium(L"Chromium", c.localApp / L"Chromium" / L"User Data", dbs);
    AddChromium(L"Opera", c.appdata / L"Opera Software" / L"Opera Stable", dbs);
    AddChromium(L"Opera GX", c.appdata / L"Opera Software" / L"Opera GX Stable", dbs);
    std::error_code ec;
    fs::path ff = c.appdata / L"Mozilla" / L"Firefox" / L"Profiles";
    for (fs::directory_iterator it(ff, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!fs::exists(it->path() / L"places.sqlite", e2)) continue;
        BrowserDb db{ L"Firefox", it->path().filename().wstring(), { it->path() / L"places.sqlite" } };
        if (fs::exists(it->path() / L"places.sqlite-wal", e2)) db.files.push_back(it->path() / L"places.sqlite-wal");
        dbs.push_back(db);
    }
    if (dbs.empty()) { r.Info(L"No se encontró historial de navegadores."); return; }

    int flagged = 0;
    for (const auto& db : dbs) {
        std::string all;
        for (const auto& f : db.files) {
            std::string data;
            if (ReadWholeFile(f, data, 700u << 20)) all += data;
        }
        for (auto& ch : all) if ((unsigned char)ch < 128) ch = (char)std::tolower((unsigned char)ch);
        std::wstring who = db.browser + (db.profile.empty() ? L"" : L" (" + db.profile + L")");
        bool any = false;
        for (const auto& rule : g_rules) {
            if (rule.type != R_WEB) continue;
            std::wstring found;
            size_t total = 0;
            for (const auto& t : rule.tokens) {
                size_t n = CountOccurrences(all, t);
                if (n) {
                    total += n;
                    if (!found.empty()) found += L", ";
                    found += L"\"" + FromUtf8(t) + L"\" ×" + std::to_wstring(n);
                }
            }
            if (total) {
                r.Flag(who + L": visitó o buscó " + rule.name + L"  →  " + found);
                any = true;
                ++flagged;
            }
        }
        if (!any) r.Info(who + L": sin páginas de hacks");
    }
    if (flagged == 0) r.Ok(L"No se encontraron páginas de hacks en " + std::to_wstring(dbs.size()) + L" historial(es).");
    r.Info(L"Incluye restos de historial borrado. El modo incógnito no deja historial.");
}

// =================== 7. MOUSE Y MACROS ===================
struct MouseSw { const wchar_t* name; std::vector<std::wstring> procs; std::vector<fs::path> dirs; };

static void ScanMouse(Report& r, ScanContext& c) {
    r.Section(L"Mouse y macros");
    const fs::path& pf = c.programFiles;
    const fs::path& pfx = c.programFilesX86;
    std::vector<MouseSw> list = {
        { L"Logitech G HUB", { L"lghub.exe", L"lghub_agent.exe", L"lghub_system_tray.exe" }, { c.localApp / L"LGHUB", pf / L"LGHUB" } },
        { L"Logitech Gaming Software", { L"lcore.exe" }, { c.localApp / L"Logitech" / L"Logitech Gaming Software", pf / L"Logitech Gaming Software" } },
        { L"Razer Synapse", { L"razer synapse 3.exe", L"razer synapse.exe", L"razercentralservice.exe", L"razer synapse service.exe" },
          { c.localApp / L"Razer", c.programData / L"Razer", pfx / L"Razer" } },
        { L"Corsair iCUE", { L"icue.exe", L"corsair.service.exe" }, { c.appdata / L"Corsair", c.programData / L"Corsair" } },
        { L"SteelSeries GG / Engine", { L"steelseriesgg.exe", L"steelseriesengine.exe" }, { c.programData / L"SteelSeries" } },
        { L"Bloody (A4Tech)", { L"bloody7.exe", L"bloody8.exe", L"bloody9.exe" }, { pfx / L"Bloody7", pfx / L"Bloody8", pfx / L"Bloody9", pf / L"Bloody7" } },
        { L"Glorious Core", { L"glorious core.exe", L"gloriouscore.exe" }, { pfx / L"Glorious Core", pf / L"Glorious Core" } },
        { L"Roccat Swarm", { L"roccat_swarm.exe" }, { c.appdata / L"ROCCAT" } },
        { L"HyperX NGENUITY", { L"ngenuity.exe" }, {} },
        { L"Redragon", {}, { pfx / L"Redragon", pf / L"Redragon" } },
        { L"Cooler Master MasterPlus", { L"masterplus.exe" }, { pfx / L"CoolerMaster" } },
        { L"X-Mouse Button Control", { L"xmousebuttoncontrol.exe" }, { c.appdata / L"Highresolution Enterprises" } },
        { L"AutoHotkey", { L"autohotkey.exe", L"autohotkey64.exe", L"autohotkeyu64.exe", L"autohotkey32.exe" }, { pf / L"AutoHotkey" } },
    };
    static const std::set<std::wstring> cfgExt = { L".json", L".db", L".xml", L".ini", L".cfg", L".dat", L".amc",
                                                   L".mcf", L".txt", L".sqlite", L".lua", L".mac", L".ahk" };
    int found = 0;
    for (const auto& sw : list) {
        bool running = false;
        for (const auto& p : sw.procs) if (c.running.count(p)) running = true;
        std::vector<fs::path> existing;
        std::error_code ec;
        for (const auto& d : sw.dirs) if (!d.empty() && fs::is_directory(d, ec)) existing.push_back(d);
        if (!running && existing.empty()) continue;
        ++found;
        r.Info(std::wstring(sw.name) + (running ? L"  ·  ABIERTO ahora" : L"  ·  instalado"));

        double newestH = 1e9;
        fs::path newestFile;
        size_t macroRefs = 0;
        fs::path macroFile;
        int checked = 0, visited = 0;
        for (const auto& d : existing) {
            fs::recursive_directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end && visited < 4000; it.increment(ec)) {
                ++visited;
                if (it.depth() >= 4) it.disable_recursion_pending();
                std::error_code e2;
                if (!it->is_regular_file(e2)) continue;
                std::wstring ext = FileExtLower(it->path().filename().wstring());
                if (!cfgExt.count(ext)) continue;
                double h = HoursSince(it->path());
                if (h >= 0 && h < newestH) { newestH = h; newestFile = it->path(); }
                uintmax_t size = it->file_size(e2);
                if (!e2 && size < (20u << 20) && checked < 300) {
                    ++checked;
                    std::string data;
                    if (ReadWholeFile(it->path(), data, 20u << 20)) {
                        size_t n = CountOccurrences(ToLowerA(data), "macro");
                        if (n) { macroRefs += n; if (macroFile.empty()) macroFile = it->path(); }
                    }
                }
            }
        }
        if (!newestFile.empty()) {
            std::wstring t = L"Configuración de " + std::wstring(sw.name) + L" modificada " + Ago(newestH) + L": " + newestFile.wstring();
            if (newestH < 0.5) r.Flag(t + L"  →  pudo editar o borrar macros justo antes del SS");
            else if (newestH < 24) r.Info(t);
        }
        if (macroRefs)
            r.Info(L"Referencias a \"macro\" en la configuración: " + std::to_wstring(macroRefs) + L" (ej. " +
                   macroFile.filename().wstring() + L"). Revisa el software del mouse durante el SS.");

        int delCount = 0;
        ULONGLONG lastDel = 0;
        for (const auto& del : c.deletes) {
            std::wstring dl = ToLower(del.dir) + L"\\";
            for (const auto& d : existing)
                if (PathUnder(dl, d)) { ++delCount; lastDel = std::max(lastDel, del.time); break; }
        }
        if (delCount)
            r.Flag(L"Borró " + std::to_wstring(delCount) + L" archivo(s) de la carpeta de " + sw.name +
                   L" en las últimas 24 h (el último " + Ago(HoursSinceFt(lastDel)) + L")");
    }
    if (found == 0) r.Ok(L"No se detectó software de mouse con macros ni AutoHotkey.");
    r.Info(L"Nota: los macros guardados en la memoria interna del mouse no se pueden ver desde la PC.");
}

// =================== 8. PREFETCH ===================
static void ScanPrefetch(Report& r, ScanContext& c) {
    r.Section(L"Prefetch (programas ejecutados)");
    DWORD en = 3, sz = sizeof(en);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management\\PrefetchParameters",
                     L"EnablePrefetcher", RRF_RT_REG_DWORD, nullptr, &en, &sz) == ERROR_SUCCESS && en == 0)
        r.Flag(L"Prefetch está DESACTIVADO en el registro (así Windows no guarda qué programas se abrieron)");
    if (!c.prefetchOk) { r.Info(L"Sin acceso a Prefetch. Abre el programa como administrador."); return; }

    ULONGLONG oldest = ~0ULL;
    for (const auto& e : c.prefetch) oldest = std::min(oldest, e.created);
    r.Info(L"Registros en Prefetch: " + std::to_wstring(c.prefetch.size()));
    if (!c.prefetch.empty()) {
        double h = HoursSinceFt(oldest);
        if (c.prefetch.size() < 20 || h < 6)
            r.Flag(L"Prefetch tiene muy pocos registros o todos son nuevos (el más antiguo es de " + Ago(h) +
                   L"): pudo haber sido limpiado");
    }
    int found = 0;
    for (const auto& e : c.prefetch) {
        auto hits = MatchName(e.exe, M_PROC | M_MOD);
        if (hits.empty()) continue;
        r.Flag(e.exe + L"  ·  última ejecución " + Ago(HoursSinceFt(e.written)) + L"  →  " + HitsToText(hits));
        ++found;
    }
    if (found == 0) r.Ok(L"Nada sospechoso en Prefetch.");
}
