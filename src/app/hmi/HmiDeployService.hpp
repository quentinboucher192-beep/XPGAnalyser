// =============================================================================
//  app/hmi/HmiDeployService.hpp - 1.28.0 : le volet Deploiement reseau, branche
//                                 sur deploy/ (le paquet, le client, l'appairage)
// -----------------------------------------------------------------------------
//  Le dossier du Studio (Settings::sharedFolder()/Deploiement) garde :
//    studio.cer / studio.cle      le certificat TLS de ce PC (la cle sous DPAPI)
//    cle_editeur.bin              la cle d'editeur Ed25519 (deploy/DevKey)
//    machines-appairees.json      les PC de maintenance appaires (deploy/MachineStore)
//  Les cles sont creees au premier geste qui en a besoin (sur le fil de travail).
//  Chaque fonction du Backend est bloquante (avec ses delais) : le volet les
//  appelle sur son fil de travail, jamais sur celui de l'ecran.
// =============================================================================
#pragma once

#include "HmiDeployPane.hpp"

#include <functional>
#include <string>

namespace app {

// `folder` : le dossier du Studio ; `program` : le programme que le lanceur du paquet lancera (le chemin
// d'IHM-Studio.exe). Les essais passent un dossier jetable et des ports a eux (`discoveryPort`).
[[nodiscard]] HmiDeployPane::Backend makeDeployBackend(std::string folder, std::function<std::string()> program,
                                                       int discoveryPort = 0);

} // namespace app
