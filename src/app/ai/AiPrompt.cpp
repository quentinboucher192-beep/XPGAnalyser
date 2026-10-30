// =============================================================================
//  app/ai/AiPrompt.cpp - ce qu'on envoie au modele, ce qu'on lit (voir AiPrompt.hpp)
// -----------------------------------------------------------------------------
//  1.15.0 (le registre) : les consignes (qui il est, comment proposer, les
//  regles de la demande, sections 5 et 9), le registre des genres tire du code
//  (AiRegistry.hpp), les connaissances actives du centre de memoire
//  (AiKnowledge.hpp), puis le contexte de la vue (AiContext.hpp) - dans cet
//  ordre : ce qui change peu d'abord (le cache du fournisseur), la vue a la fin.
//  L'outil : son schema JSON (les actions, l'enumeration des 116 genres). La
//  lecture de son entree : stricte (un champ inconnu, une operation inconnue,
//  un type faux : refuses, la raison ecrite pour le modele).
// =============================================================================
#include "AiPrompt.hpp"

#include "AiContext.hpp"
#include "AiKnowledge.hpp"
#include "AiProvider.hpp"
#include "AiRegistry.hpp"
#include "../../hmi/HmiStore.hpp"   // serializeObjectCode : l'exemple du code brut

#include <algorithm>
#include <set>

namespace app::ai {

namespace {

// Les consignes : qui il est, ce qu'il fait, les regles, les operations (au tutoiement).
const char* const kRules =
    "Tu es l'Assistant IA d'IHM Studio, le logiciel de conception d'IHM industrielles (des synoptiques "
    "d'automatisme). Tu travailles avec un automaticien : tu r\xC3\xA9ponds en fran\xC3\xA7" "ais, en le tutoyant, de fa\xC3\xA7on br\xC3\xA8ve "
    "et concr\xC3\xA8te (markdown simple : ## titres, - listes, **gras**, `code`).\n"
    "\n"
    "## Ce que tu fais\n"
    "- Tu proposes des modifications de la VUE ACTIVE (l'onglet ouvert dans l'\xC3\xA9" "diteur, d\xC3\xA9" "crite par le contexte \xC3\xA0 "
    "la fin) en appelant l'outil proposer_operations. Tu ne modifies rien toi-m\xC3\xAAme : le logiciel v\xC3\xA9rifie chaque "
    "op\xC3\xA9ration, montre un aper\xC3\xA7u et n'applique qu'avec l'accord de l'utilisateur (un Ctrl+Z annule tout).\n"
    "- Toute demande qui modifie la vue (cr\xC3\xA9" "er, modifier, d\xC3\xA9placer, redimensionner, colorer, associer une "
    "variable, aligner, r\xC3\xA9partir, dupliquer, supprimer) donne UN appel de l'outil, avec toutes ses op\xC3\xA9rations et "
    "un r\xC3\xA9sum\xC3\xA9 court (le libell\xC3\xA9 de l'historique d'annulation, par exemple \xC2\xAB Station de pompage \xC2\xBB). Avant l'appel, "
    "dis en une ou deux phrases ce que tu proposes.\n"
    "- Une question qui ne modifie rien : une r\xC3\xA9ponse en texte, sans outil.\n"
    "- Une demande ambigu\xC3\xAB (quel objet ? quelle variable ? combien ? o\xC3\xB9 ?) : demande une pr\xC3\xA9" "cision au lieu de "
    "deviner. Impossible avec les genres et les propri\xC3\xA9t\xC3\xA9s du logiciel : dis-le.\n"
    "- Comprends les fautes de frappe, les abr\xC3\xA9viations et le vocabulaire industriel (\xC2\xAB bac \xC2\xBB : une cuve, \xC2\xAB TOR \xC2\xBB "
    ": un bool\xC3\xA9" "en, \xC2\xAB P1 \xC2\xBB : la pompe 1...).\n"
    "\n"
    "## Les r\xC3\xA8gles (strictes : le logiciel refuse ce qui ne les suit pas)\n"
    "1. Genres : seulement les cl\xC3\xA9s de la liste \xC2\xAB Genres d'objets \xC2\xBB (Tank, Pump, Valve, PushButton...). Jamais un "
    "genre invent\xC3\xA9.\n"
    "2. Propri\xC3\xA9t\xC3\xA9s : seulement celles du genre (les propri\xC3\xA9t\xC3\xA9s communes et le d\xC3\xA9tail des genres, plus bas). Les "
    "valeurs en texte, selon leur r\xC3\xB4le : un nombre avec un point d\xC3\xA9" "cimal, un bool\xC3\xA9" "en TRUE ou FALSE, une couleur "
    "#RRGGBB, un choix parmi les mots list\xC3\xA9s, tel quel.\n"
    "3. Variables : seulement celles du contexte (variables de l'IHM, de l'automate reli\xC3\xA9). Jamais un nom invent\xC3\xA9, "
    "m\xC3\xAAme plausible : si la variable voulue n'existe pas, dis-le et cite les plus proches ; l'utilisateur choisira "
    "une variable existante ou demandera sa cr\xC3\xA9" "ation (le logiciel la cr\xC3\xA9" "e alors, \xC3\xA0 sa demande). Une formule (un "
    "d\xC3\xA9" "faut, un retour d'\xC3\xA9tat, une condition) ne lit que des variables qui existent.\n"
    "4. Objets cit\xC3\xA9s (objet, objets) : des noms EXISTANTS de la vue, ou cr\xC3\xA9\xC3\xA9s plus haut dans la m\xC3\xAAme liste "
    "d'op\xC3\xA9rations. \xC2\xAB Cette pompe \xC2\xBB, \xC2\xAB celle de droite \xC2\xBB, \xC2\xAB les quatre boutons \xC2\xBB se r\xC3\xA9solvent en noms : la "
    "s\xC3\xA9lection d'abord, puis les positions (x cro\xC3\xAEt vers la droite, y vers le bas). Si deux objets conviennent, "
    "demande lequel.\n"
    "5. Noms neufs : uniques dans la vue, une lettre ou _, puis des lettres, chiffres ou _ (Cuve_1, Pompe_11, "
    "Marche_P11). Num\xC3\xA9rote dans l'ordre de lecture (de gauche \xC3\xA0 droite, de haut en bas).\n"
    "6. Coordonn\xC3\xA9" "es : en pixels dans la vue, l'origine en haut \xC3\xA0 gauche ; x, y : le coin haut-gauche de l'objet ; "
    "tout doit tenir dans la vue (sa taille est dans le contexte), sans recouvrir les objets existants, sauf si on "
    "te le demande.\n"
    "7. Mise en page : aligne ce qui va ensemble (m\xC3\xAAme y pour une rang\xC3\xA9" "e, m\xC3\xAAme x pour une colonne), espace "
    "r\xC3\xA9guli\xC3\xA8rement (20 \xC3\xA0 40 px), des tailles coh\xC3\xA9rentes (celles par d\xC3\xA9" "faut du genre pour commencer), un titre en "
    "haut \xC3\xA0 gauche, le proc\xC3\xA9" "d\xC3\xA9 dans le sens du fluide.\n"
    "8. De vraies valeurs : une cuve a min, max et unit coh\xC3\xA9rents (0, 100, %), un bouton un texte (Marche, Arr\xC3\xAAt), "
    "un libell\xC3\xA9 (label) court et parlant (P11, V1) ; les couleurs du th\xC3\xA8me sombre du logiciel (fond #20242B) ; "
    "#E5534B pour un d\xC3\xA9" "faut ou un arr\xC3\xAAt, #2ECC71 pour la marche.\n"
    "9. D\xC3\xA9placer, redimensionner : modifier x, y, w, h. Dupliquer : l'op\xC3\xA9ration dupliquer. Plusieurs suppressions "
    ": l'utilisateur confirmera.\n"
    "10. Jamais de code \xC3\xA0 ex\xC3\xA9" "cuter : seules les op\xC3\xA9rations de l'outil existent. Le code brut (op\xC3\xA9ration code) ne "
    "porte ni script ni appel de fonction.\n"
    "11. Une proposition refus\xC3\xA9" "e te revient en r\xC3\xA9sultat d'outil, avec les raisons : corrige-la et repropose (un "
    "nouvel appel), ou explique \xC3\xA0 l'utilisateur pourquoi c'est impossible.\n"
    "12. Tu n'es pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9 par l'utilisation : tu lis \xC3\xA0 chaque demande les connaissances jointes ci-dessous ; "
    "ne pr\xC3\xA9tends pas te souvenir au-del\xC3\xA0 d'elles et de la conversation.\n"
    "\n"
    "## Les op\xC3\xA9rations (l'outil proposer_operations)\n"
    "- creer : genre (une cl\xC3\xA9), nom (neuf), proprietes (x, y, w, h compris ; variable : une variable existante). "
    "Exemple : {\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_1\", \"proprietes\": {\"x\": \"80\", \"y\": \"120\", \"w\": "
    "\"150\", \"h\": \"200\", \"label\": \"Cuve 1\", \"max\": \"100\", \"unit\": \"%\"}}\n"
    "- modifier : objet (son nom), proprietes (celles qui changent). Exemple : {\"action\": \"modifier\", \"objet\": "
    "\"Pompe_12\", \"proprietes\": {\"colorOn\": \"#E5534B\"}}\n"
    "- associer : objet, variable (existante). Exemple : {\"action\": \"associer\", \"objet\": \"Pompe_11\", \"variable\": "
    "\"<le nom exact d'une variable du contexte>\"}\n"
    "- aligner : objets (deux ou plus), bord : haut, bas, gauche, droite, centre-h (les centres sur une m\xC3\xAAme "
    "verticale), centre-v (les centres sur une m\xC3\xAAme horizontale).\n"
    "- distribuer : objets (trois ou plus), sens : horizontal ou vertical (des \xC3\xA9" "carts \xC3\xA9gaux).\n"
    "- supprimer : objets.\n"
    "- dupliquer : objets, noms (ceux des copies, dans le m\xC3\xAAme ordre), dx, dy (le d\xC3\xA9" "calage en pixels). Exemple : "
    "{\"action\": \"dupliquer\", \"objets\": [\"Vanne_1\"], \"noms\": [\"Vanne_4\"], \"dx\": 0, \"dy\": 120}\n"
    "- code : code (le code brut d'objets, le format de l'onglet Code brut de l'inspecteur), pour un objet riche "
    "(ses actions) ; une propri\xC3\xA9t\xC3\xA9 absente garde sa valeur par d\xC3\xA9" "faut ; tout est v\xC3\xA9rifi\xC3\xA9 comme le reste.\n";

// L'exemple du code brut : un bouton qui ouvre une vue, ecrit par le logiciel lui-meme.
const std::string& codeExample() {
    static const std::string text = [] {
        hmi::View v;
        v.id = 1;
        v.name = "Exemple";
        hmi::Object o = hmi::makeObject(hmi::Kind::Button, 2, "Bouton_Accueil", 40, 40, hmi::kNoId);
        o.set("text", "Accueil");
        hmi::Action a;
        a.trigger = hmi::Trigger::Click;
        a.operation = hmi::Operation::Navigate;
        a.target = "Accueil";
        o.actions.push_back(a);
        v.objects.push_back(o);
        // Sans les proprietes laissees a leur valeur par defaut (une propriete absente la garde) : court.
        const hmi::Object base = hmi::makeObject(hmi::Kind::Button, 2, "Bouton_Accueil", 40, 40, hmi::kNoId);
        std::string out, line;
        const std::string full = hmi::serializeObjectCode(v, {o.id});
        for (std::size_t i = 0; i <= full.size(); ++i) {
            if (i < full.size() && full[i] != '\n') {
                line += full[i];
                continue;
            }
            bool keep = line.rfind("# ", 0) != 0;
            for (const auto& p : base.props)
                if (p.key != "x" && p.key != "y" && line == "prop cle=" + p.key + " valeur=\"" + p.value + "\"") keep = false;
            if (keep && !line.empty()) out += line + "\n";
            line.clear();
        }
        return out;
    }();
    return text;
}

// Les genres dont le detail part : ceux de la vue (si elle part), ceux que la demande evoque ;
// aucun : les plus courants.
std::vector<hmi::Kind> detailKinds(const PromptInput& in, bool useWords) {
    std::vector<hmi::Kind> out;
    const auto add = [&](hmi::Kind k) {
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    };
    for (const auto k : registry().evoked(in.request, useWords, 12)) add(k);
    if (in.view && in.settings.sendView) {
        std::size_t n = 0;
        for (const auto k : kindsInView(*in.view))
            if (n++ < 20) add(k);
    }
    if (out.empty())
        for (const auto k : {hmi::Kind::Text, hmi::Kind::Rectangle, hmi::Kind::Button, hmi::Kind::PushButton, hmi::Kind::Indicator,
                             hmi::Kind::NumericDisplay})
            add(k);
    return out;
}

const char* const kAddedHeader = "## Connaissances ajout\xC3\xA9" "es (actives dans ce projet)\n";
constexpr std::size_t kAddedBytes = 20000;   // les connaissances ajoutees, au plus

std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : std::string(sep)) + x;
    return s;
}

// La lecture stricte d'une operation de l'outil : les champs de chaque action.
struct Fields {
    Operation::Action             action;
    std::vector<std::string_view> allowed;    // en plus de "action"
    std::vector<std::string_view> required;
};
const std::vector<Fields>& fieldsOf() {
    using A = Operation::Action;
    static const std::vector<Fields> f = {
        {A::Create, {"genre", "nom", "proprietes"}, {"genre"}},
        {A::Modify, {"objet", "proprietes"}, {"objet", "proprietes"}},
        {A::Bind, {"objet", "variable"}, {"objet", "variable"}},
        {A::Align, {"objets", "bord"}, {"objets", "bord"}},
        {A::Distribute, {"objets", "sens"}, {"objets"}},
        {A::Remove, {"objets"}, {"objets"}},
        {A::Duplicate, {"objets", "noms", "dx", "dy"}, {"objets"}},
        {A::Code, {"code"}, {"code"}},
    };
    return f;
}

std::string actionList() {
    std::vector<std::string> keys;
    for (const auto& f : fieldsOf()) keys.emplace_back(actionKey(f.action));
    return join(keys, ", ");
}

// Une operation de l'outil est-elle bien formee ? Vide : oui ; sinon la raison, pour le modele.
std::string malformed(const json::Value& o, std::size_t n) {
    const std::string at = "Op\xC3\xA9ration n\xC2\xB0" + std::to_string(n);
    if (!o.isObject()) return at + " : ce n'est pas un objet JSON {\"action\": ...}.";
    if (!o.at("action").isString()) return at + " : \xC2\xAB action \xC2\xBB manque (" + actionList() + ").";
    const auto action = actionFromKey(o.str("action"));
    if (!action)
        return at + " refus\xC3\xA9" "e : \xC2\xAB " + o.str("action") + " \xC2\xBB n'est pas une op\xC3\xA9ration connue (" + actionList()
               + ") ; aucun code n'est ex\xC3\xA9" "cut\xC3\xA9.";
    const Fields* f = nullptr;
    for (const auto& x : fieldsOf())
        if (x.action == *action) f = &x;
    const std::string what = at + " (" + std::string(actionKey(*action)) + ")";
    for (const auto& [key, value] : o.members()) {
        if (key == "action") continue;
        if (std::find(f->allowed.begin(), f->allowed.end(), key) != f->allowed.end()) continue;
        std::vector<std::string> allowed;
        for (const auto a : f->allowed) allowed.emplace_back(a);
        return what + " : le champ \xC2\xAB " + key + " \xC2\xBB n'en fait pas partie (ses champs : " + join(allowed, ", ")
               + ") ; une propri\xC3\xA9t\xC3\xA9 d'objet va dans \xC2\xAB proprietes \xC2\xBB.";
    }
    for (const auto req : f->required)
        if (!o.has(req)) return what + " : \xC2\xAB " + std::string(req) + " \xC2\xBB manque.";
    for (const auto key : {"genre", "nom", "objet", "variable", "bord", "sens", "code"})
        if (o.has(key) && !o.at(key).isString()) return what + " : \xC2\xAB " + std::string(key) + " \xC2\xBB doit \xC3\xAAtre un texte.";
    for (const auto key : {"genre", "objet", "variable", "code"})
        if (o.has(key) && o.str(key).empty()) return what + " : \xC2\xAB " + std::string(key) + " \xC2\xBB est vide.";
    for (const auto key : {"objets", "noms"})
        if (o.has(key)) {
            const auto& list = o.at(key);
            if (!list.isArray()) return what + " : \xC2\xAB " + std::string(key) + " \xC2\xBB doit \xC3\xAAtre une liste de noms.";
            for (const auto& item : list.items())
                if (!item.isString() || item.asString().empty()) return what + " : \xC2\xAB " + std::string(key) + " \xC2\xBB ne contient que des noms (des textes).";
            if (key == std::string_view("objets") && list.items().empty()) return what + " : \xC2\xAB objets \xC2\xBB est vide.";
        }
    for (const auto key : {"dx", "dy"})
        if (o.has(key) && !o.at(key).isNumber()) return what + " : \xC2\xAB " + std::string(key) + " \xC2\xBB doit \xC3\xAAtre un nombre (des pixels).";
    if (o.has("proprietes")) {
        const auto& props = o.at("proprietes");
        if (!props.isObject()) return what + " : \xC2\xAB proprietes \xC2\xBB doit \xC3\xAAtre un objet {\"cle\": \"valeur\"}.";
        if (*action == Operation::Action::Modify && props.members().empty()) return what + " : \xC2\xAB proprietes \xC2\xBB est vide (rien \xC3\xA0 modifier).";
        for (const auto& [key, value] : props.members())
            if (!(value.isString() || value.isNumber() || value.isBool()))
                return what + " : la valeur de \xC2\xAB " + key + " \xC2\xBB doit \xC3\xAAtre un texte, un nombre ou un bool\xC3\xA9" "en.";
    }
    if (*action == Operation::Action::Align) {
        const std::string e = o.str("bord");
        if (e != "haut" && e != "bas" && e != "gauche" && e != "droite" && e != "centre-h" && e != "centre-v")
            return what + " : \xC2\xAB bord \xC2\xBB vaut haut, bas, gauche, droite, centre-h ou centre-v (pas \xC2\xAB " + e + " \xC2\xBB).";
    }
    if (*action == Operation::Action::Distribute && o.has("sens")) {
        const std::string e = o.str("sens");
        if (e != "horizontal" && e != "vertical") return what + " : \xC2\xAB sens \xC2\xBB vaut horizontal ou vertical (pas \xC2\xAB " + e + " \xC2\xBB).";
    }
    return {};
}

} // namespace

PromptParts promptParts(const PromptInput& in) {
    PromptParts out;
    const KindRegistry& reg = registry();
    // Les connaissances : celles qu'on a deja, sinon relues (les integrees et leur etat, celles ajoutees).
    memory::Registry local;
    const memory::Registry* knowledge = in.knowledge;
    if (!knowledge) {
        local.load();
        knowledge = &local;
    }
    const auto active = [&](std::string_view id) {
        const auto* k = knowledge->find(id);
        return k && memory::hereState(*k, in.projectName) == memory::Here::Active;
    };
    const auto* kinds = knowledge->find(memory::kKindsKnowledge);

    std::string& s = out.stable;
    s = kRules;
    s += "Le code brut d'un objet, tel que le logiciel l'\xC3\xA9" "crit (ici un bouton qui ouvre la vue Accueil) :\n";
    s += codeExample();
    if (!s.empty() && s.back() != '\n') s += '\n';

    s += "\n## Genres d'objets\n";
    if (active(memory::kKindsKnowledge)) s += kinds ? kinds->content : reg.kindsText();
    else
        s += "La liste des genres n'est pas jointe (la connaissance \xC2\xAB Genres d'objets IHM \xC2\xBB est d\xC3\xA9sactiv\xC3\xA9" "e dans le centre "
             "de m\xC3\xA9moire) : n'utilise que les cl\xC3\xA9s de l'\xC3\xA9num\xC3\xA9ration \xC2\xAB genre \xC2\xBB de l'outil.\n";
    const bool propsOn = active("logiciel.proprietes");
    s += "\n## Propri\xC3\xA9t\xC3\xA9s\n";
    if (propsOn) s += reg.commonText();
    else
        s += "Le d\xC3\xA9tail des propri\xC3\xA9t\xC3\xA9s n'est pas joint (la connaissance \xC2\xAB Propri\xC3\xA9t\xC3\xA9s de chaque genre \xC2\xBB est "
             "d\xC3\xA9sactiv\xC3\xA9" "e) : n'utilise que x, y, w, h et les propri\xC3\xA9t\xC3\xA9s que la vue montre ; le logiciel refuse les autres.\n";
    // Les autres connaissances integrees actives, telles quelles (le vocabulaire sert aussi a trouver les genres evoques).
    for (const char* id : {"logiciel.vocabulaire", "logiciel.actions", "logiciel.variables", "logiciel.modeles"})
        if (active(id))
            if (const auto* k = knowledge->find(id)) s += "\n## " + k->name + "\n" + k->content;
    // Les connaissances ajoutees : pour tous les projets, ou pour CE projet ; actives.
    std::string added;
    std::size_t cut = 0;
    for (const auto& k : knowledge->all()) {
        if (k.builtIn || memory::hereState(k, in.projectName) != memory::Here::Active) continue;
        std::string one = "### " + k.name + " (" + memory::categoryLabel(k.category) + (k.project.empty() ? "" : ", projet " + k.project) + ")\n";
        if (!k.description.empty()) one += k.description + "\n";
        one += k.content;
        if (!one.empty() && one.back() != '\n') one += '\n';
        if (added.size() + one.size() > kAddedBytes) {
            cut += one.size();
            continue;
        }
        added += one;
    }
    if (!added.empty() || cut > 0) {
        s += "\n";
        s += kAddedHeader;
        s += added;
        if (cut > 0) s += "(" + std::to_string(cut) + " octets de connaissances de plus ne sont pas joints : trop longues pour une demande.)\n";
    }

    std::string& d = out.dynamic;
    if (propsOn) {
        d += "\n## D\xC3\xA9tail des genres utiles (ceux de la vue, ceux que la demande \xC3\xA9voque)\n";
        d += "Une ligne par genre : cl\xC3\xA9 (libell\xC3\xA9) taille par d\xC3\xA9" "faut, capacit\xC3\xA9s : ses propri\xC3\xA9t\xC3\xA9s propres, cl\xC3\xA9(r\xC3\xB4le)=d\xC3\xA9" "faut "
             "(les communes sont plus haut). Pour un autre genre, ses propri\xC3\xA9t\xC3\xA9s sont celles de sa cr\xC3\xA9" "ation par d\xC3\xA9" "faut : "
             "en cas de doute, n'en mets que x, y, w, h.\n";
        for (const auto k : detailKinds(in, active("logiciel.vocabulaire")))
            if (const auto* e = reg.find(k)) d += reg.detailLine(*e);
    }
    d += "\n" + contextText(in);
    return out;
}

std::string systemPrompt(const PromptInput& in) {
    PromptParts p = promptParts(in);
    return p.stable + p.dynamic;
}

std::string operationsTool() {
    using json::Value;
    const auto str = [](std::string description) {
        Value v = Value::object();
        v.set("type", "string");
        v.set("description", std::move(description));
        return v;
    };
    const auto names = [](std::string description) {
        Value v = Value::object();
        v.set("type", "array");
        Value item = Value::object();
        item.set("type", "string");
        v.set("items", item);
        v.set("description", std::move(description));
        return v;
    };
    const auto choice = [](std::initializer_list<const char*> words, std::string description) {
        Value v = Value::object();
        v.set("type", "string");
        Value e = Value::array();
        for (const char* w : words) e.push(w);
        v.set("enum", e);
        v.set("description", std::move(description));
        return v;
    };
    Value item = Value::object();
    item.set("type", "object");
    Value props = Value::object();
    {
        Value actions = Value::array();
        for (const auto& f : fieldsOf()) actions.push(std::string(actionKey(f.action)));
        Value a = Value::object();
        a.set("type", "string");
        a.set("enum", actions);
                props.set("action", a);
    }
    {
        Value kinds = Value::array();
        for (const auto& info : hmi::kKindInfos) kinds.push(std::string(info.key));
        Value g = Value::object();
        g.set("type", "string");
        g.set("enum", kinds);
        g.set("description", "creer : la cl\xC3\xA9 du genre.");
        props.set("genre", g);
    }
    props.set("nom", str("creer : un nom neuf."));
    props.set("objet", str("modifier, associer : un objet existant."));
    props.set("objets", names("aligner, distribuer, supprimer, dupliquer : des objets existants."));
    {
        Value p = Value::object();
        p.set("type", "object");
        Value any = Value::object();
        any.set("type", "string");
        p.set("additionalProperties", any);
        p.set("description", "creer, modifier : cl\xC3\xA9 du genre -> valeur en texte.");
        props.set("proprietes", p);
    }
    props.set("variable", str("associer : une variable EXISTANTE du contexte."));
    props.set("bord", choice({"haut", "bas", "gauche", "droite", "centre-h", "centre-v"}, "aligner."));
    props.set("sens", choice({"horizontal", "vertical"}, "distribuer."));
    props.set("noms", names("dupliquer : les noms neufs des copies."));
    {
        Value n = Value::object();
        n.set("type", "number");
        n.set("description", "dupliquer : le d\xC3\xA9" "calage en x (px).");
        props.set("dx", n);
        n.set("description", "dupliquer : le d\xC3\xA9" "calage en y (px).");
        props.set("dy", n);
    }
    props.set("code", str("code : le code brut d'objets, sans script."));
    item.set("properties", props);
    Value req = Value::array();
    req.push("action");
    item.set("required", req);

    Value schema = Value::object();
    schema.set("type", "object");
    Value top = Value::object();
    top.set("resume", str("Le libell\xC3\xA9 court de la proposition (Station de pompage)."));
    Value ops = Value::object();
    ops.set("type", "array");
    ops.set("minItems", 1);
    ops.set("items", item);
    ops.set("description", "Dans l'ordre : une op\xC3\xA9ration peut citer un objet cr\xC3\xA9\xC3\xA9 plus haut.");
    top.set("operations", ops);
    schema.set("properties", top);
    Value required = Value::array();
    required.push("operations");
    schema.set("required", required);

    Value tool = Value::object();
    tool.set("name", std::string(kOperationsTool));
    tool.set("description",
             "Propose des modifications de la vue active d'IHM Studio. Appelle-le pour TOUTE demande qui modifie la vue (cr\xC3\xA9" "er, "
             "modifier, d\xC3\xA9placer, redimensionner, colorer, associer une variable, aligner, r\xC3\xA9partir, dupliquer, supprimer), "
             "avec toutes les op\xC3\xA9rations en un seul appel. Le logiciel v\xC3\xA9rifie chaque op\xC3\xA9ration (genres, propri\xC3\xA9t\xC3\xA9s, "
             "valeurs, objets, variables) et montre un aper\xC3\xA7u : rien ne change avant que l'utilisateur applique (un Ctrl+Z annule "
             "tout). N'invente ni genre, ni propri\xC3\xA9t\xC3\xA9, ni variable : seulement ceux du registre et du contexte.");
    tool.set("input_schema", schema);
    Value tools = Value::array();
    tools.push(tool);
    return json::write(tools, false);
}

std::optional<Proposal> proposalFromTool(std::string_view inputJson, const PromptInput& in, std::string_view request,
                                         std::string* why) {
    const auto refuse = [&](std::string text) -> std::optional<Proposal> {
        if (why) *why = std::move(text);
        return std::nullopt;
    };
    json::Value v;
    std::string err;
    if (!json::parse(inputJson, v, &err)) return refuse("L'entr\xC3\xA9" "e de l'outil est illisible (" + err + ") : rien n'est propos\xC3\xA9.");
    if (!v.isObject()) return refuse("L'entr\xC3\xA9" "e de l'outil doit \xC3\xAAtre un objet {\"resume\": ..., \"operations\": [...]}.");
    if (!in.view) return refuse("Aucune vue n'est ouverte dans l'\xC3\xA9" "diteur : la proposition ne vise rien. Demande d'ouvrir une vue.");
    const json::Value* ops = v.find("operations");
    if (!ops) return refuse("\xC2\xAB operations \xC2\xBB manque : l'entr\xC3\xA9" "e de l'outil est {\"resume\": ..., \"operations\": [...]}.");
    if (!ops->isArray()) return refuse("\xC2\xAB operations \xC2\xBB doit \xC3\xAAtre une liste.");
    if (ops->items().empty()) return refuse("\xC2\xAB operations \xC2\xBB est vide : rien \xC3\xA0 proposer.");
    std::size_t n = 0;
    for (const auto& o : ops->items())
        if (auto bad = malformed(o, ++n); !bad.empty()) return refuse(std::move(bad));
    Proposal p;
    if (!operationsFromJson(*ops, p.ops, why)) return std::nullopt;
    p.id = newId("op");
    p.request = std::string(request);
    p.summary = v.at("resume").isString() ? v.str("resume") : std::string{};
    if (p.summary.empty()) p.summary = "Proposition de l'Assistant";
    p.view = in.view->name;
    p.viewId = in.view->id;
    p.fingerprint = fingerprint(*in.view);
    return p;
}

std::string feedback(const Check& check, const Proposal& proposal) {
    if (check.ok()) return {};
    std::string s = "Proposition refus\xC3\xA9" "e par le logiciel (rien n'a \xC3\xA9t\xC3\xA9 appliqu\xC3\xA9) :\n";
    for (const auto& e : check.errors) s += "- " + e + "\n";
    if (!check.missingVariable.empty()) {
        s += "La variable \xC2\xAB " + check.missingVariable + " \xC2\xBB n'existe pas";
        if (!check.nearVariables.empty()) s += " ; les plus proches : " + join(check.nearVariables, ", ");
        s += ". Ne l'invente pas : propose avec une variable existante, ou laisse l'utilisateur choisir (ou demander sa cr\xC3\xA9" "ation).\n";
    }
    // Le detail des genres que les erreurs citent (par leur libelle ou leur cle) : de quoi corriger.
    const KindRegistry& reg = registry();
    std::vector<const KindEntry*> cited;
    std::set<hmi::Kind> candidates;
    for (const auto& it : check.items) candidates.insert(it.kind);
    for (const auto& op : proposal.ops) {
        if (op.action != Operation::Action::Create) continue;
        if (const auto* e = reg.find(op.kind)) candidates.insert(e->kind);
        else
            for (const auto& near : reg.nearKinds(op.kind))
                if (const auto* e2 = reg.find(near.substr(0, near.find(' ')))) candidates.insert(e2->kind);
    }
    for (const auto k : candidates) {
        const auto* e = reg.find(k);
        if (!e) continue;
        const bool named = std::any_of(check.errors.begin(), check.errors.end(), [&](const std::string& err) {
            return err.find(" " + e->label + " ") != std::string::npos || err.find(" " + e->label + ")") != std::string::npos
                || err.find("(" + e->label + ")") != std::string::npos || err.find(e->key + " (") != std::string::npos
                || err.find(" " + e->label + " :") != std::string::npos;
        });
        if (named) cited.push_back(e);
    }
    if (!cited.empty()) {
        s += "Les propri\xC3\xA9t\xC3\xA9s des genres en cause (cl\xC3\xA9(r\xC3\xB4le)=d\xC3\xA9" "faut ; plus les communes x, y, w, h, rot, visible, opacity, "
             "variable...) :\n";
        for (const auto* e : cited) s += reg.detailLine(*e);
    }
    s += "Corrige et repropose (un nouvel appel de proposer_operations), ou explique \xC3\xA0 l'utilisateur ce qui manque.";
    return s;
}

} // namespace app::ai
