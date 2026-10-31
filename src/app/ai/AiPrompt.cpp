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
#include "AiDelete.hpp"     // 1.21.0 : supprimer - les genres et les strategies (le schema, la lecture stricte)
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
    "la fin) - ou, si l'utilisateur le demande, une VUE NEUVE, des VARIABLES IHM, des TYPES IHM - en appelant l'outil proposer_operations. Tu ne modifies rien toi-m\xC3\xAAme : le logiciel v\xC3\xA9rifie chaque "
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
    ": l'utilisateur confirmera ; un \xC3\xA9l\xC3\xA9ment du projet (une vue, une variable, un type...) : il confirme toujours.\n"
    "10. Jamais de code \xC3\xA0 ex\xC3\xA9" "cuter : seules les op\xC3\xA9rations de l'outil existent. Le code brut (op\xC3\xA9ration code) ne "
    "porte ni script ni appel de fonction.\n"
    "11. Une proposition refus\xC3\xA9" "e te revient en r\xC3\xA9sultat d'outil, avec les raisons : corrige-la et repropose (un "
    "nouvel appel), ou explique \xC3\xA0 l'utilisateur pourquoi c'est impossible.\n"
    "12. Tu n'es pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9 par l'utilisation : tu lis \xC3\xA0 chaque demande les connaissances jointes ci-dessous ; "
    "ne pr\xC3\xA9tends pas te souvenir au-del\xC3\xA0 d'elles et de la conversation.\n"
    "13. Cr\xC3\xA9" "er une vue, des variables, des types IHM : SEULEMENT si l'utilisateur le demande (ou accepte ta proposition "
    "de le faire). Jamais pour masquer une variable absente : si une variable manque, dis-le d'abord. Les variables et les "
    "types cr\xC3\xA9\xC3\xA9s se voient dans l'aper\xC3\xA7u avant d'appliquer. Les variables et les types (DDT) de l'automate reli\xC3\xA9 "
    "(dans le contexte) servent de mod\xC3\xA8le : m\xC3\xAAmes noms, types convertis (EBOOL -> BOOL, STRING[n] -> STRING, un DDT -> un type IHM "
    "du m\xC3\xAAme nom, cr\xC3\xA9\xC3\xA9 avant les variables qui l'emploient).\n"
    "14. Formules (fx) : dans creer ou modifier, une valeur qui commence par = pilote la propri\xC3\xA9t\xC3\xA9 (comme =... dans l'inspecteur) : "
    "du ST en lecture seule, qui ne lit que des variables existantes (ou cr\xC3\xA9\xC3\xA9" "es plus haut). Pour animer : \"visible\": "
    "\"=Defaut_P1\", \"fill\": \"=SEL(Marche_P1, '#5B6270', '#2ECC71')\" (SEL(condition, si FAUX, si VRAI) ; les couleurs entre "
    "apostrophes).\n"
    "15. Rep\xC3\xA8res $...$ : un morceau entour\xC3\xA9 de $ ($Cuves[0]$.Niveau) se lit comme sans les $, et avance d'une copie \xC3\xA0 l'autre "
    "quand on duplique avec reperes (Cuves[0] -> Cuves[1] -> Cuves[2], comme Dupliquer... Ctrl+Maj+D). Pour poser une s\xC3\xA9rie "
    "d'objets semblables : cr\xC3\xA9" "e le premier avec ses rep\xC3\xA8res, puis dupliquer avec copies et reperes.\n"
    "16. Bac \xC3\xA0 sable (1.21.0, quand l'outil executer_code est disponible) : avant de proposer une fonction ST (creer_fonction), "
    "essaie-la avec executer_code (son code et des cas d'essai : arguments, attendu) ; lis le compte rendu R\xC3\x89" "EL de chaque "
    "\xC3\xA9tape (analyse, compilation, ex\xC3\xA9" "cution des cas) ; corrige et relance tant qu'une \xC3\xA9tape \xC3\xA9" "choue (5 essais au "
    "plus). Dis ensuite exactement ce qui a \xC3\xA9t\xC3\xA9 fait : g\xC3\xA9n\xC3\xA9r\xC3\xA9, compil\xC3\xA9, ex\xC3\xA9" "cut\xC3\xA9, tests r\xC3\xA9ussis (N/M) - "
    "jamais \xC2\xAB \xC3\xA7" "a marche \xC2\xBB parce que \xC3\xA7" "a compile, jamais un essai qui n'a pas eu lieu. Le C et le C++ ne sont que relus "
    "(jamais compil\xC3\xA9s ni ex\xC3\xA9" "cut\xC3\xA9s) : dis-le. Rien n'est ex\xC3\xA9" "cut\xC3\xA9 sur un automate r\xC3\xA9" "el.\n"
    "17. Travailler en plusieurs \xC3\xA9tapes (1.21.0, quand l'outil analyser_projet est disponible) : pour une demande large (v\xC3\xA9rifier, "
    "nettoyer, supprimer, r\xC3\xA9organiser), encha\xC3\xAEne seul les \xC3\xA9tapes qui ne changent rien - analyser_projet (orphelins, impact "
    "d'une suppression, qui utilise quoi, verifier), executer_code, resultat_detection - puis propose les modifications avec proposer_operations : "
    "l'utilisateur valide avant que rien ne change (une suppression toujours). Une d\xC3\xA9tection r\xC3\xA9seau "
    "(detecter_equipements) n'est jamais lanc\xC3\xA9" "e par toi : elle ouvre la confirmation, l'utilisateur lance. Ton compte rendu suit les r\xC3\xA9sultats r\xC3\xA9" "els : "
    "ce qui a \xC3\xA9t\xC3\xA9 analys\xC3\xA9, ce qui est propos\xC3\xA9, ce qui attend la validation ; jamais une \xC3\xA9tape suppos\xC3\xA9" "e faite.\n"
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
    "- supprimer : objets (les noms) ; sans genre : des objets de la vue. 1.21.0 : genre = ce qui part - objet, instance (une "
    "instance de symbole : le symbole et ses autres instances restent), vue, popup, symbole (SA D\xC3\x89" "FINITION : toutes ses instances "
    "en d\xC3\xA9pendent), modele, variable, type, alarme, script, fonction, equipement, ressource, dossier (+ liste). strategie : seul "
    "(par d\xC3\xA9" "faut ; refus\xC3\xA9" "e si quelque chose en d\xC3\xA9pend - le logiciel dit quoi), remplacer + remplacer_par (un \xC3\xA9l\xC3\xA9ment "
    "EXISTANT du m\xC3\xAAme genre, jamais invent\xC3\xA9), references (il part avec SEULEMENT les r\xC3\xA9" "f\xC3\xA9rences qui le citent : propri\xC3\xA9t\xC3\xA9s "
    "vid\xC3\xA9" "es, actions retir\xC3\xA9" "es, instances retir\xC3\xA9" "es, variables d\xC3\xA9li\xC3\xA9" "es ; du code qui le cite : refus\xC3\xA9). inutiles: true : "
    "aussi ce qui devient inutile (popup, script sur appel, fonction, ressource ; jamais une variable ni une d\xC3\xA9" "finition). exclure : "
    "les noms \xC3\xA0 garder. Supprimer un objet garde sa variable ; une vue garde les scripts qu'elle appelle. Le logiciel analyse les "
    "d\xC3\xA9pendances (directes, indirectes, potentielles, partag\xC3\xA9" "es, orphelins), v\xC3\xA9rifie sur une copie, montre tout et demande "
    "confirmation. Exemple : {\"action\": \"supprimer\", \"genre\": \"type\", \"objets\": [\"T_Pompe\"], \"strategie\": \"remplacer\", "
    "\"remplacer_par\": \"T_Pompe_V2\"}\n"
    "- dupliquer : objets, noms (ceux des copies, dans le m\xC3\xAAme ordre), dx, dy (le d\xC3\xA9" "calage en pixels). Exemple : "
    "{\"action\": \"dupliquer\", \"objets\": [\"Vanne_1\"], \"noms\": [\"Vanne_4\"], \"dx\": 0, \"dy\": 120}\n"
    "- code : code (le code brut d'objets, le format de l'onglet Code brut de l'inspecteur), pour un objet riche "
    "(ses actions) ; une propri\xC3\xA9t\xC3\xA9 absente garde sa valeur par d\xC3\xA9" "faut ; tout est v\xC3\xA9rifi\xC3\xA9 comme le reste.\n"
    "- dupliquer, en plus : copies (1 \xC3\xA0 50 ; la copie k est d\xC3\xA9" "cal\xC3\xA9" "e de k fois dx, dy), reperes (true : les rep\xC3\xA8res "
    "$...$ \xC3\xA0 indices avancent comme Ctrl+Maj+D, les noms suivent la s\xC3\xA9rie). Exemple : {\"action\": \"dupliquer\", \"objets\": "
    "[\"Cuve_1\"], \"copies\": 3, \"reperes\": true, \"dx\": 180, \"dy\": 0}\n"
    "- creer_vue : nom (neuf, unique), proprietes (largeur, hauteur en px, fond #RRGGBB, description). Les objets qui suivent "
    "visent la vue neuve (y cr\xC3\xA9" "er les objets) ; elle s'ouvre apr\xC3\xA8s Appliquer. Exemple : "
    "{\"action\": \"creer_vue\", \"nom\": \"Vue_Pompes\", \"proprietes\": {\"largeur\": \"1280\", \"hauteur\": \"800\", \"description\": "
    "\"Les pompes du poste\"}}\n"
    // 1.19.0 : plusieurs vues.
    "  Plusieurs creer_vue possibles : un objet va dans sa \"vue\", sinon la derni\xC3\xA8re creer_vue au-dessus, sinon la vue ouverte.\n"
    "- creer_variables : variables (nom neuf, type, description, valeur initiale). Types : BOOL, INT, DINT, REAL, STRING, "
    "TIME, ARRAY[0..9] OF REAL, ou un type IHM du projet (ou cr\xC3\xA9\xC3\xA9 dans la proposition). Les autres op\xC3\xA9rations peuvent les citer. "
    "M\xC3\xAAme nom et m\xC3\xAAme type : r\xC3\xA9utilis\xC3\xA9" "e ; autre type : refus\xC3\xA9" "e. "
    "Exemple : {\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"Marche_P1\", \"type\": \"BOOL\", \"description\": "
    "\"Marche pompe 1\"}, {\"nom\": \"Consigne_Niveau\", \"type\": \"REAL\", \"valeur\": \"75\"}]}\n"
    "- creer_type : nom (T_...), description, membres (nom, type, description) ; un membre d'un autre type IHM : ce type "
    "existe ou est cr\xC3\xA9\xC3\xA9 dans la proposition (identique : r\xC3\xA9utilis\xC3\xA9). Exemple : {\"action\": \"creer_type\", \"nom\": \"T_Pompe\", "
    "\"membres\": [{\"nom\": \"Marche\", \"type\": \"BOOL\"}, {\"nom\": \"Vitesse\", \"type\": \"REAL\", \"description\": \"tr/min\"}]}\n"
    // 1.18.0 : les outils de l'IA.
    "- creer_type + generiques ([\"T\"], [\"T\", \"K : ANY_NUM\"]) : une structure g\xC3\xA9n\xC3\xA9rique (membres en T, LIST OF T).\n"
    "- creer_vue + proprietes.role (popup, symbole, modele, entete, pied ; une popup : titre, modale) ; symbole, popup : parametres "
    "[{nom, type, valeur}] et generiques. creer_symbole, creer_popup, creer_modele_vue : idem. Les objets qui suivent y vont.\n"
    "- creer_equipement : nom, type (modbus_tcp, ethernet_tcp), adresse (IPv4), port, esclave, periode ; detecter: true PROPOSE la "
    "d\xC3\xA9tection (l'utilisateur la lance ; toi, jamais).\n"
    "- adresser : variable (IHM), equipement (Modbus), adresse (%MW100, %M5, 40101 ; absente : la premi\xC3\xA8re libre) ; un tableau "
    "de structures : formule (%MW(1000 + 10 * i)) et motif ([*].Retour) ; un membre, a toute profondeur : variable "
    "Config.Sous.Valeur (son adresse propre, dans la structure).\n"
    "- creer_script : nom, langage (ST, C, C++), evenement (Demarrage, Cyclique + periode, Changement + surveille, Appel), corps. "
    "creer_fonction : nom, retour, generiques, corps (VAR_INPUT ... END_VAR, puis Nom := r\xC3\xA9sultat). Compil\xC3\xA9s, jamais ex\xC3\xA9" "cut\xC3\xA9s ici.\n"
    "- creer_alarme : nom, condition (variables existantes), message, priorite (1-4), groupe, delai (ms), consigne, incidents (false : aucun dossier d'incident).\n"
    "- creer_ressource : piece_jointe (un fichier joint \xC3\xA0 la conversation). creer_dossier : liste (vues, popups, symboles, "
    "ecrans_modeles, entetes, pieds, scripts, types, styles, ressources, variables), dossier. ranger : liste, dossier, objets.\n"
    // 1.19.0 : concevoir une installation complete.
    "INSTALLATION COMPL\xC3\x88TE (station, ligne, cuves, r\xC3\xA9gulation) : UNE proposition avec tout (types, variables, \xC3\xA9quipements, "
    "adresses, alarmes, scripts, symboles, popups, vues), dans n'importe quel ordre : le logiciel l'ordonne, la v\xC3\xA9rifie et "
    "l'applique d'un seul Ctrl+Z. R\xC3\xA9utilise l'existant ; une s\xC3\xA9rie suit un mod\xC3\xA8le. Modifier en masse : dis combien "
    "d'\xC3\xA9l\xC3\xA9ments. Ne promets que ce que contient la proposition.\n";

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
        // 1.21.0 : "vue" (la vue visee, annoncee par le schema depuis la 1.19.0) n'est plus refusee.
        {A::Create, {"genre", "nom", "proprietes", "vue"}, {"genre"}},
        {A::Modify, {"objet", "proprietes", "vue"}, {"objet", "proprietes"}},
        {A::Bind, {"objet", "variable", "vue"}, {"objet", "variable"}},
        {A::Align, {"objets", "bord", "vue"}, {"objets", "bord"}},
        {A::Distribute, {"objets", "sens", "vue"}, {"objets"}},
        // 1.21.0 : la suppression intelligente - le genre, la vue des objets, la strategie, le remplacant, les exclusions.
        {A::Remove, {"objets", "genre", "vue", "strategie", "remplacer_par", "inutiles", "exclure", "liste", "proprietes"}, {"objets"}},
        {A::Duplicate, {"objets", "noms", "dx", "dy", "copies", "reperes", "vue"}, {"objets"}},
        {A::Code, {"code", "vue"}, {"code"}},
        // 1.16.0 : la vue neuve, les variables, les types IHM.
        {A::View, {"nom", "proprietes", "parametres", "generiques", "description"}, {"nom"}},
        {A::Variables, {"variables"}, {"variables"}},
        {A::Type, {"nom", "description", "membres", "generiques"}, {"nom", "membres"}},
        // 1.18.0 : les outils de l'IA.
        {A::Equipment, {"nom", "type", "adresse", "port", "esclave", "periode", "simule", "description", "detecter", "proprietes"}, {"nom"}},
        {A::Address, {"variable", "equipement", "adresse", "formule", "motif", "lecture_seule", "proprietes"}, {"variable", "equipement"}},
        {A::Script, {"nom", "langage", "evenement", "periode", "surveille", "corps", "description", "dossier", "proprietes"}, {"nom", "corps"}},
        {A::Function, {"nom", "retour", "corps", "description", "generiques", "proprietes"}, {"nom", "corps"}},
        {A::Alarm, {"nom", "condition", "message", "priorite", "categorie", "groupe", "delai", "acquittement", "incidents", "consigne", "description", "proprietes"},
         {"nom", "condition"}},
        {A::Resource, {"nom", "piece_jointe", "dossier", "proprietes"}, {"piece_jointe"}},
        {A::Folder, {"liste", "dossier", "proprietes"}, {"liste", "dossier"}},
        {A::Store, {"liste", "dossier", "objets", "proprietes"}, {"liste", "dossier", "objets"}},
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
    // 1.16.0 : la vue neuve, le type (un nom non vide), les copies, les reperes, les variables, les membres.
    if ((*action == Operation::Action::View || *action == Operation::Action::Type) && o.str("nom").empty())
        return what + " : \xC2\xAB nom \xC2\xBB est vide.";
    if (o.has("description") && !o.at("description").isString()) return what + " : \xC2\xAB description \xC2\xBB doit \xC3\xAAtre un texte.";
    if (o.has("copies")) {
        const auto& c = o.at("copies");
        if (!c.isNumber() || c.asNumber() != static_cast<double>(static_cast<long long>(c.asNumber())) || c.asNumber() < 1 || c.asNumber() > 50)
            return what + " : \xC2\xAB copies \xC2\xBB est un nombre entier de 1 \xC3\xA0 50.";
    }
    if (o.has("reperes") && !o.at("reperes").isBool()) return what + " : \xC2\xAB reperes \xC2\xBB vaut true ou false.";
    // 1.21.0 : supprimer - un genre, une strategie connus ; les exclusions : des noms ; inutiles : un booleen.
    if (*action == Operation::Action::Remove) {
        if (o.has("genre") && !del::genreFromKey(o.str("genre")))
            return what + " : genre \xC2\xAB " + o.str("genre") + " \xC2\xBB inconnu (objet, instance, vue, popup, symbole, modele, variable, type, alarme, "
                          "script, fonction, equipement, ressource, dossier).";
        if (o.has("strategie") && (!o.at("strategie").isString() || !del::strategyFromKey(o.str("strategie"))))
            return what + " : \xC2\xAB strategie \xC2\xBB vaut seul, remplacer, references ou inutiles.";
        if (o.has("remplacer_par") && (!o.at("remplacer_par").isString() || o.str("remplacer_par").empty()))
            return what + " : \xC2\xAB remplacer_par \xC2\xBB est le nom d'un \xC3\xA9l\xC3\xA9ment EXISTANT du m\xC3\xAAme genre.";
        if (o.has("inutiles") && !o.at("inutiles").isBool()) return what + " : \xC2\xAB inutiles \xC2\xBB vaut true ou false.";
        if (o.has("exclure")) {
            const auto& list = o.at("exclure");
            if (!list.isArray()) return what + " : \xC2\xAB exclure \xC2\xBB est une liste de noms.";
            for (const auto& item : list.items())
                if (!item.isString() || item.asString().empty()) return what + " : \xC2\xAB exclure \xC2\xBB ne contient que des noms (des textes).";
        }
        if (o.has("vue") && !o.at("vue").isString()) return what + " : \xC2\xAB vue \xC2\xBB est un nom de vue.";
    }
    const auto entries = [&](std::string_view key, std::initializer_list<std::string_view> allowedKeys, bool withValue) -> std::string {
        const auto& list = o.at(key);
        const std::string k(key);
        if (!list.isArray()) return what + " : \xC2\xAB " + k + " \xC2\xBB doit \xC3\xAAtre une liste d'objets {\"nom\": ..., \"type\": ...}.";
        if (list.items().empty()) return what + " : \xC2\xAB " + k + " \xC2\xBB est vide.";
        std::size_t i = 0;
        for (const auto& e : list.items()) {
            ++i;
            const std::string at2 = what + ", " + k + " n\xC2\xB0" + std::to_string(i);
            if (!e.isObject()) return at2 + " : ce n'est pas un objet {\"nom\": ..., \"type\": ...}.";
            for (const auto& [mk, mv] : e.members()) {
                if (std::find(allowedKeys.begin(), allowedKeys.end(), mk) == allowedKeys.end()) {
                    std::vector<std::string> names;
                    for (const auto a : allowedKeys) names.emplace_back(a);
                    return at2 + " : le champ \xC2\xAB " + mk + " \xC2\xBB n'en fait pas partie (ses champs : " + join(names, ", ") + ").";
                }
                if (mk == "valeur" && withValue) {
                    if (!(mv.isString() || mv.isNumber() || mv.isBool())) return at2 + " : \xC2\xAB valeur \xC2\xBB est un texte, un nombre ou un bool\xC3\xA9" "en.";
                } else if (!mv.isString()) {
                    return at2 + " : \xC2\xAB " + mk + " \xC2\xBB doit \xC3\xAAtre un texte.";
                }
            }
            for (const char* req : {"nom", "type"})
                if (e.str(req).empty()) return at2 + " : \xC2\xAB " + std::string(req) + " \xC2\xBB manque ou est vide.";
        }
        return {};
    };
    if (o.has("variables"))
        if (auto bad = entries("variables", {"nom", "type", "description", "valeur"}, true); !bad.empty()) return bad;
    if (o.has("membres"))
        if (auto bad = entries("membres", {"nom", "type", "description"}, false); !bad.empty()) return bad;
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
        // 1.21.0 : supprimer - ce qui part (un genre d'element du projet, pas une cle d'objet).
        for (int k = 0; k <= static_cast<int>(del::Genre::Folder); ++k) kinds.push(std::string(del::genreKey(static_cast<del::Genre>(k))));
        Value g = Value::object();
        g.set("type", "string");
        g.set("enum", kinds);
        g.set("description", "creer : la cl\xC3\xA9 du genre ; supprimer : ce qui part (objet, instance, vue, popup, symbole, modele, variable, type, "
                             "alarme, script, fonction, equipement, ressource, dossier) - absent : des objets.");
        props.set("genre", g);
    }
    props.set("nom", str("creer : un nom neuf ; creer_vue : le nom de la vue neuve ; creer_type : le nom du type IHM (T_...) ; creer_equipement, "
                         "creer_script, creer_fonction, creer_alarme, creer_ressource : le nom neuf."));
    props.set("objet", str("modifier, associer : un objet existant."));
    // 1.19.0 : la vue visee par un objet (plusieurs vues dans une proposition).
    props.set("vue", str("un objet : sa vue ; absente : la derni\xC3\xA8re creer_vue au-dessus, sinon la vue ouverte."));
    props.set("objets", names("aligner, distribuer, supprimer, dupliquer : des objets existants ; ranger : les \xC3\xA9l\xC3\xA9ments \xC3\xA0 ranger ; "
                              "supprimer + genre : les \xC3\xA9l\xC3\xA9ments existants de ce genre."));
    // 1.21.0 : la suppression intelligente.
    props.set("strategie", choice({"seul", "remplacer", "references", "inutiles"},
                                  "supprimer : seul (refus\xC3\xA9 si quelque chose en d\xC3\xA9pend), remplacer (+ remplacer_par), references (retire "
                                  "seulement les r\xC3\xA9" "f\xC3\xA9rences), inutiles (+ ce qui devient inutile)."));
    props.set("remplacer_par", str("supprimer, strategie remplacer : un \xC3\xA9l\xC3\xA9ment EXISTANT du m\xC3\xAAme genre (jamais invent\xC3\xA9)."));
    props.set("exclure", names("supprimer : les \xC3\xA9l\xC3\xA9ments \xC3\xA0 garder (jamais supprim\xC3\xA9s ni modifi\xC3\xA9s)."));
    {
        Value b = Value::object();
        b.set("type", "boolean");
        b.set("description", "supprimer : true - aussi ce qui devient inutile (popup, script sur appel, fonction, ressource).");
        props.set("inutiles", b);
    }
    {
        Value p = Value::object();
        p.set("type", "object");
        Value any = Value::object();
        any.set("type", "string");
        p.set("additionalProperties", any);
        p.set("description", "creer, modifier : cl\xC3\xA9 du genre -> valeur en texte (=... : une formule fx) ; creer_vue : largeur, "
                             "hauteur, fond, description.");
        props.set("proprietes", p);
    }
    props.set("variable", str("associer : une variable EXISTANTE du contexte ; adresser : la variable IHM \xC3\xA0 lier."));
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
    // 1.16.0 : les copies et les reperes (dupliquer), les variables, le type IHM.
    {
        Value n = Value::object();
        n.set("type", "integer");
        n.set("minimum", 1);
        n.set("maximum", 50);
        n.set("description", "dupliquer : le nombre de copies (la copie k d\xC3\xA9" "cal\xC3\xA9" "e de k fois dx, dy).");
        props.set("copies", n);
        Value b = Value::object();
        b.set("type", "boolean");
        b.set("description", "dupliquer : true - les rep\xC3\xA8res $...$ \xC3\xA0 indices avancent d'une copie \xC3\xA0 l'autre (Ctrl+Maj+D).");
        props.set("reperes", b);
    }
    const auto entries = [&](std::initializer_list<const char*> keys, std::string description) {
        Value e = Value::object();
        e.set("type", "object");
        Value fields = Value::object();
        for (const char* k : keys) fields.set(k, str(std::string(k) == "valeur" ? "la valeur initiale (selon le type ; vide : 0, FALSE, '')." : std::string(k) + "."));
        e.set("properties", fields);
        Value required = Value::array();
        required.push("nom");
        required.push("type");
        e.set("required", required);
        e.set("additionalProperties", false);
        Value v = Value::object();
        v.set("type", "array");
        v.set("items", e);
        v.set("description", std::move(description));
        return v;
    };
    props.set("variables", entries({"nom", "type", "description", "valeur"},
                                   "creer_variables : les variables IHM neuves (type : BOOL, INT, REAL, STRING, TIME, ARRAY[a..b] OF ..., "
                                   "un type IHM)."));
    props.set("membres", entries({"nom", "type", "description"}, "creer_type : les membres du type (un type IHM cit\xC3\xA9 : existant ou cr\xC3\xA9\xC3\xA9 plus haut)."));
    props.set("description", str("creer_type, creer_equipement, creer_script, creer_fonction, creer_alarme : la description."));
    // 1.18.0 : les outils de l'IA - leurs champs (des textes ; un nombre peut s'ecrire en texte).
    props.set("generiques", names("creer_type, creer_vue, creer_fonction."));
    {
        Value e = Value::object();
        e.set("type", "object");
        Value fields = Value::object();
        for (const char* k : {"nom", "type", "description", "valeur"}) fields.set(k, str(std::string(k) == "valeur" ? "la valeur par d\xC3\xA9" "faut." : std::string(k) + "."));
        e.set("properties", fields);
        Value required = Value::array();
        required.push("nom");
        e.set("required", required);
        e.set("additionalProperties", false);
        Value v = Value::object();
        v.set("type", "array");
        v.set("items", e);
        v.set("description", "creer_vue (symbole, popup).");
        props.set("parametres", v);
        Value b = Value::object();
        b.set("type", "boolean");
        b.set("description", "creer_equipement : proposer la d\xC3\xA9tection (l'utilisateur la lance).");
        props.set("detecter", b);
    }
    for (const auto& [k, d] : std::initializer_list<std::pair<const char*, const char*>>{
             {"type", "creer_equipement."}, {"adresse", "creer_equipement (IPv4), adresser (%MW100)."}, {"port", "creer_equipement."},
             {"esclave", "creer_equipement."}, {"periode", "ms."}, {"simule", "creer_equipement."}, {"equipement", "adresser."},
             {"formule", "adresser."}, {"motif", "adresser."}, {"lecture_seule", "adresser."}, {"langage", "creer_script."},
             {"evenement", "creer_script."}, {"surveille", "creer_script."}, {"corps", "creer_script, creer_fonction."},
             {"retour", "creer_fonction."}, {"condition", "creer_alarme."}, {"message", "creer_alarme."}, {"priorite", "creer_alarme."},
             {"categorie", "creer_alarme."}, {"groupe", "creer_alarme."}, {"delai", "creer_alarme (ms)."}, {"acquittement", "creer_alarme."},
             {"incidents", "creer_alarme : false = pas de dossier d'incident pour elle."},
             {"consigne", "creer_alarme."}, {"piece_jointe", "creer_ressource."}, {"liste", "creer_dossier, ranger."},
             {"dossier", "creer_dossier, ranger..."}})
        props.set(k, str(d));
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
             "modifier, d\xC3\xA9placer, redimensionner, colorer, associer une variable, aligner, r\xC3\xA9partir, dupliquer, supprimer) "
             "ou qui cr\xC3\xA9" "e une vue (un symbole, une popup, un mod\xC3\xA8le, un en-t\xC3\xAAte, un pied), des variables, des types IHM (g\xC3\xA9n\xC3\xA9riques), des "
             "\xC3\xA9quipements et leurs adresses, des scripts, des fonctions, des alarmes, des ressources, des dossiers, avec toutes les op\xC3\xA9rations en un seul appel. Le logiciel v\xC3\xA9rifie chaque op\xC3\xA9ration (genres, propri\xC3\xA9t\xC3\xA9s, "
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
    const json::Value* ops = v.find("operations");
    if (!ops) return refuse("\xC2\xAB operations \xC2\xBB manque : l'entr\xC3\xA9" "e de l'outil est {\"resume\": ..., \"operations\": [...]}.");
    if (!ops->isArray()) return refuse("\xC2\xAB operations \xC2\xBB doit \xC3\xAAtre une liste.");
    if (ops->items().empty()) return refuse("\xC2\xAB operations \xC2\xBB est vide : rien \xC3\xA0 proposer.");
    std::size_t n = 0;
    for (const auto& o : ops->items())
        if (auto bad = malformed(o, ++n); !bad.empty()) return refuse(std::move(bad));
    Proposal p;
    if (!operationsFromJson(*ops, p.ops, why)) return std::nullopt;
    // 1.16.0 : la vue visee - la vue neuve d'un creer_vue, la vue ouverte si la proposition touche des objets, aucune
    // si elle ne cree que des variables ou des types (elle ne depend pas de la vue ouverte).
    if (usesOpenView(p) && !in.view)
        return refuse("Aucune vue n'est ouverte dans l'\xC3\xA9" "diteur : la proposition ne vise rien. Demande d'ouvrir une vue, ou "
                      "propose une vue neuve (creer_vue en premier).");
    p.id = newId("op");
    p.request = std::string(request);
    p.summary = v.at("resume").isString() ? v.str("resume") : std::string{};
    if (p.summary.empty()) p.summary = "Proposition de l'Assistant";
    scopeProposal(p, in.view);   // 1.19.0 : la vue ouverte d'abord, sinon la vue neuve principale
    p.fingerprint = in.project ? proposalPrint(*in.project, p) : in.view && usesOpenView(p) ? fingerprint(*in.view) : std::string{};
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

// =============================================================================
//  1.16.0 (FOURNISSEURS) : les modeles gratuits, petits ou sans appel d'outil
// =============================================================================
namespace {

// Les regles en bref (une petite fenetre de contexte : quelques milliers de jetons).
const char* const kRulesShort =
    "Tu es l'Assistant IA d'IHM Studio (conception d'IHM industrielles). Tu r\xC3\xA9ponds en fran\xC3\xA7" "ais, en tutoyant, "
    "bri\xC3\xA8vement.\n"
    "- Une demande qui modifie la VUE ACTIVE (cr\xC3\xA9" "er, modifier, d\xC3\xA9placer, colorer, associer une variable, aligner, "
    "r\xC3\xA9partir, dupliquer, supprimer) : UNE proposition d'op\xC3\xA9rations (le logiciel v\xC3\xA9rifie, montre un aper\xC3\xA7u, "
    "l'utilisateur applique). Une question : du texte seulement.\n"
    "- Seulement les genres list\xC3\xA9s, leurs propri\xC3\xA9t\xC3\xA9s, les variables du contexte, les objets de la vue : n'invente "
    "rien. Une demande ambigu\xC3\xAB : demande une pr\xC3\xA9" "cision.\n"
    "- Noms neufs uniques (lettres, chiffres, _). Coordonn\xC3\xA9" "es en pixels, origine en haut \xC3\xA0 gauche ; tout tient dans "
    "la vue, sans recouvrir. Valeurs en texte : nombres avec un point, TRUE/FALSE, couleurs #RRGGBB.\n"
    "- Op\xC3\xA9rations : creer (genre, nom, proprietes), modifier (objet, proprietes), associer (objet, variable), "
    "aligner (objets, bord : haut|bas|gauche|droite|centre-h|centre-v), distribuer (objets, sens : horizontal|vertical), "
    "supprimer (objets ; genre : vue, variable, type... ; strategie : seul|remplacer|references|inutiles), dupliquer (objets, noms, dx, "
    "dy), code (code). Jamais de code \xC3\xA0 ex\xC3\xA9" "cuter.\n"
    "- Une proposition refus\xC3\xA9" "e te revient avec ses raisons : corrige-la.\n";

bool blankChar(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && blankChar(s[a])) ++a;
    while (b > a && blankChar(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace

std::string textToolInstructions() {
    return "\n## Ta proposition : EN TEXTE (ce mod\xC3\xA8le n'a pas d'appel d'outil)\n"
           "L'outil proposer_operations n'est pas disponible ici : quand une demande modifie la vue, \xC3\xA9" "cris d'abord une ou deux "
           "phrases, puis \xC3\xA0 la FIN de ta r\xC3\xA9ponse UN SEUL bloc de code json, exactement sous cette forme (les m\xC3\xAA" "mes champs "
           "que l'entr\xC3\xA9" "e de l'outil, rien d'autre dans le bloc) :\n"
           "```json\n"
           "{\"resume\": \"Une cuve\", \"operations\": [{\"action\": \"creer\", \"genre\": \"Tank\", \"nom\": \"Cuve_1\", \"proprietes\": "
           "{\"x\": \"80\", \"y\": \"120\", \"w\": \"150\", \"h\": \"200\"}}]}\n"
           "```\n"
           "Le logiciel lit ce bloc strictement (un champ inconnu, une op\xC3\xA9ration inconnue : refus\xC3\xA9) et le v\xC3\xA9rifie comme "
           "un appel d'outil. Une question qui ne modifie rien : pas de bloc. Le r\xC3\xA9sultat de ta proposition pr\xC3\xA9" "c\xC3\xA9" "dente "
           "t'arrive en t\xC3\xAA" "te du message suivant, entre crochets.\n";
}

bool extractProposalBlock(std::string_view text, std::string* json, std::string* rest) {
    struct Block { std::size_t open{0}, bodyFrom{0}, bodyTo{0}, close{0}; };
    std::vector<Block> blocks;
    std::size_t at = 0;
    while ((at = text.find("```", at)) != std::string_view::npos) {
        std::size_t p = at + 3;
        std::size_t lineEnd = text.find('\n', p);
        const std::string_view tag = text.substr(p, (lineEnd == std::string_view::npos ? text.size() : lineEnd) - p);
        std::string t = trimmed(tag);
        for (auto& c : t)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        // Un bloc json (ou sans langue, qui commence par une accolade).
        const bool jsonTag = t == "json";
        const bool bare = t.empty() && lineEnd != std::string_view::npos && trimmed(text.substr(lineEnd + 1, 8)).rfind('{', 0) == 0;
        if (!jsonTag && !bare) {
            // Le bloc d'une autre langue : sauter jusqu'a sa fermeture.
            const std::size_t close = lineEnd == std::string_view::npos ? std::string_view::npos : text.find("```", lineEnd);
            if (close == std::string_view::npos) break;
            at = close + 3;
            continue;
        }
        Block b;
        b.open = at;
        b.bodyFrom = lineEnd == std::string_view::npos ? text.size() : lineEnd + 1;
        const std::size_t close = text.find("```", b.bodyFrom);
        b.bodyTo = close == std::string_view::npos ? text.size() : close;   // sans fermeture : jusqu'au bout
        b.close = close == std::string_view::npos ? text.size() : close + 3;
        blocks.push_back(b);
        at = b.close;
        if (close == std::string_view::npos) break;
    }
    if (blocks.empty()) return false;
    const Block* chosen = &blocks.back();
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
        if (text.substr(it->bodyFrom, it->bodyTo - it->bodyFrom).find("\"operations\"") != std::string_view::npos) {
            chosen = &*it;
            break;
        }
    if (json) *json = trimmed(text.substr(chosen->bodyFrom, chosen->bodyTo - chosen->bodyFrom));
    if (rest) *rest = trimmed(std::string(text.substr(0, chosen->open)) + std::string(text.substr(chosen->close)));
    return true;
}

std::string proposalBlock(std::string_view inputJson) { return "```json\n" + trimmed(inputJson) + "\n```"; }

PromptParts compactPromptParts(const PromptInput& in, int contextTokens) {
    PromptParts out;
    const KindRegistry& reg = registry();
    // Les budgets a la taille de la fenetre : environ trois caracteres par jeton, la moitie pour les
    // consignes (le reste : la conversation et la reponse).
    const std::size_t room = static_cast<std::size_t>(std::clamp(contextTokens, 2048, kSmallContext)) * 3 / 2;
    std::string& s = out.stable;
    s = kRulesShort;
    std::string& d = out.dynamic;
    d += "\n## Genres d'objets (seulement ceux-ci ; une ligne : cl\xC3\xA9 (libell\xC3\xA9) taille, propri\xC3\xA9t\xC3\xA9s cl\xC3\xA9(r\xC3\xB4le)=d\xC3\xA9" "faut ; "
         "communes : x, y, w, h, visible, variable)\n";
    std::size_t n = 0;
    const std::size_t kindBytes = room / 3;
    for (const auto k : detailKinds(in, true)) {
        const auto* e = reg.find(k);
        if (!e) continue;
        const std::string line = reg.detailLine(*e);
        if (n > 0 && d.size() + line.size() > kindBytes) break;
        d += line;
        ++n;
    }
    ContextLimits lim;
    lim.objectBytes = room / 4;
    lim.nameBytes = room / 12;
    lim.variableBytes = room / 8;
    lim.plcBytes = room / 10;
    lim.viewBytes = room / 24;
    d += "\n" + contextText(in, lim);
    return out;
}

} // namespace app::ai
