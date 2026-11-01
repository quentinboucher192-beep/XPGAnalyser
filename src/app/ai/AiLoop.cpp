// =============================================================================
//  app/ai/AiLoop.cpp - 1.22.4 : ne pas tourner en rond (les empreintes, les signes, la retenue)
// =============================================================================
#include "AiLoop.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>

namespace app::ai::loop {

namespace {

using mission::Mission;
using mission::ProposalLink;
using mission::Step;

constexpr char kSep = '\x1f';   // entre les champs d'une ligne d'operation (jamais dans un texte de l'IA)

// FNV-1a 64 bits, en 16 chiffres hexadecimaux : une empreinte de comparaison (pas une signature).
std::string hashHex(std::string_view s) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// Sans les blancs en trop (debut, fin, suites de blancs), en minuscules (ASCII).
std::string norm(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool gap = false;
    for (const char c : s) {
        if (space(c)) {
            gap = !out.empty();
            continue;
        }
        if (gap) out += ' ';
        gap = false;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Minuscules, sans accents (les lettres accentuees courantes du francais), les blancs resserres.
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
    return norm(out);
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : std::string(sep)) + x;
    return s;
}

std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

// Coupe a `max` octets (sans casser un caractere UTF-8), puis des points de suspension.
std::string shortText(std::string_view s, std::size_t max) {
    if (s.size() <= max) return std::string(s);
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    std::string out(s.substr(0, cut));
    while (!out.empty() && space(out.back())) out.pop_back();
    return out + "\xE2\x80\xA6";
}

// Coupe a `max` octets au dernier blanc (s'il n'est pas trop tot), puis des points de suspension.
std::string shortWords(std::string_view s, std::size_t max) {
    if (s.size() <= max) return std::string(s);
    std::size_t cut = max;
    while (cut > max / 2 && !space(s[cut])) --cut;
    if (cut <= max / 2) return shortText(s, max);
    std::string out(s.substr(0, cut));
    while (!out.empty() && (space(out.back()) || out.back() == ':' || out.back() == ',' || out.back() == ';')) out.pop_back();
    return out + "\xE2\x80\xA6";
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

std::string upperFirst(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

bool listed(const std::vector<std::string>& v, std::string_view s) { return std::find(v.begin(), v.end(), s) != v.end(); }

std::string tag(const ProposalLink& p) { return "P" + std::to_string(p.order); }

std::string count(int n, std::string_view one, std::string_view many) { return std::to_string(n) + " " + std::string(n > 1 ? many : one); }

std::string sameOps(int n) { return n == 1 ? std::string("la m\xC3\xAAme op\xC3\xA9ration") : "m\xC3\xAAmes " + std::to_string(n) + " op\xC3\xA9rations"; }
std::string sameErrors(int n) { return n == 1 ? std::string("la m\xC3\xAAme erreur") : "m\xC3\xAAmes " + std::to_string(n) + " erreurs"; }
std::string errorsOf(int n, const std::string& names, bool same) {
    std::string s = same ? sameErrors(n) : count(n, "erreur", "erreurs");
    if (!names.empty()) s += " : " + names;
    return s;
}

// Le sort d'une proposition de mission, en un mot.
std::string stateWord(const ProposalLink& l) {
    if (l.state == "refusee") return "refus\xC3\xA9" "e";
    if (l.state == "obsolete") return "obsol\xC3\xA8te";
    if (l.state == "echouee") return "en \xC3\xA9" "chec";
    if (l.state == "annulee") return "annul\xC3\xA9" "e";
    if (l.state == "interrompue") return "interrompue";
    if (l.state == "remplacee") return "remplac\xC3\xA9" "e par sa correction";   // 1.22.5 : l'avis du logiciel
    if (l.state == "appliquee") return l.failed > 0 ? "appliqu\xC3\xA9" "e en partie" : "appliqu\xC3\xA9" "e";
    if (l.state == "generee" || l.state == "validee") return "\xC3\xA0 valider";
    return l.state;
}

// Le sort d'une proposition d'une conversation, en un mot.
std::string seenWord(std::string_view state) {
    if (state == "refusee") return "refus\xC3\xA9" "e";
    if (state == "appliquee") return "appliqu\xC3\xA9" "e";
    if (state == "annulee") return "appliqu\xC3\xA9" "e puis annul\xC3\xA9" "e";
    if (state == "proposee") return "pas encore d\xC3\xA9" "cid\xC3\xA9" "e";
    if (state == "remplacee") return "remplac\xC3\xA9" "e par sa correction";   // 1.22.5
    return std::string(state);
}

bool covers(const ProposalLink& l, std::string_view step) { return listed(l.steps, step); }

// Une liste ordonnee, normalisee, d'un champ a plusieurs valeurs.
std::string listOf(const std::vector<std::string>& v, bool sorted) {
    std::vector<std::string> n;
    n.reserve(v.size());
    for (const auto& x : v)
        if (std::string y = norm(x); !y.empty()) n.push_back(std::move(y));
    if (sorted) std::sort(n.begin(), n.end());
    return join(n, ",");
}

std::string specOf(const Operation::VariableSpec& v) {
    return norm(v.name) + "|" + norm(v.type) + "|" + norm(v.initial) + "|" + norm(v.folder) + "|" + norm(v.description);
}

} // namespace

// ---- les empreintes ------------------------------------------------------------------------------------------------
std::string opLine(const Operation& op) {
    std::string s(actionKey(op.action));
    const auto field = [&s](std::string_view key, const std::string& value) {
        if (value.empty()) return;
        s += kSep;
        s += key;
        s += '=';
        s += value;
    };
    field("genre", norm(op.kind));
    field("nom", norm(op.name));
    field("vue", norm(op.target));
    field("variable", norm(op.variable));
    field("bord", norm(op.edge));
    field("objets", listOf(op.objects, true));
    // les proprietes triees par cle, sans la position (x, y)
    std::vector<std::string> props;
    for (const auto& [k, v] : op.props) {
        const std::string key = norm(k);
        if (key.empty() || key == "x" || key == "y") continue;
        props.push_back(key + "=" + norm(v));
    }
    std::sort(props.begin(), props.end());
    field("proprietes", join(props, std::string(1, kSep)));
    field("copies", listOf(op.names, false));
    field("code", norm(op.code));
    std::vector<std::string> vars;
    for (const auto& v : op.variables) vars.push_back(specOf(v));
    std::sort(vars.begin(), vars.end());
    field("variables", join(vars, ";"));
    std::vector<std::string> members;
    for (const auto& mb : op.members) members.push_back(norm(mb.name) + "|" + norm(mb.type) + "|" + norm(mb.description));
    field("membres", join(members, ";"));
    std::vector<std::string> params;
    for (const auto& pm : op.params) params.push_back(specOf(pm));
    field("parametres", join(params, ";"));
    field("generiques", listOf(op.generics, false));
    if (op.count != 1) field("nombre", std::to_string(op.count));
    if (op.markers) field("reperes", "1");
    if (op.detect) field("detecter", "1");
    field("exclure", listOf(op.keep, true));
    return s;
}

std::string opsPrint(const std::vector<Operation>& ops) {
    if (ops.empty()) return {};
    std::vector<std::string> lines;
    lines.reserve(ops.size());
    for (const auto& op : ops) lines.push_back(opLine(op));
    std::sort(lines.begin(), lines.end());
    return hashHex(join(lines, "\n"));
}

std::string opsPrint(const Proposal& p) { return opsPrint(p.ops); }

std::string errorKey(std::string_view message) {
    std::string f = folded(message);
    // la liste << les plus proches >> change avec le projet : elle ne fait pas l'erreur
    if (const auto at = f.find("les plus proches"); at != std::string::npos) {
        f.resize(at);
        while (!f.empty() && (space(f.back()) || f.back() == ';' || f.back() == '(' || f.back() == ',')) f.pop_back();
    }
    std::string out;
    out.reserve(f.size());
    for (std::size_t i = 0; i < f.size();) {
        if (!std::isdigit(static_cast<unsigned char>(f[i]))) {
            out += f[i++];
            continue;
        }
        std::size_t j = i;
        while (j < f.size() && std::isdigit(static_cast<unsigned char>(f[j]))) ++j;
        const auto glued = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '%' || c == '.'; };
        const bool inName = (i > 0 && glued(f[i - 1])) || (j < f.size() && (std::isalpha(static_cast<unsigned char>(f[j])) || f[j] == '_'));
        if (inName) out.append(f, i, j - i);   // Pompe_1, %MW100 : un nom, garde
        else out += '#';                       // << operation 12 >> : un nombre isole, sans importance
        i = j;
    }
    return out;
}

std::string errorsPrint(const std::vector<std::string>& errors) {
    std::set<std::string> keys;
    for (const auto& e : errors)
        if (std::string k = errorKey(e); !k.empty()) keys.insert(std::move(k));
    if (keys.empty()) return {};
    std::string all;
    for (const auto& k : keys) all += k + "\n";
    return hashHex(all);
}

std::string errorNames(const std::vector<std::string>& errors, std::size_t max) {
    std::vector<std::string> names;
    for (const auto& e : errors) {
        const auto a = e.find("\xC2\xAB ");
        if (a == std::string::npos) continue;
        const auto b = e.find(" \xC2\xBB", a + 3);
        if (b == std::string::npos) continue;
        std::string n = e.substr(a + 3, b - a - 3);
        while (!n.empty() && space(n.back())) n.pop_back();
        while (!n.empty() && space(n.front())) n.erase(n.begin());
        if (!n.empty() && !listed(names, n)) names.push_back(std::move(n));
    }
    if (names.empty()) {   // aucun nom entre guillemets : le debut de la premiere erreur (sans sa parenthese)
        if (errors.empty()) return {};
        std::string e = errors.front();
        if (const auto k = e.find(" ("); k != std::string::npos && k >= 10) e.resize(k);
        return shortWords(e, 70);
    }
    const bool more = names.size() > max;
    if (more) names.resize(max);
    return join(names, ", ") + (more ? "\xE2\x80\xA6" : "");
}

std::string nextKey(std::string_view next) {
    std::string k = folded(next);
    while (!k.empty() && (k.back() == '.' || k.back() == ';' || k.back() == ':' || k.back() == '!' || space(k.back()))) k.pop_back();
    return k;
}

// ---- les signes -----------------------------------------------------------------------------------------------------
std::string_view signKey(Sign s) noexcept {
    switch (s) {
        case Sign::SameOps: return "memes_operations";
        case Sign::SameErrors: return "memes_erreurs";
        case Sign::Attempts: return "tentatives";
        case Sign::SamePoint: return "meme_point";
        case Sign::SameNext: return "meme_suite";
    }
    return "?";
}

std::string_view signLabel(Sign s) noexcept {
    switch (s) {
        case Sign::SameOps: return "M\xC3\xAAmes op\xC3\xA9rations";
        case Sign::SameErrors: return "M\xC3\xAAmes erreurs";
        case Sign::Attempts: return "Trop de tentatives";
        case Sign::SamePoint: return "Le m\xC3\xAAme point";
        case Sign::SameNext: return "La m\xC3\xAAme suite";
    }
    return "?";
}

bool Circle::has(Sign s) const noexcept { return std::find(signs.begin(), signs.end(), s) != signs.end(); }

std::vector<std::string> Circle::keys() const {
    std::vector<std::string> out;
    for (const Sign s : signs) out.emplace_back(signKey(s));
    return out;
}

namespace {

void addSign(Circle& c, Sign s) {
    if (!c.has(s)) c.signs.push_back(s);
}
void addStep(Circle& c, const std::string& id) {
    if (!id.empty() && !listed(c.steps, id)) c.steps.push_back(id);
}
void finish(Circle& c) {
    if (c.lines.size() > 5) {
        c.lines.resize(5);
        c.lines.back() += " \xE2\x80\xA6";
    }
    c.text = upperFirst(join(c.lines, " ; "));
}

} // namespace

Circle missionCircle(const Mission& m, std::string_view proposalId) {
    Circle c;
    std::size_t at = m.proposals.size();
    for (std::size_t i = 0; i < m.proposals.size(); ++i)
        if (m.proposals[i].id == proposalId) at = i;
    if (at == m.proposals.size()) return c;
    const ProposalLink& now = m.proposals[at];
    const std::string P = tag(now);
    std::vector<const ProposalLink*> earlier;   // les plus recentes d'abord
    for (std::size_t i = at; i-- > 0;) earlier.push_back(&m.proposals[i]);
    // (a) LES MEMES OPERATIONS qu'une proposition precedente, quel que soit son sort - sauf une proposition devenue
    // obsolete parce que le projet a change : refaite a l'identique sur un AUTRE etat du projet, ce n'est pas un cercle
    // (le meme etat du projet : si).
    const ProposalLink* same = nullptr;
    if (!now.opsPrint.empty() && now.ops > 0)
        for (const auto* l : earlier) {
            if (l->opsPrint != now.opsPrint) continue;
            if (l->state == "obsolete" && (l->projectPrint.empty() || l->projectPrint != now.projectPrint)) continue;
            same = l;
            break;
        }
    // (b) LES MEMES ERREURS que la tentative precedente pour la meme etape (deux fois de suite)
    const ProposalLink* sameErr = nullptr;
    std::vector<std::string> errSteps;
    if (!now.errorsPrint.empty()) {
        const auto previousFor = [&](std::string_view sid) -> const ProposalLink* {
            for (const auto* l : earlier)
                if (sid.empty() || covers(*l, sid)) return l;
            return nullptr;
        };
        const std::vector<std::string> mine = now.steps.empty() ? std::vector<std::string>{std::string{}} : now.steps;
        for (const auto& sid : mine) {
            const ProposalLink* prev = previousFor(sid);
            if (!prev || prev->errorsPrint != now.errorsPrint) continue;
            if (!sameErr) sameErr = prev;
            if (prev == sameErr && !sid.empty()) errSteps.push_back(sid);
        }
    }
    if (same) {
        addSign(c, Sign::SameOps);
        const bool alsoErrors = sameErr == same;
        if (alsoErrors) addSign(c, Sign::SameErrors);
        std::string line = P + " refait " + tag(*same) + " (" + stateWord(*same) + " ; " + sameOps(now.ops);
        if (now.errorCount > 0) line += ", " + errorsOf(now.errorCount, now.errorNames, alsoErrors);
        line += ")";
        c.lines.push_back(std::move(line));
        c.again = same->id;
        for (const auto& s : now.steps)
            if (covers(*same, s)) addStep(c, s);
        if (c.steps.empty())
            for (const auto& s : now.steps) addStep(c, s);
    }
    if (sameErr && sameErr != same) {
        addSign(c, Sign::SameErrors);
        c.lines.push_back(P + " retombe sur les m\xC3\xAAmes erreurs que " + tag(*sameErr) + (errSteps.empty() ? std::string{} : " pour " + join(errSteps, ", ")) + " ("
                          + errorsOf(now.errorCount, now.errorNames, false) + ")");
        if (c.again.empty()) c.again = sameErr->id;
        for (const auto& s : errSteps) addStep(c, s);
    }
    // (c) UNE ETAPE PROPOSEE kMaxAttempts FOIS sans devenir terminee
    for (const auto& sid : now.steps) {
        const Step* s = m.step(sid);
        if (!s || s->attempts < kMaxAttempts || s->state == mission::StepState::Done) continue;
        std::vector<std::string> tags;
        const std::size_t n = std::min<std::size_t>(s->proposals.size(), static_cast<std::size_t>(s->attempts));
        for (std::size_t k = s->proposals.size() - n; k < s->proposals.size(); ++k)
            if (const ProposalLink* l = m.proposal(s->proposals[k])) tags.push_back(tag(*l));
        addSign(c, Sign::Attempts);
        c.lines.push_back(sid + " a \xC3\xA9t\xC3\xA9 propos\xC3\xA9" "e " + std::to_string(s->attempts) + " fois sans aboutir"
                          + (tags.empty() ? std::string{} : " (" + join(tags, ", ") + ")"));
        addStep(c, sid);
    }
    // (d) LE PROJET AU MEME POINT, la meme etape en attente, pour la 3e fois (depuis la derniere consigne de l'utilisateur
    // pour elle : Modifier la demande repart d'un pas neuf)
    if (!now.projectPrint.empty())
        for (const auto& sid : now.steps) {
            std::vector<std::string> tags;
            for (auto it = earlier.rbegin(); it != earlier.rend(); ++it) {   // dans l'ordre
                if (!covers(**it, sid)) continue;
                if ((*it)->loopChoice == "demande_modifiee") {
                    tags.clear();
                    continue;
                }
                // 1.22.5 : une proposition remplacee par sa correction automatique (Generer et Compiler) est la MEME tentative
                // que sa correction : le projet n'a pas bouge entre les deux, ce n'est pas un retour au meme point. Une
                // correction qui refait les memes operations ou retombe sur les memes erreurs reste un cercle ((a), (b)).
                if ((*it)->state == "remplacee") continue;
                if ((*it)->projectPrint == now.projectPrint) tags.push_back(tag(**it));
            }
            if (static_cast<int>(tags.size()) < kSamePoint) continue;
            addSign(c, Sign::SamePoint);
            std::string list = tags.size() == 2 ? tags[0] + " et " + tags[1] : join(tags, ", ");
            c.lines.push_back("le projet est revenu au m\xC3\xAAme point qu'avant " + list + ", avec " + sid + " toujours en attente");
            addStep(c, sid);
        }
    finish(c);
    return c;
}

std::string ordinalTag(int order) { return std::string("la ") + std::to_string(order) + (order == 1 ? "re" : "e") + " proposition"; }

Seen seenOf(const Proposal& p) {
    Seen s;
    s.id = p.id;
    s.state = p.state;
    const json::Value& x = p.extra.at("cercle");
    s.ops = x.str("operations");
    s.opCount = static_cast<int>(x.integer("nb_operations", static_cast<std::int64_t>(p.ops.size())));
    if (s.ops.empty()) {   // une proposition d'avant 1.22.4 : calculee (une fois, a l'arrivee de la suivante)
        s.ops = opsPrint(p);
        s.opCount = static_cast<int>(p.ops.size());
    }
    s.errors = x.str("erreurs");
    s.errorCount = static_cast<int>(x.integer("nb_erreurs"));
    s.names = x.str("noms");
    s.next = x.has("suite") ? x.str("suite") : nextKey(p.next);
    s.nextText = p.next;
    return s;
}

Circle conversationCircle(const std::vector<Seen>& earlier, const Seen& now) {
    Circle c;
    const auto subject = [&c](std::string_view verb) { return std::string(c.lines.empty() ? "Cette proposition " : "elle ") + std::string(verb); };
    const Seen* same = nullptr;
    if (!now.ops.empty() && now.opCount > 0)
        for (auto it = earlier.rbegin(); it != earlier.rend(); ++it)
            if (it->ops == now.ops) {
                same = &*it;
                break;
            }
    const Seen* prev = earlier.empty() ? nullptr : &earlier.back();
    const bool sameErr = prev && !now.errors.empty() && prev->errors == now.errors;
    if (same) {
        addSign(c, Sign::SameOps);
        const bool alsoErrors = sameErr && prev == same;
        if (alsoErrors) addSign(c, Sign::SameErrors);
        std::string line = subject("refait ") + same->tag + " (" + seenWord(same->state) + " ; " + sameOps(now.opCount);
        if (now.errorCount > 0) line += ", " + errorsOf(now.errorCount, now.names, alsoErrors);
        c.lines.push_back(line + ")");
        c.again = same->id;
    }
    if (sameErr && prev != same) {
        addSign(c, Sign::SameErrors);
        c.lines.push_back(subject("retombe sur les m\xC3\xAAmes erreurs que ") + prev->tag + " (" + errorsOf(now.errorCount, now.names, false) + ")");
        if (c.again.empty()) c.again = prev->id;
    }
    if (!now.next.empty())
        for (auto it = earlier.rbegin(); it != earlier.rend(); ++it)
            if (it->next == now.next) {
                addSign(c, Sign::SameNext);
                c.lines.push_back("la m\xC3\xAAme suite revient (" + guill(shortWords(trimmed(now.nextText.empty() ? now.next : now.nextText), 90)) + ", d\xC3\xA9j\xC3\xA0 annonc\xC3\xA9" "e par "
                                  + it->tag + ") : la demande n'avance pas");
                if (c.again.empty()) c.again = it->id;
                break;
            }
    finish(c);
    return c;
}

void stamp(Proposal& p, const std::vector<std::string>* errors) {
    json::Value& x = p.extra["cercle"];   // un extra nul devient un objet
    x.set("operations", opsPrint(p));
    x.set("nb_operations", static_cast<int>(p.ops.size()));
    x.set("suite", nextKey(p.next));
    if (errors) {
        x.set("erreurs", errorsPrint(*errors));
        x.set("nb_erreurs", static_cast<int>(errors->size()));
        x.set("noms", errorNames(*errors));
    }
}

// ---- la mission : retenir, choisir ----------------------------------------------------------------------------------
void flag(Mission& m, std::string_view proposalId, const Circle& c, std::string now) {
    ProposalLink* l = m.proposal(proposalId);
    if (!l || !c.on()) return;
    l->loop = c.text;
    l->loopSigns = c.keys();
    l->loopSteps = c.steps;
    l->loopChoice.clear();
    const std::string P = tag(*l);
    for (const auto& sid : l->steps)
        if (Step* s = m.step(sid); s && s->state == mission::StepState::AwaitingValidation) {
            s->note = P + " tourne en rond : choisis sur sa carte.";
            s->updated = now;
        }
    mission::log(m, "cercle",
                 "La mission tourne en rond : " + c.text + ". " + P + " est retenue : choisis sur sa carte (Modifier la demande, Corriger \xC3\xA0 la main, "
                 "Ignorer cette \xC3\xA9tape, Forcer la fin de la mission, Proposer quand m\xC3\xAAme).",
                 std::move(now), c.steps.size() == 1 ? c.steps.front() : std::string{}, l->id);
}

const ProposalLink* held(const Mission& m) {
    for (const auto& p : m.proposals)
        if (p.held()) return &p;
    return nullptr;
}

std::string_view choiceLabel(std::string_view choice) {
    if (choice == "quand_meme") return "Propos\xC3\xA9" "e quand m\xC3\xAAme";
    if (choice == "a_la_main") return "Corrig\xC3\xA9" "e \xC3\xA0 la main";
    if (choice == "demande_modifiee") return "Demande modifi\xC3\xA9" "e";
    if (choice == "etape_ignoree") return "\xC3\x89tape ignor\xC3\xA9" "e";
    if (choice == "fin_forcee") return "Fin forc\xC3\xA9" "e de la mission";
    return {};
}

bool choose(Mission& m, std::string_view proposalId, std::string_view choice, std::string now, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    ProposalLink* l = m.proposal(proposalId);
    if (!l) return fail("Proposition inconnue.");
    const std::string_view label = choiceLabel(choice);
    if (label.empty()) return fail("Choix inconnu.");
    if (!l->held()) return fail(tag(*l) + " n'attend pas de choix : elle ne tourne pas en rond, ou c'est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "cid\xC3\xA9.");
    l->loopChoice = std::string(choice);
    const std::string P = tag(*l);
    // Modifier la demande : la consigne de l'utilisateur repart d'un pas neuf - les tentatives de ses etapes aussi.
    if (choice == "demande_modifiee")
        for (const auto& sid : l->steps)
            if (Step* s = m.step(sid)) s->attempts = 0;
    if (choice == "quand_meme" || choice == "a_la_main")
        for (const auto& sid : l->steps)
            if (Step* s = m.step(sid); s && s->state == mission::StepState::AwaitingValidation) {
                s->note = P + " attend d'\xC3\xAAtre pr\xC3\xA9visualis\xC3\xA9" "e et appliqu\xC3\xA9" "e (" + std::string(label) + ").";
                s->updated = now;
            }
    m.decisions.push_back({now, P + " tourne en rond : " + shortText(l->loop, 240), std::string(label)});
    mission::log(m, "decision", P + " : " + std::string(label) + " (elle tournait en rond).", std::move(now),
                 l->loopSteps.size() == 1 ? l->loopSteps.front() : std::string{}, l->id);
    return true;
}

bool addConsigne(Mission& m, const std::vector<std::string>& steps, std::string text, std::string now) {
    const std::string t = trimmed(text);
    if (t.empty()) return false;
    const std::string who = steps.empty() ? std::string("la mission") : join(steps, ", ");
    m.decisions.push_back({now, "Consigne pour " + who, t});
    mission::log(m, "decision", "Consigne de l'utilisateur pour " + who + " : " + guill(shortText(t, 240)), std::move(now),
                 steps.size() == 1 ? steps.front() : std::string{});
    return true;
}

// ---- en amont -------------------------------------------------------------------------------------------------------
std::string triedText(const Mission& m, const std::vector<std::string>& steps, std::size_t maxChars) {
    constexpr std::size_t kShown = 4;
    std::vector<const ProposalLink*> tried;
    std::size_t total = 0;
    for (auto it = m.proposals.rbegin(); it != m.proposals.rend(); ++it) {
        const ProposalLink& l = *it;
        if (l.state == "generee" || l.state == "validee") continue;   // encore a decider : ce n'est pas un echec
        if (l.state == "appliquee" && l.failed == 0) continue;        // appliquee en entier : le projet le dit (plus haut)
        if (!std::any_of(steps.begin(), steps.end(), [&](const std::string& s) { return covers(l, s); })) continue;
        ++total;
        if (tried.size() < kShown) tried.push_back(&l);
    }
    std::string s;
    if (!tried.empty()) {
        s += "D\xC3\xA9j\xC3\xA0 propos\xC3\xA9 pour ces \xC3\xA9tapes, sans aboutir (refus\xC3\xA9, en \xC3\xA9" "chec, obsol\xC3\xA8te ou annul\xC3\xA9 ; le plus r\xC3\xA9" "cent d'abord) :\n";
        std::size_t shown = 0;
        for (const auto* l : tried) {
            std::string line = "- " + tag(*l) + " (" + stateWord(*l) + ")" + (l->title.empty() ? std::string{} : " " + guill(shortText(l->title, 80))) + " : "
                               + count(l->ops, "op\xC3\xA9ration", "op\xC3\xA9rations");
            std::vector<std::string> titles;
            for (const auto& ol : l->lines) {
                if (titles.size() == 3) break;
                if (!ol.title.empty()) titles.push_back(shortText(ol.title, 50));
            }
            if (!titles.empty()) line += " (" + join(titles, ", ") + (l->lines.size() > titles.size() ? ", \xE2\x80\xA6" : "") + ")";
            // ses erreurs : celles de l'application (appliquee, en echec), sinon celles de sa verification (les raisons d'un
            // refus, ajoutees apres, ne sont pas des erreurs : la raison d'une proposition obsolete ou annulee est dite a part)
            const bool applied = l->state == "appliquee" || l->state == "echouee";
            const std::size_t nErr = applied ? l->errors.size() : std::min(l->errors.size(), static_cast<std::size_t>(std::max(0, l->errorCount)));
            if (nErr > 0) {
                std::vector<std::string> errs;
                for (std::size_t k = 0; k < nErr && k < 2; ++k) errs.push_back(shortText(l->errors[k], 150));
                line += " \xE2\x80\x94 erreurs : " + join(errs, " ; ") + (nErr > 2 ? " ; et " + std::to_string(nErr - 2) + " autre(s)" : "");
            }
            if (!applied && l->state != "refusee" && l->errors.size() > nErr) line += " \xE2\x80\x94 raison : " + shortText(l->errors.back(), 150);
            if (!l->loop.empty()) line += " \xE2\x80\x94 elle tournait en rond : " + shortText(l->loop, 160);
            line = shortText(line, 480);
            if (s.size() + line.size() + 1 > maxChars - std::min<std::size_t>(maxChars, 450)) {
                s += "- \xE2\x80\xA6\n";
                break;
            }
            s += line + "\n";
            ++shown;
        }
        if (total > shown) s += "- et " + std::to_string(total - shown) + " tentative(s) plus ancienne(s).\n";
        s += "Ne repropose pas la m\xC3\xAAme chose : change d'approche, ou dis clairement ce qui bloque. Une variable introuvable ne "
             "s'invente pas : utilise une variable qui existe dans le projet, ou dis laquelle manque (l'utilisateur choisira de la cr\xC3\xA9" "er ou "
             "d'en prendre une autre).\n";
    }
    // LES CONSIGNES DE L'UTILISATEUR pour ces etapes (Modifier la demande) : les plus recentes, 3 au plus.
    std::vector<std::string> said;
    for (auto it = m.decisions.rbegin(); it != m.decisions.rend() && said.size() < 3; ++it) {
        if (it->question.rfind("Consigne pour ", 0) != 0) continue;
        const std::string who = it->question.substr(14);
        bool mine = who == "la mission";
        for (std::size_t a = 0; a <= who.size() && !mine;) {   // "S3, S4" : les identifiants un par un
            std::size_t b = who.find(", ", a);
            if (b == std::string::npos) b = who.size();
            if (listed(steps, who.substr(a, b - a))) mine = true;
            a = b + 2;
        }
        if (mine) said.push_back("- " + who + " : " + shortText(it->answer, 500));
    }
    if (!said.empty()) {
        std::reverse(said.begin(), said.end());
        s += "CONSIGNES DE L'UTILISATEUR pour ces \xC3\xA9tapes (\xC3\xA0 suivre) :\n" + join(said, "\n") + "\n";
    }
    return s;
}

} // namespace app::ai::loop
