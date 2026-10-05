// =============================================================================
//  help/ReleaseNotes.cpp - 1.11 (chantier T2) : la table des notes de version
// -----------------------------------------------------------------------------
//  1.10.0 et 1.9.0 : les 31 cartes de la fenetre Nouveautes (le registre
//  kItems de help/Novelties jusqu'a la 1.10.1, deplace ici tel quel : memes
//  id, meme ordre), plus les corrections de la maquette 1.11 validee. 1.8.0 :
//  les lignes de la maquette (resumees des LISEZ-MOI). 1.10.1 : le LISEZ-MOI
//  livre. 1.10.2 : DECISIONS-0210, revu sur son LISEZ-MOI. 1.10.3 : son
//  LISEZ-MOI (livree le 02/10 au soir). 1.10.4 : son LISEZ-MOI (livree le
//  03/10 dans la nuit ; tranche 13 de T2). 1.11 : un premier jet (tranche 11 de
//  T2), d'apres la SPEC 1.11, ses 7 cartes de la fenetre Nouveautes (tranche 14 :
//  sept lignes N devenues card(...), memes sujets, meme ordre), les
//  journaux de T1, T2 et T3, et les reliquats promis par la 1.10.2 que fait le
//  chantier R111 ; a completer le 04/10. Le domaine de chaque ligne est celui de
//  la maquette, dans son ordre. Tranche 18 : ce qui est arrive depuis (T1 78dd83f,
//  T3 68cd6dc, R111 jusqu'a 8ded614, F1 partout), accorde au LISEZ-MOI de la 1.11.
//  Tranche 22 : la date de la livraison (03/10, decision 47) ; les tutoriels
//  deduits : tous prets, ceux des macros et des blocs sans "A toi" (decision 45).
//  Tranche 23 : environ un tutoriel sur deux propose un "A toi" (R111-16, decision 55) ;
//  les notes ne le promettent plus partout.
//  Tranche 30 (1.11.1) : onze lignes, API. (API-M, API-V), les alarmes de chaque objet
//  (T1, vi-1111), les scripts ST et deux corrections de REP (reperes-1111), la remise en place (T1, tuto-1111), C111-1 et C111-2 ; la 1.11.1 "livree le 03/10 au soir" (a accorder si elle part plus tard).
//  Tranche 24 (decision 63) : R111-19, les blocs DFB ont un "A toi" (F8) ; seuls ceux des
//  macros et des types (DDT) n'en ont pas. R111-23, "le texte se tape" selon le sujet.
// =============================================================================
#include "ReleaseNotes.hpp"

#include "Novelties.hpp"
#include "Shortcuts.hpp"

#include <algorithm>

namespace help::notes {

namespace {

const std::vector<Release> kReleases = {
    {"1.11.3", "05/10/2026", "livr\xC3\xA9" "e le 05/10"},
    {"1.11.2", "04/10/2026", "livr\xC3\xA9" "e le 04/10"},
    {"1.11.1", "03/10/2026", "livr\xC3\xA9" "e le 03/10 au soir"},
    {"1.11", "03/10/2026", "livr\xC3\xA9" "e le 03/10"},
    {"1.10.4", "03/10/2026", "livr\xC3\xA9" "e le 03/10 dans la nuit"},
    {"1.10.3", "02/10/2026", "livr\xC3\xA9" "e le 02/10 au soir"},
    {"1.10.2", "02/10/2026", "livr\xC3\xA9" "e le 02/10"},
    {"1.10.1", "02/10/2026", "livr\xC3\xA9" "e le 02/10"},
    {"1.10.0", "02/10/2026", "livr\xC3\xA9" "e le 02/10 \xC3\xA0 04 h 35"},
    {"1.9.0", "01/10/2026", ""},
    {"1.8.0", "30/09/2026", ""},
};

// Une ligne de notes seule (elle n'est pas une carte de la fenetre Nouveautes).
Note note(std::string_view version, std::string_view domain, Kind kind, std::string_view text,
          std::string_view topic, std::string_view step) {
    Note n;
    n.version = version;
    n.domain = domain;
    n.kind = kind;
    n.text = text;
    n.topic = topic;
    n.tutorialStep = step;
    return n;
}

// Une carte de la fenetre Nouveautes, ecrite comme l'etait le registre de
// help/Novelties (id, version, titre, texte, image, go, widget, sujet) : c'est
// aussi une ligne des notes (genre N), dans son domaine.
struct CardSrc {
    std::string_view id, version, title, text, image, go, widget, topic;
};
Note card(std::string_view domain, std::string_view step, const CardSrc& c) {
    Note n = note(c.version, domain, Kind::New, c.text, c.topic, step);
    n.id = c.id;
    n.title = c.title;
    n.go = c.go;
    n.widget = c.widget;
    n.image = c.image;
    n.news = true;
    return n;
}

// La table, construite au premier appel (pas d'ordre d'initialisation entre
// fichiers : la fenetre Nouveautes la lit).
const std::vector<Note>& table() {
    static const std::vector<Note> t = {
        // 1.11.3 (05/10/2026, correctif urgent des captures du client) : les parametres d'une instance de
        // symbole - le fx garde, la constante convertie dans le type (Voiture), le tableau (UINTS) ; le carre
        // de legende au bout de chaque case de l'inspecteur.
        note("1.11.3", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "Param\xC3\xA8tres du symbole : une formule garde son fx apr\xC3\xA8s la saisie. =UINTS reste une formule (la pastille fx pleine, le carr\xC3\xA9 I d'une variable IHM) ; la 1.11.2 le r\xC3\xA9\xC3\xA9" "crivait UINTS, sans fx.",
         "symboles", ""),
        note("1.11.3", "\xC3\x89" "diteur IHM", Kind::Changed,
         "Param\xC3\xA8tres du symbole : sans fx, ce qu'on tape est une constante convertie dans le type du param\xC3\xA8tre, comme le texte d'un objet. Voiture devient 'Voiture' pour un STRING, 1,5 devient 1.5 pour un REAL, vrai devient TRUE pour un BOOL, Auto devient T_MODE#Auto pour une \xC3\xA9num\xC3\xA9ration. Un nom de variable (UINTS, une variable de l'automate) reste la variable ; un param\xC3\xA8tre ANY garde l'argument tel quel. Les anciens arguments sans apostrophes (Voiture;50) se lisent de la m\xC3\xAAme fa\xC3\xA7on.",
         "symboles", ""),
        note("1.11.3", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "Un tableau en param\xC3\xA8tre : UINTS (ARRAY[0..9] OF UINT) va \xC3\xA0 Value : ARRAY[0..9] OF UINT sans erreur ; une variable d'un autre type (gCoef, un REAL) est signal\xC3\xA9" "e en rouge, avec les variables du bon type propos\xC3\xA9" "es.",
         "symboles", ""),
        note("1.11.3", "Champs et expressions", Kind::New,
         "Le carr\xC3\xA9 de l\xC3\xA9gende : au bout de chaque case de l'inspecteur, un carr\xC3\xA9 dit d'o\xC3\xB9 vient la valeur - C constante, fx formule, $ rep\xC3\xA8res, A automate, I IHM, S syst\xC3\xA8me, V symbole ou vue, ! erreur. Son infobulle dit le type, la source et l'erreur s'il y en a une ; un clic ouvre la liste des carr\xC3\xA9s et leur sens (celui de la case marqu\xC3\xA9 ici), puis le s\xC3\xA9lecteur de valeur.",
         "aide-saisie", ""),
        note("1.11.3", "Champs et expressions", Kind::New,
         "Le s\xC3\xA9lecteur de valeur : l'arbre de tout ce qu'une case peut lire - l'automate, l'IHM, SYS. par domaine, le symbole ou la vue (param\xC3\xA8tres, variables publiques) -, filtr\xC3\xA9 d'office sur le type attendu (Tout montrer pour le reste), une recherche, le d\xC3\xA9tail de la variable choisie. Le R\xC3\xA9sultat reste un champ modifiable (une formule, un rep\xC3\xA8re $\xE2\x80\xA6$) avec l'aide \xC3\xA0 la saisie, ses erreurs et leurs corrections d'un clic ; Valider \xC3\xA9" "crit la case d'origine (Ctrl+Z).",
         "aide-saisie", ""),
        note("1.11.3", "Champs et expressions", Kind::New,
         "Un nom qui n'existe pas, valid\xC3\xA9 dans le s\xC3\xA9lecteur (ou \xC2\xAB Cr\xC3\xA9" "er X\xE2\x80\xA6 \xC2\xBB dans la liste des carr\xC3\xA9s) : la fen\xC3\xAAtre de cr\xC3\xA9" "ation demande la zone (IHM ou API), le type (celui de la case d'abord), la valeur initiale ou l'adresse, le commentaire. La variable et la saisie s'annulent d'un seul Ctrl+Z.",
         "variables-ihm", ""),
        note("1.11.3", "Scripts", Kind::New,
         "Exporter\xE2\x80\xA6 et Importer\xE2\x80\xA6 les scripts d'une vue, d'une popup, d'un symbole, d'un \xC3\xA9" "cran mod\xC3\xA8le, d'un en-t\xC3\xAAte ou d'un pied de page (OnOpen, OnCycle, OnClose) : un fichier texte .xpgst, lisible et modifiable. L'import coche chaque script (nouveau, identique, diff\xC3\xA9rent), remplace ou ajoute \xC3\xA0 la suite, en un seul Ctrl+Z ; un .st seul va dans l'\xC3\xA9v\xC3\xA9nement choisi.",
         "scripts", ""),
        note("1.11.3", "Scripts", Kind::New,
         "Exporter\xE2\x80\xA6 et Importer\xE2\x80\xA6 les op\xC3\xA9rateurs d'un symbole ou d'un type IHM, dans le m\xC3\xAAme format .xpgst : un op\xC3\xA9rateur de m\xC3\xAAme signature est remplac\xC3\xA9, un nouveau ajout\xC3\xA9, un op\xC3\xA9rateur refus\xC3\xA9 (en double, type inconnu) laiss\xC3\xA9 et dit.",
         "types-ihm", ""),
        // 1.11.2 (T2, tranches 41, 42 et 44 ; livree le 04/10/2026 a 6 h ; decisions 187, 201, 212 et 216) : l'export et
        // l'import (REP), les icones de l'aide a la saisie (API-V), l'instance unique (UNI), les blocages (b), (c),
        // (d) (BLK), les tutoriels et le projet modele livre avec l'exe (T1), les glyphes, les noms des langues et F1
        // (T2), D13 et le reste deja livre, puis les parametres des symboles (SYM, decision 240, tranche 46). Ni les
        // methodes, ni le .xpga (1.11.3). Deux cartes de la fenetre Nouveautes (decision 216), des lignes pour le reste.
        card("\xC3\x89" "diteur IHM", "", {"1.11.2.paquets", "1.11.2", "Exporter et importer symboles, types, fonctions et scripts",
         "Exporter les symboles\xE2\x80\xA6, les types\xE2\x80\xA6, les fonctions\xE2\x80\xA6, les scripts\xE2\x80\xA6 : un fichier (.xpgsymboles, .xpgtypes, .xpgfonctions, .xpgscripts) qui emporte ce dont ils ont besoin. Importer\xE2\x80\xA6 ouvre tout fichier fait par Exporter, ou glisse-le sur l'appli : chaque \xC3\xA9l\xC3\xA9ment et son \xC3\xA9tat dans ce projet, un nom en conflit \xC3\xA0 renommer ou \xC3\xA0 remplacer, un seul Ctrl+Z pour tout l'import.",
         "", "aide:paquets-symboles", "", "paquets-symboles"}),
        note("1.11.2", "\xC3\x89" "diteur IHM", Kind::New,
         "Un symbole emporte les symboles qu'il contient, ses images et ses styles ; une fonction, les fonctions qu'elle appelle et ses types ; tous, les variables IHM qu'ils emploient. \xC3\x80 l'import, un style du projet est gard\xC3\xA9 par d\xC3\xA9" "faut, Parcourir\xE2\x80\xA6 prend un autre fichier, et un fichier fait par une version plus r\xC3\xA9" "cente est refus\xC3\xA9, en le disant.",
         "paquets-symboles", ""),
        note("1.11.2", "\xC3\x89" "diteur IHM", Kind::Changed,
         "Importer des vues prend la m\xC3\xAAme fen\xC3\xAAtre : un tableau Nom / Sorte / Dans ce projet, les noms en conflit \xC3\xA0 trancher (Renommer en, Remplacer, Garder), le compte sur le bouton (\xC2\xAB Importer 8 \xC3\xA9l\xC3\xA9ments \xC2\xBB), Annuler.",
         "paquets-de-vues", ""),
        // Tranche 46 (SYM, decision 240 ; la demande du client de 21 h 58) : Dupliquer dans un symbole.
        note("1.11.2", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "Dupliquer\xE2\x80\xA6 dans un symbole accepte les param\xC3\xA8tres du symbole : le rep\xC3\xA8re $Value[0]$ d'un param\xC3\xA8tre Value : ARRAY[0..9] OF UINT existe (la 1.11.1 disait \xC2\xAB Value[0] n'existe pas \xC2\xBB et bloquait), ses bornes sont celles du param\xC3\xA8tre (Value[10] est hors des bornes 0..9), la colonne se pr\xC3\xA9remplit Value[1], Value[2]\xE2\x80\xA6 et Tableau\xE2\x80\xA6 propose Value. Hors d'un symbole, rien ne change.",
         "dupliquer-reperes", ""),
        // Tranche 47 (SYM 34ee461, decisions 240 et 248 ; son texte, « POUR T2 ») : la section Parametres du symbole de l'inspecteur.
        note("1.11.2", "\xC3\x89" "diteur IHM", Kind::New,
         "La section Param\xC3\xA8tres du symbole : dans l'inspecteur d'une instance, le champ Arguments d'un seul tenant laisse la place \xC3\xA0 une ligne par param\xC3\xA8tre d\xC3\xA9" "clar\xC3\xA9 dans le symbole, avec son type (Name \xC2\xB7 STRING, Value \xC2\xB7 ARRAY[0..9] OF UINT). Une case vide prend la valeur par d\xC3\xA9" "faut du symbole, montr\xC3\xA9" "e en gris ; le bouton fx y met une variable, un chemin (Armoires[1]) ou une expression. Chaque saisie s'annule d'un seul Ctrl+Z. Les anciens arguments sans nom (Voiture;50) sont relus dans l'ordre du symbole et r\xC3\xA9\xC3\xA9" "crits avec leur nom \xC3\xA0 la premi\xC3\xA8re saisie.",
         "symboles", ""),
        card("Champs et expressions", "", {"1.11.2.icones-saisie", "1.11.2", "Les ic\xC3\xB4nes de l'aide \xC3\xA0 la saisie",
         "Chaque proposition de l'aide \xC3\xA0 la saisie a son pictogramme : la nature par la forme, la provenance par la couleur du trait - automate en bleu, IHM en turquoise, objet en violet, syst\xC3\xA8me en ambre, standard en gris. Au pied de la liste, la l\xC3\xA9gende des couleurs, puis l'ic\xC3\xB4ne et les mots de la ligne choisie.",
         "", "aide:aide-saisie", "", "aide-saisie"}),
        note("1.11.2", "Application", Kind::New,
         "XPGAnalyser ne s'ouvre qu'une fois par session Windows : relanc\xC3\xA9 (raccourci, menu D\xC3\xA9marrer, un projet ou un export \xC3\xA0 ouvrir), il ram\xC3\xA8ne sa fen\xC3\xAAtre au premier plan, et le projet demand\xC3\xA9 s'y ouvre, apr\xC3\xA8s \xC2\xAB Les enregistrer d'abord \xC2\xBB ou \xC2\xAB Les abandonner \xC2\xBB si celui qui est ouvert est modifi\xC3\xA9.",
         "api-bandeaux", ""),
        note("1.11.2", "Application", Kind::Fixed,
         "Projets r\xC3\xA9" "cents, \xC2\xAB Ouvrir\xE2\x80\xA6 \xC2\xBB et \xC2\xAB Fermer le projet \xC2\xBB (le menu de la puce du projet, la palette) demandent d'abord d'enregistrer le projet ouvert s'il est modifi\xC3\xA9, comme l'accueil : ses modifications ne se perdent plus. Ctrl+O ouvre un projet depuis l'\xC3\xA9" "cran du projet.",
         "api-bandeaux", ""),
        note("1.11.2", "Application", Kind::Fixed,
         "La pastille de version du bandeau ne fige plus l'\xC3\xA9" "cran : elle relisait et comparait toute l'IHM \xC3\xA0 la derni\xC3\xA8re version, sur le fil de l'\xC3\xA9" "cran, \xC3\xA0 l'ouverture puis toutes les 30 s ; la comparaison se fait dans un fil \xC3\xA0 part, apr\xC3\xA8s un enregistrement ou une nouvelle version.",
         "versions", ""),
        note("1.11.2", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "IHM \xE2\x80\xBA Configuration \xE2\x80\xBA \xC3\x89quipements s'ouvre sans attendre : pour la carte m\xC3\xA9moire d'un \xC3\xA9quipement, la page relisait tout le projet pour chaque variable (36 s sur un projet de 953 variables) ; qui \xC3\xA9" "crit quoi est relev\xC3\xA9 une fois, et de nouveau quand le projet change.",
         "equipements", ""),
        note("1.11.2", "Application", Kind::Fixed,
         "Dans un rapport de plantage ou de blocage, l'erreur d'un script cyclique dit sa raison, au lieu d'un message vide.",
         "plantages", ""),
        note("1.11.2", "Installation", Kind::New,
         "Le projet mod\xC3\xA8le des tutoriels (Armoire_Gaz) est livr\xC3\xA9 avec l'appli, dans le dossier bac \xC3\xA0 c\xC3\xB4t\xC3\xA9 de l'exe : chaque tutoriel s'ouvre sur sa propre copie, sans toucher \xC3\xA0 tes projets. S'il manque, \xC2\xAB Tutoriel pas lanc\xC3\xA9 \xC2\xBB dit o\xC3\xB9 il est attendu et comment le remettre.",
         "didacticiel", ""),
        note("1.11.2", "Tutoriels", Kind::Changed,
         "\xC2\xAB Essayer \xC2\xBB sur une \xC3\xA9tape sans \xC2\xAB \xC3\x80 toi \xC2\xBB m\xC3\xA8ne \xC3\xA0 l'\xC2\xAB \xC3\x80 toi \xC2\xBB le plus proche, et le lecteur le dit ; un tutoriel sans \xC2\xAB \xC3\x80 toi \xC2\xBB montre les gestes, et le dit.",
         "didacticiel", ""),
        note("1.11.2", "Tutoriels", Kind::Changed,
         "Les tutoriels montrent ce dont ils parlent : celui d'une macro ouvre son formulaire, celui des symboles ouvre un symbole, celui d'un bloc ou d'un type de la biblioth\xC3\xA8que ouvre sa page dans le centre d'aide, ceux de Compiler et de Renommer montrent leur commande.",
         "didacticiel", ""),
        note("1.11.2", "Tutoriels", Kind::Changed,
         "Les tutoriels des sujets du menu \xC2\xAB ? \xC2\xBB (le menu Aide, le didacticiel, les nouveaut\xC3\xA9s, les plantages) ont leur \xC2\xAB \xC3\x80 toi \xC2\xBB \xC3\xA0 l'\xC3\xA9tape 2 (F1), et l'\xC3\xA9tape 3 ouvre la page du sujet dans le centre d'aide.",
         "menu-aide", ""),
        note("1.11.2", "Tutoriels", Kind::Changed,
         "Le bandeau \xC2\xAB BAC \xC3\x80 SABLE \xC2\xBB n'est plus affich\xC3\xA9 pendant un tutoriel.",
         "didacticiel", ""),
        note("1.11.2", "Tutoriels", Kind::Fixed,
         "Une copie rat\xC3\xA9" "e du projet mod\xC3\xA8le arr\xC3\xAAte le tutoriel en disant pourquoi et que faire (la place sur le disque, les droits d'\xC3\xA9" "criture, r\xC3\xA9installer) ; le message tient dans sa fen\xC3\xAAtre.",
         "didacticiel", ""),
        note("1.11.2", "Aide", Kind::Fixed,
         "F1 sur les onglets Variables IHM et Types IHM (IHM \xE2\x80\xBA Programmation g\xC3\xA9n\xC3\xA9rale) ouvre leur page du centre d'aide, et non plus celle des scripts.",
         "variables-ihm", ""),
        note("1.11.2", "Application", Kind::Fixed,
         "Sous Windows, les symboles de l'appli (\xE2\x96\xB8 \xE2\x96\xBE \xE2\x96\xB6 \xE2\x98\x85) sont dessin\xC3\xA9s par l'appli elle-m\xC3\xAAme : plus de \xC2\xAB ? \xC2\xBB \xC3\xA0 leur place, devant les chapitres du centre d'aide ou dans les menus.",
         "menu-aide", ""),
        note("1.11.2", "Application", Kind::Fixed,
         "Les langues dont la police de l'appli n'a pas l'\xC3\xA9" "criture se nomment en fran\xC3\xA7" "ais : Chinois, Japonais, Cor\xC3\xA9" "en, Hindi, Tha\xC3\xAF, Arabe, H\xC3\xA9" "breu.",
         "langues", ""),
        note("1.11.2", "Scripts", Kind::Fixed,
         "Dupliquer\xE2\x80\xA6 conna\xC3\xAEt les bornes des tableaux de l'automate \xC3\xA9" "crits sous API. : une case au-del\xC3\xA0 des bornes passe au rouge (\xC2\xAB hors des bornes de API.V (0..63) \xC2\xBB), comme pour une variable IHM, et Remplir \xE2\x80\xBA Tableau\xE2\x80\xA6 les prend.",
         "dupliquer-reperes", ""),
        note("1.11.2", "Scripts", Kind::Fixed,
         "Le cadenas d'une variable de l'automate en lecture seule dit pourquoi (une constante, une entr\xC3\xA9" "e %I, une ligne en lecture seule de la table des adresses) ; l'aide \xC3\xA0 la saisie montre L, et non plus L/\xC3\x89, pour une constante de l'automate.",
         "variables-api", ""),
        note("1.11.2", "Simulation", Kind::Changed,
         "Dans Simulation \xE2\x80\xBA D\xC3\xA9" "bogage, \xC2\xAB Qui a \xC3\xA9" "crit ? \xC2\xBB dit quand la section d'une ligne n'a pas tourn\xC3\xA9 au dernier cycle, sa condition d'activation \xC3\xA9tant fausse, comme sur l'automate : \xC2\xAB section inactive (condition fausse : configuree) \xC2\xBB.",
         "variables-api", ""),
        note("1.11.2", "Scripts", Kind::Fixed,
         "Un $ rest\xC3\xA9 seul dans un script ST dit o\xC3\xB9 il manque le $ de fin et comment l'\xC3\xA9" "crire, comme dans une case d'expression, au lieu de \xC2\xAB caract\xC3\xA8re inattendu \xC2\xBB.",
         "scripts", ""),
        note("1.11.2", "Scripts", Kind::Fixed,
         "L'infobulle d'une variable sans adresse ne dit plus deux fois de lui donner une adresse, et toutes les raisons du plan d'adressage te tutoient.",
         "variables-api", ""),
        // 1.11.1 (T2, tranche 30 ; decisions 104, 107, 108 ; C111-1, C111-2) : API. (API-M, API-V), les alarmes
        // de chaque objet dans Variables d'instances (T1, vi-1111), les deux corrections de l'aide.
        note("1.11.1", "Scripts", Kind::New,
         "Les variables de l'automate s'\xC3\xA9" "crivent aussi sous API. : API.<Unit\xC3\xA9>.<variable> pour une variable d'une unit\xC3\xA9 de programme, API.<globale> pour une globale, jusqu'aux membres des instances de DDT et de DFB et aux cases des tableaux (API.Armoires[0].ana.PT1.mes). Le nom sans API. marche toujours.",
         "variables-api", ""),
        note("1.11.1", "Scripts", Kind::New,
         "L'aide \xC3\xA0 la saisie propose API d\xC3\xA8s AP, puis les unit\xC3\xA9s et les globales, les variables d'une unit\xC3\xA9 (Publiques, Priv\xC3\xA9" "es, E/S) et les membres d'une instance, chacun avec son type et son acc\xC3\xA8s : L/\xC3\x89, L ou sans adresse.",
         "variables-api", ""),
        note("1.11.1", "Scripts", Kind::New,
         "Compiler et G\xC3\xA9n\xC3\xA9rer v\xC3\xA9rifient chaque API.\xE2\x80\xA6 : ce qui manque, avec le nom le plus proche ; une \xC3\xA9" "criture vers une variable en lecture seule est refus\xC3\xA9" "e, avec la raison ; une variable sans adresse avertit qu'elle n'est lue qu'en simulation.",
         "variables-api", ""),
        note("1.11.1", "\xC3\x89" "diteur IHM", Kind::Changed,
         "IHM \xE2\x80\xBA Configuration \xE2\x80\xBA Variables du programme est un arbre : les Globales, puis chaque unit\xC3\xA9 de programme (Publiques, Priv\xC3\xA9" "es, E/S), les instances de DDT et de DFB d\xC3\xA9pliables, la colonne Accessible (Lecture / \xC3\xA9" "criture, Lecture seule, Sans adresse \xE2\x80\x94 simulation seulement) et un cadenas sur chaque nom. Filtres : Toutes, Employ\xC3\xA9" "es par l'IHM, Accessibles en \xC3\xA9" "criture, Sans adresse. Un clic sur \xC2\xAB Utilis\xC3\xA9" "e par l'IHM \xC2\xBB m\xC3\xA8ne \xC3\xA0 chaque endroit ; le double-clic, le clic droit (Copier le nom, Ins\xC3\xA9rer dans le script montr\xC3\xA9, Emplois) ou glisser mettent le nom API.\xE2\x80\xA6 dans un script.",
         "variables-api", ""),
        // 1.11.1 : REP (reperes-1111) - les scripts ST, REP-10, le $ de fin oublie.
        note("1.11.1", "\xC3\x89" "diteur IHM", Kind::Changed,
         "Dans un script ST, les rep\xC3\xA8res $\xE2\x80\xA6$ sont transparents comme dans une expression (les cha\xC3\xAEnes et les commentaires gardent leurs $) ; le script d'une action \xC2\xAB Ex\xC3\xA9" "cuter un script \xC2\xBB se duplique avec ses rep\xC3\xA8res. En C et C++, rien ne change.",
         "dupliquer-reperes", ""),
        note("1.11.1", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "Sur un ancien rep\xC3\xA8re $Vanne$ dont le nom n'est pas une variable, la case ne propose plus un nom voisin (\xC2\xAB veux-tu dire \xE2\x80\xA6 \xC2\xBB) : elle dit qu'un rep\xC3\xA8re se remplit par \xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB.",
         "dupliquer-reperes", ""),
        note("1.11.1", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "Un $ de fin oubli\xC3\xA9 est dit \xC3\xA0 sa place : dans =$V[1].Ouv+$V[2].Ouv$, la case dit \xC2\xAB il manque le $ de fin de $V[1].Ouv : \xC3\xA9" "cris $V[1].Ouv$+$V[2].Ouv$ \xC2\xBB, au lieu de \xC2\xAB caract\xC3\xA8re inattendu \xC2\xBB au dernier $.",
         "dupliquer-reperes", ""),
        note("1.11.1", "Alarmes", Kind::New,
         "\xC2\xAB Variables d'instances \xC2\xBB montre, sous chaque objet, tout ce qu'il publie : ses param\xC3\xA8tres (une instance de symbole), son groupe d'alarmes avec le groupe de l'IHM auquel il est li\xC3\xA9 (AlarmLinkedGroup), et ses alarmes, chacune avec ses dix variables (Vue.Objet.Alarmes.<alarme>.Active\xE2\x80\xA6). Acked et Shelved s'\xC3\xA9" "crivent, avec la permission Acquitter ; le reste se lit.",
         "variables-instances", ""),
        // 1.11.1 : T1 (tuto-1111 094d842, vi-1111 c60694e) - la remise en place plus courte.
        note("1.11.1", "Tutoriels", Kind::Changed,
         "\xC2\xAB Remise en place\xE2\x80\xA6 \xC2\xBB est plus courte, avant chaque \xC2\xAB \xC3\x80 toi \xC2\xBB et quand tu sautes \xC3\xA0 une \xC3\xA9tape.",
         "objet-vanne", ""),
        note("1.11.1", "Aide", Kind::Fixed,
         "La fiche A4 des raccourcis porte le nom de l'appli, XPGAnalyser, comme \xC3\x80 propos et Signaler un probl\xC3\xA8me.",
         "page-raccourcis", ""),
        note("1.11.1", "Aide", Kind::Fixed,
         "Le \xC2\xAB Voir aussi \xC2\xBB de la page du menu Aide m\xC3\xA8ne aux nouveaut\xC3\xA9s et \xC3\xA0 leurs rep\xC3\xA8res orange, plus aux nouveaut\xC3\xA9s de la 1.10.",
         "menu-aide", ""),
        card("Aide", "", {"1.11.centre-aide", "1.11", "Un seul centre d'aide",
         "Il remplace les quatre \xC3\xA9" "crans d'avant (l'aide g\xC3\xA9n\xC3\xA9rale, l'aide de l'IHM, les macros, les blocs DFB / DDT) : un arbre de neuf chapitres, une recherche unique (Ctrl+F) qui groupe ses r\xC3\xA9sultats, Pr\xC3\xA9" "c\xC3\xA9" "dent / Suivant (Alt+\xE2\x86\x90 / Alt+\xE2\x86\x92), les favoris (\xE2\x98\x85) et les r\xC3\xA9" "cents. F1 l'ouvre au sujet de l'endroit.",
         "", "aide:menu-aide", "", "menu-aide"}),
        card("Aide", "", {"1.11.raccourcis", "1.11", "La page des raccourcis",
         "Group\xC3\xA9" "e par contexte, elle se cherche, dessine les touches et s'imprime en A4 (la fiche s'ouvre dans ton navigateur, qui l'imprime ou en fait un PDF). Ctrl+H reste \xC3\xA0 l'Historique ; l'import de la configuration mat\xC3\xA9rielle (.XHW), qui le portait aussi, passe \xC3\xA0 Ctrl+Maj+O.",
         "", "aide:page-raccourcis", "", "page-raccourcis"}),
        note("1.11", "Aide", Kind::New,
         "Les notes de version, de la 1.8.0 \xC3\xA0 la 1.11, group\xC3\xA9" "es par domaine : chaque ligne a son tutoriel, et \xC2\xAB Me montrer \xC2\xBB quand il y a une \xC3\xA9tape \xC3\xA0 montrer.",
         "notes-1.11", ""),
        card("Aide", "", {"1.11.signaler", "1.11", "Signaler un probl\xC3\xA8me",
         "Trois questions, une capture, les pi\xC3\xA8" "ces jointes au choix (la version, le syst\xC3\xA8me, le r\xC3\xA9sum\xC3\xA9 du projet sans ses donn\xC3\xA9" "es, le journal), l'aper\xC3\xA7u, puis un zip dans tes donn\xC3\xA9" "es (dossier signalements) ou \xC2\xAB Copier le texte \xC2\xBB. Rien ne part par le r\xC3\xA9seau.",
         "", "aide:page-signaler", "", "page-signaler"}),
        note("1.11", "Aide", Kind::Changed,
         "L'aide parle fran\xC3\xA7" "ais jusqu'au bout : l'aide g\xC3\xA9n\xC3\xA9rale, les formats d'import et l'aide de leurs colonnes, et l'aide des 31 macros de la biblioth\xC3\xA8que prennent leurs accents et le tutoiement. \xC2\xAB Enregistrer les fichiers d'exemple \xC2\xBB (Importer un fichier) les \xC3\xA9" "crit en CSV UTF-8 avec BOM : Excel lit les accents.",
         "api-importer", ""),
        // Tranche 18 : F1 ouvre le centre partout (tranche 16 de T2, decision du chef).
        note("1.11", "Aide", Kind::Changed,
         "F1 ouvre le centre d'aide partout, au sujet de l'endroit : l'objet choisi dans l'\xC3\xA9" "diteur de vue, le mot sous le curseur d'un script, le volet ou le n\xC5\x93ud de l'arbre, chaque onglet de l'automate et de la simulation. Il n'ouvre plus l'onglet \xC2\xAB IHM \xC2\xB7 Aide \xC2\xBB, ni le Didacticiel de l'API : celui-ci reste dans le centre (\xC2\xAB Didacticiel de l'API \xE2\x80\xBA \xC2\xBB, sur chaque page de L'automate).",
         "menu-aide", ""),
        card("Tutoriels", "", {"1.11.tutoriels", "1.11", "Chaque sujet a son tutoriel",
         "Anim\xC3\xA9, il se joue dans un bac \xC3\xA0 sable (une copie d'Armoire_Gaz) : selon le sujet, le curseur bouge, le texte se tape, l'\xC3\xA9l\xC3\xA9ment vis\xC3\xA9 est encadr\xC3\xA9 ; une bulle explique chaque \xC3\xA9tape. La barre du lecteur : Lecture / Pause, les \xC3\xA9tapes, la vitesse ; \xC2\xAB \xC3\x80 toi \xC2\xBB, dans environ un tutoriel sur deux, te fait faire l'\xC3\xA9tape et la v\xC3\xA9rifie.",
         "", "tuto:objet-vanne", "", "objet-vanne"}),
        note("1.11", "Tutoriels", Kind::New,
         "Les tutoriels d'objets se jouent comme dans l'\xC3\xA9" "diteur : l'instance pos\xC3\xA9" "e depuis la biblioth\xC3\xA8que, puis ses param\xC3\xA8tres un \xC3\xA0 un, avec l'\xC3\xA9tape \xC2\xAB Choisis la variante \xC2\xBB (la vanne : manuelle, motoris\xC3\xA9" "e, pneumatique, r\xC3\xA9glante).",
         "objet-vanne", "1"),
        note("1.11", "Tutoriels", Kind::New,
         "Le tutoriel d'un script : la rampe de V[0].Pos \xC3\xA9" "crite avec l'aide \xC3\xA0 la saisie, une faute expr\xC3\xA8s que Compiler (F7) trouve, puis la vanne qui s'ouvre en simulation.",
         "scripts", "4"),
        note("1.11", "Tutoriels", Kind::New,
         "Les tutoriels des objets, des sujets de l'aide, des macros, des blocs et des expressions sont d\xC3\xA9" "duits de leur page, et le centre le dit : \xC2\xAB Tous les sujets ont leur tutoriel \xC2\xBB. Environ un sur deux propose un \xC2\xAB \xC3\x80 toi \xC2\xBB : presque tous ceux des objets et des expressions te font faire leur sujet ; d'autres, dont ceux des blocs DFB, te font seulement d\xC3\xA9marrer l'IHM avec F8. Les autres, dont ceux des macros et des types (DDT), se regardent, sans \xC2\xAB \xC3\x80 toi \xC2\xBB pour l'instant.",
         "didacticiel", ""),
        // Tranche 18 : les finitions de T1 (78dd83f) : le bac, la question, Quitter.
        note("1.11", "Tutoriels", Kind::New,
         "Le bac \xC3\xA0 sable s'appelle Armoire_Gaz, dans le bandeau du haut comme dans l'arbre, et il n'entre pas dans tes projets r\xC3\xA9" "cents.",
         "objet-vanne", ""),
        note("1.11", "Tutoriels", Kind::New,
         "Ton projet a des modifications pas encore enregistr\xC3\xA9" "es ? Le tutoriel demande d'abord \xC2\xAB Enregistrer ton projet ? \xC2\xBB : Annuler ne lance rien, \xC2\xAB Enregistrer et lancer \xC2\xBB l'enregistre, puis lance le tutoriel.",
         "objet-vanne", ""),
        note("1.11", "Tutoriels", Kind::New,
         "Quitter te ram\xC3\xA8ne au centre, sur le sujet, et rouvre le projet que tu avais avant le tutoriel (sans projet ouvert avant, tu retrouves l'accueil).",
         "objet-vanne", ""),
        card("Expressions", "", {"1.11.expressions", "1.11", "La page des expressions",
         "Les 11 types (BOOL, entier, r\xC3\xA9" "el, texte, texte \xC3\xA0 trous, couleur, dur\xC3\xA9" "e, vue, \xC3\xA9num\xC3\xA9ration, membre, liste de choix), leur syntaxe, leurs op\xC3\xA9rateurs, leurs exemples et leurs erreurs courantes, avec un champ \xC2\xAB Essaie ici \xC2\xBB qui \xC3\xA9value en direct.",
         "", "aide:expr-couleur", "", "expr-couleur"}),
        note("1.11", "Expressions", Kind::New,
         "Le tutoriel d'une expression : la couleur de la cuve suit V[0].Pos, avec SEL(G, si FAUX, si VRAI).",
         "champs-expressions", ""),
        // Tranche 18 : T3 (68cd6dc, decision 2 du chef) : Remplacer par, les croisements ; R111 (cc69bb3) : T3-6.
        note("1.11", "Expressions", Kind::New,
         "Quand le type ne convient pas, la page des expressions le dit, garde la valeur et son type, et propose la conversion s\xC3\xBBre, \xC3\xA0 appliquer d'un clic par \xC2\xAB Remplacer par \xC2\xBB : TO_STRING(\xE2\x80\xA6), REAL_TO_INT(\xE2\x80\xA6), ou, en case \xC3\x89num\xC3\xA9ration, une valeur (T_MODE#Auto), SEL(condition, \xE2\x80\xA6) ou TO_T_MODE(\xE2\x80\xA6).",
         "expr-enumeration", ""),
        note("1.11", "Expressions", Kind::New,
         "Les croisements des types : une case Texte prend tout, sauf une valeur d'\xC3\xA9num\xC3\xA9ration (elle montrerait son num\xC3\xA9ro) ; une case R\xC3\xA9" "el prend un entier ; un REAL ne va pas dans une case Entier.",
         "expr-texte", ""),
        note("1.11", "Expressions", Kind::Fixed,
         "Le champ \xC2\xAB Essaie ici \xC2\xBB ne reste plus d\xC3\xA9" "cal\xC3\xA9 apr\xC3\xA8s un long exemple (Couleur) : le d\xC3\xA9" "but du texte n'est plus cach\xC3\xA9. La correction vaut pour tous les champs de texte de l'appli.",
         "expr-couleur", ""),
        card("Projet", "", {"1.11.icones-compilable", "1.11", "Les ic\xC3\xB4nes compilable et g\xC3\xA9n\xC3\xA9rable",
         "\xC3\x80 droite de chaque section et de chaque script, dans les arbres ; les unit\xC3\xA9s, les t\xC3\xA2" "ches et les dossiers les r\xC3\xA9sument. Une infobulle dit pourquoi, et deux filtres montrent \xC2\xAB Ce qui ne compile pas \xC2\xBB et \xC2\xAB Ce qui n'est pas g\xC3\xA9n\xC3\xA9r\xC3\xA9 \xC2\xBB.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Scripts", "analysis.explorer", "verifier"}),
        // Tranche 18 : R111, decision 6 (Verifier, les conditions d'activation) et les finitions (T3-11, T1-1).
        // Tranche 19 : accordee a ce que R111 a fait (0d7f618, decision 15) : la cloche a l'ouverture, le
        // meme message dans Verifier l'ordre, Ne plus le dire pour ce projet (les reglages de l'utilisateur).
        note("1.11", "Projet", Kind::New,
         "\xC3\x80 l'ouverture d'un projet import\xC3\xA9 avant la 1.8.0 dont les sections de t\xC3\xA2" "che n'ont pas leurs conditions d'activation (en simulation, elles tournent alors \xC3\xA0 chaque cycle), la cloche le dit, avec le rem\xC3\xA8" "de : Fichier \xE2\x80\xBA Importer un .XPG (nouveau MAST)\xE2\x80\xA6, qui les r\xC3\xA9tablit depuis le .XPG d'origine. \xC2\xAB V\xC3\xA9rifier l'ordre \xC2\xBB dit le m\xC3\xAAme message. Pour un projet qui n'en a vraiment pas, \xC2\xAB Ne plus le dire pour ce projet \xC2\xBB le tait : tes r\xC3\xA9glages le retiennent, le projet ne change pas.",
         "api-ordre", ""),
        note("1.11", "Projet", Kind::Fixed,
         "Le pluriel juste : \xC2\xAB 1 section \xC2\xBB, \xC2\xAB 2 sections \xC2\xBB dans l'arbre, sur le tableau de bord de l'API (1 entr\xC3\xA9" "e, 1 module), dans les membres et les lignes des macros, et sur la page Alarmes (\xC2\xAB 1 alarme \xC2\xBB).",
         "api-arbre-lot8", ""),
        note("1.11", "Projet", Kind::Fixed,
         "Dans le bandeau du haut, \xC3\xA0 l'\xC3\xA9troit, le nom du projet coup\xC3\xA9 finit par \xC2\xAB \xE2\x80\xA6 \xC2\xBB (\xC2\xAB Armoir\xE2\x80\xA6 \xC2\xBB) et son infobulle donne le nom entier ; la version aussi. \xC2\xAB modifi\xC3\xA9 \xC2\xBB ne passe plus sous Enregistrer.",
         "api-bandeaux", ""),
        note("1.11", "\xC3\x89" "diteur IHM", Kind::New,
         "Les listes d'alarmes et l'historique ont une taille de texte r\xC3\xA9glable : jusqu'ici, le texte faisait environ 11 px.",
         "alarmes-objets", ""),
        // Tranche 25 (decision 69, avec REP) : les reperes $...$ deviennent transparents pour le calcul.
        // Cette ligne n'entre dans la 1.11 que si REP y entre.
        note("1.11", "\xC3\x89" "diteur IHM", Kind::Changed,
         "Les rep\xC3\xA8res $\xE2\x80\xA6$ ne bloquent plus rien : un $\xE2\x80\xA6$ entoure le morceau d'une expression qui varie quand on duplique (=$V[1].Ouv$, =$V[0]$.Nom, =$V[0].Pos > 10$), et ses $ ne changent pas le calcul : la case se calcule, Compiler et G\xC3\xA9n\xC3\xA9rer n'en disent plus rien. \xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB pr\xC3\xA9remplit chaque rep\xC3\xA8re, ses indices d\xC3\xA9" "cal\xC3\xA9s (V[2].Ouv, V[3].Ouv\xE2\x80\xA6), les copies gardent leurs $, et ce qui n'est pas marqu\xC3\xA9 ne varie pas.",
         "dupliquer-reperes", ""),
        note("1.11", "\xC3\x89" "diteur IHM", Kind::Changed,
         "\xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB propose aussi \xC2\xAB veux-tu dire \xE2\x80\xA6 ? \xC2\xBB sur une case qui vise un membre de l'automate (Armoires[0].sorties.V8).",
         "dupliquer-reperes", ""),
        note("1.11", "\xC3\x89" "diteur IHM", Kind::Fixed,
         "Apr\xC3\xA8s Ctrl+Y, la s\xC3\xA9lection revient aux copies de \xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB.",
         "dupliquer-reperes", ""),
        // Tranche 18 : R111 (49ffe12, T2-17).
        note("1.11", "Inspecteur", Kind::Fixed,
         "Les noms entiers dans l'inspecteur : \xC2\xAB Taille du texte \xC2\xBB, \xC2\xAB Miroir horizontal \xC2\xBB ne sont plus coup\xC3\xA9s pour y mettre le type ; le type (\xC2\xAB nombre \xC2\xBB, \xC2\xAB BOOL \xC2\xBB) ne s'ajoute au bout du nom que si le nom entier tient.",
         "proprietes", ""),
        card("Alarmes", "", {"1.11.lier-alarmes", "1.11", "\xC2\xAB Lier\xE2\x80\xA6 \xC2\xBB \xC3\xA0 cocher",
         "La ligne \xC2\xAB Lier\xE2\x80\xA6 \xC2\xBB d'un groupe d'alarmes ouvre une fen\xC3\xAAtre \xC3\xA0 cocher (les groupes d'objets, les vues, les symboles), au lieu de la case o\xC3\xB9 l'on \xC3\xA9" "crivait \xC2\xAB A ; B ; symbole:Sym \xC2\xBB. Ctrl+Z rend tout en une fois.",
         "", "aide:groupes-objets", "", "groupes-objets"}),
        // Recette R111-2 : Me montrer et le sujet de ces deux lignes, « Le groupe d'alarmes d'un
        // objet » (groupes-objets), qui decrit Lier..., sa fenetre a cocher et la priorite par
        // defaut d'une nouvelle alarme ; le sujet Alarmes n'en dit rien.
        note("1.11", "Alarmes", Kind::Changed,
         "Une nouvelle alarme (IHM \xE2\x80\xBA Alarmes) prend la priorit\xC3\xA9 par d\xC3\xA9" "faut de son groupe.",
         "groupes-objets", ""),
        note("1.11", "Alarmes", Kind::New,
         "Les couleurs du groupe s'affichent dans le compteur des alarmes et dans le r\xC3\xA9sum\xC3\xA9 par zone.",
         "objet-compteur-d-alarmes", ""),
        // Tranche 18 : R111 (67d1161, T2-16).
        note("1.11", "Alarmes", Kind::Fixed,
         "Apr\xC3\xA8s Ctrl+Z, la page Alarmes ne redit plus \xC2\xAB Lier\xE2\x80\xA6 \xC2\xBB : sa ligne d'\xC3\xA9tat retrouve son r\xC3\xA9sum\xC3\xA9.",
         "alarmes", ""),
        note("1.10.4", "Scripts", Kind::Fixed,
         "Le plantage pendant la frappe est corrig\xC3\xA9 : un FOR pas encore fini \xC3\xA0 la fin d'un script (comme IF, ELSIF, WHILE, CASE ou FOR EACH) faisait lire l'analyse au-del\xC3\xA0 du texte, et l'appli se fermait (std::bad_alloc). 37 999 textes \xC3\xA0 moiti\xC3\xA9 tap\xC3\xA9s passent maintenant l'essai.",
         "erreurs-scripts", ""),
        note("1.10.4", "Scripts", Kind::New,
         "Si un diagnostic \xC3\xA9" "choue quand m\xC3\xAAme, l'appli ne tombe plus : la ligne du curseur dit \xC2\xAB diagnostic indisponible pour cette ligne \xC2\xBB (le d\xC3\xA9tail est dans Aide \xE2\x80\xBA Journal interne) et la frappe continue, dans les scripts, les fonctions et les op\xC3\xA9rateurs de l'IHM.",
         "erreurs-scripts", ""),
        note("1.10.4", "\xC3\x89" "diteur IHM", Kind::New,
         "\xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB avec les indices de tableau : un rep\xC3\xA8re qui attend une variable est pr\xC3\xA9rempli depuis l'original (V[0], V[1], V[2]\xE2\x80\xA6), avec une note bleue et Vider. Remplir \xE2\x80\xBA Tableau\xE2\x80\xA6 propose les tableaux du projet (\xC2\xAB V (0..63) \xC2\xBB).",
         "dupliquer-reperes", ""),
        note("1.10.4", "\xC3\x89" "diteur IHM", Kind::New,
         "La s\xC3\xA9rie accepte V[{n}], et une case tap\xC3\xA9" "e \xC3\xA0 la main (V[3], V[3].Ouv) est v\xC3\xA9rifi\xC3\xA9" "e sur les bornes et les membres du tableau. Hors des bornes, elle passe en rouge (\xC2\xAB V[64] : hors des bornes de V (0..63) \xC2\xBB) et Dupliquer attend.",
         "dupliquer-reperes", ""),
        note("1.10.4", "\xC3\x89" "diteur IHM", Kind::Changed,
         "Un rep\xC3\xA8re rest\xC3\xA9 dans un objet ($Vanne$) n'est plus une erreur de Compiler, mais un avertissement clair. Le bouton Remplacer\xE2\x80\xA6 ouvre Dupliquer \xC3\xA0 0 copie, pour le remplir dans l'original ; G\xC3\xA9n\xC3\xA9rer bloque toujours, et dit pourquoi.",
         "dupliquer-reperes", ""),
        note("1.10.4", "Objets", Kind::New,
         "La vanne 3 voies, dans la famille Synoptique, juste apr\xC3\xA8s la Vanne : m\xC3\xA9langeuse ou r\xC3\xA9partitrice, boisseau en T ou en L, la voie active par un entier (Position) ou par deux bool\xC3\xA9" "ens. Le passage prend sa couleur, clignote en ambre en mouvement et passe au rouge en d\xC3\xA9" "faut ; ferm\xC3\xA9" "e, elle est grise.",
         "objet-vanne-3-voies", ""),
        note("1.10.4", "Objets", Kind::New,
         "La vanne 3 voies a ses alarmes par d\xC3\xA9" "faut, son aide F1, son exemple et son tutoriel ; Compiler signale une voie \xC3\xA9" "crite \xC3\xA0 la main qui n'existe pas. Une 1.10.3 ouvre le projet sans planter : elle montre un rectangle, qui garde les propri\xC3\xA9t\xC3\xA9s de la vanne.",
         "objet-vanne-3-voies", ""),
        note("1.10.4", "Inspecteur", Kind::Changed,
         "L'inspecteur est rang\xC3\xA9 : d'abord les sections communes \xC3\xA0 tous les objets, dans le m\xC3\xAAme ordre (Objet, Position et taille, Apparence, S\xC3\xA9" "curit\xC3\xA9, Alarmes de l'objet), puis une section au nom de l'objet (\xC2\xAB Vanne \xC2\xBB, \xC2\xAB Bouton \xC2\xBB\xE2\x80\xA6).",
         "proprietes", ""),
        note("1.10.4", "Inspecteur", Kind::Changed,
         "Plus de doublon dans l'inspecteur : la section Animation est partie (la Valeur n'appara\xC3\xAEt plus qu'une fois), et Niveau d'acc\xC3\xA8s et Profil requis ne font plus qu'une case, Niveau d'acc\xC3\xA8s. Les filtres fx, Rep\xC3\xA8res et Non vides comptent juste.",
         "proprietes", ""),
        note("1.10.4", "Inspecteur", Kind::Changed,
         "L'onglet Contenu est cach\xC3\xA9 pour un objet qui n'a rien \xC3\xA0 y r\xC3\xA9gler (une vanne, un bouton, la vue seule) ; il appara\xC3\xAEt pour un groupe, un conteneur, un cadre ou un panneau, avec la liste de leurs objets.",
         "contenu", ""),
        note("1.10.4", "Inspecteur", Kind::Changed,
         "La section Communication est retir\xC3\xA9" "e, sans rien perdre : la Variable API passe en t\xC3\xAAte de la section de l'objet, sous le nom de ce qu'elle fait (Variable \xC3\xA9" "crite, Variable mesur\xC3\xA9" "e\xE2\x80\xA6), et l'adresse automate dans son infobulle. Le lien avec l'automate se fait tout seul.",
         "proprietes", ""),
        note("1.10.4", "Aide", Kind::Changed,
         "L'aide F1 suit ces changements : Dupliquer avec des rep\xC3\xA8res (un \xC3\xA9l\xC3\xA9ment de tableau, Compiler et Remplacer\xE2\x80\xA6), le diagnostic indisponible des scripts, les sections de l'inspecteur, l'onglet Contenu, la vanne 3 voies (son sujet, son exemple, son tutoriel) et le synoptique.",
         "menu-aide", ""),
        note("1.10.3", "\xC3\x89" "diteur IHM", Kind::New,
         "L'explorateur d'objets de la vue d\xC3\xA9plie chaque objet par familles, comme l'arbre de l'application : Actions, Liens fx, Param\xC3\xA8tres, Alarmes, Animations, Rep\xC3\xA8res, \xC3\x89l\xC3\xA9ments, S\xC3\xA9" "curit\xC3\xA9, Op\xC3\xA9rateurs. Un clic va \xC3\xA0 l'onglet ou \xC3\xA0 la case de la ligne.",
         "editeur", ""),
        note("1.10.3", "\xC3\x89" "diteur IHM", Kind::New,
         "Les rep\xC3\xA8res $Nom$ se voient : la famille Rep\xC3\xA8res des deux explorateurs ($Vanne$ \xC3\x97 3 \xE2\x80\x94 Valeur, Ouverture, Libell\xC3\xA9), la pastille ambre avec un $, le rep\xC3\xA8re surlign\xC3\xA9 dans l'inspecteur et son infobulle.",
         "dupliquer-reperes", ""),
        note("1.10.3", "\xC3\x89" "diteur IHM", Kind::New,
         "Les filtres de l'inspecteur : fx, $ Rep\xC3\xA8res et Non vides, chacun avec son nombre. Ils se cumulent, et l'\xC3\xA9tat est gard\xC3\xA9 pour le prochain objet.",
         "proprietes", ""),
        note("1.10.3", "\xC3\x89" "diteur IHM", Kind::Changed,
         "\xC2\xAB Nouvelle vue / popup / symbole \xC2\xBB ne montre que les mod\xC3\xA8les du r\xC3\xB4le choisi, plus \xC2\xAB Vide \xC2\xBB ; \xC2\xAB Autres mod\xC3\xA8les \xC2\xBB dispara\xC3\xAEt.",
         "modeles-de-vues", ""),
        note("1.10.3", "Simulation", Kind::Changed,
         "La barre de Simulation \xC2\xB7 IHM s'arr\xC3\xAAte au Plein \xC3\xA9" "cran. Ses autres commandes passent au clic droit sur la barre, qui dit o\xC3\xB9 chacune se trouve aussi ; Ctrl+Alt+S ouvre la page Simulation.",
         "simulation-zoom", ""),
        note("1.10.3", "Simulation", Kind::New,
         "Une expression qui garde un rep\xC3\xA8re n'est pas calcul\xC3\xA9" "e : l'onglet Expressions dit \xC2\xAB rep\xC3\xA8re non remplac\xC3\xA9 : $Vanne$ \xC2\xBB, en ambre, et la ligne d'\xC3\xA9tat les compte.",
         "simulation", ""),
        note("1.10.3", "Simulation", Kind::Fixed,
         "Les erreurs de calcul de l'onglet Expressions sont en fran\xC3\xA7" "ais : \xC2\xAB argument invalide \xC2\xBB, \xC2\xAB \xE2\x80\xA6 n'est pas d\xC3\xA9" "clar\xC3\xA9" "e \xC2\xBB, \xC2\xAB division par z\xC3\xA9ro \xC2\xBB (avant : \xC2\xAB invalid argument \xC2\xBB).",
         "simulation", ""),
        note("1.10.3", "Aide", Kind::Changed,
         "L'aide F1 suit ces changements : l'explorateur par familles, la famille Rep\xC3\xA8res, les filtres de l'inspecteur, la galerie du seul r\xC3\xB4le, la barre de Simulation \xC2\xB7 IHM et son clic droit.",
         "menu-aide", ""),
        note("1.10.3", "Corrig\xC3\xA9", Kind::Fixed,
         "Apr\xC3\xA8s un clic dans l'explorateur d'objets, F1, F3, F5 et F7 \xC3\xA0 F12 passent : F8 d\xC3\xA9marre l'IHM, F7 compile. F2, Suppr et Ctrl+Z/Y/C/V restent \xC3\xA0 l'explorateur.",
         "raccourcis", ""),
        note("1.10.3", "Corrig\xC3\xA9", Kind::Fixed,
         "Dans l'arbre de l'application, un clic sur une famille, une de ses lignes ou un rep\xC3\xA8re va au bon endroit : un rep\xC3\xA8re ouvre sa case dans l'inspecteur ; une action, l'onglet Actions.",
         "alarmes-dans-arbre", ""),
        note("1.10.2", "Scripts", Kind::Fixed,
         "L'erreur d'un script est montr\xC3\xA9" "e \xC3\xA0 la bonne ligne : celle du dernier mot \xC3\xA9" "crit, et non la ligne vide qui suit.",
         "erreurs-scripts", ""),
        note("1.10.2", "\xC3\x89" "diteur IHM", Kind::Changed,
         "\xC2\xAB Nouvelle vue / popup / symbole \xC2\xBB suit ses listes : le R\xC3\xB4le et le Mod\xC3\xA8le mettent \xC3\xA0 jour le titre, le nom, la taille et l'aper\xC3\xA7u.",
         "modeles-de-vues", ""),
        note("1.10.2", "\xC3\x89" "diteur IHM", Kind::New,
         "Treize mod\xC3\xA8les en plus (19 au lieu de 6) : des vues, des popups, des symboles, l'en-t\xC3\xAA" "te et le pied de page.",
         "modeles-de-vues", ""),
        note("1.10.2", "\xC3\x89" "diteur IHM", Kind::New,
         "\xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB (Ctrl+D) avec des rep\xC3\xA8res ($Vanne$) : le tableau des copies, Remplir, la pose sur X, Y ou en grille, et une seule commande \xC3\xA0 annuler.",
         "dupliquer-reperes", ""),
        note("1.10.2", "\xC3\x89" "diteur IHM", Kind::New,
         "L'aide \xC3\xA0 la saisie conna\xC3\xAEt les objets des vues et les param\xC3\xA8tres des instances ; l'arbre d\xC3\xA9plie un objet par familles.",
         "aide-saisie", ""),
        note("1.10.2", "\xC3\x89" "diteur IHM", Kind::New,
         "Une valeur d'\xC3\xA9num\xC3\xA9ration \xC3\xA9" "crite avec son type (Mode = T_MODE#Auto) se lit dans une case d'expression d'une vue, comme dans le programme ; TO_STRING(Mode) rend \xC2\xAB Auto \xC2\xBB et V\xC3\xA9rifier ne la signale plus.",
         "champs-expressions", ""),
        note("1.10.2", "Alarmes", Kind::New,
         "Les groupes d'alarmes : un comportement par groupe (priorit\xC3\xA9, couleurs, acquittement, son, zone, niveau, archivage) et les objets qui leur sont li\xC3\xA9s.",
         "alarmes", ""),
        note("1.10.2", "Simulation", Kind::Fixed,
         "La croix d'une popup la ferme aussit\xC3\xB4t et la vue dessous r\xC3\xA9pond ; \xC3\x89" "chap ferme la popup du dessus.",
         "popups", ""),
        note("1.10.2", "Simulation", Kind::New,
         "Le simulateur suit les param\xC3\xA8tres des unit\xC3\xA9s de programme reli\xC3\xA9s aux globales, les conditions d'activation des sections, STRING_TO_ASCII et ASCII_TO_STRING.",
         "simulation", ""),
        note("1.10.2", "Aide", Kind::New,
         "L'aide sur la communication : le principe, les moyens, les types d'acc\xC3\xA8s, le lexique. Les tableaux de l'aide passent \xC3\xA0 la ligne.",
         "communication", ""),
        note("1.10.2", "Aide", Kind::Fixed,
         "Les exemples de SEL lus \xC3\xA0 l'envers sont corrig\xC3\xA9s : SEL(G, si FAUX, si VRAI), comme la norme CEI.",
         "champs-expressions", ""),
        note("1.10.2", "Application", Kind::New,
         "Si XPGAnalyser plante ou ne r\xC3\xA9pond plus : un rapport dans crashs, la fen\xC3\xAAtre au lancement suivant et Aide \xE2\x80\xBA Journal interne.",
         "plantages", ""),
        note("1.10.1", "Simulation", Kind::Fixed,
         "Simulation \xC2\xB7 IHM : la vue suit en direct, m\xC3\xAAme apr\xC3\xA8s un passage sur un autre onglet (la vanne restait \xC3\xA0 12 %). Une seule horloge, onglet visible ou cach\xC3\xA9 ; la vue relit aussit\xC3\xB4t 14 fa\xC3\xA7ons d'\xC3\xA9" "crire.",
         "scripts", "7"),
        note("1.10.1", "Simulation", Kind::New,
         "Le Journal de Simulation \xC2\xB7 IHM se vide : \xC2\xAB Vider \xC2\xBB dans sa barre et au clic droit ; l'historique des alarmes n'est pas touch\xC3\xA9.",
         "simulation", ""),
        note("1.10.1", "Op\xC3\xA9rateurs", Kind::Changed,
         "a, b et Resultat sont dits au-dessus du script de chaque op\xC3\xA9rateur ; l'aide \xC3\xA0 la saisie et Compiler les connaissent (a. propose les membres).",
         "operateurs", ""),
        note("1.10.1", "Op\xC3\xA9rateurs", Kind::New,
         "La fen\xC3\xAAtre \xC2\xAB Ajouter un op\xC3\xA9rateur \xC2\xBB : le genre, l'op\xC3\xA9rande ou la cible, la signature, la l\xC3\xA9gende et le script pr\xC3\xA9rempli.",
         "operateurs", ""),
        note("1.10.1", "Champs et expressions", Kind::Changed,
         "Les noms et les invites des cases perdent \xC2\xAB (expression) \xC2\xBB et \xC2\xAB ou = expression\xE2\x80\xA6 \xC2\xBB : la pastille fx et le type le disent d\xC3\xA9j\xC3\xA0.",
         "champs-expressions", "1"),
        note("1.10.1", "Corrig\xC3\xA9", Kind::Fixed,
         "Dans un \xC3\xA9" "diteur de code \xC3\xA9troit, le mot \xC2\xAB modifi\xC3\xA9 \xC2\xBB ne couvre plus la premi\xC3\xA8re ligne ; une ligne trop longue finit par \xC2\xAB \xE2\x80\xA6 \xC2\xBB.",
         "scripts", ""),
        // ---- 1.10.0 : les cartes de la fenetre Nouveautes (memes id, meme ordre qu'avant) ----
        //  Les widgets : /home/claude/v110/avancement/P-identifiants.md (un motif
        //  avec '*' quand l'identifiant depend de l'ecran : "hmi.view.*.props").
        card("Champs et expressions", "3", {"1.10.expressions", "1.10.0", "Les champs \xC3\xA0 expression, partout pareils",
         "Vide, un champ dit qu'il accepte une expression (fx en creux, \xC2\xAB valeur ou = expression \xC2\xBB) ; "
         "le = reste affich\xC3\xA9 devant l'expression, et sa croix la retire.",
         "", "arbre:IHM/Vues/", "hmi.view.*.props", "champs-expressions"}),
        card("Champs et expressions", "2", {"1.10.saisie-typee", "1.10.0", "L'aide \xC3\xA0 la saisie selon le type du champ",
         "Un champ BOOL propose d'abord les variables BOOL, une couleur les couleurs, un nom de vue les vues ; "
         "le reste vient plus bas, gris\xC3\xA9, avec son type.",
         "", "arbre:IHM/Vues/", "hmi.view.*.props", "champs-expressions"}),
        card("Simulation", "", {"1.10.simulation-api-ihm", "1.10.0", "L'API et l'IHM d\xC3\xA9marrent chacune de son c\xC3\xB4t\xC3\xA9",
         "D\xC3\xA9marrer l'API, D\xC3\xA9marrer l'IHM ou D\xC3\xA9marrer les deux : deux pastilles disent en permanence ce qui tourne, "
         "et rien ne d\xC3\xA9marre l'autre sans le dire.",
         "", "arbre:Simulation/IHM", "hmi.simulation.hmiState", "simulation-api-ihm"}),
        card("Simulation", "", {"1.10.f8", "1.10.0", "F8 : l'IHM d\xC3\xA9marre ou s'arr\xC3\xAAte, de partout",
         "F8 d\xC3\xA9marre ou arr\xC3\xAAte la simulation de l'IHM o\xC3\xB9 que tu sois (Maj+F8 l'arr\xC3\xAAte), sans toucher \xC3\xA0 l'API ; "
         "F5 reste celle de l'automate.",
         "", "arbre:Simulation/IHM", "hmi.simulation.bar#hmi.start", "simulation-api-ihm"}),
        card("Simulation", "", {"1.10.barre-simulation-ihm", "1.10.0", "La barre de Simulation \xC2\xB7 IHM refaite",
         "Les boutons de l'IHM (l'\xC3\xA9tat de l'API \xC3\xA0 c\xC3\xB4t\xC3\xA9), la vue en cours, l'utilisateur connect\xC3\xA9 et le zoom de la vue "
         "(moins, %, plus, Ajuster, Ctrl+molette).",
         "", "arbre:Simulation/IHM", "hmi.simulation.zoom", "simulation-zoom"}),
        card("Simulation", "", {"1.10.plein-ecran", "1.10.0", "Le simulateur IHM en plein \xC3\xA9" "cran",
         "Le bouton Plein \xC3\xA9" "cran de Simulation \xC2\xB7 IHM, ou F11 : la vue occupe tout l'\xC3\xA9" "cran ; une petite barre "
         "garde la vue, l'\xC3\xA9tat de l'IHM et le zoom ; \xC3\x89" "chap en sort.",
         "", "arbre:Simulation/IHM", "hmi.simulation.bar#hmi.fullscreen", "simulation-zoom"}),
        card("Scripts", "4", {"1.10.compiler-scripts", "1.10.0", "Compiler trouve les erreurs des scripts",
         "Variable ou membre inexistant, fonction inconnue, \xC3\xA9" "criture interdite : chaque erreur m\xC3\xA8ne \xC3\xA0 sa ligne, "
         "et l'\xC3\xA9" "diteur de scripts la souligne en rouge pendant qu'on tape ; F7 compile.",
         "", "touche:F7", "hmi.compile", "erreurs-scripts"}),
        card("\xC3\x89" "diteur IHM", "", {"1.10.visualisation-graphique", "1.10.0", "Une visualisation graphique des variables",
         "Clic droit sur une ou plusieurs variables IHM \xE2\x80\xBA \xC2\xAB Ouvrir une visualisation graphique \xC2\xBB : "
         "une fen\xC3\xAAtre qui les trace en direct pendant la simulation.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Variables IHM", "hmi.scripts.vars.table", "graphique-variables"}),
        card("\xC3\x89" "diteur IHM", "", {"1.10.types-excel", "1.10.0", "Les membres d'un type IHM avec Excel",
         "Ctrl+C copie les membres choisis en tableau ; Ctrl+V colle un tableau d'Excel, avec l'aper\xC3\xA7u de ce qui est "
         "ajout\xC3\xA9, remplac\xC3\xA9 ou refus\xC3\xA9 ; Ctrl+Z.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Types IHM", "hmi.scripts.types.members", "types-excel"}),
        card("\xC3\x89" "diteur IHM", "", {"1.10.objet-alarmes", "1.10.0", "Les alarmes d'un objet, dans l'arbre",
         "Un objet qui porte des alarmes se d\xC3\xA9plie sur son n\xC5\x93ud Alarmes, puis sur chaque alarme "
         "(priorit\xC3\xA9, active, d\xC3\xA9" "coch\xC3\xA9" "e, surcharg\xC3\xA9" "e) ; un clic l'ouvre.",
         "", "arbre:IHM/Vues/", "hmi.view.*.objects", "alarmes-dans-arbre"}),
        card("\xC3\x89" "diteur IHM", "", {"1.10.couleurs", "1.10.0", "Le choix des couleurs complet, et une pipette",
         "Carr\xC3\xA9 saturation et valeur, teinte, transparence, #RRGGBBAA, RVB et TSV, l'aper\xC3\xA7u avant et apr\xC3\xA8s, "
         "et une pipette qui prend une couleur \xC3\xA0 l'\xC3\xA9" "cran.",
         "", "arbre:IHM/Vues/", "hmi.view.*.props", "couleurs"}),
        card("Grafcet", "", {"1.10.grafcet", "1.10.0", "L'\xC3\xA9" "diteur de grafcet refait",
         "Les grafcets des instances de DFB_GRAFCETENGINE, en fran\xC3\xA7" "ais : un dessin conforme (\xC3\xA9tapes, transitions, "
         "divergences en OU et en ET), l'\xC3\xA9" "dition qui r\xC3\xA9\xC3\xA9" "crit le programme, la marche en simulation, les contr\xC3\xB4les.",
         "", "arbre2:API/Unit\xC3\xA9s de programme/Gc_", "grafcet.*.instances", "grafcet"}),
        // Decision 11 bis (chantier R2) : le volet Code et le direct grafcet <-> sections.
        card("Grafcet", "", {"1.10.grafcet-code", "1.10.0", "Le code du grafcet, en direct avec les sections",
         "Sous le dessin, le volet Code : la r\xC3\xA9" "ceptivit\xC3\xA9 ou l'action choisie dans l'\xC3\xA9" "diteur des sections ST "
         "(aide \xC3\xA0 la saisie, X3, FIN(A2), DUREE(X1)) ; les sections ouvertes suivent aussit\xC3\xB4t, et l'inverse.",
         "", "arbre2:API/Unit\xC3\xA9s de programme/Gc_", "grafcet.*.code", "grafcet-code"}),
        // Decisions 13, 13 bis et 14 (chantiers S1 et S2) : les sujets de l'aide et
        // les widgets viendront de aide-1.10-scripts.txt et aide-1.10-operateurs.txt.
        card("Scripts", "", {"1.10.fonctions-scripts", "1.10.0", "Des fonctions dans les scripts, et des types riches",
         "Un script d\xC3\xA9" "clare ses propres fonctions (FUNCTION ... END_FUNCTION) ; elles et les fonctions IHM rendent "
         "des copies, des r\xC3\xA9" "f\xC3\xA9rences, des pointeurs, des tableaux \xC3\xA0 N dimensions et des MAP parcourues par FOR EACH.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Scripts", "hmi.scripts.editor", "fonctions-internes"}),
        card("Scripts", "", {"1.10.operateurs", "1.10.0", "Les op\xC3\xA9rateurs des symboles et des types IHM",
         "Sur un symbole ou un type IHM, des conversions TO_... et des op\xC3\xA9rateurs (+, -, *, +=, comparaisons), "
         "chacun \xC3\xA9" "crit par un script ; l'arbre les montre sous Op\xC3\xA9rateurs.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Types IHM", "hmi.scripts.types.operators", "operateurs"}),
        // Decision 15 (chantiers O, S1, S2, N, K) : les enumerations IHM.
        card("Scripts", "", {"1.10.enum", "1.10.0", "Les \xC3\xA9num\xC3\xA9rations IHM",
         "Un troisi\xC3\xA8me genre de type IHM : T_MODE (Arret, Auto, Manu...), ses valeurs et leur texte, toString et "
         "fromString \xC3\xA0 toi ; dans les scripts, T_MODE#Auto, CASE sur ses valeurs et FOR EACH v IN T_MODE.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Types IHM", "hmi.scripts.types.tools#Nouveau type IHM", "enum-types"}),
        card("Aide", "", {"1.10.aide-notations", "1.10.0", "Les exemples de l'aide en ST, C ou C++",
         "Dans l'aide F1, chaque exemple de code se lit en trois notations : ST | C | C++, en haut de son cadre ; "
         "ton choix est gard\xC3\xA9 pour tous les exemples.",
         "", "aide:fonctions", "help.hmi.pane.article#notation:", "fonctions"}),
        // Le menu Aide range (chantier R2, les six rapides).
        card("Aide", "", {"1.10.menu-aide", "1.10.0", "Le menu Aide rang\xC3\xA9",
         "Quatre blocs (chercher et lire, les guides, apprendre, nouveaut\xC3\xA9s et support), une seule entr\xC3\xA9" "e "
         "Nouveaut\xC3\xA9s\xE2\x80\xA6, les rep\xC3\xA8res en case \xC3\xA0 cocher, \xC3\x80 propos d'XPGAnalyser ; une pastille sur Aide dit ce qui reste \xC3\xA0 voir.",
         "", "projet:", "analysis.topbar#aide", "menu-aide"}),
        card("Aide", "", {"1.10.nouveautes", "1.10.0", "Les nouveaut\xC3\xA9s se voient",
         "Cette fen\xC3\xAAtre \xC3\xA0 chaque nouvelle version, les rep\xC3\xA8res orange NOUVEAU dans l'application, "
         "et dans l'aide F1 les sujets et paragraphes nouveaux encadr\xC3\xA9s en orange.",
         "", "aide:reperes-nouveautes", "*.tools#Nouveaut\xC3\xA9s seulement", "reperes-nouveautes"}),
        note("1.10.0", "Corrig\xC3\xA9", Kind::Fixed,
         "La simulation de l'API s'arr\xC3\xAAtait au cycle 1 sur le projet des armoires de gaz : FIND rend -1 quand le programme l'attend.",
         "simulation", ""),
        note("1.10.0", "Corrig\xC3\xA9", Kind::Fixed,
         "Les \xC3\xA9tapes des grafcets ne s'allumaient jamais quand leurs tableaux appartiennent \xC3\xA0 une unit\xC3\xA9.",
         "grafcet", ""),
        note("1.10.0", "Corrig\xC3\xA9", Kind::Fixed,
         "Les champs qu'on ne pouvait plus vider reviennent \xC3\xA0 leur valeur par d\xC3\xA9" "faut.",
         "champs-expressions", ""),
        // ---- 1.9.0 : les cartes de la fenetre Nouveautes (memes id, meme ordre qu'avant) ----
        card("\xC3\x89quipements", "", {"1.9.esclave-simule", "1.9.0", "L'esclave simul\xC3\xA9 li\xC3\xA9 d'un vrai \xC3\xA9quipement",
         "Dans la fiche d'un \xC3\xA9quipement Modbus TCP, coche \xC2\xAB Cloner \xC2\xBB : son esclave simul\xC3\xA9, li\xC3\xA9 par un cadenas, "
         "a sa ligne juste sous le vrai.",
         "", "arbre:IHM/Configuration/\xC3\x89quipements", "", "esclaves-lies"}),
        card("\xC3\x89quipements", "", {"1.9.bascule", "1.9.0", "L'IHM lit le vrai, l'esclave, ou bascule toute seule",
         "\xC2\xAB Le vrai ; l'esclave s'il ne r\xC3\xA9pond pas \xC2\xBB : la bascule se fait apr\xC3\xA8s 10 s sans r\xC3\xA9ponse (Basculer apr\xC3\xA8s), "
         "le retour d\xC3\xA8s qu'il r\xC3\xA9pond.",
         "", "arbre:IHM/Configuration/\xC3\x89quipements", "", "esclaves-lies"}),
        card("\xC3\x89quipements", "", {"1.9.lectures-simulees", "1.9.0", "Les lectures simul\xC3\xA9" "es se voient",
         "En marche, ce qui est lu sur un esclave simul\xC3\xA9 a un cadre violet en tirets ; le bandeau LECTURES SIMUL\xC3\x89" "ES, "
         "la barre d'\xC3\xA9tat et les courbes le disent aussi.",
         "", "", "", "lectures-simulees"}),
        card("\xC3\x89quipements", "", {"1.9.page-simulation", "1.9.0", "La page Simulation de Param\xC3\xA8tres syst\xC3\xA8me",
         "Un 3e onglet r\xC3\xA9serv\xC3\xA9 \xC3\xA0 la permission Administrer (Ctrl+Alt+S sur le poste) : il montre et commande "
         "tous les esclaves simul\xC3\xA9s.",
         "", "", "", "page-simulation"}),
        card("\xC3\x89quipements", "", {"1.9.variables-esclaves", "1.9.0", "Les variables syst\xC3\xA8me des esclaves",
         "Le domaine SYS \xC2\xAB Esclaves simul\xC3\xA9s \xC2\xBB, une structure SYS.Slave.<nom> par esclave et la fonction "
         "IHM_ESCLAVE_SIMULE, en lecture seule.",
         "", "arbre:IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Variables syst\xC3\xA8me", "", "variables-esclaves"}),
        card("Outil Modbus", "", {"1.9.lecture-cyclique", "1.9.0", "La lecture cyclique \xC3\xA0 plusieurs requ\xC3\xAAtes",
         "L'outil Modbus lit plusieurs requ\xC3\xAAtes, chacune \xC3\xA0 sa p\xC3\xA9riode ; les jeux de requ\xC3\xAAtes, l'export CSV "
         "et l'enregistrement continu.",
         "", "arbre:IHM/Outil Modbus", "", "outil-cyclique"}),
        card("Popups", "", {"1.9.parametres-popups", "1.9.0", "Les param\xC3\xA8tres typ\xC3\xA9s des popups",
         "Une liste : nom, type (de base, IHM ou DDT de l'API), mode R\xC3\xA9" "f\xC3\xA9rence, Copie ou Les deux, "
         "valeur par d\xC3\xA9" "faut et description.",
         "", "arbre:IHM/Vues", "", "parametres-popups"}),
        card("Popups", "", {"1.9.appliquer-copie", "1.9.0", "Appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence",
         "Une action : ce qui a chang\xC3\xA9 dans la copie d'un param\xC3\xA8tre \xC2\xAB Les deux \xC2\xBB est recopi\xC3\xA9 dans la variable "
         "de l'appelant.",
         "", "", "", "appliquer-copie"}),
        card("Alarmes", "", {"1.9.alarmes-symboles", "1.9.0", "Les alarmes d'un symbole",
         "Le sous-onglet Alarmes d'un symbole, \xC3\xA9" "crites avec ses param\xC3\xA8tres ; un aper\xC3\xA7u montre le groupe g\xC3\xA9n\xC3\xA9r\xC3\xA9.",
         "", "arbre:IHM/Symboles", "", "alarmes-symboles"}),
        card("Alarmes", "", {"1.9.groupe-objet", "1.9.0", "Le groupe d'alarmes d'un objet",
         "Chaque objet pos\xC3\xA9 qui porte des alarmes a son groupe (Vue_Pompes.Pompe_3) : un filtre pour les objets "
         "d'alarmes, et ses variables .AlarmActive, .AlarmCount\xE2\x80\xA6",
         "", "arbre:IHM/Configuration/Alarmes", "", "groupes-objets"}),
        card("Alarmes", "6", {"1.9.alarmes-defaut", "1.9.0", "Les alarmes par d\xC3\xA9" "faut des objets",
         "Pompe, vanne, moteur, cuve, bouteille : leurs alarmes d\xC3\xA8s la pose, chacune d\xC3\xA9" "cochable, "
         "d\xC3\xA8s que sa source est reli\xC3\xA9" "e.",
         "", "", "", "alarmes-bibliotheque"}),
        card("Alarmes", "", {"1.9.surcharge", "1.9.0", "Surcharger une alarme sur un objet",
         "Chaque champ d'une alarme se change sur l'objet pos\xC3\xA9 : l'\xC3\xA9tiquette SURCHARG\xC3\x89, le petit bouton pour revenir, "
         "Tout revenir pour toute l'alarme.",
         "", "", "", "surcharge-alarmes"}),
        note("1.9.0", "Corrig\xC3\xA9", Kind::Fixed,
         "Les alarmes suivent leurs variables m\xC3\xAAme quand aucune vue ne les lit.",
         "alarmes", ""),
        note("1.8.0", "Installation", Kind::New,
         "L'installateur : un PC vierge s'installe tout seul ; pour toi seul ou pour tous les comptes ; la page \xC2\xAB Ta biblioth\xC3\xA8que \xC2\xBB.",
         "api-dossiers", ""),
        note("1.8.0", "Installation", Kind::New,
         "La fen\xC3\xAAtre Dossiers : les dossiers de l'application se voient, s'ouvrent, se changent.",
         "api-dossiers", ""),
        note("1.8.0", "Programme", Kind::New,
         "Le programme lisible : Excel, PDF, texte (Ctrl+Maj+E).",
         "api-arbre-lot8", ""),
        note("1.8.0", "Programme", Kind::New,
         "Le comparateur de sections ; les ic\xC3\xB4nes au choix des sections, unit\xC3\xA9s, blocs, scripts.",
         "api-arbre-lot8", ""),
        note("1.8.0", "Programme", Kind::New,
         "L'import suivi d'un .XPG ou d'un .XHW, avec sa fen\xC3\xAAtre d'avancement.",
         "api-importer", ""),
        note("1.8.0", "Corrig\xC3\xA9", Kind::Fixed,
         "Les conditions d'activation des sections, perdues \xC3\xA0 l'import, sont lues.",
         "api-reimporter", ""),
    };
    return t;
}

bool sameVersion(std::string_view a, std::string_view b) { return news::compareVersions(a, b) == 0; }

} // namespace

const std::vector<Release>& releases() { return kReleases; }

const Release* release(std::string_view version) {
    for (const auto& r : kReleases)
        if (sameVersion(r.version, version)) return &r;
    return nullptr;
}

const std::vector<Note>& all() { return table(); }

std::vector<const Note*> of(std::string_view version) {
    std::vector<const Note*> out;
    for (const auto& n : table())
        if (sameVersion(n.version, version)) out.push_back(&n);
    return out;
}

std::vector<std::string_view> domainsOf(std::string_view version) {
    std::vector<std::string_view> out;
    for (const auto* n : of(version))
        if (std::find(out.begin(), out.end(), n->domain) == out.end()) out.push_back(n->domain);
    return out;
}

std::string_view kindLetter(Kind k) {
    switch (k) {
        case Kind::New:     return "N";
        case Kind::Changed: return "M";
        case Kind::Fixed:   return "C";
    }
    return {};
}

std::string_view kindLabel(Kind k) {
    switch (k) {
        case Kind::New:     return "nouveau";
        case Kind::Changed: return "modifi\xC3\xA9";
        case Kind::Fixed:   return "corrig\xC3\xA9";
    }
    return {};
}

std::vector<const Note*> search(std::string_view term) {
    std::vector<const Note*> out;
    const auto t = keys::fold(term);
    if (t.empty()) return out;
    for (const auto& n : table()) {
        std::string hay(n.title);
        hay += ' ';
        hay += n.text;
        hay += ' ';
        hay += n.domain;
        hay += ' ';
        hay += n.version;
        if (keys::fold(hay).find(t) != std::string::npos) out.push_back(&n);
    }
    return out;   // la table va deja de la plus recente a la plus ancienne
}

std::string summary(std::string_view version) {
    int added = 0, changed = 0, fixed = 0;
    for (const auto* n : of(version)) {
        if (n->kind == Kind::New) ++added;
        else if (n->kind == Kind::Changed) ++changed;
        else ++fixed;
    }
    std::string out;
    const auto part = [&out](int n, const char* one, const char* many) {
        if (n == 0) return;
        if (!out.empty()) out += ", ";
        out += std::to_string(n);
        out += ' ';
        out += n > 1 ? many : one;
    };
    part(added, "nouveaut\xC3\xA9", "nouveaut\xC3\xA9s");
    part(changed, "changement", "changements");
    part(fixed, "correction", "corrections");
    return out;
}

} // namespace help::notes
