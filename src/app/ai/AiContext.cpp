// =============================================================================
//  app/ai/AiContext.cpp - le contexte d'une demande (voir AiContext.hpp)
// =============================================================================
#include "AiContext.hpp"

#include "AiIncidentAnswers.hpp"         // 1.20.0 : les incidents que la demande evoque
#include "../../hmi/HmiRelations.hpp"   // 1.19.0 : ce qui utilise les elements nommes
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiGuide.hpp"   // fold : la demande sans accents ni casse
#include "../../hmi/HmiFolders.hpp"      // 1.18.0 : les dossiers des listes
#include "../../hmi/HmiNatives.hpp"      // 1.18.0 : les natives que la demande evoque
#include "../../hmi/HmiPublicVars.hpp"   // 1.18.0 : les variables systeme
#include "../../hmi/HmiScript.hpp"       // 1.18.0 : la signature d'une fonction IHM

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace app::ai {

namespace {

// Une valeur montree au modele : sur une ligne, coupee si elle est longue (sans couper une lettre).
std::string clip(std::string_view v, std::size_t max) {
    std::string s;
    for (const char c : v) {
        if (c == '\n') s += "\\n";
        else if (c != '\r') s += c;
    }
    if (s.size() > max) {
        std::size_t cut = max > 3 ? max - 3 : max;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut) + "...";
    }
    return s;
}

std::string num(double d) { return hmi::formatNumber(std::round(d * 10.0) / 10.0); }

// Les mots d'un texte (sans accents ni casse ; un nom Pompe_1_Marche ou PompeMarche coupe en mots).
std::vector<std::string> wordsOf(std::string_view text) {
    std::string spaced;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(text[i - 1]))) spaced += ' ';
        spaced += c;
    }
    const std::string folded = hmi::guide::fold(spaced);
    std::vector<std::string> out;
    std::string w;
    for (const char c : folded) {
        if (std::isalnum(static_cast<unsigned char>(c))) w += c;
        else {
            if (w.size() >= 3) out.push_back(w);
            w.clear();
        }
    }
    if (w.size() >= 3) out.push_back(w);
    return out;
}

// Combien de mots d'une variable la demande cite (le debut d'un mot suffit : "pomp" et "pompe").
std::size_t relevance(const std::vector<std::string>& asked, const hmi::Variable& v) {
    std::size_t n = 0;
    auto mine = wordsOf(v.name);
    for (auto& w : wordsOf(v.description)) mine.push_back(std::move(w));
    for (const auto& a : asked)
        for (const auto& m : mine) {
            const std::size_t k = std::min<std::size_t>(4, std::min(a.size(), m.size()));
            if (a == m || (k >= 4 && a.compare(0, k, m, 0, k) == 0)) {
                ++n;
                break;
            }
        }
    return n;
}

const char* roleName(std::string_view role) {
    if (role == "popup") return "popup";
    if (role == "symbole") return "symbole";
    if (role == "modele") return "\xC3\xA9" "cran mod\xC3\xA8le";
    if (role == "entete") return "mod\xC3\xA8le d'en-t\xC3\xAAte";
    if (role == "pied") return "mod\xC3\xA8le de pied";
    return "vue";
}

// ---- la vue et ses objets -------------------------------------------------------------
void viewPart(const PromptInput& in, const ContextLimits& lim, ContextInfo& info, std::string& s) {
    const hmi::View& v = *in.view;
    s += "Vue active : " + v.name + " (" + std::to_string(v.width) + " x " + std::to_string(v.height) + " px, fond " + v.background;
    if (v.role != "vue") s += std::string(", ") + roleName(v.role);
    s += ")";
    if (!v.layers.empty()) {
        s += " ; calques (du dessous au dessus) :";
        bool first = true;
        for (const auto& l : v.layers) {
            s += first ? " " : ", ";
            first = false;
            s += l.name + (l.id == v.activeLayer ? " (actif : les objets neufs y vont)" : "");
        }
    }
    s += ".\n";
    info.objects = v.objects.size();
    if (!in.settings.sendView) {
        s += "Les objets de la vue ne sont PAS envoy\xC3\xA9s (r\xC3\xA9glage de confidentialit\xC3\xA9 de l'utilisateur) : tu ne les connais pas. "
             "Pour modifier un objet existant, demande son nom exact ; pour cr\xC3\xA9" "er, place les objets dans la vue (sa taille ci-dessus).\n";
        return;
    }
    info.viewSent = true;
    if (v.objects.empty()) {
        s += "La vue est vide (aucun objet).\n";
        return;
    }
    // L'ordre : la selection d'abord, puis les plus proches d'elle ; sans selection, l'ordre de la vue.
    std::vector<const hmi::Object*> order;
    for (const auto& o : v.objects) order.push_back(&o);
    std::set<hmi::Id> selected;
    if (in.settings.sendSelection)
        for (const auto id : in.selection)
            if (v.object(id)) selected.insert(id);
    if (!selected.empty()) {
        const hmi::Box sel = hmi::edit::selectionBounds(v, std::vector<hmi::Id>(selected.begin(), selected.end()));
        const auto dist = [&](const hmi::Object* o) {
            const hmi::Box b = hmi::edit::rotatedBounds(*o);
            return std::hypot(b.cx() - sel.cx(), b.cy() - sel.cy());
        };
        std::stable_sort(order.begin(), order.end(), [&](const hmi::Object* a, const hmi::Object* b) {
            const bool sa = selected.count(a->id) != 0, sb = selected.count(b->id) != 0;
            if (sa != sb) return sa;
            return !sa && dist(a) < dist(b);
        });
    }
    // Les lignes, choisies dans cet ordre tant que le budget le permet, montrees dans l'ordre de dessin.
    std::map<const hmi::Object*, std::string> chosen;
    std::size_t used = 0;
    std::vector<const hmi::Object*> rest;
    for (const auto* o : order) {
        if (used >= lim.objectBytes) {
            rest.push_back(o);
            continue;
        }
        const hmi::Box b = o->box();
        std::string line = o->name + " | " + std::string(hmi::kindKey(o->kind)) + " | " + num(b.x) + "," + num(b.y) + "," + num(b.w) + "," + num(b.h);
        const std::string changed = changedProps(*o);
        if (!changed.empty()) line += " | " + changed;
        if (o->parent != hmi::kNoId)
            if (const auto* g = v.object(o->parent)) line += " | dans " + g->name;
        if (v.layers.size() > 1)
            if (const auto* l = v.layer(o->layer)) line += " | calque " + l->name;
        if (o->locked) line += " | verrouill\xC3\xA9";
        if (o->hidden) line += " | cach\xC3\xA9 dans l'\xC3\xA9" "diteur";
        if (!o->actions.empty()) line += " | " + std::to_string(o->actions.size()) + (o->actions.size() > 1 ? " actions" : " action");
        used += line.size() + 1;
        chosen[o] = std::move(line);
        ++info.detailed;
    }
    std::string lines;
    for (const auto& o : v.objects)
        if (const auto it = chosen.find(&o); it != chosen.end()) lines += it->second + "\n";
    if (rest.empty()) {
        s += "Objets de la vue (" + std::to_string(v.objects.size()) + "), dans l'ordre de dessin : nom | genre | x,y,w,h | les propri\xC3\xA9t\xC3\xA9s "
             "qui diff\xC3\xA8rent de la cr\xC3\xA9" "ation par d\xC3\xA9" "faut du genre (fx(...) : pilot\xC3\xA9" "e par une formule).\n";
        s += lines;
        return;
    }
    info.summarized = true;
    s += "Objets de la vue (" + std::to_string(v.objects.size()) + ") : la vue est grande, les " + std::to_string(info.detailed)
         + (selected.empty() ? " premiers dans l'ordre de dessin sont d\xC3\xA9taill\xC3\xA9s"
                             : " plus proches de la s\xC3\xA9lection (elle comprise) sont d\xC3\xA9taill\xC3\xA9s")
         + ", dans l'ordre de dessin (nom | genre | x,y,w,h | propri\xC3\xA9t\xC3\xA9s qui diff\xC3\xA8rent du d\xC3\xA9" "faut) ; les "
         + std::to_string(rest.size()) + " autres ne sont que nomm\xC3\xA9s, par genre, plus bas. Pour en modifier un, son nom suffit.\n";
    s += lines;
    // Les autres : leurs noms, par genre (dans l'ordre de la vue).
    std::vector<hmi::Kind> kinds;
    std::map<hmi::Kind, std::vector<const hmi::Object*>> byKind;
    for (const auto& o : v.objects)
        if (std::find(rest.begin(), rest.end(), &o) != rest.end()) {
            if (!byKind.count(o.kind)) kinds.push_back(o.kind);
            byKind[o.kind].push_back(&o);
        }
    std::string names;
    std::size_t left = rest.size();
    for (const auto k : kinds) {
        if (names.size() >= lim.nameBytes) break;
        const auto& list = byKind[k];
        names += std::string(hmi::kindKey(k)) + " (" + std::to_string(list.size()) + ") :";
        bool first = true;
        for (const auto* o : list) {
            if (names.size() >= lim.nameBytes) break;
            names += (first ? " " : ", ") + o->name;
            first = false;
            ++info.named;
            --left;
        }
        names += "\n";
    }
    s += "Objets nomm\xC3\xA9s seulement :\n" + names;
    if (left > 0) s += "... et " + std::to_string(left) + " autres objets (ni d\xC3\xA9taill\xC3\xA9s ni nomm\xC3\xA9s ici : demande leur nom si la demande les vise).\n";
}

void selectionPart(const PromptInput& in, ContextInfo& info, std::string& s) {
    if (!in.settings.sendSelection) {
        s += "La s\xC3\xA9lection n'est PAS envoy\xC3\xA9" "e (r\xC3\xA9glage de confidentialit\xC3\xA9) : si la demande parle de \xC2\xAB cet objet \xC2\xBB, "
             "\xC2\xAB celle de droite \xC2\xBB, demande de quel objet il s'agit (son nom).\n";
        return;
    }
    info.selectionSent = true;
    std::vector<const hmi::Object*> sel;
    if (in.view)
        for (const auto id : in.selection)
            if (const auto* o = in.view->object(id)) sel.push_back(o);
    if (sel.empty()) {
        s += "S\xC3\xA9lection : aucune.\n";
        return;
    }
    std::stable_sort(sel.begin(), sel.end(), [](const hmi::Object* a, const hmi::Object* b) {
        return hmi::edit::rotatedBounds(*a).cx() < hmi::edit::rotatedBounds(*b).cx();
    });
    s += "S\xC3\xA9lection (" + std::to_string(sel.size()) + ", de gauche \xC3\xA0 droite) :";
    bool first = true;
    for (const auto* o : sel) {
        s += (first ? " " : ", ") + o->name + " (" + std::string(hmi::kindKey(o->kind)) + ")";
        first = false;
    }
    s += ".\n";
}

void variablesPart(const PromptInput& in, const ContextLimits& lim, ContextInfo& info, std::string& s) {
    const auto& vars = in.project->programs.variables;
    info.variables = vars.size();
    std::vector<std::pair<std::string, std::string>> plc;
    if (in.env.plcVariables) plc = in.env.plcVariables();
    info.plc = plc.size();
    if (!in.settings.sendVariables) {
        s += "Les variables ne sont PAS envoy\xC3\xA9" "es (r\xC3\xA9glage de confidentialit\xC3\xA9) : n'en cite aucune de toi-m\xC3\xAAme ; pour associer "
             "une variable, demande son nom exact \xC3\xA0 l'utilisateur (le logiciel v\xC3\xA9rifie qu'elle existe).\n";
        return;
    }
    info.variablesSent = true;
    if (vars.empty()) {
        s += "Variables de l'IHM : aucune (le projet n'en a pas encore : n'en invente pas ; l'utilisateur peut en cr\xC3\xA9" "er).\n";
    } else {
        // Les plus proches de la demande d'abord (un grand projet ne tient pas en entier).
        const auto asked = wordsOf(in.request);
        std::vector<std::pair<std::size_t, const hmi::Variable*>> ranked;
        for (const auto& v : vars) ranked.emplace_back(relevance(asked, v), &v);
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string lines;
        std::set<std::string> types;
        for (const auto& [score, v] : ranked) {
            if (lines.size() >= lim.variableBytes) break;
            lines += v->name + " : " + v->type;
            if (!v->description.empty()) lines += " - " + clip(v->description, 50);
            lines += "\n";
            types.insert(v->type);
            ++info.variablesListed;
        }
        s += "Variables de l'IHM (" + std::to_string(vars.size()) + ") : nom : type - description (jamais leur valeur ; un membre d'une structure "
             "s'\xC3\xA9" "crit Variable.Membre) :\n" + lines;
        if (info.variablesListed < vars.size())
            s += "... et " + std::to_string(vars.size() - info.variablesListed)
                 + " autres, non list\xC3\xA9" "es (les plus proches de la demande sont ci-dessus) : demande le nom exact si besoin.\n";
        // Les structures de l'IHM qu'elles utilisent : leurs membres.
        std::string typeLines;
        for (const auto& t : in.project->programs.types) {
            if (!types.count(t.name) || t.members.empty() || typeLines.size() >= 1500) continue;
            typeLines += t.name + " :";
            bool first = true;
            for (const auto& m : t.members) {
                typeLines += (first ? " " : ", ") + m.name + " " + m.type;
                first = false;
            }
            typeLines += "\n";
        }
        if (!typeLines.empty()) s += "Structures de l'IHM utilis\xC3\xA9" "es (membres) :\n" + typeLines;
    }
    if (!plc.empty()) {
        std::string lines;
        for (const auto& [name, type] : plc) {
            if (lines.size() >= lim.plcBytes) break;
            lines += name + (type.empty() ? std::string{} : " : " + type) + "\n";
            ++info.plcListed;
        }
        const std::string source = in.env.plcSource ? in.env.plcSource() : std::string{};
        if (source.empty())
            s += "Variables de l'automate reli\xC3\xA9 (" + std::to_string(plc.size()) + ") :\n" + lines;
        else   // 1.16.0 : un fichier lu (IHM Studio) - pour creer des variables IHM, pas pour associer
            s += "Variables globales de l'automate lu dans " + source + " (" + std::to_string(plc.size()) + ") - elles ne s'associent PAS "
                 "directement : pour s'en servir, propose d'abord creer_variables (le nom IHM est sans \"API.\", le type converti) :\n" + lines;
        if (info.plcListed < plc.size())
            s += "... et " + std::to_string(plc.size() - info.plcListed) + " autres de l'automate, non list\xC3\xA9" "es.\n";
    } else if (in.env.plcName) {
        s += "Un automate est reli\xC3\xA9 : ses variables existent aussi (non list\xC3\xA9" "es ici) ; n'en cite que si l'utilisateur te donne le nom.\n";
    }
    // 1.16.0 : les types IHM du projet (leurs noms : une variable creee peut en etre).
    {
        const auto& types = in.project->programs.types;
        info.hmiTypes = types.size();
        if (!types.empty()) {
            std::string names;
            std::size_t left = 0;
            for (const auto& t : types) {
                if (names.size() >= 1200) {
                    ++left;
                    continue;
                }
                names += (names.empty() ? "" : ", ") + t.name;
            }
            s += "Types IHM du projet (" + std::to_string(types.size()) + ") : " + names + (left ? " ... et " + std::to_string(left) + " autres" : std::string{}) + ".\n";
        }
    }
    // 1.16.0 : les types (DDT) de l'automate relie, resumes : quand la demande parle de types, de structures,
    // de DDT ou de l'automate, ou qu'il y en a peu - pour que l'IA propose des types IHM du meme nom.
    if (in.env.plcTypes) {
        const auto ddts = in.env.plcTypes();
        info.plcTypes = ddts.size();
        std::string lowered;
        for (const char c : in.request) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool asked = false;
        for (const char* w : {"type", "ddt", "structure", "automate", "api", "plc", "variables de l"})
            if (lowered.find(w) != std::string::npos) asked = true;
        if (!ddts.empty() && (asked || ddts.size() <= 10)) {
            std::string lines;
            for (const auto& t : ddts) {
                if (lines.size() >= lim.plcTypeBytes) break;
                lines += t.name + " :";
                bool first = true;
                for (const auto& m : t.members) {
                    lines += (first ? " " : ", ") + m.name + " " + m.type;
                    first = false;
                }
                lines += "\n";
                ++info.plcTypesListed;
            }
            const std::string source = in.env.plcSource ? in.env.plcSource() : std::string{};
            s += "Types (DDT) de l'automate " + (source.empty() ? std::string("reli\xC3\xA9") : "lu dans " + source) + " (" + std::to_string(ddts.size())
                 + ") : nom : membres (pour cr\xC3\xA9" "er des types IHM du "
                 "m\xC3\xAAme nom, \xC3\xA0 la demande) :\n" + lines;
            if (info.plcTypesListed < ddts.size())
                s += "... et " + std::to_string(ddts.size() - info.plcTypesListed) + " autres DDT, non list\xC3\xA9s.\n";
        } else if (!ddts.empty()) {
            s += "L'automate reli\xC3\xA9 a " + std::to_string(ddts.size()) + " types (DDT), non list\xC3\xA9s ici (demande-les si on te parle de types).\n";
        }
    }
}

void viewsPart(const PromptInput& in, const ContextLimits& lim, std::string& s) {
    std::map<std::string, std::vector<std::string>> byRole;
    std::vector<std::string> roles;
    for (const auto& v : in.project->views) {
        if (in.view && v.id == in.view->id) continue;
        const std::string role = roleName(v.role);
        if (!byRole.count(role)) roles.push_back(role);
        byRole[role].push_back(v.name);
    }
    if (roles.empty()) {
        s += "Autres vues du projet : aucune.\n";
        return;
    }
    std::string text;
    std::size_t left = 0;
    for (const auto& role : roles) {
        bool first = true;
        for (const auto& n : byRole[role]) {
            if (text.size() >= lim.viewBytes) {
                ++left;
                continue;
            }
            if (first) text += (text.empty() ? "" : " ; ") + role + (byRole[role].size() > 1 ? "s" : "") + " :";
            text += (first ? " " : ", ") + n;
            first = false;
        }
    }
    s += "Autres vues du projet (leurs noms : une navigation, une popup les citent) : " + text;
    if (left > 0) s += " ; ... et " + std::to_string(left) + " autres";
    s += ".\n";
}

// 1.18.0 : CE QUE LES OUTILS DE L'IA LISENT - les elements du projet que ses operations citent (un equipement a adresser, une
// ressource, un dossier ou ranger), les variables systeme (SYS.*) et les natives : celles que la demande evoque (un mot commun),
// et la liste de leurs familles. Chaque partie a sa limite (rien ne deborde la fenetre du modele).
void toolsPart(const PromptInput& in, const ContextLimits& lim, std::string& s) {
    const auto& p = *in.project;
    const std::size_t part = std::max<std::size_t>(300, lim.toolBytes / 6);
    std::vector<std::string> empty;
    const auto line = [&](const char* title, const std::vector<std::string>& items) {
        if (items.empty()) {
            empty.push_back(title);
            return;
        }
        std::string text;
        std::size_t left = 0;
        for (const auto& it : items) {
            if (text.size() + it.size() > part) {
                ++left;
                continue;
            }
            text += (text.empty() ? "" : ", ") + it;
        }
        s += std::string(title) + " : " + text + (left > 0 ? " ... et " + std::to_string(left) + " autres" : std::string{}) + ".\n";
    };
    {
        std::vector<std::string> eq;
        for (const auto& e : p.equipments)
            eq.push_back(e.name + " (" + std::string(hmi::equipmentTypeLabel(e.type)) + (e.host.empty() ? std::string{} : " " + e.host + ":" + std::to_string(e.port))
                         + (e.modbus() ? ", esclave " + std::to_string(e.unit) : std::string{}) + ")");
        line("\xC3\x89quipements", eq);
        std::vector<std::string> rs, sc, fn, al;
        for (const auto& r : p.assets.resources) rs.push_back(r.name);
        for (const auto& x : p.programs.scripts) sc.push_back(x.name);
        for (const auto& x : p.programs.functions) fn.push_back(hmi::functionSignature(x));
        for (const auto& x : p.alarms) al.push_back(x.name);
        line("Ressources", rs);
        line("Scripts g\xC3\xA9n\xC3\xA9raux", sc);
        line("Fonctions IHM", fn);
        line("Alarmes", al);
        std::string dirs;
        for (int i = 0; i < static_cast<int>(hmi::fold::List::Count); ++i) {
            const auto l = static_cast<hmi::fold::List>(i);
            const auto all = hmi::fold::allFolders(p, l);
            if (all.empty()) continue;
            dirs += (dirs.empty() ? "" : " ; ") + std::string(hmi::fold::key(l)) + " :";
            for (std::size_t k = 0; k < all.size() && k < 8; ++k) dirs += (k ? ", " : " ") + all[k];
        }
        if (!p.programs.folders.empty()) {
            dirs += (dirs.empty() ? "" : " ; ") + std::string("variables :");
            for (std::size_t k = 0; k < p.programs.folders.size() && k < 8; ++k) dirs += (k ? ", " : " ") + p.programs.folders[k];
        }
        if (dirs.empty()) empty.push_back("Dossiers");
        else s += "Dossiers : " + clip(dirs, part) + ".\n";
        if (!empty.empty()) {
            std::string t;
            for (const auto& e : empty) t += (t.empty() ? "" : ", ") + e;
            s += "Aucun(e) : " + t + ".\n";
        }
    }
    // Les variables systeme et les natives : seulement celles que la demande evoque (un mot commun) - jamais toutes.
    const auto asked = wordsOf(in.request);
    const auto evokes = [&](std::string_view a, std::string_view b) {
        const auto ws = wordsOf(std::string(a) + " " + std::string(b));
        for (const auto& w : ws)
            for (const auto& q : asked)
                if (q.size() >= 4 && w.size() >= 4 && (w.rfind(q, 0) == 0 || q.rfind(w, 0) == 0)) return true;
        return false;
    };
    std::string sys, nat;
    for (const auto& v : hmi::pub::kSysVars) {
        if (!hmi::pub::sysDomainShown(static_cast<std::size_t>(v.domain)) || !evokes(v.name, v.text)) continue;
        const std::string it = "\n  SYS." + std::string(v.name) + " : " + std::string(v.type) + " - " + clip(v.text, 90);
        if (sys.size() + it.size() > lim.sysBytes) break;
        sys += it;
    }
    for (const auto& f : hmi::natives::functions()) {
        if (!evokes(f.name, f.summary)) continue;
        std::string sig = "\n  " + std::string(f.name) + "(";
        for (std::size_t k = 0; k < f.params.size(); ++k) sig += (k ? ", " : "") + std::string(f.params[k].name) + " : " + std::string(f.params[k].type);
        sig += ")" + (f.returns.empty() ? std::string{} : " : " + std::string(f.returns)) + " - " + clip(f.summary, 90);
        if (nat.size() + sig.size() > lim.nativeBytes) break;
        nat += sig;
    }
    if (!sys.empty()) s += "Variables syst\xC3\xA8me que la demande \xC3\xA9voque (en lecture) :" + sys + "\n";
    if (!nat.empty()) s += "Fonctions natives que la demande \xC3\xA9voque :" + nat + "\n";
    // 1.19.0 : LES RELATIONS DES ELEMENTS QUE LA DEMANDE NOMME (une variable, un type, une vue... ecrits tels quels) - lues
    // dans le graphe du projet (hmi::rel), pour que l'IA reponde sur ce qui l'utilise sans rien inventer. Bornees.
    if (in.project) {
        std::vector<std::string> named;
        std::string req;
        for (const char c : in.request) req += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        const auto mentioned = [&](const std::string& n) {
            if (n.size() < 4 || named.size() >= 3) return;
            std::string ln;
            for (const char c : n) ln += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (std::size_t at = req.find(ln); at != std::string::npos; at = req.find(ln, at + 1))
                if ((at == 0 || !word(req[at - 1])) && (at + ln.size() >= req.size() || !word(req[at + ln.size()]))) {
                    named.push_back(n);
                    return;
                }
        };
        for (const auto& v : in.project->programs.variables) mentioned(v.name);
        for (const auto& t : in.project->programs.types) mentioned(t.name);
        for (const auto& v : in.project->views) mentioned(v.name);
        for (const auto& e : in.project->equipments) mentioned(e.name);
        if (!named.empty()) {
            const hmi::rel::Graph g = hmi::rel::build(*in.project);
            std::string rel;
            for (const auto& n : named) {
                const auto all = g.findAll(n);
                if (all.empty()) continue;
                const auto users = hmi::rel::users(g, all.front());
                std::string line = "\n  " + n + " : ";
                if (users.empty()) line += "rien ne l'utilise";
                for (std::size_t k = 0; k < users.size() && k < 5; ++k) line += (k ? ", " : "") + hmi::rel::describe(g, users[k]);
                if (users.size() > 5) line += "... (" + std::to_string(users.size()) + " en tout)";
                line = clip(line, 200);
                if (rel.size() + line.size() > 560) break;
                rel += line;
            }
            if (!rel.empty()) s += "Ce qui utilise les \xC3\xA9l\xC3\xA9ments nomm\xC3\xA9s (graphe du projet) :" + rel + "\n";
        }
    }
}

// 1.20.0 : LES INCIDENTS, SEULEMENT QUAND LA DEMANDE EN PARLE (<< pourquoi cette alarme... >>, << diagnostic >>...) - les
// trois derniers dossiers qu'elle concerne, resumes (date, alarme, resume, valeurs, pistes et leur certitude,
// equipements, alarmes liees), au plus 6 Ko : jamais tout le projet ni toute la fiche. Une demande ordinaire n'en recoit
// rien (la taille de son contexte ne change pas).
void incidentsPart(const PromptInput& in, std::string& s) {
    if (!in.incidents || !diagnosis::isIncidentQuestion(in.request)) return;
    const auto list = in.incidents();
    static const std::vector<incidents::Item> none;
    s += diagnosis::contextBlock(in.request, list ? *list : none, 6000, 3);
}

} // namespace

std::vector<hmi::Kind> kindsInView(const hmi::View& v) {
    std::vector<hmi::Kind> out;
    for (const auto& o : v.objects)
        if (std::find(out.begin(), out.end(), o.kind) == out.end()) out.push_back(o.kind);
    return out;
}

std::string changedProps(const hmi::Object& o, std::size_t maxValue) {
    const hmi::Object base = hmi::makeObject(o.kind, 1, "x", 0, 0, hmi::kNoId);
    std::string s;
    for (const auto& p : o.props) {
        if (p.key == "x" || p.key == "y" || p.key == "w" || p.key == "h") continue;
        const hmi::Prop* b = base.find(p.key);
        if (p.expr.empty() && b && b->value == p.value) continue;
        s += (s.empty() ? "" : " ; ") + p.key + "=" + (p.expr.empty() ? clip(p.value, maxValue) : "fx(" + clip(p.expr, maxValue) + ")");
    }
    return s;
}

std::string contextText(const PromptInput& in, const ContextLimits& lim, ContextInfo* infoOut) {
    ContextInfo info;
    std::string s = "## Contexte (au moment de la demande)\n";
    s += "Projet : " + (in.projectName.empty() ? std::string("(sans nom)") : in.projectName);
    if (in.project && !in.project->config.name.empty() && in.project->config.name != in.projectName) s += " (IHM \xC2\xAB " + in.project->config.name + " \xC2\xBB)";
    s += ".\n";
    if (!in.project) {
        s += "Aucun projet IHM n'est ouvert : tu ne peux rien proposer (dis-le).\n";
        if (infoOut) *infoOut = info;
        return s;
    }
    if (in.view) viewPart(in, lim, info, s);
    else
        s += "Aucune vue n'est ouverte dans l'\xC3\xA9" "diteur : pour modifier une vue, demande de l'ouvrir (double-clic sur une vue de "
             "l'arbre du projet) ; tu peux quand m\xC3\xAAme proposer une vue neuve (creer_vue en premier), des variables ou des types IHM.\n";
    selectionPart(in, info, s);
    variablesPart(in, lim, info, s);
    viewsPart(in, lim, s);
    toolsPart(in, lim, s);   // 1.18.0
    incidentsPart(in, s);    // 1.20.0
    if (infoOut) *infoOut = info;
    return s;
}

} // namespace app::ai
