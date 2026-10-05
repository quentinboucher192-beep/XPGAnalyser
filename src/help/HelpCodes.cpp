// =============================================================================
//  help/HelpCodes.cpp — les pages elles-memes
// -----------------------------------------------------------------------------
//  Chaque page repond a trois questions, dans cet ordre, et a rien d'autre :
//  pourquoi ca arrive, ce que ca change au programme, quoi faire. Le troisieme
//  point est une liste d'actions a essayer dans l'ordre, pas un paragraphe : on
//  lit cette page avec une armoire ouverte devant soi.
// =============================================================================
#include "HelpCodes.hpp"

#include <algorithm>
#include <cctype>

namespace help {
namespace {

bool sameId(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x))
            == std::tolower(static_cast<unsigned char>(y));
    });
}

// Le message porte-t-il ce morceau ? Les messages sont ecrits en anglais pour
// les uns, en francais pour les autres, et comportent des noms interpoles au
// milieu : on reconnait donc un fragment stable, pas la phrase entiere.
bool has(std::string_view message, std::string_view fragment) {
    return message.find(fragment) != std::string_view::npos;
}

const std::vector<Code>& table() {
    static const std::vector<Code> kCodes = {

// ============================================================ 0xxx : ouvrir ==
{"XPG-0101", Severity::Error,
 "Ce dossier n'est pas un projet",
 "Le dossier ne contient pas de project.xpgproj. C'est ce fichier, et lui seul, "
 "qui fait d'un dossier un projet : il porte le nom de l'affaire, l'automate et "
 "l'etat (DEV, FINISH, LOCK).",
 "Rien n'est ouvert. Les exports qui se trouvent dans le dossier ne sont pas lus "
 "tout seuls : un .XPG posé a cote d'autres fichiers reste un fichier.",
 {"Ouvrez le .XPG directement plutot que son dossier : l'application cree alors "
  "un projet autour.",
  "Si le projet existait et que project.xpgproj a disparu, recreez un projet et "
  "reimportez l'export : le reste du dossier est du cache, il se reconstruit.",
  "Verifiez que vous n'avez pas designe le dossier PARENT du projet."},
 {"XPG-0102"}},

{"XPG-0102", Severity::Warning,
 "La configuration materielle n'est pas dans un .XPG",
 "Un export .XPG contient le programme : types, variables, sections, taches. Le "
 "rack, les modules et leurs references vivent dans un .XHW, qui est un export "
 "separe de Control Expert.",
 "Le projet s'ouvre et tout le programme est la, mais la vue Rack est vide et "
 "les adresses %I/%Q ne peuvent pas etre rattachees a un module. L'analyse des "
 "E/S est donc partielle, et elle le dit plutot que de deviner un rack.",
 {"Exportez aussi le .XHW depuis Control Expert et ouvrez-le : il complete le "
  "projet deja charge au lieu de le remplacer.",
  "Les deux fichiers peuvent etre donnes ensemble, separes par un point-virgule, "
  "dans le champ de chemin de l'accueil."},
 {"XPG-0101"}},

// ============================================================= 1xxx : modele ==
{"XPG-1201", Severity::Warning,
 "Un type declare n'est defini nulle part",
 "Une variable utilise un type que l'export ne definit pas : un DDT d'une "
 "bibliotheque non exportee, ou un bloc d'une famille absente.",
 "La taille de cette variable est INCONNUE. Le total memoire affiche est donc "
 "une borne INFERIEURE, pas une mesure - le tableau des types le marque en "
 "orange pour cette raison.",
 {"Exportez le projet avec ses bibliotheques, ou importez le DDT manquant depuis "
  "la bibliotheque partagee.",
  "Si le type vient d'une bibliotheque Schneider standard, son absence est "
  "normale : c'est le total qui est approche, pas le programme qui est faux."},
 {"XPG-4101"}},

{"XPG-1301", Severity::Warning,
 "Deux variables a la meme adresse",
 "Deux declarations pointent sur le meme %MW, %M ou la meme voie. Control Expert "
 "l'accepte : c'est parfois voulu (une vue en mot d'un groupe de bits), et le "
 "plus souvent une adresse recopiee sans etre changee.",
 "Les deux variables sont le MEME emplacement memoire. Ecrire l'une ecrase "
 "l'autre, et le programme se comporte correctement jusqu'au jour ou les deux "
 "sont ecrites dans le meme cycle.",
 {"Regardez si l'une des deux est un alias volontaire ; si oui, elle merite un "
  "commentaire qui le dit, sans quoi la question se reposera.",
  "Sinon, la deuxieme adresse a ete oubliee lors d'un copier-coller : "
  "renumerotez-la dans la zone libre indiquee par le classeur."},
 {}},

// ========================================================== 2xxx : simulateur ==
{"XPG-2101", Severity::Warning,
 "Cette section n'est pas en Structured Text",
 "Le simulateur execute du ST, et rien d'autre. Une section en LD, FBD, IL ou "
 "SFC est lue, comptee, affichee - mais pas executee.",
 "Elle est SAUTEE pendant la simulation. Les variables qu'elle ecrirait gardent "
 "leur valeur, ce qui peut faire croire qu'un equipement ne demarre pas alors "
 "que c'est la section de commande qui n'a pas tourne.",
 {"Regardez la liste de l'ordre d'execution : les sections non simulees y sont "
  "marquees en orange, donc on sait ce qui manque avant de s'interroger.",
  "Pour verifier une logique LD, forcez a la main les variables qu'elle aurait "
  "ecrites et relancez le cycle.",
  "Une section de mapping E/S generee par les macros est toujours en ST : si "
  "celle-ci ne l'est pas, elle a ete ecrite a la main."},
 {"XPG-2102", "XPG-6101"}},

{"XPG-2102", Severity::Warning,
 "Le corps de ce DFB n'est pas executable",
 "Le type de bloc existe et ses instances sont declarees, mais son corps est "
 "dans un langage que le simulateur ne joue pas - ou il n'a pas de corps du tout.",
 "Les instances STOCKENT leurs entrees et leurs sorties GARDENT leur derniere "
 "valeur. Le bloc ne calcule rien. Un DFB_EQ_PUMP dans cet etat laisse Running a "
 "FALSE quoi qu'on lui demande.",
 {"Si le bloc vient de la bibliotheque partagee, reimportez-le : ceux de libs/ "
  "ont tous un corps en ST.",
  "Sinon, forcez ses sorties pour simuler son comportement le temps du test."},
 {"XPG-2101", "XPG-4102"}},

{"XPG-2201", Severity::Error,
 "Une boucle ne se termine pas",
 "Une boucle FOR, WHILE ou REPEAT a depasse son budget d'iterations. Le "
 "simulateur en fixe un parce qu'un automate, lui, a un chien de garde : une "
 "boucle sans fin n'y tourne pas non plus, elle provoque un defaut de tache.",
 "LE CYCLE EST ARRETE. Le programme n'a pas fini son balayage, donc l'etat "
 "affiche est celui d'un cycle incomplet : les sections qui suivaient n'ont pas "
 "tourne du tout.",
 {"Regardez la condition de sortie : une comparaison sur un REAL qui n'atteint "
  "jamais exactement sa borne est la cause la plus frequente.",
  "Un FOR dont le pas est nul ne se termine jamais ; c'est signale a part.",
  "Un compteur modifie A L'INTERIEUR de la boucle et relu comme borne est le "
  "deuxieme cas classique."},
 {"XPG-2202"}},

{"XPG-2202", Severity::Error,
 "Un FOR avec un pas de zero",
 "La valeur de BY est nulle. La variable de boucle ne change donc jamais, et la "
 "condition d'arret n'est jamais atteinte.",
 "Le cycle serait arrete par le budget d'iterations. Ce diagnostic-ci arrive "
 "AVANT d'avoir tourne : le cas est reconnaissable a la lecture, et il ne coute "
 "rien de le dire tout de suite.",
 {"Le pas vient presque toujours d'une variable dont la valeur par defaut est "
  "restee a zero : cherchez qui l'ecrit, et si ce quelqu'un tourne avant."},
 {"XPG-2201"}},

{"XPG-2301", Severity::Error,
 "Cette adresse sort de la memoire configuree",
 "Un %MW, %M ou %MD au-dela de ce que l'automate declare. La taille vient de la "
 "configuration du projet, pas d'une limite du simulateur.",
 "L'automate REFUSERAIT le programme. Ce n'est pas une approximation : c'est la "
 "meme verification que fait Control Expert a la generation.",
 {"Agrandissez la zone memoire dans la configuration de l'automate, si le "
  "processeur le permet.",
  "Ou renumerotez les reports : l'onglet Memoire MW du classeur montre les "
  "plages libres."},
 {"XPG-5301"}},

{"XPG-2401", Severity::Error,
 "Fonction ou bloc inconnu du simulateur",
 "Le programme appelle quelque chose que le simulateur ne connait pas : une "
 "fonction d'une bibliotheque Schneider non modelisee, ou un DFB absent du "
 "projet.",
 "L'appel est ignore et le cycle s'arrete a cette ligne. Tout ce qui suit dans "
 "la section n'a pas tourne.",
 {"Verifiez d'abord l'orthographe : le simulateur ne fait pas de correction.",
  "Si c'est un bloc de la bibliotheque partagee, importez-le dans le projet.",
  "Si c'est une fonction standard non modelisee, remplacez-la le temps du test "
  "par une affectation directe."},
 {"XPG-2102"}},

{"XPG-2501", Severity::Warning,
 "Imbrication d'appels trop profonde",
 "Un bloc en appelle un autre, qui en appelle un autre... au-dela de la "
 "profondeur admise. La cause habituelle n'est pas la profondeur : c'est un bloc "
 "qui s'instancie lui-meme, directement ou par un intermediaire.",
 "Le cycle est arrete. Sur un vrai automate, le meme programme deborderait la "
 "pile - c'est un defaut de tache, pas un ralentissement.",
 {"Cherchez l'instance du bloc dans son propre corps : c'est le cas dans neuf "
  "fois sur dix.",
  "Un DFB qui appelle un DFB qui rappelle le premier produit le meme effet et se "
  "voit moins."},
 {}},

// =============================================================== 3xxx : macros ==
{"XPG-3101", Severity::Error,
 "Fonction inconnue dans une macro",
 "Le nom appele n'est dans aucune table de fonctions du runtime des macros. La "
 "macro est REFUSEE AVANT DE TOURNER : rien n'a ete lu, rien n'a ete ecrit, et "
 "le chemin qu'on lui a donne n'a meme pas ete regarde.",
 "Aucune modification n'est faite au projet. C'est le bon comportement : une "
 "macro a moitie appliquee serait pire.",
 {"Une faute de frappe : la liste complete des fonctions est dans l'aide, "
  "onglet Macros, rubrique des natives.",
  "Ou bien la fonction existe dans une version plus recente de l'application. "
  "OpenWorkbook, OpenSheet et Setting en sont : elles demandent le lecteur de "
  "classeur.",
  "RunMacro n'existe pas encore : ImporterClasseur ne peut donc pas tourner. "
  "ImporterProgramme fait le meme travail sans enchainer."},
 {"ImporterProgramme", "XPG-5101"}},

{"XPG-3201", Severity::Warning,
 "Une ligne du tableau a ete ecartee",
 "La macro a saute une ligne parce qu'une colonne indispensable etait vide, ou "
 "parce que la ligne est marquee inactive. Le message dit laquelle et pourquoi.",
 "Cette ligne ne produit RIEN dans le programme. Une carte inactive n'est pas "
 "declaree, une voie sans adresse n'est pas cablee.",
 {"Si c'est voulu - une carte en reserve, un equipement hors service - il n'y a "
  "rien a faire.",
  "Sinon, la colonne manquante est presque toujours une colonne CALCULEE du "
  "classeur : recalculez-le (F9) avant de relancer l'import."},
 {"XPG-5201"}},

{"XPG-3301", Severity::Error,
 "La section citee par la macro n'existe pas",
 "AppendToSection ou ClearSection nomme une section absente du projet. La macro "
 "l'a peut-etre creee sous un autre nom, ou le prefixe de section a change entre "
 "deux executions.",
 "Rien n'est ecrit dans cette section. La macro continue, donc le programme se "
 "retrouve partiellement genere - c'est l'un des rares cas ou l'etat final "
 "demande un coup d'oeil.",
 {"Relancez l'import complet plutot que de corriger a la main : les sections "
  "generees sont reecrites entierement, donc relancer remet tout d'aplomb.",
  "Verifiez le prefixe de section demande par la macro."},
 {"ImporterProgramme"}},

// ========================================================= 4xxx : bibliotheque ==
{"XPG-4101", Severity::Warning,
 "Le projet a une version plus ancienne que la bibliotheque",
 "Le bloc existe dans le projet ET dans libs/, mais pas dans la meme version. "
 "Le projet garde ce qu'il a importe : une bibliotheque ne se met jamais a jour "
 "toute seule dans un programme en service.",
 "Le comportement du projet est celui de SA version. Les corrections apportees "
 "depuis ne s'y appliquent pas - y compris les corrections de defaut.",
 {"Lisez d'abord ce qui a change : le bandeau de l'article le dit.",
  "La 1.01 des blocs corrige le bornage de Count : en 1.00 la DERNIERE voie ou "
  "le DERNIER equipement n'est jamais traite. Si vous etes en 1.00, la mise a "
  "jour n'est pas optionnelle.",
  "Mettre a jour remplace le type dans le projet ; les instances et leurs "
  "cablages sont conserves."},
 {"XPG-4102", "DFB_IO_DIG16"}},

{"XPG-4102", Severity::Error,
 "Element absent de la bibliotheque partagee",
 "Une macro ou un import demande un bloc que libs/ ne contient pas. Le dossier "
 "libs est celui indique par l'onglet Config du classeur, pas celui de "
 "l'application.",
 "Le bloc n'est pas importe. Les instances declarees ensuite citent un type qui "
 "n'existe pas, et le projet ne compilerait pas.",
 {"Verifiez le chemin « Dossier libs » de l'onglet Config.",
  "Verifiez l'orthographe du nom dans la colonne Bloc du classeur : "
  "DFB_IO_DIG16, pas DFB_IO_DIG_16.",
  "Le fichier doit s'appeler <Nom>.dfb ou <Nom>.ddt et etre dans une "
  "sous-categorie de libs/."},
 {"XPG-4101", "VerifierBibliotheque"}},

// ============================================================= 5xxx : classeur ==
{"XPG-5101", Severity::Error,
 "Le classeur ne peut pas etre ouvert",
 "Le fichier n'existe pas, n'est pas un .xlsx/.xlsm, ou est au format zip64 - "
 "que le lecteur refuse plutot que de lire de travers.",
 "Aucun import n'a lieu. Le projet n'est pas touche.",
 {"Un .xls ANCIEN FORMAT (Excel 97) n'est pas un zip : enregistrez-le en .xlsx.",
  "Un fichier ouvert dans Excel se lit quand meme : le lecteur en prend une "
  "copie en memoire et referme aussitot.",
  "Laisser le chemin vide reprend le dernier classeur ouvert."},
 {"XPG-5201"}},

{"XPG-5201", Severity::Error,
 "Onglet introuvable dans le classeur",
 "Le nom est compare EXACTEMENT, espaces compris. « Cartes API » porte une "
 "espace ; « CartesAPI » est un autre onglet.",
 "L'import s'arrete la pour les onglets indispensables (Cartes API, ES, "
 "Equipements) et continue en le signalant pour les autres (Cablage, Liens, "
 "Catalogue).",
 {"Le message liste les onglets que le classeur contient reellement : comparez.",
  "Un onglet renomme est la premiere cause d'echec d'un import."},
 {"XPG-5202"}},

{"XPG-5202", Severity::Warning,
 "La ligne d'en-tete n'a pas ete trouvee comme prevu",
 "Le lecteur CHERCHE la ligne d'en-tete dans les quinze premieres lignes, au "
 "lieu de supposer la ligne 7 : le jour ou quelqu'un insere une ligne dans le "
 "bandeau, supposer ferait lire les libelles a la place des donnees. Pour un "
 "onglet connu, il cherche sa colonne temoin ; sinon, la premiere ligne ni "
 "fusionnee ni numerotee avec au moins trois cellules.",
 "Un en-tete mal trouve donne des colonnes nommees n'importe comment, et donc "
 "des cellules vides partout : l'import produit alors un programme vide plutot "
 "qu'un programme faux.",
 {"Si l'onglet est a vous, ajoutez son nom et sa colonne temoin dans "
  "witnessFor(), en bas de XlsmSource.cpp. C'est une ligne.",
  "Verifiez que la bande de groupes au-dessus des en-tetes est bien fusionnee : "
  "c'est ce qui la distingue d'un en-tete."},
 {"XPG-5201"}},

{"XPG-5301", Severity::Warning,
 "Le registre d'alarmes est plein",
 "DFB_ALM_MANAGER balaie 64 emplacements, et ce nombre est celui de son "
 "tableau : il n'y en a pas de soixante-cinquieme.",
 "Les alarmes au-dela de la 64e NE SONT PAS enregistrees. Elles ne se datent "
 "pas, ne s'acquittent pas et n'entrent pas dans l'historique. Le compte rendu "
 "de l'import nomme celles qui n'ont pas tenu.",
 {"Desactivez les alarmes de voie peu utiles : chaque seuil analogique actif "
  "prend un emplacement, et quatre seuils sur une voie en prennent quatre.",
  "Ou posez un second gestionnaire avec son propre registre, et repartissez les "
  "alarmes par zone d'armoire."},
 {"DFB_ALM_MANAGER", "ImporterProgramme"}},

// ====================================================== 6xxx : ordre d'execution ==
{"XPG-6101", Severity::Warning,
 "Une commande d'IHM est lue apres l'automatisme",
 "La section des commandes (Reports_TC) est placee APRES les sections qui "
 "utilisent ces commandes. Control Expert execute les sections dans l'ordre du "
 "navigateur de projet, et creer une section ne la place pas.",
 "Chaque commande venant de l'IHM est prise en compte AU CYCLE SUIVANT. Un "
 "bouton repond avec un cycle de retard. Ca ne se voit pas sur une marche/arret "
 "de pompe ; ca se voit sur un comptage de pieces et sur tout ce qui est "
 "synchronise.",
 {"Remontez Reports_TC dans le dossier Ordre d'execution : clic droit, Monter, "
  "ou glissez-la.",
  "L'ordre complet que les macros attendent : Mapping_Entrees, Cablage_Entrees, "
  "Reports_TC, Equipements, Alarmes_Cycle, Reports_TM_TA, Cablage_Sorties, "
  "Mapping_Sorties."},
 {"XPG-6102", "ImporterProgramme"}},

{"XPG-6102", Severity::Warning,
 "Une mesure est publiee avant d'etre calculee",
 "La section des mesures (Reports_TM_TA) est placee AVANT les sections qui les "
 "calculent.",
 "L'IHM lit la valeur DU CYCLE PRECEDENT. Sur une temperature, personne ne le "
 "verra jamais. Sur un compteur de pieces ou un chronometre, l'ecart est visible "
 "et incomprehensible tant qu'on n'a pas regarde l'ordre.",
 {"Descendez Reports_TM_TA apres la section Equipements.",
  "Si les deux sections doivent rester ou elles sont, c'est qu'une troisieme les "
  "separe : regardez l'ordre complet plutot que la paire."},
 {"XPG-6101"}},

{"XPG-6103", Severity::Warning,
 "Une carte de sortie est appelee avant ses reglages",
 "Le bloc d'E/S est appele avant que .IsOut soit pose. Au PREMIER cycle, il "
 "prend donc la carte pour une entree et calcule Val a partir de Raw au lieu de "
 "l'inverse.",
 "Un cycle, une fois, au demarrage. C'est exactement le genre de defaut qu'on ne "
 "retrouve jamais : il ne se reproduit pas, et il ne laisse pas de trace.",
 {"Une carte de sortie se genere en TROIS temps : les reglages, puis l'appel du "
  "bloc, puis la recopie .Raw vers %Q. Relancez ImporterProgramme, qui le fait.",
  "Si la section a ete ecrite a la main, deplacez la ligne d'appel apres les "
  "affectations de .IsOut."},
 {"ImporterProgramme", "XPG-6101"}},

{"XPG-6104", Severity::Info,
 "Alarmes_Init ne va pas dans la tache",
 "Cette section remplit le registre d'alarmes : libelles, identifiants, "
 "severites. Ce sont des constantes.",
 "Placee dans le cycle, elle reecrit les 64 emplacements a chaque balayage. Le "
 "programme fonctionne, et perd du temps de cycle pour rien - typiquement 300 "
 "affectations toutes les 20 ms.",
 {"Conditionnez-la par un bit de premier cycle.",
  "Ou faites-en une sous-routine et appelez-la une fois depuis l'initialisation."},
 {"ImporterProgramme", "DFB_ALM_MANAGER"}},

    };
    return kCodes;
}

} // namespace

const std::vector<Code>& allCodes() { return table(); }

const Code* find(std::string_view id) {
    for (const auto& c : table())
        if (sameId(c.id, id)) return &c;
    return nullptr;
}

// -----------------------------------------------------------------------------
std::string_view codeFor(std::string_view message) {
    // L'ORDRE COMPTE ICI, et c'est le seul endroit de ce fichier ou c'est vrai :
    // les fragments les plus precis passent avant les plus generaux, sinon
    // « FOR loop with a step of zero » serait attrape par « loop ran more than ».
    if (has(message, "step of zero"))                         return "XPG-2202";
    if (has(message, "loop ran more than")
        || has(message, "not terminating"))                   return "XPG-2201";
    if (has(message, "outside the configured data memory"))   return "XPG-2301";
    if (has(message, "nests function-block calls"))           return "XPG-2501";
    if (has(message, "no Structured Text body"))              return "XPG-2102";
    if (has(message, "runs Structured Text only"))            return "XPG-2101";
    if (has(message, "is not a function or a block"))         return "XPG-2401";

    if (has(message, "fonction inconnue"))                    return "XPG-3101";
    if (has(message, "la section '")
        && (has(message, "n'existe pas")))                    return "XPG-3301";

    if (has(message, "aucune bibliotheque")
        || has(message, "LibImport"))                         return "XPG-4102";

    if (has(message, "classeur introuvable")
        || has(message, "ce n'est pas un classeur")
        || has(message, "zip64"))                             return "XPG-5101";
    if (has(message, "aucun onglet nomme")
        || has(message, "onglet introuvable"))                return "XPG-5201";
    if (has(message, "registre plein"))                       return "XPG-5301";

    if (has(message, "not a project folder"))                 return "XPG-0101";
    if (has(message, "Rack and module layout is not part"))   return "XPG-0102";
    return {};
}

std::string withCode(std::string_view message) {
    const auto code = codeFor(message);
    if (code.empty()) return std::string(message);
    return std::string(code) + "  " + std::string(message);
}

// -----------------------------------------------------------------------------
namespace {

ui::Tone toneOf(Severity s) {
    return s == Severity::Error ? ui::Tone::Error : s == Severity::Warning ? ui::Tone::Warning
                                                                             : ui::Tone::Info;
}
const char* wordOf(Severity s) {
    return s == Severity::Error ? "erreur" : s == Severity::Warning ? "avertissement" : "information";
}
ui::HelpBlock block(ui::HelpBlockKind k, std::string text, std::string label = {}) {
    ui::HelpBlock b;
    b.kind = k;
    b.text = std::move(text);
    b.label = std::move(label);
    return b;
}

} // namespace

ui::HelpArticle article(const Code& c) {
    using K = ui::HelpBlockKind;
    ui::HelpArticle a;

    // L'en-tete : le code en grand, sa gravite en etiquette, et le chemin du
    // retour vers la table des codes.
    auto hero = block(K::Hero, std::string(c.id), "Code de diagnostic");
    hero.tone = toneOf(c.severity);
    hero.pills.push_back({wordOf(c.severity), ui::kNoColor, toneOf(c.severity)});
    hero.links.push_back({"Diagnostics", "diag:index", "Tous les codes", ui::kNoColor, ui::Tone::None});
    hero.links.push_back({std::string(c.id), {}, {}, ui::kNoColor, ui::Tone::None});
    a.blocks.push_back(std::move(hero));
    a.blocks.push_back(block(K::Lead, std::string(c.title)));

    a.blocks.push_back(block(K::Heading, "Pourquoi ça arrive"));
    a.blocks.push_back(block(K::Paragraph, std::string(c.cause)));

    // L'EFFET AVANT LA SOLUTION, toujours. Quelqu'un qui lit cette page decide
    // d'abord si ca le concerne ; savoir quoi faire ne sert qu'apres. Il est
    // dans un encadre de la couleur de sa gravite : c'est lui qu'on cherche.
    a.blocks.push_back(block(K::Heading, "Ce que ça change au programme"));
    {
        auto effet = block(K::Callout, std::string(c.effect));
        effet.severity = c.severity == Severity::Error ? 2 : c.severity == Severity::Warning ? 1 : 0;
        a.blocks.push_back(std::move(effet));
    }

    if (!c.fixes.empty()) {
        a.blocks.push_back(block(K::Heading, "Quoi faire"));
        for (std::size_t i = 0; i < c.fixes.size(); ++i) {
            auto b = block(K::Bullet, std::string(c.fixes[i]), std::to_string(i + 1));
            b.tone = ui::Tone::Accent;
            a.blocks.push_back(std::move(b));
        }
    }
    if (!c.see.empty()) {
        a.blocks.push_back(block(K::Heading, "Voir aussi"));
        auto links = block(K::Links, {});
        for (const auto& v : c.see) {
            const std::string name(v);
            const bool code = name.rfind("XPG-", 0) == 0;
            links.links.push_back({name, (code ? "dia:" : "lib:") + name,
                                   code ? "Le code " + name : "Ouvrir " + name,
                                   ui::kNoColor, code ? ui::Tone::Info : ui::Tone::None});
        }
        a.blocks.push_back(std::move(links));
    }
    return a;
}

ui::HelpArticle indexArticle() {
    using K = ui::HelpBlockKind;
    ui::HelpArticle a;
    auto hero = block(K::Hero, "Codes de diagnostic",
                      std::to_string(table().size()) + " codes, du 0xxx au 6xxx");
    hero.tone = ui::Tone::Info;
    a.blocks.push_back(std::move(hero));
    a.blocks.push_back(block(K::Lead,
        "Chaque message porte un code. Le code se cherche, se cite dans un courriel et ne "
        "change jamais de sens : un numéro retiré reste vide plutôt que d'être réutilisé. "
        "Chaque carte ouvre la page de son code."));

    std::string famille;
    for (const auto& c : table()) {
        const auto tete = std::string(c.id).substr(4, 1);
        if (tete != famille) {
            famille = tete;
            const char* titre =
                  tete == "0" ? "0xxx · ouvrir un fichier, un projet, un export"
                : tete == "1" ? "1xxx · le modèle : types, variables, sections"
                : tete == "2" ? "2xxx · le simulateur"
                : tete == "3" ? "3xxx · les macros"
                : tete == "4" ? "4xxx · la bibliothèque partagée"
                : tete == "5" ? "5xxx · le classeur .xlsm"
                              : "6xxx · l'ordre d'exécution";
            a.blocks.push_back(block(K::Heading, titre));
        }
        auto card = block(K::Term, std::string(c.title), std::string(c.id));
        card.detail = wordOf(c.severity);
        card.pills.push_back({wordOf(c.severity), ui::kNoColor, toneOf(c.severity)});
        card.links.push_back({std::string(c.id), "dia:" + std::string(c.id),
                              "Ouvrir " + std::string(c.id), ui::kNoColor, ui::Tone::None});
        a.blocks.push_back(std::move(card));
    }
    return a;
}

} // namespace help
