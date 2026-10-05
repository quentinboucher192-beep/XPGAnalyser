// =============================================================================
//  project/MacroSpec.hpp - ce qu'une macro demande, dit dans son en-tete
//  (lot macros 1)
// -----------------------------------------------------------------------------
//  UNE MACRO POSE SES QUESTIONS PAR Ask(), ET C'EST TOUJOURS VRAI. Ce fichier
//  n'y change rien : il lit, dans le bloc d'en-tete "(* ... *)" que
//  l'interpreteur saute deja, des lignes "#!" qui disent COMMENT poser chaque
//  question. Une macro sans ces lignes tourne exactement comme avant ; elle
//  gagne seulement un formulaire plus pauvre.
//
//      #! categorie = Importer depuis le classeur
//      #! champ classeur = fichier .xlsx .xlsm facultatif
//      #! libelle classeur = Le classeur de l'affaire
//      #! champ tache = tache
//      #! champ sevEq = nombre 1..4 : 1 Information, 2 Avertissement, 3 Defaut, 4 Arret
//      #! champ modeRepli = choix-classeur Modes, Mode, Nom : -1 aucun
//      #! groupe Alarmes = alarmes, sevEq, liste
//      #! avance = prefSection, forcePrio
//      #! exemple prefixe = {}Pompes, {}Vannes, {}Moteurs
//      #! tableau es = Le tableau des E/S (CSV exporte d'un onglet)
//      #! lit = Config, Cartes API, ES
//      #! lit-facultatif = Reglages, Modes
//      #! produit = 20 sections, rangees dans l'ordre de la tache
//      #! appliquer = Reecrire {sInit} et {sCycle} entierement
//
//  LES LIBELLES S'ECRIVENT ICI, AVEC ACCENTS ET APOSTROPHES. Dans le code, un
//  libelle est un litteral ST : une apostrophe s'y ecrit $', et 49 questions
//  sur 154 avaient fini par s'ecrire "Tache d accueil". Le texte du Ask reste
//  le repli quand la ligne manque.
//
//  UNE CLE PEUT ETRE UN MOTIF : "mode*nom" couvre mode0nom, mode1nom... - les
//  questions qu'une boucle pose avec une cle calculee. Dans un libelle, {}
//  est remplace par ce que l'etoile a couvert.
//
//  CE FICHIER NE DEPEND QUE DE LA BIBLIOTHEQUE STANDARD : il se verifie sans
//  projet, sans ecran et sans disque (macrospec_test).
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace project::macro {

// Le genre d'un champ : il choisit le widget du formulaire.
enum class FieldKind : std::uint8_t {
    Text,          // texte           un champ libre
    Name,          // nom             un identifiant IEC, verifie, avec son exemple
    File,          // fichier         a lire : ..., Ctrl+V, glisser, recents
    OutputFile,    // fichier-sortie  a ecrire a cote du classeur
    Task,          // tache           les taches du projet
    Section,       // section         les sections du projet
    Unit,          // unite           les unites de programme
    Subroutine,    // sous-routine    les sous-routines
    Dfb,           // dfb             les blocs du projet et de la bibliotheque
    Ddt,           // ddt             les types derives du projet et de la bibliotheque
    Type,          // type            tout type : elementaire, DDT, DFB
    Variable,      // variable        les variables globales
    LibraryItem,   // bibliotheque    un element de libs/
    Macro,         // macro           une macro de la bibliotheque
    Sheet,         // onglet          un onglet du classeur choisi
    SheetChoice,   // choix-classeur  une valeur lue dans un onglet du classeur
    YesNo,         // oui-non         un interrupteur (O / N)
    Choice,        // choix           des boutons, ou une liste s'ils sont nombreux
    Number,        // nombre          borne ; a libelles, des boutons
    List,          // liste           une ligne par element, jointes par des virgules
    Checks,        // cases           des cases a cocher (retard, onglets, options)
};

[[nodiscard]] std::string_view kindKey(FieldKind) noexcept;          // "tache"
[[nodiscard]] std::optional<FieldKind> kindFromKey(std::string_view) noexcept;   // accents toleres
// Les genres qui designent un objet du projet ou de la bibliotheque (une liste
// a proposer pendant la frappe).
[[nodiscard]] bool isPicker(FieldKind) noexcept;

struct FieldOption {
    std::string value;
    std::string label;
};

struct FieldSpec {
    std::string key;                    // "classeur", ou un motif "mode*nom"
    FieldKind   kind{FieldKind::Text};
    bool        declared{false};        // une ligne "#! champ" existe
    std::string label;                  // "#! libelle", sinon le texte du Ask
    std::string help;                   // "#! param"
    std::string group;                  // "#! groupe"
    bool        advanced{false};        // "#! avance"
    bool        optional{false};        // facultatif : vide permis
    bool        allowNew{false};        // nouvelle / nouveau : un nom qui n'existe pas encore
    bool        mustExist{false};       // existant : un nom inconnu est une erreur
    bool        emptyMeansAll{false};   // cases : tout coche = reponse vide
    std::vector<std::string> extensions;   // ".xlsx", ".xlsm"
    std::vector<FieldOption> options;      // les libelles des choix
    bool        hasRange{false};
    double      minimum{0.0}, maximum{0.0};
    std::string sheet, valueColumn, labelColumn;   // choix-classeur
    std::string source;                 // cases : "retard", "onglets", "options"
    std::string membersOf;              // liste membres-de <cle>
    std::string example;                // "{}Pompes, {}Vannes"
    // Ce que dit l'appel Ask (lu dans le code, pas execute).
    std::string askPrompt, askPreset, askKind;

    [[nodiscard]] bool isPattern() const noexcept { return key.find('*') != std::string::npos; }
    // Le libelle d'une option ("3" -> "Defaut"), vide si aucune.
    [[nodiscard]] std::string optionLabel(std::string_view value) const;
};

struct GroupSpec {
    std::string name;
    std::vector<std::string> keys;
};

struct MacroSpec {
    std::string name;
    std::string summary;
    std::string version;
    std::string category;               // "Importer depuis le classeur/Etapes"
    std::vector<FieldSpec> fields;      // les champs declares puis ceux des Ask sans ligne
    std::vector<GroupSpec> groups;      // dans l'ordre des lignes
    std::vector<std::string> advanced;
    std::vector<std::string> reads;     // les onglets lus (requis)
    std::vector<std::string> readsOptional;
    bool        readsDeclared{false};   // "#! lit" present (sinon : deduit du code)
    std::vector<std::string> produces;
    std::string applyText;
    std::vector<std::string> launches;  // RunMacro('X'), dans l'ordre du code
    struct Table { std::string name, label; };
    std::vector<Table> tables;          // "#! tableau", et les OpenTable du code
    std::vector<std::string> problems;  // ce qui ne va pas dans les lignes #!

    // Lot API 2 : CE QUE LA MACRO TOUCHE, lu dans son code et dans le genre de
    // ses champs - rien a declarer. Les sept pastilles de l'onglet Macros.
    struct Touches {
        bool workbook{false};   // OpenWorkbook, OpenSheet, ou un champ fichier .xlsx / .xlsm
        bool csv{false};        // un tableau CSV : #! tableau, OpenTable, un champ fichier .csv
        bool chains{false};     // RunMacro : elle enchaine d'autres macros
        bool library{false};    // LibImport, LibUpdate : elle importe de la bibliotheque
        bool sections{false};   // AddSection, AppendToSection, ClearSection, ReplaceInSection,
                                // AddProgramUnit, AddSubroutine
        bool variables{false};  // AddVariable
        bool fileOut{false};    // FileWrite, FileAppend, un champ fichier-sortie : un fichier a cote
        bool order{false};      // PlaceSection : elle range l'ordre d'execution
        [[nodiscard]] bool modifiesProject() const noexcept { return library || sections || variables || order; }
    };
    Touches touches;

    // Le champ d'une cle : exacte d'abord, puis un motif ; nul sinon.
    [[nodiscard]] const FieldSpec* field(std::string_view key) const;
    // Ce que l'etoile d'un motif couvre ("mode*nom", "mode3nom" -> "3").
    [[nodiscard]] static std::optional<std::string> matchPattern(std::string_view pattern, std::string_view key);
    [[nodiscard]] std::string groupOf(std::string_view key) const;
    [[nodiscard]] bool isAdvanced(std::string_view key) const;
};

// Lit le bloc d'en-tete et les appels du code. Ne leve jamais : une ligne mal
// formee devient un element de `problems`.
[[nodiscard]] MacroSpec parseMacroSpec(std::string_view source, std::string_view name = {});

// "{}Pompes, {}Vannes" et "EQ_" -> "EQ_Pompes, EQ_Vannes". Vide sans exemple.
[[nodiscard]] std::string exampleFor(const FieldSpec&, std::string_view answer);

// "Reecrire {sInit} et {sCycle}" avec les reponses. Sans "#! appliquer" :
// `fallback` (le texte du Confirm de la macro, s'il y en a un).
[[nodiscard]] std::string applyTextFor(const MacroSpec&, const std::map<std::string, std::string>& answers,
                                       std::string_view fallback);

// Le libelle d'un champ, motif resolu ("Mode {} : son nom" -> "Mode 3 : son nom").
[[nodiscard]] std::string labelFor(const FieldSpec&, std::string_view key, std::string_view runtimePrompt);

// Ce que dit un champ en quelques mots, pour la liste "ce qu'elle va te
// demander" : "fichier .xlsx .xlsm", "tache du projet", "1 a 4, a libelles".
[[nodiscard]] std::string describe(const FieldSpec&);

// ---- verifier une reponse ----------------------------------------------------
enum class Verdict : std::uint8_t { Ok, Info, Warning, Error };
struct Check {
    Verdict     verdict{Verdict::Ok};
    std::string message;
};
// Un identifiant IEC 61131-3 : une lettre ou _, puis lettres, chiffres, _ ; pas
// deux _ de suite ; 32 caracteres au plus.
[[nodiscard]] bool isIdentifier(std::string_view) noexcept;
[[nodiscard]] Check checkName(std::string_view text, bool optional);
[[nodiscard]] Check checkNumber(std::string_view text, double minimum, double maximum);
// Le nom de fichier d'une macro : un identifiant, qui ne commence pas par _
// (les dossiers _corbeille et _modeles sont a elle).
[[nodiscard]] Check checkMacroName(std::string_view name);

// Les accents retires (e accent aigu -> e), pour comparer des mots tapes.
[[nodiscard]] std::string foldAccents(std::string_view utf8);

} // namespace project::macro
