#pragma once
// =============================================================================
//  help/SandboxTrash.hpp - jeter un bac a sable sans l'attendre (1.11, T1)
// -----------------------------------------------------------------------------
//  Tranche 23 (decision 44, la Â« Remise en placeâ€¦ Â») : chaque remise a neuf
//  recopie Armoire_Gaz dans l'un des deux dossiers du tutoriel, et effacait
//  d'abord, sur place, la copie d'avant-derniere (environ 600 fichiers et
//  dossiers) : 1,5 a 2,1 s des 3,5 a 3,9 s de la remise, mesure par ses appels
//  au systeme, strace, sur le texte dossier ; le reste : la copie 0,1 a 0,5 s,
//  l'ouverture et l'IHM 0,6 a 1,1 s, puis ProjectOpened et l'ecran d'analyse).
//  discard() libere le dossier tout de suite : il le renomme (un seul appel au
//  systeme, dans le meme dossier parent), puis l'efface dans un fil a part.
//  1.11.1 (tranche 31) : un seul fil et une file d'attente ; le fil principal
//  n'attend plus jamais un effacement (avant : « au plus un fil a la fois », le
//  suivant attendait le precedent ; sous charge, 8,1 s de blocage dans
//  std::thread::join, rapport de T2 du 03/10 a 11 h 57). Renommage impossible,
//  ou pas de fil : effacement sur place, comme avant.
//  finishDiscards() vide la file et attend le fil (la sortie de l'appli :
//  tutorials::shutdown ; les essais).
//  Une corbeille laissee par un arret brutal porte un nom qui n'est pas celui
//  d'un tutoriel : le menage des bacs de plus de 10 minutes l'efface.
// =============================================================================

#include <filesystem>
#include <string>

namespace help::sandbox {

inline constexpr const char* kTrashPrefix = "corbeille-";

// Rend vrai si `dir` n'existe plus au retour (libre pour une copie neuve).
bool discard(const std::filesystem::path& dir);

// Attend que la file soit vide et le fil parti (les effacements en cours et en attente).
void finishDiscards();
// 1.11.1 (tranche 31) : efface `dir` dans le fil, sans le renommer et sans attendre (les bacs
// des tutoriels quittes depuis plus de 10 minutes). Deja dans la file, ou en cours : rien.
void discardLater(const std::filesystem::path& dir);
// `dir` est-il dans la file, ou en cours d'effacement ?
[[nodiscard]] bool pending(const std::filesystem::path& dir);
// 1.11.1 (T1, R111-14 / R111-24) : `folder` est-il `root` (le dossier des bacs) ou dedans ?
// Sans toucher au disque (chemins normalises). tutorials::isSandboxFolder s'en sert.
[[nodiscard]] bool contains(const std::string& folder, const std::filesystem::path& root);

} // namespace help::sandbox
