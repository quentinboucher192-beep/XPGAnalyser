// =============================================================================
//  project/DefaultMacros.cpp — the sub-macros this program ships with
// -----------------------------------------------------------------------------
//  WHY THESE ARE SCRIPTS AND THE NATIVES ARE NOT.
//
//  A native is anything that reaches the project model, the command stack, the
//  disk or the screen: the macro language has no way to touch those, so they
//  have to be C++. Everything in this file is a COMPOSITION of natives -
//  "import the type if it is missing, warn if it is behind", "wire a whole card"
//  - and compositions are exactly what somebody will want to adjust for their
//  own site.
//
//  So they live in libs/macros/ as text, they are written there only when
//  MISSING, and a file you corrected is never overwritten. A new release that
//  quietly put its own version back would be the most destructive thing this
//  program could do to somebody's work.
// =============================================================================
#include "SharedLibrary.hpp"

namespace project {

    const std::vector<SharedLibrary::DefaultMacro>& SharedLibrary::defaultMacros() {
        static const std::vector<DefaultMacro> kMacros = {

            // -------------------------------------------------------------------
            {"EnsureLibraryItem", "Macros", "1.10",
             "Importe un element de la bibliotheque s'il manque, previent s'il est depasse",
             R"MAC((* EnsureLibraryItem

   Le type ou le bloc est-il la ? Sinon on l'importe. Deja la mais plus ancien
   que la bibliotheque ? On le DIT et on ne touche a rien : remplacer un type
   qu'un programme utilise deja n'est pas une decision qu'une macro prend.

   Elle DEMANDE son entree plutot que de l'attendre d'ailleurs, pour qu'on
   puisse la lancer seule depuis le navigateur. Une sous-macro qui ne peut
   s'executer qu'appelee est une sous-macro qu'on ne peut pas essayer. *)

nom := Ask('nom', 'Quel element de la bibliotheque ?', 'ST_IO_Ana');

IF nom = '' THEN
    Fail('aucun nom donne');
ELSIF TypeExists(nom) OR PouExists(nom) THEN
    IF LibIsOutdated(nom) THEN
        Warn(nom + ' : le projet en a une copie plus ancienne que la bibliotheque ('
             + LibVersion(nom) + '). Rien n a ete remplace.');
    ELSE
        Log(nom + ' est deja dans le projet et a jour.');
    END_IF;
ELSIF NOT LibHasItem(nom) THEN
    Fail('la bibliotheque partagee ne propose pas ' + nom);
ELSE
    Log('import de ' + nom + ' v' + LibVersion(nom));
    LibImport(nom);
END_IF;
)MAC"},

// -------------------------------------------------------------------
{"EnsureSection", "Macros", "1.10",
 "Cree une section si elle n'existe pas deja, globale ou dans une unite",
 R"MAC((* EnsureSection

   Creer une section qui existe deja est refuse, ce qui arrete la macro. Une
   macro relancee doit pouvoir l'etre - c'est tout l'objet de ce fichier.

   GLOBALE OU DANS UNE UNITE : les deux. La premiere version imposait une unite
   de programme, ce qui n'a aucune raison d'etre. Une section de tache se cree
   avec un proprietaire vide, et c'est un choix, pas un defaut. *)

nom := Ask('section', 'Nom de la section', 'Mapping_ES');
ou  := AskChoice('ou', 'Ou la creer ?', 'Section de tache (globale),Dans une unite',
                 'Section de tache (globale)');

IF ou = 'Dans une unite' THEN
    unite := Ask('unite', 'Dans quelle unite de programme ?', 'Logigrammes_A');
    IF NOT PouExists(unite) THEN
        Fail('l unite ' + unite + ' n existe pas');
    END_IF;
ELSE
    unite := '';
END_IF;

IF SectionExists(nom) THEN
    Log('la section ' + nom + ' existe deja');
ELSE
    Log('creation de ' + nom);
    AddSection(nom, 'MAST', 'ST', unite);
END_IF;
)MAC"},

// -------------------------------------------------------------------
{"CreerVariable", "Macros", "1.00",
 "Cree une variable elementaire si elle n'existe pas deja",
 R"MAC((* CreerVariable

   Creer une variable qui existe deja est refuse, ce qui arrete la macro. Une
   macro relancee doit pouvoir l'etre. *)

nom   := Ask('nom', 'Nom de la variable', '');
type  := Ask('type', 'Type', 'BOOL');
ou    := AskChoice('ou', 'Portee', 'Globale,Dans une unite', 'Globale');
AskNow();

IF ou = 'Dans une unite' THEN
    unite := Ask('unite', 'Quelle unite de programme ?', 'Logigrammes_A');
    portee := 'Private';
ELSE
    unite := '';
    portee := 'Global';
END_IF;

IF nom = '' THEN
    Fail('aucun nom donne');
ELSIF VariableExists(nom) THEN
    Log(nom + ' existe deja');
ELSE
    AddVariable(nom, type, portee, unite);
    Log(nom + ' : ' + type);
END_IF;
)MAC"},

// -------------------------------------------------------------------
{"CreerInstanceDDT", "Macros", "1.00",
 "Cree une instance d'un type derive, en l'important au besoin",
 R"MAC((* CreerInstanceDDT

   Une instance de DDT est une variable dont le type est un DDT. Le type doit
   donc exister AVANT : c'est pour ca que cette macro l'importe au besoin plutot
   que de laisser la creation echouer avec un message sur un type inconnu. *)

type := Ask('type', 'Quel type derive ?', 'ST_IO_Ana');
nom  := Ask('nom', 'Nom de l instance', '');
combien := AskChoice('combien', 'Une seule ou un tableau ?', 'Une seule,Un tableau',
                     'Une seule');
AskNow();

IF combien = 'Un tableau' THEN
    taille := AskNumber('taille', 'Combien d elements ?', 1, 256, '8');
END_IF;

IF nom = '' THEN
    Fail('aucun nom donne');
END_IF;

IF NOT TypeExists(type) THEN
    IF LibHasItem(type) THEN
        Log('import du type ' + type);
        LibImport(type);
    ELSE
        Fail('le type ' + type + ' n existe pas et la bibliotheque ne le propose pas');
    END_IF;
END_IF;

IF VariableExists(nom) THEN
    Log(nom + ' existe deja');
ELSIF combien = 'Un tableau' THEN
    AddVariable(nom, 'ARRAY[0..' + DINT_TO_STRING(REAL_TO_INT(taille) - 1) + '] OF ' + type,
                'Global', '');
    Log(nom + ' : tableau de ' + DINT_TO_STRING(REAL_TO_INT(taille)) + ' ' + type);
ELSE
    AddVariable(nom, type, 'Global', '');
    Log(nom + ' : ' + type);
END_IF;
)MAC"},

// -------------------------------------------------------------------
{"CreerInstanceDFB", "Macros", "1.00",
 "Cree une instance d'un bloc et ecrit son appel",
 R"MAC((* CreerInstanceDFB

   Une instance de bloc qui n'est jamais appelee ne fait rien. Cette macro cree
   donc la variable ET ecrit la ligne d'appel - les deux, parce que l'une sans
   l'autre est une moitie de travail qui a l'air terminee. *)

bloc := Ask('bloc', 'Quel bloc ?', 'DFB_IO_ANA08');
nom  := Ask('nom', 'Nom de l instance', '');
section := Ask('section', 'Dans quelle section ecrire l appel ? (vide = aucune)', '');
AskNow();

IF nom = '' THEN
    Fail('aucun nom donne');
END_IF;

IF NOT PouExists(bloc) THEN
    IF LibHasItem(bloc) THEN
        Log('import du bloc ' + bloc);
        LibImport(bloc);
    ELSE
        Fail('le bloc ' + bloc + ' n existe pas et la bibliotheque ne le propose pas');
    END_IF;
END_IF;

IF VariableExists(nom) THEN
    Log(nom + ' existe deja');
ELSE
    AddVariable(nom, bloc, 'Global', '');
    Log(nom + ' : ' + bloc);
END_IF;

IF section <> '' THEN
    IF SectionExists(section) THEN
        AppendToSection(section, nom + '();');
        Log('appel ecrit dans ' + section);
    ELSE
        Warn('la section ' + section + ' n existe pas : aucun appel ecrit');
    END_IF;
END_IF;
)MAC"},

// -------------------------------------------------------------------
{"ImportES", "Macros", "3.10",
 "Declare les tableaux et les instances, puis cable et configure chaque voie",
 R"MAC((* ImportES

   Lit un onglet du classeur de configuration, exporte en CSV, et fait TOUT ce
   qu'il faut pour que les voies fonctionnent. Un export ne contient qu'un
   onglet, donc une execution traite une famille : relancez-la par famille.

     - importe les DDT et le DFB de la bonne taille,
     - declare le tableau de voies de chaque carte,
     - declare l'instance du bloc et ecrit son appel,
     - cable chaque voie,
     - ecrit TOUS ses parametres, pas seulement .Raw.

   LA VERSION PRECEDENTE N'ECRIVAIT QUE .Raw. Un tableau de ST_IO_Ana sans
   echelle, sans seuils et sans instance de bloc ne fait rien du tout : la voie
   etait cablee vers une structure que personne ne faisait tourner. C'etait une
   macro qui avait l'air de marcher.

   CE QU'ELLE NE FAIT PAS : elle ne supprime rien. Relancee apres l'ajout de
   trois voies au classeur, elle ajoute ces trois voies. *)

ou := AskChoice('ou', 'Ou creer les sections ?',
                'Section de tache (globale),Dans une unite de programme',
                'Section de tache (globale)');
famille := AskChoice('famille', 'Quelle famille contient ce tableau ?',
                     'Entrees TOR,Sorties TOR,Entrees ANA,Sorties ANA', 'Entrees ANA');

(* Ces trois reponses decident des questions suivantes. Sans cette coupure, le
   meme tour demanderait aussi les quatre noms de section - y compris quand on
   vient de repondre "une seule". *)
AskNow();

IF ou = 'Dans une unite de programme' THEN
    unite := Ask('unite', 'Quelle unite ?', 'Logigrammes_A');
ELSE
    unite := '';
END_IF;

(* UNE SECTION, et le nom propose suit la famille. Le choix "par famille ou une
   seule" ne voulait rien dire : un export CSV ne contient qu'un onglet, donc une
   execution ne traite qu'une famille de toute facon. Offrir un decoupage que la
   macro ne peut pas faire etait une option qui ne decidait de rien. *)
IF famille = 'Entrees TOR' THEN
    section := Ask('sectionDI', 'Nom de la section', 'Mapping_DI');
ELSIF famille = 'Sorties TOR' THEN
    section := Ask('sectionDO', 'Nom de la section', 'Mapping_DO');
ELSIF famille = 'Entrees ANA' THEN
    section := Ask('sectionAI', 'Nom de la section', 'Mapping_AI');
ELSE
    section := Ask('sectionAO', 'Nom de la section', 'Mapping_AO');
END_IF;

tache := Ask('tache', 'Tache appelante', 'MAST');
cycle := Ask('cycle', 'Periode de la tache, en ms', '20');
AskNow();

IF NOT Confirm('Import E/S', 'Cabler ' + famille + ' dans ' + section + ' ?') THEN
    Fail('annule');
END_IF;

analogique := (famille = 'Entrees ANA') OR (famille = 'Sorties ANA');
sortie     := (famille = 'Sorties TOR') OR (famille = 'Sorties ANA');

IF analogique THEN
    typeVoie := 'ST_IO_Ana';
ELSE
    typeVoie := 'ST_IO_Dig';
END_IF;

(* Les types avant les blocs qui les utilisent : un ARRAY OF ST_IO_Ana ne peut
   pas etre declare avant ST_IO_Ana. *)
IF analogique AND NOT TypeExists('ST_IO_Thr') AND LibHasItem('ST_IO_Thr') THEN
    LibImport('ST_IO_Thr');
END_IF;
IF NOT TypeExists(typeVoie) AND LibHasItem(typeVoie) THEN
    LibImport(typeVoie);
END_IF;

IF NOT PouExists(unite) AND unite <> '' THEN
    AddProgramUnit(unite, tache);
END_IF;
IF NOT SectionExists(section) THEN
    AddSection(section, tache, 'ST', unite);
END_IF;

t := OpenTable('es');
IF t < 0 THEN
    Fail('aucun tableau charge. Bouton Table... : exportez en CSV l onglet '
         + famille + ' du classeur, puis chargez-le.');
END_IF;
lignes := RowCount(t);
Log('tableau : ' + DINT_TO_STRING(lignes) + ' voie(s)');

(* Une variable de macro nait a sa premiere AFFECTATION. Celles qu'une
   comparaison lit avant tout le reste doivent donc exister d'abord - sinon la
   macro s'arrete sur "n'est pas declaree", ce qui est exact et peu utile. *)
carteEnCours := '';
voies := 0;
n := 0;
m := 0;
taille := 0;
bloc := '';
instance := '';

(* ---- premier passage : combien de voies par carte ---------------------
   La taille du tableau et celle du bloc en decoulent, et on ne peut pas les
   connaitre avant d'avoir vu toutes les lignes de cette carte. *)
FOR i := 0 TO lignes - 1 DO
    tableau := Cell(t, i, 'Tableau');
    indice  := Cell(t, i, 'Index');
    adresse := Cell(t, i, 'Adresse');

    IF tableau = '' OR adresse = '' THEN
        SkipRow('ligne ' + DINT_TO_STRING(i) + ' : tableau ou adresse manquant');
    ELSE
        (* La derniere voie vue pour cette carte donne sa taille. Les lignes
           d'une carte se suivent dans le classeur ; si ce n'etait pas le cas,
           le maximum resterait juste. *)
        n := STRING_TO_INT(indice) + 1;
        IF tableau <> carteEnCours THEN
            IF carteEnCours <> '' THEN
                Log(carteEnCours + ' : ' + DINT_TO_STRING(voies) + ' voie(s)');
            END_IF;
            carteEnCours := tableau;
            voies := n;
        ELSIF n > voies THEN
            voies := n;
        END_IF;
    END_IF;
END_FOR;
IF carteEnCours <> '' THEN
    Log(carteEnCours + ' : ' + DINT_TO_STRING(voies) + ' voie(s)');
END_IF;

(* ---- second passage : declarer, instancier, cabler, configurer -------- *)
carteEnCours := '';
FOR i := 0 TO lignes - 1 DO
    tableau := Cell(t, i, 'Tableau');
    indice  := Cell(t, i, 'Index');
    adresse := Cell(t, i, 'Adresse');
    cible   := tableau + '[' + indice + ']';

    IF tableau <> '' AND adresse <> '' THEN

        IF tableau <> carteEnCours THEN
            carteEnCours := tableau;

            (* La taille du bloc : la premiere de 4/8/16/32/64 qui contient les
               voies de cette carte. *)
            n := 0;
            FOR k := 0 TO lignes - 1 DO
                IF Cell(t, k, 'Tableau') = tableau THEN
                    m := STRING_TO_INT(Cell(t, k, 'Index')) + 1;
                    IF m > n THEN n := m; END_IF;
                END_IF;
            END_FOR;

            IF n <= 4 THEN
                taille := 4;
            ELSIF n <= 8 THEN
                taille := 8;
            ELSIF n <= 16 THEN
                taille := 16;
            ELSIF n <= 32 THEN
                taille := 32;
            ELSE
                taille := 64;
            END_IF;

            IF analogique THEN
                bloc := 'DFB_IO_ANA';
            ELSE
                bloc := 'DFB_IO_DIG';
            END_IF;
            IF taille < 10 THEN
                bloc := bloc + '0' + DINT_TO_STRING(taille);
            ELSE
                bloc := bloc + DINT_TO_STRING(taille);
            END_IF;

            IF NOT PouExists(bloc) AND LibHasItem(bloc) THEN
                LibImport(bloc);
            END_IF;

            (* Le tableau de voies. *)
            IF NOT VariableExists(tableau) THEN
                AddVariable(tableau,
                            'ARRAY[0..' + DINT_TO_STRING(taille - 1) + '] OF ' + typeVoie,
                            'Global', '');
            END_IF;

            (* L'INSTANCE DU BLOC, qui manquait entierement. Un tableau de voies
               sans bloc pour le faire tourner ne fait rien : c'est de la donnee
               que personne ne lit.

               Le nom suit la convention du classeur : CarteAI_R0S10 donne
               IO_AI_R0S10, et non IO_CarteAI_R0S10. *)
            IF STARTSWITH(tableau, 'Carte') THEN
                instance := 'IO_' + MID(tableau, 5);
            ELSE
                instance := 'IO_' + tableau;
            END_IF;
            IF NOT VariableExists(instance) THEN
                AddVariable(instance, bloc, 'Global', '');
            END_IF;

            (* Un cartouche par carte, avec ce qu'il faut pour s'y retrouver :
               le bloc, la taille, le nombre de voies. Sans ca, 457 lignes
               d'affectations forment un pavé que personne ne relit. *)
            AppendToSection(section, '');
            AppendToSection(section,
                '(* ==================================================================== *)');
            AppendToSection(section, '(*  ' + tableau + '  -  ' + DINT_TO_STRING(n)
                + ' voie(s)  -  ' + bloc + '  -  instance ' + instance + '  *)');
            AppendToSection(section,
                '(* ==================================================================== *)');
            AppendToSection(section,
                instance + '(Count := ' + DINT_TO_STRING(n)
                + ', CycleMs := ' + cycle + ', Chan := ' + tableau + ');');
        END_IF;

        (* Une ligne vide et un titre par voie : le libelle du classeur, qui
           est la seule chose qui dise ce que %IW0.10.3 mesure. *)
        AppendToSection(section, '');
        AppendToSection(section, '(* -- ' + cible + '  ' + adresse + '  '
            + Cell(t, i, 'Designation') + ' *)');

        (* ---- le cablage --------------------------------------------- *)
        IF famille = 'Entrees ANA' THEN
            AppendToSection(section, cible + '.Raw := INT_TO_REAL(' + adresse + ');');
        ELSIF famille = 'Sorties ANA' THEN
            AppendToSection(section, adresse + ' := REAL_TO_INT(' + cible + '.Raw);');
        ELSIF famille = 'Entrees TOR' THEN
            AppendToSection(section, cible + '.Raw := ' + adresse + ';');
        ELSE
            AppendToSection(section, adresse + ' := ' + cible + '.Raw;');
        END_IF;

        (* ---- TOUS les parametres, pas seulement .Raw ----------------- *)
        IF sortie THEN
            AppendToSection(section, cible + '.IsOut := TRUE;');
        ELSE
            AppendToSection(section, cible + '.IsOut := FALSE;');
        END_IF;

        IF analogique THEN
            AppendToSection(section, cible + '.RawMin := '
                + REAL_TO_STRING(STRING_TO_REAL(Cell(t, i, 'RawMin'))) + ';');
            AppendToSection(section, cible + '.RawMax := '
                + REAL_TO_STRING(STRING_TO_REAL(Cell(t, i, 'RawMax'))) + ';');
            AppendToSection(section, cible + '.EngMin := '
                + REAL_TO_STRING(STRING_TO_REAL(Cell(t, i, 'EngMin'))) + ';');
            AppendToSection(section, cible + '.EngMax := '
                + REAL_TO_STRING(STRING_TO_REAL(Cell(t, i, 'EngMax'))) + ';');
            IF Cell(t, i, 'Clamp') = 'N' THEN
                AppendToSection(section, cible + '.Clamp := FALSE;');
            ELSE
                AppendToSection(section, cible + '.Clamp := TRUE;');
            END_IF;
            AppendToSection(section, cible + '.DeadBand := '
                + REAL_TO_STRING(STRING_TO_REAL(Cell(t, i, 'DeadBand'))) + ';');
            AppendToSection(section, cible + '.FiltN := '
                + DINT_TO_STRING(STRING_TO_INT(Cell(t, i, 'FiltN'))) + ';');
            AppendToSection(section, cible + '.RateMax := '
                + REAL_TO_STRING(STRING_TO_REAL(Cell(t, i, 'RateMax'))) + ';');

            FOR k := 0 TO 3 DO
                seuil := cible + '.Thr[' + DINT_TO_STRING(k) + ']';
                colEn := 'S' + DINT_TO_STRING(k) + '_En';
                IF Cell(t, i, colEn) = 'O' THEN
                    AppendToSection(section, seuil + '.En := TRUE;');
                    IF Cell(t, i, 'S' + DINT_TO_STRING(k) + '_Dir') = 'B' THEN
                        AppendToSection(section, seuil + '.Dir := 1;');
                    ELSE
                        AppendToSection(section, seuil + '.Dir := 0;');
                    END_IF;
                    AppendToSection(section, seuil + '.Val := ' + REAL_TO_STRING(
                        STRING_TO_REAL(Cell(t, i, 'S' + DINT_TO_STRING(k) + '_Val'))) + ';');
                    AppendToSection(section, seuil + '.Hyst := ' + REAL_TO_STRING(
                        STRING_TO_REAL(Cell(t, i, 'S' + DINT_TO_STRING(k) + '_Hyst'))) + ';');
                    AppendToSection(section, seuil + '.DelayMs := ' + DINT_TO_STRING(
                        STRING_TO_INT(Cell(t, i, 'S' + DINT_TO_STRING(k) + '_DelayMs'))) + ';');
                    AppendToSection(section, seuil + '.Sev := ' + DINT_TO_STRING(
                        STRING_TO_INT(Cell(t, i, 'S' + DINT_TO_STRING(k) + '_Sev'))) + ';');
                ELSE
                    AppendToSection(section, seuil + '.En := FALSE;');
                END_IF;
            END_FOR;
        ELSE
            IF Cell(t, i, 'Inv') = 'O' THEN
                AppendToSection(section, cible + '.Inv := TRUE;');
            ELSE
                AppendToSection(section, cible + '.Inv := FALSE;');
            END_IF;
            AppendToSection(section, cible + '.DebounceMs := '
                + DINT_TO_STRING(STRING_TO_INT(Cell(t, i, 'DebounceMs'))) + ';');
            IF Cell(t, i, 'AlarmEn') = 'O' THEN
                AppendToSection(section, cible + '.AlarmEn := TRUE;');
                IF Cell(t, i, 'AlarmState') = '0' THEN
                    AppendToSection(section, cible + '.AlarmState := FALSE;');
                ELSE
                    AppendToSection(section, cible + '.AlarmState := TRUE;');
                END_IF;
                AppendToSection(section, cible + '.AlarmDelayMs := '
                    + DINT_TO_STRING(STRING_TO_INT(Cell(t, i, 'AlarmDelayMs'))) + ';');
                AppendToSection(section, cible + '.Sev := '
                    + DINT_TO_STRING(STRING_TO_INT(Cell(t, i, 'Sev'))) + ';');
            ELSE
                AppendToSection(section, cible + '.AlarmEn := FALSE;');
            END_IF;
        END_IF;
    END_IF;
END_FOR;

Log('cablage termine dans ' + section);
)MAC"},

// -------------------------------------------------------------------
{"LierAlarmes", "Macros", "1.10",
 "Enregistre et cable toutes les alarmes d'un onglet dans le gestionnaire",
 R"MAC((* LierAlarmes

   Prend un onglet du classeur, exporte en CSV, et branche chacune de ses
   alarmes dans DFB_ALM_MANAGER : l'UUID, le libelle, le lien vers l'adresse,
   la gravite, et la ligne qui remonte l'etat a chaque cycle.

   L'UUID PORTE LA PROVENANCE. Il n'est pas tire au hasard : ses 32 bits sont
   le rack, le module, la voie et le numero de seuil, packes. Un identifiant
   trouve dans un journal dans cinq ans dit encore de quelle voie il vient,
   sans aucune table a tenir a jour.

       bits 31..28  genre   1 TOR, 2 seuil analogique
       bits 27..24  rack    bits 23..18  module
       bits 17..12  voie    bits 11..8   seuil     bits 7..0  libre

   LA DISPOSITION EST CELLE D'AlmUuid, et la duplication est assumee : la macro
   ecrit un litteral pour que le fichier genere se lise sans rien executer. Le
   commentaire pose a cote de chaque UUID en donne les parties, ce qui rend un
   ecart visible plutot que silencieux. *)

famille := AskChoice('famille', 'Quelle famille contient ce tableau ?',
                     'Entrees TOR,Sorties TOR,Entrees ANA,Sorties ANA', 'Entrees ANA');
AskNow();

registre := Ask('registre', 'Nom du registre d alarmes', 'AlmDefs');
etats    := Ask('etats', 'Nom du tableau d etats', 'AlmState');
histo    := Ask('histo', 'Nom de l historique', 'AlmHist');
cmd      := Ask('cmd', 'Nom des commandes', 'AlmCmd');
stat     := Ask('stat', 'Nom de l etat', 'AlmStat');
instance := Ask('instance', 'Nom de l instance du gestionnaire', 'GestionAlarmes');
sInit    := Ask('sInit', 'Section d initialisation du registre', 'Alarmes_Init');
sCycle   := Ask('sCycle', 'Section cyclique', 'Alarmes_Cycle');
depart   := AskNumber('depart', 'Premier emplacement libre du registre', 0, 63, '0');
AskNow();

IF NOT Confirm('Alarmes', 'Enregistrer les alarmes de ' + famille + ' ?') THEN
    Fail('annule');
END_IF;

analogique := (famille = 'Entrees ANA') OR (famille = 'Sorties ANA');
apos := CHR(39);

FOR k := 0 TO 3 DO
    IF k = 0 THEN nom := 'ST_ALM_Def';
    ELSIF k = 1 THEN nom := 'ST_ALM_Event';
    ELSIF k = 2 THEN nom := 'ST_ALM_Cmd';
    ELSE nom := 'ST_ALM_Stat';
    END_IF;
    IF NOT TypeExists(nom) AND LibHasItem(nom) THEN LibImport(nom); END_IF;
END_FOR;
IF NOT PouExists('DFB_ALM_MANAGER') AND LibHasItem('DFB_ALM_MANAGER') THEN
    LibImport('DFB_ALM_MANAGER');
END_IF;

IF NOT VariableExists(registre) THEN
    AddVariable(registre, 'ARRAY[0..63] OF ST_ALM_Def', 'Global', '');
END_IF;
IF NOT VariableExists(etats) THEN
    AddVariable(etats, 'ARRAY[0..63] OF BOOL', 'Global', '');
END_IF;
IF NOT VariableExists(histo) THEN
    AddVariable(histo, 'ARRAY[0..255] OF ST_ALM_Event', 'Global', '');
END_IF;
IF NOT VariableExists(cmd) THEN
    AddVariable(cmd, 'ST_ALM_Cmd', 'Global', '');
END_IF;
IF NOT VariableExists(stat) THEN
    AddVariable(stat, 'ST_ALM_Stat', 'Global', '');
END_IF;
IF NOT VariableExists(instance) THEN
    AddVariable(instance, 'DFB_ALM_MANAGER', 'Global', '');
END_IF;

IF NOT SectionExists(sInit) THEN
    AddSection(sInit, 'MAST', 'ST', '');
END_IF;
IF NOT SectionExists(sCycle) THEN
    AddSection(sCycle, 'MAST', 'ST', '');
    AppendToSection(sCycle, instance + '(Count := 64, CycleMs := 20, Cmd := ' + cmd
        + ', State := ' + etats + ', Defs := ' + registre + ', History := ' + histo
        + ', Stat := ' + stat + ');');
END_IF;

t := OpenTable('alarmes');
IF t < 0 THEN
    Fail('aucun tableau charge. Bouton Table... : exportez l onglet ' + famille + ' en CSV.');
END_IF;

(* LE TABLEAU EST-IL BIEN DE CETTE FAMILLE ?
   Sans ce controle, choisir "Entrees ANA" sur un export TOR produisait 128
   avertissements "aucune colonne S0_En" et zero alarme - un resultat qui a
   l'air d'un probleme de donnees alors que c'est une erreur de choix. *)
IF analogique AND NOT HasColumn(t, 'S0_En') THEN
    IF HasColumn(t, 'AlarmEn') THEN
        Fail('ce tableau est un onglet TOR (il a AlarmEn, pas S0_En). '
             + 'Relancez en choisissant Entrees TOR ou Sorties TOR.');
    ELSE
        Fail('ce tableau n a ni S0_En ni AlarmEn : ce n est pas un onglet d E/S.');
    END_IF;
END_IF;
IF NOT analogique AND NOT HasColumn(t, 'AlarmEn') THEN
    IF HasColumn(t, 'S0_En') THEN
        Fail('ce tableau est un onglet analogique (il a S0_En, pas AlarmEn). '
             + 'Relancez en choisissant Entrees ANA ou Sorties ANA.');
    ELSE
        Fail('ce tableau n a ni AlarmEn ni S0_En : ce n est pas un onglet d E/S.');
    END_IF;
END_IF;

slot := REAL_TO_INT(depart);
lignes := RowCount(t);
poses := 0;
rack := 0;
module := 0;
voie := 0;
genre := 0;
uuid := 0;
sev := 1;
cible := '';
d := 0;

FOR i := 0 TO lignes - 1 DO
    adresse := Cell(t, i, 'Adresse');
    tableau := Cell(t, i, 'Tableau');
    indice  := Cell(t, i, 'Index');
    libelle := Cell(t, i, 'Designation');

    IF adresse = '' OR tableau = '' THEN
        SkipRow('ligne ' + DINT_TO_STRING(i) + ' : adresse ou tableau manquant');
    ELSE
        p1 := FIND(adresse, '.', 0);
        p2 := FIND(adresse, '.', p1 + 1);
        IF p1 < 0 OR p2 < 0 THEN
            SkipRow('ligne ' + DINT_TO_STRING(i) + ' : adresse illisible (' + adresse + ')');
        ELSE
            (* Le rack suit les lettres : %IW0 donne 0, %I0 donne 0. *)
            d := 1;
            WHILE d < p1 AND FIND('0123456789', MID(adresse, d, 1), 0) < 0 DO
                d := d + 1;
            END_WHILE;
            rack   := STRING_TO_INT(MID(adresse, d, p1 - d));
            module := STRING_TO_INT(MID(adresse, p1 + 1, p2 - p1 - 1));
            voie   := STRING_TO_INT(MID(adresse, p2 + 1));

            IF analogique THEN
                genre := 2;
                FOR k := 0 TO 3 DO
                    IF Cell(t, i, 'S' + DINT_TO_STRING(k) + '_En') = 'O' THEN
                        IF slot > 63 THEN
                            Warn('registre plein : ' + adresse + ' seuil '
                                 + DINT_TO_STRING(k) + ' non enregistre');
                        ELSE
                            uuid := genre * 268435456 + rack * 16777216
                                  + module * 262144 + voie * 4096 + k * 256;
                            sev := STRING_TO_INT(Cell(t, i, 'S' + DINT_TO_STRING(k) + '_Sev'));
                            cible := registre + '[' + DINT_TO_STRING(slot) + ']';

                            AppendToSection(sInit, '');
                            AppendToSection(sInit, '(* ' + adresse + ' seuil '
                                + DINT_TO_STRING(k) + '   genre=' + DINT_TO_STRING(genre)
                                + ' rack=' + DINT_TO_STRING(rack)
                                + ' module=' + DINT_TO_STRING(module)
                                + ' voie=' + DINT_TO_STRING(voie) + ' *)');
                            AppendToSection(sInit, cible + '.Uuid := '
                                + DINT_TO_STRING(uuid) + ';');
                            AppendToSection(sInit, cible + '.Label := ' + apos
                                + MID(libelle, 0, 24) + ' S' + DINT_TO_STRING(k) + apos + ';');
                            AppendToSection(sInit, cible + '.Link := ' + apos + adresse
                                + apos + ';');
                            AppendToSection(sInit, cible + '.Sev := '
                                + DINT_TO_STRING(sev) + ';');
                            AppendToSection(sInit, cible + '.Used := TRUE;');

                            AppendToSection(sCycle, etats + '[' + DINT_TO_STRING(slot)
                                + '] := ' + tableau + '[' + indice + '].Thr['
                                + DINT_TO_STRING(k) + '].Raised;');
                            slot := slot + 1;
                            poses := poses + 1;
                        END_IF;
                    END_IF;
                END_FOR;
            ELSIF Cell(t, i, 'AlarmEn') = 'O' THEN
                genre := 1;
                IF slot > 63 THEN
                    Warn('registre plein : ' + adresse + ' non enregistre');
                ELSE
                    uuid := genre * 268435456 + rack * 16777216
                          + module * 262144 + voie * 4096;
                    sev := STRING_TO_INT(Cell(t, i, 'Sev'));
                    cible := registre + '[' + DINT_TO_STRING(slot) + ']';

                    AppendToSection(sInit, '');
                    AppendToSection(sInit, '(* ' + adresse
                        + '   genre=' + DINT_TO_STRING(genre)
                        + ' rack=' + DINT_TO_STRING(rack)
                        + ' module=' + DINT_TO_STRING(module)
                        + ' voie=' + DINT_TO_STRING(voie) + ' *)');
                    AppendToSection(sInit, cible + '.Uuid := ' + DINT_TO_STRING(uuid) + ';');
                    AppendToSection(sInit, cible + '.Label := ' + apos
                        + MID(libelle, 0, 30) + apos + ';');
                    AppendToSection(sInit, cible + '.Link := ' + apos + adresse + apos + ';');
                    AppendToSection(sInit, cible + '.Sev := ' + DINT_TO_STRING(sev) + ';');
                    AppendToSection(sInit, cible + '.Used := TRUE;');

                    AppendToSection(sCycle, etats + '[' + DINT_TO_STRING(slot) + '] := '
                        + tableau + '[' + indice + '].Alarm;');
                    slot := slot + 1;
                    poses := poses + 1;
                END_IF;
            END_IF;
        END_IF;
    END_IF;
END_FOR;

IF poses = 0 THEN
    (* Dire POURQUOI. "0 alarme" tout seul ressemble a une panne de la macro,
       alors que c'est ce que le classeur contient. *)
    IF analogique THEN
        Warn('aucune alarme enregistree : aucune voie de ce tableau n a un seuil actif '
             + '(colonnes S0_En a S3_En toutes a N). Activez-les dans le classeur, '
             + 'ou il n y a rien a surveiller sur ces voies.');
    ELSE
        Warn('aucune alarme enregistree : aucune voie de ce tableau n a AlarmEn = O. '
             + 'Une entree TOR ne devient une alarme que si vous le demandez.');
    END_IF;
END_IF;

Log(DINT_TO_STRING(poses) + ' alarme(s) enregistree(s), prochain emplacement libre : '
    + DINT_TO_STRING(slot));
)MAC"},

// -------------------------------------------------------------------
{"CreerSR", "Macros", "1.00",
 "Cree une sous-routine, avec ses variables d'entree et de sortie",
 R"MAC((* CreerSR

   UNE SOUS-ROUTINE N'A PAS DE PARAMETRES. C'est la chose a savoir avant tout
   le reste : on ne peut pas ecrire MaSR(a := 1, b => r). Les arguments et les
   retours passent par des VARIABLES CONVENUES, que l'appelant remplit avant
   l'appel et relit apres.

   Cette macro cree donc trois choses d'un coup : la sous-routine, ses
   variables d'entree, et ses variables de sortie - nommees de la meme maniere,
   pour qu'on voie a leur nom a quoi elles servent.

       MaSR_In_<nom>    ce que l'appelant remplit avant
       MaSR_Out_<nom>   ce qu'il relit apres

   La convention n'est pas jolie, elle est LISIBLE : six mois plus tard, un
   Ctrl+F sur MaSR_ donne toute l'interface de la sous-routine. *)

nom    := Ask('nom', 'Nom de la sous-routine', '');
tache  := Ask('tache', 'Tache', 'MAST');
AskNow();

entrees := Ask('entrees', 'Variables d entree, separees par des virgules (nom:type)', '');
sorties := Ask('sorties', 'Variables de sortie, meme forme', '');
AskNow();

IF nom = '' THEN
    Fail('aucun nom donne');
END_IF;

IF SectionExists(nom) THEN
    IF IsSubroutine(nom) THEN
        Log('la sous-routine ' + nom + ' existe deja');
    ELSE
        Fail(nom + ' existe deja, mais c est une SECTION, pas une sous-routine. '
             + 'Une section tourne a chaque cycle ; les remplacer l une par l autre '
             + 'changerait le programme.');
    END_IF;
ELSE
    AddSubroutine(nom, tache);
    Log('sous-routine ' + nom + ' creee');
END_IF;

(* Les variables. Une liste 'niveau:REAL, marche:BOOL' se decoupe ici : le
   langage n'a pas de tableau, donc on avance a la virgule. *)
FOR sens := 0 TO 1 DO
    IF sens = 0 THEN
        liste := entrees;
        prefixe := nom + '_In_';
    ELSE
        liste := sorties;
        prefixe := nom + '_Out_';
    END_IF;

    depart := 0;
    WHILE depart < LEN(liste) DO
        virgule := FIND(liste, ',', depart);
        IF virgule < 0 THEN
            morceau := MID(liste, depart);
            depart := LEN(liste);
        ELSE
            morceau := MID(liste, depart, virgule - depart);
            depart := virgule + 1;
        END_IF;

        deuxPoints := FIND(morceau, ':', 0);
        IF deuxPoints < 0 THEN
            IF LEN(morceau) > 0 THEN
                Warn('"' + morceau + '" : il manque le type, attendu nom:TYPE');
            END_IF;
        ELSE
            champ := MID(morceau, 0, deuxPoints);
            typeChamp := MID(morceau, deuxPoints + 1);
            (* Les espaces autour se retirent a la main : le langage n'a pas de
               TRIM, et ' niveau' n'est pas un nom de variable valide. *)
            WHILE LEN(champ) > 0 AND MID(champ, 0, 1) = ' ' DO
                champ := MID(champ, 1);
            END_WHILE;
            WHILE LEN(typeChamp) > 0 AND MID(typeChamp, 0, 1) = ' ' DO
                typeChamp := MID(typeChamp, 1);
            END_WHILE;

            IF champ = '' OR typeChamp = '' THEN
                Warn('"' + morceau + '" : nom ou type vide, ignore');
            ELSE
                complet := prefixe + champ;
                IF VariableExists(complet) THEN
                    Log(complet + ' existe deja');
                ELSE
                    AddVariable(complet, typeChamp, 'Global', '');
                    Log(complet + ' : ' + typeChamp);
                END_IF;
            END_IF;
        END_IF;
    END_WHILE;
END_FOR;
)MAC"},

// -------------------------------------------------------------------
{"AppelerSR", "Macros", "1.00",
 "Ecrit l'appel d'une sous-routine, avec le remplissage de ses entrees",
 R"MAC((* AppelerSR

   Ecrit dans une section l'appel d'une sous-routine, precede du remplissage
   de ses variables d'entree et suivi de la relecture de ses sorties.

   UN APPEL SEUL NE SUFFIT PRESQUE JAMAIS. Une SR n'a pas de parametres : si
   l'appelant n'ecrit pas ses entrees juste avant, la SR tourne avec ce qu'un
   autre appelant y a laisse au cycle precedent. C'est le piege de la SR, et
   c'est pour ca que cette macro ecrit les trois parties ensemble. *)

sr      := Ask('sr', 'Quelle sous-routine ?', '');
section := Ask('section', 'Dans quelle section ecrire l appel ?', '');
AskNow();

valeurs := Ask('valeurs', 'Entrees a poser (nom=expression, separees par des virgules)', '');
AskNow();

IF sr = '' OR section = '' THEN
    Fail('il faut une sous-routine et une section');
END_IF;
IF NOT SectionExists(sr) THEN
    Fail('la sous-routine ' + sr + ' n existe pas');
END_IF;
IF NOT IsSubroutine(sr) THEN
    Fail(sr + ' est une section, pas une sous-routine : elle tourne deja a chaque '
         + 'cycle et ne s appelle pas.');
END_IF;
IF NOT SectionExists(section) THEN
    Fail('la section ' + section + ' n existe pas');
END_IF;

AppendToSection(section, '');
AppendToSection(section, '(* --- ' + sr + ' --- *)');

depart := 0;
poses := 0;
WHILE depart < LEN(valeurs) DO
    virgule := FIND(valeurs, ',', depart);
    IF virgule < 0 THEN
        morceau := MID(valeurs, depart);
        depart := LEN(valeurs);
    ELSE
        morceau := MID(valeurs, depart, virgule - depart);
        depart := virgule + 1;
    END_IF;

    egal := FIND(morceau, '=', 0);
    IF egal < 0 THEN
        IF LEN(morceau) > 0 THEN
            Warn('"' + morceau + '" : attendu nom=expression');
        END_IF;
    ELSE
        champ := MID(morceau, 0, egal);
        expr := MID(morceau, egal + 1);
        WHILE LEN(champ) > 0 AND MID(champ, 0, 1) = ' ' DO
            champ := MID(champ, 1);
        END_WHILE;
        WHILE LEN(expr) > 0 AND MID(expr, 0, 1) = ' ' DO
            expr := MID(expr, 1);
        END_WHILE;

        cible := sr + '_In_' + champ;
        IF NOT VariableExists(cible) THEN
            Warn(cible + ' n existe pas : la ligne est ecrite quand meme, mais '
                 + 'elle ne compilera pas tant que la variable n est pas creee.');
        END_IF;
        AppendToSection(section, cible + ' := ' + expr + ';');
        poses := poses + 1;
    END_IF;
END_WHILE;

AppendToSection(section, sr + ';');
IF poses = 0 THEN
    Log('appel ecrit sans entree : verifiez que ' + sr + ' n en attend aucune.');
END_IF;
)MAC"},

// -------------------------------------------------------------------
{"GenererModes", "Macros", "1.00",
 "Ecrit la section des modes : un CASE qui appelle les SR de chaque mode",
 R"MAC((* GenererModes

   Ecrit la section qui fait tourner les modes. Elle se lit en deux moitiees,
   et la separation est le coeur de l'affaire :

     DFB_MODE_MANAGER decide QUEL mode est actif. C'est de la donnee - une
     condition, une priorite, un temps minimum - et ca vit dans un DDT.

     CETTE SECTION decide CE QUI TOURNE dans chaque mode. C'est une suite de
     noms de sous-routines dans un ORDRE, et une suite ordonnee de noms ne se
     range pas dans un DDT. C'est du code, et il faut donc l'ecrire.

   Ce que la macro produit :

       GestionModes(Count := 4, CycleMs := 20, Def := Modes, ...);

       CASE Modes_Courant OF
           0:  Init_Systeme;
               Acquisition;
           1:  Acquisition;
               Production;
       END_CASE;

   Relancee, elle REECRIT la section entiere plutot que d'y ajouter : une
   section de modes a moitie ancienne et a moitie nouvelle est pire que les
   deux. *)

section  := Ask('section', 'Nom de la section des modes', 'Modes');
table    := Ask('table', 'Nom du tableau de modes', 'Modes');
instance := Ask('instance', 'Nom de l instance du gestionnaire', 'GestionModes');
courant  := Ask('courant', 'Variable du mode courant', 'Mode_Courant');
combien  := AskNumber('combien', 'Combien de modes ?', 1, 16, '4');
cycle    := Ask('cycle', 'Periode de la tache, en ms', '20');
AskNow();

n := REAL_TO_INT(combien);

(* Les noms et les appels, un tour de questions par mode. Poser les seize d'un
   coup quand on en veut quatre remplirait l'ecran de champs vides. *)
FOR m := 0 TO n - 1 DO
    cle := 'm' + DINT_TO_STRING(m);
    nomMode := Ask(cle + 'nom', 'Mode ' + DINT_TO_STRING(m) + ' : son nom',
                   'MODE' + DINT_TO_STRING(m));
    appels := Ask(cle + 'sr', 'Mode ' + DINT_TO_STRING(m)
                  + ' : sous-routines a appeler, dans l ordre, separees par des virgules', '');
END_FOR;
AskNow();

IF NOT TypeExists('ST_MODE_Def') AND LibHasItem('ST_MODE_Def') THEN
    LibImport('ST_MODE_Def');
END_IF;
IF NOT PouExists('DFB_MODE_MANAGER') AND LibHasItem('DFB_MODE_MANAGER') THEN
    LibImport('DFB_MODE_MANAGER');
END_IF;

IF NOT VariableExists(table) THEN
    AddVariable(table, 'ARRAY[0..15] OF ST_MODE_Def', 'Global', '');
END_IF;
IF NOT VariableExists(instance) THEN
    AddVariable(instance, 'DFB_MODE_MANAGER', 'Global', '');
END_IF;
IF NOT VariableExists(courant) THEN
    AddVariable(courant, 'INT', 'Global', '');
END_IF;
IF NOT VariableExists(courant + '_Prec') THEN
    AddVariable(courant + '_Prec', 'INT', 'Global', '');
END_IF;
IF NOT VariableExists(courant + '_Ms') THEN
    AddVariable(courant + '_Ms', 'DINT', 'Global', '');
END_IF;

IF NOT SectionExists(section) THEN
    AddSection(section, 'MAST', 'ST', '');
END_IF;

ClearSection(section);
AppendToSection(section, '(* Section generee par la macro GenererModes.');
AppendToSection(section, '   Les conditions d entree se calculent AILLEURS et');
AppendToSection(section, '   arrivent ici par ' + table + '[i].Req. *)');
AppendToSection(section, '');
AppendToSection(section, instance + '(Count := ' + DINT_TO_STRING(n)
    + ', CycleMs := ' + cycle + ', Def := ' + table
    + ', Current := ' + courant + ', PrevMode := ' + courant + '_Prec'
    + ', CurrentMs := ' + courant + '_Ms);');
AppendToSection(section, '');
AppendToSection(section, 'CASE ' + courant + ' OF');

vides := 0;
FOR m := 0 TO n - 1 DO
    cle := 'm' + DINT_TO_STRING(m);
    nomMode := Ask(cle + 'nom', '', 'MODE' + DINT_TO_STRING(m));
    appels := Ask(cle + 'sr', '', '');

    AppendToSection(section, '    ' + DINT_TO_STRING(m) + ':  (* ' + nomMode + ' *)');

    depart := 0;
    poses := 0;
    WHILE depart < LEN(appels) DO
        virgule := FIND(appels, ',', depart);
        IF virgule < 0 THEN
            sr := MID(appels, depart);
            depart := LEN(appels);
        ELSE
            sr := MID(appels, depart, virgule - depart);
            depart := virgule + 1;
        END_IF;
        WHILE LEN(sr) > 0 AND MID(sr, 0, 1) = ' ' DO
            sr := MID(sr, 1);
        END_WHILE;

        IF sr <> '' THEN
            IF NOT SectionExists(sr) THEN
                (* Ecrite quand meme, et signalee : refuser tout le mode parce
                   qu'une SR n est pas encore creee obligerait a les creer dans
                   un ordre impose. *)
                Warn('mode ' + nomMode + ' : la sous-routine ' + sr
                     + ' n existe pas encore');
            ELSIF NOT IsSubroutine(sr) THEN
                Warn('mode ' + nomMode + ' : ' + sr + ' est une SECTION. Elle tourne '
                     + 'deja a chaque cycle, l appeler ici la ferait tourner deux fois.');
            END_IF;
            AppendToSection(section, '        ' + sr + ';');
            poses := poses + 1;
        END_IF;
    END_WHILE;

    IF poses = 0 THEN
        AppendToSection(section, '        ;   (* rien a faire dans ce mode *)');
        vides := vides + 1;
    END_IF;
END_FOR;

AppendToSection(section, 'END_CASE;');

IF vides > 0 THEN
    Warn(DINT_TO_STRING(vides) + ' mode(s) n appellent aucune sous-routine.');
END_IF;
Log('section ' + section + ' generee pour ' + DINT_TO_STRING(n) + ' mode(s)');
)MAC"},

        {"VerifierBibliotheque", "Macros", "1.10",
         "Liste les elements du projet que la bibliotheque propose en plus recent",
         R"MAC((* VerifierBibliotheque

   Ne modifie rien. Elle regarde et elle rapporte, ce qui est exactement ce
   qu'on veut avant une mise a jour : savoir ce qui bougerait. *)

retard := 0;

nom := 'ST_IO_Thr';
IF TypeExists(nom) AND LibIsOutdated(nom) THEN
    Warn(nom + ' : la bibliotheque propose ' + LibVersion(nom));
    retard := retard + 1;
END_IF;
nom := 'ST_IO_Ana';
IF TypeExists(nom) AND LibIsOutdated(nom) THEN
    Warn(nom + ' : la bibliotheque propose ' + LibVersion(nom));
    retard := retard + 1;
END_IF;
nom := 'ST_IO_Dig';
IF TypeExists(nom) AND LibIsOutdated(nom) THEN
    Warn(nom + ' : la bibliotheque propose ' + LibVersion(nom));
    retard := retard + 1;
END_IF;

IF retard = 0 THEN
    Log('tout est a jour');
ELSE
    Log(DINT_TO_STRING(retard) + ' element(s) en retard sur la bibliotheque');
END_IF;
)MAC"},
        };
        return kMacros;
    }

} // namespace project