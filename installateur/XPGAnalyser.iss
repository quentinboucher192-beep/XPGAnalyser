; =============================================================================
;  installateur\XPGAnalyser.iss - l'installateur Windows de XPGAnalyser
; -----------------------------------------------------------------------------
;  Fabriqué par outils\build_installer.bat (ou package.bat) avec Inno Setup 6.3
;  ou plus : toutes les valeurs viennent de outils\config.ini, passées par /D
;  (Nom, Editeur, Version, AppGuid, Exe, Staging...). Les valeurs ci-dessous ne
;  servent que si on compile ce fichier à la main.
;
;  Ce qu'il fait, sur un PC vierge comme sur une ancienne version :
;   - demande pour qui installer (moi seul, sans droits ; ou tous les comptes) ;
;   - demande où ranger les données (Documents\XPGAnalyser par défaut) : jamais
;     dans le dossier du programme ;
;   - installe le programme ET tout ce dont il a besoin (SDL3.dll, le runtime
;     Visual C++ à côté de l'exe) : rien d'autre à installer, pas d'Internet ;
;   - sauvegarde la version installée avant de la remplacer ; ferme
;     l'application proprement ; reprend les anciennes données (copiées, jamais
;     déplacées ni effacées) ; vérifie chaque fichier (manifeste SHA-256) ;
;   - laisse une réserve de réparation et les scripts de maintenance (menu
;     Démarrer > XPGAnalyser > Maintenance) ;
;   - note chaque étape : une installation coupée se reprend ;
;   - la désinstallation garde tes données, sauf si tu tapes SUPPRIMER.
;
;  1.8.0 (suite) :
;   - PC vierge : aucun projet. La reprise ne propose que les versions vraiment
;     INSTALLÉES (leur exe est là, ou le lot 8 connu du registre) ;
;   - la page « Ta bibliothèque » : les blocs livrés, et les tiens, ajoutés par
;     glisser-déposer ou par Ajouter (une vraie liste Windows : aide\xpgliste.dll) ;
;   - « Désinstaller XPGAnalyser » dans le menu Démarrer (la recherche Windows le trouve) ;
;   - l'option « Épingler à la barre des tâches » (au premier lancement, par l'application) ;
;   - la page « Ce qui a été installé » : les programmes, leur architecture, les dossiers.
;
;  Sans surveillance : XPGAnalyser-Setup-<v>.exe /VERYSILENT /SUPPRESSMSGBOXES
;    [/CURRENTUSER | /ALLUSERS] [/DIR="..."] [/DONNEES="D:\Mes données"]
;    [/MIGRER=oui|non] [/DESINSTALLERLOT8=oui|non] [/LOG="fichier"]
;  (voir docs\GUIDE-INSTALLATION.md).
; =============================================================================

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 ou plus est necessaire (outils\install_build_tools.bat l'installe).
#endif

#ifndef Nom
  #define Nom "XPGAnalyser"
#endif
#ifndef Editeur
  #define Editeur "XPGAnalyser"
#endif
#ifndef Version
  #define Version "1.11.22"
#endif
#ifndef AppGuid
  #define AppGuid "68A57092-419E-4BEF-8486-57B131E9C592"
#endif
#ifndef Exe
  #define Exe "XpgAnalyzer.exe"
#endif
#ifndef Description
  #define Description "Analyse, IHM et simulation de projets Control Expert"
#endif
#ifndef UrlSupport
  #define UrlSupport ""
#endif
#ifndef Staging
  #define Staging "..\dist\staging"
#endif
#ifndef Sortie
  #define Sortie ".."
#endif
#ifndef DonneesDefaut
  #define DonneesDefaut "{Documents}\XPGAnalyser"
#endif
#ifndef AnciensDossiers
  #define AnciensDossiers "{pf32}\XpgAnalyzer;{pf}\XpgAnalyzer;{localappdata}\Programs\XpgAnalyzer"
#endif
#ifndef Chaine
  #define Chaine "msvc"
#endif

[Setup]
AppId={{{#AppGuid}}
AppName={#Nom}
AppVersion={#Version}
AppVerName={#Nom} {#Version}
AppPublisher={#Editeur}
#if UrlSupport != ""
AppPublisherURL={#UrlSupport}
AppSupportURL={#UrlSupport}
#endif
AppComments={#Description}
AppCopyright={#Editeur}
VersionInfoVersion={#Version}.0
VersionInfoProductName={#Nom}
VersionInfoProductVersion={#Version}
VersionInfoDescription=Installation de {#Nom} {#Version}
VersionInfoCompany={#Editeur}
DefaultDirName={autopf}\{#Nom}
DefaultGroupName={#Nom}
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UsePreviousPrivileges=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
WizardSizePercent=110
WizardImageFile=images\grande.bmp,images\grande-2x.bmp
WizardSmallImageFile=images\petite.bmp,images\petite-2x.bmp
SetupIconFile=..\resources\logo\xpg_analyzer.ico
UninstallDisplayIcon={app}\{#Exe}
UninstallDisplayName={#Nom}
OutputDir={#Sortie}
OutputBaseFilename={#Nom}-Setup-{#Version}
Compression=lzma2/ultra
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
ShowLanguageDialog=no
LanguageDetectionMethod=none
AlwaysShowDirOnReadyPage=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
; Le texte d'origine parle d'icônes « sur le Bureau », même quand il n'y en a pas.
FinishedLabel=L'assistant a terminé l'installation de [name].%n%n[name] est dans le menu Démarrer, avec sa Maintenance (vérifier, réparer, restaurer). Pour le désinstaller : tape « désinstaller » dans la recherche Windows. Tes dossiers se règlent dans l'application : accueil > Dossiers.
FinishedLabelNoIcons=L'assistant a terminé l'installation de [name].

[CustomMessages]
fr.RaccourciBureau=Créer un raccourci sur le Bureau
fr.Raccourcis=Raccourcis :
fr.Lancer=Lancer {#Nom}
fr.EpinglerBarre=Épingler {#Nom} à la barre des tâches (au premier lancement : Windows te demande de confirmer)

[Tasks]
Name: "bureau"; Description: "{cm:RaccourciBureau}"; GroupDescription: "{cm:Raccourcis}"; Flags: unchecked
; 1.8.0 : Windows interdit a un installateur d'epingler ; l'application le demande a Windows
; au premier lancement (installation.ini : epingler_barre_taches), une fois par compte.
Name: "barre"; Description: "{cm:EpinglerBarre}"; GroupDescription: "{cm:Raccourcis}"

[Files]
; Le programme et tout ce dont il a besoin (préparé par outils\package.bat dans dist\staging).
Source: "{#Staging}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; La réserve de réparation : les mêmes fichiers (Inno Setup ne les stocke qu'une fois).
Source: "{#Staging}\*"; DestDir: "{code:DossierReserve}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Les étapes PowerShell, pour la sauvegarde AVANT la copie (le programme n'est pas encore là).
Source: "{#Staging}\maintenance\lib\*"; DestDir: "{tmp}\xpg\lib"; Flags: dontcopy
Source: "{#Staging}\maintenance\config.ini"; DestDir: "{tmp}\xpg"; Flags: dontcopy
; 1.8.0 : la page « Ta bibliothèque » - la liste Windows (une DLL 32 bits, comme Setup.exe)
; et l'index des blocs livrés (lu avant la copie).
Source: "aide\xpgliste.dll"; Flags: dontcopy
Source: "{#Staging}\libs\index.txt"; DestName: "libs-index.txt"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\{#Nom}\{#Nom}"; Filename: "{app}\{#Exe}"; WorkingDir: "{app}"; Comment: "{#Description}"
Name: "{autoprograms}\{#Nom}\Maintenance\Réparer"; Filename: "{app}\maintenance\repair.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Vérifier l'installation"; Filename: "{app}\maintenance\verify_installation.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Reprendre une installation interrompue"; Filename: "{app}\maintenance\resume_installation.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Restaurer une sauvegarde"; Filename: "{app}\maintenance\restore_backup.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Collecter les journaux"; Filename: "{app}\maintenance\collect_diagnostics.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Réinitialiser mes réglages"; Filename: "{app}\maintenance\reset_user_settings.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Tes données"; Filename: "{code:DonneesReel}"; Check: not IsAdminInstallMode
; 1.8.0 : trouvé par la recherche Windows (« désinstaller »). Tes données restent (voir plus bas).
Name: "{autoprograms}\{#Nom}\Désinstaller {#Nom}"; Filename: "{uninstallexe}"; Comment: "Désinstaller {#Nom} (tes données sont gardées)"
Name: "{autodesktop}\{#Nom}"; Filename: "{app}\{#Exe}"; WorkingDir: "{app}"; Tasks: bureau

[Registry]
Root: HKA; Subkey: "Software\{#Nom}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\{#Nom}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"
Root: HKA; Subkey: "Software\{#Nom}"; ValueType: string; ValueName: "Version"; ValueData: "{#Version}"
Root: HKA; Subkey: "Software\{#Nom}"; ValueType: string; ValueName: "Portee"; ValueData: "{code:Portee}"
Root: HKA; Subkey: "Software\{#Nom}"; ValueType: string; ValueName: "DossierDonnees"; ValueData: "{code:DonneesBrut}"

[Run]
Filename: "{app}\{#Exe}"; Description: "{cm:Lancer}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\installation.ini"
Type: files; Name: "{code:DossierReserve}\{#Nom}-Setup-*.exe"
Type: dirifempty; Name: "{code:DossierReserve}"
Type: dirifempty; Name: "{app}"

[Code]
const
  CleLot8 = 'Software\XpgAnalyzer';
  Marque = '{#Nom}';

var
  PageDonnees: TInputDirWizardPage;
  PageReprise: TInputOptionWizardPage;
  PageAutre: TInputDirWizardPage;
  Sources: TArrayOfString;
  SourceLot8: Integer;
  OptionLot8: Integer;
  OptionAutre: Integer;
  DonneesDefautReel: String;
  JournalInstallation: String;
  SupprimerDonnees: Boolean;
  // Windows refuse de lancer les scripts PowerShell (une strategie de groupe : AllSigned...).
  PowerShellBloque: Boolean;
  NumeroEtape: Integer;
  // 1.8.0 : la page « Ta bibliothèque » (la liste : aide\xpgliste.dll) et la page
  // « Ce qui a été installé ».
  PageBiblio: TWizardPage;
  PageRecap: TOutputMsgMemoWizardPage;
  ComboRangement: TNewComboBox;
  TexteCompte: TNewStaticText;
  ListeOk: Boolean;
  MinuterieListe: LongWord;
  CategoriesLivrees: TStringList;
  RecapEcrit: Boolean;
  BoutonCopier: TNewButton;

// ---------------------------------------------- 1.8.0 : la liste des blocs ----
//  Une vraie ListView de Windows (aide\xpgliste.c) : setuponly et delayload - la DLL
//  n'est chargee qu'a la premiere page qui s'en sert, jamais par le desinstallateur.
function ListeCreer(Parent: HWND; X, Y, Largeur, Hauteur: Integer; Police: Integer; Notifier: HWND): Integer;
  external 'ListeCreer@files:xpgliste.dll stdcall setuponly delayload';
procedure ListeCategorieConnue(Categorie: String);
  external 'ListeCategorieConnue@files:xpgliste.dll stdcall setuponly delayload';
function ListeAjouterLivree(Nom, Categorie, TypeBloc, Version, Description: String): Integer;
  external 'ListeAjouterLivree@files:xpgliste.dll stdcall setuponly delayload';
function ListeAjouterChemin(Chemin: String): Integer;
  external 'ListeAjouterChemin@files:xpgliste.dll stdcall setuponly delayload';
function ListeRetirerSelection: Integer;
  external 'ListeRetirerSelection@files:xpgliste.dll stdcall setuponly delayload';
function ListeSelectionLivrees: Integer;
  external 'ListeSelectionLivrees@files:xpgliste.dll stdcall setuponly delayload';
function ListeNombreLivrees: Integer;
  external 'ListeNombreLivrees@files:xpgliste.dll stdcall setuponly delayload';
function ListeNombreAjoutees: Integer;
  external 'ListeNombreAjoutees@files:xpgliste.dll stdcall setuponly delayload';
function ListeAjoutee(N: Integer; Tampon: String; Taille: Integer): Integer;
  external 'ListeAjoutee@files:xpgliste.dll stdcall setuponly delayload';
// Le compteur de la page suit les depots (la liste les recoit seule) : une minuterie.
function SetTimer(Fenetre: HWND; Id, Periode, Rappel: LongWord): LongWord;
  external 'SetTimer@user32.dll stdcall setuponly';
function KillTimer(Fenetre: HWND; Id: LongWord): BOOL;
  external 'KillTimer@user32.dll stdcall setuponly';

// ------------------------------------------------------------------ outils ----
function Portee(Param: String): String;
begin
  if IsAdminInstallMode then Result := 'tous' else Result := 'utilisateur';
end;

function DossierEtat(Param: String): String;
begin
  if IsAdminInstallMode then Result := ExpandConstant('{commonappdata}\{#Nom}')
  else Result := ExpandConstant('{localappdata}\XpgAnalyzer');
end;

function DossierReserve(Param: String): String;
begin
  Result := DossierEtat('') + '\Reserve\{#Version}';
end;

// Remplace De par Vers, sans tenir compte des majuscules.
function RemplacerSansCasse(S, De, Vers: String): String;
var P: Integer;
begin
  Result := '';
  P := Pos(Lowercase(De), Lowercase(S));
  while P > 0 do begin
    Result := Result + Copy(S, 1, P - 1) + Vers;
    Delete(S, 1, P - 1 + Length(De));
    P := Pos(Lowercase(De), Lowercase(S));
  end;
  Result := Result + S;
end;

// Les %VARIABLES% de Windows (une variable inconnue reste telle quelle).
function DevelopperVariables(S: String): String;
var P, Q: Integer;
    Nom, Valeur: String;
begin
  Result := '';
  P := Pos('%', S);
  while P > 0 do begin
    Q := Pos('%', Copy(S, P + 1, Length(S)));
    if Q = 0 then Break;
    Nom := Copy(S, P + 1, Q - 1);
    Valeur := '';
    if Nom <> '' then Valeur := GetEnv(Nom);
    if Valeur = '' then begin
      Result := Result + Copy(S, 1, P + Q);
    end else begin
      Result := Result + Copy(S, 1, P - 1) + Valeur;
    end;
    Delete(S, 1, P + Q);
    P := Pos('%', S);
  end;
  Result := Result + S;
end;

// Les jetons des chemins comme l'application et outils\config.ini les comprennent :
// {Documents}, {AppData}, {LocalAppData}, {ProgramData}, {ProgramFiles}, {ProgramFiles(x86)},
// {pf}, {pf32} (sans tenir compte des majuscules), et les %VARIABLES%.
function DevelopperJetons(S: String): String;
begin
  Result := S;
  Result := RemplacerSansCasse(Result, '{Documents}', ExpandConstant('{userdocs}'));
  Result := RemplacerSansCasse(Result, '{LocalAppData}', ExpandConstant('{localappdata}'));
  Result := RemplacerSansCasse(Result, '{AppData}', ExpandConstant('{userappdata}'));
  Result := RemplacerSansCasse(Result, '{ProgramData}', ExpandConstant('{commonappdata}'));
  Result := RemplacerSansCasse(Result, '{ProgramFiles(x86)}', ExpandConstant('{commonpf32}'));
  Result := RemplacerSansCasse(Result, '{ProgramFiles}', ExpandConstant('{commonpf64}'));
  Result := RemplacerSansCasse(Result, '{pf32}', ExpandConstant('{commonpf32}'));
  Result := RemplacerSansCasse(Result, '{pf}', ExpandConstant('{commonpf64}'));
  Result := DevelopperVariables(Result);
end;

// Un ancien dossier de config.ini : les jetons ci-dessus, puis les constantes d'Inno Setup
// ({localappdata}...) ; un jeton inconnu fait ignorer ce dossier (noté dans le journal)
// au lieu d'arrêter l'installateur.
function DevelopperChemin(S: String): String;
begin
  Result := DevelopperJetons(S);
  if Pos('{', Result) > 0 then begin
    try
      Result := ExpandConstant(Result);
    except
      Log('Ancien dossier ignoré (jeton inconnu) : ' + S);
      Result := '';
    end;
  end;
end;

// La valeur a ecrire : le jeton si c'est le defaut (chaque compte a alors le sien),
// sinon le chemin choisi.
function DonneesBrut(Param: String): String;
var V: String;
begin
  V := ExpandConstant('{param:DONNEES|}');
  if V = '' then begin
    if PageDonnees <> nil then V := PageDonnees.Values[0] else V := DonneesDefautReel;
  end;
  if CompareText(RemoveBackslashUnlessRoot(V), RemoveBackslashUnlessRoot(DonneesDefautReel)) = 0 then
    Result := '{#DonneesDefaut}'
  else
    Result := RemoveBackslashUnlessRoot(V);
end;

function DonneesReel(Param: String): String;
begin
  Result := DevelopperJetons(DonneesBrut(''));
end;

function PowerShell: String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function Horodatage: String;
begin
  Result := GetDateTimeString('yyyy-mm-dd_hh-nn-ss', '-', '-');
end;

// Une etape PowerShell (installer_etape.ps1), cachee, attendue ; son code. Demarre : le script a
// vraiment demarre (il a ecrit sa marque) ; sinon, Windows l'a refuse (strategie de groupe).
function EtapePowerShell(Script, Arguments: String; var Code: Integer; var Demarre: Boolean): Boolean;
var Params, FichierMarque: String;
begin
  NumeroEtape := NumeroEtape + 1;
  FichierMarque := ExpandConstant('{tmp}') + '\xpg-marque-' + IntToStr(NumeroEtape) + '.txt';
  DeleteFile(FichierMarque);
  Params := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + Script + '" ' + Arguments +
            ' /journal:"' + JournalInstallation + '" /marque:"' + FichierMarque + '"';
  Log('PowerShell : ' + Params);
  Code := -1;
  Result := Exec(PowerShell, Params, '', SW_HIDE, ewWaitUntilTerminated, Code);
  Demarre := FileExists(FichierMarque);
  DeleteFile(FichierMarque);
  if Demarre then Log('PowerShell : code ' + IntToStr(Code))
  else Log('PowerShell : code ' + IntToStr(Code) + ' ; le script n''a pas démarré (Windows l''a refusé : stratégie de groupe ?)');
end;

function DejaDansLaListe(Chemin: String): Boolean;
var I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(Sources) - 1 do
    if CompareText(RemoveBackslashUnlessRoot(Sources[I]), RemoveBackslashUnlessRoot(Chemin)) = 0 then Result := True;
end;

// 1.8.0 : une ANCIENNE VERSION, c'est une version installee - son programme est la. Un
// dossier qui n'a que des projets (une copie des sources, un dossier build\) n'en est pas
// une : sur un PC vierge, il faisait apparaitre des projets. (Le lot 8, lui, est connu du
// registre ; un autre dossier peut toujours etre ajoute a la main.)
function AUneApplication(Dossier: String): Boolean;
begin
  Result := FileExists(Dossier + '\{#Exe}') or FileExists(Dossier + '\xpg_analyzer.exe');
end;

function AUnAncienContenu(Dossier: String): Boolean;
begin
  Result := DirExists(Dossier + '\projets') or DirExists(Dossier + '\libs') or DirExists(Dossier + '\captures') or
            DirExists(Dossier + '\build\Debug\projets') or DirExists(Dossier + '\build\Release\projets');
end;

procedure AjouterSource(Chemin: String; Lot8: Boolean);
var N: Integer;
begin
  Chemin := RemoveBackslashUnlessRoot(Chemin);
  if (Chemin = '') or DejaDansLaListe(Chemin) or not AUnAncienContenu(Chemin) then Exit;
  if not Lot8 and not AUneApplication(Chemin) then begin
    Log('Ancien dossier ignoré (pas d''application installée) : ' + Chemin);
    Exit;
  end;
  // Pas le dossier ou l'on installe (une mise a jour de XPGAnalyser n'est pas une ancienne version).
  if FileExists(Chemin + '\installation.ini') then Exit;
  N := GetArrayLength(Sources);
  SetArrayLength(Sources, N + 1);
  Sources[N] := Chemin;
  if Lot8 then SourceLot8 := N;
end;

procedure ChercherAnciennesVersions;
var S, Liste, Un: String;
    P: Integer;
begin
  SetArrayLength(Sources, 0);
  SourceLot8 := -1;
  if RegQueryStringValue(HKCU, CleLot8, 'InstallDir', S) then AjouterSource(S, True);
  Liste := '{#AnciensDossiers}';
  while Liste <> '' do begin
    P := Pos(';', Liste);
    if P = 0 then begin Un := Liste; Liste := ''; end
    else begin Un := Copy(Liste, 1, P - 1); Delete(Liste, 1, P); end;
    Un := Trim(Un);
    if Un <> '' then AjouterSource(DevelopperChemin(Un), False);
  end;
end;

function ListeSources: String;
var I: Integer;
begin
  Result := '';
  if PageReprise = nil then Exit;
  for I := 0 to GetArrayLength(Sources) - 1 do
    if PageReprise.Values[I] then begin
      if Result <> '' then Result := Result + '|';
      Result := Result + Sources[I];
    end;
  if (OptionAutre >= 0) and PageReprise.Values[OptionAutre] and (PageAutre <> nil) and (Trim(PageAutre.Values[0]) <> '') then begin
    if Result <> '' then Result := Result + '|';
    Result := Result + RemoveBackslashUnlessRoot(Trim(PageAutre.Values[0]));
  end;
end;

// ------------------------------------------- 1.8.0 : la page « Ta bibliothèque » ----
// Le n-ieme champ (0...) d'une ligne de libs\index.txt : kind ; category ; name ; version ; author ; comment
function Champ(L: String; N: Integer): String;
var P, K: Integer;
begin
  for K := 1 to N do begin
    P := Pos(';', L);
    if P = 0 then begin
      L := '';
      Break;
    end;
    Delete(L, 1, P);
  end;
  P := Pos(';', L);
  if (P > 0) and (N < 5) then Result := Trim(Copy(L, 1, P - 1)) else Result := Trim(L);
end;

function TypeAffiche(Kind: String): String;
begin
  if CompareText(Kind, 'dfb') = 0 then Result := 'DFB'
  else if CompareText(Kind, 'ddt') = 0 then Result := 'DDT'
  else Result := 'Macro';
end;

// Les blocs livres (l'index de la version installee), dans la liste ; leurs categories.
procedure ChargerBiblioLivree;
var Lignes: TArrayOfString;
    I: Integer;
    L: String;
begin
  CategoriesLivrees := TStringList.Create;
  CategoriesLivrees.Sorted := True;
  CategoriesLivrees.Duplicates := dupIgnore;
  ExtractTemporaryFile('libs-index.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\libs-index.txt'), Lignes) then begin
    Log('Ta bibliothèque : libs-index.txt illisible');
    Exit;
  end;
  for I := 0 to GetArrayLength(Lignes) - 1 do begin
    L := Trim(Lignes[I]);
    if (L = '') or (Copy(L, 1, 1) = '#') then Continue;
    if Champ(L, 2) = '' then Continue;
    CategoriesLivrees.Add(Champ(L, 1));
    ListeAjouterLivree(Champ(L, 2), Champ(L, 1), TypeAffiche(Champ(L, 0)), Champ(L, 3), Champ(L, 5));
  end;
  for I := 0 to CategoriesLivrees.Count - 1 do ListeCategorieConnue(CategoriesLivrees[I]);
end;

procedure MajCompteBiblio;
begin
  if (TexteCompte = nil) or not ListeOk then Exit;
  try
    TexteCompte.Caption := IntToStr(ListeNombreLivrees) + ' livrés · ' + IntToStr(ListeNombreAjoutees) + ' à toi';
  except
    Log('Ta bibliothèque : ' + GetExceptionMessage);
  end;
end;

// La minuterie (pendant que la page est a l'ecran) : un depot depuis l'Explorateur arrive
// dans la liste sans passer par l'assistant ; le compteur le suit.
procedure MinuterieBiblio(A1, A2, A3, A4: LongWord);
begin
  MajCompteBiblio;
end;

procedure AjouterFichiersClick(Sender: TObject);
var Liste: TStrings;
    I, N: Integer;
begin
  Liste := TStringList.Create;
  try
    if GetOpenFileNameMulti('Tes blocs à ajouter', Liste, '', 'Blocs de bibliothèque (*.dfb;*.ddt;*.mac)|*.dfb;*.ddt;*.mac|Tous les fichiers|*.*', 'dfb') then begin
      N := 0;
      for I := 0 to Liste.Count - 1 do N := N + ListeAjouterChemin(Liste[I]);
      if N < Liste.Count then
        MsgBox(IntToStr(Liste.Count - N) + ' fichier(s) laissé(s) de côté : pas un bloc (.dfb, .ddt, .mac), ou déjà dans la liste.', mbInformation, MB_OK);
    end;
  finally
    Liste.Free;
  end;
  MajCompteBiblio;
end;

procedure AjouterDossierClick(Sender: TObject);
var D: String;
begin
  D := '';
  if BrowseForFolder('Le dossier de tes blocs (ses sous-dossiers aussi) :', D, False) then
    if ListeAjouterChemin(D) = 0 then
      MsgBox('Aucun bloc de plus (.dfb, .ddt, .mac) dans ' + D + '.', mbInformation, MB_OK);
  MajCompteBiblio;
end;

procedure RetirerClick(Sender: TObject);
begin
  if ListeRetirerSelection = 0 then begin
    if ListeSelectionLivrees > 0 then
      MsgBox('Les blocs livrés restent : seuls les tiens (« Tes ajouts ») se retirent.', mbInformation, MB_OK)
    else
      MsgBox('Choisis d''abord, dans « Tes ajouts », les blocs à retirer.', mbInformation, MB_OK);
  end;
  MajCompteBiblio;
end;

// Les blocs ajoutes, pour l'etape PowerShell : "chemin<TAB>categorie" par ligne (UTF-8).
function FichierBlocsAjoutes: String;
var Lignes: TArrayOfString;
    I, N, T: Integer;
    Tampon, Ligne, Cat: String;
begin
  Result := '';
  if not ListeOk then Exit;
  try
    N := ListeNombreAjoutees;
  except
    N := 0;
  end;
  if N = 0 then Exit;
  SetArrayLength(Lignes, N);
  for I := 0 to N - 1 do begin
    Tampon := StringOfChar(' ', 1100);
    T := ListeAjoutee(I, Tampon, 1100);
    Ligne := Copy(Tampon, 1, T);
    // Le rangement choisi remplace celui d'apres le dossier (0 : d'apres le dossier).
    if (ComboRangement <> nil) and (ComboRangement.ItemIndex > 0) and (Pos(#9, Ligne) > 0) then begin
      Cat := ComboRangement.Items[ComboRangement.ItemIndex];
      Ligne := Copy(Ligne, 1, Pos(#9, Ligne)) + Cat;
    end;
    Lignes[I] := Ligne;
  end;
  Result := ExpandConstant('{tmp}') + '\libs-ajoutees.txt';
  if not SaveStringsToUTF8File(Result, Lignes, False) then Result := '';
end;

// ---------------------------------------- 1.8.0 : la page « Ce qui a été installé » ----
procedure Ajoute(var L: TArrayOfString; S: String);
var N: Integer;
begin
  N := GetArrayLength(L);
  SetArrayLength(L, N + 1);
  L[N] := S;
end;

function Colonne(S: String; Largeur: Integer): String;
begin
  Result := S;
  while Length(Result) < Largeur do Result := Result + ' ';
  Result := Result + ' ';
end;

function TailleDe(Fichier: String): String;
var T: Integer;
begin
  if not FileSize(Fichier, T) then begin
    Result := '-';
    Exit;
  end;
  if T >= 1048576 then Result := IntToStr(T div 1048576) + ',' + IntToStr((T mod 1048576) * 10 div 1048576) + ' Mo'
  else Result := IntToStr((T + 1023) div 1024) + ' Ko';
end;

function CompterSousDossiers(D: String): Integer;
var F: TFindRec;
begin
  Result := 0;
  if FindFirst(D + '\*', F) then begin
    try
      repeat
        if (F.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0) and (F.Name <> '.') and (F.Name <> '..') then Result := Result + 1;
      until not FindNext(F);
    finally
      FindClose(F);
    end;
  end;
end;

function LeWindows: String;
var V: TWindowsVersion;
begin
  GetWindowsVersionEx(V);
  if V.Build >= 22000 then Result := 'Windows 11' else Result := 'Windows 10';
  Result := Result + ' (build ' + IntToStr(V.Build) + ')';
  if IsARM64 then Result := Result + ', ARM64 : le programme x64 tourne en émulation'
  else if IsX64OS then Result := Result + ', 64 bits (x64)'
  else Result := Result + ', 32 bits';
end;

function RecapLignes: TArrayOfString;
var L: TArrayOfString;
    App, Don, Etat, Menu, Unins, S: String;
    N: Integer;
begin
  App := ExpandConstant('{app}');
  Don := DonneesReel('');
  Etat := DossierEtat('');
  Menu := ExpandConstant('{group}');
  Unins := ExpandConstant('{uninstallexe}');
  if IsAdminInstallMode then S := 'pour tous les comptes' else S := 'pour toi seul';
  Ajoute(L, '{#Nom} {#Version} · installé le ' + GetDateTimeString('dd/mm/yyyy à hh:nn', '/', ':') + ' · ' + S);
  Ajoute(L, 'Ce PC : ' + LeWindows);
  Ajoute(L, '');
  Ajoute(L, 'LES PROGRAMMES');
  Ajoute(L, '  ' + Colonne('{#Exe}', 17) + Colonne('le programme', 27) + Colonne('64 bits (x64)', 15) + TailleDe(App + '\{#Exe}'));
  Ajoute(L, '  ' + Colonne(ExtractFileName(Unins), 17) + Colonne('le désinstallateur', 27) + Colonne('32 bits (x86)', 15) + TailleDe(Unins));
  Ajoute(L, '  ' + Colonne('SDL3.dll', 17) + Colonne('fenêtres, affichage, son', 27) + Colonne('64 bits (x64)', 15) + TailleDe(App + '\SDL3.dll'));
  Ajoute(L, '  ' + Colonne('maintenance\', 17) + '6 scripts (.bat) et leurs étapes PowerShell :');
  Ajoute(L, '  ' + Colonne('', 17) + 'réparer, vérifier, reprendre, restaurer,');
  Ajoute(L, '  ' + Colonne('', 17) + 'journaux, réinitialiser les réglages');
  Ajoute(L, '  Rien d''autre à installer : tout est à côté de l''exe.');
  Ajoute(L, '');
  Ajoute(L, 'LE PROGRAMME   ' + App);
  Ajoute(L, '  ├─ ' + Colonne('{#Exe}', 32));
  Ajoute(L, '  ├─ ' + Colonne('SDL3.dll', 32));
  Ajoute(L, '  ├─ ' + Colonne(ExtractFileName(Unins) + ', .dat', 32) + 'le désinstallateur');
  Ajoute(L, '  ├─ ' + Colonne('installation.ini', 32) + 'écrit par l''installateur');
  Ajoute(L, '  ├─ ' + Colonne('manifeste.json', 32) + 'chaque fichier, sa taille, son SHA-256');
  Ajoute(L, '  ├─ ' + Colonne('libs\', 32) + 'la bibliothèque livrée');
  Ajoute(L, '  ├─ ' + Colonne('maintenance\', 32) + 'les scripts, lib\ (PowerShell)');
  Ajoute(L, '  ├─ ' + Colonne('resources\', 32) + 'le catalogue des automates');
  Ajoute(L, '  └─ ' + Colonne('licences\', 32) + 'SDL3, miniz, stb, nanosvg, minimp3');
  Ajoute(L, '');
  if IsAdminInstallMode then Ajoute(L, 'TES DONNÉES   Documents\{#Nom} de chaque compte (ici : ' + Don + ')')
  else Ajoute(L, 'TES DONNÉES   ' + Don);
  N := CompterSousDossiers(Don + '\projets');
  if N = 0 then S := 'vide : aucun projet' else S := IntToStr(N) + ' projet(s)';
  Ajoute(L, '  ├─ ' + Colonne('projets\', 32) + S);
  S := 'la bibliothèque';
  if ListeOk then
    try
      S := IntToStr(ListeNombreLivrees) + ' blocs livrés';
      if ListeNombreAjoutees > 0 then S := S + ' + ' + IntToStr(ListeNombreAjoutees) + ' à toi';
    except
    end;
  Ajoute(L, '  ├─ ' + Colonne('libs\', 32) + S);
  Ajoute(L, '  ├─ ' + Colonne('resources\', 32) + 'ton catalogue');
  Ajoute(L, '  └─ ' + Colonne('captures\', 32));
  Ajoute(L, '');
  Ajoute(L, 'LA MAINTENANCE   ' + Etat);
  Ajoute(L, '  ├─ ' + Colonne('Journaux\', 32) + ExtractFileName(JournalInstallation));
  Ajoute(L, '  ├─ ' + Colonne('Sauvegardes\', 32) + 'la version d''avant, s''il y en avait une');
  Ajoute(L, '  ├─ ' + Colonne('Reserve\{#Version}\', 32) + 'de quoi réparer sans Internet');
  Ajoute(L, '  └─ ' + Colonne('Etat\', 32));
  Ajoute(L, '');
  Ajoute(L, 'LE MENU DÉMARRER   ' + Menu);
  Ajoute(L, '  ├─ ' + Colonne('{#Nom}', 32));
  Ajoute(L, '  ├─ ' + Colonne('Désinstaller {#Nom}', 32) + 'la recherche Windows le trouve');
  if not IsAdminInstallMode then Ajoute(L, '  ├─ ' + Colonne('Tes données', 32));
  Ajoute(L, '  └─ ' + Colonne('Maintenance\', 32) + '6 raccourcis');
  Ajoute(L, '');
  if WizardIsTaskSelected('bureau') then S := '{#Nom}' else S := 'aucun raccourci';
  Ajoute(L, Colonne('LE BUREAU', 20) + S);
  if WizardIsTaskSelected('barre') then S := 'épinglage proposé au premier lancement' else S := 'rien (non demandé)';
  Ajoute(L, Colonne('LA BARRE DES TÂCHES', 20) + S);
  if IsAdminInstallMode then S := 'HKLM' else S := 'HKCU';
  Ajoute(L, Colonne('LE REGISTRE', 20) + S + '\Software\{#Nom}');
  Ajoute(L, Colonne('', 20) + 'InstallDir, Version, Portee, DossierDonnees');
  Ajoute(L, Colonne('', 20) + 'et la désinstallation (Paramètres > Applications)');
  Result := L;
end;

function Joindre(L: TArrayOfString): String;
var I: Integer;
begin
  Result := '';
  for I := 0 to GetArrayLength(L) - 1 do begin
    if I > 0 then Result := Result + #13#10;
    Result := Result + L[I];
  end;
end;

procedure EcrireRecap;
var L: TArrayOfString;
    F: String;
begin
  if RecapEcrit then Exit;
  RecapEcrit := True;
  L := RecapLignes;
  F := DossierEtat('') + '\Journaux\recapitulatif_' + Horodatage + '.txt';
  if not SaveStringsToUTF8File(F, L, False) then Log('Récapitulatif : écriture impossible (' + F + ')');
  if PageRecap <> nil then begin
    PageRecap.RichEditViewer.Lines.Text := Joindre(L);
    PageRecap.SubCaptionLabel.Caption := 'Aussi enregistré dans tes journaux : ' + ExtractFileName(F);
  end;
end;

procedure CopierRecapClick(Sender: TObject);
begin
  if PageRecap = nil then Exit;
  // Tout choisir, puis WM_COPY (le presse-papiers de Windows).
  SendMessage(PageRecap.RichEditViewer.Handle, $00B1 {EM_SETSEL}, 0, -1);
  SendMessage(PageRecap.RichEditViewer.Handle, $0301 {WM_COPY}, 0, 0);
  TNewButton(Sender).Caption := 'Copié';
end;

// --------------------------------------------------------------- l'assistant ----
function InitializeSetup: Boolean;
var Etat: AnsiString;
    F: String;
begin
  Result := True;
  DonneesDefautReel := DevelopperJetons('{#DonneesDefaut}');
  JournalInstallation := DossierEtat('') + '\Journaux\installation_' + Horodatage + '.log';
  ForceDirectories(ExtractFileDir(JournalInstallation));
  OptionLot8 := -1; OptionAutre := -1;
  // Une installation precedente interrompue : on le dit, elle est reprise. statut.txt ne
  // contient que le statut du moment (installation.json garde tout l'historique).
  F := DossierEtat('') + '\Etat\statut.txt';
  if FileExists(F) and LoadStringFromFile(F, Etat) and (Trim(Etat) <> '') and (Pos('terminee', Etat) <> 1) then begin
    Log('Installation precedente interrompue : ' + F);
    if not WizardSilent then
      MsgBox('Une installation précédente de {#Nom} a été interrompue avant la fin.' + #13#10 + #13#10 +
             'Celle-ci va la reprendre : les fichiers seront recopiés et vérifiés. Tes données ne sont pas touchées.',
             mbInformation, MB_OK);
  end;
end;

procedure CreerPageBiblio;
var Intro, Aide: TNewStaticText;
    BAjouter, BDossier, BRetirer: TNewButton;
    Haut, HListe, I: Integer;
begin
  PageBiblio := CreateCustomPage(PageAutre.ID, 'Ta bibliothèque', 'Les blocs livrés avec {#Nom}, et les tiens : DFB, DDT, macros.');
  ComboRangement := TNewComboBox.Create(PageBiblio);
  ComboRangement.Parent := PageBiblio.Surface;
  ComboRangement.Style := csDropDownList;
  ComboRangement.Width := ScaleX(170);
  ComboRangement.Left := PageBiblio.SurfaceWidth - ComboRangement.Width;
  ComboRangement.Top := 0;
  Intro := TNewStaticText.Create(PageBiblio);
  Intro.Parent := PageBiblio.Surface;
  Intro.AutoSize := False;
  Intro.WordWrap := True;
  Intro.Left := 0;
  Intro.Top := 0;
  Intro.Width := ComboRangement.Left - ScaleX(8);
  Intro.Height := ScaleY(28);
  Intro.Caption := 'Glisse ici tes fichiers .dfb, .ddt, .mac (ou un dossier entier), ou utilise Ajouter. Rangés (sinon : Perso) :';
  Haut := ScaleY(32);
  HListe := PageBiblio.SurfaceHeight - Haut - ScaleY(50);
  BAjouter := TNewButton.Create(PageBiblio);
  BAjouter.Parent := PageBiblio.Surface;
  BAjouter.Caption := '&Ajouter des fichiers...';
  BAjouter.Left := 0;
  BAjouter.Top := Haut + HListe + ScaleY(6);
  BAjouter.Width := ScaleX(130);
  BAjouter.Height := ScaleY(23);
  BAjouter.OnClick := @AjouterFichiersClick;
  BDossier := TNewButton.Create(PageBiblio);
  BDossier.Parent := PageBiblio.Surface;
  BDossier.Caption := 'Ajouter un &dossier...';
  BDossier.Left := BAjouter.Left + BAjouter.Width + ScaleX(6);
  BDossier.Top := BAjouter.Top;
  BDossier.Width := ScaleX(130);
  BDossier.Height := ScaleY(23);
  BDossier.OnClick := @AjouterDossierClick;
  BRetirer := TNewButton.Create(PageBiblio);
  BRetirer.Parent := PageBiblio.Surface;
  BRetirer.Caption := 'Re&tirer';
  BRetirer.Left := BDossier.Left + BDossier.Width + ScaleX(6);
  BRetirer.Top := BAjouter.Top;
  BRetirer.Width := ScaleX(80);
  BRetirer.Height := ScaleY(23);
  BRetirer.OnClick := @RetirerClick;
  TexteCompte := TNewStaticText.Create(PageBiblio);
  TexteCompte.Parent := PageBiblio.Surface;
  TexteCompte.AutoSize := False;
#if Ver >= EncodeVer(6, 5, 0)
  TexteCompte.Alignment := taRightJustify;   { 1.11.3 : Inno Setup 6.5 et plus ; avant, a gauche }
#endif
  TexteCompte.Left := BRetirer.Left + BRetirer.Width + ScaleX(6);
  TexteCompte.Width := PageBiblio.SurfaceWidth - TexteCompte.Left;
  TexteCompte.Top := BAjouter.Top + ScaleY(4);
  TexteCompte.Height := ScaleY(16);
  Aide := TNewStaticText.Create(PageBiblio);
  Aide.Parent := PageBiblio.Surface;
  Aide.AutoSize := False;
  Aide.WordWrap := True;
  Aide.Left := 0;
  Aide.Top := BAjouter.Top + ScaleY(27);
  Aide.Width := PageBiblio.SurfaceWidth;
  Aide.Height := ScaleY(16);
  Aide.Font.Color := clGray;
  Aide.Caption := 'Même nom qu''un bloc livré : le tien est gardé, le livré rangé à côté.';

  ComboRangement.Items.Add('d''après leur dossier');
  ComboRangement.Items.Add('Perso');
  ListeOk := False;
  try
    ListeOk := ListeCreer(PageBiblio.Surface.Handle, 0, Haut, PageBiblio.SurfaceWidth, HListe, WizardForm.Font.Handle, WizardForm.Handle) = 1;
    if ListeOk then ChargerBiblioLivree;
  except
    ListeOk := False;
    Log('Ta bibliothèque : la liste ne s''affiche pas (' + GetExceptionMessage + ')');
  end;
  if CategoriesLivrees <> nil then
    for I := 0 to CategoriesLivrees.Count - 1 do ComboRangement.Items.Add(CategoriesLivrees[I]);
  ComboRangement.ItemIndex := 0;
  if not ListeOk then begin
    // Sans la liste (la DLL refusee, un Windows sans elle) : l'installation continue ;
    // les blocs s'ajoutent aussi plus tard, dans l'application.
    Intro.Height := ScaleY(60);
    Intro.Caption := 'La liste des blocs ne peut pas s''afficher sur ce PC. La bibliothèque livrée est installée ; '
                   + 'tes blocs s''ajoutent aussi plus tard, dans {#Nom} (Bibliothèque).';
    ComboRangement.Visible := False;
    BAjouter.Enabled := False;
    BDossier.Enabled := False;
    BRetirer.Enabled := False;
  end;
  MajCompteBiblio;
end;

procedure InitializeWizard;
var I: Integer;
    S, Precedent: String;
begin
  // ---- Tes donnees ----
  S := 'Ce dossier reste à toi : une mise à jour, une réparation ou une désinstallation ne l''effacent jamais sans ta confirmation.' + #13#10 +
       'Il ne peut pas être dans le dossier du programme. Tu pourras le changer plus tard : accueil de {#Nom} > Dossiers.';
  if IsAdminInstallMode then
    S := S + #13#10 + #13#10 + 'Installation pour tous les comptes : avec le dossier proposé, chaque compte Windows a le sien, dans ses propres Documents.';
  PageDonnees := CreateInputDirPage(wpSelectDir, 'Tes données', 'Où ranger tes projets, ta bibliothèque, ton catalogue et tes captures ?', S, False, '{#Nom}');
  PageDonnees.Add('Dossier des données :');
  PageDonnees.Values[0] := DonneesDefautReel;
  if RegQueryStringValue(HKA, 'Software\{#Nom}', 'DossierDonnees', Precedent) and (Precedent <> '') then
    PageDonnees.Values[0] := DevelopperJetons(Precedent);

  // ---- Reprendre tes anciennes donnees ----
  ChercherAnciennesVersions;
  PageReprise := CreateInputOptionPage(PageDonnees.ID, 'Reprendre tes anciennes données',
    'Trouvées sur ce PC. Elles sont copiées dans ton dossier des données ; les originaux ne sont pas modifiés.',
    'Coche ce qu''il faut reprendre (projets, bibliothèque, catalogue, captures). Un fichier de bibliothèque que tu as modifié garde ta version ; la version livrée est rangée à côté.',
    False, False);
  for I := 0 to GetArrayLength(Sources) - 1 do begin
    if I = SourceLot8 then S := 'Installation du lot 8 : ' else S := 'Ancien dossier : ';
    PageReprise.Add(S + Sources[I]);
    PageReprise.Values[I] := True;
  end;
  if SourceLot8 >= 0 then begin
    OptionLot8 := PageReprise.Add('Puis désinstaller la version du lot 8 (ses fichiers de programme seulement)');
    PageReprise.Values[OptionLot8] := True;
  end;
  OptionAutre := PageReprise.Add('Ajouter un autre dossier (une version portable, par exemple)');
  PageReprise.Values[OptionAutre] := False;

  PageAutre := CreateInputDirPage(PageReprise.ID, 'Un autre ancien dossier',
    'Le dossier d''une ancienne version : celui qui contient projets\ et libs\.', '', False, '');
  PageAutre.Add('Dossier :');

  // ---- 1.8.0 : Ta bibliothèque ----
  CreerPageBiblio;
  // ---- 1.8.0 : Ce qui a été installé (après la copie) ----
  PageRecap := CreateOutputMsgMemoPage(wpInstalling, 'Ce qui a été installé',
    'Les programmes, leur architecture, et où se trouve chaque chose.', 'Le même texte est enregistré dans tes journaux.', '');
  PageRecap.RichEditViewer.Font.Name := 'Consolas';
  PageRecap.RichEditViewer.Font.Size := 9;
  PageRecap.RichEditViewer.WordWrap := False;
  PageRecap.RichEditViewer.ScrollBars := ssBoth;
  BoutonCopier := TNewButton.Create(WizardForm);
  BoutonCopier.Parent := WizardForm;
  BoutonCopier.Caption := '&Copier';
  BoutonCopier.Left := WizardForm.ClientWidth - WizardForm.CancelButton.Left - WizardForm.CancelButton.Width;   // la marge de droite, a gauche
  if BoutonCopier.Left < ScaleX(10) then BoutonCopier.Left := ScaleX(10);
  BoutonCopier.Top := WizardForm.NextButton.Top;
  BoutonCopier.Width := WizardForm.NextButton.Width;
  BoutonCopier.Height := WizardForm.NextButton.Height;
  BoutonCopier.OnClick := @CopierRecapClick;
  BoutonCopier.Visible := False;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // La page « Ta bibliothèque » : le compteur suit les depots (minuterie tant qu'elle est la).
  if (PageBiblio <> nil) and (CurPageID = PageBiblio.ID) and ListeOk then begin
    if MinuterieListe = 0 then MinuterieListe := SetTimer(0, 0, 400, CreateCallback(@MinuterieBiblio));
    MajCompteBiblio;
  end else if MinuterieListe <> 0 then begin
    KillTimer(0, MinuterieListe);
    MinuterieListe := 0;
  end;
  if BoutonCopier <> nil then BoutonCopier.Visible := (PageRecap <> nil) and (CurPageID = PageRecap.ID);
  // Installe : la fin ne revient pas en arriere.
  if CurPageID = wpFinished then WizardForm.BackButton.Visible := False;
  if (PageRecap <> nil) and (CurPageID = PageRecap.ID) then begin
    EcrireRecap;
    // Installe : plus de retour, plus d'annulation ; Suivant mene a la fin.
    WizardForm.BackButton.Visible := False;
    WizardForm.CancelButton.Visible := False;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  // 1.8.0 : sur un PC vierge (aucune version installee trouvee), pas de page de reprise :
  // rien a reprendre, aucun projet. (Un ancien dossier se reprend aussi plus tard :
  // Maintenance > Reprendre une installation interrompue, /migrer:"D:\ancien".)
  if (PageReprise <> nil) and (PageID = PageReprise.ID) then Result := GetArrayLength(Sources) = 0;
  if (PageAutre <> nil) and (PageID = PageAutre.ID) then Result := (GetArrayLength(Sources) = 0) or not PageReprise.Values[OptionAutre];
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var D, App, Essai: String;
begin
  Result := True;
  if CurPageID = PageDonnees.ID then begin
    D := RemoveBackslashUnlessRoot(Trim(PageDonnees.Values[0]));
    App := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
    if D = '' then begin MsgBox('Choisis un dossier pour tes données.', mbError, MB_OK); Result := False; Exit; end;
    if (CompareText(D, App) = 0) or (Pos(Lowercase(App) + '\', Lowercase(D) + '\') = 1) then begin
      MsgBox('Tes données ne peuvent pas être dans le dossier du programme (' + App + ') : il est remplacé à chaque mise à jour.', mbError, MB_OK);
      Result := False; Exit;
    end;
    if not ForceDirectories(D) then begin MsgBox('Impossible de créer ' + D + '.', mbError, MB_OK); Result := False; Exit; end;
    Essai := D + '\.xpg-essai.tmp';
    if not SaveStringToFile(Essai, 'essai', False) then begin
      MsgBox('Impossible d''écrire dans ' + D + ' : choisis un dossier où tu as le droit d''écrire.', mbError, MB_OK);
      Result := False; Exit;
    end;
    DeleteFile(Essai);
  end;
  if (PageAutre <> nil) and (CurPageID = PageAutre.ID) then begin
    D := Trim(PageAutre.Values[0]);
    if (D = '') or not DirExists(D) or not AUnAncienContenu(D) then begin
      MsgBox('Ce dossier n''a ni projets\, ni libs\, ni captures\ : ce n''est pas une ancienne version de {#Nom}.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// Le resume de la page « Pret a installer » : des lignes courtes (le cadre ne les coupe pas).
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var S, L, Prec: String;
begin
  S := '';
  if IsAdminInstallMode then S := S + 'Pour : tous les comptes de ce PC' + NewLine else S := S + 'Pour : toi seul (sans droits d''administrateur)' + NewLine;
  S := S + NewLine + MemoDirInfo + NewLine;
  S := S + NewLine + 'Tes données :' + NewLine + Space + DonneesReel('') + NewLine;
  L := ListeSources;
  if L <> '' then begin
    StringChangeEx(L, '|', NewLine + Space, True);
    S := S + NewLine + 'Données reprises (copiées) depuis :' + NewLine + Space + L + NewLine;
    if (OptionLot8 >= 0) and PageReprise.Values[OptionLot8] and (SourceLot8 >= 0) and PageReprise.Values[SourceLot8] then
      S := S + Space + 'puis la version du lot 8 est désinstallée' + NewLine + Space + '(son programme seulement ; tes données restent).' + NewLine;
  end;
  // 1.8.0 : la bibliotheque (livree, et les blocs ajoutes a la page Ta bibliotheque).
  if ListeOk then
    try
      S := S + NewLine + 'Ta bibliothèque :' + NewLine + Space + IntToStr(ListeNombreLivrees) + ' blocs livrés (DFB, DDT, macros)' + NewLine;
      if ListeNombreAjoutees > 0 then begin
        S := S + Space + '+ ' + IntToStr(ListeNombreAjoutees) + ' à toi, copiés dans ta bibliothèque' + NewLine;
        if (ComboRangement <> nil) and (ComboRangement.ItemIndex > 0) then
          S := S + Space + '(rangés dans libs\' + ComboRangement.Items[ComboRangement.ItemIndex] + ')' + NewLine;
      end;
    except
    end;
  if RegQueryStringValue(HKA, 'Software\{#Nom}', 'Version', Prec) and (Prec <> '') then
    S := S + NewLine + 'Mise à jour de la version ' + Prec + ' :' + NewLine +
         Space + 'elle est d''abord sauvegardée (programme, réglages, registre).' + NewLine;
  if MemoTasksInfo <> '' then S := S + NewLine + MemoTasksInfo;
  // 1.8.0 : toujours, dans le menu Demarrer (la recherche Windows le trouve).
  S := S + NewLine + 'Menu Démarrer :' + NewLine + Space + '{#Nom}, Désinstaller {#Nom}, Maintenance' + NewLine;
  Result := S;
end;

// --------------------------------------------------------------- installer ----
function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer;
    Lance, Demarre: Boolean;
begin
  Result := '';
  ExtractTemporaryFiles('{tmp}\xpg\lib\*');
  ExtractTemporaryFiles('{tmp}\xpg\config.ini');
  if not FileExists(PowerShell) then begin
    Result := 'Windows PowerShell 5.1 est introuvable (' + PowerShell + ') : il fait partie de Windows 10 et 11.';
    Exit;
  end;
  Lance := EtapePowerShell(ExpandConstant('{tmp}\xpg\lib\installer_etape.ps1'),
       '/etape:avant-copie /dossier:"' + ExpandConstant('{app}') + '" /version:{#Version} /etat:"' + DossierEtat('') + '"', Code, Demarre);
  if not Demarre then begin
    // Une strategie de groupe interdit les scripts (ou PowerShell lui-meme : AppLocker, SRP ; Exec
    // echoue alors) : l'installation se fait quand meme (le programme ; l'application prepare
    // elle-meme tes donnees au premier lancement), sans les etapes PowerShell. Rien n'est contourne.
    if not Lance then Log('PowerShell n''a pas pu être lancé : ' + SysErrorMessage(Code) + ' (' + IntToStr(Code) + ')');
    PowerShellBloque := True;
    if not WizardSilent then
      MsgBox('Windows empêche PowerShell de lancer les étapes de l''installation (une stratégie de groupe, sans doute).' + #13#10 + #13#10 +
             '{#Nom} va quand même s''installer, mais sans la sauvegarde de la version déjà installée, sans la reprise des anciennes données et sans la vérification des fichiers. Tes données ne sont pas touchées.' + #13#10 + #13#10 +
             'Pour avoir ces étapes, demande à l''administrateur du PC d''autoriser les scripts, puis relance cet installateur.',
             mbInformation, MB_OK);
    Exit;
  end;
  if Code <> 0 then
    Result := 'La sauvegarde de la version installée n''a pas pu être faite (code ' + IntToStr(Code) + '). Rien n''a été modifié.' + #13#10 +
              'Journal : ' + JournalInstallation;
end;

procedure EcrireInstallationIni;
var L: TArrayOfString;
    Bureau, Barre: String;
begin
  if WizardIsTaskSelected('bureau') then Bureau := 'oui' else Bureau := 'non';
  // 1.8.0 : l'application demande l'epinglage a Windows au premier lancement (une fois par compte).
  if WizardIsTaskSelected('barre') then Barre := 'oui' else Barre := 'non';
  SetArrayLength(L, 22);
  L[0] := '; installation.ini - écrit par l''installateur de {#Nom} {#Version}. Ne pas modifier :';
  L[1] := '; tes dossiers se changent dans %APPDATA%\XpgAnalyzer\XPGAnalyser.ini (accueil > Dossiers).';
  L[2] := '[Installation]';
  L[3] := 'nom = {#Nom}';
  L[4] := 'version = {#Version}';
  L[5] := 'portee = ' + Portee('');
  L[6] := 'date = ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':');
  L[7] := 'exe = {#Exe}';
  L[8] := 'chaine = {#Chaine}';
  L[9] := 'raccourci_bureau = ' + Bureau;
  L[10] := 'epingler_barre_taches = ' + Barre;
  L[11] := '';
  L[12] := '[Dossiers]';
  L[13] := '; Le dossier des données par défaut ({Documents} : celui de chaque compte).';
  L[14] := 'donnees = ' + DonneesBrut('');
  L[15] := '';
  L[16] := '[Maintenance]';
  L[17] := 'etat = ' + DossierEtat('');
  L[18] := 'journaux = ' + DossierEtat('') + '\Journaux';
  L[19] := 'sauvegardes = ' + DossierEtat('') + '\Sauvegardes';
  L[20] := 'reserve = ' + DossierEtat('') + '\Reserve';
  L[21] := '';
  if not SaveStringsToUTF8File(ExpandConstant('{app}\installation.ini'), L, False) then
    Log('installation.ini : ecriture impossible');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Code: Integer;
    Sources2, Lot8, Migrer, Setup: String;
    Demarre: Boolean;
begin
  // 1.8.0 : sans assistant (/VERYSILENT), le recapitulatif va quand meme dans les journaux.
  if CurStep = ssDone then begin
    EcrireRecap;
    Exit;
  end;
  if CurStep <> ssPostInstall then Exit;
  EcrireInstallationIni;
  // L'installateur lui-meme dans la reserve : repair.bat (reinstaller) et la reprise s'en servent.
  Setup := ExpandConstant('{srcexe}');
  if not FileCopy(Setup, DossierReserve('') + '\' + ExtractFileName(Setup), False) then Log('Copie de l''installateur dans la reserve : echec');
  if PowerShellBloque then begin
    Log('Etape apres-copie non lancee : Windows refuse les scripts PowerShell (voir plus haut).');
    Exit;
  end;
  // Les donnees, la reprise des anciennes versions, la verification.
  Sources2 := ListeSources;
  Migrer := Lowercase(ExpandConstant('{param:MIGRER|}'));
  if Migrer = 'non' then Sources2 := ''
  else if (Migrer = 'oui') and WizardSilent then begin
    ChercherAnciennesVersions;
    Sources2 := '';
    for Code := 0 to GetArrayLength(Sources) - 1 do begin
      if Sources2 <> '' then Sources2 := Sources2 + '|';
      Sources2 := Sources2 + Sources[Code];
    end;
  end;
  Lot8 := 'non';
  if (OptionLot8 >= 0) and (PageReprise <> nil) and PageReprise.Values[OptionLot8] and (SourceLot8 >= 0) and PageReprise.Values[SourceLot8] then Lot8 := 'oui';
  if Lowercase(ExpandConstant('{param:DESINSTALLERLOT8|}')) = 'non' then Lot8 := 'non';
  WizardForm.StatusLabel.Caption := 'Tes données, la reprise des anciennes versions, la vérification...';
  if not EtapePowerShell(ExpandConstant('{app}\maintenance\lib\installer_etape.ps1'),
       '/etape:apres-copie /dossier:"' + ExpandConstant('{app}') + '" /sources:"' + Sources2 + '" /desinstaller-lot8:' + Lot8
       + ' /libs-ajoutees:"' + FichierBlocsAjoutes + '"', Code, Demarre) or (Code <> 0) then begin
    Log('Etape apres-copie : echec, code ' + IntToStr(Code));
    if not WizardSilent then
      MsgBox('{#Nom} est installé, mais une étape n''a pas abouti (tes données, la reprise ou la vérification ; code ' + IntToStr(Code) + ').' + #13#10 + #13#10 +
             'Tes anciennes données n''ont pas été touchées. Pour finir : menu Démarrer > {#Nom} > Maintenance > Reprendre une installation interrompue.' + #13#10 + #13#10 +
             'Journal : ' + JournalInstallation, mbError, MB_OK);
  end;
end;

// --------------------------------------------------------------- desinstaller ----
function ConfirmerSuppression: Boolean;
var F: TSetupForm;
    Texte: TNewStaticText;
    Saisie: TNewEdit;
    Ok, Annuler: TNewButton;
begin
  Result := False;
#if Ver >= EncodeVer(6, 5, 0)
  F := CreateCustomForm(ScaleX(460), ScaleY(170), False, False);
#else
  F := CreateCustomForm;   { 1.11.3 : Inno Setup 6.4 - la taille ensuite }
  F.ClientWidth := ScaleX(460);
  F.ClientHeight := ScaleY(170);
#endif
  try
    F.Caption := 'Supprimer tes données';
    Texte := TNewStaticText.Create(F);
    Texte.Parent := F;
    Texte.Left := ScaleX(12); Texte.Top := ScaleY(12); Texte.Width := F.ClientWidth - ScaleX(24);
    Texte.AutoSize := False; Texte.WordWrap := True; Texte.Height := ScaleY(70);
    Texte.Caption := 'Tes projets, ta bibliothèque, tes captures et tes réglages iront dans la Corbeille.' + #13#10 +
                     'Pour confirmer, tape SUPPRIMER en majuscules :';
    Saisie := TNewEdit.Create(F);
    Saisie.Parent := F;
    Saisie.Left := ScaleX(12); Saisie.Top := ScaleY(88); Saisie.Width := F.ClientWidth - ScaleX(24);
    Ok := TNewButton.Create(F);
    Ok.Parent := F; Ok.Caption := 'Supprimer'; Ok.ModalResult := mrOk;
    Ok.Left := F.ClientWidth - ScaleX(200); Ok.Top := ScaleY(128); Ok.Width := ScaleX(90); Ok.Height := ScaleY(26);
    Annuler := TNewButton.Create(F);
    Annuler.Parent := F; Annuler.Caption := 'Garder'; Annuler.ModalResult := mrCancel; Annuler.Cancel := True; Annuler.Default := True;
    Annuler.Left := F.ClientWidth - ScaleX(102); Annuler.Top := ScaleY(128); Annuler.Width := ScaleX(90); Annuler.Height := ScaleY(26);
    if F.ShowModal() = mrOk then Result := (Saisie.Text = 'SUPPRIMER');
  finally
    F.Free();
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := True;
  SupprimerDonnees := False;
  if Lowercase(ExpandConstant('{param:SUPPRIMERDONNEES|}')) = 'oui' then begin SupprimerDonnees := True; Exit; end;
  if UninstallSilent then Exit;
  if MsgBox('Désinstaller {#Nom} garde tes données (projets, bibliothèque, captures) et tes réglages.' + #13#10 + #13#10 +
            'Les garder ? (conseillé)' + #13#10 + '(Non : tu pourras choisir de les supprimer, après une confirmation.)',
            mbConfirmation, MB_YESNO) = IDNO then begin
    SupprimerDonnees := ConfirmerSuppression;
    if not SupprimerDonnees then MsgBox('Tes données sont gardées.', mbInformation, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Code: Integer;
    Choix: String;
    Demarre: Boolean;
begin
  if CurUninstallStep <> usUninstall then Exit;
  JournalInstallation := DossierEtat('') + '\Journaux\desinstallation_' + Horodatage + '.log';
  ForceDirectories(ExtractFileDir(JournalInstallation));
  if SupprimerDonnees then Choix := 'oui' else Choix := 'non';
  Demarre := False;
  if FileExists(ExpandConstant('{app}\maintenance\lib\installer_etape.ps1')) then
    EtapePowerShell(ExpandConstant('{app}\maintenance\lib\installer_etape.ps1'),
      '/etape:desinstallation /dossier:"' + ExpandConstant('{app}') + '" /supprimer-donnees:' + Choix, Code, Demarre);
  if not Demarre then begin
    // Sans PowerShell (strategie de groupe) : l'etat de la maintenance part quand meme ; tes
    // donnees restent (la Corbeille demande PowerShell), et on te le dit si tu voulais les supprimer.
    DelTree(DossierEtat('') + '\Etat', True, True, True);
    if SupprimerDonnees and not UninstallSilent then
      MsgBox('Windows empêche PowerShell de lancer les scripts : tes données n''ont pas été mises à la Corbeille.' + #13#10 + #13#10 +
             'Supprime-les toi-même si tu le veux : ton dossier des données (Documents\XPGAnalyser par défaut) et ' + ExpandConstant('{userappdata}') + '\XpgAnalyzer.',
             mbInformation, MB_OK);
  end;
end;
