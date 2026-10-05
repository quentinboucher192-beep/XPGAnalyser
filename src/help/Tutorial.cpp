#include "help/Tutorial.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace help {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

std::vector<std::string> splitList(std::string_view s, char sep) {
    std::vector<std::string> out;
    while (true) {
        const auto pos = s.find(sep);
        const auto part = trim(s.substr(0, pos));
        if (!part.empty()) out.emplace_back(part);
        if (pos == std::string_view::npos) break;
        s.remove_prefix(pos + 1);
    }
    return out;
}

void replaceAll(std::string& s, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    for (std::size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

bool parseInt(std::string_view s, int& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

std::optional<double> parseNumber(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    std::string tmp(s);
    for (auto& c : tmp) if (c == ',') c = '.';
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end != tmp.c_str() + tmp.size()) return std::nullopt;
    return v;
}

// Les genres de cible que la scene sait trouver (CONCEPTION-T1, 1.4).
constexpr std::string_view kTargetKinds[] = {
    "biblio", "variante", "vue", "propriete", "outil", "bouton", "arbre", "onglet", "sous-onglet",
    "editeur", "saisie", "ecran", "alarme", "zone", "menu", "script",
    "champ", "ligne",    // tranche 6 : un champ d'un dialogue (N-ieme ou identifiant), la ligne d'un tableau
    "barre",             // tranche 24 (R111-12) : une partie de la barre du haut (barre:? : le "?", TutorialScreen.hpp)
    "macro"};            // 1.11.2 (R1112-3) : une macro de la liste de l'onglet Macros (UiDriver::locate)

bool isOp(std::string_view w) {
    return w == "=" || w == "<>" || w == "<" || w == "<=" || w == ">" || w == ">=" || w == "~";
}

// "<chemin> <op> <valeur>" : le chemin et la valeur peuvent contenir des espaces.
bool parseCondition(std::string_view text, TutorialCheck& out) {
    const auto w = tutorialWords(text);
    for (std::size_t i = 1; i + 1 <= w.size(); ++i) {
        if (!isOp(w[i])) continue;
        out.path.clear();
        out.value.clear();
        for (std::size_t k = 0; k < i; ++k) out.path += (k ? " " : "") + w[k];
        for (std::size_t k = i + 1; k < w.size(); ++k) out.value += (k > i + 1 ? " " : "") + w[k];
        out.op = w[i];
        return !out.path.empty() && i + 1 < w.size();
    }
    return false;
}

const char* kDefaultBravo = "Bravo !";

} // namespace

std::vector<std::string> tutorialWords(std::string_view line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size()) break;
        std::string w;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                // \" et \\ seulement, comme ScriptRunner : un autre \ reste tel quel.
                if (line[i] == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) ++i;
                w += line[i++];
            }
            ++i;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') w += line[i++];
        }
        out.push_back(std::move(w));
    }
    return out;
}

int parseDurationMs(std::string_view w) {
    w = trim(w);
    if (w.empty()) return -1;
    double factor = 1000.0 / 30.0;  // un entier nu : des images a 1/30 s
    if (w.size() > 2 && w.substr(w.size() - 2) == "ms") { factor = 1.0; w.remove_suffix(2); }
    else if (w.back() == 's') { factor = 1000.0; w.remove_suffix(1); }
    const auto v = parseNumber(w);
    if (!v || *v < 0) return -1;
    return static_cast<int>(std::lround(*v * factor));
}

std::size_t utf8Letters(std::string_view text) {
    std::size_t n = 0;
    for (const char c : text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

int readingTimeMs(std::string_view text) {
    const auto ms = static_cast<int>(utf8Letters(text)) * 45;
    return std::clamp(ms, 1500, 6000);
}

int gestureGapMs(GestureKind kind) {
    switch (kind) {
    case GestureKind::Spot:
    case GestureKind::Prepare: return 0;
    case GestureKind::Say: return kSayGapMs;
    default: return kGestureGapMs;
    }
}

std::string formatClock(int ms) {
    if (ms < 0) ms = 0;
    const int s = (ms + 500) / 1000;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

bool isPrepareCommand(std::string_view word) {
    // Tranche 14 (decision du chef) : action <identifiant>, une action de l'appli (App::actions()).
    // Tranche 19 (decision 12 du 03/10) : aide <cle>, le centre d'aide ouvert sur ce sujet.
    return word == "vue" || word == "poser" || word == "regler" || word == "variable" || word == "type"
           || word == "action" || word == "aide";
}

const char* gestureName(GestureKind kind) {
    switch (kind) {
    case GestureKind::Move: return "survol";
    case GestureKind::Click: return "clic";
    case GestureKind::Drag: return "glisser";
    case GestureKind::Type: return "texte";
    case GestureKind::Key: return "touche";
    case GestureKind::Spot: return "encadrer";
    case GestureKind::Say: return "dire";
    case GestureKind::Wait: return "attendre";
    case GestureKind::Run: return "simuler";
    case GestureKind::Prepare: return "preparer";
    }
    return "?";
}

bool isKnownTarget(std::string_view target) {
    if (target.empty()) return false;
    const auto colon = target.find(':');
    if (colon == std::string_view::npos)
        return target.find(' ') == std::string_view::npos;  // une zone nommee : variantes, apercu-etats...
    const auto kind = target.substr(0, colon);
    if (colon + 1 >= target.size()) return false;
    return std::find(std::begin(kTargetKinds), std::end(kTargetKinds), kind) != std::end(kTargetKinds);
}

bool VariantFilter::accepts(std::string_view variant) const {
    if (names.empty()) return true;
    const bool in = std::find(names.begin(), names.end(), variant) != names.end();
    return negate ? !in : in;
}

std::size_t CompiledTutorial::stepAt(double ms) const {
    if (steps.empty()) return 0;
    for (std::size_t i = 0; i < steps.size(); ++i)
        if (ms < steps[i].startMs + steps[i].durationMs) return i;
    return steps.size() - 1;
}

Tutorial Tutorial::parse(std::string_view text, std::vector<TutorialProblem>* problems) {
    Tutorial t;
    auto problem = [&](int line, std::string msg) {
        if (problems) problems->push_back({line, std::move(msg)});
    };
    bool haveHeader = false;
    bool inBefore = false;   // dans la section @avant (avant le premier ==)
    std::vector<std::pair<int, VariantFilter>> filtersToCheck;
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto eol = text.find('\n', pos);
        const auto raw = text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
        pos = eol == std::string_view::npos ? text.size() + 1 : eol + 1;
        ++lineNo;
        auto line = trim(raw);
        if (line.empty() || line.front() == '#') continue;

        // [a, b] ou [!a] en tete de ligne : la ligne n'existe que pour ces variantes.
        VariantFilter filter;
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close == std::string_view::npos) {
                problem(lineNo, "crochet [ sans ]");
                continue;
            }
            auto inside = trim(line.substr(1, close - 1));
            if (!inside.empty() && inside.front() == '!') { filter.negate = true; inside.remove_prefix(1); }
            filter.names = splitList(inside, ',');
            filtersToCheck.emplace_back(lineNo, filter);
            line = trim(line.substr(close + 1));
        }

        // L'en-tete et les etapes.
        if (startsWith(line, "==")) {
            SourceStep s;
            s.line = lineNo;
            auto rest = trim(line.substr(2));
            const auto bar = rest.find('|');
            if (bar == std::string_view::npos) {
                problem(lineNo, "\xC3\xA9tape sans titre : == <n> | <titre>");
            } else {
                if (!parseInt(trim(rest.substr(0, bar)), s.writtenNumber))
                    problem(lineNo, "num\xC3\xA9ro d'\xC3\xA9tape illisible");
                auto title = trim(rest.substr(bar + 1));
                if (!title.empty() && title.back() == ']') {
                    const auto open = title.rfind('[');
                    if (open != std::string_view::npos) {
                        auto inside = trim(title.substr(open + 1, title.size() - open - 2));
                        if (!inside.empty() && inside.front() == '!') { s.filter.negate = true; inside.remove_prefix(1); }
                        s.filter.names = splitList(inside, ',');
                        filtersToCheck.emplace_back(lineNo, s.filter);
                        title = trim(title.substr(0, open));
                    }
                }
                s.title = std::string(title);
                if (s.title.empty()) problem(lineNo, "\xC3\xA9tape sans titre");
            }
            if (!filter.names.empty()) s.filter = filter;
            t.source.push_back(std::move(s));
            inBefore = false;
            continue;
        }
        if (line.front() == '=') {
            const auto rest = trim(line.substr(1));
            const auto bar = rest.find('|');
            t.id = std::string(trim(rest.substr(0, bar)));
            if (bar != std::string_view::npos) t.title = std::string(trim(rest.substr(bar + 1)));
            haveHeader = true;
            if (t.id.empty() || t.id.find(' ') != std::string::npos ||
                std::any_of(t.id.begin(), t.id.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
                problem(lineNo, "identifiant : en minuscules, sans espaces");
            if (t.title.empty()) problem(lineNo, "tutoriel sans titre : = <id> | <titre>");
            continue;
        }

        // Les directives @.
        if (line.front() == '@') {
            const auto sp = line.find(' ');
            const auto key = line.substr(0, sp);
            const auto arg = sp == std::string_view::npos ? std::string_view{} : trim(line.substr(sp + 1));
            if (key == "@sujet") { t.topics.emplace_back(arg); continue; }
            if (key == "@avant") {
                if (!t.source.empty()) problem(lineNo, "@avant : avant la premi\xC3\xA8re \xC3\xA9tape");
                else if (inBefore || !t.before.empty()) problem(lineNo, "@avant : une seule fois");
                else inBefore = true;
                continue;
            }
            if (key == "@bac") {
                const auto bar = arg.find('|');
                t.sandbox = std::string(trim(arg.substr(0, bar)));
                if (bar != std::string_view::npos) t.view = std::string(trim(arg.substr(bar + 1)));
                continue;
            }
            if (key == "@variantes") { t.variants = splitList(arg, ','); continue; }
            if (key == "@variante") { t.defaultVariant = std::string(arg); continue; }
            if (key == "@duree-lettre") {
                if (!parseInt(arg, t.msPerLetter) || t.msPerLetter < 0) problem(lineNo, "@duree-lettre : un entier");
                continue;
            }
            Item item;
            item.filter = filter;
            item.line = lineNo;
            if (key == "@atoi") item.kind = Item::Kind::ATry;
            else if (key == "@cible") item.kind = Item::Kind::Target;
            else if (key == "@verifier") item.kind = Item::Kind::Verify;
            else if (key == "@presque") item.kind = Item::Kind::Almost;
            else if (key == "@bravo") item.kind = Item::Kind::Bravo;
            else if (key == "@onok") item.kind = Item::Kind::OnOk;
            else if (key == "@montrer") item.kind = Item::Kind::Show;   // 1.11.2 : ce que l'A toi montre
            else { problem(lineNo, "directive inconnue : " + std::string(key)); continue; }
            if (inBefore) { problem(lineNo, std::string(key) + " : pas dans @avant"); continue; }
            if (t.source.empty()) { problem(lineNo, std::string(key) + " avant la premi\xC3\xA8re \xC3\xA9tape"); continue; }
            if (arg.empty()) { problem(lineNo, std::string(key) + " sans texte"); continue; }
            item.text = std::string(arg);
            if (item.kind == Item::Kind::Target || item.kind == Item::Kind::Show) {
                const auto w = tutorialWords(arg);
                if (w.size() != 1 || !isKnownTarget(w[0])) {
                    problem(lineNo, std::string(key) + " : une cible connue");
                    continue;
                }
                item.text = w[0];
            } else if (item.kind == Item::Kind::Verify || item.kind == Item::Kind::Almost) {
                auto cond = arg;
                if (item.kind == Item::Kind::Almost) {
                    const auto bar = arg.find(" | ");
                    if (bar == std::string_view::npos) { problem(lineNo, "@presque <condition> | <raison>"); continue; }
                    cond = trim(arg.substr(0, bar));
                    item.check.reason = std::string(trim(arg.substr(bar + 3)));
                }
                if (!parseCondition(cond, item.check)) {
                    problem(lineNo, "condition illisible : <chemin> <op> <valeur>, op parmi = <> < <= > >= ~");
                    continue;
                }
                item.check.line = lineNo;
            } else if (item.kind == Item::Kind::OnOk) {
                if (!startsWith(arg, "variante=") || arg.size() <= 9) { problem(lineNo, "@onok variante=<chemin>"); continue; }
                item.text = std::string(arg.substr(9));
            }
            t.source.back().items.push_back(std::move(item));
            continue;
        }

        // La bulle.
        if (line.front() == ':') {
            Item item;
            item.kind = Item::Kind::Bubble;
            item.filter = filter;
            item.line = lineNo;
            item.newParagraph = startsWith(line, "::");
            item.text = std::string(trim(line.substr(item.newParagraph ? 2 : 1)));
            if (inBefore) { problem(lineNo, "pas de bulle dans @avant"); continue; }
            if (t.source.empty()) { problem(lineNo, "bulle avant la premi\xC3\xA8re \xC3\xA9tape"); continue; }
            t.source.back().items.push_back(std::move(item));
            continue;
        }

        // Les gestes.
        const auto w = tutorialWords(line);
        Gesture g;
        g.line = lineNo;
        const auto& cmd = w[0];
        const std::size_t argc = w.size() - 1;
        auto needTarget = [&](const std::string& target) {
            if (!isKnownTarget(target)) {
                problem(lineNo, "cible inconnue : " + target + " (genre:nom, un nom avec des espaces entre guillemets)");
                return false;
            }
            return true;
        };
        bool ok = true;
        if (cmd == "survol" || cmd == "encadrer") {
            g.kind = cmd == "survol" ? GestureKind::Move : GestureKind::Spot;
            g.durationMs = cmd == "survol" ? 650 : 0;   // la maquette : move 650, spot 0
            if (argc != 1) { problem(lineNo, cmd + " <cible>"); ok = false; }
            else { g.target = w[1]; ok = needTarget(g.target); }
        } else if (cmd == "clic") {
            g.kind = GestureKind::Click;
            g.durationMs = 650 + kGestureGapMs + 380;   // la maquette : move, puis click (380)
            if (argc < 1 || argc > 2) { problem(lineNo, "clic <cible> [double|droit]"); ok = false; }
            else {
                g.target = w[1];
                ok = needTarget(g.target);
                if (argc == 2) {
                    if (w[2] == "double") g.doubleClick = true;
                    else if (w[2] == "droit") g.rightClick = true;
                    else { problem(lineNo, "clic <cible> [double|droit] : " + w[2] + " ? (un nom avec des espaces entre guillemets)"); ok = false; }
                }
            }
        } else if (cmd == "glisser") {
            g.kind = GestureKind::Drag;
            g.durationMs = 1400;
            if (argc != 2) { problem(lineNo, "glisser <cible> <cible>"); ok = false; }
            else { g.target = w[1]; g.target2 = w[2]; ok = needTarget(g.target) && needTarget(g.target2); }
        } else if (cmd == "texte") {
            g.kind = GestureKind::Type;
            if (argc == 1) g.text = w[1];
            else if (argc == 2) { g.target = w[1]; g.text = w[2]; ok = needTarget(g.target); }
            else { problem(lineNo, "texte [<cible>] \"<texte>\""); ok = false; }
        } else if (cmd == "touche") {
            g.kind = GestureKind::Key;
            g.durationMs = 520;
            if (argc != 1) { problem(lineNo, "touche <touche>"); ok = false; }
            else g.text = w[1];
        } else if (cmd == "dire") {
            g.kind = GestureKind::Say;
            if (argc != 1) { problem(lineNo, "dire \"<texte>\""); ok = false; }
            else g.text = w[1];
        } else if (cmd == "attendre" || cmd == "simuler") {
            g.kind = cmd == "attendre" ? GestureKind::Wait : GestureKind::Run;
            g.durationMs = argc == 1 ? parseDurationMs(w[1]) : -1;
            if (g.durationMs < 0) { problem(lineNo, cmd + " <dur\xC3\xA9" "e> : 1.5s, 800ms ou des images"); ok = false; }
        } else if (isPrepareCommand(cmd)) {
            // @avant : vue <nom> ; poser <genre> <x,y> [<nom>] ; regler <objet> <propriete> <valeur>.
            g.kind = GestureKind::Prepare;
            g.durationMs = 0;
            g.args = w;
            // 1.11.2 (R1112-3) : aide <cle> aussi dans une etape (un sujet du « ? » : son « A toi » F1 ouvre
            // le centre a l'etape 2, puis l'etape 3 y montre la page du sujet, que ses bulles decrivent).
            if (!inBefore && cmd != "aide") { problem(lineNo, cmd + " : seulement dans @avant"); ok = false; }
            else if (cmd == "vue" && argc != 1) { problem(lineNo, "vue <nom>"); ok = false; }
            else if (cmd == "poser" && (argc < 2 || argc > 3 || w[2].find(',') == std::string::npos)) {
                problem(lineNo, "poser <genre> <x,y> [<nom>]");
                ok = false;
            } else if (cmd == "regler" && argc != 3) { problem(lineNo, "regler <objet> <propri\xC3\xA9t\xC3\xA9> <valeur>"); ok = false; }
            // Tranche 5 : variable <nom> "<type>" (une variable IHM du bac : V "ARRAY[0..3] OF T_VANNE") ;
            // type <nom> "<membre> : <TYPE>; ..." (un type IHM structure : T_VANNE).
            else if (cmd == "variable" && argc != 2) { problem(lineNo, "variable <nom> \"<type>\""); ok = false; }
            else if (cmd == "type" && (argc != 2 || w[2].find(':') == std::string::npos)) {
                problem(lineNo, "type <nom> \"<membre> : <TYPE>; ...\"");
                ok = false;
            }
            // Tranche 14 : action <identifiant> joue une action de l'appli, comme son menu ou son
            // raccourci (action help.expressions : la page des expressions s'ouvre par-dessus).
            // L'identifiant est en minuscules, avec des points, sans espace ; l'appli dit s'il existe.
            else if (cmd == "action" && (argc != 1 || w[1].empty()
                                         || w[1].find_first_of(" \t\"") != std::string::npos)) {
                problem(lineNo, "action <identifiant> : une action de l'appli, par exemple help.expressions");
                ok = false;
            }
            // Tranche 19 : aide <cle> ouvre le centre d'aide sur ce sujet, par-dessus le bac (les pages
            // speciales du centre : aide page-signaler). La cle est celle du centre, sans espace.
            else if (cmd == "aide" && (argc != 1 || w[1].empty()
                                       || w[1].find_first_of(" \t\"") != std::string::npos)) {
                problem(lineNo, "aide <cl\xC3\xA9> : un sujet du centre d'aide, par exemple page-signaler");
                ok = false;
            }
        } else {
            problem(lineNo, "commande inconnue : " + cmd);
            ok = false;
        }
        if (!ok) continue;
        Item item;
        item.kind = Item::Kind::Gesture;
        item.filter = filter;
        item.line = lineNo;
        item.gesture = std::move(g);
        if (inBefore) { t.before.push_back(std::move(item)); continue; }
        if (t.source.empty()) { problem(lineNo, "geste avant la premi\xC3\xA8re \xC3\xA9tape"); continue; }
        t.source.back().items.push_back(std::move(item));
    }

    if (!haveHeader) problem(1, "il manque l'en-t\xC3\xAAte : = <id> | <titre>");
    if (t.source.empty()) problem(lineNo, "aucune \xC3\xA9tape");
    if (!t.defaultVariant.empty() &&
        std::find(t.variants.begin(), t.variants.end(), t.defaultVariant) == t.variants.end())
        problem(1, "@variante " + t.defaultVariant + " n'est pas dans @variantes");
    if (t.defaultVariant.empty() && !t.variants.empty()) t.defaultVariant = t.variants.front();
    for (const auto& [line, f] : filtersToCheck) {
        if (t.variants.empty()) { problem(line, "[...] sans @variantes"); continue; }
        if (f.names.empty()) problem(line, "[] vide");
        for (const auto& n : f.names)
            if (std::find(t.variants.begin(), t.variants.end(), n) == t.variants.end())
                problem(line, "variante inconnue : " + n);
    }
    return t;
}

CompiledTutorial Tutorial::compile(std::string_view variantIn) const {
    CompiledTutorial c;
    c.id = id;
    c.title = title;
    c.sandbox = sandbox;
    c.view = view;
    c.variants = variants;
    std::string variant(variantIn.empty() ? std::string_view(defaultVariant) : variantIn);
    if (!variants.empty() && std::find(variants.begin(), variants.end(), variant) == variants.end()) {
        c.problems.push_back({0, "variante inconnue : " + variant});
        variant = defaultVariant;
    }
    c.variant = variant;
    auto subst = [&](std::string s) {
        replaceAll(s, "{variante}", variant);
        return s;
    };

    // @avant : hors de la frise (duree 0 pour le lecteur), {variante} remplace partout.
    for (const auto& item : before) {
        if (!item.filter.accepts(variant)) continue;
        Gesture g = item.gesture;
        g.target = subst(g.target);
        g.target2 = subst(g.target2);
        g.text = subst(g.text);
        for (auto& a : g.args) a = subst(a);
        c.before.push_back(std::move(g));
    }

    int at = 0;
    for (const auto& src : source) {
        if (!src.filter.accepts(variant)) continue;
        TutorialStep step;
        step.title = subst(src.title);
        step.writtenNumber = src.writtenNumber;
        step.line = src.line;
        TutorialATry aTry;
        bool hasATry = false;
        std::string lastTarget;
        std::string firstTile;   // 1.11.2 : la premiere tuile des gestes (TutorialATry::show)
        for (const auto& item : src.items) {
            if (!item.filter.accepts(variant)) continue;
            switch (item.kind) {
            case Item::Kind::Bubble: {
                const auto text = subst(item.text);
                if (step.bubble.empty()) step.bubble = text;
                else step.bubble += (item.newParagraph ? "\n" : " ") + text;
                break;
            }
            case Item::Kind::Gesture: {
                Gesture g = item.gesture;
                g.target = subst(g.target);
                g.target2 = subst(g.target2);
                g.text = subst(g.text);
                if (g.kind == GestureKind::Type)
                    g.durationMs = 260 + msPerLetter * static_cast<int>(utf8Letters(g.text));
                else if (g.kind == GestureKind::Say)
                    g.durationMs = 1;   // la bulle change ; le temps de la lire suit (kSayGapMs)
                g.durationMs = std::max(g.durationMs, 1);
                if (!g.target.empty()) lastTarget = g.target2.empty() ? g.target : g.target2;
                // 1.11.2 : la premiere tuile de l'etape (la bibliotheque ou ses variantes), ce que l'A toi montre.
                if (firstTile.empty() && (startsWith(g.target, "biblio:") || startsWith(g.target, "variante:")))
                    firstTile = g.target;
                step.gestures.push_back(std::move(g));
                break;
            }
            case Item::Kind::ATry: hasATry = true; aTry.instruction = subst(item.text); break;
            case Item::Kind::Target: aTry.target = subst(item.text); break;
            case Item::Kind::Verify:
            case Item::Kind::Almost: {
                TutorialCheck ck = item.check;
                ck.path = subst(ck.path);
                ck.value = subst(ck.value);
                ck.reason = subst(ck.reason);
                (item.kind == Item::Kind::Verify ? aTry.checks : aTry.almost).push_back(std::move(ck));
                break;
            }
            case Item::Kind::Bravo: aTry.bravo = subst(item.text); break;
            case Item::Kind::OnOk: aTry.onOkVariantFrom = item.text; break;
            case Item::Kind::Show: aTry.show = subst(item.text); break;
            }
        }
        int sum = 0;
        for (auto& g : step.gestures) { g.startMs = sum; sum += g.durationMs + gestureGapMs(g.kind); }
        step.durationMs = std::max(sum + kStepReadMs, step.bubble.empty() ? 0 : readingTimeMs(step.bubble));
        step.durationMs = std::max(step.durationMs, 1);
        if (step.bubble.empty()) c.problems.push_back({src.line, "\xC3\xA9tape sans bulle : " + step.title});
        if (step.gestures.empty()) c.problems.push_back({src.line, "\xC3\xA9tape sans geste : " + step.title});
        if (hasATry) {
            if (aTry.target.empty()) aTry.target = lastTarget;
            if (aTry.show.empty()) aTry.show = firstTile;
            if (aTry.bravo.empty()) aTry.bravo = kDefaultBravo;
            if (aTry.checks.empty())
                c.problems.push_back({src.line, "\xC3\x80 toi sans @verifier : " + step.title});
            step.aTry = std::move(aTry);
        } else if (!aTry.checks.empty() || !aTry.almost.empty()) {
            c.problems.push_back({src.line, "@verifier sans @atoi : " + step.title});
        }
        step.startMs = at;
        at += step.durationMs;
        step.number = static_cast<int>(c.steps.size()) + 1;
        c.steps.push_back(std::move(step));
    }
    c.totalMs = at;
    if (c.steps.empty()) c.problems.push_back({0, "aucune \xC3\xA9tape pour la variante " + variant});
    return c;
}

bool evaluateCheck(const TutorialCheck& check, std::string_view actual) {
    const auto& op = check.op;
    if (op == "~") return actual.find(check.value) != std::string_view::npos;
    const auto a = parseNumber(actual);
    const auto b = parseNumber(check.value);
    int cmp = 0;
    if (a && b) cmp = *a < *b ? -1 : (*a > *b ? 1 : 0);
    else cmp = actual < std::string_view(check.value) ? -1 : (actual > std::string_view(check.value) ? 1 : 0);
    if (op == "=") return cmp == 0;
    if (op == "<>") return cmp != 0;
    if (op == "<") return cmp < 0;
    if (op == "<=") return cmp <= 0;
    if (op == ">") return cmp > 0;
    if (op == ">=") return cmp >= 0;
    return false;
}

CheckOutcome evaluateATry(const TutorialATry& aTry, const PathReader& read) {
    CheckOutcome out;
    bool all = !aTry.checks.empty();
    for (std::size_t i = 0; i < aTry.checks.size(); ++i) {
        const auto v = read ? read(aTry.checks[i].path) : std::nullopt;
        if (i == 0) out.value = v.value_or(std::string());
        if (!v || !evaluateCheck(aTry.checks[i], *v)) all = false;
    }
    for (const auto& a : aTry.almost) {
        const auto v = read ? read(a.path) : std::nullopt;
        if (v && evaluateCheck(a, *v)) {
            out.result = CheckOutcome::Result::Almost;
            out.message = a.reason;
            replaceAll(out.message, "{valeur}", *v);
            return out;
        }
    }
    if (all) {
        out.result = CheckOutcome::Result::Ok;
        out.message = aTry.bravo.empty() ? std::string(kDefaultBravo) : aTry.bravo;
        replaceAll(out.message, "{valeur}", out.value);
    }
    return out;
}

} // namespace help
