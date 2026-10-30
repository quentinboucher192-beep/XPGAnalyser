@echo off
rem =============================================================================
rem  maintenance\poste.cmd - 1.14.0 : le poste d'exploitation, relance s'il se ferme
rem -----------------------------------------------------------------------------
rem  Pose par l'installateur d'API IHM Studio (le profil Poste d'exploitation, la case
rem  "Le relancer s'il se ferme") ; le raccourci du menu Demarrer et le demarrage avec
rem  la session le lancent, reduit :
rem      poste.cmd --ihm "D:\Projets\Armoire"      (ou : poste.cmd "D:\Projets\Armoire")
rem  Il lance IHM Studio en poste d'exploitation (IHM-Studio.exe --poste : plein ecran,
rem  sans l'editeur) et le relance 5 s apres qu'il s'est ferme. S'il se ferme 3 fois de
rem  suite moins de 60 s apres son lancement, il plante sans doute au demarrage : le
rem  lanceur s'arrete (pas de boucle folle) et l'ecrit dans le journal - poste.log, dans
rem  les Journaux de l'installation (installation.ini, [Maintenance] journaux), sinon dans
rem  %LOCALAPPDATA%\XpgAnalyzer\Journaux.
rem  Pour quitter le poste pour de bon : fermer d'abord cette fenetre (reduite dans la
rem  barre des taches), puis quitter le poste (Ctrl+Alt+Q).
rem =============================================================================
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
title Poste d'exploitation - relance automatique
set "RELANCE_SECONDES=5"
set "COURT_SECONDES=60"
set "ECHECS_MAX=3"
set "EXE=%~dp0..\IHM-Studio.exe"
set "PROJET=%~1"
if /i "%~1"=="--ihm" set "PROJET=%~2"
if /i "%~1"=="--poste" set "PROJET=%~2"
call :journal
if not defined PROJET (
  call :noter "pas de projet : poste.cmd --ihm D:\Projets\Armoire"
  goto :fin
)
if not exist "%EXE%" (
  call :noter "IHM Studio est introuvable : %EXE%"
  goto :fin
)
call :noter "le lanceur demarre : %PROJET%"
set /a ECHECS=0
:boucle
call :secondes T0
call :noter "IHM Studio se lance en poste d'exploitation"
start "" /wait "%EXE%" --poste "%PROJET%"
set "CODE=!ERRORLEVEL!"
call :secondes T1
set /a DUREE=T1-T0
if !DUREE! lss 0 set /a DUREE+=86400
call :noter "IHM Studio s'est ferme apres !DUREE! s, code !CODE!"
if !DUREE! lss %COURT_SECONDES% (set /a ECHECS+=1) else (set /a ECHECS=0)
if !ECHECS! geq %ECHECS_MAX% (
  call :noter "!ECHECS! fermetures de suite moins de %COURT_SECONDES% s apres son lancement : IHM Studio ne demarre pas, le lanceur s'arrete."
  call :noter "A voir : le projet, son etat FINISH, les rapports de plantage. Pour relancer : menu Demarrer, Poste d'exploitation."
  goto :fin
)
call :noter "relance dans %RELANCE_SECONDES% s"
timeout /t %RELANCE_SECONDES% /nobreak >nul 2>&1 || ping -n %RELANCE_SECONDES% 127.0.0.1 >nul 2>&1
goto :boucle

:fin
endlocal
exit /b 0

rem Les secondes depuis minuit (%TIME% : " 9:05:03,12" ou "09:05:03.12" selon la langue ; ce qui
rem n'est pas un nombre compte pour 0).
:secondes
set "H=" & set "M=" & set "SS="
for /f "tokens=1-3 delims=:., " %%a in ("%TIME%") do (set "H=%%a" & set "M=%%b" & set "SS=%%c")
call :entier H
call :entier M
call :entier SS
set /a "%~1=H*3600+M*60+SS"
exit /b 0

rem Un entier en decimal, sans ses zeros de tete (sinon "08" se lirait en octal) ; 0 si ce n'en est pas un.
:entier
set "N=!%~1!"
if not defined N set "N=0"
for /f "delims=0123456789" %%x in ("!N!") do set "N=0"
:entier_zero
if not "!N!"=="0" if "!N:~0,1!"=="0" (set "N=!N:~1!" & goto :entier_zero)
set "%~1=!N!"
exit /b 0

rem Le journal : celui de l'installation, s'il s'ecrit ; sinon celui du compte.
:journal
set "JOURNAUX="
for /f "usebackq tokens=1,* delims==" %%a in (`findstr /b /c:"journaux" "%~dp0..\installation.ini" 2^>nul`) do set "JOURNAUX=%%b"
if defined JOURNAUX if "!JOURNAUX:~0,1!"==" " set "JOURNAUX=!JOURNAUX:~1!"
if defined JOURNAUX if "!JOURNAUX:~0,1!"==" " set "JOURNAUX=!JOURNAUX:~1!"
if defined JOURNAUX if not exist "!JOURNAUX!\" mkdir "!JOURNAUX!" >nul 2>&1
if defined JOURNAUX (
  (type nul >>"!JOURNAUX!\poste.log") 2>nul
  if errorlevel 1 set "JOURNAUX="
)
if not defined JOURNAUX set "JOURNAUX=%LOCALAPPDATA%\XpgAnalyzer\Journaux"
if not exist "!JOURNAUX!\" mkdir "!JOURNAUX!" >nul 2>&1
set "JOURNAL=!JOURNAUX!\poste.log"
exit /b 0

rem Une ligne, a l'ecran et dans le journal (avec la date et l'heure).
:noter
set "LIGNE=%~1"
echo %DATE% %TIME% !LIGNE!
>>"!JOURNAL!" echo %DATE% %TIME% !LIGNE!
exit /b 0
