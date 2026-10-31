// =============================================================================
//  app/ai/AiIncidentAnswers.cpp - 1.20.0 : l'Assistant IA diagnostique les incidents (voir l'en-tete)
// -----------------------------------------------------------------------------
//  Tout ce qui est ecrit ici vient d'un dossier d'incident (le contexte copie au
//  moment du defaut) ou du graphe des relations du projet ouvert : un nom qui
//  n'y est pas n'apparait jamais (sauf celui que l'utilisateur a ecrit, pour
//  dire qu'il n'est dans aucun dossier).
// =============================================================================
#include "AiIncidentAnswers.hpp"

#include "../../hmi/HmiGuide.hpp"       // fold : sans accents ni casse
#include "../../hmi/HmiRelations.hpp"   // le projet ouvert : ce qui lit ou ecrit une variable

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>

namespace app::ai::diagnosis {

namespace {

namespace inc = hmi::incident;
using incidents::Item;
using incidents::State;

constexpr std::size_t npos = std::string::npos;

// Les phrases qui reviennent.
const char* const kDash = " \xE2\x80\x94 ";
const char* const kCausality = "*L'ordre d'apparition n'est pas une preuve de causalit\xC3\xA9 : ce sont des pistes \xC3\xA0 v\xC3\xA9rifier, pas des "
                               "causes \xC3\xA9tablies.*";
const char* const kCorrelation = "*Appara\xC3\xAEtre en m\xC3\xAAme temps est une corr\xC3\xA9lation, pas une causalit\xC3\xA9 : une d\xC3\xA9pendance "
                                 "commune rend une cause commune possible, elle ne la prouve pas.*";

// ---- les mots ------------------------------------------------------------------------------------------------------
std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool iequal(std::string_view a, std::string_view b) { return a.size() == b.size() && lower(a) == lower(b); }
std::string code(std::string_view s) { return "`" + std::string(s) + "`"; }
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Un mot de la demande : tel qu'ecrit (Pompe_2.Pression) et replie (pompe_2.pression).
struct Token {
    std::string raw, f;
};

// La demande, repliee : f = " pourquoi cette alarme est elle apparue " (sans accents ni casse ; tout ce qui n'est ni
// lettre, ni chiffre, ni _ separe) ; tokens : les mots avec _ et . (un nom du projet, un chemin).
struct Req {
    std::string              f;
    std::vector<std::string> words;
    std::vector<Token>       tokens;

    explicit Req(std::string_view request) {
        const std::string folded = hmi::guide::fold(request);
        std::string w;
        for (const char c : folded) {
            const auto u = static_cast<unsigned char>(c);
            if (u < 0x80 && (std::isalnum(u) || c == '_')) {
                w += c;
            } else if (!w.empty()) {
                words.push_back(w);
                w.clear();
            }
        }
        if (!w.empty()) words.push_back(w);
        f = " ";
        for (const auto& x : words) f += x + " ";
        // les noms : les lettres (accents compris), les chiffres, _ et . ; le point du bout n'en est pas
        std::string cur;
        const auto push = [&] {
            while (!cur.empty() && cur.back() == '.') cur.pop_back();
            while (!cur.empty() && cur.front() == '.') cur.erase(0, 1);
            if (!cur.empty()) {
                std::string f2;
                for (const char c : hmi::guide::fold(cur))
                    if (static_cast<unsigned char>(c) < 0x80) f2 += c;
                tokens.push_back({cur, f2});
            }
            cur.clear();
        };
        // les guillemets, les espaces insecables (C2 ..) et les apostrophes typographiques (E2 .. ..) separent
        for (std::size_t i = 0; i < request.size(); ++i) {
            const auto u = static_cast<unsigned char>(request[i]);
            if (u == 0xC2 || u == 0xE2) {
                push();
                i += u == 0xC2 ? 1 : 2;
            } else if (std::isalnum(u) || u == '_' || u == '.' || u >= 0x80) {
                cur += request[i];
            } else {
                push();
            }
        }
        push();
    }
    [[nodiscard]] bool has(std::string_view w) const { return f.find(" " + std::string(w) + " ") != npos; }
    [[nodiscard]] bool phrase(std::string_view p) const { return has(p); }
    [[nodiscard]] bool prefix(std::string_view p) const {
        return std::any_of(words.begin(), words.end(), [&](const std::string& w) { return w.rfind(p, 0) == 0; });
    }
};

bool digits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }); }
bool alpha(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }); }

// Les mots de la question elle-meme : jamais un sujet.
bool stopWord(std::string_view w) {
    static const std::set<std::string, std::less<>> k = {
        "pourquoi", "cette", "cet", "ce", "ces", "la", "le", "les", "de", "du", "des", "un", "une", "est", "elle", "il", "ils", "elles", "sont", "ete",
        "apparue", "apparu", "apparues", "apparus", "apparait", "apparaissent", "alarme", "alarmes", "defaut", "defauts", "incident", "incidents",
        "diagnostic", "diagnostique", "complet", "complete", "fais", "moi", "retrouve", "retrouver", "toute", "tout", "tous", "toutes", "chaine",
        "dependance", "dependances", "quelles", "quels", "quel", "quelle", "variable", "variables", "peuvent", "peut", "etre", "origine", "cinq",
        "meme", "memes", "temps", "deja", "dans", "passe", "compare", "comparer", "derniers", "dernieres", "dernier", "derniere", "equipement",
        "equipements", "commun", "commune", "communs", "communes", "differents", "differentes", "montre", "montrer", "scripts", "script",
        "fonctions", "fonction", "influencer", "influence", "influencent", "communication", "communications", "expliquent", "explique",
        "expliquer", "mesure", "mesures", "que", "qui", "quoi", "ou", "et", "en", "au", "aux", "sur", "pour", "par", "avec", "mon", "ma", "mes",
        "son", "sa", "ses", "leur", "leurs", "deux", "trois", "quatre", "six", "sept", "huit", "neuf", "dix", "fois", "combien", "historique",
        "precedent", "precedents", "liste", "donne", "dis", "analyse", "fiche", "cause", "causes", "raison", "ensemble", "moment", "instant",
        "valeur", "valeurs", "liaison", "reseau", "apparition", "apparitions", "declenche", "declenchee", "declenchement", "survenu", "survenue",
        "plusieurs", "autres", "autre", "attente", "valides", "valide", "pas", "ne", "se", "qu", "l", "d", "a", "s", "y", "c", "j", "n", "t",
        "simultanement", "simultanees", "lien", "liens", "panne", "pannes", "eu", "avant", "apres", "lors", "pendant", "lequel", "laquelle"};
    return k.count(w) > 0;
}

// `needle` est un composant de `hay`, sans casse : au debut ou apres un signe qui n'est ni lettre ni chiffre, et suivi
// de meme ("pompe_2" dans "POMPE_2_DEFAUT" ou "Pompe_2.Pression", jamais dans "Pompe_20"). npos : non.
std::size_t component(std::string_view hay, std::string_view needle) {
    if (needle.empty() || needle.size() > hay.size()) return npos;
    const std::string h = lower(hay), n = lower(needle);
    for (std::size_t at = h.find(n); at != npos; at = h.find(n, at + 1)) {
        const std::size_t end = at + n.size();
        const bool before = at == 0 || !std::isalnum(static_cast<unsigned char>(h[at - 1]));
        const bool after = end == h.size() || !std::isalnum(static_cast<unsigned char>(h[end]));
        if (before && after) return at;
    }
    return npos;
}

// "Pompe_2.Pression" -> "Pompe_2" ; "Cuves[2].Niveau" -> "Cuves".
std::string rootOf(std::string_view path) {
    const auto cut = path.find_first_of(".[");
    return std::string(cut == npos ? path : path.substr(0, cut));
}

// Une duree en francais : "0,8 s".
std::string seconds(double s) {
    const long tenths = std::lround(std::abs(s) * 10.0);
    return std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " s";
}

// "2026-10-31 17:40:12.350" -> des secondes (pour comparer deux incidents) ; nullopt : illisible. Sans sscanf : la
// virgule decimale d'une locale francaise ne change rien.
std::optional<double> stamp(std::string_view at) {
    const auto num = [&](std::size_t from, std::size_t len) -> std::optional<int> {
        if (from + len > at.size()) return std::nullopt;
        int v = 0;
        for (std::size_t i = from; i < from + len; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(at[i]))) return std::nullopt;
            v = v * 10 + (at[i] - '0');
        }
        return v;
    };
    const auto Y = num(0, 4), M = num(5, 2), D = num(8, 2), h = num(11, 2), m = num(14, 2), s = num(17, 2);
    if (!Y || !M || !D || !h || !m || !s || *M < 1 || *M > 12) return std::nullopt;
    double frac = 0, scale = 0.1;
    if (at.size() > 19 && (at[19] == '.' || at[19] == ','))
        for (std::size_t i = 20; i < at.size() && std::isdigit(static_cast<unsigned char>(at[i])); ++i, scale /= 10) frac += (at[i] - '0') * scale;
    // les jours depuis 1970 (l'algorithme civil de H. Hinnant)
    const int y = *Y - (*M <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (*M + (*M > 2 ? -3 : 9)) + 2) / 5 + *D - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const double days = era * 146097.0 + doe - 719468.0;
    return days * 86400.0 + *h * 3600.0 + *m * 60.0 + *s + frac;
}

// ---- les incidents --------------------------------------------------------------------------------------------------
std::string atOf(const Item& it) { return it.snapshot.trigger.at.empty() ? it.snapshot.createdAt : it.snapshot.trigger.at; }
std::string shortAt(const std::string& at) {
    const auto dot = at.find('.');
    return dot == npos ? at : at.substr(0, dot);
}
std::string alarmOf(const Item& it) { return it.snapshot.alarm.name.empty() ? it.snapshot.trigger.name : it.snapshot.alarm.name; }
bool commEvent(inc::EventKind k) { return k == inc::EventKind::CommLost || k == inc::EventKind::CommBack; }
bool appearEvent(inc::EventKind k) { return k == inc::EventKind::Appear || k == inc::EventKind::Reappear; }
const char* stateOf(const Item& it) { return it.state == State::Pending ? "en attente" : "valid\xC3\xA9"; }

// Les lisibles, du plus recent au plus ancien.
std::vector<const Item*> ordered(const std::vector<Item>& items) {
    std::vector<const Item*> out;
    for (const auto& it : items)
        if (it.readable) out.push_back(&it);
    std::stable_sort(out.begin(), out.end(), [](const Item* a, const Item* b) { return atOf(*a) > atOf(*b); });
    return out;
}

// Un objet de la chaine : "Pompe_1 (Vue_Station)" -> l'objet, la vue.
std::pair<std::string, std::string> objectParts(const std::string& name) {
    const auto open = name.rfind(" (");
    if (open == npos || name.back() != ')') return {name, {}};
    return {name.substr(0, open), name.substr(open + 2, name.size() - open - 3)};
}

// Les noms qu'un incident cite (les variables, la chaine, les valeurs, puis l'alarme) - ou chercher un sujet.
std::vector<std::string> citedNames(const Item& it) {
    const auto& s = it.snapshot;
    std::vector<std::string> v(s.conditionVariables.begin(), s.conditionVariables.end());
    for (const auto& l : s.chain) {
        if (l.kind == "objet") {
            auto [obj, view] = objectParts(l.name);
            v.push_back(obj);
            if (!view.empty()) v.push_back(view);
        } else if (l.kind == "adresse") {
            v.push_back(l.name.substr(0, l.name.find(" @ ")));
        } else {
            v.push_back(l.name);
        }
    }
    for (const auto& x : s.values) v.push_back(x.path);
    v.push_back(s.trigger.name);
    if (!s.alarm.name.empty()) v.push_back(s.alarm.name);
    return v;
}

bool cites(const Item& it, std::string_view subject) {
    const auto names = citedNames(it);
    return std::any_of(names.begin(), names.end(), [&](const std::string& n) { return component(n, subject) != npos; });
}

// Les equipements de la chaine (sans doublon, leur profondeur la plus petite) ; un defaut de communication : le sien.
std::vector<std::pair<std::string, int>> equipmentsOf(const Item& it) {
    std::vector<std::pair<std::string, int>> out;
    const auto add = [&](const std::string& n, int depth) {
        for (auto& [name, d] : out)
            if (iequal(name, n)) {
                d = std::min(d, depth);
                return;
            }
        out.emplace_back(n, depth);
    };
    if (commEvent(it.snapshot.trigger.kind) && !it.snapshot.trigger.name.empty()) add(it.snapshot.trigger.name, 1);
    for (const auto& l : it.snapshot.chain)
        if (l.kind == "equipement") add(l.name, l.depth);
    return out;
}
bool hasEquipment(const Item& it, std::string_view eq) {
    const auto e = equipmentsOf(it);
    return std::any_of(e.begin(), e.end(), [&](const auto& p) { return iequal(p.first, eq); });
}

// La valeur d'un chemin au moment du defaut : "3.2 (bonne)", ou "non disponible" - jamais supposee.
std::string valueOf(const inc::Snapshot& s, std::string_view path) {
    for (const auto& v : s.values)
        if (iequal(v.path, path)) {
            if (!v.available) return "non disponible";
            return v.value + (v.quality.empty() ? std::string{} : " (" + v.quality + ")");
        }
    return "non disponible";
}

// Pour comparer : "non lue" si cet incident ne releve pas ce chemin (une autre alarme), sinon valueOf.
std::string valueOrUnread(const inc::Snapshot& s, std::string_view path) {
    const bool read = std::any_of(s.values.begin(), s.values.end(), [&](const inc::ValueAt& v) { return iequal(v.path, path); })
                   || std::any_of(s.conditionVariables.begin(), s.conditionVariables.end(), [&](const std::string& c) { return iequal(c, path); });
    return read ? valueOf(s, path) : std::string("non lue");
}

// "synthese (certitude)" d'une ligne de liste ; sans synthese : la certitude seule.
std::string gist(const Item& it) {
    const std::string syn = it.synthesis();
    return (syn.empty() ? std::string{} : syn + " ") + "(" + it.certainty() + ")";
}

// La fiche (hmi::incident::diagnosis) pour le panneau : il rend "## " en intertitre, pas "### " ; un "   - " devient "- ".
std::string forPanel(std::string_view md) {
    std::string out;
    std::size_t from = 0;
    while (from < md.size()) {
        std::size_t nl = md.find('\n', from);
        if (nl == npos) nl = md.size();
        std::string line(md.substr(from, nl - from));
        from = nl + 1;
        if (line.rfind("### ", 0) == 0) line = "**" + line.substr(4) + "**";
        else {
            std::size_t lead = 0;
            while (lead < line.size() && line[lead] == ' ') ++lead;
            if (lead > 0 && line.compare(lead, 2, "- ") == 0) line = line.substr(lead);
        }
        out += line + "\n";
    }
    return out;
}

// ---- le sujet -------------------------------------------------------------------------------------------------------
struct Subject {
    std::string name;      // tel qu'ecrit dans les dossiers (vide : aucun)
    bool        alarm{false};   // le nom exact de l'alarme d'un incident
    std::string absent;    // un nom ecrit qui n'est dans aucun dossier (ni dans le projet)
};

// "Pompe_2" au debut d'un composant de `name` qui commence par `noun` ("pompe") suivi d'un nombre (Pompe_2, POMPE2) ;
// vide : non.
std::string seriesIn(const std::string& name, const std::string& noun) {
    const std::string l = lower(name);
    for (std::size_t at = l.find(noun); at != npos; at = l.find(noun, at + 1)) {
        if (at > 0 && std::isalnum(static_cast<unsigned char>(l[at - 1]))) continue;
        std::size_t p = at + noun.size();
        if (p < l.size() && l[p] == '_') ++p;
        const std::size_t d = p;
        while (p < l.size() && std::isdigit(static_cast<unsigned char>(l[p]))) ++p;
        if (p == d || (p < l.size() && std::isalnum(static_cast<unsigned char>(l[p])))) continue;
        return name.substr(at, p - at);
    }
    return {};
}

// Les variables qu'un incident cite (la condition, la chaine, les valeurs).
std::vector<std::string> variableNames(const Item& it) {
    const auto& s = it.snapshot;
    std::vector<std::string> v(s.conditionVariables.begin(), s.conditionVariables.end());
    for (const auto& l : s.chain)
        if (l.kind == "variable") v.push_back(l.name);
    for (const auto& x : s.values) v.push_back(x.path);
    return v;
}

// `variablesOnly` : une variable seulement (<< les scripts qui peuvent influencer X >>) ; `project` : ses variables
// comptent aussi (un nom qui n'est dans aucun dossier).
Subject subjectOf(const Req& r, const std::vector<const Item*>& list, const hmi::Project* project = nullptr, bool variablesOnly = false) {
    Subject out;
    // les candidats : << la pompe 2 >> (Pompe_2, Pompe2), puis les noms ecrits ; un nom << fort >> (avec _ . ou un
    // chiffre) peut etre un composant d'un nom ; un mot simple doit etre un nom entier.
    struct Cand {
        std::string f, said;
        bool        strong{false};
    };
    std::vector<Cand> cands;
    for (std::size_t i = 0; i + 1 < r.tokens.size(); ++i)
        if (alpha(r.tokens[i].f) && !stopWord(r.tokens[i].f) && digits(r.tokens[i + 1].f)) {
            const std::string said = r.tokens[i].raw + " " + r.tokens[i + 1].raw;
            cands.push_back({r.tokens[i].f + "_" + r.tokens[i + 1].f, said, true});
            cands.push_back({r.tokens[i].f + r.tokens[i + 1].f, said, true});
        }
    for (const auto& t : r.tokens) {
        if (t.f.size() < 3 || stopWord(t.f) || digits(t.f)) continue;
        cands.push_back({t.f, t.raw, t.f.find_first_of("_.0123456789") != npos});
    }
    const auto namesOf = [&](const Item& it) { return variablesOnly ? variableNames(it) : citedNames(it); };
    // 1. un nom entier : une alarme, une variable, un chemin ou sa racine (Pompe_2.Pression -> Pompe_2)
    for (const auto& c : cands)
        for (const Item* it : list) {
            if (!variablesOnly && (iequal(it->snapshot.trigger.name, c.f) || iequal(it->snapshot.alarm.name, c.f))) {
                out.name = it->snapshot.trigger.name.empty() ? it->snapshot.alarm.name : it->snapshot.trigger.name;
                out.alarm = true;
                return out;
            }
            for (const auto& n : namesOf(*it)) {
                if (iequal(n, c.f)) {
                    out.name = n;
                    return out;
                }
                if (const std::string root = rootOf(n); root != n && iequal(root, c.f)) {
                    out.name = root;
                    return out;
                }
            }
        }
    // 2. une variable du projet ouvert (que les dossiers ne citent pas)
    if (project)
        for (const auto& c : cands) {
            if (!c.strong) continue;
            if (const hmi::Variable* v = project->variable(rootOf(c.f))) {
                const auto cut = c.said.find_first_of(".[");
                out.name = v->name + (cut == npos ? std::string{} : c.said.substr(cut));
                return out;
            }
        }
    // 3. un composant d'un nom (<< pompe_2 >> dans POMPE_2_DEFAUT)
    for (const auto& c : cands) {
        if (!c.strong) continue;
        for (const Item* it : list)
            for (const auto& n : namesOf(*it))
                if (const auto at = component(n, c.f); at != npos) {
                    out.name = n.substr(at, c.f.size());
                    return out;
                }
    }
    // 4. << cette pompe >>, << la cuve >> : la serie du plus recent incident qui en cite une (Pompe_2)
    for (const auto& w : r.words) {
        if (!alpha(w) || w.size() < 3 || stopWord(w)) continue;
        std::string noun = w;
        if (noun.size() > 4 && (noun.back() == 's' || noun.back() == 'x')) noun.pop_back();
        for (const Item* it : list)
            for (const auto& n : namesOf(*it))
                if (const std::string s = seriesIn(n, noun); !s.empty()) {
                    out.name = s;
                    return out;
                }
    }
    // 5. un nom fort ecrit qui n'est nulle part
    for (const auto& c : cands)
        if (c.strong) {
            out.absent = c.said;
            break;
        }
    return out;
}

// Les incidents du sujet : son alarme (le nom exact), ou ceux qui le citent.
std::vector<const Item*> about(const std::vector<const Item*>& list, const Subject& s) {
    if (s.name.empty()) return list;
    std::vector<const Item*> out;
    for (const Item* it : list)
        if (s.alarm ? (iequal(it->snapshot.trigger.name, s.name) || iequal(it->snapshot.alarm.name, s.name)) : cites(*it, s.name)) out.push_back(it);
    return out;
}

// Un nombre ecrit avant << derniers >>, << incidents >>... (<< les trois derniers >>) ; `dflt` sinon.
std::size_t countAsked(const Req& r, std::size_t dflt) {
    static const std::map<std::string, std::size_t, std::less<>> k = {{"deux", 2}, {"trois", 3}, {"quatre", 4}, {"cinq", 5}, {"six", 6},
                                                                       {"sept", 7}, {"huit", 8},  {"neuf", 9},  {"dix", 10}};
    static const std::set<std::string, std::less<>> after = {"derniers", "dernieres", "incidents", "defauts", "alarmes", "occurrences", "fois",
                                                             "precedents"};
    for (std::size_t i = 0; i + 1 < r.words.size(); ++i) {
        if (!after.count(r.words[i + 1])) continue;
        if (const auto it = k.find(r.words[i]); it != k.end()) return it->second;
        if (digits(r.words[i]) && r.words[i].size() <= 2) return static_cast<std::size_t>(std::stoi(r.words[i]));
    }
    return dflt;
}

// ---- la reponse en cours ---------------------------------------------------------------------------------------------
struct Out {
    Answer                a;
    std::set<std::string> seen;

    void ref(const std::string& kind, const std::string& name, const std::string& view = {}) {
        if (name.empty() || kind.empty()) return;
        if (!seen.insert(kind + "|" + lower(name) + "|" + lower(view)).second) return;
        a.refs.push_back({kind, name, view});
    }
    void refLink(const inc::Link& l) {
        if (l.kind == "variable") ref("variable", rootOf(l.name));
        else if (l.kind == "structure") ref("type", l.name);
        else if (l.kind == "equipement" || l.kind == "script" || l.kind == "fonction" || l.kind == "vue" || l.kind == "alarme") ref(l.kind, l.name);
        else if (l.kind == "objet") {
            auto [obj, view] = objectParts(l.name);
            ref("objet", obj, view);
        }
    }
    void refAlarm(const Item& it) {
        if (commEvent(it.snapshot.trigger.kind)) ref("equipement", it.snapshot.trigger.name);
        else ref("alarme", alarmOf(it));
    }
    void suggest(std::string s) {
        if (std::find(a.suggestions.begin(), a.suggestions.end(), s) == a.suggestions.end() && a.suggestions.size() < 5) a.suggestions.push_back(std::move(s));
    }
};

// Les questions suivantes : COURTES (le panneau en montre une par ligne ; l'IA integree retire celles de plus de 45
// caracteres, kSuggestionMax), chacune comprise ici. Avec le nom de l'alarme s'il tient, sinon une tournure generale.
constexpr std::size_t kShort = 45;
std::size_t chars(std::string_view s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}
// La premiere tournure qui tient (les plus courtes gardent le nom), sinon la generale.
std::string pick(std::initializer_list<std::string> named, const char* general) {
    for (const auto& n : named)
        if (chars(n) <= kShort) return n;
    return general;
}
std::string askWhy(const std::string& a) { return pick({"Pourquoi l'alarme " + a + " ?", "Pourquoi l'alarme " + a}, "Pourquoi cette alarme ?"); }
std::string askDiagnosis(const std::string& a) { return pick({"Diagnostic complet de " + a, "Diagnostic de " + a}, "Diagnostic complet du dernier incident"); }
std::string askChain(const std::string& a) {
    return pick({"Cha\xC3\xAEne de d\xC3\xA9pendances de " + a, "Cha\xC3\xAEne de l'alarme " + a}, "Cha\xC3\xAEne de d\xC3\xA9pendances de cette alarme");
}
std::string askOrigins(const std::string& a) {
    return pick({"Origine du d\xC3\xA9" "faut " + a + " ?", "Origine du d\xC3\xA9" "faut " + a}, "Origine de ce d\xC3\xA9" "faut ?");
}
std::string askHistory(const std::string& a) {
    return pick({"Historique de l'alarme " + a, "Alarme " + a + " d\xC3\xA9j\xC3\xA0 apparue ?"}, "Cette alarme est-elle d\xC3\xA9j\xC3\xA0 apparue ?");
}
std::string askCompare(const std::string& s) { return pick({"Compare les incidents de " + s}, "Compare les trois derniers incidents"); }
std::string askScripts(const std::string& v) { return pick({"Scripts qui influencent " + v}, "Scripts qui influencent cette variable"); }
const char* const kAskCommon = "Quel \xC3\xA9quipement est commun aux d\xC3\xA9" "fauts ?";
const char* const kAskComm = "Les d\xC3\xA9" "fauts de communication expliquent-ils ?";
const char* const kAskSimultaneous = "Pourquoi ces alarmes en m\xC3\xAAme temps ?";

void suggestAbout(Out& o, const Item& it, Question skip) {
    const std::string a = alarmOf(it);
    if (skip != Question::Why) o.suggest(askWhy(a));
    if (skip != Question::Diagnosis) o.suggest(askDiagnosis(a));
    if (skip != Question::Chain) o.suggest(askChain(a));
    if (skip != Question::Origins) o.suggest(askOrigins(a));
    if (skip != Question::History) o.suggest(askHistory(a));
}

// (jamais un `code` dans du gras ou de l'italique : le panneau bascule ses styles a chaque marque)
std::string folderLine(const Item& it) { return "*Le dossier :* " + code(it.name) + " *(" + std::string(stateOf(it)) + ")*\n"; }

std::string headOf(const Item& it, std::size_t ofSame) {
    std::string t = shortAt(atOf(it)) + ", " + stateOf(it);
    if (ofSame > 1) t += ", le dernier des " + std::to_string(ofSame);
    return t;
}

// Le sujet ecrit qui n'est dans aucun dossier : le dire (avec les alarmes qui ont des incidents).
void absentSubject(Out& o, const std::string& said, const std::vector<const Item*>& list) {
    o.a.text = "Je ne trouve **aucun incident** qui cite " + code(said) + " : ni son alarme, ni sa cha\xC3\xAEne de d\xC3\xA9pendances, ni ses valeurs.\n";
    std::vector<std::string> alarms;
    for (const Item* it : list) {
        const std::string a = alarmOf(*it);
        if (std::none_of(alarms.begin(), alarms.end(), [&](const std::string& x) { return iequal(x, a); })) alarms.push_back(a);
    }
    if (!alarms.empty()) {
        o.a.text += "Les alarmes et \xC3\xA9v\xC3\xA9nements qui ont un dossier d'incident : ";
        for (std::size_t i = 0; i < alarms.size() && i < 10; ++i) o.a.text += (i ? ", " : "") + code(alarms[i]);
        o.a.text += alarms.size() > 10 ? ", \xE2\x80\xA6\n" : ".\n";
        for (std::size_t i = 0; i < alarms.size() && i < 3; ++i) o.suggest(askWhy(alarms[i]));
    }
}

// ---- aucun incident --------------------------------------------------------------------------------------------------
void noIncident(Out& o) {
    o.a.text = "Je n'ai **aucun dossier d'incident** pour ce projet : sans donn\xC3\xA9" "es, je ne diagnostique rien (je n'invente pas de cause).\n"
               "Un incident se cr\xC3\xA9" "e tout seul quand une alarme appara\xC3\xAEt pendant la **simulation de l'IHM** (ou sur le poste "
               "d'exploitation), la **surveillance des incidents** active : le contexte du d\xC3\xA9" "faut (la cha\xC3\xAEne de d\xC3\xA9pendances, "
               "les valeurs, la chronologie) est copi\xC3\xA9 dans `ihm/ressources/enregistrements/en attente/`.\n"
               "Lance la simulation, provoque l'alarme, puis repose ta question.";
    o.suggest("V\xC3\xA9rifie tout mon projet");
    o.suggest("Que sais-tu faire ?");
}

// ---- pourquoi -------------------------------------------------------------------------------------------------------
void why(Out& o, const Item& it, std::size_t ofSame) {
    const auto& s = it.snapshot;
    std::string& t = o.a.text;
    o.refAlarm(it);
    t += "**Pourquoi** " + code(alarmOf(it)) + " **est apparue** \xE2\x80\x94 d'apr\xC3\xA8s son dossier d'incident (" + headOf(it, ofSame) + ") :\n";
    if (commEvent(s.trigger.kind)) {
        t += "- " + std::string(inc::eventLabel(s.trigger.kind)) + " avec l'\xC3\xA9quipement " + code(s.trigger.name)
             + (s.trigger.detail.empty() ? std::string{} : " (" + s.trigger.detail + ")") + ".\n";
    } else if (s.alarm.known && !s.alarm.condition.empty()) {
        t += "- **Fait** : sa condition " + code(s.alarm.condition) + " \xC3\xA9tait vraie"
             + (s.alarm.delayMs > 0 ? " depuis sa temporisation (" + std::to_string(s.alarm.delayMs) + " ms)" : std::string{}) + " \xE2\x80\x94 "
             + std::string(inc::eventLabel(s.trigger.kind)) + (s.trigger.at.empty() ? std::string{} : " le " + shortAt(s.trigger.at)) + ".\n";
    } else if (!s.summary.empty()) {
        t += "- " + s.summary + "\n";
    }
    if (!s.conditionVariables.empty()) {
        t += "- Les valeurs lues par la condition au moment du d\xC3\xA9" "faut : ";
        for (std::size_t i = 0; i < s.conditionVariables.size() && i < 8; ++i) {
            t += (i ? " ; " : "") + code(s.conditionVariables[i]) + " = " + valueOf(s, s.conditionVariables[i]);
            o.ref("variable", rootOf(s.conditionVariables[i]));
        }
        t += ".\n";
    }
    if (s.hypotheses.empty()) {
        t += "**Aucune piste class\xC3\xA9" "e** dans ce dossier : les donn\xC3\xA9" "es copi\xC3\xA9" "es n'en appuient aucune.\n";
        std::string eqs;
        for (const auto& [eq, d] : equipmentsOf(it))
            if (d <= 1 && !commEvent(s.trigger.kind)) eqs += (eqs.empty() ? "" : ", ") + code(eq);
        if (!eqs.empty()) t += "- Sa condition lit des variables de " + eqs + " (fait v\xC3\xA9rifi\xC3\xA9) : c'est par l\xC3\xA0 que je commencerais.\n";
    } else {
        t += "**Les pistes, class\xC3\xA9" "es** (les mieux appuy\xC3\xA9" "es d'abord) :\n";
        for (std::size_t i = 0; i < s.hypotheses.size() && i < 8; ++i) {
            const auto& h = s.hypotheses[i];
            t += std::to_string(i + 1) + ". [" + std::string(inc::certaintyLabel(h.certainty)) + "] " + h.text + "\n";
            for (std::size_t k = 0; k < h.evidence.size() && k < 3; ++k) t += "- appui : " + h.evidence[k] + "\n";
            for (std::size_t k = 0; k < h.check.size() && k < 2; ++k) t += "- \xC3\xA0 v\xC3\xA9rifier : " + h.check[k] + "\n";
        }
    }
    std::string near;
    for (const auto& r : s.related)
        if (r.kind != inc::Related::Kind::Independent) {
            near += "- " + code(r.name) + " : " + std::string(inc::relatedLabel(r.kind)) + (r.why.empty() ? std::string{} : " (" + r.why + ")")
                    + (r.active ? ", active" : "") + "\n";
            o.ref("alarme", r.name);
        }
    if (!near.empty()) t += "**Les autres alarmes li\xC3\xA9" "es ou proches** :\n" + near;
    for (const auto& l : s.chain)
        if (l.depth == 1) o.refLink(l);
    if (const auto* an = it.latest(); an && an->number > 0)
        t += "**L'analyse de l'IA** (" + an->source + ", certitude : " + it.certainty() + ") : " + it.synthesis() + "\n";
    if (!s.missing.empty()) {
        t += "**Non disponible** :\n";
        for (std::size_t i = 0; i < s.missing.size() && i < 6; ++i) t += "- " + s.missing[i] + "\n";
    }
    t += std::string(kCausality) + "\n" + folderLine(it);
    suggestAbout(o, it, Question::Why);
}

// ---- le diagnostic complet (la fiche) ------------------------------------------------------------------------------
void fiche(Out& o, const Item& it, std::size_t ofSame) {
    o.refAlarm(it);
    std::string analysis;
    if (const auto* an = it.latest(); an && an->number > 0) analysis = "*" + an->source + "* : " + an->text;
    std::string& t = o.a.text;
    t += "**Diagnostic complet** \xE2\x80\x94 le dossier d'incident de " + code(alarmOf(it)) + " (" + headOf(it, ofSame) + ") :\n";
    t += forPanel(inc::diagnosis(it.snapshot, analysis));
    if (!it.comments.empty()) {
        t += "**Commentaires (" + std::to_string(it.comments.size()) + ")**\n";
        for (std::size_t i = it.comments.size() > 3 ? it.comments.size() - 3 : 0; i < it.comments.size(); ++i) {
            const auto& c = it.comments[i];
            t += "- " + shortAt(c.createdAt) + (c.author.empty() ? std::string{} : ", " + c.author) + " : " + c.text + "\n";
        }
    }
    for (const auto& l : it.snapshot.chain) o.refLink(l);
    t += std::string(kCausality) + "\n" + folderLine(it);
    suggestAbout(o, it, Question::Diagnosis);
}

// ---- la chaine de dependances --------------------------------------------------------------------------------------
std::string linkLine(const inc::Link& l) {
    return "- [" + std::string(inc::certaintyLabel(l.certainty)) + "] " + l.kind + " " + code(l.name) + kDash + l.relation
           + (l.where.empty() ? std::string{} : kDash + l.where) + (l.detail.empty() ? std::string{} : " (" + l.detail + ")") + "\n";
}

void chain(Out& o, const Item& it, std::size_t ofSame) {
    const auto& s = it.snapshot;
    std::string& t = o.a.text;
    o.refAlarm(it);
    t += "**La cha\xC3\xAEne de d\xC3\xA9pendances** de " + code(alarmOf(it)) + " \xE2\x80\x94 copi\xC3\xA9" "e au moment du d\xC3\xA9" "faut ("
         + headOf(it, ofSame) + ") :\n";
    if (!s.alarm.condition.empty()) t += "- Condition : " + code(s.alarm.condition) + "\n";
    if (s.chain.empty()) {
        t += "**Aucune d\xC3\xA9pendance** n'a \xC3\xA9t\xC3\xA9 lue dans le projet au moment du d\xC3\xA9" "faut.\n";
    } else {
        std::set<int> depths;
        for (const auto& l : s.chain) depths.insert(l.depth);
        std::size_t facts = 0, indirect = 0, hyp = 0;
        for (const int d : depths) {
            t += "**Profondeur " + std::to_string(d) + (d <= 1 ? std::string(" \xE2\x80\x94 directe") : d == 2 ? std::string(" \xE2\x80\x94 indirecte") : std::string{}) + "**\n";
            for (const auto& l : s.chain) {
                if (l.depth != d) continue;
                t += linkLine(l);
                o.refLink(l);
                (l.certainty == inc::Certainty::Fact ? facts : l.certainty == inc::Certainty::Indirect ? indirect : hyp) += 1;
            }
        }
        t += "*" + plural(s.chain.size(), "maillon", "maillons") + " : " + std::to_string(facts) + " fait(s) v\xC3\xA9rifi\xC3\xA9(s), " + std::to_string(indirect)
             + " relation(s) indirecte(s)" + (hyp ? ", " + std::to_string(hyp) + " hypoth\xC3\xA8se(s)" : std::string{}) + ". Lus dans le graphe des "
             "relations du projet tel qu'il \xC3\xA9tait.*\n";
        if (s.chain.size() >= 120) t += "*La cha\xC3\xAEne est born\xC3\xA9" "e : les maillons au-del\xC3\xA0 n'ont pas \xC3\xA9t\xC3\xA9 copi\xC3\xA9s.*\n";
    }
    if (!s.missing.empty()) {
        t += "**Non suivi ou non disponible** :\n";
        for (std::size_t i = 0; i < s.missing.size() && i < 6; ++i) t += "- " + s.missing[i] + "\n";
    }
    t += folderLine(it);
    suggestAbout(o, it, Question::Chain);
}

// ---- les variables a l'origine ---------------------------------------------------------------------------------------
void origins(Out& o, const Item& it, std::size_t ofSame) {
    const auto& s = it.snapshot;
    std::string& t = o.a.text;
    o.refAlarm(it);
    t += "**Les variables qui peuvent \xC3\xAAtre \xC3\xA0 l'origine** de " + code(alarmOf(it)) + " (" + headOf(it, ofSame) + ") :\n";
    std::vector<const inc::Link*> indirect;
    for (const auto& l : s.chain)
        if (l.kind == "variable" && l.depth >= 2) indirect.push_back(&l);
    if (s.conditionVariables.empty() && indirect.empty()) {
        t += "Sa condition ne lit **aucune variable connue** du projet : je ne peux pas en citer (je n'en suppose pas).\n";
    }
    if (!s.conditionVariables.empty()) {
        t += "**Directes** \xE2\x80\x94 lues par sa condition" + (s.alarm.condition.empty() ? std::string{} : " " + code(s.alarm.condition))
             + " (fait v\xC3\xA9rifi\xC3\xA9) :\n";
        for (const auto& path : s.conditionVariables) {
            const std::string root = rootOf(path);
            std::string from;
            for (const auto& l : s.chain) {
                if (l.kind == "equipement" && l.relation == "source de " + root) from = " \xE2\x80\x94 vient de l'\xC3\xA9quipement " + code(l.name);
                if (l.kind == "adresse" && l.name.rfind(root + " @ ", 0) == 0) from += " (adresse " + l.name.substr(root.size() + 3) + ")";
            }
            t += "- " + code(path) + " = " + valueOf(s, path) + from + "\n";
            o.ref("variable", root);
        }
    }
    if (!indirect.empty()) {
        t += "**Indirectes** \xE2\x80\x94 lues par un traitement qui \xC3\xA9" "crit une variable de la condition (relation indirecte) :\n";
        for (const auto* l : indirect) {
            t += "- " + code(l->name) + " = " + valueOf(s, l->name) + kDash + l->relation + "\n";
            o.ref("variable", rootOf(l->name));
        }
    }
    t += "*\xC2\xAB Peuvent \xC3\xAAtre \xC3\xA0 l'origine \xC2\xBB : la condition les lit, ou un traitement qui l'alimente les lit ; aucune n'est une cause "
         "\xC3\xA9tablie. \xC2\xAB Non disponible \xC2\xBB : la valeur n'a pas \xC3\xA9t\xC3\xA9 relev\xC3\xA9" "e au moment du d\xC3\xA9" "faut (je ne la suppose pas).*\n";
    t += folderLine(it);
    suggestAbout(o, it, Question::Origins);
    if (!s.conditionVariables.empty()) o.suggest(askScripts(s.conditionVariables.front()));
}

// ---- deja apparue ----------------------------------------------------------------------------------------------------
void history(Out& o, const std::vector<const Item*>& mine, const std::string& what) {
    std::string& t = o.a.text;
    const Item& last = *mine.front();
    o.refAlarm(last);
    std::set<std::string> ids;
    for (const Item* it : mine) ids.insert(it->snapshot.id);
    std::vector<inc::Snapshot::Previous> extra;
    for (const auto& p : last.snapshot.previous)
        if (!p.id.empty() && !ids.count(p.id)) {
            extra.push_back(p);
            ids.insert(p.id);
        }
    const std::size_t total = mine.size() + extra.size();
    if (total >= 2) t += "**Oui** : " + code(what) + " a **" + std::to_string(total) + " incidents** enregistr\xC3\xA9s (le plus r\xC3\xA9" "cent d'abord) :\n";
    else t += "**Non** : un seul incident de " + code(what) + " est enregistr\xC3\xA9 \xE2\x80\x94 aucun pr\xC3\xA9" "c\xC3\xA9" "dent :\n";
    for (std::size_t i = 0; i < mine.size() && i < 15; ++i) {
        const Item& it = *mine[i];
        t += "- " + shortAt(atOf(it)) + kDash + code(alarmOf(it)) + kDash + stateOf(it) + kDash + gist(it) + "\n";
        o.refAlarm(it);
    }
    if (mine.size() > 15) t += "- \xE2\x80\xA6 et " + std::to_string(mine.size() - 15) + " autres.\n";
    if (!extra.empty()) {
        t += "**Not\xC3\xA9s dans l'historique du dernier incident** (au moment du d\xC3\xA9" "faut ; leur dossier n'est plus dans la liste) :\n";
        for (std::size_t i = 0; i < extra.size() && i < 10; ++i)
            t += "- " + shortAt(extra[i].at) + kDash + extra[i].state + (extra[i].summary.empty() ? std::string{} : kDash + extra[i].summary) + "\n";
    }
    t += "*D'apr\xC3\xA8s les dossiers d'incident : la surveillance ne voit que ce qui s'est pass\xC3\xA9 pendant qu'elle \xC3\xA9tait active.*\n";
    suggestAbout(o, last, Question::History);
    if (total >= 2) o.suggest(askCompare(what));
}

// ---- comparer --------------------------------------------------------------------------------------------------------
void compare(Out& o, const std::vector<const Item*>& mine, std::size_t total, const std::string& subject, std::size_t n) {
    std::string& t = o.a.text;
    const std::size_t k = std::min(n, mine.size());
    if (subject.empty()) t += "**Les " + std::to_string(k) + " derniers incidents**";
    else t += "**Les " + std::to_string(k) + " derniers incidents** de " + code(subject);
    t += " (sur " + std::to_string(total) + "), du plus r\xC3\xA9" "cent au plus ancien :\n";
    if (k < n) t += "*Tu en demandais " + std::to_string(n) + " : il n'y en a que " + std::to_string(k) + ".*\n";
    std::map<std::string, std::pair<std::string, std::size_t>> eqCount;   // cle -> (nom, combien)
    std::map<std::string, std::pair<std::string, std::size_t>> pistes;
    std::vector<std::string> paths;
    for (std::size_t i = 0; i < k; ++i) {
        const Item& it = *mine[i];
        const auto& s = it.snapshot;
        o.refAlarm(it);
        t += std::to_string(i + 1) + ". **" + shortAt(atOf(it)) + "**" + kDash + "alarme " + code(alarmOf(it)) + " (" + stateOf(it) + ")\n";
        if (!s.values.empty()) {
            t += "- valeurs : ";
            for (std::size_t v = 0; v < s.values.size() && v < 6; ++v) t += (v ? " ; " : "") + code(s.values[v].path) + " = " + valueOf(s, s.values[v].path);
            t += "\n";
        } else if (!s.conditionVariables.empty()) {
            t += "- valeurs : non disponibles\n";
        }
        for (const auto& v : s.values)
            if (std::none_of(paths.begin(), paths.end(), [&](const std::string& p) { return iequal(p, v.path); })) paths.push_back(v.path);
        if (!s.hypotheses.empty()) {
            t += "- pistes : ";
            for (std::size_t h = 0; h < s.hypotheses.size() && h < 2; ++h)
                t += (h ? " ; " : "") + std::string("[") + std::string(inc::certaintyLabel(s.hypotheses[h].certainty)) + "] " + s.hypotheses[h].text;
            t += "\n";
        } else {
            t += "- pistes : aucune\n";
        }
        t += "- certitude : " + it.certainty() + "\n";
        for (const auto& [eq, d] : equipmentsOf(it)) {
            auto& c = eqCount[lower(eq)];
            if (c.first.empty()) c.first = eq;
            ++c.second;
            o.ref("equipement", eq);
        }
        std::set<std::string> once;
        for (const auto& h : s.hypotheses)
            if (once.insert(h.text).second) {
                auto& c = pistes[h.text];
                c.first = std::string(inc::certaintyLabel(h.certainty));
                ++c.second;
            }
    }
    if (k >= 2 && !paths.empty()) {
        t += "**Les valeurs d'un incident \xC3\xA0 l'autre** (du plus r\xC3\xA9" "cent au plus ancien) :\n";
        for (std::size_t p = 0; p < paths.size() && p < 6; ++p) {
            t += "- " + code(paths[p]) + " : ";
            for (std::size_t i = 0; i < k; ++i) t += (i ? " \xE2\x86\x92 " : "") + valueOrUnread(mine[i]->snapshot, paths[p]);
            t += "\n";
        }
    }
    if (k >= 2) {
        std::string common;
        for (const auto& [key, c] : eqCount)
            if (c.second >= 2) common += (common.empty() ? "" : " ; ") + std::string("\xC3\xA9quipement ") + code(c.first) + " (" + std::to_string(c.second) + " sur " + std::to_string(k) + ")";
        for (const auto& [text, c] : pistes)
            if (c.second >= 2) common += (common.empty() ? "" : " ; ") + std::string("la piste \xC2\xAB ") + text + " \xC2\xBB (" + c.first + ", " + std::to_string(c.second) + " sur " + std::to_string(k) + ")";
        t += common.empty() ? "**En commun** : rien (ni \xC3\xA9quipement ni piste partag\xC3\xA9s par deux de ces incidents).\n" : "**En commun** : " + common + ".\n";
    }
    t += std::string(kCausality) + "\n";
    if (k > 0) suggestAbout(o, *mine.front(), Question::Compare);
    o.suggest(kAskCommon);
}

// ---- l'equipement commun ------------------------------------------------------------------------------------------
void commonEquipment(Out& o, const std::vector<const Item*>& mine) {
    std::string& t = o.a.text;
    const std::size_t k = std::min<std::size_t>(10, mine.size());
    struct Eq {
        std::string              name;
        int                      depth{9};
        std::vector<std::string> alarms;
        std::size_t              count{0};
    };
    std::vector<Eq> eqs;
    for (std::size_t i = 0; i < k; ++i)
        for (const auto& [name, depth] : equipmentsOf(*mine[i])) {
            auto at = std::find_if(eqs.begin(), eqs.end(), [&](const Eq& e) { return iequal(e.name, name); });
            if (at == eqs.end()) at = eqs.insert(eqs.end(), Eq{name, depth, {}, 0});
            at->depth = std::min(at->depth, depth);
            ++at->count;
            const std::string a = commEvent(mine[i]->snapshot.trigger.kind) ? std::string("perte de communication") : code(alarmOf(*mine[i]));
            if (std::none_of(at->alarms.begin(), at->alarms.end(), [&](const std::string& x) { return iequal(x, a); })) at->alarms.push_back(a);
        }
    std::stable_sort(eqs.begin(), eqs.end(), [](const Eq& a, const Eq& b) { return a.count > b.count; });
    t += "**Les \xC3\xA9quipements des " + plural(k, "dernier incident", "derniers incidents") + "** (lus dans leurs cha\xC3\xAEnes de d\xC3\xA9pendances) :\n";
    if (eqs.empty()) {
        t += "**Aucun \xC3\xA9quipement** n'appara\xC3\xAEt dans leurs cha\xC3\xAEnes : leurs conditions ne lisent aucune variable li\xC3\xA9" "e \xC3\xA0 un "
             "\xC3\xA9quipement.\n";
        o.suggest(kAskComm);
        return;
    }
    for (const auto& e : eqs) {
        t += "- " + code(e.name) + " : dans **" + std::to_string(e.count) + " sur " + std::to_string(k) + "** (";
        for (std::size_t i = 0; i < e.alarms.size() && i < 6; ++i) t += (i ? ", " : "") + e.alarms[i];
        t += std::string(e.alarms.size() > 6 ? ", \xE2\x80\xA6" : "") + ")" + kDash + (e.depth <= 1 ? "directe (la condition lit ses variables)" : "indirecte (via un traitement)") + "\n";
        o.ref("equipement", e.name);
    }
    std::string all;
    for (const auto& e : eqs)
        if (e.count == k) all += (all.empty() ? "" : ", ") + code(e.name);
    if (k < 2) t += "Un seul incident : rien \xC3\xA0 comparer.\n";
    else if (!all.empty()) t += "**Commun \xC3\xA0 tous** : " + all + ".\n";
    else t += "**Aucun \xC3\xA9quipement n'est commun \xC3\xA0 tous** ; le plus fr\xC3\xA9quent : " + code(eqs.front().name) + " (" + std::to_string(eqs.front().count) + " sur " + std::to_string(k) + ").\n";
    t += "*Un \xC3\xA9quipement commun est une d\xC3\xA9pendance partag\xC3\xA9" "e, pas la preuve d'une cause commune : v\xC3\xA9rifie sa communication, son "
         "alimentation, ses capteurs.*\n";
    o.suggest(kAskComm);
    o.suggest(askCompare(eqs.front().name));
}

// ---- en meme temps ---------------------------------------------------------------------------------------------------
struct Near {
    std::string name;
    double      dt{0};
    const Item* item{nullptr};   // son dossier, s'il en a un
};

std::vector<Near> simultaneousWith(const Item& a, const std::vector<const Item*>& list) {
    std::vector<Near> out;
    const auto add = [&](const std::string& name, double dt, const Item* it) {
        if (iequal(name, a.snapshot.trigger.name)) return;
        for (auto& n : out)
            if (iequal(n.name, name)) {
                if (std::abs(dt) < std::abs(n.dt)) n.dt = dt;
                if (!n.item) n.item = it;
                return;
            }
        out.push_back({name, dt, it});
    };
    for (const auto& e : a.snapshot.timeline)
        if (appearEvent(e.kind) && std::abs(e.t - a.snapshot.trigger.t) <= 1.0) add(e.name, e.t - a.snapshot.trigger.t, nullptr);
    if (const auto ta = stamp(atOf(a)))
        for (const Item* b : list) {
            if (b == &a || commEvent(b->snapshot.trigger.kind)) continue;
            if (const auto tb = stamp(atOf(*b)); tb && std::abs(*tb - *ta) <= 1.0) add(b->snapshot.trigger.name, *tb - *ta, b);
        }
    // le dossier de chacun (le plus proche de l'instant)
    for (auto& n : out)
        if (!n.item)
            for (const Item* b : list)
                if (iequal(b->snapshot.trigger.name, n.name)) {
                    const auto ta = stamp(atOf(a)), tb = stamp(atOf(*b));
                    if (ta && tb && std::abs(*tb - *ta) <= 1.0) n.item = b;
                }
    std::sort(out.begin(), out.end(), [](const Near& x, const Near& y) { return x.dt < y.dt; });
    return out;
}

void simultaneous(Out& o, const std::vector<const Item*>& list, const Item* forced) {
    std::string& t = o.a.text;
    const Item* anchor = forced;
    std::vector<Near> best;
    if (anchor) best = simultaneousWith(*anchor, list);
    else
        for (std::size_t i = 0; i < list.size() && i < 50; ++i) {
            if (commEvent(list[i]->snapshot.trigger.kind)) continue;
            auto n = simultaneousWith(*list[i], list);
            if (n.size() > best.size()) {
                best = std::move(n);
                anchor = list[i];
            }
        }
    if (!anchor || best.empty()) {
        const Item& a = anchor ? *anchor : *list.front();
        o.refAlarm(a);
        t += "Je ne trouve **aucune alarme apparue en m\xC3\xAAme temps** (\xC3\xA0 moins d'une seconde) " + std::string(anchor ? "que " + code(alarmOf(a)) : std::string("dans les dossiers"))
             + " : ni dans les chronologies copi\xC3\xA9" "es, ni entre les incidents.\n";
        o.suggest(kAskCommon);
        o.suggest(kAskComm);
        return;
    }
    const Item& a = *anchor;
    o.refAlarm(a);
    t += "**" + std::to_string(best.size() + 1) + " alarmes sont apparues \xC3\xA0 moins d'une seconde d'intervalle** autour de " + code(alarmOf(a)) + " (" + shortAt(atOf(a)) + ") :\n";
    t += "- " + code(alarmOf(a)) + " : l'instant de r\xC3\xA9" "f\xC3\xA9rence\n";
    for (const auto& n : best) {
        t += "- " + code(n.name) + " : " + (std::abs(n.dt) < 0.05 ? std::string("au m\xC3\xAAme instant") : seconds(n.dt) + (n.dt < 0 ? " avant" : " apr\xC3\xA8s"))
             + (n.item ? std::string{} : std::string(" (pas de dossier : seulement dans la chronologie)")) + "\n";
        o.ref("alarme", n.name);
    }
    // les dependances communes : les alarmes liees (une dependance commune, potentiellement secondaire) des dossiers
    std::vector<const Item*> members{&a};
    for (const auto& n : best)
        if (n.item && std::find(members.begin(), members.end(), n.item) == members.end()) members.push_back(n.item);
    const auto inGroup = [&](const std::string& name) {
        if (iequal(name, a.snapshot.trigger.name)) return true;
        return std::any_of(best.begin(), best.end(), [&](const Near& n) { return iequal(n.name, name); });
    };
    std::string shared;
    std::set<std::string> pairs;
    for (const Item* m : members)
        for (const auto& r : m->snapshot.related) {
            if ((r.kind != inc::Related::Kind::Shared && r.kind != inc::Related::Kind::Secondary) || !inGroup(r.name)) continue;
            std::string x = lower(m->snapshot.trigger.name), y = lower(r.name);
            if (x > y) std::swap(x, y);
            if (!pairs.insert(x + "|" + y).second) continue;
            shared += "- " + code(m->snapshot.trigger.name) + " et " + code(r.name) + " : " + std::string(inc::relatedLabel(r.kind))
                      + (r.why.empty() ? std::string{} : " (" + r.why + ")") + "\n";
        }
    // les equipements communs
    std::vector<std::pair<std::string, std::vector<std::string>>> eqs;
    for (const Item* m : members)
        for (const auto& [eq, d] : equipmentsOf(*m)) {
            auto at = std::find_if(eqs.begin(), eqs.end(), [&](const auto& p) { return iequal(p.first, eq); });
            if (at == eqs.end()) at = eqs.insert(eqs.end(), std::make_pair(eq, std::vector<std::string>{}));
            at->second.push_back(m->snapshot.trigger.name);
        }
    std::string common;
    for (const auto& [eq, alarms] : eqs)
        if (alarms.size() >= 2) {
            common += "- " + code(eq) + " : ";
            for (std::size_t i = 0; i < alarms.size() && i < 8; ++i) common += (i ? ", " : "") + code(alarms[i]);
            common += "\n";
            o.ref("equipement", eq);
        }
    // les pertes de communication juste avant
    std::string comm;
    std::set<std::string> commSeen;
    for (const Item* m : members)
        for (const auto& e : m->snapshot.timeline)
            if (e.kind == inc::EventKind::CommLost && e.t <= m->snapshot.trigger.t + 0.001 && commSeen.insert(lower(e.name)).second) {
                comm += "- " + code(e.name) + " : " + std::string(inc::eventLabel(e.kind)) + " " + seconds(m->snapshot.trigger.t - e.t) + " avant "
                        + code(m->snapshot.trigger.name) + (hasEquipment(*m, e.name) ? " \xE2\x80\x94 sa condition lit des variables de cet \xC3\xA9quipement : "
                                                                                       "la perte **peut expliquer** cette alarme (relation indirecte)"
                                                                                     : " \xE2\x80\x94 sa condition ne lit aucune variable de cet \xC3\xA9quipement") + "\n";
                o.ref("equipement", e.name);
            }
    if (!shared.empty()) t += "**D\xC3\xA9pendances communes** (lues dans les dossiers) :\n" + shared;
    if (!common.empty()) t += "**\xC3\x89quipements communs** :\n" + common;
    if (!comm.empty()) t += "**Juste avant** :\n" + comm;
    const std::size_t without = static_cast<std::size_t>(std::count_if(best.begin(), best.end(), [](const Near& n) { return !n.item; }));
    if (without > 0)
        t += "*" + plural(without, "alarme n'a", "alarmes n'ont") + " pas de dossier d'incident : " + (without > 1 ? "leurs" : "ses")
             + " d\xC3\xA9pendances ne sont pas connues.*\n";
    if (shared.empty() && common.empty() && comm.empty())
        t += "**Rien dans les dossiers ne les relie** (ni variable, ni \xC3\xA9quipement, ni perte de communication) : elles sont peut-\xC3\xAAtre ind\xC3\xA9pendantes.\n";
    else
        t += "Une **cause commune est possible** par ce qu'elles partagent ; v\xC3\xA9rifie-le d'abord l\xC3\xA0.\n";
    t += std::string(kCorrelation) + "\n";
    o.suggest(kAskCommon);
    o.suggest(kAskComm);
    o.suggest(askWhy(alarmOf(a)));
}

// ---- les scripts qui peuvent influencer une variable ----------------------------------------------------------------
void scripts(Out& o, const std::vector<const Item*>& list, const std::string& var, const hmi::Project* project) {
    std::string& t = o.a.text;
    const std::string root = rootOf(var);
    o.ref("variable", root);
    t += "**Ce qui peut influencer** " + code(var) + " :\n";
    // 1. les dossiers d'incident
    std::string fromInc;
    std::set<std::string> seen;
    std::string equipment;
    for (std::size_t i = 0; i < list.size() && i < 30; ++i) {
        const Item& it = *list[i];
        for (const auto& l : it.snapshot.chain) {
            if (l.kind == "equipement" && l.relation == "source de " + root && equipment.empty()) equipment = l.name;
            const bool code_ = l.kind == "script" || l.kind == "fonction";
            if (code_) {
                static const std::string kWrites = "\xC3\xA9" "crit ", kReads = "lit ";
                const bool writes = l.relation.rfind(kWrites, 0) == 0;
                const bool reads = l.relation.rfind(kReads, 0) == 0;
                const std::string target = writes ? l.relation.substr(kWrites.size()) : reads ? l.relation.substr(kReads.size()) : std::string{};
                if (target.empty() || (component(target, var) == npos && component(var, target) == npos)) continue;
                if (!seen.insert(l.kind + "|" + lower(l.name) + "|" + (writes ? "w" : "r")).second) continue;
                fromInc += "- [" + std::string(inc::certaintyLabel(l.certainty)) + "] " + l.kind + " " + code(l.name) + " "
                           + (writes ? std::string("**l'\xC3\xA9" "crit**") : std::string("la lit (sans la modifier)"))
                           + (l.where.empty() ? std::string{} : kDash + l.where) + " \xE2\x80\x94 incident " + code(alarmOf(it)) + " du " + shortAt(atOf(it)) + "\n";
                o.refLink(l);
            } else if (l.kind == "variable" && l.depth >= 2 && component(l.name, var) != npos) {
                // << lue par W (qui ecrit la condition) >> : W depend de la variable, et alimente l'alarme
                const std::string by = l.relation.rfind("lue par ", 0) == 0 ? l.relation.substr(8, l.relation.find(" (") == npos ? npos : l.relation.find(" (") - 8) : std::string{};
                if (by.empty() || !seen.insert("via|" + lower(by)).second) continue;
                fromInc += "- [" + std::string(inc::certaintyLabel(l.certainty)) + "] " + code(by) + " la lit et \xC3\xA9" "crit la condition de " + code(alarmOf(it))
                           + " (incident du " + shortAt(atOf(it)) + ") : la variable influence le traitement, pas l'inverse\n";
            }
        }
    }
    if (!fromInc.empty()) t += "**D'apr\xC3\xA8s les dossiers d'incident** (le contexte copi\xC3\xA9 au moment des d\xC3\xA9" "fauts) :\n" + fromInc;
    // 2. le projet ouvert
    std::string fromProject;
    if (project) {
        const hmi::rel::Graph g = hmi::rel::build(*project);
        for (const auto n : hmi::rel::scriptsUsing(g, var)) {
            const auto& node = g.node(n);
            std::string verb, where;
            for (const auto e : g.out(n)) {
                const auto& edge = g.edges()[e];
                const auto& to = g.node(edge.to);
                const bool hit = iequal(to.name, root) && (edge.path.empty() || component(edge.path, var) != npos || component(var, edge.path) != npos);
                if (!hit) continue;
                if (edge.link == hmi::rel::Link::Writes) verb = "**l'\xC3\xA9" "crit**";
                else if (verb.empty() && edge.link == hmi::rel::Link::Watches) verb = "la surveille";
                else if (verb.empty() && edge.link == hmi::rel::Link::Reads) verb = "la lit (sans la modifier)";
                if (where.empty()) where = edge.where;
            }
            fromProject += "- " + hmi::rel::describe(g, n) + (verb.empty() ? std::string{} : " " + verb) + (where.empty() ? std::string{} : kDash + where) + "\n";
            if (node.kind == hmi::rel::Kind::Script) o.ref("script", node.name);
            else if (node.kind == hmi::rel::Kind::Function) o.ref("fonction", node.name);
        }
        if (equipment.empty())
            if (const hmi::Variable* v = project->variable(root); v && !v->equipment.empty()) equipment = v->equipment;
    }
    if (!fromProject.empty()) t += "**D'apr\xC3\xA8s le projet ouvert** (le graphe des relations, tel qu'il est maintenant) :\n" + fromProject;
    if (fromInc.empty() && fromProject.empty())
        t += "**Aucun script ni fonction** ne lit ni n'\xC3\xA9" "crit " + code(var) + std::string(project ? " (dans les dossiers d'incident et le projet ouvert)."
                                                                                                    : " dans les dossiers d'incident (ouvre le projet pour chercher aussi dans son graphe).") + "\n";
    if (!equipment.empty()) {
        t += "- Sa valeur vient aussi de l'\xC3\xA9quipement " + code(equipment) + " : une mesure ou une communication fausse l'influence.\n";
        o.ref("equipement", equipment);
    }
    t += "*Seuls ceux qui l'\xC3\xA9" "crivent peuvent la modifier ; ceux qui la lisent en d\xC3\xA9pendent. Un nom calcul\xC3\xA9 en marche (une cible "
         "=expression) n'est pas suivi.*\n";
    for (const Item* it : list)
        if (cites(*it, var)) {
            suggestAbout(o, *it, Question::Scripts);
            break;
        }
    o.suggest(kAskCommon);
}

// ---- la communication et les alarmes de mesure ------------------------------------------------------------------------
void communication(Out& o, const std::vector<const Item*>& list) {
    std::string& t = o.a.text;
    const std::size_t k = std::min<std::size_t>(30, list.size());
    std::vector<const Item*> measures, commIncidents;
    for (std::size_t i = 0; i < k; ++i) {
        const auto kind = list[i]->snapshot.trigger.kind;
        if (kind == inc::EventKind::CommLost) commIncidents.push_back(list[i]);
        else if (!commEvent(kind) && kind != inc::EventKind::Technical) measures.push_back(list[i]);
    }
    // les pertes de communication vues (les dossiers de communication, les chronologies)
    std::vector<std::string> lost;
    const auto noteLost = [&](const std::string& eq) {
        if (std::none_of(lost.begin(), lost.end(), [&](const std::string& x) { return iequal(x, eq); })) lost.push_back(eq);
    };
    for (const Item* c : commIncidents) noteLost(c->snapshot.trigger.name);
    for (const Item* m : measures)
        for (const auto& e : m->snapshot.timeline)
            if (e.kind == inc::EventKind::CommLost) noteLost(e.name);
    t += "**Les d\xC3\xA9" "fauts de communication et les alarmes de mesure** (les " + plural(k, "dernier incident", "derniers incidents") + ") :\n";
    if (lost.empty()) {
        t += "- Aucune perte de communication n'est enregistr\xC3\xA9" "e (ni incident de communication, ni \xC3\xA9v\xC3\xA9nement dans les chronologies).\n";
    } else {
        t += "- Pertes de communication enregistr\xC3\xA9" "es : ";
        for (std::size_t i = 0; i < lost.size() && i < 8; ++i) {
            t += (i ? ", " : "") + code(lost[i]);
            o.ref("equipement", lost[i]);
        }
        t += ".\n";
    }
    std::string can, none;
    std::size_t explained = 0;
    for (const Item* m : measures) {
        const auto& s = m->snapshot;
        const auto eqs = equipmentsOf(*m);
        std::string why;
        // une perte de communication d'un de SES equipements, avant elle (sa chronologie)
        std::map<std::string, std::pair<double, bool>> lastLost;   // equipement -> (instant, retrouvee avant l'alarme)
        for (const auto& e : s.timeline) {
            if (!commEvent(e.kind) || e.t > s.trigger.t + 0.001 || !hasEquipment(*m, e.name)) continue;
            if (e.kind == inc::EventKind::CommLost) lastLost[lower(e.name)] = {e.t, false};
            else if (auto it = lastLost.find(lower(e.name)); it != lastLost.end()) it->second.second = true;
        }
        for (const auto& [eq, d] : eqs)
            if (const auto it = lastLost.find(lower(eq)); it != lastLost.end())
                why += "- " + code(eq) + " a perdu la communication " + seconds(s.trigger.t - it->second.first) + " avant"
                       + (it->second.second ? " (retrouv\xC3\xA9" "e avant l'alarme : le lien est plus faible)" : std::string{})
                       + " et la condition lit ses variables : ses valeurs ont pu \xC3\xAAtre fig\xC3\xA9" "es ou fausses.\n";
        // un incident de communication de ses equipements juste avant (les dates)
        if (why.empty())
            if (const auto tm = stamp(atOf(*m)))
                for (const Item* c : commIncidents)
                    if (const auto tc = stamp(atOf(*c)); tc && *tc <= *tm && *tm - *tc <= 120.0 && hasEquipment(*m, c->snapshot.trigger.name))
                        why += "- " + code(c->snapshot.trigger.name) + " : incident de communication " + seconds(*tm - *tc) + " avant, et la condition lit ses variables.\n";
        // l'equipement injoignable au moment du defaut
        if (why.empty())
            for (const auto& st : s.equipmentStates)
                for (const auto& [eq, d] : eqs)
                    if (st.rfind(eq + " : ", 0) == 0 && lower(st).find("injoignable") != npos) why += "- " + code(eq) + " \xC3\xA9tait injoignable au moment du d\xC3\xA9" "faut (" + st + ").\n";
        if (!why.empty()) {
            ++explained;
            for (const auto& r : s.related)
                if (r.kind == inc::Related::Kind::Secondary) why += "- " + code(r.name) + " : " + std::string(inc::relatedLabel(r.kind)) + " (" + r.why + ")\n";
            for (const auto& h : s.hypotheses)
                if (h.certainty == inc::Certainty::Indirect && (lower(h.text).find("communication") != npos || lower(h.text).find("injoignable") != npos))
                    why += "- piste du dossier : [" + std::string(inc::certaintyLabel(h.certainty)) + "] " + h.text + "\n";
            can += code(alarmOf(*m)) + " (" + shortAt(atOf(*m)) + ") \xE2\x80\x94 **peut s'expliquer** par la communication (relation indirecte) :\n" + why;
            o.refAlarm(*m);
            for (const auto& [eq, d] : eqs) o.ref("equipement", eq);
        } else {
            std::string reason;
            if (eqs.empty()) reason = "sa condition ne lit aucune variable d'un \xC3\xA9quipement";
            else {
                reason = "aucune perte de communication de ";
                for (std::size_t i = 0; i < eqs.size() && i < 4; ++i) reason += (i ? ", " : "") + code(eqs[i].first);
                reason += " avant elle";
                for (const auto& e : s.timeline)
                    if (e.kind == inc::EventKind::CommLost && !hasEquipment(*m, e.name)) {
                        reason += " (" + code(e.name) + " a perdu la communication, mais la condition ne lit aucune de ses variables)";
                        break;
                    }
            }
            none += "- " + code(alarmOf(*m)) + " (" + shortAt(atOf(*m)) + ") : " + reason + ".\n";
        }
    }
    if (measures.empty()) {
        t += "Aucune **alarme de mesure** dans ces incidents : rien \xC3\xA0 expliquer.\n";
    } else {
        if (!can.empty()) t += can;
        if (!none.empty()) t += "**Sans lien connu avec la communication** :\n" + none;
        t += explained > 0 ? "**" + std::to_string(explained) + " alarme(s) de mesure sur " + std::to_string(measures.size())
                                 + " peuvent s'expliquer par un d\xC3\xA9" "faut de communication** \xE2\x80\x94 une relation indirecte, pas une preuve.\n"
                           : "**Rien dans les dossiers ne relie** ces " + plural(measures.size(), "alarme de mesure", "alarmes de mesure") + " \xC3\xA0 un d\xC3\xA9" "faut de communication.\n";
    }
    t += "*Une perte de communication rend douteuses les mesures de son \xC3\xA9quipement : elle peut expliquer une alarme qui les lit, sans le prouver. "
         "V\xC3\xA9rifie la liaison, puis la valeur brute du capteur.*\n";
    o.suggest(kAskCommon);
    o.suggest(kAskSimultaneous);
    if (!measures.empty()) o.suggest(askWhy(alarmOf(*measures.front())));
}

// ---- la liste -------------------------------------------------------------------------------------------------------
void listing(Out& o, const std::vector<const Item*>& list, const Req& r) {
    std::string& t = o.a.text;
    std::vector<const Item*> shown;
    const bool onlyPending = r.has("attente"), onlyValid = r.prefix("valid") && !onlyPending;
    std::size_t pending = 0, valid = 0;
    for (const Item* it : list) {
        (it->state == State::Pending ? pending : valid) += 1;
        if ((onlyPending && it->state != State::Pending) || (onlyValid && it->state != State::Validated)) continue;
        shown.push_back(it);
    }
    t += "**" + plural(list.size(), "incident", "incidents") + "** : " + std::to_string(pending) + " en attente, " + std::to_string(valid) + " valid\xC3\xA9(s)";
    t += shown.empty() ? std::string(".\n") : " \xE2\x80\x94 " + std::string(onlyPending ? "en attente" : onlyValid ? "valid\xC3\xA9s" : "les derniers") + ", le plus r\xC3\xA9" "cent d'abord :\n";
    for (std::size_t i = 0; i < shown.size() && i < 12; ++i) {
        const Item& it = *shown[i];
        t += "- " + shortAt(atOf(it)) + kDash + code(alarmOf(it)) + kDash + stateOf(it) + kDash + gist(it) + "\n";
        o.refAlarm(it);
    }
    if (shown.size() > 12) t += "- \xE2\x80\xA6 et " + std::to_string(shown.size() - 12) + " autres (la liste des incidents les montre tous).\n";
    if (!list.empty()) suggestAbout(o, *list.front(), Question::List);
}

} // namespace

// ==================================================================================================================
Question classify(std::string_view request) {
    const Req r(request);
    // 1. est-ce une question sur les incidents ? Jamais une demande de creation ou de modification (<< Cree une vue de
    //    diagnostic >>, << Ajoute une alarme de defaut >> restent a l'IA integree).
    static const char* const creating[] = {"ajout", "rajout", "cree", "insere", "anim", "clignot", "supprim", "renomm", "dupliqu", "deplac",
                                           "colori", "configur", "aligne", "redimension"};
    for (const char* c : creating)
        if (r.prefix(c)) return Question::None;
    if (r.has("mets") || r.has("met") || r.has("pose") || r.has("place") || r.has("mettre")) return Question::None;
    const bool strong = r.prefix("incident") || r.prefix("diagnost") || (r.has("chaine") && r.prefix("dependance"));
    if (!strong) {
        const bool alarmish = r.prefix("alarme") || r.prefix("defaut") || r.prefix("panne") || r.prefix("declenchement");
        const bool asking = r.has("pourquoi") || r.prefix("origine") || r.prefix("caus") || r.prefix("expliqu") || r.phrase("en meme temps")
                         || r.prefix("simultan") || r.phrase("au meme moment") || r.has("deja") || r.phrase("dans le passe") || r.prefix("historique")
                         || r.prefix("preceden") || r.has("commun") || r.has("commune") || r.has("communs") || r.has("communes") || r.prefix("compar")
                         || r.has("chaine") || r.prefix("dependance") || r.phrase("combien de fois") || r.phrase("que s est il passe")
                         || r.prefix("analys");
        const bool influence = r.prefix("influenc") && (r.prefix("variable") || r.prefix("script") || r.prefix("fonction"));
        if (!(alarmish && asking) && !influence) return Question::None;
    }
    // 2. laquelle
    const bool comm = r.prefix("communic") || r.has("comm") || r.has("liaison") || r.has("liaisons");
    if (comm && (r.prefix("expliqu") || r.prefix("caus") || r.prefix("origin") || r.prefix("responsab") || r.has("lien") || r.has("liens")
                 || r.prefix("provoqu") || r.prefix("declench")))
        return Question::Communication;
    if ((r.prefix("script") || r.prefix("fonction") || r.has("code") || r.prefix("traitement"))
        && (r.prefix("influenc") || r.prefix("agiss") || r.prefix("modifi") || r.prefix("ecri") || r.prefix("peuv") || r.prefix("agi")))
        return Question::Scripts;
    if (r.phrase("en meme temps") || r.prefix("simultan") || r.phrase("au meme moment") || r.phrase("meme instant") || r.has("ensemble")
        || r.phrase("d un coup"))
        return Question::Simultaneous;
    if (r.has("commun") || r.has("commune") || r.has("communs") || r.has("communes") || r.prefix("partag")) return Question::CommonEquipment;
    if (r.prefix("compar")) return Question::Compare;
    if (r.has("deja") || r.phrase("dans le passe") || r.prefix("historique") || r.prefix("preceden") || r.phrase("combien de fois")
        || r.prefix("auparavant") || r.prefix("recurren") || r.has("souvent"))
        return Question::History;
    if (r.has("chaine") || r.prefix("dependance")) return Question::Chain;
    if (r.prefix("origine") || ((r.has("variables") || r.has("variable")) && (r.prefix("caus") || r.prefix("provoqu") || r.prefix("declench") || r.prefix("expliqu"))))
        return Question::Origins;
    if (r.prefix("diagnost") || r.has("complet") || r.has("complete") || r.has("fiche")) return Question::Diagnosis;
    if (r.has("pourquoi") || r.prefix("caus") || r.prefix("expliqu") || r.phrase("que s est il passe") || r.prefix("analys")) return Question::Why;
    if (r.has("incidents") || r.prefix("liste") || r.has("combien") || r.has("quels") || r.has("quelles") || r.has("attente")) return Question::List;
    return Question::Why;
}

bool isIncidentQuestion(std::string_view request) { return classify(request) != Question::None; }

Answer answer(std::string_view request, const std::vector<Item>& items, const hmi::Project* project) {
    const Req r(request);
    Out o;
    Question q = classify(request);
    if (q == Question::None) q = Question::Why;
    o.a.question = q;
    const auto list = ordered(items);
    const Subject subj = q == Question::Scripts ? subjectOf(r, list, project, true) : subjectOf(r, list);
    // les scripts : une variable du projet suffit (meme sans incident)
    if (q == Question::Scripts) {
        std::string var = subj.name;
        if (var.empty() && subj.absent.empty() && !list.empty())
            for (const Item* it : list)
                if (!it->snapshot.conditionVariables.empty()) {
                    var = it->snapshot.conditionVariables.front();
                    break;
                }
        if (!var.empty()) {
            scripts(o, list, var, project);
            return o.a;
        }
        if (list.empty()) {
            noIncident(o);
            return o.a;
        }
        if (!subj.absent.empty()) {
            absentSubject(o, subj.absent, list);
            return o.a;
        }
        o.a.text = "De quelle variable parles-tu ? Nomme-la telle qu'elle est dans le projet, par exemple `Montre-moi les scripts qui peuvent "
                   "influencer Niveau_Cuve_1`.";
        o.a.understood = false;
        return o.a;
    }
    if (list.empty()) {
        noIncident(o);
        return o.a;
    }
    if (!subj.absent.empty()) {
        absentSubject(o, subj.absent, list);
        return o.a;
    }
    const auto mine = about(list, subj);
    if (mine.empty()) {   // (le sujet vient des dossiers : n'arrive pas)
        absentSubject(o, subj.name, list);
        return o.a;
    }
    const Item& target = *mine.front();
    // combien d'incidents de la meme alarme (<< le dernier des 3 >>)
    std::size_t same = 0;
    for (const Item* it : list) same += iequal(it->snapshot.trigger.name, target.snapshot.trigger.name) ? 1 : 0;
    switch (q) {
        case Question::Diagnosis: fiche(o, target, same); break;
        case Question::Chain: chain(o, target, same); break;
        case Question::Origins: origins(o, target, same); break;
        case Question::History: {
            if (subj.name.empty() || subj.alarm) {
                std::vector<const Item*> of;
                for (const Item* it : list)
                    if (iequal(it->snapshot.trigger.name, target.snapshot.trigger.name)) of.push_back(it);
                history(o, of, target.snapshot.trigger.name);
            } else {
                history(o, mine, subj.name);
            }
            break;
        }
        case Question::Compare: {
            if (subj.name.empty()) compare(o, list, list.size(), {}, countAsked(r, 3));
            else compare(o, mine, mine.size(), subj.name, countAsked(r, 3));
            break;
        }
        case Question::CommonEquipment: commonEquipment(o, subj.name.empty() ? list : mine); break;
        case Question::Simultaneous: simultaneous(o, list, subj.name.empty() ? nullptr : &target); break;
        case Question::Communication: communication(o, subj.name.empty() ? list : mine); break;
        case Question::List: listing(o, subj.name.empty() ? list : mine, r); break;
        default: why(o, target, same); break;
    }
    return o.a;
}

// ---- le contexte de l'IA distante ---------------------------------------------------------------------------------------
std::string contextBlock(std::string_view request, const std::vector<Item>& items, std::size_t maxBytes, std::size_t count) {
    const auto clip = [](std::string s, std::size_t max) {
        if (s.size() <= max) return s;
        std::size_t cut = max > 4 ? max - 4 : 0;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        return s.substr(0, cut) + "...\n";
    };
    std::string s = "## Incidents enregistr\xC3\xA9s (dossiers immuables, copi\xC3\xA9s au moment du d\xC3\xA9" "faut)\n";
    const auto list = ordered(items);
    if (list.empty()) {
        s += "Aucun dossier d'incident pour ce projet (une alarme en simulation de l'IHM, la surveillance des incidents active, en cr\xC3\xA9" "e un) : "
             "ne d\xC3\xA9" "cris aucun incident, dis qu'il n'y en a pas.\n";
        return clip(std::move(s), maxBytes);
    }
    const Req r(request);
    const Subject subj = subjectOf(r, list);
    auto mine = about(list, subj);
    if (mine.empty()) mine = list;
    const std::size_t n = std::min(count, mine.size());
    s += "Les " + std::to_string(n) + " derniers que la demande concerne (sur " + std::to_string(list.size()) + " incidents). R\xC3\xA8gles : cite seulement "
         "ces donn\xC3\xA9" "es ; une valeur absente est \xC2\xAB non disponible \xC2\xBB ; distingue fait v\xC3\xA9rifi\xC3\xA9, relation indirecte et "
         "hypoth\xC3\xA8se ; l'ordre d'apparition n'est pas une preuve de causalit\xC3\xA9 ; dis \xC2\xAB peut expliquer \xC2\xBB, jamais une cause affirm\xC3\xA9" "e.\n";
    if (!subj.absent.empty()) s += "(La demande cite \xC2\xAB " + subj.absent + " \xC2\xBB : aucun incident ne le cite.)\n";
    const std::size_t budget = maxBytes > s.size() ? (maxBytes - s.size()) / std::max<std::size_t>(1, n) : 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Item& it = *mine[i];
        const auto& sn = it.snapshot;
        std::string b = "- " + shortAt(atOf(it)) + " - " + (commEvent(sn.trigger.kind) ? "communication " : "alarme ") + alarmOf(it) + " (" + stateOf(it)
                        + ", dossier " + it.name + ")\n";
        if (!sn.summary.empty()) b += "  r\xC3\xA9sum\xC3\xA9 : " + sn.summary + "\n";
        if (!sn.alarm.condition.empty()) b += "  condition : " + sn.alarm.condition + "\n";
        if (!sn.conditionVariables.empty()) {
            b += "  valeurs :";
            for (std::size_t v = 0; v < sn.conditionVariables.size() && v < 6; ++v)
                b += std::string(v ? " ;" : "") + " " + sn.conditionVariables[v] + " = " + valueOf(sn, sn.conditionVariables[v]);
            b += "\n";
        }
        if (!sn.hypotheses.empty()) {
            b += "  pistes :";
            for (std::size_t h = 0; h < sn.hypotheses.size() && h < 4; ++h)
                b += std::string(h ? " ;" : "") + " [" + std::string(inc::certaintyLabel(sn.hypotheses[h].certainty)) + "] " + sn.hypotheses[h].text;
            b += "\n";
        }
        if (const auto eqs = equipmentsOf(it); !eqs.empty()) {
            b += "  \xC3\xA9quipements :";
            for (std::size_t e = 0; e < eqs.size() && e < 6; ++e) b += std::string(e ? "," : "") + " " + eqs[e].first + (eqs[e].second <= 1 ? " (direct)" : " (indirect)");
            b += "\n";
        }
        std::string rel;
        for (std::size_t k = 0; k < sn.related.size() && k < 6; ++k)
            rel += std::string(k ? " ;" : "") + " " + sn.related[k].name + " (" + std::string(inc::relatedLabel(sn.related[k].kind)) + (sn.related[k].why.empty() ? std::string{} : " : " + sn.related[k].why) + ")";
        if (!rel.empty()) b += "  alarmes li\xC3\xA9" "es :" + rel + "\n";
        std::string tl;
        std::size_t shown = 0;
        for (const auto& e : sn.timeline) {
            if (std::abs(e.t - sn.trigger.t) > 10.0 || shown >= 8) continue;
            const double dt = e.t - sn.trigger.t;
            tl += std::string(shown++ ? " ;" : "") + " " + (dt < 0 ? "-" : "+") + seconds(dt) + " " + std::string(inc::eventLabel(e.kind)) + " " + e.name;
        }
        if (!tl.empty()) b += "  chronologie (autour du d\xC3\xA9" "faut) :" + tl + "\n";
        if (const auto* an = it.latest(); an && an->number > 0) b += "  analyse IA (" + an->source + ") : " + it.synthesis() + " (certitude : " + it.certainty() + ")\n";
        if (!sn.missing.empty()) b += "  non disponible : " + sn.missing.front() + (sn.missing.size() > 1 ? " (+" + std::to_string(sn.missing.size() - 1) + ")" : std::string{}) + "\n";
        s += clip(std::move(b), budget);
    }
    return clip(std::move(s), maxBytes);
}

} // namespace app::ai::diagnosis
