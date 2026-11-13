@echo off
rem Poste d'exploitation - PRG1 (xpg_analyzer, lot 14)
rem L'IHM seule, en plein ecran. Le projet est le dossier de ce fichier :
rem copie avec lui, il marche encore. Ctrl+Alt+Q pour en sortir.
chcp 65001 >nul
start "" "C:\Program Files (x86)\XpgAnalyzer\build\Debug\XpgAnalyzer.exe" --ihm "%~dp0."
