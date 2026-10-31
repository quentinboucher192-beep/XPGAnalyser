// =============================================================================
//  app/ai/AiProjectAnswers.cpp - 1.19.0 : les relations, la verification, les adresses
// =============================================================================
#include "AiProjectAnswers.hpp"

#include "../../hmi/HmiFolders.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace app::ai::answers {

namespace {

namespace rel = hmi::rel;
using rel::Kind;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) { return a.size() == b.size() && lower(a) == lower(b); }

std::string code(std::string_view s) { return "`" + std::string(s) + "`"; }

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Les mots de la demande : lettres (accents compris), chiffres, _ et . (Cuve_1.Niveau).
std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '.' || u >= 0x80) cur += c;
        else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    for (auto& w : out)
        while (!w.empty() && w.back() == '.') w.pop_back();
    std::erase_if(out, [](const std::string& w) { return w.empty(); });
    return out;
}

bool number(std::string_view w) { return !w.empty() && std::all_of(w.begin(), w.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }); }

// Les mots de la question elle-meme : jamais un sujet, meme si un element porte ce nom.
bool questionWord(std::string_view w) {
    static const std::set<std::string> k = {"quels", "quelles", "quel", "quelle", "objets", "objet", "variables", "variable", "alarmes",
                                            "alarme", "scripts", "script", "fonctions", "fonction", "vue", "vues", "cette", "ce", "cet",
                                            "type", "types", "utilisent", "utilise", "concernent", "concerne", "orphelins", "orphelin",
                                            "equipements", "montre", "moi", "les", "des", "la", "le", "de", "du", "si", "je", "supprime",
                                            "dans", "sont", "qui", "projet", "mon", "tout", "est", "utilises", "utilisees"};
    return k.count(lower(w)) > 0;
}

// Le rang d'un genre pour departager deux elements du meme nom (une variable avant l'objet qui porte son nom).
int kindRank(Kind k, const std::vector<Kind>& prefer) {
    if (const auto it = std::find(prefer.begin(), prefer.end(), k); it != prefer.end()) return static_cast<int>(it - prefer.begin());
    static const Kind order[] = {Kind::Variable, Kind::Type, Kind::Equipment, Kind::View, Kind::Symbol, Kind::Popup, Kind::Template,
                                 Kind::Alarm,    Kind::Script, Kind::Function, Kind::Resource, Kind::Object, Kind::Folder, Kind::Recipe};
    for (std::size_t i = 0; i < std::size(order); ++i)
        if (order[i] == k) return 100 + static_cast<int>(i);
    return 200;
}

std::optional<std::size_t> best(const rel::Graph& g, std::string_view name, const std::vector<Kind>& prefer) {
    std::optional<std::size_t> pick;
    for (const auto i : g.findAll(name)) {
        if (!iequal(g.node(i).name, name) && g.node(i).kind != Kind::Object) continue;
        if (!pick || kindRank(g.node(i).kind, prefer) < kindRank(g.node(*pick).kind, prefer)) pick = i;
    }
    return pick;
}

// La ligne d'un element trouve, et ou il le fait (l'arete qui va vers le sujet).
std::string line(const rel::Graph& g, std::size_t node, std::string_view subject) {
    std::string where;
    for (const auto e : g.out(node)) {
        const auto& edge = g.edges()[e];
        const std::string to = lower(g.node(edge.to).name), p = lower(edge.path), s = lower(subject);
        if (to == s || p == s || p.rfind(s + ".", 0) == 0 || p.rfind(s + "[", 0) == 0 || (!p.empty() && s.rfind(p + ".", 0) == 0)) {
            where = edge.where.empty() ? std::string{} : " \xE2\x80\x94 " + edge.where + (edge.path.empty() ? std::string{} : " (" + edge.path + ")");
            break;
        }
    }
    return "- " + rel::describe(g, node) + where + "\n";
}

std::string list(const rel::Graph& g, const std::vector<std::size_t>& nodes, std::string_view subject, std::size_t max = 30) {
    std::string out;
    for (std::size_t k = 0; k < nodes.size() && k < max; ++k) out += line(g, nodes[k], subject);
    if (nodes.size() > max) out += "- \xE2\x80\xA6 et " + std::to_string(nodes.size() - max) + " autres.\n";
    return out;
}

const char* kLimits = "*(Lu dans le graphe des relations du projet : les noms \xC3\xA9" "crits dans les propri\xC3\xA9t\xC3\xA9s, les formules, les actions et "
                      "le code. Un nom calcul\xC3\xA9 en marche - une cible `=expression` - n'y est pas suivi.)*";

std::vector<std::string> nearNames(const rel::Graph& g, std::string_view said, Kind kind, std::size_t max = 6) {
    std::vector<std::string> out;
    const std::string s = lower(said);
    for (const auto& n : g.nodes())
        if (n.kind == kind && (s.empty() || lower(n.name).find(s.substr(0, std::min<std::size_t>(s.size(), 4))) != std::string::npos))
            if (out.size() < max) out.push_back(code(n.name));
    return out;
}

std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
}

// Une proposition d'adresses (adresser) : les corrections ou le plan d'optimisation, a valider.
Proposal addressProposal(const hmi::Project& project, const std::vector<std::pair<std::string, std::string>>& moves, std::string summary) {
    Proposal p;
    p.summary = std::move(summary);
    std::set<std::string> seen;
    for (const auto& [variable, after] : moves) {
        const hmi::Variable* v = project.variable(variable);
        if (!v || v->equipment.empty() || !seen.insert(lower(variable)).second) continue;
        Operation op;
        op.action = Operation::Action::Address;
        op.variable = v->name;
        op.props.emplace_back("equipement", v->equipment);
        op.props.emplace_back("adresse", after);
        p.ops.push_back(std::move(op));
    }
    return p;
}

} // namespace

// ---- le sujet ------------------------------------------------------------------------------------------------------
std::string subjectOf(const rel::Graph& g, std::string_view request, const std::vector<Kind>& prefer) {
    const auto w = words(request);
    // 1. << la pompe 2 >>, << cuve 1 >> : le mot et le nombre (Pompe_2, Pompe2)
    for (std::size_t i = 0; i + 1 < w.size(); ++i)
        if (!number(w[i]) && number(w[i + 1]) && !questionWord(w[i]))
            for (const std::string& cand : {w[i] + "_" + w[i + 1], w[i] + w[i + 1]})
                if (const auto n = best(g, cand, prefer)) return g.node(*n).name;
    // 2. un nom ecrit tel quel : ceux qui portent _ , . ou un chiffre d'abord (un vrai nom de projet), puis les autres
    for (const int pass : {0, 1}) {
        std::optional<std::size_t> pick;
        for (const auto& word : w) {
            if (questionWord(word)) continue;
            const bool strong = word.find_first_of("_.0123456789") != std::string::npos;
            if ((pass == 0) != strong || word.size() < 3) continue;
            if (const auto n = best(g, word, prefer); n && (!pick || kindRank(g.node(*n).kind, prefer) < kindRank(g.node(*pick).kind, prefer)))
                pick = n;
        }
        if (pick) return g.node(*pick).name;
        // un membre : Cuve_1.Niveau (le graphe a la variable Cuve_1)
        if (pass == 0)
            for (const auto& word : w)
                if (const auto dot = word.find('.'); dot != std::string::npos && dot > 0)
                    if (best(g, word.substr(0, dot), {Kind::Variable})) return word;
    }
    return {};
}

// ---- les relations -------------------------------------------------------------------------------------------------
Answer relations(const hmi::Project& project, Question q, std::string_view subjectIn, std::string_view openView) {
    const rel::Graph g = rel::build(project);
    Answer a;
    std::string subject(subjectIn);
    if (q == Question::Orphans) {
        const auto o = rel::orphans(g);
        if (o.empty()) {
            a.text = "**Aucun \xC3\xA9l\xC3\xA9ment orphelin** : chaque variable, symbole, popup, fonction, script sur appel, ressource, \xC3\xA9quipement "
                     "et type est employ\xC3\xA9 quelque part.\n" + std::string(kLimits);
            return a;
        }
        a.text = "**" + plural(o.size(), "\xC3\xA9l\xC3\xA9ment orphelin", "\xC3\xA9l\xC3\xA9ments orphelins") + "** (jamais employ\xC3\xA9s) :\n";
        for (std::size_t k = 0; k < o.size() && k < 40; ++k) a.text += "- " + rel::describe(g, o[k]) + " \xE2\x80\x94 " + rel::orphanReason(g, o[k]) + "\n";
        if (o.size() > 40) a.text += "- \xE2\x80\xA6 et " + std::to_string(o.size() - 40) + " autres.\n";
        a.text += "Je ne supprime rien : v\xC3\xA9rifie chacun avant (un nom peut \xC3\xAAtre lu par un calcul que je ne suis pas).\n" + std::string(kLimits);
        return a;
    }
    if (q == Question::ViewEquipments) {
        const std::string name = subject.empty() ? std::string(openView) : subject;
        const auto v = name.empty() ? std::nullopt : g.find(Kind::View, name);
        if (!v) {
            a.text = name.empty() ? "Quelle vue ? Ouvre-la dans l'\xC3\xA9" "diteur, ou nomme-la (`Quels \xC3\xA9quipements utilise Vue_Station ?`)."
                                  : "Je ne trouve pas la vue " + code(name) + ". Les vues : " + joined(nearNames(g, {}, Kind::View, 12)) + ".";
            a.understood = !name.empty();
            return a;
        }
        const auto eq = rel::equipmentsOfView(g, *v);
        if (eq.empty()) {
            a.text = "La vue " + code(g.node(*v).name) + " n'utilise **aucun \xC3\xA9quipement** : ses objets, ses actions et ses scripts ne lisent ni "
                     "n'\xC3\xA9" "crivent aucune variable li\xC3\xA9" "e \xC3\xA0 un \xC3\xA9quipement.\n" + std::string(kLimits);
            return a;
        }
        a.text = "La vue " + code(g.node(*v).name) + " utilise **" + plural(eq.size(), "\xC3\xA9quipement", "\xC3\xA9quipements") + "** (par les "
                 "variables que ses objets, ses actions et ses scripts lisent ou \xC3\xA9" "crivent) :\n";
        for (const auto e : eq) a.text += "- " + rel::describe(g, e) + "\n";
        a.text += kLimits;
        return a;
    }
    if (subject.empty()) {
        a.text = "De quel \xC3\xA9l\xC3\xA9ment parles-tu ? Nomme-le tel qu'il est dans le projet, par exemple `Quels objets utilisent Niveau_Cuve_1 ?` "
                 "ou `Quelles alarmes concernent la pompe 2 ?`.";
        a.understood = false;
        return a;
    }
    const auto known = best(g, subject, {Kind::Variable});
    const bool member = subject.find('.') != std::string::npos;
    if (!known && !member) {
        std::vector<std::string> near;
        for (const auto& n : g.nodes())
            if (lower(n.name).find(lower(subject).substr(0, std::min<std::size_t>(subject.size(), 4))) != std::string::npos && near.size() < 8)
                near.push_back(code(n.name));
        a.text = "Je ne trouve **aucun \xC3\xA9l\xC3\xA9ment** " + code(subject) + " dans le projet"
                 + (near.empty() ? std::string(".") : " ; les plus proches : " + joined(near) + ".");
        return a;
    }
    switch (q) {
        case Question::ObjectsUsing:
        case Question::ScriptsUsing:
        case Question::AlarmsOf: {
            const auto found = q == Question::ObjectsUsing  ? rel::objectsUsing(g, subject)
                             : q == Question::ScriptsUsing ? rel::scriptsUsing(g, subject)
                                                            : rel::alarmsAbout(g, subject);
            const char* what = q == Question::ObjectsUsing ? "objet" : q == Question::ScriptsUsing ? "script ou fonction" : "alarme";
            const char* whats = q == Question::ObjectsUsing ? "objets" : q == Question::ScriptsUsing ? "scripts ou fonctions" : "alarmes";
            if (found.empty()) {
                a.text = "**Aucun" + std::string(q == Question::AlarmsOf ? "e " : " ") + what + "** n'utilise " + code(subject) + ".\n" + std::string(kLimits);
                return a;
            }
            a.text = "**" + plural(found.size(), what, whats) + "** " + (q == Question::AlarmsOf ? "concernent " : "utilisent ") + code(subject) + " :\n"
                     + list(g, found, subject) + kLimits;
            return a;
        }
        case Question::Users: {
            if (!known) {
                const auto r = rel::readers(g, subject);
                a.text = r.empty() ? "**Rien** n'utilise " + code(subject) + ".\n" + std::string(kLimits)
                                   : "**" + plural(r.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + "** utilisent " + code(subject) + " :\n"
                                         + list(g, r, subject) + kLimits;
                return a;
            }
            const auto u = rel::users(g, *known);
            a.text = u.empty() ? rel::describe(g, *known) + " : **rien ne l'utilise**.\n" + std::string(kLimits)
                               : rel::describe(g, *known) + " est utilis\xC3\xA9 par **" + plural(u.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + "** :\n"
                                     + list(g, u, g.node(*known).name) + kLimits;
            return a;
        }
        case Question::Impact: {
            if (!known) {
                a.text = "Je ne sais dire l'effet d'une suppression que pour un \xC3\xA9l\xC3\xA9ment entier (une variable, un type, une vue...), pas "
                         "pour " + code(subject) + ".";
                return a;
            }
            const auto hits = rel::impactDetails(g, *known);
            std::string removed, affected, moved;
            std::size_t nr = 0, na = 0, nm = 0;
            for (const auto& h : hits) {
                const std::string why = h.edge < g.edges().size() ? " \xE2\x80\x94 " + rel::edgeText(g, g.edges()[h.edge]) : std::string{};
                std::string& into = h.how == rel::Hit::How::Removed ? removed : h.how == rel::Hit::How::Moved ? moved : affected;
                std::size_t& n = h.how == rel::Hit::How::Removed ? nr : h.how == rel::Hit::How::Moved ? nm : na;
                if (++n <= 25) into += "- " + rel::describe(g, h.node) + why + "\n";
                else if (n == 26) into += "- \xE2\x80\xA6\n";
            }
            a.text = "Si tu supprimes " + rel::describe(g, *known) + " :\n";
            if (hits.empty()) a.text += "**rien d'autre n'est touch\xC3\xA9** (aucun \xC3\xA9l\xC3\xA9ment ne d\xC3\xA9pend de lui).\n";
            if (nr) a.text += "**Supprim\xC3\xA9 avec lui (" + std::to_string(nr) + ")** :\n" + removed;
            if (na) a.text += "**Touch\xC3\xA9 (" + std::to_string(na) + ")** - ils le citent, ils ne fonctionneront plus tels quels :\n" + affected;
            if (nm) a.text += "**D\xC3\xA9plac\xC3\xA9 (" + std::to_string(nm) + ")** :\n" + moved;
            a.text += "Je ne supprime rien : c'est une simulation.\n" + std::string(kLimits);
            return a;
        }
        default: break;
    }
    return a;
}

// ---- verifier tout le projet ---------------------------------------------------------------------------------------
Answer verify(const hmi::Project& project, const hmi::NameExists& plc) {
    namespace au = hmi::audit;
    Answer a;
    const au::Report rep = au::run(project, plc ? plc : hmi::NameExists([](std::string_view) { return false; }));
    a.text = "**V\xC3\xA9rification du projet** : " + plural(rep.errors, "erreur bloquante", "erreurs bloquantes") + ", "
             + plural(rep.warnings, "avertissement", "avertissements") + ", " + plural(rep.infos, "information", "informations") + ".\n";
    std::vector<std::pair<std::string, std::string>> moves;
    std::size_t other = 0;
    const auto section = [&](au::Severity sev, const char* head, std::size_t max) {
        std::size_t n = 0, total = 0;
        for (const auto& f : rep.findings) total += f.severity == sev;
        if (total == 0) return;
        a.text += "**" + std::string(head) + "**\n";
        for (const auto& f : rep.findings) {
            if (f.severity != sev) continue;
            if (++n > max) continue;
            a.text += "- [" + f.category + "] " + f.message + (f.where.empty() ? std::string{} : " \xE2\x80\x94 " + f.where) + "\n";
            for (const auto& fx : f.fixes) {
                a.text += "  - correction propos\xC3\xA9" "e : " + fx.target + (fx.before.empty() && fx.after.empty() ? std::string{} : " : " + (fx.before.empty() ? std::string("(vide)") : fx.before)
                                                                                                            + " \xE2\x86\x92 " + (fx.after.empty() ? std::string("(retir\xC3\xA9)") : fx.after))
                          + (fx.why.empty() ? std::string{} : " (" + fx.why + ")") + "\n";
            }
        }
        if (total > max) a.text += "- \xE2\x80\xA6 et " + std::to_string(total - max) + " autres (Compiler et G\xC3\xA9n\xC3\xA9rer les montrent tous).\n";
    };
    section(au::Severity::Error, "Erreurs bloquantes", 15);
    section(au::Severity::Warning, "Avertissements", 15);
    section(au::Severity::Info, "Informations", 10);
    for (const auto& f : rep.findings)
        for (const auto& fx : f.fixes) {
            if (fx.what == "adresse" && fx.target.rfind("variable ", 0) == 0 && !fx.after.empty()) moves.emplace_back(fx.target.substr(9), fx.after);
            else ++other;
        }
    for (const auto& n : rep.notes) a.text += "*" + n + "*\n";
    if (rep.findings.empty()) a.text += "Rien \xC3\xA0 corriger : le projet est coh\xC3\xA9rent d'apr\xC3\xA8s G\xC3\xA9n\xC3\xA9rer, Compiler, les adresses, les relations et les dossiers.\n";
    if (!moves.empty()) {
        Proposal p = addressProposal(project, moves, "Corrections d'adresse (" + std::to_string(moves.size()) + ")");
        const Check ck = validate(project, p);
        if (!p.ops.empty() && ck.ok()) {
            a.text += "Les **" + plural(p.ops.size(), "correction d'adresse", "corrections d'adresse")
                      + "** sont dans la proposition ci-dessous : rien n'est modifi\xC3\xA9 tant que tu ne l'as pas valid\xC3\xA9" "e (Pr\xC3\xA9visualiser, puis "
                        "Appliquer : un seul Ctrl+Z).";
            a.proposal = std::move(p);
        } else if (!p.ops.empty()) {
            a.text += "Les corrections d'adresse ne passent pas la v\xC3\xA9rification (" + (ck.errors.empty() ? std::string("?") : ck.errors.front())
                      + ") : je ne les propose pas.";
        }
    }
    if (other > 0)
        a.text += std::string(moves.empty() ? "" : "\n") + "Les autres corrections (" + std::to_string(other)
                  + ") se font \xC3\xA0 la main, l\xC3\xA0 o\xC3\xB9 je l'indique : je ne modifie jamais un texte ou une suppression sans toi.";
    return a;
}

// ---- les conventions du projet --------------------------------------------------------------------------------------
namespace {

// Le prefixe partage par au moins la moitie des noms (et deux au moins) : "T_", "Vue_", "Sym_" ; vide : aucun.
std::string commonPrefix(const std::vector<std::string>& names) {
    std::map<std::string, std::size_t> count;
    for (const auto& n : names)
        if (const auto cut = n.find('_'); cut != std::string::npos && cut > 0 && cut <= 6) ++count[n.substr(0, cut + 1)];
    std::string best;
    std::size_t most = 0;
    for (const auto& [p, n] : count)
        if (n > most) {
            most = n;
            best = p;
        }
    return most >= 2 && most * 2 >= names.size() ? best : std::string{};
}

// Les series : Pompe_1, Pompe_2... -> "Pompe_N (3)".
std::vector<std::string> seriesOf(const std::vector<std::string>& names) {
    std::map<std::string, std::size_t> count;
    for (const auto& n : names) {
        std::size_t end = n.size();
        while (end > 0 && std::isdigit(static_cast<unsigned char>(n[end - 1]))) --end;
        if (end == n.size() || end == 0) continue;
        ++count[n.substr(0, end)];
    }
    std::vector<std::string> out;
    for (const auto& [base, n] : count)
        if (n >= 2) out.push_back(code(base + "N") + " (" + std::to_string(n) + ")");
    return out;
}

} // namespace

Answer conventions(const hmi::Project& project) {
    Answer a;
    std::vector<std::string> lines;
    const auto names = [](const auto& list, auto get) {
        std::vector<std::string> out;
        for (const auto& x : list) out.push_back(get(x));
        return out;
    };
    const auto types = names(project.programs.types, [](const hmi::HmiType& t) { return t.name; });
    const auto vars = names(project.programs.variables, [](const hmi::Variable& v) { return v.name; });
    std::vector<std::string> views, symbols, popups;
    std::map<std::string, std::size_t> sizes;
    for (const auto& v : project.views) {
        if (v.role == "symbole") symbols.push_back(v.name);
        else if (v.role == "popup") popups.push_back(v.name);
        else if (v.role == "vue" || v.role.empty()) {
            views.push_back(v.name);
            ++sizes[std::to_string(v.width) + "\xC3\x97" + std::to_string(v.height)];
        }
    }
    const auto alarms = names(project.alarms, [](const hmi::AlarmDef& x) { return x.name; });
    const auto prefixLine = [&](const char* what, const std::vector<std::string>& list) {
        if (list.empty()) return;
        const std::string p = commonPrefix(list);
        lines.push_back(std::string(what) + " : " + std::to_string(list.size()) + (p.empty() ? std::string(", sans pr\xC3\xA9" "fixe commun") : ", pr\xC3\xA9" "fixe " + code(p)));
    };
    prefixLine("Types", types);
    prefixLine("Vues", views);
    prefixLine("Symboles", symbols);
    prefixLine("Popups", popups);
    prefixLine("Alarmes", alarms);
    if (const auto s = seriesOf(vars); !s.empty()) lines.push_back("S\xC3\xA9ries de variables : " + joined(s));
    if (!sizes.empty()) {
        const auto most = std::max_element(sizes.begin(), sizes.end(), [](const auto& x, const auto& y) { return x.second < y.second; });
        lines.push_back("Taille des vues : " + most->first + " (" + std::to_string(most->second) + " sur " + std::to_string(views.size()) + ")");
    }
    {
        std::map<std::string, std::pair<long, long>> span;
        for (const auto& r : hmi::audit::ranges(project)) {
            auto& sp = span[r.equipment];
            if (sp.second < sp.first || sp.first == 0) sp = {static_cast<long>(r.first), static_cast<long>(r.last)};
            sp.first = std::min(sp.first, static_cast<long>(r.first));
            sp.second = std::max(sp.second, static_cast<long>(r.last));
        }
        for (const auto& [eq, sp] : span) lines.push_back("Adresses de " + code(eq) + " : de la case " + std::to_string(sp.first) + " \xC3\xA0 " + std::to_string(sp.second));
    }
    if (!project.alarms.empty()) {
        std::map<int, std::size_t> prio;
        for (const auto& x : project.alarms) ++prio[x.priority];
        std::string t = "Priorit\xC3\xA9s des alarmes : ";
        bool first = true;
        for (const auto& [p, n] : prio) {
            t += (first ? "" : ", ") + std::string(hmi::alarmPriorityLabel(p)) + " " + std::to_string(n);
            first = false;
        }
        lines.push_back(t);
    }
    {
        std::set<std::string> folders;
        for (std::size_t l = 0; l < static_cast<std::size_t>(hmi::fold::List::Count); ++l)
            for (const auto& f : hmi::fold::allFolders(project, static_cast<hmi::fold::List>(l))) folders.insert(f);
        if (!folders.empty()) {
            std::vector<std::string> list(folders.begin(), folders.end());
            if (list.size() > 10) list.resize(10);
            lines.push_back("Dossiers : " + joined(list));
        }
    }
    if (lines.empty()) {
        a.text = "Le projet est encore vide : je ne vois **aucune convention** \xC3\xA0 suivre. Celles du logiciel s'appliquent (T_ pour un type, "
                 "Vue_ pour une vue, des s\xC3\xA9ries Nom_1, Nom_2...).";
        return a;
    }
    a.text = "**Les conventions du projet** (lues dans le projet, je les suis quand je con\xC3\xA7ois) :\n";
    for (const auto& l : lines) a.text += "- " + l + "\n";
    a.text += "Pour que l'IA connect\xC3\xA9" "e les suive aussi, garde-les dans la **m\xC3\xA9moire de l'IA** (Param\xC3\xA8tres de l'Assistant \xE2\x80\xBA "
              "M\xC3\xA9moire, une connaissance \xC2\xAB Contexte de projet \xC2\xBB propre \xC3\xA0 ce projet) : elle se voit, se corrige et s'exporte "
              "(.xia). Je n'y \xC3\xA9" "cris rien sans toi.";
    return a;
}

// ---- optimiser les adresses ----------------------------------------------------------------------------------------
Answer optimizeAddresses(const hmi::Project& project, const std::vector<std::string>& filter) {
    namespace au = hmi::audit;
    Answer a;
    const au::Optimization opt = au::optimize(project, filter);
    if (opt.empty()) {
        a.text = "**Rien \xC3\xA0 optimiser**" + std::string(filter.empty() ? "" : " pour " + joined(filter)) + " : les adresses sont d\xC3\xA9j\xC3\xA0 rang\xC3\xA9" "es "
                 "sans trou ni chevauchement (ou ne peuvent pas bouger).\n";
        for (const auto& n : opt.notes) a.text += "- " + n + "\n";
        return a;
    }
    a.text = "**Plan d'optimisation des adresses** - rien n'est appliqu\xC3\xA9 : c'est une proposition.\n";
    a.text += "**Avant**\n";
    for (const auto& f : opt.before) a.text += "- " + f.equipment + " : " + f.text + "\n";
    a.text += "**Apr\xC3\xA8s**\n";
    for (const auto& f : opt.after) a.text += "- " + f.equipment + " : " + f.text + "\n";
    a.text += "Chevauchements : " + std::to_string(opt.overlapsBefore) + " \xE2\x86\x92 " + std::to_string(opt.overlapsAfter) + ".\n";
    a.text += "**Les d\xC3\xA9placements (" + std::to_string(opt.moves.size()) + ")**\n";
    std::vector<std::pair<std::string, std::string>> moves;
    for (std::size_t k = 0; k < opt.moves.size(); ++k) {
        const auto& m = opt.moves[k];
        if (k < 40) a.text += "- " + code(m.variable) + " (" + m.equipment + ") : " + m.before + " \xE2\x86\x92 " + m.after + (m.reason.empty() ? std::string{} : " \xE2\x80\x94 " + m.reason) + "\n";
        moves.emplace_back(m.variable, m.after);
    }
    if (opt.moves.size() > 40) a.text += "- \xE2\x80\xA6 et " + std::to_string(opt.moves.size() - 40) + " autres (l'aper\xC3\xA7u les montre tous).\n";
    for (const auto& n : opt.notes) a.text += "- *" + n + "*\n";
    Proposal p = addressProposal(project, moves, "Optimisation des adresses (" + std::to_string(moves.size()) + ")");
    const Check ck = validate(project, p);
    if (!p.ops.empty() && ck.ok()) {
        a.text += "Valide la proposition pour l'appliquer (un seul Ctrl+Z la reprend). Attention : l'automate ou l'appareil doit utiliser les m\xC3\xAAmes "
                  "adresses.";
        a.proposal = std::move(p);
    } else {
        a.text += "Le plan ne passe pas la v\xC3\xA9rification (" + (ck.errors.empty() ? std::string("aucune op\xC3\xA9ration") : ck.errors.front())
                  + ") : je ne le propose pas.";
    }
    return a;
}

} // namespace app::ai::answers
