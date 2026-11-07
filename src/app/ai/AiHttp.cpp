// =============================================================================
//  app/ai/AiHttp.cpp - voir AiHttp.hpp
// =============================================================================
#include "AiHttp.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <winhttp.h>
#else
#    include <dlfcn.h>
#endif

namespace app::ai {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// "Nom: valeur" -> (nom en minuscules, valeur sans blancs). Faux : pas un en-tete.
bool splitHeader(std::string_view line, std::pair<std::string, std::string>& out) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    out = {lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1))};
    return true;
}

std::mutex& factoryMutex() {
    static std::mutex m;
    return m;
}
HttpFactory& factoryRef() {
    static HttpFactory f;
    return f;
}

HttpResponse failed(HttpResponse::Failure f, std::string_view host, int idle = 0, std::string detail = {}) {
    HttpResponse r;
    r.failure = f;
    r.error = failureText(f, host, idle);
    if (!detail.empty()) r.error += " (" + detail + ")";
    return r;
}

// =============================================================================
//  Linux (et les autres) : libcurl, chargee a la demande
// =============================================================================
#if !defined(_WIN32)

// Les constantes de curl.h (stables depuis des annees) : aucun en-tete de curl a la compilation.
enum : int {
    kOptWriteData = 10001,
    kOptUrl = 10002,
    kOptProxy = 10004,
    kOptErrorBuffer = 10010,
    kOptWriteFunction = 20011,
    kOptPostFields = 10015,
    kOptUserAgent = 10018,
    kOptCustomRequest = 10036,   // 1.31.0 : PATCH, PUT (l'outil de l'editeur et GitHub)
    kOptHttpHeader = 10023,
    kOptHeaderData = 10029,
    kOptNoProgress = 43,
    kOptPost = 47,
    kOptXferInfoData = 10057,
    kOptConnectTimeout = 78,
    kOptHeaderFunction = 20079,
    kOptHttpGet = 80,
    kOptNoSignal = 99,
    kOptPostFieldSizeLarge = 30120,
    kOptXferInfoFunction = 20219,
    kOptProtocols = 181,          // CURLOPT_PROTOCOLS (un masque : avant 7.85)
    kOptProtocolsStr = 10318,     // CURLOPT_PROTOCOLS_STR (7.85 et apres)
    kInfoResponseCode = 0x200002,
};
enum : long { kProtoHttp = 1, kProtoHttps = 2 };   // CURLPROTO_HTTP, CURLPROTO_HTTPS
enum : int {
    kCurlOk = 0,
    kCurlCouldntResolveProxy = 5,
    kCurlCouldntResolveHost = 6,
    kCurlCouldntConnect = 7,
    kCurlPartialFile = 18,
    kCurlWriteError = 23,
    kCurlTimedOut = 28,
    kCurlSslConnect = 35,
    kCurlAborted = 42,
    kCurlGotNothing = 52,
    kCurlSendError = 55,
    kCurlRecvError = 56,
    kCurlSslCertProblem = 58,
    kCurlSslCipher = 59,
    kCurlPeerFailedVerification = 60,
    kCurlSslCaCertBadFile = 77,
    kCurlSslShutdown = 80,
    kCurlSslIssuer = 83,
    kCurlSslPinned = 90,
    kCurlSslInvalidStatus = 91,
};

struct CurlLib {
    using GlobalInit = int (*)(long);
    using EasyInit = void* (*)();
    using EasySetopt = int (*)(void*, int, ...);
    using EasyPerform = int (*)(void*);
    using EasyCleanup = void (*)(void*);
    using EasyGetinfo = int (*)(void*, int, ...);
    using EasyStrerror = const char* (*)(int);
    using SlistAppend = void* (*)(void*, const char*);
    using SlistFreeAll = void (*)(void*);
    using Version = const char* (*)();
    GlobalInit   globalInit{};
    EasyInit     easyInit{};
    EasySetopt   setopt{};
    EasyPerform  perform{};
    EasyCleanup  cleanup{};
    EasyGetinfo  getinfo{};
    EasyStrerror strerror{};
    SlistAppend  slistAppend{};
    SlistFreeAll slistFreeAll{};
    std::string  name;   // "libcurl 8.5.0"
    std::string  why;
    [[nodiscard]] bool ok() const noexcept {
        return easyInit && setopt && perform && cleanup && getinfo && slistAppend && slistFreeAll;
    }
};

// Chargee une fois, jamais dechargee (comme libpcap, hmi/HmiSpy.cpp).
const CurlLib& curlLib() {
    static const CurlLib lib = [] {
        CurlLib c;
        void* h = nullptr;
        for (const char* so : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"}) {
            h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
            if (h) break;
        }
        if (!h) {
            c.why = "pas de connexion possible sur ce poste (libcurl manque : le paquet libcurl4)";
            return c;
        }
        const auto sym = [&](const char* s) { return dlsym(h, s); };
        c.globalInit = reinterpret_cast<CurlLib::GlobalInit>(sym("curl_global_init"));
        c.easyInit = reinterpret_cast<CurlLib::EasyInit>(sym("curl_easy_init"));
        c.setopt = reinterpret_cast<CurlLib::EasySetopt>(sym("curl_easy_setopt"));
        c.perform = reinterpret_cast<CurlLib::EasyPerform>(sym("curl_easy_perform"));
        c.cleanup = reinterpret_cast<CurlLib::EasyCleanup>(sym("curl_easy_cleanup"));
        c.getinfo = reinterpret_cast<CurlLib::EasyGetinfo>(sym("curl_easy_getinfo"));
        c.strerror = reinterpret_cast<CurlLib::EasyStrerror>(sym("curl_easy_strerror"));
        c.slistAppend = reinterpret_cast<CurlLib::SlistAppend>(sym("curl_slist_append"));
        c.slistFreeAll = reinterpret_cast<CurlLib::SlistFreeAll>(sym("curl_slist_free_all"));
        if (!c.ok()) {
            c.why = "pas de connexion possible sur ce poste (libcurl incompl\xC3\xA8te)";
            return c;
        }
        // Une fois, avant tout fil (curl_global_init n'est pas sur entre plusieurs fils).
        if (c.globalInit) (void)c.globalInit(3L);   // CURL_GLOBAL_DEFAULT
        c.name = "libcurl";
        if (auto v = reinterpret_cast<CurlLib::Version>(sym("curl_version"))) {
            std::string text = v() ? v() : "";
            text = text.substr(0, text.find(' '));
            if (text.rfind("libcurl/", 0) == 0) c.name = "libcurl " + text.substr(8);
        }
        return c;
    }();
    return lib;
}

struct CurlCtx {
    const HttpSink*                       sink{nullptr};
    const std::atomic<bool>*              cancelled{nullptr};
    std::chrono::steady_clock::time_point last{};
    int                                   idle{60};
    bool                                  timedOut{false};
    int                                   status{0};
    HttpHeaders                           headers;
    bool                                  headersSent{false};
    std::int64_t                          uploaded{-1};
    void sendHeaders() {
        if (headersSent || status < 200) return;
        headersSent = true;
        if (sink && sink->onHeaders) sink->onHeaders(status, headers);
    }
};

std::size_t curlOnHeader(char* data, std::size_t size, std::size_t n, void* user) {
    auto& ctx = *static_cast<CurlCtx*>(user);
    const std::size_t total = size * n;
    if (ctx.cancelled->load()) return 0;
    ctx.last = std::chrono::steady_clock::now();
    std::string_view line(data, total);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    if (line.rfind("HTTP/", 0) == 0) {
        // Une ligne d'etat : une nouvelle reponse (apres un 100 Continue, un CONNECT de proxy).
        const auto sp = line.find(' ');
        ctx.status = sp == std::string_view::npos ? 0 : std::atoi(std::string(line.substr(sp + 1, 3)).c_str());
        ctx.headers.clear();
        ctx.headersSent = false;
    } else if (line.empty()) {
        ctx.sendHeaders();
    } else {
        std::pair<std::string, std::string> h;
        if (splitHeader(line, h)) ctx.headers.push_back(std::move(h));
    }
    return total;
}

std::size_t curlOnData(char* data, std::size_t size, std::size_t n, void* user) {
    auto& ctx = *static_cast<CurlCtx*>(user);
    const std::size_t total = size * n;
    if (ctx.cancelled->load()) return 0;
    ctx.last = std::chrono::steady_clock::now();
    ctx.sendHeaders();
    if (ctx.sink && ctx.sink->onData) ctx.sink->onData(std::string_view(data, total));
    return total;
}

int curlOnProgress(void* user, std::int64_t, std::int64_t, std::int64_t, std::int64_t ulnow) {
    auto& ctx = *static_cast<CurlCtx*>(user);
    if (ctx.cancelled->load()) return 1;
    const auto now = std::chrono::steady_clock::now();
    if (ulnow != ctx.uploaded) {   // l'envoi d'un gros corps compte comme une activite
        ctx.uploaded = ulnow;
        ctx.last = now;
    }
    if (now - ctx.last > std::chrono::seconds(ctx.idle)) {
        ctx.timedOut = true;
        return 1;
    }
    return 0;
}

class CurlClient final : public HttpClient {
public:
    HttpResponse perform(const HttpRequest& req, const HttpSink& sink) override {
        Url u;
        std::string why;
        if (!parseUrl(req.url, u, &why, req.allowPrivateHttp)) {
            HttpResponse r;
            r.failure = HttpResponse::Failure::BadUrl;
            r.error = why;
            return r;
        }
        if (cancelled_) return failed(HttpResponse::Failure::Cancelled, u.host);
        const CurlLib& lib = curlLib();
        if (!lib.ok()) return failed(HttpResponse::Failure::Unavailable, u.host);
        void* h = lib.easyInit();
        if (!h) return failed(HttpResponse::Failure::Other, u.host, 0, "curl_easy_init");
        char errbuf[256] = {0};
        CurlCtx ctx;
        ctx.sink = &sink;
        ctx.cancelled = &cancelled_;
        ctx.last = std::chrono::steady_clock::now();
        ctx.idle = std::max(1, req.idleTimeoutSeconds);
        using WriteFn = std::size_t (*)(char*, std::size_t, std::size_t, void*);
        using ProgressFn = int (*)(void*, std::int64_t, std::int64_t, std::int64_t, std::int64_t);
        lib.setopt(h, kOptUrl, req.url.c_str());
        lib.setopt(h, kOptNoSignal, 1L);
        lib.setopt(h, kOptConnectTimeout, static_cast<long>(std::max(1, req.connectTimeoutSeconds)));
        lib.setopt(h, kOptErrorBuffer, errbuf);
        lib.setopt(h, kOptUserAgent, "IHM-Studio");
        // http et https seulement (pas de file://, ftp://...) ; une libcurl trop ancienne pour la forme texte : le masque.
        // Les redirections ne sont jamais suivies (FOLLOWLOCATION reste a 0) : un 3xx est rendu tel quel.
        if (lib.setopt(h, kOptProtocolsStr, "http,https") != kCurlOk) (void)lib.setopt(h, kOptProtocols, kProtoHttp | kProtoHttps);
        void* list = nullptr;
        for (const auto& [k, v] : req.headers) list = lib.slistAppend(list, (k + ": " + v).c_str());
        list = lib.slistAppend(list, "Expect:");   // pas de 100-continue : le corps part tout de suite
        lib.setopt(h, kOptHttpHeader, list);
        if (req.method == "GET") {
            lib.setopt(h, kOptHttpGet, 1L);
        } else {
            lib.setopt(h, kOptPost, 1L);
            lib.setopt(h, kOptPostFieldSizeLarge, static_cast<std::int64_t>(req.body.size()));
            lib.setopt(h, kOptPostFields, req.body.data());
            // 1.31.0 : une autre methode avec un corps (PATCH, PUT) : le corps part comme un POST, sous son nom.
            if (req.method != "POST") lib.setopt(h, kOptCustomRequest, req.method.c_str());
        }
        lib.setopt(h, kOptWriteFunction, static_cast<WriteFn>(&curlOnData));
        lib.setopt(h, kOptWriteData, static_cast<void*>(&ctx));
        lib.setopt(h, kOptHeaderFunction, static_cast<WriteFn>(&curlOnHeader));
        lib.setopt(h, kOptHeaderData, static_cast<void*>(&ctx));
        lib.setopt(h, kOptNoProgress, 0L);
        lib.setopt(h, kOptXferInfoFunction, static_cast<ProgressFn>(&curlOnProgress));
        lib.setopt(h, kOptXferInfoData, static_cast<void*>(&ctx));
        // Ce poste : jamais par un proxy (les essais, un relais local).
        if (u.loopback() || u.privateLan()) lib.setopt(h, kOptProxy, "");   // 1.16.0 : un serveur de l'usine, sans proxy
        const int rc = lib.perform(h);
        long code = 0;
        lib.getinfo(h, kInfoResponseCode, &code);
        lib.slistFreeAll(list);
        lib.cleanup(h);

        HttpResponse r;
        r.status = ctx.status > 0 ? ctx.status : static_cast<int>(code);
        r.headers = std::move(ctx.headers);
        if (rc == kCurlOk && !cancelled_) {
            if (!ctx.headersSent && sink.onHeaders && r.status >= 200) sink.onHeaders(r.status, r.headers);
            return r;
        }
        std::string detail = errbuf[0] ? std::string(errbuf) : (lib.strerror ? std::string(lib.strerror(rc)) : std::string{});
        HttpResponse::Failure f = HttpResponse::Failure::Other;
        if (cancelled_) f = HttpResponse::Failure::Cancelled, detail.clear();
        else if (ctx.timedOut || rc == kCurlTimedOut) f = HttpResponse::Failure::Timeout, detail.clear();
        else if (rc == kCurlCouldntResolveHost || rc == kCurlCouldntResolveProxy) f = HttpResponse::Failure::Resolve;
        else if (rc == kCurlCouldntConnect) f = HttpResponse::Failure::Connect;
        else if (rc == kCurlSslConnect || rc == kCurlPeerFailedVerification || rc == kCurlSslCertProblem || rc == kCurlSslCipher
                 || rc == kCurlSslCaCertBadFile || rc == kCurlSslShutdown || rc == kCurlSslIssuer || rc == kCurlSslPinned
                 || rc == kCurlSslInvalidStatus)
            f = HttpResponse::Failure::Tls;
        else if (rc == kCurlGotNothing || rc == kCurlSendError || rc == kCurlRecvError || rc == kCurlPartialFile)
            f = HttpResponse::Failure::Cut;
        else if (rc == kCurlAborted || rc == kCurlWriteError)
            f = HttpResponse::Failure::Cancelled;
        HttpResponse e = failed(f, u.host, ctx.idle, f == HttpResponse::Failure::Other || f == HttpResponse::Failure::Tls ? detail : std::string{});
        e.status = r.status;
        e.headers = std::move(r.headers);
        return e;
    }
    void cancel() override { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    std::atomic<bool> cancelled_{false};
};

#else
// =============================================================================
//  Windows : WinHTTP (le TLS, les proxys et les certificats du systeme)
// =============================================================================
#    ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#        define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#    endif
#    ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#        define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#    endif
#    ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#        define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#    endif
#    ifndef WINHTTP_OPTION_REDIRECT_POLICY
#        define WINHTTP_OPTION_REDIRECT_POLICY 88
#    endif
#    ifndef WINHTTP_OPTION_REDIRECT_POLICY_NEVER
#        define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0
#    endif
#    ifndef WINHTTP_OPTION_DISABLE_FEATURE
#        define WINHTTP_OPTION_DISABLE_FEATURE 63
#    endif
#    ifndef WINHTTP_DISABLE_REDIRECTS
#        define WINHTTP_DISABLE_REDIRECTS 0x00000002
#    endif

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w, std::size_t len) {
    if (!w || len == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), s.data(), n, nullptr, nullptr);
    return s;
}

class WinHttpClient final : public HttpClient {
public:
    ~WinHttpClient() override {
        std::lock_guard<std::mutex> lk(m_);
        if (request_) WinHttpCloseHandle(request_);
        request_ = nullptr;
    }

    HttpResponse perform(const HttpRequest& req, const HttpSink& sink) override {
        Url u;
        std::string why;
        if (!parseUrl(req.url, u, &why, req.allowPrivateHttp)) {
            HttpResponse r;
            r.failure = HttpResponse::Failure::BadUrl;
            r.error = why;
            return r;
        }
        if (cancelled_) return failed(HttpResponse::Failure::Cancelled, u.host);
        const int idle = std::max(1, req.idleTimeoutSeconds);
        const bool direct = u.loopback() || u.privateLan();   // 1.16.0 : un serveur de l'usine, sans proxy
        HINTERNET session = WinHttpOpen(L"IHM-Studio", direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session && !direct)   // avant Windows 8.1 : les reglages de proxy de WinHTTP
            session = WinHttpOpen(L"IHM-Studio", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return failed(HttpResponse::Failure::Other, u.host, 0, "WinHttpOpen " + std::to_string(GetLastError()));
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // un Windows sans TLS 1.3
            (void)WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
        const int connectMs = std::max(1, req.connectTimeoutSeconds) * 1000;
        (void)WinHttpSetTimeouts(session, connectMs, connectMs, idle * 1000, idle * 1000);
        HINTERNET connection = WinHttpConnect(session, widen(u.host).c_str(), static_cast<INTERNET_PORT>(u.port), 0);
        if (!connection) {
            const DWORD err = GetLastError();
            WinHttpCloseHandle(session);
            return failure(err, u.host, idle, 0);
        }
        HINTERNET request = WinHttpOpenRequest(connection, widen(req.method).c_str(), widen(u.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!request) {
            const DWORD err = GetLastError();
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return failure(err, u.host, idle, 0);
        }
        // Jamais de redirection suivie (WinHTTP les suit par defaut, en-tetes et cle compris) : un 3xx est rendu tel quel.
        // Ni la politique ni la fonction ne se coupent : la requete ne part pas.
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        const BOOL policyOk = WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never);
        DWORD noRedirects = WINHTTP_DISABLE_REDIRECTS;
        const BOOL featureOk = WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirects, sizeof noRedirects);
        if (!policyOk && !featureOk) {
            const DWORD err = GetLastError();
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return failed(HttpResponse::Failure::Other, u.host, 0, "redirections non d\xC3\xA9sactivables (WinHTTP " + std::to_string(err) + ")");
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            request_ = request;
        }
        const auto finish = [&] {
            std::lock_guard<std::mutex> lk(m_);
            if (request_) WinHttpCloseHandle(request_);
            request_ = nullptr;
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
        };
        if (cancelled_) {
            finish();
            return failed(HttpResponse::Failure::Cancelled, u.host);
        }
        std::wstring headers;
        for (const auto& [k, v] : req.headers) headers += widen(k + ": " + v) + L"\r\n";
        if (!headers.empty())
            (void)WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        // Le corps est envoye d'un bloc (le fil de travail attend : l'interface, non).
        LPVOID body = req.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(req.body.data());
        const DWORD bodyLen = static_cast<DWORD>(req.body.size());
        BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, bodyLen, bodyLen, 0);
        if (ok) ok = WinHttpReceiveResponse(request, nullptr);
        if (!ok) {
            const DWORD err = GetLastError();
            finish();
            return failure(err, u.host, idle, 0);
        }
        HttpResponse r;
        DWORD code = 0, size = sizeof code;
        (void)WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
                                  WINHTTP_NO_HEADER_INDEX);
        r.status = static_cast<int>(code);
        DWORD len = 0;
        (void)WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len,
                                  WINHTTP_NO_HEADER_INDEX);
        if (len > 0) {
            std::wstring raw(len / sizeof(wchar_t) + 1, L'\0');
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &len,
                                    WINHTTP_NO_HEADER_INDEX)) {
                const std::string text = narrow(raw.c_str(), len / sizeof(wchar_t));
                std::size_t from = 0;
                while (from < text.size()) {
                    std::size_t nl = text.find('\n', from);
                    if (nl == std::string::npos) nl = text.size();
                    std::string_view line(text.data() + from, nl - from);
                    while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                    std::pair<std::string, std::string> h;
                    if (line.rfind("HTTP/", 0) != 0 && splitHeader(line, h)) r.headers.push_back(std::move(h));
                    from = nl + 1;
                }
            }
        }
        if (sink.onHeaders) sink.onHeaders(r.status, r.headers);
        std::string buf;
        while (true) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) {
                const DWORD err = GetLastError();
                finish();
                HttpResponse e = failure(err, u.host, idle, r.status);
                e.status = r.status;
                e.headers = std::move(r.headers);
                return e;
            }
            if (avail == 0) break;
            buf.resize(std::min<DWORD>(avail, 64 * 1024));
            DWORD got = 0;
            if (!WinHttpReadData(request, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
                const DWORD err = GetLastError();
                finish();
                HttpResponse e = failure(err, u.host, idle, r.status);
                e.status = r.status;
                e.headers = std::move(r.headers);
                return e;
            }
            if (got == 0) break;
            if (sink.onData) sink.onData(std::string_view(buf.data(), got));
            if (cancelled_) break;
        }
        finish();
        if (cancelled_) {
            HttpResponse e = failed(HttpResponse::Failure::Cancelled, u.host);
            e.status = r.status;
            return e;
        }
        return r;
    }

    void cancel() override {
        cancelled_ = true;
        // Fermer la requete depuis un autre fil : l'appel bloque rend ERROR_WINHTTP_OPERATION_CANCELLED.
        std::lock_guard<std::mutex> lk(m_);
        if (request_) WinHttpCloseHandle(request_);
        request_ = nullptr;
    }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    HttpResponse failure(DWORD err, std::string_view host, int idle, int status) const {
        using F = HttpResponse::Failure;
        F f = F::Other;
        if (cancelled_ || err == ERROR_WINHTTP_OPERATION_CANCELLED || err == ERROR_INVALID_HANDLE) f = F::Cancelled;
        else if (err == ERROR_WINHTTP_TIMEOUT) f = F::Timeout;
        else if (err == ERROR_WINHTTP_NAME_NOT_RESOLVED) f = F::Resolve;
        else if (err == ERROR_WINHTTP_CANNOT_CONNECT || err == ERROR_WINHTTP_AUTODETECTION_FAILED || err == ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT
                 || err == ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT)
            f = F::Connect;
        else if (err == ERROR_WINHTTP_SECURE_FAILURE || err == ERROR_WINHTTP_SECURE_CERT_CN_INVALID || err == ERROR_WINHTTP_SECURE_CERT_DATE_INVALID
                 || err == ERROR_WINHTTP_SECURE_INVALID_CA || err == ERROR_WINHTTP_SECURE_INVALID_CERT || err == ERROR_WINHTTP_SECURE_CERT_REV_FAILED
                 || err == ERROR_WINHTTP_SECURE_CERT_REVOKED || err == ERROR_WINHTTP_SECURE_CHANNEL_ERROR)
            f = F::Tls;
        else if (err == ERROR_WINHTTP_CONNECTION_ERROR || err == ERROR_WINHTTP_INVALID_SERVER_RESPONSE)
            f = status > 0 ? F::Cut : F::Connect;
        std::string detail;
        if (f == F::Other || f == F::Tls) detail = "WinHTTP " + std::to_string(err);
        return failed(f, host, idle, detail);
    }

    std::mutex        m_;
    HINTERNET         request_{nullptr};
    std::atomic<bool> cancelled_{false};
};
#endif

} // namespace

// ---- les adresses --------------------------------------------------------------------
namespace {

// Une IPv4 stricte : 4 nombres decimaux de 0 a 255, sans zero en tete (pas de 0127, 127.1,
// 0x7f) - une forme ambigue n'est jamais prise pour une adresse locale. Ecrit a la main
// plutot qu'inet_pton : AiHttp sert aussi l'outil de l'editeur, sans ws2_32 sous Windows.
bool ipv4Strict(std::string_view s, std::uint8_t out[4]) {
    int part = 0;
    std::size_t i = 0;
    while (part < 4) {
        std::size_t j = i;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9' && j - i < 4) ++j;
        const std::size_t len = j - i;
        if (len == 0 || len > 3 || (len > 1 && s[i] == '0')) return false;
        int v = 0;
        for (std::size_t k = i; k < j; ++k) v = v * 10 + (s[k] - '0');
        if (v > 255) return false;
        out[part++] = static_cast<std::uint8_t>(v);
        if (part == 4) return j == s.size();
        if (j >= s.size() || s[j] != '.') return false;
        i = j + 1;
    }
    return false;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Une IPv6 (groupes hexadecimaux, un seul "::", une IPv4 stricte en fin) ; une zone (%eth0) : refusee.
bool ipv6Parse(std::string_view s, std::uint8_t out[16]) {
    std::uint16_t words[8] = {0};
    int n = 0, gap = -1;
    std::size_t i = 0;
    if (s.size() < 2) return false;
    if (s[0] == ':') {
        if (s[1] != ':') return false;
        gap = 0;
        i = 2;
    }
    while (i < s.size()) {
        std::size_t j = i;
        while (j < s.size() && hexDigit(s[j]) >= 0 && j - i < 5) ++j;
        if (j < s.size() && s[j] == '.') {   // l'IPv4 de la fin (::ffff:127.0.0.1)
            std::uint8_t v4[4];
            if (n > 6 || !ipv4Strict(s.substr(i), v4)) return false;
            words[n++] = static_cast<std::uint16_t>((v4[0] << 8) | v4[1]);
            words[n++] = static_cast<std::uint16_t>((v4[2] << 8) | v4[3]);
            i = s.size();
            break;
        }
        if (j == i || j - i > 4 || n >= 8) return false;
        unsigned v = 0;
        for (std::size_t k = i; k < j; ++k) v = v * 16 + static_cast<unsigned>(hexDigit(s[k]));
        words[n++] = static_cast<std::uint16_t>(v);
        i = j;
        if (i == s.size()) break;
        if (s[i] != ':') return false;
        ++i;
        if (i < s.size() && s[i] == ':') {
            if (gap >= 0) return false;
            gap = n;
            ++i;
        } else if (i == s.size()) {
            return false;   // un ":" seul a la fin
        }
    }
    if (gap < 0 ? n != 8 : n > 7) return false;
    std::uint16_t full[8] = {0};
    if (gap < 0) {
        std::copy(words, words + 8, full);
    } else {
        std::copy(words, words + gap, full);
        std::copy(words + gap, words + n, full + (8 - (n - gap)));
    }
    for (int k = 0; k < 8; ++k) {
        out[2 * k] = static_cast<std::uint8_t>(full[k] >> 8);
        out[2 * k + 1] = static_cast<std::uint8_t>(full[k] & 0xFF);
    }
    return true;
}

} // namespace

bool Url::loopback() const {
    // Le nom lu en entier, jamais par son debut : 127.evil.com n'est pas ce poste.
    const std::string h = lower(host);
    if (h == "localhost") return true;
    std::uint8_t v4[4];
    if (ipv4Strict(h, v4)) return v4[0] == 127;
    std::uint8_t v6[16];
    if (!ipv6Parse(h, v6)) return false;
    const bool zeros10 = std::all_of(v6, v6 + 10, [](std::uint8_t b) { return b == 0; });
    if (zeros10 && v6[10] == 0 && v6[11] == 0 && v6[12] == 0 && v6[13] == 0 && v6[14] == 0 && v6[15] == 1) return true;   // ::1
    return zeros10 && v6[10] == 0xFF && v6[11] == 0xFF && v6[12] == 127;   // ::ffff:127.x.x.x
}

std::string Url::base() const {
    std::string out = https ? "https://" : "http://";
    out += host.find(':') != std::string::npos ? "[" + host + "]" : host;
    if (port != (https ? 443 : 80)) out += ":" + std::to_string(port);
    return out;
}

std::string Url::text() const { return base() + path; }

bool Url::privateLan() const {
    // Une adresse IPv4 stricte : 10.x.x.x, 192.168.x.x, 172.16.x.x a 172.31.x.x (010.1.2.3, 10.1 : non).
    std::uint8_t v4[4];
    if (!ipv4Strict(host, v4)) return false;
    const int a = v4[0], b = v4[1];
    return a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
}

bool parseUrl(std::string_view text, Url& out, std::string* why) { return parseUrl(text, out, why, false); }

bool parseUrl(std::string_view text, Url& out, std::string* why, bool allowPrivateHttp) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const std::string t = trim(text);
    const std::string lt = lower(t);
    Url u;
    std::size_t at = 0;
    if (lt.rfind("https://", 0) == 0) {
        u.https = true;
        u.port = 443;
        at = 8;
    } else if (lt.rfind("http://", 0) == 0) {
        u.https = false;
        u.port = 80;
        at = 7;
    } else {
        return fail("\xC2\xAB " + t + " \xC2\xBB : l'adresse doit commencer par https:// (ou http:// vers ce poste).");
    }
    const std::size_t end = t.find_first_of("/?#", at);
    const std::string authority = t.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (authority.find('@') != std::string::npos) return fail("\xC2\xAB " + t + " \xC2\xBB : pas d'identifiant dans l'adresse.");
    std::string hostPort = authority;
    if (!hostPort.empty() && hostPort.front() == '[') {
        const auto close = hostPort.find(']');
        if (close == std::string::npos) return fail("\xC2\xAB " + t + " \xC2\xBB : adresse IPv6 mal ferm\xC3\xA9" "e.");
        u.host = hostPort.substr(1, close - 1);
        hostPort = hostPort.substr(close + 1);
        if (!hostPort.empty() && hostPort.front() != ':') return fail("\xC2\xAB " + t + " \xC2\xBB : adresse mal form\xC3\xA9" "e.");
    } else {
        const auto colon = hostPort.rfind(':');
        u.host = hostPort.substr(0, colon);
        hostPort = colon == std::string::npos ? std::string{} : hostPort.substr(colon);
    }
    if (!hostPort.empty()) {
        const std::string port = hostPort.substr(1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return fail("\xC2\xAB " + t + " \xC2\xBB : le port n'est pas un nombre.");
        u.port = std::atoi(port.c_str());
        if (u.port < 1 || u.port > 65535) return fail("\xC2\xAB " + t + " \xC2\xBB : le port est hors de 1..65535.");
    }
    if (u.host.empty()) return fail("\xC2\xAB " + t + " \xC2\xBB : il manque le nom du serveur.");
    if (u.host.find_first_of(" \t\r\n") != std::string::npos) return fail("\xC2\xAB " + t + " \xC2\xBB : un blanc dans le nom du serveur.");
    std::string path = end == std::string::npos ? std::string{} : t.substr(end);
    if (const auto hash = path.find('#'); hash != std::string::npos) path.resize(hash);
    u.hasPath = !path.empty() && path != "/" && path[0] == '/';
    if (path.empty() || path[0] != '/') path = "/" + path;
    u.path = path;
    if (!u.https && !u.loopback() && !(allowPrivateHttp && u.privateLan()))
        return fail("\xC2\xAB " + t + " \xC2\xBB : http:// n'est accept\xC3\xA9 que vers ce poste (127.0.0.1) ; la cl\xC3\xA9 ne passe jamais en clair "
                    "sur le r\xC3\xA9seau : utilise https://.");
    out = std::move(u);
    return true;
}

std::string HttpResponse::header(std::string_view name) const {
    const std::string want = lower(name);
    for (const auto& [k, v] : headers)
        if (k == want) return v;
    return {};
}

std::string failureText(HttpResponse::Failure f, std::string_view host, int idle) {
    const std::string h = "\xC2\xAB " + std::string(host) + " \xC2\xBB";
    switch (f) {
        case HttpResponse::Failure::None: return {};
        case HttpResponse::Failure::Unavailable: {
            std::string why;
            (void)httpAvailable(&why);
            return why.empty() ? std::string("Pas de connexion possible sur ce poste.") : "Pas de connexion possible sur ce poste : " + why + ".";
        }
        case HttpResponse::Failure::BadUrl: return "Adresse refus\xC3\xA9" "e : " + h + ".";
        case HttpResponse::Failure::Resolve: return "Serveur introuvable : " + h + " (hors ligne, ou nom inconnu).";
        case HttpResponse::Failure::Connect: return "Connexion impossible \xC3\xA0 " + h + " (hors ligne, pare-feu ou proxy).";
        case HttpResponse::Failure::Tls: return "Connexion s\xC3\xA9" "curis\xC3\xA9" "e refus\xC3\xA9" "e par " + h + " (certificat non reconnu)";
        case HttpResponse::Failure::Timeout:
            return idle > 0 ? "D\xC3\xA9lai d\xC3\xA9pass\xC3\xA9 : rien re\xC3\xA7u de " + h + " pendant " + std::to_string(idle) + " s."
                            : "D\xC3\xA9lai d\xC3\xA9pass\xC3\xA9 : " + h + " ne r\xC3\xA9pond pas.";
        case HttpResponse::Failure::Cut: return "La connexion \xC3\xA0 " + h + " a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e pendant la r\xC3\xA9ponse.";
        case HttpResponse::Failure::Cancelled: return "Arr\xC3\xAAt\xC3\xA9" "e.";
        case HttpResponse::Failure::Other: return "Erreur r\xC3\xA9seau avec " + h;
    }
    return {};
}

// ---- la fabrique --------------------------------------------------------------------------
void setHttpFactory(HttpFactory factory) {
    std::lock_guard<std::mutex> lk(factoryMutex());
    factoryRef() = std::move(factory);
}

std::unique_ptr<HttpClient> makeHttpClient() {
    {
        std::lock_guard<std::mutex> lk(factoryMutex());
        if (factoryRef()) return factoryRef()();
    }
#if defined(_WIN32)
    return std::make_unique<WinHttpClient>();
#else
    return std::make_unique<CurlClient>();
#endif
}

bool httpAvailable(std::string* why) {
    {
        std::lock_guard<std::mutex> lk(factoryMutex());
        if (factoryRef()) return true;
    }
#if defined(_WIN32)
    (void)why;
    return true;
#else
    const CurlLib& lib = curlLib();
    if (!lib.ok() && why) *why = lib.why;
    return lib.ok();
#endif
}

std::string httpTransportName() {
    {
        std::lock_guard<std::mutex> lk(factoryMutex());
        if (factoryRef()) return "essai";
    }
#if defined(_WIN32)
    return "WinHTTP";
#else
    const CurlLib& lib = curlLib();
    return lib.ok() ? lib.name : std::string("aucun (libcurl manque)");
#endif
}

std::string base64(std::string_view bytes) {
    static const char* const k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]), b = static_cast<unsigned char>(bytes[i + 1]), c = static_cast<unsigned char>(bytes[i + 2]);
        out += k[a >> 2];
        out += k[((a & 3) << 4) | (b >> 4)];
        out += k[((b & 15) << 2) | (c >> 6)];
        out += k[c & 63];
    }
    if (i < bytes.size()) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0u;
        out += k[a >> 2];
        out += k[((a & 3) << 4) | (b >> 4)];
        out += i + 1 < bytes.size() ? k[(b & 15) << 2] : '=';
        out += '=';
    }
    return out;
}

} // namespace app::ai
