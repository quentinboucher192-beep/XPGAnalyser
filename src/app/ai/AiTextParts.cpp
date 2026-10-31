// =============================================================================
//  app/ai/AiTextParts.cpp - voir AiTextParts.hpp
// =============================================================================
#include "AiTextParts.hpp"

#include "AiAnthropic.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>

namespace app::ai::textparts {

namespace fs = std::filesystem;

namespace {

bool validUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return false;
        i += n;
    }
    return true;
}

void putUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Windows-1252 vers UTF-8 (0x80-0x9F : la table de Windows ; le reste : Latin-1).
std::string fromCp1252(std::string_view s) {
    static constexpr unsigned k80[32] = {0x20AC, 0x81,   0x201A, 0x192,  0x201E, 0x2026, 0x2020, 0x2021, 0x2C6,  0x2030, 0x160,
                                         0x2039, 0x152,  0x8D,   0x17D,  0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                         0x2013, 0x2014, 0x2DC,  0x2122, 0x161,  0x203A, 0x153,  0x9D,   0x17E,  0x178};
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80) out += ch;
        else putUtf8(out, c < 0xA0 ? k80[c - 0x80] : c);
    }
    return out;
}

// Le debut d'un caractere UTF-8 a `n` ou avant.
std::size_t charStart(std::string_view s, std::size_t n) {
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) >> 6) == 0x2) --n;
    return n;
}

std::string sizeText(std::uintmax_t b) {
    char buf[48];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu o", static_cast<unsigned long long>(b));
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.0f Ko", static_cast<double>(b) / 1024.0);
    else std::snprintf(buf, sizeof buf, "%.1f Mo", static_cast<double>(b) / (1024.0 * 1024.0));
    std::string s = buf;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string guillemets(std::string_view name) { return "\xC2\xAB " + std::string(name) + " \xC2\xBB"; }

std::string partLabel(const std::string& name, std::uintmax_t bytes, std::size_t i, std::size_t n, const Part& p) {
    std::string h = "Pi\xC3\xA8" "ce jointe " + guillemets(name) + " (" + sizeText(bytes) + ")";
    if (n > 1)
        h += " \xE2\x80\x94 partie " + std::to_string(i + 1) + "/" + std::to_string(n) + " (lignes " + std::to_string(p.firstLine) + " \xC3\xA0 "
             + std::to_string(p.lastLine) + ")";
    return h;
}

std::string range(std::size_t from, std::size_t to) {   // 1-based, inclus
    return from == to ? "la partie " + std::to_string(from) : "les parties " + std::to_string(from) + " \xC3\xA0 " + std::to_string(to);
}

constexpr std::size_t kLabelBytes = 160;   // l'en-tete d'une partie, a peu pres
constexpr std::size_t kMentionBytes = 260; // une mention << pas envoyee >>

std::size_t extractBytes() { return static_cast<std::size_t>(kExtractTokens * kBytesPerToken) + kLabelBytes; }

// Les extraits deja faits, le temps de la session (la cle : le chemin, la taille, la partie).
std::mutex                         g_m;
std::map<std::string, std::string> g_extracts;

std::string cacheKey(const ProviderAttachment& a, std::size_t i, std::size_t n) {
    return a.path + "|" + std::to_string(a.bytes) + "|" + std::to_string(i) + "/" + std::to_string(n);
}

} // namespace

std::string normalize(std::string bytes) {
    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
    if (!validUtf8(bytes)) bytes = fromCp1252(bytes);
    return bytes;
}

std::vector<Part> split(std::string_view text, std::size_t maxBytes) {
    std::vector<Part> out;
    if (maxBytes < 64) maxBytes = 64;
    std::size_t pos = 0, line = 1;
    while (pos < text.size()) {
        std::size_t end = std::min(text.size(), pos + maxBytes);
        if (end < text.size()) {
            // la derniere fin de ligne de la fenetre ; aucune : une ligne trop longue, coupee sur un caractere entier
            const std::size_t nl = text.rfind('\n', end - 1);
            if (nl != std::string_view::npos && nl >= pos) end = nl + 1;
            else end = std::max(pos + 1, charStart(text, end));
        }
        Part p;
        p.text = std::string(text.substr(pos, end - pos));
        p.firstLine = line;
        const auto lines = static_cast<std::size_t>(std::count(p.text.begin(), p.text.end(), '\n'));
        p.lastLine = line + (lines == 0 ? 0 : (p.text.back() == '\n' ? lines - 1 : lines));
        line += lines;
        out.push_back(std::move(p));
        pos = end;
    }
    return out;
}

std::size_t budgetBytes(const ProviderRequest& req, int fallbackWindow) {
    const long long window = req.contextTokens > 0 ? req.contextTokens : fallbackWindow > 0 ? fallbackWindow : kUnknownWindow;
    std::size_t used = (req.systemStable.empty() && req.systemDynamic.empty() ? req.system.size() : req.systemStable.size() + req.systemDynamic.size())
                       + req.toolsJson.size();
    long long others = 0;   // les images et les PDF : leurs jetons, a peu pres
    for (const auto& t : req.turns) {
        used += t.text.size() + t.toolInputJson.size() + t.toolResultText.size() + 64;
        for (const auto& a : t.attachments) {
            const auto as = planAttachment(a).as;
            if (as == AttachmentPlan::As::Image) others += 1600;
            else if (as == AttachmentPlan::As::Document) others += static_cast<long long>(a.bytes / 1024) * 30 + 2000;   // ~ 1 500 jetons par page
            else if (as != AttachmentPlan::As::Text) others += 80;
        }
    }
    const double left = static_cast<double>(window) * 0.9 - static_cast<double>(std::max(0, req.maxTokens)) - static_cast<double>(used) / kBytesPerToken
                        - static_cast<double>(others) - 1000.0;
    return left <= 0 ? 0 : static_cast<std::size_t>(left * kBytesPerToken);
}

Prepared prepareOne(const ProviderAttachment& a, const std::string& text, std::size_t budget) {
    Prepared out;
    const auto parts = split(text);
    if (parts.empty()) {
        out.blocks.push_back(partLabel(a.name, a.bytes, 0, 1, Part{}) + " :\n(vide)");
        return out;
    }
    const std::size_t n = parts.size();
    std::size_t used = 0, k = 0;
    while (k < n && used + parts[k].text.size() + kLabelBytes + (k + 1 < n ? kMentionBytes : 0) <= budget) used += parts[k++].text.size() + kLabelBytes;
    for (std::size_t i = 0; i < k; ++i) out.blocks.push_back(partLabel(a.name, a.bytes, i, n, parts[i]) + " :\n" + parts[i].text);
    if (k < n) {
        out.blocks.push_back("[Pi\xC3\xA8" "ce jointe " + guillemets(a.name) + " : " + range(k + 1, n) + " sur " + std::to_string(n) + " (lignes "
                             + std::to_string(parts[k].firstLine) + " \xC3\xA0 " + std::to_string(parts.back().lastLine)
                             + ") ne sont pas envoy\xC3\xA9" "es : la fen\xC3\xAAtre de contexte du mod\xC3\xA8le est pleine. Ne devine pas leur contenu ; "
                               "si elles comptent, dis-le \xC3\xA0 l'utilisateur.]");
        out.note = guillemets(a.name) + " (" + sizeText(a.bytes) + ") ne tient pas entier dans la fen\xC3\xAAtre du mod\xC3\xA8le : "
                   + (k == 0 ? std::string("aucune partie envoy\xC3\xA9" "e") : range(1, k) + " sur " + std::to_string(n) + " envoy\xC3\xA9" "es") + ".";
    }
    return out;
}

ProviderRequest prereadRequest(const ProviderRequest& req, const std::string& question, const std::string& fileName, const Part& part, std::size_t index,
                               std::size_t count) {
    ProviderRequest m;
    m.model = req.model;
    m.endpoint = req.endpoint;
    m.timeoutSeconds = req.timeoutSeconds;
    m.maxTokens = kExtractTokens;
    m.preset = req.preset;
    m.contextTokens = req.contextTokens;
    m.textFallback = false;
    m.system = "Tu lis UNE partie d'un fichier joint, pour pr\xC3\xA9parer la r\xC3\xA9ponse \xC3\xA0 une demande. Recopie fid\xC3\xA8lement (valeurs, noms, "
               "num\xC3\xA9ros de ligne exacts) tout ce qui peut servir \xC3\xA0 cette demande ; r\xC3\xA9sume le reste en une ou deux phrases. "
               "N'invente rien, ne compl\xC3\xA8te rien, ne r\xC3\xA9ponds pas encore \xC3\xA0 la demande. Sois bref (au plus 1 500 mots).";
    ProviderTurn t;
    t.text = "Demande de l'utilisateur : " + (question.empty() ? std::string("(aucune, lis le fichier)") : question) + "\n\nFichier " + guillemets(fileName)
             + " \xE2\x80\x94 partie " + std::to_string(index + 1) + "/" + std::to_string(count) + " (lignes " + std::to_string(part.firstLine) + " \xC3\xA0 "
             + std::to_string(part.lastLine) + ") :\n" + part.text;
    m.turns.push_back(std::move(t));
    return m;
}

void forgetExtracts() {
    std::lock_guard<std::mutex> lk(g_m);
    g_extracts.clear();
}

void prepare(ProviderRequest& req, int fallbackWindow, const Ask& ask, const Progress& progress, const std::function<bool()>& cancelled,
             std::vector<std::string>* notes) {
    const auto note = [&](std::string n) {
        if (notes && !n.empty() && std::find(notes->begin(), notes->end(), n) == notes->end()) notes->push_back(std::move(n));
    };
    std::size_t budget = budgetBytes(req, fallbackWindow);
    std::ptrdiff_t last = -1;
    for (std::size_t i = req.turns.size(); i-- > 0;)
        if (req.turns[i].role == ProviderTurn::Role::User) {
            last = static_cast<std::ptrdiff_t>(i);
            break;
        }
    const std::string question = last >= 0 ? req.turns[static_cast<std::size_t>(last)].text : std::string{};
    for (std::size_t ti = req.turns.size(); ti-- > 0;) {   // la derniere demande d'abord
        auto& turn = req.turns[ti];
        if (turn.role != ProviderTurn::Role::User) continue;
        const bool latest = static_cast<std::ptrdiff_t>(ti) == last;
        for (auto& a : turn.attachments) {
            if (planAttachment(a).as != AttachmentPlan::As::Text) continue;
            std::string raw;
            if (a.path.empty() || !core::readFileAll(fs::path(std::u8string(a.path.begin(), a.path.end())), raw)) continue;   // le corps le dira
            const std::string text = normalize(std::move(raw));
            const auto parts = split(text);
            a.prepared = true;
            a.blocks.clear();
            const std::size_t n = parts.size();
            if (n == 0) {
                a.blocks.push_back(partLabel(a.name, a.bytes, 0, 1, Part{}) + " :\n(vide)");
                continue;
            }
            std::size_t whole = 0;
            for (const auto& p : parts) whole += p.text.size() + kLabelBytes;
            if (whole <= budget) {   // il tient : entier, en parties
                for (std::size_t i = 0; i < n; ++i) a.blocks.push_back(partLabel(a.name, a.bytes, i, n, parts[i]) + " :\n" + parts[i].text);
                budget -= whole;
                continue;
            }
            // Il ne tient pas : les premieres parties telles quelles, les suivantes lues a part (un extrait chacune).
            const bool canRead = latest && static_cast<bool>(ask);
            const auto cached = [&](std::size_t i) {
                std::lock_guard<std::mutex> lk(g_m);
                return g_extracts.count(cacheKey(a, i, n)) > 0;
            };
            const auto readable = [&](std::size_t i) { return canRead || cached(i); };
            const auto restCost = [&](std::size_t from) {   // les parties `from`.. : un extrait (au plus kMaxPrereads) ou une mention
                std::size_t cost = 0, reads = 0;
                for (std::size_t i = from; i < n; ++i)
                    if (readable(i) && reads < kMaxPrereads) {
                        cost += extractBytes();
                        ++reads;
                    } else {
                        cost += kMentionBytes / 4;
                    }
                return cost + kMentionBytes;
            };
            std::size_t k = 0, used = 0;
            while (k < n && used + parts[k].text.size() + kLabelBytes + restCost(k + 1) <= budget) used += parts[k++].text.size() + kLabelBytes;
            for (std::size_t i = 0; i < k; ++i) a.blocks.push_back(partLabel(a.name, a.bytes, i, n, parts[i]) + " :\n" + parts[i].text);
            std::size_t room = budget > used ? budget - used : 0;
            std::size_t readCount = 0, firstUnread = n, reads = 0, unread = 0;
            std::string failure;
            for (std::size_t i = k; i < n; ++i) {
                const bool stop = (cancelled && cancelled()) || reads >= kMaxPrereads || room < extractBytes() + kMentionBytes;
                std::string extract;
                bool have = false;
                if (!stop) {
                    {
                        std::lock_guard<std::mutex> lk(g_m);
                        if (const auto it = g_extracts.find(cacheKey(a, i, n)); it != g_extracts.end()) {
                            extract = it->second;
                            have = true;
                        }
                    }
                    if (!have && canRead) {
                        if (progress) progress("Lecture de " + guillemets(a.name) + " : partie " + std::to_string(i + 1) + "/" + std::to_string(n) + "\xE2\x80\xA6");
                        std::string err;
                        if (auto got = ask(prereadRequest(req, question, a.name, parts[i], i, n), &err); got && !got->empty()) {
                            extract = std::move(*got);
                            if (extract.size() > extractBytes() * 2) extract.resize(charStart(extract, extractBytes() * 2));
                            have = true;
                            std::lock_guard<std::mutex> lk(g_m);
                            g_extracts[cacheKey(a, i, n)] = extract;
                        } else if (failure.empty()) {
                            failure = err.empty() ? std::string("sans r\xC3\xA9ponse") : err;
                        }
                    }
                    ++reads;
                }
                if (!have) {
                    if (firstUnread == n) firstUnread = i;
                    ++unread;
                    continue;
                }
                std::string block = partLabel(a.name, a.bytes, i, n, parts[i]) + ", lue \xC3\xA0 part (un extrait, pas le texte entier) :\n" + extract;
                room = room > block.size() ? room - block.size() : 0;
                a.blocks.push_back(std::move(block));
                ++readCount;
            }
            if (unread > 0)
                a.blocks.push_back("[Pi\xC3\xA8" "ce jointe " + guillemets(a.name) + " : " + std::to_string(unread) + " partie(s) sur " + std::to_string(n)
                                   + " (\xC3\xA0 partir de la partie " + std::to_string(firstUnread + 1) + ", ligne " + std::to_string(parts[firstUnread].firstLine)
                                   + ") ne sont pas envoy\xC3\xA9" "es : la fen\xC3\xAAtre de contexte du mod\xC3\xA8le est pleine. Ne devine pas leur contenu ; "
                                     "si elles comptent, dis-le \xC3\xA0 l'utilisateur.]");
            std::string msg = guillemets(a.name) + " (" + sizeText(a.bytes) + ") ne tient pas entier dans la fen\xC3\xAAtre du mod\xC3\xA8le : " + std::to_string(n)
                              + " parties, ";
            msg += k == 0 ? std::string("aucune telle quelle") : (k == 1 ? std::string("la premi\xC3\xA8re") : "les " + std::to_string(k) + " premi\xC3\xA8res") + " telle(s) quelle(s)";
            if (readCount > 0) msg += ", " + std::to_string(readCount) + " lue(s) \xC3\xA0 part (un extrait chacune)";
            if (unread > 0) msg += ", " + std::to_string(unread) + " non lue(s)" + (failure.empty() ? std::string{} : " (" + failure + ")");
            note(msg + ".");
            budget = room;
        }
    }
}

} // namespace app::ai::textparts
