// =============================================================================
//  core/Version.hpp - le nom et la version de l'application (1.11.24)
// -----------------------------------------------------------------------------
//  La seule source pour le code. La meme version est ecrite dans
//  CMakeLists.txt (project VERSION), resources/windows/xpg_analyzer.rc et
//  outils/config.ini ; outils\package.bat verifie que les quatre concordent
//  avant de fabriquer l'installateur.
//
//  1.12.20 : LES NOMS. Deux applications, API Studio (l'automate) et IHM Studio
//  (l'IHM) ; XPG_ANALYZER_NAME est le nom des deux ensemble (l'installateur,
//  Applications installees, le menu Demarrer). core::productName() (Edition.hpp)
//  donne celui de l'application lancee : c'est lui que l'interface montre.
//  Les dossiers de l'application gardent leur nom d'origine (%APPDATA%\XpgAnalyzer,
//  XPGAnalyser.ini, Documents\XPGAnalyser) : les renommer perdrait les reglages
//  et les donnees ; les fichiers .XPG de Control Expert restent ce qu'ils sont.
// =============================================================================
#pragma once

#define XPG_ANALYZER_VERSION "1.12.29"
#define XPG_ANALYZER_NAME    "API IHM Studio"
#define XPG_PRODUCT_API      "API Studio"
#define XPG_PRODUCT_IHM      "IHM Studio"
