; =============================================================================
;  installateur/XPG-License-Admin.iss - 1.40.0 : L'INSTALLATEUR DE L'OUTIL DE
;  L'EDITEUR (XPG License Manager Admin) - un seul .exe a la place du zip
; -----------------------------------------------------------------------------
;  JAMAIS LIVRE A UN CLIENT : il porte l'application de l'editeur (celle qui signe
;  les licences). Construit par outils/mingw/admin_editeur.sh, a part, a cote du
;  dossier editeur-<version> (jamais dans dist/).
;
;  Ce qu'il installe (la page << Ce que tu installes >>, ses composants) :
;    l'application    XPG-License-Manager-Admin.exe, SDL3.dll (toujours) et les
;                     exemples (registre.json, parametres-exemple.json)
;    la console       XPG-License-Admin.exe (les memes commandes, pour les scripts)
;    le service       le kit de mise en ligne du service des licences, dans
;                     %LOCALAPPDATA%\XPG\ServiceLicences (un dossier ou l'on ecrit :
;                     cle-publique.txt, node_modules ; garde d'une version a l'autre).
;                     1.47.0 : TOUJOURS installe (comme l'application) : l'admin+
;                     s'en sert, et un admin pendant une derogation courte << Gerer
;                     le service >> sans reinstaller (docs/ADMINS.md, par. 11)
;    la connexion     1.47.0 : connexion-service.xpgsvc (le certificat du service,
;                     signe par la cle de l'editeur : son adresse et sa cle ; rien de
;                     secret) copie a cote du programme - celui que porte
;                     l'installateur (admin_editeur.sh : ressources-licences/), puis
;                     celui pose a cote de l'installateur ({src}, un navigateur
;                     peut l'avoir renomme << connexion-service (1).xpgsvc >>).
;                     L'Admin le lit au premier lancement et le verifie : l'Admin
;                     d'un futur admin demarre connecte au service de l'editeur.
;    les notes        LISEZ-MOI-EDITEUR.txt
;  Les options (<< Les options >>) : un raccourci sur le Bureau, ouvrir les .xpgreq,
;  .xpglic, .xpgreg, .xpgrev d'un double-clic, la console dans le PATH.
;  La cle de l'editeur, l'archive et les reglages (%LOCALAPPDATA%\XPG\LicenseAdmin) ne
;  sont JAMAIS touches : ni a l'installation, ni a la desinstallation (la page << Ta cle
;  et ton archive >> dit ce qui est la).
;  Pour toi seul par defaut (%LOCALAPPDATA%\Programs, sans droits d'administrateur) ;
;  pour tous les comptes : le dialogue du debut, ou /ALLUSERS.
;  En silencieux : /VERYSILENT /SUPPRESSMSGBOXES [/COMPONENTS=...] [/TASKS=...].
;
;  La compilation (admin_editeur.sh) :
;    ISCC /DVersion=1.40.0 /DSource=<dossier editeur-version> /DSortie=<dossier> XPG-License-Admin.iss
; =============================================================================
#define Nom "XPG License Manager Admin"
#define Editeur "XPG"
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #error "/DSource=<le dossier editeur-version fait par admin_editeur.sh> manque"
#endif
#ifndef Sortie
  #define Sortie "."
#endif
#define App "XPG-License-Manager-Admin.exe"
#define Console "XPG-License-Admin.exe"
#define ProgIdDemande "XPG.LicenseAdmin.Demande"
#define ProgIdLicence "XPG.LicenseAdmin.Licence"
#define ProgIdRegistre "XPG.LicenseAdmin.Registre"
#define ProgIdRevocations "XPG.LicenseAdmin.Revocations"
; 1.47.0 : l'installateur porte-t-il la connexion au service (admin_editeur.sh l'a mise dans le dossier) ?
#if FileExists(AddBackslash(Source) + "connexion-service.xpgsvc")
  #define AvecConnexion
#endif

[Setup]
AppId={{6F1C2B7E-4A9D-4E51-9C3B-2D8A40E1F5A7}
AppName={#Nom}
AppVersion={#Version}
AppVerName={#Nom} {#Version}
AppPublisher={#Editeur}
AppComments=L'outil de l'éditeur pour les licences (jamais livré à un client)
AppCopyright={#Editeur}
VersionInfoVersion={#Version}.0
VersionInfoProductName={#Nom}
VersionInfoProductVersion={#Version}
VersionInfoDescription=Installation de {#Nom} {#Version}
VersionInfoCompany={#Editeur}
DefaultDirName={code:DossierProgrammeDefaut}
DefaultGroupName={#Nom}
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
UsePreviousTasks=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UsePreviousPrivileges=yes
UsedUserAreasWarning=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
WizardResizable=no
DisableWelcomePage=no
WizardImageFile=admin\grande.bmp,admin\grande-2x.bmp
WizardSmallImageFile=admin\petite.bmp,admin\petite-2x.bmp
SetupIconFile=..\resources\logo\licences_admin.ico
UninstallDisplayIcon={app}\{#App}
UninstallDisplayName={#Nom}
OutputDir={#Sortie}
OutputBaseFilename=XPG-License-Admin-Setup-{#Version}-EDITEUR
Compression=lzma2/ultra
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
ChangesEnvironment=yes
SetupLogging=yes
ShowLanguageDialog=no
LanguageDetectionMethod=none
AlwaysShowDirOnReadyPage=yes
AlwaysShowComponentsList=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
WelcomeLabel1=Bienvenue dans l'installation de l'Admin
WelcomeLabel2=Tout est dans ce seul fichier : l'application de l'éditeur, sa console et de quoi mettre en ligne ton service des licences.%n%nTa clé d'éditeur, ton archive et tes réglages restent sur ce PC, à leur place : l'installation n'y touche jamais, la désinstallation non plus.%n%nTu avais l'ancien dossier du zip (editeur-...) ? Une fois l'Admin installé, tu peux le supprimer : plus rien n'en sert.
ClickNext=Clique sur Suivant pour continuer, ou sur Annuler pour quitter l'assistant.
ClickFinish=Clique sur Terminer pour fermer l'assistant.
ClickInstall=Clique sur Installer pour commencer, ou sur Précédent pour changer un choix.
SelectDirDesc=Où installer l'Admin ?
SelectDirBrowseLabel=Pour continuer, clique sur Suivant. Pour choisir un autre dossier, clique sur Parcourir.
DiskSpaceMBLabel=Il faut au moins [mb] Mo d'espace libre.
WizardSelectComponents=Ce que tu installes
SelectComponentsDesc=L'application est toujours installée ; le reste, à ton choix.
SelectComponentsLabel2=Coche ce que tu veux sur ce PC, puis clique sur Suivant.
ComponentsDiskSpaceMBLabel=Il faut au moins [mb] Mo d'espace libre.
WizardSelectTasks=Les options
SelectTasksDesc=Les raccourcis, les fichiers qu'elle ouvre, la console pour tes scripts.
SelectTasksLabel2=Coche ce que tu veux, puis clique sur Suivant. Une mise à jour reprend tes choix d'avant.
WizardReady=Récapitulatif
ReadyLabel1=Tout est prêt : relis le résumé avant d'installer.
ReadyLabel2a=Clique sur Installer pour commencer, ou sur Précédent pour changer un choix.
ReadyMemoComponents=Ce que tu installes :
ReadyMemoTasks=Les options :
InstallingLabel=Copie des fichiers de l'Admin.
PreparingDesc=L'assistant prépare l'installation de l'Admin sur ton PC.
ExitSetupMessage=L'installation n'est pas terminée : si tu quittes maintenant, l'Admin ne sera pas installé.%n%nTu pourras relancer l'assistant plus tard.%n%nQuitter l'assistant ?
FinishedHeadingLabel=C'est installé !
FinishedLabel=L'Admin est dans le menu Démarrer (avec Déployer le service en ligne et la console). Ta clé et ton archive sont là où elles étaient. Pour désinstaller : tape « désinstaller » dans la recherche Windows.
FinishedLabelNoIcons=L'assistant a terminé l'installation de l'Admin.

[CustomMessages]
fr.Raccourcis=Les raccourcis :
fr.Fichiers=Les fichiers, d'un double-clic :
fr.Windows=Windows :
fr.Lancer=Lancer l'Admin

[Types]
Name: "tout"; Description: "Tout installer"
Name: "choix"; Description: "Un choix"; Flags: iscustom

[Components]
Name: "app"; Description: "L'application : licences, clients, demandes, PC en direct"; Types: tout choix; Flags: fixed
Name: "console"; Description: "La console : les mêmes commandes pour tes scripts (XPG-License-Admin.exe)"; Types: tout
Name: "service"; Description: "Le service en ligne : de quoi le mettre en ligne ou à jour (l'admin+, ou un admin sous dérogation ; menu Démarrer › Déployer le service en ligne)"; Types: tout choix; Flags: fixed
Name: "notes"; Description: "Les notes à lire (LISEZ-MOI-EDITEUR.txt)"; Types: tout

[Tasks]
Name: "bureau"; Description: "Un raccourci de l'Admin sur le Bureau"; GroupDescription: "{cm:Raccourcis}"
Name: "fichiers"; Description: "Ouvrir les demandes (.xpgreq), licences (.xpglic), registres (.xpgreg) et listes de révocation (.xpgrev) avec l'Admin"; GroupDescription: "{cm:Fichiers}"
Name: "path"; Description: "Ajouter la console au PATH (taper XPG-License-Admin dans n'importe quel terminal)"; GroupDescription: "{cm:Windows}"; Components: console; Flags: unchecked

[Files]
Source: "{#Source}\{#App}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Source}\SDL3.dll"; DestDir: "{app}"; Components: app; Flags: ignoreversion
; 1.41.0 : la police de l'interface (Inter, et sa licence SIL OFL)
Source: "{#Source}\resources\fonts\*"; DestDir: "{app}\resources\fonts"; Components: app; Flags: ignoreversion
Source: "{#Source}\registre.json"; DestDir: "{app}\exemples"; Components: app; Flags: ignoreversion
Source: "{#Source}\parametres-exemple.json"; DestDir: "{app}\exemples"; Components: app; Flags: ignoreversion
Source: "{#Source}\{#Console}"; DestDir: "{app}"; Components: console; Flags: ignoreversion
Source: "{#Source}\LISEZ-MOI-EDITEUR.txt"; DestDir: "{app}"; Components: notes; Flags: ignoreversion
; le kit du service : un dossier ou l'on ecrit (cle-publique.txt, node_modules de npx) - jamais Program Files
Source: "{#Source}\service-licences\*"; DestDir: "{localappdata}\XPG\ServiceLicences"; Components: service; Flags: ignoreversion recursesubdirs createallsubdirs
; 1.47.0 : la connexion au service (le certificat signe par l'editeur) - celle que porte l'installateur, puis celle posee a
; cote de lui (la plus precise : elle remplace l'autre) ; l'Admin la verifie au premier lancement (ServiceConnection.hpp)
#ifdef AvecConnexion
Source: "{#Source}\connexion-service.xpgsvc"; DestDir: "{app}"; Components: app; Flags: ignoreversion
#endif
Source: "{src}\connexion-service*.xpgsvc"; DestDir: "{app}"; Components: app; Flags: external skipifsourcedoesntexist ignoreversion
; l'habillage de l'assistant (lu par lui seul, rien n'est installe)
Source: "polices\Barlow-Regular.ttf"; Flags: dontcopy
Source: "polices\Barlow-Medium.ttf"; Flags: dontcopy
Source: "polices\Barlow-SemiBold.ttf"; Flags: dontcopy
Source: "polices\Barlow-Bold.ttf"; Flags: dontcopy
Source: "admin\logo.bmp"; Flags: dontcopy
Source: "admin\logo-2x.bmp"; Flags: dontcopy

[InstallDelete]
; 1.47.0 : une connexion au service d'une installation d'avant ne reste pas (celle de cette installation la remplace)
Type: files; Name: "{app}\connexion-service*.xpgsvc"

[Icons]
Name: "{group}\{#Nom}"; Filename: "{app}\{#App}"; WorkingDir: "{app}"; Comment: "Les licences, les clients, les demandes, les PC en direct"
Name: "{group}\Déployer le service en ligne"; Filename: "{localappdata}\XPG\ServiceLicences\deployer.bat"; WorkingDir: "{localappdata}\XPG\ServiceLicences"; Components: service; Comment: "Mettre en ligne (ou à jour) ton service des licences sur Cloudflare"
Name: "{group}\Le dossier du service en ligne"; Filename: "{localappdata}\XPG\ServiceLicences"; Components: service
Name: "{group}\Console de l'Admin"; Filename: "{cmd}"; Parameters: "/k ""cd /d ""{app}"" && {#Console} --aide"""; WorkingDir: "{app}"; Components: console; Comment: "Un terminal dans le dossier de l'Admin"
Name: "{group}\Notes de l'Admin (LISEZ-MOI)"; Filename: "{app}\LISEZ-MOI-EDITEUR.txt"; Components: notes
Name: "{group}\Désinstaller l'Admin"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#Nom}"; Filename: "{app}\{#App}"; WorkingDir: "{app}"; Tasks: bureau

[Registry]
; les fichiers de l'editeur, d'un double-clic (pour toi seul : HKCU ; pour tous : HKLM)
Root: HKA; Subkey: "Software\Classes\.xpgreq"; ValueType: string; ValueName: ""; ValueData: "{#ProgIdDemande}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpgreq\OpenWithProgids"; ValueType: string; ValueName: "{#ProgIdDemande}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdDemande}"; ValueType: string; ValueName: ""; ValueData: "Demande de licence XPG"; Flags: uninsdeletekey; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdDemande}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#App},0"; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdDemande}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#App}"" ""%1"""; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpglic"; ValueType: string; ValueName: ""; ValueData: "{#ProgIdLicence}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpglic\OpenWithProgids"; ValueType: string; ValueName: "{#ProgIdLicence}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdLicence}"; ValueType: string; ValueName: ""; ValueData: "Licence XPG"; Flags: uninsdeletekey; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdLicence}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#App},0"; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdLicence}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#App}"" ""%1"""; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpgreg"; ValueType: string; ValueName: ""; ValueData: "{#ProgIdRegistre}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpgreg\OpenWithProgids"; ValueType: string; ValueName: "{#ProgIdRegistre}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdRegistre}"; ValueType: string; ValueName: ""; ValueData: "Registre des licences XPG"; Flags: uninsdeletekey; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdRegistre}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#App},0"; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdRegistre}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#App}"" ""%1"""; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpgrev"; ValueType: string; ValueName: ""; ValueData: "{#ProgIdRevocations}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpgrev\OpenWithProgids"; ValueType: string; ValueName: "{#ProgIdRevocations}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdRevocations}"; ValueType: string; ValueName: ""; ValueData: "Liste de révocation XPG"; Flags: uninsdeletekey; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdRevocations}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#App},0"; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdRevocations}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#App}"" ""%1"""; Tasks: fichiers
; la console dans le PATH : [Code] (AjouterAuPath a la fin de l'installation, RetirerDuPath a la desinstallation)

[Run]
Filename: "{app}\{#App}"; Description: "{cm:Lancer}"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[Code]
const
  FR_PRIVATE = $10;
  CouleurColonne = $003A1F0D;      // #0D1F3A : le marine de l'installateur des Studios
  CouleurTitre = $0047270F;        // #0F2747
  CouleurTexte = $00806B5B;        // #5B6B80
  CouleurFaite = $00EAD3BF;        // le nom d'une etape passee
  CouleurAVenir = $00AD937F;       // le nom d'une etape a venir
  CouleurTurquoise = $00B6C42D;    // #2DC4B6
  CouleurAmbre = $0042C5F5;        // #F5C542
  CouleurPastille = $005E3F2A;     // #2A3F5E : une etape a venir
  ColonneL = 224;                  // la largeur de la colonne des etapes (96 ppp)
  PlusH = 90;                      // la fenetre plus haute que celle d'Inno Setup (96 ppp), si l'ecran le permet
  SPI_GETWORKAREA = $0030;
  NbEtapes = 8;

var
  PolicesOk: Boolean;
  Colonne: TPanel;
  Marques: array of TPanel;
  Noms: array of TLabel;
  PageDonnees: TWizardPage;
  TexteDonneesLabel: TNewStaticText;

function AddFontResourceEx(Fichier: String; Drapeaux: DWORD; Reserve: Cardinal): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall setuponly';
function CreateEllipticRgn(X1, Y1, X2, Y2: Integer): LongWord;
  external 'CreateEllipticRgn@gdi32.dll stdcall setuponly';
function SetWindowRgn(Fenetre: HWND; Region: LongWord; Redessiner: BOOL): Integer;
  external 'SetWindowRgn@user32.dll stdcall setuponly';
function SystemParametersInfo(Action, Param: Cardinal; var Zone: TRect; WinIni: Cardinal): BOOL;
  external 'SystemParametersInfoW@user32.dll stdcall setuponly';

// ---------------------------------------------------------------- les dossiers ----
// Le dossier du programme propose : pour toi seul, %LOCALAPPDATA%\Programs (ce que vaut {autopf} sous Windows ;
// sous Wine il ne se developpe pas) ; pour tous les comptes, Program Files.
function DossierProgrammeDefaut(Param: String): String;
begin
  if IsAdminInstallMode then Result := ExpandConstant('{autopf}\{#Nom}')
  else Result := ExpandConstant('{localappdata}\Programs\{#Nom}');
end;

// Le dossier de la cle et de l'archive (license_admin::defaultKeyFolder) : jamais touche.
function DossierDonnees: String;
begin
  Result := ExpandConstant('{localappdata}\XPG\LicenseAdmin');
end;

// ------------------------------------------------------------------- le PATH ----
function CleEnvironnement(Machine: Boolean; var Racine: Integer; var Cle: String): Boolean;
begin
  if Machine then begin
    Racine := HKEY_LOCAL_MACHINE;
    Cle := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  end else begin
    Racine := HKEY_CURRENT_USER;
    Cle := 'Environment';
  end;
  Result := True;
end;

// Vrai : le dossier de l'Admin n'est pas encore dans ce PATH (celui du compte, ou celui de la machine pour une
// installation pour tous).
function AjouterAuPath(Machine: Boolean): Boolean;
var Racine: Integer;
    Cle, Chemin: String;
begin
  Result := False;
  if Machine <> IsAdminInstallMode then Exit;
  CleEnvironnement(Machine, Racine, Cle);
  if not RegQueryStringValue(Racine, Cle, 'Path', Chemin) then Chemin := '';
  Result := Pos(';' + Lowercase(ExpandConstant('{app}')) + ';', ';' + Lowercase(Chemin) + ';') = 0;
end;

// A la fin de l'installation (l'option << la console dans le PATH >>) : le dossier de l'Admin ajoute au PATH du compte
// (de la machine pour une installation pour tous), s'il n'y est pas deja ; sans << ; >> en trop si le PATH etait vide.
procedure AjouterAuPathMaintenant;
var Racine: Integer;
    Cle, Chemin: String;
begin
  if not AjouterAuPath(IsAdminInstallMode) then Exit;
  CleEnvironnement(IsAdminInstallMode, Racine, Cle);
  if not RegQueryStringValue(Racine, Cle, 'Path', Chemin) then Chemin := '';
  if (Chemin <> '') and (Copy(Chemin, Length(Chemin), 1) <> ';') then Chemin := Chemin + ';';
  RegWriteExpandStringValue(Racine, Cle, 'Path', Chemin + ExpandConstant('{app}'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('path') then AjouterAuPathMaintenant;
end;

// A la desinstallation : le dossier de l'Admin retire du PATH (seulement lui).
procedure RetirerDuPath;
var Racine: Integer;
    Cle, Chemin, App: String;
    P: Integer;
begin
  CleEnvironnement(IsAdminInstallMode, Racine, Cle);
  if not RegQueryStringValue(Racine, Cle, 'Path', Chemin) then Exit;
  App := ExpandConstant('{app}');
  P := Pos(';' + Lowercase(App) + ';', ';' + Lowercase(Chemin) + ';');
  if P = 0 then Exit;
  Delete(Chemin, P, Length(App));
  // le point-virgule en trop (celui d'avant, ou celui d'apres s'il etait le premier)
  if (P > 1) and (Copy(Chemin, P - 1, 1) = ';') then Delete(Chemin, P - 1, 1)
  else if Copy(Chemin, P, 1) = ';' then Delete(Chemin, P, 1);
  // vide : il n'y avait que lui (la valeur est retiree, comme avant l'installation)
  if Trim(Chemin) = '' then RegDeleteValue(Racine, Cle, 'Path')
  else RegWriteExpandStringValue(Racine, Cle, 'Path', Chemin);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then RetirerDuPath;
end;

// --------------------------------------------------------- la cle et l'archive ----
function CompterFichiers(Motif: String): Integer;
var R: TFindRec;
begin
  Result := 0;
  if FindFirst(Motif, R) then
    try
      repeat
        Result := Result + 1;
      until not FindNext(R);
    finally
      FindClose(R);
    end;
end;

// 1.47.0 : la connexion au service : portee par l'installateur, ou posee a cote de lui.
function ConnexionPresente: Boolean;
var R: TFindRec;
begin
#ifdef AvecConnexion
  Result := True;
#else
  Result := False;
  if FindFirst(ExpandConstant('{src}\connexion-service*.xpgsvc'), R) then begin
    Result := True;
    FindClose(R);
  end;
#endif
end;

function TexteDonnees: String;
var D: String;
    N: Integer;
begin
  D := DossierDonnees;
  if FileExists(D + '\cle-editeur-licences.cle') then begin
    N := CompterFichiers(D + '\archive\licences\*.xpglic');
    Result := 'Trouvées sur ce PC, dans ' + D + ' :' + #13#10 + #13#10 +
              '    •  ta clé d''éditeur' + #13#10;
    if N = 1 then Result := Result + '    •  ton archive : 1 licence' + #13#10
    else Result := Result + '    •  ton archive : ' + IntToStr(N) + ' licences' + #13#10;
    Result := Result + '    •  tes réglages (le service, le courriel)' + #13#10 + #13#10 +
              'Rien n''y est changé : l''Admin les reprend telles quelles. La désinstallation ne les efface pas non plus.';
  end else if DirExists(D) then
    Result := 'Le dossier de l''Admin existe (' + D + '), sans clé d''éditeur. Au premier lancement, l''Admin te propose de la créer, ' +
              'ou de restaurer ta sauvegarde (Paramètres de sécurité). Rien n''y est changé par l''installation.'
  else
    Result := 'Pas encore de clé d''éditeur sur ce PC. Au premier lancement, l''Admin te propose de la créer, ou de restaurer ta sauvegarde ' +
              '(Paramètres de sécurité ; le fichier de sauvegarde et sa phrase de passe).' + #13#10 + #13#10 +
              'Elle sera gardée dans ' + D + ' : jamais dans le dossier du programme, jamais effacée par une désinstallation.';
  if WizardIsComponentSelected('service') then
    Result := Result + #13#10 + #13#10 + 'Le service en ligne s''installe dans ' + ExpandConstant('{localappdata}\XPG\ServiceLicences') +
              ' : ton fichier cle-publique.txt y reste d''une version à l''autre.';
  // 1.47.0 : la connexion au service de l'editeur (signee par sa cle) : l'Admin demarre connecte
  if ConnexionPresente then
    Result := Result + #13#10 + #13#10 + 'La connexion au service de l''éditeur vient avec cette installation (signée par sa clé) : ' +
              'l''Admin démarre connecté à son service, sans adresse à taper.';
  Result := Result + #13#10 + #13#10 + 'Une clé gardée ailleurs (l''option --dossier) : elle n''est pas touchée non plus.';
end;

// --------------------------------------------------------------- l'habillage ----
procedure ChargerPolices;
var Nom: String;
    I: Integer;
begin
  PolicesOk := False;
  for I := 0 to 3 do begin
    case I of
      0: Nom := 'Barlow-Regular.ttf';
      1: Nom := 'Barlow-Medium.ttf';
      2: Nom := 'Barlow-SemiBold.ttf';
    else
      Nom := 'Barlow-Bold.ttf';
    end;
    try
      ExtractTemporaryFile(Nom);
      if AddFontResourceEx(ExpandConstant('{tmp}\') + Nom, FR_PRIVATE, 0) > 0 then PolicesOk := True
      else Log('Police non chargée : ' + Nom);
    except
      Log('Police non chargée (' + GetExceptionMessage + ')');
    end;
  end;
end;

// La police : Barlow (Graisse : '', 'Medium', 'SemiBold', 'Bold'), ou Segoe UI si elle n'a pas pu etre chargee.
procedure Ecrire(F: TFont; Graisse: String; Taille: Integer; Couleur: TColor);
begin
  if not PolicesOk then begin
    F.Name := 'Segoe UI';
    if (Graisse = 'SemiBold') or (Graisse = 'Bold') then F.Style := [fsBold] else F.Style := [];
  end else begin
    if Graisse = '' then F.Name := 'Barlow' else F.Name := 'Barlow ' + Graisse;
    F.Style := [];
  end;
  F.Size := Taille;
  F.Color := Couleur;
end;

function NouveauTexte(Parent: TWinControl; Texte: String; X, Y, L, H: Integer; Graisse: String; Taille: Integer; Couleur: TColor): TLabel;
begin
  Result := TLabel.Create(WizardForm);
  Result.Parent := Parent;
  Result.AutoSize := False;
  Result.WordWrap := True;
  Result.Transparent := True;
  Result.SetBounds(X, Y, L, H);
  Ecrire(Result.Font, Graisse, Taille, Couleur);
  Result.Caption := Texte;
end;

function NomEtape(I: Integer): String;
begin
  case I of
    0: Result := 'Bienvenue';
    1: Result := 'Emplacement';
    2: Result := 'Ce que tu installes';
    3: Result := 'Les options';
    4: Result := 'Ta clé et ton archive';
    5: Result := 'Récapitulatif';
    6: Result := 'Installation';
  else
    Result := 'Terminé';
  end;
end;

function EtapeDePage(PageID: Integer): Integer;
begin
  if PageID = wpWelcome then Result := 0
  else if PageID = wpSelectDir then Result := 1
  else if PageID = wpSelectComponents then Result := 2
  else if PageID = wpSelectTasks then Result := 3
  else if (PageDonnees <> nil) and (PageID = PageDonnees.ID) then Result := 4
  else if PageID = wpReady then Result := 5
  else if (PageID = wpPreparing) or (PageID = wpInstalling) then Result := 6
  else if PageID = wpFinished then Result := 7
  else Result := -1;
end;

// Les etapes : passee (une pastille turquoise), en cours (ambre, le nom en gras), a venir (une pastille sombre).
// Des pastilles dessinees (un panneau rond) : aucune police n'est necessaire pour une coche.
procedure MajEtapes(Courante: Integer);
var I: Integer;
begin
  if Colonne = nil then Exit;
  for I := 0 to NbEtapes - 1 do begin
    if I < Courante then begin
      Marques[I].Color := CouleurTurquoise;
      Ecrire(Noms[I].Font, '', 11, CouleurFaite);
    end else if I = Courante then begin
      Marques[I].Color := CouleurAmbre;
      Ecrire(Noms[I].Font, 'SemiBold', 11, clWhite);
    end else begin
      Marques[I].Color := CouleurPastille;
      Ecrire(Noms[I].Font, '', 11, CouleurAVenir);
    end;
    Marques[I].Invalidate;
  end;
end;

procedure CreerColonne;
var L, I, X, Y, W0, Annuler0, H: Integer;
    Zone: TRect;
    Logo: TBitmapImage;
    Pied: TLabel;
    Fichier: String;
begin
  L := ScaleX(ColonneL);
  // la fenetre s'elargit de la colonne : les pages gardent leur place et leur taille ; elle grandit aussi en
  // hauteur (PlusH) - jamais au-dela de l'ecran (la zone de travail ; un petit ecran garde la taille d'Inno Setup)
  W0 := WizardForm.ClientWidth;
  Annuler0 := WizardForm.CancelButton.Left;
  WizardForm.ClientWidth := W0 + L;
  H := ScaleY(PlusH);
  if SystemParametersInfo(SPI_GETWORKAREA, 0, Zone, 0) then
    if (Zone.Bottom - Zone.Top > 0) and (WizardForm.Height + H > Zone.Bottom - Zone.Top - ScaleY(8)) then
      H := Zone.Bottom - Zone.Top - ScaleY(8) - WizardForm.Height;
  if H > 0 then WizardForm.ClientHeight := WizardForm.ClientHeight + H;
  WizardForm.OuterNotebook.SetBounds(L, WizardForm.OuterNotebook.Top, W0, WizardForm.OuterNotebook.Height);
  WizardForm.Bevel.SetBounds(L, WizardForm.Bevel.Top, W0, WizardForm.Bevel.Height);
  WizardForm.BeveledLabel.Left := WizardForm.BeveledLabel.Left + L;
  // les boutons du bas suivent le bord droit (s'ils ne sont pas ancres a droite, ils sont deplaces ici)
  if WizardForm.CancelButton.Left = Annuler0 then begin
    WizardForm.CancelButton.Left := WizardForm.CancelButton.Left + L;
    WizardForm.NextButton.Left := WizardForm.NextButton.Left + L;
    WizardForm.BackButton.Left := WizardForm.BackButton.Left + L;
  end;

  Colonne := TPanel.Create(WizardForm);
  Colonne.Parent := WizardForm;
  Colonne.SetBounds(0, 0, L, WizardForm.ClientHeight);
  Colonne.BevelOuter := bvNone;
  Colonne.ParentBackground := False;
  Colonne.Color := CouleurColonne;
  Colonne.Anchors := [akLeft, akTop, akBottom];

  X := ScaleX(24);
  Logo := TBitmapImage.Create(WizardForm);
  Logo.Parent := Colonne;
  Logo.SetBounds(X, ScaleY(26), ScaleX(56), ScaleY(56));
  Logo.Stretch := True;
  if ScaleX(100) >= 150 then Fichier := 'logo-2x.bmp' else Fichier := 'logo.bmp';
  try
    ExtractTemporaryFile(Fichier);
    Logo.Bitmap.LoadFromFile(ExpandConstant('{tmp}\') + Fichier);
  except
    Log('Logo non chargé (' + GetExceptionMessage + ')');
  end;
  NouveauTexte(Colonne, 'License Manager' + #13#10 + 'Admin', X, ScaleY(94), L - X - ScaleX(8), ScaleY(52), 'Bold', 15, clWhite);
  NouveauTexte(Colonne, 'Version {#Version} · pour l''éditeur', X, ScaleY(150), L - X - ScaleX(8), ScaleY(18), '', 9, CouleurAVenir);

  SetArrayLength(Marques, NbEtapes);
  SetArrayLength(Noms, NbEtapes);
  Y := ScaleY(186);
  for I := 0 to NbEtapes - 1 do begin
    Marques[I] := TPanel.Create(WizardForm);
    Marques[I].Parent := Colonne;
    Marques[I].SetBounds(X + ScaleX(2), Y + ScaleY(6), ScaleX(12), ScaleY(12));
    Marques[I].BevelOuter := bvNone;
    Marques[I].ParentBackground := False;
    Marques[I].Color := CouleurPastille;
    SetWindowRgn(Marques[I].Handle, CreateEllipticRgn(0, 0, Marques[I].Width + 1, Marques[I].Height + 1), True);
    Noms[I] := NouveauTexte(Colonne, NomEtape(I), X + ScaleX(26), Y, L - X - ScaleX(30), ScaleY(24), '', 11, CouleurAVenir);
    Y := Y + ScaleY(30);
  end;
  Pied := NouveauTexte(Colonne, 'Un seul fichier : l''application, la console et ton service en ligne. Rien d''autre à télécharger.',
                       X, Colonne.Height - ScaleY(70), L - X - ScaleX(14), ScaleY(52), '', 8, CouleurAVenir);
  Pied.Anchors := [akLeft, akBottom];
  Pied.Visible := Colonne.Height - ScaleY(70) > Y + ScaleY(8);
end;

// L'en-tete des pages : le titre et la description en Barlow, un trait turquoise dessous.
procedure HabillerEnTete;
var Trait: TPanel;
begin
  WizardForm.MainPanel.ParentBackground := False;
  WizardForm.MainPanel.Color := clWhite;
  // plus haut de 14 points : le titre en Barlow tient en entier, la description sur deux lignes
  WizardForm.MainPanel.Height := WizardForm.MainPanel.Height + ScaleY(14);
  WizardForm.InnerNotebook.Top := WizardForm.InnerNotebook.Top + ScaleY(14);
  WizardForm.InnerNotebook.Height := WizardForm.InnerNotebook.Height - ScaleY(14);
  Ecrire(WizardForm.PageNameLabel.Font, 'SemiBold', 13, CouleurTitre);
  WizardForm.PageNameLabel.Top := ScaleY(12);
  WizardForm.PageNameLabel.Height := ScaleY(26);
  Ecrire(WizardForm.PageDescriptionLabel.Font, '', 10, CouleurTexte);
  WizardForm.PageDescriptionLabel.Top := WizardForm.PageNameLabel.Top + ScaleY(28);
  WizardForm.PageDescriptionLabel.Height := WizardForm.MainPanel.Height - WizardForm.PageDescriptionLabel.Top - ScaleY(2);
  WizardForm.Bevel1.Visible := False;
  // l'icone de l'en-tete : la colonne montre deja le logo
  WizardForm.WizardSmallBitmapImage.Visible := False;
  Trait := TPanel.Create(WizardForm);
  Trait.Parent := WizardForm.InnerPage;
  Trait.SetBounds(0, WizardForm.MainPanel.Top + WizardForm.MainPanel.Height, WizardForm.InnerPage.Width, ScaleY(3));
  Trait.BevelOuter := bvNone;
  Trait.ParentBackground := False;
  Trait.Color := CouleurTurquoise;
  Trait.Anchors := [akLeft, akTop, akRight];
end;

// L'accueil et la fin : sans l'image de gauche (la colonne la remplace), le texte sur toute la largeur.
procedure HabillerAccueilEtFin;
var X, L: Integer;
begin
  X := ScaleX(36);
  L := WizardForm.WelcomePage.ClientWidth - 2 * X;
  WizardForm.WizardBitmapImage.Visible := False;
  WizardForm.WizardBitmapImage2.Visible := False;
  WizardForm.WelcomeLabel1.SetBounds(X, ScaleY(34), L, ScaleY(60));
  Ecrire(WizardForm.WelcomeLabel1.Font, 'Bold', 18, CouleurTitre);
  WizardForm.WelcomeLabel2.SetBounds(X, ScaleY(104), L, WizardForm.WelcomePage.ClientHeight - ScaleY(120));
  Ecrire(WizardForm.WelcomeLabel2.Font, '', 11, CouleurTexte);
  WizardForm.FinishedHeadingLabel.SetBounds(X, ScaleY(34), L, ScaleY(40));
  Ecrire(WizardForm.FinishedHeadingLabel.Font, 'Bold', 18, CouleurTitre);
  // la fin : le texte a une hauteur fixe, la case << Lancer l'Admin >> juste dessous - places ici, une fois (deplacee
  // a l'affichage de la page, Wine laissait son ancien dessin : deux cases)
  WizardForm.FinishedLabel.AutoSize := False;
  WizardForm.FinishedLabel.WordWrap := True;
  WizardForm.FinishedLabel.SetBounds(X, ScaleY(84), L, ScaleY(80));
  Ecrire(WizardForm.FinishedLabel.Font, '', 11, CouleurTexte);
  WizardForm.RunList.SetBounds(X, ScaleY(176), L, WizardForm.RunList.Height);
  WizardForm.YesRadio.Left := X;
  WizardForm.NoRadio.Left := X;
end;

procedure InitializeWizard;
begin
  ChargerPolices;
  PageDonnees := CreateCustomPage(wpSelectTasks, 'Ta clé et ton archive',
                                  'Elles restent sur ce PC, à leur place : l''installation n''y touche jamais.');
  TexteDonneesLabel := TNewStaticText.Create(WizardForm);
  TexteDonneesLabel.Parent := PageDonnees.Surface;
  TexteDonneesLabel.AutoSize := False;
  TexteDonneesLabel.WordWrap := True;
  TexteDonneesLabel.SetBounds(0, ScaleY(4), PageDonnees.SurfaceWidth, PageDonnees.SurfaceHeight - ScaleY(4));
  TexteDonneesLabel.Anchors := [akLeft, akTop, akRight, akBottom];
  Ecrire(TexteDonneesLabel.Font, '', 11, CouleurTexte);
  CreerColonne;
  HabillerEnTete;
  HabillerAccueilEtFin;
  MajEtapes(0);
end;

procedure CurPageChanged(CurPageID: Integer);
var E: Integer;
begin
  E := EtapeDePage(CurPageID);
  if E >= 0 then MajEtapes(E);
  if CurPageID = PageDonnees.ID then TexteDonneesLabel.Caption := TexteDonnees;
end;
