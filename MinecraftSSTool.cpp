// =====================================================================
//  MinecraftSSTool.cpp - Herramienta de revision (SS) para Minecraft Java
//  Interfaz grafica (Win32 + GDI+). Compilar con compilar.bat
//  Archivos: config.h (panel), util.h, net.h, scans.h
// =====================================================================
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <knownfolders.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <winhttp.h>
#include <winevt.h>
#include <sddl.h>

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wevtapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

#include "config.h"
#include "util.h"
#include "net.h"
#include "scans.h"

using namespace Gdiplus;

// ============================ HILO DE ESCANEO ============================
static const int kSteps = 8;

static void PostAbort(HWND hwnd, const std::wstring& msg) {
    auto* m = new std::wstring(msg);
    if (!PostMessageW(hwnd, WM_APP_ABORT, 0, (LPARAM)m)) delete m;
}

static void RunScan(HWND hwnd, std::wstring license) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ScanContext c;
    c.appdata         = KnownFolder(FOLDERID_RoamingAppData);
    c.home            = KnownFolder(FOLDERID_Profile);
    c.desktop         = KnownFolder(FOLDERID_Desktop);
    c.downloads       = KnownFolder(FOLDERID_Downloads);
    c.localApp        = KnownFolder(FOLDERID_LocalAppData);
    c.programData     = KnownFolder(FOLDERID_ProgramData);
    c.programFiles    = KnownFolder(FOLDERID_ProgramFiles);
    c.programFilesX86 = KnownFolder(FOLDERID_ProgramFilesX86);
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp)) c.temp = tmp;
    if (GetWindowsDirectoryW(tmp, MAX_PATH)) c.windir = tmp;

    LoadDefaultRules();
    std::wstring hwid = GetHwid();

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"%04d-%02d-%02d_%02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    Report r(hwnd);
    r.Raw(L"Reporte SS Minecraft - " + std::wstring(stamp) + L" - versión " + SS_TOOL_VERSION);

    int remoteRules = 0;
    if (Online()) {
        r.Section(L"Licencia");
        auto res = CallRpc(L"check_license", "{\"p_key\":" + JStr(license) + ",\"p_hwid\":" + JStr(hwid) + "}");
        if (!res.ok || res.status >= 300) {
            PostAbort(hwnd, L"No se pudo conectar con el panel. " + HttpErrorText(res));
            CoUninitialize();
            return;
        }
        if (!JsonGetBool(res.body, "ok")) {
            std::wstring msg = JsonGetString(res.body, "msg");
            PostAbort(hwnd, L"Licencia rechazada: " + (msg.empty() ? FromUtf8(res.body) : msg));
            CoUninitialize();
            return;
        }
        r.Ok(L"Licencia válida: " + license);
        auto rr = CallRpc(L"get_rules", "{\"p_key\":" + JStr(license) + ",\"p_hwid\":" + JStr(hwid) + "}");
        if (rr.ok && rr.status < 300) remoteRules = LoadRemoteRules(JsonTopString(rr.body));
    }
    r.Info(L"Reglas de detección cargadas: " + std::to_wstring(g_rules.size()) +
           (Online() ? L" (" + std::to_wstring(remoteRules) + L" desde el panel)" : L""));

    LoadPrefetch(c);

    auto step = [&](int i, auto fn) {
        PostMessageW(hwnd, WM_APP_STEP, (WPARAM)i, 0);
        fn();
        PostMessageW(hwnd, WM_APP_STEP, (WPARAM)i, 1);
    };
    step(0, [&] { ScanSystem(r, c); });
    step(1, [&] { ScanProcesses(r, c); });
    step(2, [&] { ScanMods(r, c); });
    step(3, [&] { ScanDeleted(r, c); });
    step(4, [&] { ScanRecentFiles(r, c); });
    step(5, [&] { ScanBrowsers(r, c); });
    step(6, [&] { ScanMouse(r, c); });
    step(7, [&] { ScanPrefetch(r, c); });

    r.Raw(L"");
    r.Raw(L"==== RESUMEN ====");
    r.Raw(L"  Alertas encontradas: " + std::to_wstring(r.Flags()));
    r.Raw(L"  Nota: una alerta es un indicio, no una prueba. El staff debe revisar cada una.");

    fs::path out = c.desktop / (L"SS_Reporte_" + std::wstring(stamp) + L".txt");
    bool saved = r.Save(out);

    int upload = 0;
    std::wstring upMsg;
    if (Online()) {
        std::string flags = "[";
        size_t n = 0;
        for (const auto& f : r.FlagList()) {
            if (n++ >= 1000) break;
            if (n > 1) flags += ",";
            flags += JStr(f);
        }
        flags += "]";
        std::string body = "{\"p_key\":" + JStr(license) + ",\"p_hwid\":" + JStr(hwid) + ",\"p_pc\":" + JStr(GetPcName()) +
                           ",\"p_user\":" + JStr(GetWinUser()) + ",\"p_version\":" + JStr(SS_TOOL_VERSION) +
                           ",\"p_alerts\":" + std::to_string(r.Flags()) + ",\"p_flags\":" + flags +
                           ",\"p_report\":\"" + JsonEscUtf8(r.Text()) + "\"}";
        auto res = CallRpc(L"submit_scan", body);
        if (res.ok && res.status < 300 && JsonGetBool(res.body, "ok")) {
            upload = 1;
            upMsg = L"Resultados enviados al panel.";
        } else {
            upload = 2;
            std::wstring m = res.ok ? JsonGetString(res.body, "msg") : L"";
            upMsg = L"No se pudieron enviar los resultados al panel: " + (m.empty() ? HttpErrorText(res) : m);
        }
    }

    auto* d = new DoneMsg{ r.Flags(), out.wstring(), saved, upload, upMsg };
    if (!PostMessageW(hwnd, WM_APP_DONE, 0, (LPARAM)d)) delete d;
    CoUninitialize();
}

// ============================== INTERFAZ ===============================
static const Color C_BG      (255,  13,  15,  21);
static const Color C_PANEL   (255,  22,  25,  34);
static const Color C_PANEL2  (255,  31,  35,  48);
static const Color C_BORDER  (255,  42,  47,  63);
static const Color C_TEXT    (255, 232, 234, 242);
static const Color C_MUTED   (255, 138, 144, 162);
static const Color C_ACCENT  (255, 124,  92, 255);
static const Color C_ACCENT_D(255,  98,  70, 214);
static const Color C_GREEN   (255,  46, 204, 113);
static const Color C_RED     (255, 255,  77,  94);
static const Color C_ORANGE  (255, 255, 176,  32);
static const Color C_WHITE   (255, 255, 255, 255);

enum { ID_START = 101, ID_REPORT = 102, ID_LIST = 103, ID_LICENSE = 104, TIMER_ANIM = 1 };

struct StepUI { const wchar_t* name; const wchar_t* desc; int state; int flags; };  // 0 pendiente, 1 escaneando, 2 listo
static StepUI g_steps[kSteps] = {
    { L"Sistema y sesiones",   L"Reinicios, sesiones y registros",     0, 0 },
    { L"Minecraft y procesos", L"Tiempo abierto, inyecciones, cierres", 0, 0 },
    { L"Mods",                 L"Contenido de cada .jar y renombres",   0, 0 },
    { L"Borrados y renombres", L"Disco (USN) y papelera",               0, 0 },
    { L"Archivos recientes",   L"Descargas, Escritorio, AppData",       0, 0 },
    { L"Historial web",        L"Páginas de hacks visitadas",           0, 0 },
    { L"Mouse y macros",       L"Software del mouse y AutoHotkey",      0, 0 },
    { L"Prefetch",             L"Programas ejecutados",                 0, 0 },
};
static int  g_current = -1, g_totalFlags = 0, g_phase = 0, g_upload = 0;
static bool g_running = false, g_done = false, g_isAdmin = false;
static std::wstring g_reportPath;
static float S = 1.0f;
static HWND g_hList = nullptr, g_hStart = nullptr, g_hReport = nullptr, g_hLicense = nullptr;
static HBRUSH g_panelBrush = nullptr, g_editBrush = nullptr;
static HFONT g_hEditFont = nullptr;
static Font *g_fTitle, *g_fNorm, *g_fSmall, *g_fSmallBold, *g_fBold, *g_fSection, *g_fBig;

static float P(float v) { return v * S; }

static void CreateFonts() {
    FontFamily ff(L"Segoe UI");
    g_fTitle     = new Font(&ff, P(22.0f), FontStyleBold,    UnitPixel);
    g_fNorm      = new Font(&ff, P(13.5f), FontStyleRegular, UnitPixel);
    g_fSmall     = new Font(&ff, P(12.0f), FontStyleRegular, UnitPixel);
    g_fSmallBold = new Font(&ff, P(12.0f), FontStyleBold,    UnitPixel);
    g_fBold      = new Font(&ff, P(14.5f), FontStyleBold,    UnitPixel);
    g_fSection   = new Font(&ff, P(11.5f), FontStyleBold,    UnitPixel);
    g_fBig       = new Font(&ff, P(40.0f), FontStyleBold,    UnitPixel);
    g_hEditFont  = CreateFontW(-(int)P(15), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
static void DeleteFonts() {
    delete g_fTitle; delete g_fNorm; delete g_fSmall; delete g_fSmallBold;
    delete g_fBold; delete g_fSection; delete g_fBig;
    if (g_hEditFont) DeleteObject(g_hEditFont);
}

static void FillRound(Graphics& g, RectF r, float rad, const Color& fill, const Color* border = nullptr) {
    rad = std::min(rad, std::min(r.Width, r.Height) / 2.0f);
    if (rad < 0.5f) { SolidBrush b(fill); g.FillRectangle(&b, r); return; }
    GraphicsPath p;
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(fill);
    g.FillPath(&b, &p);
    if (border) { Pen pen(*border, std::max(1.0f, S)); g.DrawPath(&pen, &p); }
}

static void DText(Graphics& g, const std::wstring& s, Font* f, const Color& c, const RectF& r,
                  StringAlignment ha = StringAlignmentNear, StringAlignment va = StringAlignmentCenter,
                  bool wrap = false) {
    StringFormat sf;
    sf.SetAlignment(ha);
    sf.SetLineAlignment(va);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    if (!wrap) sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &sf, &b);
}

static void DrawCheck(Graphics& g, float cx, float cy) {
    Pen pen(C_WHITE, P(2.2f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    PointF pts[3] = { PointF(cx - P(4.5f), cy), PointF(cx - P(1.5f), cy + P(3.5f)), PointF(cx + P(5), cy - P(3.5f)) };
    g.DrawLines(&pen, pts, 3);
}

struct Layout { RectF startBtn, licBox, licEdit, pill, progress, steps[kSteps], summary, reportBtn, panel, list; };

static Layout CalcLayout(int w, int h) {
    Layout L;
    float W = (float)w, H = (float)h, M = P(24);
    L.startBtn = RectF(W - M - P(200), P(22), P(200), P(46));
    L.licBox   = RectF(L.startBtn.X - P(12) - P(250), P(22), P(250), P(46));
    L.licEdit  = RectF(L.licBox.X + P(14), L.licBox.Y + (L.licBox.Height - P(22)) / 2, L.licBox.Width - P(28), P(22));
    L.pill     = RectF(L.licBox.X - P(12) - P(140), P(32), P(140), P(26));
    L.progress = RectF(M, P(88), W - 2 * M, P(6));
    float colW = P(300), y = P(110), cardH = P(42), gap = P(6);
    for (int i = 0; i < kSteps; ++i) L.steps[i] = RectF(M, y + i * (cardH + gap), colW, cardH);
    float sy = y + kSteps * (cardH + gap) + P(4);
    L.reportBtn = RectF(M, H - M - P(46), colW, P(46));
    L.summary = RectF(M, sy, colW, std::max(P(96), L.reportBtn.Y - P(12) - sy));
    float px = M + colW + P(16);
    L.panel = RectF(px, y, W - px - M, H - y - M);
    L.list  = RectF(px + P(8), y + P(50), L.panel.Width - P(16), L.panel.Height - P(58));
    return L;
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(C_BG);
        Layout L = CalcLayout(w, h);

        // ---- Encabezado ----
        RectF logo(P(24), P(22), P(46), P(46));
        FillRound(g, logo, P(12), C_ACCENT);
        DText(g, L"SS", g_fBold, C_WHITE, logo, StringAlignmentCenter, StringAlignmentCenter);
        DText(g, L"Minecraft SS Tool", g_fTitle, C_TEXT, RectF(P(84), P(18), P(300), P(32)));
        DText(g, std::wstring(L"Anti-cheat para Minecraft Java  ·  v") + SS_TOOL_VERSION, g_fNorm, C_MUTED,
              RectF(P(84), P(48), P(300), P(22)));

        FillRound(g, L.pill, P(13), C_PANEL, &C_BORDER);
        SolidBrush dot(g_isAdmin ? C_GREEN : C_ORANGE);
        g.FillEllipse(&dot, L.pill.X + P(12), L.pill.Y + L.pill.Height / 2 - P(4), P(8), P(8));
        DText(g, g_isAdmin ? L"Administrador" : L"Sin admin", g_fSmallBold, C_TEXT,
              RectF(L.pill.X + P(26), L.pill.Y, L.pill.Width - P(30), L.pill.Height));

        if (Online()) {
            FillRound(g, L.licBox, P(10), C_PANEL2, &C_BORDER);
        } else {
            FillRound(g, L.licBox, P(10), C_PANEL, &C_BORDER);
            DText(g, L"Modo local (sin panel)", g_fSmallBold, C_MUTED, L.licBox, StringAlignmentCenter, StringAlignmentCenter);
        }

        // ---- Barra de progreso ----
        FillRound(g, L.progress, P(3), C_PANEL2);
        int doneSteps = 0;
        for (auto& s : g_steps) if (s.state == 2) ++doneSteps;
        float frac = g_done ? 1.0f
                            : (doneSteps + ((g_current >= 0 && g_steps[g_current].state == 1) ? 0.5f : 0.0f)) / (float)kSteps;
        if (frac > 0) {
            RectF pr = L.progress;
            pr.Width = std::max(pr.Width * frac, P(6));
            FillRound(g, pr, P(3), g_done ? (g_totalFlags ? C_RED : C_GREEN) : C_ACCENT);
        }

        // ---- Pasos ----
        for (int i = 0; i < kSteps; ++i) {
            const StepUI& st = g_steps[i];
            RectF r = L.steps[i];
            bool active = st.state == 1;
            FillRound(g, r, P(10), active ? C_PANEL2 : C_PANEL, active ? &C_ACCENT : &C_BORDER);

            float cx = r.X + P(23), cy = r.Y + r.Height / 2, rad = P(10);
            RectF circ(cx - rad, cy - rad, 2 * rad, 2 * rad);
            if (st.state == 0) {
                Pen pen(C_BORDER, P(2));
                g.DrawEllipse(&pen, circ);
                DText(g, std::to_wstring(i + 1), g_fSmallBold, C_MUTED, circ, StringAlignmentCenter, StringAlignmentCenter);
            } else if (st.state == 1) {
                int a = 50 + (int)(40 * std::sin(g_phase * 0.3));
                SolidBrush glow(Color((BYTE)a, 124, 92, 255));
                g.FillEllipse(&glow, circ.X - P(4), circ.Y - P(4), circ.Width + P(8), circ.Height + P(8));
                SolidBrush b(C_ACCENT);
                g.FillEllipse(&b, circ);
                SolidBrush inner(C_WHITE);
                g.FillEllipse(&inner, cx - P(3), cy - P(3), P(6), P(6));
            } else if (st.flags == 0) {
                SolidBrush b(C_GREEN);
                g.FillEllipse(&b, circ);
                DrawCheck(g, cx, cy);
            } else {
                SolidBrush b(C_RED);
                g.FillEllipse(&b, circ);
                DText(g, L"!", g_fBold, C_WHITE, circ, StringAlignmentCenter, StringAlignmentCenter);
            }

            DText(g, st.name, g_fBold, st.state == 0 ? C_MUTED : C_TEXT,
                  RectF(r.X + P(42), r.Y + P(3), r.Width - P(146), P(20)));
            DText(g, st.desc, g_fSmall, C_MUTED, RectF(r.X + P(42), r.Y + P(21), r.Width - P(146), P(17)));

            std::wstring badge;
            Color bc = C_MUTED;
            if (st.state == 1) { badge = L"Escaneando"; bc = C_ACCENT; }
            else if (st.state == 2 && st.flags > 0) {
                badge = std::to_wstring(st.flags) + (st.flags == 1 ? L" alerta" : L" alertas");
                bc = C_RED;
            } else if (st.state == 2) { badge = L"Limpio"; bc = C_GREEN; }
            if (!badge.empty()) {
                RectF br(r.X + r.Width - P(98), cy - P(10), P(88), P(20));
                FillRound(g, br, P(10), Color(45, bc.GetR(), bc.GetG(), bc.GetB()));
                DText(g, badge, g_fSmallBold, bc, br, StringAlignmentCenter, StringAlignmentCenter);
            }
        }

        // ---- Resumen ----
        RectF s = L.summary;
        FillRound(g, s, P(12), C_PANEL, &C_BORDER);
        DText(g, L"RESUMEN", g_fSection, C_MUTED, RectF(s.X + P(18), s.Y + P(10), s.Width - P(36), P(18)));
        std::wstring big, cap;
        Color bigC = C_MUTED;
        if (!g_running && !g_done) { big = L"—"; cap = L"Listo para escanear"; }
        else if (g_running) {
            big = std::to_wstring(g_totalFlags);
            cap = L"alertas hasta ahora…";
            bigC = g_totalFlags ? C_ORANGE : C_ACCENT;
        } else {
            big = std::to_wstring(g_totalFlags);
            cap = g_totalFlags == 0 ? L"Sin alertas: todo limpio"
                                    : (g_totalFlags == 1 ? L"alerta encontrada" : L"alertas encontradas");
            bigC = g_totalFlags ? C_RED : C_GREEN;
        }
        DText(g, big, g_fBig, bigC, RectF(s.X + P(16), s.Y + P(26), s.Width - P(32), P(48)));
        DText(g, cap, g_fBold, C_TEXT, RectF(s.X + P(18), s.Y + P(72), s.Width - P(36), P(20)));
        if (s.Height >= P(130))
            DText(g, L"Una alerta es un indicio, no una prueba. Revisa cada una antes de sancionar.",
                  g_fSmall, C_MUTED, RectF(s.X + P(18), s.Y + P(96), s.Width - P(36), s.Height - P(100)),
                  StringAlignmentNear, StringAlignmentNear, true);

        // ---- Panel de resultados ----
        FillRound(g, L.panel, P(12), C_PANEL, &C_BORDER);
        DText(g, L"Resultados", g_fBold, C_TEXT, RectF(L.panel.X + P(18), L.panel.Y + P(12), P(300), P(26)));
        int count = g_hList ? (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0) : 0;
        std::wstring right = std::to_wstring(count) + L" líneas";
        if (g_done && !g_reportPath.empty()) right = L"Reporte guardado en el Escritorio  ·  " + right;
        if (g_done && g_upload == 1) right = L"Enviado al panel  ·  " + right;
        if (g_done && g_upload == 2) right = L"No se envió al panel  ·  " + right;
        DText(g, right, g_fSmall, C_MUTED,
              RectF(L.panel.X + P(200), L.panel.Y + P(12), L.panel.Width - P(218), P(26)), StringAlignmentFar);
        Pen sep(C_BORDER, std::max(1.0f, S));
        g.DrawLine(&sep, L.panel.X + P(1), L.panel.Y + P(46), L.panel.X + L.panel.Width - P(1), L.panel.Y + P(46));
    }
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void DrawListItem(const DRAWITEMSTRUCT* d) {
    if (d->itemID == (UINT)-1 || d->itemAction == ODA_FOCUS) return;
    int len = (int)SendMessageW(d->hwndItem, LB_GETTEXTLEN, d->itemID, 0);
    std::wstring text(len > 0 ? len : 0, L'\0');
    if (len > 0) SendMessageW(d->hwndItem, LB_GETTEXT, d->itemID, (LPARAM)text.data());
    int kind = (int)SendMessageW(d->hwndItem, LB_GETITEMDATA, d->itemID, 0);

    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    HDC mem = CreateCompatibleDC(d->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(C_PANEL);
        bool sel = (d->itemState & ODS_SELECTED) != 0;
        RectF row(P(2), P(1), w - P(4), h - P(2));
        if (sel) FillRound(g, row, P(6), C_PANEL2);
        else if (kind == K_FLAG) FillRound(g, row, P(6), Color(30, 255, 77, 94));

        float x = P(10), cy = h / 2.0f;
        RectF tr(x + P(26), 0, w - x - P(34), (float)h);
        switch (kind) {
        case K_SECTION: {
            SolidBrush b(C_ACCENT);
            g.FillRectangle(&b, x, cy - P(7), P(3), P(14));
            DText(g, ToUpper(text), g_fSection, C_ACCENT, RectF(x + P(12), 0, w - x - P(20), (float)h));
            break;
        }
        case K_FLAG: {
            RectF badge(x, cy - P(9), P(18), P(18));
            FillRound(g, badge, P(9), C_RED);
            DText(g, L"!", g_fSmallBold, C_WHITE, badge, StringAlignmentCenter, StringAlignmentCenter);
            DText(g, text, g_fNorm, C_TEXT, tr);
            break;
        }
        case K_OK: {
            RectF badge(x, cy - P(9), P(18), P(18));
            FillRound(g, badge, P(9), Color(60, 46, 204, 113));
            SolidBrush b(C_GREEN);
            g.FillEllipse(&b, x + P(5), cy - P(4), P(8), P(8));
            DText(g, text, g_fNorm, C_GREEN, tr);
            break;
        }
        default: {
            SolidBrush b(C_BORDER);
            g.FillEllipse(&b, x + P(6), cy - P(3), P(6), P(6));
            DText(g, text, g_fNorm, C_MUTED, tr);
            break;
        }
        }
    }
    BitBlt(d->hDC, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void DrawButton(const DRAWITEMSTRUCT* d) {
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC mem = CreateCompatibleDC(d->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(C_BG);
        wchar_t txt[64] = {};
        GetWindowTextW(d->hwndItem, txt, 64);
        bool pressed  = (d->itemState & ODS_SELECTED) != 0;
        bool disabled = (d->itemState & ODS_DISABLED) != 0;
        bool primary  = d->CtlID == ID_START;
        Color fill = C_PANEL2, tc = C_TEXT;
        const Color* border = nullptr;
        if (disabled)      { fill = C_PANEL; tc = C_MUTED; border = &C_BORDER; }
        else if (primary)  { fill = pressed ? C_ACCENT_D : C_ACCENT; }
        else               { fill = pressed ? C_PANEL : C_PANEL2; border = &C_BORDER; }
        FillRound(g, RectF(P(1), P(1), w - P(2), h - P(2)), P(10), fill, border);
        DText(g, txt, g_fBold, tc, RectF(0, 0, (float)w, (float)h), StringAlignmentCenter, StringAlignmentCenter);
    }
    BitBlt(d->hDC, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void AddItem(int kind, const std::wstring& text) {
    LRESULT idx = SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)text.c_str());
    if (idx >= 0) SendMessageW(g_hList, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)kind);
}

static void AddIntro() {
    AddItem(K_SECTION, L"Antes de empezar: qué se revisa");
    AddItem(K_INFO, L"Procesos abiertos, cuánto tiempo lleva Minecraft abierto y qué programas se cerraron hace poco.");
    AddItem(K_INFO, L"Mods y versiones de Minecraft, revisando el contenido de cada .jar (detecta archivos renombrados).");
    AddItem(K_INFO, L"Archivos borrados, renombrados y enviados a la papelera en las últimas 24 h.");
    AddItem(K_INFO, L"Archivos recientes en Descargas, Escritorio, Documentos, AppData y Temp.");
    AddItem(K_INFO, L"Historial de navegadores: SOLO se buscan páginas de hacks, no se copia el historial.");
    AddItem(K_INFO, L"Software del mouse (macros), AutoHotkey, sesiones de Windows y Prefetch.");
    if (Online()) {
        AddItem(K_INFO, L"Al terminar se envía el reporte al panel del staff (nombre de PC, usuario de Windows, alertas y reporte).");
        AddItem(K_INFO, L"Escribe el código de licencia que te dio el staff y pulsa «Iniciar escaneo» para aceptar.");
    } else {
        AddItem(K_OK, L"Modo local: no se envía nada a internet. Solo se guarda un reporte .txt en el Escritorio.");
        AddItem(K_INFO, L"Al pulsar «Iniciar escaneo» aceptas la revisión.");
    }
}

static void LayoutChildren(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    Layout L = CalcLayout(rc.right, rc.bottom);
    auto mv = [](HWND h, const RectF& r) {
        MoveWindow(h, (int)r.X, (int)r.Y, (int)r.Width, (int)r.Height, TRUE);
    };
    mv(g_hStart, L.startBtn);
    mv(g_hReport, L.reportBtn);
    mv(g_hList, L.list);
    mv(g_hLicense, L.licEdit);
}

static void SetIdleUi() {
    g_running = false;
    KillTimer(GetParent(g_hStart), TIMER_ANIM);
    SetWindowTextW(g_hStart, g_done ? L"Escanear de nuevo" : L"Iniciar escaneo");
    EnableWindow(g_hStart, TRUE);
    EnableWindow(g_hLicense, TRUE);
}

static void StartScan(HWND hwnd) {
    if (g_running) return;
    std::wstring lic;
    if (Online()) {
        wchar_t buf[128] = {};
        GetWindowTextW(g_hLicense, buf, 128);
        lic = ToUpper(Trim(buf));
        if (lic.empty()) {
            MessageBoxW(hwnd, L"Escribe el código de licencia que te dio el staff.", L"Licencia", MB_ICONINFORMATION);
            SetFocus(g_hLicense);
            return;
        }
    }
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (auto& s : g_steps) { s.state = 0; s.flags = 0; }
    g_current = -1;
    g_totalFlags = 0;
    g_done = false;
    g_upload = 0;
    g_running = true;
    g_reportPath.clear();
    EnableWindow(g_hReport, FALSE);
    EnableWindow(g_hLicense, FALSE);
    SetWindowTextW(g_hStart, L"Escaneando…");
    EnableWindow(g_hStart, FALSE);
    SetTimer(hwnd, TIMER_ANIM, 60, nullptr);
    AddItem(K_INFO, Online() ? L"Comprobando licencia…" : L"Iniciando escaneo…");
    std::thread(RunScan, hwnd, lic).detach();
    InvalidateRect(hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        S = GetDpiForWindow(hwnd) / 96.0f;
        CreateFonts();
        g_isAdmin = IsUserAnAdmin() != FALSE;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
        COLORREF cap = RGB(13, 15, 21);
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
        g_panelBrush = CreateSolidBrush(RGB(22, 25, 34));
        g_editBrush  = CreateSolidBrush(RGB(31, 35, 48));

        HINSTANCE hi = ((LPCREATESTRUCTW)lp)->hInstance;
        g_hStart = CreateWindowW(L"BUTTON", L"Iniciar escaneo", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_START, hi, nullptr);
        g_hReport = CreateWindowW(L"BUTTON", L"Abrir reporte", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_REPORT, hi, nullptr);
        g_hList = CreateWindowExW(0, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                                  LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_LIST, hi, nullptr);
        g_hLicense = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | (Online() ? WS_VISIBLE : 0) | WS_TABSTOP | ES_AUTOHSCROLL | ES_UPPERCASE,
                                     0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_LICENSE, hi, nullptr);
        SendMessageW(g_hLicense, WM_SETFONT, (WPARAM)g_hEditFont, TRUE);
        SendMessageW(g_hLicense, EM_SETCUEBANNER, TRUE, (LPARAM)L"Código de licencia");
        SendMessageW(g_hLicense, EM_LIMITTEXT, 64, 0);
        SetWindowTheme(g_hList, L"DarkMode_Explorer", nullptr);
        AddIntro();
        LayoutChildren(hwnd);
        return 0;
    }
    case WM_SIZE:
        if (g_hList) LayoutChildren(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        RECT r{ 0, 0, (LONG)P(1000), (LONG)P(700) };
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_MEASUREITEM: {
        auto* m = (MEASUREITEMSTRUCT*)lp;
        if (m->CtlType == ODT_LISTBOX) m->itemHeight = (UINT)P(30);
        return TRUE;
    }
    case WM_DRAWITEM: {
        auto* d = (DRAWITEMSTRUCT*)lp;
        if (d->CtlType == ODT_LISTBOX) DrawListItem(d);
        else if (d->CtlType == ODT_BUTTON) DrawButton(d);
        return TRUE;
    }
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)g_panelBrush;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        bool enabled = IsWindowEnabled((HWND)lp) != FALSE;
        SetTextColor(dc, enabled ? RGB(232, 234, 242) : RGB(138, 144, 162));
        SetBkColor(dc, RGB(31, 35, 48));
        return (LRESULT)g_editBrush;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_START) StartScan(hwnd);
        else if (LOWORD(wp) == ID_REPORT && !g_reportPath.empty())
            ShellExecuteW(hwnd, L"open", g_reportPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    case WM_TIMER:
        ++g_phase;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_APP_LOG: {
        auto* m = (LogMsg*)lp;
        AddItem(m->kind, m->text);
        if (m->kind == K_FLAG) {
            ++g_totalFlags;
            if (g_current >= 0) ++g_steps[g_current].flags;
        }
        int count = (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0);
        SendMessageW(g_hList, LB_SETTOPINDEX, (WPARAM)std::max(0, count - 1), 0);
        delete m;
        return 0;
    }
    case WM_APP_STEP: {
        int i = (int)wp;
        if (i >= 0 && i < kSteps) {
            g_steps[i].state = lp ? 2 : 1;
            g_current = i;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_APP_ABORT: {
        auto* m = (std::wstring*)lp;
        AddItem(K_FLAG, *m);
        delete m;
        g_done = false;
        SetIdleUi();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_APP_DONE: {
        auto* d = (DoneMsg*)lp;
        g_done = true;
        g_upload = d->upload;
        if (d->saved) {
            g_reportPath = d->path;
            AddItem(K_SECTION, L"Reporte");
            AddItem(K_OK, L"Guardado en: " + d->path);
        } else {
            AddItem(K_FLAG, L"No se pudo guardar el reporte en el Escritorio.");
        }
        if (d->upload == 1) AddItem(K_OK, d->uploadMsg);
        else if (d->upload == 2) AddItem(K_FLAG, d->uploadMsg);
        EnableWindow(g_hReport, d->saved ? TRUE : FALSE);
        SetIdleUi();
        SendMessageW(g_hList, LB_SETTOPINDEX, 0, 0);
        delete d;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ANIM);
        if (g_panelBrush) DeleteObject(g_panelBrush);
        if (g_editBrush) DeleteObject(g_editBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &gsi, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_SHIELD);
    wc.hIconSm = LoadIcon(nullptr, IDI_SHIELD);
    wc.lpszClassName = L"MinecraftSSToolWnd";
    RegisterClassExW(&wc);

    float s = GetDpiForSystem() / 96.0f;
    RECT r{ 0, 0, (LONG)(1120 * s), (LONG)(760 * s) };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    ww = std::min(ww, (int)(work.right - work.left));
    wh = std::min(wh, (int)(work.bottom - work.top));
    int x = work.left + std::max(0, (int)(work.right - work.left - ww) / 2);
    int y = work.top + std::max(0, (int)(work.bottom - work.top - wh) / 2);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Minecraft SS Tool",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, ww, wh,
                                nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteFonts();
    GdiplusShutdown(token);
    return 0;
}
