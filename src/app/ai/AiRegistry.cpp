// =============================================================================
//  app/ai/AiRegistry.cpp - le registre des genres d'objets (voir AiRegistry.hpp)
// =============================================================================
#include "AiRegistry.hpp"

#include "../hmi/HmiPanels.hpp"        // hmiPaletteFamily, hmiPaletteWords, hmiPropertyLabel, hmiPropertyCategories
#include "../../hmi/HmiGuide.hpp"      // topicForKind (la description), fold, plain

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace app::ai {

namespace {

namespace roles = hmi::roles;
using R = roles::Role;

// Le mot d'un role, en clair (la connaissance des proprietes du centre de memoire).
const char* roleWord(R r) {
    switch (r) {
        case R::Number: return "nombre";
        case R::Boolean: return "bool\xC3\xA9" "en";
        case R::State: return "\xC3\xA9tat";
        case R::Text: return "texte";
        case R::Duration: return "dur\xC3\xA9" "e";
        case R::Date: return "date";
        case R::Color: return "couleur";
        case R::Name: return "nom";
        case R::Formula: return "formule";
        case R::List: return "liste";
        case R::Mini: return "petit langage";
        case R::Choice: return "choix";
        case R::Format: return "format";
    }
    return "texte";
}

// Le role en abrege (le texte du modele ; la legende est dans commonText).
const char* roleLetter(R r) {
    switch (r) {
        case R::Number: return "n";
        case R::Boolean: return "b";
        case R::State: return "e";
        case R::Text: return "t";
        case R::Duration: return "d";
        case R::Date: return "h";
        case R::Color: return "c";
        case R::Name: return "nm";
        case R::Formula: return "f";
        case R::List: return "l";
        case R::Mini: return "m";
        case R::Choice: return "ch";
        case R::Format: return "fmt";
    }
    return "t";
}

// La premiere phrase d'un texte d'aide, sans ses marques, au plus `max` octets (coupee a un mot).
std::string firstSentence(std::string_view text, std::size_t max = 140) {
    std::string s = hmi::guide::plain(text);
    for (std::size_t i = 0; i + 1 < s.size(); ++i)
        if ((s[i] == '.' || s[i] == ':' || s[i] == ';') && s[i + 1] == ' ') {
            s.resize(s[i] == '.' ? i + 1 : i);
            break;
        }
    // sans les blancs (une espace insecable avant les deux-points) ni le point de la fin : la ligne continue
    for (;;) {
        if (!s.empty() && (s.back() == ' ' || s.back() == '.')) {
            s.pop_back();
            continue;
        }
        if (s.size() >= 2 && static_cast<unsigned char>(s[s.size() - 2]) == 0xC2 && static_cast<unsigned char>(s.back()) == 0xA0) {
            s.resize(s.size() - 2);
            continue;
        }
        break;
    }
    if (s.size() > max) {
        std::size_t cut = s.rfind(' ', max - 3);
        if (cut == std::string::npos || cut < max / 2) cut = max - 3;
        // ne pas couper une lettre accentuee (UTF-8) en deux
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut) + "...";
    }
    return s;
}

// Une valeur montree au modele : entre guillemets si elle porte un blanc ou un signe du format,
// coupee si elle est longue, les retours a la ligne ecrits \n.
std::string shown(std::string_view v, std::size_t max = 32) {
    std::string s;
    for (const char c : v) {
        if (c == '\n') s += "\\n";
        else if (c != '\r') s += c;
    }
    if (s.size() > max) {
        std::size_t cut = max - 3;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut) + "...";
    }
    const bool quote = s.find_first_of(" ()|;=\"") != std::string::npos;
    return quote ? "\"" + s + "\"" : s;
}

// Les mots d'un texte replie (sans casse ni accents) : lettres et chiffres.
std::vector<std::string> tokens(std::string_view folded) {
    std::vector<std::string> out;
    std::string w;
    for (const char c : folded) {
        if (std::isalnum(static_cast<unsigned char>(c))) w += c;
        else if (!w.empty()) {
            out.push_back(std::move(w));
            w.clear();
        }
    }
    if (!w.empty()) out.push_back(std::move(w));
    return out;
}

// Le mot de la demande est-il ce mot, ou son pluriel (-s, -x ; -al -> -aux) ?
bool sameWord(const std::string& said, const std::string& word) {
    if (said == word) return true;
    if (said.size() == word.size() + 1 && said.compare(0, word.size(), word) == 0 && (said.back() == 's' || said.back() == 'x')) return true;
    if (word.size() > 2 && word.compare(word.size() - 2, 2, "al") == 0 && said == word.substr(0, word.size() - 2) + "aux") return true;
    return false;
}

// Le premier rang ou la suite de mots `phrase` apparait dans `said` ; -1 : nulle part.
int findPhrase(const std::vector<std::string>& said, const std::vector<std::string>& phrase) {
    if (phrase.empty() || said.size() < phrase.size()) return -1;
    for (std::size_t i = 0; i + phrase.size() <= said.size(); ++i) {
        bool all = true;
        for (std::size_t j = 0; j < phrase.size() && all; ++j) all = sameWord(said[i + j], phrase[j]);
        if (all) return static_cast<int>(i);
    }
    return -1;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// Les listes a choix de l'inspecteur pour un objet de ce genre (la cle -> les mots proposes).
std::map<std::string, std::vector<std::string>> inspectorChoices(const hmi::Object& o) {
    std::map<std::string, std::vector<std::string>> out;
    hmi::View v;
    v.id = 1;
    v.name = "Registre";
    v.objects.push_back(o);
    const auto cats = hmiPropertyCategories(v, {o.id}, nullptr, HmiPropertyCommits{});
    const auto walk = [&](const auto& self, const std::vector<ui::PropertyGrid::Category>& list) -> void {
        for (const auto& c : list) {
            for (const auto& p : c.properties)
                if (p.type == ui::PropertyGrid::ValueType::Enum && !p.key.empty() && !p.enumValues.empty() && !out.count(p.key))
                    out[p.key] = p.enumValues;
            self(self, c.children);
        }
    };
    walk(walk, cats);
    return out;
}

} // namespace

const PropertyInfo* KindEntry::prop(std::string_view k) const noexcept {
    for (const auto& p : props)
        if (p.key == k) return &p;
    return nullptr;
}

const KindRegistry& KindRegistry::get() {
    static const KindRegistry r;
    return r;
}

KindRegistry::KindRegistry() {
    std::set<hmi::Kind> placeable(std::begin(hmi::kPlaceableKinds), std::end(hmi::kPlaceableKinds));
    kinds_.reserve(std::size(hmi::kKindInfos));
    for (const auto& info : hmi::kKindInfos) {
        KindEntry e;
        e.kind = info.kind;
        e.key = std::string(info.key);
        e.label = std::string(info.label);
        e.family = hmiPaletteFamily(info.kind);
        if (const auto* t = hmi::guide::topicForKind(info.key)) e.description = firstSentence(t->summary);
        for (const auto w : hmiPaletteWords(info.kind)) e.words.emplace_back(w);
        e.valueRole = roles::valueRole(info.kind);
        e.placeable = placeable.count(info.kind) != 0;
        e.synoptic = hmi::kindIsSynoptic(info.kind);
        e.level = hmi::kindHasLevel(info.kind);
        e.shows = hmi::kindShowsValue(info.kind) || e.synoptic || e.level;
        e.writes = hmi::kindWritesVariable(info.kind);
        e.holds = hmi::kindHoldsChildren(info.kind);
        const hmi::Object o = hmi::makeObject(info.kind, 1, e.key + "_1", 0, 0, hmi::kNoId);
        e.w = o.number("w");
        e.h = o.number("h");
        const auto choices = inspectorChoices(o);
        for (const auto& p : o.props) {
            PropertyInfo pi;
            pi.key = p.key;
            pi.value = p.value;
            pi.label = hmiPropertyLabel(info.kind, p.key);
            const roles::KeyRole kr = roles::roleOf(info.kind, p.key);
            pi.role = kr.role;
            pi.item = kr.item;
            pi.type = std::string(kr.type);
            pi.variable = roles::readsAsFormula(info.kind, p.key);
            // Les mots d'une liste a choix : gardes s'ils sont des VALEURS (la valeur par defaut en est une) ;
            // une liste de libelles (une valeur enregistree autrement) n'en dit rien de sur.
            if (const auto it = choices.find(p.key); it != choices.end() && pi.role == R::Choice)
                if (p.value.empty() || std::find(it->second.begin(), it->second.end(), p.value) != it->second.end()) pi.choices = it->second;
            e.props.push_back(std::move(pi));
        }
        kinds_.push_back(std::move(e));
    }
    // Les communes : les cles de tous les genres de la bibliotheque, dans l'ordre de la premiere creation.
    std::map<std::string, std::size_t> seen;
    std::size_t lib = 0;
    for (const auto& e : kinds_) {
        if (!e.placeable) continue;
        ++lib;
        for (const auto& p : e.props) ++seen[p.key];
    }
    for (const auto& e : kinds_) {
        if (!e.placeable) continue;
        for (const auto& p : e.props)
            if (seen[p.key] == lib) common_.push_back(p);
        break;
    }
    for (auto& e : kinds_)
        for (auto& p : e.props) p.common = seen.count(p.key) && seen[p.key] == lib;
}

const KindEntry* KindRegistry::find(hmi::Kind k) const noexcept {
    const auto i = static_cast<std::size_t>(k);
    return i < kinds_.size() && kinds_[i].kind == k ? &kinds_[i] : nullptr;
}

const KindEntry* KindRegistry::find(std::string_view key) const noexcept {
    for (const auto& e : kinds_)
        if (e.key == key) return &e;
    return nullptr;
}

std::string KindRegistry::kindsText() const {
    std::string s = "Les genres d'objets d'IHM Studio (chaque objet d'une vue en est un, avec des propri\xC3\xA9t\xC3\xA9s cl\xC3\xA9/valeur) : "
                    "cl\xC3\xA9 | libell\xC3\xA9 | famille de la biblioth\xC3\xA8que (- : hors biblioth\xC3\xA8que).\n";
    for (const auto& e : kinds_) s += e.key + " | " + e.label + " | " + (e.family.empty() ? std::string("-") : e.family) + "\n";
    return s;
}

std::string KindRegistry::roleCode(const PropertyInfo& p) {
    if (p.role == R::Choice && !p.choices.empty()) {
        std::string s;
        for (const auto& c : p.choices) s += (s.empty() ? "" : "|") + c;
        return s;
    }
    std::string s = roleLetter(p.role);
    if ((p.role == R::Formula || p.role == R::List) && p.item != R::Text) s += std::string(":") + roleLetter(p.item);
    return s;
}

std::string KindRegistry::commonText() const {
    std::string s =
        "Abr\xC3\xA9viations des r\xC3\xB4les : n nombre (point d\xC3\xA9" "cimal) ; b bool\xC3\xA9" "en (TRUE ou FALSE) ; c couleur #RRGGBB ; "
        "t texte (des trous {Variable} possibles) ; e \xC3\xA9tat ; d dur\xC3\xA9" "e ; h date ; nm nom d'un \xC3\xA9l\xC3\xA9ment du projet "
        "(vue, ressource, groupe...) ; f formule lue par le moteur : le nom d'une variable EXISTANTE, ou une condition sur des "
        "variables existantes (f:b bool\xC3\xA9" "enne, f:n num\xC3\xA9rique) ; l liste a;b;c (l:f liste de formules) ; m petit langage ; "
        "fmt format (0.0) ; a|b|c : un de ces mots, tel quel.\n";
    s += "Propri\xC3\xA9t\xC3\xA9s communes \xC3\xA0 tous les genres :";
    for (const auto& p : common_) {
        s += " " + p.key + "(" + roleCode(p) + ")";
        if (!p.value.empty() && p.key != "x" && p.key != "y" && p.key != "w" && p.key != "h") s += "=" + shown(p.value);
    }
    s += ". x, y : le coin haut-gauche dans la vue (pixels) ; w, h : la taille ; variable : la variable lue (un afficheur, "
         "un symbole) ou \xC3\xA9" "crite (une commande).\n";
    return s;
}

std::string KindRegistry::detailLine(const KindEntry& e) const {
    std::string s = e.key + " (" + e.label + ") " + hmi::formatNumber(e.w) + "x" + hmi::formatNumber(e.h);
    if (!e.description.empty()) s += " - " + e.description;
    std::vector<std::string> caps;
    if (e.level) caps.push_back("niveau entre min et max");
    else if (e.shows) caps.push_back(std::string("montre une valeur (") + roleLetter(e.valueRole) + ")");
    if (e.writes) caps.push_back(std::string("\xC3\xA9" "crit sa variable (") + roleLetter(e.valueRole) + ")");
    if (e.holds) caps.push_back("tient des enfants");
    for (const auto& c : caps) s += ", " + c;
    s += " :";
    for (const auto& p : e.props) {
        if (p.common) continue;
        s += " " + p.key + "(" + roleCode(p) + ")";
        if (!p.value.empty()) s += "=" + shown(p.value);
    }
    s += "\n";
    return s;
}

std::string KindRegistry::propertiesText() const {
    std::string s = "Propri\xC3\xA9t\xC3\xA9s de chaque genre, telles que la cr\xC3\xA9" "ation par d\xC3\xA9" "faut les pose (hmi::makeObject), "
                    "avec leur r\xC3\xB4le (hmi::roles). Une ligne par genre : cl\xC3\xA9 (libell\xC3\xA9 de l'inspecteur) = valeur par d\xC3\xA9" "faut "
                    "[r\xC3\xB4le].\n";
    for (const auto& e : kinds_) {
        s += e.key + " :";
        bool first = true;
        for (const auto& p : e.props) {
            s += first ? " " : " ; ";
            first = false;
            s += p.key;
            if (!p.label.empty() && p.label != p.key) s += " (" + p.label + ")";
            s += " = " + (p.value.empty() ? std::string("\"\"") : p.value);
            s += std::string(" [") + roleWord(p.role) + "]";
        }
        s += "\n";
    }
    return s;
}

std::vector<hmi::Kind> KindRegistry::evoked(std::string_view request, bool useWords, std::size_t max) const {
    const auto said = tokens(hmi::guide::fold(request));
    struct Hit {
        int       at;
        hmi::Kind kind;
    };
    std::vector<Hit> hits;
    for (const auto& e : kinds_) {
        int best = -1;
        const auto consider = [&](std::string_view phrase) {
            const auto words = tokens(hmi::guide::fold(phrase));
            if (words.empty() || (words.size() == 1 && words.front().size() < 3)) return;
            const int at = findPhrase(said, words);
            if (at >= 0 && (best < 0 || at < best)) best = at;
        };
        consider(e.key);
        consider(e.label);
        if (useWords)
            for (const auto& w : e.words) consider(w);
        if (best >= 0) hits.push_back({best, e.kind});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.at < b.at; });
    std::vector<hmi::Kind> out;
    for (const auto& h : hits) {
        if (out.size() >= max) break;
        out.push_back(h.kind);
    }
    return out;
}

std::vector<std::string> KindRegistry::nearKinds(std::string_view name, std::size_t max) const {
    const std::string want = lowerAscii(hmi::guide::fold(name));
    struct Scored {
        std::size_t score;
        const KindEntry* e;
    };
    std::vector<Scored> all;
    for (const auto& e : kinds_) {
        std::size_t best = std::string::npos;
        const auto consider = [&](std::string_view s) {
            const std::string c = lowerAscii(hmi::guide::fold(s));
            if (c.empty()) return;
            std::size_t d = distance(want, c);
            // un debut commun (Pompe -> Pompe doseuse, Tank -> TankLevel) compte presque autant qu'egal
            if (c.size() >= 4 && want.size() >= 4 && (c.rfind(want, 0) == 0 || want.rfind(c, 0) == 0)) d = std::min<std::size_t>(d, 1);
            best = std::min(best, d);
        };
        consider(e.key);
        consider(e.label);
        for (const auto& w : e.words) consider(w);
        if (best <= std::max<std::size_t>(2, want.size() / 4)) all.push_back({best, &e});
    }
    std::stable_sort(all.begin(), all.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });
    std::vector<std::string> out;
    for (const auto& s : all) {
        if (out.size() >= max) break;
        out.push_back(s.e->key + " (" + s.e->label + ")");
    }
    return out;
}

} // namespace app::ai
