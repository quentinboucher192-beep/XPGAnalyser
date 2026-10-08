// =============================================================================
//  core/Version.hpp - le nom et la version de l'application (1.11.13)
// -----------------------------------------------------------------------------
//  La seule source pour le code. La meme version est ecrite dans
//  CMakeLists.txt (project VERSION), resources/windows/xpg_analyzer.rc et
//  outils/config.ini ; outils\package.bat verifie que les quatre concordent
//  avant de fabriquer l'installateur.
//
//  XPG_ANALYZER_NAME : le nom montre a l'utilisateur (Applications installees,
//  menu Demarrer, --version). Les dossiers de l'application gardent leur nom
//  d'origine (%APPDATA%\XpgAnalyzer) : les renommer perdrait les reglages.
// =============================================================================
#pragma once

#define XPG_ANALYZER_VERSION "1.11.13"
#define XPG_ANALYZER_NAME    "XPGAnalyser"
