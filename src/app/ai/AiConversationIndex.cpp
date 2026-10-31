// =============================================================================
//  app/ai/AiConversationIndex.cpp - voir AiConversationIndex.hpp
// =============================================================================
#include "AiConversationIndex.hpp"

#include "AiMemoryJson.hpp"
#include "AiPaths.hpp"
#include "../../core/AtomicFile.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../ui/TextSearch.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>

namespace app::ai::convindex {

namespace fs = std::filesystem;

namespace {

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool isWordByte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

// Les mots d'un texte, tels quels ; un tiret entre deux caracteres de mot reste dans le mot (V-201).
std::vector<std::string> rawTokens(std::string_view text) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (isWordByte(c) || (c == '-' && !cur.empty() && i + 1 < text.size() && isWordByte(static_cast<unsigned char>(text[i + 1])))) {
            cur += static_cast<char>(c);
        } else if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool hasDigit(std::string_view s) { return std::any_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }); }
bool ascii(std::string_view s) { return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }); }

// Un nom d'objet ou de variable : commence par une lettre, ASCII, et porte un '_', des chiffres, ou une majuscule au milieu.
bool looksLikeName(std::string_view t) {
    if (t.size() < 3 || t.size() > 64 || !ascii(t) || !std::isalpha(static_cast<unsigned char>(t[0]))) return false;
    if (t.find('_') != std::string_view::npos) return true;
    if (hasDigit(t)) return true;
    for (std::size_t i = 1; i < t.size(); ++i)
        if (std::isupper(static_cast<unsigned char>(t[i])) && std::islower(static_cast<unsigned char>(t[i - 1]))) return true;
    return false;
}

// Un repere d'equipement : 1 a 3 majuscules, un tiret ou un souligne facultatif, 2 a 4 chiffres (P101, LT_201, V-12).
bool looksLikeTag(std::string_view t) {
    std::size_t i = 0;
    while (i < t.size() && i < 3 && std::isupper(static_cast<unsigned char>(t[i]))) ++i;
    if (i == 0) return false;
    if (i < t.size() && (t[i] == '-' || t[i] == '_')) ++i;
    std::size_t d = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
        ++i;
        ++d;
    }
    if (i < t.size() && std::isupper(static_cast<unsigned char>(t[i]))) ++i;
    return d >= 2 && d <= 4 && i == t.size();
}

const std::set<std::string>& stopWords() {
    static const std::set<std::string> k = {
        "avec", "dans", "pour", "mais", "plus", "tout", "tous", "toute", "toutes", "cette", "cela", "ceci", "comme", "elle", "elles",
        "nous", "vous", "leur", "leurs", "sont", "etre", "avoir", "fait", "faire", "peux", "peut", "veux", "voici", "voila", "aussi",
        "alors", "ainsi", "entre", "apres", "avant", "sans", "sous", "chez", "donc", "quand", "quel", "quelle", "quels", "quelles",
        "dont", "ceux", "celle", "celui", "celles", "merci", "bonjour", "bien", "tres", "trop", "encore", "deja", "meme", "autre",
        "autres", "chaque", "faut", "doit", "vais", "sera", "serait", "avait", "etait", "pourquoi", "comment", "parce", "puis",
        "votre", "notre", "leurs", "cest", "quil", "quelle", "pouvez", "pourrais", "pourrait", "veut", "dois", "suis", "estce",
        "avez", "ajoute", "ajouter", "cree", "creer", "moi", "toi", "lui", "the", "and", "this", "that", "with", "from", "have",
        "your", "here", "there", "what", "when", "which", "will", "would", "should", "could", "true", "false", "null"};
    return k;
}

struct EquipmentWord {
    std::string_view folded, label;
};
constexpr EquipmentWord kEquipment[] = {
    {"pompe", "pompe"}, {"vanne", "vanne"}, {"electrovanne", "\xC3\xA9lectrovanne"}, {"moteur", "moteur"}, {"cuve", "cuve"},
    {"reservoir", "r\xC3\xA9servoir"}, {"capteur", "capteur"}, {"sonde", "sonde"}, {"transmetteur", "transmetteur"},
    {"variateur", "variateur"}, {"automate", "automate"}, {"compresseur", "compresseur"}, {"ventilateur", "ventilateur"},
    {"convoyeur", "convoyeur"}, {"doseur", "doseur"}, {"debitmetre", "d\xC3\xA9" "bitm\xC3\xA8tre"}, {"chaudiere", "chaudi\xC3\xA8re"},
    {"echangeur", "\xC3\xA9" "changeur"}, {"filtre", "filtre"}, {"agitateur", "agitateur"}, {"malaxeur", "malaxeur"},
    {"broyeur", "broyeur"}, {"verin", "v\xC3\xA9rin"}, {"disjoncteur", "disjoncteur"}, {"contacteur", "contacteur"},
    {"m340", "M340"}, {"m580", "M580"}, {"premium", "Premium"}, {"quantum", "Quantum"}, {"modbus", "Modbus"},
    {"profinet", "Profinet"}, {"profibus", "Profibus"}, {"opc", "OPC"}, {"ihm", "IHM"}, {"plc", "PLC"},
};

std::string folded(std::string_view s) { return ui::foldForSearch(s); }

std::string singular(std::string w) {
    if (w.size() > 4 && w.back() == 's') w.pop_back();
    if (w.size() > 4 && w.back() == 'x') w.pop_back();
    return w;
}

void addUnique(std::vector<std::string>& v, std::string s, std::size_t max) {
    if (s.empty() || v.size() >= max) return;
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
}

bool containsAny(const std::string& foldedText, std::initializer_list<std::string_view> marks) {
    for (const auto m : marks)
        if (foldedText.find(m) != std::string::npos) return true;
    return false;
}

std::string oneLine(std::string_view s, std::size_t max) {
    std::string out;
    out.reserve(std::min(s.size(), max + 8));
    bool space = false;
    for (const char ch : s) {
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            space = true;
            continue;
        }
        if (ch == '`' || ch == '*') continue;
        if (space && !out.empty()) out += ' ';
        space = false;
        out += ch;
        if (out.size() >= max) break;
    }
    if (out.size() >= max) {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out = out.substr(0, cut) + "\xE2\x80\xA6";
    }
    return out;
}

void languagesOf(std::string_view raw, const std::string& f, std::vector<std::string>& out) {
    // les blocs de code : ```st, ```cpp...
    for (std::size_t at = raw.find("```"); at != std::string_view::npos; at = raw.find("```", at + 3)) {
        std::size_t e = at + 3;
        while (e < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[e])) || raw[e] == '+')) ++e;
        std::string tag(raw.substr(at + 3, e - at - 3));
        for (auto& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tag == "st" || tag == "iecst" || tag == "scl") addUnique(out, "ST", 12);
        else if (tag == "c") addUnique(out, "C", 12);
        else if (tag == "cpp" || tag == "c++" || tag == "cxx") addUnique(out, "C++", 12);
        else if (tag == "json") addUnique(out, "JSON", 12);
        else if (tag == "xml") addUnique(out, "XML", 12);
        else if (tag == "python" || tag == "py") addUnique(out, "Python", 12);
        else if (tag == "ladder" || tag == "ld") addUnique(out, "Ladder", 12);
        if (e == at + 3) at = e;   // un bloc qui se ferme : rien apres
    }
    if (f.find("texte structure") != std::string::npos) addUnique(out, "ST", 12);
    if (raw.find("C++") != std::string_view::npos) addUnique(out, "C++", 12);
    if (f.find("ladder") != std::string::npos) addUnique(out, "Ladder", 12);
    if (f.find("grafcet") != std::string::npos) addUnique(out, "Grafcet", 12);
    for (const auto& t : rawTokens(raw)) {
        if (t == "ST") addUnique(out, "ST", 12);
        else if (t == "SFC") addUnique(out, "Grafcet", 12);
        else if (t == "FBD") addUnique(out, "FBD", 12);
        else if (t == "DFB") addUnique(out, "DFB", 12);
    }
}

std::string languageOfExt(std::string_view e) {
    if (e == "st") return "ST";
    if (e == "c" || e == "h") return "C";
    if (e == "cpp" || e == "hpp") return "C++";
    if (e == "json") return "JSON";
    if (e == "xml") return "XML";
    return {};
}

std::string extOf(const Attachment& a) {
    if (!a.ext.empty()) return a.ext;
    if (!a.type.empty()) return a.type;
    const auto dot = a.name.rfind('.');
    std::string e = dot == std::string::npos ? std::string{} : a.name.substr(dot + 1);
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

std::string datePart(std::string_view iso) { return std::string(iso.substr(0, std::min<std::size_t>(10, iso.size()))); }

std::string shortDay(std::string_view iso) {
    if (iso.size() < 16) return std::string(iso);
    return std::string(iso.substr(8, 2)) + "/" + std::string(iso.substr(5, 2)) + "/" + std::string(iso.substr(0, 4)) + " " + std::string(iso.substr(11, 5));
}

// ---- le JSON de l'index
json::Value strings(const std::vector<std::string>& v) {
    json::Value a = json::Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}
std::vector<std::string> stringsOf(const json::Value& v) {
    std::vector<std::string> out;
    for (const auto& s : v.items())
        if (s.isString()) out.push_back(s.asString());
    return out;
}
json::Value snippets(const std::vector<Snippet>& v) {
    json::Value a = json::Value::array();
    for (const auto& s : v) {
        json::Value o = json::Value::object();
        o.set("message", s.message);
        o.set("role", s.role);
        o.set("date", s.date);
        o.set("texte", s.text);
        a.push(std::move(o));
    }
    return a;
}
std::vector<Snippet> snippetsOf(const json::Value& v) {
    std::vector<Snippet> out;
    for (const auto& o : v.items())
        if (o.isObject()) out.push_back({o.str("message"), o.str("role"), o.str("date"), o.str("texte")});
    return out;
}

} // namespace

// ------------------------------------------------------------------- l'entree ---
Entry indexConversation(const Conversation& c) {
    Entry e;
    e.id = c.id;
    e.title = c.title;
    e.created = c.created;
    e.modified = c.modified;
    e.project = c.project.name;
    e.projectPath = c.project.path;
    e.projectId = c.project.extra.str("id");
    e.messages = c.messages.size();
    e.stamp = c.modified + "|" + std::to_string(c.messages.size());
    std::map<std::string, int> counts;
    std::set<std::string> equipmentSeen;
    const auto& stop = stopWords();
    std::string all = c.title + "\n";
    for (const auto& m : c.messages) {
        const std::string f = folded(m.text);
        all += m.text;
        all += '\n';
        // les mots-cles
        for (const auto& t : rawTokens(f)) {
            if (t.size() < 4 || hasDigit(t) || stop.count(t)) continue;
            ++counts[singular(t)];
        }
        // les noms d'objets, les reperes, les equipements
        for (const auto& t : rawTokens(m.text)) {
            if (looksLikeTag(t)) addUnique(e.equipment, t, 40);
            if (looksLikeName(t)) addUnique(e.objects, t, 60);
            const std::string ft = singular(folded(t));
            for (const auto& w : kEquipment)
                if (ft == w.folded) addUnique(e.equipment, std::string(w.label), 40);
        }
        for (std::size_t at = m.text.find('`'); at != std::string::npos;) {
            const auto end = m.text.find('`', at + 1);
            if (end == std::string::npos) break;
            const std::string inner = m.text.substr(at + 1, end - at - 1);
            if (!inner.empty() && inner.size() <= 60 && inner.find_first_of(" \n") == std::string::npos && inner != "``") addUnique(e.objects, inner, 60);
            at = m.text.find('`', end + 1);
        }
        languagesOf(m.text, f, e.languages);
        // les genres d'objets IHM : la cle (Tank) ou le libelle (Cuve)
        for (const auto& info : hmi::kKindInfos) {
            const std::string label = folded(info.label);
            bool hit = false;
            if (label != "texte" && label != "image" && label != "ligne" && label.size() >= 4) {
                const auto at = f.find(label);
                hit = at != std::string::npos && (at == 0 || !isWordByte(static_cast<unsigned char>(f[at - 1])))
                      && (at + label.size() >= f.size() || !isWordByte(static_cast<unsigned char>(f[at + label.size()])));
            }
            if (!hit && info.key.size() >= 4) {
                const auto at = m.text.find(info.key);
                hit = at != std::string::npos && (at == 0 || !isWordByte(static_cast<unsigned char>(m.text[at - 1])))
                      && (at + info.key.size() >= m.text.size() || !isWordByte(static_cast<unsigned char>(m.text[at + info.key.size()])));
            }
            if (hit) addUnique(e.types, std::string(info.key), 30);
        }
        // les pieces jointes
        for (const auto& a : m.attachments) {
            addUnique(e.attachments, a.name, 40);
            const std::string ext = extOf(a);
            addUnique(e.types, ext, 30);
            addUnique(e.languages, languageOfExt(ext), 12);
        }
        // les problemes et les solutions
        const bool user = m.role == "utilisateur";
        if (user && containsAny(f, {"erreur", "probleme", "bug", "marche pas", "fonctionne pas", "plante", "echec", "impossible", "compile pas",
                                     "defaut", "incorrect", "panne", "bloque", "s'affiche pas", "saffiche pas"}))
            if (e.problems.size() < 12) e.problems.push_back({m.id, m.role, m.date, oneLine(m.text, 240)});
        const bool applied = m.proposal && m.proposal->state == "appliquee";
        if (!user && (applied || containsAny(f, {"solution", "corrige", "correction", "il suffit", "resolu", "remplace", "la cause", "pour corriger"})))
            if (e.solutions.size() < 12)
                e.solutions.push_back({m.id, m.role, m.date,
                                       applied ? "Proposition appliqu\xC3\xA9" "e : " + oneLine(m.proposal->summary.empty() ? m.text : m.proposal->summary, 200)
                                               : oneLine(m.text, 240)});
    }
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sorted.size() && i < 25; ++i) e.keywords.push_back(sorted[i].first);
    return e;
}

Index buildIndex(const Index& previous, const std::vector<Conversation>& all, std::size_t* reindexed) {
    Index out;
    out.built = isoDate(now());
    std::map<std::string, const Entry*> old;
    for (const auto& e : previous.entries) old[e.id] = &e;
    std::size_t n = 0;
    for (const auto& c : all) {
        const std::string stamp = c.modified + "|" + std::to_string(c.messages.size());
        if (const auto it = old.find(c.id); it != old.end() && it->second->stamp == stamp) {
            out.entries.push_back(*it->second);
            continue;
        }
        out.entries.push_back(indexConversation(c));
        ++n;
    }
    if (reindexed) *reindexed = n;
    return out;
}

std::string indexFile() { return root().empty() ? std::string{} : detail::join(indexDir(), "conversations.json"); }

Index loadIndex() {
    Index out;
    const std::string file = indexFile();
    std::string text;
    if (file.empty() || !core::readFileAll(P(file), text)) return out;
    json::Value v;
    if (!json::parse(text, v) || !v.isObject() || v.integer("format") != 1) return out;
    out.built = v.str("construit");
    for (const auto& o : v.at("conversations").items()) {
        if (!o.isObject()) continue;
        Entry e;
        e.id = o.str("id");
        e.title = o.str("titre");
        e.created = o.str("cree");
        e.modified = o.str("modifie");
        e.project = o.str("projet");
        e.projectPath = o.str("chemin");
        e.projectId = o.str("projetId");
        e.stamp = o.str("empreinte");
        e.messages = static_cast<std::size_t>(std::max<std::int64_t>(0, o.integer("messages")));
        e.keywords = stringsOf(o.at("motsCles"));
        e.objects = stringsOf(o.at("objets"));
        e.equipment = stringsOf(o.at("equipements"));
        e.languages = stringsOf(o.at("langages"));
        e.types = stringsOf(o.at("genres"));
        e.attachments = stringsOf(o.at("pieces"));
        e.problems = snippetsOf(o.at("problemes"));
        e.solutions = snippetsOf(o.at("solutions"));
        if (!e.id.empty()) out.entries.push_back(std::move(e));
    }
    return out;
}

bool saveIndex(const Index& index, std::string* why) {
    const std::string file = indexFile();
    if (file.empty()) return true;
    json::Value v = json::Value::object();
    v.set("format", 1);
    v.set("construit", index.built);
    json::Value list = json::Value::array();
    for (const auto& e : index.entries) {
        json::Value o = json::Value::object();
        o.set("id", e.id);
        o.set("titre", e.title);
        o.set("cree", e.created);
        o.set("modifie", e.modified);
        o.set("projet", e.project);
        o.set("chemin", e.projectPath);
        o.set("projetId", e.projectId);
        o.set("empreinte", e.stamp);
        o.set("messages", static_cast<std::uint64_t>(e.messages));
        o.set("motsCles", strings(e.keywords));
        o.set("objets", strings(e.objects));
        o.set("equipements", strings(e.equipment));
        o.set("langages", strings(e.languages));
        o.set("genres", strings(e.types));
        o.set("pieces", strings(e.attachments));
        o.set("problemes", snippets(e.problems));
        o.set("solutions", snippets(e.solutions));
        list.push(std::move(o));
    }
    v.set("conversations", std::move(list));
    std::error_code ec;
    fs::create_directories(P(file).parent_path(), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    if (auto r = core::writeFileAtomic(P(file), json::write(v), how); !r) {
        if (why) *why = "\xC3\xA9" "criture impossible : " + file;
        return false;
    }
    return true;
}

Index refreshIndex(std::size_t* reindexed, std::vector<Conversation>* conversations) {
    std::vector<Conversation> all = loadConversations();
    Index idx = buildIndex(loadIndex(), all, reindexed);
    (void)saveIndex(idx);
    if (conversations) *conversations = std::move(all);
    return idx;
}

// ----------------------------------------------------------------- la recherche ---
Result search(const Index& index, const Query& q, const std::vector<Conversation>& conversations) {
    Result r;
    const ui::SearchQuery words(q.text);
    std::map<std::string, const Conversation*> byId;
    for (const auto& c : conversations) byId[c.id] = &c;
    const std::string fObject = folded(q.object), fType = folded(q.type), fEquip = singular(folded(q.equipment)), fLang = folded(q.language),
                      fProject = folded(q.project);
    const auto anyContains = [](const std::vector<std::string>& v, const std::string& needle) {
        for (const auto& s : v)
            if (folded(s).find(needle) != std::string::npos) return true;
        return false;
    };
    for (const auto& e : index.entries) {
        if (!q.excludeConversation.empty() && e.id == q.excludeConversation) continue;
        ++r.searched;
        Match m;
        m.id = e.id;
        m.title = e.title;
        m.project = e.project;
        m.created = e.created;
        m.modified = e.modified;
        m.messages = e.messages;
        // les filtres
        if (!fProject.empty() && e.projectId != q.project && folded(e.project) != fProject) continue;
        if (!q.from.empty() && datePart(e.modified.empty() ? e.created : e.modified) < datePart(q.from)) continue;
        if (!q.to.empty() && datePart(e.created) > datePart(q.to)) continue;
        if (!fObject.empty()) {
            if (!anyContains(e.objects, fObject)) continue;
            m.why.push_back("objet " + q.object);
            m.score += 8;
        }
        if (!fType.empty()) {
            bool hit = anyContains(e.types, fType);
            if (!hit)
                for (const auto& info : hmi::kKindInfos)
                    if (folded(info.label) == fType && std::find(e.types.begin(), e.types.end(), std::string(info.key)) != e.types.end()) hit = true;
            if (!hit) continue;
            m.why.push_back("genre " + q.type);
            m.score += 5;
        }
        if (!fEquip.empty()) {
            if (!anyContains(e.equipment, fEquip)) continue;
            m.why.push_back("\xC3\xA9quipement " + q.equipment);
            m.score += 5;
        }
        if (!fLang.empty()) {
            bool hit = false;
            for (const auto& l : e.languages) hit = hit || folded(l) == fLang;
            if (!hit) continue;
            m.why.push_back("langage " + q.language);
            m.score += 4;
        }
        if (q.kind == Query::Kind::Problem && e.problems.empty()) continue;
        if (q.kind == Query::Kind::Solution && e.solutions.empty()) continue;
        // les mots : dans le titre, l'index ou les messages (tous les mots, dans l'un ou l'autre)
        const auto it = byId.find(e.id);
        const Conversation* conv = it == byId.end() ? nullptr : it->second;
        struct Scored {
            int         rank{0};
            std::size_t order{0};
            Excerpt     ex;
        };
        std::vector<Scored> candidates;
        if (!words.empty()) {
            ui::SearchQuery::Progress pr;
            words.feed(pr, e.title);
            for (const auto& k : e.keywords) words.feed(pr, k);
            for (const auto& o : e.objects) words.feed(pr, o);
            for (const auto& a : e.attachments) words.feed(pr, a);
            if (conv)
                for (const auto& msg : conv->messages) words.feed(pr, msg.text);
            if (!words.satisfied(pr)) continue;
            m.why.push_back("mots");
            if (words.matches({e.title})) m.score += 15;
            if (conv)
                for (std::size_t i = 0; i < conv->messages.size(); ++i) {
                    const auto& msg = conv->messages[i];
                    const bool all = words.matches({msg.text});
                    const bool some = !all && !words.ranges(msg.text).empty();
                    if (!all && !some) continue;
                    m.score += all ? 10 : 3;
                    candidates.push_back({all ? 2 : 1, i, {e.id, e.title, msg.id, msg.role, msg.date, oneLine(msg.text, 260)}});
                }
        }
        // problemes et solutions : leurs extraits d'abord quand on les cherche
        if (q.kind != Query::Kind::Any) {
            const auto& list = q.kind == Query::Kind::Problem ? e.problems : e.solutions;
            m.why.push_back(q.kind == Query::Kind::Problem ? "probl\xC3\xA8me" : "solution");
            m.score += 6;
            for (std::size_t i = 0; i < list.size(); ++i)
                if (words.empty() || words.matches({list[i].text}))
                    candidates.push_back({3, i, {e.id, e.title, list[i].message, list[i].role, list[i].date, list[i].text}});
        }
        if (words.empty() && q.kind == Query::Kind::Any && conv) {   // des filtres seuls : le debut de la conversation
            for (std::size_t i = 0; i < conv->messages.size() && candidates.size() < q.maxExcerpts; ++i)
                candidates.push_back({0, i, {e.id, e.title, conv->messages[i].id, conv->messages[i].role, conv->messages[i].date, oneLine(conv->messages[i].text, 260)}});
        }
        if (m.why.empty() && words.empty() && q.kind == Query::Kind::Any && fObject.empty() && fType.empty() && fEquip.empty() && fLang.empty())
            m.why.push_back(fProject.empty() ? "date" : "projet");
        // les meilleurs extraits, remis dans l'ordre de la conversation, sans doublon de message
        std::stable_sort(candidates.begin(), candidates.end(), [](const Scored& a, const Scored& b) { return a.rank > b.rank; });
        std::vector<Scored> kept;
        for (auto& cnd : candidates) {
            if (kept.size() >= q.maxExcerpts) break;
            if (std::any_of(kept.begin(), kept.end(), [&](const Scored& k) { return !k.ex.message.empty() && k.ex.message == cnd.ex.message; })) continue;
            kept.push_back(std::move(cnd));
        }
        std::stable_sort(kept.begin(), kept.end(), [](const Scored& a, const Scored& b) { return a.ex.date < b.ex.date; });
        for (auto& k : kept) m.excerpts.push_back(std::move(k.ex));
        r.matches.push_back(std::move(m));
    }
    std::stable_sort(r.matches.begin(), r.matches.end(), [](const Match& a, const Match& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.modified > b.modified;
    });
    if (r.matches.size() > q.maxConversations) r.matches.resize(q.maxConversations);
    for (const auto& m : r.matches)
        for (const auto& ex : m.excerpts) r.chronology.push_back(ex);
    std::stable_sort(r.chronology.begin(), r.chronology.end(), [](const Excerpt& a, const Excerpt& b) { return a.date < b.date; });
    return r;
}

std::string resultText(const Result& r, const Query& q) {
    std::string what = q.text;
    for (const auto* f : {&q.object, &q.type, &q.equipment, &q.language, &q.project})
        if (!f->empty()) what += (what.empty() ? "" : ", ") + *f;
    if (r.matches.empty())
        return "Aucune conversation sauvegard\xC3\xA9" "e ne correspond" + (what.empty() ? std::string{} : " \xC3\xA0 \xC2\xAB " + what + " \xC2\xBB")
               + " (" + std::to_string(r.searched) + " parcourue(s)). Dis-le \xC3\xA0 l'utilisateur : n'invente pas de souvenir.";
    std::string out = std::to_string(r.matches.size()) + (r.matches.size() > 1 ? " conversations retrouv\xC3\xA9" "es" : " conversation retrouv\xC3\xA9" "e")
                      + " (sur " + std::to_string(r.searched) + ") :";
    std::size_t n = 0;
    for (const auto& m : r.matches) {
        out += "\n" + std::to_string(++n) + ". \xC2\xAB " + m.title + " \xC2\xBB (" + (m.project.empty() ? std::string{} : "projet " + m.project + ", ")
               + "du " + shortDay(m.created) + " au " + shortDay(m.modified) + ", " + std::to_string(m.messages) + " messages, id " + m.id + ")";
        if (!m.why.empty()) {
            std::string w;
            for (const auto& s : m.why) w += (w.empty() ? "" : ", ") + s;
            out += " \xE2\x80\x94 correspond : " + w;
        }
        for (const auto& ex : m.excerpts)
            out += "\n   - " + shortDay(ex.date) + " " + (ex.role == "assistant" ? "Assistant" : "Utilisateur") + " : " + ex.text;
    }
    if (r.chronology.size() > 1) {
        out += "\nChronologie (du plus ancien au plus r\xC3\xA9" "cent) :";
        for (const auto& ex : r.chronology)
            out += "\n- " + shortDay(ex.date) + " [" + ex.title + "] " + (ex.role == "assistant" ? "Assistant" : "Utilisateur") + " : " + oneLine(ex.text, 140);
    }
    out += "\nCes extraits viennent des conversations sauvegard\xC3\xA9" "es sur ce PC : ne compl\xC3\xA8te pas ce qu'ils ne disent pas.";
    return out;
}

// ---------------------------------------------------------------- le fil d'indexation ---
Indexer::Indexer() : thread_([this] { run(); }) {}

Indexer::~Indexer() {
    {
        const std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::uint64_t Indexer::rebuild() {
    std::uint64_t t;
    {
        const std::lock_guard<std::mutex> lk(m_);
        t = ++next_;
        jobs_.push_back({t, false, {}});
    }
    cv_.notify_all();
    return t;
}

std::uint64_t Indexer::search(Query q) {
    std::uint64_t t;
    {
        const std::lock_guard<std::mutex> lk(m_);
        t = ++next_;
        jobs_.push_back({t, true, std::move(q)});
    }
    cv_.notify_all();
    return t;
}

std::vector<Indexer::Event> Indexer::poll() {
    const std::lock_guard<std::mutex> lk(m_);
    std::vector<Event> out;
    out.swap(done_);
    return out;
}

bool Indexer::busy() const {
    const std::lock_guard<std::mutex> lk(m_);
    return working_ || !jobs_.empty();
}

void Indexer::cancel() {
    const std::lock_guard<std::mutex> lk(m_);
    jobs_.clear();
    cancelledUpTo_ = next_;
}

bool Indexer::waitIdle(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        if (!busy()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return !busy();
}

void Indexer::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            working_ = true;
        }
        Event ev;
        ev.ticket = job.ticket;
        std::vector<Conversation> convs;
        const Index idx = refreshIndex(&ev.reindexed, &convs);
        ev.conversations = idx.entries.size();
        if (job.searching) {
            ev.type = Event::Type::Searched;
            ev.result = convindex::search(idx, job.query, convs);
        }
        const std::lock_guard<std::mutex> lk(m_);
        working_ = false;
        if (job.ticket > cancelledUpTo_) done_.push_back(std::move(ev));
    }
}

// ------------------------------------------------------------------- l'outil ---
std::string searchConversationsToolJson() {
    return "{\"name\": \"chercher_conversations\", \"description\": \"Cherche dans les ANCIENNES conversations sauvegardees sur le PC de "
           "l'utilisateur (pour reprendre un travail, retrouver une solution, une decision, un code). Rend les conversations "
           "trouvees, des extraits et une chronologie. Presente a l'utilisateur ce qui a ete retrouve avant de poursuivre ; si rien "
           "n'est trouve, dis-le : n'invente jamais un souvenir.\", \"input_schema\": {\"type\": \"object\", \"properties\": {"
           "\"mots\": {\"type\": \"string\", \"description\": \"les mots-cles (tous doivent se trouver)\"}, "
           "\"objet\": {\"type\": \"string\", \"description\": \"un nom d'objet, de variable ou de bloc (Pompe_1_Marche, DFB_Temps)\"}, "
           "\"genre\": {\"type\": \"string\", \"description\": \"un genre d'objet IHM ou une extension de fichier joint\"}, "
           "\"equipement\": {\"type\": \"string\"}, \"langage\": {\"type\": \"string\", \"description\": \"ST, C, C++, JSON, XML...\"}, "
           "\"projet\": {\"type\": \"string\", \"description\": \"le nom du projet\"}, "
           "\"depuis\": {\"type\": \"string\", \"description\": \"date ISO AAAA-MM-JJ\"}, \"jusqua\": {\"type\": \"string\"}, "
           "\"genre_de_resultat\": {\"type\": \"string\", \"enum\": [\"tout\", \"probleme\", \"solution\"]}}}}";
}

std::string runSearchConversationsTool(std::string_view inputJson, std::string_view currentConversation, bool* isError) {
    if (isError) *isError = true;
    memory::Json in;
    std::string why;
    if (!memory::parseJson(inputJson, in, &why) || !in.isObject()) return "Entr\xC3\xA9" "e illisible (" + why + ").";
    if (root().empty()) return "Aucune conversation n'est sauvegard\xC3\xA9" "e sur ce poste : rien \xC3\xA0 chercher. Dis-le \xC3\xA0 l'utilisateur.";
    if (isError) *isError = false;
    Query q;
    q.text = in.str("mots");
    q.object = in.str("objet");
    q.type = in.str("genre");
    q.equipment = in.str("equipement");
    q.language = in.str("langage");
    q.project = in.str("projet");
    q.from = in.str("depuis");
    q.to = in.str("jusqua");
    const std::string kind = in.str("genre_de_resultat");
    q.kind = kind == "probleme" ? Query::Kind::Problem : kind == "solution" ? Query::Kind::Solution : Query::Kind::Any;
    q.excludeConversation = std::string(currentConversation);
    std::vector<Conversation> convs;
    const Index idx = refreshIndex(nullptr, &convs);
    return resultText(search(idx, q, convs), q);
}

} // namespace app::ai::convindex
