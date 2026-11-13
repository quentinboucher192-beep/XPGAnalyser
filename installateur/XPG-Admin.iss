; =============================================================================
;  installateur/XPG-Admin.iss - 1.49.0 : L'INSTALLATEUR DE XPG ADMIN (l'Admin
;  classique : celui des admins, recu avec une invitation ; docs/ADMINS.md, par. 12)
; -----------------------------------------------------------------------------
;  Construit par outils/mingw/admin_editeur.sh, dans le MEME dossier que celui de
;  l'editeur (XPG-License-Admin-Setup-<v>-EDITEUR.exe) : XPG-Admin-Setup-<v>.exe.
;  JAMAIS LIVRE A UN CLIENT (l'admin+ l'envoie a un futur admin, avec son code).
;
;  Ce qu'il installe (pas de page de composants : tout) :
;    l'application    XPG-Admin.exe, SDL3.dll, la police de l'interface
;    le service       le kit de mise en ligne du service des licences, dans
;                     %LOCALAPPDATA%\XPG\ServiceLicences : un admin s'en sert
;                     pendant une derogation courte << Gerer le service >>
;    la connexion     connexion-service.xpgsvc (le certificat du service, signe par
;                     la cle de l'editeur ; rien de secret) a cote du programme -
;                     celui que porte l'installateur, puis celui pose a cote de lui
;  Pas de console (XPG-License-Admin.exe reste a l'admin+).
;  LE CODE D'INVITATION (la page << Code d'invitation >>) : INV-XXXX-XXXX, 8 caracteres
;  de l'alphabet de Crockford sans I L O U (tirets et minuscules toleres) ; obligatoire
;  en mode normal (sautee si une cle d'admin est deja la : une mise a jour) ; en silencieux
;  /INVITATION=INV-XXXX-XXXX (facultatif ; la mise a jour du Studio n'en passe pas). Ecrit dans
;  %LOCALAPPDATA%\XPG\LicenseAdmin\invitation.txt (une ligne, le compte seul y a acces) :
;  XPG Admin le lit au premier lancement et s'inscrit avec lui : le code accepte, ce PC est
;  admin (1.51.0 : plus de demande d'admin, ni de code a 6 chiffres a dire ; un code inconnu,
;  expire ou deja servi : XPG Admin dit pourquoi et le fait retaper).
;  La cle d'admin, l'archive et les reglages (%LOCALAPPDATA%\XPG\LicenseAdmin) ne sont
;  JAMAIS touches : un admin qui avait l'Admin d'avant (1.48) garde sa cle et ses donnees.
;  En silencieux : /VERYSILENT /SUPPRESSMSGBOXES [/INVITATION=...] [/TASKS=...].
;
;  La compilation (admin_editeur.sh) :
;    ISCC /DVersion=1.49.0 /DSource=<dossier admin-version> /DSortie=<dossier> XPG-Admin.iss
; =============================================================================
#define Nom "XPG Admin"
#define Editeur "XPG"
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #error "/DSource=<le dossier admin-version fait par admin_editeur.sh> manque"
#endif
#ifndef Sortie
  #define Sortie "."
#endif
#define App "XPG-Admin.exe"
#define ProgIdDemande "XPG.Admin.Demande"
#define ProgIdLicence "XPG.Admin.Licence"
#if FileExists(AddBackslash(Source) + "connexion-service.xpgsvc")
  #define AvecConnexion
#endif

[Setup]
AppId={{8B3D5E21-7C4A-4F96-A1E0-5D2C9B7F3A64}
AppName={#Nom}
AppVersion={#Version}
AppVerName={#Nom} {#Version}
AppPublisher={#Editeur}
AppComments=L'Admin des licences pour les admins (reçu avec une invitation de l'admin+)
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
SetupIconFile=..\resources\logo\xpg_admin.ico
UninstallDisplayIcon={app}\{#App}
UninstallDisplayName={#Nom}
OutputDir={#Sortie}
OutputBaseFilename=XPG-Admin-Setup-{#Version}
Compression=lzma2/ultra
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
SetupLogging=yes
ShowLanguageDialog=no
LanguageDetectionMethod=none
AlwaysShowDirOnReadyPage=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
WelcomeLabel1=Bienvenue dans l'installation de XPG Admin
WelcomeLabel2=L'admin+ t'a invité à devenir admin des licences. Cet assistant installe XPG Admin, ton programme d'admin.%n%nGarde sous la main le code d'invitation qu'il t'a donné avec ce fichier (INV-XXXX-XXXX) : l'étape suivante le demande. Ce code suffit : dès qu'il est accepté, tu es admin.%n%nTa clé d'admin naîtra sur ce PC au premier lancement ; elle n'en sort jamais.
ClickNext=Clique sur Suivant pour continuer, ou sur Annuler pour quitter l'assistant.
ClickFinish=Clique sur Terminer pour fermer l'assistant.
ClickInstall=Clique sur Installer pour commencer, ou sur Précédent pour changer un choix.
SelectDirDesc=Où installer XPG Admin ?
SelectDirBrowseLabel=Pour continuer, clique sur Suivant. Pour choisir un autre dossier, clique sur Parcourir.
DiskSpaceMBLabel=Il faut au moins [mb] Mo d'espace libre.
WizardSelectTasks=Les options
SelectTasksDesc=Un raccourci, les fichiers que XPG Admin ouvre.
SelectTasksLabel2=Coche ce que tu veux, puis clique sur Suivant. Une mise à jour reprend tes choix d'avant.
WizardReady=Récapitulatif
ReadyLabel1=Tout est prêt : relis le résumé avant d'installer.
ReadyLabel2a=Clique sur Installer pour commencer, ou sur Précédent pour changer un choix.
ReadyMemoTasks=Les options :
InstallingLabel=Copie des fichiers de XPG Admin.
PreparingDesc=L'assistant prépare l'installation de XPG Admin sur ton PC.
ExitSetupMessage=L'installation n'est pas terminée : si tu quittes maintenant, XPG Admin ne sera pas installé.%n%nTu pourras relancer l'assistant plus tard.%n%nQuitter l'assistant ?
FinishedHeadingLabel=C'est installé !
FinishedLabel=XPG Admin est dans le menu Démarrer. Au premier lancement, ta clé d'admin naît sur ce PC et ton code d'invitation fait de toi un admin : rien d'autre à faire, rien à dire à l'admin+. Un code refusé (inconnu, expiré, déjà servi) : XPG Admin te dit pourquoi et te le fait retaper.
FinishedLabelNoIcons=L'assistant a terminé l'installation de XPG Admin.

[CustomMessages]
fr.Raccourcis=Les raccourcis :
fr.Fichiers=Les fichiers, d'un double-clic :
fr.Lancer=Lancer XPG Admin

[Tasks]
Name: "bureau"; Description: "Un raccourci de XPG Admin sur le Bureau"; GroupDescription: "{cm:Raccourcis}"
Name: "fichiers"; Description: "Ouvrir les demandes (.xpgreq) et les licences (.xpglic) avec XPG Admin"; GroupDescription: "{cm:Fichiers}"; Flags: unchecked

[Files]
Source: "{#Source}\{#App}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\SDL3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\resources\fonts\*"; DestDir: "{app}\resources\fonts"; Flags: ignoreversion
; le kit du service (une derogation courte << Gerer le service >>) : un dossier ou l'on ecrit - jamais Program Files
Source: "{#Source}\service-licences\*"; DestDir: "{localappdata}\XPG\ServiceLicences"; Flags: ignoreversion recursesubdirs createallsubdirs
; la connexion au service (le certificat signe par l'editeur) : celle que porte l'installateur, puis celle posee a cote de lui
#ifdef AvecConnexion
Source: "{#Source}\connexion-service.xpgsvc"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "{src}\connexion-service*.xpgsvc"; DestDir: "{app}"; Flags: external skipifsourcedoesntexist ignoreversion
; l'habillage de l'assistant (lu par lui seul, rien n'est installe) : celui de l'installateur de l'editeur
Source: "polices\Barlow-Regular.ttf"; Flags: dontcopy
Source: "polices\Barlow-Medium.ttf"; Flags: dontcopy
Source: "polices\Barlow-SemiBold.ttf"; Flags: dontcopy
Source: "polices\Barlow-Bold.ttf"; Flags: dontcopy
Source: "admin\logo.bmp"; Flags: dontcopy
Source: "admin\logo-2x.bmp"; Flags: dontcopy

[InstallDelete]
Type: files; Name: "{app}\connexion-service*.xpgsvc"

[Icons]
Name: "{group}\{#Nom}"; Filename: "{app}\{#App}"; WorkingDir: "{app}"; Comment: "Les licences, les clients, les demandes : ce que tes droits d'admin permettent"
Name: "{group}\Déployer le service en ligne (dérogation)"; Filename: "{localappdata}\XPG\ServiceLicences\deployer.bat"; WorkingDir: "{localappdata}\XPG\ServiceLicences"; Comment: "Seulement pendant une dérogation « Gérer le service » accordée par l'admin+"
Name: "{group}\Désinstaller XPG Admin"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#Nom}"; Filename: "{app}\{#App}"; WorkingDir: "{app}"; Tasks: bureau

[Registry]
Root: HKA; Subkey: "Software\Classes\.xpgreq\OpenWithProgids"; ValueType: string; ValueName: "{#ProgIdDemande}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdDemande}"; ValueType: string; ValueName: ""; ValueData: "Demande de licence XPG"; Flags: uninsdeletekey; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdDemande}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#App},0"; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdDemande}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#App}"" ""%1"""; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\.xpglic\OpenWithProgids"; ValueType: string; ValueName: "{#ProgIdLicence}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdLicence}"; ValueType: string; ValueName: ""; ValueData: "Licence XPG"; Flags: uninsdeletekey; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdLicence}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#App},0"; Tasks: fichiers
Root: HKA; Subkey: "Software\Classes\{#ProgIdLicence}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#App}"" ""%1"""; Tasks: fichiers

[Run]
Filename: "{app}\{#App}"; Description: "{cm:Lancer}"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[Code]
const
  FR_PRIVATE = $10;
  CouleurColonne = $003A1F0D;      // #0D1F3A : le marine de l'installateur de l'editeur
  CouleurTitre = $0047270F;        // #0F2747
  CouleurTexte = $00806B5B;        // #5B6B80
  CouleurFaite = $00EAD3BF;
  CouleurAVenir = $00AD937F;
  CouleurTurquoise = $00B6C42D;    // #2DC4B6
  CouleurAmbre = $0042C5F5;        // #F5C542
  CouleurPastille = $005E3F2A;     // #2A3F5E
  CouleurErreur = $002626DC;       // #DC2626
  ColonneL = 224;
  PlusH = 90;
  SPI_GETWORKAREA = $0030;
  NbEtapes = 8;
  // l'alphabet du code d'invitation : Crockford, sans I L O U (docs/ADMINS.md, par. 12)
  Alphabet = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';

var
  PolicesOk: Boolean;
  Colonne: TPanel;
  Marques: array of TPanel;
  Noms: array of TLabel;
  PageInvitation: TInputQueryWizardPage;
  ErreurInvitation: TNewStaticText;
  PageDonnees: TWizardPage;
  TexteDonneesLabel: TNewStaticText;
  CodeRetenu: String;

function AddFontResourceEx(Fichier: String; Drapeaux: DWORD; Reserve: Cardinal): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall setuponly';
function CreateEllipticRgn(X1, Y1, X2, Y2: Integer): LongWord;
  external 'CreateEllipticRgn@gdi32.dll stdcall setuponly';
function SetWindowRgn(Fenetre: HWND; Region: LongWord; Redessiner: BOOL): Integer;
  external 'SetWindowRgn@user32.dll stdcall setuponly';
function SystemParametersInfo(Action, Param: Cardinal; var Zone: TRect; WinIni: Cardinal): BOOL;
  external 'SystemParametersInfoW@user32.dll stdcall setuponly';

// ---------------------------------------------------------------- les dossiers ----
function DossierProgrammeDefaut(Param: String): String;
begin
  if IsAdminInstallMode then Result := ExpandConstant('{autopf}\{#Nom}')
  else Result := ExpandConstant('{localappdata}\Programs\{#Nom}');
end;

// Le dossier de la cle d'admin et des donnees (le meme que l'Admin d'avant) : jamais touche, sauf invitation.txt.
function DossierDonnees: String;
begin
  Result := ExpandConstant('{localappdata}\XPG\LicenseAdmin');
end;

// ------------------------------------------------------------ le code d'invitation ----
// Le code tape (tirets, espaces et minuscules toleres) mis en forme : << INV-XXXX-XXXX >> ; '' : pas un code.
function CodeNormalise(Texte: String): String;
var I: Integer;
    C, Brut: String;
begin
  Result := '';
  Brut := '';
  Texte := Uppercase(Trim(Texte));
  for I := 1 to Length(Texte) do begin
    C := Copy(Texte, I, 1);
    if (C <> '-') and (C <> ' ') then Brut := Brut + C;
  end;
  if (Length(Brut) <> 11) or (Copy(Brut, 1, 3) <> 'INV') then Exit;
  for I := 4 to 11 do
    if Pos(Copy(Brut, I, 1), Alphabet) = 0 then Exit;
  Result := 'INV-' + Copy(Brut, 4, 4) + '-' + Copy(Brut, 8, 4);
end;

// invitation.txt : une ligne, puis le fichier reserve au compte (icacls : l'heritage retire, le compte seul).
procedure EcrireInvitation(Code: String);
var Fichier: String;
    Code2: Integer;
begin
  if Code = '' then Exit;
  ForceDirectories(DossierDonnees);
  Fichier := DossierDonnees + '\invitation.txt';
  if not SaveStringToFile(Fichier, Code + #13#10, False) then begin
    Log('invitation.txt ne s''écrit pas : ' + Fichier);
    Exit;
  end;
  Log('Code d''invitation écrit : ' + Fichier);
  if Exec(ExpandConstant('{sys}\icacls.exe'), '"' + Fichier + '" /inheritance:r /grant:r "' + GetUserNameString + ':F"', '', SW_HIDE,
          ewWaitUntilTerminated, Code2) then
    Log('invitation.txt : réservé au compte ' + GetUserNameString + ' (icacls : ' + IntToStr(Code2) + ')')
  else
    Log('invitation.txt : icacls ne s''est pas lancé (le fichier reste dans le dossier du compte)');
end;

// Une cle d'admin deja sur ce PC (l'Admin d'avant, une mise a jour) : pas de code a taper.
function CleAdminPresente: Boolean;
begin
  Result := FileExists(DossierDonnees + '\cle-admin.cle');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageInvitation <> nil) and (PageID = PageInvitation.ID) and CleAdminPresente;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  // en silencieux, la page passe toujours : le code (facultatif) vient de /INVITATION= (CurStepChanged)
  if WizardSilent then Exit;
  if (PageInvitation <> nil) and (CurPageID = PageInvitation.ID) then begin
    CodeRetenu := CodeNormalise(PageInvitation.Values[0]);
    if CodeRetenu = '' then begin
      ErreurInvitation.Caption := 'Ce n''est pas un code d''invitation : il s''écrit INV- puis 8 lettres ou chiffres (sans I, L, O, U), ' +
                                  'par exemple INV-7K3M-Q9TD. Recopie celui que l''admin+ t''a donné.';
      Result := False;
    end else begin
      ErreurInvitation.Caption := '';
      PageInvitation.Values[0] := CodeRetenu;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Param: String;
begin
  if CurStep <> ssPostInstall then Exit;
  // en silencieux : /INVITATION=INV-XXXX-XXXX (facultatif ; faux : rien n'est ecrit, XPG Admin le demandera)
  if WizardSilent then begin
    Param := ExpandConstant('{param:INVITATION|}');
    CodeRetenu := CodeNormalise(Param);
    if (Param <> '') and (CodeRetenu = '') then Log('/INVITATION=' + Param + ' : pas un code d''invitation, rien n''est écrit');
  end;
  EcrireInvitation(CodeRetenu);
end;

// La connexion au service : portee par l'installateur, ou posee a cote de lui.
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

// ------------------------------------------------------------- la cle d'admin ----
function TexteDonnees: String;
var D: String;
begin
  D := DossierDonnees;
  if FileExists(D + '\cle-admin.cle') then
    Result := 'Ta clé d''admin est déjà sur ce PC (' + D + ') : XPG Admin la reprend telle quelle, avec tes réglages et ton archive. ' +
              'Rien à refaire : ni demande, ni code.'
  else
    Result := 'Au premier lancement, XPG Admin crée ta clé d''admin sur ce PC (dans ' + D + ' ; elle n''en sort jamais) et ton code ' +
              'd''invitation fait de toi un admin, avec les droits que l''admin+ a choisis : ni demande, ni code à lui dire.';
  if FileExists(D + '\cle-editeur-licences.cle') then
    Result := Result + #13#10 + #13#10 + 'Ce PC a aussi la clé de l''éditeur : XPG Admin ne s''en sert jamais (elle reste à XPG License Manager Admin).';
  if ConnexionPresente then
    Result := Result + #13#10 + #13#10 + 'La connexion au service de l''éditeur vient avec cette installation (signée par sa clé) : ' +
              'XPG Admin démarre connecté à son service, sans adresse à taper.';
  Result := Result + #13#10 + #13#10 + 'La désinstallation n''efface ni ta clé, ni tes données.';
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
    1: Result := 'Code d''invitation';
    2: Result := 'Emplacement';
    3: Result := 'Les options';
    4: Result := 'Ta clé d''admin';
    5: Result := 'Récapitulatif';
    6: Result := 'Installation';
  else
    Result := 'Terminé';
  end;
end;

function EtapeDePage(PageID: Integer): Integer;
begin
  if PageID = wpWelcome then Result := 0
  else if (PageInvitation <> nil) and (PageID = PageInvitation.ID) then Result := 1
  else if PageID = wpSelectDir then Result := 2
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
  NouveauTexte(Colonne, 'XPG' + #13#10 + 'Admin', X, ScaleY(94), L - X - ScaleX(8), ScaleY(52), 'Bold', 15, clWhite);
  NouveauTexte(Colonne, 'Version {#Version} · pour les admins', X, ScaleY(150), L - X - ScaleX(8), ScaleY(18), '', 9, CouleurAVenir);

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
  Pied := NouveauTexte(Colonne, 'Un seul fichier, et le code d''invitation de l''admin+. Rien d''autre à télécharger.',
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

// 1.50.0 : << dans l'installateur de l'admin classique attention des textes se chevauchent >> - la page << Code
// d'invitation >> : ses textes passent en Barlow (11, le code en 14), plus hauts que la police d'Inno Setup, mais
// restaient a la place calculee pour elle : la consigne coupee apres sa premiere ligne, le bas de << Code
// d'invitation : >> sous le champ. Recomposee ici, de haut en bas, apres le changement de police : la consigne
// retournee a la ligne a sa hauteur (AdjustHeight), l'etiquette dessous, le champ dessous, l'erreur dessous jusqu'au
// bas de la page. Les hauteurs sont prises a la largeur du moment, la plus etroite : la page qui s'elargit ensuite
// (le style << modern >>) donne moins de lignes, jamais plus ; les largeurs suivent le bord droit (Anchors).
procedure ComposerPageInvitation;
var Y, L: Integer;
begin
  L := PageInvitation.SurfaceWidth;
  Y := 0;
  with PageInvitation.SubCaptionLabel do begin
    AutoSize := False;
    WordWrap := True;
    SetBounds(0, Y, L, ScaleY(20));
    AdjustHeight;
    Anchors := [akLeft, akTop, akRight];
    Y := Top + Height + ScaleY(14);
  end;
  with PageInvitation.PromptLabels[0] do begin
    AutoSize := False;
    WordWrap := True;
    SetBounds(0, Y, L, ScaleY(20));
    AdjustHeight;
    Anchors := [akLeft, akTop, akRight];
    Y := Top + Height + ScaleY(6);
  end;
  with PageInvitation.Edits[0] do begin
    Top := Y;
    Left := 0;
    Width := L;
    if Height < ScaleY(30) then Height := ScaleY(30);
    Anchors := [akLeft, akTop, akRight];
    Y := Top + Height + ScaleY(14);
  end;
  ErreurInvitation.SetBounds(0, Y, L, PageInvitation.SurfaceHeight - Y);
  ErreurInvitation.Anchors := [akLeft, akTop, akRight, akBottom];
end;

procedure InitializeWizard;
begin
  ChargerPolices;
  // la page << Code d'invitation >> (apres l'accueil) : obligatoire ; prerempli par /INVITATION=
  PageInvitation := CreateInputQueryPage(wpWelcome, 'Code d''invitation',
                                         'Le code que l''admin+ t''a donné avec cet installateur (valable 48 h, une seule fois) : il fait de ce PC un admin.',
                                         'Tape ton code d''invitation, tel que l''admin+ te l''a donné ou dicté : INV- puis 8 lettres ou chiffres. ' +
                                         'Les minuscules et les tirets oubliés sont acceptés. XPG Admin le vérifie au premier lancement ; ' +
                                         'refusé (inconnu, expiré, déjà servi), il te dit pourquoi et te le fait retaper.');
  PageInvitation.Add('Code d''invitation :', False);
  PageInvitation.Values[0] := ExpandConstant('{param:INVITATION|}');
  Ecrire(PageInvitation.SubCaptionLabel.Font, '', 11, CouleurTexte);
  Ecrire(PageInvitation.PromptLabels[0].Font, 'SemiBold', 11, CouleurTitre);
  Ecrire(PageInvitation.Edits[0].Font, 'SemiBold', 14, CouleurTitre);
  PageInvitation.Edits[0].MaxLength := 20;
  ErreurInvitation := TNewStaticText.Create(WizardForm);
  ErreurInvitation.Parent := PageInvitation.Surface;
  ErreurInvitation.AutoSize := False;
  ErreurInvitation.WordWrap := True;
  Ecrire(ErreurInvitation.Font, '', 10, CouleurErreur);
  ComposerPageInvitation;   // 1.50.0 : rien ne se chevauche (les textes en Barlow sont plus hauts)
  PageDonnees := CreateCustomPage(wpSelectTasks, 'Ta clé d''admin',
                                  'Elle naît sur ce PC au premier lancement, et n''en sort jamais.');
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
  // 1.50.0 : le recapitulatif retourne ses longues lignes a sa largeur (avant : coupees au bord droit, une barre
  // de defilement horizontale pour les lire)
  WizardForm.ReadyMemo.WordWrap := True;
  WizardForm.ReadyMemo.ScrollBars := ssVertical;
  MajEtapes(0);
end;

procedure CurPageChanged(CurPageID: Integer);
var E: Integer;
begin
  E := EtapeDePage(CurPageID);
  if E >= 0 then MajEtapes(E);
  if CurPageID = PageDonnees.ID then TexteDonneesLabel.Caption := TexteDonnees;
end;

// Le recapitulatif : le code d'invitation y est rappele.
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  if CodeRetenu <> '' then Result := 'Code d''invitation :' + NewLine + Space + CodeRetenu + NewLine + NewLine + MemoDirInfo
  else Result := 'Ta clé d''admin est déjà sur ce PC : pas de code d''invitation.' + NewLine + NewLine + MemoDirInfo;
  if MemoTasksInfo <> '' then Result := Result + NewLine + NewLine + MemoTasksInfo;
end;
