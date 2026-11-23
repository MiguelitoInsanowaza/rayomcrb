#pragma once
// =====================================================================
//  util.h - utilidades, reporte, reglas de deteccion y lector de .jar
// =====================================================================

// ------------------------------ texto ---------------------------------
static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}
static std::string ToLowerA(std::string s) {
    for (auto& c : s) if ((unsigned char)c < 128) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::wstring ToUpper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return s;
}
static std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        out.push_back(Trim(s.substr(start, p == std::wstring::npos ? std::wstring::npos : p - start)));
        if (p == std::wstring::npos) break;
        start = p + 1;
    }
    return out;
}
static bool StartsWith(const std::wstring& s, const std::wstring& pre) {
    return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}
static bool EndsWith(const std::wstring& s, const std::wstring& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static std::wstring Truncate(const std::wstring& s, size_t n) {
    return s.size() <= n ? s : s.substr(0, n) + L"…";
}
static std::wstring FileNameOfPath(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}
static std::wstring FileExtLower(const std::wstring& name) {
    size_t p = name.find_last_of(L'.');
    return p == std::wstring::npos ? L"" : ToLower(name.substr(p));
}

// ------------------------------ tiempo --------------------------------
static ULONGLONG FileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}
static ULONGLONG NowFt() {
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    return FileTimeToU64(f);
}
static const double kFtPerHour = 36000000000.0;  // unidades de 100 ns en una hora

static double HoursSinceFt(ULONGLONG ft) {
    if (ft == 0) return -1;
    ULONGLONG n = NowFt();
    return n <= ft ? 0 : (n - ft) / kFtPerHour;
}
// Horas desde que el archivo se creo o modifico (lo mas reciente)
static double HoursSince(const fs::path& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return -1;
    return HoursSinceFt(std::max(FileTimeToU64(d.ftCreationTime), FileTimeToU64(d.ftLastWriteTime)));
}
static bool FileTimes(const fs::path& p, ULONGLONG& created, ULONGLONG& written) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return false;
    created = FileTimeToU64(d.ftCreationTime);
    written = FileTimeToU64(d.ftLastWriteTime);
    return true;
}
static std::wstring FormatHours(double h) {
    if (h < 0) return L"?";
    double m = h * 60;
    if (m < 1) return L"menos de 1 min";
    if (h < 1) return std::to_wstring((int)m) + L" min";
    if (h < 48) {
        int hh = (int)h;
        int mm = (int)((h - hh) * 60);
        return std::to_wstring(hh) + L" h " + std::to_wstring(mm) + L" min";
    }
    return std::to_wstring((int)(h / 24)) + L" días";
}
static std::wstring Ago(double h) { return L"hace " + FormatHours(h); }
static std::wstring LocalTimeStr(ULONGLONG ft) {
    FILETIME u, l;
    u.dwLowDateTime = (DWORD)ft;
    u.dwHighDateTime = (DWORD)(ft >> 32);
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&u, &l) || !FileTimeToSystemTime(&l, &st)) return L"?";
    wchar_t b[32];
    swprintf(b, 32, L"%02d/%02d %02d:%02d", st.wDay, st.wMonth, st.wHour, st.wMinute);
    return b;
}

// ------------------------------ sistema -------------------------------
static fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) result = p;
    CoTaskMemFree(p);
    return result;
}
static HANDLE OpenShared(const fs::path& p) {
    return CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
static bool ReadAt(HANDLE h, ULONGLONG off, void* buf, DWORD len) {
    OVERLAPPED ov{};
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    DWORD got = 0;
    return ReadFile(h, buf, len, &got, &ov) && got == len;
}
// Lee hasta `cap` bytes de un archivo aunque este abierto por otro programa
static bool ReadWholeFile(const fs::path& p, std::string& out, size_t cap) {
    HANDLE h = OpenShared(p);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
    size_t n = (size_t)std::min<LONGLONG>(sz.QuadPart, (LONGLONG)cap);
    out.resize(n);
    size_t done = 0;
    while (done < n) {
        DWORD chunk = (DWORD)std::min<size_t>(n - done, 16u << 20);
        DWORD got = 0;
        OVERLAPPED ov{};
        ov.Offset = (DWORD)done;
        ov.OffsetHigh = (DWORD)((ULONGLONG)done >> 32);
        if (!ReadFile(h, &out[done], chunk, &got, &ov) || got == 0) break;
        done += got;
    }
    out.resize(done);
    CloseHandle(h);
    return true;
}
static size_t CountOccurrences(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return 0;
    size_t n = 0, p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
    return n;
}

// ======================= MENSAJES ESCANER -> UI ========================
#define WM_APP_LOG   (WM_APP + 1)
#define WM_APP_STEP  (WM_APP + 2)
#define WM_APP_DONE  (WM_APP + 3)
#define WM_APP_ABORT (WM_APP + 4)

enum Kind { K_SECTION = 0, K_INFO = 1, K_FLAG = 2, K_OK = 3 };
struct LogMsg  { int kind; std::wstring text; };
struct DoneMsg { int flags; std::wstring path; bool saved; int upload; std::wstring uploadMsg; };

class Report {
public:
    explicit Report(HWND hwnd) : hwnd_(hwnd) {}
    void Raw(const std::wstring& s)     { lines_.push_back(Utf8(s)); }
    void Section(const std::wstring& t) { Raw(L""); Raw(L"==== " + t + L" ===="); Post(K_SECTION, t); }
    void Info(const std::wstring& s)    { Raw(L"  [-] " + s); Post(K_INFO, s); }
    void Ok(const std::wstring& s)      { Raw(L"  [OK] " + s); Post(K_OK, s); }
    void Flag(const std::wstring& s) {
        ++flags_;
        flagList_.push_back(s);
        Raw(L"  [!] " + s);
        Post(K_FLAG, s);
    }
    int Flags() const { return flags_; }
    const std::vector<std::wstring>& FlagList() const { return flagList_; }
    std::string Text() const {
        std::string t;
        for (const auto& l : lines_) { t += l; t += "\n"; }
        return t;
    }
    bool Save(const fs::path& file) const {
        std::ofstream f(file, std::ios::binary);
        if (!f) return false;
        f << "\xEF\xBB\xBF";
        for (const auto& l : lines_) f << l << "\r\n";
        return true;
    }
private:
    void Post(int kind, const std::wstring& t) {
        auto* m = new LogMsg{ kind, t };
        if (!PostMessageW(hwnd_, WM_APP_LOG, 0, (LPARAM)m)) delete m;
    }
    HWND hwnd_;
    std::vector<std::string> lines_;
    std::vector<std::wstring> flagList_;
    int flags_ = 0;
};

// ======================== REGLAS DE DETECCION ==========================
// R_MOD  : se busca en nombres de archivo Y DENTRO de los .jar (clases/carpetas)
// R_WEB  : se busca en el historial de los navegadores
// R_PROC : se busca en nombres de procesos y archivos
enum RuleType { R_MOD = 0, R_WEB = 1, R_PROC = 2 };
enum { M_MOD = 1, M_WEB = 2, M_PROC = 4 };

struct Rule { int type; std::wstring name; std::vector<std::string> tokens; };
static std::vector<Rule> g_rules;

static void AddRule(int type, const wchar_t* name, std::initializer_list<const char*> toks) {
    Rule r;
    r.type = type;
    r.name = name;
    for (auto t : toks) r.tokens.push_back(t);
    g_rules.push_back(r);
}

static void LoadDefaultRules() {
    g_rules.clear();
    // ---- Mods prohibidos (lista del servidor) ----
    AddRule(R_MOD, L"AutoTotem",                {"autototem", "auto_totem", "auto-totem"});
    AddRule(R_MOD, L"Elytra Equip / Swap",      {"elytraswap", "elytra_swap", "elytra-swap", "autoelytra", "auto_elytra",
                                                 "elytraequip", "elytra_equip", "elytra-equip"});
    AddRule(R_MOD, L"FastPlace",                {"fastplace", "fast_place", "fast-place"});
    AddRule(R_MOD, L"X-Ray",                    {"xray", "x-ray", "x_ray"});
    AddRule(R_MOD, L"Glow / Outline ESP",       {"glowesp", "glow_esp", "outlineesp", "outline_esp"});
    AddRule(R_MOD, L"Player / Entity ESP",      {"playeresp", "player_esp", "entityesp", "entity_esp", "/esp/"});
    AddRule(R_MOD, L"NameTags",                 {"nametags", "name_tags"});
    AddRule(R_MOD, L"Accurate Block Placement", {"accurateblockplacement", "accurate_block_placement", "accurateplacement"});
    AddRule(R_MOD, L"Freecam",                  {"freecam", "free_cam"});
    AddRule(R_MOD, L"No Input Lag / Tick Rate", {"noinputlag", "no_input_lag", "tickrate", "tick_rate"});
    AddRule(R_MOD, L"Attack Through Grass",     {"attackthrough", "attack_through", "throughgrass", "through_grass", "hitthrough"});
    AddRule(R_MOD, L"Item Scroller",            {"itemscroller", "item_scroller"});
    AddRule(R_MOD, L"Mouse Tweaks",             {"mousetweaks", "mouse_tweaks"});
    AddRule(R_MOD, L"NoJumpDelay",              {"nojumpdelay", "no_jump_delay", "jumpdelay"});
    AddRule(R_MOD, L"Totem Helper",             {"totemhelper", "totem_helper"});
    AddRule(R_MOD, L"Fast Totem",               {"fasttotem", "fast_totem"});
    AddRule(R_MOD, L"Anti Ghost",               {"antighost", "anti_ghost", "anti-ghost"});
    AddRule(R_MOD, L"Tweakeroo (trae Freecam, Accurate Placement y más)", {"tweakeroo"});
    // ---- Clientes de hacks ----
    AddRule(R_MOD, L"Doomsday Client",  {"doomsday"});
    AddRule(R_MOD, L"Vape",             {"vape"});
    AddRule(R_MOD, L"Meteor Client",    {"meteordevelopment", "meteor-client", "meteorclient"});
    AddRule(R_MOD, L"Wurst",            {"wurstclient", "net/wurst"});
    AddRule(R_MOD, L"LiquidBounce",     {"liquidbounce", "net/ccbluex"});
    AddRule(R_MOD, L"Aristois",         {"aristois"});
    AddRule(R_MOD, L"RusherHack",       {"rusherhack"});
    AddRule(R_MOD, L"ThunderHack",      {"thunderhack"});
    AddRule(R_MOD, L"BleachHack",       {"bleachhack"});
    AddRule(R_MOD, L"Impact / Future",  {"impactclient", "futureclient"});
    AddRule(R_MOD, L"Baritone",         {"baritone"});
    AddRule(R_MOD, L"Konas / Phobos / SalHack", {"konas", "phobos", "salhack"});
    AddRule(R_MOD, L"Inertia / Boze / Wolfram / Huzuni", {"inertiaclient", "bozeclient", "wolfram", "huzuni"});
    AddRule(R_MOD, L"Novoline / Raven / Drip / Entropy / Whiteout",
            {"novoline", "ravenb", "ravenclient", "dripclient", "driplite", "drip-lite", "entropyclient", "whiteout"});
    AddRule(R_MOD, L"Funciones de cheat", {"killaura", "autoclicker", "triggerbot", "aimassist", "aimbot", "reachhack", "velocityhack"});
    // ---- Programas ----
    AddRule(R_PROC, L"Autoclicker",       {"autoclick", "auto-click", "auto clicker", "clicker", "gcclicker"});
    AddRule(R_PROC, L"Macros / AutoHotkey", {"autohotkey", ".ahk", "macro"});
    AddRule(R_PROC, L"Inyector / Cheat Engine", {"injector", "inject", "cheatengine", "cheat engine", "processhacker"});
    AddRule(R_PROC, L"Cliente de hacks",  {"doomsday", "vape", "wurst", "meteor", "liquidbounce", "aristois", "rusherhack",
                                           "thunderhack", "novoline", "whiteout", "entropy", "raven"});
    AddRule(R_PROC, L"Palabra sospechosa", {"cheat", "hack", "xray"});
    // ---- Paginas web ----
    AddRule(R_WEB, L"Doomsday Client", {"doomsdayclient.com", "doomsdayclient"});
    AddRule(R_WEB, L"Vape",            {"vape.gg", "intent.store"});
    AddRule(R_WEB, L"Meteor Client",   {"meteorclient.com"});
    AddRule(R_WEB, L"Wurst",           {"wurstclient.net"});
    AddRule(R_WEB, L"LiquidBounce",    {"liquidbounce.net"});
    AddRule(R_WEB, L"Aristois",        {"aristois.net"});
    AddRule(R_WEB, L"RusherHack",      {"rusherhack.org"});
    AddRule(R_WEB, L"Rise",            {"riseclient.com"});
    AddRule(R_WEB, L"Novoline",        {"novoline."});
    AddRule(R_WEB, L"Búsquedas de hacks", {"hacked+client", "hacked-client", "hacked%20client", "ghost+client", "ghost-client",
                                           "ghostclient", "autoclicker", "auto+clicker", "xray+mod", "x-ray+mod", "killaura"});
}

// Agrega reglas descargadas del panel. Formato por linea: tipo|nombre|token1,token2
static int LoadRemoteRules(const std::wstring& text) {
    int n = 0;
    for (const auto& line : Split(text, L'\n')) {
        if (line.empty()) continue;
        auto parts = Split(line, L'|');
        if (parts.size() < 3) continue;
        Rule r;
        r.type = parts[0] == L"web" ? R_WEB : (parts[0] == L"process" ? R_PROC : R_MOD);
        r.name = parts[1];
        for (const auto& t : Split(parts[2], L','))
            if (!t.empty()) r.tokens.push_back(Utf8(ToLower(t)));
        if (!r.tokens.empty()) { g_rules.push_back(r); ++n; }
    }
    return n;
}

struct Hit { std::wstring rule; std::string token; };

// Busca reglas en un texto (ya en minusculas, UTF-8).
// namesOnly = true ignora tokens con "/" (solo sirven dentro de los .jar).
static std::vector<Hit> MatchText(const std::string& lower, int mask, bool namesOnly) {
    std::vector<Hit> hits;
    for (const auto& r : g_rules) {
        if (!((1 << r.type) & mask)) continue;
        for (const auto& t : r.tokens) {
            if (namesOnly && t.find('/') != std::string::npos) continue;
            if (lower.find(t) != std::string::npos) { hits.push_back({ r.name, t }); break; }
        }
    }
    return hits;
}
static std::vector<Hit> MatchName(const std::wstring& name, int mask) {
    return MatchText(Utf8(ToLower(name)), mask, true);
}
static std::wstring HitsToText(const std::vector<Hit>& hits) {
    std::wstring s;
    for (size_t i = 0; i < hits.size() && i < 5; ++i) {
        if (i) s += L", ";
        s += hits[i].rule + L" (\"" + FromUtf8(hits[i].token) + L"\")";
    }
    if (hits.size() > 5) s += L" y " + std::to_wstring(hits.size() - 5) + L" más";
    return s;
}

// ========================= LECTOR DE .JAR / ZIP ========================
static uint16_t Rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t Rd32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool StartsWithPK(const fs::path& p) {
    HANDLE h = OpenShared(p);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char b[4] = {};
    bool ok = ReadAt(h, 0, b, 4) && b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4;
    CloseHandle(h);
    return ok;
}
// Lee los nombres de todos los archivos dentro de un zip/jar (directorio central)
static bool ReadZipEntryNames(const fs::path& p, std::vector<std::string>& names, size_t maxEntries = 120000) {
    HANDLE h = OpenShared(p);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    LARGE_INTEGER sz;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 22) {
        DWORD tail = (DWORD)std::min<LONGLONG>(sz.QuadPart, 65557);
        std::vector<unsigned char> buf(tail);
        if (ReadAt(h, (ULONGLONG)sz.QuadPart - tail, buf.data(), tail)) {
            long long eocd = -1;
            for (long long i = (long long)tail - 22; i >= 0; --i) {
                if (buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 5 && buf[i + 3] == 6) { eocd = i; break; }
            }
            if (eocd >= 0) {
                uint32_t cdSize = Rd32(&buf[(size_t)eocd + 12]);
                uint32_t cdOff  = Rd32(&buf[(size_t)eocd + 16]);
                if (cdSize != 0xFFFFFFFF && cdOff != 0xFFFFFFFF && cdSize < (64u << 20) &&
                    (ULONGLONG)cdOff + cdSize <= (ULONGLONG)sz.QuadPart) {
                    std::vector<unsigned char> cd(cdSize);
                    if (cdSize == 0 || ReadAt(h, cdOff, cd.data(), cdSize)) {
                        size_t pos = 0;
                        while (pos + 46 <= cdSize && names.size() < maxEntries) {
                            if (Rd32(&cd[pos]) != 0x02014b50) break;
                            uint16_t n = Rd16(&cd[pos + 28]), x = Rd16(&cd[pos + 30]), c = Rd16(&cd[pos + 32]);
                            if (pos + 46 + n > cdSize) break;
                            names.emplace_back((const char*)&cd[pos + 46], n);
                            pos += 46 + (size_t)n + x + c;
                        }
                        ok = true;
                    }
                }
            }
        }
    }
    CloseHandle(h);
    return ok;
}

struct JarResult { bool isZip = false; size_t entries = 0, classes = 0; std::vector<Hit> hits; };

// Revisa el CONTENIDO de un .jar: aunque le cambien el nombre, las clases siguen ahi
static JarResult AnalyzeJar(const fs::path& p) {
    JarResult jr;
    std::vector<std::string> names;
    if (!ReadZipEntryNames(p, names)) return jr;
    jr.isZip = true;
    jr.entries = names.size();
    std::vector<bool> seen(g_rules.size(), false);
    for (const auto& n : names) {
        std::string l = ToLowerA(n);
        if (l.size() > 6 && l.compare(l.size() - 6, 6, ".class") == 0) ++jr.classes;
        for (size_t i = 0; i < g_rules.size(); ++i) {
            if (seen[i] || g_rules[i].type != R_MOD) continue;
            for (const auto& t : g_rules[i].tokens) {
                if (l.find(t) != std::string::npos) {
                    seen[i] = true;
                    jr.hits.push_back({ g_rules[i].name, t });
                    break;
                }
            }
        }
    }
    return jr;
}
