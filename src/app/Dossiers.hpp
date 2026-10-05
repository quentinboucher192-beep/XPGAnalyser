// =============================================================================
//  app/Dossiers.hpp - 1.8.0 : les dossiers de l'application
// -----------------------------------------------------------------------------
//  L'application range tout dans son DOSSIER DE TRAVAIL : projets, libs
//  (la bibliotheque), resources (le catalogue, les blocs Schneider),
//  captures. Installee dans Program Files, elle ne pourrait pas y ecrire ;
//  et le cahier des charges veut les donnees hors du dossier du programme.
//
//  LA REGLE, POUR NE RIEN CHANGER AILLEURS :
//
//    - un fichier installation.ini A COTE DE L'EXE (ecrit par l'installateur)
//      dit que l'application est installee et donne ses valeurs par defaut ;
//    - %APPDATA%\XpgAnalyzer\XPGAnalyser.ini (celui de l'utilisateur) les
//      remplace, cle par cle ; l'accueil (Dossiers) et le menu Projet
//      (Dossiers de l'application...) le lisent et l'ecrivent ;
//    - au demarrage, le DOSSIER DES DONNEES devient le dossier de travail :
//      tout le code qui dit "projets", "libs", "resources", "captures" en
//      relatif continue de marcher, sans une ligne changee ;
//    - projets, bibliotheque et captures peuvent aller ailleurs (un partage
//      reseau pour la bibliotheque, par exemple) : App::projectsRoot(),
//      SharedLibrary::defaultRoot() et la capture F12 demandent actif().
//
//  SANS installation.ini (la version de developpement, la version portable,
//  les sessions rejouees), RIEN ne change : les dossiers suivent le dossier
//  de travail, comme avant, et XPGAnalyser.ini est ignore.
//
//  Le format .ini : UTF-8 (avec ou sans BOM), [Section], cle = valeur, les
//  lignes ; et # sont des commentaires (seulement en debut de ligne : un
//  chemin Windows peut contenir un ;). Une valeur peut commencer par
//  {Documents}, {AppData}, {LocalAppData}, {ProgramData}, {Programme} (le
//  dossier de l'exe) ou {Donnees}, et contenir des %VARIABLES%. Un chemin
//  relatif part du dossier des donnees (du dossier du programme pour
//  "donnees" lui-meme : une cle USB).
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::dossiers {

// Les dossiers que l'utilisateur peut changer, dans l'ordre de la fenetre.
enum class Cle : std::uint8_t { Donnees = 0, Projets, Bibliotheque, Captures };
inline constexpr std::size_t kNombreCles = 4;
inline constexpr std::array<Cle, kNombreCles> kCles{Cle::Donnees, Cle::Projets, Cle::Bibliotheque, Cle::Captures};

[[nodiscard]] std::string_view nomCle(Cle c) noexcept;        // "donnees", "projets", "bibliotheque", "captures"
[[nodiscard]] std::string_view libelle(Cle c) noexcept;       // "Donnees", "Projets"... (UTF-8)
[[nodiscard]] std::string_view sousDossier(Cle c) noexcept;   // "", "projets", "libs", "captures"

// La valeur par defaut du dossier des donnees d'une installation.
inline constexpr std::string_view kDonneesParDefaut = "{Documents}\\XPGAnalyser";
// Le fichier de l'utilisateur (dans le dossier des reglages) et celui de l'installation.
inline constexpr std::string_view kNomIniUtilisateur = "XPGAnalyser.ini";
inline constexpr std::string_view kNomIniInstallation = "installation.ini";

// ---------------------------------------------------------------- le .ini ----
class Ini {
public:
    [[nodiscard]] static Ini depuisTexte(std::string_view texte);
    // Le texte a ecrire : les lignes d'origine (commentaires compris), jointes
    // par \r\n, sans BOM (ecrire() l'ajoute).
    [[nodiscard]] std::string texte() const;
    // Absente : nullopt ; presente et vide : "".
    [[nodiscard]] std::optional<std::string> valeur(std::string_view section, std::string_view cle) const;
    // Remplace la ligne de la cle, ou l'ajoute a la fin de la section (creee si besoin).
    void poser(std::string_view section, std::string_view cle, std::string_view valeur);
    [[nodiscard]] bool vide() const noexcept { return lignes_.empty(); }
    [[nodiscard]] const std::vector<std::string>& lignes() const noexcept { return lignes_; }

private:
    std::vector<std::string> lignes_;
};

[[nodiscard]] std::optional<Ini> lireIni(const std::filesystem::path& fichier);
// Ecrit en UTF-8 avec BOM (le Bloc-notes et PowerShell 5.1 le lisent sans hesiter),
// par un fichier temporaire renomme ensuite.
bool ecrireIni(const std::filesystem::path& fichier, const Ini& ini, std::string* pourquoi = nullptr);
// Le modele de XPGAnalyser.ini (toutes les cles vides : les valeurs par defaut).
[[nodiscard]] std::string modeleIniUtilisateur();

// ------------------------------------------------------------- les chemins ----
// Les chemins sont en UTF-8 dans tout le programme ; std::filesystem::path(std::string)
// les lirait dans la page de code du systeme sous Windows.
[[nodiscard]] std::filesystem::path cheminDe(std::string_view utf8);
[[nodiscard]] std::string utf8De(const std::filesystem::path& p);

struct Contexte {
    std::string documents, appData, localAppData, programData, programme, donnees;
    // %NOM% : la variable d'environnement, nullopt si elle n'existe pas (le texte reste).
    std::function<std::optional<std::string>(std::string_view)> variable;
};
[[nodiscard]] Contexte contexteSysteme(const std::filesystem::path& dossierExe);
// Les jetons {..} et les %VARIABLES% remplaces ; rien d'autre.
[[nodiscard]] std::string developper(std::string_view brut, const Contexte& c);
// developper(), puis absolu (un relatif part de `base`), normalise, sans separateur final.
[[nodiscard]] std::string absolu(std::string_view brut, const Contexte& c, std::string_view base);

// ------------------------------------------------------------ la resolution ----
enum class Source : std::uint8_t { Utilisateur, Installation, ParDefaut, Developpement };
[[nodiscard]] std::string_view texteSource(Source s) noexcept;

struct Dossier {
    Cle         cle{Cle::Donnees};
    std::string chemin;     // absolu, UTF-8
    std::string brut;       // la valeur ecrite (vide : le defaut)
    Source      source{Source::ParDefaut};
};

struct Etat {
    bool installe = false;                    // installation.ini a cote de l'exe
    std::array<Dossier, kNombreCles> dossiers{};
    std::string iniUtilisateur;               // son chemin, qu'il existe ou non
    std::string iniInstallation;              // vide : pas d'installation.ini
    std::string reglages;                     // %APPDATA%\XpgAnalyzer
    std::string programme;                    // le dossier de l'exe
    std::string journaux, sauvegardes;        // [Maintenance] de installation.ini (vide : inconnus)
    std::string portee, versionInstallee;     // [Installation] de installation.ini
    bool epinglerBarre = false;               // [Installation] epingler_barre_taches = oui
    [[nodiscard]] const Dossier& operator[](Cle c) const noexcept { return dossiers[static_cast<std::size_t>(c)]; }
};

// `dossierTravail` : celui de la version de developpement (dossier de travail
// du moment) ; `reglages` : le dossier de settings.txt.
[[nodiscard]] Etat resoudre(const Ini* installation, const Ini* utilisateur, const Contexte& c,
                            std::string_view dossierTravail, std::string_view reglages);

// -------------------------------------------------------------- le demarrage ----
struct Demarrage {
    bool installe = false;
    std::string donnees;                  // le dossier de travail choisi (installe)
    std::vector<std::string> messages;    // ce qui n'a pas pu se faire comme prevu
};
// Lit installation.ini et XPGAnalyser.ini, cree et amorce le dossier des donnees
// (resources\ et libs\ livres, copies sans rien ecraser), s'y place. Sans
// installation.ini : ne fait rien (installe = false).
Demarrage preparer(const std::filesystem::path& dossierExe, std::string_view reglages);

// L'etat retenu au demarrage (et apres un changement). Avant preparer() : la
// version de developpement, dossier de travail du moment.
[[nodiscard]] const Etat& actuel();
// Le chemin a utiliser pour ce dossier, "" : le comportement d'avant (le
// dossier de travail) - toujours "" dans la version de developpement.
[[nodiscard]] std::string actif(Cle c);
// Les messages du demarrage (l'accueil les montre une fois).
[[nodiscard]] std::vector<std::string> prendreMessages();
// Pour les tests : poser l'etat sans rien lire.
void poserEtat(Etat e);

// --------------------------------------------------------------- changer ----
struct Changement {
    Cle         cle{Cle::Donnees};
    std::string brut;          // ce que l'utilisateur a ecrit (vide : le defaut)
    bool        copier{false}; // copier le contenu de l'ancien dossier dans le nouveau
};
struct Bilan {
    bool ok = true;
    bool redemarrer = false;              // donnees ou bibliotheque : au prochain demarrage
    bool ecrit = false;                   // XPGAnalyser.ini a ete ecrit
    std::vector<std::string> lignes;      // ce qui a ete fait, pour l'utilisateur (UTF-8)
};
// Verifie chaque nouveau dossier (cree, inscriptible), copie si demande (sans
// ecraser, l'ancien reste), ecrit XPGAnalyser.ini (l'ancien garde en
// XPGAnalyser.ini.precedent), puis applique projets et captures tout de suite.
Bilan changer(const std::vector<Changement>& changements, const Contexte& c);

// ------------------------------------------------------------ utilitaires ----
bool dossierInscriptible(const std::filesystem::path& p, std::string* pourquoi);
struct Copie {
    std::size_t    fichiers = 0, gardes = 0, erreurs = 0;
    std::uintmax_t octets = 0;
    std::string    premiereErreur;
};
// Copie l'arbre `de` dans `vers` SANS ECRASER : un fichier deja la est garde.
Copie copierContenu(const std::filesystem::path& de, const std::filesystem::path& vers);
// `enfant` est `parent` ou dedans (sans tenir compte de la casse sous Windows).
[[nodiscard]] bool contient(const std::filesystem::path& parent, const std::filesystem::path& enfant);
// Le nombre de fichiers d'un dossier (arrete a `limite`), et leur taille.
std::size_t compterFichiers(const std::filesystem::path& p, std::uintmax_t* octets, std::size_t limite);
// L'Explorateur (Windows) ou xdg-open ; vide : ouvert.
[[nodiscard]] std::string ouvrirDansLeSysteme(const std::string& cheminUtf8);
[[nodiscard]] std::string tailleLisible(std::uintmax_t octets);

} // namespace app::dossiers
