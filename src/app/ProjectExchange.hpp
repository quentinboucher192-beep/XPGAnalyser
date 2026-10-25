// =============================================================================
//  app/ProjectExchange.hpp - 1.12.30 : echanger un projet (archive)
// -----------------------------------------------------------------------------
//  LES GESTES DU MENU PROJET, dans les deux applications :
//
//    Exporter en archive .xihm / .xapi...   tout le projet dans un seul fichier
//                                           (project/ProjectArchive, hmi/HmiArchive,
//                                           project/ApiArchive) ;
//    Importer une archive...                le fichier, puis comme Ouvrir.
//
//  OUVRIR UNE ARCHIVE (Ouvrir..., un fichier lache sur la fenetre, la ligne de
//  commande - IHM-Studio.exe chemin.xihm, l'installateur associe l'extension -,
//  une autre instance qui la passe a celle qui tourne) : l'archive est relue et
//  verifiee (abimee : le dire, et proposer de deballer ce qui est intact ; d'une
//  version plus recente ou de l'autre application : le dire), puis OU la deballer
//  (par defaut : le dossier des projets, sous son nom), puis le projet s'ouvre et
//  le controle dit ce qu'il a trouve.
//
//  LE TRAVAIL LONG SE FAIT A COTE : ecrire l'archive, la deballer (sur un autre
//  fil, la tache de fond du bandeau). L'interface ne se fige pas.
// =============================================================================
#pragma once

#include <string>

namespace app {

class App;

namespace exchange {

void askExportArchive(App&);
void askImportArchive(App&);

// Une archive de projet (.xihm, .xapi, l'ancienne .zip de l'IHM) : le dialogue
// suit (vrai). Faux : ce n'en est pas une, le chemin suit son cours.
bool openArchive(App&, const std::string& path);
// Une archive de projet (un fichier lache sur l'espace de travail : celle-la
// s'ouvre, les autres vont aux ressources).
[[nodiscard]] bool isArchive(const std::string& path);
// Ce que Ouvrir donne a openArchive : une archive de projet.
[[nodiscard]] bool isExchangeFile(const std::string& path);

// A chaque image : les taches finies rendent la main.
void poll(App&);
// A la fermeture : les taches en cours se terminent.
void shutdown();
// Une tache en cours (les seances l'attendent avant leur capture).
[[nodiscard]] bool busy();
// Ce qu'a dit la derniere tache finie (les seances, les essais).
[[nodiscard]] const std::string& lastReport();

} // namespace exchange
} // namespace app
