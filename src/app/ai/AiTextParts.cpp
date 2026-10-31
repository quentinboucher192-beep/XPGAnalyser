// =============================================================================
//  app/ai/AiTextParts.cpp - voir AiTextParts.hpp
// =============================================================================
#include "AiTextParts.hpp"

#include "AiAnthropic.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
             std::vector<std::string>* notes, Library* library) {
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
            // 1.20.1 : et son texte exact reste lisible a la demande (l'outil de lecture).
            if (library && std::none_of(library->files.begin(), library->files.end(), [&](const Library::File& f) { return f.name == a.name; })) {
                Library::File f;
                f.name = a.name;
                f.text = text;
                f.lineStarts.push_back(0);
                for (std::size_t i = 0; i < f.text.size(); ++i)
                    if (f.text[i] == '\n' && i + 1 < f.text.size()) f.lineStarts.push_back(i + 1);
                library->files.push_back(std::move(f));
            }
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
                std::string block = partLabel(a.name, a.bytes, i, n, parts[i]) + ", lue \xC3\xA0 part (un extrait, pas le texte entier"
                                    + (library ? std::string(" ; son texte exact : l'outil lire_piece_jointe") : std::string{}) + ") :\n" + extract;
                room = room > block.size() ? room - block.size() : 0;
                a.blocks.push_back(std::move(block));
                ++readCount;
            }
            if (unread > 0)
                a.blocks.push_back("[Pi\xC3\xA8" "ce jointe " + guillemets(a.name) + " : " + std::to_string(unread) + " partie(s) sur " + std::to_string(n)
                                   + " (\xC3\xA0 partir de la partie " + std::to_string(firstUnread + 1) + ", ligne " + std::to_string(parts[firstUnread].firstLine)
                                   + ") ne sont pas envoy\xC3\xA9" "es : la fen\xC3\xAAtre de contexte du mod\xC3\xA8le est pleine. Ne devine pas leur contenu ; "
                                   + (library ? std::string("lis le texte exact des lignes utiles avec l'outil lire_piece_jointe, ou dis-le \xC3\xA0 l'utilisateur.]")
                                              : std::string("si elles comptent, dis-le \xC3\xA0 l'utilisateur.]")));
            std::string msg = guillemets(a.name) + " (" + sizeText(a.bytes) + ") ne tient pas entier dans la fen\xC3\xAAtre du mod\xC3\xA8le : " + std::to_string(n)
                              + " parties, ";
            msg += k == 0 ? std::string("aucune telle quelle") : (k == 1 ? std::string("la premi\xC3\xA8re") : "les " + std::to_string(k) + " premi\xC3\xA8res") + " telle(s) quelle(s)";
            if (readCount > 0) msg += ", " + std::to_string(readCount) + " lue(s) \xC3\xA0 part (un extrait chacune)";
            if (unread > 0) msg += ", " + std::to_string(unread) + " non lue(s)" + (failure.empty() ? std::string{} : " (" + failure + ")");
            note(msg + (library ? std::string(" L'IA peut en lire le texte exact \xC3\xA0 la demande.") : std::string(".")));
            budget = room;
        }
    }
    if (library) library->room = budget;
}

std::string readToolJson() {
    return "{\"name\": \"lire_piece_jointe\", \"description\": \"Lit le texte EXACT de lignes d'une piece jointe qui n'a pas tenu "
           "entiere dans la demande (ses parties sont en extrait ou non envoyees). Un appel a la fois ; demande seulement les lignes "
           "utiles (la place est comptee). Ne devine jamais le contenu non lu.\", \"input_schema\": {\"type\": \"object\", "
           "\"properties\": {\"fichier\": {\"type\": \"string\", \"description\": \"le nom de la piece jointe\"}, "
           "\"de_ligne\": {\"type\": \"integer\", \"description\": \"la premiere ligne (1 = la premiere)\"}, "
           "\"a_ligne\": {\"type\": \"integer\", \"description\": \"la derniere ligne (incluse)\"}}, "
           "\"required\": [\"fichier\", \"de_ligne\", \"a_ligne\"]}}";
}

std::string withReadTool(const std::string& toolsJson) {
    if (toolsJson.empty()) return "[" + readToolJson() + "]";
    const auto close = toolsJson.find_last_of(']');
    if (close == std::string::npos) return toolsJson;
    const auto open = toolsJson.find('[');
    const bool empty = open != std::string::npos && toolsJson.find_first_not_of(" \t\r\n", open + 1) == close;
    return toolsJson.substr(0, close) + (empty ? "" : ", ") + readToolJson() + toolsJson.substr(close);
}

std::string Library::read(std::string_view inputJson, std::string* detail) {
    // L'entree : {"fichier": ..., "de_ligne": n, "a_ligne": m} - lue sans dependre du lecteur JSON (des entiers, un nom).
    const auto field = [&](std::string_view key) -> std::string {
        const std::string k = "\"" + std::string(key) + "\"";
        auto at = inputJson.find(k);
        if (at == std::string_view::npos) return {};
        at = inputJson.find(':', at + k.size());
        if (at == std::string_view::npos) return {};
        ++at;
        while (at < inputJson.size() && (inputJson[at] == ' ' || inputJson[at] == '\t')) ++at;
        if (at < inputJson.size() && inputJson[at] == '"') {
            std::string out;
            for (std::size_t i = at + 1; i < inputJson.size() && inputJson[i] != '"'; ++i) {
                if (inputJson[i] == '\\' && i + 1 < inputJson.size()) ++i;
                out += inputJson[i];
            }
            return out;
        }
        std::size_t end = at;
        while (end < inputJson.size() && (std::isdigit(static_cast<unsigned char>(inputJson[end])) || inputJson[end] == '-')) ++end;
        return std::string(inputJson.substr(at, end - at));
    };
    const std::string name = field("fichier");
    const File* file = nullptr;
    for (const auto& f : files) {
        std::string a = f.name, b = name;
        for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (a == b) file = &f;
    }
    if (!file) {
        std::string list;
        for (const auto& f : files) list += (list.empty() ? "" : ", ") + guillemets(f.name);
        return "Pi\xC3\xA8" "ce jointe " + guillemets(name) + " introuvable parmi celles qu'on peut lire : " + list + ".";
    }
    const long long total = static_cast<long long>(file->lineStarts.size());
    long long from = std::atoll(field("de_ligne").c_str()), to = std::atoll(field("a_ligne").c_str());
    if (from < 1) from = 1;
    if (to < from) to = from;
    if (from > total) return guillemets(file->name) + " n'a que " + std::to_string(total) + " lignes.";
    if (to > total) to = total;
    const auto begin = file->lineStarts[static_cast<std::size_t>(from - 1)];
    const auto endOf = [&](long long line) {   // la fin de la ligne `line` (1 = la premiere) : le debut de la suivante
        return line < total ? file->lineStarts[static_cast<std::size_t>(line)] : file->text.size();
    };
    const std::string head = guillemets(file->name) + ", lignes " + std::to_string(from) + " \xC3\xA0 ";
    if (room < 512) return "Plus de place dans la demande pour lire " + guillemets(file->name) + " : r\xC3\xA9ponds avec ce que tu as, ou dis \xC3\xA0 "
                           "l'utilisateur ce qui manque (une question plus pr\xC3\xA9" "cise, ou un mod\xC3\xA8le \xC3\xA0 plus grande fen\xC3\xAAtre).";
    const std::size_t space = room - head.size() - 160;
    long long last = to;
    while (last > from && endOf(last) - begin > space) --last;   // jusqu'a la derniere ligne entiere qui tient
    std::size_t end = endOf(last);
    if (end - begin > space) end = charStart(file->text, begin + space);   // une seule ligne trop longue : coupee
    std::string out = head + std::to_string(last) + " (texte exact) :\n" + file->text.substr(begin, end - begin);
    if (last < to) out += "\n[La place manque : lignes " + std::to_string(last + 1) + " \xC3\xA0 " + std::to_string(to) + " non lues.]";
    room = room > out.size() ? room - out.size() : 0;
    if (detail) *detail = "Lecture exacte de " + guillemets(file->name) + " : lignes " + std::to_string(from) + " \xC3\xA0 " + std::to_string(last) + "\xE2\x80\xA6";
    return out;
}

} // namespace app::ai::textparts
