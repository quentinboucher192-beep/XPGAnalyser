// =============================================================================
//  app/ai/AiMission.cpp - 1.22.0 : les missions IA persistantes (le modele et ses regles)
// =============================================================================
#include "AiMission.hpp"

#include "../../hmi/HmiAudit.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiRelations.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace app::ai::mission {

namespace {

using json::Value;

// ---- petits outils ---------------------------------------------------------------------------------------------
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Minuscules, sans accents (les lettres accentuees courantes du francais) : pour chercher et comparer.
std::string folded(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            char r = 0;
            if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) r = 'a';
            else if (d == 0x87 || d == 0xA7) r = 'c';
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) r = 'e';
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) r = 'i';
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) r = 'o';
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) r = 'u';
            if (r) {
                out += r;
                ++i;
                continue;
            }
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : std::string(sep)) + x;
    return s;
}

std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

Value strings(const std::vector<std::string>& v) {
    Value a = Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}
std::vector<std::string> stringsOf(const Value& a) {
    std::vector<std::string> out;
    for (const auto& x : a.items())
        if (x.isString() && !x.asString().empty()) out.push_back(x.asString());
    return out;
}

int weightOf(const Step& s) { return std::max(1, s.plannedOps); }

// "genre|nom|vue" ou "genre:nom" -> (genre, nom, vue)
struct RefParts {
    std::string kind, name, view;
};
RefParts partsOf(std::string_view ref) {
    RefParts p;
    const auto bar = ref.find('|');
    if (bar != std::string_view::npos) {
        p.kind = std::string(ref.substr(0, bar));
        const auto rest = ref.substr(bar + 1);
        const auto bar2 = rest.find('|');
        p.name = std::string(rest.substr(0, bar2));
        if (bar2 != std::string_view::npos) p.view = std::string(rest.substr(bar2 + 1));
        return p;
    }
    const auto colon = ref.find(':');
    if (colon == std::string_view::npos) {
        p.name = trimmed(ref);
        return p;
    }
    p.kind = folded(trimmed(ref.substr(0, colon)));
    std::string rest = trimmed(ref.substr(colon + 1));
    // "vue:Vue_Station - deja conforme" : le nom s'arrete au premier blanc
    const auto sp = rest.find_first_of(" \t");
    p.name = sp == std::string::npos ? rest : rest.substr(0, sp);
    return p;
}

std::string refText(std::string_view ref) {
    const RefParts p = partsOf(ref);
    std::string s = p.kind.empty() ? p.name : p.kind + " " + p.name;
    if (!p.view.empty()) s += " (" + p.view + ")";
    return s;
}

// Retrouver un element du projet par son genre et son nom (le graphe des relations, construit une fois).
class Finder {
public:
    explicit Finder(const hmi::Project& p) : project_(p) {}
    bool exists(std::string_view ref) {
        const RefParts p = partsOf(ref);
        if (p.name.empty()) return false;
        // 1.22.2 : d'abord une recherche DIRECTE par le nom. Le graphe des relations relit toutes les formules et tous les
        // scripts du projet : des centaines de ms sur un gros projet, sur le fil de l'interface, a CHAQUE geste quand une
        // mission suit le projet (verifyAgainst) - une saccade. Le graphe ne sert plus que si la recherche directe ne trouve
        // pas (un nom qualifie, un genre rare) : jamais un faux << absent >>.
        if (direct(p)) return true;
        const auto& g = graph();
        using K = hmi::rel::Kind;
        const std::string k = folded(p.kind);
        const auto has = [&](K kind, const std::string& name) { return g.find(kind, name).has_value(); };
        if (k == "variable") return has(K::Variable, p.name);
        if (k == "type") return has(K::Type, p.name);
        if (k == "equipement") return has(K::Equipment, p.name);
        if (k == "vue" || k == "symbole" || k == "popup" || k == "modele" || k == "entete" || k == "pied" || k == "ecran modele")
            return has(K::View, p.name);
        if (k == "script") return has(K::Script, p.name);
        if (k == "fonction") return has(K::Function, p.name);
        if (k == "alarme") return has(K::Alarm, p.name);
        if (k == "ressource") return has(K::Resource, p.name);
        if (k == "dossier") return has(K::Folder, p.name);
        if (k == "recette") return has(K::Recipe, p.name);
        if (k == "objet" || k == "instance") return p.view.empty() ? has(K::Object, p.name) : has(K::Object, p.view + "." + p.name);
        // un genre inconnu : par le nom seul
        return !g.findAll(p.name).empty();
    }

private:
    bool direct(const RefParts& p) const {
        const std::string k = folded(p.kind);
        const std::string want = folded(p.name);
        const auto named = [&](const auto& list) {
            return std::any_of(list.begin(), list.end(), [&](const auto& x) { return folded(x.name) == want; });
        };
        if (k == "variable") return project_.variable(p.name) != nullptr;
        if (k == "type") return project_.hmiTypeByName(p.name) != nullptr;
        if (k == "equipement") return project_.equipmentByName(p.name) != nullptr;
        if (k == "vue" || k == "symbole" || k == "popup" || k == "modele" || k == "entete" || k == "pied" || k == "ecran modele")
            return named(project_.views);
        if (k == "script") return named(project_.programs.scripts);
        if (k == "fonction") return named(project_.programs.functions);
        if (k == "objet" || k == "instance") {
            const auto inView = [&](const hmi::View& v) { return named(v.objects); };
            if (!p.view.empty()) {
                const std::string vk = folded(p.view);
                return std::any_of(project_.views.begin(), project_.views.end(), [&](const hmi::View& v) { return folded(v.name) == vk && inView(v); });
            }
            return std::any_of(project_.views.begin(), project_.views.end(), inView);
        }
        return false;
    }
    const hmi::rel::Graph& graph() {
        if (!graph_) graph_ = std::make_unique<hmi::rel::Graph>(hmi::rel::build(project_));
        return *graph_;
    }
    const hmi::Project&               project_;
    std::unique_ptr<hmi::rel::Graph>  graph_;
};

const std::vector<std::pair<std::string_view, std::string_view>>& categoryTable() {
    static const std::vector<std::pair<std::string_view, std::string_view>> t = {
        {"analyse", "Analyse"},        {"equipements", "\xC3\x89quipements"}, {"types", "Types IHM"},
        {"variables", "Variables"},    {"vues", "Vues"},               {"objets", "Objets des vues"},
        {"symboles", "Symboles"},      {"popups", "Popups"},           {"alarmes", "Alarmes"},
        {"scripts", "Scripts"},        {"fonctions", "Fonctions"},     {"ressources", "Ressources"},
        {"verification", "V\xC3\xA9rification"}, {"audit", "Audit"},         {"rapport", "Rapport"},
        {"autre", "Autre"},
    };
    return t;
}

std::string normalizeCategory(std::string_view raw) {
    std::string k = folded(trimmed(raw));
    for (auto& c : k)
        if (c == ' ' || c == '-') c = '_';
    static const std::vector<std::pair<std::string_view, std::string_view>> alias = {
        {"equipement", "equipements"}, {"materiel", "equipements"}, {"communication", "equipements"}, {"modbus", "equipements"},
        {"type", "types"},             {"structures", "types"},     {"structure", "types"},           {"variable", "variables"},
        {"adresses", "variables"},     {"vue", "vues"},             {"ecrans", "vues"},               {"ecran", "vues"},
        {"ihm", "vues"},               {"objet", "objets"},         {"symbole", "symboles"},          {"popup", "popups"},
        {"alarme", "alarmes"},         {"surveillance", "alarmes"}, {"script", "scripts"},            {"fonction", "fonctions"},
        {"ressource", "ressources"},   {"controle", "verification"}, {"compilation", "verification"}, {"tests", "verification"},
        {"verifications", "verification"}, {"bilan", "rapport"},    {"analyser", "analyse"},
    };
    for (const auto& [a, b] : alias)
        if (k == a) return std::string(b);
    for (const auto& [key, label] : categoryTable())
        if (k == key) return std::string(key);
    return "autre";
}

// Les categories dont une etape depend implicitement (si le plan en a).
std::vector<std::string_view> impliedFrom(std::string_view cat) {
    static const std::map<std::string_view, std::vector<std::string_view>> rules = {
        {"equipements", {"analyse"}},
        {"types", {"analyse"}},
        {"ressources", {"analyse"}},
        {"variables", {"analyse", "types", "equipements"}},
        {"symboles", {"analyse", "types", "variables"}},
        {"popups", {"analyse", "variables", "symboles", "types"}},
        {"vues", {"analyse", "variables", "symboles", "popups", "types", "ressources"}},
        {"objets", {"analyse", "vues", "variables", "symboles"}},
        {"alarmes", {"analyse", "variables"}},
        {"fonctions", {"analyse", "variables", "types"}},
        {"scripts", {"analyse", "variables", "fonctions"}},
        {"autre", {"analyse"}},
        {"verification", {"analyse", "equipements", "types", "variables", "symboles", "popups", "vues", "objets", "alarmes", "fonctions",
                          "scripts", "ressources", "autre"}},
        {"audit", {"analyse", "equipements", "types", "variables", "symboles", "popups", "vues", "objets", "alarmes", "fonctions",
                   "scripts", "ressources", "autre", "verification"}},
        {"rapport", {"analyse", "equipements", "types", "variables", "symboles", "popups", "vues", "objets", "alarmes", "fonctions",
                     "scripts", "ressources", "autre", "verification", "audit"}},
    };
    const auto it = rules.find(cat);
    return it == rules.end() ? std::vector<std::string_view>{} : it->second;
}

// a depend-il (transitivement) de b ?
bool dependsOn(const std::vector<Step>& steps, std::string_view a, std::string_view b) {
    std::set<std::string> seen;
    std::vector<std::string> todo{std::string(a)};
    while (!todo.empty()) {
        const std::string cur = todo.back();
        todo.pop_back();
        if (!seen.insert(cur).second) continue;
        for (const auto& s : steps)
            if (s.id == cur)
                for (const auto& d : s.deps) {
                    if (d == b) return true;
                    todo.push_back(d);
                }
    }
    return false;
}

std::string nextStepId(const std::vector<Step>& steps) {
    int n = 0;
    for (const auto& s : steps)
        if (s.id.size() > 1 && s.id[0] == 'S') n = std::max(n, std::atoi(s.id.c_str() + 1));
    return "S" + std::to_string(n + 1);
}

std::string percentText(double x) { return std::to_string(static_cast<int>(std::lround(x * 100.0))) + " %"; }

const ProposalLink* lastApplied(const Mission& m) {
    const ProposalLink* last = nullptr;
    for (const auto& p : m.proposals)
        if (p.state == "appliquee") last = &p;
    return last;
}

// Les elements que les propositions appliquees ont crees ("genre|nom|vue"), sans doublon, dans l'ordre.
std::vector<std::string> createdRefs(const Mission& m) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& p : m.proposals) {
        if (p.state != "appliquee") continue;
        for (const auto& l : p.lines)
            if (l.state == "appliquee")
                for (const auto& r : l.creates)
                    if (seen.insert(r).second) out.push_back(r);
    }
    return out;
}

// 1.22.3 : l'etape a-t-elle ete ignoree par la fin forcee ?
bool forcedOut(const Mission& m, std::string_view id) {
    return m.forced.on() && std::find(m.forced.steps.begin(), m.forced.steps.end(), id) != m.forced.steps.end();
}

// "3 \xC3\xA9tapes ignor\xC3\xA9" "es" / "1 \xC3\xA9tape ignor\xC3\xA9" "e"
std::string ignoredCount(std::size_t n) { return std::to_string(n) + (n > 1 ? " \xC3\xA9tapes ignor\xC3\xA9" "es" : " \xC3\xA9tape ignor\xC3\xA9" "e"); }

} // namespace

// ---- les etats ----------------------------------------------------------------------------------------------------
std::string_view stepLabel(StepState s) noexcept {
    switch (s) {
        case StepState::ToPlan: return "\xC3\x80 planifier";
        case StepState::Waiting: return "En attente";
        case StepState::Ready: return "Pr\xC3\xAAte";
        case StepState::Running: return "En cours";
        case StepState::AwaitingValidation: return "En attente de validation";
        case StepState::Applied: return "Appliqu\xC3\xA9" "e";
        case StepState::Verifying: return "V\xC3\xA9rification en cours";
        case StepState::Done: return "Termin\xC3\xA9" "e";
        case StepState::Error: return "En erreur";
        case StepState::Blocked: return "Bloqu\xC3\xA9" "e";
        case StepState::Skipped: return "Ignor\xC3\xA9" "e";
        case StepState::Cancelled: return "Annul\xC3\xA9" "e";
    }
    return "?";
}

std::string_view stepKey(StepState s) noexcept {
    switch (s) {
        case StepState::ToPlan: return "a_planifier";
        case StepState::Waiting: return "en_attente";
        case StepState::Ready: return "prete";
        case StepState::Running: return "en_cours";
        case StepState::AwaitingValidation: return "a_valider";
        case StepState::Applied: return "appliquee";
        case StepState::Verifying: return "verification";
        case StepState::Done: return "terminee";
        case StepState::Error: return "erreur";
        case StepState::Blocked: return "bloquee";
        case StepState::Skipped: return "ignoree";
        case StepState::Cancelled: return "annulee";
    }
    return "en_attente";
}

StepState stepFromKey(std::string_view raw) noexcept {
    const std::string k = folded(trimmed(raw));
    static const StepState all[] = {StepState::ToPlan,  StepState::Waiting, StepState::Ready,   StepState::Running,
                                    StepState::AwaitingValidation, StepState::Applied, StepState::Verifying, StepState::Done,
                                    StepState::Error,   StepState::Blocked, StepState::Skipped, StepState::Cancelled};
    for (const auto s : all)
        if (k == stepKey(s) || k == folded(stepLabel(s))) return s;
    return StepState::Waiting;
}

bool canMove(StepState from, StepState to) noexcept {
    using S = StepState;
    if (from == to) return true;
    switch (from) {
        case S::ToPlan: return to == S::Waiting || to == S::Ready || to == S::Skipped || to == S::Cancelled || to == S::Blocked;
        case S::Waiting: return to == S::Ready || to == S::Blocked || to == S::Skipped || to == S::Cancelled;
        case S::Ready:
            return to == S::Running || to == S::AwaitingValidation || to == S::Waiting || to == S::Blocked || to == S::Skipped
                   || to == S::Cancelled;
        case S::Running: return to == S::AwaitingValidation || to == S::Ready || to == S::Error || to == S::Cancelled;
        case S::AwaitingValidation:
            return to == S::Applied || to == S::Ready || to == S::Error || to == S::Running || to == S::Cancelled;
        case S::Applied: return to == S::Verifying || to == S::Done || to == S::Error || to == S::Ready;
        case S::Verifying: return to == S::Done || to == S::Error || to == S::Ready;
        case S::Done: return to == S::Ready;
        case S::Error: return to == S::Running || to == S::AwaitingValidation || to == S::Ready || to == S::Skipped || to == S::Cancelled;
        case S::Blocked: return to == S::Waiting || to == S::Ready || to == S::Skipped || to == S::Cancelled;
        case S::Skipped: return to == S::Waiting || to == S::Ready || to == S::Blocked;
        case S::Cancelled: return false;
    }
    return false;
}

bool isFinished(StepState s) noexcept { return s == StepState::Done || s == StepState::Skipped; }
bool isBlocking(StepState s) noexcept { return s == StepState::Error || s == StepState::Blocked || s == StepState::Cancelled; }

std::string_view stateLabel(State s) noexcept {
    switch (s) {
        case State::Planning: return "Planification";
        case State::Active: return "En cours";
        case State::Paused: return "En pause";
        case State::Blocked: return "Bloqu\xC3\xA9" "e";
        case State::Done: return "Termin\xC3\xA9" "e";
        case State::Cancelled: return "Annul\xC3\xA9" "e";
    }
    return "?";
}
std::string_view stateKey(State s) noexcept {
    switch (s) {
        case State::Planning: return "planification";
        case State::Active: return "en_cours";
        case State::Paused: return "en_pause";
        case State::Blocked: return "bloquee";
        case State::Done: return "terminee";
        case State::Cancelled: return "annulee";
    }
    return "en_cours";
}
State stateFromKey(std::string_view raw) noexcept {
    const std::string k = folded(trimmed(raw));
    static const State all[] = {State::Planning, State::Active, State::Paused, State::Blocked, State::Done, State::Cancelled};
    for (const auto s : all)
        if (k == stateKey(s) || k == folded(stateLabel(s))) return s;
    return State::Active;
}

std::string_view coherenceLabel(Coherence c) noexcept {
    switch (c) {
        case Coherence::Unknown: return "Non v\xC3\xA9rifi\xC3\xA9" "e";
        case Coherence::Same: return "Projet inchang\xC3\xA9";
        case Coherence::Changed: return "Projet modifi\xC3\xA9";
        case Coherence::Missing: return "Projet introuvable";
    }
    return "?";
}
std::string_view coherenceKey(Coherence c) noexcept {
    switch (c) {
        case Coherence::Unknown: return "inconnue";
        case Coherence::Same: return "identique";
        case Coherence::Changed: return "modifie";
        case Coherence::Missing: return "introuvable";
    }
    return "inconnue";
}
Coherence coherenceFromKey(std::string_view raw) noexcept {
    const std::string k = folded(trimmed(raw));
    if (k == "identique") return Coherence::Same;
    if (k == "modifie") return Coherence::Changed;
    if (k == "introuvable") return Coherence::Missing;
    return Coherence::Unknown;
}

const std::vector<std::string_view>& categories() {
    static const std::vector<std::string_view> v = [] {
        std::vector<std::string_view> out;
        for (const auto& [k, l] : categoryTable()) out.push_back(k);
        return out;
    }();
    return v;
}
std::string_view categoryLabel(std::string_view key) {
    for (const auto& [k, l] : categoryTable())
        if (k == key) return l;
    return "Autre";
}

// ---- Mission ------------------------------------------------------------------------------------------------------
const Step* Mission::step(std::string_view key) const noexcept {
    for (const auto& s : steps)
        if (s.id == key) return &s;
    return nullptr;
}
Step* Mission::step(std::string_view key) noexcept {
    for (auto& s : steps)
        if (s.id == key) return &s;
    return nullptr;
}
const ProposalLink* Mission::proposal(std::string_view key) const noexcept {
    for (const auto& p : proposals)
        if (p.id == key) return &p;
    return nullptr;
}
ProposalLink* Mission::proposal(std::string_view key) noexcept {
    for (auto& p : proposals)
        if (p.id == key) return &p;
    return nullptr;
}

// ---- creer, journaliser ---------------------------------------------------------------------------------------------
Mission make(std::string request, std::string conversation, std::string now) {
    static std::atomic<unsigned> counter{0};
    Mission m;
    std::string digits;
    for (const char c : now)
        if (c >= '0' && c <= '9') digits += c;
    const unsigned n = counter.fetch_add(1) + 1;
    const auto seed = std::hash<std::string>{}(request + "/" + now + "/" + std::to_string(n));
    char tail[16];
    std::snprintf(tail, sizeof tail, "%04x", static_cast<unsigned>(seed & 0xFFFFu));
    m.id = "mission-" + (digits.empty() ? std::string("0") : digits) + "-" + tail;
    m.request = std::move(request);
    // Un nom provisoire (le plan le remplace) : les premiers mots de la demande.
    {
        std::istringstream in(m.request);
        std::string word, name;
        int words = 0;
        while (in >> word && words < 7) {
            if (name.size() + word.size() + 1 > 48) break;
            name += (name.empty() ? "" : " ") + word;
            ++words;
        }
        m.name = name.empty() ? std::string("Mission") : name;
    }
    m.conversation = conversation;
    if (!conversation.empty()) m.conversations.push_back(conversation);
    m.created = now;
    m.modified = now;
    m.state = State::Planning;
    log(m, "creation", "Mission cr\xC3\xA9\xC3\xA9" "e : " + guill(m.request), now);
    return m;
}

void log(Mission& m, std::string kind, std::string text, std::string now, std::string step, std::string proposal) {
    Event e;
    e.date = now;
    e.kind = std::move(kind);
    e.text = std::move(text);
    e.step = std::move(step);
    e.proposal = std::move(proposal);
    m.history.push_back(std::move(e));
    if (m.history.size() > kMaxHistory) m.history.erase(m.history.begin() + 1, m.history.begin() + static_cast<std::ptrdiff_t>(m.history.size() - kMaxHistory + 1));
    m.modified = now;
}

bool looksComplex(std::string_view request) {
    const std::string t = folded(request);
    if (trimmed(t).empty()) return false;
    // Une question (ou en est..., pourquoi...) n'est jamais une mission.
    static const char* const questions[] = {"ou en est", "pourquoi", "qu'est-ce", "quel ", "quelle ", "quels ", "quelles ", "combien", "est-ce que",
                                            "montre-moi", "reprends la mission", "reprendre la mission"};
    const std::string head = t.substr(0, std::min<std::size_t>(t.size(), 40));
    for (const char* q : questions)
        if (head.find(q) == 0) return false;
    if (!t.empty() && trimmed(t).back() == '?') return false;
    static const std::vector<std::vector<const char*>> families = {
        {"equipement", "modbus", "automate", "esclave"},
        {"variable", "adresse", "structure"},
        {"alarme", "surveillance", "defaut"},
        {"script", "fonction", "logique"},
        {"vue", "ecran", "page", "synoptique", "accueil"},
        {"symbole", "popup"},
        {"type ihm", "types ihm", "type "},
        {"recette", "ressource", "image"},
    };
    int count = 0;
    for (const auto& fam : families)
        for (const char* w : fam)
            if (contains(t, w)) {
                ++count;
                break;
            }
    static const char* const wide[] = {"complet", "complete", "entier", "entiere", "installation", "station", "de bout en bout",
                                       "toute la supervision", "projet entier", "toutes les", "tous les"};
    bool broad = false;
    for (const char* w : wide) broad = broad || contains(t, w);
    std::size_t words = 0;
    {
        std::istringstream in(t);
        std::string w;
        while (in >> w) ++words;
    }
    if (count >= 3) return true;
    if (broad && count >= 2) return true;
    return words >= 40 && count >= 2;
}

// ---- le plan ---------------------------------------------------------------------------------------------------------
std::vector<std::string> implicitDeps(const Step& s, const std::vector<Step>& steps) {
    std::vector<std::string> out;
    const auto cats = impliedFrom(s.category);
    for (const auto& o : steps) {
        if (o.id == s.id) continue;
        if (std::find(cats.begin(), cats.end(), std::string_view(o.category)) == cats.end()) continue;
        if (std::find(s.deps.begin(), s.deps.end(), o.id) != s.deps.end()) continue;
        out.push_back(o.id);
    }
    return out;
}

bool orderSteps(std::vector<Step>& steps, std::string* cycle) {
    const std::size_t n = steps.size();
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) index[steps[i].id] = i;
    std::vector<int> indeg(n, 0);
    std::vector<std::vector<std::size_t>> users(n);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& d : steps[i].deps)
            if (const auto it = index.find(d); it != index.end()) {
                ++indeg[i];
                users[it->second].push_back(i);
            }
    std::vector<std::size_t> order;
    std::vector<bool> done(n, false);
    for (std::size_t round = 0; round < n; ++round) {
        std::size_t pick = n;
        for (std::size_t i = 0; i < n; ++i)
            if (!done[i] && indeg[i] == 0) {
                pick = i;
                break;   // a egalite : l'ordre ecrit
            }
        if (pick == n) break;
        done[pick] = true;
        order.push_back(pick);
        for (const auto u : users[pick]) --indeg[u];
    }
    if (order.size() != n) {
        if (cycle) {
            // un chemin du cycle : partir d'une etape restante et suivre une dependance restante
            std::size_t cur = n;
            for (std::size_t i = 0; i < n; ++i)
                if (!done[i]) {
                    cur = i;
                    break;
                }
            std::vector<std::string> path;
            std::set<std::size_t> seen;
            while (cur < n && seen.insert(cur).second) {
                path.push_back(steps[cur].id);
                std::size_t next = n;
                for (const auto& d : steps[cur].deps)
                    if (const auto it = index.find(d); it != index.end() && !done[it->second]) {
                        next = it->second;
                        break;
                    }
                cur = next;
            }
            if (cur < n) path.push_back(steps[cur].id);
            *cycle = join(path, " -> ");
        }
        return false;
    }
    std::vector<Step> sorted;
    sorted.reserve(n);
    for (const auto i : order) sorted.push_back(std::move(steps[i]));
    steps = std::move(sorted);
    return true;
}

void refreshReadiness(Mission& m) {
    if (m.state == State::Cancelled) return;
    for (auto& s : m.steps) {
        if (s.state != StepState::Waiting && s.state != StepState::Ready && s.state != StepState::Blocked && s.state != StepState::ToPlan)
            continue;
        std::vector<std::string> blocking, waiting;
        for (const auto& d : s.deps) {
            const Step* dep = m.step(d);
            if (!dep) continue;
            if (isBlocking(dep->state)) blocking.push_back(d + " (" + std::string(stepLabel(dep->state)) + ")");
            else if (!isFinished(dep->state)) waiting.push_back(d);
        }
        if (!blocking.empty()) {
            s.state = StepState::Blocked;
            s.note = "D\xC3\xA9pend de " + join(blocking, ", ") + ".";
        } else if (!waiting.empty()) {
            s.state = StepState::Waiting;
            s.note = "Attend " + join(waiting, ", ") + ".";
        } else {
            s.state = StepState::Ready;
            s.note.clear();
        }
    }
}

std::string planToolJson() {
    Value tool = Value::object();
    tool.set("name", std::string(kPlanTool));
    tool.set("description",
             "Enregistre le PLAN d'une mission IA : l'analyse du projet r\xC3\xA9" "el (ce qui existe et est conforme, \xC3\xA0 modifier, \xC3\xA0 "
             "compl\xC3\xA9ter, \xC3\xA0 cr\xC3\xA9" "er, impossible) et les \xC3\xA9tapes n\xC3\xA9" "cessaires, chacune avec sa cat\xC3\xA9gorie, ses d\xC3\xA9pendances (les "
             "identifiants des \xC3\xA9tapes dont elle a besoin), une estimation du nombre d'op\xC3\xA9rations et ce qu'elle produira. "
             "L'ordre d'ex\xC3\xA9" "cution est calcul\xC3\xA9 par le logiciel \xC3\xA0 partir des d\xC3\xA9pendances. N'appelle PAS proposer_operations "
             "dans la m\xC3\xAAme r\xC3\xA9ponse.");
    Value str = Value::object();
    str.set("type", "string");
    Value list = Value::object();
    list.set("type", "array");
    list.set("items", str);

    Value step = Value::object();
    step.set("type", "object");
    Value sp = Value::object();
    {
        Value v = str;
        v.set("description", "Identifiant court et stable (S1, S2...). Absent : donn\xC3\xA9 dans l'ordre.");
        sp.set("id", v);
    }
    {
        Value v = str;
        v.set("description", "Le titre de l'\xC3\xA9tape (Cr\xC3\xA9" "er les \xC3\xA9quipements Modbus).");
        sp.set("titre", v);
    }
    {
        Value v = str;
        v.set("description", "Ce qu'elle fait, en une ou deux phrases ; ce qui existe d\xC3\xA9j\xC3\xA0 et sera r\xC3\xA9utilis\xC3\xA9.");
        sp.set("detail", v);
    }
    {
        Value v = str;
        std::string cats;
        for (const auto c : categories()) cats += (cats.empty() ? "" : ", ") + std::string(c);
        v.set("description", "Une cat\xC3\xA9gorie parmi : " + cats + ".");
        sp.set("categorie", v);
    }
    {
        Value v = list;
        v.set("description", "Les identifiants des \xC3\xA9tapes dont elle d\xC3\xA9pend.");
        sp.set("depend_de", v);
    }
    {
        Value v = Value::object();
        v.set("type", "integer");
        v.set("description", "Estimation du nombre d'op\xC3\xA9rations (0 pour une \xC3\xA9tape d'analyse ou de v\xC3\xA9rification).");
        sp.set("operations", v);
    }
    {
        Value v = Value::object();
        v.set("type", "boolean");
        v.set("description", "Obligatoire pour que la mission soit termin\xC3\xA9" "e (vrai par d\xC3\xA9" "faut).");
        sp.set("obligatoire", v);
    }
    {
        Value v = list;
        v.set("description", "Ce qu'elle produira, genre:nom (variable:Pompe_1_Marche, vue:Vue_Station, alarme:Niveau_Haut).");
        sp.set("produit", v);
    }
    step.set("properties", sp);
    Value req = Value::array();
    req.push("titre");
    step.set("required", req);

    Value analysis = Value::object();
    analysis.set("type", "object");
    Value ap = Value::object();
    for (const char* k : {"conforme", "a_modifier", "a_completer", "a_creer", "impossible"}) {
        Value v = list;
        v.set("description", "genre:nom - remarque");
        ap.set(k, v);
    }
    ap.set("resume", str);
    analysis.set("properties", ap);

    Value props = Value::object();
    {
        Value v = str;
        v.set("description", "Le nom court de la mission (Station de pompage).");
        props.set("nom", v);
    }
    props.set("description", str);
    props.set("analyse", analysis);
    Value steps = Value::object();
    steps.set("type", "array");
    steps.set("minItems", 1);
    steps.set("items", step);
    props.set("etapes", steps);
    Value schema = Value::object();
    schema.set("type", "object");
    schema.set("properties", props);
    Value required = Value::array();
    required.push("etapes");
    schema.set("required", required);
    tool.set("input_schema", schema);
    return json::write(tool, false);
}

bool planFromTool(Mission& m, std::string_view inputJson, std::string now, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    Value v;
    std::string err;
    if (!json::parse(inputJson, v, &err) || !v.isObject()) return fail("Le plan est illisible (" + (err.empty() ? std::string("pas un objet JSON") : err) + ").");
    const Value* list = v.find("etapes");
    if (!list || !list->isArray() || list->items().empty()) return fail("\xC2\xAB etapes \xC2\xBB manque ou est vide : un plan a au moins une \xC3\xA9tape.");
    if (list->items().size() > 80) return fail("Trop d'\xC3\xA9tapes (" + std::to_string(list->items().size()) + ", 80 au plus) : regroupe-les.");

    std::vector<Step> fresh;
    std::set<std::string> ids;
    std::size_t n = 0;
    for (const auto& x : list->items()) {
        ++n;
        if (!x.isObject()) return fail("L'\xC3\xA9tape n\xC2\xB0" + std::to_string(n) + " n'est pas un objet.");
        Step s;
        s.id = trimmed(x.str("id"));
        s.title = trimmed(x.str("titre"));
        if (s.title.empty()) return fail("L'\xC3\xA9tape n\xC2\xB0" + std::to_string(n) + " n'a pas de titre.");
        s.detail = trimmed(x.str("detail"));
        s.category = normalizeCategory(x.str("categorie"));
        s.deps = stringsOf(x.at("depend_de"));
        s.plannedOps = static_cast<int>(std::clamp<std::int64_t>(x.integer("operations", 0), 0, 5000));
        s.mandatory = x.flag("obligatoire", true);
        s.expected = stringsOf(x.at("produit"));
        s.updated = now;
        fresh.push_back(std::move(s));
    }
    // Les identifiants : ceux donnes (uniques), sinon S1, S2... a la suite des existants.
    std::vector<Step> all = fresh;
    for (auto& s : all) {
        if (!s.id.empty() && ids.insert(s.id).second) continue;
        if (!s.id.empty()) return fail("L'identifiant " + s.id + " est donn\xC3\xA9 \xC3\xA0 deux \xC3\xA9tapes.");
    }
    for (auto& s : all)
        if (s.id.empty()) {
            std::vector<Step> known = m.steps;
            for (const auto& o : all)
                if (!o.id.empty()) known.push_back(o);
            s.id = nextStepId(known);
            ids.insert(s.id);
        }

    // LA FUSION avec un plan existant : les etapes qui ont du travail (en cours, a valider, appliquees, terminees, en
    // erreur, ignorees) gardent leur etat et leurs propositions ; une etape terminee absente du nouveau plan est gardee
    // (son travail est dans le projet). Jamais tout reinitialiser.
    const bool replan = !m.steps.empty();
    std::vector<std::string> added, kept, dropped;
    std::vector<Step> merged;
    for (auto& s : all) {
        if (const Step* old = m.step(s.id)) {
            const bool worked = old->state != StepState::ToPlan && old->state != StepState::Waiting && old->state != StepState::Ready
                                && old->state != StepState::Blocked;
            if (worked) {
                Step keep = *old;
                if (!isFinished(old->state)) {
                    keep.detail = s.detail.empty() ? keep.detail : s.detail;
                    keep.deps = s.deps;
                    keep.expected = s.expected.empty() ? keep.expected : s.expected;
                    keep.plannedOps = s.plannedOps;
                }
                merged.push_back(std::move(keep));
                kept.push_back(s.id);
                continue;
            }
            s.proposals = old->proposals;
        } else if (replan) {
            added.push_back(s.id);
        }
        merged.push_back(std::move(s));
    }
    for (const auto& old : m.steps) {
        if (ids.count(old.id)) continue;
        if (old.state == StepState::Done || old.state == StepState::Applied || old.state == StepState::Verifying
            || old.state == StepState::AwaitingValidation || old.state == StepState::Running || old.state == StepState::Error) {
            Step keep = old;
            keep.note = keep.note.empty() ? std::string("Gard\xC3\xA9" "e : son travail est dans le projet (absente du plan revu).") : keep.note;
            merged.push_back(std::move(keep));
            kept.push_back(old.id);
        } else {
            dropped.push_back(old.id);
        }
    }
    // Les dependances : connues, pas soi-meme ; puis les implicites (sans creer de cycle).
    std::set<std::string> known;
    for (const auto& s : merged) known.insert(s.id);
    for (auto& s : merged) {
        std::vector<std::string> deps;
        for (const auto& d : s.deps) {
            if (d == s.id) return fail("L'\xC3\xA9tape " + s.id + " d\xC3\xA9pend d'elle-m\xC3\xAAme.");
            if (!known.count(d)) return fail("L'\xC3\xA9tape " + s.id + " d\xC3\xA9pend de " + d + ", qui n'existe pas dans le plan.");
            if (std::find(deps.begin(), deps.end(), d) == deps.end()) deps.push_back(d);
        }
        s.deps = std::move(deps);
        s.implicit.clear();
    }
    {
        std::string cycle;
        std::vector<Step> probe = merged;
        if (!orderSteps(probe, &cycle)) return fail("Le plan a une d\xC3\xA9pendance circulaire : " + cycle + ".");
    }
    for (auto& s : merged) {
        for (const auto& d : implicitDeps(s, merged)) {
            if (dependsOn(merged, d, s.id)) continue;   // jamais un cycle par une regle implicite
            s.deps.push_back(d);
            s.implicit.push_back(d);
        }
    }
    std::string cycle;
    if (!orderSteps(merged, &cycle)) return fail("Le plan a une d\xC3\xA9pendance circulaire : " + cycle + ".");

    // Le plan est valide : la mission change.
    if (const std::string name = trimmed(v.str("nom")); !name.empty()) m.name = name.substr(0, 80);
    if (const std::string d = trimmed(v.str("description")); !d.empty()) m.description = d;
    if (const Value* a = v.find("analyse"); a && a->isObject()) {
        m.analysis.conform = stringsOf(a->at("conforme"));
        m.analysis.toModify = stringsOf(a->at("a_modifier"));
        m.analysis.toComplete = stringsOf(a->at("a_completer"));
        m.analysis.toCreate = stringsOf(a->at("a_creer"));
        m.analysis.impossible = stringsOf(a->at("impossible"));
        m.analysis.summary = trimmed(a->str("resume"));
    }
    for (auto& s : merged)
        if (s.state == StepState::ToPlan) s.state = StepState::Waiting;
    m.steps = std::move(merged);
    refreshReadiness(m);
    if (replan) {
        ++m.planRevision;
        std::string what = "Plan revu (r\xC3\xA9vision " + std::to_string(m.planRevision) + ") : " + std::to_string(m.steps.size()) + " \xC3\xA9tapes";
        if (!added.empty()) what += " ; ajout\xC3\xA9" "es : " + join(added, ", ");
        if (!dropped.empty()) what += " ; retir\xC3\xA9" "es : " + join(dropped, ", ");
        if (!kept.empty()) what += " ; gard\xC3\xA9" "es avec leur travail : " + join(kept, ", ");
        log(m, "plan_modifie", what + ".", now);
    } else {
        std::size_t implicitCount = 0;
        for (const auto& s : m.steps) implicitCount += s.implicit.size();
        log(m, "plan",
            "Plan \xC3\xA9tabli : " + std::to_string(m.steps.size()) + " \xC3\xA9tapes ordonn\xC3\xA9" "es par leurs d\xC3\xA9pendances"
                + (implicitCount ? " (" + std::to_string(implicitCount) + " d\xC3\xA9pendances ajout\xC3\xA9" "es par les cat\xC3\xA9gories)" : std::string{}) + ".",
            now);
        if (!m.analysis.summary.empty() || !m.analysis.conform.empty() || !m.analysis.toCreate.empty())
            log(m, "analyse",
                "Analyse du projet : " + std::to_string(m.analysis.conform.size()) + " conforme(s), "
                    + std::to_string(m.analysis.toModify.size() + m.analysis.toComplete.size()) + " \xC3\xA0 modifier ou compl\xC3\xA9ter, "
                    + std::to_string(m.analysis.toCreate.size()) + " \xC3\xA0 cr\xC3\xA9" "er, " + std::to_string(m.analysis.impossible.size())
                    + " impossible(s).",
                now);
    }
    if (m.state == State::Planning) m.state = State::Active;
    refreshState(m, now);
    return true;
}

bool editPlan(Mission& m, const PlanEdit& e, std::string now, std::string* why, std::string* added) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (m.state == State::Cancelled) return fail("La mission est annul\xC3\xA9" "e : son plan ne change plus.");
    if (m.forced.on()) return fail("La mission est termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : son plan ne change plus.");   // 1.22.3
    std::vector<Step> steps = m.steps;
    std::string text;
    const auto find = [&](const std::string& id) -> Step* {
        for (auto& s : steps)
            if (s.id == id) return &s;
        return nullptr;
    };
    switch (e.kind) {
        case PlanEdit::Kind::Add: {
            if (trimmed(e.title).empty()) return fail("Une \xC3\xA9tape a un titre.");
            Step s;
            s.id = nextStepId(steps);
            s.title = trimmed(e.title);
            s.detail = trimmed(e.detail);
            s.category = normalizeCategory(e.category);
            s.deps = e.deps;
            s.mandatory = e.mandatory;
            s.state = StepState::Waiting;
            s.updated = now;
            for (const auto& d : s.deps)
                if (!find(d)) return fail("D\xC3\xA9pendance inconnue : " + d + ".");
            if (added) *added = s.id;
            text = "\xC3\x89tape ajout\xC3\xA9" "e : " + s.id + " " + guill(s.title) + ".";
            steps.push_back(std::move(s));
            break;
        }
        case PlanEdit::Kind::Remove: {
            Step* s = find(e.step);
            if (!s) return fail("\xC3\x89tape inconnue : " + e.step + ".");
            if (s->state == StepState::Done || s->state == StepState::Applied || s->state == StepState::Verifying)
                return fail("L'\xC3\xA9tape " + s->id + " est appliqu\xC3\xA9" "e : son travail est dans le projet. Ignore-la plut\xC3\xB4t (rien n'est d\xC3\xA9" "fait).");
            if (s->state == StepState::Running || s->state == StepState::AwaitingValidation)
                return fail("Une proposition de l'\xC3\xA9tape " + s->id + " est en cours : refuse-la ou attends-la d'abord.");
            for (auto& o : steps) o.deps.erase(std::remove(o.deps.begin(), o.deps.end(), s->id), o.deps.end());
            text = "\xC3\x89tape retir\xC3\xA9" "e : " + s->id + " " + guill(s->title) + " (ses d\xC3\xA9pendantes ne l'attendent plus).";
            const std::string id = s->id;
            steps.erase(std::remove_if(steps.begin(), steps.end(), [&](const Step& x) { return x.id == id; }), steps.end());
            break;
        }
        case PlanEdit::Kind::Skip: {
            Step* s = find(e.step);
            if (!s) return fail("\xC3\x89tape inconnue : " + e.step + ".");
            if (!canMove(s->state, StepState::Skipped)) return fail("L'\xC3\xA9tape " + s->id + " (" + std::string(stepLabel(s->state)) + ") ne peut pas \xC3\xAAtre ignor\xC3\xA9" "e.");
            s->state = StepState::Skipped;
            s->note = "Ignor\xC3\xA9" "e par l'utilisateur.";
            s->updated = now;
            text = "\xC3\x89tape ignor\xC3\xA9" "e : " + s->id + " " + guill(s->title) + ".";
            break;
        }
        case PlanEdit::Kind::Unskip: {
            Step* s = find(e.step);
            if (!s || s->state != StepState::Skipped) return fail("L'\xC3\xA9tape n'est pas ignor\xC3\xA9" "e.");
            s->state = StepState::Waiting;
            s->note.clear();
            s->updated = now;
            text = "\xC3\x89tape reprise : " + s->id + " " + guill(s->title) + ".";
            break;
        }
        case PlanEdit::Kind::Rename: {
            Step* s = find(e.step);
            if (!s) return fail("\xC3\x89tape inconnue : " + e.step + ".");
            if (trimmed(e.title).empty()) return fail("Une \xC3\xA9tape a un titre.");
            text = "\xC3\x89tape renomm\xC3\xA9" "e : " + s->id + " " + guill(s->title) + " -> " + guill(trimmed(e.title)) + ".";
            s->title = trimmed(e.title);
            if (!trimmed(e.detail).empty()) s->detail = trimmed(e.detail);
            if (!trimmed(e.category).empty()) s->category = normalizeCategory(e.category);
            break;
        }
        case PlanEdit::Kind::SetDeps: {
            Step* s = find(e.step);
            if (!s) return fail("\xC3\x89tape inconnue : " + e.step + ".");
            for (const auto& d : e.deps) {
                if (d == s->id) return fail("Une \xC3\xA9tape ne d\xC3\xA9pend pas d'elle-m\xC3\xAAme.");
                if (!find(d)) return fail("D\xC3\xA9pendance inconnue : " + d + ".");
            }
            s->deps = e.deps;
            s->implicit.clear();
            text = "D\xC3\xA9pendances de " + s->id + " : " + (e.deps.empty() ? std::string("aucune") : join(e.deps, ", ")) + ".";
            break;
        }
        case PlanEdit::Kind::SetMandatory: {
            Step* s = find(e.step);
            if (!s) return fail("\xC3\x89tape inconnue : " + e.step + ".");
            s->mandatory = e.mandatory;
            text = s->id + (e.mandatory ? " est obligatoire." : " devient facultative.");
            break;
        }
    }
    std::string cycle;
    if (!orderSteps(steps, &cycle)) return fail("Ce changement cr\xC3\xA9" "e une d\xC3\xA9pendance circulaire : " + cycle + ".");
    m.steps = std::move(steps);
    ++m.planRevision;
    refreshReadiness(m);
    log(m, "plan_modifie", text, now, e.step);
    refreshState(m, now);
    return true;
}

// ---- le prochain lot --------------------------------------------------------------------------------------------------
Batch nextBatch(const Mission& m, int maxOps) {
    Batch b;
    for (const auto& s : m.steps)
        if (s.state == StepState::Blocked) b.blocked.push_back(s.id + " : " + (s.note.empty() ? std::string("bloqu\xC3\xA9" "e") : s.note));
    if (m.state == State::Planning) {
        b.why = "Le plan n'est pas encore \xC3\xA9tabli.";
        return b;
    }
    if (m.state == State::Paused) {
        b.why = "La mission est en pause : reprends-la d'abord.";
        return b;
    }
    if (m.state == State::Cancelled) {
        b.why = "La mission est annul\xC3\xA9" "e.";
        return b;
    }
    if (m.coherence == Coherence::Missing) {
        b.why = "Le projet de la mission n'est pas celui qui est ouvert : ouvre-le d'abord.";
        return b;
    }
    const int budget = std::max(1, maxOps);
    std::set<std::string> in;
    for (const auto& s : m.steps) {
        if (s.state != StepState::Ready) continue;
        const int w = weightOf(s);
        if (!b.steps.empty() && b.ops + w > budget) continue;
        b.steps.push_back(s.id);
        in.insert(s.id);
        b.ops += w;
    }
    // Les etapes en attente dont les dependances sont finies ou dans le lot (une chaine dans une proposition).
    for (bool grew = !b.steps.empty(); grew;) {
        grew = false;
        for (const auto& s : m.steps) {
            if (s.state != StepState::Waiting || in.count(s.id)) continue;
            bool ok = true;
            for (const auto& d : s.deps) {
                const Step* dep = m.step(d);
                if (!dep) continue;
                if (!isFinished(dep->state) && !in.count(d)) ok = false;
            }
            if (!ok) continue;
            const int w = weightOf(s);
            if (b.ops + w > budget) continue;
            b.steps.push_back(s.id);
            in.insert(s.id);
            b.ops += w;
            grew = true;
        }
    }
    // Dans l'ordre du plan.
    std::vector<std::string> ordered;
    for (const auto& s : m.steps)
        if (in.count(s.id)) ordered.push_back(s.id);
    b.steps = std::move(ordered);
    if (!b.steps.empty()) return b;
    // Rien de realisable : pourquoi.
    std::vector<std::string> awaiting, errors, running;
    bool allFinished = true;
    for (const auto& s : m.steps) {
        if (s.state == StepState::AwaitingValidation) awaiting.push_back(s.id);
        if (s.state == StepState::Error) errors.push_back(s.id + " " + guill(s.title));
        if (s.state == StepState::Running) running.push_back(s.id);
        if (s.mandatory && !isFinished(s.state) && s.state != StepState::Cancelled) allFinished = false;
    }
    if (!running.empty()) b.why = "Une proposition se pr\xC3\xA9pare d\xC3\xA9j\xC3\xA0 (" + join(running, ", ") + ").";
    else if (!awaiting.empty()) b.why = "Une proposition attend d'\xC3\xAAtre pr\xC3\xA9visualis\xC3\xA9" "e et appliqu\xC3\xA9" "e (" + join(awaiting, ", ") + ").";
    else if (!errors.empty()) b.why = "Une \xC3\xA9tape est en erreur : " + join(errors, ", ") + ". Propose une correction.";
    else if (allFinished) b.why = "Toutes les \xC3\xA9tapes obligatoires sont termin\xC3\xA9" "es : lance l'audit final.";
    else if (!b.blocked.empty()) b.why = "Les \xC3\xA9tapes restantes sont bloqu\xC3\xA9" "es : " + join(b.blocked, " ; ") + ".";
    else b.why = "Rien n'est r\xC3\xA9" "alisable pour l'instant.";
    return b;
}

bool independent(const Mission& m, std::string_view a, std::string_view b) {
    return !dependsOn(m.steps, a, b) && !dependsOn(m.steps, b, a);
}

// ---- le suivi -----------------------------------------------------------------------------------------------------------
bool setStepState(Mission& m, std::string_view id, StepState to, std::string now, std::string note, std::string* why) {
    Step* s = m.step(id);
    if (!s) {
        if (why) *why = "\xC3\x89tape inconnue : " + std::string(id) + ".";
        return false;
    }
    if (!canMove(s->state, to)) {
        if (why)
            *why = "L'\xC3\xA9tape " + s->id + " ne passe pas de \xC2\xAB " + std::string(stepLabel(s->state)) + " \xC2\xBB \xC3\xA0 \xC2\xAB " + std::string(stepLabel(to)) + " \xC2\xBB.";
        return false;
    }
    const StepState from = s->state;
    s->state = to;
    s->note = std::move(note);
    s->updated = now;
    if (to == StepState::Error) s->error = s->note;
    // 1.22.1 : un appel a l'IA fini sans proposition (en cours -> prete : interrompu, en echec) est dit aussi - le journal
    // de l'etape le montre avec sa raison.
    if (from != to && (to == StepState::Done || to == StepState::Error || to == StepState::Skipped || to == StepState::Cancelled
                       || (from == StepState::Done && to == StepState::Ready) || (from == StepState::Running && to == StepState::Ready)))
        log(m, "etape", s->id + " " + guill(s->title) + " : " + std::string(stepLabel(to)) + (s->note.empty() ? std::string{} : " \xE2\x80\x94 " + s->note), now, s->id);
    return true;
}

void onGenerated(Mission& m, const Proposal& p, const std::string& conversation, const std::string& message,
                 const std::vector<std::string>& steps, std::string now) {
    ProposalLink* link = m.proposal(p.id);
    if (!link) {
        ProposalLink l;
        l.id = p.id;
        l.order = static_cast<int>(m.proposals.size()) + 1;
        m.proposals.push_back(std::move(l));
        link = &m.proposals.back();
    }
    link->conversation = conversation;
    link->message = message;
    link->title = p.summary;
    link->state = "generee";
    link->ops = static_cast<int>(p.ops.size());
    link->create = p.create;
    link->modify = p.modify;
    link->remove = p.remove;
    link->created = now;
    link->next = p.next;
    link->lines.clear();
    std::vector<std::string> covered = steps;
    for (const auto& op : p.ops) {
        OpLine l;
        l.step = op.step;
        l.title = std::string(actionKey(op.action)) + (op.name.empty() ? std::string{} : " " + op.name);
        l.state = "a_faire";
        link->lines.push_back(std::move(l));
        if (!op.step.empty() && m.step(op.step) && std::find(covered.begin(), covered.end(), op.step) == covered.end())
            covered.push_back(op.step);
    }
    std::vector<std::string> real;
    for (const auto& id : covered)
        if (m.step(id)) real.push_back(id);
    link->steps = real;
    if (!conversation.empty() && std::find(m.conversations.begin(), m.conversations.end(), conversation) == m.conversations.end())
        m.conversations.push_back(conversation);
    for (const auto& id : real) {
        Step* s = m.step(id);
        if (std::find(s->proposals.begin(), s->proposals.end(), p.id) == s->proposals.end()) s->proposals.push_back(p.id);
        if (s->state == StepState::Ready || s->state == StepState::Waiting) s->state = StepState::Running;   // le chemin normal
        if (canMove(s->state, StepState::AwaitingValidation)) {
            s->state = StepState::AwaitingValidation;
            s->note = "P" + std::to_string(link->order) + " attend d'\xC3\xAAtre pr\xC3\xA9visualis\xC3\xA9" "e et appliqu\xC3\xA9" "e.";
            s->updated = now;
        }
    }
    log(m, "proposition",
        "P" + std::to_string(link->order) + " " + guill(p.summary.empty() ? std::string("Proposition") : p.summary) + " : "
            + std::to_string(p.ops.size()) + " op\xC3\xA9ration(s)" + (real.empty() ? std::string{} : " pour " + join(real, ", ")) + ".",
        now, {}, p.id);
}

void onChecked(Mission& m, std::string_view id, const Check& check, std::string now) {
    ProposalLink* link = m.proposal(id);
    if (!link) return;
    link->errors = check.errors;
    if (link->state == "generee" || link->state == "validee") link->state = check.ok() ? "validee" : "generee";
    if (!check.ok())
        log(m, "validation",
            "P" + std::to_string(link->order) + " : " + std::to_string(check.errors.size()) + " erreur(s) de validation \xE2\x80\x94 "
                + (check.errors.empty() ? std::string{} : check.errors.front()),
            now, {}, link->id);
    else
        log(m, "validation", "P" + std::to_string(link->order) + " v\xC3\xA9rifi\xC3\xA9" "e sur une copie du projet : pr\xC3\xAAte \xC3\xA0 appliquer.", now, {}, link->id);
}

void onApplied(Mission& m, std::string_view id, const plan::Plan& pl, const plan::Report& report, const hmi::Project& after, std::string now) {
    ProposalLink* link = m.proposal(id);
    if (!link) return;
    link->appliedAt = now;
    link->applied = static_cast<int>(report.applied);
    link->failed = static_cast<int>(report.failed);
    link->skipped = static_cast<int>(report.skipped);
    link->report = plan::reportToJson(report);
    link->errors = report.failures;
    // Chaque operation : son etat reel (le plan mis a jour par le compte rendu), ce qu'elle cree.
    if (link->lines.size() < static_cast<std::size_t>(link->ops)) link->lines.resize(static_cast<std::size_t>(link->ops));
    int reuse = 0;
    for (const auto& st : pl.steps) {
        if (st.op >= link->lines.size()) link->lines.resize(st.op + 1);
        OpLine& l = link->lines[st.op];
        l.id = st.id;
        l.title = st.title;
        l.verb = std::string(plan::verbLabel(st.verb));
        l.state = std::string(plan::stateKey(st.state));
        l.creates.clear();
        for (const auto& r : st.creates) l.creates.push_back(r.kind + "|" + r.name + "|" + r.view);
        l.error = st.errors.empty() ? std::string{} : st.errors.front();
        if (st.verb == plan::Verb::Reuse) ++reuse;
    }
    link->reuse = reuse;
    const bool nothing = report.restored || report.applied == 0;
    link->state = nothing ? "echouee" : "appliquee";
    Finder finder(after);
    std::vector<std::string> doneSteps, errorSteps;
    for (const auto& sid : link->steps) {
        Step* s = m.step(sid);
        if (!s) continue;
        std::vector<const OpLine*> mine;
        for (const auto& l : link->lines)
            if (l.step == sid || l.step.empty()) mine.push_back(&l);
        const std::string P = "P" + std::to_string(link->order);
        if (mine.empty()) {
            if (canMove(s->state, StepState::Ready)) setStepState(m, sid, StepState::Ready, now, "Aucune op\xC3\xA9ration de " + P + " ne la servait.");
            continue;
        }
        std::vector<std::string> errs;
        bool anyApplied = false, allIgnored = true;
        for (const auto* l : mine) {
            if (l->state == "echouee") errs.push_back((l->id.empty() ? std::string{} : l->id + " ") + l->title + (l->error.empty() ? std::string{} : " : " + l->error));
            if (l->state == "appliquee") anyApplied = true;
            if (l->state != "ignoree") allIgnored = false;
        }
        if (!canMove(s->state, StepState::Error)) s->state = StepState::AwaitingValidation;   // le projet reel fait foi
        if (report.restored) {
            const std::string why = report.failures.empty() ? std::string("la transaction a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e, le projet n'a pas chang\xC3\xA9")
                                                            : report.failures.front();
            setStepState(m, sid, StepState::Error, now, P + " non appliqu\xC3\xA9" "e : " + why + ".");
            errorSteps.push_back(sid);
            continue;
        }
        if (!errs.empty()) {
            s->state = StepState::Applied;
            setStepState(m, sid, StepState::Error, now, P + " : " + join(errs, " ; "));
            errorSteps.push_back(sid);
            continue;
        }
        if (allIgnored || !anyApplied) {
            setStepState(m, sid, StepState::Ready, now, "Les op\xC3\xA9rations de " + P + " pour cette \xC3\xA9tape ont \xC3\xA9t\xC3\xA9 retir\xC3\xA9" "es : rien n'a \xC3\xA9t\xC3\xA9 appliqu\xC3\xA9.");
            continue;
        }
        // Verifier dans le projet d'apres : ce que ses operations creent y est-il ?
        std::vector<std::string> missing;
        for (const auto* l : mine)
            if (l->state == "appliquee")
                for (const auto& r : l->creates)
                    if (!finder.exists(r)) missing.push_back(refText(r));
        s->state = StepState::Applied;
        if (!missing.empty()) {
            setStepState(m, sid, StepState::Error, now, "Absent du projet apr\xC3\xA8s " + P + " : " + join(missing, ", ") + ".");
            errorSteps.push_back(sid);
        } else {
            setStepState(m, sid, StepState::Done, now, "Appliqu\xC3\xA9" "e par " + P + " et v\xC3\xA9rifi\xC3\xA9" "e dans le projet.");
            s->error.clear();
            doneSteps.push_back(sid);
        }
    }
    log(m, "application",
        "P" + std::to_string(link->order) + " " + (nothing ? std::string("non appliqu\xC3\xA9" "e") : std::string("appliqu\xC3\xA9" "e")) + " : "
            + std::to_string(report.applied) + " op\xC3\xA9ration(s) appliqu\xC3\xA9" "e(s), " + std::to_string(report.failed) + " \xC3\xA9" "chou\xC3\xA9" "e(s), "
            + std::to_string(report.skipped) + " ignor\xC3\xA9" "e(s)" + (doneSteps.empty() ? std::string{} : " ; termin\xC3\xA9" "es : " + join(doneSteps, ", "))
            + (errorSteps.empty() ? std::string{} : " ; en erreur : " + join(errorSteps, ", ")) + ".",
        now, {}, link->id);
    m.resumePoint = "Apr\xC3\xA8s P" + std::to_string(link->order) + " (" + now + ")";
    refreshReadiness(m);
    refreshState(m, now);
}

void onDropped(Mission& m, std::string_view id, std::string_view state, std::string reason, std::string now) {
    ProposalLink* link = m.proposal(id);
    if (link) {
        link->state = std::string(state);
        if (!reason.empty()) link->errors.push_back(reason);
    }
    const std::string P = link ? "P" + std::to_string(link->order) : std::string(id);
    const std::vector<std::string> steps = link ? link->steps : std::vector<std::string>{};
    for (const auto& sid : steps) {
        Step* s = m.step(sid);
        if (!s) continue;
        if (s->state == StepState::Running || s->state == StepState::AwaitingValidation)
            setStepState(m, sid, StepState::Ready, now, P + " " + std::string(state) + (reason.empty() ? std::string{} : " : " + reason));
    }
    std::string label = std::string(state);
    if (state == "refusee") label = "refus\xC3\xA9" "e";
    else if (state == "obsolete") label = "devenue obsol\xC3\xA8te (le projet a chang\xC3\xA9)";
    else if (state == "interrompue") label = "interrompue";
    else if (state == "echouee") label = "en \xC3\xA9" "chec";
    else if (state == "annulee") label = "annul\xC3\xA9" "e";
    log(m, state == "interrompue" ? "interruption" : "proposition", P + " " + label + (reason.empty() ? std::string{} : " : " + reason) + ".", now, {},
        std::string(id));
    refreshReadiness(m);
    refreshState(m, now);
}

bool exists(const hmi::Project& p, std::string_view ref) {
    Finder f(p);
    return f.exists(ref);
}

bool verifyAgainst(Mission& m, const hmi::Project& project, std::string now) {
    bool changed = false;
    Finder finder(project);
    for (auto& link : m.proposals) {
        if (link.state != "appliquee" && link.state != "annulee") continue;
        std::vector<std::string> refs, missing;
        for (const auto& l : link.lines)
            if (l.state == "appliquee")
                for (const auto& r : l.creates) {
                    refs.push_back(r);
                    if (!finder.exists(r)) missing.push_back(refText(r));
                }
        if (refs.empty()) continue;
        const std::string P = "P" + std::to_string(link.order);
        if (link.state == "appliquee" && missing.size() == refs.size()) {
            // Tout ce qu'elle avait cree est parti : annulee (Ctrl+Z) ou defaite a la main.
            link.state = "annulee";
            changed = true;
            for (const auto& sid : link.steps)
                if (Step* s = m.step(sid); s && s->state == StepState::Done)
                    setStepState(m, sid, StepState::Ready, now, "Ce que " + P + " avait cr\xC3\xA9\xC3\xA9 n'est plus dans le projet (Ctrl+Z ?).");
            log(m, "proposition", P + " annul\xC3\xA9" "e apr\xC3\xA8s coup : ce qu'elle avait cr\xC3\xA9\xC3\xA9 n'est plus dans le projet.", now, {}, link.id);
        } else if (link.state == "annulee" && missing.empty()) {
            // Retablie (Ctrl+Y) : tout est de nouveau la.
            link.state = "appliquee";
            changed = true;
            for (const auto& sid : link.steps)
                if (Step* s = m.step(sid);
                    s && (s->state == StepState::Ready || s->state == StepState::Waiting || s->state == StepState::Blocked)) {
                    s->state = StepState::Applied;
                    setStepState(m, sid, StepState::Done, now, "R\xC3\xA9tablie : ce que " + P + " avait cr\xC3\xA9\xC3\xA9 est de nouveau dans le projet.");
                }
            log(m, "proposition", P + " r\xC3\xA9tablie : ce qu'elle avait cr\xC3\xA9\xC3\xA9 est de nouveau dans le projet.", now, {}, link.id);
        } else if (link.state == "appliquee" && !missing.empty()) {
            for (const auto& sid : link.steps)
                if (Step* s = m.step(sid); s && s->state == StepState::Done) {
                    setStepState(m, sid, StepState::Ready, now, "Plus dans le projet : " + join(missing, ", ") + ".");
                    changed = true;
                }
        }
    }
    if (changed) {
        refreshReadiness(m);
        refreshState(m, now);
    }
    return changed;
}

void refreshState(Mission& m, std::string now) {
    if (m.state == State::Cancelled || m.state == State::Paused || m.state == State::Planning) return;
    if (m.state == State::Done && m.forced.on()) return;   // 1.22.3 : une fin forcee ne se rouvre pas (le projet peut changer apres)
    const State before = m.state;
    bool allMandatoryDone = true, anyActionable = false, anyBlockedOrError = false;
    for (const auto& s : m.steps) {
        if (s.state == StepState::Cancelled) continue;
        if (s.mandatory && !isFinished(s.state)) allMandatoryDone = false;
        if (s.state == StepState::Ready || s.state == StepState::Running || s.state == StepState::AwaitingValidation
            || s.state == StepState::Applied || s.state == StepState::Verifying || s.state == StepState::Waiting)
            anyActionable = true;
        if (s.state == StepState::Error || s.state == StepState::Blocked) anyBlockedOrError = true;
    }
    const bool hasReport = m.finalReport.isObject();
    const bool complete = hasReport && m.finalReport.flag("complete", false);
    if (allMandatoryDone && complete && !anyBlockedOrError) m.state = State::Done;
    else if (!anyActionable && anyBlockedOrError && !allMandatoryDone) m.state = State::Blocked;
    else m.state = State::Active;
    if (m.state != before) log(m, m.state == State::Done ? "terminee" : "etat", "Mission : " + std::string(stateLabel(m.state)) + ".", now);
}

bool pause(Mission& m, std::string now, std::string* why) {
    if (m.state == State::Cancelled || m.state == State::Done) {
        if (why) *why = std::string("La mission est ") + (m.state == State::Done ? "termin\xC3\xA9" "e" : "annul\xC3\xA9" "e") + " : elle ne se met pas en pause.";
        return false;
    }
    if (m.state == State::Paused) return true;
    m.state = State::Paused;
    log(m, "pause", "Mission mise en pause : aucune g\xC3\xA9n\xC3\xA9ration ni application tant qu'elle n'est pas reprise.", now);
    return true;
}

bool resume(Mission& m, std::string now, std::string* why) {
    if (m.state == State::Cancelled) {
        if (why) *why = "La mission est annul\xC3\xA9" "e : elle ne reprend pas.";
        return false;
    }
    if (m.state != State::Paused) return true;
    m.state = m.steps.empty() ? State::Planning : State::Active;
    log(m, "reprise", "Mission reprise.", now);
    refreshReadiness(m);
    refreshState(m, now);
    return true;
}

bool cancel(Mission& m, std::string now, std::string* why) {
    if (m.state == State::Cancelled) return true;
    if (m.state == State::Done) {
        if (why) *why = "La mission est termin\xC3\xA9" "e : il n'y a plus rien \xC3\xA0 annuler.";
        return false;
    }
    for (auto& s : m.steps)
        if (!isFinished(s.state)) {
            s.state = StepState::Cancelled;
            s.note = "Mission annul\xC3\xA9" "e.";
            s.updated = now;
        }
    for (auto& p : m.proposals)
        if (p.state == "generee" || p.state == "validee") p.state = "annulee";
    m.pending = {};
    m.state = State::Cancelled;
    log(m, "annulation", "Mission annul\xC3\xA9" "e : aucune de ses propositions ne s'appliquera plus (ce qui a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 appliqu\xC3\xA9 reste dans le projet).", now);
    return true;
}

void archive(Mission& m, bool on, std::string now) {
    if (m.archived == on) return;
    m.archived = on;
    log(m, "archive", on ? "Mission archiv\xC3\xA9" "e." : "Mission sortie des archives.", now);
}

bool mayApply(const Mission& m, std::string_view id, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (m.state == State::Paused) return fail("La mission " + guill(m.name) + " est en pause : reprends-la avant d'appliquer.");
    if (m.state == State::Cancelled) return fail("La mission " + guill(m.name) + " est annul\xC3\xA9" "e : ses propositions ne s'appliquent plus.");
    if (m.forced.on())   // 1.22.3
        return fail("La mission " + guill(m.name) + " est termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : ses propositions ne s'appliquent plus.");
    if (const ProposalLink* p = m.proposal(id)) {
        if (p->state == "obsolete") return fail("Cette proposition est devenue obsol\xC3\xA8te (le projet a chang\xC3\xA9) : demande la suite pour en avoir une \xC3\xA0 jour.");
        if (p->state == "annulee") return fail("Cette proposition est annul\xC3\xA9" "e.");
        if (p->state == "interrompue") return fail("Cette proposition a \xC3\xA9t\xC3\xA9 interrompue.");
    }
    return true;
}

// ---- 1.22.3 : forcer la fin (voir le .hpp) ----------------------------------------------------------------------------
bool mayForceFinish(const Mission& m, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (m.state == State::Done)
        return fail(std::string("La mission est d\xC3\xA9j\xC3\xA0 termin\xC3\xA9" "e") + (m.forced.on() ? " (fin forc\xC3\xA9" "e)" : "")
                    + " : il n'y a plus rien \xC3\xA0 arr\xC3\xAAter.");
    if (m.state == State::Cancelled) return fail("La mission est annul\xC3\xA9" "e : sa fin ne se force pas.");
    return true;
}

std::vector<std::string> unfinishedSteps(const Mission& m) {
    std::vector<std::string> out;
    for (const auto& s : m.steps)
        if (s.state != StepState::Done && s.state != StepState::Skipped && s.state != StepState::Cancelled) out.push_back(s.id);
    return out;
}

std::vector<std::string> awaitingProposals(const Mission& m) {
    std::vector<std::string> out;
    for (const auto& p : m.proposals)
        if (p.state == "generee" || p.state == "validee") out.push_back(p.id);
    return out;
}

bool forceFinish(Mission& m, std::string now, std::string reason, std::string* why) {
    if (!mayForceFinish(m, why)) return false;
    reason = trimmed(reason);
    if (reason.empty()) reason = std::string(kForcedReason);
    std::string note = reason;   // la note de chaque etape ignoree : "Fin forc\xC3\xA9" "e par l'utilisateur."
    if (note[0] >= 'a' && note[0] <= 'z') note[0] = static_cast<char>(note[0] - 'a' + 'A');
    if (note.back() != '.') note += '.';
    Forced f;
    f.date = now;
    f.reason = reason;
    // 1. l'appel a l'IA de la mission (l'Assistant l'a arrete) : efface et dit ; rien n'est rejoue
    if (m.pending.active) {
        log(m, "interruption",
            "Appel \xC3\xA0 l'IA (" + (m.pending.purpose.empty() ? std::string("proposition") : m.pending.purpose)
                + ") arr\xC3\xAAt\xC3\xA9 : fin forc\xC3\xA9" "e de la mission. Rien n'est rejou\xC3\xA9.",
            now);
        m.pending = {};
    }
    // 2. les propositions qui attendent une decision : refusees (l'etat existant d'une proposition non appliquee par
    //    choix de l'utilisateur) - mayApply les refuse de toute facon (la fin forcee)
    std::vector<std::string> tags;
    for (auto& p : m.proposals) {
        if (p.state != "generee" && p.state != "validee") continue;
        p.state = "refusee";
        p.errors.push_back("Non appliqu\xC3\xA9" "e : fin forc\xC3\xA9" "e de la mission.");
        f.proposals.push_back(p.id);
        tags.push_back("P" + std::to_string(p.order));
        log(m, "proposition", tags.back() + " refus\xC3\xA9" "e : fin forc\xC3\xA9" "e de la mission (elle ne s'appliquera pas).", now, {}, p.id);
    }
    // 3. les etapes non terminees : ignorees, par les transitions de canMove (en cours, a valider, appliquee, en
    //    verification : par << prete >> d'abord, comme une proposition refusee ou un appel arrete)
    for (auto& s : m.steps) {
        if (s.state == StepState::Done || s.state == StepState::Skipped || s.state == StepState::Cancelled) continue;
        if (!canMove(s.state, StepState::Skipped) && canMove(s.state, StepState::Ready) && canMove(StepState::Ready, StepState::Skipped))
            s.state = StepState::Ready;
        if (setStepState(m, s.id, StepState::Skipped, now, note)) f.steps.push_back(s.id);
    }
    // 4. la mission : terminee ; un ancien rapport final ne la decrit plus (l'audit final le refait)
    m.forced = std::move(f);
    m.finalReport = Value();
    m.state = State::Done;
    m.resumePoint = "Fin forc\xC3\xA9" "e (" + now + ")";
    const std::size_t n = m.forced.steps.size();
    std::string text = "Fin forc\xC3\xA9" "e par l'utilisateur : "
                       + (n == 0 ? std::string("aucune \xC3\xA9tape \xC3\xA0 ignorer") : ignoredCount(n) + " (" + join(m.forced.steps, ", ") + ")");
    if (!tags.empty()) text += " ; " + join(tags, ", ") + (tags.size() > 1 ? " non appliqu\xC3\xA9" "es" : " non appliqu\xC3\xA9" "e");
    text += ". Rien n'est d\xC3\xA9" "fait dans le projet.";
    if (reason != kForcedReason) text += " Raison : " + reason + ".";
    log(m, "terminee", text, now);
    return true;
}

std::string stateText(const Mission& m) {
    if (m.state == State::Done && m.forced.on()) return "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)";
    return std::string(stateLabel(m.state));
}

std::string stepText(const Mission& m, const Step& s) {
    if (s.state == StepState::Skipped && forcedOut(m, s.id)) return "Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e)";
    return std::string(stepLabel(s.state));
}

// ---- la progression ---------------------------------------------------------------------------------------------------
int Progress::percent() const noexcept { return static_cast<int>(std::lround(global * 100.0)); }

std::string Progress::text() const {
    std::string s = std::to_string(done) + " / " + std::to_string(steps) + " \xC3\xA9tape" + (steps > 1 ? "s" : "") + " termin\xC3\xA9" "e" + (done > 1 ? "s" : "");
    if (running + awaiting > 0) s += " \xC2\xB7 " + std::to_string(running + awaiting) + " en cours";
    if (waiting + ready > 0) s += " \xC2\xB7 " + std::to_string(waiting + ready) + " en attente";
    if (errors > 0) s += " \xC2\xB7 " + std::to_string(errors) + " en erreur";
    if (blocked > 0) s += " \xC2\xB7 " + std::to_string(blocked) + " bloqu\xC3\xA9" "e" + (blocked > 1 ? "s" : "");
    s += " \xC2\xB7 " + std::to_string(percent()) + " %";
    return s;
}

Progress progressOf(const Mission& m) {
    Progress p;
    double total = 0, gen = 0, val = 0, app = 0, done = 0;
    for (const auto& s : m.steps) {
        if (s.state == StepState::Skipped) {
            ++p.skipped;
            continue;
        }
        if (s.state == StepState::Cancelled) continue;
        ++p.steps;
        const double w = weightOf(s);
        total += w;
        switch (s.state) {
            case StepState::Done: ++p.done; break;
            case StepState::Running: ++p.running; break;
            case StepState::AwaitingValidation:
            case StepState::Applied:
            case StepState::Verifying: ++p.awaiting; break;
            case StepState::Waiting:
            case StepState::ToPlan: ++p.waiting; break;
            case StepState::Ready: ++p.ready; break;
            case StepState::Error: ++p.errors; break;
            case StepState::Blocked: ++p.blocked; break;
            default: break;
        }
        // Les propositions de l'etape : la plus avancee compte.
        bool generated = false, validated = false, applied = false;
        for (const auto& pid : s.proposals)
            if (const ProposalLink* l = m.proposal(pid)) {
                if (l->state != "interrompue") generated = true;
                if (l->state == "validee" || l->state == "appliquee") validated = true;
                if (l->state == "appliquee") applied = true;
            }
        if (s.state == StepState::Done) generated = validated = applied = true;
        if (s.state == StepState::Applied || s.state == StepState::Verifying) applied = validated = generated = true;
        if (s.state == StepState::AwaitingValidation || s.state == StepState::Running) generated = generated || s.state == StepState::AwaitingValidation;
        if (generated) gen += w;
        if (validated) val += w;
        if (applied) app += w;
        if (s.state == StepState::Done) done += w;
    }
    p.planning = m.steps.empty() ? 0.0 : 1.0;
    if (total > 0) {
        p.generation = gen / total;
        p.validation = val / total;
        p.application = app / total;
        p.global = done / total;
    }
    return p;
}

std::string progressRule() {
    return "Chaque \xC3\xA9tape p\xC3\xA8se son estimation d'op\xC3\xA9rations (au moins 1) ; les \xC3\xA9tapes ignor\xC3\xA9" "es ou annul\xC3\xA9" "es sortent du calcul. "
           "Planification : 100 % d\xC3\xA8s que le plan existe. G\xC3\xA9n\xC3\xA9ration : les \xC3\xA9tapes qui ont une proposition. Validation : celles dont la "
           "proposition est v\xC3\xA9rifi\xC3\xA9" "e ou appliqu\xC3\xA9" "e. Application : celles appliqu\xC3\xA9" "es. Globale : celles TERMIN\xC3\x89" "ES, c'est-\xC3\xA0-dire appliqu\xC3\xA9" "es "
           "et v\xC3\xA9rifi\xC3\xA9" "es dans le projet.\n"
           + stepProgressRule();
}

// ---- 1.22.1 : ce que fait une etape -----------------------------------------------------------------------------------
namespace {

// La proposition sert-elle l'etape ? (ses etapes, celles de l'etape, une operation qui la cite)
bool serves(const ProposalLink& p, const Step& s) {
    if (std::find(p.steps.begin(), p.steps.end(), s.id) != p.steps.end()) return true;
    if (std::find(s.proposals.begin(), s.proposals.end(), p.id) != s.proposals.end()) return true;
    return std::any_of(p.lines.begin(), p.lines.end(), [&](const OpLine& l) { return l.step == s.id; });
}
// Une operation de la proposition est-elle de l'etape ? (sans etape : de toutes celles de la proposition)
bool lineOf(const ProposalLink& p, const OpLine& l, const Step& s) {
    return l.step == s.id || (l.step.empty() && std::find(p.steps.begin(), p.steps.end(), s.id) != p.steps.end());
}
// Retiree de l'apercu, ou reprise par Ctrl+Z : elle ne compte pas.
bool retired(const OpLine& l) { return l.state == "ignoree" || l.state == "annulee"; }
// Une proposition qui attend Previsualiser / Appliquer.
bool awaiting(const ProposalLink& p) { return p.state == "generee" || p.state == "validee"; }
// Celles qui comptent pour la progression d'une etape : a valider, appliquees, en echec (une refusee, obsolete,
// interrompue ou annulee n'a rien - ou plus rien - dans le projet).
bool countsFor(const ProposalLink& p) { return awaiting(p) || p.state == "appliquee" || p.state == "echouee"; }

std::string tag(const ProposalLink& p) { return "P" + std::to_string(p.order); }

std::string plural(int n, std::string_view one, std::string_view many) { return std::to_string(n) + " " + std::string(n > 1 ? many : one); }

// "le 01/11 \xC3\xA0 12:04" (une date ISO ; vide : illisible)
std::string dayAt(std::string_view iso) {
    if (iso.size() < 16 || iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':') return {};
    return "le " + std::string(iso.substr(8, 2)) + "/" + std::string(iso.substr(5, 2)) + " \xC3\xA0 " + std::string(iso.substr(11, 5));
}

// Un texte en une ligne courte : la premiere ligne, au plus `max` octets (jamais au milieu d'un caractere).
std::string shortLine(std::string_view s, std::size_t max = 140) {
    std::string t = trimmed(s.substr(0, s.find_first_of("\r\n")));
    if (t.size() <= max) return t;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;
    t.resize(cut);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t + "\xE2\x80\xA6";
}

std::string lowerFirst(std::string s) {
    if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = static_cast<char>(s[0] - 'A' + 'a');
    return s;
}

// L'identifiant cite tel quel ("S1" dans "pour S1, S2" ; pas dans "S10" ni "Vue_S1").
bool cites(std::string_view text, std::string_view id) {
    if (id.empty()) return false;
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t at = text.find(id); at != std::string_view::npos; at = text.find(id, at + 1)) {
        const bool before = at == 0 || !word(text[at - 1]);
        const bool after = at + id.size() >= text.size() || !word(text[at + id.size()]);
        if (before && after) return true;
    }
    return false;
}

// L'appel a l'IA de la mission vise-t-il cette etape ?
bool called(const Mission& m, const Step& s) {
    return m.pending.active && std::find(m.pending.steps.begin(), m.pending.steps.end(), s.id) != m.pending.steps.end();
}

// La plus recente proposition a valider qui sert l'etape ; nul : aucune.
const ProposalLink* awaitingFor(const Mission& m, const Step& s) {
    const ProposalLink* best = nullptr;
    for (const auto& p : m.proposals)
        if (awaiting(p) && serves(p, s) && (!best || p.order > best->order)) best = &p;
    return best;
}

// La plus recente proposition appliquee qui sert l'etape ; nul : aucune.
const ProposalLink* appliedFor(const Mission& m, const Step& s) {
    const ProposalLink* best = nullptr;
    for (const auto& p : m.proposals)
        if (p.state == "appliquee" && serves(p, s) && (!best || p.order > best->order)) best = &p;
    return best;
}

// La proposition appliquee sur un fil de fond, si elle sert l'etape ; nul sinon.
const ProposalLink* applyingFor(const Mission& m, const Step& s, const Live& live) {
    if (live.applying.empty()) return nullptr;
    const ProposalLink* p = m.proposal(live.applying);
    return p && serves(*p, s) ? p : nullptr;
}

std::string since(const Live& live) { return live.elapsed >= 0 ? " (depuis " + elapsedText(live.elapsed) + ")" : std::string{}; }

} // namespace

double StepProgress::frac() const noexcept { return percent < 0 ? 0.0 : static_cast<double>(percent) / 100.0; }

std::string StepProgress::text() const { return percent < 0 ? std::string("\xE2\x80\x94") : std::to_string(percent) + " %"; }

std::string StepProgress::detail() const {
    if (percent < 0) return "hors du calcul de la progression";
    if (estimated)
        return percent >= 100 ? std::string("termin\xC3\xA9" "e (aucune op\xC3\xA9ration suivie)")
                              : "aucune op\xC3\xA9ration propos\xC3\xA9" "e encore (estimation : " + plural(planned, "op\xC3\xA9ration", "op\xC3\xA9rations") + ")";
    std::string s = std::to_string(applied) + " / " + plural(planned, "op\xC3\xA9ration appliqu\xC3\xA9" "e", "op\xC3\xA9rations appliqu\xC3\xA9" "es");
    if (failed > 0) s += " \xC2\xB7 " + std::to_string(failed) + " en \xC3\xA9" "chec";
    if (toValidate > 0) s += " \xC2\xB7 " + std::to_string(toValidate) + " \xC3\xA0 valider";
    return s;
}

// LA REGLE PAR ETAPE (voir le .hpp) : deterministe, tiree des propositions et de leurs operations suivies.
StepProgress stepProgressOf(const Mission& m, const Step& s) {
    StepProgress r;
    r.planned = weightOf(s);
    if (s.state == StepState::Skipped || s.state == StepState::Cancelled) {
        r.percent = -1;
        return r;
    }
    int applied = 0, pending = 0, latest = 0;
    std::vector<int> failedAt;   // l'ordre de la proposition de chaque operation en echec (ou non executee)
    for (const auto& p : m.proposals) {
        if (!countsFor(p) || !serves(p, s)) continue;
        bool any = false;
        for (const auto& l : p.lines) {
            if (!lineOf(p, l, s) || retired(l)) continue;
            any = true;
            if (p.state == "appliquee" && l.state == "appliquee") ++applied;
            else if (awaiting(p)) ++pending;
            else failedAt.push_back(p.order);
        }
        if (any) latest = std::max(latest, p.order);
    }
    // une operation en echec ne compte plus quand une proposition plus recente sert l'etape (la correction la reprend)
    int failed = 0;
    for (const int o : failedAt) failed += o >= latest ? 1 : 0;
    r.applied = applied;
    r.toValidate = pending;
    r.failed = failed;
    if (const int total = applied + pending + failed; total > 0) {
        r.planned = total;
        r.estimated = false;
    }
    if (s.state == StepState::Done) r.percent = 100;
    else r.percent = std::min(99, applied * 100 / std::max(1, r.planned));
    return r;
}

std::string stepProgressRule() {
    return "Par \xC3\xA9tape : ses op\xC3\xA9rations appliqu\xC3\xA9" "es (lues dans le projet apr\xC3\xA8s Appliquer) sur ses op\xC3\xA9rations pr\xC3\xA9vues \xE2\x80\x94 celles de "
           "ses propositions \xC3\xA0 valider, appliqu\xC3\xA9" "es ou en \xC3\xA9" "chec (sans les op\xC3\xA9rations retir\xC3\xA9" "es ; une op\xC3\xA9ration en \xC3\xA9" "chec ne compte "
           "plus quand une proposition plus r\xC3\xA9" "cente reprend l'\xC3\xA9tape), sinon l'estimation du plan. Termin\xC3\xA9" "e (v\xC3\xA9rifi\xC3\xA9" "e dans le "
           "projet) : 100 % ; sinon jamais plus de 99 %. Ignor\xC3\xA9" "e ou annul\xC3\xA9" "e : pas de pourcentage.";
}

std::string elapsedText(long long sec) {
    if (sec < 0) sec = 0;
    char buf[48];
    if (sec >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", sec / 3600, (sec / 60) % 60, sec % 60);
    else std::snprintf(buf, sizeof buf, "%lld:%02lld", sec / 60, sec % 60);
    return buf;
}

StepActivity activityOf(const Mission& m, const Step& s, const Live& live) {
    using S = StepState;
    StepActivity a;
    const auto set = [&a](Activity k, std::string t, bool on = false) {
        a.kind = k;
        a.text = std::move(t);
        a.live = on;
    };
    const std::string nextTag = "P" + std::to_string(m.proposals.size() + 1);   // la proposition que l'IA prepare
    // les etats ou plus rien ne se fait
    if (s.state == S::Cancelled) {
        set(Activity::Off, "Annul\xC3\xA9" "e : rien ne s'applique plus pour elle.");
        return a;
    }
    if (s.state == S::Skipped) {
        set(Activity::Off, forcedOut(m, s.id) ? std::string("Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e) : hors du calcul de la progression.")   // 1.22.3
                                              : std::string("Ignor\xC3\xA9" "e par l'utilisateur : hors du calcul de la progression."));
        return a;
    }
    if (s.state == S::Done) {
        const StepProgress pr = stepProgressOf(m, s);
        const ProposalLink* last = appliedFor(m, s);
        if (last) a.proposal = last->id;
        const std::string when = dayAt(last && !last->appliedAt.empty() ? last->appliedAt : s.updated);
        if (pr.applied > 0)
            set(Activity::Done, "Termin\xC3\xA9" "e \xE2\x80\x94 " + plural(pr.applied, "op\xC3\xA9ration appliqu\xC3\xA9" "e", "op\xC3\xA9rations appliqu\xC3\xA9" "es")
                                    + (when.empty() ? std::string{} : " " + when));
        else
            set(Activity::Done, "Termin\xC3\xA9" "e" + (when.empty() ? std::string{} : " " + when) + " \xE2\x80\x94 v\xC3\xA9rifi\xC3\xA9" "e dans le projet");
        return a;
    }
    // 1.22.3 : la mission est finie (fin forcee) - une etape redevenue non terminee apres coup (Ctrl+Z) n'avance plus
    if (m.state == State::Done && m.forced.on()) {
        set(Activity::Off, "Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : plus rien ne se fait pour elle.");
        return a;
    }
    // l'IA travaille pour elle maintenant (une proposition, une correction)
    if (called(m, s) && live.calling) {
        if (m.pending.purpose == "correction") set(Activity::Working, "L'IA pr\xC3\xA9pare une correction (" + nextTag + ")\xE2\x80\xA6" + since(live), true);
        else set(Activity::Working, "L'IA pr\xC3\xA9pare la proposition " + nextTag + "\xE2\x80\xA6" + since(live), true);
        return a;
    }
    // une de ses propositions s'applique sur un fil de fond
    if (const ProposalLink* p = applyingFor(m, s, live)) {
        a.proposal = p->id;
        set(Activity::Working,
            "Application de " + tag(*p) + "\xE2\x80\xA6" + (live.applyPercent >= 0 ? " " + std::to_string(std::clamp(live.applyPercent, 0, 100)) + " %" : std::string{}),
            true);
        return a;
    }
    const bool paused = m.state == State::Paused;
    switch (s.state) {
        case S::Running:
            if (m.pending.active) set(Activity::Blocked, "Appel \xC3\xA0 l'IA interrompu : rien n'est rejou\xC3\xA9, \xC3\xA0 relancer.");
            else set(Activity::Working, "Une proposition se pr\xC3\xA9pare.");
            break;
        case S::AwaitingValidation:
            if (const ProposalLink* p = awaitingFor(m, s)) {
                a.proposal = p->id;
                const std::string ops = plural(p->ops, "op\xC3\xA9ration", "op\xC3\xA9rations");
                if (p->state == "validee")
                    set(Activity::ToValidate, tag(*p) + " v\xC3\xA9rifi\xC3\xA9" "e : " + ops + (p->ops > 1 ? " pr\xC3\xAAtes" : " pr\xC3\xAAte") + " \xE2\x80\x94 Appliquer");
                else if (!p->errors.empty())
                    set(Activity::Blocked, tag(*p) + " : " + plural(static_cast<int>(p->errors.size()), "erreur", "erreurs") + " \xC3\xA0 la v\xC3\xA9rification \xE2\x80\x94 "
                                               + shortLine(p->errors.front(), 100));
                else
                    set(Activity::ToValidate, tag(*p) + " : " + ops + " \xC3\xA0 valider \xE2\x80\x94 Pr\xC3\xA9visualiser puis Appliquer");
            } else {
                set(Activity::ToValidate, "En attente de validation" + (s.note.empty() ? std::string(".") : " : " + shortLine(s.note)));
            }
            break;
        case S::Applied: {
            const ProposalLink* p = appliedFor(m, s);
            if (p) a.proposal = p->id;
            set(Activity::Working, (p ? "Appliqu\xC3\xA9" "e par " + tag(*p) : std::string("Appliqu\xC3\xA9" "e")) + " : la v\xC3\xA9rification dans le projet suit.");
            break;
        }
        case S::Verifying: set(Activity::Working, "V\xC3\xA9rification sur le projet r\xC3\xA9" "el\xE2\x80\xA6", live.auditing); break;
        case S::Error:
            if (const std::string why = shortLine(s.error.empty() ? s.note : s.error); !why.empty()) set(Activity::Error, "En erreur : " + why);
            else set(Activity::Error, "En erreur.");
            break;
        case S::Blocked: {
            std::vector<std::string> by;
            for (const auto& d : s.deps)
                if (const Step* dep = m.step(d); dep && isBlocking(dep->state)) by.push_back(d + " (" + lowerFirst(std::string(stepLabel(dep->state))) + ")");
            if (!by.empty()) set(Activity::Blocked, "Bloqu\xC3\xA9" "e : d\xC3\xA9pend de " + join(by, ", "));
            else set(Activity::Blocked, "Bloqu\xC3\xA9" "e" + (s.note.empty() ? std::string(".") : " : " + shortLine(s.note)));
            break;
        }
        case S::Waiting: {
            std::vector<std::string> by;
            for (const auto& d : s.deps)
                if (const Step* dep = m.step(d); dep && !isFinished(dep->state)) by.push_back(d);
            if (!by.empty()) set(Activity::Waiting, "Attend " + join(by, ", ") + (by.size() > 1 ? " (d\xC3\xA9pendances)" : " (d\xC3\xA9pendance)"));
            else set(Activity::Waiting, "En attente" + (s.note.empty() ? std::string(".") : " : " + shortLine(s.note)));
            break;
        }
        case S::Ready:
            if (paused) set(Activity::Waiting, "Pr\xC3\xAAte \xE2\x80\x94 la mission est en pause : reprends-la d'abord.");
            else if (m.coherence == Coherence::Missing) set(Activity::Waiting, "Pr\xC3\xAAte \xE2\x80\x94 ouvre le projet de la mission pour continuer.");
            else set(Activity::Idle, "Pr\xC3\xAAte : \xC2\xAB Proposer la suite \xC2\xBB la demande \xC3\xA0 l'IA.");
            return a;
        case S::ToPlan: set(Activity::Waiting, "\xC3\x80 planifier : le plan n'est pas encore fait."); break;
        default: break;
    }
    if (paused && a.kind != Activity::Error && a.kind != Activity::Blocked) {
        a.text = "Mission en pause \xE2\x80\x94 " + a.text;
        a.kind = Activity::Waiting;
        a.live = false;
    }
    return a;
}

const Step* currentStep(const Mission& m, const Live& live) {
    using S = StepState;
    const auto rank = [&](const Step& s) -> int {
        if (isFinished(s.state) || s.state == S::Cancelled) return 99;
        if ((called(m, s) && live.calling) || applyingFor(m, s, live)) return 0;
        switch (s.state) {
            case S::AwaitingValidation: return 1;
            case S::Applied:
            case S::Verifying: return 2;
            case S::Running: return 3;
            case S::Error: return 4;
            case S::Ready: return 5;
            case S::Blocked: return 6;
            default: return 7;   // en attente, a planifier
        }
    };
    const Step* best = nullptr;
    int bestRank = 99;
    for (const auto& s : m.steps)
        if (const int r = rank(s); r < bestRank) {
            best = &s;
            bestRank = r;
        }
    return best;
}

std::string missionActivity(const Mission& m, const Live& live) {
    if (live.auditing) return "Audit final en cours (sur une copie du projet)\xE2\x80\xA6";
    if (m.state == State::Done && m.forced.on()) {   // 1.22.3
        const std::size_t n = m.forced.steps.size();
        return "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)" + (n == 0 ? std::string(".") : " : " + ignoredCount(n) + " (" + join(m.forced.steps, ", ") + ").");
    }
    if (!m.pending.active) return {};
    const std::string& purpose = m.pending.purpose;
    if (!live.calling)
        return "Appel \xC3\xA0 l'IA interrompu (" + (purpose.empty() ? std::string("proposition") : purpose) + ") : rien n'est rejou\xC3\xA9.";
    if (purpose == "plan") return "L'IA analyse le projet et pr\xC3\xA9pare le plan\xE2\x80\xA6" + since(live);
    if (purpose == "actualisation") return "L'IA relit le projet et revoit le plan\xE2\x80\xA6" + since(live);
    const std::string steps = m.pending.steps.empty() ? std::string{} : " (" + join(m.pending.steps, ", ") + ")";
    if (purpose == "correction") return "L'IA pr\xC3\xA9pare une correction" + steps + "\xE2\x80\xA6" + since(live);
    return "L'IA pr\xC3\xA9pare la proposition P" + std::to_string(m.proposals.size() + 1) + steps + "\xE2\x80\xA6" + since(live);
}

std::vector<JournalLine> stepJournal(const Mission& m, const Step& s) {
    std::vector<JournalLine> out;
    std::vector<const ProposalLink*> mine;
    for (const auto& p : m.proposals)
        if (serves(p, s)) mine.push_back(&p);
    const auto mineOf = [&mine](std::string_view id) -> const ProposalLink* {
        for (const auto* p : mine)
            if (p->id == id) return p;
        return nullptr;
    };
    // ses operations dans une proposition appliquee : celles appliquees (le libelle lisible), celles en echec (l'erreur)
    const auto operations = [&](const ProposalLink& p, const std::string& date) {
        for (const auto& l : p.lines) {
            if (!lineOf(p, l, s)) continue;
            const std::string title = l.title.empty() ? l.id : l.title;
            if (l.state == "appliquee") out.push_back({date, "operation", "\xE2\x9C\x93 " + title, p.id});
            else if (l.state == "echouee") out.push_back({date, "echec", "\xC3\x89" "chec : " + title + (l.error.empty() ? std::string{} : " \xE2\x80\x94 " + l.error), p.id});
        }
    };
    std::set<std::string> generated, applied;
    for (const auto& e : m.history) {
        const ProposalLink* p = e.proposal.empty() ? nullptr : mineOf(e.proposal);
        // le sien : son identifiant, une proposition qui la sert, ou un texte qui la cite sans viser une autre etape
        const bool related = e.step == s.id || p != nullptr || (e.step.empty() && e.proposal.empty() && cites(e.text, s.id));
        if (!related) continue;
        out.push_back({e.date, e.kind, e.text, e.proposal});
        if (!p) continue;
        if (e.kind == "proposition" && e.text.rfind(tag(*p) + " \xC2\xAB", 0) == 0) generated.insert(p->id);
        if (e.kind == "application" && applied.insert(p->id).second) operations(*p, e.date);
    }
    // l'historique est borne : ce qu'il a perdu, repris de ce que la mission garde de ses propositions
    for (const auto* p : mine) {
        if (!generated.count(p->id) && !p->created.empty())
            out.push_back({p->created, "proposition",
                           tag(*p) + " " + guill(p->title.empty() ? std::string("Proposition") : p->title) + " : " + plural(p->ops, "op\xC3\xA9ration", "op\xC3\xA9rations") + ".",
                           p->id});
        if (!applied.count(p->id) && !p->appliedAt.empty()) {
            out.push_back({p->appliedAt, "application",
                           tag(*p) + " appliqu\xC3\xA9" "e : " + plural(p->applied, "op\xC3\xA9ration appliqu\xC3\xA9" "e", "op\xC3\xA9rations appliqu\xC3\xA9" "es") + ", "
                               + std::to_string(p->failed) + " en \xC3\xA9" "chec.",
                           p->id});
            operations(*p, p->appliedAt);
        }
    }
    for (const auto& d : m.decisions)
        if (cites(d.question, s.id) || cites(d.answer, s.id)) out.push_back({d.date, "decision", d.question + " \xE2\x86\x92 " + d.answer, {}});
    std::stable_sort(out.begin(), out.end(), [](const JournalLine& a, const JournalLine& b) { return a.date < b.date; });
    return out;
}

// ---- les textes ------------------------------------------------------------------------------------------------------
std::string nextAction(const Mission& m) {
    if (m.state == State::Cancelled) return "Mission annul\xC3\xA9" "e : consulter son historique.";
    if (m.coherence == Coherence::Missing) return "Ouvrir le projet de la mission (" + (m.project.name.empty() ? std::string("inconnu") : m.project.name) + ").";
    if (m.pending.active) return "L'IA travaille (" + m.pending.purpose + ") : attendre sa r\xC3\xA9ponse.";
    if (m.state == State::Paused) return "Reprendre la mission.";
    if (m.state == State::Planning) return "\xC3\x89tablir le plan : l'IA analyse le projet.";
    if (m.state == State::Done && m.forced.on())   // 1.22.3
        return m.finalReport.isObject() ? std::string("Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : consulter le rapport final.")
                                        : std::string("Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : lancer l'audit final pour son rapport.");
    if (m.state == State::Done) return "Mission termin\xC3\xA9" "e : consulter le rapport final.";
    if (m.coherence == Coherence::Changed) return "Actualiser le contexte : le projet a chang\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde.";
    for (const auto& s : m.steps)
        if (s.state == StepState::Error) return "Corriger " + s.id + " " + guill(s.title) + " : " + (s.error.empty() ? s.note : s.error);
    for (const auto& p : m.proposals)
        if (p.state == "generee" || p.state == "validee") return "Pr\xC3\xA9visualiser et appliquer P" + std::to_string(p.order) + " " + guill(p.title) + ".";
    const Batch b = nextBatch(m, 1 << 20);
    if (!b.steps.empty()) {
        std::vector<std::string> named;
        for (const auto& id : b.steps)
            if (const Step* s = m.step(id); s && named.size() < 3) named.push_back(id + " " + s->title);
        return "Proposer la suite : " + join(named, ", ") + (b.steps.size() > 3 ? " (+" + std::to_string(b.steps.size() - 3) + ")" : std::string{}) + ".";
    }
    bool allDone = true;
    for (const auto& s : m.steps)
        if (s.mandatory && !isFinished(s.state) && s.state != StepState::Cancelled) allDone = false;
    if (allDone && !m.finalReport.isObject()) return "Lancer l'audit final de la mission.";
    if (allDone) return "Consulter le rapport final (des points restent \xC3\xA0 v\xC3\xA9rifier).";
    return b.why;
}

std::string summaryOf(const Mission& m) {
    const Progress p = progressOf(m);
    std::string s = stateText(m) + " \xC2\xB7 " + p.text();
    if (const ProposalLink* l = lastApplied(m)) s += " \xC2\xB7 Derni\xC3\xA8re appliqu\xC3\xA9" "e : P" + std::to_string(l->order);
    s += " \xC2\xB7 Prochaine action : " + nextAction(m);
    return s;
}

std::string planningPrompt(const Mission& m) {
    std::string s;
    s += "MISSION IA " + guill(m.name) + " (" + m.id + ") \xE2\x80\x94 PLANIFICATION.\n";
    s += "Demande initiale exacte : " + guill(m.request) + "\n";
    s += "Avant toute op\xC3\xA9ration, ANALYSE LE PROJET R\xC3\x89" "EL (le contexte ci-dessus, et l'outil analyser_projet s'il est disponible) : "
         "ce qui existe d\xC3\xA9j\xC3\xA0 et est conforme, ce qui existe et est \xC3\xA0 modifier ou \xC3\xA0 compl\xC3\xA9ter, ce qui est \xC3\xA0 cr\xC3\xA9" "er, ce qui est "
         "impossible avec les fonctionnalit\xC3\xA9s du logiciel. Ne recr\xC3\xA9" "e jamais un \xC3\xA9l\xC3\xA9ment qui existe d\xC3\xA9j\xC3\xA0 : il sera r\xC3\xA9utilis\xC3\xA9.\n";
    s += "Puis appelle l'outil planifier_mission : un nom court, une description, l'analyse, et les \xC3\xA9tapes (titre, d\xC3\xA9tail, cat\xC3\xA9gorie, "
         "d\xC3\xA9pendances par identifiant, estimation du nombre d'op\xC3\xA9rations, obligatoire, ce qu'elle produira en genre:nom). L'ordre "
         "d'ex\xC3\xA9" "cution sera calcul\xC3\xA9 par le logiciel \xC3\xA0 partir des d\xC3\xA9pendances, pas de l'ordre o\xC3\xB9 tu les \xC3\xA9" "cris. Une \xC3\xA9tape peut d\xC3\xA9pendre "
         "de plusieurs autres. Ne propose AUCUNE op\xC3\xA9ration maintenant (pas de proposer_operations) : les propositions viendront "
         "ensuite, \xC3\xA9tape par \xC3\xA9tape.\n";
    if (!m.steps.empty()) {
        s += "PLAN ACTUEL (r\xC3\xA9vision " + std::to_string(m.planRevision) + ") \xE2\x80\x94 garde les identifiants ; les \xC3\xA9tapes termin\xC3\xA9" "es sont gard\xC3\xA9" "es "
             "telles quelles, ne les replanifie pas :\n";
        for (const auto& st : m.steps)
            s += "- " + st.id + " [" + std::string(stepLabel(st.state)) + "] " + st.title + (st.deps.empty() ? std::string{} : " (d\xC3\xA9pend de " + join(st.deps, ", ") + ")") + "\n";
    }
    return s;
}

std::string batchPrompt(const Mission& m, const hmi::Project& project, const Batch& b, std::string_view changes) {
    Finder finder(project);
    std::string s;
    const int order = static_cast<int>(m.proposals.size()) + 1;
    s += "MISSION IA " + guill(m.name) + " (" + m.id + ") \xE2\x80\x94 PROPOSITION P" + std::to_string(order) + ".\n";
    s += "Demande initiale : " + guill(m.request) + "\n";
    s += "PLAN (dans l'ordre d'ex\xC3\xA9" "cution, avec l'\xC3\xA9tat r\xC3\xA9" "el) :\n";
    for (const auto& st : m.steps)
        s += "- " + st.id + " [" + std::string(stepLabel(st.state)) + "] " + st.title + (st.deps.empty() ? std::string{} : " (d\xC3\xA9pend de " + join(st.deps, ", ") + ")") + "\n";
    // Ce qui est reellement dans le projet : les elements crees par les propositions precedentes, verifies a l'instant.
    std::vector<std::string> present, absent;
    for (const auto& r : createdRefs(m)) (finder.exists(r) ? present : absent).push_back(refText(r));
    s += "CE QUI EST R\xC3\x89" "ELLEMENT DANS LE PROJET (cr\xC3\xA9\xC3\xA9 par les propositions pr\xC3\xA9" "c\xC3\xA9" "dentes de la mission, v\xC3\xA9rifi\xC3\xA9 \xC3\xA0 l'instant) : "
         + (present.empty() ? std::string("rien encore") : join(present, ", ")) + ".\n";
    if (!absent.empty()) s += "N'Y EST PLUS (ne pas supposer qu'il existe) : " + join(absent, ", ") + ".\n";
    s += "CE QUI A CHANG\xC3\x89 dans le projet depuis la derni\xC3\xA8re proposition : " + (changes.empty() ? std::string("rien") : std::string(changes)) + ".\n";
    s += "\xC3\x89TAPES \xC3\x80 COUVRIR MAINTENANT :\n";
    for (const auto& id : b.steps)
        if (const Step* st = m.step(id)) {
            s += "- " + st->id + " " + st->title + (st->detail.empty() ? std::string{} : " \xE2\x80\x94 " + st->detail);
            if (!st->expected.empty()) s += " (produit : " + join(st->expected, ", ") + ")";
            s += "\n";
        }
    s += "R\xC3\x88GLES : une seule proposition (proposer_operations) qui couvre ces \xC3\xA9tapes et seulement elles ; chaque op\xC3\xA9ration porte "
         "\"etape\" (l'identifiant de l'\xC3\xA9tape qu'elle sert) ; r\xC3\xA9utilise les noms EXACTS des \xC3\xA9l\xC3\xA9ments pr\xC3\xA9sents, ne recr\xC3\xA9" "e rien de ce qui "
         "est list\xC3\xA9 comme pr\xC3\xA9sent ; si c'est trop long pour une r\xC3\xA9ponse, couvre une partie compl\xC3\xA8te et dis le reste dans \"suite\" ; "
         "\"suite\" cite aussi les prochaines \xC3\xA9tapes du plan. Le logiciel v\xC3\xA9rifiera la proposition sur une copie avant de la montrer.\n";
    return s;
}

std::string correctionPrompt(const Mission& m, const hmi::Project& project, std::string_view stepId) {
    (void)project;
    const Step* st = m.step(stepId);
    std::string s;
    s += "MISSION IA " + guill(m.name) + " (" + m.id + ") \xE2\x80\x94 CORRECTION de l'\xC3\xA9tape " + std::string(stepId);
    if (st) s += " " + guill(st->title);
    s += ".\n";
    if (!st) return s + "\xC3\x89tape inconnue.\n";
    s += "Erreur exacte : " + (st->error.empty() ? st->note : st->error) + "\n";
    std::vector<std::string> ops;
    for (const auto& pid : st->proposals)
        if (const ProposalLink* l = m.proposal(pid))
            for (const auto& line : l->lines)
                if ((line.step == stepId || line.step.empty()) && line.state == "echouee")
                    ops.push_back("P" + std::to_string(l->order) + " " + line.id + " " + line.title + (line.error.empty() ? std::string{} : " : " + line.error));
    s += "Op\xC3\xA9rations touch\xC3\xA9" "es : " + (ops.empty() ? std::string("aucune op\xC3\xA9ration \xC3\xA9" "chou\xC3\xA9" "e enregistr\xC3\xA9" "e") : join(ops, " ; ")) + ".\n";
    std::vector<std::string> deps, users;
    for (const auto& d : st->deps)
        if (const Step* x = m.step(d)) deps.push_back(d + " (" + std::string(stepLabel(x->state)) + ")");
    for (const auto& o : m.steps)
        if (std::find(o.deps.begin(), o.deps.end(), st->id) != o.deps.end()) users.push_back(o.id + " (" + std::string(stepLabel(o.state)) + ")");
    s += "Ses d\xC3\xA9pendances : " + (deps.empty() ? std::string("aucune") : join(deps, ", ")) + ". Les \xC3\xA9tapes qui l'attendent : "
         + (users.empty() ? std::string("aucune") : join(users, ", ")) + ".\n";
    s += "Propose UNE proposition de correction (proposer_operations, chaque op\xC3\xA9ration avec \"etape\": \"" + st->id
         + "\") qui corrige la cause ; ne modifie pas en silence ce qui est d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9 ; r\xC3\xA9utilise les \xC3\xA9l\xC3\xA9ments existants. Elle sera "
           "v\xC3\xA9rifi\xC3\xA9" "e sur une copie puis pr\xC3\xA9sent\xC3\xA9" "e \xC3\xA0 l'utilisateur.\n";
    return s;
}

std::string overviewPrompt(const std::vector<Mission>& missions, std::string_view projectId) {
    std::vector<const Mission*> mine;
    for (const auto& m : missions) {
        if (m.archived) continue;
        if (!projectId.empty() && !m.project.id.empty() && m.project.id != projectId) continue;
        mine.push_back(&m);
    }
    if (mine.empty()) return {};
    std::string s = "MISSIONS IA DE CE PROJET (donn\xC3\xA9" "es enregistr\xC3\xA9" "es par le logiciel ; le d\xC3\xA9tail : l'outil etat_mission ; chercher : "
                    "chercher_missions ; n'invente jamais leur \xC3\xA9tat) :\n";
    std::size_t n = 0;
    for (const auto* m : mine) {
        if (++n > 6) {
            s += "- ... et " + std::to_string(mine.size() - 6) + " autre(s).\n";
            break;
        }
        const Progress p = progressOf(*m);
        s += "- " + guill(m->name) + " (" + m->id + ") : " + std::string(stateLabel(m->state)) + ", " + p.text() + " ; prochaine action : " + nextAction(*m) + "\n";
    }
    return s;
}

std::string detailText(const Mission& m) {
    const Progress p = progressOf(m);
    std::string s;
    s += "Mission " + guill(m.name) + " (" + m.id + ")\n";
    s += "\xC3\x89tat : " + stateText(m) + (m.archived ? " (archiv\xC3\xA9" "e)" : "") + " \xC2\xB7 " + p.text() + "\n";
    if (m.forced.on())   // 1.22.3
        s += "Fin forc\xC3\xA9" "e le " + m.forced.date + " (" + m.forced.reason + ") : "
             + (m.forced.steps.empty() ? std::string("aucune \xC3\xA9tape ignor\xC3\xA9" "e") : ignoredCount(m.forced.steps.size()) + " (" + join(m.forced.steps, ", ") + ")")
             + (m.forced.proposals.empty() ? std::string{} : ", " + std::to_string(m.forced.proposals.size()) + " proposition(s) refus\xC3\xA9" "e(s) sans \xC3\xAAtre appliqu\xC3\xA9" "e(s)")
             + ". Rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "fait dans le projet.\n";
    s += "Progression : planification " + percentText(p.planning) + ", g\xC3\xA9n\xC3\xA9ration " + percentText(p.generation) + ", validation "
         + percentText(p.validation) + ", application " + percentText(p.application) + ", globale " + percentText(p.global) + ".\n";
    s += "Projet : " + (m.project.name.empty() ? std::string("?") : m.project.name) + " \xE2\x80\x94 " + std::string(coherenceLabel(m.coherence))
         + (m.coherenceText.empty() ? std::string{} : " (" + m.coherenceText + ")") + "\n";
    s += "Cr\xC3\xA9\xC3\xA9" "e le " + m.created + ", derni\xC3\xA8re activit\xC3\xA9 le " + m.modified + ".\n";
    s += "Demande initiale : " + guill(m.request) + "\n";
    if (!m.description.empty()) s += "Description : " + m.description + "\n";
    if (!m.analysis.summary.empty()) s += "Analyse : " + m.analysis.summary + "\n";
    if (!m.analysis.impossible.empty()) s += "Impossible : " + join(m.analysis.impossible, " ; ") + "\n";
    s += "\xC3\x89tapes :\n";
    for (const auto& st : m.steps) {
        s += "- " + st.id + " [" + stepText(m, st) + "] " + st.title;
        if (!st.deps.empty()) s += " (d\xC3\xA9pend de " + join(st.deps, ", ") + ")";
        if (!st.proposals.empty()) {
            std::vector<std::string> ps;
            for (const auto& pid : st.proposals)
                if (const ProposalLink* l = m.proposal(pid)) ps.push_back("P" + std::to_string(l->order));
            s += " \xE2\x80\x94 " + join(ps, ", ");
        }
        if (!st.error.empty() && st.state == StepState::Error) s += " \xE2\x80\x94 erreur : " + st.error;
        else if (!st.note.empty()) s += " \xE2\x80\x94 " + st.note;
        s += "\n";
    }
    if (!m.proposals.empty()) {
        s += "Propositions :\n";
        for (const auto& l : m.proposals) {
            s += "- P" + std::to_string(l.order) + " " + guill(l.title) + " : " + l.state + ", " + std::to_string(l.ops) + " op\xC3\xA9ration(s)";
            if (l.state == "appliquee") s += " (" + std::to_string(l.applied) + " appliqu\xC3\xA9" "e(s), " + std::to_string(l.failed) + " \xC3\xA9" "chou\xC3\xA9" "e(s))";
            if (!l.steps.empty()) s += " pour " + join(l.steps, ", ");
            s += "\n";
        }
    }
    const auto created = createdRefs(m);
    if (!created.empty()) {
        std::vector<std::string> names;
        for (const auto& r : created) names.push_back(refText(r));
        s += "Cr\xC3\xA9\xC3\xA9 par la mission : " + join(names, ", ") + "\n";
    }
    std::vector<std::string> errs;
    for (const auto& e : m.history)
        if (e.kind == "erreur" || (e.kind == "etape" && contains(e.text, "En erreur"))) errs.push_back(e.date + " " + e.text);
    if (!errs.empty()) s += "Erreurs rencontr\xC3\xA9" "es :\n- " + join(errs, "\n- ") + "\n";
    s += "Prochaine action : " + nextAction(m) + "\n";
    s += "Derni\xC3\xA8re activit\xC3\xA9 : ";
    for (std::size_t i = m.history.size() > 6 ? m.history.size() - 6 : 0; i < m.history.size(); ++i) s += "\n  " + m.history[i].date + " " + m.history[i].text;
    s += "\n";
    if (m.finalReport.isObject()) s += "Rapport final :\n" + reportText(reportFromJson(m.finalReport));
    return s;
}

std::vector<std::size_t> search(const std::vector<Mission>& missions, std::string_view query, bool withArchived) {
    std::vector<std::string> words;
    {
        std::istringstream in(folded(query));
        std::string w;
        static const std::set<std::string> stop = {"la", "le", "les", "de", "des", "du", "un", "une", "mission", "missions", "reprends",
                                                   "reprendre", "ou", "en", "est", "quelles", "quels", "quel", "quelle", "d'hier", "et",
                                                   "a", "au", "aux", "l", "d"};
        while (in >> w) {
            while (!w.empty() && !std::isalnum(static_cast<unsigned char>(w.back())) && w.back() != '_') w.pop_back();
            while (!w.empty() && !std::isalnum(static_cast<unsigned char>(w.front())) && w.front() != '_') w.erase(0, 1);
            if (w.size() >= 2 && !stop.count(w)) words.push_back(w);
        }
    }
    struct Hit {
        std::size_t index;
        int         score;
    };
    std::vector<Hit> hits;
    for (std::size_t i = 0; i < missions.size(); ++i) {
        const Mission& m = missions[i];
        if (m.archived && !withArchived) continue;
        int score = words.empty() ? 1 : 0;
        const std::string name = folded(m.name), req = folded(m.request), id = folded(m.id);
        std::string steps;
        for (const auto& s : m.steps) steps += folded(s.title) + " " + folded(s.detail) + " ";
        std::string made;
        for (const auto& r : createdRefs(m)) made += folded(r) + " ";
        for (const auto& w : words) {
            if (id == w) score += 20;
            if (contains(name, w)) score += 6;
            if (contains(req, w)) score += 3;
            if (contains(steps, w)) score += 2;
            if (contains(made, w)) score += 3;
        }
        if (score > 0) hits.push_back({i, score});
    }
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
        if (a.score != b.score) return a.score > b.score;
        return missions[a.index].modified > missions[b.index].modified;
    });
    std::vector<std::size_t> out;
    for (const auto& h : hits) out.push_back(h.index);
    return out;
}

// ---- le rapport final -------------------------------------------------------------------------------------------------
FinalReport finalReport(const Mission& m, const hmi::Project& project, const hmi::audit::Report* audit, std::string now) {
    FinalReport r;
    r.date = now;
    Finder finder(project);
    std::set<std::string> seen;
    for (const auto& ref : createdRefs(m)) {
        const std::string t = refText(ref);
        if (!seen.insert(t).second) continue;
        if (finder.exists(ref)) r.created.push_back(t);
        else r.toCheck.push_back(t + " : cr\xC3\xA9\xC3\xA9 par la mission, n'est plus dans le projet.");
    }
    std::set<std::string> mod, reu;
    for (const auto& l : m.proposals) {
        if (l.state != "appliquee" || !l.report.isObject()) continue;
        const plan::Report rep = plan::reportFromJson(l.report);
        for (const auto& line : rep.modified)
            for (const auto& n : line.names)
                if (mod.insert(line.category + " " + n).second) r.modified.push_back(line.category + " " + n);
        for (const auto& line : rep.reused)
            for (const auto& n : line.names)
                if (reu.insert(line.category + " " + n).second) r.reused.push_back(line.category + " " + n);
    }
    bool mandatoryOpen = false, blockedOrError = false;
    std::vector<std::string> skippedIds;   // 1.22.3 : les etapes ignorees par la fin forcee
    for (const auto& s : m.steps) {
        if (s.state == StepState::Done) r.finished.push_back(s.id + " " + s.title);
        else if (s.state == StepState::Skipped && forcedOut(m, s.id)) {
            r.skipped.push_back(s.id + " " + s.title);
            skippedIds.push_back(s.id);
        } else if (s.state == StepState::Skipped) r.finished.push_back(s.id + " " + s.title + " (ignor\xC3\xA9" "e)");
        else if (s.state != StepState::Cancelled) {
            r.notDone.push_back(s.id + " " + s.title + " \xE2\x80\x94 " + std::string(stepLabel(s.state)) + (s.note.empty() ? std::string{} : " : " + s.note));
            if (s.mandatory) mandatoryOpen = true;
        }
        if (s.state == StepState::Error || s.state == StepState::Blocked) blockedOrError = true;
        if (!s.error.empty())
            r.errors.push_back(s.id + " " + s.title + " : " + s.error + (s.state == StepState::Done ? " (corrig\xC3\xA9" "e)" : " (" + std::string(stepLabel(s.state)) + ")"));
    }
    for (const auto& x : m.analysis.impossible) r.notDone.push_back("Impossible avec le logiciel : " + x);
    Finder f2(project);
    for (const auto& c : m.analysis.conform)
        if (partsOf(c).kind.size() && !f2.exists(c)) r.toCheck.push_back("L'analyse disait conforme : " + refText(c) + ", absent du projet.");
    if (audit) {
        r.auditRun = true;
        r.auditErrors = static_cast<int>(audit->errors);
        r.auditWarnings = static_cast<int>(audit->warnings);
        std::size_t shown = 0;
        for (const auto& f : audit->findings) {
            if (f.severity == hmi::audit::Severity::Error) r.errors.push_back("Audit : " + f.message + (f.where.empty() ? std::string{} : " (" + f.where + ")"));
            else if (f.severity == hmi::audit::Severity::Warning && shown < 25) {
                r.toCheck.push_back("Audit : " + f.message + (f.where.empty() ? std::string{} : " (" + f.where + ")"));
                ++shown;
            }
        }
        if (audit->warnings > shown) r.toCheck.push_back("... et " + std::to_string(audit->warnings - shown) + " autre(s) avertissement(s) de l'audit.");
    } else {
        r.toCheck.push_back("L'audit global du projet n'a pas pu \xC3\xAAtre fait : la coh\xC3\xA9rence n'est PAS v\xC3\xA9rifi\xC3\xA9" "e (v\xC3\xA9rification impossible).");
    }
    bool equipments = false;
    for (const auto& ref : createdRefs(m)) equipments = equipments || folded(partsOf(ref).kind) == "equipement";
    if (equipments) r.toCheck.push_back("La communication r\xC3\xA9" "elle avec les \xC3\xA9quipements n'est pas v\xC3\xA9rifiable sans l'installation : \xC3\xA0 essayer sur site.");
    // 1.22.3 : une fin forcee - jamais << complete >> si elle a ignore une etape ; ce qui reste a faire est dit
    r.forced = m.forced.on();
    if (!r.skipped.empty())
        r.toCheck.push_back("Fin forc\xC3\xA9" "e : " + join(skippedIds, ", ") + (skippedIds.size() > 1 ? " ne sont pas faites" : " n'est pas faite")
                            + " (ignor\xC3\xA9" "e" + (skippedIds.size() > 1 ? "s" : "") + ") \xE2\x80\x94 \xC3\xA0 reprendre dans une nouvelle mission si besoin.");
    r.complete = !mandatoryOpen && !blockedOrError && r.auditRun && r.auditErrors == 0 && r.skipped.empty();
    if (r.forced) {
        std::string v = "Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e par l'utilisateur) : "
                        + (skippedIds.empty() ? std::string("aucune \xC3\xA9tape ignor\xC3\xA9" "e") : ignoredCount(skippedIds.size()) + " (" + join(skippedIds, ", ") + ")")
                        + ", " + std::to_string(r.finished.size()) + " termin\xC3\xA9" "e(s), " + std::to_string(r.created.size()) + " \xC3\xA9l\xC3\xA9ment(s) cr\xC3\xA9\xC3\xA9(s)";
        if (!r.auditRun) v += " ; audit non fait (v\xC3\xA9rification impossible).";
        else v += " ; audit : " + std::to_string(r.auditErrors) + " erreur(s) bloquante(s), " + std::to_string(r.auditWarnings) + " avertissement(s).";
        r.verdict = std::move(v);
    } else if (r.complete)
        r.verdict = "Mission termin\xC3\xA9" "e : " + std::to_string(r.finished.size()) + " \xC3\xA9tape(s), " + std::to_string(r.created.size()) + " \xC3\xA9l\xC3\xA9ment(s) cr\xC3\xA9\xC3\xA9(s), "
                    + "audit sans erreur bloquante (" + std::to_string(r.auditWarnings) + " avertissement(s)).";
    else {
        std::vector<std::string> why;
        if (mandatoryOpen) why.push_back("des \xC3\xA9tapes obligatoires ne sont pas termin\xC3\xA9" "es");
        if (blockedOrError) why.push_back("des \xC3\xA9tapes sont en erreur ou bloqu\xC3\xA9" "es");
        if (!r.auditRun) why.push_back("l'audit n'a pas pu \xC3\xAAtre fait");
        else if (r.auditErrors > 0) why.push_back(std::to_string(r.auditErrors) + " erreur(s) bloquante(s) \xC3\xA0 l'audit");
        r.verdict = "Mission non termin\xC3\xA9" "e : " + join(why, ", ") + ".";
    }
    return r;
}

Value reportToJson(const FinalReport& r) {
    Value o = Value::object();
    o.set("date", r.date);
    o.set("crees", strings(r.created));
    o.set("modifies", strings(r.modified));
    o.set("reutilises", strings(r.reused));
    o.set("terminees", strings(r.finished));
    o.set("non_realises", strings(r.notDone));
    o.set("erreurs", strings(r.errors));
    o.set("a_verifier", strings(r.toCheck));
    o.set("audit_fait", r.auditRun);
    o.set("audit_erreurs", r.auditErrors);
    o.set("audit_avertissements", r.auditWarnings);
    o.set("complete", r.complete);
    o.set("verdict", r.verdict);
    if (r.forced) {   // 1.22.3 : seulement apres une fin forcee (les autres rapports restent tels quels)
        o.set("fin_forcee", true);
        o.set("ignorees_fin_forcee", strings(r.skipped));
    }
    return o;
}

FinalReport reportFromJson(const Value& o) {
    FinalReport r;
    r.date = o.str("date");
    r.created = stringsOf(o.at("crees"));
    r.modified = stringsOf(o.at("modifies"));
    r.reused = stringsOf(o.at("reutilises"));
    r.finished = stringsOf(o.at("terminees"));
    r.notDone = stringsOf(o.at("non_realises"));
    r.errors = stringsOf(o.at("erreurs"));
    r.toCheck = stringsOf(o.at("a_verifier"));
    r.auditRun = o.flag("audit_fait");
    r.auditErrors = static_cast<int>(o.integer("audit_erreurs"));
    r.auditWarnings = static_cast<int>(o.integer("audit_avertissements"));
    r.complete = o.flag("complete");
    r.verdict = o.str("verdict");
    r.forced = o.flag("fin_forcee");
    r.skipped = stringsOf(o.at("ignorees_fin_forcee"));
    return r;
}

std::string reportText(const FinalReport& r) {
    std::string s;
    const auto section = [&](const char* title, const std::vector<std::string>& v) {
        s += std::string("**") + title + "** (" + std::to_string(v.size()) + ")\n";
        if (v.empty()) s += "- aucun\n";
        for (const auto& x : v) s += "- " + x + "\n";
    };
    if (r.forced) s += "**Fin forc\xC3\xA9" "e par l'utilisateur** : rien n'a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "fait dans le projet.\n";   // 1.22.3
    section("Cr\xC3\xA9\xC3\xA9s", r.created);
    section("Modifi\xC3\xA9s", r.modified);
    section("R\xC3\xA9utilis\xC3\xA9s", r.reused);
    section("Termin\xC3\xA9s", r.finished);
    if (r.forced) section("Ignor\xC3\xA9" "es (fin forc\xC3\xA9" "e)", r.skipped);
    section("Non r\xC3\xA9" "alis\xC3\xA9s", r.notDone);
    section("Erreurs", r.errors);
    section("\xC3\x80 v\xC3\xA9rifier", r.toCheck);
    s += r.auditRun ? "Audit global : " + std::to_string(r.auditErrors) + " erreur(s) bloquante(s), " + std::to_string(r.auditWarnings) + " avertissement(s).\n"
                    : std::string("Audit global : non fait (v\xC3\xA9rification impossible).\n");
    s += r.verdict + "\n";
    return s;
}

// ---- les outils de recherche ------------------------------------------------------------------------------------------
std::string searchToolJson() {
    Value tool = Value::object();
    tool.set("name", std::string(kSearchTool));
    tool.set("description",
             "Cherche dans les MISSIONS IA enregistr\xC3\xA9" "es (toutes les conversations) : par leur nom, leur demande, leurs \xC3\xA9tapes, les "
             "\xC3\xA9l\xC3\xA9ments cr\xC3\xA9\xC3\xA9s. Rend pour chacune son identifiant, son \xC3\xA9tat r\xC3\xA9" "el, sa progression et sa prochaine action. \xC3\x80 utiliser pour "
             "\xC2\xAB o\xC3\xB9 en est la mission... \xC2\xBB, \xC2\xAB reprends la mission d'hier \xC2\xBB. N'invente jamais l'\xC3\xA9tat d'une mission.");
    Value props = Value::object();
    Value q = Value::object();
    q.set("type", "string");
    q.set("description", "Les mots \xC3\xA0 chercher (station pompage) ; vide : les missions les plus r\xC3\xA9" "centes.");
    props.set("texte", q);
    Value schema = Value::object();
    schema.set("type", "object");
    schema.set("properties", props);
    tool.set("input_schema", schema);
    return json::write(tool, false);
}

std::string detailToolJson() {
    Value tool = Value::object();
    tool.set("name", std::string(kDetailTool));
    tool.set("description",
             "La fiche compl\xC3\xA8te d'une mission IA enregistr\xC3\xA9" "e (par son identifiant ou son nom) : \xC3\xA9tat, \xC3\xA9tapes et leurs \xC3\xA9tats, "
             "propositions, ce qui a \xC3\xA9t\xC3\xA9 r\xC3\xA9" "ellement cr\xC3\xA9\xC3\xA9, erreurs, prochaine action, rapport final.");
    Value props = Value::object();
    Value q = Value::object();
    q.set("type", "string");
    q.set("description", "L'identifiant (mission-...) ou le nom de la mission.");
    props.set("mission", q);
    Value schema = Value::object();
    schema.set("type", "object");
    schema.set("properties", props);
    Value req = Value::array();
    req.push("mission");
    schema.set("required", req);
    tool.set("input_schema", schema);
    return json::write(tool, false);
}

std::string runSearchTool(const std::vector<Mission>& missions, std::string_view inputJson, std::string_view projectId) {
    Value v;
    std::string text;
    if (json::parse(inputJson, v) && v.isObject()) text = v.str("texte");
    const auto found = search(missions, text, true);
    if (found.empty())
        return text.empty() ? std::string("Aucune mission enregistr\xC3\xA9" "e.") : "Aucune mission ne correspond \xC3\xA0 " + guill(text) + " (donn\xC3\xA9" "es enregistr\xC3\xA9" "es relues \xC3\xA0 l'instant).";
    std::string s = std::to_string(found.size()) + " mission(s) trouv\xC3\xA9" "e(s) (donn\xC3\xA9" "es enregistr\xC3\xA9" "es) :\n";
    std::size_t n = 0;
    for (const auto i : found) {
        if (++n > 6) break;
        const Mission& m = missions[i];
        const Progress p = progressOf(m);
        s += "- " + guill(m.name) + " (" + m.id + ")" + (m.archived ? " [archiv\xC3\xA9" "e]" : "") + " : " + std::string(stateLabel(m.state)) + ", " + p.text()
             + " ; projet " + (m.project.name.empty() ? std::string("?") : m.project.name)
             + (!projectId.empty() && !m.project.id.empty() && m.project.id != projectId ? std::string(" (pas le projet ouvert)") : std::string{})
             + " ; derni\xC3\xA8re activit\xC3\xA9 " + m.modified + " ; prochaine action : " + nextAction(m) + "\n";
    }
    s += "Le d\xC3\xA9tail d'une mission : l'outil etat_mission.";
    return s;
}

std::string runDetailTool(const std::vector<Mission>& missions, std::string_view inputJson) {
    Value v;
    std::string key;
    if (json::parse(inputJson, v) && v.isObject()) key = trimmed(v.str("mission"));
    if (key.empty()) return "Donne l'identifiant ou le nom de la mission.";
    for (const auto& m : missions)
        if (m.id == key) return detailText(m);
    for (const auto& m : missions)
        if (folded(m.name) == folded(key)) return detailText(m);
    const auto found = search(missions, key, true);
    if (found.empty()) return "Aucune mission " + guill(key) + " n'est enregistr\xC3\xA9" "e.";
    return detailText(missions[found.front()]);
}

// ---- le fichier -----------------------------------------------------------------------------------------------------
Value toJson(const Mission& m) {
    Value o = Value::object();
    o.set("format", m.format);
    o.set("id", m.id);
    o.set("nom", m.name);
    o.set("description", m.description);
    o.set("demande", m.request);
    o.set("cree", m.created);
    o.set("modifie", m.modified);
    Value pr = Value::object();
    pr.set("id", m.project.id);
    pr.set("nom", m.project.name);
    pr.set("chemin", m.project.path);
    pr.set("empreinte", m.project.print);
    pr.set("empreinte_prise", m.project.printed);
    Value files = Value::object();
    for (const auto& [f, h] : m.project.files) files.set(f, h);
    pr.set("fichiers", files);
    o.set("projet", pr);
    o.set("conversation", m.conversation);
    o.set("conversations", strings(m.conversations));
    o.set("fournisseur", m.provider);
    o.set("modele", m.model);
    o.set("etat", std::string(stateKey(m.state)));
    o.set("archivee", m.archived);
    o.set("revision_plan", m.planRevision);
    Value steps = Value::array();
    for (const auto& s : m.steps) {
        Value x = Value::object();
        x.set("id", s.id);
        x.set("titre", s.title);
        x.set("detail", s.detail);
        x.set("categorie", s.category);
        x.set("depend_de", strings(s.deps));
        x.set("implicites", strings(s.implicit));
        x.set("etat", std::string(stepKey(s.state)));
        x.set("obligatoire", s.mandatory);
        x.set("operations", s.plannedOps);
        x.set("produit", strings(s.expected));
        x.set("propositions", strings(s.proposals));
        x.set("note", s.note);
        x.set("erreur", s.error);
        x.set("maj", s.updated);
        json::addMembers(x, s.extra);
        steps.push(std::move(x));
    }
    o.set("etapes", std::move(steps));
    Value an = Value::object();
    an.set("conforme", strings(m.analysis.conform));
    an.set("a_modifier", strings(m.analysis.toModify));
    an.set("a_completer", strings(m.analysis.toComplete));
    an.set("a_creer", strings(m.analysis.toCreate));
    an.set("impossible", strings(m.analysis.impossible));
    an.set("resume", m.analysis.summary);
    o.set("analyse", an);
    Value props = Value::array();
    for (const auto& p : m.proposals) {
        Value x = Value::object();
        x.set("id", p.id);
        x.set("ordre", p.order);
        x.set("conversation", p.conversation);
        x.set("message", p.message);
        x.set("etapes", strings(p.steps));
        x.set("titre", p.title);
        x.set("etat", p.state);
        x.set("operations", p.ops);
        x.set("creer", p.create);
        x.set("modifier", p.modify);
        x.set("reutiliser", p.reuse);
        x.set("supprimer", p.remove);
        x.set("appliquees", p.applied);
        x.set("echouees", p.failed);
        x.set("ignorees", p.skipped);
        x.set("cree", p.created);
        x.set("appliquee_le", p.appliedAt);
        x.set("erreurs", strings(p.errors));
        Value lines = Value::array();
        for (const auto& l : p.lines) {
            Value y = Value::object();
            y.set("id", l.id);
            y.set("etape", l.step);
            y.set("titre", l.title);
            y.set("verbe", l.verb);
            y.set("etat", l.state);
            y.set("cree", strings(l.creates));
            if (!l.error.empty()) y.set("erreur", l.error);
            lines.push(std::move(y));
        }
        x.set("lignes", std::move(lines));
        if (!p.report.isNull()) x.set("compte_rendu", p.report);
        x.set("suite", p.next);
        props.push(std::move(x));
    }
    o.set("propositions", std::move(props));
    Value hist = Value::array();
    for (const auto& e : m.history) {
        Value x = Value::object();
        x.set("date", e.date);
        x.set("genre", e.kind);
        x.set("texte", e.text);
        if (!e.step.empty()) x.set("etape", e.step);
        if (!e.proposal.empty()) x.set("proposition", e.proposal);
        hist.push(std::move(x));
    }
    o.set("historique", std::move(hist));
    Value dec = Value::array();
    for (const auto& d : m.decisions) {
        Value x = Value::object();
        x.set("date", d.date);
        x.set("question", d.question);
        x.set("reponse", d.answer);
        dec.push(std::move(x));
    }
    o.set("decisions", std::move(dec));
    Value pend = Value::object();
    pend.set("actif", m.pending.active);
    pend.set("objet", m.pending.purpose);
    pend.set("conversation", m.pending.conversation);
    pend.set("debut", m.pending.started);
    pend.set("etapes", strings(m.pending.steps));
    o.set("appel_en_cours", pend);
    o.set("coherence", std::string(coherenceKey(m.coherence)));
    o.set("coherence_texte", m.coherenceText);
    o.set("point_de_reprise", m.resumePoint);
    o.set("resume", m.summary);
    o.set("pieces_jointes", strings(m.attachments));
    if (!m.finalReport.isNull()) o.set("rapport_final", m.finalReport);
    if (m.forced.on()) {   // 1.22.3 : seulement apres une fin forcee (les autres fichiers restent tels quels)
        Value f = Value::object();
        f.set("date", m.forced.date);
        f.set("raison", m.forced.reason);
        f.set("etapes", strings(m.forced.steps));
        f.set("propositions", strings(m.forced.proposals));
        o.set("fin_forcee", f);
    }
    json::addMembers(o, m.extra);
    return o;
}

Mission fromJson(const Value& o, int* fromFormat, bool* readOnly) {
    Mission m;
    const int format = o.has("format") ? static_cast<int>(o.integer("format", 0)) : 0;
    if (fromFormat) *fromFormat = format;
    if (readOnly) *readOnly = format > kFormat;
    m.format = format > kFormat ? format : kFormat;   // un format plus recent garde son numero (jamais reecrit)
    m.id = o.str("id");
    m.name = o.str("nom", "Mission");
    m.description = o.str("description");
    m.request = o.str("demande");
    m.created = o.str("cree");
    m.modified = o.str("modifie", m.created);
    const Value& pr = o.at("projet");
    if (pr.isObject()) {
        m.project.id = pr.str("id");
        m.project.name = pr.str("nom");
        m.project.path = pr.str("chemin");
        m.project.print = pr.str("empreinte");
        m.project.printed = pr.str("empreinte_prise");
        for (const auto& [f, h] : pr.at("fichiers").members())
            if (h.isString()) m.project.files.emplace_back(f, h.asString());
    } else if (pr.isString()) {   // format 0 : le nom du projet seul
        m.project.name = pr.asString();
    }
    m.conversation = o.str("conversation");
    m.conversations = stringsOf(o.at("conversations"));
    if (m.conversations.empty() && !m.conversation.empty()) m.conversations.push_back(m.conversation);
    m.provider = o.str("fournisseur");
    m.model = o.str("modele");
    m.state = stateFromKey(o.str("etat", "en_cours"));
    m.archived = o.flag("archivee");
    m.planRevision = static_cast<int>(o.integer("revision_plan"));
    for (const auto& x : o.at("etapes").items()) {
        if (!x.isObject()) continue;
        Step s;
        s.id = x.str("id");
        s.title = x.str("titre");
        s.detail = x.str("detail");
        s.category = normalizeCategory(x.str("categorie", "autre"));
        s.deps = stringsOf(x.at("depend_de"));
        s.implicit = stringsOf(x.at("implicites"));
        s.state = stepFromKey(x.str("etat", "en_attente"));
        s.mandatory = x.flag("obligatoire", true);
        s.plannedOps = static_cast<int>(x.integer("operations"));
        s.expected = stringsOf(x.at("produit"));
        s.proposals = stringsOf(x.at("propositions"));
        s.note = x.str("note");
        s.error = x.str("erreur");
        s.updated = x.str("maj");
        s.extra = json::unknownMembers(x, {"id", "titre", "detail", "categorie", "depend_de", "implicites", "etat", "obligatoire", "operations",
                                           "produit", "propositions", "note", "erreur", "maj"});
        if (s.id.empty()) s.id = "S" + std::to_string(m.steps.size() + 1);
        m.steps.push_back(std::move(s));
    }
    const Value& an = o.at("analyse");
    m.analysis.conform = stringsOf(an.at("conforme"));
    m.analysis.toModify = stringsOf(an.at("a_modifier"));
    m.analysis.toComplete = stringsOf(an.at("a_completer"));
    m.analysis.toCreate = stringsOf(an.at("a_creer"));
    m.analysis.impossible = stringsOf(an.at("impossible"));
    m.analysis.summary = an.str("resume");
    int order = 0;
    for (const auto& x : o.at("propositions").items()) {
        ProposalLink p;
        if (x.isString()) {   // format 0 : la liste des identifiants seuls
            p.id = x.asString();
            p.order = ++order;
            p.state = "generee";
            m.proposals.push_back(std::move(p));
            continue;
        }
        if (!x.isObject()) continue;
        p.id = x.str("id");
        p.order = static_cast<int>(x.integer("ordre", ++order));
        order = std::max(order, p.order);
        p.conversation = x.str("conversation");
        p.message = x.str("message");
        p.steps = stringsOf(x.at("etapes"));
        p.title = x.str("titre");
        p.state = x.str("etat", "generee");
        p.ops = static_cast<int>(x.integer("operations"));
        p.create = static_cast<int>(x.integer("creer"));
        p.modify = static_cast<int>(x.integer("modifier"));
        p.reuse = static_cast<int>(x.integer("reutiliser"));
        p.remove = static_cast<int>(x.integer("supprimer"));
        p.applied = static_cast<int>(x.integer("appliquees"));
        p.failed = static_cast<int>(x.integer("echouees"));
        p.skipped = static_cast<int>(x.integer("ignorees"));
        p.created = x.str("cree");
        p.appliedAt = x.str("appliquee_le");
        p.errors = stringsOf(x.at("erreurs"));
        for (const auto& y : x.at("lignes").items()) {
            OpLine l;
            l.id = y.str("id");
            l.step = y.str("etape");
            l.title = y.str("titre");
            l.verb = y.str("verbe");
            l.state = y.str("etat");
            l.creates = stringsOf(y.at("cree"));
            l.error = y.str("erreur");
            p.lines.push_back(std::move(l));
        }
        if (const Value* r = x.find("compte_rendu")) p.report = *r;
        p.next = x.str("suite");
        m.proposals.push_back(std::move(p));
    }
    for (const auto& x : o.at("historique").items()) {
        Event e;
        e.date = x.str("date");
        e.kind = x.str("genre");
        e.text = x.str("texte");
        e.step = x.str("etape");
        e.proposal = x.str("proposition");
        m.history.push_back(std::move(e));
    }
    for (const auto& x : o.at("decisions").items()) m.decisions.push_back({x.str("date"), x.str("question"), x.str("reponse")});
    const Value& pend = o.at("appel_en_cours");
    m.pending.active = pend.flag("actif");
    m.pending.purpose = pend.str("objet");
    m.pending.conversation = pend.str("conversation");
    m.pending.started = pend.str("debut");
    m.pending.steps = stringsOf(pend.at("etapes"));
    m.coherence = coherenceFromKey(o.str("coherence"));
    m.coherenceText = o.str("coherence_texte");
    m.resumePoint = o.str("point_de_reprise");
    m.summary = o.str("resume");
    m.attachments = stringsOf(o.at("pieces_jointes"));
    if (const Value* r = o.find("rapport_final")) m.finalReport = *r;
    if (const Value& f = o.at("fin_forcee"); f.isObject()) {   // 1.22.3
        m.forced.date = f.str("date");
        m.forced.reason = f.str("raison", kForcedReason);
        m.forced.steps = stringsOf(f.at("etapes"));
        m.forced.proposals = stringsOf(f.at("propositions"));
    }
    m.extra = json::unknownMembers(o, {"format", "id", "nom", "description", "demande", "cree", "modifie", "projet", "conversation", "conversations",
                                       "fournisseur", "modele", "etat", "archivee", "revision_plan", "etapes", "analyse", "propositions",
                                       "historique", "decisions", "appel_en_cours", "coherence", "coherence_texte", "point_de_reprise", "resume",
                                       "pieces_jointes", "rapport_final", "fin_forcee"});
    // Format 0 : des etapes sans dependances calculees - l'ordre et les etats d'attente refaits.
    if (format == 0) {
        std::string cycle;
        (void)orderSteps(m.steps, &cycle);
        refreshReadiness(m);
    }
    return m;
}

} // namespace app::ai::mission
