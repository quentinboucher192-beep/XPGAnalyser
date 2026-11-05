// =============================================================================
//  hmi/HmiSoundLibrary.hpp - 1.24.0 : LA BIBLIOTHEQUE DE SONS (les sons natifs, Mes sons)
// -----------------------------------------------------------------------------
//  Le client (1.14.2, repris le 05/11) : << une bibliotheque de sons : ecouter,
//  importer, Mes sons >>, comme la bibliotheque d'images (HmiAssets.hpp, imagelib).
//
//  LES SONS NATIFS sont FABRIQUES par le logiciel (une synthese : des notes, des
//  balayages, des impulsions, une enveloppe), pas lus dans des fichiers : rien a
//  livrer, rien a licencier, le meme son partout. Ranges par categorie : Alarmes
//  (de la plus grave a la plus douce, en suivant l'esprit de l'ISA-18.2 : plus
//  c'est grave, plus c'est rapide et insistant), Notifications, Interface,
//  Procede. Chacun : un WAV PCM 16 bits mono a 22 050 Hz, fait a la premiere
//  demande et garde.
//
//  MES SONS : ceux que l'utilisateur y ajoute (un fichier WAV ou MP3, ou un son
//  du projet), gardes HORS du projet, dans le dossier de ses reglages (a cote de
//  Mes images) : ils le suivent d'un projet a l'autre.
//
//  IMPORTER un son en fait une RESSOURCE du projet, comme un fichier importe : le
//  meme contenu deja la est reutilise ; un nom pris par un autre contenu devient
//  nom_2 (la regle des ressources). Ensuite, il se choisit partout ou un son se
//  choisit (les alarmes, l'action Jouer un son, les scripts).
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::soundlib {

inline constexpr std::string_view kMine = "Mes sons";       // la categorie des sons de l'utilisateur
inline constexpr int               kRate = 22050;            // la frequence des sons natifs

struct Sound {
    std::string id;             // "alarme-haute" ; Mes sons : "mes/" + le nom du fichier
    std::string label;          // "Alarme haute priorite" ; Mes sons : le nom du fichier sans extension
    std::string category;       // "Alarmes"... ; Mes sons : kMine
    std::string words;          // les mots-cles de la recherche
    std::string description;    // a quoi il sert (une phrase)
    double      seconds{0};
    std::string format;         // "WAV", "MP3"
    std::string file;           // Mes sons : son fichier ; vide : natif
    BlobPtr     data;           // son contenu
    [[nodiscard]] bool mine() const noexcept { return !file.empty(); }
};

// ---- les sons natifs ----
[[nodiscard]] const std::vector<Sound>& builtIn();                    // dans l'ordre de leurs categories
[[nodiscard]] const std::vector<std::string>& builtInCategories();    // Alarmes, Notifications, Interface, Procede
[[nodiscard]] const Sound* findBuiltIn(std::string_view id);
// Les echantillons d'un son natif (16 bits, mono, kRate) - les essais, la forme d'onde.
[[nodiscard]] std::vector<std::int16_t> samplesOf(std::string_view id);

// ---- Mes sons : un dossier (les reglages de l'utilisateur), relu a la demande ----
void setUserFolder(std::string folder);
[[nodiscard]] const std::string& userFolder();
// Les sons du dossier (WAV, MP3), par nom ; vide : pas de dossier.
[[nodiscard]] std::vector<Sound> userSounds();
// Ajouter un contenu (un son du projet, un fichier lu) sous le nom `name` (avec son extension) : un autre contenu deja
// sous ce nom : "nom (2).wav" ; le meme contenu deja la : rien n'est ecrit (`existed`). `stored` : le nom du fichier
// garde. Faux et `why` : pas un son, pas de dossier, ecriture impossible.
bool addUserSound(const std::string& name, const Bytes& data, std::string* stored = nullptr, bool* existed = nullptr,
                  std::string* why = nullptr);
bool addUserFile(const std::string& path, std::string* stored = nullptr, bool* existed = nullptr, std::string* why = nullptr);
bool removeUserSound(const Sound&, std::string* why = nullptr);
// Il monte a chaque ajout, retrait ou autre dossier : les volets ouverts relisent Mes sons quand il a change.
[[nodiscard]] std::uint64_t userRevision() noexcept;

// ---- chercher, importer ----
// Le nom, la categorie, la description et les mots-cles : chaque mot cherche commence un de leurs mots, sans casse ni
// accents.
[[nodiscard]] bool matches(const Sound&, std::string_view query);
// Le nom propose dans les ressources : "alarme-haute.wav" ; Mes sons : son fichier.
[[nodiscard]] std::string resourceName(const Sound&);
struct Imported {
    Id          id{kNoId};
    std::string name;
    bool        reused{false};  // le meme contenu etait deja une ressource du projet
};
// Dans le projet (l'appelant fait la commande) : le meme contenu deja la est reutilise, sinon une ressource neuve.
Imported importSound(Project&, const Sound&);
// La ressource du projet qui a deja ce contenu ; nul : aucune.
[[nodiscard]] const Resource* importedAs(const Project&, const Sound&);
// "1,4 s" ; "0,08 s".
[[nodiscard]] std::string durationText(double seconds);

} // namespace hmi::soundlib
