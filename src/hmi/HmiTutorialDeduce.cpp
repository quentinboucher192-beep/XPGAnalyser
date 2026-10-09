#include "hmi/HmiTutorialDeduce.hpp"

#include "help/Tutorial.hpp"   // tranche 12 : isPrepareCommand (le lecteur de T1 connait-il « action » ?)
#include "hmi/HmiExprGuide.hpp"
#include "hmi/HmiGuide.hpp"
#include "hmi/HmiModel.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <cstdlib>

namespace help {
namespace {

// Un mot du format .tuto entre guillemets : \" et \\ s'echappent (Tutorial.cpp, tutorialWords).
std::string q(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += (c == '\n' || c == '\r') ? ' ' : c;
    }
    return out + "\"";
}

// Une ligne de bulle ou d'instruction : une seule ligne.
// Tranche 9 (le lot de T1) : plus de « … » au milieu d'une phrase (249 bulles coupees) : la bulle
// de T1 passe a la ligne. Un texte plus long que max s'arrete a la fin d'une phrase (. ! ?
// suivi d'une espace) ; une seule longue phrase reste entiere, jamais coupee dans un mot.
// Tranche 18 (relecture comme le client : « Variable (REAL, INT...) », « une recette … », « Tout relacher … ») :
// trois points colles a un mot font « … » ; « … » se colle au mot qui le precede (pas apres une parenthese ouvrante).
// « x,y x,y ... », apres une espace, est le nom d'une case de l'inspecteur (Points de la Ligne) : il reste.
std::string typo(std::string s) {
    for (std::size_t p = 0; (p = s.find("...", p)) != std::string::npos;) {
        if (p > 0 && s[p - 1] != ' ' && s[p - 1] != '(') s.replace(p, 3, "\xE2\x80\xA6");
        else p += 3;
    }
    for (std::size_t p; (p = s.find("\xE2\x80\xA6.")) != std::string::npos;) s.erase(p + 3, 1);   // « Détacher…. »
    for (std::size_t p = 0; (p = s.find(" \xE2\x80\xA6", p)) != std::string::npos;) {   // apres une lettre ; « 0; … » (du code) reste
        const auto c = p > 0 ? static_cast<unsigned char>(s[p - 1]) : 0;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c & 0xC0) == 0x80) s.erase(p, 1);
        else ++p;
    }
    return s;
}
std::string line(std::string_view s, std::size_t max = 300) {
    std::string out;
    for (char c : typo(std::string(s))) out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    if (out.size() <= max) return out;
    std::size_t end = std::string::npos;
    for (std::size_t p = max / 3; p < max && p + 1 < out.size(); ++p) {
        const char c = out[p];
        if ((c == '.' || c == '!' || c == '?') && out[p + 1] == ' ' && out[p - 1] != '.') end = p;
    }
    return end == std::string::npos ? out : out.substr(0, end + 1);
}

// Tranche 16 (T1, les bulles de tutos-v5 lues comme le client : jusqu'a 757 caracteres, 7 lignes ; des
// phrases finies par « : ») : une bulle de prose garde 240 caracteres au plus (environ 2 lignes de la
// bulle de T1). Une phrase trop longue perd ses parentheses, puis s'arrete au dernier « ; » (un point),
// puis a la derniere virgule (« … ») ; une bulle ne finit jamais par « : ».
constexpr std::size_t kBubbleMax = 240;
std::size_t chars(std::string_view s) {   // des caracteres, pas des octets (UTF-8)
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}
std::size_t byteAt(std::string_view s, std::size_t nChars) {   // l'octet du caractere nChars
    std::size_t n = 0;
    for (std::size_t b = 0; b < s.size(); ++b)
        if ((static_cast<unsigned char>(s[b]) & 0xC0) != 0x80 && n++ == nChars) return b;
    return s.size();
}
std::string endSentence(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == ';' || s.back() == ',' || s.back() == ':')) s.pop_back();
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?' &&
        !(s.size() >= 3 && s.compare(s.size() - 3, 3, "\xE2\x80\xA6") == 0))
        s += '.';
    return s;
}
std::string shortSentence(std::string s, std::size_t max = kBubbleMax) {   // une seule phrase, trop longue
    if (chars(s) <= max) return s;
    for (std::size_t open; chars(s) > max && (open = s.find(" (")) != std::string::npos;) {   // ses parentheses
        std::size_t depth = 0, close = open + 1;
        for (; close < s.size(); ++close) {
            if (s[close] == '(') ++depth;
            else if (s[close] == ')' && --depth == 0) break;
        }
        if (close >= s.size()) break;
        s.erase(open, close - open + 1);
    }
    if (chars(s) <= max) return endSentence(s);
    const std::size_t limit = byteAt(s, max - 2);
    if (const auto semi = s.rfind(" ; ", limit); semi != std::string::npos && semi > limit / 3) return endSentence(s.substr(0, semi));
    auto cut = s.rfind(", ", limit);
    if (cut == std::string::npos || cut < limit / 3) cut = s.rfind(' ', limit);
    if (cut == std::string::npos || cut == 0) cut = limit;
    std::string head = s.substr(0, cut);
    while (!head.empty() && (head.back() == ' ' || head.back() == ',' || head.back() == ':' || head.back() == ';')) head.pop_back();
    return head + "\xE2\x80\xA6";   // tranche 18 : « … » colle au mot, comme en francais
}
// Les phrases d'un texte (une fin : . ! ? suivi d'une espace, pas « … »).
std::vector<std::string> sentencesOf(std::string_view text) {
    std::vector<std::string> out;
    std::string s;
    for (char c : typo(std::string(text))) s += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    std::size_t from = 0;
    // Tranche 18 (champs-expressions : « l'expression. » » coupe avant son « » ») : « . » suivi de « » » ou de « ) »
    // ne finit pas la phrase ; la fermeture reste avec elle.
    // Ni dans « » : « Vide : nombre. Accepte une expression… » est une seule citation.
    int quoted = s.compare(0, 2, "\xC2\xAB") == 0 ? 1 : 0;
    for (std::size_t p = 1; p + 1 < s.size(); ++p) {
        if (s.compare(p, 2, "\xC2\xAB") == 0) ++quoted;
        else if (s.compare(p, 2, "\xC2\xBB") == 0 && quoted > 0) --quoted;
        if (quoted == 0 && (s[p] == '.' || s[p] == '!' || s[p] == '?') && s[p + 1] == ' ' && s[p - 1] != '.' &&
            s.compare(p + 2, 2, "\xC2\xBB") != 0 && (p + 2 >= s.size() || s[p + 2] != ')')) {
            out.push_back(s.substr(from, p + 1 - from));
            from = p + 2;
        }
    }
    if (from < s.size()) out.push_back(s.substr(from));
    for (auto& o : out) {
        while (!o.empty() && o.front() == ' ') o.erase(0, 1);
        while (!o.empty() && o.back() == ' ') o.pop_back();
    }
    std::erase_if(out, [](const std::string& o) { return o.empty(); });
    return out;
}
// Tranche 17 (T1, point 6 : du code dans une bulle - une URL avec {numero}, « Alarmes · <groupe interne> (n) »,
// « xpg_analyzer --ihm <dossier du projet> ») : une bulle de prose n'a ni URL, ni <…> colle, ni requete (a={b}&c),
// ni option « --x ». Un <mot> seul (entre espaces ou guillemets) perd ses chevrons et un « (n) » tombe ; une parenthese
// qui contient du code tombe ; une phrase qui en a encore s'arrete avant lui, a la derniere « , », « : », « - »
// ou « ; » (40 caracteres au moins) ; sinon elle n'est pas dite (vide), s'il reste une autre phrase.
bool codeWord(std::string_view w) {
    if (w.find("://") != std::string_view::npos || w.find("::") != std::string_view::npos) return true;
    if (w.size() > 2 && w[0] == '-' && w[1] == '-' && w[2] != '-') return true;
    const auto lt = w.find('<');
    if (lt != std::string_view::npos && w.find('>', lt) != std::string_view::npos) return true;
    // un {champ} seul est un texte a trous, que l'objet Texte montre ({Niveau:0.0}) : il reste ; dans une requete
    // (to={numero}&text={message}), c'est du code
    const auto br = w.find('{');
    return br != std::string_view::npos && w.find('}', br) != std::string_view::npos && w.find_first_of("=&") != std::string_view::npos;
}
std::size_t firstCode(std::string_view s) {   // l'octet du premier mot de code ; npos : aucun
    for (std::size_t at = 0; at < s.size();) {
        const auto end = std::min(s.find(' ', at), s.size());
        if (codeWord(s.substr(at, end - at))) return at;
        at = end + 1;
    }
    return std::string_view::npos;
}
std::string withoutCode(std::string s) {
    for (std::size_t lt = s.find('<'); lt != std::string::npos; lt = s.find('<', lt + 1)) {   // <groupe interne> seul
        const auto gt = s.find('>', lt);
        if (gt == std::string::npos) break;
        const bool before = lt == 0 || s[lt - 1] == ' ' || (lt >= 2 && s.compare(lt - 2, 2, "\xC2\xA0") == 0) ||
                            (lt >= 2 && s.compare(lt - 2, 2, "\xC2\xAB") == 0);
        const bool after = gt + 1 >= s.size() || s[gt + 1] == ' ' ||
                           ((s[gt + 1] == '.' || s[gt + 1] == ',') && (gt + 2 >= s.size() || s[gt + 2] == ' '));
        const auto inner = s.substr(lt + 1, gt - lt - 1);
        // « < > » (les fleches d'un bouton) n'est pas un <mot> : il faut une lettre, sans espace au bord
        const bool word = !inner.empty() && inner.front() != ' ' && inner.back() != ' ' &&
                          std::any_of(inner.begin(), inner.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
        if (before && after && word && inner.find_first_of("<>{}/") == std::string::npos) s.replace(lt, gt - lt + 1, inner);
    }
    for (std::size_t at; (at = s.find(" (n)")) != std::string::npos;) s.erase(at, 4);
    for (std::size_t open = s.find(" ("); open != std::string::npos;) {   // une parenthese qui contient du code
        std::size_t depth = 0, close = open + 1;
        for (; close < s.size(); ++close) {
            if (s[close] == '(') ++depth;
            else if (s[close] == ')' && --depth == 0) break;
        }
        if (close >= s.size()) break;
        if (firstCode(std::string_view(s).substr(open + 2, close - open - 2)) != std::string_view::npos) { s.erase(open, close - open + 1); continue; }
        open = s.find(" (", open + 1);
    }
    // Du code suivi de son exemple entre parentheses (« s'appelle <Vue>.<Objet> (Vue_Cartes.Carte_Armoire_1) ») :
    // l'exemple, en gras, prend sa place.
    for (std::size_t code; (code = firstCode(s)) != std::string::npos;) {
        const auto end = std::min(s.find(' ', code), s.size());
        if (s.compare(end, 2, " (") != 0) break;
        const auto close = s.find(')', end + 2);
        if (close == std::string::npos || s.find('(', end + 2) < close) break;
        const std::string example = s.substr(end + 2, close - end - 2);
        if (example.empty() || firstCode(example) != std::string::npos) break;
        s.replace(code, close + 1 - code, "**" + example + "**");
    }
    const auto code = firstCode(s);
    if (code == std::string::npos) return s;
    // La coupe : a la derniere limite avant le code, hors parentheses (« , », « : », « ; », « et », « - » devant un
    // mot) ; un bout de moins de 12 caracteres depuis la limite d'avant (« ; En C++ : std::array… ») recule d'une limite.
    std::vector<std::size_t> cuts;
    int depth = 0;
    for (std::size_t p = 0; p < code; ++p) {
        if (s[p] == '(') ++depth;
        else if (s[p] == ')' && depth > 0) --depth;
        if (depth > 0) continue;
        for (const std::string_view sep : {", ", " : ", " ; ", " et ", " - "})
            if (s.compare(p, sep.size(), sep) == 0 && (sep != " - " || (p + 3 < s.size() && std::isalpha(static_cast<unsigned char>(s[p + 3])) != 0)))
                cuts.push_back(p);
    }
    while (cuts.size() > 1 && chars(s.substr(cuts[cuts.size() - 2], cuts.back() - cuts[cuts.size() - 2])) < 14) cuts.pop_back();
    if (cuts.empty() || chars(s.substr(0, cuts.back())) < 40) return {};
    return endSentence(s.substr(0, cuts.back()));
}
// Un texte en bulles de 240 caracteres au plus, par phrases entieres ; deux bulles au plus (la bulle de
// l'etape, puis un « dire ») : la suite se lit dans l'aide.
std::vector<std::string> bubbles(std::string_view text, std::size_t most = 2) {
    std::vector<std::string> out;
    auto all = sentencesOf(text);
    {   // tranche 17 : sans code ; une phrase qui n'est que du code se tait, s'il en reste une autre
        std::vector<std::string> clean;
        for (const auto& s : all)
            if (auto c = withoutCode(s); !c.empty()) clean.push_back(std::move(c));
        if (!clean.empty()) all = std::move(clean);
    }
    for (auto s : all) {
        s = shortSentence(std::move(s));
        if (!out.empty() && chars(out.back()) + 1 + chars(s) <= kBubbleMax) out.back() += " " + s;
        else if (out.size() < most) out.push_back(s);
        else break;
    }
    for (auto& o : out)
        if (!o.empty() && (o.back() == ':' || o.back() == ';' || o.back() == ',')) o = endSentence(o);
    return out;
}
std::string prose(std::string_view text) {
    const auto b = bubbles(text, 1);
    return b.empty() ? std::string() : b.front();
}

// Tranche 9 : un sujet du guide qui parle de la simulation (son lieu, son titre, son resume ou ses
// intertitres) : son « A toi » est F8. Les autres n'ont plus d'« A toi » colle (« regarde ce que
// fait Le journal d'audit ») : T1 l'a demande, un « A toi » doit venir du sujet.
bool aboutSimulation(const TopicInfo& i) {
    auto has = [](const std::string& s) {
        return s.find("simulation") != std::string::npos || s.find("Simulation") != std::string::npos ||
               s.find("F8") != std::string::npos;
    };
    if (has(i.place) || has(i.title) || has(i.summary)) return true;
    for (const auto& h : i.headings)
        if (has(h)) return true;
    for (const auto& text : i.headingTexts)
        if (has(text)) return true;
    return false;
}

void header(std::string& t, const TopicInfo& i, std::string_view bac) {
    t += "# Deduit (T3, 1.11) : " + std::string(topicKindName(i.kind)) + " " + i.key +
         ". Un fichier tools/tutoriels/*.tuto avec @sujet " + i.key + " le remplace.\n";
    t += "= " + deducedId(i.key) + " | " + line(i.title, 120) + "\n";
    t += "@sujet " + i.key + "\n";
    t += "@bac " + std::string(bac) + "\n";
}

// Le nom donne a l'instance : la fin de la cle, en identifiant, puis "_Tuto" comme la vue Vue_Tuto
// (objet-voyant-led -> Voyant_led_Tuto). Tranche 9 : plus de "Mon_" (« Mon_vanne », « Mon_pompe » :
// le genre est faux pour les noms feminins) ; "_Tuto" ne heurte aucun nom d'Armoire_Gaz.
std::string nameFor(const TopicInfo& i) {
    const auto dash = i.key.find('-');
    std::string n = (dash == std::string::npos ? i.key : i.key.substr(dash + 1)) + "_Tuto";
    for (auto& c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';
    if (!n.empty() && n[0] >= 'a' && n[0] <= 'z') n[0] = static_cast<char>(n[0] - 'a' + 'A');
    return n;
}

// Tranche 8 : la propriete « Valeur » d'un objet (le guide, @param), qui se lie par une
// expression. Le guide dit ce qu'elle attend : « Vrai : allume ; faux : eteint » (un BOOL), sinon
// une valeur montree (un nombre : un niveau, une mesure, une position).
// Tranche 12 (la 1.10.4 : la vanne 3 voies) : la valeur est le @param de cle "value" du guide,
// quel que soit son libelle (la vanne 3 voies l'appelle « Position », comme son inspecteur) ;
// sans sujet du guide (un sujet fait a la main), le libelle « Valeur ».
const std::pair<std::string, std::string>* valueProperty(const TopicInfo& i) {
    const auto label = hmi::tutotopics::valueLabel(i);
    for (const auto& p : i.properties)
        if (!label.empty() && p.first == label) return &p;
    return nullptr;
}
// L'expression d'exemple, sur la variable creee par @avant (V : ARRAY[0..3] OF T_VANNE, comme
// les tutoriels ecrits de T1) : =V[0].Pos > 50 pour un BOOL, =V[0].Pos pour un nombre.
std::string valueExpression(const std::string& guideText) {
    const bool boolean = guideText.rfind("Vrai", 0) == 0;
    return boolean ? "=V[0].Pos > 50" : "=V[0].Pos";
}

// Tranche 16 (T1 : « Ses couleurs suivent son etat : marche, arret, defaut » est faux pour une Ligne, un
// Conteneur, un Bandeau d'alarme) : seul un objet neuf qui a des couleurs d'etat (colorOn, colorOff,
// colorFault : makeObject) a l'etape « Ses etats ». Sans genre connu (un sujet fait a la main) : oui.
bool hasStateColors(const TopicInfo& i) {
    if (i.objectType.empty()) return true;
    for (const auto k : hmi::kPlaceableKinds) {
        if (hmi::kindKey(k) != i.objectType) continue;
        const hmi::Object fresh = hmi::makeObject(k, hmi::kNoId, {}, 0, 0, hmi::kNoId);
        return fresh.find("colorOn") || fresh.find("colorOff") || fresh.find("colorFault");
    }
    return true;
}

// Tranche 18 (relecture comme le client : « Valeurs (expressions a;b) : Une expression par part. », « Minimum : La
// valeur a gauche », une majuscule apres « : » dans 100 objets) : la description d'une propriete, apres son nom et
// « : », commence par une minuscule. Un sigle (CSV, IHM), un nom propre (Excel, Modbus) ou un endroit
// (Configuration › Recettes) garde sa majuscule.
std::string afterLabel(std::string s) {
    static constexpr std::string_view kKeep[] = {"Excel", "Modbus", "Windows", "Linux", "Configuration", "Simulation", "Param\xC3\xA8tres"};
    for (const auto k : kKeep)
        if (s.rfind(k, 0) == 0 && (s.size() == k.size() || s[k.size()] == ' ' || s[k.size()] == ',')) return s;
    if (s.size() > 1 && s[0] >= 'A' && s[0] <= 'Z' && ((s[1] >= 'a' && s[1] <= 'z') || s[1] == '\'' || s[1] == ' '))
        s[0] = static_cast<char>(s[0] - 'A' + 'a');
    else if (s.size() > 2 && static_cast<unsigned char>(s[0]) == 0xC3 && (static_cast<unsigned char>(s[1]) == 0x80 || static_cast<unsigned char>(s[1]) == 0x89))
        s[1] = static_cast<char>(static_cast<unsigned char>(s[1]) + 0x20);   // À É -> à é
    return s;
}

std::string objectText(const TopicInfo& i) {
    std::string t;
    header(t, i, "Armoire_Gaz | Vue_Tuto");
    if (!i.variants.empty()) {
        t += "@variantes ";
        for (std::size_t n = 0; n < i.variants.size(); ++n) t += (n ? ", " : "") + i.variants[n];
        t += "\n";
    }
    const auto* value = valueProperty(i);
    t += "@avant\n";
    if (value) {   // Tranche 8 : la variable que l'expression de la Valeur lit
        t += "type T_VANNE \"Pos : REAL; Defaut : BOOL; Bouge : BOOL; Cmd : BOOL\"\n";
        t += "variable V \"ARRAY[0..3] OF T_VANNE\"\n";
    }
    t += "vue Vue_Tuto\n\n";
    const std::string tile = "biblio:" + i.name;
    const bool variants = !i.variants.empty();
    t += "== 1 | " + std::string(variants ? "Choisis la variante et pose-la" : "Pose l'objet") + "\n";
    {   // tranche 16 : la bulle entiere tient en 240 caracteres (la premiere phrase du resume, raccourcie s'il le faut)
        const std::string head = "Dans la biblioth\xC3\xA8que, la tuile **" + i.name + "** : glisse-la dans la vue. ";
        const auto first = sentencesOf(i.summary);
        const std::size_t room = kBubbleMax > chars(head) + 20 ? kBubbleMax - chars(head) : 20;
        std::string rest = first.empty() ? std::string() : shortSentence(first.front(), room);
        if (first.size() > 1 && chars(rest) + 1 + chars(first[1]) <= room) rest += " " + first[1];
        t += ": " + head + rest + "\n";
    }
    t += "clic " + q(tile) + "\n";
    if (variants) t += "encadrer variantes\nclic \"variante:{variante}\"\n";
    t += "glisser " + q(variants ? "variante:{variante}" : tile) + " vue:400,262\n";
    t += "dire " + q("Pos\xC3\xA9 dans la vue : l'inspecteur, \xC3\xA0 droite, montre ses propri\xC3\xA9t\xC3\xA9s.") + "\n";
    t += "@atoi Glisse " + i.name + " de la biblioth\xC3\xA8que dans la vue.\n";
    t += "@cible vue:400,262\n";
    t += "@verifier objets[" + i.name + "] >= 1\n\n";

    t += "== 2 | Nomme l'objet\n";
    t += ": Le **Nom** sert aux scripts, aux alarmes et aux expressions.\n";
    t += "clic propriete:Nom\ntexte propriete:Nom " + q(nameFor(i)) + "\n";
    t += "touche Return\nencadrer propriete:Nom\n\n";

    std::size_t shown = 0;
    if (value) {
        // Tranche 8 : lier sa valeur par une expression (la table « Etapes deduites » de la maquette : lier).
        const std::string expr = valueExpression(value->second);
        const std::string cell = q("propriete:" + value->first);   // "propriete:Valeur" ("propriete:Position" : 3 voies)
        t += "== 3 | Lie sa valeur par une expression\n";
        t += ": **" + value->first + "** : " + afterLabel(line(value->second, 160)) + " Dans la case, **=** puis l'expression.\n";
        t += "clic " + cell + "\n";
        t += "texte " + cell + " " + q(expr) + "\n";
        t += "touche Return\nencadrer " + cell + "\n";
        // Tranche 9 : plus de « (F1 dans la case) » : F1 ouvre encore l'aide de l'objet ; la page des
        // expressions par la case viendra avec le centre d'aide (T2, exprguide::forWant).
        t += "dire " + q("La valeur suit " + expr.substr(1) + " ; la page des expressions, dans l'aide, dit ce que la case attend.") + "\n";
        shown = 1;
        for (const auto& [label, text] : i.properties) {   // et une autre propriete principale
            if (label == value->first) continue;
            t += "encadrer " + q("propriete:" + label) + "\n";
            t += "dire " + q(prose(label + " : " + afterLabel(text))) + "\n";
            break;
        }
    } else {
    t += "== 3 | Ses propri\xC3\xA9t\xC3\xA9s principales\n";
    // Tranche 16 (T1 : « Les proprietes qui comptent le plus pour Conteneur. » ne disait pas lesquelles) : la bulle les nomme.
    if (i.properties.empty()) t += ": L'inspecteur, \xC3\xA0 droite, montre ses propri\xC3\xA9t\xC3\xA9s.\n";
    else if (i.properties.size() == 1) t += ": Sa propri\xC3\xA9t\xC3\xA9 principale : **" + i.properties[0].first + "**.\n";
    else t += ": Ses deux propri\xC3\xA9t\xC3\xA9s principales : **" + i.properties[0].first + "** et **" + i.properties[1].first + "**.\n";
    for (const auto& [label, text] : i.properties) {
        if (shown == 2) break;
        t += "encadrer " + q("propriete:" + label) + "\n";
        t += "dire " + q(prose(label + " : " + afterLabel(text))) + "\n";
        ++shown;
    }
    }
    if (shown == 0) t += "dire " + q("L'inspecteur r\xC3\xA8gle tout : un clic sur une case, ou = pour une expression.") + "\n";
    // L'etape 4 garde son geste pour tous (encadrer apercu-etats : l'objet dans la vue, UiDriver) : seuls les
    // mots changent pour un objet sans couleurs d'etat.
    if (hasStateColors(i)) {
    t += "\n== 4 | Ses \xC3\xA9tats\n";
    t += ": Ses couleurs suivent son \xC3\xA9tat : marche, arr\xC3\xAAt, d\xC3\xA9" "faut.\n";
    t += "encadrer apercu-etats\n";
    t += "dire " + q("Chaque \xC3\xA9tat a sa couleur ; un d\xC3\xA9" "faut fait clignoter l'objet et arme son alarme, s'il en a une.") + "\n\n";
    } else {
    t += "\n== 4 | Dans la vue\n";
    t += ": Le voici dans la vue, tel que l'op\xC3\xA9rateur le verra : chaque r\xC3\xA9glage de l'inspecteur s'y voit aussit\xC3\xB4t.\n";
    t += "encadrer apercu-etats\n";
    t += "dire " + q("Une expression dans une de ses cases le fait suivre les variables, en simulation comme sur le poste.") + "\n\n";
    }
    t += "== 5 | Essaie en simulation\n";
    // Tranche 18 (relecture comme le client : « l'objet suit ses variables en direct » est faux pour un Bouton d'export,
    // une Gestion de recettes, des Statistiques d'alarmes ; « l'objet suit » deux fois de suite) : la bulle dit le geste,
    // le « dire » ce qui se voit - la Valeur pour un objet qui en a une, sinon l'objet tel que l'operateur s'en sert.
    t += ": **F8** d\xC3\xA9marre l'IHM en simulation.\n";
    t += "touche F8\nattendre 1.5s\n";
    t += "dire " + q(value ? "La simulation tourne : change V[0].Pos, l'objet suit sa " + value->first + "."
                           : std::string("La simulation tourne : l'objet marche comme sur le poste d'exploitation.")) + "\n";
    return t;
}

// Tranche 17 (T1, point 9 : « Ouvre l'endroit » ne dit pas ce qu'on montre, et l'etape n'ouvre parfois rien) :
// le titre de l'etape 1 d'un sujet du guide dit l'endroit montre - « Dans l'arbre : IHM › Configuration ›
// Alarmes » ; sans endroit, « En bref ».
std::string placeTitle(const std::string& place) {
    constexpr std::string_view kTree = "arbre:";
    if (place.empty()) return "En bref";
    if (place == "barre:?") return "Dans la barre du haut : le \xC2\xAB ? \xC2\xBB";   // tranche 22 (R111-12)
    if (place.rfind(kTree, 0) != 0) return "L'endroit";
    std::string out;
    for (const char c : place.substr(kTree.size())) out += c == '/' ? std::string(" \xE2\x80\xBA ") : std::string(1, c);
    return "Dans l'arbre : " + out;
}
// Tranche 17 (T1, point 8 : « L'objet est choisi dans sa vue… » sous « Un clic sur une alarme » : la bulle decrit
// un geste que personne ne fait) : sous un intertitre qui nomme un geste, la bulle le dit comme une condition -
// « Si tu cliques sur une alarme, l'objet est choisi… ». Vide : l'intertitre ne nomme pas de geste.
// Seulement quand la bulle dit l'effet du geste comme s'il avait lieu (« L'objet se deplie », « L'objet est
// choisi » : se, s', est ou sont dans ses cinq premiers mots) ; une bulle qui explique comment faire
// (« La bibliotheque a deux onglets », « choisis-les… ») reste telle quelle. Les intertitres : « Un clic sur X »,
// « Un double clic sur X », « Un clic droit sur X », ou un seul verbe (« Deplier » -> « Si tu le deplies »).
bool describesResult(std::string_view text) {
    std::size_t end = 0;
    for (int w = 0; w < 5 && end != std::string_view::npos; ++w) end = text.find(' ', end + 1);
    const std::string head(text.substr(0, end == std::string_view::npos ? text.size() : end + 1));
    for (const std::string_view v : {" se ", " s'", " est ", " sont "})
        if (head.find(v) != std::string::npos) return true;
    return false;
}
std::string gestureLead(const std::string& title, std::string_view text) {
    if (!describesResult(text)) return {};
    static constexpr std::pair<std::string_view, std::string_view> kLeads[] = {
        {"Un double clic sur ", "Si tu double-cliques sur "}, {"Un clic droit sur ", "Si tu fais un clic droit sur "},
        {"Un clic sur ", "Si tu cliques sur "}};
    for (const auto& [from, to] : kLeads)
        if (title.rfind(from, 0) == 0) return std::string(to) + title.substr(from.size());
    static constexpr std::pair<std::string_view, std::string_view> kVerbs[] = {
        {"D\xC3\xA9plier", "Si tu le d\xC3\xA9plies"}, {"Replier", "Si tu le replies"}, {"Glisser", "Si tu le glisses"},
        {"Double-cliquer", "Si tu double-cliques"}, {"Cliquer", "Si tu cliques"}};
    for (const auto& [verb, lead] : kVerbs)
        if (title == verb) return std::string(lead);
    return {};
}
std::string lowerStart(std::string s) {   // « L'objet… » -> « l'objet… » ; un sigle (IHM) garde sa casse
    if (s.size() > 1 && s[0] >= 'A' && s[0] <= 'Z' && ((s[1] >= 'a' && s[1] <= 'z') || s[1] == '\'' || s[1] == ' '))
        s[0] = static_cast<char>(s[0] - 'A' + 'a');
    else if (s.size() > 2 && static_cast<unsigned char>(s[0]) == 0xC3 && (static_cast<unsigned char>(s[1]) == 0x80 || static_cast<unsigned char>(s[1]) == 0x89))
        s[1] = static_cast<char>(static_cast<unsigned char>(s[1]) + 0x20);   // À É -> à é
    return s;
}

// Tranche 18 (relecture comme le client : 367 etapes du guide ou le « dire » redit mot pour mot la bulle de l'etape,
// et la bulle reste deux fois son temps de lecture) : un paragraphe court de plusieurs phrases donne sa 1re phrase a
// la bulle, la suite au « dire ». Les gestes ne changent pas. Une seule phrase : la bulle et le « dire » la gardent,
// et guideText n'ecrit pas ce « dire » (tranche 19, decision 27 du chef).
std::pair<std::string, std::string> stepAndSay(const std::vector<std::string>& parts, std::string_view fallback) {
    if (parts.empty()) return {line(fallback), line(fallback)};
    if (parts.size() > 1) return {parts.front(), parts.back()};
    // Une fin de phrase dans « » ou dans une parenthese (« le « ? » en haut a droite ») ne coupe pas : la bulle va
    // jusqu'a la premiere phrase dont les guillemets et les parentheses sont fermes.
    const auto s = sentencesOf(parts.front());
    const auto open = [](const std::string& x) {
        int q = 0, p = 0;
        for (std::size_t b = 0; b < x.size(); ++b) {
            if (x[b] == '(') ++p;
            else if (x[b] == ')') --p;
            else if (x.compare(b, 2, "\xC2\xAB") == 0) ++q;
            else if (x.compare(b, 2, "\xC2\xBB") == 0) --q;
        }
        return q > 0 || p > 0;
    };
    std::string head;
    std::size_t n = 0;
    const auto starts = [](const std::string& x) {   // une vraie phrase commence par une majuscule, un chiffre ou « ; « a. propose » non
        const unsigned char c = x.empty() ? 0 : static_cast<unsigned char>(x[0]);
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == 0xC3 || x.rfind("\xC2\xAB", 0) == 0 || x.rfind("**", 0) == 0;
    };
    while (n < s.size()) {
        head += (head.empty() ? "" : " ") + s[n++];
        if (!open(head) && (n >= s.size() || starts(s[n]))) break;
    }
    if (n >= s.size()) return {parts.front(), parts.front()};
    std::string rest;
    for (std::size_t k = n; k < s.size(); ++k) rest += (k > n ? " " : "") + s[k];
    return {head, rest};
}

// 1.11.2 (T1, R1112-5 de R111 : api-compiler et api-renommer avaient UNE etape de 3 s, « Dans l'arbre : IHM », la bulle
// « Didacticiel : « … » » ; Compiler n'etait ni montre ni nomme : « le tutoriel ne fait rien ») : ces pages de l'aide
// generale (HelpDocument.cpp) n'ont que des puces et le renvoi au parcours ; leur tutoriel montre l'endroit et le geste.
// L'etape 1 garde l'endroit (kTopicPlaces) avec sa propre bulle ; les etapes suivantes sont ecrites ici, le renvoi au
// parcours du didacticiel en dernier « dire ». Les bulles reprennent les puces de la page.
struct PageTourStep { std::string_view title, bubble, gestures; };
struct PageTour { std::string_view key, opening; PageTourStep steps[2]; };
const PageTour* pageTour(std::string_view key) {
    static const PageTour kTours[] = {
        {"api-compiler",
         "Compiler et G\xC3\xA9n\xC3\xA9rer sont deux boutons de la rang\xC3\xA9" "e d'outils sous IHM, dans l'arbre du projet.",
         {{"IHM \xE2\x80\xBA Compiler",
           "IHM \xE2\x80\xBA Compiler relit les expressions : la liste des erreurs dit o\xC3\xB9 (vue \xE2\x80\xBA objet \xE2\x80\xBA propri\xC3\xA9t\xC3\xA9), quoi, "
           "et propose le bon nom ; Aller \xC3\xA0, Remplacer, Tout remplacer.",
           "clic \"arbre:IHM/Compiler\"\nattendre 1.5s\nencadrer \"arbre:IHM/Compiler\"\nattendre 1s\n"},
          {"IHM \xE2\x80\xBA G\xC3\xA9n\xC3\xA9rer",
           "IHM \xE2\x80\xBA G\xC3\xA9n\xC3\xA9rer refuse tant qu'il reste des erreurs, et dit lesquelles : rien de cass\xC3\xA9 ne part sur le pupitre.",
           "encadrer \"arbre:IHM/G\xC3\xA9n\xC3\xA9rer\"\nattendre 1s\n"
           "dire \"Pas \xC3\xA0 pas : le parcours \xC2\xAB Expressions : Compiler et G\xC3\xA9n\xC3\xA9rer \xC2\xBB du didacticiel de l'API.\"\n"}}},
        {"api-renommer",
         "Renommer partout : F2, le clic droit, le bouton Renommer, la case Nom et le double-clic ouvrent le dialogue Renommer.",
         {{"Le dialogue Renommer",
           "Le bouton Renommer ouvre le dialogue : le verdict du nom \xC3\xA0 chaque lettre, et o\xC3\xB9 \xC3\xA7" "a change, en onglets (Tout, API, IHM, "
           "Tables), avant et apr\xC3\xA8s.",
           "clic \"outil:Renommer\"\nattendre 1.5s\n"},
          {"Confirmer ou Annuler",
           "Confirmer (ou Entr\xC3\xA9" "e) fait tout en une commande : un seul Ctrl+Z pour les deux c\xC3\xB4t\xC3\xA9s ; Annuler (\xC3\x89" "chap) ne touche \xC3\xA0 rien.",
           "touche Escape\nattendre 1s\n"
           "dire \"Pas \xC3\xA0 pas : le parcours \xC2\xAB Renommer partout \xC2\xBB du didacticiel de l'API.\"\n"}}},
    };
    for (const auto& tour : kTours)
        if (tour.key == key) return &tour;
    return nullptr;
}
// 1.11.2 (T1, R1112-3 de R111 : editeur, a 8 s, « L'ecran de l'editeur » decrivait l'explorateur d'objets et les calques
// pendant que l'ecran montrait la liste IHM › Vues) : un sujet dont l'endroit est une liste qui mene a un editeur ouvre
// une vue du bac a sa premiere etape d'intertitre (la ligne Vue_Commandes, puis l'outil Ouvrir de la liste, vus par
// la sonde de la tranche 41 sur l'appli de 8ced3b2).
std::string_view firstHeadingGestures(std::string_view key) {
    if (key == "editeur") return "clic \"ligne:Vue_Commandes\"\nclic \"outil:Ouvrir\"\nattendre 1.5s\n";
    return {};
}
// 1.11.2 (T1, R1112-3 de R111 : symboles, a 20 s, « Le symbole » parlait des proprietes de la vue du symbole pendant que
// l'ecran montrait la liste IHM › Symboles, aucun symbole ouvert) : l'intertitre qui decrit l'editeur d'un element de la
// liste ouvre cet element a son etape : la ligne Carte_Armoire (un symbole d'Armoire_Gaz), puis l'outil Ouvrir de la
// liste (sonde de la tranche 43 sur l'appli de 597b2a3 : onglet.courant ~ Carte_Armoire, faux avant, juste apres).
std::string_view headingGestures(std::string_view key, std::string_view heading) {
    if (key == "symboles" && heading == "Le symbole") return "clic \"ligne:Carte_Armoire\"\nclic \"outil:Ouvrir\"\nattendre 1.5s\n";
    return {};
}

std::string guideText(const TopicInfo& topic) {
    // Tranche 20 (R111-1) : une page de l'aide generale (sans @lieux : HelpCenterScreen ne remplit pas `place`) prend
    // l'endroit de son sujet (kTopicPlaces) : l'etape 1 ouvre l'onglet et l'encadre, comme pour un sujet du guide.
    TopicInfo i = topic;
    const PageTour* tour = pageTour(i.key);   // 1.11.2 (R1112-5)
    if (tour) i.summary = std::string(tour->opening);
    // Tranche 22 (R111-12) : un sujet de Demarrer qui parle de l'aide vit au « ? » de la barre du haut, quels que soient
    // ses @lieux (le centre peut les avoir deja changes en ligne de l'arbre).
    if (auto bar = hmi::tutotopics::barPlaceForTopic(i.key); !bar.empty()) i.place = std::move(bar);
    if (i.place.empty()) i.place = hmi::tutotopics::treePathForTopic(i.key);
    const bool helpMenu = i.place == "barre:?";
    std::string t;
    header(t, i, "Armoire_Gaz");
    t += "\n== 1 | " + placeTitle(i.place) + "\n";
    const std::string& opening = i.summary.empty() ? i.title : i.summary;
    const auto [openSay, openDire] = stepAndSay(bubbles(opening, 1), opening);
    t += ": " + openSay + "\n";
    if (!i.place.empty()) {
        // Tranche 8 : une ligne sous un dossier replie au depart (IHM > Configuration > Alarmes) :
        // un double clic deplie d'abord son dossier, comme les tutoriels ecrits de T1.
        if (const auto parent = hmi::tutotopics::foldedParent(i.place); !parent.empty())
            t += "clic " + q(parent) + " double\n";
        // Tranche 17 (decision 13 du chef : le clic sur Simulation > IHM demarrait deja l'IHM, l'« A toi » F8
        // etait vrai avant le geste) : une ligne qui demarre la simulation s'encadre, sans clic.
        // Tranche 22 (R111-12) : le « ? » s'encadre sans clic : son menu ouvert resterait sous les bulles, et F1, le geste
        // de l'« A toi », ouvre le centre lui-meme.
        if (!hmi::tutotopics::startsSimulation(i.place) && !helpMenu) t += "clic " + q(i.place) + "\n";
        t += "encadrer " + q(i.place) + "\n";
    }
    // Tranche 19 (decision 27 du chef) : une bulle d'une seule phrase n'est plus redite par un « dire » (la bulle restait
    // deux fois son temps de lecture). L'etape garde un geste : le clic et l'encadre de l'endroit, sinon une attente.
    if (openDire != openSay) t += "dire " + q(openDire) + "\n";
    else if (i.place.empty()) t += "attendre 1s\n";
    // 5 etapes au plus : l'endroit, les intertitres, et « A toi » s'il y en a un (la simulation ; tranche 22 : le centre
    // d'aide pour un sujet du « ? », qui n'a plus l'« A toi » F8 commun, sans lien avec lui).
    const bool simulation = !helpMenu && aboutSimulation(i);
    // 1.11.2 (T1, decision 141 : « les plus vus » = les sujets ou mene F1 et le chapitre Demarrer) : un de ces sujets, sans
    // autre « A toi » (ni F8 ni F1), fait choisir sa ligne de l'arbre a l'etape 1, celle qu'elle clique et encadre. La
    // condition arbre.choisi(<chemin>) lit la ligne choisie de l'explorateur comme la cible arbre: la cherche (le debut de
    // chaque libelle : help/TutorialScreen.hpp) ; fausse avant le clic. Declaree sure a la tranche 37, apres le
    // verificateur de T1 (les plus vus qui ont cet « A toi », justes).
    if (!simulation && !helpMenu && i.place.rfind("arbre:", 0) == 0 && hmi::tutotopics::mostViewedTopic(i.key)
        && hmi::tutotopics::conditionDeclaredSafe("arbre.choisi")) {
        const std::string path = i.place.substr(6);
        const auto slash = path.rfind('/');
        const std::string label = slash == std::string::npos ? path : path.substr(slash + 1);
        std::string shown;   // « IHM › Configuration › Alarmes »
        for (const char c : path) shown += c == '/' ? std::string(" \xE2\x80\xBA ") : std::string(1, c);
        t += slash == std::string::npos ? "@atoi Choisis " + label + " dans l'arbre du projet.\n"
                                        : "@atoi Ouvre " + label + " dans l'arbre du projet (" + shown + ").\n";
        t += "@cible " + q(i.place) + "\n";
        t += "@verifier arbre.choisi(" + path + ") = oui\n";
    }
    if (tour) {   // 1.11.2 (R1112-5) : l'endroit, puis le geste (Compiler, Renommer), le renvoi au parcours en dernier
        int at = 1;
        for (const auto& s : tour->steps) {
            t += "\n== " + std::to_string(++at) + " | " + std::string(s.title) + "\n";
            t += ": " + std::string(s.bubble) + "\n";
            t += s.gestures;
        }
        return t;
    }
    // Tranche 21 (R111-11, decision 45 : les tutoriels de la simulation montraient le tableau de bord de l'API pendant
    // leurs bulles, et la chose decrite n'apparaissait qu'a l'« A toi ») : un sujet qui vit dans Simulation > IHM (son
    // etape 1 encadre la ligne sans la cliquer, decision 13) demarre l'IHM a l'etape 2, F8 restant son « A toi » ; ses
    // intertitres se lisent ensuite sur la simulation qui tourne (le cadre, la pastille, le bandeau, la barre).
    const bool simFirst = simulation && hmi::tutotopics::startsSimulation(i.place);
    const auto simStep = [&t, simFirst](int at) {
        t += "\n== " + std::to_string(at) + " | \xC3\x80 toi : " + (simFirst ? "d\xC3\xA9marre l'IHM en simulation" : "essaie en simulation") + "\n";
        t += simFirst ? ": **F8** d\xC3\xA9marre l'IHM en simulation : la suite se lit sur elle.\n"
                      : ": Ce que tu viens de voir se joue dans l'IHM : **F8** la d\xC3\xA9marre.\n";
        t += "touche F8\nattendre 1.5s\n";
        // Tranche 18 (relecture comme le client : « ce que dit ce sujet », le mot du centre d'aide) : la bulle parle de ce qu'on a lu.
        t += "dire " + q(simFirst ? "L'IHM tourne : la suite se voit en direct." : "L'IHM tourne : tout cela se voit maintenant en direct.") + "\n";
        t += "@atoi D\xC3\xA9marre l'IHM en simulation (F8).\n";
        t += "@cible ecran:800,450\n";
        t += "@verifier simulation.ihm = marche\n";
    };
    // Tranche 23 (decision 62 : apres F1, centre.ouvert lit encore « non » au verificateur de T1) : l'« A toi » du « ? »
    // attend que T1 declare sa condition sure ; d'ici la, le sujet encadre le « ? » et lit ses intertitres, sans « A toi ».
    // Tranche 24 (decision 67) : elle l'est ; le texte redevient celui de la tranche 22 (39f40e7), F1 compris.
    const bool helpATry = helpMenu && hmi::tutotopics::conditionDeclaredSafe("centre.ouvert = oui");
    // 1.11.2 (R1112-3 : avec le bac, menu-aide et didacticiel decrivaient a 8 s la recherche du centre et la page du
    // didacticiel pendant que l'ecran montrait le bac ; le centre ne s'ouvrait qu'a l'« A toi », en dernier) : comme
    // la simulation (R111-11), l'« A toi » F1 vient a l'etape 2 ; l'etape 3 ouvre dans le centre la page du sujet
    // (aide <cle>), que les bulles des intertitres decrivent. « Essayer » mene a l'etape 2 (R1112-4).
    const auto helpStep = [&t, &i](int at) {
        t += "\n== " + std::to_string(at) + " | \xC3\x80 toi : ouvre le centre d'aide\n";
        t += ": Le centre d'aide s'ouvre par **F1**, ou par le menu du \xC2\xAB ? \xC2\xBB de la barre du haut : la suite se lit dans le centre.\n";
        t += "touche F1\nattendre 1s\n";
        t += "@atoi Ouvre le centre d'aide (F1).\n";
        t += "@cible " + q(i.place) + "\n";
        t += "@verifier centre.ouvert = oui\n";
    };
    const int lastHeading = simulation && !simFirst ? 4 : 5;
    int n = 1;
    if (simFirst) simStep(++n);
    if (helpATry) helpStep(++n);
    bool pageShown = !helpATry;   // la page du sujet, ouverte dans le centre par la premiere etape d'apres
    std::string_view opensEditor = firstHeadingGestures(i.key);   // 1.11.2 (R1112-3) : editeur ouvre une vue
    for (std::size_t h = 0; h < i.headings.size(); ++h) {
        if (n == lastHeading) break;
        const std::string& title = i.headings[h];
        const std::string text = h < i.headingTexts.size() ? i.headingTexts[h] : std::string();
        if (text.empty()) continue;   // tranche 9 : un intertitre sans corps n'est pas une etape (fromGuide les ecarte deja)
        ++n;
        t += "\n== " + std::to_string(n) + " | " + line(title, 80) + "\n";
        // Tranche 16 : 240 caracteres par bulle ; la suite d'un long paragraphe dans un second « dire ».
        // Les gestes ne changent pas (un « dire », une attente) : la bulle de l'etape dit la 1re partie, le « dire » la 2e.
        // Tranche 17 : sous un intertitre qui nomme un geste, la bulle le dit comme une condition (gestureLead).
        const std::string lead = gestureLead(title, text);
        const auto [stepSay, stepDire] = stepAndSay(bubbles(lead.empty() ? text : lead + ", " + lowerStart(text)), text);
        t += ": " + stepSay + "\n";
        if (!pageShown) { t += "aide " + i.key + "\n"; pageShown = true; }   // 1.11.2 (R1112-3)
        if (!opensEditor.empty()) { t += opensEditor; opensEditor = {}; }     // 1.11.2 (R1112-3) : editeur
        t += headingGestures(i.key, title);                                   // 1.11.2 (R1112-3) : symboles
        if (stepDire != stepSay) t += "dire " + q(stepDire) + "\n";   // tranche 19 : pas de « dire » qui redit la bulle
        t += "attendre 1s\n";
    }
    if (helpATry) {
        // Tranche 22 (R111-12) : l'« A toi » du sujet, la condition de T1 (centre.ouvert : un ecran du centre dans la pile
        // des menus). Pas deja vrai : l'etape 1 encadre le « ? » sans l'ouvrir, et la remise en place rouvre le bac.
        // Tranche 24 (decision 67) : R111 a vu ces « A toi » F1 marcher ; le geste reste F1. 1.11.2 (R1112-3) : il est
        // l'etape 2 (helpStep, plus haut) ; un sujet sans intertitre montre sa page dans une derniere etape.
        if (!pageShown && n < 5) {
            t += "\n== " + std::to_string(++n) + " | " + line(i.title, 80) + "\n";
            t += ": " + line(opening, 240) + "\n";
            t += "aide " + i.key + "\nattendre 1s\n";
        }
        return t;
    }
    if (!simulation || simFirst) return t;
    simStep(++n);
    return t;
}

// Tranche 13 : l'exemple que l'« A toi » d'un type d'expression fait ecrire. Pas le premier, que la
// page met dans le champ quand on choisit le type (HmiExprPageView::show) : le plus court des autres
// (un vrai geste, mais court : =16#FF, ='#2A7FBF80', =Mode) ; a longueur egale, le premier venu.
// Le type est celui de la table (hmi::exprguide::find accepte "expr-<cle>") ; nullptr : aucun autre.
const hmi::exprguide::Example* otherExample(const TopicInfo& i) {
    const auto* type = hmi::exprguide::find(i.key);
    if (!type || type->examples.size() < 2) return nullptr;
    const auto first = type->examples.front().source;
    const hmi::exprguide::Example* best = nullptr;
    for (std::size_t n = 1; n < type->examples.size(); ++n) {
        const auto& e = type->examples[n];
        if (e.source.empty() || e.source == first || e.source == i.example) continue;
        if (!best || e.source.size() < best->source.size()) best = &e;
    }
    return best;
}

// Tranche 17 (T1, point 6 : « Tableau[indice].Membre (* l'indice va de la borne basse a la borne haute *) V[0].Pos
// V[SYS.UserLevel].Defaut », du code brut dans la bulle) : le cadre de la table (ses lignes, separees par \n) en
// phrases. Ses formes (separees par deux espaces ou plus) en gras, puis son commentaire (* ... *) apres « : » ;
// une ligne sans commentaire apres la premiere est un exemple : « Par exemple **V[0].Pos**. »
std::vector<std::string> syntaxSentences(std::string_view syntax) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < syntax.size()) {
        const auto end = std::min(syntax.find('\n', at), syntax.size());
        const std::string raw(syntax.substr(at, end - at));
        at = end + 1;
        std::string code = raw, note;
        if (const auto open = raw.find("(*"); open != std::string::npos) {
            const auto close = raw.find("*)", open + 2);
            note = raw.substr(open + 2, (close == std::string::npos ? raw.size() : close) - open - 2);
            code = raw.substr(0, open) + (close == std::string::npos ? std::string() : "  " + raw.substr(close + 2));
            while (!note.empty() && note.front() == ' ') note.erase(0, 1);
            while (!note.empty() && note.back() == ' ') note.pop_back();
        }
        std::vector<std::string> items;
        for (std::size_t p = 0; p < code.size();) {
            const auto gap = code.find("  ", p);
            std::string it = code.substr(p, (gap == std::string::npos ? code.size() : gap) - p);
            while (!it.empty() && it.front() == ' ') it.erase(0, 1);
            while (!it.empty() && it.back() == ' ') it.pop_back();
            if (!it.empty()) items.push_back(std::move(it));
            if (gap == std::string::npos) break;
            p = gap + 2;
        }
        if (items.empty()) continue;
        std::string s;
        for (std::size_t n = 0; n < items.size(); ++n) s += (n ? ", **" : "**") + items[n] + "**";
        if (!note.empty()) s += " : " + note;
        else if (!out.empty()) s = "Par exemple " + s;
        out.push_back(endSentence(s));
    }
    return out;
}

// Tranche 18 (relecture comme le client : « une comparaison », « un indice calcule : V[2] », un bout de phrase sans
// majuscule ni point sous l'exemple ecrit) : l'exemple en gras, puis ce qu'il montre - « **=Pression > 3.5** : une
// comparaison. », « **=V[0].Pos / 100.0** : une fraction, ici 0.42. »
std::string exampleSaid(const std::string& source, std::string caption) {
    if (caption.empty()) return "Le r\xC3\xA9sultat et son type sont dessous.";
    for (std::size_t p; (p = caption.find(" : ")) != std::string::npos;) caption.replace(p, 3, ", ici ");
    return endSentence(line("**" + line(source, 120) + "** : " + caption));
}

std::string expressionText(const TopicInfo& i) {
    std::string t;
    // Tranche 11 (lot v2 de T1 : les 11 expr-* echouent a l'etape 1, « bouton:B   BOOL introuvable ») :
    // le texte supposait la page des expressions deja ouverte. Un tutoriel part du bac, sur l'ecran
    // principal, et le format n'a pas de geste « ouvre la page ». Le chemin qui existe, celui de
    // l'etape 7 d'expression-couleur (T1, 7/7 sur l'appli fusionnee) : une cuve dans Vue_Tuto, clic
    // droit sur sa case Couleur du contenu, puis « La page des expressions… » (I111, lien 5), qui
    // ouvre la page ; la liste de gauche choisit ensuite le type du sujet.
    // Tranche 12 (decision du chef) : T1 ajoute la commande « action <identifiant> » de @avant (tuto-111,
    // 586a643). La page s'ouvre alors par « action help.expressions », en une ligne, sans dependre d'une
    // case ni d'une cuve. Le deducteur le prend des que le lecteur connait la commande (apres
    // l'integration de 586a643) ; sans elle, le chemin du clic droit reste.
    const bool byAction = isPrepareCommand("action");
    const std::string field(kExprTrialField);
    const auto gap = i.button.find("   ");
    const std::string typeTitle = gap == std::string::npos ? i.button : i.button.substr(gap + 3);
    if (byAction) {
        header(t, i, "Armoire_Gaz");
        t += "@avant\naction help.expressions\n";
        t += "\n== 1 | Choisis le type\n";
        // LOT-TUTOS « Pour T3 » (T1, 2 h 45) : la bulle dit ou se trouve la page, sans decrire un clic qu'on ne voit pas.
        t += ": La page des expressions est ouverte. Elle est dans le menu **Aide**, \xC2\xAB Les expressions \xC2\xBB, ou au clic droit "
             "sur une case \xC3\xA0 expression, \xC2\xAB La page des expressions\xE2\x80\xA6 \xC2\xBB. \xC3\x80 gauche, les 11 types : "
             "clique **" + typeTitle + "**.\n";
    } else {
    header(t, i, "Armoire_Gaz | Vue_Tuto");
    t += "@avant\nvue Vue_Tuto\nposer Cuve 660,180 Cuve_Tuto\n";
    t += "\n== 1 | Ouvre la page et choisis le type\n";
    t += ": Dans l'inspecteur, **clic droit** sur une case \xC3\xA0 expression (ici **Couleur du contenu** de la cuve), puis "
         "**La page des expressions\xE2\x80\xA6**. \xC3\x80 gauche, les 11 types : clique **" + typeTitle + "**.\n";
    t += "clic vue:Cuve_Tuto\n";
    t += "clic " + q("propriete:Couleur du contenu") + " droit\n";
    t += "clic " + q("menu:La page des expressions\xE2\x80\xA6") + "\n";
    }
    t += "clic " + q("bouton:" + i.button) + "\n";
    t += "encadrer " + q("bouton:" + i.button) + "\n";
    // Tranche 18 (relecture comme le client : « BOOL : ce que la case attend, BOOL. Par exemple Pompe_Marche AND NOT
    // V[0].Defaut », sans point, et le meme exemple a l'etape 2 ; « T#5s · T#1m30s ») : une phrase finie, les exemples
    // en gras, separes par une virgule, et seulement ceux que l'etape 2 ne dit pas.
    const auto said = syntaxSentences(i.syntax);
    {
        std::string later;
        for (const auto& x : said) later += x + " ";
        std::string examples;
        for (std::size_t p = 0; p <= i.summary.size();) {
            const auto dot = i.summary.find(" \xC2\xB7 ", p);
            std::string ex = i.summary.substr(p, (dot == std::string::npos ? i.summary.size() : dot) - p);
            while (!ex.empty() && ex.front() == ' ') ex.erase(0, 1);
            while (!ex.empty() && ex.back() == ' ') ex.pop_back();
            if (!ex.empty() && later.find(ex) == std::string::npos) examples += (examples.empty() ? "**" : ", **") + ex + "**";
            if (dot == std::string::npos) break;
            p = dot + 4;
        }
        std::string lead = typeTitle == i.wanted ? "Pour une case qui attend " + i.wanted + "."
                                                 : typeTitle + " : pour une case qui attend " + i.wanted + ".";
        if (!examples.empty()) lead += " Par exemple " + examples + ".";
        t += "dire " + q(line(lead)) + "\n\n";
    }
    t += "== 2 | La syntaxe et les op\xC3\xA9rateurs\n";
    {   // tranche 17 (T1, point 6 : le cadre de la table copie tel quel, commentaire compris) : en phrases
        t += ": " + (said.empty() ? line(i.syntax) : said.front()) + "\n";
        t += "dire " + q(said.empty() ? line(i.syntax) : said.back()) + "\n";
    }
    if (!i.operators.empty()) t += "dire " + q(line("Les op\xC3\xA9rateurs : " + i.operators + ".")) + "\n";
    // Tranche 13 (decision du chef, 03/10 : le chemin essai.texte) : choisir le type met deja son
    // premier exemple dans le champ, qui « convient » : `essai.convient = oui` etait vrai avant tout
    // geste (LOT-TUTOS, T1, 2 h 45). L'« A toi » fait ecrire un AUTRE exemple de la table et verifie
    // le texte tape (essai.texte), puis qu'il convient. Sans autre exemple (un sujet fait a la main,
    // hors de la table) : l'ancien « A toi ».
    if (const auto* other = otherExample(i)) {
        const std::string source(other->source), caption(other->caption);
        t += "\n== 3 | Un autre exemple dans le champ d'essai\n";
        t += ": Le champ d'essai montre d\xC3\xA9j\xC3\xA0 le premier exemple, jug\xC3\xA9 par le vrai moteur. \xC3\x89" "cris-en "
             "un autre : le r\xC3\xA9sultat et son type changent \xC3\xA0 chaque lettre.\n";
        t += "clic " + field + "\n";
        t += "texte " + field + " " + q(source) + "\n";
        t += "encadrer " + field + "\n";
        t += "dire " + q(exampleSaid(source, caption)) + "\n";
        t += "@atoi \xC3\x89" "cris " + line(source, 120) + " dans le champ d'essai.\n";
        t += "@cible " + field + "\n";
        t += "@verifier essai.texte = " + q(source) + "\n";
        t += "@verifier essai.convient = oui\n\n";
    } else {
    t += "\n== 3 | Un exemple dans le champ d'essai\n";
    t += ": Un clic sur un exemple le met dans le champ d'essai : le vrai moteur l'\xC3\xA9value.\n";
    t += "clic " + field + "\n";
    t += "texte " + field + " " + q(i.example) + "\n";
    t += "encadrer " + field + "\n";
    t += "dire " + q(exampleSaid(i.example, i.exampleCaption)) + "\n";
    t += "@atoi \xC3\x89" "cris " + line(i.example, 120) + " dans le champ d'essai.\n";
    t += "@cible " + field + "\n";
    t += "@verifier essai.convient = oui\n\n";
    }
    t += "== 4 | Une erreur courante\n";
    t += ": " + line(i.reason) + "\n";
    t += "texte " + field + " " + q(i.wrong) + "\n";
    t += "encadrer " + field + "\n";
    t += "dire " + q(line("Faux : " + i.wrong + ". Juste : " + i.right + ".")) + "\n";
    t += "clic " + q("bouton:Remplacer par") + "\n\n";
    t += "== 5 | Dans une vraie case\n";
    t += ": Dans l'inspecteur, une case qui attend " + i.wanted + " : **=** puis l'expression ; l'aide \xC3\xA0 la saisie propose ce qui convient.\n";
    t += "dire " + q("Le champ d'essai juge comme la case : ce qui passe ici passe dans la vue.") + "\n";
    return t;
}

// Tranche 15 (recette T3-9, I111 : l'exemple d'un DDT peut faire 600 caracteres - ST_EQ_Motor - et
// line() ne coupe pas du code, qui n'a pas de fin de phrase) : 240 caracteres au plus, une seule
// espace entre les mots, coupe apres la derniere instruction entiere (« ; »), puis « … ».
std::string exampleBubble(std::string_view example) {
    std::string s;
    for (const char c : example) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { if (!s.empty() && s.back() != ' ') s += ' '; }
        else s += c;
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    constexpr std::size_t kMax = 240;
    if (s.size() <= kMax) return s;
    std::size_t cut = s.rfind(';', kMax);
    if (cut != std::string::npos && cut >= kMax / 3) return s.substr(0, cut + 1) + " \xE2\x80\xA6";
    cut = s.rfind(' ', kMax);
    if (cut == std::string::npos || cut == 0) {
        cut = kMax;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
    }
    return s.substr(0, cut) + " \xE2\x80\xA6";
}

std::string libraryText(const TopicInfo& i) {
    // Macro, DFB, DDT : leur description et leurs champs. Ces tutoriels montrent.
    // Tranche 9 (le lot de T1) : l'etape 1 vise le dossier de l'arbre de l'automate (visible des
    // l'ouverture : session 88) ; seul un DFB garde un « A toi » en simulation (son corps y tourne) :
    // une macro se lance depuis l'onglet Macros, un DDT se declare, F8 n'y montre rien.
    std::string t;
    header(t, i, "Armoire_Gaz");
    const std::string folder = i.kind == TopicKind::Macro   ? "arbre:API/Macros"
                             : i.kind == TopicKind::Block ? "arbre:API/Blocs DFB"
                                                          : "arbre:API/Types d\xC3\xA9riv\xC3\xA9s";
    const std::string folderName = folder.substr(folder.rfind('/') + 1);
    t += "\n== 1 | Ouvre " + folderName + " dans l'arbre de l'automate\n";
    t += ": " + prose(i.summary.empty() ? i.title : i.summary) + "\n";
    t += "clic " + q(folder) + "\nencadrer " + q(folder) + "\n";
    // Tranche 22 (R111-17, decision 54 : « DFB_IO_ANA32 est dans Blocs DFB, sous API », alors qu'Armoire_Gaz ne l'a pas) :
    // un bloc ou un type vient du catalogue de la bibliotheque (CenterSources::librarySources) ; Blocs DFB et Types derives
    // listent ceux du projet ouvert. Le texte parle de la bibliotheque et n'affirme rien du projet. L'onglet Macros, lui,
    // liste les macros disponibles : sa phrase reste.
    if (i.kind == TopicKind::Macro)
        t += "dire " + q(line(i.name + " est dans " + folderName + ", sous API.")) + "\n";
    else
        t += "dire " + q(line(i.name + " vient de la biblioth\xC3\xA8que. " + folderName + ", sous API, liste "
                              + (i.kind == TopicKind::Block ? "les blocs" : "les types")
                              + " du projet ouvert : il n'y est que si le projet le contient.")) + "\n";
    // 1.11.2 (T1 ; le LISEZ-MOI de la 1.11.0 : « ceux des macros et des types DDT n'ont pas encore d'A toi ») : un DDT
    // fait ouvrir son dossier. Le clic sur la ligne ouvre l'onglet « API · Types derives » : onglet.courant le lit,
    // faux avant le geste (le bac s'ouvre sur l'onglet API), juste apres (vu au verificateur de T1, tranche 35,
    // 3 / 3 sur bloc-ST_ALM_Cmd ; les 41 DDT de la bibliotheque, tranche 37). Une macro : son clic laisse l'onglet API,
    // d'ou la condition arbre.choisi, ci-dessous.
    if (i.kind == TopicKind::DataType) {
        t += "@atoi Ouvre " + folderName + " dans l'arbre de l'automate (API \xE2\x80\xBA " + folderName + ").\n";
        t += "@cible " + q(folder) + "\n";
        t += "@verifier onglet.courant ~ " + folderName + "\n";
    } else if (i.kind == TopicKind::Macro && hmi::tutotopics::conditionDeclaredSafe("arbre.choisi")) {
        // 1.11.2 (T1, decision 141) : une macro fait ouvrir son dossier. Son clic laisse l'onglet « API » (vu au
        // verificateur de T1, tranche 35) : la condition neuve arbre.choisi(<chemin>) lit la ligne choisie de l'arbre
        // (help/TutorialScreen.hpp). Declaree sure a la tranche 37 (les 31 macros justes au verificateur).
        t += "@atoi Ouvre " + folderName + " dans l'arbre de l'automate (API \xE2\x80\xBA " + folderName + ").\n";
        t += "@cible " + q(folder) + "\n";
        t += "@verifier arbre.choisi(" + folder.substr(6) + ") = oui\n";
    }
    // Tranche 15 (T3-9 : I111 remplit fields depuis la bibliotheque, e9218d4 ; vu dans l'appli de
    // l'integration, session r99) : l'etape 2 est une phrase - combien, puis les quatre premiers ;
    // un bloc donne ses entrees, puis ses sorties (le sens est au bout du type : « REAL, entree »).
    using Fields = std::vector<std::pair<std::string, std::string>>;
    const auto firsts = [](const Fields& fs) {
        std::string s;
        for (std::size_t n = 0; n < fs.size() && n < 4; ++n) s += (n ? ", " : "") + fs[n].first + (fs[n].second.empty() ? "" : " (" + fs[n].second + ")");
        if (fs.size() > 4) s += " et " + std::to_string(fs.size() - 4) + (fs.size() - 4 > 1 ? " autres" : " autre");
        return s;
    };
    std::string list;
    if (i.kind == TopicKind::Block) {
        const std::string in = "entr\xC3\xA9" "e", out = "sortie", both = "entr\xC3\xA9" "e/sortie";
        Fields ins, outs, inouts, rest;
        for (const auto& [name, type] : i.fields) {
            const auto comma = type.rfind(", ");
            const std::string sens = comma == std::string::npos ? std::string() : type.substr(comma + 2);
            Fields& to = sens == in ? ins : sens == out ? outs : sens == both ? inouts : rest;
            to.emplace_back(name, &to == &rest ? type : type.substr(0, comma));
        }
        const auto part = [&](const std::string& one, const std::string& many, const Fields& fs) {
            return fs.empty() ? std::string() : (fs.size() > 1 ? many : one) + " (" + std::to_string(fs.size()) + ") : " + firsts(fs) + ".";
        };
        for (const std::string& p : {part("Une entr\xC3\xA9" "e", "Ses entr\xC3\xA9" "es", ins), part("Une sortie", "Ses sorties", outs),
                                     part("Une entr\xC3\xA9" "e/sortie", "Ses entr\xC3\xA9" "es/sorties", inouts),
                                     part("Un param\xC3\xA8tre", "Ses param\xC3\xA8tres", rest)})
            if (!p.empty()) list += (list.empty() ? "" : " ") + p;
    } else if (!i.fields.empty()) {
        const std::size_t n = i.fields.size();
        list = std::to_string(n) + (i.kind == TopicKind::Macro ? (n > 1 ? " champs" : " champ") : (n > 1 ? " membres" : " membre")) + " : "
             + firsts(i.fields) + ".";
    }
    const char* what = i.kind == TopicKind::Macro ? "Son formulaire" : i.kind == TopicKind::Block ? "Ses entr\xC3\xA9" "es et ses sorties" : "Ses membres";
    t += "\n== 2 | " + std::string(what) + "\n";
    // Tranche 21 (R111-13, decision 45 : la decision 27 n'avait touche que le centre ; 152 etapes de la bibliotheque
    // redisaient leur bulle) : un « dire » egal a la bulle devient une attente (une etape garde un geste, T1).
    const auto sayOrWait = [&t](const std::string& bubble, const std::string& say) {
        t += ": " + bubble + "\n";
        t += say != bubble ? "dire " + q(say) + "\n" : std::string("attendre 1s\n");
    };
    // 1.11.2 (T1, R1112-7 de R111 : a 8 s, la bulle de bloc-DFB_IO_ANA32 donnait ses entrees et ses sorties, l'ecran montrait
    // la liste des blocs d'Armoire_Gaz, DFB_GRAFCETENGINE choisi : le bloc nomme n'y est pas, il vient de la bibliotheque) :
    // un bloc ou un type DDT ouvre a l'etape 2 sa page dans le centre d'aide (aide <cle>, permis dans une etape depuis
    // a700b25), qui montre ce que dit la bulle ; l'etape 3 la ferme (Echap) : le bac revient, F8 (l'« A toi » d'un bloc) y
    // demarre l'IHM.
    // 1.11.2 (T1, R1112-3 de R111 : « Son formulaire » d'une macro disait ses champs sur la vue d'ensemble de l'automate,
    // aucun formulaire a l'ecran ; le clic sur API › Macros n'ouvre pas d'onglet, sondes des tranches 41 et 42) : l'etape 2
    // d'une macro ouvre l'onglet Macros (l'outil « Macros » de la barre de l'onglet API), choisit la macro dans la liste
    // (sa fiche, a droite), puis l'ouvre d'un double clic (comme Lancer) : son formulaire, dont la bulle dit les champs.
    const bool showPage = i.kind != TopicKind::Macro;
    {
        const std::string bubble = line(list.empty() ? std::string(what) + " : voir son aide." : list);
        const std::string say = line(list.empty() ? i.name + " : son aide le dit." : list);
        t += ": " + bubble + "\n";
        if (showPage) t += "aide " + i.key + "\n";
        else
            t += "clic \"outil:Macros\"\nattendre 1s\nclic " + q("macro:" + i.name) + "\nattendre 1s\nclic " + q("macro:" + i.name)
                 + " double\nattendre 1.5s\n";
        t += say != bubble ? "dire " + q(say) + "\n" : std::string("attendre 1s\n");
    }
    // Tranche 22 (R111-17) : rien ne fait tourner l'exemple ici ; le titre dit ou il va.
    t += "\n== 3 | " + std::string(i.kind == TopicKind::Macro ? "Ce qu'elle \xC3\xA9" "crit" : i.kind == TopicKind::Block ? "Dans une section ST" : "Lis-le dans une section") + "\n";
    // Tranche 9 : sans exemple ni resume (les macros de libs/ n'ont pas de « #! summary »), la bulle
    // n'est plus vide (le lecteur de T1 refuse une etape sans bulle) : ce que la sorte fait. Le resume
    // n'y est plus repete (il est deja la bulle de l'etape 1).
    const std::string usage = i.kind == TopicKind::Macro
        ? "Lance-la depuis l'onglet Macros : elle \xC3\xA9" "crit dans le projet ce que dit son formulaire."
        : i.kind == TopicKind::Block ? "Pose-le dans une section ST : ses entr\xC3\xA9" "es et ses sorties le pilotent."
                                     : "D\xC3\xA9" "clare une variable de ce type : ses membres se lisent avec un point.";
    if (showPage) {   // 1.11.2 (R1112-7) : la bulle, puis Echap ferme le centre (le bac revient), puis le « dire »
        const std::string bubble = line(i.example.empty() ? usage : exampleBubble(i.example)), say = line(usage);
        t += ": " + bubble + "\ntouche Escape\nattendre 1s\n";
        if (say != bubble) t += "dire " + q(say) + "\n";
    } else {
        sayOrWait(line(i.example.empty() ? usage : exampleBubble(i.example)), line(usage));
    }
    if (i.kind != TopicKind::Block) return t;
    t += "\n== 4 | \xC3\x80 toi : essaie en simulation\n";
    // Tranche 22 (R111-17) : F8 ne demarre que l'IHM (l'infobulle de « Demarrer l'IHM » le dit), et le bloc n'est peut-etre
    // pas dans le projet : ni « l'automate simule avec elle », ni « y joue son role ».
    t += ": **F8** d\xC3\xA9marre l'IHM en simulation, sans le programme de l'automate.\n";
    t += "touche F8\nattendre 1.5s\n";
    t += "dire " + q("Le programme de l'automate d\xC3\xA9marre \xC3\xA0 part, par \xC2\xAB D\xC3\xA9marrer l'API \xC2\xBB : un bloc ne tourne qu'avec lui.") + "\n";
    t += "@atoi D\xC3\xA9marre la simulation (F8).\n";
    t += "@cible ecran:800,450\n";
    t += "@verifier simulation.ihm = marche\n";
    return t;
}

} // namespace

// topicKindName : celui de T1 (help/TutorialLaunch.hpp), depuis l'integration I111.

// L'identifiant d'un tutoriel est en minuscules (Tutorial::parse) : "macro-CREER_VANNE" -> "deduit-macro-creer_vanne".
std::string deducedId(std::string_view key) {
    std::string id = "deduit-" + std::string(key);
    for (auto& c : id)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return id;
}

std::string whyNoTutorial(const TopicInfo& i) {
    if (i.key.empty() || i.key.find(' ') != std::string::npos) return "cl\xC3\xA9 vide ou avec un espace";
    if (i.title.empty()) return "sans titre";
    switch (i.kind) {
    case TopicKind::Object:
        if (i.name.empty()) return "objet sans tuile dans la biblioth\xC3\xA8que";
        break;
    case TopicKind::Guide:
        if (i.summary.empty() && i.headings.empty()) return "sujet du guide sans r\xC3\xA9sum\xC3\xA9 ni intertitre";
        break;
    case TopicKind::Expression:
        if (i.button.empty() || i.example.empty() || i.wrong.empty() || i.wanted.empty())
            return "type d'expression sans exemple, sans erreur courante ou sans type attendu";
        break;
    case TopicKind::Macro:
    case TopicKind::Block:
    case TopicKind::DataType:
        if (i.name.empty()) return "sans nom";
        break;
    case TopicKind::Special:
    case TopicKind::Other:
        return "page sp\xC3\xA9" "ciale : pas de tutoriel d\xC3\xA9" "duit";
    }
    return {};
}

std::string deduceTutorial(const TopicInfo& i) {
    if (!whyNoTutorial(i).empty()) return {};
    switch (i.kind) {
    case TopicKind::Object: return objectText(i);
    case TopicKind::Guide: return guideText(i);
    case TopicKind::Expression: return expressionText(i);
    case TopicKind::Macro:
    case TopicKind::Block:
    case TopicKind::DataType: return libraryText(i);
    case TopicKind::Special:
    case TopicKind::Other: break;
    }
    return {};
}

} // namespace help

namespace hmi::tutotopics {

namespace {
// Tranche 9 : un tableau du guide (lignes separees par \n, cellules par \t, la premiere ligne : les
// titres) en une phrase.
// Tranche 16 (T1 : « ; et 3 autres. » ne dit pas lesquels ; « . ; » ; une majuscule apres « : ») : les lignes
// en entier (« a : b ; c : d ») si elles tiennent en 240 caracteres ; sinon leurs premieres cases, toutes
// (« a, c, e et g. »), ou celles qui tiennent, puis « entre autres ». Une case perd son point final ; le
// mot courant qui suit « : » (un article...) prend sa minuscule.
std::string lowerCommonStart(std::string s) {
    static constexpr std::string_view kWords[] = {"Le ", "La ", "Les ", "L'", "Un ", "Une ", "Des ", "Du ", "De ", "Ce ", "Cet ", "Cette ",
                                                  "Ces ", "Son ", "Sa ", "Ses ", "Leur ", "Leurs ", "Il ", "Elle ", "On ", "Chaque ", "Tout ",
                                                  "Toute ", "Tous ", "En ", "Dans ", "Pour ", "Par ", "Sur ", "Avec ", "Sans ", "Au ", "Aux ", "Une "};
    for (const auto w : kWords)
        if (s.rfind(w, 0) == 0) { s[0] = static_cast<char>(s[0] - 'A' + 'a'); return s; }
    if (s.rfind("\xC3\x80 ", 0) == 0) s[1] = static_cast<char>(0xA0);   // « À » -> « à »
    return s;
}
std::string noFinalDot(std::string s) {
    while (!s.empty() && (s.back() == '.' || s.back() == ' ' || s.back() == ';')) s.pop_back();
    return s;
}
std::size_t charCount(std::string_view s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}
// « a, b, c et d » ; au-dela de max caracteres : « a, b, entre autres ».
std::string enumeration(const std::vector<std::string>& items, std::size_t max) {
    std::string all;
    for (std::size_t n = 0; n < items.size(); ++n)
        all += (n == 0 ? "" : n + 1 == items.size() ? " et " : ", ") + items[n];
    if (charCount(all) <= max) return all;
    std::string some;
    for (const auto& it : items) {
        if (charCount(some) + charCount(it) + 16 > max) break;
        some += (some.empty() ? "" : ", ") + it;
    }
    return some.empty() ? std::string() : some + ", entre autres";
}
std::string tableText(std::string_view table) {
    std::vector<std::string> rows;
    std::size_t at = 0;
    while (at <= table.size()) {
        const auto end = std::min(table.find('\n', at), table.size());
        if (end > at) rows.emplace_back(table.substr(at, end - at));
        at = end + 1;
    }
    std::vector<std::string> full, firsts;
    bool anyText = false;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const auto tab = rows[r].find('\t');
        std::string cell = noFinalDot(guide::plain(rows[r].substr(0, tab)));
        if (cell.empty()) continue;
        // Tranche 18 (relecture comme le client : « les écritures, Les lectures, Le regroupement ») : la liste
        // continue une phrase ; chaque article (Le, La, Les, L'…) y prend sa minuscule apres le premier (finishText
        // met la minuscule au premier quand la liste suit « : »).
        if (!firsts.empty()) cell = lowerCommonStart(std::move(cell));   // la 1re garde sa majuscule : une liste seule est une phrase
        firsts.push_back(cell);
        if (tab != std::string::npos) {
            const auto next = rows[r].find('\t', tab + 1);
            const auto second = noFinalDot(guide::plain(rows[r].substr(tab + 1, next == std::string::npos ? std::string::npos : next - tab - 1)));
            // tranche 16 (T1 : « Symbole : (vide) ; Arguments : (vide). ») : une valeur vide n'est pas un texte
            if (!second.empty() && second != "(vide)" && second != "-" && second != "\xE2\x80\x94") {
                cell += " : " + lowerCommonStart(second);
                anyText = true;
            }
        }
        full.push_back(cell);
    }
    if (full.empty()) return {};
    if (!anyText) return enumeration(firsts, 238) + ".";
    std::string out;
    for (const auto& f : full) out += (out.empty() ? "" : " ; ") + f;
    if (charCount(out) <= 238) return out + ".";
    const std::string list = enumeration(firsts, 238);
    return list.empty() ? std::string() : list + ".";
}

struct TreePlace { std::string_view place, path; };
constexpr TreePlace kTreePlaces[] = {
    // Visibles a l'ouverture (IHM, ses dossiers ; API ; Simulation et ses lignes ; Versions).
    {"scripts", "arbre:IHM/Programmation"}, {"fonctions", "arbre:IHM/Programmation"},
    {"vues", "arbre:IHM/Vues"}, {"vue", "arbre:IHM/Vues"},
    {"ihm", "arbre:IHM"}, {"api", "arbre:API"}, {"versions", "arbre:Versions"},
    {"config", "arbre:IHM/Configuration"}, {"fichiers", "arbre:IHM/Fichiers externes"},
    {"ressources", "arbre:IHM/Ressources"}, {"symboles", "arbre:IHM/Symboles"}, {"styles", "arbre:IHM/Styles"},
    {"essais", "arbre:IHM/Essais"}, {"simulation", "arbre:Simulation/IHM"},
    // Sous IHM > Configuration, replie au depart.
    {"alarmes", "arbre:IHM/Configuration/Alarmes"}, {"recettes", "arbre:IHM/Configuration/Recettes"},
    {"utilisateurs", "arbre:IHM/Configuration/Utilisateurs"}, {"historiques", "arbre:IHM/Configuration/Historiques"},
    {"langues", "arbre:IHM/Configuration/Langues"}, {"unites", "arbre:IHM/Configuration/Unit\xC3\xA9s et formats"},
    {"equipements", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"poste", "arbre:IHM/Configuration/Poste d'exploitation"}, {"notifications", "arbre:IHM/Configuration/Notifications"},
    {"rapports", "arbre:IHM/Configuration/Rapports"}, {"web", "arbre:IHM/Configuration/Acc\xC3\xA8s web"},
    // Sous IHM > Programmation generale, replie au depart.
    {"types-ihm", "arbre:IHM/Programmation/Types IHM"}, {"variables-ihm", "arbre:IHM/Programmation/Variables IHM"},
    {"variables-systeme", "arbre:IHM/Programmation/Variables syst\xC3\xA8me"},
    {"variables-instances", "arbre:IHM/Programmation/Variables d'instances"},
    // Sous IHM > Vues, replie au depart.
    {"popups", "arbre:IHM/Vues/Popups"}, {"modeles", "arbre:IHM/Vues/Mod\xC3\xA8les"},
};

// Tranche 10 (le point 1 du lot de T1 : 67 sujets du guide sans geste) : un sujet dont aucun lieu
// (@lieux) n'a de ligne dans l'arbre prend celle-ci, choisie d'apres son texte. Seules des lignes que
// la session 88 a vues (kTreePlaces) : les outils (Compiler, Generer, Rechercher, Echanges) sont des
// boutons de la rangee d'outils, que la cible arbre: ne vise pas ; leurs sujets ouvrent le dossier IHM.
struct TopicPlace { std::string_view key, path; };
constexpr TopicPlace kTopicPlaces[] = {
    {"nouveautes-1-9", "arbre:IHM"}, {"nouveautes-1-10", "arbre:IHM"}, {"reperes-nouveautes", "arbre:IHM"},
    {"historique", "arbre:IHM/Vues"}, {"comparer-versions", "arbre:Versions"}, {"onglets", "arbre:IHM/Vues"},
    {"aller-a", "arbre:IHM"}, {"excel", "arbre:IHM/Programmation/Variables IHM"}, {"dossiers", "arbre:IHM/Vues"},
    {"didacticiel", "arbre:IHM"}, {"nouveautes-lot8", "arbre:IHM"}, {"objets", "arbre:IHM/Vues"},
    {"proprietes", "arbre:IHM/Vues"}, {"actions", "arbre:IHM/Vues"}, {"parametres-popups", "arbre:IHM/Vues/Popups"},
    {"appliquer-copie", "arbre:IHM/Vues"}, {"tutoriel-symbole", "arbre:IHM/Symboles"},
    {"alarmes-symboles", "arbre:IHM/Symboles"}, {"contenu", "arbre:IHM/Vues"},
    {"raccourcis-vue", "arbre:IHM/Vues"},                                  // 1.11.23 : les raccourcis d'une vue
    {"modeles-de-vues", "arbre:IHM/Vues/Mod\xC3\xA8les"}, {"paquets-de-vues", "arbre:IHM/Vues"},
    // 1.11.2 (T2, decision 247 : la chaine 4 le disait « sans lieu ») : l'export et l'import des symboles, des types, des
    // fonctions et des scripts partent du dossier IHM > Symboles (ses boutons, son clic droit), comme les vues de leur dossier.
    {"paquets-symboles", "arbre:IHM/Symboles"},
    {"rechercher-remplacer", "arbre:IHM"}, {"dupliquer-reperes", "arbre:IHM/Vues"}, {"affichage", "arbre:IHM/Configuration"},
    {"communication", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"qualite-valeurs", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"variables-liees", "arbre:IHM/Configuration/\xC3\x89quipements"}, {"lectures-simulees", "arbre:Simulation/IHM"},
    {"reseau-pc", "arbre:IHM/Configuration/\xC3\x89quipements"}, {"scanner-ip", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"outil-modbus", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"outil-cyclique", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"principe-communication", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"moyens-communication", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"types-acces", "arbre:IHM/Configuration/\xC3\x89quipements"},
    {"lexique-communication", "arbre:IHM/Configuration/\xC3\x89quipements"}, {"objet-communs", "arbre:IHM/Vues"},
    {"synoptique", "arbre:IHM/Symboles"}, {"alarmes-bibliotheque", "arbre:IHM/Configuration/Alarmes"},
    {"objet-instance-de-symbole", "arbre:IHM/Symboles"}, {"graphiques", "arbre:IHM/Vues"},
    {"alarmes-objets", "arbre:IHM/Configuration/Alarmes"}, {"alarmes-dans-arbre", "arbre:IHM/Vues"},
    {"production", "arbre:IHM/Configuration/Poste d'exploitation"}, {"navigation", "arbre:IHM/Vues"},
    {"navigation-marche", "arbre:Simulation/IHM"}, {"variables-ihm", "arbre:IHM/Programmation/Variables IHM"},
    {"tableaux-ihm", "arbre:IHM/Programmation/Variables IHM"}, {"variables-locales", "arbre:IHM/Programmation"},
    {"reference", "arbre:IHM/Programmation"}, {"aide-saisie", "arbre:IHM/Vues"},
    {"variables-esclaves", "arbre:IHM/Programmation/Variables syst\xC3\xA8me"},
    {"groupes-objets", "arbre:IHM/Configuration/Alarmes"}, {"surcharge-alarmes", "arbre:IHM/Configuration/Alarmes"},
    {"signature", "arbre:IHM/Configuration/Utilisateurs"}, {"audit", "arbre:IHM/Configuration/Utilisateurs"},
    {"parametres-systeme", "arbre:Simulation/IHM"}, {"page-simulation", "arbre:Simulation/IHM"},
    {"menu-connexion", "arbre:IHM/Configuration/Utilisateurs"}, {"actions-ressources", "arbre:IHM/Ressources"},
    {"verifier", "arbre:IHM"}, {"performances", "arbre:Simulation/IHM"}, {"reprise", "arbre:IHM"},
    {"etats-projet", "arbre:IHM"}, {"echange", "arbre:IHM"}, {"dossier", "arbre:IHM/Fichiers externes"},
    // Tranche 20 (R111-1, decision 36 du chef : 17 tutoriels de Demarrer et de L'automate sans geste, une bulle seule) :
    // les 21 pages de l'aide generale (help::center::apiPageGroups, sujets "api-<ancre>") ont leur endroit, d'apres la
    // table de F1 (help/F1Table.cpp, T2) quand elle les cite : l'onglet de l'API ou de la simulation que F1 y ouvre, par
    // sa ligne de l'arbre (visible a l'ouverture, lignes de la session 97). Les pages d'un ecran sans ligne (les themes,
    // les dossiers, l'explorateur, les bandeaux) montrent le tableau de bord (la ligne API) ; glisser, les Ressources
    // (ou va un fichier depose). Aucune ne demarre l'IHM : l'« A toi » F8 des pages de la simulation n'est pas deja vrai.
    {"api-arbre-lot8", "arbre:API"}, {"api-importer", "arbre:API/Configuration"}, {"api-reimporter", "arbre:API"},
    {"api-ordre", "arbre:API/Ordre d'ex\xC3\xA9" "cution"}, {"api-renommer", "arbre:API/Variables"},
    {"api-compiler", "arbre:IHM"}, {"api-alarmes", "arbre:API/Blocs DFB"},
    {"api-ddt-popups", "arbre:API/Types d\xC3\xA9riv\xC3\xA9s"},
    {"api-sim-ensemble", "arbre:Simulation/Vue d'ensemble"}, {"api-sim-debogage", "arbre:Simulation/D\xC3\xA9" "bogage"},
    {"api-sim-pause", "arbre:Simulation/Automate"}, {"api-sim-forcages", "arbre:Simulation/For\xC3\xA7" "ages"},
    {"api-sim-courbes", "arbre:Simulation/Courbes"}, {"api-sim-journal", "arbre:Simulation/Journal"},
    {"api-glisser", "arbre:IHM/Ressources"}, {"api-themes", "arbre:API"}, {"api-dossiers", "arbre:API"},
    {"api-explorateur-lot8", "arbre:API"}, {"api-bandeaux", "arbre:API"}, {"api-filtres", "arbre:API/Variables"},
    {"api-exports-lot8", "arbre:API"},
    {"raccourcis", "arbre:IHM"},
    // Integration I111 (tranche 9) : la recette de T2 (T2-2, `2a7fe37`) retire « alarmes » des @lieux de
    // champs-expressions (F1 sur la page Alarmes ouvre Alarmes) ; ses autres lieux (inspecteur, proprietes,
    // actions, parametres) n'ont pas de ligne. Ses cases sont dans l'inspecteur d'un objet d'une vue,
    // comme proprietes et actions : la ligne des vues.
    {"champs-expressions", "arbre:IHM/Vues"},
};

// Tranche 21 (R111-9) : la regle de l'inspecteur range de la 1.10.4 (HmiPanels.cpp, hmiObjectNeedsVariable, dans l'appli,
// que xpg_hmi ne voit pas) : la case de la variable se montre toujours pour un objet dont la variable fait ce que sa Valeur
// ne fait pas (une commande l'ecrit, un champ de saisie aussi, un histogramme la mesure, un onglet, un repli, un plan, un
// resume par zone la suivent, un compteur horaire y garde son total) ; pour les autres, seulement si elle est deja reglee.
// L'essai ciblesInspecteur111 compare les cibles des deduits a l'inspecteur que l'appli construit.
bool variableShownWhenEmpty(Kind k) {
    return kindWritesVariable(k) || k == Kind::InputField || k == Kind::Histogram || k == Kind::TabContainer
        || k == Kind::CollapsiblePanel || k == Kind::ZoneMap || k == Kind::AlarmSummary || k == Kind::HourMeter;
}
} // namespace

std::string treePathForTopic(std::string_view key) {
    for (const auto& p : kTopicPlaces)
        if (p.key == key) return std::string(p.path);
    return {};
}

// Tranche 22 (R111-12, decision 53 : menu-aide cliquait API, nouveautes-lot8 IHM, et finissaient par F8) : les sujets de
// Demarrer qui parlent de l'aide ont pour endroit le « ? » de la barre du haut, qui ouvre le menu Aide (« L'aide (F1), les
// guides, le didacticiel, les nouveautes, a propos », TopBar.cpp) ; Signaler un probleme et les notes de version sont des
// pages du centre. Les autres sujets de Demarrer gardent leur ligne de l'arbre (IHM, Versions, Vues, Variables IHM).
std::string barPlaceForTopic(std::string_view key) {
    for (const std::string_view k : {"menu-aide", "reperes-nouveautes", "nouveautes-1-9", "nouveautes-1-10", "nouveautes-lot8",
                                     "plantages", "didacticiel"})
        if (k == key) return "barre:?";
    return {};
}

// Tranche 23 (decision 62) : les conditions que T1 a vues justes au verificateur, sur l'appli (les lots, depuis la
// tranche 10). Tranche 24 (decision 67) : centre.ouvert en est (R111 a vu les 7 « A toi » F1 de Demarrer marcher sur
// la 12e, 3 fois sur 3 ; T1 la declare sure sur la 13e, apres son e4ff663 : le premier geste apres une remise a neuf).
// 1.11.2 (T1, tranche 37, apres la livraison de la 1.11.1 : decisions 151 et 152) : arbre.choisi (la ligne choisie de
// l'arbre ; ses « A toi » : les macros, les sujets les plus vus) et onglet.courant (l'« A toi » des types DDT, fdb746b)
// en sont : vues justes au verificateur de T1 sur l'appli de 8677b47 (t36/apres-1111.sh : les plus vus, les 31 macros,
// les 41 DDT de la bibliotheque, chacun faux avant ses gestes). Le reglage d'essai, invisible du client,
// XPG_TUTO_CONDITIONS_ESSAI="<chemin>" (des chemins separes par des virgules) compte une condition neuve comme sure,
// pour que l'appli ecrive les textes deduits avec ses « A toi », a verifier avant de la declarer ici.
bool conditionDeclaredSafe(std::string_view check) {
    if (check.rfind("objets[", 0) == 0) return true;   // le genre peut avoir des espaces (« Bouton a impulsion »)
    std::string_view path = check.substr(0, check.find(' '));
    if (const auto paren = path.find('('); paren != std::string_view::npos) path = path.substr(0, paren);   // arbre.choisi(<chemin>)
    for (const std::string_view safe : {"simulation.ihm", "essai.texte", "essai.convient", "centre.ouvert", "arbre.choisi", "onglet.courant"})
        if (path == safe) return true;
    if (const char* trial = std::getenv("XPG_TUTO_CONDITIONS_ESSAI")) {
        const std::string_view list(trial);
        for (std::size_t from = 0; from <= list.size();) {
            const auto comma = std::min(list.find(',', from), list.size());
            if (!path.empty() && list.substr(from, comma - from) == path) return true;
            from = comma + 1;
        }
    }
    return false;
}

// 1.11.2 (T1, decision 141 : « les plus vus », accepte par le chef) : les sujets ou mene F1 (les cles de la table de F1,
// help/F1Table.cpp de T2 : xpg_help, que xpg_hmi et hmi_test ne lient pas, d'ou la liste ici) et ceux du chapitre
// Demarrer (guide-ihm.txt, « = Demarrer » ; les pages "start" de CenterSources.cpp, sujets "api-<ancre>").
bool mostViewedTopic(std::string_view key) {
    static constexpr std::string_view kKeys[] = {
        // la table de F1 : les endroits de l'IHM, l'ecran du projet, les onglets de l'API et de la simulation, les ecrans
        "editeur", "actions", "contenu", "scripts", "fonctions", "ihm", "vues", "configuration", "fichiers", "ressources",
        "echange", "symboles", "styles-nommes", "rechercher-remplacer", "essais", "langues", "unites-formats",
        "variables-ihm", "types-ihm", "variables-systeme", "variables-instances", "alarmes", "recettes", "securite",
        "historiques", "communication", "reseau-pc", "scanner-ip", "equipements", "variables-liees", "poste-exploitation",
        "notifications", "rapports", "acces-web", "simulation", "outil-modbus", "verifier", "historique", "grafcet",
        "api-arbre-lot8", "api-ordre", "api-filtres", "api-ddt-popups", "api-sim-forcages", "api-importer", "api-reimporter",
        "api-sim-ensemble", "api-sim-debogage", "api-sim-courbes", "api-sim-journal", "api-themes", "api-renommer",
        "api-glisser",
        // le chapitre Demarrer : les sujets du guide, puis les pages "start" de l'aide generale
        "nouveautes-1-9", "nouveautes-1-10", "reperes-nouveautes", "menu-aide", "plantages", "versions",
        "comparer-versions", "onglets", "aller-a", "excel", "dossiers", "didacticiel", "nouveautes-lot8",
        "api-dossiers", "api-explorateur-lot8", "api-bandeaux", "api-exports-lot8"};
    for (const auto k : kKeys)
        if (k == key) return true;
    return false;
}

std::string treePathForPlace(std::string_view place) {
    // Les lieux les plus frequents du guide (@lieux), vers une ligne de l'explorateur qui se
    // voit sans rien deplier (ProjectTreeModel::text) : Scripts et Fonctions sont sous
    // « Programmation generale [n] », replie ; la cible de T1 (arbre:) cherche chaque morceau
    // par le debut de son libelle, dans les lignes visibles.
    // Tranche 8 : « Alarmes [n] » est sous IHM > Configuration, replie au depart (l'explorateur
    // ne deplie que deux niveaux) : arbre:IHM/Alarmes ne se trouvait pas. Le chemin passe par
    // Configuration, que guideText deplie d'abord (foldedParent).
    // Tranche 8 : plus de lieux, d'apres les lignes que la session 88 a ecrites (arbre-lignes, Armoire_Gaz
    // ouvert) ; un chemin a trois morceaux passe par un dossier replie, que guideText deplie d'abord.
    for (const auto& p : kTreePlaces)
        if (p.place == place) return std::string(p.path);
    return {};
}

std::vector<std::string> knownPlaces() {
    std::vector<std::string> out;
    for (const auto& p : kTreePlaces) out.emplace_back(p.place);
    return out;
}

std::string foldedParent(std::string_view path) {
    // "arbre:IHM/Configuration/Alarmes" -> "arbre:IHM/Configuration" ; une ligne a deux
    // morceaux (arbre:IHM/Vues) se voit sans rien deplier : vide.
    constexpr std::string_view kTree = "arbre:";
    if (path.substr(0, kTree.size()) != kTree) return {};
    const auto rest = path.substr(kTree.size());
    if (std::count(rest.begin(), rest.end(), '/') < 2) return {};
    return std::string(path.substr(0, path.rfind('/')));
}

bool startsSimulation(std::string_view place) {
    constexpr std::string_view kSimHmi = "arbre:Simulation/IHM";
    return place == kSimHmi || (place.size() > kSimHmi.size() && place.substr(0, kSimHmi.size()) == kSimHmi && place[kSimHmi.size()] == '/');
}

std::string atoiAlreadyReached(std::string_view text) {
    // Les gestes qui changent l'etat (pas « dire », « attendre » ni « encadrer »), de @avant puis des etapes.
    const auto effective = [](std::string_view l) {
        for (const std::string_view g : {"clic ", "texte ", "touche ", "glisser ", "poser ", "action ", "regler ", "vue ", "type ", "variable "})
            if (l.substr(0, g.size()) == g) return true;
        return false;
    };
    const auto unquote = [](std::string_view s) {   // "a" -> a ; sans guillemets : le premier mot
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        if (!s.empty() && s.front() == '"') {
            std::string out;
            for (std::size_t p = 1; p < s.size() && s[p] != '"'; ++p) out += s[p] == '\\' && p + 1 < s.size() ? s[++p] : s[p];
            return out;
        }
        return std::string(s.substr(0, std::min(s.find(' '), s.size())));
    };
    std::string subject;
    std::vector<std::string> before, mine, checks;
    int step = 0;
    bool inPrepare = false;
    std::string found;
    const auto judge = [&]() {
        if (!found.empty() || step == 0 || checks.empty()) return;
        const auto done = [&](std::string_view g) { return std::find(before.begin(), before.end(), g) != before.end(); };
        const auto any = [&](auto&& pred) { return std::any_of(before.begin(), before.end(), pred); };
        const bool typeChosen = any([](const std::string& g) { return g.rfind("clic \"bouton:", 0) == 0; });
        std::vector<std::string> why;
        bool all = true;
        for (const auto& c : checks) {
            std::string reason;
            if (c == "simulation.ihm = marche") {
                if (done("touche F8")) reason = "F8 est d\xC3\xA9j\xC3\xA0 press\xC3\xA9";
                for (const auto& g : before)
                    if (reason.empty() && g.rfind("clic ", 0) == 0 && startsSimulation(unquote(std::string_view(g).substr(5))))
                        reason = "le clic sur " + unquote(std::string_view(g).substr(5)) + " d\xC3\xA9marre d\xC3\xA9j\xC3\xA0 l'IHM";
            } else if (c == "centre.ouvert = oui") {   // tranche 22 (R111-12) : la condition de T1
                if (done("touche F1")) reason = "F1 est d\xC3\xA9j\xC3\xA0 press\xC3\xA9";
            } else if (c == "essai.convient = oui") {
                if (typeChosen || any([](const std::string& g) { return g.rfind("texte ", 0) == 0; }))
                    reason = "le champ d'essai a d\xC3\xA9j\xC3\xA0 un exemple qui convient";
            } else if (c.rfind("essai.texte = ", 0) == 0) {
                const std::string want = unquote(std::string_view(c).substr(14));
                for (const auto& g : before)
                    if (g.rfind("texte ", 0) == 0) {
                        const auto sp = g.find(' ', 6);
                        if (sp != std::string::npos && unquote(std::string_view(g).substr(sp + 1)) == want) reason = "« " + want + " » est d\xC3\xA9j\xC3\xA0 tap\xC3\xA9";
                    }
                if (reason.empty() && typeChosen)
                    if (const auto* type = exprguide::find(subject); type && !type->examples.empty() && type->examples.front().source == want)
                        reason = "« " + want + " » est le premier exemple, que la page met dans le champ";
            } else if (c.rfind("objets[", 0) == 0) {
                const std::string name = c.substr(7, c.find(']') - 7);
                for (const auto& g : before)
                    if ((g.rfind("poser " + name + " ", 0) == 0 || g == "glisser \"biblio:" + name + "\"" ||
                         g.rfind("glisser \"biblio:" + name + "\" ", 0) == 0) && reason.empty())
                        reason = name + " est d\xC3\xA9j\xC3\xA0 pos\xC3\xA9";
            }
            if (reason.empty()) all = false;
            else why.push_back(c + " : " + reason);
        }
        // Tout « A toi » dont les gestes ont tous deja ete faits avant lui.
        if (!all && !mine.empty() && std::all_of(mine.begin(), mine.end(), done)) {
            why.assign(1, "ses gestes sont tous d\xC3\xA9j\xC3\xA0 faits");
            all = true;
        }
        if (all) {
            found = "\xC3\xA9tape " + std::to_string(step) + " : ";
            for (std::size_t n = 0; n < why.size(); ++n) found += (n ? " ; " : "") + why[n];
        }
    };
    std::size_t at = 0;
    bool atoi = false;
    while (at <= text.size()) {
        const auto end = std::min(text.find('\n', at), text.size());
        std::string_view l = text.substr(at, end - at);
        at = end + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.remove_suffix(1);
        if (l.substr(0, 7) == "@sujet ") { subject = std::string(l.substr(7)); continue; }
        if (l == "@avant") { inPrepare = true; continue; }
        if (l.substr(0, 3) == "== ") {
            if (atoi) judge();
            for (auto& g : mine) before.push_back(std::move(g));
            mine.clear();
            checks.clear();
            atoi = false;
            inPrepare = false;
            step = std::atoi(std::string(l.substr(3)).c_str());
            continue;
        }
        if (l.substr(0, 6) == "@atoi ") { atoi = true; continue; }
        if (l.substr(0, 10) == "@verifier ") { checks.emplace_back(l.substr(10)); continue; }
        if (!effective(l)) continue;
        (inPrepare ? before : mine).emplace_back(l);
    }
    if (atoi) judge();
    return found;
}

// Tranche 15 (recette T3-13 : 8 sujets du guide sans intertitre - retours-fonctions, tableaux-n,
// map-iterateurs, dialecte-ihm, reference, actions-ressources, configuration, raccourcis - avaient
// un deduit d'UNE etape, de 6 s, sans « A toi ») : sans intertitre, le corps du sujet donne les
// etapes, dans son ordre - les lignes de ses tableaux (3 au plus par tableau, 4 s'il n'a rien
// d'autre : la premiere case est le titre, les autres le texte), ses puces (le gras de tete est le
// titre), ses astuces et ses mises en garde ; 4 au plus (l'endroit est l'etape 1). Le code se saute.
namespace {
std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    // e accent aigu, grave, circonflexe, a accent grave (UTF-8 : C3 A9, A8, AA, A0 -> C3 89, 88, 8A, 80)
    else if (s.size() > 1 && static_cast<unsigned char>(s[0]) == 0xC3) {
        const auto c = static_cast<unsigned char>(s[1]);
        if (c == 0xA9 || c == 0xA8 || c == 0xAA || c == 0xA0) s[1] = static_cast<char>(c - 0x20);
    }
    return s;
}
std::string sentence(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == ';' || s.back() == ',' || s.back() == ':')) s.pop_back();
    // tranche 16 : « …tout ou rien (une valeur hors bornes arrete tout) » prend aussi son point
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?' &&
        !(s.size() >= 3 && s.compare(s.size() - 3, 3, "\xE2\x80\xA6") == 0))
        s += '.';
    return s;
}
std::vector<std::string> splitOn(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= s.size()) {
        const auto end = std::min(s.find(sep, at), s.size());
        out.emplace_back(s.substr(at, end - at));
        at = end + 1;
    }
    return out;
}
void contentSteps(const hmi::guide::Topic& topic, help::TopicInfo& i) {
    for (const std::size_t perTable : {std::size_t{3}, std::size_t{4}}) {
        std::vector<std::pair<std::string, std::string>> items;
        for (const auto& bl : topic.blocks) {
            if (items.size() >= 4) break;
            if (bl.kind == guide::BlockKind::Table) {
                const auto rows = splitOn(bl.text, '\n');
                if (rows.size() < 2) continue;
                const auto head = splitOn(rows[0], '\t');
                std::size_t taken = 0;
                for (std::size_t r = 1; r < rows.size() && taken < perTable && items.size() < 4; ++r) {
                    const auto cells = splitOn(rows[r], '\t');
                    // Du code (`m[cle] := v`) garde sa casse ; un mot (une valeur) prend sa majuscule.
                    const std::string first = guide::plain(cells[0]);
                    const std::string title = cells[0].rfind("`", 0) == 0 ? first : capitalized(first);
                    std::string text;
                    for (std::size_t c = 1; c < cells.size(); ++c) {
                        const std::string cell = guide::plain(cells[c]);
                        if (cell.empty()) continue;
                        const std::string label = cells.size() > 2 && c < head.size() ? guide::plain(head[c]) : std::string();
                        text += (text.empty() ? "" : " ; ") + (label.empty() ? cell : label + " : " + cell);
                    }
                    // Un titre fini par « [...] » serait lu comme une variante (« m[cle] ») : la ligne se saute.
                    if (title.empty() || text.empty() || title.back() == ']') continue;
                    items.emplace_back(title, sentence(capitalized(text)));
                    ++taken;
                }
                continue;
            }
            if (bl.kind == guide::BlockKind::Code || bl.kind == guide::BlockKind::Heading) continue;
            const std::string text = guide::plain(bl.text);
            if (text.empty()) continue;
            std::string title, bubble = text;
            if (bl.text.rfind("**", 0) == 0) {   // **Identite** : nom, description...
                const auto close = bl.text.find("**", 2);
                if (close != std::string::npos) {
                    title = guide::plain(bl.text.substr(2, close - 2));
                    std::string rest = guide::plain(bl.text.substr(close + 2));
                    while (!rest.empty() && (rest[0] == ' ' || rest[0] == ':')) rest.erase(0, 1);
                    if (!rest.empty()) bubble = capitalized(rest);
                }
            }
            if (title.empty()) {
                if (bl.kind == guide::BlockKind::Tip) title = "Astuce";
                else if (bl.kind == guide::BlockKind::Warning) title = "Attention";
                else {   // le debut de la phrase : jusqu'au premier « : », sinon cinq mots
                    const auto colon = text.find(" : ");
                    if (colon != std::string::npos && colon <= 48) title = text.substr(0, colon);
                    else {
                        std::size_t at = 0;
                        for (int w = 0; w < 5 && at != std::string::npos; ++w) at = text.find(' ', at + 1);
                        title = at == std::string::npos ? text : text.substr(0, at) + "\xE2\x80\xA6";
                    }
                }
            }
            if (title == bubble || title.back() == ']') continue;
            items.emplace_back(title, sentence(bubble));
        }
        if (items.size() >= 3 || perTable == 4) {
            for (auto& [title, text] : items) {
                i.headings.push_back(std::move(title));
                i.headingTexts.push_back(std::move(text));
            }
            return;
        }
    }
}
// Tranche 16 (T1 : « Il est le produit de trois taux : » - la liste qui suivait etait perdue) : un texte
// fini par « : » prend la liste qui le suit (ses puces ou ses etapes, en court : le gras de tete, ou le
// debut jusqu'a « : ») ; sans liste, le « : » devient un point. Une puce prise seule (« a gauche,
// l'explorateur... ; ») devient une phrase : sa majuscule, son point.
std::string shortItem(const guide::Block& bl) {
    if (bl.text.rfind("**", 0) == 0) {
        const auto close = bl.text.find("**", 2);
        if (close != std::string::npos) return lowerCommonStart(noFinalDot(guide::plain(bl.text.substr(2, close - 2))));
    }
    const std::string plain = noFinalDot(guide::plain(bl.text));
    // Tranche 18 (relecture comme le client, champs-expressions : « le bouton ✕ au bout de la ligne (« Retirer l'expression »
    // coupe avant « : ») : pas de coupe a un « : » qui est dans « » ou dans une parenthese ouverte.
    const auto opened = [](std::string_view x) {
        int q = 0, p = 0;
        for (std::size_t b = 0; b < x.size(); ++b) {
            if (x[b] == '(') ++p;
            else if (x[b] == ')') --p;
            else if (x.compare(b, 2, "\xC2\xAB") == 0) ++q;
            else if (x.compare(b, 2, "\xC2\xBB") == 0) --q;
        }
        return q > 0 || p > 0;
    };
    if (const auto colon = plain.find(" : "); colon != std::string::npos && colon <= 60) {
        if (!opened(std::string_view(plain).substr(0, colon))) return lowerCommonStart(plain.substr(0, colon));
        return {};
    }
    if (charCount(plain) <= 70) return lowerCommonStart(plain);
    return {};
}
std::string finishText(std::string text, guide::BlockKind from, const std::vector<guide::Block>& blocks, std::size_t next) {
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (!text.empty() && text.back() == ':') {
        std::vector<std::string> items;
        bool skipped = false;
        for (std::size_t n = next; n < blocks.size(); ++n) {
            const auto k = blocks[n].kind;
            if (k == guide::BlockKind::Heading) break;
            if (k == guide::BlockKind::Code) continue;
            if (k == guide::BlockKind::Table && items.empty()) {
                std::string table = tableText(blocks[n].text);
                if (!table.empty()) { text = noFinalDot(text.substr(0, text.size() - 1)) + " : " + lowerCommonStart(table); return text; }
                break;
            }
            if (k != guide::BlockKind::Bullet && k != guide::BlockKind::Step) break;
            if (auto it = shortItem(blocks[n]); !it.empty()) items.push_back(std::move(it));
            else skipped = true;
        }
        while (!text.empty() && (text.back() == ':' || text.back() == ' ')) text.pop_back();
        std::string list = enumeration(items, 200);
        if (!list.empty() && skipped && list.find("entre autres") == std::string::npos) list += ", entre autres";
        return list.empty() ? sentence(text) : text + " : " + list + ".";
    }
    if (from == guide::BlockKind::Bullet || from == guide::BlockKind::Step) {
        // Tranche 19 (relecture comme le client, poste-exploitation : « Xpg_analyzer --ihm … ») : un element qui commence
        // par du code (`xpg_analyzer --ihm …`) garde sa casse, comme la 1re case d'un tableau (tableText).
        const bool code = next > 0 && next - 1 < blocks.size() && blocks[next - 1].text.rfind("`", 0) == 0;
        return sentence(code ? text : capitalized(text));
    }
    return text;
}
// Tranche 16 (T1 : alarmes-dans-arbre et l'instance de symbole citent Vue_Pompes.Pompe_3, qu'Armoire_Gaz,
// le projet du bac a sable, n'a pas) : les noms d'exemple du guide deviennent ceux du bac (Vue_Cartes et
// ses instances Carte_Armoire_1..., qu'on voit dans la page Alarmes).
std::string sandboxNames(std::string s) {
    static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
        {"Vue_Pompes.Pompe_3", "Vue_Cartes.Carte_Armoire_1"}, {"Vue_Pompes", "Vue_Cartes"}, {"Pompe_3", "Carte_Armoire_1"}};
    for (const auto& [from, to] : kNames)
        for (std::size_t at; (at = s.find(from)) != std::string::npos;) s.replace(at, from.size(), to);
    return s;
}
} // namespace

help::TopicInfo fromGuide(const hmi::guide::Topic& topic) {
    help::TopicInfo i;
    i.key = topic.key;
    i.title = topic.title;
    i.summary = guide::plain(topic.summary);
    // Un objet qui a sa tuile dans la bibliotheque suit le modele de l'objet ; les autres
    // (SymbolInstance : un symbole se pose depuis la liste des symboles, "*" : les
    // parametres communs) suivent celui du guide, par leurs intertitres.
    std::optional<Kind> kind;
    if (!topic.kind.empty() && topic.kind != "*") {
        for (auto k : kPlaceableKinds)
            if (kindKey(k) == topic.kind) { i.name = std::string(kindLabel(k)); i.objectType = topic.kind; kind = k; }
    }
    i.places = topic.places;
    if (!i.name.empty() && kind) {
        i.kind = help::TopicKind::Object;
        // Tranche 9 (le lot de T1 : objet-courbe, « Noms des plumes (a;b) » introuvable) : l'inspecteur
        // montre les proprietes de l'objet et celles d'un objet NEUF de son genre (HmiPanels.cpp,
        // makeObject) ; une Courbe neuve n'a pas « names ». Un tutoriel ne vise donc que celles-la.
        const Object fresh = makeObject(*kind, kNoId, {}, 0, 0, kNoId);
        for (const auto& p : topic.params) {
            // Tranche 21 (R111-9, decision 45 : 8 tutoriels d'objets encadraient « Variable montree », introuvable) :
            // l'inspecteur range de la 1.10.4 ne montre la variable d'un objet qui n'en fait rien de plus que sa Valeur
            // que si elle est deja reglee (HmiPanels.cpp, hmiObjectNeedsVariable) ; l'objet pose par le tutoriel n'en a pas.
            if (p.key == "variable" && !variableShownWhenEmpty(*kind)) continue;
            // L'essai ciblesInspecteur111 l'a vu : la colonne du guide ne peut pas contenir « | » (son separateur), la case
            // de l'inspecteur s'appelle « Etats (valeur = texte | couleur) » (HmiPanels.cpp) ; le guide ecrit « , ».
            const std::string label = p.key == "stateList" && p.label == "\xC3\x89tats (valeur = texte, couleur)"
                                    ? std::string("\xC3\x89tats (valeur = texte | couleur)") : p.label;
            if (!label.empty() && fresh.find(p.key)) i.properties.emplace_back(label, sandboxNames(guide::plain(p.text)));
        }
        i.summary = sandboxNames(i.summary);
        return i;
    }
    i.kind = help::TopicKind::Guide;
    for (const auto& place : topic.places)
        if (auto path = treePathForPlace(place); !path.empty()) { i.place = path; break; }
    if (auto bar = barPlaceForTopic(topic.key); !bar.empty()) i.place = std::move(bar);   // tranche 22 (R111-12)
    if (i.place.empty()) i.place = treePathForTopic(topic.key);     // tranche 10 : chaque sujet a son geste
    for (std::size_t b = 0; b < topic.blocks.size(); ++b) {
        if (topic.blocks[b].kind != guide::BlockKind::Heading) continue;
        // Le premier texte de la section (paragraphe, puce, etape, astuce, ou tranche 9 : un tableau,
        // par ses premieres lignes) ; le code se saute.
        std::string text;
        std::size_t next = b + 1;
        auto from = guide::BlockKind::Paragraph;
        for (std::size_t n = b + 1; n < topic.blocks.size() && text.empty(); ++n) {
            const auto k = topic.blocks[n].kind;
            if (k == guide::BlockKind::Heading) break;
            if (k == guide::BlockKind::Table) text = tableText(topic.blocks[n].text);
            else if (k != guide::BlockKind::Code) text = guide::plain(topic.blocks[n].text);
            next = n + 1;
            from = k;
        }
        if (!text.empty()) text = finishText(std::move(text), from, topic.blocks, next);   // tranche 16
        // Tranche 9 (le lot de T1 : 27 bulles qui repetaient leur titre) : un intertitre sans corps
        // n'est pas une etape.
        if (text.empty()) continue;
        i.headings.push_back(guide::plain(topic.blocks[b].text));
        i.headingTexts.push_back(std::move(text));
    }
    if (i.headings.empty()) contentSteps(topic, i);
    // Tranche 16 : une bulle ne finit pas par « : » ; les noms d'exemple sont ceux du bac a sable.
    i.summary = sandboxNames(i.summary);
    if (!i.summary.empty() && i.summary.back() == ':') i.summary = sentence(i.summary.substr(0, i.summary.size() - 1));
    for (auto& h : i.headings) h = sandboxNames(h);
    for (auto& h : i.headingTexts) h = sandboxNames(h);
    return i;
}

help::TopicInfo fromExprType(const hmi::exprguide::TypeEntry& type) {
    help::TopicInfo i;
    i.kind = help::TopicKind::Expression;
    i.key = exprguide::topicKey(type);
    i.title = "Les expressions : " + std::string(type.title);
    i.name = std::string(type.key);
    i.summary = std::string(type.summary);
    i.button = std::string(type.glyph) + "   " + std::string(type.title);   // HmiExprScreen, typeLabel
    i.syntax = std::string(type.syntax);
    for (std::size_t n = 0; n < type.operators.size() && n < 6; ++n)
        i.operators += (n ? ", " : "") + std::string(type.operators[n].chip);
    if (!type.examples.empty()) {
        i.example = std::string(type.examples.front().source);
        i.exampleCaption = std::string(type.examples.front().caption);
    }
    // Tranche 15 (un BOOL ne convient pas a une case Enumeration, decision du chef) : l'etape 4 se
    // joue dans la page, sur la case de ce type ; elle prend donc une erreur de cette case (pas
    // « =Mode = 1 », une condition dont le juste est un BOOL), sinon la premiere qui n'est pas un piege.
    for (const bool ownCase : {true, false}) {
        for (const auto& m : type.mistakes) {
            if (m.trap || (ownCase && !m.caseKey.empty())) continue;
            i.wrong = std::string(m.wrong);
            i.right = std::string(m.right);
            i.reason = std::string(m.reason);
            break;
        }
        if (!i.wrong.empty()) break;
    }
    i.wanted = std::string(type.wanted);
    return i;
}

// Tranche 12 (la 1.10.4 : la vanne 3 voies) : la propriete qu'une expression lie, par la cle "value"
// du @param de son sujet du guide (« Valeur » pour 35 objets, « Position » pour la vanne 3 voies),
// si elle est parmi les proprietes du sujet (celles d'un objet neuf) ; un sujet fait a la main,
// sans guide : « Valeur » s'il l'a. Vide : l'objet n'a pas de valeur a lier.
std::string valueLabel(const help::TopicInfo& i) {
    if (i.kind != help::TopicKind::Object) return {};
    const auto has = [&](std::string_view label) {
        return std::any_of(i.properties.begin(), i.properties.end(), [&](const auto& p) { return p.first == label; });
    };
    if (const auto* topic = guide::topic(i.key)) {
        for (const auto& p : topic->params)
            if (p.key == "value" && !p.label.empty() && has(p.label)) return p.label;
        return {};
    }
    return has("Valeur") ? std::string("Valeur") : std::string();
}

std::vector<help::TopicInfo> all() {
    std::vector<help::TopicInfo> out;
    for (const auto& t : guide::topics()) out.push_back(fromGuide(t));
    for (const auto& t : exprguide::all()) out.push_back(fromExprType(t));
    return out;
}

} // namespace hmi::tutotopics
