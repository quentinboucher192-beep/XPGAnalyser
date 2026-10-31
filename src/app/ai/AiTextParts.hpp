// =============================================================================
//  app/ai/AiTextParts.hpp - 1.20.0 : les pieces jointes TEXTE, decoupees a la mesure du modele
// -----------------------------------------------------------------------------
//  Avant la 1.20.0, un texte joint (.txt, .csv, .json, .xml) etait coupe a 200 Ko.
//  Maintenant, AVANT d'envoyer, la limite est verifiee : la fenetre de contexte du
//  modele (connue par la liste des modeles, ou donnee dans les Parametres), moins la
//  reponse attendue, les consignes et les messages. Le fichier part ENTIER tant qu'il
//  tient, decoupe en parties (aux fins de ligne, << partie 2/5, lignes 1201 a 2400 >>) ;
//  au-dela, les parties qui ne tiennent pas sont LUES A PART, une demande par partie
//  (une << lecture prealable >> : ce qui sert a la demande, recopie fidelement), et leur
//  extrait part a leur place. Rien n'est jamais coupe en silence : ce qui n'a pas pu
//  etre lu est dit, a l'IA et a l'utilisateur.
//
//  Tout se passe sur le fil du fournisseur (jamais sur celui de l'interface) ; l'etat
//  << Lecture de x : partie 3/9 >> est rendu au panneau.
// =============================================================================
#pragma once

#include "AiProvider.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::textparts {

inline constexpr std::size_t kPartBytes = 96 * 1024;        // une partie (un bloc de texte)
inline constexpr double      kBytesPerToken = 2.5;          // prudent : un CSV, du code (~2,5) ; du francais (~3,5)
inline constexpr int         kExtractTokens = 3000;         // la longueur d'un extrait (la reponse d'une lecture prealable)
inline constexpr std::size_t kMaxPrereads = 40;             // les lectures prealables d'une demande, au plus
inline constexpr int         kUnknownWindow = 128000;       // la fenetre d'un modele inconnu (jetons)

struct Part {
    std::string text;
    std::size_t firstLine{1}, lastLine{1};
};

// Le texte d'un fichier en UTF-8 : sans BOM ; du Windows-1252 (Excel) converti.
[[nodiscard]] std::string normalize(std::string bytes);
// Decoupe aux fins de ligne en parties d'au plus `maxBytes` (une ligne plus longue : coupee sans casser un caractere).
[[nodiscard]] std::vector<Part> split(std::string_view text, std::size_t maxBytes = kPartBytes);
// Les octets de texte joint que la demande peut porter : la fenetre (contextTokens, sinon `fallbackWindow`), moins
// la reponse (maxTokens), les consignes, les textes des messages et une marge de 10 %.
[[nodiscard]] std::size_t budgetBytes(const ProviderRequest& req, int fallbackWindow);

// Une piece texte, preparee : les blocs qui partent a sa place (dans l'ordre) et, si elle ne part pas entiere telle
// quelle, la note pour l'utilisateur.
struct Prepared {
    std::vector<std::string> blocks;
    std::string              note;
};

// La lecture prealable d'une partie : une demande sans outil (`mini`), son texte (nullopt : echec, `error` dit pourquoi).
using Ask = std::function<std::optional<std::string>(const ProviderRequest& mini, std::string* error)>;
using Progress = std::function<void(const std::string& detail)>;

// 1.20.1 : LE TEXTE EXACT A LA DEMANDE. Les pieces qui n'ont pas tenu entieres (des parties en extrait ou non envoyees)
// restent lisibles : l'IA appelle l'outil << lire_piece_jointe >> (fichier, de_ligne, a_ligne) et le fournisseur lui
// repond, sur son fil, par le texte EXACT de ces lignes - dans la place qui reste dans la fenetre du modele.
inline constexpr std::string_view kReadTool = "lire_piece_jointe";
inline constexpr int              kMaxReads = 8;   // les lectures exactes d'une demande, au plus
struct Library {
    struct File {
        std::string              name;
        std::string              text;
        std::vector<std::size_t> lineStarts;   // l'octet ou commence chaque ligne
    };
    std::vector<File> files;
    std::size_t       room{0};   // les octets de texte que la demande peut encore porter
    [[nodiscard]] bool active() const noexcept { return !files.empty(); }
    // La reponse a un appel de l'outil (son entree JSON) : le texte exact des lignes demandees (borne par la place), ou
    // ce qui ne va pas (fichier inconnu, lignes hors du fichier, plus de place). `detail` : pour l'etat du panneau.
    [[nodiscard]] std::string read(std::string_view inputJson, std::string* detail = nullptr);
};
// La definition de l'outil (au format Anthropic : name, description, input_schema), a ajouter au tableau des outils.
[[nodiscard]] std::string readToolJson();
// Le tableau des outils `toolsJson` (vide : aucun) avec l'outil de lecture en plus.
[[nodiscard]] std::string withReadTool(const std::string& toolsJson);
// 1.21.0 : le tableau des outils avec un outil de plus (sa definition JSON) - les outils locaux (LocalTool).
[[nodiscard]] std::string withTool(const std::string& toolsJson, const std::string& definition);

// Prepare les pieces texte de `req` (ProviderAttachment::prepared). Le budget va d'abord a la DERNIERE demande, puis
// aux plus anciennes. Une piece qui tient : ses parties. Sinon : ses premieres parties, puis les autres lues a part
// (`ask` ; seulement pour la derniere demande - les plus anciennes reprennent les extraits deja faits, gardes le temps
// de la session) ; ce qui ne peut pas etre lu est dit. `ask` vide : pas de lecture prealable. `cancelled` : Arreter.
// Les notes : pour l'utilisateur.
// `library` non nul : les pieces qui n'ont pas tenu entieres y sont gardees (le texte exact a la demande) et les blocs
// le disent a l'IA ; sa place est ce qui reste de la fenetre.
void prepare(ProviderRequest& req, int fallbackWindow, const Ask& ask, const Progress& progress, const std::function<bool()>& cancelled,
             std::vector<std::string>* notes, Library* library = nullptr);

// La preparation d'une seule piece, sans lecture prealable, dans `budget` octets (les corps des fournisseurs, quand
// prepare n'a pas ete appele).
[[nodiscard]] Prepared prepareOne(const ProviderAttachment& a, const std::string& text, std::size_t budget);

// La demande d'une lecture prealable (pour les essais) : le modele de `req`, sans outil, la demande de l'utilisateur
// et la partie.
[[nodiscard]] ProviderRequest prereadRequest(const ProviderRequest& req, const std::string& question, const std::string& fileName, const Part& part,
                                             std::size_t index, std::size_t count);

// Oublie les extraits gardes (les essais).
void forgetExtracts();

} // namespace app::ai::textparts
