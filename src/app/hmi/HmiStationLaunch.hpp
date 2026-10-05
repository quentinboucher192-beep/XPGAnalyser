// =============================================================================
//  app/hmi/HmiStationLaunch.hpp - lancer le poste d'exploitation (lot 14)
// -----------------------------------------------------------------------------
//  LA LIGNE DE COMMANDE. xpg_analyzer --ihm "<dossier du projet>" : l'IHM
//  seule, en plein ecran (screens/StationScreen).
//
//  LE LANCEUR, pose dans le dossier du projet : lancer-poste.cmd (Windows) ou
//  lancer-poste.sh. Il donne au programme LE DOSSIER OU IL SE TROUVE : copie
//  avec le projet sur un autre poste, il marche encore (le programme, lui,
//  doit etre au meme endroit).
//
//  DEMARRER AVEC LA SESSION. Windows : une valeur dans la cle Run de
//  l'utilisateur (HKCU\Software\Microsoft\Windows\CurrentVersion\Run) ;
//  Linux : un fichier .desktop dans ~/.config/autostart (ou
//  $XDG_CONFIG_HOME/autostart). Rien a installer, rien pour les autres
//  utilisateurs du poste ; se retire d'un clic.
//
//  Ce fichier est le seul du poste a parler au systeme : il n'inclut aucun
//  en-tete de l'interface (windows.h reste ici).
// =============================================================================
#pragma once

#include <string>

namespace app::station {

// Le systeme ou l'on tourne (les lanceurs et le demarrage en different).
[[nodiscard]] constexpr bool isWindows() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

// Le programme qui tourne : son chemin complet (vide si inconnu).
[[nodiscard]] std::string currentExecutable();

// La ligne de commande du poste : "programme" --ihm "dossier".
[[nodiscard]] std::string commandLine(const std::string& exe, const std::string& projectFolder, bool windows = isWindows());

// Le lanceur : son nom (lancer-poste.cmd / lancer-poste.sh) et son contenu.
[[nodiscard]] std::string launcherName(bool windows = isWindows());
[[nodiscard]] std::string launcherText(const std::string& exe, const std::string& projectName, bool windows = isWindows());
// L'ecrire dans le dossier du projet (executable sous Linux). `where` : son
// chemin ; sinon `why`.
bool writeLauncher(const std::string& exe, const std::string& projectFolder, const std::string& projectName, std::string* where,
                   std::string* why);

// Demarrer avec la session. Lot 15 : UN SEUL PROJET PAR PC - une seule entree,
// "XPG Poste d'exploitation" ; en poser une pour un projet remplace celle d'un
// autre (et les entrees du lot 14, une par projet, s'effacent).
[[nodiscard]] std::string autostartName(const std::string& projectName = {});
// Le fichier .desktop (Linux) : xpg-poste.desktop.
[[nodiscard]] std::string autostartFile(const std::string& projectName = {});
// L'entree existe-t-elle ; `command` : la ligne qu'elle lance ; `where` : ou
// elle est (la cle, le fichier).
[[nodiscard]] bool autostartEnabled(const std::string& projectName = {}, std::string* command = nullptr, std::string* where = nullptr);
// Le dossier du projet que lance l'entree (vide : aucune).
[[nodiscard]] std::string autostartProject(std::string* where = nullptr);
// Le dossier que donne une ligne de commande (... --ihm "dossier").
[[nodiscard]] std::string folderOfCommand(const std::string& command);
// on : l'entree lance CE projet ; off : l'entree s'efface si elle lance ce
// projet (celle d'un autre reste).
bool setAutostart(const std::string& projectName, const std::string& exe, const std::string& projectFolder, bool on, std::string* where,
                  std::string* why);

// Le contenu du fichier .desktop (Linux) - pour les essais.
[[nodiscard]] std::string desktopEntry(const std::string& projectName, const std::string& exe, const std::string& projectFolder);

} // namespace app::station
