#pragma once
// =====================================================================
//  net.h - conexion con el panel (Supabase) por HTTPS usando WinHTTP
// =====================================================================

static bool Online() { return wcslen(SS_SUPABASE_URL) > 0 && wcslen(SS_SUPABASE_KEY) > 0; }

// ------------------------------- JSON ---------------------------------
static std::string JsonEscUtf8(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}
static std::string JStr(const std::wstring& w) { return "\"" + JsonEscUtf8(Utf8(w)) + "\""; }

static void AppendUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
    } else {
        o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
        o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
    }
}
static bool ParseJsonString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            if (i + 4 > s.size()) return false;
            uint32_t cp = (uint32_t)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                uint32_t lo = (uint32_t)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
            }
            AppendUtf8(out, cp);
            break;
        }
        default: out += e;
        }
    }
    return false;
}
static size_t SkipWs(const std::string& s, size_t i) {
    while (i < s.size() && isspace((unsigned char)s[i])) ++i;
    return i;
}
static size_t FindJsonKey(const std::string& body, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t p = body.find(k);
    if (p == std::string::npos) return std::string::npos;
    p = SkipWs(body, p + k.size());
    if (p >= body.size() || body[p] != ':') return std::string::npos;
    return SkipWs(body, p + 1);
}
static std::wstring JsonGetString(const std::string& body, const std::string& key) {
    size_t p = FindJsonKey(body, key);
    std::string out;
    if (p != std::string::npos && ParseJsonString(body, p, out)) return FromUtf8(out);
    return L"";
}
static bool JsonGetBool(const std::string& body, const std::string& key) {
    size_t p = FindJsonKey(body, key);
    return p != std::string::npos && body.compare(p, 4, "true") == 0;
}
// La respuesta completa es un string JSON ("...")
static std::wstring JsonTopString(const std::string& body) {
    size_t p = SkipWs(body, 0);
    std::string out;
    if (p < body.size() && body[p] == '"' && ParseJsonString(body, p, out)) return FromUtf8(out);
    return L"";
}

// ------------------------------- HTTP ---------------------------------
struct HttpResult { bool ok = false; DWORD status = 0; std::string body; std::wstring error; };

static HttpResult PostJson(const std::wstring& url, const std::string& json, const std::wstring& key) {
    HttpResult r;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { r.error = L"La URL del panel no es válida."; return r; }

    HINTERNET s = WinHttpOpen(L"MinecraftSSTool/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"MinecraftSSTool/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { r.error = L"No se pudo iniciar la conexión."; return r; }
    WinHttpSetTimeouts(s, 10000, 10000, 20000, 60000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET q = c ? WinHttpOpenRequest(c, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    if (q) {
        std::wstring headers = L"Content-Type: application/json\r\napikey: " + key + L"\r\n";
        if (StartsWith(key, L"eyJ")) headers += L"Authorization: Bearer " + key + L"\r\n";  // claves "anon" antiguas (JWT)
        BOOL sent = WinHttpSendRequest(q, headers.c_str(), (DWORD)-1L, (LPVOID)json.data(), (DWORD)json.size(),
                                       (DWORD)json.size(), 0);
        if (sent && WinHttpReceiveResponse(q, nullptr)) {
            DWORD status = 0, len = sizeof(status);
            WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &len, WINHTTP_NO_HEADER_INDEX);
            r.status = status;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(q, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(q, chunk.data(), avail, &got) || got == 0) break;
                r.body.append(chunk.data(), got);
                if (r.body.size() > (8u << 20)) break;
            }
            r.ok = true;
        } else {
            r.error = L"Sin conexión con el panel (error " + std::to_wstring(GetLastError()) + L").";
        }
    } else {
        r.error = L"No se pudo conectar con el panel.";
    }
    if (q) WinHttpCloseHandle(q);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return r;
}

// Llama a una funcion del panel: POST /rest/v1/rpc/<fn>
static HttpResult CallRpc(const wchar_t* fn, const std::string& json) {
    std::wstring url = SS_SUPABASE_URL;
    while (!url.empty() && url.back() == L'/') url.pop_back();
    url += L"/rest/v1/rpc/";
    url += fn;
    return PostJson(url, json, SS_SUPABASE_KEY);
}

static std::wstring HttpErrorText(const HttpResult& r) {
    if (!r.ok) return r.error;
    std::wstring msg = JsonGetString(r.body, "message");
    if (msg.empty()) msg = Truncate(FromUtf8(r.body), 200);
    return L"HTTP " + std::to_wstring(r.status) + L": " + msg;
}

// ------------------------------ identidad ------------------------------
static std::wstring GetHwid() {
    wchar_t buf[128] = {};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &sz) == ERROR_SUCCESS)
        return buf;
    return L"desconocido";
}
static std::wstring GetPcName() {
    wchar_t b[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameW(b, &n) ? std::wstring(b) : L"?";
}
static std::wstring GetWinUser() {
    wchar_t b[256] = {};
    DWORD n = 256;
    return GetUserNameW(b, &n) ? std::wstring(b) : L"?";
}
