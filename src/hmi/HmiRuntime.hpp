// =============================================================================
//  hmi/HmiRuntime.hpp - l'IHM en marche : scripts, actions, navigation
// -----------------------------------------------------------------------------
//  CE QUI TOURNE PENDANT LA SIMULATION, SANS ECRAN. L'ecran (le volet
//  Simulation) donne l'heure, les clics et les appuis ; le moteur rend ce qu'il
//  faut dessiner : la vue courante, les popups, la transition en cours, le
//  journal. Les tests pilotent le meme moteur, avec une horloge a la main.
//
//  LE CYCLE IHM (Configuration > cycle, 100 ms par defaut). A chaque cycle :
//  OnCycle de la vue et des popups ouvertes, les scripts generaux Cyclique
//  (chacun a sa periode) et Changement, les fronts et les changements de
//  valeur surveilles par les actions. A chaque appel de tick (plus souvent) :
//  les timers, les appuis longs, la fin des transitions.
//
//  LES VARIABLES : celles de l'IHM (Programmation generale > Variables) d'abord,
//  puis celles de l'automate. Un script ST lit et ecrit les deux de la meme
//  facon ; une expression de vue aussi (lecture seulement).
//
//  LES FONCTIONS IHM, appelables depuis un script ST :
//    IHM_NAVIGUER('Vue')   IHM_POPUP('Vue')   IHM_FERMER_POPUP()
//    IHM_JOURNAL('texte {Niveau:0.0}')        IHM_APPELER('Script')
//    IHM_SON('bip.wav')    IHM_VUE() : STRING (la vue courante)
//    IHM_TEMPS() : TIME (depuis le lancement de l'IHM)
//
//  LOT 7 : un script peut declarer ses variables locales (VAR gardees d'une
//  execution a l'autre, VAR_TEMP remises a zero) ; les FONCTIONS IHM du projet
//  (Programmation generale > Fonctions) s'appellent comme celles de l'automate,
//  depuis un script, une expression de vue ou une action.
//
//  RIEN NE BOUCLE SANS FIN : un appel de script dans un appel de script, une
//  navigation qui en declenche une autre... s'arretent a 8 niveaux, et le
//  journal dit pourquoi ("appel circulaire"). Un script qui boucle est coupe
//  par le budget d'instructions du simulateur.
// =============================================================================
#pragma once

#include "HmiExport.hpp"
#include "HmiKeys.hpp"                // 1.11.23 : les raccourcis des vues
#include "HmiExpr.hpp"
#include "HmiPopupParams.hpp"
#include "HmiHistory.hpp"
#include "HmiControls.hpp"
#include "HmiLoginMenu.hpp"
#include "HmiModel.hpp"
#include "HmiProduction.hpp"
#include "HmiSystemMenu.hpp"
#include "HmiScript.hpp"
#include "HmiSignature.hpp"
#include "HmiPerf.hpp"                 // lot 13 : les performances
#include "HmiDisplay.hpp"              // lot 13 : l'affichage
#include "HmiTypes.hpp"                // lot 16 : structures et tableaux
#include "HmiMedia.hpp"                // lot 16 : les GIF animes
#include "HmiLog.hpp"                  // 1.11.14 : les niveaux de IHM_LOG
#include "HmiSimData.hpp"              // 1.11.15 : la remanence de simulation

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

// ---- transitions ----------------------------------------------------------------
// La courbe d'animation : t de 0 a 1, la progression rendue (un rebond depasse 1
// avant d'y revenir). lineaire, acceleree, deceleree, douce, rebond, bezier(a,b,c,d).
[[nodiscard]] double ease(std::string_view curve, double t);
// Ce que le dessin applique a une vue pendant la transition : opacite,
// decalage (en fraction de la vue), echelle et angle (degres) autour du centre.
struct Frame {
    double opacity{1}, offsetX{0}, offsetY{0}, scale{1}, angle{0};
    bool operator==(const Frame&) const = default;
};
// La vue qui arrive et celle qui part, a la progression `p` (deja lissee).
[[nodiscard]] Frame incomingFrame(const Transition&, double p);
[[nodiscard]] Frame outgoingFrame(const Transition&, double p);

// ---- journal ------------------------------------------------------------------
struct JournalEntry {
    double      time{0};       // secondes depuis le lancement de l'IHM
    std::string stamp;         // "14:05:12.350" (l'heure du poste)
    std::string kind;          // "Action", "Navigation", "Script", "Journal", "Erreur", "IHM_LOG"
    std::string source;        // "Vue_Armoire_A/Btn_Accueil", "script Demarrage"
    std::string message;
    // 1.11.14 : la Console. Le niveau (IHM_LOG le donne ; sinon celui de la categorie),
    // le code qui l'a dit (le script, la fonction, la vue, l'objet ; kNoId : aucun) et
    // sa ligne (0 : inconnue), le cycle du moteur et la session (le numero du demarrage).
    LogLevel      level{LogLevel::Info};
    std::string   code;        // "Horloge", "Vue_A.OnOpen", "fonction Moyenne" ("" : pas de code)
    Id            view{kNoId}, object{kNoId}, script{kNoId}, function{kNoId};
    int           line{0};
    long long     cycle{0};
    int           session{0};
};

// ---- alarmes en cours (lot 4) ---------------------------------------------------
//  Une alarme reste dans la liste tant qu'elle est active OU pas acquittee ;
//  disparue ET acquittee, elle passe a l'historique.
struct LiveAlarm {
    Id          alarm{kNoId};
    std::string name, message, group, category;
    int         priority{3};
    bool        active{true};
    bool        acked{false};
    bool        ackRequired{true};               // faux : acquittee d'office, terminee a sa disparition
    std::string appeared, ackedAt, cleared;      // "2026-09-22 07:40:12.350"
    std::string ackedBy;
    std::string instruction;                     // lot 11 : la consigne, remplie a l'apparition
    // 1.9 : une alarme d'objet (HmiObjectAlarms.hpp) - son groupe interne ("Vue.Objet")
    // en plus du groupe declare, et les symboles des instances qui la contiennent
    // ("Sym_A;Sym_B") ; vides : une alarme du projet.
    std::string objectGroup, symbols;
    [[nodiscard]] std::string state() const;     // Active, Acquittee, Disparue a acquitter, Active sans acquittement
};

// ---- lot 11 : une alarme mise de cote --------------------------------------------
//  ISA 18.2 : l'operateur ecarte une alarme qui le gene (un capteur en panne, un
//  essai) pour un temps, en disant pourquoi. Tant qu'elle est de cote, elle
//  n'apparait plus ; au bout du temps, elle revient d'elle-meme.
struct ShelvedAlarm {
    Id          alarm{kNoId};
    std::string name, group;
    std::string objectGroup, symbols;   // 1.9 : comme LiveAlarm
    int         priority{3};
    std::string by;            // qui (l'utilisateur connecte ; '' : personne)
    std::string reason;        // pourquoi
    std::string since;         // "2026-09-24 14:05:12.350"
    double      until{-1};     // instant IHM de la fin ; negatif : sans limite
};

// ---- lot 11 : un fichier a ecrire (bouton d'export, action Exporter) ---------------
struct ExportRequest {
    std::string                  fileName;   // "alarmes_2026-09-24.csv" : le nom seul
    std::string                  format;     // "CSV", "Excel", "PDF"
    std::string                  source;     // "alarmes", "objet:Courbe_1"...
    std::size_t                  rows{0};
    std::shared_ptr<const Bytes> data;
    std::string                  origin;     // "Vue/Objet" (journal)
};

// ---- lot 14 : un evenement d'alarme, pour les notifications (courriel, SMS) ----------
struct AlarmNotice {
    std::string kind;          // "Apparition", "R\xC3\xA9" "apparition", "Acquittement", "Disparition"
    Id          alarm{kNoId};
    std::string name, message, group, category, instruction;
    int         priority{3};
    std::string user;          // qui a acquitte ('' : personne)
    std::string at;            // "2026-09-25 17:40:12.350"
};
// Les notifications envoyees (SYS.NotifySent, SYS.NotifyFailed, SYS.NotifyLast).
struct NotifyStats {
    unsigned long long sent{0}, failed{0};
    std::string        last;   // "17:42:05 courriel a Astreinte : PT1_A_Vide"
    bool               enabled{false};
    int                webClients{0};   // les navigateurs connectes (SYS.WebClients)
};
// ---- lot 15 : l'etat d'un equipement du reseau (SYS.Equip*, IHM_EQUIPEMENT_OK, ------
//  l'objet Etat de la communication d'un equipement) : l'ecran le tient a jour.
struct EquipmentStatus {
    std::string name;
    std::string type;               // "Modbus TCP/IP", "Ethernet TCP/IP"
    std::string address;            // "192.168.1.30:502 (esclave 1)", "simule (127.0.0.1:50123)"
    bool        enabled{true};
    bool        simulated{false};
    bool        reachable{false};   // Modbus : la liaison est connectee ; Ethernet : le dernier ping (ou le port) a repondu
    bool        tested{false};      // au moins un essai (liaison ou ping) a eu lieu
    double      pingMs{-1};         // le dernier ping ; -1 : aucun, ou pas de reponse
    std::string state;              // "Connecte", "Joignable", "Injoignable", "Simule", "Desactive"
    std::string why;                // injoignable : pourquoi
    // Lot 17 : son jumeau (un esclave virtuel), et a qui parle l'IHM.
    bool        twin{false};        // il a un jumeau
    bool        viaTwin{false};     // l'IHM (ici) parle au jumeau, pas au vrai appareil
    std::string twinName;           // "Balance B (virtuelle)"
    std::string twinState;          // "en marche", "panne simulee", "arrete", "exception 04 forcee"
    std::string twinAddress;        // "192.168.56.100 (simule, 127.0.0.1:50123)"
    int         twinTone{0};        // 1 en marche, 2 panne ou exception, 5 arrete
    std::string realState;          // le vrai appareil : "joignable, 4 ms", "injoignable", "pas encore teste", "aucun (seulement simule)"
    int         realTone{0};        // 1 joignable, 3 injoignable, 0 inconnu
    // 1.9 : ce que lit l'IHM quand il a un esclave simule lie (viaTwin : l'esclave).
    std::string readMode;           // la source en vigueur : "vrai", "esclave", "auto" (vide : pas d'esclave)
    bool        fallback{false};    // l'esclave, par la bascule automatique : le vrai ne repond pas
    bool        chosen{false};      // la source est choisie sur la page Simulation (pas celle de la fiche)
    bool        realOnline{false};  // le vrai appareil repond (liaison connectee, ou dernier essai reussi)
    double      readSince{0};       // heure murale (s depuis 1970) de la derniere bascule ; 0 : jamais
    double      realSilentSince{0}; // heure murale depuis laquelle le vrai ne repond plus ; 0 : il repond (ou inconnu)
    std::string readWhy;            // "le vrai ne r\xC3\xA9pond pas depuis 14:02:21", "choisi sur la page Simulation", "seulement simul\xC3\xA9"
};

// ---- 1.9 : les esclaves simules (la page Simulation, SYS.Slave.*) -----------------
//  L'ecran (EquipmentHost) les decrit ; la page Simulation de Parametres systeme
//  les montre et les commande (SimSlaveCommand). Rien de ce qu'elle change ne va
//  dans le projet : ca dure jusqu'au redemarrage de l'IHM.
struct SimSlaveValue {
    std::string key;                // la ligne : l'adresse telle qu'ecrite ("%MW8504", "43001")
    std::string variable;           // "Vitesse_Affichee" ; vide : un registre sans variable
    std::string address;            // "%MW8504"
    std::string type;               // "INT", "REAL", "BOOL"...
    std::string unit;               // l'unite de la variable ("tr/min")
    std::string note;               // "lue par l'IHM", "\xC3\xA9" "crite par l'IHM", "bit d'un mot"
    bool        boolean{false};     // une bobine, une entree TOR, un bit, un BOOL
    bool        animated{false};    // il bouge tout seul (Animer)
    std::string kind;               // son mouvement : behaviorKindKey ("sinus", "rampe"...) ; vide : aucun
    std::string kindLabel;          // "sinus", "aucun"
    double      low{0}, high{100};  // la zone de mouvement, en valeur de la variable
    double      period{10};         // s
    bool        forced{false};
    double      forcedValue{0};     // brut
    std::string forcedText;         // "1450"
    std::string value;              // la valeur en direct, en clair ("1 450", "TRUE")
    double      number{0};          // ... en nombre (valeur de la variable)
    // 1.9 (page Simulation) : la valeur forcee en valeur de la variable (mise a
    // l'echelle comprise), et si la variable est mise a l'echelle (le pas des - et +).
    double      forcedNumber{0};
    bool        scaled{false};
};
struct SimSlave {
    std::string   equipment;        // "Variateur ATV320"
    std::string   name;             // "Variateur ATV320 \xC2\xB7 esclave simul\xC3\xA9" (Equipment::twinLabel)
    std::string   key;              // SYS.Slave.<key> : hmi::slaveKey(equipment)
    bool          linked{true};     // le clone d'un vrai appareil ; faux : seulement simule
    bool          enabled{true};    // l'equipement est actif
    bool          running{false};   // en marche (son serveur repond sur 127.0.0.1:port)
    bool          responds{true};   // faux : panne simulee
    int           exception{0};     // une exception forcee (0 : aucune)
    bool          read{false};      // l'IHM le lit (a la place du vrai, ou parce qu'il n'y a que lui)
    bool          fallback{false};  // ... par la bascule automatique
    std::string   mode;             // la source en vigueur : "vrai", "esclave", "auto"
    bool          chosen{false};    // choisie sur la page (pas celle de la fiche)
    bool          modeLocked{false};// la page ne peut pas la changer (poste "toujours le vrai", seulement simule)
    std::string   modeWhy;          // pourquoi
    bool          realOnline{false};
    double        realSilentSince{0};
    double        readSince{0};
    int           port{0};          // 127.0.0.1:port (0 : arrete)
    std::uint64_t requests{0}, refusedWrites{0};
    int           clients{0};
    double        responseMs{0};    // son temps de reponse regle
    int           animated{0}, forced{0};
    std::string   state;            // "en marche \xC2\xB7 61 requ\xC3\xAAtes", "arr\xC3\xAAt\xC3\xA9", "panne simul\xC3\xA9" "e"
    std::string   why;              // "le vrai ne r\xC3\xA9pond pas depuis 14:02:21"
    std::vector<SimSlaveValue> values;    // seulement quand on les demande (la page ouverte)
};
// Une commande de la page Simulation (equipment : l'equipement ; vide pour
// "tout_animer", "tout_arreter", "deforcer_tout" : tous les esclaves) :
//   "source"     value "vrai" | "esclave" | "auto"     ce que l'IHM lit
//   "revenir"                                          le vrai, maintenant (la bascule cesse)
//   "marche", "repond"        value "1" | "0"
//   "exception"  value "0", "1", "2", "3", "4", "6", "11"
//   "animer"     key, value "1" | "0"
//   "mouvement"  key, value : behaviorKindKey ou son libelle ; "aucun"
//   "zone"       key, value "lo;hi" (valeur de la variable)
//   "periode"    key, value (s)
//   "forcer"     key, value (brut ; vide : deforcer)
//   "forcer_valeur" key, value (en valeur de la variable : la page Simulation)
//   "tout_animer", "tout_arreter", "deforcer_tout"
struct SimSlaveCommand {
    std::string equipment;
    std::string what;
    std::string key;
    std::string value;
};

// ---- lot 14 : un rapport periodique ecrit (l'ecran l'envoie en piece jointe) -----
struct ReportOutput {
    Id                           report{kNoId};
    std::string                  name, title, from, to;   // la periode, "2026-09-24 06:00"
    std::string                  fileName, path;          // "rapport_Journalier_2026-09-25.pdf", ou il est ecrit
    std::string                  format;                  // "PDF", "Excel"
    std::shared_ptr<const Bytes> data;
    std::string                  recipients;              // les noms (Report::recipients)
};

// ---- lot 11 : l'editeur de recette en marche ----------------------------------------
struct RecipeEditorState {
    std::string              recipe;
    Id                       record{kNoId};      // le jeu montre
    std::vector<std::string> values;             // les valeurs montrees (celles du jeu, ou modifiees)
    std::vector<bool>        edited;             // modifiees, pas encore enregistrees
    std::vector<std::string> installed;          // les valeurs de l'installation (au dernier cycle)
    std::string              message;            // la derniere reponse
    bool                     error{false};
    double                   messageAt{0};
    [[nodiscard]] bool dirty() const noexcept;
};

// ---- courbes : les points d'une plume d'un objet Courbe --------------------------
struct TrendSeries {
    std::string expression;
    std::deque<std::pair<double, double>> points;   // (instant IHM en s, valeur)
};

// ---- lot 6 : ce que le moteur demande a l'ecran -------------------------------
//  LE GESTIONNAIRE DE RECETTES change le PROJET (ajouter, modifier, supprimer un
//  jeu, y lire les valeurs de l'installation) : c'est l'ecran qui le fait, par
//  une commande annulable, puis le moteur relit le projet.
struct RecipeRequest {
    std::string              op;              // "ajouter", "modifier", "supprimer", "lire"
    Id                       object{kNoId};   // le gestionnaire qui demande
    std::string              recipe;
    Id                       record{kNoId};   // le jeu choisi (kNoId : ajouter)
    std::string              recordName;
    std::vector<std::string> values;          // lire : les valeurs lues dans l'installation
    std::string              source;          // "Vue/Objet" (journal)
};
//  "Demander une ressource" : l'ecran ouvre le selecteur de fichiers, filtre sur
//  les extensions ; la ressource ajoutee au projet, son nom va dans la variable.
struct ResourceRequest {
    std::vector<std::string> extensions;      // {"png", "jpg"} ; vide : tout
    std::string              variable;        // STRING qui recoit le nom (facultatif)
    std::string              source;
};
// ---- lot 8 : la gestion des utilisateurs, demandee a l'ecran ------------------
//  Comme les recettes : la liste des utilisateurs est dans le PROJET ; ajouter,
//  modifier, supprimer, activer, donner un mot de passe passent par une commande
//  annulable de l'ecran (un dialogue quand il faut saisir quelque chose).
struct UserRequest {
    std::string op;              // "ajouter", "modifier", "supprimer", "activer", "motdepasse", "changer"
    Id          object{kNoId};   // l'objet qui demande
    Id          user{kNoId};     // l'utilisateur vise (kNoId : ajouter)
    std::string login;
    bool        enabled{true};   // activer : le nouvel etat
    std::string salt, hash;      // changer : le mot de passe de l'utilisateur connecte, deja chiffre
    std::string source;          // "Vue/Objet" (journal)
    // Lot 12 : le menu de connexion - "groupe" (le compte `user` passe dans le
    // groupe `group`), "role" (le groupe `group` gagne ou perd le role `role`
    // selon `enabled`), "deconnexion" (la deconnexion automatique du projet :
    // `minutes`, 0 : jamais).
    Id          group{kNoId};
    std::string role;
    int         minutes{0};
};

// ---- lot 8 : une popup ouverte -------------------------------------------------
//  OU ELLE EST (dans la vue du dessous, barre de titre comprise), CE QU'ELLE A
//  RECU (ses parametres) ET D'OU ELLE VIENT (Changer de popup empile, Popup
//  precedente depile). Chaque popup a son propre historique.
// 1.9 : LA COPIE D'UN PARAMETRE (mode Copie ou Les deux), tenue par la popup
// ouverte (deux popups ouvertes ont chacune la leur ; fermer la popup l'oublie).
// Les valeurs sont a plat, par membre (en majuscules) : "" la valeur entiere
// d'un scalaire ; ".MARCHE", "[2]", "[2].CONSIGNE" ceux d'une structure ou d'un
// tableau. `captured` : a l'ouverture ; `values` : maintenant (la popup les lit
// et les modifie ; rien ne part vers l'automate). Appliquer copie sur reference
// n'ecrit que les membres modifies depuis la capture.
struct ParamCopy {
    std::string name;                // le parametre (tel que declare)
    std::string source;              // la variable de l'appelant (Pompes[3]) ; vide : une valeur
    ParamMode   mode{ParamMode::Copy};
    std::string root;                // le nom interne qui la designe ("$COPIE1")
    std::map<std::string, sim::Value, std::less<>> captured, values;
    std::map<std::string, std::string, std::less<>> paths;   // le membre tel que declare (".Vitesse"), pour ecrire
};
struct PopupSlot {
    Id          view{kNoId};
    double      x{0}, y{0};          // coin haut-gauche de la fenetre, en pixels de la vue du dessous
    std::string placement;           // tel que demande ("centre", "objet", "x,y"...)
    Id          opener{kNoId};       // l'objet qui l'a ouverte (placement "objet")
    std::string arguments;           // "Moteur := Pompes[3]" tels que donnes
    std::shared_ptr<const Scope> scope;   // les parametres, resolus a l'ouverture
    std::vector<ParamCopy> copies;        // 1.9 : les parametres Copie et Les deux
    struct Back {
        Id                           view{kNoId};
        double                       x{0}, y{0};
        std::string                  arguments;
        std::shared_ptr<const Scope> scope;
        std::vector<ParamCopy>       copies;   // 1.9
    };
    std::vector<Back> history;
    [[nodiscard]] double width(const View&) const;    // la fenetre : la vue (+ rien)
    [[nodiscard]] double height(const View&) const;   // la vue + la barre de titre
};
// "Moteur := Pompes[3]; Titre := 'Pompe 3'" -> {("Moteur", "Pompes[3]"), ("Titre", "'Pompe 3'")}.
// ";" dans une chaine ou un index ne coupe pas ; "=" vaut ":=".
[[nodiscard]] std::vector<std::pair<std::string, std::string>> parseArguments(std::string_view text);
// Un chemin de variable (Pompes[i + 1].Marche) et pas un calcul : un parametre
// ainsi relie designe la variable (on lit et on ecrit a travers lui).
[[nodiscard]] bool isVariablePath(std::string_view text);
// Les parametres d'une vue dans l'editeur (et pour Compiler) : leurs valeurs
// par defaut, sans rien evaluer (les index restent tels quels).
[[nodiscard]] Scope designScope(const View&);

// ---- lot 8 : la saisie au clavier en marche ------------------------------------
//  UN CHAMP A LE FOCUS A LA FOIS (un champ de saisie, ou un champ d'un objet de
//  connexion) : ce qui est tape y va, Entree valide, Echap annule, Tab passe au
//  champ suivant du meme objet.
struct FormState {
    std::string                        focus;           // le champ qui a le focus ; vide : aucun
    std::map<std::string, std::string> text;            // ce qui est tape, par champ
    std::map<std::string, std::size_t> caret;           // la position du curseur (octets)
    bool                               fresh{false};    // la premiere frappe remplace (champ de saisie)
    std::string                        message;         // la derniere reponse (erreur ou succes)
    bool                               error{false};
    double                             messageAt{0};
    std::string                        chosen;          // connexion en liste : le login choisi aux fleches
    Id                                 selected{kNoId}; // gestion des utilisateurs : la ligne choisie
    double                             confirmUntil{0}; // deconnexion a confirmer : jusqu'a quand
};
enum class EditKey : std::uint8_t { Backspace, Delete, Enter, Escape, Tab, BackTab, Left, Right, Home, End };

// "png; *.JPG, .svg" -> {"png", "jpg", "svg"}
[[nodiscard]] std::vector<std::string> parseExtensionFilter(std::string_view text);
[[nodiscard]] bool                     extensionAccepted(const std::vector<std::string>& extensions, std::string_view fileName);

// ---- lot 9 : l'automate, tel que le lisent les variables SYS.Plc* ----------------
//  L'ecran le donne (Hooks::plcStatus) : le moteur ne connait du simulateur que
//  ses variables.
// Lot 14 : la liaison Modbus TCP (HmiComm.hpp).
namespace comm {
class Link;
enum class Quality : std::uint8_t;
}

struct PlcStatus {
    bool          attached{false}, running{false}, paused{false}, halted{false};
    std::uint64_t scans{0};
    int           cycleMs{0};
    int           forced{0};
    std::string   error;            // le dernier defaut ("" : aucun)
    std::string   project;
};

struct ObjectAlarm;   // 1.9 : HmiObjectAlarms.hpp

class Runtime {
public:
    struct Hooks {
        std::function<void(const std::string& resource)> playSound;          // IHM_SON
        std::function<void(const JournalEntry&)>         journaled;          // chaque entree
        // "Changer d'utilisateur" : l'ecran demande le mot de passe (ou le code),
        // puis appelle login(). Vide : seule l'autorisation par expression passe.
        std::function<void(const std::string& login)>    askLogin;
        // lot 6
        std::function<void(const RecipeRequest&)>        recipeRequest;
        std::function<void(const ResourceRequest&)>      requestResource;
        // lot 8 : la gestion des utilisateurs et le changement de mot de passe
        std::function<void(const UserRequest&)>          userRequest;
        // lot 9 : l'etat du simulateur de l'automate (SYS.PlcRunning...)
        std::function<PlcStatus()>                       plcStatus;
        // lot 10 : "Redemarrer l'IHM" du menu Parametres systeme (l'ecran le fait,
        // comme son bouton) ; vide : le moteur s'arrete et repart lui-meme.
        std::function<void()>                            restart;
        // lot 11 : ecrire un fichier exporte (dans exports/ du projet). Vrai :
        // ecrit ; `where` : le chemin, ou pourquoi ce n'est pas fait.
        std::function<bool(const ExportRequest&, std::string* where)> exportFile;
        // lot 13 : chaque ligne du journal d'audit, a peine ecrite (l'ecran l'ajoute
        // tout de suite a ihm/historique/audit.csv : une panne ne la perd pas).
        std::function<void(const AuditEntry&)>           audited;
        // lot 14 : le serveur de demonstration (le simulateur expose en Modbus
        // TCP) tourne-t-il, et sur quel port (SYS.CommDemoServer, le diagnostic) ?
        std::function<std::pair<bool, int>()>            commDemo;
        // lot 14 : l'IHM tourne-t-elle en poste d'exploitation, et sur combien d'ecrans ?
        std::function<std::pair<bool, int>()>            station;
        // lot 14 : chaque evenement d'alarme (apparition, reapparition,
        // acquittement, disparition) - les notifications ; leurs chiffres ;
        // un rapport periodique ecrit (l'envoyer aux destinataires).
        std::function<void(const AlarmNotice&)>          alarmNotice;
        std::function<NotifyStats()>                     notifyStats;
        std::function<void(const ReportOutput&)>         reportWritten;
        // lot 15 : la liaison d'un equipement du reseau, par son nom (nulle : pas
        // de liaison - Ethernet TCP/IP, desactive, pas encore partie) ; l'etat de
        // tous les equipements.
        std::function<comm::Link*(const std::string& equipment)> equipmentLink;
        // 1.11.7 : LE FORCAGE COMMUN - une variable liee dont la case est forcee (ou animee)
        // dans son esclave simule (la cle : la variable en majuscules, Four1.Temperature ->
        // FOUR1.TEMPERATURE). Vrai : une ecriture (un script, une action) est ignoree, sans
        // erreur - le forcage passe avant ; la lecture rend la valeur forcee de l'esclave.
        std::function<bool(const std::string& variable)>         boundForced;
        std::function<std::vector<EquipmentStatus>()>            equipmentStatus;
        // 1.9 : les esclaves simules (withValues : leurs lignes aussi - la page
        // Simulation ouverte) ; une commande de la page (faux et why : refusee).
        std::function<std::vector<SimSlave>(bool withValues)>         simSlaves;
        std::function<bool(const SimSlaveCommand&, std::string* why)> simSlaveCommand;
        // ---- Lot API 8 : les exports qui demandent ou ----
        // Un export parti d'un GESTE DE L'OPERATEUR (le bouton d'export, une action
        // Exporter, IHM_EXPORTER dans le script qu'un clic lance) dont l'option
        // "Demander ou enregistrer" est cochee : l'ecran pose la question
        // (exports/ propose, le bouton ...), ecrit a la reponse puis appelle
        // exportAnswered. Faux (ou vide) : personne pour repondre ici - exportFile
        // l'ecrit tout de suite dans exports/, comme avant.
        std::function<bool(const ExportRequest&)>                askExport;
    };
    struct Animation {
        Id         from{kNoId}, to{kNoId};
        Transition spec;
        double     start{0};
        bool       popup{false};     // l'une des deux est une popup, dessinee par-dessus
        bool       closing{false};   // une popup qui se ferme
    };

    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Le projet est relu a chaque cycle : l'editeur peut le changer en marche.
    // `plc` peut etre nul (pas de simulateur) : seules les variables IHM existent.
    void bind(const Project* project, sim::Environment* plc);
    [[nodiscard]] const Project* project() const noexcept { return project_; }   // lot 12 : le menu de connexion le dessine
    void setHooks(Hooks h) { hooks_ = std::move(h); }

    // Variables IHM a leur valeur initiale, scripts Demarrage, vue de demarrage
    // ouverte (OnOpen, actions d'ouverture).
    void start(double now);
    void stop(double now, const std::string& why = {});   // 1.11.14 : why - la raison, dans la meme ligne du journal
    [[nodiscard]] bool running() const noexcept { return running_; }
    void tick(double now);

    // La souris sur un objet de la vue du dessus (la popup, s'il y en a une).
    void press(Id object, double now);
    void release(Id object, double now, bool inside);
    void doubleClick(Id object, double now);
    [[nodiscard]] Id pressed() const noexcept { return pressed_; }

    // ---- 1.11.23 : LA SOURIS ET LE CLAVIER (SYS.Mouse*, SYS.Key*, SYS.Key.<touche>) ET LES
    //      RACCOURCIS DES VUES (HmiRuntimeInput.cpp) ------------------------------------------
    //  Le canevas qui montre l'IHM (la simulation de l'editeur, le poste) dit ou est la souris,
    //  ses boutons, sa molette, et chaque touche enfoncee ou relachee. Une touche enfoncee
    //  cherche son raccourci : les popups du dessus vers le dessous, puis la vue (avec son
    //  modele, son en-tete, son pied) ; la premiere qui a une action de cette touche (memes
    //  Ctrl, Maj, Alt) la prend - une popup modale sans elle arrete la recherche. Puis :
    //  Touche enfoncee part tout de suite (le front montant ; la repetition du systeme ne
    //  compte pas), Touche maintenue une fois apres sa duree, Touche repetee toutes ses
    //  periodes tant qu'elle est tenue et sa vue montree, Touche relachee au relachement (le
    //  front descendant), dans la vue qui l'a prise meme fermee depuis. Des gestes de
    //  l'operateur : la securite, l'audit et le journal (Raccourci) les voient.
    struct Pointer {
        Id           view{kNoId};
        std::string  viewName, objectName;   // "Vue_1", "Vue_1.Bouton_2" ('' : aucun)
        double       x{0}, y{0};             // en pixels de la vue
        bool         inside{false};
        std::uint8_t buttons{0};             // 1 gauche, 2 droit, 4 milieu
        std::int64_t wheel{0};               // les crans depuis le lancement
    };
    void pointerMoved(Id view, double x, double y, Id object, bool inside);
    void pointerButton(int button /* 0 gauche, 1 droit, 2 milieu */, bool down, double now);
    void pointerWheel(double notches);
    void keyModifiers(bool ctrl, bool shift, bool alt);
    // Vrai : un raccourci a pris la touche (l'hote ne la passe pas a ses propres raccourcis).
    // `typing` : un champ de saisie a le clavier - seules F1..F12 y partent en raccourci.
    bool keyDown(const keys::Chord& chord, double now, bool repeat = false, bool typing = false);
    bool keyUp(std::string_view key, double now);
    // Le clavier perdu (le canevas n'a plus le focus) : chaque touche tenue est relachee.
    void releaseAllKeys(double now);
    // La vue qui prendrait cette touche (kNoId : aucune).
    [[nodiscard]] Id keyOwner(const keys::Chord&) const;
    [[nodiscard]] const Pointer& pointer() const noexcept { return pointer_; }
    [[nodiscard]] bool keyHeld(std::string_view key) const;

    bool navigate(Id view, const Transition&, double now);
    bool navigate(std::string_view viewName, const Transition&, double now);
    bool openPopup(Id view, const Transition&, double now);
    bool closePopup(const Transition&, double now);
    [[nodiscard]] Id currentView() const noexcept { return current_; }
    // La vue qui recoit les clics : la popup du dessus, sinon la vue courante.
    [[nodiscard]] Id topView() const noexcept { return popups_.empty() ? current_ : popups_.back(); }
    [[nodiscard]] const std::vector<Id>& popups() const noexcept { return popups_; }

    // ---- lot 8 : les popups intelligentes ------------------------------------------
    //  Ouvrir : a sa place (centree, sous l'objet clique, dans un coin, la ou
    //  elle etait, ou en x,y), avec ses parametres. DEJA OUVERTE : elle revient
    //  devant (et reprend les parametres donnes). Changer : la popup du dessus
    //  laisse la place a une autre, au meme endroit, et s'en souvient (Popup
    //  precedente y revient). Centrer : au milieu de la vue du dessous.
    bool navigate(Id view, const Transition&, double now, std::string_view arguments);
    bool openPopup(Id view, const Transition&, double now, std::string_view arguments, std::string_view placement,
                   Id opener = kNoId);
    bool changePopup(Id view, const Transition&, double now, std::string_view arguments = {});
    bool centerPopup(Id view = kNoId);                         // kNoId : celle du dessus
    bool previousPopup(const Transition&, double now);
    std::size_t closeAllPopups(const Transition&, double now);
    bool closePopupAt(std::size_t index, const Transition&, double now);   // la croix d'une popup
    void movePopup(std::size_t index, double x, double y);             // tiree par sa barre de titre
    bool raisePopup(std::size_t index);                                // un clic dans une popup non modale
    [[nodiscard]] const std::vector<PopupSlot>& popupSlots() const noexcept { return slots_; }
    // Les parametres d'une vue ouverte (la vue courante ou une popup) ; nul sans.
    [[nodiscard]] const Scope* viewScope(Id view) const;

    // ---- 1.9 : les parametres Copie / Les deux des popups (HmiRuntimeParams.cpp) ----
    //  Appliquer copie sur reference : la popup `view` (kNoId : celle du dessus)
    //  ecrit sa copie du parametre `name` (vide ou "*" : tous ceux en mode Les
    //  deux) dans la variable de l'appelant - SEULEMENT LES MEMBRES MODIFIES
    //  depuis la capture (ce que l'automate a change entre-temps reste). Rend
    //  le nombre de membres ecrits ; -1 : refuse (`why` dit pourquoi).
    int applyCopy(std::string_view name, Id view, double now, std::string* why = nullptr);
    // La copie d'un parametre d'une popup ouverte ; nulle : pas de copie.
    [[nodiscard]] const ParamCopy* paramCopy(Id view, std::string_view name) const;
    // Les DDT du programme (les membres a capturer) : fournis par l'appli, les tests.
    void setPlcTypes(params::PlcTypes t) { plcTypes_ = std::move(t); }

    // ---- lot 8 : les objets a saisie et les objets des utilisateurs ----------------
    //  Un clic sur une partie : "champ" (champ de saisie), "champ:utilisateur",
    //  "champ:motdepasse", "precedent", "suivant", "bouton" (connexion) ;
    //  "champ:ancien", "champ:nouveau", "champ:confirmation", "bouton" (changer le
    //  mot de passe) ; "bouton" (deconnexion) ; "ligne:2", "bouton:Ajouter"...
    //  (gestion des utilisateurs, comme le gestionnaire de recettes).
    void objectPart(Id object, std::string_view part, double now);
    void typeText(std::string_view text, double now);
    void typeKey(EditKey, double now);
    // Un clic ailleurs. 1.11.23 : `towards` - l'objet clique ; un bouton qui porte Valider les
    // saisies (au clic) ne fait pas perdre la saisie : il la valide lui-meme.
    void unfocus(double now, Id towards = kNoId);
    [[nodiscard]] Id               focusedObject() const noexcept { return focused_; }
    [[nodiscard]] const FormState* formState(Id object) const;
    // Le clavier virtuel du champ qui a le focus : "numerique", "complet", ou "".
    [[nodiscard]] std::string      keyboardMode() const;
    // Deconnexion automatique : les secondes qui restent (-1 : pas de minuterie).
    [[nodiscard]] double           autoLogoutRemaining(double now) const;
    [[nodiscard]] double           loggedInAt() const noexcept { return loginAt_; }
    [[nodiscard]] const std::optional<Animation>& animation() const noexcept { return animation_; }
    [[nodiscard]] double animationProgress(double now) const;   // lissee ; 1 sans animation

    [[nodiscard]] const std::deque<JournalEntry>& journal() const noexcept { return journal_; }
    void clearJournal() { journal_.clear(); }
    void log(std::string kind, std::string source, std::string message);
    // 1.11.14 : une ligne d'un niveau donne (IHM_LOG) ; `line` : la ligne du code (0 : celle
    // de l'instruction en cours, s'il y en a une).
    void logAt(LogLevel level, std::string kind, std::string source, std::string message, int line = 0);
    // 1.11.14 : la session de la Console - le numero du demarrage depuis l'ouverture de
    // l'application (1 au premier start, toutes les IHM simulees confondues).
    [[nodiscard]] int session() const noexcept { return session_; }
    // 1.11.15 : LA REMANENCE DE SIMULATION (hmi::simdata). captureData : les cases des
    // variables IHM non liees, a leur valeur du moment (prises a l'arret). setStartData :
    // celles a rendre au prochain start(), apres les valeurs initiales et avant les
    // scripts de Demarrage (une seule fois). lastRestore : ce que ce retour a fait
    // (rien : le dernier demarrage n'avait rien a rendre).
    // 1.11.16 : `keep` - les seules variables a prendre (les remanentes du poste) ; `label` - ce
    // que dit le journal du retour ("Variables remanentes restaurees" ; vide : la simulation).
    [[nodiscard]] std::vector<simdata::Cell> captureData(const std::function<bool(const Variable&)>& keep = {}) const;
    void setStartData(std::vector<simdata::Cell> cells, std::string label = {}) {
        startData_ = std::move(cells);
        startLabel_ = std::move(label);
    }
    [[nodiscard]] const std::optional<simdata::Report>& lastRestore() const noexcept { return lastRestore_; }
    // 1.11.16 : des cases rendues EN MARCHE (le poste, apres la reprise d'un arret brutal :
    // ses variables remanentes, plus recentes que l'etat de reprise, l'emportent) - les
    // memes regles que le retour au demarrage ; le journal le dit sous `label`.
    simdata::Report applyData(const std::vector<simdata::Cell>& cells, const std::string& label);
    // 1.11.18 (refonte des scripts, lot 5) : LES DECLARATIONS PERSISTANTES des scripts (une
    // variable du modele au stockage Persistante). captureData les prend avec les variables -
    // une case simdata::kDeclarationPath chacune : sa valeur du moment, ou celle rendue au
    // demarrage si son script n'a pas encore tourne. start() (et applyData) les gardent ; la
    // premiere execution de leur script les rend (un type simple ou une enumeration ; un type
    // change : convertie si compatible, sinon la valeur initiale et un avertissement).
    [[nodiscard]] std::size_t persistentPending() const noexcept { return persistPending_.size(); }

    // Lot 14 : la liaison Modbus TCP quand l'IHM est reliee a un automate reel
    // (nulle : le simulateur, ou rien).
    [[nodiscard]] const comm::Link* link() const noexcept;
    [[nodiscard]] comm::Link*       link() noexcept;
    // La qualite d'une valeur de l'automate ("Pression", "Tab[3]") : None pour ce
    // qui n'en est pas une (variable IHM, SYS., parametre de vue, vue) et sans
    // liaison ; sinon bonne, en attente, ancienne ou mauvaise (why : pourquoi).
    [[nodiscard]] comm::Quality plcQuality(std::string_view path, std::string* why = nullptr) const;
    // Une expression qui lit une valeur de l'automate pas encore lue, ou
    // illisible : les archives, les courbes et les graphiques la sautent.
    [[nodiscard]] bool plcUnread(const std::string& expression);
    // 1.9 : LES ALARMES A SURVEILLER - le seul point d'entree. Les variables de
    // l'automate et des equipements citees par leurs conditions (chemins de bits
    // et membres compris), leurs messages et leurs consignes sont SUIVIES EN
    // PERMANENCE par la liaison qui les lit (comm::Link::follow) : abonnees des
    // le demarrage, lues a chaque cycle, gardees quand les vues changent. Avant,
    // une alarme dont aucune vue ouverte ni aucun script ne lisait la variable ne
    // se levait jamais (la liaison n'abonne que ce qu'on lit). `start` remet les
    // listes a zero et y passe les alarmes du projet (source "projet", tenue a
    // jour si on les modifie en marche ; les mesures archivees suivent le meme
    // chemin, source "archives") ; une autre source d'alarmes evaluees (les
    // alarmes generees par les objets) y passe les siennes sous son nom, apres
    // `start` : chaque appel remplace la liste de sa source (vide : plus rien a
    // suivre pour elle). `stop` les retire des liaisons. Rend le nombre de
    // chemins suivis en tout (toutes sources).
    std::size_t watchAlarms(std::string_view source, const std::vector<AlarmDef>& defs);
    // Les chemins suivis (toutes sources, sans doublon, dans l'ordre des
    // sources puis de rencontre) : le diagnostic et les essais.
    [[nodiscard]] std::vector<std::string> watchedAlarmPaths() const;
    // Lot 15 : les variables IHM liees a un equipement - lues et ecrites par sa
    // liaison, avec leur qualite. La qualite compte-t-elle (une liaison Modbus,
    // ou des variables liees) ?
    [[nodiscard]] bool qualityRelevant() const noexcept;
    [[nodiscard]] const Variable* boundVariable(std::string_view name) const;
    // Lot 16 : une structure ou un tableau IHM en marche (ses bornes, son poids) ;
    // nul : ce chemin n'en est pas un. Les indices hors des bornes lus ou ecrits
    // depuis le lancement (chacun une fois au journal).
    [[nodiscard]] const types::Aggregate* aggregate(std::string_view path) const;
    [[nodiscard]] std::size_t outOfBounds() const noexcept { return boundsReported_.size(); }
    // La qualite d'une variable IHM liee, structure ou tableau compris (la pire
    // de ses cases) ; None : pas liee.
    [[nodiscard]] comm::Quality variableQuality(const Variable&, std::string* why = nullptr) const;

    // ---- lot 16 : les GIF animes (HmiRuntimeLot16.cpp) ----------------------------
    //  Chaque objet GIF anime d'une vue ouverte a sa lecture : lance a l'ouverture
    //  de la vue, par une action (Jouer le GIF...), par IHM_GIF_JOUER, ou tant que
    //  sa condition est vraie ; une fois, N fois ou en boucle, a sa vitesse ; a la
    //  fin, il reste sur sa derniere image, revient a la premiere, ou se cache.
    struct GifPlayback {
        bool   playing{false};
        bool   paused{false};
        bool   ended{false};       // fini (N fois joue)
        double startedAt{0};       // l'heure du (re)depart, moins le temps deja joue
        double pausedAt{0};
        int    loops{0};           // 0 : sans fin
        double speed{1.0};
        bool   conditionWas{false};
        int    frame{0};           // a la derniere mise a jour
        long long loopsDone{0};
    };
    [[nodiscard]] const GifPlayback* gifPlayback(Id object) const;
    // L'image a montrer a l'heure `t` (-1 : cache ; 0 : la premiere, avant de jouer).
    [[nodiscard]] int gifFrameAt(Id object, double t) const;
    // "jouer", "pause", "arreter", "rejouer" (count : N fois, 0 sans fin ; -1 :
    // celui de l'objet). Faux : pas de GIF anime de ce nom dans une vue ouverte.
    bool gifCommand(std::string_view objectName, std::string_view what, int count, double now, const std::string& source,
                    std::string* why = nullptr);
    [[nodiscard]] comm::Link*      equipmentLink(std::string_view equipment);
    [[nodiscard]] std::vector<EquipmentStatus> equipmentStatuses() const;
    [[nodiscard]] std::optional<EquipmentStatus> equipmentStatus(std::string_view name) const;

    // ---- 1.9 : les esclaves simules (HmiRuntimeSlaves.cpp) ---------------------------
    //  Ceux de l'application (Hooks::simSlaves) : la page Simulation, SYS.Slave.*,
    //  SYS.Sim*. Une variable IHM liee lue en ce moment sur l'esclave simule de son
    //  equipement (le chemin : "Vitesse", "Four1.Consigne", "Tab[2]") : le nom de
    //  l'equipement ; vide : lue sur le vrai (ou pas liee). Les equipements lus sur
    //  leur esclave en ce moment : le bandeau LECTURES SIMULEES.
    [[nodiscard]] std::vector<SimSlave> simSlaves(bool withValues = false) const;
    [[nodiscard]] std::string slaveReadOf(std::string_view path) const;
    [[nodiscard]] std::vector<EquipmentStatus> simulatedReads() const;
    // Une commande de la page Simulation : la permission Administrer (securite
    // eteinte : tout le monde) ; au journal. Faux et why : refusee.
    bool simSlaveCommand(const SimSlaveCommand&, double now, std::string* why = nullptr);
    // Lot 15 : l'etat de la marche, pour la reprise apres un arret brutal - la
    // vue montree, les variables IHM (les compteurs), les alarmes en cours et
    // leur acquittement. En texte (une ligne par element) ; `restoreState`
    // le recharge sur une marche qui vient de demarrer (report : ce qui l'a ete).
    [[nodiscard]] std::string stateSnapshot() const;
    bool restoreState(std::string_view text, double now, std::string* report = nullptr);
    // Lot 14 : les rapports periodiques. Generer maintenant : la periode en cours
    // jusqu'a maintenant (`current`), ou la derniere complete. Vrai : ecrit
    // (`where` : le fichier) ; faux : pourquoi. Et quand part le prochain.
    bool writeReport(Id report, double now, bool current, std::string* where = nullptr);
    [[nodiscard]] std::string nextReportText(Id report) const;
    // Le simulateur est-il relie (sans liaison Modbus) ?
    [[nodiscard]] bool simulatorAttached() const noexcept;
    // Le serveur de demonstration tourne-t-il, sur quel port (le crochet commDemo) ?
    [[nodiscard]] std::pair<bool, int> commDemoState() const {
        return hooks_.commDemo ? hooks_.commDemo() : std::pair<bool, int>{false, 0};
    }
    // Reconnecter la liaison, remettre ses compteurs a zero (le diagnostic automate).
    bool commReconnect(double now, const std::string& source);
    bool commResetCounters(double now, const std::string& source);

    // Variables IHM, puis automate : ce que lisent les expressions des vues.
    [[nodiscard]] sim::Environment& environment();
    [[nodiscard]] const sim::Value* variable(std::string_view name) const;
    // 1.12.2 : UNE VARIABLE IHM D'UN TYPE OBJET (LIST, VECTOR, MAP, TUPLE : types::isRich) - son
    // objet, entier, dans la memoire de l'IHM (les scripts et les expressions le lisent et
    // l'ecrivent en place) ; nul : pas une telle variable. `richText` : sa valeur ecrite comme un
    // litteral ([1, 2, 3], ['a' := 1]) ; vide : pas une telle variable.
    [[nodiscard]] sim::ObjRef richVariable(std::string_view name) const;
    [[nodiscard]] std::string richText(std::string_view name) const;
    // Lui donner la valeur d'un litteral (la simulation : « forcer ») ; faux et `why` sinon.
    bool setRichVariable(std::string_view name, std::string_view literal, std::string* why = nullptr);
    // 1.11.5 : FORCER UNE VARIABLE IHM (l'onglet Variables IHM de la simulation), le meme
    // contrat que l'automate simule : forcee, elle prend la valeur donnee et ignore les
    // ecritures (scripts, actions, champs, equipement lie) jusqu'au deforcage. Une case
    // (Four1.Vannes[1].Position) se force seule. Faux : pas une variable IHM, ou la valeur
    // n'est pas du bon type (`why` le dit).
    bool forceVariable(std::string_view name, const sim::Value& value, std::string* why = nullptr);
    bool unforceVariable(std::string_view name);
    void unforceAllVariables();
    [[nodiscard]] bool variableForced(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> forcedVariables() const;
    // Un script general, par son nom (Programmation generale, et les tests).
    bool callScript(std::string_view name, double now, std::string* error = nullptr);
    // Combien de fois un script a tourne ; sa derniere erreur.
    [[nodiscard]] std::size_t runs(Id script) const;
    [[nodiscard]] std::string lastError(Id script) const;
    [[nodiscard]] double      startedAt() const noexcept { return startNow_; }
    [[nodiscard]] double      now() const noexcept { return now_; }
    // Secondes depuis 1970 a cet instant IHM (l'horodatage des mesures).
    [[nodiscard]] double      epochOf(double now) const;
    // "2026-09-22 07:40:12.350" a cet instant IHM.
    [[nodiscard]] std::string dateStampOf(double now) const;

    // ---- lot 4 : alarmes ----------------------------------------------------------
    // Ou garder l'historique (alarmes terminees, evenements, systeme, mesures) ;
    // nul : en memoire seulement (events(), closedAlarms()).
    void setHistory(History* h) noexcept { history_ = h; }
    [[nodiscard]] const std::vector<LiveAlarm>&     alarms() const noexcept { return alarms_; }
    [[nodiscard]] const std::deque<HistoryEvent>&   events() const noexcept { return events_; }
    [[nodiscard]] const std::deque<AlarmOccurrence>& closedAlarms() const noexcept { return closed_; }
    [[nodiscard]] std::size_t unacknowledged() const noexcept;
    [[nodiscard]] int         highestPriority() const noexcept;   // parmi les non acquittees ; 0 sans
    // "" ou "*" : toutes ; "groupe:Armoire A" : un groupe ; sinon le nom d'une
    // alarme. Rend le nombre acquitte ; 0 et `why` si refuse ou rien a acquitter.
    std::size_t acknowledge(std::string_view target, double now, std::string* why = nullptr);

    // ---- recettes ------------------------------------------------------------------
    bool applyRecipe(std::string_view recipe, std::string_view record, double now, std::string* why = nullptr);
    // Les valeurs courantes des elements (lire un jeu depuis l'installation).
    [[nodiscard]] bool readRecipe(std::string_view recipe, std::vector<std::string>& values, std::string* why = nullptr);

    // ---- securite ------------------------------------------------------------------
    // `unixSeconds` : l'heure du code dynamique (negative : maintenant).
    bool login(std::string_view login, std::string_view secret, double now, std::string* why = nullptr,
               double unixSeconds = -1);
    void logout(double now, std::string_view reason = {});
    [[nodiscard]] const std::string& userLogin() const noexcept { return user_; }
    // Lot 13 : par ou passe la prochaine connexion (le journal d'audit le dit) :
    // "Menu de connexion", "Vue/Panneau_Connexion", "dialogue"...
    void setLoginSource(std::string source) { loginVia_ = std::move(source); }
    [[nodiscard]] const User*        user() const;
    [[nodiscard]] int                level() const;                 // 0 sans utilisateur ; securite eteinte : 99
    [[nodiscard]] bool               permitted(std::string_view permission) const;
    // Lot 8 : la connexion en liste montre cet utilisateur - celui choisi aux
    // fleches, sinon l'utilisateur connecte, sinon le premier compte actif.
    [[nodiscard]] const User*        loginChoice(Id object) const;
    // Le niveau d'acces de l'objet et son autorisation par expression.
    [[nodiscard]] bool               objectAllowed(const Object&, std::string* why = nullptr);

    // ---- courbes -------------------------------------------------------------------
    [[nodiscard]] const std::vector<TrendSeries>* trend(Id view, Id object) const;
    // Lot 18 : LES MARQUES DES COURBES - ce qui a change la valeur d'une variable
    // simulee (une zone de mouvement tiree, un forcage) : une ligne verticale sur
    // les courbes qui la tracent, a l'heure du changement (now()).
    struct TrendMarker {
        double      at{0};
        std::string variable;          // la plume ("Courant_L1")
        std::string text;              // "zone : 8 - 16 -> 10 - 18", "forc\xC3\xA9" "e : 51"
        bool        forced{false};
        // 1.9 : une bascule de l'equipement (le vrai <-> l'esclave simule) - le trait
        // pointille violet, "14:02:31 - lu sur l'esclave simule".
        bool        slave{false};
    };
    void addTrendMarker(std::string variable, std::string text, bool forced, bool slave = false);
    [[nodiscard]] const std::vector<TrendMarker>& trendMarkers() const noexcept { return markers_; }

    // ---- lot 6 : la vue telle qu'elle tourne (ecran modele, en-tete, pied) -----------
    //  Le dessin et l'evaluation prennent celle-ci, pas celle du projet : les
    //  objets du modele y sont, ses scripts passent avant ceux de la vue.
    [[nodiscard]] const View* composedView(Id view) const { return viewOf(view); }

    // ---- lot 6 : le gestionnaire de recettes ----------------------------------------
    //  Le jeu choisi, par objet (un clic sur une ligne). Un clic sur une partie de
    //  l'objet : "ligne:2" choisit le 3e jeu ; "bouton:Appliquer" ecrit le jeu choisi,
    //  "bouton:Ajouter" / Modifier / Supprimer / Lire passent par l'ecran
    //  (Hooks::recipeRequest). Sous securite, les boutons demandent la permission
    //  Recettes.
    [[nodiscard]] Id recipeSelection(Id object) const;
    void selectRecipeRecord(Id object, Id record);
    void recipeManagerClick(Id object, std::string_view part, double now);

    // ---- lot 6 : actions sur les ressources ----------------------------------------
    // La ressource choisie par l'ecran pour une demande : son nom dans la variable.
    void resourceProvided(const ResourceRequest&, std::string_view resourceName, double now);
    // Un tableau relie en marche a un fichier externe (Lier un tableau) : son nom,
    // ou nul (la source de l'editeur).
    [[nodiscard]] const std::string* tableSource(Id object) const;
    // Les parametres systeme, montres par-dessus la vue (Parametres systeme).
    [[nodiscard]] bool systemShown() const noexcept { return systemShown_; }
    void showSystem(bool shown) noexcept { systemShown_ = shown; }
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> systemInfo() const;

    // ---- lot 10 : le menu natif Parametres systeme (HmiRuntimeSystem.cpp) -------------
    //  Ouvert par l'action Parametres systeme, l'objet du meme nom ou
    //  IHM_PARAMETRES_SYSTEME() ; `tab` : 0 Reglages, 1 Diagnostic ; 1.9 : 2 la page
    //  Simulation (sans la permission Administrer : la carte du refus, "Acces refuse"
    //  au journal ; le poste qui ne l'a pas : Reglages).
    void openSystemMenu(int tab, double now, const std::string& source = {});
    [[nodiscard]] int systemTab() const noexcept { return systemTab_; }
    // Une partie du menu (systemMenuHit) : un onglet, un reglage, l'heure, un
    // geste de maintenance, fermer ("dehors" aussi).
    void systemPart(std::string_view part, double now);
    // Les reglages du poste : gardes d'un lancement de l'IHM a l'autre, jamais
    // dans le projet.
    [[nodiscard]] const SystemSettings& settings() const noexcept { return settings_; }
    void setSettings(const SystemSettings& s) { settings_ = s; }
    [[nodiscard]] bool settingAllowed(std::string_view key) const;      // protege : Administrer
    // Le dernier message du menu (un reglage change, un refus).
    [[nodiscard]] const std::string& systemMessage() const noexcept { return systemMessage_; }
    [[nodiscard]] bool systemMessageIsError() const noexcept { return systemMessageError_; }
    // L'heure en cours de reglage dans le menu (sinon celle de l'IHM) ; `pending` : pas appliquee.
    [[nodiscard]] DateTime systemClock(bool* pending = nullptr) const;
    [[nodiscard]] std::size_t systemScroll() const noexcept { return systemScroll_; }
    // Le diagnostic : des groupes de lignes, chacune reliee a sa variable SYS.
    [[nodiscard]] std::vector<DiagGroup> diagnostics() const;
    [[nodiscard]] std::size_t diagnosticRows() const;    // lignes, titres des groupes compris

    // ---- 1.9 : la page Simulation de Parametres systeme (HmiRuntimeSimPage.cpp) ---------
    //  L'onglet 2 : les esclaves simules de l'application (simSlaves), ce que lit
    //  l'IHM pour chaque equipement lie, et les valeurs de l'esclave choisi.
    //  systemPart traduit ses parties en commandes (simSlaveCommand : la
    //  permission Administrer, le journal) et dit ce qui s'est passe (le message
    //  du menu). L'esclave choisi (par son equipement) et le defilement vivent ici :
    //  ils survivent au rafraichissement des donnees, pas au redemarrage de l'IHM.
    [[nodiscard]] bool simPageAvailable() const;      // la page existe ici (le poste : Station::simPage)
    [[nodiscard]] bool simPageAllowed() const;        // la permission Administrer (securite eteinte : tous)
    // Ce que la geometrie doit savoir (les cartes, les lignes de l'esclave choisi).
    [[nodiscard]] SimPageShape simPageShape(const std::vector<SimSlave>& slaves) const;
    [[nodiscard]] std::size_t simChosenIndex(const std::vector<SimSlave>& slaves) const;
    [[nodiscard]] const std::string& simChosen() const noexcept { return simChosen_; }    // l'equipement ("" : par defaut)
    [[nodiscard]] std::size_t simScroll() const noexcept { return simScroll_; }
    [[nodiscard]] std::size_t simListScroll() const noexcept { return simListScroll_; }
    // Change a chaque geste de la page (une commande, un choix) : l'ecran relit les esclaves.
    [[nodiscard]] std::uint64_t simRevision() const noexcept { return simRevision_; }
    // L'IHM lit-elle cet equipement sur son esclave simule (ou n'a-t-il que lui) ?
    // IHM_ESCLAVE_SIMULE, SYS.Slave.<nom>.Read.
    [[nodiscard]] bool readOnSlave(std::string_view equipment) const;
    [[nodiscard]] static bool slaveRead(const SimSlave&) noexcept;
    // La ligne violette d'une carte : "L'IHM lit l'esclave : le vrai ne repond pas
    // depuis 14:02:21." ; vide : l'IHM ne le lit pas.
    [[nodiscard]] static std::string simCardWhy(const SimSlave&);
    // La mise en veille : l'ecran est eteint depuis `screensaverMin` minutes sans
    // toucher. wake() : vrai s'il dormait (ce toucher-la est mange, rien n'agit).
    [[nodiscard]] bool asleep(double now) const;
    bool wake(double now);
    // La deconnexion automatique en vigueur (reglage du poste, sinon le projet) ; 0 : jamais.
    [[nodiscard]] int autoLogoutMinutes() const;

    // ---- lot 7 : essayer une fonction IHM (le volet Fonctions, les tests) ----------
    //  prime : les variables IHM a leur valeur initiale, SANS lancer l'IHM (ni
    //  script de demarrage, ni vue) - un banc d'essai. runFunction : l'appel,
    //  comme depuis un script (ses ecritures ont lieu) ; faux et `why` si la
    //  fonction n'existe pas ou echoue. Arguments : dans l'ordre (nom vide) ou
    //  par leur nom.
    void prime(double now);
    bool runFunction(std::string_view name, const std::vector<std::pair<std::string, sim::Value>>& args,
                     sim::Value& result, std::string* why = nullptr);
    // 1.11.20 : une fonction par son identifiant (deux surcharges portent le meme nom) ; `outputs` :
    // la valeur finale de ses parametres E/S et sorties, dans l'ordre (l'essai les montre).
    bool runFunction(Id id, const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result,
                     std::string* why = nullptr, std::vector<std::pair<std::string, sim::Value>>* outputs = nullptr);

    // ---- lot 9 : les commandes et les afficheurs --------------------------------------
    //  Les commandes ECRIVENT leur variable ("variable"), sous la permission
    //  Piloter quand la securite est active : l'impulsion a l'appui et au
    //  relachement (press / release), les bascules au clic, les choix par partie
    //  (objectPart : "position:1", "option:2", "choix:0", "plus:jour",
    //  "case:0,14"...), le curseur et le potentiometre en glissant.
    //  `fraction` : 0..1 de la course ; `commit` : relache (la valeur est ecrite,
    //  au pas, dans les bornes ; "Ecrire en glissant" ecrit a chaque mouvement).
    void dragValue(Id object, double fraction, bool commit, double now);
    // La valeur montree pendant qu'on glisse ; nullopt : celle de la variable.
    [[nodiscard]] std::optional<double> dragPreview(Id object) const;
    // Bouton a confirmation : le second clic attendu (jusqu'a 3 s) ; l'appui
    // maintenu, de 0 a 1.
    [[nodiscard]] bool   confirmPending(Id object, double now) const;
    [[nodiscard]] double holdProgress(Id object, double now) const;
    // La liste deroulante ouverte, et sa premiere ligne montree.
    [[nodiscard]] bool comboOpen(Id object, std::size_t* first = nullptr) const;
    // 1.12.2 : LES ELEMENTS D'UNE SOURCE (itemsFrom d'une liste, d'une liste deroulante...) : une
    // enumeration du projet (ses textes ; la valeur : son nombre), une variable LIST, VECTOR, MAP,
    // un tableau IHM (ses elements, ses cles), une expression qui rend "a;b;c". Faux : illisible.
    bool resolveChoices(std::string_view source, std::vector<Choice>& out);
    // 1.12.2 : la liste (Kind::List), le tableau dynamique (rowsFrom) - le rang de leur premiere
    // ligne montree (ils defilent).
    [[nodiscard]] std::size_t listFirst(Id object) const;
    // 1.12.2 : LES LIGNES D'UN TABLEAU DYNAMIQUE (rowsFrom) : une variable LIST, VECTOR, un tableau
    // IHM (de valeurs, de structures, de tuples) ou une MAP - une ligne par element, autant qu'il en a ;
    // `headers` : les colonnes trouvees (les membres, Item1..., Valeur ; Cle et Valeur pour une MAP).
    // Faux : pas de source, ou elle ne se lit pas.
    bool tableRows(const Object& o, std::vector<std::string>& headers, std::vector<std::vector<std::string>>& rows) const;
    // Le programmateur horaire : ses plages (celles de sa variable, ou de l'objet).
    [[nodiscard]] const WeekSchedule* schedule(Id object) const;
    // La date et l'heure : celle en cours de reglage, sinon celle de la variable
    // (sinon maintenant) ; `pending` : un reglage pas encore valide.
    [[nodiscard]] DateTime pickerValue(Id object, bool* pending = nullptr) const;
    // Le compteur horaire (secondes comptees) ; la fleche de tendance (-1, 0, +1).
    [[nodiscard]] double hourMeterSeconds(Id object) const;
    [[nodiscard]] bool   hourMeterRunning(Id object) const;      // sa condition est vraie : il compte
    [[nodiscard]] int    trendArrow(Id object) const;

    // ---- lot 11 : les graphiques (HmiRuntimeLot11.cpp) ---------------------------------
    //  Ce que le moteur mesure a chaque cycle pour les graphiques qui ont une
    //  memoire : un chronogramme (les changements de chaque ligne : instant,
    //  valeur), une courbe XY (les points (x, y) de chaque courbe), un
    //  histogramme (les mesures : instant, valeur). Nul : rien encore.
    [[nodiscard]] const std::vector<TrendSeries>* chartSeries(Id view, Id object) const;

    // ---- lot 11 : les alarmes ----------------------------------------------------------
    //  Mettre de cote : "" (l'alarme choisie), un nom, "groupe:Zone". `minutes` <= 0 :
    //  sans limite (au plus la limite du projet sinon). Permission Acquitter sous
    //  securite. Rend le nombre mis de cote ; 0 et `why` sinon.
    [[nodiscard]] const std::vector<ShelvedAlarm>& shelvedAlarms() const noexcept { return shelved_; }
    [[nodiscard]] bool isShelved(Id alarm) const;
    std::size_t shelve(std::string_view target, double minutes, std::string_view reason, double now, std::string* why = nullptr);
    // "" : l'alarme choisie ; "*" : toutes ; un nom ; "groupe:Zone".
    std::size_t unshelve(std::string_view target, double now, std::string* why = nullptr);
    // L'alarme choisie (un clic dans un bandeau, une liste, un resume) et la zone choisie.
    [[nodiscard]] const std::string& selectedAlarm() const noexcept { return selectedAlarm_; }
    void selectAlarm(std::string_view name);
    [[nodiscard]] const std::string& selectedZone() const noexcept { return selectedZone_; }
    // Faire taire : plus de son d'alarme (ni de repetition) jusqu'a la prochaine apparition.
    void silenceAlarms(double now, const std::string& source = {});
    [[nodiscard]] bool alarmsSilenced() const noexcept { return silenced_; }
    // Le bandeau : l'indice dans alarms() de l'alarme qu'il montre (-1 : aucune).
    [[nodiscard]] int bannerAlarm(const Object&) const;

    // ---- 1.9 : les alarmes des objets (HmiObjectAlarms.hpp, HmiRuntimeObjectAlarms.cpp) ----
    //  Generees depuis le projet (objets du synoptique, instances de symboles), les
    //  cochees vivent comme celles du projet (apparition, delai, acquittement, mise
    //  de cote, historique, sons, SYS.Alarm*). Refaites quand le projet change (au
    //  plus une fois par seconde en marche) ; refreshObjectAlarms() : tout de suite.
    void refreshObjectAlarms();
    [[nodiscard]] std::size_t       objectAlarmCount() const noexcept;    // generees et cochees
    [[nodiscard]] const ObjectAlarm* objectAlarm(Id) const noexcept;      // nul : pas une alarme d'objet
    [[nodiscard]] const AlarmDef*   alarmDef(Id) const noexcept;          // du projet, ou d'un objet
    [[nodiscard]] const AlarmDef*   alarmDefByName(std::string_view) const noexcept;
    // Les variables publiques d'un objet qui porte des alarmes (son groupe interne,
    // "Vue.Objet" ; une instance compte ses objets) : en cours, actives, a
    // acquitter, la priorite la plus forte en cours (0 : aucune). `known` : faux
    // quand aucun objet ne porte ce groupe.
    struct GroupFigures {
        std::size_t count{0}, active{0}, unacked{0};
        int         highest{0};
        bool        known{false};
    };
    [[nodiscard]] GroupFigures objectGroupFigures(std::string_view group) const;
    [[nodiscard]] bool         isObjectGroup(std::string_view group) const;
    // Les alarmes d'objet (cochees) d'un groupe : groupe d'objet, groupe declare, motif, symbole:.
    [[nodiscard]] std::vector<const AlarmDef*> objectAlarmDefsIn(std::string_view filter) const;
    // Les definitions generees cochees (copies, identifiants de marche compris) :
    // ce que le moteur fait vivre en plus des alarmes du projet. Pour la fusion
    // avec B : watchAlarms("objets", activeObjectAlarmDefs()) apres start() et a
    // chaque regeneration (objectAlarmsGeneration() change).
    [[nodiscard]] std::vector<AlarmDef> activeObjectAlarmDefs() const;
    [[nodiscard]] std::uint64_t         objectAlarmsGeneration() const noexcept { return objectAlarmsGen_; }

    // ---- lot 11 : la production ---------------------------------------------------------
    [[nodiscard]] ProductionFigures production(Id object) const;
    // ---- lot 11 : l'editeur de recette (nul : l'objet n'a pas encore tourne) --------------
    [[nodiscard]] const RecipeEditorState* recipeEditor(Id object) const;
    // ---- lot 11 : l'export ------------------------------------------------------------------
    //  Le tableau d'une source : "alarmes", "historique", "evenements", "systeme",
    //  "mesures", "recette:Nom", "objet:Nom" (un objet de la vue du dessus).
    [[nodiscard]] bool exportTable(std::string_view source, ExportTable& out, std::string* why = nullptr);
    //  Le fichier : le nom (texte a trous : export_{SYS.Date}), le format ("CSV",
    //  "Excel", "PDF" ; vide : l'extension du nom, sinon CSV). L'ecran l'ecrit.
    //  Lot API 8 : `askWhere` - l'option "Demander ou enregistrer" de ce qui
    //  exporte ; la question ne se pose que pendant un geste de l'operateur (un
    //  clic, un double clic, un appui long, et ce qu'ils declenchent) et si
    //  l'ecran sait la poser (Hooks::askExport). Vrai : ecrit, ou la question est
    //  posee (le fichier s'ecrit a la reponse, exportAnswered).
    bool exportData(std::string_view source, std::string_view fileName, std::string_view format, double now,
                    const std::string& origin = {}, std::string* why = nullptr, bool askWhere = false);
    [[nodiscard]] const std::string& lastExport() const noexcept { return lastExport_; }
    // ---- Lot API 8 : les exports qui demandent ou ----
    //  La reponse a une question posee (Hooks::askExport) : `written` - le fichier
    //  est ecrit, `where` son chemin (SYS.LastExport, SYS.ExportCount, l'evenement) ;
    //  sinon `where` vide : annule (le journal le dit), rempli : pourquoi.
    void exportAnswered(const ExportRequest& rq, bool written, const std::string& where, double now);
    //  Un export lance par l'operateur hors d'un objet (une commande de session) :
    //  comme le clic d'un bouton d'export - un geste, donc la question si `askWhere`.
    bool exportAsOperator(std::string_view source, std::string_view fileName, std::string_view format, double now, bool askWhere,
                          std::string* why = nullptr);

    // ---- lot 12 : la navigation (HmiRuntimeLot12.cpp) -------------------------------------
    //  L'HISTORIQUE : chaque navigation empile la vue quittee (au plus 50) ;
    //  Precedent y revient (la vue quittee passe dans Suivant), Suivant repart ;
    //  une navigation ordinaire vide Suivant. L'ACCUEIL : la vue de demarrage du
    //  groupe de l'utilisateur connecte, sinon celle du projet.
    struct NavEntry {
        Id          view{kNoId};
        std::string arguments;       // les parametres donnes a l'ouverture
    };
    [[nodiscard]] const std::vector<NavEntry>& backHistory() const noexcept { return back_; }
    [[nodiscard]] const std::vector<NavEntry>& forwardHistory() const noexcept { return forward_; }
    // Les vues de Precedent, de la plus ancienne a la plus recente (le fil d'Ariane).
    [[nodiscard]] std::vector<std::string> historyNames() const;
    bool goBack(const Transition&, double now, int steps = 1, std::string* why = nullptr);
    bool goForward(const Transition&, double now, std::string* why = nullptr);
    bool goHome(const Transition&, double now, std::string* why = nullptr);
    [[nodiscard]] Id homeView() const;
    // Un glisser sur le fond de la vue (Configuration : Changer de vue en glissant) :
    // +1 la vue suivante (le doigt part vers la gauche), -1 la precedente. L'ordre :
    // celui de la barre de navigation de la vue, sinon les vues ordinaires du projet.
    bool swipe(int direction, double now, std::string* why = nullptr);
    // Le zoom de la vue courante en marche (l'ecran le donne), en % : SYS.ViewZoom.
    void setViewZoom(double percent) noexcept { viewZoom_ = percent; }
    [[nodiscard]] double viewZoom() const noexcept { return viewZoom_; }
    // "Accueil > Production > Ligne 1" : la vue courante et ses vues parentes.
    [[nodiscard]] std::string navigationPath() const;

    // ---- lot 12 : le menu natif de connexion (HmiRuntimeLogin.cpp) ---------------------
    //  Comme Parametres systeme : par-dessus la vue, ouvert par l'action "Menu de
    //  connexion", l'objet du meme nom ou IHM_MENU_CONNEXION(). Ses onglets se
    //  montrent selon le niveau de l'utilisateur connecte (visibleLoginTabs) ;
    //  un onglet demande qu'il ne voit pas : Connexion, et le message dit
    //  pourquoi. Changer un compte, un role, la deconnexion automatique demande
    //  la permission Administrer et passe par l'ecran (Hooks::userRequest).
    void openLoginMenu(LoginTab tab, double now, const std::string& source = {});
    [[nodiscard]] bool     loginShown() const noexcept { return loginShown_; }
    [[nodiscard]] LoginTab loginTab() const noexcept { return loginTab_; }
    [[nodiscard]] std::vector<LoginTab> loginTabs() const;
    // Une partie du menu (loginMenuHit) ; "defiler:=12" : la premiere ligne montree.
    void loginPart(std::string_view part, double now);
    // Les champs tapes ("secret", "ancien", "nouveau", "confirmation"), le message,
    // le compte choisi aux fleches (chosen), la ligne choisie (selected).
    [[nodiscard]] const FormState& loginForm() const noexcept { return loginForm_; }
    [[nodiscard]] bool   loginFocused() const noexcept { return loginShown_ && !loginForm_.focus.empty(); }
    // Le compte montre par l'onglet Connexion : celui choisi aux fleches, sinon
    // l'utilisateur connecte, sinon le premier compte actif.
    [[nodiscard]] const User* loginAccount() const;
    [[nodiscard]] std::size_t loginScroll() const noexcept { return loginScroll_; }
    [[nodiscard]] std::size_t loginRows() const;      // Comptes : les comptes ; Journal : ses lignes
    // Les connexions, deconnexions, refus et changements de comptes, recents d'abord.
    [[nodiscard]] std::vector<HistoryEvent> loginJournal() const;

    // ---- lot 9 : les variables systeme et d'instances (HmiRuntimePublic.cpp) ----------
    //  SYS.UserName, SYS.CurrentView... (lecture seule) ; Vue.Objet.Propriete,
    //  Vue.Objet.Pressed, Vue.Open, Popup.X... (HmiPublicVars.hpp). Les
    //  expressions, les textes a trous et les scripts les lisent par
    //  environment() ; un script ou l'action Affecter les ecrit : la propriete
    //  est alors figee a cette valeur (son expression ne compte plus) jusqu'au
    //  redemarrage. publicWrite : 1 ecrit, 0 refuse (`why`), -1 pas une variable
    //  publique (sa racine n'est ni SYS ni une vue).
    [[nodiscard]] bool publicRead(std::string_view path, sim::Value& out);
    int                publicWrite(std::string_view path, const sim::Value& value, std::string* why = nullptr);
    [[nodiscard]] bool publicExists(std::string_view path) const;
    // Les proprietes ecrites en marche : combien, et la valeur de l'une (nul : pas ecrite).
    [[nodiscard]] std::size_t        overrideCount() const noexcept;
    [[nodiscard]] const std::string* overrideOf(Id view, Id object, std::string_view key) const;

    // ---- lot 13 : la securite renforcee (HmiRuntimeLot13.cpp) --------------------------
    //  LE JOURNAL D'AUDIT (Configuration > Historiques > Journal d'audit) : chaque
    //  variable ecrite par un geste de l'operateur (un clic, un choix, une
    //  saisie, un glisser : avant -> apres), chaque recette appliquee (toutes ses
    //  valeurs sur une ligne), acquittement, mise de cote, connexion, refus,
    //  verrouillage, mot de passe change, signature. Chaine par les empreintes.
    //  Les lignes : celles de l'historique (setHistory), sinon celles du moteur.
    [[nodiscard]] const std::vector<AuditEntry>& auditTrail() const noexcept;
    [[nodiscard]] bool auditOn() const noexcept;
    // Une ligne venue de l'ecran (un mot de passe donne dans un dialogue).
    void recordAudit(std::string kind, std::string source, std::string target, std::string before = {}, std::string after = {},
                     std::string reason = {});
    //  LE VERROUILLAGE : Security::lockAttempts echecs de suite et le compte ne se
    //  connecte plus - Security::lockMinutes minutes, ou jusqu'a ce qu'un
    //  administrateur le deverrouille (onglet Comptes du menu de connexion).
    [[nodiscard]] const AccountState* accountState(std::string_view login) const;
    [[nodiscard]] bool accountLocked(std::string_view login) const;
    bool unlockAccount(std::string_view login, double now, std::string* why = nullptr, const std::string& source = {});
    //  LE RENOUVELLEMENT : un mot de passe perime, ou donne par un administrateur
    //  (a changer a la premiere connexion) - login() refuse, le menu de connexion
    //  s'ouvre et demande le nouveau ; accepte, la connexion se fait.
    [[nodiscard]] bool renewalPending() const noexcept { return renewal_.has_value(); }
    [[nodiscard]] std::string renewalLogin() const { return renewal_ ? renewal_->login : std::string{}; }
    [[nodiscard]] std::string renewalReason() const { return renewal_ ? renewal_->reason : std::string{}; }
    //  LA SIGNATURE ELECTRONIQUE (HmiSignature.hpp) : le geste d'un objet a
    //  signature attend le panneau natif ; signe, il agit.
    [[nodiscard]] bool signatureShown() const noexcept { return signature_.has_value(); }
    [[nodiscard]] const SignatureRequest* signatureRequest() const noexcept { return signature_ ? &*signature_ : nullptr; }
    [[nodiscard]] const FormState& signatureForm() const noexcept { return signForm_; }
    void signaturePart(std::string_view part, double now);
    [[nodiscard]] const User* signatureSigner() const;      // celui qui signe
    [[nodiscard]] const User* signatureVisa() const;        // double : le second
    [[nodiscard]] const std::string& lastSignature() const noexcept { return lastSignature_; }
    //  1.11.7 : LE CLAVIER VIRTUEL D'UNE ACTION (Operation::Keyboard, HmiPrompt.hpp) - un
    //  champ de saisie par-dessus la vue, modal : ce qui est tape y va (typeText,
    //  typeKey), Entree (ou Valider) ecrit la valeur dans la cible si elle se lit
    //  (un nombre, dans ses limites ; un BOOL ; un texte), Echap (ou Annuler) ferme.
    struct KeyboardPrompt {
        std::string           target;              // la variable (sans ses $)
        std::string           title;               // le titre montre
        std::string           keyboard;            // "numerique" ou "complet"
        std::string           unit;                // "degC"
        std::optional<double> min, max;
        bool                  mask{false};         // un code : des points
        sim::Type             type{sim::Type::Unknown};
        std::string           source;              // l'objet de l'action (le journal)
    };
    [[nodiscard]] bool promptShown() const noexcept { return prompt_.has_value(); }
    [[nodiscard]] const KeyboardPrompt* keyboardPrompt() const noexcept { return prompt_ ? &*prompt_ : nullptr; }
    [[nodiscard]] const FormState& promptForm() const noexcept { return promptForm_; }
    // "bouton:valider", "bouton:annuler", "fermer", "champ", "dehors".
    void promptPart(std::string_view part, double now);
    //  L'AVERTISSEMENT : les Security::logoutWarnS dernieres secondes avant la
    //  deconnexion automatique, un bandeau compte ; un toucher (stayConnected)
    //  remet la minuterie a zero.
    [[nodiscard]] bool logoutWarning(double now) const;
    void stayConnected(double now);
    //  LE BADGE (Security::badgeLogin) : un lecteur tape le numero puis Entree,
    //  hors de tout champ (typeText, typeKey) ; le compte dont c'est le badge
    //  se connecte, sans mot de passe.
    [[nodiscard]] bool badgeListening() const;
    bool loginBadge(std::string_view number, double now, std::string* why = nullptr);

    // ---- lot 13 : les performances (HmiPerf.hpp) --------------------------------------
    //  Le moteur mesure ses cycles, ses scripts, ses ecritures ; l'ecran lui donne
    //  l'evaluation des expressions et le dessin (notePerf). Remis a zero au
    //  lancement, et a la demande.
    [[nodiscard]] const PerfStats& perf() const noexcept { return perf_; }
    void resetPerf();
    void notePerf(double evaluateMs, double paintMs, std::size_t expressions, std::size_t objects);

    // ---- lot 13 : la langue (HmiLanguages.hpp) ------------------------------------------
    //  SYS.Language : la langue que lit l'operateur - celle du demarrage
    //  (Configuration > Langues), puis un Selecteur de langue, l'action Changer de
    //  langue, IHM_LANGUE('en') ou SYS.Language := 'de'. Le dessin traduit la vue
    //  montree (translatedView) ; une alarme prend son message et sa consigne dans
    //  la langue du moment ou elle apparait. setLanguage : un code ("en"), un nom
    //  ("English") ou "suivante" (la langue d'apres, la premiere apres la
    //  derniere) ; faux : langue inconnue (`why`, et le journal).
    [[nodiscard]] const std::string& language() const noexcept { return language_; }
    bool setLanguage(std::string_view code, double now, const std::string& source, std::string* why = nullptr);
    [[nodiscard]] std::size_t languageChanges() const noexcept { return languageChanges_; }
    // ---- lot 13 : l'affichage (HmiDisplay.hpp) -----------------------------------------------
    //  Au lancement, celui du projet (Configuration) ; puis le menu Parametres
    //  systeme, SYS.TextScale, SYS.ColorMode, SYS.StatusSymbols, SYS.Theme,
    //  l'action "Changer de theme", IHM_THEME('jour'), le Selecteur de theme.
    //  setDisplay : "texte" (100, 125, 150, 175 ; "plus", "moins"), "couleurs"
    //  (normal, daltonien), "symboles" (TRUE, FALSE), "theme" (jour, nuit,
    //  bascule). Faux : valeur refusee (`why`).
    [[nodiscard]] DisplayOptions displayOptions() const;
    bool setDisplay(std::string_view key, std::string_view value, double now, const std::string& source, std::string* why = nullptr);

private:
    class Env;
    friend class Env;
    struct TriggerState {
        bool        known{false};
        std::string last;          // la derniere valeur vue (en texte)
        double      nextFire{0};
    };
    const View* viewOf(Id) const;
    sim::Value* ihmSlot(std::string_view name);      // lot 15 : la case d'une variable IHM (nulle : aucune)
    void openView(Id view, double now, bool popup);
    void closeView(Id view, double now);
    void runViewScript(const View&, std::string_view event, double now);
    bool runScript(const Script&, const std::string& source, double now, std::string* error);
    bool runStatements(const std::string& code, const std::string& source, double now, std::string* error,
                       Id scriptId = kNoId);
    void fire(const View&, const Object*, const Action&, double now, bool byUser = false);
    void runActions(const View&, const Object*, Trigger, double now);
    void evaluateAlarms(double now);
    void closeAlarm(std::size_t index);
    void sampleTrends(double now);
    void sampleArchive(double now);
    void event(std::string kind, std::string source, std::string message);
    void watchTriggers(const View&, double now, bool cycle);
    void cycle(double now);
    void initVariables();                                   // les variables IHM, a leur valeur initiale
    [[nodiscard]] std::string evalText(const std::string& expr, bool* ok = nullptr);
    [[nodiscard]] bool evalBool(const std::string& expr, bool fallback);
    bool write(const std::string& name, const sim::Value& v, const std::string& source);
    [[nodiscard]] std::string stampOf(double now) const;
    [[nodiscard]] std::string where(const View&, const Object*) const;
    [[nodiscard]] std::vector<std::string> actionOriginNames(const View&) const;   // lot 6
    // ---- lot 8
    [[nodiscard]] std::shared_ptr<const Scope> scopePtr(Id view) const;
    void syncPopupIds();
    void placeSlot(PopupSlot&, const View* opener = nullptr);
    std::shared_ptr<const Scope> buildScope(const View&, std::string_view arguments);
    const View* shownViewOf(Id object) const;     // la vue ouverte qui porte cet objet
    void submitForm(const View&, const Object&, double now);
    void formMessage(Id object, std::string message, bool error, double now);
    void userManagerClick(const View&, const Object&, std::string_view part, double now);
    // ---- lot 9 (HmiRuntimeControls.cpp)
    struct ControlState {
        std::optional<double>   drag;               // curseur, potentiometre : la valeur suivie
        double                  confirmUntil{0};    // bouton : le second clic attendu jusqu'a
        bool                    holdFired{false};   // bouton : l'appui maintenu a fait son effet
        bool                    comboOpen{false};
        std::size_t             comboFirst{0};
        std::optional<WeekSchedule> schedule;       // programmateur : ses plages
        std::string             scheduleText;       // ... telles qu'ecrites dans sa variable
        int                     output{-1};         // ... la derniere sortie ecrite (-1 : aucune)
        std::optional<DateTime> picker;             // date et heure : le reglage en cours
        double                  runSeconds{0};      // compteur horaire : le temps compte
        bool                    running{false};     // ... sa condition, au dernier cycle
        double                  lastTick{-1};
        double                  lastWritten{-1};    // ... ce qu'il a ecrit dans sa variable (secondes)
        std::deque<std::pair<double, double>> samples;   // fleche de tendance : (instant, valeur)
        int                     trend{0};
        std::optional<DateTime> current;            // date et heure : la valeur de sa variable
    };
    std::map<Id, ControlState> controls_;
    // Les parametres d'une vue ouverte, le temps d'un bloc (HmiRuntimeControls.cpp
    // ne voit pas Env) : l'objet rendu les remet en place quand il meurt.
    [[nodiscard]] std::shared_ptr<void> viewAliases(Id view);
    // Ecrire la variable d'une commande : permission, parametres de la vue, journal.
    bool writeCommand(const View&, const Object&, const sim::Value&, double now, const std::string& shown = {},
                      bool journal = true);
    [[nodiscard]] bool numberProp(const Object&, std::string_view key, double& out);   // valeur ou expression
    void controlPress(const View&, const Object&, double now);
    void controlRelease(const View&, const Object&, double now, bool click);
    // Vrai : le clic est garde par la confirmation (pas d'actions cette fois).
    bool buttonConfirmHolds(const Object&, double now);
    void controlPart(const View&, const Object&, std::string_view part, double now);
    void controlsCycle(double now);
    void holdTick(double now);
    [[nodiscard]] const WeekSchedule& scheduleOf(const Object&, ControlState&);
    [[nodiscard]] std::optional<sim::Value> choiceValue(const Object&, const Choice&);
    // ---- lot 9 : les variables publiques (HmiRuntimePublic.cpp)
    bool sysValue(std::string_view name, sim::Value& out) const;
    bool inputSysValue(std::string_view name, sim::Value& out) const;   // 1.11.23 : SYS.Mouse*, SYS.Key*, SYS.Shortcut*
    bool keySysValue(std::string_view key, sim::Value& out) const;      // 1.11.23 : SYS.Key.<touche>
    bool commSysValue(std::string_view name, sim::Value& out) const;   // lot 14 : SYS.Comm*
    bool notifySysValue(std::string_view name, sim::Value& out) const; // lot 14 : SYS.Notify*
    void notice(const LiveAlarm&, std::string kind);                    // lot 14 : aux notifications
    [[nodiscard]] bool alarmUndecided(const AlarmDef&);                 // lot 14 : une variable pas encore lue
    void refreshBound();                                                // lot 15 : l'index des variables liees
    void readBound(const std::string& key, sim::Value& cache);          // la valeur lue par l'equipement
    int  writeBound(const std::string& key, const sim::Value& v, std::string* why);   // 1 ecrit, 0 refuse, -1 pas liee
    [[nodiscard]] comm::Quality boundQuality(const Variable&, std::string* why) const;
    bool equipSysValue(std::string_view name, sim::Value& out) const;   // SYS.Equip*
    std::map<std::string, Variable, std::less<>> bound_;               // cle : le nom en majuscules
    std::vector<Variable>                        boundSource_;         // les variables du projet quand l'index a ete fait
    std::vector<HmiType>                         boundTypes_;          // lot 16 : et les types IHM
    // Lot 16 : les structures et tableaux IHM (cle : le chemin en majuscules),
    // les indices hors des bornes deja dits au journal.
    std::map<std::string, types::Aggregate, std::less<>> aggregates_;
    std::map<std::string, sim::ObjRef, std::less<>>      rich_;        // 1.12.2 : les variables objets (cle : le nom en majuscules)
    std::set<std::string>                                boundsReported_;
    std::set<std::string, std::less<>>                   forcedIhm_;   // 1.11.5 : les variables IHM forcees (en majuscules)
    bool aggregateRead(const std::string& path, sim::Value& out);
    [[nodiscard]] bool outOfBoundsPath(const std::string& path, std::string* why) const;
    void reportBounds(const std::string& path, const std::string& why);
    bool aggregateAssign(const std::string& target, const std::string& source);
    // Lot 16 : les GIF animes - leur lecture, les durees de leurs images.
    std::map<Id, GifPlayback>                   gifs_;
    mutable std::map<const void*, GifTiming>    gifTimings_;
    [[nodiscard]] const GifTiming* gifTimingOf(const Object&) const;
    [[nodiscard]] const Object* openGifNamed(std::string_view name, const View** where = nullptr) const;
    void gifStart(const Object&, GifPlayback&, double now, int loops);
    void gifsOpenView(const View&, double now);
    void gifsCycle(double now);
    bool gifMemberValue(const Object&, std::string_view info, sim::Value& out) const;
    void reportsCycle(double now);                                      // lot 14 : les rapports a leur heure
    std::map<Id, std::pair<std::string, std::vector<std::string>>> alarmPaths_;   // la condition, ses chemins d'automate
    std::map<std::string, std::vector<std::string>, std::less<>>    exprPaths_;    // une expression, ses chemins d'automate
    // 1.9 : ce que les liaisons suivent en permanence (watchAlarms) : par source,
    // les chemins ; leur repartition (automate, equipements) refaite quand les
    // listes ou les variables liees changent ; remise aux liaisons deux fois par
    // seconde (une liaison refaite, un equipement bascule sur son esclave).
    void followWatched(double now, bool force);
    void unfollowAll();
    std::map<std::string, std::vector<std::string>, std::less<>>    alarmWatch_;
    std::vector<AlarmDef>                                           watchedProject_;
    std::vector<std::string>                                        watchedArchive_;
    std::vector<std::string>                                        followPlc_;
    std::map<std::string, std::vector<std::string>, std::less<>>    followEquip_;
    std::set<std::string>                                           followedEquip_;
    std::uint64_t                                                   watchRev_{0}, routedRev_{0}, boundRev_{0}, routedBoundRev_{0};
    double                                                          followNext_{-1};
    std::map<Id, double>                                            nextReport_;   // l'heure (epoch) du prochain rapport
    double                                                          lastReportCheck_{-1};
    std::string                                                     lastReport_;   // SYS.ReportLast
    bool viewMemberValue(const View& v, std::string_view member, sim::Value& out);
    bool objectMemberValue(const View& v, const Object& o, std::string_view key, std::string_view info, sim::Value& out);
    // 1.11.1 (decision 108) : Vue.Objet.Alarmes.<alarme>.<membre> - lire, ecrire (Acked, Shelved).
    bool alarmMemberValue(std::string_view alarm, std::string_view member, sim::Value& out) const;
    int  alarmMemberWrite(std::string_view path, std::string_view alarm, std::string_view member, const sim::Value& value,
                          std::string* why);
    [[nodiscard]] bool overridden(const View&) const;
    void applyOverrides(View& composed, const View& original) const;
    [[nodiscard]] bool viewOpen(Id view) const;
    std::map<std::pair<Id, Id>, std::map<std::string, std::string>> overrides_;   // (vue, objet) -> cle -> valeur
    std::map<Id, std::string> backgrounds_;          // vue -> fond ecrit en marche
    std::map<Id, long long>   openCounts_;           // vue -> ouvertures
    Id                        previousView_{kNoId};
    long long                 navigations_{0};
    long long                 cycles_{0};
    std::string               lastRecipe_, lastRecipeAt_;
    long long                 recipeApplies_{0};
    std::string               lastSound_;
    std::string               lastAlarm_, lastAlarmMessage_, lastAlarmAt_;
    int                       publicDepth_{0};      // une propriete qui se lit elle-meme
    std::string               publicWhy_;           // le dernier refus d'ecriture (le journal le dit)

    const Project*           project_{nullptr};
    sim::Environment*        plc_{nullptr};
    std::unique_ptr<Env>     env_;
    Hooks                    hooks_;
    bool                     running_{false};
    double                   startNow_{0}, lastCycle_{-1}, now_{0};
    long long                startWallMs_{0};
    Id                       current_{kNoId};
    std::vector<Id>          popups_;                    // les vues des popups (slots_, dans le meme ordre)
    std::vector<PopupSlot>   slots_;                     // lot 8
    std::shared_ptr<const Scope> currentScope_;          // lot 8 : les parametres de la vue courante
    std::map<Id, std::pair<double, double>> lastPopupPos_;   // lot 8 : "derniere" place, par vue
    std::map<Id, FormState>  forms_;                     // lot 8 : la saisie, par objet
    Id                       focused_{kNoId};
    double                   loginAt_{0};
    Id                       openerView_{kNoId};         // la vue de l'objet qui ouvre une popup
    std::optional<Animation> animation_;
    std::deque<JournalEntry> journal_;
    std::map<std::string, TriggerState> triggers_;       // "vue:objet:indice"
    std::map<Id, double>     scriptNext_;                // Cyclique : la prochaine fois
    std::map<Id, std::string> scriptWatch_;              // Changement : la derniere valeur
    std::map<Id, std::size_t> runs_;
    std::map<Id, std::string> errors_;
    std::string              source_;                    // qui execute (pour le journal)
    // 1.11.14 : le code qui tourne (la source d'une ligne de la Console) et sa ligne en
    // cours (l'interprete l'ecrit a chaque instruction : sim::RunLimits::trace).
    struct CodeOrigin {
        std::string name;                                   // "Horloge", "Vue_A.OnOpen", "fonction Moyenne"
        Id          view{kNoId}, object{kNoId}, script{kNoId}, function{kNoId};
    };
    CodeOrigin               origin_;
    sim::ExecTrace           trace_;
    int                      session_{0};
    std::optional<std::vector<simdata::Cell>> startData_;    // 1.11.15 : a rendre au prochain start()
    std::map<Id, simdata::Cell>               persistPending_;   // 1.11.18 (lot 5) : par declaration, avant la 1re execution
    std::string                               startLabel_;   // 1.11.16 : ce qu'en dit le journal
    std::optional<simdata::Report>            lastRestore_;  // ... et ce que le dernier en a fait
    std::string              abort_;                     // un appel imbrique coupe : la raison
    std::string              actionOrigin_;              // lot 6 : l'action de vue vient de ce modele
    Id                       pressed_{kNoId};
    double                   pressStart_{0};
    bool                     longFired_{false};
    int                      depth_{0};                  // appels et navigations imbriques
    // 1.9 : les copies des parametres (HmiRuntimeParams.cpp)
    params::PlcTypes         plcTypes_;
    int                      copySeq_{0};
    void captureCopies(PopupSlot&, const View&, std::string_view arguments);
    bool copyRead(std::string_view path, sim::Value& out) const;
    int  copyWrite(std::string_view path, const sim::Value& v, std::string* why);   // 1 ecrit, 0 refuse, -1 pas une copie
    // Lire / ecrire un chemin absolu, sans les parametres de la vue dont le code
    // tourne (la capture, l'application d'une copie).
    bool readDirect(const std::string& path, sim::Value& out);
    bool writeDirect(const std::string& path, const sim::Value& v, const std::string& source);
    int                      readOnly_{0};               // lot 7 : une fonction appelee par une expression
    // Un code ST pret a tourner : le programme (blocs de declaration otes) et
    // ses variables locales (lot 7). Par source ; les fonctions IHM a part.
    struct Prepared {
        std::shared_ptr<sim::Program> program;
        std::vector<LocalVar>         locals;
        std::string                   error;       // la premiere faute (declaration ou syntaxe)
    };
    const Prepared& prepare(const std::string& code, const std::string& source, bool function);
    void restorePersistent(Id script, sim::Locals& locals);            // 1.11.18 (lot 5)
    // Les cases de declaration de `cells` gardees pour leur script (les autres : rendues par l'appelant).
    std::vector<simdata::Cell> takePersistent(const std::vector<simdata::Cell>& cells);
    bool callFunction(const HmiFunction&, const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result,
                      std::vector<std::pair<std::string, sim::Value>>* outputs = nullptr);
    std::map<std::string, Prepared, std::less<>> programs_;   // par source
    // Les VAR gardees d'un script (lot 7) : "s<id>" ou, pour le code d'une action, "c<code>".
    std::map<std::string, std::map<std::string, sim::Value, std::less<>>> retained_;
    // 1.10 (dialecte IHM) : les variables d'un script, tenues par le simulateur
    // (simples et riches : tableaux, MAP, references...), gardees comme retained_.
    std::map<std::string, sim::Locals> scriptLocals_;
    std::set<std::string>    functionErrors_;            // une erreur de fonction dite une fois (expressions de vue)
    std::map<std::string, Expression, std::less<>> expressions_;
    // lot 4
    History*                         history_{nullptr};
    std::vector<LiveAlarm>           alarms_;
    // 1.9 : les alarmes des objets (HmiRuntimeObjectAlarms.cpp), et quand elles ont ete faites.
    struct ObjectAlarmTable;
    std::shared_ptr<ObjectAlarmTable> objectAlarms_;
    double                           objectAlarmsAt_{-1e9};
    std::uint64_t                    objectAlarmsGen_{0};   // +1 a chaque regeneration dont la liste cochee change
    std::uint64_t                    objectAlarmsWatched_{0};   // 1.9 (B + G) : la generation passee a watchAlarms("objets")
    void                             objectAlarmsCycle(double now);
    // Les alarmes generees (cochees ou non), vide sans table (HmiRuntimeObjectAlarms.cpp).
    [[nodiscard]] const std::vector<ObjectAlarm>& objectAlarmList() const noexcept;
    std::map<Id, double>             alarmPending_;     // condition vraie depuis (temporisation)
    std::set<Id>                     alarmBroken_;      // condition illisible : dite une fois
    std::deque<HistoryEvent>         events_;
    std::deque<AlarmOccurrence>      closed_;
    std::string                      user_;
    double                           lastActivity_{0};
    // ---- 1.11.23 : la souris, le clavier, les raccourcis (HmiRuntimeInput.cpp) ----
    struct HeldKey {
        keys::Chord                  chord;        // la touche et ses modificateurs a l'appui
        double                       since{0};
        Id                           owner{kNoId}; // la vue qui l'a prise (kNoId : aucune)
        std::shared_ptr<const View>  snapshot;     // cette vue a l'appui (le relachement apres sa fermeture)
        std::shared_ptr<const Scope> scope;        // ses parametres (une popup)
        std::set<std::size_t>        held;         // Touche maintenue : les actions deja parties
        std::map<std::size_t, double> next;        // Touche repetee : la prochaine fois
    };
    std::map<std::string, HeldKey, std::less<>> keysDown_;
    Pointer                          pointer_;
    bool                             keyCtrl_{false}, keyShift_{false}, keyAlt_{false};
    std::string                      keyLast_, keyLastKey_, shortcutLast_;   // "Ctrl+F5", "F5", "Vue_1 . F5"
    double                           keyLastSince_{-1};
    Id                               keyScopeView_{kNoId};   // le relachement d'une popup fermee : ses parametres
    std::shared_ptr<const Scope>     keyScope_;
    std::int64_t                     keyPresses_{0}, shortcutCount_{0};
    void resetInput();
    void keysTick(double now);
    bool submitFailed_{false};          // 1.11.23 : Valider les saisies refusee - la suite du geste ne part pas
    [[nodiscard]] bool clickSubmits(Id object) const;   // ... cet objet la porte (au clic)
    void runKeyActions(const View&, Trigger, const keys::Chord&, double now, const std::function<bool(std::size_t)>& pick = {});
    std::map<std::string, std::vector<TrendSeries>> trends_;   // "vue:objet"
    std::vector<TrendMarker>                        markers_;  // lot 18
    double                           nextSample_{-1};
    // lot 6 : les vues composees (modeles, en-tete, pied), refaites a chaque
    // entree du moteur (tick, clics) - jamais pendant un cycle : un script qui
    // navigue garde la vue qu'il est en train d'executer.
    mutable std::map<Id, View>       composed_;
    // 1.11.10 : les fonctions de symbole appelees ("Vue.Instance.Fonction", en majuscules),
    // pretes a tourner (nul : pas une fonction d'instance) ; refaites avec composed_.
    std::map<std::string, std::shared_ptr<const HmiFunction>> boundCalls_;
    [[nodiscard]] const HmiFunction* symbolCall(std::string_view call);
    std::map<Id, Id>                 recipeSelection_;  // gestionnaire -> jeu choisi
    std::map<Id, std::string>        tableSources_;     // tableau -> fichier externe (Lier un tableau)
    bool                             systemShown_{false};
    std::size_t                      soundsPlayed_{0};
    // lot 10 : le menu Parametres systeme
    SystemSettings                   settings_;
    int                              systemTab_{0};
    std::size_t                      systemScroll_{0};
    std::string                      systemMessage_;
    bool                             systemMessageError_{false};
    std::optional<DateTime>          systemClock_;
    // 1.9 : la page Simulation (HmiRuntimeSimPage.cpp)
    std::string                      simChosen_;              // l'equipement de l'esclave choisi
    std::size_t                      simScroll_{0};           // la premiere ligne montree
    std::size_t                      simListScroll_{SimPageShape::kAutoScroll};   // la premiere carte montree (automatique : la choisie)
    std::uint64_t                    simRevision_{0};
    bool                             simAfterLogin_{false};   // Se connecter... : la page revient apres la connexion
    bool                             simPagePart(std::string_view part, double now);
    void                             simChooseDefault(const std::vector<SimSlave>& slaves);
    void                             simPageOpened(const std::string& source);
    bool                             slaveSysValue(std::string_view name, sim::Value& out) const;   // SYS.Sim*, SYS.Slave.*
    bool                             sleeping_{false};  // la veille, au dernier tick (le journal le dit)
    bool                             sleepNow_{false};  // "Eteindre l'ecran" : jusqu'au prochain toucher
    bool                             playSound(const std::string& name, const std::string& source);
    void                             systemSay(std::string text, bool error);
    void                             sleepTick(double now);
    // lot 11 (HmiRuntimeLot11.cpp) : graphiques, alarmes, production, saisie, export
    std::map<std::string, std::vector<TrendSeries>> charts_;       // "vue:objet"
    std::map<std::string, std::string>              chartResets_;  // courbe XY : la derniere valeur de "reset"
    std::map<std::string, double>                   chartNext_;    // histogramme : la prochaine mesure
    std::vector<ShelvedAlarm>                       shelved_;
    std::string                                     selectedAlarm_, selectedZone_;
    bool                                            silenced_{false};
    double                                          nextAlarmSound_{-1};
    long long                                       soundCycle_{-1};   // le cycle du dernier son d'apparition...
    int                                             soundPriority_{0}; // ... et sa priorite (un seul son par cycle)
    std::set<std::string>                           soundMissing_;  // un son d'alarme introuvable : dit une fois
    std::map<Id, int>                               bannerSteps_;   // bandeau : les "suivante"
    struct ProductionState {
        bool   started{false};
        double shiftStart{0};
        int    shiftMinute{-1};
        std::string shift;
        double baseGood{0}, baseBad{0}, lastGood{0}, lastBad{0}, offsetGood{0}, offsetBad{0};
        double runSeconds{0}, lastTick{-1};
        bool   running{false};
        std::deque<std::pair<double, double>> totals;
        ProductionFigures figures;
    };
    std::map<Id, ProductionState>                   production_;
    std::map<Id, RecipeEditorState>                 editors_;
    std::string                                     lastExport_;
    long long                                       exports_{0};
    // Lot API 8 : dans l'action qu'un geste de l'operateur lance (un clic, un
    // double clic, un appui long) et le script qu'elle execute - IHM_EXPORTER y
    // demande ou ; 0 dans un script de vue, une action qui part seule (fire, runViewScript).
    int                                             exportAsk_{0};
    void sampleCharts(double now);
    void alarmsCycle(double now);                   // la fin des mises de cote, la repetition des sons
    void alarmAppeared(const LiveAlarm&, double now);
    void productionCycle(double now);
    void productionReset(const Object&, ProductionState&, double now, const std::string& why);
    void editorsCycle();
    RecipeEditorState& editorOf(const Object&);
    void lot11Part(const View&, const Object&, std::string_view part, double now);
    bool lot11Submit(const View&, const Object&, double now);   // Entree : tableau de variables, editeur de recette
    bool applyValues(const Recipe&, const std::vector<std::string>& values, const std::string& label, double now,
                     std::string* why);
    void resetLot11();
    // lot 12 (HmiRuntimeLot12.cpp) : l'historique, les objets de navigation et de structure
    std::vector<NavEntry>                           back_, forward_;
    std::string                                     currentArgs_;      // les parametres de la vue courante
    bool                                            historyMove_{false};   // Precedent / Suivant : ne pas empiler
    double                                          viewZoom_{100};
    void lot12Part(const View&, const Object&, std::string_view part, double now);
    void languagePart(const View&, const Object&, std::string_view part, double now);   // lot 13 : le selecteur de langue
    void commPart(const Object&, std::string_view part, double now);                     // lot 14 : le diagnostic automate
    bool navigateByUser(const View&, const Object*, Operation, const std::string& target, Transition, double now);
    void setOverride(const View&, const Object&, const std::string& key, const std::string& value);
    void resetLot12();
    // lot 12 (HmiRuntimeLogin.cpp) : le menu de connexion
    bool                                            loginShown_{false};
    LoginTab                                        loginTab_{LoginTab::Connexion};
    FormState                                       loginForm_;
    bool                                            loginKeyboard_{false};   // un champ touche : le clavier virtuel
    std::size_t                                     loginScroll_{0};
    std::optional<LoginTab>                         loginWanted_;          // demande a l'ouverture, pas encore visible
    void loginSay(std::string text, bool error, double now);
    void loginSubmit(double now);
    bool loginTypeKey(EditKey, double now);
    void loginTypeText(std::string_view, double now);
    [[nodiscard]] std::string loginKeyboardMode() const;
    void loginAccountsPart(std::string_view button, double now);
    void loginAccessPart(std::string_view part, double now);
    void loginAfterUserChange();                     // un onglet qui ne se voit plus : Connexion
    void resetLogin();
    // lot 13 (HmiRuntimeLot13.cpp) : l'audit, le verrouillage, le renouvellement, la
    // signature, l'avertissement de deconnexion, le badge
    std::vector<AuditEntry>                         auditLocal_;
    std::vector<AccountState>                       accountsLocal_;
    int                                             gesture_{0};         // un geste de l'operateur en cours
    std::string                                     gestureWhere_;
    int                                             auditMute_{0};       // une recette : une ligne pour tout
    std::string                                     signedBy_, signedReason_;   // pendant le geste signe
    bool                                            signatureBypass_{false};
    std::optional<SignatureRequest>                 signature_;
    std::optional<KeyboardPrompt>                   prompt_;          // 1.11.7 : le clavier virtuel d'une action
    FormState                                       promptForm_;
    void openPrompt(const View& v, const Object* o, const Action& a, const std::string& source);
    void promptTypeText(std::string_view text);
    void promptTypeKey(EditKey k, double now);
    void promptSubmit(double now);
    FormState                                       signForm_;
    bool                                            signKeyboard_{false};
    std::string                                     lastSignature_;
    PerfStats                                       perf_;              // lot 13 : les performances
    std::string                                     language_;          // lot 13 : SYS.Language
    std::size_t                                     languageChanges_{0};
    void resetDisplay();                                                // lot 13 : la langue, l'affichage du projet
    void applyProjectDisplay();                                         // ... l'affichage seul (Reglages par defaut)
    struct Renewal { std::string login, reason; };
    std::optional<Renewal>                          renewal_;
    std::string                                     loginVia_;
    bool                                            warned_{false};      // l'avertissement a ete dit au journal
    std::string                                     badgeBuffer_;
    double                                          badgeLast_{-1};
    struct GestureGuard {
        Runtime& rt;
        GestureGuard(Runtime& r, std::string where) : rt(r) { rt.beginGesture(std::move(where)); }
        ~GestureGuard() { rt.endGesture(); }
        GestureGuard(const GestureGuard&) = delete;
        GestureGuard& operator=(const GestureGuard&) = delete;
    };
    std::vector<AuditEntry>&         auditList();
    std::vector<AccountState>&       accountList();
    const std::vector<AccountState>& accountList() const;
    void beginGesture(std::string where);
    void endGesture();
    [[nodiscard]] bool auditingWrites() const noexcept;
    void auditWrite(const std::string& name, const sim::Value& before, const sim::Value& after);
    void audit(std::string kind, std::string source, std::string target, std::string before = {}, std::string after = {},
               std::string reason = {});
    [[nodiscard]] std::string loginFailed(const User&, double now);     // le texte a ajouter au refus
    void loginSucceeded(const User&);
    void completeLogin(const User&, double now, const std::string& how);
    [[nodiscard]] bool signatureNeeded(const View&, const Object&);
    [[nodiscard]] std::string signatureKeyboardMode() const;
    void requestSignature(const View&, const Object&, SignatureRequest::Gesture, double now, std::string part = {},
                          double fraction = 0, std::string text = {});
    void signatureSubmit(double now);
    void signatureTypeText(std::string_view, double now);
    void signatureTypeKey(EditKey, double now);
    void signatureCancel(double now, const std::string& why);
    void resumeSigned(const SignatureRequest&, double now);
    void renewalSubmit(double now);
    void badgeTyped(std::string_view text, double now);
    bool badgeEnter(double now);
    void resetLot13();
    // 1.9 : les bascules des equipements (le vrai <-> l'esclave simule) : au
    // journal, et une marque sur les courbes des variables liees.
    void slaveWatch(double now);
    std::map<std::string, std::pair<bool, bool>>    slaveReads_;        // equipement -> (lu sur l'esclave, par la bascule)
};

} // namespace hmi
