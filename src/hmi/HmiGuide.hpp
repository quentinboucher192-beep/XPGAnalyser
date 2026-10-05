// =============================================================================
//  hmi/HmiGuide.hpp - le guide de l'IHM : l'aide F1 et le guide Word / PDF
// -----------------------------------------------------------------------------
//  UNE SEULE SOURCE. Le texte vit dans tools/guide-ihm/guide-ihm.txt ; le
//  script generer_guide.py en tire HmiGuideText.cpp (ce qui est compile ici)
//  et le guide Word illustre. L'aide de l'application et le document livre
//  disent donc toujours la meme chose.
//
//  CE MODULE NE DESSINE RIEN : des sujets (titre, chapitre, resume, blocs,
//  renvois), la recherche, et ce que F1 doit ouvrir selon l'endroit ou l'on
//  est (un volet) ou le nom sous le curseur (IHM_NAVIGUER, VAR_TEMP...). Le
//  volet d'aide (app/hmi/HmiHelpPane) les met en page.
//
//  Dans les textes : **gras** et `code` ; plain() les ote.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::guide {

enum class BlockKind : std::uint8_t {
    Heading,     // un intertitre
    Paragraph,
    Bullet,      // une puce
    Step,        // une etape numerotee (label : son numero)
    Code,        // du ST, sur plusieurs lignes
    Tip,         // une astuce
    Warning,     // attention
    Table,       // lignes separees par \n, cellules par \t ; la premiere : les titres
};

struct Block {
    BlockKind   kind{BlockKind::Paragraph};
    std::string text;
    std::string label;
    // 1.10 (chantier P) : "1.10" - le bloc a change dans cette version (@nouveau
    // dans la source ; un intertitre l'emporte sur toute sa section). Vide : non.
    std::string since{};
};

// Une capture d'ecran du guide Word / PDF (PNG_174_...png) et sa legende.
struct Shot {
    std::string file, caption;
};

// Lot 8 : un parametre d'un objet de la bibliotheque (@param dans la source) :
// sa cle ("fill"), son libelle dans l'inspecteur, sa valeur par defaut, ce qu'il regle.
struct Param {
    std::string key, label, def, text;
};

// Lot 16 : une etape du tutoriel d'un objet (@tuto dans la source) - a quel
// instant de l'exemple (secondes), l'objet que la bulle montre ("" : la vue),
// le titre du chapitre, ce que dit la bulle.
struct TutoStep {
    double      at{0};
    std::string target, title, text;
};

struct Topic {
    std::string              key;        // "fonctions"
    std::string              title;      // "Fonctions IHM"
    std::string              chapter;    // "Programmer"
    std::string              summary;    // ce qu'on lit en premier
    std::vector<Block>       blocks;
    std::vector<std::string> see;        // les sujets voisins (cles)
    std::vector<std::string> words;      // F1 sur ces noms : IHM_NAVIGUER, VAR_TEMP...
    std::vector<std::string> places;     // F1 depuis ces volets : "scripts", "fonctions"...
    std::vector<Shot>        shots;
    // Lot 8 : un objet de la bibliotheque (@objet) - son genre ("Button" ; "*" :
    // les parametres communs a tous), ses parametres, ce que montre son exemple anime.
    std::string              kind;
    std::vector<Param>       params;
    std::string              example;
    // Lot 16 : le tutoriel ecrit a la main (vide : il se deduit de l'exemple).
    std::vector<TutoStep>    tutorial;
    // 1.10 (chantier P) : la version ou le sujet est apparu (@depuis 1.9) ; vide :
    // un sujet d'avant. L'aide encadre en orange ce qui est plus recent que la
    // derniere version vue par l'utilisateur (help/Novelties.hpp).
    std::string              since{};
};

// 1.10 : la version la plus recente d'un sujet - la sienne (@depuis) ou celle
// du plus recent de ses blocs (@nouveau) ; "" : rien de marque.
[[nodiscard]] std::string latestChange(const Topic& t);

// Tous les sujets, dans l'ordre du guide (HmiGuideText.cpp, genere).
[[nodiscard]] const std::vector<Topic>& topics();
// Les chapitres, dans l'ordre.
[[nodiscard]] std::vector<std::string> chapters();
[[nodiscard]] const Topic* topic(std::string_view key) noexcept;

// F1. Le nom sous le curseur (sans casse) : le sujet qui en parle, "" sinon.
[[nodiscard]] std::string topicForWord(std::string_view word);
// Lot 8 : le sujet d'un objet de la bibliotheque, par son genre ("Button") ; nul sans.
[[nodiscard]] const Topic* topicForKind(std::string_view kindKey) noexcept;
// Lot 8 : ce que regle un parametre d'un objet - dans le sujet de l'objet, sinon
// dans celui des parametres communs ; "" si le guide n'en dit rien.
[[nodiscard]] std::string paramHelp(std::string_view kindKey, std::string_view key);
// F1. L'endroit ou l'on est - un volet ("scripts", "fonctions", "vue",
// "alarmes"...) : son sujet, "" sinon.
[[nodiscard]] std::string topicForPlace(std::string_view place);

// La recherche : sans casse ni accents ; le titre et les mots d'abord, puis le
// resume, les intertitres, le texte. `excerpt` : le passage autour du terme.
struct Hit {
    std::string key;
    int         score{0};
    std::string excerpt;
};
[[nodiscard]] std::vector<Hit> search(std::string_view term, std::size_t limit = 30);

// Le texte sans ses marques (** et `).
[[nodiscard]] std::string plain(std::string_view text);
// Sans accents ni casse ("echap" pour la touche Echap accentuee) : ce que compare la recherche.
[[nodiscard]] std::string fold(std::string_view text);

} // namespace hmi::guide
