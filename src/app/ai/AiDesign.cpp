// =============================================================================
//  app/ai/AiDesign.cpp - voir AiDesign.hpp
// =============================================================================
#include "AiDesign.hpp"

#include "AiRegistry.hpp"
#include "../../hmi/HmiAudit.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiFolders.hpp"
#include "../../hmi/HmiRoles.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace app::ai::design {

namespace {

using A = Operation::Action;
using Props = std::vector<std::pair<std::string, std::string>>;

// =========================================================================================
//  LES MOTS
// =========================================================================================
std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool identChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le texte sans ses accents (les lettres accentuees du francais en UTF-8), la casse gardee : "Regulation" de
// "R\xC3\xA9gulation", "OE" de "\xC5\x92".
std::string plain(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            const bool up = d < 0xA0;
            const unsigned x = up ? d + 0x20u : d;
            char b = 0;
            if (x >= 0xA0 && x <= 0xA5) b = 'a';
            else if (x == 0xA7) b = 'c';
            else if (x >= 0xA8 && x <= 0xAB) b = 'e';
            else if (x >= 0xAC && x <= 0xAF) b = 'i';
            else if (x >= 0xB2 && x <= 0xB6) b = 'o';
            else if (x >= 0xB9 && x <= 0xBC) b = 'u';
            if (b != 0) {
                out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(b))) : b;
                ++i;
                continue;
            }
        }
        if (c == 0xC5 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) == 0x92 || static_cast<unsigned char>(s[i + 1]) == 0x93)) {
            out += static_cast<unsigned char>(s[i + 1]) == 0x92 ? "OE" : "oe";
            ++i;
            continue;
        }
        out += static_cast<char>(c);
    }
    return out;
}

// Replie : minuscules, sans accents (pour comparer ce qu'un utilisateur ecrit).
std::string fold(std::string_view s) { return lower(plain(s)); }

// Un identifiant tire d'un texte : "Station Nord" -> "Station_Nord", "R\xC3\xA9gulation" -> "Regulation" ; vide : aucun.
std::string identifierOf(std::string_view s) {
    std::string out;
    for (const char c : plain(trimmed(s))) {
        if (identChar(c)) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front()))) out.insert(out.begin(), '_');
    if (out.size() > 48) out.resize(48);
    return out;
}

std::string num(double d) { return hmi::formatNumber(d); }
std::string code(std::string_view s) { return "`" + std::string(s) + "`"; }
std::string capital(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
// "Niveau_Haut" -> "Niveau Haut"
std::string spaced(std::string s) {
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}
std::string count(std::size_t n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n > 1 ? many : one);
}
std::string joinList(const std::vector<std::string>& items, std::size_t max = 8) {
    std::string s;
    for (std::size_t i = 0; i < items.size() && i < max; ++i) s += (i ? ", " : "") + items[i];
    if (items.size() > max) s += ", \xE2\x80\xA6 (" + std::to_string(items.size()) + " en tout)";
    return s;
}
std::string codes(const std::vector<std::string>& items, std::size_t max = 8) {
    std::vector<std::string> c;
    for (const auto& i : items) c.push_back(code(i));
    return joinList(c, max);
}

// UN NOM CITE dans un texte (une expression, du code, des arguments) : le nom entier, sans casse, ni membre d'un autre
// (Armoire.Pompe_2), ni morceau d'un autre nom (Pompe_21). Les reperes $...$ en sont transparents.
bool refersTo(std::string_view text, std::string_view name) {
    if (name.empty() || text.size() < name.size()) return false;
    const std::string t = lower(text), n = lower(name);
    for (std::size_t at = t.find(n); at != std::string::npos; at = t.find(n, at + 1)) {
        const bool before = at > 0 && (identChar(t[at - 1]) || t[at - 1] == '.');
        const std::size_t end = at + n.size();
        const bool after = end < t.size() && identChar(t[end]);
        if (!before && !after) return true;
    }
    return false;
}
// ... et remplace (le nom neuf tel qu'il est ecrit).
std::string replaceRefs(std::string_view text, std::string_view from, std::string_view to) {
    if (from.empty() || text.size() < from.size()) return std::string(text);
    const std::string t = lower(text), n = lower(from);
    std::string out;
    std::size_t last = 0;
    for (std::size_t at = t.find(n); at != std::string::npos; at = t.find(n, at + 1)) {
        const bool before = at > 0 && (identChar(t[at - 1]) || t[at - 1] == '.');
        const std::size_t end = at + n.size();
        const bool after = end < t.size() && identChar(t[end]);
        if (before || after) continue;
        out += std::string(text.substr(last, at - last)) + std::string(to);
        last = end;
        at = end - 1;
    }
    return out + std::string(text.substr(last));
}
// Un libelle ("Pompe 2" -> "Pompe 3") : la phrase entiere, sans casse, pas suivie d'un chiffre.
std::string replacePhrase(std::string_view text, std::string_view from, std::string_view to) {
    if (from.empty() || text.size() < from.size()) return std::string(text);
    const std::string t = lower(text), n = lower(from);
    std::string out;
    std::size_t last = 0;
    for (std::size_t at = t.find(n); at != std::string::npos; at = t.find(n, at + 1)) {
        const bool before = at > 0 && identChar(t[at - 1]);
        const std::size_t end = at + n.size();
        const bool after = end < t.size() && identChar(t[end]);
        if (before || after) continue;
        out += std::string(text.substr(last, at - last)) + std::string(to);
        last = end;
        at = end - 1;
    }
    return out + std::string(text.substr(last));
}

// UN MORCEAU DE NOM : Pompe_2 dans Txt_Dispo_Pompe_2 ou Pompe_2_Defaut - borne par le debut, la fin ou un caractere qui
// n'est ni une lettre ni un chiffre ("_" compris) ; jamais Pompe_2 dans Pompe_21.
bool alnum(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
bool hasNamePart(std::string_view name, std::string_view part) {
    if (part.empty() || name.size() < part.size()) return false;
    const std::string t = lower(name), n = lower(part);
    for (std::size_t at = t.find(n); at != std::string::npos; at = t.find(n, at + 1)) {
        const std::size_t end = at + n.size();
        if ((at == 0 || !alnum(t[at - 1])) && (end >= t.size() || !alnum(t[end]))) return true;
    }
    return false;
}
std::string replaceNamePart(std::string_view name, std::string_view from, std::string_view to) {
    if (from.empty() || name.size() < from.size()) return std::string(name);
    const std::string t = lower(name), n = lower(from);
    std::string out;
    std::size_t last = 0;
    for (std::size_t at = t.find(n); at != std::string::npos; at = t.find(n, at + 1)) {
        const std::size_t end = at + n.size();
        if (!((at == 0 || !alnum(t[at - 1])) && (end >= t.size() || !alnum(t[end])))) continue;
        out += std::string(name.substr(last, at - last)) + std::string(to);
        last = end;
        at = end - 1;
    }
    return out + std::string(name.substr(last));
}

// "Pompe_12" et "Pompe" -> 12 ; -1 : pas de la serie.
int indexIn(std::string_view name, std::string_view prefix) {
    if (prefix.empty() || name.size() < prefix.size() + 2 || !iequal(name.substr(0, prefix.size()), prefix) || name[prefix.size()] != '_') return -1;
    const std::string_view digits = name.substr(prefix.size() + 1);
    if (digits.empty() || digits.size() > 6 || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return -1;
    return std::stoi(std::string(digits));
}

bool numericType(std::string_view type) {
    static const std::set<std::string> k = {"INT", "DINT", "SINT", "LINT", "UINT", "UDINT", "USINT", "ULINT", "REAL", "LREAL", "WORD", "DWORD", "BYTE", "LWORD"};
    std::string u(type);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return k.count(u) > 0;
}
bool boolType(std::string_view type) { return iequal(type, "BOOL"); }

// =========================================================================================
//  LES NOMS DU PROJET ET CEUX QUE LA PROPOSITION PREND
// =========================================================================================
struct Names {
    const hmi::Project&   pr;
    std::set<std::string> vars, views, alarms, scripts, equipments, types;   // en minuscules

    [[nodiscard]] bool viewTaken(std::string_view n) const {
        for (const auto& v : pr.views)
            if (iequal(v.name, n)) return true;
        return views.count(lower(n)) > 0;
    }
    [[nodiscard]] bool variableTaken(std::string_view n) const { return pr.variable(n) != nullptr || vars.count(lower(n)) > 0; }
    // Un nom de variable neuve : un identifiant, ni mot reserve, ni variable, ni fonction, ni vue du meme nom.
    [[nodiscard]] bool variableFree(const std::string& n) const {
        if (!hmi::isIdentifier(n) || hmi::isReservedWord(n) || iequal(n, "SYS") || iequal(n, "API")) return false;
        return !variableTaken(n) && pr.functionByName(n) == nullptr && !viewTaken(n);
    }
    // Un nom de vue neuve : ni vue (sans casse), ni variable (Vue.Objet se confondrait).
    [[nodiscard]] bool viewFree(const std::string& n) const { return !viewTaken(n) && !variableTaken(n); }
    [[nodiscard]] bool alarmTaken(std::string_view n) const {
        for (const auto& a : pr.alarms)
            if (iequal(a.name, n)) return true;
        return alarms.count(lower(n)) > 0;
    }
    [[nodiscard]] bool scriptTaken(std::string_view n) const {
        for (const auto& s : pr.programs.scripts)
            if (iequal(s.name, n)) return true;
        return scripts.count(lower(n)) > 0;
    }
    [[nodiscard]] bool equipmentTaken(std::string_view n) const { return pr.equipmentByName(n) != nullptr || equipments.count(lower(n)) > 0; }
    [[nodiscard]] bool typeTaken(std::string_view n) const { return pr.hmiTypeByName(n) != nullptr || types.count(lower(n)) > 0; }
};

// =========================================================================================
//  LES ADRESSES
// =========================================================================================
// L'EMPRISE d'une variable de ce type liee a un equipement Modbus, en mots de registre : mesuree par le plan des
// adresses du logiciel (hmi::audit::ranges) sur un projet d'essai - les BOOL a la suite 16 par mot, REAL et DINT
// 2 mots, INT 1... rien n'est recalcule ici.
long footprint(const hmi::Project& pr, const std::vector<hmi::HmiType>& extra, const std::string& type) {
    hmi::Project probe;
    probe.programs.types = pr.programs.types;
    for (const auto& t : extra)
        if (!probe.hmiTypeByName(t.name)) probe.programs.types.push_back(t);
    hmi::Equipment e;
    e.id = probe.allocate();
    e.name = "Essai";
    e.simulated = true;
    probe.equipments.push_back(e);
    hmi::Variable v;
    v.id = probe.allocate();
    v.name = "Essai_V";
    v.type = type;
    v.equipment = e.name;
    v.address = "%MW0";
    probe.programs.variables.push_back(v);
    long end = 0;
    for (const auto& r : hmi::audit::ranges(probe))
        if (r.table == hmi::MemTable::Holding) end = std::max<long>(end, static_cast<long>(r.last) + 1);
    return std::max<long>(end, 1);
}

// Les places libres des registres de maintien d'un equipement, prises au fur et a mesure par la proposition.
struct Allocator {
    std::vector<std::pair<long, long>> free;   // [debut, fin], dans l'ordre
    // `size` mots, a partir de `from` au plus tot (la premiere place qui convient) ; faux : plus de place.
    bool take(long size, long from, long& at) {
        for (auto it = free.begin(); it != free.end(); ++it) {
            const long a = std::max(it->first, from);
            if (a + size - 1 > it->second) continue;
            const auto range = *it;
            it = free.erase(it);
            if (a + size <= range.second) it = free.insert(it, {a + size, range.second});
            if (range.first <= a - 1) free.insert(it, {range.first, a - 1});
            at = a;
            return true;
        }
        return false;
    }
};
Allocator allocatorFor(const hmi::Project& pr, const std::string& equipment) {
    Allocator a;
    if (const auto* e = pr.equipmentByName(equipment)) a.free = hmi::audit::freeRanges(pr, e->name, hmi::MemTable::Holding, 0, 65535);
    else a.free = {{0, 65535}};
    return a;
}

// =========================================================================================
//  LA PROPOSITION EN CONSTRUCTION
// =========================================================================================
// Une vue visee par des objets : creee par la proposition, ou du projet.
struct Canvas {
    std::string           name;
    const hmi::View*      existing{nullptr};
    std::set<std::string> taken;          // les noms d'objets (minuscules)
    int                   objects{0};     // poses par la proposition
};

struct Build {
    const hmi::Project&      pr;
    Names                    names{pr, {}, {}, {}, {}, {}, {}};
    Proposal                 p;
    std::vector<std::string> reused, limits;
    std::vector<hmi::HmiType> newTypes;     // les types crees (l'emprise des variables neuves)
    std::map<std::string, long> sizes;      // l'emprise par type

    explicit Build(const hmi::Project& project) : pr(project) {}

    Operation& add(A action, std::string name = {}, Props props = {}) {
        Operation op;
        op.action = action;
        op.name = std::move(name);
        op.props = std::move(props);
        p.ops.push_back(std::move(op));
        return p.ops.back();
    }
    long sizeOf(const std::string& type) {
        const std::string key = lower(type);
        if (const auto it = sizes.find(key); it != sizes.end()) return it->second;
        return sizes[key] = footprint(pr, newTypes, type);
    }

    Canvas canvas(const std::string& view) const {
        Canvas c;
        c.name = view;
        c.existing = pr.viewByName(view);
        if (c.existing)
            for (const auto& o : c.existing->objects) c.taken.insert(lower(o.name));
        return c;
    }
    // Un nom d'objet libre dans la vue (le nom voulu, sinon Nom_2, Nom_3...).
    static std::string objectName(Canvas& c, const std::string& wanted) {
        std::string w = identifierOf(wanted);
        if (w.empty()) w = "Objet";
        if (!c.taken.count(lower(w))) {
            c.taken.insert(lower(w));
            return w;
        }
        for (int n = 2; n < 100000; ++n) {
            const std::string s = w + "_" + std::to_string(n);
            if (!c.taken.count(lower(s))) {
                c.taken.insert(lower(s));
                return s;
            }
        }
        return w;
    }
    // creer : un objet de la vue `c`.
    std::string object(Canvas& c, hmi::Kind kind, const std::string& name, double x, double y, double w, double h, Props more = {}) {
        Operation& op = add(A::Create, objectName(c, name), {{"x", num(x)}, {"y", num(y)}, {"w", num(w)}, {"h", num(h)}});
        op.kind = std::string(hmi::kindKey(kind));
        op.target = c.name;
        for (auto& m : more) op.props.push_back(std::move(m));
        ++c.objects;
        return op.name;
    }
    // code : des objets (actions comprises) ecrits par le logiciel lui-meme (hmi::serializeObjectCode), relus et
    // verifies par la validation comme un collage de code brut.
    void codeObjects(Canvas& c, std::vector<hmi::Object> objs) {
        if (objs.empty()) return;
        hmi::View tmp;
        tmp.id = 1;
        std::vector<hmi::Id> tops;
        for (const auto& o : objs)
            if (o.parent == hmi::kNoId) tops.push_back(o.id);
        tmp.objects = std::move(objs);
        Operation& op = add(A::Code);
        op.code = hmi::serializeObjectCode(tmp, tops);
        op.target = c.name;
        c.objects += static_cast<int>(tmp.objects.size());
    }
    // Un bouton qui ouvre la popup `popup` sur l'element `variable` (son parametre `param`).
    void popupButton(Canvas& c, const std::string& name, double x, double y, const std::string& text, const std::string& popup,
                     const std::string& arguments) {
        hmi::Object b = hmi::makeObject(hmi::Kind::Button, 1, objectName(c, name), x, y, hmi::kNoId);
        b.setNumber("w", 120);
        b.setNumber("h", 32);
        b.set("text", text);
        b.setNumber("fontSize", 14);
        hmi::Action a;
        a.trigger = hmi::Trigger::Click;
        a.operation = hmi::Operation::Popup;
        a.target = popup;
        a.value = arguments;
        b.actions.push_back(std::move(a));
        codeObjects(c, {std::move(b)});
    }

    Design finish(std::string summary, std::string text) {
        Design d;
        p.id = "design";
        p.summary = std::move(summary);
        scopeProposal(p, nullptr);
        if (!p.ops.empty()) p.fingerprint = proposalPrint(pr, p);
        d.proposal = std::move(p);
        d.text = std::move(text);
        d.reused = std::move(reused);
        d.limits = std::move(limits);
        return d;
    }
};

// Le texte de la fin : ce qui est reutilise, ce qu'il faut savoir.
std::string tail(const std::vector<std::string>& reused, const std::vector<std::string>& limits) {
    std::string s;
    if (!reused.empty()) {
        s += "\n**R\xC3\xA9utilis\xC3\xA9 tel quel** (d\xC3\xA9j\xC3\xA0 dans le projet, jamais remplac\xC3\xA9) :";
        for (const auto& r : reused) s += "\n- " + r;
    }
    if (!limits.empty()) {
        s += "\n**\xC3\x80 savoir :**";
        for (const auto& l : limits) s += "\n- " + l;
    }
    return s;
}

// =========================================================================================
//  LES MODELES D'ELEMENTS (les types de l'installation, leurs roles)
// =========================================================================================
struct AssetDef {
    const char*                        key;
    const char*                        type;          // "T_Pompe"
    const char*                        description;
    std::vector<Operation::MemberSpec> members;
    const char*                        prefix;        // "Pompe" : Pompe_1, Pompe_2...
    const char*                        noun;          // "pompe"
    const char*                        nouns;         // "pompes"
    hmi::Kind                          kind;          // l'objet du synoptique
    const char*                        param;         // le parametre du symbole et de la popup
    const char*                        measure;       // le role de la mesure montree (vide : aucune)
    const char*                        unit;          // son unite
};

const AssetDef& pumpDef() {
    static const AssetDef d{"pompe", "T_Pompe", "Pompe : marche, d\xC3\xA9" "faut, disponibilit\xC3\xA9, mode, consigne, intensit\xC3\xA9 et heures",
                            {{"Marche", "BOOL", "Retour de marche (la pompe tourne)"},
                             {"Defaut", "BOOL", "D\xC3\xA9" "faut (disjoncteur, thermique, discordance)"},
                             {"Disponible", "BOOL", "Pr\xC3\xAAte \xC3\xA0 d\xC3\xA9marrer (sans d\xC3\xA9" "faut, en automatique)"},
                             {"Mode_Auto", "BOOL", "Mode automatique (faux : manuel)"},
                             {"Consigne", "REAL", "Consigne de vitesse (%)"},
                             {"Intensite", "REAL", "Intensit\xC3\xA9 absorb\xC3\xA9" "e (A)"},
                             {"Heures_Fonctionnement", "DINT", "Compteur d'heures de fonctionnement"}},
                            "Pompe", "pompe", "pompes", hmi::Kind::Pump, "Pompe", "current", "A"};
    return d;
}
const AssetDef& tankDef() {
    static const AssetDef d{"cuve", "T_Cuve", "Cuve : niveau, d\xC3\xA9tecteurs et seuils, volume",
                            {{"Niveau", "REAL", "Niveau (%)"},
                             {"Niveau_Haut", "BOOL", "Niveau haut atteint"},
                             {"Niveau_Bas", "BOOL", "Niveau bas atteint"},
                             {"Seuil_Haut", "REAL", "Seuil de niveau haut (%)"},
                             {"Seuil_Bas", "REAL", "Seuil de niveau bas (%)"},
                             {"Volume", "REAL", "Volume contenu (m3)"}},
                            "Cuve", "cuve", "cuves", hmi::Kind::Tank, "Cuve", "volume", "m3"};
    return d;
}
const AssetDef& conveyorDef() {
    static const AssetDef d{"convoyeur", "T_Convoyeur", "Convoyeur : marche, d\xC3\xA9" "fauts, mode, vitesse et pi\xC3\xA8" "ces",
                            {{"Marche", "BOOL", "Retour de marche"},
                             {"Defaut", "BOOL", "D\xC3\xA9" "faut (moteur, arr\xC3\xAAt d'urgence)"},
                             {"Bourrage", "BOOL", "Bourrage d\xC3\xA9tect\xC3\xA9"},
                             {"Mode_Auto", "BOOL", "Mode automatique (faux : manuel)"},
                             {"Vitesse", "REAL", "Vitesse mesur\xC3\xA9" "e (m/min)"},
                             {"Consigne", "REAL", "Consigne de vitesse (m/min)"},
                             {"Pieces", "DINT", "Pi\xC3\xA8" "ces transport\xC3\xA9" "es"}},
                            "Convoyeur", "convoyeur", "convoyeurs", hmi::Kind::Conveyor, "Convoyeur", "speed", "m/min"};
    return d;
}
const AssetDef& valveDef() {
    static const AssetDef d{"vanne", "T_Vanne", "Vanne tout ou rien : fins de course, commande, d\xC3\xA9" "faut, mode",
                            {{"Ouverte", "BOOL", "Fin de course ouverte"},
                             {"Fermee", "BOOL", "Fin de course ferm\xC3\xA9" "e"},
                             {"Commande", "BOOL", "Ordre d'ouverture"},
                             {"Defaut", "BOOL", "D\xC3\xA9" "faut (discordance)"},
                             {"Mode_Auto", "BOOL", "Mode automatique (faux : manuel)"}},
                            "Vanne", "vanne", "vannes", hmi::Kind::Valve, "Vanne", "", ""};
    return d;
}
const AssetDef& loopDef() {
    static const AssetDef d{"boucle", "T_Boucle", "Boucle de r\xC3\xA9gulation PID : mesure, consigne, sortie, r\xC3\xA9glages",
                            {{"Mesure", "REAL", "Mesure (grandeur r\xC3\xA9gl\xC3\xA9" "e)"},
                             {"Consigne", "REAL", "Consigne"},
                             {"Sortie", "REAL", "Sortie (%)"},
                             {"Mode_Auto", "BOOL", "Mode automatique (faux : manuel)"},
                             {"Kp", "REAL", "Gain proportionnel"},
                             {"Ti", "REAL", "Temps int\xC3\xA9gral (s)"},
                             {"Td", "REAL", "Temps d\xC3\xA9riv\xC3\xA9 (s)"},
                             {"Defaut", "BOOL", "D\xC3\xA9" "faut de r\xC3\xA9gulation"}},
                            "Boucle", "boucle", "boucles", hmi::Kind::IsaInstrument, "Boucle", "measure", ""};
    return d;
}
const AssetDef& regValveDef() {
    static const AssetDef d{"vanne_reglante", "T_Vanne_Reglante", "Vanne r\xC3\xA9glante : position, commande, d\xC3\xA9" "faut, mode",
                            {{"Position", "REAL", "Position mesur\xC3\xA9" "e (%)"},
                             {"Commande", "REAL", "Commande d'ouverture (%)"},
                             {"Defaut", "BOOL", "D\xC3\xA9" "faut (discordance)"},
                             {"Mode_Auto", "BOOL", "Mode automatique (faux : manuel)"}},
                            "Vanne_Reglante", "vanne r\xC3\xA9glante", "vannes r\xC3\xA9glantes", hmi::Kind::Valve, "Vanne", "position", "%"};
    return d;
}

// LES ROLES : le membre qui joue chaque role, d'apres son nom (les noms des modeles du logiciel compris) et sa classe
// (un BOOL, un nombre) - un type du projet reutilise garde ses noms.
struct RoleDef {
    const char*              role;
    bool                     boolean;
    std::vector<const char*> names;
};
const std::vector<RoleDef>& roleDefs() {
    static const std::vector<RoleDef> k = {
        {"run", true, {"Marche", "En_Marche"}},
        {"fault", true, {"Defaut"}},
        {"available", true, {"Disponible", "Pret"}},
        {"auto", true, {"Mode_Auto", "Auto"}},
        {"jam", true, {"Bourrage"}},
        {"open", true, {"Ouverte"}},
        {"closed", true, {"Fermee"}},
        {"command", true, {"Commande"}},
        {"high", true, {"Niveau_Haut", "NiveauHaut"}},
        {"low", true, {"Niveau_Bas", "NiveauBas"}},
        {"setpoint", false, {"Consigne", "Consigne_Vitesse"}},
        {"current", false, {"Intensite", "Courant"}},
        {"hours", false, {"Heures_Fonctionnement", "HeuresMarche", "Heures"}},
        {"level", false, {"Niveau"}},
        {"highSet", false, {"Seuil_Haut", "SeuilHaut"}},
        {"lowSet", false, {"Seuil_Bas", "SeuilBas"}},
        {"volume", false, {"Volume"}},
        {"speed", false, {"Vitesse"}},
        {"count", false, {"Pieces", "Compteur", "NbPieces"}},
        {"position", false, {"Position", "Ouverture"}},
        {"output", false, {"Sortie"}},
        {"measure", false, {"Mesure"}},
        {"commandValue", false, {"Commande"}},
    };
    return k;
}

// Un element de l'installation tel qu'il sera : son type (cree ou celui du projet), ses variables.
struct Asset {
    const AssetDef*              def{nullptr};
    std::string                  type;
    std::vector<hmi::TypeMember> members;
    bool                         typeExisted{false}, typeSame{false};
    std::vector<std::string>     vars;
    std::vector<bool>            existed;     // la variable etait deja la, du meme type
    std::vector<int>             index;       // son numero (Pompe_3 : 3)
    std::vector<std::string>     skipped;     // les noms pris par autre chose

    [[nodiscard]] std::string member(std::string_view role) const {
        for (const auto& r : roleDefs()) {
            if (role != r.role) continue;
            for (const char* n : r.names)
                for (const auto& m : members)
                    if (iequal(m.name, n) && (r.boolean ? boolType(m.type) : numericType(m.type))) return m.name;
        }
        return {};
    }
    [[nodiscard]] std::string label(std::size_t i) const { return capital(def->noun) + " " + std::to_string(index[i]); }
    [[nodiscard]] bool empty() const noexcept { return vars.empty(); }
};

bool sameMembers(const hmi::HmiType& t, const std::vector<Operation::MemberSpec>& wanted) {
    if (t.members.size() != wanted.size() || !t.generics.empty()) return false;
    for (const auto& m : wanted) {
        const auto it = std::find_if(t.members.begin(), t.members.end(), [&](const hmi::TypeMember& x) { return iequal(x.name, m.name); });
        if (it == t.members.end() || !iequal(it->type, hmi::types::normalized(m.type))) return false;
    }
    return true;
}

std::string membersText(const std::vector<hmi::TypeMember>& members, std::size_t max = 8) {
    std::vector<std::string> s;
    for (const auto& m : members) s.push_back(m.name + " : " + m.type);
    return joinList(s, max);
}

// Le type et les variables d'un element : un type du meme nom est reutilise tel quel (ses membres) ; une variable du
// meme nom et du meme type sert telle quelle ; un nom pris autrement : le numero libre suivant.
Asset resolveAsset(Build& b, const AssetDef& def, int wanted, const std::string& varPrefix) {
    Asset a;
    a.def = &def;
    a.type = def.type;
    if (wanted <= 0) return a;
    const hmi::HmiType* t = b.pr.hmiTypeByName(def.type);
    if (t && t->kind != hmi::HmiTypeKind::Structure) {
        // une enumeration, un rappel, une interface porte ce nom : un nom de type libre
        for (int n = 2; n < 1000; ++n)
            if (const std::string s = std::string(def.type) + "_" + std::to_string(n); !b.names.typeTaken(s)) {
                b.limits.push_back(code(def.type) + " est d\xC3\xA9j\xC3\xA0 un autre genre de type : la structure s'appelle " + code(s) + ".");
                a.type = s;
                break;
            }
        t = nullptr;
    }
    if (t) {
        a.typeExisted = true;
        a.type = t->name;
        a.members = t->members;
        a.typeSame = sameMembers(*t, def.members);
    } else {
        for (const auto& m : def.members) {
            hmi::TypeMember tm;
            tm.name = m.name;
            tm.type = m.type;
            tm.description = m.description;
            a.members.push_back(std::move(tm));
        }
    }
    b.names.types.insert(lower(a.type));
    for (int k = 1; static_cast<int>(a.vars.size()) < wanted && k < 100000; ++k) {
        const std::string n = varPrefix + "_" + std::to_string(k);
        if (const auto* v = b.pr.variable(n)) {
            if (iequal(hmi::types::normalized(v->type), a.type) && !b.names.vars.count(lower(n))) {
                a.vars.push_back(v->name);
                a.existed.push_back(true);
                a.index.push_back(k);
                b.names.vars.insert(lower(n));
            } else {
                a.skipped.push_back(v->name + " (" + v->type + ")");
            }
            continue;
        }
        if (!b.names.variableFree(n)) {
            a.skipped.push_back(n);
            continue;
        }
        a.vars.push_back(n);
        a.existed.push_back(false);
        a.index.push_back(k);
        b.names.vars.insert(lower(n));
    }
    return a;
}

// La formule "=A.m OR B.m..." (vide : aucun membre).
std::string anyOf(const Asset& a, const std::string& member) {
    if (member.empty() || a.vars.empty()) return {};
    std::string s = "=";
    for (std::size_t i = 0; i < a.vars.size() && i < 24; ++i) s += (i ? " OR " : "") + a.vars[i] + "." + member;
    return s;
}

// =========================================================================================
//  LE SYMBOLE ET LA POPUP D'UN ELEMENT (lies a leur parametre par des formules)
// =========================================================================================
std::string formatOf(const std::string& type) { return boolType(type) ? "0" : (iequal(type, "REAL") || iequal(type, "LREAL")) ? "0.0" : "0"; }

std::string unitOfMember(const Asset& a, const std::string& member) {
    if (member.empty()) return {};
    for (const auto& [role, unit] : std::initializer_list<std::pair<const char*, const char*>>{
             {"current", "A"}, {"setpoint", "%"}, {"level", "%"}, {"highSet", "%"}, {"lowSet", "%"}, {"volume", "m3"}, {"speed", "m/min"},
             {"position", "%"}, {"output", "%"}, {"commandValue", "%"}, {"hours", "h"}})
        if (a.member(role) == member) {
            if (std::string_view(role) == "setpoint" && a.def->kind == hmi::Kind::Conveyor) return "m/min";
            return unit;
        }
    return {};
}

// Le symbole : l'objet du synoptique (etat, defaut), le mode, la mesure. Rend le nombre d'objets.
void symbolObjects(Build& b, Canvas& c, const Asset& a, const std::string& param) {
    const std::string P = param + ".";
    Props main;
    if (hmi::kindHasLevel(a.def->kind)) {
        if (const auto m = a.member("level"); !m.empty()) main.emplace_back("value", "=" + P + m);
    } else if (a.def->kind == hmi::Kind::IsaInstrument) {
        if (const auto m = a.member("measure"); !m.empty()) main.emplace_back("value", "=" + P + m);
        main.emplace_back("function", "PIC");
    } else if (a.def->kind == hmi::Kind::Valve && !a.member("position").empty()) {
        main.emplace_back("value", "=" + P + a.member("position") + " > 1.0");
        main.emplace_back("opening", "=" + P + a.member("position"));
    } else if (const auto m = a.member(a.def->kind == hmi::Kind::Valve ? "open" : "run"); !m.empty()) {
        main.emplace_back("value", "=" + P + m);
    }
    if (const auto m = a.member("fault"); !m.empty()) main.emplace_back("fault", "=" + P + m);
    main.emplace_back("labelPosition", "aucun");
    const bool wide = a.def->kind == hmi::Kind::Conveyor;
    b.object(c, a.def->kind, a.def->prefix, wide ? 10 : 35, 10, wide ? 140 : 90, wide ? 56 : 90, std::move(main));
    if (const auto m = a.member("auto"); !m.empty()) {
        b.object(c, hmi::Kind::Indicator, "Auto", 12, 116, 18, 18, {{"value", "=" + P + m}});
        b.object(c, hmi::Kind::Text, "Txt_Auto", 34, 110, 50, 30, {{"text", "Auto"}, {"fontSize", "13"}});
    }
    const std::string measure = a.def->measure[0] != '\0' ? a.member(a.def->measure) : std::string{};
    if (!measure.empty() && !hmi::kindHasLevel(a.def->kind))
        b.object(c, hmi::Kind::NumericDisplay, "Mesure", 84, 108, 70, 34,
                 {{"value", "=" + P + measure}, {"unit", unitOfMember(a, measure)}, {"fontSize", "14"}, {"format", "0.0"}});
}

// La popup : un voyant par etat (BOOL), une valeur par nombre, un bouton Fermer. `height` : sa hauteur.
double popupHeight(const Asset& a) {
    std::size_t bools = 0, nums = 0;
    for (const auto& m : a.members) {
        if (boolType(m.type)) ++bools;
        else if (numericType(m.type)) ++nums;
    }
    bools = std::min<std::size_t>(bools, 8);
    nums = std::min<std::size_t>(nums, 8);
    return 64 + static_cast<double>((bools + 1) / 2) * 36 + static_cast<double>(nums) * 44 + 70;
}
void popupObjects(Build& b, Canvas& c, const Asset& a, const std::string& param, double height) {
    const std::string P = param + ".";
    b.object(c, hmi::Kind::Text, "Titre", 20, 16, 400, 34,
             {{"text", "D\xC3\xA9tail : " + std::string(a.def->noun) + " ({" + param + "_Nom})"}, {"fontSize", "18"}});
    double y = 64;
    int col = 0, bools = 0, nums = 0;
    for (const auto& m : a.members) {
        if (!boolType(m.type) || bools >= 8) continue;
        ++bools;
        const double x = 20 + col * 210;
        Props on{{"value", "=" + P + m.name}};
        if (m.name == a.member("fault") || m.name == a.member("jam")) on.emplace_back("colorOn", "#E5534B");
        b.object(c, hmi::Kind::Indicator, "Etat_" + m.name, x, y + 4, 22, 22, std::move(on));
        b.object(c, hmi::Kind::Text, "Txt_" + m.name, x + 30, y, 170, 30, {{"text", spaced(m.name)}, {"fontSize", "14"}});
        if (++col == 2) {
            col = 0;
            y += 36;
        }
    }
    if (col != 0) y += 36;
    y += 8;
    for (const auto& m : a.members) {
        if (!numericType(m.type) || nums >= 8) continue;
        ++nums;
        b.object(c, hmi::Kind::Text, "Txt_" + m.name, 20, y + 2, 200, 30, {{"text", spaced(m.name)}, {"fontSize", "14"}});
        b.object(c, hmi::Kind::NumericDisplay, "Valeur_" + m.name, 230, y - 2, 190, 36,
                 {{"value", "=" + P + m.name}, {"format", formatOf(m.type)}, {"unit", unitOfMember(a, m.name)}, {"fontSize", "18"}});
        y += 44;
    }
    hmi::Object close = hmi::makeObject(hmi::Kind::Button, 1, Build::objectName(c, "Fermer"), 300, height - 52, hmi::kNoId);
    close.setNumber("w", 120);
    close.setNumber("h", 36);
    close.set("text", "Fermer");
    hmi::Action act;
    act.trigger = hmi::Trigger::Click;
    act.operation = hmi::Operation::ClosePopup;
    close.actions.push_back(std::move(act));
    b.codeObjects(c, {std::move(close)});
}

// =========================================================================================
//  L'INSTALLATION
// =========================================================================================
struct Plan {
    std::string              base;          // Station
    std::string              title;         // "Station de pompage"
    std::vector<Asset>       assets;
    std::size_t              main{0};        // l'element du symbole et de la popup
    std::string              equipment;     // l'equipement (vide : aucun)
    std::string              extraVar;      // ligne : Ligne_En_Marche
    bool                     extraNew{false};
};

// "la station de pompage", "le parc de cuves" ; "de la ...", "du ...".
std::string the(Installation k) { return std::string(k == Installation::TankFarm ? "le " : "la ") + std::string(installationLabel(k)); }
std::string ofThe(Installation k) { return std::string(k == Installation::TankFarm ? "du " : "de la ") + std::string(installationLabel(k)); }

// La vue montre-t-elle la variable (un objet la cite : une propriete, une formule, une action) ?
bool viewShows(const hmi::View& v, std::string_view var) {
    for (const auto& o : v.objects) {
        for (const auto& pp : o.props)
            if (refersTo(pp.value, var) || refersTo(pp.expr, var)) return true;
        for (const auto& ac : o.actions)
            if (refersTo(ac.value, var) || refersTo(ac.target, var)) return true;
    }
    return false;
}

const char* defaultBase(Installation k) {
    switch (k) {
        case Installation::PumpingStation: return "Station";
        case Installation::ProductionLine: return "Ligne";
        case Installation::TankFarm: return "Parc_Cuves";
        case Installation::Regulation: return "Regulation";
    }
    return "Station";
}

} // namespace

std::string_view installationLabel(Installation k) noexcept {
    switch (k) {
        case Installation::PumpingStation: return "station de pompage";
        case Installation::ProductionLine: return "ligne de production";
        case Installation::TankFarm: return "parc de cuves";
        case Installation::Regulation: return "r\xC3\xA9gulation";
    }
    return "station de pompage";
}

Design installation(const hmi::Project& pr, const InstallationSpec& spec) {
    Build b(pr);
    Plan plan;
    plan.base = identifierOf(spec.name);
    if (plan.base.empty()) plan.base = defaultBase(spec.kind);
    const std::string pre = identifierOf(spec.name).empty() ? std::string{} : plan.base + "_";
    const auto clampCount = [](int n) { return std::clamp(n, 0, 200); };
    const int pumps = clampCount(spec.pumps), tanks = clampCount(spec.tanks), loops = clampCount(spec.loops);
    const std::string label(installationLabel(spec.kind));
    plan.title = capital(label) + (identifierOf(spec.name).empty() ? std::string{} : " " + spaced(plan.base));

    switch (spec.kind) {
        case Installation::PumpingStation:
            plan.assets.push_back(resolveAsset(b, tankDef(), tanks, pre + tankDef().prefix));
            plan.assets.push_back(resolveAsset(b, pumpDef(), pumps, pre + pumpDef().prefix));
            plan.main = 1;
            break;
        case Installation::ProductionLine:
            plan.assets.push_back(resolveAsset(b, conveyorDef(), pumps, pre + conveyorDef().prefix));
            plan.main = 0;
            break;
        case Installation::TankFarm:
            plan.assets.push_back(resolveAsset(b, tankDef(), tanks, pre + tankDef().prefix));
            plan.assets.push_back(resolveAsset(b, valveDef(), tanks, pre + valveDef().prefix));
            plan.main = 0;
            break;
        case Installation::Regulation:
            plan.assets.push_back(resolveAsset(b, loopDef(), loops, pre + loopDef().prefix));
            plan.assets.push_back(resolveAsset(b, regValveDef(), pumps, pre + regValveDef().prefix));
            plan.main = 0;
            break;
    }
    const bool any = std::any_of(plan.assets.begin(), plan.assets.end(), [](const Asset& a) { return !a.empty(); });
    if (!any) return b.finish(plan.title, "Rien \xC3\xA0 construire : " + the(spec.kind) + " n'aurait aucun \xC3\xA9l\xC3\xA9ment (le nombre demand\xC3\xA9 est 0).");

    std::vector<std::string> lines;   // le resume, une ligne par categorie
    // ---- 1. les types ---------------------------------------------------------------------------------
    {
        std::vector<std::string> made;
        for (auto& a : plan.assets) {
            if (a.empty()) continue;
            if (!a.typeExisted || a.typeSame) {
                Operation& op = b.add(A::Type, a.type, {{"description", a.def->description}});
                op.members = a.def->members;
                if (!a.typeExisted) {
                    hmi::HmiType t;
                    t.name = a.type;
                    t.members = a.members;
                    b.newTypes.push_back(std::move(t));
                    made.push_back(a.type + " (" + std::to_string(a.members.size()) + " membres)");
                } else {
                    b.reused.push_back("Type " + code(a.type) + " (" + std::to_string(a.members.size()) + " membres, identique)");
                }
            } else {
                b.reused.push_back("Type " + code(a.type) + " (d\xC3\xA9j\xC3\xA0 dans le projet avec d'autres membres : " + membersText(a.members)
                                   + ") - gard\xC3\xA9 tel quel, l'installation suit ses membres");
            }
        }
        if (!made.empty()) lines.push_back("**Types** : " + joinList(made));
    }
    // ---- 2. les variables -------------------------------------------------------------------------------
    std::vector<std::string> createdVars;
    {
        Operation& op = b.add(A::Variables);
        std::vector<std::string> made;
        for (auto& a : plan.assets) {
            for (std::size_t i = 0; i < a.vars.size(); ++i) {
                op.variables.push_back({a.vars[i], a.type, capital(a.def->noun) + " " + std::to_string(a.index[i]) + " (" + plan.title + ")", ""});
                if (a.existed[i]) b.reused.push_back("Variable " + code(a.vars[i]) + " : " + a.type);
                else {
                    made.push_back(a.vars[i]);
                    createdVars.push_back(a.vars[i]);
                }
            }
            if (!a.skipped.empty())
                b.limits.push_back(capital(a.def->nouns) + " : " + joinList(a.skipped) + " d\xC3\xA9j\xC3\xA0 pris par autre chose - les " + a.def->nouns
                                   + " prennent les num\xC3\xA9ros libres suivants (" + joinList(a.vars) + ").");
        }
        if (spec.kind == Installation::ProductionLine && !plan.assets[0].empty()) {
            std::string n = plan.base + "_En_Marche";
            for (int k = 2; k < 1000; ++k) {
                if (const auto* v = pr.variable(n); v && boolType(v->type) && !b.names.vars.count(lower(n))) break;
                if (b.names.variableFree(n)) break;
                n = plan.base + "_En_Marche_" + std::to_string(k);
            }
            plan.extraVar = n;
            plan.extraNew = pr.variable(n) == nullptr;
            b.names.vars.insert(lower(n));
            op.variables.push_back({n, "BOOL", "Toute la ligne tourne (" + plan.title + ")", "FALSE"});
            if (plan.extraNew) {
                made.push_back(n);
                createdVars.push_back(n);
            } else {
                b.reused.push_back("Variable " + code(n) + " : BOOL");
            }
        }
        if (!made.empty()) lines.push_back("**Variables** : " + count(made.size(), "variable", "variables") + " (" + joinList(made, 6) + ")");
    }
    // ---- 3. l'equipement Modbus TCP, simule ---------------------------------------------------------------
    std::vector<std::string> addresses;
    if (spec.equipment) {
        static const hmi::Equipment defaults;
        std::string name = plan.base + "_Modbus";
        const hmi::Equipment* existing = pr.equipmentByName(name);
        for (int k = 2; existing && !existing->modbus() && k < 1000; ++k) {
            name = plan.base + "_Modbus_" + std::to_string(k);
            existing = pr.equipmentByName(name);
        }
        plan.equipment = existing ? existing->name : name;
        b.names.equipments.insert(lower(plan.equipment));
        const Props settings{{"type", "modbus_tcp"}, {"simule", "oui"}, {"port", "502"}, {"esclave", "1"}, {"periode", "500"},
                             {"description", "\xC3\x89quipement " + ofThe(spec.kind) + " (simul\xC3\xA9 : r\xC3\xA9gler son adresse IP pour le vrai appareil)"}};
        if (!existing) {
            b.add(A::Equipment, plan.equipment, settings);
            lines.push_back("**\xC3\x89quipement Modbus TCP** : " + code(plan.equipment)
                            + ", **simul\xC3\xA9** (" + the(spec.kind) + " fonctionne sans r\xC3\xA9seau ; son adresse IP se r\xC3\xA8gle ensuite pour le vrai appareil)");
        } else if (existing->simulated && existing->port == 502 && existing->unit == 1 && iequal(existing->host, defaults.host)
                   && existing->type == hmi::EquipmentType::ModbusTcp) {
            b.add(A::Equipment, plan.equipment, settings);   // le meme : la validation le reutilise
            b.reused.push_back("\xC3\x89quipement " + code(plan.equipment) + " (Modbus TCP simul\xC3\xA9, m\xC3\xAAmes r\xC3\xA9glages)");
        } else {
            b.reused.push_back("\xC3\x89quipement " + code(plan.equipment) + " (" + (existing->simulated ? std::string("simul\xC3\xA9") : existing->host) + ":"
                               + std::to_string(existing->port) + ", esclave " + std::to_string(existing->unit) + ") - ses r\xC3\xA9glages gard\xC3\xA9s");
        }
        // ---- 4. les adresses : les structures a la suite, aux places libres --------------------------------------
        Allocator alloc = allocatorFor(pr, plan.equipment);
        long first = -1, last = -1;
        std::vector<std::string> elsewhere;
        for (const auto& a : plan.assets)
            for (std::size_t i = 0; i < a.vars.size(); ++i) {
                if (a.existed[i]) {
                    const auto* v = pr.variable(a.vars[i]);
                    if (v && v->bound()) {
                        if (!iequal(v->equipment, plan.equipment)) elsewhere.push_back(v->name + " (" + v->equipment + ")");
                        continue;   // deja adressee : gardee
                    }
                }
                Operation& op = b.add(A::Address, {}, {{"equipement", plan.equipment}});
                op.variable = a.vars[i];
                long at = 0;
                const long size = b.sizeOf(a.type);
                if (alloc.take(size, 0, at)) {
                    op.props.emplace_back("adresse", "%MW" + std::to_string(at));
                    if (first < 0 || at < first) first = at;
                    last = std::max(last, at + size - 1);
                }
                addresses.push_back(a.vars[i]);
            }
        if (!plan.extraVar.empty() && plan.extraNew) {
            Operation& op = b.add(A::Address, {}, {{"equipement", plan.equipment}});
            op.variable = plan.extraVar;
            long at = 0;
            if (alloc.take(b.sizeOf("BOOL"), 0, at)) {
                op.props.emplace_back("adresse", "%MW" + std::to_string(at));
                last = std::max(last, at);
            }
            addresses.push_back(plan.extraVar);
        }
        if (!addresses.empty())
            lines.push_back("**Adresses** : " + count(addresses.size(), "variable plac\xC3\xA9" "e", "variables plac\xC3\xA9" "es") + " sur " + code(plan.equipment)
                            + (first >= 0 ? ", \xC3\xA0 la suite aux places libres (%MW" + std::to_string(first) + " \xC3\xA0 %MW" + std::to_string(last) + ")" : std::string{}));
        if (!elsewhere.empty()) b.limits.push_back("D\xC3\xA9j\xC3\xA0 li\xC3\xA9" "es \xC3\xA0 un autre \xC3\xA9quipement, gard\xC3\xA9" "es : " + joinList(elsewhere) + ".");
    }
    // ---- 5. les alarmes ---------------------------------------------------------------------------------------
    std::size_t alarmCount = 0;
    std::vector<std::string> alarmKinds;
    if (spec.alarms) {
        std::vector<std::string> kept, missing;
        const auto alarm = [&](const std::string& name, const std::string& condition, const std::string& message, int priority) {
            if (b.names.alarmTaken(name)) {
                kept.push_back(name);
                return false;
            }
            b.names.alarms.insert(lower(name));
            b.add(A::Alarm, name, {{"condition", condition}, {"message", message}, {"priorite", std::to_string(priority)}, {"groupe", plan.base}});
            ++alarmCount;
            return true;
        };
        for (const auto& a : plan.assets) {
            if (a.empty()) continue;
            std::size_t n = 0;
            const std::string fault = a.member("fault");
            const bool modelHasFault = std::any_of(a.def->members.begin(), a.def->members.end(), [](const Operation::MemberSpec& m) { return m.name == "Defaut"; });
            if (fault.empty() && modelHasFault)
                missing.push_back(code(a.type) + " (celui du projet) n'a pas de membre D\xC3\xA9" "faut : pas d'alarme de d\xC3\xA9" "faut pour les " + a.def->nouns);
            for (std::size_t i = 0; i < a.vars.size(); ++i) {
                const std::string& v = a.vars[i];
                if (!fault.empty()) n += alarm(v + "_Defaut", v + "." + fault, a.label(i) + " en d\xC3\xA9" "faut", a.def->kind == hmi::Kind::Pump ? 2 : 3);
                if (a.def == &tankDef()) {
                    const std::string level = a.member("level"), high = a.member("high"), low = a.member("low");
                    const std::string highCond = !high.empty() ? v + "." + high : !level.empty() ? v + "." + level + " >= 90.0" : std::string{};
                    const std::string lowCond = !low.empty() ? v + "." + low : !level.empty() ? v + "." + level + " <= 10.0" : std::string{};
                    if (!highCond.empty()) n += alarm(v + "_Niveau_Haut", highCond, a.label(i) + " : niveau haut", 2);
                    if (!lowCond.empty()) n += alarm(v + "_Niveau_Bas", lowCond, a.label(i) + " : niveau bas", 3);
                }
                if (const auto jam = a.member("jam"); !jam.empty()) n += alarm(v + "_Bourrage", v + "." + jam, a.label(i) + " : bourrage", 2);
                if (a.def == &loopDef()) {
                    const std::string m = a.member("measure"), sp = a.member("setpoint");
                    if (!m.empty() && !sp.empty())
                        n += alarm(v + "_Ecart", "(" + v + "." + m + " - " + v + "." + sp + " > 10.0) OR (" + v + "." + sp + " - " + v + "." + m + " > 10.0)",
                                   a.label(i) + " : \xC3\xA9" "cart mesure / consigne", 3);
                }
            }
            if (n > 0) alarmKinds.push_back(std::to_string(n) + " sur les " + a.def->nouns);
        }
        if (alarmCount > 0)
            lines.push_back("**Alarmes** : " + std::to_string(alarmCount) + " (" + joinList(alarmKinds) + "), groupe " + code(plan.base));
        if (!kept.empty()) b.reused.push_back(count(kept.size(), "alarme", "alarmes") + " du m\xC3\xAAme nom, gard\xC3\xA9" "es telles quelles : " + codes(kept, 6));
        for (const auto& m : missing) b.limits.push_back(m + ".");
    }
    // ---- 6. les scripts (ST, compiles par la validation, jamais executes ici) ---------------------------------
    std::vector<std::string> scriptLines;
    if (spec.scripts) {
        std::string init, cycle;
        for (const auto& a : plan.assets)
            for (std::size_t i = 0; i < a.vars.size(); ++i) {
                const std::string& v = a.vars[i];
                if (a.def == &tankDef()) {
                    const std::string hs = a.member("highSet"), ls = a.member("lowSet"), lv = a.member("level"), hi = a.member("high"), lo = a.member("low");
                    if (!hs.empty()) init += v + "." + hs + " := 90.0;\n";
                    if (!ls.empty()) init += v + "." + ls + " := 10.0;\n";
                    if (!lv.empty() && !hi.empty() && !hs.empty()) cycle += v + "." + hi + " := " + v + "." + lv + " >= " + v + "." + hs + ";\n";
                    if (!lv.empty() && !lo.empty() && !ls.empty()) cycle += v + "." + lo + " := " + v + "." + lv + " <= " + v + "." + ls + ";\n";
                }
                if (const std::string av = a.member("available"), f = a.member("fault"), au = a.member("auto"); !av.empty() && !f.empty())
                    cycle += v + "." + av + " := NOT " + v + "." + f + (au.empty() ? std::string{} : " AND " + v + "." + au) + ";\n";
            }
        if (spec.kind == Installation::ProductionLine && !plan.extraVar.empty()) {
            const Asset& c = plan.assets[0];
            const std::string run = c.member("run");
            if (!run.empty()) {
                cycle += plan.extraVar + " := ";
                for (std::size_t i = 0; i < c.vars.size(); ++i) cycle += (i ? " AND " : "") + c.vars[i] + "." + run;
                cycle += ";\n";
            }
        }
        if (spec.kind == Installation::Regulation) {
            const Asset& l = plan.assets[0];
            const Asset& v = plan.assets[1];
            const std::string out = l.member("output"), au = l.member("auto"), cmd = v.member("commandValue");
            for (std::size_t i = 0; i < l.vars.size() && i < v.vars.size() && !out.empty() && !cmd.empty(); ++i) {
                if (!au.empty()) cycle += "IF " + l.vars[i] + "." + au + " THEN\n    " + v.vars[i] + "." + cmd + " := " + l.vars[i] + "." + out + ";\nEND_IF;\n";
                else cycle += v.vars[i] + "." + cmd + " := " + l.vars[i] + "." + out + ";\n";
            }
        }
        const auto script = [&](const std::string& name, const char* event, const std::string& what, const std::string& body) {
            if (body.empty()) return;
            if (b.names.scriptTaken(name)) {
                b.reused.push_back("Script " + code(name) + " (existe d\xC3\xA9j\xC3\xA0 : gard\xC3\xA9 tel quel)");
                b.limits.push_back("Le script " + code(name) + " existant n'est pas r\xC3\xA9\xC3\xA9" "crit : s'il ne lit pas encore les \xC3\xA9l\xC3\xA9ments neufs, "
                                   "les y ajouter dans l'\xC3\xA9" "diteur.");
                return;
            }
            b.names.scripts.insert(lower(name));
            Props props{{"langage", "ST"}, {"evenement", event}};
            if (std::string_view(event) == "Cyclique") props.emplace_back("periode", "1000");
            props.emplace_back("corps", "(* " + plan.base + " : " + what + " - cree par l'Assistant IA *)\n" + body);
            props.emplace_back("dossier", plan.base);
            b.add(A::Script, name, std::move(props));
            scriptLines.push_back(code(name) + (std::string_view(event) == "Cyclique" ? " (toutes les secondes : " : " (au d\xC3\xA9marrage : ") + what + ")");
        };
        script(plan.base + "_Init", "Demarrage", "seuils des cuves", init);
        script(plan.base + "_Cycle", "Cyclique",
               spec.kind == Installation::PumpingStation ? "disponibilit\xC3\xA9 des pompes, d\xC3\xA9tecteurs de niveau"
               : spec.kind == Installation::ProductionLine ? "marche de la ligne"
               : spec.kind == Installation::TankFarm ? "d\xC3\xA9tecteurs de niveau" : "commande des vannes en automatique",
               cycle);
        if (!scriptLines.empty())
            lines.push_back("**Scripts ST** : " + joinList(scriptLines) + " - compil\xC3\xA9s par la v\xC3\xA9rification, jamais ex\xC3\xA9" "cut\xC3\xA9s par l'Assistant");
    }
    // ---- 7. le symbole et la popup de l'element principal -------------------------------------------------
    const Asset& main = plan.assets[plan.main];
    std::string popup, popupParam, popupNameParam;
    if (!main.empty() && (spec.symbols || spec.popups)) {
        const std::string param = main.def->param;
        Operation::VariableSpec prm{param, main.type, capital(main.def->noun) + " montr\xC3\xA9" "e", ""};
        if (spec.symbols) {
            const std::string name = std::string("Sym_") + main.def->prefix;
            if (const hmi::View* v = pr.viewByName(name); v || b.names.viewTaken(name)) {
                if (v && v->role == "symbole") b.reused.push_back("Symbole " + code(name) + " (existe d\xC3\xA9j\xC3\xA0 : gard\xC3\xA9 tel quel)");
                else b.limits.push_back("Une vue " + code(name) + " existe d\xC3\xA9j\xC3\xA0 (pas un symbole) : le symbole n'est pas cr\xC3\xA9\xC3\xA9.");
            } else if (b.names.viewFree(name)) {
                b.names.views.insert(lower(name));
                Operation& op = b.add(A::View, name, {{"role", "symbole"}, {"largeur", "160"}, {"hauteur", "150"}, {"dossier", plan.base},
                                                      {"description", "Symbole d'une " + std::string(main.def->noun) + " (" + main.type + ")"}});
                op.params = {prm};
                Canvas c = b.canvas(name);
                symbolObjects(b, c, main, param);
                lines.push_back("**Symbole** " + code(name) + " (param\xC3\xA8tre " + param + " : " + main.type + ", "
                                + count(static_cast<std::size_t>(c.objects), "objet li\xC3\xA9", "objets li\xC3\xA9s") + " au param\xC3\xA8tre)");
                b.limits.push_back("Les **instances** de " + code(name) + " se posent depuis la section Symboles de la biblioth\xC3\xA8que : "
                                   "l'Assistant ne pose pas d'instance de symbole (la v\xC3\xA9rification le refuse) - la vue montre les " + main.def->nouns
                                   + " avec les objets du synoptique.");
            }
        }
        if (spec.popups) {
            const std::string name = std::string("Pop_") + main.def->prefix;
            if (const hmi::View* v = pr.viewByName(name); v || b.names.viewTaken(name)) {
                if (v && v->role == "popup") {
                    // une popup existante : ses boutons si elle a un parametre de ce nom, ou un seul parametre du type
                    for (const auto& vp : v->params) {
                        if (iequal(vp.name, param) || (v->params.size() == 1 && iequal(vp.type, main.type))) popupParam = vp.name;
                        if (iequal(vp.name, param + "_Nom") && iequal(vp.type, "STRING")) popupNameParam = vp.name;
                    }
                    b.reused.push_back("Popup " + code(name) + " (existe d\xC3\xA9j\xC3\xA0 : gard\xC3\xA9" "e telle quelle)");
                    if (popupParam.empty()) b.limits.push_back("La popup " + code(name) + " existante n'a pas de param\xC3\xA8tre " + param + " : pas de bouton D\xC3\xA9tails vers elle.");
                    else popup = name;
                } else {
                    b.limits.push_back("Une vue " + code(name) + " existe d\xC3\xA9j\xC3\xA0 (pas une popup) : la popup n'est pas cr\xC3\xA9\xC3\xA9" "e.");
                }
            } else if (b.names.viewFree(name)) {
                b.names.views.insert(lower(name));
                const double h = popupHeight(main);
                Operation& op = b.add(A::View, name, {{"role", "popup"}, {"largeur", "440"}, {"hauteur", num(h)}, {"titre", capital(main.def->noun)},
                                                      {"dossier", plan.base}, {"description", "D\xC3\xA9tail d'une " + std::string(main.def->noun) + " (" + main.type + ")"}});
                Operation::VariableSpec nameParam{param + "_Nom", "STRING", "Le nom montr\xC3\xA9 dans le titre", "''"};
                op.params = {prm, nameParam};
                Canvas c = b.canvas(name);
                popupObjects(b, c, main, param, h);
                popup = name;
                popupParam = param;
                popupNameParam = nameParam.name;
                lines.push_back("**Popup** " + code(name) + " (param\xC3\xA8tre " + param + ", " + count(static_cast<std::size_t>(c.objects), "objet", "objets")
                                + " : les \xC3\xA9tats, les valeurs, Fermer)");
                b.limits.push_back("La popup " + code(name) + " **montre** l'\xC3\xA9tat et les valeurs : ses commandes (marche, mode, consigne) ne se relient pas "
                                   "\xC3\xA0 son param\xC3\xA8tre par l'Assistant (la v\xC3\xA9rification n'accepte comme variable d'une commande qu'une variable du projet) "
                                   "- \xC3\xA0 poser dans l'\xC3\xA9" "diteur.");
            }
        }
    }
    // ---- 8. la vue de supervision ------------------------------------------------------------------------------
    if (spec.view) {
        // la vue du meme nom se complete si c'est la sienne (une vue ordinaire qui montre deja une de ses variables : une
        // conception precedente) ; une autre vue de ce nom (celle de l'utilisateur), un nom pris : Vue_Station_2...
        const auto ours = [&](const hmi::View& v) {
            if (v.role != "vue") return false;
            for (const auto& a : plan.assets)
                for (std::size_t i = 0; i < a.vars.size(); ++i)
                    if (a.existed[i] && viewShows(v, a.vars[i])) return true;
            return false;
        };
        std::string name = "Vue_" + plan.base;
        const hmi::View* ex = pr.viewByName(name);
        for (int k = 2; k < 1000; ++k) {
            if (ex ? ours(*ex) : b.names.viewFree(name)) break;
            name = "Vue_" + plan.base + "_" + std::to_string(k);
            ex = pr.viewByName(name);
        }
        b.names.views.insert(lower(name));
        // ce que la vue existante montre deja (un objet qui cite la variable)
        const auto shown = [&](const std::string& var) { return ex && viewShows(*ex, var); };
        double W = 1280, H = 720;
        const auto width = [&](std::size_t n, double col) { W = std::max(W, 80 + static_cast<double>(n) * col); };
        for (const auto& a : plan.assets) width(a.vars.size(), a.def->kind == hmi::Kind::Conveyor ? 270 : a.def == &loopDef() ? 300 : a.def == &tankDef() ? 220 : 150);
        if (spec.kind == Installation::Regulation) width(plan.assets[1].vars.size(), 300);
        Canvas c = b.canvas(name);
        if (!ex) {
            b.add(A::View, name, {{"largeur", num(W)}, {"hauteur", num(H)}, {"fond", "#151A21"}, {"dossier", plan.base},
                                  {"description", plan.title + " : la vue de supervision (cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA)"}});
            b.object(c, hmi::Kind::Text, "Titre", 40, 20, 700, 40, {{"text", plan.title}, {"fontSize", "26"}});
            if (spec.alarms) b.object(c, hmi::Kind::AlarmBanner, "Bandeau_Alarmes", 40, 70, W - 80, 40, {{"group", plan.base}});
        }
        const auto details = [&](const Asset& a, std::size_t i, double x, double y) {
            if (popup.empty() || &a != &main) return;
            std::string args = popupParam + " := " + a.vars[i];
            if (!popupNameParam.empty()) args += "; " + popupNameParam + " := '" + a.label(i) + "'";
            b.popupButton(c, "Details_" + a.vars[i], x, y, "D\xC3\xA9tails", popup, args);
        };
        const auto bindProps = [](const std::string& var, const std::string& member) -> Props {
            if (member.empty()) return {};
            return {{"variable", var + "." + member}};
        };
        std::size_t added = 0;
        switch (spec.kind) {
            case Installation::PumpingStation: {
                const Asset& t = plan.assets[0];
                const Asset& p = plan.assets[1];
                const std::string anyRun = anyOf(p, p.member("run"));
                for (std::size_t i = 0; i < t.vars.size(); ++i) {
                    if (shown(t.vars[i])) continue;
                    const double x = 60 + static_cast<double>(i) * 220;
                    const std::string& v = t.vars[i];
                    Props pr2 = bindProps(v, t.member("level"));
                    pr2.emplace_back("label", t.label(i));
                    pr2.emplace_back("labelPosition", "dessus");
                    if (const auto hs = t.member("highSet"); !hs.empty()) pr2.emplace_back("high", "=" + v + "." + hs);
                    if (const auto ls = t.member("lowSet"); !ls.empty()) pr2.emplace_back("low", "=" + v + "." + ls);
                    b.object(c, hmi::Kind::Tank, v, x, 150, 140, 170, std::move(pr2));
                    Props pipe{{"rot", "90"}};
                    if (!anyRun.empty()) pipe.emplace_back("value", anyRun);
                    b.object(c, hmi::Kind::Pipe, "Sortie_" + v, x + 45, 335, 50, 20, std::move(pipe));
                    ++added;
                }
                if (!ex && !p.empty()) {
                    Props pipe;
                    if (!anyRun.empty()) pipe.emplace_back("value", anyRun);
                    b.object(c, hmi::Kind::Pipe, "Collecteur", 40, 370, W - 80, 20, std::move(pipe));
                }
                for (std::size_t i = 0; i < p.vars.size(); ++i) {
                    if (shown(p.vars[i])) continue;
                    const double x = 65 + static_cast<double>(i) * 150;
                    const std::string& v = p.vars[i];
                    const std::string run = p.member("run"), fault = p.member("fault");
                    Props pipe{{"rot", "90"}};
                    if (!run.empty()) pipe.emplace_back("value", "=" + v + "." + run);
                    b.object(c, hmi::Kind::Pipe, "Aspiration_" + v, x + 30, 395, 30, 20, std::move(pipe));
                    Props pump = bindProps(v, run);
                    if (!fault.empty()) pump.emplace_back("fault", "=" + v + "." + fault);
                    pump.emplace_back("label", p.label(i));
                    b.object(c, hmi::Kind::Pump, v, x, 420, 90, 80, std::move(pump));
                    if (const auto cur = p.member("current"); !cur.empty()) {
                        Props d = bindProps(v, cur);
                        d.emplace_back("unit", "A");
                        d.emplace_back("fontSize", "18");
                        b.object(c, hmi::Kind::NumericDisplay, "Intensite_" + v, x - 15, 530, 120, 40, std::move(d));
                    }
                    if (const auto av = p.member("available"); !av.empty()) {
                        b.object(c, hmi::Kind::Indicator, "Dispo_" + v, x - 15, 584, 20, 20, bindProps(v, av));
                        b.object(c, hmi::Kind::Text, "Txt_Dispo_" + v, x + 10, 578, 100, 30, {{"text", "Disponible"}, {"fontSize", "13"}});
                    }
                    details(p, i, x - 15, 624);
                    ++added;
                }
                break;
            }
            case Installation::ProductionLine: {
                const Asset& cv = plan.assets[0];
                if (!ex && !plan.extraVar.empty()) {
                    b.object(c, hmi::Kind::Indicator, "Etat_Ligne", 40, 134, 24, 24, {{"variable", plan.extraVar}});
                    b.object(c, hmi::Kind::Text, "Txt_Etat_Ligne", 72, 130, 260, 32, {{"text", "Ligne en marche"}, {"fontSize", "16"}});
                }
                for (std::size_t i = 0; i < cv.vars.size(); ++i) {
                    if (shown(cv.vars[i])) continue;
                    const double x = 40 + static_cast<double>(i) * 270;
                    const std::string& v = cv.vars[i];
                    const std::string run = cv.member("run"), fault = cv.member("fault");
                    Props conv = bindProps(v, run);
                    if (!fault.empty()) conv.emplace_back("fault", "=" + v + "." + fault);
                    conv.emplace_back("label", cv.label(i));
                    b.object(c, hmi::Kind::Conveyor, v, x, 220, 230, 56, std::move(conv));
                    if (i + 1 < cv.vars.size()) {
                        Props arrow;
                        if (!run.empty()) arrow.emplace_back("value", "=" + v + "." + run);
                        b.object(c, hmi::Kind::FlowArrow, "Fleche_" + v, x + 232, 236, 34, 24, std::move(arrow));
                    }
                    if (const auto sp = cv.member("speed"); !sp.empty()) {
                        Props d = bindProps(v, sp);
                        d.emplace_back("unit", "m/min");
                        d.emplace_back("fontSize", "18");
                        b.object(c, hmi::Kind::NumericDisplay, "Vitesse_" + v, x, 320, 140, 40, std::move(d));
                    }
                    if (const auto jam = cv.member("jam"); !jam.empty()) {
                        Props ind = bindProps(v, jam);
                        ind.emplace_back("colorOn", "#E5534B");
                        b.object(c, hmi::Kind::Indicator, "Bourrage_" + v, x, 376, 20, 20, std::move(ind));
                        b.object(c, hmi::Kind::Text, "Txt_Bourrage_" + v, x + 26, 370, 110, 30, {{"text", "Bourrage"}, {"fontSize", "13"}});
                    }
                    if (const auto cnt = cv.member("count"); !cnt.empty()) {
                        Props d = bindProps(v, cnt);
                        d.emplace_back("format", "0");
                        d.emplace_back("unit", "pi\xC3\xA8" "ces");
                        d.emplace_back("fontSize", "18");
                        b.object(c, hmi::Kind::NumericDisplay, "Pieces_" + v, x, 410, 140, 40, std::move(d));
                    }
                    details(cv, i, x, 466);
                    ++added;
                }
                break;
            }
            case Installation::TankFarm: {
                const Asset& t = plan.assets[0];
                const Asset& vl = plan.assets[1];
                for (std::size_t i = 0; i < t.vars.size(); ++i) {
                    if (shown(t.vars[i])) continue;
                    const double x = 60 + static_cast<double>(i) * 220;
                    const std::string& v = t.vars[i];
                    Props tank = bindProps(v, t.member("level"));
                    tank.emplace_back("label", t.label(i));
                    tank.emplace_back("labelPosition", "dessus");
                    if (const auto hs = t.member("highSet"); !hs.empty()) tank.emplace_back("high", "=" + v + "." + hs);
                    if (const auto ls = t.member("lowSet"); !ls.empty()) tank.emplace_back("low", "=" + v + "." + ls);
                    b.object(c, hmi::Kind::Tank, v, x, 150, 140, 200, std::move(tank));
                    if (i < vl.vars.size()) {
                        const std::string& w = vl.vars[i];
                        const std::string open = vl.member("open"), fault = vl.member("fault");
                        Props pipe{{"rot", "90"}};
                        if (!open.empty()) pipe.emplace_back("value", "=" + w + "." + open);
                        b.object(c, hmi::Kind::Pipe, "Sortie_" + v, x + 45, 365, 50, 20, std::move(pipe));
                        Props valve = bindProps(w, open);
                        if (!fault.empty()) valve.emplace_back("fault", "=" + w + "." + fault);
                        valve.emplace_back("label", vl.label(i));
                        b.object(c, hmi::Kind::Valve, w, x + 30, 400, 80, 64, std::move(valve));
                    }
                    if (const auto vo = t.member("volume"); !vo.empty()) {
                        Props d = bindProps(v, vo);
                        d.emplace_back("unit", "m3");
                        d.emplace_back("fontSize", "18");
                        b.object(c, hmi::Kind::NumericDisplay, "Volume_" + v, x, 500, 140, 40, std::move(d));
                    }
                    details(t, i, x + 10, 560);
                    ++added;
                }
                break;
            }
            case Installation::Regulation: {
                const Asset& l = plan.assets[0];
                const Asset& vr = plan.assets[1];
                for (std::size_t i = 0; i < l.vars.size(); ++i) {
                    if (shown(l.vars[i])) continue;
                    const double x = 40 + static_cast<double>(i) * 300;
                    const std::string& v = l.vars[i];
                    b.object(c, hmi::Kind::Frame, "Cadre_" + v, x, 130, 280, 250, {{"title", l.label(i)}});
                    Props isa = bindProps(v, l.member("measure"));
                    isa.emplace_back("function", "PIC");
                    isa.emplace_back("loop", std::to_string(100 + l.index[i]));
                    isa.emplace_back("unit", "");
                    if (const auto f = l.member("fault"); !f.empty()) isa.emplace_back("fault", "=" + v + "." + f);
                    b.object(c, hmi::Kind::IsaInstrument, v, x + 16, 176, 76, 100, std::move(isa));
                    double y = 172;
                    for (const auto& [role, text] : std::initializer_list<std::pair<const char*, const char*>>{{"measure", "Mesure"}, {"setpoint", "Consigne"}, {"output", "Sortie"}}) {
                        const std::string m = l.member(role);
                        if (m.empty()) continue;
                        b.object(c, hmi::Kind::Text, "Txt_" + m + "_" + v, x + 104, y - 2, 70, 30, {{"text", text}, {"fontSize", "13"}});
                        Props d = bindProps(v, m);
                        d.emplace_back("fontSize", "16");
                        d.emplace_back("unit", std::string_view(role) == "output" ? "%" : "");
                        b.object(c, hmi::Kind::NumericDisplay, m + "_" + v, x + 176, y - 4, 92, 34, std::move(d));
                        y += 44;
                    }
                    if (const auto au = l.member("auto"); !au.empty()) {
                        b.object(c, hmi::Kind::Indicator, "Auto_" + v, x + 16, 300, 20, 20, bindProps(v, au));
                        b.object(c, hmi::Kind::Text, "Txt_Auto_" + v, x + 42, 294, 60, 30, {{"text", "Auto"}, {"fontSize", "13"}});
                    }
                    details(l, i, x + 144, 336);
                    ++added;
                }
                for (std::size_t i = 0; i < vr.vars.size(); ++i) {
                    if (shown(vr.vars[i])) continue;
                    const double x = 140 + static_cast<double>(i) * 300;
                    const std::string& v = vr.vars[i];
                    Props valve{{"valveType", "r\xC3\xA9glante"}, {"label", vr.label(i)}};
                    if (const auto pos = vr.member("position"); !pos.empty()) {
                        valve.emplace_back("value", "=" + v + "." + pos + " > 1.0");
                        valve.emplace_back("opening", "=" + v + "." + pos);
                    }
                    if (const auto f = vr.member("fault"); !f.empty()) valve.emplace_back("fault", "=" + v + "." + f);
                    b.object(c, hmi::Kind::Valve, v, x, 440, 80, 64, std::move(valve));
                    if (const auto pos = vr.member("position"); !pos.empty()) {
                        Props d = bindProps(v, pos);
                        d.emplace_back("unit", "%");
                        d.emplace_back("fontSize", "18");
                        b.object(c, hmi::Kind::NumericDisplay, "Position_" + v, x - 30, 540, 140, 40, std::move(d));
                    }
                    ++added;
                }
                break;
            }
        }
        const std::size_t objs = static_cast<std::size_t>(c.objects);
        if (!ex) {
            lines.push_back("**Vue de supervision** " + code(name) + " (" + num(W) + " \xC3\x97 " + num(H) + ", " + count(objs, "objet", "objets")
                            + " : le titre, le bandeau d'alarmes, les " + main.def->nouns + " et leurs mesures li\xC3\xA9" "es aux variables"
                            + (popup.empty() ? std::string{} : ", un bouton D\xC3\xA9tails par " + std::string(main.def->noun) + " qui ouvre " + code(popup)) + ")");
        } else if (added > 0) {
            lines.push_back("**Vue** " + code(name) + " (existante) : " + count(objs, "objet ajout\xC3\xA9", "objets ajout\xC3\xA9s") + " pour "
                            + count(added, "\xC3\xA9l\xC3\xA9ment neuf", "\xC3\xA9l\xC3\xA9ments neufs") + " (\xC3\xA0 leur place dans la grille de la vue)");
            b.reused.push_back("Vue " + code(name) + " (existe d\xC3\xA9j\xC3\xA0 : compl\xC3\xA9t\xC3\xA9" "e, rien n'y est retir\xC3\xA9)");
        } else {
            b.reused.push_back("Vue " + code(name) + " (montre d\xC3\xA9j\xC3\xA0 tous les \xC3\xA9l\xC3\xA9ments)");
        }
    }
    // ---- 9. le rangement : les variables neuves dans le dossier de l'installation --------------------------------
    if (!createdVars.empty()) {
        Operation& op = b.add(A::Store, {}, {{"liste", "variables"}, {"dossier", plan.base}});
        op.objects = createdVars;
        lines.push_back("**Dossiers** : " + code(plan.base) + " (les variables neuves ; la vue, le symbole, la popup et les scripts aussi)");
    }
    // ---- le texte -----------------------------------------------------------------------------------------------
    std::vector<std::string> parts;
    for (const auto& a : plan.assets)
        if (!a.empty()) parts.push_back(count(a.vars.size(), a.def->noun, a.def->nouns));
    std::string text = "## " + plan.title + "\n";
    const std::size_t ops = b.p.ops.size();
    const bool nothing = lines.empty();
    if (nothing) {
        text += "Tout existe d\xC3\xA9j\xC3\xA0 dans le projet (" + joinList(parts) + ") : rien de neuf \xC3\xA0 cr\xC3\xA9" "er.";
    } else {
        text += "Je propose de construire " + the(spec.kind) + " **" + plan.base + "** (" + joinList(parts) + ") en "
                + count(ops, "op\xC3\xA9ration", "op\xC3\xA9rations") + ", appliqu\xC3\xA9" "es d'un seul coup (un seul Ctrl+Z reprend tout) :";
        for (const auto& l : lines) text += "\n- " + l + " ;";
        if (text.size() > 2 && text.compare(text.size() - 2, 2, " ;") == 0) text.replace(text.size() - 2, 2, ".");
    }
    text += tail(b.reused, b.limits);
    return b.finish(plan.title + " (" + joinList(parts) + ")", std::move(text));
}

// =========================================================================================================
//  MODIFIER L'EXISTANT
// =========================================================================================================
namespace {

struct SeriesItem {
    int                  index{0};
    const hmi::Variable* var{nullptr};
};

// Les variables d'une serie (Prefixe_N), triees par numero ; `base` : le prefixe tel qu'il est ecrit dans le projet.
std::vector<SeriesItem> seriesOf(const hmi::Project& pr, std::string_view prefix, std::string* base = nullptr) {
    std::string p = identifierOf(prefix);
    while (!p.empty() && p.back() == '_') p.pop_back();
    const auto collect = [&](const std::string& pre) {
        std::vector<SeriesItem> out;
        for (const auto& v : pr.programs.variables)
            if (const int k = indexIn(v.name, pre); k >= 0) out.push_back({k, &v});
        return out;
    };
    auto out = collect(p);
    if (out.empty() && p.size() > 3 && (p.back() == 's' || p.back() == 'x')) {
        p.pop_back();
        out = collect(p);
    }
    std::sort(out.begin(), out.end(), [](const SeriesItem& a, const SeriesItem& b) { return a.index < b.index; });
    if (base) *base = out.empty() ? p : out.front().var->name.substr(0, p.size());
    return out;
}

// Une variable dite : son nom ("Pompe_2"), ou "pompe 2" ; nul : aucune.
const hmi::Variable* variableNamed(const hmi::Project& pr, std::string_view said) {
    if (const auto* v = pr.variable(trimmed(said))) return v;
    return pr.variable(identifierOf(said));
}

// Un objet cite-t-il la variable (une propriete, une formule, une action, les parametres d'une instance) ?
bool objectRefers(const hmi::Object& o, std::string_view var) {
    for (const auto& pp : o.props)
        if (refersTo(pp.value, var) || refersTo(pp.expr, var)) return true;
    for (const auto& a : o.actions)
        if (refersTo(a.target, var) || refersTo(a.value, var) || refersTo(a.watch, var) || refersTo(a.guard, var)) return true;
    return false;
}

// Les objets du haut d'une vue qui citent la variable (eux ou un de leurs enfants).
std::vector<const hmi::Object*> topObjectsOf(const hmi::View& v, std::string_view var) {
    std::vector<const hmi::Object*> out;
    for (const auto& o : v.objects) {
        if (o.parent != hmi::kNoId) continue;
        bool hit = objectRefers(o, var);
        if (!hit)
            for (const auto id : v.descendantsOf(o.id))
                if (const auto* d = v.object(id); d && objectRefers(*d, var)) hit = true;
        if (hit) out.push_back(&o);
    }
    return out;
}

// Les autres elements de la serie (le modele mis a part).
std::vector<std::string> othersOf(const std::vector<SeriesItem>& series, std::string_view model) {
    std::vector<std::string> out;
    for (const auto& it : series)
        if (!iequal(it.var->name, model)) out.push_back(it.var->name);
    return out;
}

// Un objet du haut (ou un de ses enfants) cite-t-il la variable, ou la porte-t-il dans son nom (Txt_Dispo_Pompe_2) ?
bool treeRefers(const hmi::View& v, const hmi::Object& top, std::string_view var) {
    if (objectRefers(top, var) || hasNamePart(top.name, var)) return true;
    for (const auto id : v.descendantsOf(top.id))
        if (const auto* d = v.object(id); d && (objectRefers(*d, var) || hasNamePart(d->name, var))) return true;
    return false;
}

// LES OBJETS D'UN ELEMENT dans une vue : ceux du haut qui le citent (ou portent son nom) et ne citent aucun autre
// element de la serie. `shared` : ceux qui le citent AVEC d'autres (le collecteur de toutes les pompes) - jamais
// copies ; leur formule s'etend aux copies quand c'est une simple liste (A OR B).
std::vector<const hmi::Object*> ownedObjects(const hmi::View& v, std::string_view var, const std::vector<std::string>& others,
                                             std::vector<const hmi::Object*>* shared = nullptr) {
    std::vector<const hmi::Object*> out;
    for (const auto& o : v.objects) {
        if (o.parent != hmi::kNoId || !treeRefers(v, o, var)) continue;
        const bool withOthers = std::any_of(others.begin(), others.end(), [&](const std::string& x) { return treeRefers(v, o, x); });
        if (!withOthers) out.push_back(&o);
        else if (shared) shared->push_back(&o);
    }
    return out;
}

// "A.m OR B.m" + C -> "A.m OR B.m OR C.m" (les termes qui citent le modele, recopies pour chaque copie) ; rien : la
// formule n'est pas une simple liste.
std::optional<std::string> extendTerms(const std::string& expr, std::string_view model, const std::vector<std::string>& news) {
    for (const std::string sep : {" OR ", " AND ", " + "}) {
        std::vector<std::string> terms;
        std::size_t from = 0;
        for (std::size_t at = expr.find(sep); at != std::string::npos; at = expr.find(sep, from)) {
            terms.push_back(expr.substr(from, at - from));
            from = at + sep.size();
        }
        terms.push_back(expr.substr(from));
        if (terms.size() < 2) continue;
        std::vector<std::string> mine;
        for (const auto& t : terms) {
            if (t.find_first_of("()") != std::string::npos) return std::nullopt;
            if (refersTo(t, model)) mine.push_back(trimmed(t));
        }
        if (mine.empty()) return std::nullopt;
        std::string out = expr;
        for (const auto& n : news)
            for (const auto& t : mine) out += sep + replaceRefs(t, model, n);
        return out;
    }
    return std::nullopt;
}

hmi::Box boxOf(const std::vector<const hmi::Object*>& objs) {
    hmi::Box b{};
    bool first = true;
    for (const auto* o : objs) {
        const hmi::Box x = o->box();
        b = first ? x : b.united(x);
        first = false;
    }
    return b;
}

// Le genre se pose-t-il par l'Assistant (la bibliotheque, ou un groupe) ?
bool placeable(hmi::Kind k) {
    if (k == hmi::Kind::Group) return true;
    const auto* e = registry().find(k);
    return e && !e->family.empty();
}
bool runsCode(hmi::Operation o) noexcept {
    return o == hmi::Operation::RunScript || o == hmi::Operation::CallScript || o == hmi::Operation::Call || o == hmi::Operation::Maths;
}

// Une place libre pres de `near` dans la vue (a droite, dessous, a gauche, dessus), hors des objets du haut et de
// `reserved` ; sinon a droite.
hmi::Box spotNear(const hmi::View& v, const hmi::Box& near, double w, double h, const std::vector<hmi::Box>& reserved) {
    const std::vector<hmi::Box> tries = {{near.right() + 12, near.y, w, h}, {near.x, near.bottom() + 12, w, h}, {near.x - w - 12, near.y, w, h},
                                         {near.x, near.y - h - 12, w, h}, {near.right() + 12, near.bottom() - h, w, h}};
    const auto clear = [&](const hmi::Box& b) {
        if (b.x < 0 || b.y < 0 || b.right() > v.width || b.bottom() > v.height) return false;
        for (const auto& o : v.objects)
            if (o.parent == hmi::kNoId && o.box().intersects(b)) return false;
        for (const auto& r : reserved)
            if (r.intersects(b)) return false;
        return true;
    };
    for (const auto& t : tries)
        if (clear(t)) return t;
    return tries.front();
}

// LA COPIE D'UN ELEMENT (etendre une serie, dupliquer) : `model` -> `name`, sa place `step` fois le pas de la serie.
struct Clone {
    std::string variable;      // le nom neuf
    int         steps{1};      // de combien de pas il est decale du modele (dans chaque vue)
};
struct CloneReport {
    std::size_t              alarms{0}, objects{0}, extended{0};
    std::vector<std::string> views, addresses, scripts;
};

// Le pas d'une serie dans une vue : l'ecart entre les objets du modele et ceux d'un autre element (divise par l'ecart
// des numeros) ; sinon la largeur des objets du modele et une marge.
std::pair<double, double> stepIn(const hmi::View& v, const std::vector<SeriesItem>& series, const hmi::Variable& model, int modelIndex) {
    const auto mine = ownedObjects(v, model.name, othersOf(series, model.name));
    const hmi::Box bm = boxOf(mine);
    if (modelIndex >= 0) {
        // l'element le plus proche qui a des objets dans la vue
        const SeriesItem* best = nullptr;
        for (const auto& it : series) {
            if (it.var == &model || it.index == modelIndex) continue;
            if (ownedObjects(v, it.var->name, othersOf(series, it.var->name)).empty()) continue;
            if (!best || std::abs(it.index - modelIndex) < std::abs(best->index - modelIndex)) best = &it;
        }
        if (best) {
            const hmi::Box bo = boxOf(ownedObjects(v, best->var->name, othersOf(series, best->var->name)));
            const double d = static_cast<double>(modelIndex - best->index);
            const double dx = (bm.x - bo.x) / d, dy = (bm.y - bo.y) / d;
            if (std::abs(dx) > 0.5 || std::abs(dy) > 0.5) return {dx, dy};
        }
    }
    return {bm.w + 20, 0};
}

void cloneItem(Build& b, const hmi::Variable& model, int modelIndex, const std::vector<SeriesItem>& series, const std::vector<Clone>& clones,
               CloneReport& rep) {
    const hmi::Project& pr = b.pr;
    const std::string fromLabel = spaced(model.name);
    // 1. les variables
    {
        Operation& op = b.add(A::Variables);
        const bool elementary = hmi::types::isElementary(model.type);
        for (const auto& c : clones) {
            const std::string desc = model.description.empty() ? "Copie de " + model.name + " (m\xC3\xAAme mod\xC3\xA8le)"
                                                               : replacePhrase(replaceRefs(model.description, model.name, c.variable), fromLabel, spaced(c.variable));
            op.variables.push_back({c.variable, model.type, desc, elementary ? model.initial : std::string{}});
        }
    }
    // 2. l'adresse : le meme equipement, une place libre de meme emprise (apres la serie)
    if (model.bound()) {
        const hmi::Equipment* eq = pr.equipmentByName(model.equipment);
        if (eq && eq->modbus()) {
            long size = 0, end = -1;
            hmi::MemTable table = hmi::MemTable::Holding;
            std::set<std::string> names;
            for (const auto& it : series) names.insert(lower(it.var->name));
            names.insert(lower(model.name));
            for (const auto& r : hmi::audit::ranges(pr, eq->name)) {
                if (names.count(lower(r.variable))) end = std::max<long>(end, static_cast<long>(r.last));
                if (!iequal(r.variable, model.name)) continue;
                table = r.table;
                size = std::max<long>(size, static_cast<long>(r.last) + 1);
            }
            long low = -1;
            for (const auto& r : hmi::audit::ranges(pr, eq->name))
                if (iequal(r.variable, model.name)) low = low < 0 ? static_cast<long>(r.first) : std::min(low, static_cast<long>(r.first));
            if (low >= 0) size -= low;
            const std::string addr = trimmed(model.address);
            const bool mw = addr.size() > 3 && iequal(addr.substr(0, 3), "%MW");
            const bool modicon = addr.size() == 5 && addr[0] == '4' && std::all_of(addr.begin(), addr.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
            Allocator alloc;
            if (table == hmi::MemTable::Holding && size > 0) alloc.free = hmi::audit::freeRanges(pr, eq->name, hmi::MemTable::Holding, 0, 65535);
            for (const auto& c : clones) {
                Operation& op = b.add(A::Address, {}, {{"equipement", eq->name}});
                op.variable = c.variable;
                if (model.readOnly) op.props.emplace_back("lecture_seule", "oui");
                long at = 0;
                if ((mw || modicon) && table == hmi::MemTable::Holding && size > 0 && (alloc.take(size, end + 1, at) || alloc.take(size, 0, at))) {
                    std::string text;
                    if (mw) text = "%MW" + std::to_string(at);
                    else {
                        text = std::to_string(at + 1);
                        while (text.size() < 4) text.insert(text.begin(), '0');
                        text = "4" + text;
                    }
                    op.props.emplace_back("adresse", text);
                    rep.addresses.push_back(c.variable + " " + text);
                } else {
                    rep.addresses.push_back(c.variable + " (la premi\xC3\xA8re place libre de " + eq->name + ")");
                }
            }
            if (!model.places.empty() || !model.internal.empty() || model.scaled())
                b.limits.push_back("Les r\xC3\xA9glages particuliers de " + code(model.name) + " (places de membres, membres internes, mise \xC3\xA0 l'\xC3\xA9" "chelle) "
                                   "ne sont pas recopi\xC3\xA9s : l'op\xC3\xA9ration adresser ne les porte pas.");
        } else if (eq) {
            b.limits.push_back(code(model.name) + " est li\xC3\xA9" "e \xC3\xA0 " + code(eq->name) + " (Ethernet, sans adresse) : les copies ne le sont pas.");
        }
    }
    // 3. les alarmes du modele (celles qui le citent seul : une alarme sur plusieurs elements ne se recopie pas)
    const std::vector<std::string> others = othersOf(series, model.name);
    std::vector<std::string> newNames;
    for (const auto& c : clones) newNames.push_back(c.variable);
    for (const auto& al : pr.alarms) {
        if (!refersTo(al.condition, model.name)) continue;
        if (std::any_of(others.begin(), others.end(), [&](const std::string& x) { return refersTo(al.condition, x); })) {
            b.limits.push_back("L'alarme " + code(al.name) + " cite plusieurs \xC3\xA9l\xC3\xA9ments de la s\xC3\xA9rie : elle n'est pas recopi\xC3\xA9" "e "
                               "(une alarme existante ne se modifie pas par l'Assistant).");
            continue;
        }
        for (const auto& c : clones) {
            std::string name = replaceNamePart(al.name, model.name, c.variable);
            if (name == al.name) {
                // un numero a la fin du nom (Defaut_P2) : celui de la copie ; sinon le nom de la copie a la suite
                std::size_t end = name.size();
                while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
                const int ci = indexIn(c.variable, model.name.substr(0, model.name.rfind('_') == std::string::npos ? 0 : model.name.rfind('_')));
                if (end < name.size() && modelIndex >= 0 && std::to_string(modelIndex) == name.substr(end) && ci >= 0) name = name.substr(0, end) + std::to_string(ci);
                else name += "_" + c.variable;
            }
            const std::string stem = name;
            for (int k = 2; b.names.alarmTaken(name) && k < 1000; ++k) name = stem + "_" + std::to_string(k);
            b.names.alarms.insert(lower(name));
            const std::string toLabel = spaced(c.variable);
            Props props{{"condition", replaceRefs(al.condition, model.name, c.variable)},
                        {"message", replacePhrase(replaceRefs(al.message, model.name, c.variable), fromLabel, toLabel)},
                        {"priorite", std::to_string(std::clamp(al.priority, 1, hmi::kAlarmPriorities))},
                        {"acquittement", al.ackRequired ? "oui" : "non"}};
            if (!al.group.empty()) props.emplace_back("groupe", al.group);
            if (al.delayMs > 0) props.emplace_back("delai", std::to_string(al.delayMs));
            if (!al.category.empty()) props.emplace_back("categorie", al.category);
            if (!al.instruction.empty()) props.emplace_back("consigne", replacePhrase(replaceRefs(al.instruction, model.name, c.variable), fromLabel, toLabel));
            b.add(A::Alarm, name, std::move(props));
            ++rep.alarms;
        }
    }
    // 4. les objets : dans chaque vue qui montre le modele, ses objets copies et decales du pas de la serie
    for (const auto& v : pr.views) {
        std::vector<const hmi::Object*> shared;
        const auto tops = ownedObjects(v, model.name, others, &shared);
        if (tops.empty() && shared.empty()) continue;
        const auto [sx, sy] = stepIn(v, series, model, modelIndex);
        Canvas cv = b.canvas(v.name);
        std::size_t made = 0;
        for (const auto* o : shared) {
            Props changed;
            for (const auto& pp : o->props)
                if (!pp.expr.empty() && refersTo(pp.expr, model.name))
                    if (const auto ne = extendTerms(pp.expr, model.name, newNames)) changed.emplace_back(pp.key, "=" + *ne);
            if (changed.empty()) {
                b.limits.push_back("Dans " + code(v.name) + ", " + code(o->name) + " cite plusieurs \xC3\xA9l\xC3\xA9ments de la s\xC3\xA9rie : il n'est ni copi\xC3\xA9 "
                                   "ni \xC3\xA9tendu - \xC3\xA0 compl\xC3\xA9ter pour " + joinList(newNames) + ".");
                continue;
            }
            Operation& mod = b.add(A::Modify, o->name, std::move(changed));
            mod.target = v.name;
            ++rep.extended;
        }
        for (const auto& c : clones) {
            const double dx = sx * c.steps, dy = sy * c.steps;
            const std::string toLabel = spaced(c.variable);
            std::vector<hmi::Object> copies;
            std::vector<const hmi::Object*> special;   // a dupliquer (une instance de symbole...)
            for (const auto* top : tops) {
                std::vector<const hmi::Object*> tree{top};
                for (const auto id : v.descendantsOf(top->id))
                    if (const auto* d = v.object(id)) tree.push_back(d);
                bool ok = true;
                for (const auto* o : tree) {
                    if (!placeable(o->kind) || !o->functionOverrides.empty() || !o->alarmOverrides.empty()) ok = false;
                    for (const auto& a : o->actions)
                        if (runsCode(a.operation)) ok = false;
                }
                if (!ok) {
                    special.push_back(top);
                    continue;
                }
                for (const auto* o : tree) {
                    hmi::Object x = *o;
                    for (auto& pp : x.props) {
                        pp.value = replacePhrase(replaceRefs(pp.value, model.name, c.variable), fromLabel, toLabel);
                        pp.expr = replaceRefs(pp.expr, model.name, c.variable);
                    }
                    for (auto& a : x.actions) {
                        a.target = replaceRefs(a.target, model.name, c.variable);
                        a.value = replacePhrase(replaceRefs(a.value, model.name, c.variable), fromLabel, toLabel);
                        a.watch = replaceRefs(a.watch, model.name, c.variable);
                        a.guard = replaceRefs(a.guard, model.name, c.variable);
                    }
                    x.setNumber("x", x.number("x") + dx);
                    x.setNumber("y", x.number("y") + dy);
                    std::string wanted = replaceNamePart(o->name, model.name, c.variable);
                    if (wanted == o->name) wanted = o->name + "_" + c.variable;
                    x.name = Build::objectName(cv, wanted);
                    x.locked = false;
                    copies.push_back(std::move(x));
                }
            }
            made += copies.size();
            b.codeObjects(cv, std::move(copies));
            for (const auto* top : special) {
                // DUPLIQUER (le geste Ctrl+D garde tout) puis MODIFIER ce qui cite le modele
                std::string wanted = replaceNamePart(top->name, model.name, c.variable);
                if (wanted == top->name) wanted = top->name + "_" + c.variable;
                wanted = Build::objectName(cv, wanted);
                Operation& dup = b.add(A::Duplicate);
                dup.objects = {top->name};
                dup.names = {wanted};
                dup.dx = dx;
                dup.dy = dy;
                dup.target = v.name;
                ++made;
                Props changed;
                for (const auto& pp : top->props) {
                    if (pp.key == "x" || pp.key == "y") continue;
                    const std::string nv = replaceRefs(pp.value, model.name, c.variable), ne = replaceRefs(pp.expr, model.name, c.variable);
                    if (!pp.expr.empty() && ne != pp.expr) changed.emplace_back(pp.key, "=" + ne);
                    else if (nv != pp.value) changed.emplace_back(pp.key, nv);
                }
                if (!changed.empty()) {
                    Operation& mod = b.add(A::Modify, wanted, std::move(changed));
                    mod.target = v.name;
                }
                bool more = !v.descendantsOf(top->id).empty();
                for (const auto& a : top->actions) more = more || refersTo(a.value, model.name) || refersTo(a.target, model.name);
                if (more)
                    b.limits.push_back("Dans " + code(v.name) + ", la copie de " + code(top->name) + " garde " + code(model.name)
                                       + " dans ses actions ou ses enfants (seules ses propri\xC3\xA9t\xC3\xA9s se modifient) : \xC3\xA0 v\xC3\xA9rifier.");
            }
        }
        if (made > 0 || !shared.empty()) {
            rep.objects += made;
            rep.views.push_back(v.name);
        }
    }
    // 5. les scripts qui citent le modele : jamais reecrits
    for (const auto& s : pr.programs.scripts)
        if (refersTo(s.body, model.name)) rep.scripts.push_back(s.name);
    for (const auto& f : pr.programs.functions)
        if (refersTo(f.body, model.name)) rep.scripts.push_back(f.name);
}

std::string cloneText(const CloneReport& rep, const std::vector<std::string>& names) {
    std::string s;
    s += "\n- **Variables** : " + codes(names);
    if (!rep.addresses.empty()) s += "\n- **Adresses** : " + joinList(rep.addresses);
    s += "\n- **Alarmes** : " + (rep.alarms ? std::to_string(rep.alarms) + " (celles du mod\xC3\xA8le, recopi\xC3\xA9" "es)" : std::string("aucune (le mod\xC3\xA8le n'en a pas)"));
    s += "\n- **Objets** : " + (rep.objects ? count(rep.objects, "objet", "objets") + " dans " + count(rep.views.size(), "vue", "vues") + " (" + codes(rep.views) + "), d\xC3\xA9" "cal\xC3\xA9s du pas de la s\xC3\xA9rie"
                                          : std::string("aucun (aucune vue ne montre le mod\xC3\xA8le)"));
    if (rep.extended)
        s += "\n- **Objets communs** : " + count(rep.extended, "objet qui cite", "objets qui citent") + " toute la s\xC3\xA9rie (un collecteur...) : "
             + (rep.extended > 1 ? "leur formule s'\xC3\xA9tend" : "sa formule s'\xC3\xA9tend") + " aux copies";
    return s;
}

std::vector<std::string> scriptLimits(const std::vector<std::string>& scripts, const std::string& model) {
    std::vector<std::string> out;
    if (!scripts.empty())
        out.push_back((scripts.size() > 1 ? "Les scripts " : "Le script ") + codes(scripts) + (scripts.size() > 1 ? " lisent " : " lit ") + code(model)
                      + " : un script existant n'est pas r\xC3\xA9\xC3\xA9" "crit par l'Assistant - y ajouter les copies \xC3\xA0 la main si besoin.");
    return out;
}

} // namespace

Design extendSeries(const hmi::Project& pr, std::string_view prefix, int wanted) {
    Build b(pr);
    std::string base;
    const auto series = seriesOf(pr, prefix, &base);
    if (series.empty())
        return b.finish("\xC3\x89tendre une s\xC3\xA9rie", "Je ne trouve aucune s\xC3\xA9rie " + code(identifierOf(prefix) + "_1") + ", " + code(identifierOf(prefix) + "_2")
                                                           + "\xE2\x80\xA6 dans les variables du projet : rien \xC3\xA0 \xC3\xA9tendre.");
    const int n = std::clamp(wanted, 1, 50);
    const SeriesItem& model = series.back();
    std::vector<Clone> clones;
    std::vector<std::string> names, skipped;
    for (int k = model.index + 1; static_cast<int>(clones.size()) < n && k < model.index + 10000; ++k) {
        const std::string name = base + "_" + std::to_string(k);
        if (!b.names.variableFree(name)) {
            skipped.push_back(name);
            continue;
        }
        b.names.vars.insert(lower(name));
        clones.push_back({name, k - model.index});
        names.push_back(name);
    }
    std::vector<std::string> members;
    for (const auto& it : series) members.push_back(it.var->name);
    CloneReport rep;
    cloneItem(b, *model.var, model.index, series, clones, rep);
    std::string text = "## \xC3\x89tendre la s\xC3\xA9rie " + base + "\n";
    text += "La s\xC3\xA9rie compte **" + count(series.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + "** (" + codes(members) + ") ; mod\xC3\xA8le : **" + model.var->name
            + "** (" + model.var->type + "). Je propose **" + count(clones.size(), "\xC3\xA9l\xC3\xA9ment de plus", "\xC3\xA9l\xC3\xA9ments de plus") + "** sur le m\xC3\xAAme mod\xC3\xA8le :";
    text += cloneText(rep, names);
    if (!skipped.empty()) b.limits.push_back("D\xC3\xA9j\xC3\xA0 pris, saut\xC3\xA9s : " + codes(skipped) + ".");
    for (auto& l : scriptLimits(rep.scripts, model.var->name)) b.limits.push_back(std::move(l));
    text += tail(b.reused, b.limits);
    return b.finish("\xC3\x89tendre la s\xC3\xA9rie " + base + " (" + joinList(names) + ")", std::move(text));
}

Design duplicateItem(const hmi::Project& pr, std::string_view from, std::string_view to) {
    Build b(pr);
    const hmi::Variable* model = variableNamed(pr, from);
    if (!model) return b.finish("Dupliquer", "Je ne trouve pas " + code(identifierOf(from)) + " dans les variables du projet : rien \xC3\xA0 dupliquer.");
    // le nom neuf, ecrit comme le modele ("cuve 4" -> Cuve_4)
    std::string name = identifierOf(to);
    const auto cut = model->name.rfind('_');
    const std::string base = cut == std::string::npos ? model->name : model->name.substr(0, cut);
    if (iequal(name.substr(0, std::min(name.size(), base.size())), base) && name.size() > base.size()) name = base + name.substr(base.size());
    if (name.empty() || !hmi::isIdentifier(name))
        return b.finish("Dupliquer", "Quel nom pour la copie de " + code(model->name) + " ? (" + code(std::string(to)) + " n'est pas un nom de variable valable)");
    if (b.pr.variable(name)) return b.finish("Dupliquer", code(name) + " existe d\xC3\xA9j\xC3\xA0 : je ne le remplace pas. Choisis un autre nom.");
    if (!b.names.variableFree(name)) return b.finish("Dupliquer", code(name) + " est d\xC3\xA9j\xC3\xA0 pris (une fonction, une vue ou un mot r\xC3\xA9serv\xC3\xA9).");
    b.names.vars.insert(lower(name));
    const auto series = seriesOf(pr, base);
    const int mi = indexIn(model->name, base), ni = indexIn(name, base);
    const int steps = mi >= 0 && ni >= 0 && ni != mi ? ni - mi : 1;
    CloneReport rep;
    cloneItem(b, *model, mi, series, {{name, steps}}, rep);
    std::string text = "## Dupliquer " + model->name + " en " + name + "\n";
    text += "Je propose une copie de **" + model->name + "** (" + model->type + ") nomm\xC3\xA9" "e **" + name + "** :";
    text += cloneText(rep, {name});
    for (auto& l : scriptLimits(rep.scripts, model->name)) b.limits.push_back(std::move(l));
    text += tail(b.reused, b.limits);
    return b.finish("Dupliquer " + model->name + " en " + name, std::move(text));
}

Design alarmOnSeries(const hmi::Project& pr, std::string_view prefix, std::string_view member, std::string_view comparison, std::string_view threshold,
                     std::string_view label) {
    Build b(pr);
    std::string base;
    const auto series = seriesOf(pr, prefix, &base);
    if (series.empty())
        return b.finish("Alarmes sur une s\xC3\xA9rie", "Je ne trouve aucune s\xC3\xA9rie " + code(identifierOf(prefix) + "_1") + "\xE2\x80\xA6 dans les variables du projet.");
    // la comparaison
    std::string cmp = trimmed(comparison);
    const std::string fc = fold(cmp);
    if (fc.rfind("haut", 0) == 0 || fc.rfind("sup", 0) == 0 || fc == "depasse") cmp = ">";
    else if (fc.rfind("bas", 0) == 0 || fc.rfind("inf", 0) == 0 || fc == "sous") cmp = "<";
    else if (fc == "egal" || fc == "==") cmp = "=";
    static const std::set<std::string> ops = {">", ">=", "<", "<=", "=", "<>", ""};
    if (!ops.count(cmp)) return b.finish("Alarmes sur une s\xC3\xA9rie", "Comparaison " + code(std::string(comparison)) + " inconnue (>, >=, <, <=, =, <>).");
    const std::string thr = trimmed(threshold);
    double d = 0;
    if (!cmp.empty() && thr.empty()) return b.finish("Alarmes sur une s\xC3\xA9rie", "Quel seuil ? Par exemple " + code(base + "_1." + identifierOf(member) + " " + cmp + " 80") + ".");
    if (!thr.empty() && !hmi::roles::readNumber(thr, d) && !pr.variable(thr))
        return b.finish("Alarmes sur une s\xC3\xA9rie", "Le seuil " + code(thr) + " n'est ni un nombre ni une variable du projet.");
    const bool number = !thr.empty() && hmi::roles::readNumber(thr, d);
    // le seuil ecrit comme le membre compare : 80.0 pour un REAL, 80 pour un entier
    const auto literal = [&](std::string_view type) {
        if (!number) return thr;
        const std::string n = num(d);
        const bool real = iequal(type, "REAL") || iequal(type, "LREAL");
        if (real) return n.find('.') == std::string::npos ? n + ".0" : n;
        return n;
    };
    std::string lab = identifierOf(label);
    if (lab.empty()) lab = identifierOf(std::string(member) + (cmp == ">" || cmp == ">=" ? "_Haut" : cmp == "<" || cmp == "<=" ? "_Bas" : ""));
    if (lab.empty()) lab = "Alarme";
    // le groupe : celui des alarmes deja posees sur la serie, sinon le nom de la serie
    std::string group;
    for (const auto& al : pr.alarms)
        for (const auto& it : series)
            if (group.empty() && !al.group.empty() && refersTo(al.condition, it.var->name)) group = al.group;
    if (group.empty()) group = base;
    std::vector<std::string> made, kept, without;
    const std::string wantedMember = fold(identifierOf(member));
    for (const auto& it : series) {
        const hmi::Variable& v = *it.var;
        std::string subject = v.name;
        bool numeric = numericType(v.type), boolean = boolType(v.type);
        std::string subjectType = v.type;
        if (!wantedMember.empty()) {
            std::string found;
            if (const auto* t = pr.hmiTypeByName(v.type))
                for (const auto& m : t->members)
                    if (fold(m.name) == wantedMember || fold(spaced(m.name)) == fold(member)) {
                        found = m.name;
                        numeric = numericType(m.type);
                        boolean = boolType(m.type);
                        subjectType = m.type;
                    }
            if (!found.empty()) subject = v.name + "." + found;
            else if (const auto* x = pr.variable(v.name + "_" + identifierOf(member))) {   // une variable de mesure a cote (Cuve_1_Temperature)
                subject = x->name;
                numeric = numericType(x->type);
                boolean = boolType(x->type);
                subjectType = x->type;
            } else {
                without.push_back(v.name);
                continue;
            }
        }
        if ((cmp.empty() && !boolean) || (!cmp.empty() && cmp != "=" && cmp != "<>" && !numeric)) {
            without.push_back(v.name + " (" + subject + " n'est pas " + (cmp.empty() ? "un BOOL" : "un nombre") + ")");
            continue;
        }
        const std::string condition = cmp.empty() ? subject : subject + " " + cmp + " " + literal(subjectType);
        const std::string name = v.name + "_" + lab;
        if (b.names.alarmTaken(name)) {
            kept.push_back(name);
            continue;
        }
        b.names.alarms.insert(lower(name));
        const std::string msg = spaced(v.name) + " : " + lower(spaced(lab)) + " (" + condition + ")";
        b.add(A::Alarm, name, {{"condition", condition}, {"message", msg}, {"priorite", "2"}, {"groupe", group}});
        made.push_back(name);
    }
    std::vector<std::string> members;
    for (const auto& it : series) members.push_back(it.var->name);
    std::string text = "## Alarme " + spaced(lab) + " sur la s\xC3\xA9rie " + base + "\n";
    text += "La s\xC3\xA9rie compte **" + count(series.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + "** (" + codes(members) + "). ";
    if (made.empty()) text += "Aucune alarme \xC3\xA0 cr\xC3\xA9" "er.";
    else
        text += "Je propose **" + count(made.size(), "alarme", "alarmes") + "** (" + codes(made, 6) + "), priorit\xC3\xA9 2, groupe " + code(group)
                + " ; la condition de chacune est compil\xC3\xA9" "e avant d'\xC3\xAAtre propos\xC3\xA9" "e.";
    if (!kept.empty()) b.reused.push_back(count(kept.size(), "alarme existe", "alarmes existent") + " d\xC3\xA9j\xC3\xA0 : " + codes(kept, 6));
    if (!without.empty())
        b.limits.push_back("Sans " + (member.empty() ? std::string("valeur adapt\xC3\xA9" "e") : "membre " + code(capital(identifierOf(member)))) + " : " + joinList(without, 6)
                           + " - pas d'alarme (un type existant ne gagne pas de membre par l'Assistant : l'ajouter dans Types IHM, ou demander une mesure).");
    text += tail(b.reused, b.limits);
    return b.finish("Alarme " + spaced(lab) + " sur " + base, std::move(text));
}

namespace {
// "A := x; B := 'y'" -> les paires (nom, valeur).
std::vector<std::pair<std::string, std::string>> splitParams(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string cur;
    bool quoted = false;
    const auto flush = [&] {
        const auto at = cur.find(":=");
        if (at != std::string::npos) out.emplace_back(trimmed(cur.substr(0, at)), trimmed(cur.substr(at + 2)));
        cur.clear();
    };
    for (const char c : text) {
        if (c == '\'') quoted = !quoted;
        if (c == ';' && !quoted) {
            flush();
            continue;
        }
        cur += c;
    }
    flush();
    return out;
}
} // namespace

Design replaceSymbol(const hmi::Project& pr, std::string_view prefix, std::string_view newSymbol) {
    Build b(pr);
    const std::string target = trimmed(newSymbol);
    const hmi::View* sym = nullptr;
    for (const auto& v : pr.views)
        if (iequal(v.name, target)) sym = &v;
    if (!sym || sym->role != "symbole")
        return b.finish("Remplacer un symbole", "Je ne trouve pas le symbole " + code(target) + " dans le projet" + (sym ? std::string(" (c'est une vue, pas un symbole)") : std::string{})
                                                    + " : rien n'est remplac\xC3\xA9.");
    std::string base;
    const auto series = seriesOf(pr, prefix, &base);
    const std::string p = identifierOf(prefix);
    std::vector<std::string> done, skipped, already;
    std::set<std::string> views;
    for (const auto& v : pr.views)
        for (const auto& o : v.objects) {
            if (o.kind != hmi::Kind::SymbolInstance) continue;
            const std::string current = o.text("symbol"), params = o.text("params");
            bool hit = indexIn(o.name, base) >= 0 || indexIn(o.name, p) >= 0 || iequal(o.name, p) || iequal(current, p);
            for (const auto& it : series) hit = hit || refersTo(params, it.var->name);
            if (!hit) continue;
            const std::string where = v.name + "." + o.name;
            if (iequal(current, sym->name)) {
                already.push_back(where);
                continue;
            }
            // les parametres suivent : meme nom ; ou un seul de chaque cote
            const hmi::View* old = pr.viewByName(current);
            auto pairs = splitParams(params);
            std::vector<std::string> missing;
            for (auto& [k, val] : pairs) {
                if (sym->param(k)) continue;
                if (pairs.size() == 1 && sym->params.size() == 1 && (!old || old->params.size() == 1)) k = sym->params.front().name;
                else missing.push_back(k);
            }
            if (!missing.empty()) {
                skipped.push_back(where + " (" + (missing.size() > 1 ? "ses param\xC3\xA8tres " : "son param\xC3\xA8tre ") + joinList(missing) + " n'existe"
                                  + (missing.size() > 1 ? "nt" : "") + " pas dans " + sym->name + ")");
                continue;
            }
            std::string np;
            for (const auto& [k, val] : pairs) np += (np.empty() ? "" : "; ") + k + " := " + val;
            Props props{{"symbol", sym->name}};
            if (np != params && !pairs.empty()) props.emplace_back("params", np);
            Operation& op = b.add(A::Modify, o.name, std::move(props));
            op.target = v.name;
            done.push_back(where);
            views.insert(v.name);
        }
    std::string text = "## Remplacer le symbole par " + sym->name + "\n";
    const std::size_t concerned = done.size() + skipped.size() + already.size();
    if (concerned > 0) text += count(concerned, "instance de symbole concern\xC3\xA9" "e", "instances de symbole concern\xC3\xA9" "es") + " : ";
    if (done.empty()) text += concerned > 0 ? "aucune \xC3\xA0 changer" : "Aucune instance \xC3\xA0 changer";
    else
        text += "**" + count(done.size(), "instance", "instances") + "** dans " + count(views.size(), "vue", "vues") + " prennent le symbole **" + sym->name + "** : "
                + joinList(done, 10) + " (leur place, leur taille et leurs param\xC3\xA8tres sont gard\xC3\xA9s)";
    if (done.empty() && skipped.empty() && already.empty()) text += " : aucune instance de symbole ne cite la s\xC3\xA9rie " + code(p) + ".";
    else text += ".";
    if (!already.empty()) b.reused.push_back(count(already.size(), "instance a", "instances ont") + " d\xC3\xA9j\xC3\xA0 ce symbole : " + joinList(already, 6));
    if (!skipped.empty()) b.limits.push_back("Laiss\xC3\xA9" "es telles quelles : " + joinList(skipped, 6) + ".");
    if (done.empty() && skipped.empty() && already.empty())
        b.limits.push_back("Les objets du synoptique (une pompe, une cuve...) ne changent pas de genre par l'Assistant : seules les instances de symbole changent de symbole.");
    text += tail(b.reused, b.limits);
    return b.finish("Remplacer le symbole par " + sym->name, std::move(text));
}

namespace {
struct MeasureDef {
    const char* word;      // replie
    const char* member;    // le nom du membre / de la variable
    const char* noun;      // "pression"
    const char* unit;
    const char* isa;       // la fonction ISA
};
const MeasureDef* measureOf(std::string_view what) {
    static const std::vector<MeasureDef> k = {
        {"pression", "Pression", "pression", "bar", "PT"},       {"temperature", "Temperature", "temp\xC3\xA9rature", "\xC2\xB0" "C", "TT"},
        {"debit", "Debit", "d\xC3\xA9" "bit", "m3/h", "FT"},     {"niveau", "Niveau", "niveau", "%", "LT"},
        {"courant", "Courant", "courant", "A", "IT"},            {"intensite", "Courant", "intensit\xC3\xA9", "A", "IT"},
        {"vitesse", "Vitesse", "vitesse", "tr/min", "ST"},       {"vibration", "Vibration", "vibration", "mm/s", "VT"},
        {"humidite", "Humidite", "humidit\xC3\xA9", "%", "MT"},
    };
    const std::string f = fold(trimmed(what));
    for (const auto& m : k)
        if (f.rfind(m.word, 0) == 0 || f.find(std::string(" ") + m.word) != std::string::npos) return &m;
    return nullptr;
}
} // namespace

Design addMeasure(const hmi::Project& pr, std::string_view on, std::string_view what) {
    Build b(pr);
    const hmi::Variable* host = variableNamed(pr, on);
    if (!host) return b.finish("Ajouter une mesure", "Je ne trouve pas " + code(identifierOf(on)) + " dans les variables du projet.");
    const MeasureDef* m = measureOf(what);
    if (!m)
        return b.finish("Ajouter une mesure", "Quelle mesure ? pression, temp\xC3\xA9rature, d\xC3\xA9" "bit, niveau, courant, vitesse, vibration ou humidit\xC3\xA9 (" + code(std::string(what))
                                                  + " n'en est pas une que je connais).");
    // le membre du type s'il existe ; sinon une variable de mesure a cote
    std::string subject;
    if (const auto* t = pr.hmiTypeByName(host->type))
        for (const auto& mm : t->members)
            if (fold(mm.name) == fold(m->member) && numericType(mm.type)) subject = host->name + "." + mm.name;
    std::vector<std::string> lines;
    bool created = false;
    if (subject.empty()) {
        std::string name = host->name + "_" + m->member;
        if (const auto* x = pr.variable(name); x && numericType(x->type)) {
            subject = x->name;
            b.reused.push_back("Variable " + code(x->name) + " : " + x->type + " (existe d\xC3\xA9j\xC3\xA0)");
        } else {
            for (int k = 2; !b.names.variableFree(name) && k < 1000; ++k) name = host->name + "_" + m->member + "_" + std::to_string(k);
            b.names.vars.insert(lower(name));
            Operation& op = b.add(A::Variables);
            op.variables.push_back({name, "REAL", "Mesure de " + std::string(m->noun) + " de " + host->name + " (" + m->unit + ")", "0"});
            subject = name;
            created = true;
            lines.push_back("**Variable** " + code(name) + " : REAL (" + m->unit + ")");
            // son adresse : l'equipement de l'element, une place libre apres lui
            if (host->bound())
                if (const auto* eq = pr.equipmentByName(host->equipment); eq && eq->modbus()) {
                    long end = -1;
                    for (const auto& r : hmi::audit::ranges(pr, eq->name))
                        if (iequal(r.variable, host->name) && r.table == hmi::MemTable::Holding) end = std::max<long>(end, static_cast<long>(r.last));
                    Allocator alloc = allocatorFor(pr, eq->name);
                    long at = 0;
                    Operation& ad = b.add(A::Address, {}, {{"equipement", eq->name}});
                    ad.variable = name;
                    ad.props.emplace_back("lecture_seule", "oui");
                    const long size = b.sizeOf("REAL");
                    if (alloc.take(size, end + 1, at) || alloc.take(size, 0, at)) {
                        ad.props.emplace_back("adresse", "%MW" + std::to_string(at));
                        lines.push_back("**Adresse** : " + code(eq->name) + " %MW" + std::to_string(at) + " (lecture seule)");
                    } else {
                        lines.push_back("**Adresse** : " + code(eq->name) + ", la premi\xC3\xA8re place libre");
                    }
                }
        }
    } else {
        b.reused.push_back("Membre " + code(subject) + " (le type " + host->type + " l'a d\xC3\xA9j\xC3\xA0)");
    }
    // un instrument pres de l'element, dans chaque vue qui le montre
    std::vector<std::string> views;
    std::vector<std::string> others;
    if (const auto cut = host->name.rfind('_'); cut != std::string::npos) others = othersOf(seriesOf(pr, host->name.substr(0, cut)), host->name);
    for (const auto& v : pr.views) {
        auto tops = ownedObjects(v, host->name, others);   // ses objets a lui (pas le collecteur de toute la serie)
        if (tops.empty()) tops = topObjectsOf(v, host->name);
        if (tops.empty()) continue;
        Canvas c = b.canvas(v.name);
        const hmi::Box at = spotNear(v, boxOf(tops), 76, 100, {});
        b.object(c, hmi::Kind::IsaInstrument, std::string(m->isa) + "_" + host->name, at.x, at.y, at.w, at.h,
                 {{"variable", subject}, {"function", m->isa}, {"unit", m->unit}, {"label", spaced(host->name)}});
        views.push_back(v.name);
    }
    if (!views.empty()) lines.push_back("**Instrument** " + std::string(m->isa) + " pr\xC3\xA8s de " + host->name + " dans " + count(views.size(), "vue", "vues") + " (" + codes(views) + ")");
    else b.limits.push_back("Aucune vue ne montre " + code(host->name) + " : la mesure est cr\xC3\xA9\xC3\xA9" "e sans objet (\xC3\xA0 poser ensuite).");
    if (created) b.limits.push_back("Le type " + code(host->type) + " ne gagne pas de membre : la mesure est une variable \xC3\xA0 part, " + code(subject) + ".");
    std::string text = "## Mesure de " + std::string(m->noun) + " sur " + host->name + "\n";
    if (lines.empty()) text += "Rien \xC3\xA0 ajouter.";
    else {
        text += "Je propose :";
        for (const auto& l : lines) text += "\n- " + l;
    }
    text += tail(b.reused, b.limits);
    return b.finish("Mesure de " + std::string(m->noun) + " sur " + host->name, std::move(text));
}

Design moveToFolder(const hmi::Project& pr, std::string_view listKey, std::string_view filter, std::string_view folder) {
    Build b(pr);
    std::string list = fold(trimmed(listKey));
    if (list == "vue") list = "vues";
    if (list == "popup") list = "popups";
    if (list == "symbole") list = "symboles";
    if (list == "script") list = "scripts";
    if (list == "type") list = "types";
    if (list == "variable") list = "variables";
    if (list == "ressource") list = "ressources";
    const std::string path = trimmed(folder);
    if (path.empty()) return b.finish("Ranger", "Dans quel dossier ? Par exemple " + code("Deplace toutes les vues de production dans le dossier Production") + ".");
    const std::string f = fold(trimmed(filter));
    const auto matches = [&](std::string_view name, std::string_view in, std::string_view description) {
        return f.empty() || fold(name).find(f) != std::string::npos || fold(in).find(f) != std::string::npos || fold(description).find(f) != std::string::npos;
    };
    std::vector<std::string> names, already;
    bool exists = false;
    if (list == "variables") {
        for (const auto& v : pr.programs.variables)
            if (matches(v.name, v.folder, v.description)) (hmi::fold::sameFolder(v.folder, path) ? already : names).push_back(v.name);
        for (const auto& d : pr.programs.folders) exists = exists || hmi::fold::sameFolder(d, path);
    } else {
        const auto l = hmi::fold::fromKey(list);
        if (!l) return b.finish("Ranger", "Liste " + code(std::string(listKey)) + " inconnue (vues, popups, symboles, scripts, types, styles, ressources, variables).");
        for (const auto& it : hmi::fold::items(pr, *l)) {
            std::string description;
            if (const auto* v = pr.viewByName(it.name)) description = v->description;
            if (matches(it.name, it.folder, description)) (hmi::fold::sameFolder(it.folder, path) ? already : names).push_back(it.name);
        }
        for (const auto& d : hmi::fold::allFolders(pr, *l)) exists = exists || hmi::fold::sameFolder(d, path);
    }
    std::string text = "## Ranger dans " + path + "\n";
    if (names.empty()) {
        text += "Aucun \xC3\xA9l\xC3\xA9ment de la liste " + list + (f.empty() ? std::string{} : " ne r\xC3\xA9pond \xC3\xA0 " + code(trimmed(filter))) + (already.empty() ? std::string{} : " hors du dossier") + " : rien \xC3\xA0 d\xC3\xA9placer.";
    } else {
        if (!exists) b.add(A::Folder, {}, {{"liste", list}, {"dossier", path}});
        Operation& op = b.add(A::Store, {}, {{"liste", list}, {"dossier", path}});
        op.objects = names;
        text += "**" + count(names.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + "** de la liste " + list + " concern\xC3\xA9" + (names.size() > 1 ? "s" : "") + " : "
                + codes(names, 12) + " - d\xC3\xA9plac\xC3\xA9" + (names.size() > 1 ? "s" : "") + " dans **" + path + "**" + (exists ? std::string{} : " (le dossier est cr\xC3\xA9\xC3\xA9)")
                + ". Leurs noms ne changent pas.";
    }
    if (!already.empty()) b.reused.push_back(count(already.size(), "\xC3\xA9l\xC3\xA9ment est", "\xC3\xA9l\xC3\xA9ments sont") + " d\xC3\xA9j\xC3\xA0 dans ce dossier : " + codes(already, 6));
    text += tail(b.reused, b.limits);
    return b.finish("Ranger dans " + path, std::move(text));
}

} // namespace app::ai::design
