// =============================================================================
//  app/ai/AiActivity.cpp - ce que fait l'IA, a l'instant, et ce que ca coute (1.22.6) : voir AiActivity.hpp
// =============================================================================
#include "AiActivity.hpp"

#include "AiUsage.hpp"   // tokensText, waitText

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app::ai::activity {

namespace {

const char* const kEllipsis = "\xE2\x80\xA6";
const char* const kDot = " \xC2\xB7 ";
const char* const kOpsTool = "proposer_operations";   // kOperationsTool (AiProvider.hpp)
const char* const kPlanTool = "planifier_mission";    // mission::kPlanTool (AiMission.hpp)

std::string withoutEllipsis(std::string s) {
    for (;;) {
        if (s.size() >= 3 && s.compare(s.size() - 3, 3, kEllipsis) == 0) s.resize(s.size() - 3);
        else if (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
        else break;
    }
    return s;
}
std::string withEllipsis(const std::string& s) { return withoutEllipsis(s) + kEllipsis; }

std::string plural(long long n, const char* one, const char* many) { return grouped(n) + " " + (n > 1 ? many : one); }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendUtf8(std::string& out, unsigned cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';   // une moitie de paire : sans interet pour un nom
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Un outil repondu sur ce PC (le fil du fournisseur), en clair : ce qu'il fait, ce qu'il a fait, le but de l'appel qui l'a
// demande. `specific` : la phrase dit plus que ce que l'outil dit de lui-meme (son etat << outil >>).
struct ToolWords {
    std::string doing, done, purpose;
    bool        specific{true};
};
ToolWords localWords(const std::string& name, const std::string& input) {
    json::Value v;
    if (!json::parse(input.empty() ? std::string_view("{}") : std::string_view(input), v) || !v.isObject()) v = json::Value::object();
    const auto num = [&](const char* k, long long d) -> long long {
        const json::Value* x = v.find(k);
        if (!x) return d;
        if (x->isNumber()) return x->asInt(d);
        if (x->isString()) return std::atoll(x->asString().c_str());
        return d;
    };
    ToolWords w;
    if (name == "lignes_classeur") {   // sheets::kRowsTool (AiFileModels.hpp)
        const std::string f = v.str("fichier");
        const long long from = std::max(1LL, num("a_partir_de", 1));
        const long long count = std::clamp(num("combien", 200), 1LL, 200LL);
        const std::string rows = "lignes " + std::to_string(from) + " \xC3\xA0 " + std::to_string(from + count - 1);
        w.doing = "L'IA lit " + (f.empty() ? std::string("le classeur joint") : "le classeur " + f) + " (outil lignes_classeur, " + rows + ")" + kEllipsis;
        w.done = (f.empty() ? std::string("Classeur joint lu") : "Classeur " + f + " lu") + " (" + rows + ")";
        w.purpose = "Lecture du " + (f.empty() ? std::string("classeur joint") : "classeur " + f) + " (lignes_classeur)";
    } else if (name == "analyser_projet") {   // agent::kAnalyseTool (AiAgentTools.hpp)
        std::string what = v.str("question");
        if (const std::string s = v.str("sujet"); !s.empty()) what += (what.empty() ? std::string{} : std::string(" de ")) + s;
        const std::string tag = "analyser_projet" + (what.empty() ? std::string{} : " : " + what);
        w.doing = "Analyse du projet (" + tag + ")" + kEllipsis;
        w.done = "Projet analys\xC3\xA9 (" + tag + ")";
        w.purpose = "Analyse du projet (" + tag + ")";
    } else if (name == "lire_piece_jointe") {   // textparts::kReadTool (AiTextParts.hpp)
        const std::string f = v.str("fichier");
        const long long from = num("de_ligne", 0), to = num("a_ligne", 0);
        const std::string rows = from > 0 ? ", lignes " + std::to_string(from) + (to >= from ? " \xC3\xA0 " + std::to_string(to) : std::string{}) : std::string{};
        w.doing = "L'IA relit " + (f.empty() ? std::string("une pi\xC3\xA8" "ce jointe") : f) + " (lire_piece_jointe" + rows + ")" + kEllipsis;
        w.done = "Lecture exacte de " + (f.empty() ? std::string("la pi\xC3\xA8" "ce jointe") : f) + (rows.empty() ? std::string{} : " (" + rows.substr(2) + ")");
        w.purpose = "Lecture exacte d'une pi\xC3\xA8" "ce jointe (lire_piece_jointe)";
    } else {
        struct Known {
            const char* name;
            const char* doing;
            const char* purpose;
        };
        static const Known kKnown[] = {
            {"executer_code", "Essai de code dans le bac \xC3\xA0 sable (executer_code)", "Essai de code (executer_code)"},
            {"proposer_souvenir", "L'IA propose un souvenir (proposer_souvenir)", "Souvenir propos\xC3\xA9 (proposer_souvenir)"},
            {"chercher_souvenirs", "L'IA cherche dans les souvenirs (chercher_souvenirs)", "Recherche dans les souvenirs (chercher_souvenirs)"},
            {"chercher_conversations", "L'IA cherche dans les conversations (chercher_conversations)", "Recherche dans les conversations (chercher_conversations)"},
            {"chercher_missions", "L'IA cherche dans les missions (chercher_missions)", "Recherche dans les missions (chercher_missions)"},
            {"etat_mission", "L'IA lit une mission enregistr\xC3\xA9" "e (etat_mission)", "Lecture d'une mission (etat_mission)"},
            {"detecter_equipements", "L'IA pr\xC3\xA9pare une d\xC3\xA9tection r\xC3\xA9seau (detecter_equipements)",
             "D\xC3\xA9tection r\xC3\xA9seau propos\xC3\xA9" "e (detecter_equipements)"},
            {"resultat_detection", "L'IA lit le compte rendu du scan (resultat_detection)", "Lecture du compte rendu du scan (resultat_detection)"},
        };
        w.specific = false;
        w.doing = "L'IA utilise l'outil " + name;
        w.purpose = "Outil " + name;
        for (const auto& k : kKnown)
            if (name == k.name) {
                w.doing = k.doing;
                w.purpose = k.purpose;
            }
        w.done = w.purpose;
        w.doing += kEllipsis;
    }
    return w;
}

// Une etape de planifier_mission, en clair : "S5 : Configurations de gaz".
std::string stepText(const std::map<std::string, std::string>& f) {
    const auto get = [&](const char* k) {
        const auto it = f.find(k);
        return it == f.end() ? std::string{} : it->second;
    };
    const std::string id = get("id"), title = get("titre");
    if (id.empty()) return title;
    return title.empty() ? id : id + " : " + title;
}

} // namespace

// ---- les textes ----------------------------------------------------------------------------------------------------
std::string clockText(double seconds) {
    const long long s = std::max(0LL, static_cast<long long>(std::floor(seconds)));
    char buf[40];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
    return buf;
}

std::string shortDuration(double seconds) {
    if (seconds < 0) seconds = 0;
    char buf[40];
    if (seconds < 10.0) {
        std::snprintf(buf, sizeof buf, "%.1f", std::floor(seconds * 10.0) / 10.0);
        std::string s = buf;
        std::replace(s.begin(), s.end(), '.', ',');
        return s + "\xC2\xA0s";
    }
    if (seconds < 60.0) {
        std::snprintf(buf, sizeof buf, "%.0f", std::floor(seconds));
        return std::string(buf) + "\xC2\xA0s";
    }
    return clockText(seconds);
}

std::string grouped(long long n) {
    const std::string d = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += "\xC2\xA0";
        out += d[i];
    }
    return (n < 0 ? "-" : "") + out;
}

long long estimateTokens(std::size_t bytes) {
    if (bytes == 0) return 0;
    return std::max(1LL, static_cast<long long>((bytes + kBytesPerToken - 1) / kBytesPerToken));
}

long long requestTokens(const ProviderRequest& r) {
    std::size_t bytes = r.systemStable.empty() ? r.system.size() : r.systemStable.size() + r.systemDynamic.size();
    bytes += r.toolsJson.size();
    for (const auto& t : r.localTools) bytes += t.definitionJson.size();
    for (const auto& t : r.turns) {
        bytes += t.text.size() + t.toolInputJson.size() + t.toolResultText.size();
        for (const auto& a : t.attachments)
            for (const auto& b : a.blocks) bytes += b.size();
    }
    return estimateTokens(bytes);
}

// ---- l'entree d'un appel d'outil ---------------------------------------------------------------------------------------
bool InputReader::inItem() const noexcept {
    return stack_.size() == 3 && stack_[0].object && stack_[0].key == key_ && !stack_[1].object && stack_[2].object;
}

void InputReader::feed(std::string_view chunk) {
    bytes_ += chunk.size();
    for (const char ch : chunk) {
        if (over_) return;
        if (inStr_) {
            if (hex_ > 0) {   // \uXXXX
                const int d = hexValue(ch);
                code_ = code_ * 16 + static_cast<unsigned>(d < 0 ? 0 : d);
                if (--hex_ == 0) {
                    hex_ = -1;
                    if (keep_ && str_.size() < 160) appendUtf8(str_, code_);
                }
                continue;
            }
            if (esc_) {
                esc_ = false;
                if (ch == 'u') {
                    hex_ = 4;
                    code_ = 0;
                    continue;
                }
                if (keep_ && str_.size() < 160) str_ += (ch == 'n' || ch == 't' || ch == 'r' || ch == 'b' || ch == 'f') ? ' ' : ch;
                continue;
            }
            if (ch == '\\') {
                esc_ = true;
                continue;
            }
            if (ch == '"') {
                inStr_ = false;
                endString();
                continue;
            }
            if (keep_ && str_.size() < 160) str_ += ch;
            continue;
        }
        switch (ch) {
            case '"': {
                inStr_ = true;
                str_.clear();
                strKey_ = !stack_.empty() && stack_.back().object && stack_.back().expectKey;
                // les cles (courtes) ; les textes d'un element suivi ; le premier de ses "objets"
                const bool firstOfObjects = stack_.size() == 4 && stack_[0].object && stack_[0].key == key_ && !stack_[1].object && stack_[2].object
                                            && stack_[2].key == "objets" && !stack_[3].object && stack_[3].index == 0;
                keep_ = strKey_ || inItem() || firstOfObjects;
                break;
            }
            case '{':
            case '[': {
                if (ch == '{' && stack_.size() == 2 && stack_[0].object && stack_[0].key == key_ && !stack_[1].object) {
                    ++items_;   // un element du tableau suivi commence
                    last_.clear();
                }
                Frame f;
                f.object = ch == '{';
                stack_.push_back(std::move(f));
                break;
            }
            case '}':
            case ']': {
                if (stack_.empty()) break;
                if (ch == ']' && stack_.size() == 2 && stack_[0].object && stack_[0].key == key_ && !stack_[1].object) closed_ = true;
                stack_.pop_back();
                if (stack_.empty()) over_ = true;   // l'objet racine est fini : la suite (le texte apres un bloc) ne compte pas
                break;
            }
            case ':':
                if (!stack_.empty() && stack_.back().object) stack_.back().expectKey = false;
                break;
            case ',':
                if (!stack_.empty()) {
                    if (stack_.back().object) stack_.back().expectKey = true;
                    else ++stack_.back().index;
                }
                break;
            default: break;
        }
    }
}

void InputReader::endString() {
    if (stack_.empty()) return;
    if (strKey_) {
        stack_.back().key = str_;
        return;
    }
    if (!keep_) return;
    if (inItem()) {
        last_[stack_[2].key] = str_;
    } else if (stack_.size() == 4 && stack_[2].key == "objets" && !last_.count("objets")) {
        last_["objets"] = str_;
    }
}

std::string operationText(const std::map<std::string, std::string>& fields) {
    const auto get = [&](const char* k) {
        const auto it = fields.find(k);
        return it == fields.end() ? std::string{} : it->second;
    };
    const std::string action = get("action"), nom = get("nom"), objet = get("objet"), objets = get("objets"), variable = get("variable"),
                      vue = get("vue"), genre = get("genre");
    const std::string target = !nom.empty() ? nom : !objet.empty() ? objet : !objets.empty() ? objets : action == "adresser" ? variable : genre;
    struct Verb {
        const char* action;
        const char* text;
    };
    static const Verb kVerbs[] = {
        {"creer", "cr\xC3\xA9" "er"},
        {"modifier", "modifier"},
        {"associer", "associer"},
        {"aligner", "aligner"},
        {"distribuer", "r\xC3\xA9partir"},
        {"supprimer", "supprimer"},
        {"dupliquer", "dupliquer"},
        {"code", "\xC3\xA9" "crire le code"},
        {"creer_vue", "cr\xC3\xA9" "er la vue"},
        {"creer_variables", "cr\xC3\xA9" "er des variables"},
        {"creer_type", "cr\xC3\xA9" "er le type"},
        {"creer_equipement", "cr\xC3\xA9" "er l'\xC3\xA9quipement"},
        {"adresser", "adresser"},
        {"creer_script", "cr\xC3\xA9" "er le script"},
        {"creer_fonction", "cr\xC3\xA9" "er la fonction"},
        {"creer_alarme", "cr\xC3\xA9" "er l'alarme"},
        {"creer_ressource", "cr\xC3\xA9" "er la ressource"},
        {"creer_dossier", "cr\xC3\xA9" "er le dossier"},
        {"ranger", "ranger"},
    };
    std::string s;
    if (!action.empty()) {
        s = action;
        for (const auto& v : kVerbs)
            if (action == v.action) s = v.text;
    }
    if (!target.empty() && action != "creer_variables") s += (s.empty() ? std::string{} : std::string(" ")) + target;
    if (action == "associer" && !variable.empty()) s += " \xC3\xA0 " + variable;
    if (!vue.empty() && action != "creer_vue" && vue != target && !s.empty()) s += " sur " + vue;
    return s;
}

// ---- le bilan ----------------------------------------------------------------------------------------------------------
long long Bill::total() const noexcept {
    long long n = 0;
    for (const auto& c : calls) n += c.total();
    return n;
}

bool Bill::estimated() const noexcept {
    return std::any_of(calls.begin(), calls.end(), [](const Call& c) { return c.estimated(); });
}

json::Value toJson(const Bill& b) {
    json::Value o = json::Value::object();
    o.set("format", 1);
    o.set("duree", std::round(b.seconds * 10.0) / 10.0);
    json::Value list = json::Value::array();
    for (const auto& c : b.calls) {
        json::Value x = json::Value::object();
        x.set("but", c.purpose);
        x.set("entree", static_cast<std::int64_t>(c.usage.input));
        x.set("cacheLu", static_cast<std::int64_t>(c.usage.cacheRead));
        x.set("cacheEcrit", static_cast<std::int64_t>(c.usage.cacheWrite));
        x.set("sortie", static_cast<std::int64_t>(c.output()));
        if (c.thinking > 0) {
            x.set("reflexion", static_cast<std::int64_t>(c.thinking));
            x.set("reflexionExacte", c.thinkingExact);
        }
        if (c.estimated()) x.set("estime", true);
        if (c.aside) x.set("aPart", true);
        if (c.carried) x.set("dejaCompte", true);
        x.set("duree", std::round(c.seconds * 10.0) / 10.0);
        list.push(std::move(x));
    }
    o.set("appels", std::move(list));
    return o;
}

std::optional<Bill> billFrom(const json::Value& extra) {
    if (!extra.isObject()) return std::nullopt;
    const json::Value* v = extra.find("bilan");
    if (!v || !v->isObject()) return std::nullopt;
    const json::Value* list = v->find("appels");
    if (!list || !list->isArray()) return std::nullopt;
    Bill b;
    b.seconds = v->num("duree");
    for (const auto& x : list->items()) {
        if (!x.isObject()) continue;
        Call c;
        c.purpose = x.str("but");
        c.usage.input = x.integer("entree");
        c.usage.cacheRead = x.integer("cacheLu");
        c.usage.cacheWrite = x.integer("cacheEcrit");
        const long long out = x.integer("sortie");
        if (x.flag("estime")) c.estimate = out;
        else c.usage.output = out;
        c.outputKnown = !x.flag("estime");
        c.thinking = x.integer("reflexion");
        c.thinkingExact = x.flag("reflexionExacte");
        c.aside = x.flag("aPart");
        c.carried = x.flag("dejaCompte");
        c.seconds = x.num("duree");
        b.calls.push_back(std::move(c));
    }
    if (b.calls.empty()) return std::nullopt;
    return b;
}

std::string billLine(const Bill& b) {
    std::string s = (b.estimated() ? std::string("env. ") : std::string{}) + tokensText(b.total()) + " jetons";
    if (b.seconds > 0) s += kDot + clockText(b.seconds);
    if (b.calls.size() > 1) s += kDot + std::to_string(b.calls.size()) + " appels";
    return s;
}

std::vector<std::string> billDetail(const Bill& b) {
    std::vector<std::string> out;
    long long in = 0, outTokens = 0;
    for (std::size_t i = 0; i < b.calls.size(); ++i) {
        const Call& c = b.calls[i];
        in += c.input();
        outTokens += c.output();
        std::string s = std::to_string(i + 1) + ". " + (c.purpose.empty() ? std::string("Appel au service") : c.purpose);
        if (c.carried) s += " (d\xC3\xA9j\xC3\xA0 compt\xC3\xA9)";
        s += " \xE2\x80\x94 entr\xC3\xA9" "e " + tokensText(c.input());
        if (c.usage.cacheRead > 0 || c.usage.cacheWrite > 0) {
            s += " (dont ";
            if (c.usage.cacheRead > 0) s += tokensText(c.usage.cacheRead) + " lus du cache";
            if (c.usage.cacheRead > 0 && c.usage.cacheWrite > 0) s += ", ";
            if (c.usage.cacheWrite > 0) s += tokensText(c.usage.cacheWrite) + " mis en cache";
            s += ")";
        }
        s += kDot + std::string("sortie ") + (c.estimated() ? "env. " : "") + tokensText(c.output());
        if (c.thinking > 0) s += std::string(" (dont ") + (c.thinkingExact ? "" : "env. ") + tokensText(c.thinking) + " de r\xC3\xA9" "flexion)";
        if (c.seconds > 0) s += kDot + shortDuration(c.seconds);
        out.push_back(std::move(s));
    }
    std::string total = "Total : " + (b.estimated() ? std::string("env. ") : std::string{}) + tokensText(b.total()) + " jetons (entr\xC3\xA9" "e "
                        + tokensText(in) + ", sortie " + tokensText(outTokens) + ")";
    if (b.seconds > 0) total += kDot + clockText(b.seconds);
    out.push_back(std::move(total));
    return out;
}

// ---- le suivi ----------------------------------------------------------------------------------------------------------
void Tracker::start(Setup s, double now) {
    *this = Tracker{};
    setup_ = std::move(s);
    start_ = now;
    cur_.kind = Kind::Sending;
    cur_.start = now;
    calls_.resize(1);
    calls_[0].start = now;
}

Tracker::CallState& Tracker::call(int k, double now) {
    const std::size_t i = static_cast<std::size_t>(std::max(0, k));
    while (calls_.size() <= i) {
        CallState c;
        c.start = now;
        calls_.push_back(std::move(c));
    }
    return calls_[i];
}

void Tracker::begin(Kind k, double now) {
    cur_.end = now;
    done_.push_back(std::move(cur_));
    while (done_.size() > kHistory) done_.pop_front();
    cur_ = StepState{};
    cur_.kind = k;
    cur_.start = now;
    cur_.call = current_;
    if (k == Kind::Thinking) ++call(current_, now).thinkingSteps;
    if (k == Kind::Writing) inWord_ = false;
}

void Tracker::closeCall(CallState& c, double now) {
    if (c.end < 0) c.end = now;
    // Un service qui ne marque pas la fin de son usage (un essai, un relais) : ce qu'il a dit vaut s'il couvre ce qui est arrive.
    if (!c.c.outputKnown && c.c.usage.output > 0 && c.c.usage.output >= estimateTokens(c.text + c.thinking + c.tool)) c.c.outputKnown = true;
}

void Tracker::feedTool(std::string_view chunk) {
    if (!reader_) return;
    reader_->feed(chunk);
    cur_.items = reader_->items();
    cur_.closed = reader_->closed();
}

bool Tracker::observe(const ProviderEvent& e, double now) {
    if (!started() || finished_) return false;
    bool changed = false;
    const auto step = [&](Kind k) {
        begin(k, now);
        changed = true;
    };
    switch (e.type) {
        case ProviderEvent::Type::Status:
            if (e.text == "connexion") {
                if (cur_.kind != Kind::Sending) step(Kind::Sending);
            } else if (e.text == "reflexion") {
                if (cur_.kind != Kind::Thinking) step(Kind::Thinking);
            } else if (e.text == "ecriture") {
                if (cur_.kind != Kind::Writing && !(setup_.textTool && fenced_)) step(Kind::Writing);
            } else if (e.text == "operations") {
                if (cur_.kind != Kind::Tool) step(Kind::Tool);
            } else if (e.text == "reprise") {
                if (cur_.kind != Kind::Retry) step(Kind::Retry);
                cur_.detail = e.detail;
            } else if (e.text == "lecture") {
                if (cur_.kind == Kind::LocalTool && cur_.tool == "lire_piece_jointe") {   // la lecture exacte que l'outil vient de faire
                    cur_.detail = e.detail;
                    break;
                }
                step(Kind::Reading);
                cur_.detail = e.detail;
                reading_ = e.detail;
                readingAt_ = now;
            } else if (e.text == "outil") {
                if (cur_.kind != Kind::LocalTool) step(Kind::LocalTool);
                cur_.detail = e.detail;
            } else if (e.text == "longueur" || e.text == "longueur-modele") {
                step(Kind::Length);
                cur_.detail = e.detail;
            } else if (e.text == "sans-outil") {
                step(Kind::Sending);
                cur_.detail = e.detail;
                setup_.textTool = true;
            }
            break;
        case ProviderEvent::Type::Text: {
            CallState& c = call(current_, now);
            c.text += e.text.size();
            if (setup_.textTool) {   // la proposition en texte : le bloc ```json se lit comme l'entree d'un outil
                if (fenced_) {
                    feedTool(e.text);
                    break;
                }
                const std::string probe = tail_ + e.text;
                if (const auto at = probe.find("```"); at != std::string::npos) {
                    fenced_ = true;
                    step(Kind::Tool);
                    cur_.tool = kOpsTool;
                    reader_.emplace("operations");
                    readerTool_ = "texte";
                    feedTool(std::string_view(probe).substr(at + 3));
                    break;
                }
                tail_ = probe.size() > 2 ? probe.substr(probe.size() - 2) : probe;
            }
            if (cur_.kind != Kind::Writing && cur_.kind != Kind::Tool) step(Kind::Writing);
            if (cur_.kind == Kind::Writing)
                for (const char ch : e.text) {
                    const bool blank = ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r';
                    if (blank) inWord_ = false;
                    else if (!inWord_) {
                        ++cur_.words;
                        inWord_ = true;
                    }
                }
            break;
        }
        case ProviderEvent::Type::Usage: {
            if (e.preread) {   // une lecture a part d'une piece jointe : un appel de plus
                Call a;
                a.purpose = reading_.empty() ? std::string("Lecture \xC3\xA0 part d'une pi\xC3\xA8" "ce jointe") : "Lecture \xC3\xA0 part : " + withoutEllipsis(reading_);
                a.usage = e.usage;
                a.outputKnown = true;
                a.aside = true;
                a.seconds = readingAt_ >= 0 ? std::max(0.0, now - readingAt_) : 0.0;
                aside_.push_back(std::move(a));
                readingAt_ = now;
                changed = true;
                break;
            }
            if (e.usage.empty()) break;
            if (e.call > current_) current_ = e.call;
            CallState& c = call(e.call, now);
            c.c.usage = e.usage;
            if (e.text == "fin") c.c.outputKnown = true;
            if (e.usage.reasoning > 0) {
                c.c.thinking = e.usage.reasoning;
                c.c.thinkingExact = true;
            }
            break;
        }
        case ProviderEvent::Type::Progress:
            if (e.text == "reflexion") {
                CallState& c = call(current_, now);
                c.thinkingSeen = true;
                c.thinking += static_cast<std::size_t>(std::max(0LL, e.amount));
                if (cur_.kind != Kind::Thinking) step(Kind::Thinking);
                cur_.thinking += static_cast<std::size_t>(std::max(0LL, e.amount));
            } else if (e.text == "entree") {
                call(current_, now).tool += e.toolInputJson.size();
                const std::string key = e.toolName + "|" + e.toolUseId;
                if (cur_.kind != Kind::Tool || readerTool_ != key) {
                    if (cur_.kind != Kind::Tool || !cur_.tool.empty()) step(Kind::Tool);
                    reader_.emplace(e.toolName == kPlanTool ? "etapes" : "operations");
                    readerTool_ = key;
                }
                cur_.tool = e.toolName;
                feedTool(e.toolInputJson);
            } else if (e.text == "outil") {
                // L'appel qui l'a demande est fini : son but est cet outil.
                const ToolWords w = localWords(e.toolName, e.toolInputJson);
                CallState& c = call(current_, now);
                c.c.purpose = w.purpose;
                closeCall(c, now);
                step(Kind::LocalTool);
                cur_.tool = e.toolName;
                cur_.doing = w.specific ? w.doing : std::string{};
                cur_.done = w.done;
                if (!w.specific) cur_.detail = w.doing;
            } else if (e.text == "appel") {
                current_ = std::max(0, e.call);
                CallState& c = call(current_, now);
                c.start = now;
                c.end = -1;
                step(Kind::Sending);
                cur_.after = e.toolName;
            } else if (e.text == "attente") {
                if (cur_.kind != Kind::Retry) step(Kind::Retry);
                cur_.until = now + std::max(0.0, e.seconds);
                cur_.http = e.httpStatus;
            }
            break;
        case ProviderEvent::Type::ToolUse:
            // Un appel rendu d'un coup (un fournisseur sans morceaux) : son entree lue maintenant, pour le compte.
            if ((e.toolName == kOpsTool || e.toolName == kPlanTool) && (!reader_ || reader_->bytes() == 0)) {
                if (cur_.kind != Kind::Tool) step(Kind::Tool);
                cur_.tool = e.toolName;
                reader_.emplace(e.toolName == kPlanTool ? "etapes" : "operations");
                readerTool_ = e.toolName + "|" + e.toolUseId;
                feedTool(e.toolInputJson);
            }
            break;
        default: break;
    }
    return changed;
}

void Tracker::finish(double now, bool proposed) {
    if (!started() || finished_) return;
    begin(Kind::Sending, now);   // l'etape en cours passe dans l'historique
    finished_ = true;
    finishedAt_ = now;
    for (auto& c : calls_) closeCall(c, now);
    if (!calls_.empty() && calls_.back().c.purpose.empty())
        calls_.back().c.purpose = !setup_.purpose.empty() ? setup_.purpose : proposed ? std::string("Proposition") : std::string("R\xC3\xA9ponse");
    for (auto& c : calls_)
        if (c.c.purpose.empty()) c.c.purpose = "R\xC3\xA9ponse";
}

void Tracker::carry(const Bill& cut) {
    for (auto c : cut.calls) {
        if (!c.carried) c.purpose = "R\xC3\xA9ponse coup\xC3\xA9" "e : " + (c.purpose.empty() ? std::string("appel") : c.purpose);
        c.carried = true;
        carried_.push_back(std::move(c));
    }
}

Call Tracker::callOf(const CallState& cs, double now) const {
    Call c = cs.c;
    c.estimate = estimateTokens(cs.text + cs.thinking + cs.tool);
    c.seconds = std::max(0.0, (cs.end >= 0 ? cs.end : now) - cs.start);
    if (!c.thinkingExact && cs.thinkingSeen) {
        long long t = estimateTokens(cs.thinking);
        // La reflexion que le service ne montre pas (son texte omis) : la sortie dite moins ce qui est arrive en clair.
        if (c.outputKnown) t = std::max(t, c.usage.output - estimateTokens(cs.text + cs.tool));
        c.thinking = std::clamp(t, 0LL, std::max(0LL, c.output()));
    }
    return c;
}

Bill Tracker::bill(double now) const {
    Bill b;
    if (!started()) return b;
    for (const auto& c : carried_) b.calls.push_back(c);
    for (const auto& c : aside_) b.calls.push_back(c);
    for (const auto& c : calls_) b.calls.push_back(callOf(c, now));
    b.seconds = std::max(0.0, (finished_ ? finishedAt_ : now) - start_);
    return b;
}

ProviderUsage Tracker::realUsage() const {
    ProviderUsage u;
    for (const auto& c : calls_) {
        u.input += c.c.usage.input;
        u.output += c.c.usage.output;
        u.cacheRead += c.c.usage.cacheRead;
        u.cacheWrite += c.c.usage.cacheWrite;
        u.reasoning += c.c.usage.reasoning;
    }
    return u;
}

long long Tracker::liveTokens() const {
    long long n = 0;
    for (const auto& c : calls_) n += callOf(c, start_).total();
    for (const auto& c : aside_) n += c.total();
    return n;
}

bool Tracker::estimating() const {
    return std::any_of(calls_.begin(), calls_.end(), [this](const CallState& c) { return callOf(c, start_).estimated(); });
}

long long Tracker::contextNow() const {
    for (auto it = calls_.rbegin(); it != calls_.rend(); ++it)
        if (it->c.input() > 0) return it->c.input();
    return 0;
}

std::string Tracker::present(const StepState& s, double now) const {
    const double el = std::max(0.0, now - s.start);
    const std::string time = el >= 1.0 ? " (" + clockText(el) + ")" : std::string{};
    switch (s.kind) {
        case Kind::Sending: {
            if (!s.detail.empty()) return withEllipsis(s.detail);   // sans outil : redemandee en texte
            if (!s.after.empty()) return "Envoi de la suite \xC3\xA0 l'IA (le r\xC3\xA9sultat de " + s.after + ")" + kEllipsis + time;
            std::string t = "Envoi de la demande";
            if (s.call == 0 && setup_.contextEstimate > 0) t += " (env. " + tokensText(setup_.contextEstimate) + " jetons de contexte)";
            return t + kEllipsis + time;
        }
        case Kind::Retry: {
            const std::string cause = s.http == 429   ? std::string("limite")
                                      : s.http == 529 ? std::string("service surcharg\xC3\xA9")
                                      : s.http >= 500 ? "erreur " + std::to_string(s.http)
                                                      : std::string{};
            if (s.until >= 0) {
                const double left = s.until - now;
                if (left > 0)
                    return "Attente du service (" + (cause.empty() ? std::string{} : cause + " : ") + "nouvelle tentative dans " + waitText(left) + ")" + kEllipsis;
                return std::string("Attente du service : nouvelle tentative") + kEllipsis;
            }
            return "Attente du service" + (cause.empty() ? std::string{} : " (" + cause + ")") + kEllipsis;
        }
        case Kind::Length:
            return "Le service accepte au plus " + grouped(std::atoll(s.detail.c_str())) + " jetons par r\xC3\xA9ponse : demande renvoy\xC3\xA9" "e" + kEllipsis;
        case Kind::Reading: return withEllipsis(s.detail.empty() ? std::string("Lecture d'une pi\xC3\xA8" "ce jointe") : s.detail) + time;
        case Kind::Thinking: {
            std::string t = std::string("L'IA r\xC3\xA9" "fl\xC3\xA9" "chit") + kEllipsis;
            if (s.thinking > 0) t += " env. " + grouped(estimateTokens(s.thinking)) + " jetons de r\xC3\xA9" "flexion";
            return t + " (" + clockText(el) + ")";
        }
        case Kind::Writing:
            return std::string("L'IA \xC3\xA9" "crit sa r\xC3\xA9ponse") + kEllipsis
                   + (s.words > 0 ? " " + plural(static_cast<long long>(s.words), "mot", "mots") : std::string{});
        case Kind::Tool: {
            const bool plan = s.tool == kPlanTool;
            if (!plan && !s.tool.empty() && s.tool != kOpsTool) return "L'IA pr\xC3\xA9pare l'appel de l'outil " + s.tool + kEllipsis;
            const std::string what = plan ? "le plan de la mission" : "la proposition";
            if (s.closed)
                return "L'IA termine " + what + " : " + (plan ? plural(s.items, "\xC3\xA9tape", "\xC3\xA9tapes") : plural(s.items, "op\xC3\xA9ration", "op\xC3\xA9rations")) + kEllipsis;
            if (s.items <= 0) return "L'IA \xC3\xA9" "crit " + what + kEllipsis + time;
            std::string t = "L'IA \xC3\xA9" "crit " + what + " : " + (plan ? "\xC3\xA9tape " : "op\xC3\xA9ration ") + std::to_string(s.items);
            if (reader_) {
                const std::string d = plan ? stepText(reader_->last()) : operationText(reader_->last());
                if (!d.empty()) t += " (" + d + ")";
            }
            return t + kEllipsis;
        }
        case Kind::LocalTool:
            if (!s.doing.empty()) return s.doing + time;
            return withEllipsis(s.detail.empty() ? "L'IA utilise l'outil " + s.tool : s.detail) + time;
    }
    return {};
}

std::string Tracker::past(const StepState& s) const {
    const CallState* c = s.call >= 0 && static_cast<std::size_t>(s.call) < calls_.size() ? &calls_[static_cast<std::size_t>(s.call)] : nullptr;
    switch (s.kind) {
        case Kind::Sending: {
            if (!s.detail.empty()) return withoutEllipsis(s.detail);
            std::string t = s.after.empty() ? std::string("Demande envoy\xC3\xA9" "e") : "Suite envoy\xC3\xA9" "e (r\xC3\xA9sultat de " + s.after + ")";
            if (c && c->c.input() > 0) {
                t += " : " + tokensText(c->c.input()) + " jetons de contexte";
                const ProviderUsage& u = c->c.usage;
                if (u.cacheRead > 0 || u.cacheWrite > 0) {
                    t += " (dont ";
                    if (u.cacheRead > 0) t += tokensText(u.cacheRead) + " en cache";
                    if (u.cacheRead > 0 && u.cacheWrite > 0) t += ", ";
                    if (u.cacheWrite > 0) t += tokensText(u.cacheWrite) + " mis en cache";
                    t += ")";
                }
            } else if (s.call == 0 && s.after.empty() && setup_.contextEstimate > 0) {
                t += " (env. " + tokensText(setup_.contextEstimate) + " jetons de contexte)";
            }
            return t;
        }
        case Kind::Retry: return "Attente du service" + (s.http > 0 ? " (" + std::to_string(s.http) + ")" : std::string{});
        case Kind::Length: return "Longueur de r\xC3\xA9ponse ramen\xC3\xA9" "e \xC3\xA0 " + grouped(std::atoll(s.detail.c_str())) + " jetons";
        case Kind::Reading: return withoutEllipsis(s.detail.empty() ? std::string("Lecture d'une pi\xC3\xA8" "ce jointe") : s.detail);
        case Kind::Thinking: {
            long long t = estimateTokens(s.thinking);
            bool exact = false;
            if (c && c->thinkingSteps == 1) {   // la seule reflexion de son appel : ce que l'appel en dit a sa fin
                const Call cc = callOf(*c, s.end);
                if (cc.outputKnown || cc.thinkingExact) {
                    t = std::max(t, cc.thinking);
                    exact = cc.thinkingExact;
                }
            }
            return std::string("L'IA a r\xC3\xA9" "fl\xC3\xA9" "chi") + (t > 0 ? std::string(" : ") + (exact ? "" : "env. ") + grouped(t) + " jetons" : std::string{});
        }
        case Kind::Writing:
            return std::string("L'IA a \xC3\xA9" "crit sa r\xC3\xA9ponse") + (s.words > 0 ? " : " + plural(static_cast<long long>(s.words), "mot", "mots") : std::string{});
        case Kind::Tool: {
            if (s.tool == kPlanTool) return std::string("L'IA a \xC3\xA9" "crit le plan") + (s.items > 0 ? " : " + plural(s.items, "\xC3\xA9tape", "\xC3\xA9tapes") : std::string{});
            if (!s.tool.empty() && s.tool != kOpsTool) return "L'IA a appel\xC3\xA9 l'outil " + s.tool;
            return std::string("L'IA a \xC3\xA9" "crit la proposition") + (s.items > 0 ? " : " + plural(s.items, "op\xC3\xA9ration", "op\xC3\xA9rations") : std::string{});
        }
        case Kind::LocalTool: return !s.done.empty() ? s.done : withoutEllipsis(s.detail);
    }
    return {};
}

std::vector<Step> Tracker::history() const {
    std::vector<Step> out;
    for (auto it = done_.rbegin(); it != done_.rend(); ++it) {
        std::string t = past(*it);
        if (t.empty()) continue;
        out.push_back({std::move(t), std::max(0.0, it->end - it->start)});
    }
    return out;
}

Snapshot Tracker::snapshot(double now) const {
    Snapshot s;
    if (!started()) return s;
    s.context = setup_.context;
    if (!finished_) {
        s.line = present(cur_, now);
        // ce que l'indicateur dit en plus (Assistant::statusDetail) : la ligne le dit deja pour une lecture, une longueur refusee,
        // un outil, une demande renvoyee sans outil ; il reste pour la cause d'une nouvelle tentative, une reprise en parties.
        s.keepDetail = !(cur_.kind == Kind::Reading || cur_.kind == Kind::Length || cur_.kind == Kind::LocalTool
                         || (cur_.kind == Kind::Sending && !cur_.detail.empty()));
    }
    s.history = history();
    return s;
}

} // namespace app::ai::activity
