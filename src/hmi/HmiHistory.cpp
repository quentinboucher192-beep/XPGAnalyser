#include "HmiHistory.hpp"

#include "HmiCrypto.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace hmi {

std::string wallStamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[96];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return b;
}

double wallEpoch() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()) / 1000.0;
}

std::string csvField(std::string_view s) {
    bool quote = false;
    for (char c : s) if (c == ';' || c == '"' || c == '\n' || c == '\r') quote = true;
    if (!quote) return std::string(s);
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    out += '"';
    return out;
}

std::vector<std::string> csvSplit(std::string_view line, char separator, bool newlines) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else inQuotes = false;
            } else if (newlines && c == '\\' && i + 1 < line.size() && line[i + 1] == 'n') {
                cur += '\n';
                ++i;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == separator) {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

namespace {

template <class T>
void keepLast(std::vector<T>& v, std::size_t max) {
    if (v.size() > max) v.erase(v.begin(), v.begin() + static_cast<long>(v.size() - max));
}

// "2026-09-22 07:40" -> un nombre de jours, pour la conservation. Grossier
// (mois de 31 jours) mais monotone : c'est tout ce qu'on lui demande.
long dayNumber(std::string_view stamp) {
    int y = 0, m = 0, d = 0;
    if (stamp.size() < 10 || std::sscanf(std::string(stamp.substr(0, 10)).c_str(), "%d-%d-%d", &y, &m, &d) != 3) return -1;
    return static_cast<long>(y) * 372 + static_cast<long>(m) * 31 + d;
}

std::string header(std::string_view which) {
    if (which == "alarmes") return "alarme;nom;priorite;categorie;groupe;message;apparue;acquittee;par;disparue";
    if (which == "mesures") return "horodatage;epoch;variable;valeur";
    if (which == "audit") return "horodatage;utilisateur;type;source;cible;avant;apres;motif;signature;precedent;empreinte";
    if (which == "comptes") return "login;echecs;verrouille_le;jusqu_a";
    return "horodatage;type;source;message;utilisateur";
}

// Lot 13 : un champ d'audit tient sur une ligne (le CSV le relit a l'identique,
// l'empreinte aussi).
std::string oneLine(std::string s) {
    for (auto& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}

std::string auditLine(const AuditEntry& e) {
    return csvField(e.stamp) + ";" + csvField(e.user) + ";" + csvField(e.kind) + ";" + csvField(e.source) + ";" + csvField(e.target) + ";"
         + csvField(e.before) + ";" + csvField(e.after) + ";" + csvField(e.reason) + ";" + csvField(e.signature) + ";" + e.previous + ";"
         + e.hash + "\n";
}

bool sameLogin(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string thousands(std::size_t n) {
    std::string s = std::to_string(n), out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) out += ' ';
        out += s[i];
    }
    return out;
}

} // namespace

// ================================================================ lot 13 : audit ===
std::string auditHash(const AuditEntry& e) {
    std::string text;
    text.reserve(256);
    for (const std::string* f : {&e.previous, &e.stamp, &e.user, &e.kind, &e.source, &e.target, &e.before, &e.after, &e.reason,
                                 &e.signature}) {
        text += *f;
        text += '\x1F';          // un separateur qu'aucun champ ne contient
    }
    return toHex(sha256(text));
}

const AuditEntry& appendAudit(std::vector<AuditEntry>& list, AuditEntry e) {
    for (std::string* f : {&e.stamp, &e.user, &e.kind, &e.source, &e.target, &e.before, &e.after, &e.reason, &e.signature}) *f = oneLine(*f);
    e.previous = list.empty() ? std::string(kAuditGenesis) : list.back().hash;
    e.hash = auditHash(e);
    list.push_back(std::move(e));
    return list.back();
}

AuditCheck verifyAudit(const std::vector<AuditEntry>& list) {
    AuditCheck c;
    c.lines = list.size();
    if (list.empty()) {
        c.message = "journal d'audit vide";
        return c;
    }
    c.fromOrigin = list.front().previous == kAuditGenesis;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& e = list[i];
        if (auditHash(e) != e.hash) {
            c.ok = false;
            c.bad = i + 1;
            c.message = "ligne " + std::to_string(i + 1) + " (" + e.stamp.substr(0, std::min<std::size_t>(19, e.stamp.size()))
                      + ") : empreinte fausse \xE2\x80\x94 la ligne a \xC3\xA9t\xC3\xA9 retouch\xC3\xA9" "e";
            return c;
        }
        if (i > 0 && e.previous != list[i - 1].hash) {
            c.ok = false;
            c.bad = i + 1;
            c.message = "ligne " + std::to_string(i + 1) + " (" + e.stamp.substr(0, std::min<std::size_t>(19, e.stamp.size()))
                      + ") : cha\xC3\xAEnage rompu \xE2\x80\x94 une ligne a \xC3\xA9t\xC3\xA9 supprim\xC3\xA9" "e ou ins\xC3\xA9r\xC3\xA9" "e avant elle";
            return c;
        }
    }
    c.message = thousands(list.size()) + (list.size() > 1 ? " lignes" : " ligne") + " : cha\xC3\xAEne intacte"
              + (c.fromOrigin ? std::string(" depuis la toute premi\xC3\xA8re")
                              : std::string(" (les plus anciennes sont tomb\xC3\xA9" "es : limite de ") + thousands(kAuditMax) + ")");
    return c;
}

AccountState* History::account(std::string_view login) noexcept {
    for (auto& a : accounts) if (sameLogin(a.login, login)) return &a;
    return nullptr;
}
const AccountState* History::account(std::string_view login) const noexcept {
    for (const auto& a : accounts) if (sameLogin(a.login, login)) return &a;
    return nullptr;
}

void History::trim(const HistorySettings& s, std::string_view nowStamp) {
    const auto max = static_cast<std::size_t>(std::max(1, s.maxEntries));
    keepLast(alarms, max);
    keepLast(events, max);
    keepLast(system, max);
    keepLast(samples, max * 8);      // plusieurs variables par instant
    keepLast(audit, kAuditMax);      // lot 13 : sa propre limite, pas de conservation par date
    // Lot 13 : un compte sans echec ni verrou n'a rien a garder.
    accounts.erase(std::remove_if(accounts.begin(), accounts.end(),
                                  [](const AccountState& a) { return a.failures == 0 && a.lockedAt.empty(); }),
                   accounts.end());
    const long today = dayNumber(nowStamp);
    if (today < 0 || s.retentionDays <= 0) return;
    const long oldest = today - s.retentionDays;
    auto tooOld = [&](std::string_view stamp) { const long d = dayNumber(stamp); return d >= 0 && d < oldest; };
    alarms.erase(std::remove_if(alarms.begin(), alarms.end(), [&](const AlarmOccurrence& a) { return tooOld(a.appeared); }), alarms.end());
    events.erase(std::remove_if(events.begin(), events.end(), [&](const HistoryEvent& e) { return tooOld(e.stamp); }), events.end());
    system.erase(std::remove_if(system.begin(), system.end(), [&](const HistoryEvent& e) { return tooOld(e.stamp); }), system.end());
    samples.erase(std::remove_if(samples.begin(), samples.end(), [&](const HistorySample& e) { return tooOld(e.stamp); }), samples.end());
}

std::string historyCsv(const History& h, std::string_view which) {
    std::string out = header(which) + "\n";
    if (which == "alarmes") {
        for (const auto& a : h.alarms)
            out += std::to_string(a.alarm) + ";" + csvField(a.name) + ";" + std::to_string(a.priority) + ";" + csvField(a.category)
                 + ";" + csvField(a.group) + ";" + csvField(a.message) + ";" + a.appeared + ";" + a.acked + ";"
                 + csvField(a.ackedBy) + ";" + a.cleared + "\n";
    } else if (which == "mesures") {
        char b[64];
        for (const auto& m : h.samples) {
            std::snprintf(b, sizeof b, "%.3f", m.epoch);
            out += m.stamp + ";" + b + ";" + csvField(m.variable) + ";" + formatNumber(m.value) + "\n";
        }
    } else if (which == "audit") {
        for (const auto& e : h.audit) out += auditLine(e);
    } else if (which == "comptes") {
        char b[64];
        for (const auto& a : h.accounts) {
            std::snprintf(b, sizeof b, "%.3f", a.lockedUntil);
            out += csvField(a.login) + ";" + std::to_string(a.failures) + ";" + a.lockedAt + ";" + b + "\n";
        }
    } else {
        const auto& list = which == "systeme" ? h.system : h.events;
        for (const auto& e : list)
            out += e.stamp + ";" + csvField(e.kind) + ";" + csvField(e.source) + ";" + csvField(e.message) + ";" + csvField(e.user) + "\n";
    }
    return out;
}

bool parseHistoryCsv(std::string_view text, std::string_view which, History& into, std::string* error) {
    std::size_t pos = 0, lineNo = 0;
    bool first = true;
    while (pos < text.size()) {
        const std::size_t eol = text.find('\n', pos);
        std::string_view line = text.substr(pos, (eol == std::string_view::npos ? text.size() : eol) - pos);
        pos = eol == std::string_view::npos ? text.size() : eol + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (first) { first = false; continue; }           // l'entete
        // Lot 13 : le journal d'audit se relit a l'identique (son empreinte en depend).
        const auto f = csvSplit(line, ';', which != "audit");
        if (which == "audit") {
            if (f.size() < 11) { if (error) *error = "ligne " + std::to_string(lineNo) + " : 11 colonnes attendues"; return false; }
            AuditEntry e;
            e.stamp = f[0];
            e.user = f[1];
            e.kind = f[2];
            e.source = f[3];
            e.target = f[4];
            e.before = f[5];
            e.after = f[6];
            e.reason = f[7];
            e.signature = f[8];
            e.previous = f[9];
            e.hash = f[10];
            into.audit.push_back(std::move(e));
            continue;
        }
        if (which == "comptes") {
            if (f.size() < 4) { if (error) *error = "ligne " + std::to_string(lineNo) + " : 4 colonnes attendues"; return false; }
            AccountState a;
            a.login = f[0];
            a.failures = std::max(0, std::atoi(f[1].c_str()));
            a.lockedAt = f[2];
            a.lockedUntil = std::strtod(f[3].c_str(), nullptr);
            if (!a.login.empty()) into.accounts.push_back(std::move(a));
            continue;
        }
        if (which == "alarmes") {
            if (f.size() < 10) { if (error) *error = "ligne " + std::to_string(lineNo) + " : 10 colonnes attendues"; return false; }
            AlarmOccurrence a;
            a.alarm = static_cast<Id>(std::strtoul(f[0].c_str(), nullptr, 10));
            a.name = f[1];
            a.priority = std::atoi(f[2].c_str());
            a.category = f[3];
            a.group = f[4];
            a.message = f[5];
            a.appeared = f[6];
            a.acked = f[7];
            a.ackedBy = f[8];
            a.cleared = f[9];
            into.alarms.push_back(std::move(a));
        } else if (which == "mesures") {
            if (f.size() < 4) { if (error) *error = "ligne " + std::to_string(lineNo) + " : 4 colonnes attendues"; return false; }
            HistorySample m;
            m.stamp = f[0];
            m.epoch = std::strtod(f[1].c_str(), nullptr);
            m.variable = f[2];
            double v = 0;
            (void)parseNumber(f[3], v);
            m.value = v;
            into.samples.push_back(std::move(m));
        } else {
            if (f.size() < 5) { if (error) *error = "ligne " + std::to_string(lineNo) + " : 5 colonnes attendues"; return false; }
            HistoryEvent e{f[0], f[1], f[2], f[3], f[4]};
            (which == "systeme" ? into.system : into.events).push_back(std::move(e));
        }
    }
    return true;
}

core::Status saveHistory(const History& h, const std::string& projectFolder) {
    const fs::path dir = fs::path(projectFolder) / "ihm" / "historique";
    std::error_code ec;
    if (h.empty() && !fs::exists(dir, ec)) return core::ok();     // rien a garder, rien a creer
    fs::create_directories(dir, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, "dossier " + dir.string() + " : " + ec.message());
    for (const auto which : kHistoryFiles) {
        const fs::path target = dir / (std::string(which) + ".csv");
        const fs::path tmp = target.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + tmp.string());
            out << historyCsv(h, which);
            if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + tmp.string());
        }
        fs::rename(tmp, target, ec);
        if (ec) {
            fs::remove(target, ec);
            fs::rename(tmp, target, ec);
            if (ec) return core::fail(core::ErrorCode::FileUnreadable, "remplacement impossible : " + target.string());
        }
    }
    return core::ok();
}

core::Status appendAuditFile(const AuditEntry& e, const std::string& projectFolder) {
    if (projectFolder.empty()) return core::ok();
    const fs::path dir = fs::path(projectFolder) / "ihm" / "historique";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, "dossier " + dir.string() + " : " + ec.message());
    const fs::path file = dir / "audit.csv";
    const bool fresh = !fs::exists(file, ec) || fs::file_size(file, ec) == 0;
    std::ofstream out(file, std::ios::binary | std::ios::app);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + file.string());
    if (fresh) out << header("audit") << "\n";
    out << auditLine(e);
    out.flush();
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + file.string());
    return core::ok();
}

History loadHistory(const std::string& projectFolder, std::vector<std::string>* warnings) {
    History h;
    const fs::path dir = fs::path(projectFolder) / "ihm" / "historique";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return h;
    for (const auto which : kHistoryFiles) {
        const fs::path file = dir / (std::string(which) + ".csv");
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::ostringstream s;
        s << in.rdbuf();
        std::string error;
        if (!parseHistoryCsv(s.str(), which, h, &error) && warnings)
            warnings->push_back("historique " + std::string(which) + ".csv : " + error);
    }
    return h;
}

} // namespace hmi
