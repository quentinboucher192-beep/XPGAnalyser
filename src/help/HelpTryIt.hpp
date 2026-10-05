// =============================================================================
//  help/HelpTryIt.hpp — le bouton Essayer : l'exemple qui tourne pour de vrai
// -----------------------------------------------------------------------------
//  L'AIDE MONTRAIT UN BOUT DE ST ET IL FALLAIT LA CROIRE SUR PAROLE.
//
//  Ce module monte un projet de poche autour de l'exemple d'un bloc - le DDT,
//  le DFB, une instance, une section contenant l'exemple - et le fait tourner
//  dans le vrai simulateur. Le lecteur voit la valeur de chaque variable a cote
//  de chaque ligne, force une entree, relance, et comprend le bloc en trente
//  secondes.
//
//  C'EST AUSSI UN TEST DE L'AIDE, et c'est peut-etre ce qui compte le plus.
//  `tryAll` lance les exemples de toute la bibliotheque et nomme ceux qui ne
//  compilent plus. Un exemple faux se signale le jour ou il devient faux, au
//  lieu de rester la deux ans.
//
//  CE QU'IL N'EST PAS : un banc d'essai. Le projet de poche ne contient que ce
//  que l'exemple touche, il ne tourne qu'a la demande, et il est jete en
//  sortant de la page. Rien de ce qui s'y passe n'atteint le projet ouvert -
//  c'est la propriete qui permet d'appuyer sur Essayer sans y reflechir.
//
//  LES DECLARATIONS SONT DEDUITES DE L'EXEMPLE. Un exemple ne declare rien : il
//  ecrit `IO_Pompes(Count := 2, Eq := Pompes);`. On retrouve donc les types par
//  les parametres du bloc appele - `Eq` est un `ARRAY[0..15] OF ST_EQ_Pump`,
//  donc `Pompes` l'est aussi. Ce qui reste sans type est declare en BOOL et
//  SIGNALE : une hypothese muette serait pire qu'un exemple qui ne tourne pas.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/LibraryCatalog.hpp"
#include "../sim/Runtime.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace help {

// Ce qu'une variable vaut apres le cycle, telle qu'elle s'affiche a cote de
// l'exemple.
struct Observed {
    std::string name;
    std::string value;
    bool        forced{false};
    // Le type de la VALEUR, tel que le simulateur le tient : "BOOL", "INT"...
    // Sans lui, un INT qui vaut 0 se prenait pour un booleen - la table
    // proposait de « basculer » un compteur.
    std::string type{};
};

// Une ligne de l'exemple, et ce que le cycle en a fait.
struct TryLine {
    std::string              text;       // la ligne, telle qu'ecrite
    std::vector<Observed>    values;     // les variables qu'elle nomme
};

struct TryReport {
    bool                     ran{false};
    std::string              failure;      // vide si tout s'est bien passe
    std::vector<std::string> notes;        // ce qui a ete suppose, et pourquoi
    std::vector<std::string> declared;     // "Pompes : ARRAY[0..15] OF ST_EQ_Pump"
    std::vector<TryLine>     lines;
    std::vector<Observed>    all;          // toutes les variables, pour un tableau
    std::uint32_t            scans{0};
};

// Une session : le projet de poche et son simulateur, gardes vivants pour que
// forcer et relancer aient un sens. La detruire jette tout.
class TrySession {
public:
    // `library` sert a retrouver les DDT dont le bloc depend. Passer la
    // bibliotheque entiere est volontaire : un bloc d'equipement cite un DDT
    // qui cite un autre DDT, et resoudre a la main cette chaine au moment de
    // l'appel serait la refaire a chaque fois.
    [[nodiscard]] static core::Result<std::shared_ptr<TrySession>> build(
        const project::CatalogEntry& entry,
        const std::vector<project::CatalogEntry>& library);

    // UN PROGRAMME ECRIT POUR L'OCCASION, et non l'exemple d'une entree. C'est ce
    // qui sert aux essais de comportement des blocs : un scenario pose ses
    // variables, appelle le bloc, et verifie ce qu'il calcule - l'exemple, lui,
    // montre et ne prouve rien.
    //
    // `declarations` : une variable par ligne, « Nom : TYPE », le point-virgule
    // final tolere, les lignes vides et les commentaires (* *) ignores. Les
    // DDT et DFB cites sont pris dans la bibliotheque, avec ce dont ils
    // dependent. Un type inconnu est une ERREUR : un essai qui declarerait en
    // BOOL ce qu'il ne connait pas prouverait autre chose que ce qu'il annonce.
    [[nodiscard]] static core::Result<std::shared_ptr<TrySession>> buildProgram(
        std::string_view declarations, std::string_view program,
        const std::vector<project::CatalogEntry>& library);

    // Ecrire une valeur SANS la forcer : le programme peut la changer au cycle
    // suivant. C'est la difference entre poser une consigne et bloquer un
    // capteur. Rend false quand le nom est inconnu.
    bool write(const std::string& name, const std::string& value);

    // La valeur telle qu'elle s'affiche ("TRUE", "42", "1.5"), vide si le nom
    // est inconnu.
    [[nodiscard]] std::string read(const std::string& name) const;

    // Un cycle de plus. Rend le releve complet.
    TryReport step(std::int64_t deltaMs = 20);

    // UN CYCLE SANS RELEVE. `step` releve chaque variable et ce que chaque ligne
    // touche : c'est ce que l'aide affiche, et sur un projet de 2000 noms c'est
    // cent fois plus long que le cycle lui-meme. Les essais longs - des
    // milliers de cycles, un moteur GRAFCET secoue au hasard - n'en ont pas
    // besoin. Rend le premier message d'erreur du cycle, vide si tout va bien.
    std::string advance(std::int64_t deltaMs = 20);
    // Les instructions executees par le dernier `advance` : le cout d'un bloc,
    // pour comparer deux versions.
    [[nodiscard]] std::uint64_t lastStatements() const noexcept { return lastStatements_; }

    // LIRE SANS EXECUTER. `step(0)` n'est pas une lecture : le cycle tourne
    // quand meme, simplement sans que le temps avance - les temporisations ne
    // bougent pas, mais la logique, elle, s'execute une fois de plus. Rafraichir
    // un tableau ne doit pas faire avancer la machine qu'il observe.
    [[nodiscard]] TryReport observe() const;

    // Forcer une entree, puis relancer : c'est ce qui fait comprendre un bloc.
    // Rend false quand le nom est inconnu - une faute de frappe ne doit pas
    // passer pour un forcage sans effet.
    bool force(const std::string& name, const std::string& value);
    bool unforce(const std::string& name);
    void reset();

    [[nodiscard]] const std::vector<std::string>& variableNames() const noexcept {
        return names_;
    }
    [[nodiscard]] const std::vector<std::string>& notes() const noexcept { return notes_; }
    // Ce que la preparation a REFUSE : un corps de bloc qui ne se lit pas, un
    // type introuvable. Le simulateur saute alors le bloc sans s'arreter, et
    // l'essai paraissait passer sur un bloc qui ne tournait pas.
    [[nodiscard]] const std::vector<std::string>& preparationErrors() const noexcept {
        return prepErrors_;
    }
    [[nodiscard]] const std::vector<std::string>& declared() const noexcept { return declared_; }
    [[nodiscard]] const std::string& example() const noexcept { return example_; }

private:
    void fillReport(TryReport&) const;

    std::shared_ptr<domain::Project>       project_;
    std::unique_ptr<sim::Runtime>          runtime_;
    std::string                            example_;
    std::vector<std::string>               lines_;
    std::vector<std::string>               names_;
    std::vector<std::string>               notes_;
    std::vector<std::string>               prepErrors_;
    std::vector<std::string>               declared_;
    std::uint64_t                          lastStatements_{0};
};

// ---- la passe sur toute la bibliotheque -------------------------------------
struct TryAllRow {
    std::string name;
    std::string category;
    bool        hasExample{false};
    bool        ok{false};
    std::string failure;
    // Les assertions « #! expect » de l'entree : combien, combien tenues.
    // Un exemple sans assertion prouve seulement qu'il tourne ; avec, il
    // prouve ce qu'il calcule.
    std::size_t expectations{0};
    std::size_t expectationsOk{0};
};

struct TryAllReport {
    std::vector<TryAllRow> rows;
    std::size_t            withExample{0};
    std::size_t            ok{0};
    [[nodiscard]] std::size_t broken() const noexcept { return withExample - ok; }
};

// Lance tous les exemples. C'est long a l'echelle d'un clic - une soixantaine
// de projets de poche - donc c'est une commande, pas quelque chose qui tourne
// a l'ouverture de la page.
//
// LES ASSERTIONS DE L'EXEMPLE. Une entree peut porter :
//    #! given  = Pompes[0].Fbk := TRUE          pose avant le premier cycle
//    #! expect = 3 : Pompes[0].Running = TRUE   ce que vaut X apres 3 cycles
// Le given est POSE une fois, pas force : le programme peut le changer
// ensuite, comme il changerait la variable dans l'automate.
// Les cycles durent 20 ms. Une assertion fausse rend l'exemple CASSE, avec
// ce qui etait attendu et ce qui a ete lu : c'est ainsi qu'un bloc corrige
// qui change de comportement se signale tout seul.
[[nodiscard]] TryAllReport tryAll(const std::vector<project::CatalogEntry>& library);

// La meme chose pour une seule entree.
[[nodiscard]] TryAllRow tryOne(const project::CatalogEntry& entry,
                               const std::vector<project::CatalogEntry>& library);

// Le corps ST d'un fichier de bibliotheque : tout ce qui suit "<<<section ...>>>".
// Expose parce que le catalogue ne le garde pas - il decrit l'interface - et
// que c'est ce qu'il faut pour que le bloc CALCULE quelque chose.
[[nodiscard]] std::string bodyOfLibraryFile(std::string_view contents);

// Toutes les sections d'un fichier de bibliotheque, dans l'ordre : (nom, corps).
// Un DFB peut en avoir plusieurs ; bodyOfLibraryFile ne rend que la premiere.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
sectionsOfLibraryFile(std::string_view contents);

} // namespace help
