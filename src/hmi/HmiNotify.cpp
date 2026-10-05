// hmi/HmiNotify.cpp - les notifications des alarmes : courriel et SMS (lot 14).
#include "HmiNotify.hpp"

#include "HmiCrypto.hpp"
#include "HmiHistory.hpp"
#include "HmiNet.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <utility>

namespace hmi::notify {

namespace {

constexpr const char* kCrlf = "\r\n";

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ';' || c == ',') {
            if (!trimmed(cur).empty()) out.push_back(trimmed(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trimmed(cur).empty()) out.push_back(trimmed(cur));
    return out;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool sameText(std::string_view a, std::string_view b) { return lowerAscii(a) == lowerAscii(b); }

bool parseClock(std::string_view text, int& minutes) {
    int h = 0, m = 0;
    const std::string t = trimmed(text);
    if (std::sscanf(t.c_str(), "%d:%d", &h, &m) == 2 || std::sscanf(t.c_str(), "%dh%d", &h, &m) == 2) {
        if (h < 0 || h > 24 || m < 0 || m > 59) return false;
        minutes = std::min(1440, h * 60 + m);
        return true;
    }
    if (std::sscanf(t.c_str(), "%d", &h) == 1 && h >= 0 && h <= 24) {
        minutes = h * 60;
        return true;
    }
    return false;
}

// "Fri, 25 Sep 2026 17:40:12 +0200" : l'heure du poste et son decalage.
std::string mailDate() {
    const std::time_t t = std::time(nullptr);
    std::tm local{}, utc{};
#ifdef _WIN32
    localtime_s(&local, &t);
    gmtime_s(&utc, &t);
#else
    localtime_r(&t, &local);
    gmtime_r(&t, &utc);
#endif
    std::tm u = utc;
    u.tm_isdst = local.tm_isdst;
    const long offset = static_cast<long>(std::difftime(std::mktime(&local), std::mktime(&u)) / 60.0);
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char b[64];
    std::snprintf(b, sizeof b, "%s, %d %s %d %02d:%02d:%02d %c%02ld%02ld", days[local.tm_wday % 7], local.tm_mday, months[local.tm_mon % 12],
                  local.tm_year + 1900, local.tm_hour, local.tm_min, local.tm_sec, offset < 0 ? '-' : '+', std::labs(offset) / 60,
                  std::labs(offset) % 60);
    return b;
}

bool ascii(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80 && c != '\r' && c != '\n'; });
}

// RFC 2047 : un en-tete en UTF-8, en mots encodes de 45 octets au plus
// (coupes entre deux caracteres), replies.
std::string encodeHeader(std::string_view text) {
    if (ascii(text)) return std::string(text);
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = std::min(text.size(), i + 45);
        while (end < text.size() && end > i && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        if (!out.empty()) out += "\r\n ";
        out += "=?UTF-8?B?" + net::base64(text.substr(i, end - i)) + "?=";
        i = end;
    }
    return out;
}

// Du base64 en lignes de 76 caracteres.
std::string base64Lines(std::string_view bytes) {
    const std::string all = net::base64(bytes);
    std::string out;
    for (std::size_t i = 0; i < all.size(); i += 76) out += all.substr(i, 76) + kCrlf;
    return out;
}

std::string crlf(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') continue;
        if (text[i] == '\n') out += kCrlf;
        else out += text[i];
    }
    return out;
}

std::string decodeBase64(std::string_view in) {
    static const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int acc = 0, bits = 0;
    for (const char c : in) {
        const int v = value(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

// "=?UTF-8?B?...?=" (plusieurs mots, replies) -> le texte.
std::string decodeHeader(std::string_view v) {
    std::string out;
    std::size_t i = 0;
    bool lastWord = false;
    while (i < v.size()) {
        const std::size_t start = v.find("=?", i);
        if (start == std::string_view::npos) {
            out += std::string(v.substr(i));
            break;
        }
        const std::string between(v.substr(i, start - i));
        if (!(lastWord && trimmed(between).empty())) out += between;
        const std::size_t q1 = v.find('?', start + 2), q2 = q1 == std::string_view::npos ? q1 : v.find('?', q1 + 1);
        const std::size_t end = q2 == std::string_view::npos ? q2 : v.find("?=", q2 + 1);
        if (end == std::string_view::npos) {
            out += std::string(v.substr(start));
            break;
        }
        const std::string encoding = lowerAscii(v.substr(q1 + 1, q2 - q1 - 1));
        const std::string_view payload = v.substr(q2 + 1, end - q2 - 1);
        out += encoding == "b" ? decodeBase64(payload) : std::string(payload);
        lastWord = true;
        i = end + 2;
    }
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string stateOf(std::string_view kind) {
    if (kind == "Apparition") return "apparue";
    if (kind == "Disparition") return "disparue";
    if (kind == "Acquittement") return "acquitt\xC3\xA9" "e";
    if (kind == "Essai") return "essai";
    return "r\xC3\xA9" "apparue";
}

std::string clockNow() {
    const std::string w = wallStamp();
    return w.size() >= 19 ? w.substr(0, 19) : w;
}

// Une reponse SMTP (plusieurs lignes "250-..." puis "250 ...") : son code.
int readReply(net::Socket& s, int timeoutMs, std::string& text) {
    text.clear();
    for (int guard = 0; guard < 64; ++guard) {
        std::string line;
        if (!s.receiveLine(line, timeoutMs)) return 0;
        text = line;
        if (line.size() < 3) return 0;
        if (line.size() == 3 || line[3] != '-') return std::atoi(line.substr(0, 3).c_str());
    }
    return 0;
}

} // namespace

// ===================================================================== modeles ==
std::string expand(std::string_view tpl, const AlarmNotice& n, std::string_view project) {
    std::string s(tpl);
    const std::string at = n.at.size() >= 19 ? n.at.substr(0, 19) : n.at;
    s = replaceAll(std::move(s), "{projet}", project);
    s = replaceAll(std::move(s), "{alarme}", n.name);
    s = replaceAll(std::move(s), "{message}", n.message);
    s = replaceAll(std::move(s), "{priorite}", std::string(alarmPriorityLabel(std::clamp(n.priority, 1, kAlarmPriorities))));
    s = replaceAll(std::move(s), "{groupe}", n.group.empty() ? std::string("\xE2\x80\x94") : n.group);
    s = replaceAll(std::move(s), "{categorie}", n.category);
    s = replaceAll(std::move(s), "{etat}", stateOf(n.kind));
    s = replaceAll(std::move(s), "{heure}", at.size() >= 19 ? at.substr(11, 8) : at);
    s = replaceAll(std::move(s), "{date}", at.size() >= 10 ? at.substr(0, 10) : at);
    s = replaceAll(std::move(s), "{consigne}", n.instruction.empty() ? std::string("\xE2\x80\x94") : n.instruction);
    s = replaceAll(std::move(s), "{utilisateur}", n.user.empty() ? std::string("\xE2\x80\x94") : n.user);
    return s;
}

std::string defaultBody() {
    return "Alarme {alarme} : {etat}\n"
           "\n"
           "Priorit\xC3\xA9 : {priorite}\n"
           "Groupe : {groupe}\n"
           "Message : {message}\n"
           "Heure : {date} {heure}\n"
           "\n"
           "Consigne :\n"
           "{consigne}\n"
           "\n"
           "-- \n"
           "{projet} (IHM XpgAnalyzer). Ce message part tout seul : ne pas y r\xC3\xA9pondre.";
}

bool onDuty(const NotifyRecipient& r, int weekday, int minutes) {
    if (!r.duty) return true;
    int from = 0, to = 1440;
    if (!parseClock(r.dutyFrom, from) || !parseClock(r.dutyTo, to)) return false;
    const auto day = [&](int d) { return r.dutyDays.find(static_cast<char>('0' + ((d - 1 + 7) % 7) + 1)) != std::string::npos; };
    if (from == to) return day(weekday);                                       // la journee entiere
    if (from < to) return day(weekday) && minutes >= from && minutes < to;
    // Passe minuit (18:00 - 08:00) : le matin appartient a l'astreinte de la veille.
    return (day(weekday) && minutes >= from) || (day(weekday - 1) && minutes < to);
}

bool wants(const NotifyRecipient& r, const AlarmNotice& n, int weekday, int minutes) {
    if (!r.enabled) return false;
    if (n.kind == "Acquittement") return false;
    if (n.kind == "Disparition" && !r.onClear) return false;
    if (n.priority > r.maxPriority) return false;
    if (!trimmed(r.groups).empty()) {
        const auto groups = splitList(r.groups);
        if (std::none_of(groups.begin(), groups.end(), [&](const std::string& g) { return sameText(g, n.group); })) return false;
    }
    return onDuty(r, weekday, minutes);
}

// ================================================================= le secret ==
namespace {
constexpr std::string_view kMaskKey = "xpg-ihm-notifications-lot14";
std::string keystream(std::string_view salt, std::size_t size) {
    std::string out;
    for (unsigned block = 0; out.size() < size; ++block) {
        const auto d = hmacSha256(kMaskKey, std::string(salt) + ":" + std::to_string(block));
        out.append(reinterpret_cast<const char*>(d.data()), d.size());
    }
    out.resize(size);
    return out;
}
} // namespace

std::string maskSecret(std::string_view plain) {
    if (plain.empty()) return {};
    const std::string salt = randomHex(8);
    const std::string ks = keystream(salt, plain.size());
    std::string x(plain);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<char>(x[i] ^ ks[i]);
    return "m1:" + salt + ":" + toHex(reinterpret_cast<const std::uint8_t*>(x.data()), x.size());
}

std::string unmaskSecret(std::string_view masked) {
    if (masked.rfind("m1:", 0) != 0) return std::string(masked);      // ecrit a la main, en clair
    const std::size_t colon = masked.find(':', 3);
    if (colon == std::string_view::npos) return {};
    const std::string salt(masked.substr(3, colon - 3));
    std::string x = fromHex(masked.substr(colon + 1));
    const std::string ks = keystream(salt, x.size());
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<char>(x[i] ^ ks[i]);
    return x;
}

// ================================================================= le courriel ==
SmtpSettings smtpOf(const Notifications& n) {
    SmtpSettings s;
    s.host = n.smtpHost;
    s.port = n.smtpPort;
    s.from = n.smtpFrom;
    s.user = n.smtpUser;
    s.password = unmaskSecret(n.smtpPassword);
    s.timeoutMs = std::max(1, n.timeoutS) * 1000;
    return s;
}

std::string composeMail(const SmtpSettings& s, const Mail& m, std::string_view date, std::string_view boundary) {
    std::string to;
    for (const auto& a : m.to) to += (to.empty() ? "" : ", ") + a;
    std::string out;
    out += "From: " + s.from + kCrlf;
    out += "To: " + to + kCrlf;
    out += "Subject: " + encodeHeader(m.subject) + kCrlf;
    out += "Date: " + std::string(date) + kCrlf;
    out += "Message-ID: <" + std::string(boundary) + "@" + s.helo + ">" + kCrlf;
    out += "MIME-Version: 1.0\r\n";
    out += "X-Mailer: XpgAnalyzer IHM\r\n";
    if (!m.attachment) {
        out += "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n";
        out += base64Lines(crlf(m.body));
        return out;
    }
    out += "Content-Type: multipart/mixed; boundary=\"" + std::string(boundary) + "\"\r\n\r\n";
    out += "Ce message contient une piece jointe.\r\n";
    out += "--" + std::string(boundary) + kCrlf;
    out += "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n";
    out += base64Lines(crlf(m.body));
    out += "--" + std::string(boundary) + kCrlf;
    out += "Content-Type: " + m.attachmentType + "; name=\"" + m.attachmentName + "\"\r\n";
    out += "Content-Transfer-Encoding: base64\r\n";
    out += "Content-Disposition: attachment; filename=\"" + m.attachmentName + "\"\r\n\r\n";
    out += base64Lines(std::string_view(reinterpret_cast<const char*>(m.attachment->data()), m.attachment->size()));
    out += "--" + std::string(boundary) + "--\r\n";
    return out;
}

bool sendMail(const SmtpSettings& s, const Mail& m, std::string* why, std::string* reply) {
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (s.host.empty()) return fail("aucun relais SMTP (Configuration > Notifications)");
    if (m.to.empty()) return fail("aucun destinataire");
    net::Socket sock;
    std::string err;
    if (!sock.connect(s.host, s.port, s.timeoutMs, &err))
        return fail("relais " + s.host + ":" + std::to_string(s.port) + " injoignable : " + err);
    std::string text;
    const auto command = [&](const std::string& line, std::initializer_list<int> expected, const char* what) {
        if (!line.empty() && !sock.sendAll(line + kCrlf, s.timeoutMs, &err)) return fail(std::string(what) + " : envoi impossible (" + err + ")");
        const int code = readReply(sock, s.timeoutMs, text);
        if (code == 0) return fail(std::string(what) + " : pas de r\xC3\xA9ponse du relais");
        if (std::find(expected.begin(), expected.end(), code) == expected.end()) return fail(std::string(what) + " refus\xC3\xA9 : " + text);
        return true;
    };
    if (!command({}, {220}, "accueil")) return false;
    if (!command("EHLO " + s.helo, {250}, "EHLO") && !command("HELO " + s.helo, {250}, "HELO")) return false;
    if (!s.user.empty()) {
        if (!command("AUTH LOGIN", {334}, "authentification")) return false;
        if (!command(net::base64(s.user), {334}, "authentification (utilisateur)")) return false;
        if (!command(net::base64(s.password), {235}, "authentification (mot de passe)")) return false;
    }
    if (!command("MAIL FROM:<" + s.from + ">", {250}, "exp\xC3\xA9" "diteur")) return false;
    std::size_t accepted = 0;
    std::string refused;
    for (const auto& to : m.to) {
        if (!sock.sendAll("RCPT TO:<" + to + ">\r\n", s.timeoutMs, &err)) return fail("destinataire : envoi impossible (" + err + ")");
        const int code = readReply(sock, s.timeoutMs, text);
        if (code == 250 || code == 251) ++accepted;
        else refused += (refused.empty() ? "" : ", ") + to + " (" + text + ")";
    }
    if (accepted == 0) return fail("destinataire(s) refus\xC3\xA9(s) : " + refused);
    if (!command("DATA", {354}, "DATA")) return false;
    char token[40];
    std::snprintf(token, sizeof token, "xpg-%s", randomHex(10).c_str());
    std::string message = composeMail(s, m, mailDate(), token);
    // La transparence : une ligne qui commence par un point en prend un second.
    std::string stuffed;
    stuffed.reserve(message.size() + 16);
    bool lineStart = true;
    for (const char c : message) {
        if (lineStart && c == '.') stuffed += '.';
        stuffed += c;
        lineStart = c == '\n';
    }
    if (!sock.sendAll(stuffed + ".\r\n", s.timeoutMs, &err)) return fail("message : envoi impossible (" + err + ")");
    const int code = readReply(sock, s.timeoutMs, text);
    if (code != 250) return fail(code == 0 ? std::string("message : pas de r\xC3\xA9ponse du relais") : "message refus\xC3\xA9 : " + text);
    if (reply) *reply = text + (refused.empty() ? std::string{} : " (refus\xC3\xA9s : " + refused + ")");
    (void)sock.sendAll("QUIT\r\n", 1000);
    std::string bye;
    (void)readReply(sock, 1000, bye);
    return true;
}

// ================================================================= le SMS ==
std::string urlEncode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

std::string urlDecode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1]))
                   && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

HttpResult httpRequest(const std::string& method, const std::string& url, const std::string& body, const std::string& contentType,
                       int timeoutMs) {
    HttpResult r;
    const std::string lower = lowerAscii(url.substr(0, 8));
    if (lower.rfind("https://", 0) == 0) {
        r.why = "https : pas pris en charge (une passerelle en http, sur le r\xC3\xA9seau local)";
        return r;
    }
    if (lower.rfind("http://", 0) != 0) {
        r.why = "l'URL doit commencer par http://";
        return r;
    }
    const std::string rest = url.substr(7);
    const std::size_t slash = rest.find('/');
    const std::string hostPort = rest.substr(0, slash);
    const std::string path = slash == std::string::npos ? std::string("/") : rest.substr(slash);
    std::string host = hostPort;
    int port = 80;
    if (const std::size_t colon = hostPort.rfind(':'); colon != std::string::npos) {
        host = hostPort.substr(0, colon);
        port = std::atoi(hostPort.c_str() + colon + 1);
    }
    if (host.empty() || port <= 0 || port > 65535) {
        r.why = "URL illisible : " + url;
        return r;
    }
    net::Socket sock;
    std::string err;
    if (!sock.connect(host, port, timeoutMs, &err)) {
        r.why = "passerelle " + hostPort + " injoignable : " + err;
        return r;
    }
    std::string rq = method + " " + path + " HTTP/1.1\r\nHost: " + hostPort + "\r\nUser-Agent: XpgAnalyzer-IHM\r\nConnection: close\r\n";
    if (!body.empty() || method == "POST")
        rq += "Content-Type: " + contentType + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    rq += "\r\n" + body;
    if (!sock.sendAll(rq, timeoutMs, &err)) {
        r.why = "envoi impossible : " + err;
        return r;
    }
    std::string line;
    if (!sock.receiveLine(line, timeoutMs)) {
        r.why = "pas de r\xC3\xA9ponse de la passerelle";
        return r;
    }
    if (line.rfind("HTTP/", 0) != 0 || line.size() < 12) {
        r.why = "r\xC3\xA9ponse illisible : " + line;
        return r;
    }
    r.status = std::atoi(line.c_str() + line.find(' ') + 1);
    long long length = -1;
    for (int guard = 0; guard < 100; ++guard) {
        std::string h;
        if (!sock.receiveLine(h, timeoutMs) || h.empty()) break;
        if (lowerAscii(h).rfind("content-length:", 0) == 0) length = std::atoll(h.c_str() + 15);
    }
    if (length > 0 && length < 1 << 20) {
        std::string b(static_cast<std::size_t>(length), '\0');
        if (sock.receiveExact(b.data(), b.size(), timeoutMs)) r.body = std::move(b);
    } else if (length < 0) {
        char buf[4096];
        for (int guard = 0; guard < 64; ++guard) {
            const long n = sock.receiveSome(buf, sizeof buf, timeoutMs);
            if (n <= 0) break;
            r.body.append(buf, static_cast<std::size_t>(n));
            if (r.body.size() > 65536) break;
        }
    }
    if (!r.ok()) r.why = "HTTP " + std::to_string(r.status) + (r.body.empty() ? std::string{} : " : " + trimmed(r.body.substr(0, 120)));
    return r;
}

bool sendSms(const std::string& urlTemplate, const std::string& bodyTemplate, const std::string& phone, const std::string& text,
             int timeoutMs, std::string* why, int* status) {
    const auto fill = [&](std::string t) {
        t = replaceAll(std::move(t), "{numero}", urlEncode(phone));
        return replaceAll(std::move(t), "{message}", urlEncode(text));
    };
    if (trimmed(urlTemplate).empty()) {
        if (why) *why = "aucune passerelle SMS (Configuration > Notifications)";
        return false;
    }
    const bool post = !trimmed(bodyTemplate).empty();
    const auto r = httpRequest(post ? "POST" : "GET", fill(trimmed(urlTemplate)), post ? fill(bodyTemplate) : std::string{},
                               "application/x-www-form-urlencoded", timeoutMs);
    if (status) *status = r.status;
    if (!r.ok() && why) *why = r.why;
    return r.ok();
}

// ================================================================ le distributeur ==
Dispatcher::Dispatcher() { worker_ = std::thread([this] { run(); }); }

Dispatcher::~Dispatcher() { stop(); }

void Dispatcher::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Dispatcher::configure(const Notifications& n, std::string projectName) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = n;
    project_ = std::move(projectName);
    if (!n.enabled) pending_.clear();
}

void Dispatcher::record(Sent s) {
    if (s.at.empty()) s.at = clockNow();
    if (s.ok) {
        ++stats_.sent;
        stats_.last = s.at.substr(std::min<std::size_t>(11, s.at.size())) + " " + lowerAscii(s.channel) + " \xC3\xA0 " + s.recipient
                    + (s.alarm.empty() ? std::string{} : " : " + s.alarm);
    } else if (!s.skipped) {
        ++stats_.failed;
    }
    done_.push_back(std::move(s));
    if (done_.size() > 500) done_.erase(done_.begin(), done_.begin() + static_cast<std::ptrdiff_t>(done_.size() - 500));
}

Dispatcher::Job Dispatcher::mailJob(const NotifyRecipient& r, const AlarmNotice& n) const {
    Job j;
    j.channel = "Courriel";
    j.recipient = r.name;
    j.address = r.email;
    j.to = {r.email};
    j.alarm = n.name;
    j.kind = n.kind;
    j.subject = expand(settings_.subject.empty() ? std::string("[{projet}] {priorite} : {alarme}") : settings_.subject, n, project_);
    j.text = expand(trimmed(settings_.body).empty() ? defaultBody() : settings_.body, n, project_);
    return j;
}

Dispatcher::Job Dispatcher::smsJob(const NotifyRecipient& r, const AlarmNotice& n) const {
    Job j;
    j.channel = "SMS";
    j.recipient = r.name;
    j.address = r.phone;
    j.alarm = n.name;
    j.kind = n.kind;
    std::string text = expand(settings_.sms.empty() ? std::string("{projet} {priorite} {etat} : {message} ({heure})") : settings_.sms, n, project_);
    if (text.size() > 300) text = text.substr(0, 297) + "...";
    j.subject = j.text = std::move(text);
    return j;
}

void Dispatcher::enqueue(Job job, double now) {
    // Une meme alarme au meme destinataire : une fois en `repeatS` secondes (la
    // reapparition compte comme l'apparition ; la disparition a son propre compte).
    const std::string key = job.recipient + "|" + job.channel + "|" + job.alarm + (job.kind == "Disparition" ? "|fin" : "");
    if (!job.alarm.empty() && settings_.repeatS > 0) {
        const auto it = lastSent_.find(key);
        if (it != lastSent_.end() && now - it->second < settings_.repeatS) {
            Sent s{clockNow(), job.channel, job.recipient, job.address, job.alarm, job.kind, job.subject, false, true,
                   "d\xC3\xA9j\xC3\xA0 pr\xC3\xA9venu il y a " + std::to_string(static_cast<long long>(now - it->second)) + " s (une fois en "
                       + std::to_string(settings_.repeatS) + " s)"};
            record(std::move(s));
            return;
        }
    }
    // Au plus `maxPerHour` messages dans l'heure glissante.
    while (!hour_.empty() && now - hour_.front() >= 3600.0) hour_.pop_front();
    if (static_cast<int>(hour_.size()) >= std::max(1, settings_.maxPerHour)) {
        Sent s{clockNow(), job.channel, job.recipient, job.address, job.alarm, job.kind, job.subject, false, true,
               "rafale : " + std::to_string(hour_.size()) + " messages dans l'heure (au plus " + std::to_string(settings_.maxPerHour) + ")"};
        record(std::move(s));
        lastFlood_ = now;
        return;
    }
    if (!job.alarm.empty()) lastSent_[key] = now;
    hour_.push_back(now);
    queue_.push_back(std::move(job));
    wake_.notify_one();
}

void Dispatcher::dispatch(const AlarmNotice& n, double now, int weekday, int minutes) {
    for (const auto& r : settings_.recipients) {
        if (!wants(r, n, weekday, minutes)) continue;
        if (!trimmed(r.email).empty() && !trimmed(settings_.smtpHost).empty()) enqueue(mailJob(r, n), now);
        if (!trimmed(r.phone).empty() && !trimmed(settings_.smsUrl).empty()) enqueue(smsJob(r, n), now);
    }
}

void Dispatcher::notice(const AlarmNotice& n, double now, int weekday, int minutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!settings_.enabled) return;
    if (n.kind == "Acquittement" || n.kind == "Disparition") {
        // Acquittee ou disparue avant la fin du delai : rien ne part.
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->notice.alarm == n.alarm && it->notice.name == n.name) {
                Sent s;
                s.channel = "\xE2\x80\x94";
                s.alarm = n.name;
                s.kind = it->notice.kind;
                s.skipped = true;
                s.result = std::string(n.kind == "Acquittement" ? "acquitt\xC3\xA9" "e" : "disparue") + " avant le d\xC3\xA9lai de "
                         + std::to_string(settings_.delayS) + " s : rien n'est parti";
                record(std::move(s));
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        if (n.kind == "Disparition") dispatch(n, now, weekday, minutes);
        return;
    }
    if (settings_.delayS > 0) {
        pending_.push_back({n, now + settings_.delayS, weekday, minutes});
        return;
    }
    dispatch(n, now, weekday, minutes);
}

void Dispatcher::tick(double now) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->due <= now) {
            const Pending p = *it;
            it = pending_.erase(it);
            if (settings_.enabled) dispatch(p.notice, now, p.weekday, p.minutes);
        } else {
            ++it;
        }
    }
}

void Dispatcher::test(const NotifyRecipient& r, bool sms) {
    std::lock_guard<std::mutex> lock(mutex_);
    AlarmNotice n;
    n.kind = "Essai";
    n.name = "ESSAI";
    n.message = "Essai des notifications : si vous lisez ceci, le relais et l'adresse sont bons.";
    n.priority = 1;
    n.group = "Essai";
    n.at = wallStamp();
    Job j = sms ? smsJob(r, n) : mailJob(r, n);
    j.alarm.clear();
    j.kind = "Essai";
    queue_.push_back(std::move(j));
    wake_.notify_one();
}

void Dispatcher::sendReport(const std::string& recipientNames, const std::string& subject, const std::string& body, const std::string& fileName,
                            std::shared_ptr<const Bytes> data, const std::string& contentType) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job j;
    j.channel = "Courriel";
    j.kind = "Rapport";
    j.subject = subject;
    j.text = body;
    j.attachmentName = fileName;
    j.attachmentType = contentType;
    j.attachment = std::move(data);
    for (const auto& name : splitList(recipientNames))
        for (const auto& r : settings_.recipients)
            if (sameText(r.name, name) && !trimmed(r.email).empty()) {
                j.to.push_back(trimmed(r.email));
                j.recipient += (j.recipient.empty() ? "" : ", ") + r.name;
            }
    j.address = j.to.empty() ? std::string{} : j.to.front() + (j.to.size() > 1 ? " (+" + std::to_string(j.to.size() - 1) + ")" : std::string{});
    if (j.to.empty() || trimmed(settings_.smtpHost).empty()) {
        Sent s;
        s.channel = "Courriel";
        s.recipient = recipientNames;
        s.kind = "Rapport";
        s.subject = subject;
        s.skipped = true;
        s.result = j.to.empty() ? std::string("aucun destinataire nomm\xC3\xA9 n'a de courriel") : std::string("aucun relais SMTP");
        record(std::move(s));
        return;
    }
    queue_.push_back(std::move(j));
    wake_.notify_one();
}

void Dispatcher::run() {
    for (;;) {
        Job job;
        Notifications cfg;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            cfg = settings_;
            busy_ = true;
        }
        Sent s;
        s.channel = job.channel;
        s.recipient = job.recipient;
        s.address = job.address;
        s.alarm = job.alarm;
        s.kind = job.kind;
        s.subject = job.subject;
        std::string why, reply;
        if (job.channel == "SMS") {
            int status = 0;
            s.ok = sendSms(cfg.smsUrl, cfg.smsBody, job.address, job.text, std::max(1, cfg.timeoutS) * 1000, &why, &status);
            s.result = s.ok ? "HTTP " + std::to_string(status) : why;
        } else {
            Mail m;
            m.to = job.to;
            m.subject = job.subject;
            m.body = job.text;
            m.attachmentName = job.attachmentName;
            m.attachmentType = job.attachmentType;
            m.attachment = job.attachment;
            s.ok = sendMail(smtpOf(cfg), m, &why, &reply);
            s.result = s.ok ? reply : why;
        }
        s.at = clockNow();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            record(std::move(s));
            busy_ = false;
        }
        wake_.notify_all();
    }
}

std::vector<Sent> Dispatcher::takeSent() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Sent> out;
    out.swap(done_);
    return out;
}

std::size_t Dispatcher::waiting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::size_t Dispatcher::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + (busy_ ? 1u : 0u);
}

NotifyStats Dispatcher::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    NotifyStats s = stats_;
    s.enabled = settings_.enabled;
    return s;
}

bool Dispatcher::drain(int timeoutMs) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < until) {
        if (queued() == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return queued() == 0;
}

// ================================================================ la boite d'essai ==
struct TestServer::Sockets {
    net::Socket smtp, http;
};

TestServer::TestServer() = default;
TestServer::~TestServer() { stop(); }

bool TestServer::start(int smtpPort, std::string* why) {
    stop();
    auto s = std::make_unique<Sockets>();
    std::string err;
    if (!s->smtp.listen("127.0.0.1", smtpPort, &err)) {
        if (why) *why = "port " + std::to_string(smtpPort) + " : " + err;
        return false;
    }
    if (!s->http.listen("127.0.0.1", smtpPort ? smtpPort + 1 : 0, &err)) {
        if (why) *why = "port " + std::to_string(smtpPort + 1) + " : " + err;
        return false;
    }
    smtpPort_ = s->smtp.localPort();
    httpPort_ = s->http.localPort();
    sockets_ = std::move(s);
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void TestServer::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    sockets_.reset();
}

void TestServer::add(Received r) {
    std::lock_guard<std::mutex> lock(mutex_);
    received_.push_back(std::move(r));
    ++total_;
    if (received_.size() > 200) received_.erase(received_.begin());
}

std::vector<Received> TestServer::received() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_;
}

std::size_t TestServer::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_;
}

void TestServer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    received_.clear();
}

namespace {

// Le message recu : le sujet, le texte (la premiere partie text/plain), la
// piece jointe.
void parseMessage(const std::string& raw, Received& r) {
    const auto headersOf = [](std::string_view block) {
        std::vector<std::pair<std::string, std::string>> out;
        std::size_t pos = 0;
        while (pos < block.size()) {
            std::size_t eol = block.find("\r\n", pos);
            if (eol == std::string_view::npos) eol = block.size();
            std::string line(block.substr(pos, eol - pos));
            pos = eol + 2;
            if (line.empty()) continue;
            if ((line[0] == ' ' || line[0] == '\t') && !out.empty()) {
                out.back().second += line.substr(1);
                continue;
            }
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            out.emplace_back(lowerAscii(line.substr(0, colon)), trimmed(line.substr(colon + 1)));
        }
        return out;
    };
    const auto get = [](const std::vector<std::pair<std::string, std::string>>& hs, const char* key) {
        for (const auto& [k, v] : hs)
            if (k == key) return v;
        return std::string{};
    };
    const auto decodePart = [&](const std::vector<std::pair<std::string, std::string>>& hs, std::string_view body) {
        const std::string enc = lowerAscii(get(hs, "content-transfer-encoding"));
        std::string text = enc == "base64" ? decodeBase64(body) : std::string(body);
        std::string clean;
        for (const char c : text)
            if (c != '\r') clean += c;
        while (!clean.empty() && clean.back() == '\n') clean.pop_back();
        return clean;
    };
    const std::size_t split = raw.find("\r\n\r\n");
    const std::string head = raw.substr(0, split);
    const std::string body = split == std::string::npos ? std::string{} : raw.substr(split + 4);
    const auto hs = headersOf(head);
    r.subject = decodeHeader(get(hs, "subject"));
    const std::string type = get(hs, "content-type");
    const std::size_t b = type.find("boundary=");
    if (lowerAscii(type).rfind("multipart/", 0) != 0 || b == std::string::npos) {
        r.body = decodePart(hs, body);
        return;
    }
    std::string boundary = type.substr(b + 9);
    if (!boundary.empty() && boundary.front() == '"') boundary = boundary.substr(1, boundary.find('"', 1) - 1);
    const std::string marker = "--" + boundary;
    std::size_t pos = body.find(marker);
    while (pos != std::string::npos) {
        pos += marker.size();
        if (body.compare(pos, 2, "--") == 0) break;
        const std::size_t next = body.find(marker, pos);
        const std::string part = body.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        const std::size_t ps = part.find("\r\n\r\n");
        const auto ph = headersOf(part.substr(0, ps));
        const std::string pbody = ps == std::string::npos ? std::string{} : part.substr(ps + 4);
        const std::string disp = get(ph, "content-disposition");
        if (lowerAscii(disp).rfind("attachment", 0) == 0) {
            std::string name;
            if (const std::size_t f = disp.find("filename=\""); f != std::string::npos) name = disp.substr(f + 10, disp.find('"', f + 10) - f - 10);
            const std::size_t bytes = decodeBase64(pbody).size();
            r.attachment = name + " (" + std::to_string(bytes) + " octets)";
        } else if (r.body.empty()) {
            r.body = decodePart(ph, pbody);
        }
        pos = next;
    }
}

void serveSmtp(net::Socket& c, const std::function<void(Received)>& add) {
    constexpr int kWait = 5000;
    if (!c.sendAll("220 xpg-boite-essai ESMTP (la boite d'essai de l'IHM)\r\n", kWait)) return;
    std::string from;
    std::vector<std::string> to;
    for (int guard = 0; guard < 1000; ++guard) {
        std::string line;
        if (!c.receiveLine(line, kWait)) return;
        const std::string up = lowerAscii(line.substr(0, 10));
        const auto between = [](const std::string& s) {
            const std::size_t a = s.find('<'), b = s.find('>');
            return a != std::string::npos && b != std::string::npos && b > a ? s.substr(a + 1, b - a - 1) : trimmed(s.substr(s.find(':') + 1));
        };
        if (up.rfind("ehlo", 0) == 0) {
            if (!c.sendAll("250-xpg-boite-essai\r\n250-AUTH LOGIN PLAIN\r\n250 8BITMIME\r\n", kWait)) return;
        } else if (up.rfind("helo", 0) == 0) {
            if (!c.sendAll("250 xpg-boite-essai\r\n", kWait)) return;
        } else if (up.rfind("auth login", 0) == 0) {
            std::string ignored;
            if (!c.sendAll("334 VXNlcm5hbWU6\r\n", kWait) || !c.receiveLine(ignored, kWait)) return;
            if (!c.sendAll("334 UGFzc3dvcmQ6\r\n", kWait) || !c.receiveLine(ignored, kWait)) return;
            if (!c.sendAll("235 2.7.0 Authentication successful\r\n", kWait)) return;
        } else if (up.rfind("mail from", 0) == 0) {
            from = between(line);
            to.clear();
            if (!c.sendAll("250 2.1.0 Ok\r\n", kWait)) return;
        } else if (up.rfind("rcpt to", 0) == 0) {
            to.push_back(between(line));
            if (!c.sendAll("250 2.1.5 Ok\r\n", kWait)) return;
        } else if (up.rfind("data", 0) == 0) {
            if (!c.sendAll("354 End data with <CR><LF>.<CR><LF>\r\n", kWait)) return;
            std::string raw;
            for (int lines = 0; lines < 200000; ++lines) {
                std::string l;
                if (!c.receiveLine(l, kWait, 1 << 16)) return;
                if (l == ".") break;
                if (l.rfind("..", 0) == 0) l.erase(0, 1);
                raw += l + "\r\n";
            }
            Received r;
            r.at = clockNow().substr(11);
            r.channel = "Courriel";
            r.from = from;
            for (const auto& t : to) r.to += (r.to.empty() ? "" : ", ") + t;
            parseMessage(raw, r);
            add(std::move(r));
            if (!c.sendAll("250 2.0.0 Ok: queued\r\n", kWait)) return;
        } else if (up.rfind("quit", 0) == 0) {
            (void)c.sendAll("221 2.0.0 Bye\r\n", kWait);
            return;
        } else if (up.rfind("rset", 0) == 0 || up.rfind("noop", 0) == 0) {
            if (!c.sendAll("250 2.0.0 Ok\r\n", kWait)) return;
        } else {
            if (!c.sendAll("502 5.5.2 Error: command not recognized\r\n", kWait)) return;
        }
    }
}

void serveHttp(net::Socket& c, const std::function<void(Received)>& add) {
    constexpr int kWait = 5000;
    std::string request;
    if (!c.receiveLine(request, kWait)) return;
    long long length = 0;
    for (int guard = 0; guard < 100; ++guard) {
        std::string h;
        if (!c.receiveLine(h, kWait) || h.empty()) break;
        if (lowerAscii(h).rfind("content-length:", 0) == 0) length = std::atoll(h.c_str() + 15);
    }
    std::string body;
    if (length > 0 && length < (1 << 16)) {
        body.resize(static_cast<std::size_t>(length));
        if (!c.receiveExact(body.data(), body.size(), kWait)) body.clear();
    }
    const std::size_t sp1 = request.find(' '), sp2 = request.find(' ', sp1 + 1);
    const std::string target = sp1 == std::string::npos ? std::string{} : request.substr(sp1 + 1, sp2 - sp1 - 1);
    std::map<std::string, std::string> params;
    const auto parse = [&](std::string_view q) {
        std::size_t pos = 0;
        while (pos <= q.size()) {
            const std::size_t amp = q.find('&', pos);
            const std::string_view kv = q.substr(pos, (amp == std::string_view::npos ? q.size() : amp) - pos);
            const std::size_t eq = kv.find('=');
            if (eq != std::string_view::npos) params[lowerAscii(urlDecode(kv.substr(0, eq)))] = urlDecode(kv.substr(eq + 1));
            if (amp == std::string_view::npos) break;
            pos = amp + 1;
        }
    };
    if (const std::size_t q = target.find('?'); q != std::string::npos) parse(std::string_view(target).substr(q + 1));
    parse(body);
    const auto pick = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (const auto it = params.find(k); it != params.end()) return it->second;
        return std::string{};
    };
    Received r;
    r.at = clockNow().substr(11);
    r.channel = "SMS";
    r.to = pick({"to", "numero", "number", "phone", "tel"});
    r.body = pick({"text", "message", "msg", "body"});
    r.subject = r.body;
    const std::string method = request.substr(0, request.find(' '));
    r.from = "passerelle (" + method + " " + target.substr(0, target.find('?')) + ")";
    add(std::move(r));
    (void)c.sendAll("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\nOK\n", kWait);
}

} // namespace

void TestServer::run() {
    const auto adder = [this](Received r) { add(std::move(r)); };
    while (!stop_) {
        const auto ready = net::waitReadable({&sockets_->smtp, &sockets_->http}, 150);
        for (const auto i : ready) {
            auto& listener = i == 0 ? sockets_->smtp : sockets_->http;
            auto conn = listener.accept(0);
            if (!conn.valid()) continue;
            if (i == 0) serveSmtp(conn, adder);
            else serveHttp(conn, adder);
        }
    }
    running_ = false;
}

} // namespace hmi::notify
