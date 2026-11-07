@echo off
chcp 65001 >nul
setlocal EnableExtensions
title Service des licences XPG - mise en ligne
rem =============================================================================
rem  deployer.bat - 1.35.0 : mettre en ligne le service des licences sur TON
rem  compte Cloudflare (gratuit). Le pas a pas : LISEZ-MOI-SERVICE.txt.
rem  Il verifie Node.js, puis lance deployer.mjs (a cote de ce fichier) : la cle
rem  PUBLIQUE de l'editeur (cle-publique.txt), wrangler deploy, l'adresse.
rem  Aucun secret ici : la cle privee de l'editeur ne quitte jamais son outil.
rem =============================================================================
cd /d "%~dp0"

where node >nul 2>nul
if errorlevel 1 (
  echo.
  echo Node.js n'est pas installé sur ce PC.
  echo Installe la version LTS depuis https://nodejs.org ^(le bouton « LTS »^),
  echo ferme cette fenêtre, puis relance deployer.bat.
  goto :erreur
)
where npx >nul 2>nul
if errorlevel 1 (
  echo.
  echo npx manque : réinstalle Node.js ^(version LTS, https://nodejs.org^), puis relance deployer.bat.
  goto :erreur
)
if not exist "%~dp0deployer.mjs" (
  echo.
  echo deployer.mjs manque : il doit être à côté de deployer.bat.
  goto :erreur
)

node "%~dp0deployer.mjs" %*
if errorlevel 1 goto :erreur
echo.
pause
exit /b 0

:erreur
echo.
echo La mise en ligne n'a pas été faite.
pause
exit /b 1
