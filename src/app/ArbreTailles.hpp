// =============================================================================
//  app/ArbreTailles.hpp - 1.21.0 : le poids des fichiers de l'application
// -----------------------------------------------------------------------------
//  La fenetre A propos montre TOUS les fichiers de l'application en arbre, avec
//  leur poids et le total : le dossier du programme, les reglages de
//  l'utilisateur (l'Assistant IA, les rapports de plantage, les themes...), les
//  donnees (projets, bibliotheque, captures). Le parcours du disque est ici,
//  sans ecran, pour etre essaye (dossiers_test) :
//
//    - parcourir() : l'arbre des tailles d'un dossier. Les liens symboliques ne
//      sont pas suivis (ni comptes) ; ce qui ne se lit pas (droits, dossier
//      verrouille) est saute et compte ; un drapeau l'arrete au plus tot (la
//      fenetre fermee, Actualiser) ;
//    - retenir() : les racines qui existent, sans celles qui sont dans une
//      autre (un dossier n'est jamais compte deux fois) ;
//    - mesurer() : les deux, le tri et le total.
//
//  Le fil de fond qui l'appelle et la fenetre sont dans AboutDialog.cpp. Les
//  chemins sont en UTF-8, comme dans app/Dossiers.hpp.
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace app::tailles {

// Un fichier ou un dossier, et son poids.
struct Noeud {
    std::string        nom;                  // le nom (UTF-8) ; une racine : le nom de son dossier
    std::uintmax_t     octets = 0;           // un dossier : tout ce qu'il contient
    std::uint64_t      fichiers = 0;         // un dossier : ses fichiers, sous-dossiers compris ; un fichier : 1
    bool               dossier = false;
    // Au-dela de Options::maxNoeuds, les fichiers sont comptes (dans octets et
    // fichiers) sans etre listes : combien, et leur poids.
    std::uint64_t      nonDetailles = 0;
    std::uintmax_t     octetsNonDetailles = 0;
    std::vector<Noeud> enfants;
};

// Ce qui avance pendant le parcours : lu par un autre fil (la fenetre).
struct Progression {
    std::atomic<std::uint64_t> fichiers{0};
    std::atomic<std::uint64_t> octets{0};
    std::atomic<std::uint64_t> illisibles{0};
};

struct Options {
    const std::atomic<bool>* arret = nullptr;        // vrai : s'arreter au plus tot
    Progression*             progression = nullptr;  // facultatif
    std::size_t              maxNoeuds = 200000;     // les noeuds gardes en memoire (tout est compte)
    int                      profondeurMax = 64;     // plus profond : illisible (une boucle de jonctions)
};

struct Parcours {
    Noeud         racine;
    std::uint64_t illisibles = 0;   // dossiers et fichiers qui n'ont pas pu etre lus
    std::size_t   noeuds = 0;       // les noeuds gardes
    bool          annule = false;
};
// `dossier` et tout ce qu'il contient (lui-meme est suivi s'il est un lien).
[[nodiscard]] Parcours parcourir(const std::filesystem::path& dossier, const Options& o = {});

// Le tri des enfants : par poids decroissant (puis par nom), ou par nom (les
// dossiers d'abord, sans tenir compte de la casse).
enum class Tri : std::uint8_t { Poids, Nom };
[[nodiscard]] bool avant(const Noeud& a, const Noeud& b, Tri t);
void trier(Noeud& n, Tri t);   // tout l'arbre

// ------------------------------------------------------------ les racines ----
struct Racine {
    std::string libelle;   // ce que l'utilisateur lit ("Programme install\xC3\xA9")
    std::string chemin;    // UTF-8
};
struct Incluse {
    Racine                   racine;
    std::size_t              dans = 0;     // le rang de la racine gardee qui la contient
    std::vector<std::string> sousChemin;   // ses dossiers depuis cette racine (vide : la meme)
    // Apres mesurer() : son poids, lu dans l'arbre de sa racine (faux : pas trouvee).
    bool                     trouvee = false;
    std::uintmax_t           octets = 0;
    std::uint64_t            fichiers = 0;
};
struct Retenues {
    std::vector<Racine>  racines;    // celles qui existent, dans l'ordre donne, aucune dans une autre
    std::vector<Incluse> incluses;   // celles qui sont dans une racine gardee (ou la meme, plus loin)
};
// Les candidates qui ne sont pas des dossiers sont oubliees.
[[nodiscard]] Retenues retenir(const std::vector<Racine>& candidates);

struct Mesure {
    Racine racine;
    Noeud  arbre;
};
struct Bilan {
    std::vector<Mesure>  racines;
    std::vector<Incluse> incluses;
    std::uintmax_t       octets = 0;      // le poids total (chaque fichier une fois)
    std::uint64_t        fichiers = 0;
    std::uint64_t        illisibles = 0;
    bool                 annule = false;
};
// retenir(), parcourir() chaque racine (dans l'ordre), trier, totaliser.
// `debut` : appele avant chaque racine (la fenetre dit laquelle est en cours).
[[nodiscard]] Bilan mesurer(const std::vector<Racine>& candidates, const Options& o = {}, Tri tri = Tri::Poids,
                            const std::function<void(std::size_t, const Racine&)>& debut = {});

// --------------------------------------------------------------- le texte ----
// 12 345 (une espace fine insecable entre les milliers).
[[nodiscard]] std::string milliers(std::uint64_t n);
// "1 fichier", "12 345 fichiers".
[[nodiscard]] std::string nombreDeFichiers(std::uint64_t n);
// Le resume a copier : `entete`, le total, chaque racine (poids, fichiers,
// chemin) et ce qu'elle contient de connu, les elements illisibles.
[[nodiscard]] std::string resume(const Bilan& b, std::string_view entete);

} // namespace app::tailles
