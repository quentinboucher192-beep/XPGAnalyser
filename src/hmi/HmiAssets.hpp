// =============================================================================
//  hmi/HmiAssets.hpp — ressources et fichiers externes : importer, citer, suivre
// -----------------------------------------------------------------------------
//  RESSOURCES. Importer lit le fichier une fois, en tire ce qu'il contient
//  (hmi/HmiMedia) et le copie dans le projet. Qui la cite : une Image par sa
//  propriete "image", une Video par "video" (et son image d'attente "poster"),
//  un texte par sa police "font", et toute expression qui la nomme entre
//  guillemets - SEL(Ouvert, 'vanne_fermee.png', 'vanne_ouverte.png'). Ce qui
//  n'est cite nulle part est "inutilise" : Generer le signale, et le
//  gestionnaire sait le montrer et le retirer.
//
//  FICHIERS EXTERNES. On garde le chemin, et ce qu'on a vu en liant : taille et
//  date. L'etat se recalcule a chaque affichage : present, modifie depuis le
//  lien, absent, ou non verifiable (une base externe, qui n'est qu'une chaine
//  de connexion tant qu'aucun pilote n'est installe).
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---- ressources ---------------------------------------------------------------
// Lire un fichier et en faire une ressource (identifiant attribue par le
// projet, nom libre). N'ajoute rien : l'appelant l'ajoute dans une commande.
[[nodiscard]] core::Result<Resource> readResource(Project&, const std::string& path, std::string wantedName = {});
// La meme, depuis des octets deja en memoire (tests, import d'archive).
[[nodiscard]] Resource makeResource(Project&, std::string name, BlobPtr data, std::string origin);
// Remettre a jour ce qu'on sait d'une ressource d'apres son contenu.
void describeResource(Resource&);

struct Citation {
    Id          view{kNoId};
    Id          object{kNoId};
    std::string where;        // "Vue_Armoire_A/Logo_Site.image"
    bool        expression{false};
};
// Qui cite `name` : proprietes et expressions de tous les objets.
[[nodiscard]] std::vector<Citation> citations(const Project&, std::string_view name);
// Les proprietes qui designent une ressource, par genre d'objet.
[[nodiscard]] bool citesResource(std::string_view propertyKey) noexcept;
[[nodiscard]] std::vector<const Resource*> unusedResources(const Project&);
// Renommer et mettre a jour ce qui la cite (proprietes et expressions).
// Rend le nombre de proprietes modifiees ; faux si le nom est pris ou vide.
bool renameResource(Project&, Id resource, const std::string& newName, std::size_t* updated = nullptr,
                    std::string* why = nullptr);

// ---- fichiers externes ------------------------------------------------------------
enum class ExternalStatus : std::uint8_t { Present, Modified, Missing, Unverifiable };
[[nodiscard]] std::string_view externalStatusLabel(ExternalStatus) noexcept;   // "present", "modifie"...

struct ExternalState {
    ExternalStatus status{ExternalStatus::Missing};
    std::uint64_t  bytes{0};
    std::string    modified;       // la date du fichier sur le disque, maintenant
    std::string    resolvedPath;
};
// Le chemin reel : absolu tel quel, relatif au dossier du projet sinon.
[[nodiscard]] std::string   resolveExternalPath(const ExternalFile&, const std::string& projectFolder);
[[nodiscard]] ExternalState externalState(const ExternalFile&, const std::string& projectFolder);
// Un nouveau lien : taille et date relevees maintenant.
[[nodiscard]] ExternalFile linkExternal(Project&, std::string name, ExternalKind, std::string path, std::string part,
                                        const std::string& projectFolder);
// "Relier" : prendre acte d'un fichier modifie (nouvelle taille, nouvelle date).
void relinkExternal(ExternalFile&, const std::string& projectFolder);
// Qui montre ce fichier : les Tableaux dont la source est `name`.
[[nodiscard]] std::vector<Citation> externalCitations(const Project&, std::string_view name);

// La date d'un fichier, "2026-09-22 01:10:05" ; vide si absent.
[[nodiscard]] std::string fileStamp(const std::string& path);

} // namespace hmi
