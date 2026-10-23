; =============================================================================
;  installateur\XPGAnalyser.iss - l'installateur Windows d'API Studio et d'IHM Studio (1.12.20 ;
;  avant : XPGAnalyser - le nom de ce fichier, la cle du registre et les dossiers techniques le gardent)
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
;     Démarrer > API IHM Studio > Maintenance) ;
;   - note chaque étape : une installation coupée se reprend ;
;   - la désinstallation garde tes données, sauf si tu tapes SUPPRIMER.
;
;  1.8.0 (suite) :
;   - PC vierge : aucun projet. La reprise ne propose que les versions vraiment
;     INSTALLÉES (leur exe est là, ou le lot 8 connu du registre) ;
;   - la page « Ta bibliothèque » : les blocs livrés, et les tiens, ajoutés par
;     glisser-déposer ou par Ajouter (une vraie liste Windows : aide\xpgliste.dll) ;
;   - « Désinstaller API IHM Studio » dans le menu Démarrer (la recherche Windows le trouve) ;
;   - l'option « Épingler à la barre des tâches » (au premier lancement, par l'application) ;
;   - la page « Ce qui a été installé » : les programmes, leur architecture, les dossiers.
;
;  1.12.27 - l'habillage :
;   - une colonne à gauche, aux couleurs des deux logos : le logo, le nom, la version, et les
;     ÉTAPES de l'installation (faite, en cours, à venir) - une liste générique, décrite à un
;     seul endroit du [Code] (EtapesNoms, EtapeDePage) : une page de plus = une ligne ;
;   - la police Barlow (images\..\polices, licence SIL OFL), chargée pour l'assistant seul :
;     rien n'est installé dans Windows ;
;   - des images : le bandeau de l'accueil et celui de la fin, une icône par étape dans
;     l'en-tête, en 1x et en 2x (écrans à 200 %) ;
;   - pendant la copie, une animation qui vient d'un GIF (images\animation.gif, découpé par
;     outils\installateur_images.py - remplace le GIF pour changer l'animation), une barre en
;     dégradé avec son pourcentage, et des astuces ;
;   - la page d'accueil revient : les deux applications, ce que fait l'assistant.
;
;  1.12.28 - le mouvement (un seul moteur : une minuterie, des transitions et des scènes,
;  voir « le mouvement » dans le [Code]) :
;   - chaque page arrive en glissant (du côté où l'on va), son titre et sa description
;     apparaissent, l'icône de l'étape grandit, le trait de l'en-tête se dessine ;
;   - la colonne avance d'une étape à l'autre : la pastille glisse, le lien se remplit, la coche
;     se dessine, l'étape en cours bat ; pendant la copie, son anneau se remplit ; des
;     impulsions parcourent le décor du bas ;
;   - l'accueil s'assemble (le réseau s'allume, les deux logos arrivent, le lien se fait, un
;     éclat passe), puis le titre, le texte et les cartes montent ;
;   - pendant la préparation et la copie : l'animation, une barre qui suit en douceur (son
;     éclat, son pourcentage qui compte), le fichier en cours, le temps qui reste, et un
;     diaporama de ce que t'apportent les deux applications (il remplace les astuces) ;
;   - « Ce qui a été installé » s'écrit ligne à ligne ; la fin : le cercle surgit, la coche se
;     dessine, des confettis, des étincelles ;
;   - les calques et leurs repères : outils\installateur_images.py (images\ui\*.bmp, 1x et 2x,
;     et images\ui\habillage.ini, lu à la compilation) ;
;   - sans assistant (/SILENT, /VERYSILENT), rien de tout cela n'est créé ; une erreur pendant
;     une animation l'arrête (notée dans le journal), jamais l'installation.
;
;  1.12.29 - plus grand, plus calme, plus de détails :
;   - l'assistant fait 912 x 580 à 96 ppp (Dimensionner ; plus grand à 125, 150, 200 %, ramené à
;     la taille de l'écran s'il le faut) ; la colonne, les bandeaux, les cartes et la page de la
;     copie sont refaits à cette taille (outils\installateur_images.py) ;
;   - rien ne clignote : la fenêtre se dessine d'un bloc (WS_EX_COMPOSITED ; /COMPOSITION=non
;     l'enlève) ; aucun texte n'est posé sur une animation (les noms des étapes sont une image à
;     part, que la pulsation ne touche pas ; la note de la colonne est au-dessus du décor) ; les
;     textes ne changent plus de couleur en arrivant ; le pourcentage, le fichier en cours et le
;     temps qui reste s'écrivent dans des images, au plus cinq fois par seconde ;
;   - la préparation et la copie ont leurs « Détails » : chaque action, son heure et son état (en
;     cours, fait, attention, erreur) - la version trouvée et sa sauvegarde, la copie (fichiers,
;     tailles), les raccourcis, puis tes dossiers, la reprise de chaque ancienne version (ce qui en
;     est pris), le lot 8, la vérification. Les étapes PowerShell écrivent leurs lignes au fur et à
;     mesure (/progres:<fichier>, voir installer_etape.ps1), que l'assistant lit pendant qu'elles
;     tournent ; sans assistant, les mêmes lignes vont dans le journal (/LOG) ;
;   - « Ce qui a été installé » dit ce qui a été sauvegardé, ce qui a été repris et d'où.
;
;  Sans surveillance : API-IHM-Studio-Setup-<v>.exe /VERYSILENT /SUPPRESSMSGBOXES
;    [/CURRENTUSER | /ALLUSERS] [/DIR="..."] [/DONNEES="D:\Mes données"]
;    [/MIGRER=oui|non] [/DESINSTALLERLOT8=oui|non] [/LOG="fichier"]
;  (voir docs\GUIDE-INSTALLATION.md). Avec l'assistant : /COMPOSITION=non (1.12.29) si sa fenêtre
;  se dessine mal sur un PC (le double tampon de Windows est alors enlevé).
; =============================================================================

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 ou plus est necessaire (outils\install_build_tools.bat l'installe).
#endif

#ifndef Nom
  #define Nom "API IHM Studio"
#endif
; 1.12.20 : les deux applications ; Cle : le nom d'origine, celui du registre (Software\XPGAnalyser) et
; des dossiers techniques - une mise a jour retrouve ainsi l'installation, ses donnees et ses reglages.
#ifndef NomApi
  #define NomApi "API Studio"
#endif
#ifndef NomIhm
  #define NomIhm "IHM Studio"
#endif
#ifndef Cle
  #define Cle "XPGAnalyser"
#endif
#ifndef Fichier
  #define Fichier "API-IHM-Studio"
#endif
#ifndef Editeur
  #define Editeur "API IHM Studio"
#endif
#ifndef Version
  #define Version "1.12.0"
#endif
#ifndef AppGuid
  #define AppGuid "68A57092-419E-4BEF-8486-57B131E9C592"
#endif
#ifndef Exe
  #define Exe "API-Studio.exe"
#endif
; 1.12.0 : deux applications - API Studio ({#Exe}) et IHM Studio ({#ExeIhm}).
#ifndef ExeIhm
  #define ExeIhm "IHM-Studio.exe"
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
; 1.12.27 : l'animation de la copie - écrite par outils\installateur_images.py d'après images\animation.gif.
#define AnimIni AddBackslash(SourcePath) + "images\ui\animation.ini"
#define AnimImages ReadIni(AnimIni, "Animation", "images", "1")
#define AnimDelai ReadIni(AnimIni, "Animation", "delai", "60")
#define AnimLargeur ReadIni(AnimIni, "Animation", "largeur", "360")
#define AnimHauteur ReadIni(AnimIni, "Animation", "hauteur", "120")
; 1.12.28 : les repères des calques (positions, nombre d'images de chaque bande) - écrits par
; outils\installateur_images.py.
#define HabIni AddBackslash(SourcePath) + "images\ui\habillage.ini"
#define HabBandes ReadIni(HabIni, "Bandes", "liste", "")
#define AccueilLogos ReadIni(HabIni, "Accueil", "logos", "304,6;416,12")
#define AccueilPoints ReadIni(HabIni, "Accueil", "points", "")
#define AccueilNoeuds ReadIni(HabIni, "Accueil", "noeuds", "")
#define AccueilLiens ReadIni(HabIni, "Accueil", "liens", "")
#define FinDisque ReadIni(HabIni, "Fin", "disque", "325,0")
#define FinCoche ReadIni(HabIni, "Fin", "coche", "370,52")
#define FinCentre ReadIni(HabIni, "Fin", "centre", "400,75")
#define FinEtincelles ReadIni(HabIni, "Fin", "etincelles", "")
#define FinNoeuds ReadIni(HabIni, "Fin", "noeuds", "")
#define DecorPistes ReadIni(HabIni, "Decor", "pistes", "")
#define DecorHaut ReadIni(HabIni, "Decor", "haut", "54")
#define DecorH ReadIni(HabIni, "Decor", "hauteur", "96")
; 1.12.29 : la colonne, les bandeaux de l'accueil et de la fin (leur taille, leur éclat, la lueur de l'accueil).
#define ColonneL ReadIni(HabIni, "Colonne", "largeur", "232")
#define AccueilTaille ReadIni(HabIni, "Accueil", "taille", "620,196")
#define AccueilEclat ReadIni(HabIni, "Accueil", "eclat", "277,675")
#define AccueilLueur ReadIni(HabIni, "Accueil", "lueur", "207")
#define FinTaille ReadIni(HabIni, "Fin", "taille", "620,196")
#define FinEclat ReadIni(HabIni, "Fin", "eclat", "310,664")
; 1.12.29 : ce que la copie pose, d'après le manifeste de la livraison (les « Détails » de la copie) :
; le nombre de fichiers, leur taille, et les gros (un Mo et plus : leur nom et leur taille).
#define ManifesteFichier AddBackslash(Staging) + "manifeste.json"
#define NbFichiers 0
#define TailleFichiers 0
#define GrosFichiers ""
#define ManifesteH 0
#define ManifesteLigne ""
#define ManifesteChemin ""
#define ManifesteTaille 0
; (dans un #sub, #define ferait une variable locale : les globales changent par #expr)
#sub ManifesteLire
  #expr ManifesteLigne = Trim(FileRead(ManifesteH))
  #if Pos('"chemin":', ManifesteLigne) == 1
    #expr ManifesteChemin = StringChange(StringChange(StringChange(Trim(Copy(ManifesteLigne, 10)), '"', ""), ",", ""), "\\", "\")
  #elif Pos('"taille":', ManifesteLigne) == 1
    #expr ManifesteTaille = Int(StringChange(Trim(Copy(ManifesteLigne, 10)), ",", ""), 0)
    #expr NbFichiers = NbFichiers + 1
    #expr TailleFichiers = TailleFichiers + ManifesteTaille
    #if ManifesteTaille >= 1048576
      #expr GrosFichiers = GrosFichiers + StringChange(ManifesteChemin, "'", "''") + ":" + Str(ManifesteTaille) + "|"
    #endif
  #endif
#endsub
#if FileExists(ManifesteFichier)
  #for {ManifesteH = FileOpen(ManifesteFichier); ManifesteH && !FileEof(ManifesteH); 0} ManifesteLire
  #if ManifesteH
    #expr FileClose(ManifesteH)
  #endif
  ; le manifeste lui-même est copié aussi (il ne se cite pas)
  #expr NbFichiers = NbFichiers + 1
  #expr TailleFichiers = TailleFichiers + FileSize(ManifesteFichier)
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
; 1.12.20 : le menu Demarrer prend le nouveau nom (l'ancien dossier XPGAnalyser s'en va, [InstallDelete]).
UsePreviousGroup=no
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
; 1.12.27 : la colonne des étapes s'ajoute à gauche ; la page d'accueil revient. 1.12.29 : la taille vient
; du [Code] (Dimensionner : 912 x 580 à 96 ppp, ramenée à l'écran s'il le faut) - un pourcentage ici,
; Inno Setup l'appliquait APRÈS InitializeWizard, colonne comprise (sans assistant : comme avant). Et
; l'assistant « modern » était redimensionnable, mais pas au-delà de 150 % de sa taille d'origine : la
; 1.12.28 restait ainsi à 750 de large ; une taille fixe (la mise en page des pages en dépend).
WizardSizePercent=100
WizardResizable=no
DisableWelcomePage=no
WizardImageFile=images\grande.bmp,images\grande-2x.bmp
WizardSmallImageFile=images\petite.bmp,images\petite-2x.bmp
SetupIconFile=..\resources\logo\studio.ico
UninstallDisplayIcon={app}\{#Exe}
UninstallDisplayName={#Nom}
OutputDir={#Sortie}
OutputBaseFilename={#Fichier}-Setup-{#Version}
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
; 1.12.27 : des textes courts, au tutoiement, comme les pages de l'assistant (l'en-tête n'a que deux lignes).
ClickNext=Clique sur Suivant pour continuer, ou sur Annuler pour quitter l'assistant.
SelectDirDesc=Où installer [name] ?
SelectDirBrowseLabel=Pour continuer, clique sur Suivant. Pour choisir un autre dossier, clique sur Parcourir.
DiskSpaceMBLabel=Il faut au moins [mb] Mo d'espace libre.
SelectTasksDesc=Les raccourcis, et ce que l'assistant fait en plus.
SelectTasksLabel2=Choisis ce que l'assistant doit faire en plus, puis clique sur Suivant.
ReadyLabel1=Tout est prêt : relis le résumé avant d'installer.
ReadyLabel2a=Clique sur Installer pour commencer, ou sur Précédent pour changer un choix.
InstallingLabel=Copie des fichiers d'{#Nom}, puis vérification.
; 1.12.28 : la page « Préparation » (le texte d'origine vouvoie).
PreparingDesc=L'assistant prépare l'installation d'{#Nom} sur ton PC.
; 1.12.28 : ce message-là ne remplace pas [name] (il s'affichait tel quel) : le nom, directement.
ExitSetupMessage=L'installation n'est pas terminée : si tu quittes maintenant, {#Nom} ne sera pas installé.%n%nTu pourras relancer l'assistant plus tard.%n%nQuitter l'assistant ?
FinishedHeadingLabel=C'est installé !
FinishedLabel={#NomApi} et {#NomIhm} sont dans le menu Démarrer, avec leur Maintenance (vérifier, réparer, restaurer). Tes dossiers se règlent dans l'application : accueil > Dossiers. Pour désinstaller : tape « désinstaller » dans la recherche Windows.
FinishedLabelNoIcons=L'assistant a terminé l'installation de [name].

[CustomMessages]
fr.RaccourciBureau=Créer un raccourci sur le Bureau
fr.Raccourcis=Raccourcis :
fr.Lancer=Lancer {#NomApi}
fr.LancerIhm=Lancer {#NomIhm}
fr.EpinglerBarre=Épingler {#NomApi} et {#NomIhm} à la barre des tâches (au premier lancement : Windows te demande de confirmer)

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
; 1.8.0 : la page « Ta bibliothèque » - l'index des blocs livrés (lu avant la copie) et la liste
; Windows (une DLL 32 bits, comme Setup.exe).
Source: "{#Staging}\libs\index.txt"; DestName: "libs-index.txt"; Flags: dontcopy
; 1.12.28 : ce que l'assistant sort pour lui seul (la liste, la police, les images) a son propre
; bloc compressé (solidbreak) : il le lit sans décompresser le programme. Avant, chaque image sortie
; à la demande refaisait tout le flux (1 à 3 secondes d'attente au démarrage et à chaque page).
Source: "aide\xpgliste.dll"; Flags: dontcopy solidbreak
; 1.12.27 : l'habillage de l'assistant - sa police (avec sa licence) et ses images ; lus par
; l'assistant seul, rien n'est installé. 1.12.28 : les images d'une échelle (1x : {tmp}\ui,
; 2x : {tmp}\ui2x) sortent d'un coup au démarrage (voir Habiller).
Source: "polices\*.ttf"; Flags: dontcopy
Source: "polices\OFL-Barlow.txt"; Flags: dontcopy
Source: "images\ui\*.bmp"; Excludes: "*-2x.bmp"; DestDir: "{tmp}\ui"; Flags: dontcopy
Source: "images\ui\*-2x.bmp"; DestDir: "{tmp}\ui2x"; Flags: dontcopy

[InstallDelete]
; 1.12.0 : deux applications - l'exe unique de la 1.11 et ses raccourcis s'en vont.
Type: files; Name: "{app}\XpgAnalyzer.exe"
Type: files; Name: "{autoprograms}\{#Cle}\{#Cle}.lnk"
Type: files; Name: "{autodesktop}\{#Cle}.lnk"
; 1.12.20 : API Studio et IHM Studio - les exe, le menu Demarrer et les raccourcis de XPGAnalyser s'en vont.
Type: files; Name: "{app}\XpgAnalyzer-API.exe"
Type: files; Name: "{app}\XpgAnalyzer-IHM.exe"
Type: filesandordirs; Name: "{autoprograms}\{#Cle}"
Type: files; Name: "{autodesktop}\{#Cle} API.lnk"
Type: files; Name: "{autodesktop}\{#Cle} IHM.lnk"

[Icons]
Name: "{autoprograms}\{#Nom}\{#NomApi}"; Filename: "{app}\{#Exe}"; WorkingDir: "{app}"; Comment: "Le programme de l'automate : analyse, variables, tables, simulation"
Name: "{autoprograms}\{#Nom}\{#NomIhm}"; Filename: "{app}\{#ExeIhm}"; WorkingDir: "{app}"; Comment: "Les vues, les scripts, les équipements, la simulation de l'IHM"
Name: "{autoprograms}\{#Nom}\Maintenance\Réparer"; Filename: "{app}\maintenance\repair.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Vérifier l'installation"; Filename: "{app}\maintenance\verify_installation.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Reprendre une installation interrompue"; Filename: "{app}\maintenance\resume_installation.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Restaurer une sauvegarde"; Filename: "{app}\maintenance\restore_backup.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Collecter les journaux"; Filename: "{app}\maintenance\collect_diagnostics.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Maintenance\Réinitialiser mes réglages"; Filename: "{app}\maintenance\reset_user_settings.bat"; WorkingDir: "{app}\maintenance"; IconFilename: "{app}\{#Exe}"
Name: "{autoprograms}\{#Nom}\Tes données"; Filename: "{code:DonneesReel}"; Check: not IsAdminInstallMode
; 1.8.0 : trouvé par la recherche Windows (« désinstaller »). Tes données restent (voir plus bas).
Name: "{autoprograms}\{#Nom}\Désinstaller {#Nom}"; Filename: "{uninstallexe}"; Comment: "Désinstaller {#Nom} (tes données sont gardées)"
Name: "{autodesktop}\{#NomApi}"; Filename: "{app}\{#Exe}"; WorkingDir: "{app}"; Tasks: bureau
Name: "{autodesktop}\{#NomIhm}"; Filename: "{app}\{#ExeIhm}"; WorkingDir: "{app}"; Tasks: bureau

[Registry]
Root: HKA; Subkey: "Software\{#Cle}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\{#Cle}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"
Root: HKA; Subkey: "Software\{#Cle}"; ValueType: string; ValueName: "Version"; ValueData: "{#Version}"
Root: HKA; Subkey: "Software\{#Cle}"; ValueType: string; ValueName: "Portee"; ValueData: "{code:Portee}"
Root: HKA; Subkey: "Software\{#Cle}"; ValueType: string; ValueName: "DossierDonnees"; ValueData: "{code:DonneesBrut}"

[Run]
Filename: "{app}\{#Exe}"; Description: "{cm:Lancer}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#ExeIhm}"; Description: "{cm:LancerIhm}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: files; Name: "{app}\installation.ini"
Type: files; Name: "{code:DossierReserve}\{#Fichier}-Setup-*.exe"
Type: files; Name: "{code:DossierReserve}\{#Cle}-Setup-*.exe"
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
  // 1.12.28 : le récapitulatif s'écrit ligne à ligne (voir TicRecap)
  RecapTexte: TArrayOfString;
  RecapDebut, RecapVu: Integer;

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
  if IsAdminInstallMode then Result := ExpandConstant('{commonappdata}\{#Cle}')
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

// ================================================ 1.12.29 : les détails, les lignes ====
//  Les « Détails » de la préparation et de la copie : une ligne par action - son heure, son état
//  (en cours, fait, attention, erreur, ou une information), son texte, court et au tutoiement.
//  Ici, les lignes elles-mêmes, qu'elles viennent de l'assistant ou d'une étape PowerShell
//  (/progres:<fichier>, voir Write-XpgProgres, lu pendant que l'étape tourne : Exec attend en
//  traitant les messages, la minuterie continue). Une ligne qui a une clé se met à jour (« 37 / 151 »,
//  puis « 151 / 151 ») ; chacune va aussi dans le journal de l'installation (/LOG), avec ou sans
//  assistant. Leur dessin : plus bas, « les détails, à l'écran ».
const
  jEnCours = 0;
  jFait = 1;
  jAttention = 2;
  jErreur = 3;
  jInfo = 4;

type
  TLigneJournal = record
    Heure, Cle, Texte: String;
    Etat, Niveau: Integer;
  end;

var
  Lignes: array of TLigneJournal;
  JournalSale: Boolean;
  // le fichier de l'étape PowerShell en cours ('' : aucune), ce qui en a été lu (octets, lignes
  // entières, lignes prises), le numéro de l'étape (ses clés lui sont propres), la dernière lecture
  ProgresFichier: String;
  ProgresOctets, ProgresComplet, ProgresPris, ProgresEtape, ProgresVu: Integer;
  // les lignes « recap » des étapes (« rubrique|niveau|texte ») : pour « Ce qui a été installé »
  RecapPs: TArrayOfString;
  // le début de l'installation (la préparation), pour sa durée
  InstallDebut: DWORD;

function GetTickCount: DWORD;
  external 'GetTickCount@kernel32.dll stdcall setuponly';

function JournalNomEtat(Etat: Integer): String;
begin
  case Etat of
    jEnCours: Result := 'en cours';
    jFait: Result := 'fait';
    jAttention: Result := 'attention';
    jErreur: Result := 'erreur';
  else
    Result := 'info';
  end;
end;

// Une ligne de plus - ou, si sa clé est déjà là, la mise à jour de la sienne (elle garde sa place et
// son heure, celle de son début : la colonne des heures se lit de haut en bas) ; rend son numéro. Le
// journal (/LOG) la reçoit à chaque état.
function JournalEcrire(Etat, Niveau: Integer; Cle, Texte, Heure: String): Integer;
var I, N: Integer;
    Retrait: String;
begin
  Result := -1;
  if Heure = '' then Heure := GetDateTimeString('hh:nn:ss', '-', ':');
  if Niveau > 0 then Retrait := '    ' else Retrait := '';
  if Cle <> '' then
    for I := GetArrayLength(Lignes) - 1 downto 0 do
      if Lignes[I].Cle = Cle then begin
        Result := I;
        Break;
      end;
  if Result >= 0 then begin
    if Lignes[Result].Etat <> Etat then begin
      Log('Détails : ' + Retrait + '[' + JournalNomEtat(Etat) + '] ' + Texte);
      Lignes[Result].Etat := Etat;
      Lignes[Result].Texte := Texte;
      JournalSale := True;
    end else if Lignes[Result].Texte <> Texte then begin
      Lignes[Result].Texte := Texte;
      JournalSale := True;
    end;
    Exit;
  end;
  N := GetArrayLength(Lignes);
  SetArrayLength(Lignes, N + 1);
  Lignes[N].Heure := Heure;
  Lignes[N].Cle := Cle;
  Lignes[N].Texte := Texte;
  Lignes[N].Etat := Etat;
  Lignes[N].Niveau := Niveau;
  Log('Détails : ' + Retrait + '[' + JournalNomEtat(Etat) + '] ' + Texte);
  JournalSale := True;
  Result := N;
end;

procedure JournalNote(Etat: Integer; Cle, Texte: String);
begin
  JournalEcrire(Etat, 0, Cle, Texte, '');
end;

function JournalALaCle(Cle: String): Boolean;
var I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(Lignes) - 1 do
    if Lignes[I].Cle = Cle then Result := True;
end;

procedure JournalDetail(Etat: Integer; Cle, Texte: String);
begin
  JournalEcrire(Etat, 1, Cle, Texte, '');
end;

// Un détail de la ligne Parent, posé sous elle (et sous ses autres détails, « Parent:... ») même si
// d'autres lignes l'ont suivie ; à son heure, pour que les heures se suivent. Sans elle : à la fin.
procedure JournalSous(Etat: Integer; Parent, Cle, Texte: String);
var I, P, N: Integer;
begin
  P := -1;
  for I := 0 to GetArrayLength(Lignes) - 1 do
    if (Lignes[I].Cle = Parent) or (Copy(Lignes[I].Cle, 1, Length(Parent) + 1) = Parent + ':') then P := I;
  N := GetArrayLength(Lignes);
  if (P < 0) or (P = N - 1) or ((Cle <> '') and JournalALaCle(Cle)) then begin
    JournalEcrire(Etat, 1, Cle, Texte, '');
    Exit;
  end;
  SetArrayLength(Lignes, N + 1);
  for I := N downto P + 2 do begin
    Lignes[I].Heure := Lignes[I - 1].Heure;
    Lignes[I].Cle := Lignes[I - 1].Cle;
    Lignes[I].Texte := Lignes[I - 1].Texte;
    Lignes[I].Etat := Lignes[I - 1].Etat;
    Lignes[I].Niveau := Lignes[I - 1].Niveau;
  end;
  Lignes[P + 1].Heure := Lignes[P].Heure;
  Lignes[P + 1].Cle := Cle;
  Lignes[P + 1].Texte := Texte;
  Lignes[P + 1].Etat := Etat;
  Lignes[P + 1].Niveau := 1;
  Log('Détails :     [' + JournalNomEtat(Etat) + '] ' + Texte);
  JournalSale := True;
end;

// Une ligne d'une étape PowerShell : « HH:mm:ss|etat|niveau|cle|texte » (le texte peut avoir des |).
procedure ProgresLigne(L: String);
var P, K, Etat, N: Integer;
    Champ: TArrayOfString;
begin
  SetArrayLength(Champ, 4);
  for K := 0 to 3 do begin
    P := Pos('|', L);
    if P = 0 then Exit;
    Champ[K] := Copy(L, 1, P - 1);
    Delete(L, 1, P);
  end;
  if Champ[1] = 'recap' then begin
    N := GetArrayLength(RecapPs);
    SetArrayLength(RecapPs, N + 1);
    RecapPs[N] := Champ[3] + '|' + IntToStr(StrToIntDef(Champ[2], 0)) + '|' + L;
    Exit;
  end;
  if Champ[1] = 'encours' then Etat := jEnCours
  else if Champ[1] = 'fait' then Etat := jFait
  else if Champ[1] = 'attention' then Etat := jAttention
  else if Champ[1] = 'erreur' then Etat := jErreur
  else Etat := jInfo;
  if Champ[3] <> '' then Champ[3] := 'ps' + IntToStr(ProgresEtape) + ':' + Champ[3];
  JournalEcrire(Etat, StrToIntDef(Champ[2], 0), Champ[3], L, Champ[0]);
end;

// Les lignes nouvelles du fichier de l'étape en cours. Une ligne n'est prise qu'entière (son saut
// de ligne est écrit : PowerShell écrit chaque ligne d'un coup) ; Fin : l'étape est finie, tout est
// pris. Le texte est relu par LoadStringsFromLockedFile (l'UTF-8 y est reconnu ; « locked » :
// PowerShell peut écrire pendant qu'on lit).
procedure ProgresLire(Fin: Boolean);
var Brut: AnsiString;
    Texte: TArrayOfString;
    I, N: Integer;
begin
  if ProgresFichier = '' then Exit;
  if not LoadStringFromLockedFile(ProgresFichier, Brut) then Exit;
  N := Length(Brut);
  for I := ProgresOctets + 1 to N do
    if Ord(Brut[I]) = 10 then ProgresComplet := ProgresComplet + 1;
  ProgresOctets := N;
  if (ProgresComplet <= ProgresPris) and not Fin then Exit;
  if not LoadStringsFromLockedFile(ProgresFichier, Texte) then Exit;
  N := GetArrayLength(Texte);
  if not Fin and (N > ProgresComplet) then N := ProgresComplet;
  for I := ProgresPris to N - 1 do
    if Trim(Texte[I]) <> '' then ProgresLigne(Texte[I]);
  if N > ProgresPris then ProgresPris := N;
end;

procedure ProgresCommencer(Fichier: String; Etape: Integer);
begin
  ProgresFichier := Fichier;
  ProgresOctets := 0;
  ProgresComplet := 0;
  ProgresPris := 0;
  ProgresEtape := Etape;
  ProgresVu := 0;
end;

procedure ProgresFinir;
begin
  try
    ProgresLire(True);
  except
    Log('Détails : ' + GetExceptionMessage);
  end;
  ProgresFichier := '';
end;

// 52220928 -> « 49,8 Mo »
function TailleLisible(T: Extended): String;
begin
  if T >= 1073741824 then Result := Format('%.1f Go', [T / 1073741824])
  else if T >= 1048576 then Result := Format('%.1f Mo', [T / 1048576])
  else if T >= 1024 then Result := IntToStr(Round(T / 1024)) + ' Ko'
  else Result := IntToStr(Round(T)) + ' o';
  StringChangeEx(Result, '.', ',', True);
end;

// 72400 -> « 1 min 12 s »
function DureeLisible(Ms: Integer): String;
var S: Integer;
begin
  S := (Ms + 500) div 1000;
  if S < 60 then Result := IntToStr(S) + ' s'
  else Result := IntToStr(S div 60) + ' min ' + Format('%.2d', [S mod 60]) + ' s';
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
// 1.12.29 : a l'installation, /progres:<fichier> - ses lignes pour les « Détails », lues pendant
// qu'elle tourne (la minuterie de l'assistant), puis les dernieres a la fin (aussi sans assistant :
// elles vont dans le journal). La desinstallation n'en a pas.
function EtapePowerShell(Script, Arguments: String; var Code: Integer; var Demarre: Boolean): Boolean;
var Params, FichierMarque, Progres: String;
begin
  NumeroEtape := NumeroEtape + 1;
  FichierMarque := ExpandConstant('{tmp}') + '\xpg-marque-' + IntToStr(NumeroEtape) + '.txt';
  DeleteFile(FichierMarque);
  Progres := '';
  if not IsUninstaller then begin
    Progres := ExpandConstant('{tmp}') + '\xpg-progres-' + IntToStr(NumeroEtape) + '.txt';
    DeleteFile(Progres);
  end;
  Params := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + Script + '" ' + Arguments +
            ' /journal:"' + JournalInstallation + '" /marque:"' + FichierMarque + '"';
  if Progres <> '' then Params := Params + ' /progres:"' + Progres + '"';
  Log('PowerShell : ' + Params);
  Code := -1;
  ProgresCommencer(Progres, NumeroEtape);
  try
    Result := Exec(PowerShell, Params, '', SW_HIDE, ewWaitUntilTerminated, Code);
  finally
    ProgresFinir;
  end;
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
  Result := FileExists(Dossier + '\{#Exe}') or FileExists(Dossier + '\{#ExeIhm}') or FileExists(Dossier + '\XpgAnalyzer.exe')
            or FileExists(Dossier + '\XpgAnalyzer-API.exe') or FileExists(Dossier + '\XpgAnalyzer-IHM.exe')
            or FileExists(Dossier + '\xpg_analyzer.exe');
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

// 1.12.29 : une rubrique des lignes « recap » des étapes PowerShell (rubrique|niveau|texte), sous son titre.
procedure RecapRubrique(var L: TArrayOfString; Rubrique, Titre, Note: String);
var I, N: Integer;
    S, Niveau: String;
begin
  N := 0;
  for I := 0 to GetArrayLength(RecapPs) - 1 do begin
    S := RecapPs[I];
    if Copy(S, 1, Length(Rubrique) + 1) <> Rubrique + '|' then Continue;
    Delete(S, 1, Length(Rubrique) + 1);
    Niveau := Copy(S, 1, Pos('|', S) - 1);
    Delete(S, 1, Pos('|', S));
    if N = 0 then begin
      if Note <> '' then Ajoute(L, Titre + '   (' + Note + ')') else Ajoute(L, Titre);
    end;
    N := N + 1;
    if Niveau = '0' then Ajoute(L, '  ' + S) else Ajoute(L, '      ' + S);
  end;
  if N > 0 then Ajoute(L, '');
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
  // 1.12.29 : ce que les étapes PowerShell ont fait (leurs lignes « recap ») : la version d'avant,
  // ce qui a été repris et d'où, tes blocs
  RecapRubrique(L, 'avant', 'LA VERSION D''AVANT', '');
  RecapRubrique(L, 'reprise', 'REPRIS DE TES ANCIENNES VERSIONS', 'copié : les originaux ne sont pas modifiés');
  RecapRubrique(L, 'blocs', 'TES BLOCS', '');
  Ajoute(L, 'LES PROGRAMMES');
  Ajoute(L, '  ' + Colonne('{#Exe}', 21) + Colonne('API Studio', 23) + Colonne('64 bits (x64)', 15) + TailleDe(App + '\{#Exe}'));
  Ajoute(L, '  ' + Colonne('{#ExeIhm}', 21) + Colonne('IHM Studio', 23) + Colonne('64 bits (x64)', 15) + TailleDe(App + '\{#ExeIhm}'));
  Ajoute(L, '  ' + Colonne(ExtractFileName(Unins), 17) + Colonne('le désinstallateur', 27) + Colonne('32 bits (x86)', 15) + TailleDe(Unins));
  Ajoute(L, '  ' + Colonne('SDL3.dll', 17) + Colonne('fenêtres, affichage, son', 27) + Colonne('64 bits (x64)', 15) + TailleDe(App + '\SDL3.dll'));
  Ajoute(L, '  ' + Colonne('maintenance\', 17) + '6 scripts (.bat) et leurs étapes PowerShell :');
  Ajoute(L, '  ' + Colonne('', 17) + 'réparer, vérifier, reprendre, restaurer,');
  Ajoute(L, '  ' + Colonne('', 17) + 'journaux, réinitialiser les réglages');
  Ajoute(L, '  Rien d''autre à installer : tout est à côté de l''exe.');
  Ajoute(L, '');
  Ajoute(L, 'LE PROGRAMME   ' + App);
  Ajoute(L, '  ├─ ' + Colonne('{#Exe}', 32) + 'API Studio');
  Ajoute(L, '  ├─ ' + Colonne('{#ExeIhm}', 32) + 'IHM Studio');
  Ajoute(L, '  ├─ ' + Colonne('SDL3.dll', 32));
  Ajoute(L, '  ├─ ' + Colonne(ExtractFileName(Unins) + ', .dat', 32) + 'le désinstallateur');
  Ajoute(L, '  ├─ ' + Colonne('installation.ini', 32) + 'écrit par l''installateur');
  Ajoute(L, '  ├─ ' + Colonne('manifeste.json', 32) + 'chaque fichier, sa taille, son SHA-256');
  Ajoute(L, '  ├─ ' + Colonne('libs\', 32) + 'la bibliothèque livrée');
  Ajoute(L, '  ├─ ' + Colonne('maintenance\', 32) + 'les scripts, lib\ (PowerShell)');
  Ajoute(L, '  ├─ ' + Colonne('resources\', 32) + 'le catalogue des automates');
  Ajoute(L, '  └─ ' + Colonne('licences\', 32) + 'SDL3, miniz, stb, nanosvg, minimp3');
  Ajoute(L, '');
  if IsAdminInstallMode then Ajoute(L, 'TES DONNÉES   Documents\{#Cle} de chaque compte (ici : ' + Don + ')')
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
  // 1.12.29 : le rapport de l'étape d'après la copie (tes données, la reprise, la vérification)
  for N := 0 to GetArrayLength(RecapPs) - 1 do
    if Copy(RecapPs[N], 1, 8) = 'rapport|' then
      Ajoute(L, '  │  ' + Colonne('', 33) + ExtractFileName(Copy(RecapPs[N], 11, Length(RecapPs[N]))) + ' : tes données, la reprise, la vérification');
  Ajoute(L, '  ├─ ' + Colonne('Sauvegardes\', 32) + 'la version d''avant, s''il y en avait une');
  Ajoute(L, '  ├─ ' + Colonne('Reserve\{#Version}\', 32) + 'de quoi réparer sans Internet');
  Ajoute(L, '  └─ ' + Colonne('Etat\', 32));
  Ajoute(L, '');
  Ajoute(L, 'LE MENU DÉMARRER   ' + Menu);
  Ajoute(L, '  ├─ ' + Colonne('{#NomApi}', 32));
  Ajoute(L, '  ├─ ' + Colonne('{#NomIhm}', 32));
  Ajoute(L, '  ├─ ' + Colonne('Désinstaller {#Nom}', 32) + 'la recherche Windows le trouve');
  if not IsAdminInstallMode then Ajoute(L, '  ├─ ' + Colonne('Tes données', 32));
  Ajoute(L, '  └─ ' + Colonne('Maintenance\', 32) + '6 raccourcis');
  Ajoute(L, '');
  if WizardIsTaskSelected('bureau') then S := '{#NomApi}, {#NomIhm}' else S := 'aucun raccourci';
  Ajoute(L, Colonne('LE BUREAU', 20) + S);
  if WizardIsTaskSelected('barre') then S := 'épinglage proposé au premier lancement' else S := 'rien (non demandé)';
  Ajoute(L, Colonne('LA BARRE DES TÂCHES', 20) + S);
  if IsAdminInstallMode then S := 'HKLM' else S := 'HKCU';
  Ajoute(L, Colonne('LE REGISTRE', 20) + S + '\Software\{#Cle}');
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

// 1.12.28 : tout le texte, d'un coup (la fin du défilé, une copie, une panne du mouvement).
procedure RecapTout;
begin
  if (PageRecap = nil) or (RecapVu >= GetArrayLength(RecapTexte)) then Exit;
  RecapVu := GetArrayLength(RecapTexte);
  PageRecap.RichEditViewer.Lines.Text := Joindre(RecapTexte);
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
    RecapTexte := L;
    RecapVu := 0;
    RecapTout;
    PageRecap.SubCaptionLabel.Caption := 'Aussi enregistré dans tes journaux : ' + ExtractFileName(F);
  end;
end;

procedure CopierRecapClick(Sender: TObject);
begin
  if PageRecap = nil then Exit;
  RecapTout;
  // Tout choisir, puis WM_COPY (le presse-papiers de Windows).
  SendMessage(PageRecap.RichEditViewer.Handle, $00B1 {EM_SETSEL}, 0, -1);
  SendMessage(PageRecap.RichEditViewer.Handle, $0301 {WM_COPY}, 0, 0);
  TNewButton(Sender).Caption := 'Copié';
end;

// ============================================================ 1.12.27 : l'habillage ====
//  La colonne de gauche (le logo, le nom, la version, les étapes), l'en-tête des pages, les
//  bandeaux de l'accueil et de la fin ; pendant la copie, l'animation, la barre et le diaporama.
//  Les couleurs (TColor : $00BBGGRR) et les étapes sont décrites ici, et nulle part ailleurs.
//  1.12.28 : tout cela BOUGE - voir « le mouvement », plus bas.
const
  CouleurColonne = $003A1F0D;      // #0D1F3A : le marine des logos
  CouleurTitre = $0047270F;        // #0F2747
  CouleurTexte = $00806B5B;        // #5B6B80
  CouleurFaite = $00EAD3BF;        // le nom d'une étape passée
  CouleurAVenir = $00AD937F;       // le nom d'une étape à venir
  CouleurTurquoise = $00B6C42D;    // #2DC4B6 : le trait entre deux étapes passées
  CouleurLienAVenir = $005E3F2A;   // le trait entre deux étapes à venir
  CouleurPiste = $00F3ECE6;        // #E6ECF3 : la barre, vide
  CouleurCarte = $00FAF6F3;        // #F3F6FA : les cartes de l'accueil, le récapitulatif
  CouleurPoint = $00E2D8CC;        // #CCD8E2 : les points du diaporama
  CouleurAmbre = $0042C5F5;        // #F5C542 : le jaune des logos
  CouleurBleu = $00F6823B;         // #3B82F6
  CouleurRose = $009C78F2;         // #F2789C : une couleur de plus pour les confettis
  FR_PRIVATE = $10;
  SRCCOPY = $00CC0020;
  HALFTONE = 4;
  WM_SETREDRAW = $000B;
  RDW_TOUT = $0185;                // RedrawWindow : INVALIDATE, ERASE, ALLCHILDREN, UPDATENOW
  // Les étapes, dans l'ordre : leur nom | leur icône (images\ui\icone-<nom>.bmp). Une page
  // de plus dans l'assistant : son étape ici (ou une existante), et sa ligne dans EtapeDePage.
  EtapesNoms = 'Bienvenue|Emplacement|Tes données|Bibliothèque|Raccourcis|Récapitulatif|Installation|Terminé';
  EtapesIcones = 'bienvenue|emplacement|donnees|bibliotheque|raccourcis|recapitulatif|installation|termine';
  // 1.12.28 : le diaporama de la copie (une diapositive toutes les 5,6 s) : son image
  // (images\ui\<image>.bmp) | son titre | son texte. Une diapositive de plus = un morceau de plus
  // dans chacune des trois listes. (#160 : une espace insécable, devant les deux-points.)
  DiaposImages = 'diapo-api|diapo-ihm|diapo-versions|diapo-donnees|diapo-aide|diapo-maintenance';
  DiaposTitres = '{#NomApi}|{#NomIhm}|Tes projets gardent leurs versions|Tes données restent à toi|L''aide est toujours là|Une maintenance intégrée';
  DiaposTextes = 'Le programme de ton automate' + #160 + ': analyse, variables, tables et simulation.|' +
                 'Tes vues, tes scripts et tes équipements, simulés sans automate branché.|' +
                 'Compare-les, et reviens en arrière quand tu veux.|' +
                 'Une mise à jour ou une réparation ne les efface jamais.|' +
                 'F1 l''ouvre, dans {#NomApi} comme dans {#NomIhm}.|' +
                 'Menu Démarrer > {#Nom} > Maintenance' + #160 + ': vérifier, réparer, revenir à la version d''avant.';
  DiapoDuree = 5600;
  DiapoFondu = 700;
  // 1.12.28 : les étapes de la colonne, le « rail » (unités 1x : posé en RailX, RailY de la colonne ;
  // l'étape I a son centre à RailPremier + RailPas * I du haut du rail). 1.12.29 : DEUX images côte à
  // côte, coupées en RailCoupe - les marques (elles bougent : la pulsation, l'anneau de la copie) et
  // les noms (redessinés seulement quand une étape change d'état) : la pulsation d'une marque ne
  // redessine jamais le nom à côté d'elle.
  RailX = 12;
  RailY = 140;
  RailL = 208;
  RailH = 274;
  RailPremier = 18;
  RailPas = 34;
  RailMarqueX = 22;
  RailCoupe = 40;
  RailTexteX = 46;
  // le décor du bas de la colonne : sa hauteur (images\ui\decor.bmp, 1x)
  DecorH = {#DecorH};
  // 1.12.29 : la taille de l'assistant (unités 1x : des pixels à 96 ppp avec sa police, Tahoma 8 ;
  // voir Dimensionner), la largeur de la colonne (images\ui\habillage.ini)
  FenetreL = 912;
  FenetreH = 580;
  ColonneL = {#ColonneL};
  GWL_EXSTYLE = -20;
  WS_EX_COMPOSITED = $02000000;
  SPI_GETWORKAREA = $0030;
  // 1.12.29 : les détails, à l'écran (unités 1x) : l'en-tête, une ligne, ses colonnes (l'heure, le
  // centre de la marque, le texte ; le retrait du niveau 1), les couleurs de l'heure et des états
  JournalTeteH = 30;
  JournalLigneH = 19;
  JournalHeureX = 14;
  JournalMarqueX = 70;
  JournalTexteX = 84;
  JournalRetrait = 16;
  CouleurHeure = $00A08E7E;        // #7E8EA0
  CouleurAttention = $00006A9A;    // #9A6A00
  CouleurErreur = $002828C6;       // #C62828
  // les courbes (voir Courbe) et ce qu'une transition change (voir TransitionAjouter) ; 1.12.29 : un
  // texte ne change plus de couleur en arrivant (il se redessinait à chaque pas) : il apparaît
  // (gVisible), puis glisse
  cLineaire = 0;
  cSortie = 1;
  cRebond = 2;
  cDouce = 3;
  gPosition = 0;
  gBornes = 1;
  gVisible = 2;
  gLargeur = 3;

type
  TEntiers = array of Integer;
  // Une transition : un contrôle va d'un état (A) à un autre (B) en Duree ms, après Delai ms,
  // selon une courbe (Allure). Debut : -1 tant que la minuterie ne l'a pas vue.
  TTransition = record
    Cible: TControl;
    Genre, Allure, Delai, Duree, Debut: Integer;
    A0, A1, A2, A3, B0, B1, B2, B3: Integer;
  end;
  // Un calque : une image, ou une bande d'images l'une sous l'autre (N images de L x H), à
  // l'échelle des images (1x ou 2x).
  TSprite = record
    Bmp: TBitmap;
    L, H, N: Integer;
  end;
  // Une scène : une image recomposée par la minuterie. Travail : à l'échelle des images (LT x HT) ;
  // Image : à la taille de l'écran (le même bitmap quand les tailles sont égales).
  TScene = record
    Image: TBitmapImage;
    Travail: TBitmap;
    LT, HT, Debut, Dernier: Integer;
    Active: Boolean;
  end;
  // Un confetti de la fin (unités 1x ; ms) : son départ, sa vitesse, sa rotation.
  TConfetti = record
    X, Y, VX, VY, Tour, Phase: Extended;
    Couleur, Taille: Integer;
  end;
  // 1.12.29 : un texte qui change souvent (le pourcentage, le fichier en cours, le temps qui reste) :
  // une image où il s'écrit, sur son fond - un changement, un seul dessin, sans effacer (un TLabel
  // s'efface avant de se redessiner : il clignotait). Aligne : 0 à gauche, 1 à droite, 2 au centre.
  TTexteVif = record
    Image: TBitmapImage;
    Texte, Graisse: String;
    Taille, Couleur, Fond, Aligne: Integer;
  end;

var
  PolicesOk, Ui2x: Boolean;
  LargeurColonne, NbEtapes, Echelle: Integer;
  ColonneEtapes: TPanel;
  EnTeteIcone, EnTeteTrait: TBitmapImage;
  EnTeteCadre: TPanel;
  AnimBande: TBitmap;
  AnimN, AnimL, AnimH, CopieX, CopieY: Integer;
  BarreDegrade: TBitmapImage;
  BarrePiste, BarrePleine: TPanel;
  PreparationTexte: TLabel;
  VifPourcent, VifFichier, VifReste: TTexteVif;
  // 1.12.29 : la fenêtre double-tamponnée (son handle : s'il change, on recommence) ; /COMPOSITION=non
  FenetreComposee: HWND;
  SansComposition, Dimensionne: Boolean;
  // 1.12.28 : le moteur
  HorlogeBase, Minuterie, RappelTic: LongWord;
  MoteurEnPanne, EnTic: Boolean;
  Transitions: array of TTransition;
  SensPage, PageAvant: Integer;
  NotebookX, NotebookY, TitreX, TitreY, DescX, DescY, IconeX, IconeY, IconeT: Integer;
  // les scènes et leurs calques (1.12.29 : le rail, ses marques ; RailNoms, les noms des étapes)
  Rail, RailNoms, Decor, Heros, Succes, Copie: TScene;
  SpriteAvenir, SpriteArrivee, SpritePulse, SpriteCoche, SpriteAnneau, SpritePastille: TSprite;
  SpriteLueur, SpriteLueurT, SpriteApi, SpriteIhm, SpritePoints, SpriteEclat: TSprite;
  SpriteDisque, SpriteCocheFin, SpriteEtincA, SpriteEtincB, SpriteBarreEclat: TSprite;
  FondAccueil, FondFin, FondDecor, BarreFond: TBitmap;
  // la colonne : la progression P (en étapes : 2,5 = entre la 3e et la 4e) et sa cible ;
  // l'état de chaque étape (0 à venir, 1 en cours, 2 faite), le précédent, depuis quand, la
  // couleur de son nom avant le changement
  RailP, RailP0, RailCible, RailPastille, RailPastille0: Extended;
  RailEtat, RailAvant, RailDepuis, RailCouleur: TEntiers;
  RailTemps, RailVu, RailHorloge, RailDepart, RailDuree, EtapeCopie: Integer;
  // 1.12.29 : quand les noms ont été dessinés la dernière fois (leur fin de changement doit l'être,
  // même si la minuterie s'est arrêtée entre-temps : une sortie de fichiers, une étape qui bloque)
  RailNomsVu: Integer;
  // le décor : ses pistes (à la suite), trois impulsions qui les parcourent
  PisteX, PisteY, PistePremier, PisteNb: TEntiers;
  ImpPiste, ImpDebut, ImpDuree, ImpSens: TEntiers;
  ImpProchaine: Integer;
  // l'accueil : le réseau (noeuds X, Y, rayon en dixièmes ; liens A-B), les deux logos, les points
  HerosNX, HerosNY, HerosNR, HerosLA, HerosLB, HerosPX, HerosPY, HerosImpLien, HerosImpDebut: TEntiers;
  HerosApiX, HerosApiY, HerosIhmX, HerosIhmY, HerosImpProchaine: Integer;
  AccTitreX, AccTitreY, AccTexteX, AccTexteY, AccEclat0, AccEclat1: Integer;
  CartePanneau: array of TPanel;
  CarteLogo: array of TBitmapImage;
  CarteTitre, CarteTexte: array of TLabel;
  CarteX, CarteY, CarteLogoX, CarteLogoY, CarteLogoL, CarteLogoH: TEntiers;
  // la fin : le réseau, les étincelles (X, Y, taille, couleur), le cercle, la coche, les confettis
  FinNX, FinNY, FinNR, FinEX, FinEY, FinES, FinEC: TEntiers;
  FinDisqueX, FinDisqueY, FinCocheX, FinCocheY, FinCX, FinCY, FinH1, FinEclat0, FinEclat1: Integer;
  Confettis: array of TConfetti;
  // la copie : la barre (sa cible, ce qui est affiché), le temps qui reste, le fichier, le diaporama
  BarreCible, BarreAffiche, CopieEtaLisse: Extended;
  CopieDebut, CopieVu, BarreVu: Integer;
  CopieEnCours: Boolean;
  AppDossier, ReserveDossier: String;
  DiapoCarte: TPanel;
  DiapoImage: TBitmapImage;
  DiapoCache: array of TBitmap;
  DiapoActif: Boolean;
  DiapoDebut, DiapoVu, DiapoY, DiapoH: Integer;
  // 1.12.29 : la copie, pour les détails - le groupe en cours (1 le programme, 2 la réserve, 3 les
  // raccourcis, 4 le registre, 5 la désinstallation), ses fichiers vus, le dernier fichier et le
  // dernier état vus, la ligne du gros fichier en cours
  CopieGroupe, CopieN: Integer;
  CopieVuFichier, CopieVuStatut, CopieGros, CopieGrosTexte: String;
  // 1.12.29 : les détails, à l'écran - l'image (une scène), la roue (la ligne en cours la plus
  // récente), les marques par état ; la ligne du bas à l'écran (fractionnaire pendant le
  // défilement), le dernier dessin, la taille en unités 1x, la page où ils sont, la phase
  Journal, JournalRoue: TScene;
  SpriteJRoue: TSprite;
  JournalPos: Extended;
  JournalVu, JournalHorloge, JournalL1, JournalH1, JournalRoueVu, JournalY: Integer;
  JournalActif: Boolean;
  JournalPhase: String;

function AddFontResourceEx(Fichier: String; Drapeaux: DWORD; Reserve: Cardinal): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall setuponly';
function BitBlt(Dest: LongWord; X, Y, L, H: Integer; Source: LongWord; XS, YS: Integer; Operation: DWORD): BOOL;
  external 'BitBlt@gdi32.dll stdcall setuponly';
function CreateRoundRectRgn(X1, Y1, X2, Y2, LE, HE: Integer): LongWord;
  external 'CreateRoundRectRgn@gdi32.dll stdcall setuponly';
function SetWindowRgn(Fenetre: HWND; Region: LongWord; Redessiner: BOOL): Integer;
  external 'SetWindowRgn@user32.dll stdcall setuponly';
// 1.12.28 : le dessin des calques (AlphaBlend : l'alpha prémultiplié des BMP 32 bits ; le temps,
// GetTickCount, est déclaré plus haut, avec les lignes des détails)
function GdiAlphaBlend(Dest: LongWord; X, Y, L, H: Integer; Source: LongWord; XS, YS, LS, HS: Integer; Melange: DWORD): BOOL;
  external 'GdiAlphaBlend@gdi32.dll stdcall setuponly';
function StretchBlt(Dest: LongWord; X, Y, L, H: Integer; Source: LongWord; XS, YS, LS, HS: Integer; Operation: DWORD): BOOL;
  external 'StretchBlt@gdi32.dll stdcall setuponly';
function SetStretchBltMode(DC: LongWord; Mode: Integer): Integer;
  external 'SetStretchBltMode@gdi32.dll stdcall setuponly';
function SetBrushOrgEx(DC: LongWord; X, Y: Integer; Ancien: LongWord): BOOL;
  external 'SetBrushOrgEx@gdi32.dll stdcall setuponly';
function IsIconic(Fenetre: HWND): Integer;
  external 'IsIconic@user32.dll stdcall setuponly';
function RedrawWindow(Fenetre: HWND; Zone, Region: LongWord; Drapeaux: Cardinal): BOOL;
  external 'RedrawWindow@user32.dll stdcall setuponly';
// 1.12.29 : la fenêtre double-tamponnée (WS_EX_COMPOSITED), la zone de travail de l'écran (la taille
// de l'assistant), la découpe d'un dessin (les lignes des détails sous leur en-tête)
function GetWindowLong(Fenetre: HWND; Index: Integer): Longint;
  external 'GetWindowLongW@user32.dll stdcall setuponly';
function SetWindowLong(Fenetre: HWND; Index: Integer; Valeur: Longint): Longint;
  external 'SetWindowLongW@user32.dll stdcall setuponly';
function SystemParametersInfo(Action, Param: Cardinal; var Zone: TRect; WinIni: Cardinal): BOOL;
  external 'SystemParametersInfoW@user32.dll stdcall setuponly';
function IntersectClipRect(DC: LongWord; X1, Y1, X2, Y2: Integer): Integer;
  external 'IntersectClipRect@gdi32.dll stdcall setuponly';
function SelectClipRgn(DC: LongWord; Region: LongWord): Integer;
  external 'SelectClipRgn@gdi32.dll stdcall setuponly';

// Le N-ième morceau (0...) de 'a|b|c' (ou d'une liste à un autre séparateur), et leur nombre.
function MorceauDe(S: String; N: Integer; Sep: String): String;
var P: Integer;
begin
  while N > 0 do begin
    P := Pos(Sep, S);
    if P = 0 then begin
      S := '';
      Break;
    end;
    Delete(S, 1, P);
    N := N - 1;
  end;
  P := Pos(Sep, S);
  if P > 0 then Result := Copy(S, 1, P - 1) else Result := S;
end;

function Morceau(S: String; N: Integer): String;
begin
  Result := MorceauDe(S, N, '|');
end;

function NombreDe(S, Sep: String): Integer;
var I: Integer;
begin
  Result := 0;
  if S = '' then Exit;
  Result := 1;
  for I := 1 to Length(S) do
    if S[I] = Sep[1] then Result := Result + 1;
end;

function NombreDeMorceaux(S: String): Integer;
begin
  Result := NombreDe(S, '|');
end;

// Le N-ième entier (0...) de 'a,b,c'.
function Nombre(S: String; N: Integer): Integer;
begin
  Result := StrToIntDef(Trim(MorceauDe(S, N, ',')), 0);
end;

function Mini(A, B: Integer): Integer;
begin
  if A < B then Result := A else Result := B;
end;

function Maxi(A, B: Integer): Integer;
begin
  if A > B then Result := A else Result := B;
end;

// Une image de l'habillage : en 2x dès que l'écran est agrandi (réduite, elle reste nette), sinon
// en 1x. 1.12.28 : toutes celles d'une échelle sont sorties au démarrage (Habiller) - jamais à la
// demande : pendant la copie, l'extracteur d'Inno Setup est occupé (et ne se rappelle pas lui-même).
function ImageUi(Nom: String): String;
begin
  if Ui2x then Result := ExpandConstant('{tmp}\ui2x\') + Nom + '-2x.bmp'
  else Result := ExpandConstant('{tmp}\ui\') + Nom + '.bmp';
end;

function NouvelleImage(Parent: TWinControl; Nom: String; X, Y, L, H: Integer): TBitmapImage;
begin
  Result := TBitmapImage.Create(WizardForm);
  Result.Parent := Parent;
  Result.SetBounds(X, Y, L, H);
  Result.Stretch := True;
  if Nom <> '' then Result.Bitmap.LoadFromFile(ImageUi(Nom));
end;

// La police : Barlow (Graisse : '', 'Medium', 'SemiBold'), ou celle de Windows si elle n'a pas pu
// être chargée.
procedure Ecrire(F: TFont; Graisse: String; Taille: Integer; Couleur: TColor);
begin
  if not PolicesOk then begin
    F.Name := 'Segoe UI';
    if Graisse = 'SemiBold' then F.Style := [fsBold] else F.Style := [];
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

function NouveauPanneau(Parent: TWinControl; X, Y, L, H: Integer; Couleur: TColor): TPanel;
begin
  Result := TPanel.Create(WizardForm);
  Result.Parent := Parent;
  Result.SetBounds(X, Y, L, H);
  Result.BevelOuter := bvNone;
  Result.ParentBackground := False;
  Result.Color := Couleur;
end;

procedure Arrondir(P: TWinControl; Rayon: Integer);
begin
  SetWindowRgn(P.Handle, CreateRoundRectRgn(0, 0, P.Width + 1, P.Height + 1, Rayon, Rayon), True);
end;

procedure ChargerPolices;
var I: Integer;
    Nom: String;
begin
  PolicesOk := False;
  try
    ExtractTemporaryFiles('{tmp}\Barlow-*.ttf');   // 1.12.28 : les quatre d'un coup
  except
    Log('Police non chargée (' + GetExceptionMessage + ')');
    Exit;
  end;
  for I := 0 to 3 do begin
    Nom := Morceau('Barlow-Regular.ttf|Barlow-Medium.ttf|Barlow-SemiBold.ttf|Barlow-Bold.ttf', I);
    if AddFontResourceEx(ExpandConstant('{tmp}\') + Nom, FR_PRIVATE, 0) > 0 then PolicesOk := True
    else Log('Police non chargée : ' + Nom);
  end;
end;

// L'étape d'une page (voir EtapesNoms).
function EtapeDePage(PageID: Integer): Integer;
begin
  if PageID = wpWelcome then Result := 0
  else if PageID = wpSelectDir then Result := 1
  else if ((PageDonnees <> nil) and (PageID = PageDonnees.ID)) or ((PageReprise <> nil) and (PageID = PageReprise.ID))
          or ((PageAutre <> nil) and (PageID = PageAutre.ID)) then Result := 2
  else if (PageBiblio <> nil) and (PageID = PageBiblio.ID) then Result := 3
  else if PageID = wpSelectTasks then Result := 4
  else if PageID = wpReady then Result := 5
  else if (PageID = wpPreparing) or (PageID = wpInstalling) then Result := 6
  else Result := 7;   // « Ce qui a été installé » et la fin
end;

// ======================================================== 1.12.28 : le mouvement ====
//  Tout ce qui bouge passe par un seul moteur : UNE minuterie (SetTimer + CreateCallback, un tic
//  toutes les 15 ms, voir Tic) et deux sortes d'animations :
//   - les TRANSITIONS (Transitions[]) : un contrôle va d'un état à un autre en un temps donné,
//     selon une courbe - il glisse (Arriver), grandit (Grandir), change de couleur (Teinter) ou
//     de largeur (Elargir). Une page qui arrive, un titre qui apparaît, une carte qui monte ;
//   - les SCÈNES : une image recomposée à chaque tic - les étapes de la colonne (Rail), le décor
//     du bas de la colonne (Decor), l'accueil (Heros), la fin (Succes), l'animation de la copie
//     (Copie) ; pendant la copie, la barre et le diaporama. Une scène dessine dans son image de
//     TRAVAIL, à l'échelle des images (1x, ou 2x dès que l'écran est agrandi), puis la pose à
//     l'écran (réduite en HALFTONE si la taille diffère : nette à 100, 125, 150, 200 %) ; ses
//     textes s'écrivent ensuite, à la taille de l'écran.
//  Les calques (images\ui\*.bmp, 32 bits) et leurs repères (images\ui\habillage.ini) viennent de
//  outils\installateur_images.py. Le temps : GetTickCount ; une animation créée avant que
//  l'assistant soit à l'écran part au premier tic où il l'est (rien ne se joue en coulisse).
//  Sans assistant (/SILENT, /VERYSILENT) : rien n'est créé, la minuterie ne démarre jamais.
//  Une erreur pendant un tic : notée dans le journal, tout se met à son état final et s'arrête
//  (rien ne reste à moitié) ; l'installation continue.

// ------------------------------------------------------------- le temps, les courbes ----
function Horloge: Integer;
begin
  Result := GetTickCount - HorlogeBase;
end;

function Borne(F: Extended): Extended;
begin
  if F < 0 then Result := 0
  else if F > 1 then Result := 1
  else Result := F;
end;

// De 0 à 1 : cSortie (vite, puis doucement), cRebond (dépasse un peu, puis revient), cDouce
// (doucement au début et à la fin), cLineaire.
function Courbe(C: Integer; T: Extended): Extended;
var U: Extended;
begin
  if T <= 0 then begin
    Result := 0;
    Exit;
  end;
  if T >= 1 then begin
    Result := 1;
    Exit;
  end;
  case C of
    cSortie: begin
        U := 1 - T;
        Result := 1 - U * U * U;
      end;
    cRebond: begin
        U := T - 1;
        Result := 1 + 2.70158 * U * U * U + 1.70158 * U * U;
      end;
    cDouce: Result := (1 - Cos(Pi * T)) / 2;
  else
    Result := T;
  end;
end;

function Entre(A, B: Integer; F: Extended): Integer;
begin
  Result := A + Round((B - A) * F);
end;

function CouleurEntre(A, B: Integer; F: Extended): Integer;
begin
  F := Borne(F);
  Result := Entre(A and $FF, B and $FF, F) or (Entre((A shr 8) and $FF, (B shr 8) and $FF, F) shl 8)
            or (Entre((A shr 16) and $FF, (B shr 16) and $FF, F) shl 16);
end;

// ------------------------------------------------------------------ les transitions ----
// 1.12.29 : rien n'est écrit qui ne change pas (une position, une taille, la visibilité).
procedure TransitionAppliquer(I: Integer; F: Extended);
var C: TControl;
    X, Y, L, H: Integer;
begin
  C := Transitions[I].Cible;
  if C = nil then Exit;
  case Transitions[I].Genre of
    gPosition: begin
        X := Entre(Transitions[I].A0, Transitions[I].B0, F);
        Y := Entre(Transitions[I].A1, Transitions[I].B1, F);
        if (X <> C.Left) or (Y <> C.Top) then C.SetBounds(X, Y, C.Width, C.Height);
      end;
    gBornes: begin
        X := Entre(Transitions[I].A0, Transitions[I].B0, F);
        Y := Entre(Transitions[I].A1, Transitions[I].B1, F);
        L := Entre(Transitions[I].A2, Transitions[I].B2, F);
        H := Entre(Transitions[I].A3, Transitions[I].B3, F);
        if (X <> C.Left) or (Y <> C.Top) or (L <> C.Width) or (H <> C.Height) then C.SetBounds(X, Y, L, H);
      end;
    gVisible: if C.Visible <> (F > 0) then C.Visible := F > 0;
    gLargeur: begin
        L := Maxi(0, Entre(Transitions[I].A0, Transitions[I].B0, F));
        if L <> C.Width then C.Width := L;
      end;
  end;
end;

// Une transition de plus (elle remplace celle qui changeait déjà la même chose du même contrôle) ;
// le contrôle prend aussitôt son état de départ (le premier dessin de la page le montre ainsi).
procedure TransitionAjouter(Cible: TControl; Genre, Allure, Delai, Duree, A0, A1, A2, A3, B0, B1, B2, B3: Integer);
var I, Libre: Integer;
begin
  if Cible = nil then Exit;
  Libre := -1;
  for I := 0 to GetArrayLength(Transitions) - 1 do
    if (Transitions[I].Cible = nil) or ((Transitions[I].Cible = Cible) and (Transitions[I].Genre = Genre)) then begin
      Transitions[I].Cible := nil;
      if Libre < 0 then Libre := I;
    end;
  if Libre < 0 then begin
    Libre := GetArrayLength(Transitions);
    SetArrayLength(Transitions, Libre + 1);
  end;
  Transitions[Libre].Cible := Cible;
  Transitions[Libre].Genre := Genre;
  Transitions[Libre].Allure := Allure;
  Transitions[Libre].Delai := Delai;
  Transitions[Libre].Duree := Maxi(1, Duree);
  Transitions[Libre].Debut := -1;
  Transitions[Libre].A0 := A0;
  Transitions[Libre].A1 := A1;
  Transitions[Libre].A2 := A2;
  Transitions[Libre].A3 := A3;
  Transitions[Libre].B0 := B0;
  Transitions[Libre].B1 := B1;
  Transitions[Libre].B2 := B2;
  Transitions[Libre].B3 := B3;
  if MoteurEnPanne then begin
    TransitionAppliquer(Libre, 1);
    Transitions[Libre].Cible := nil;
  end else
    TransitionAppliquer(Libre, 0);
end;

// C arrive en (X, Y), depuis (X + DX, Y + DY).
procedure Arriver(C: TControl; X, Y, DX, DY, Delai, Duree, Allure: Integer);
begin
  TransitionAjouter(C, gPosition, Allure, Delai, Duree, X + DX, Y + DY, 0, 0, X, Y, 0, 0);
end;

// 1.12.29 : C apparaît après Delai ms (caché jusque-là). Avant, un texte arrivait en changeant de
// couleur : il se redessinait à chaque pas (sur l'écran, il clignotait).
procedure Apparaitre(C: TControl; Delai: Integer);
begin
  TransitionAjouter(C, gVisible, cLineaire, Delai, 1, 0, 0, 0, 0, 1, 0, 0, 0);
end;

// C grandit jusqu'à (X, Y, L, H), depuis Pourcent % de sa taille, autour de son centre, avec un
// petit rebond à la fin.
procedure Grandir(C: TControl; X, Y, L, H, Pourcent, Delai, Duree: Integer);
var L0, H0: Integer;
begin
  L0 := Maxi(1, L * Pourcent div 100);
  H0 := Maxi(1, H * Pourcent div 100);
  TransitionAjouter(C, gBornes, cRebond, Delai, Duree, X + (L - L0) div 2, Y + (H - H0) div 2, L0, H0, X, Y, L, H);
end;

// La largeur de C passe de De à Vers.
procedure Elargir(C: TControl; De, Vers, Delai, Duree: Integer);
begin
  TransitionAjouter(C, gLargeur, cDouce, Delai, Duree, De, 0, 0, 0, Vers, 0, 0, 0);
end;

// Tout à son état final (une panne).
procedure TransitionsFinir;
var I: Integer;
begin
  for I := 0 to GetArrayLength(Transitions) - 1 do
    if Transitions[I].Cible <> nil then begin
      try
        TransitionAppliquer(I, 1);
      except
        Log('Habillage : ' + GetExceptionMessage);
      end;
      Transitions[I].Cible := nil;
    end;
end;

procedure TicTransitions(Ms: Integer);
var I, T: Integer;
begin
  for I := 0 to GetArrayLength(Transitions) - 1 do
    if Transitions[I].Cible <> nil then begin
      if Transitions[I].Debut < 0 then Transitions[I].Debut := Ms + Transitions[I].Delai;
      T := Ms - Transitions[I].Debut;
      if T >= Transitions[I].Duree then begin
        TransitionAppliquer(I, 1);
        Transitions[I].Cible := nil;
      end else if T >= 0 then
        TransitionAppliquer(I, Courbe(Transitions[I].Allure, T * 1.0 / Transitions[I].Duree));
    end;
end;

// ----------------------------------------------------------- les calques, les scènes ----
// Le nombre d'images d'une bande (images\ui\habillage.ini, lu à la compilation).
function NombreImages(Nom: String): Integer;
var S: String;
    P: Integer;
begin
  Result := 1;
  S := ';{#HabBandes};';
  P := Pos(';' + Nom + ':', S);
  if P = 0 then Exit;
  Delete(S, 1, P + Length(Nom) + 1);
  P := Pos(';', S);
  if P > 0 then S := Copy(S, 1, P - 1);
  Result := Maxi(1, Nombre(S, 2));
end;

procedure SpriteCharger(var S: TSprite; Nom: String);
begin
  S.Bmp := TBitmap.Create;
  S.Bmp.LoadFromFile(ImageUi(Nom));
  S.N := NombreImages(Nom);
  S.L := S.Bmp.Width;
  S.H := S.Bmp.Height div S.N;
end;

function FondCharger(Nom: String): TBitmap;
begin
  Result := TBitmap.Create;
  Result.LoadFromFile(ImageUi(Nom));
end;

// L'image K (0...) de la bande S, posée en (X, Y) de Dest et étirée en L x H (les lueurs, douces :
// l'étirement ne se voit pas), opacité Alpha (0 à 255).
procedure SpritePoserTaille(Dest: TBitmap; var S: TSprite; K, X, Y, L, H, Alpha: Integer);
begin
  if (S.Bmp = nil) or (Alpha <= 0) or (L <= 0) or (H <= 0) then Exit;
  if Alpha > 255 then Alpha := 255;
  if K < 0 then K := 0;
  if K >= S.N then K := S.N - 1;
  GdiAlphaBlend(Dest.Canvas.Handle, X, Y, L, H, S.Bmp.Canvas.Handle, 0, K * S.H, S.L, S.H, $01000000 or (Alpha shl 16));
end;

procedure SpritePoser(Dest: TBitmap; var S: TSprite; K, X, Y, Alpha: Integer);
begin
  SpritePoserTaille(Dest, S, K, X, Y, S.L, S.H, Alpha);
end;

procedure Remplir(C: TCanvas; X1, Y1, X2, Y2, Couleur: Integer);
var R: TRect;
begin
  if (X2 <= X1) or (Y2 <= Y1) then Exit;
  R.Left := X1;
  R.Top := Y1;
  R.Right := X2;
  R.Bottom := Y2;
  C.Brush.Style := bsSolid;
  C.Brush.Color := Couleur;
  C.FillRect(R);
end;

// Une image réduite (ou agrandie) proprement (HALFTONE) : de Source (LS x HS) vers Dest (L x H).
procedure Reduire(Dest: LongWord; X, Y, L, H: Integer; Source: LongWord; LS, HS: Integer);
begin
  SetStretchBltMode(Dest, HALFTONE);
  SetBrushOrgEx(Dest, 0, 0, 0);
  StretchBlt(Dest, X, Y, L, H, Source, 0, 0, LS, HS, SRCCOPY);
end;

// Une scène : son image à l'écran (X, Y, L x H, dans Parent) et son image de travail (LT x HT).
procedure ScenePreparer(var S: TScene; Parent: TWinControl; X, Y, L, H, LT, HT: Integer);
begin
  S.Image := TBitmapImage.Create(WizardForm);
  S.Image.Parent := Parent;
  S.Image.SetBounds(X, Y, L, H);
  S.Image.Bitmap.Width := L;
  S.Image.Bitmap.Height := H;
  S.LT := LT;
  S.HT := HT;
  if (LT = L) and (HT = H) then S.Travail := S.Image.Bitmap
  else begin
    S.Travail := TBitmap.Create;
    S.Travail.Width := LT;
    S.Travail.Height := HT;
  end;
  S.Debut := -1;
  S.Dernier := -100000;
  S.Active := False;
end;

// Le travail à l'écran (réduit s'il le faut) ; l'appelant écrit ensuite ses textes, puis Repaint.
procedure SceneMontrer(var S: TScene);
begin
  if S.Travail <> S.Image.Bitmap then
    Reduire(S.Image.Bitmap.Canvas.Handle, 0, 0, S.Image.Bitmap.Width, S.Image.Bitmap.Height, S.Travail.Canvas.Handle, S.LT, S.HT);
end;

// Écrit Texte en (X, Y), coupé aux espaces pour tenir dans la largeur L ; rend la hauteur écrite.
function TexteCoupe(C: TCanvas; X, Y, L: Integer; Texte: String): Integer;
var Ligne, Mot: String;
    P, H: Integer;
begin
  Result := 0;
  H := C.TextHeight('Ég');
  C.Brush.Style := bsClear;
  Ligne := '';
  while Texte <> '' do begin
    P := Pos(' ', Texte);
    if P = 0 then begin
      Mot := Texte;
      Texte := '';
    end else begin
      Mot := Copy(Texte, 1, P - 1);
      Delete(Texte, 1, P);
    end;
    if (Ligne <> '') and (C.TextWidth(Ligne + ' ' + Mot) > L) then begin
      C.TextOut(X, Y + Result, Ligne);
      Result := Result + H;
      Ligne := Mot;
    end else if Ligne = '' then Ligne := Mot
    else Ligne := Ligne + ' ' + Mot;
  end;
  if Ligne <> '' then begin
    C.TextOut(X, Y + Result, Ligne);
    Result := Result + H;
  end;
end;

// 1.12.29 : un texte qui tient dans L - trop long, il perd son milieu (« … ») : le début et la fin
// d'un chemin, d'un compte, restent lisibles.
function TexteTenu(C: TCanvas; S: String; L: Integer): String;
var A, B: Integer;
begin
  Result := S;
  if (L <= 0) or (C.TextWidth(S) <= L) then Exit;
  A := Length(S) div 2;
  B := A + 1;
  while (A > 1) and (B <= Length(S)) and (C.TextWidth(Copy(S, 1, A) + '…' + Copy(S, B, Length(S))) > L) do
    if (B - A) mod 2 = 1 then A := A - 1 else B := B + 1;
  Result := Copy(S, 1, A) + '…' + Copy(S, B, Length(S));
end;

// 1.12.29 : un texte qui change souvent, dans son image (voir TTexteVif) : écrit seulement s'il change.
procedure TexteVifPoser(var T: TTexteVif; S: String);
var C: TCanvas;
    X, L, H: Integer;
begin
  if (T.Image = nil) or (S = T.Texte) then Exit;
  T.Texte := S;
  C := T.Image.Bitmap.Canvas;
  L := T.Image.Bitmap.Width;
  H := T.Image.Bitmap.Height;
  Remplir(C, 0, 0, L, H, T.Fond);
  if S <> '' then begin
    Ecrire(C.Font, T.Graisse, T.Taille, T.Couleur);
    S := TexteTenu(C, S, L);
    if T.Aligne = 1 then X := L - C.TextWidth(S)
    else if T.Aligne = 2 then X := (L - C.TextWidth(S)) div 2
    else X := 0;
    C.Brush.Style := bsClear;
    C.TextOut(X, (H - C.TextHeight('Ég')) div 2, S);
  end;
  T.Image.Repaint;
end;

procedure TexteVifCreer(var T: TTexteVif; Parent: TWinControl; X, Y, L, H: Integer; Graisse: String; Taille, Couleur, Fond, Aligne: Integer);
begin
  T.Image := TBitmapImage.Create(WizardForm);
  T.Image.Parent := Parent;
  T.Image.SetBounds(X, Y, L, H);
  T.Image.Bitmap.Width := L;
  T.Image.Bitmap.Height := H;
  T.Graisse := Graisse;
  T.Taille := Taille;
  T.Couleur := Couleur;
  T.Fond := Fond;
  T.Aligne := Aligne;
  T.Texte := '-';
  TexteVifPoser(T, '');
end;

// Les noeuds d'un réseau, lus dans habillage.ini : 'x,y,r;x,y,r...' (r : dixièmes de point).
procedure LireNoeuds(S: String; var X, Y, R: TEntiers);
var I, N: Integer;
    U: String;
begin
  N := NombreDe(S, ';');
  SetArrayLength(X, N);
  SetArrayLength(Y, N);
  SetArrayLength(R, N);
  for I := 0 to N - 1 do begin
    U := MorceauDe(S, I, ';');
    X[I] := Nombre(U, 0);
    Y[I] := Nombre(U, 1);
    R[I] := Nombre(U, 2);
  end;
end;

// Une liste de couples 'a,b;a,b...' (les liens d'un réseau, les points du lien de l'accueil).
procedure LireCouples(S: String; var A, B: TEntiers);
var I, N: Integer;
    U: String;
begin
  N := NombreDe(S, ';');
  SetArrayLength(A, N);
  SetArrayLength(B, N);
  for I := 0 to N - 1 do begin
    U := MorceauDe(S, I, ';');
    A[I] := Nombre(U, 0);
    B[I] := Nombre(U, 1);
  end;
end;

// Les noeuds d'un réseau s'allument (de gauche à droite, à l'entrée), puis scintillent.
procedure DessinerNoeuds(B: TBitmap; var X, Y, R: TEntiers; T: Integer);
var K, L, A: Integer;
    Entree, Scint: Extended;
begin
  for K := 0 to GetArrayLength(X) - 1 do begin
    Entree := Borne((T - 80 - X[K] * 1.6) / 380.0);
    Scint := 0.5 + 0.5 * Sin(2 * Pi * (T / 2600.0 + K * 0.137));
    A := Round(255 * Entree * (0.18 + 0.55 * Scint * Scint));
    L := Round(R[K] * 0.55 * Echelle);
    SpritePoserTaille(B, SpriteLueur, 0, X[K] * Echelle - L div 2, Y[K] * Echelle - L div 2, L, L, A);
  end;
end;

// L'éclat : une bande de lumière traverse le bandeau (de X0 à X1, unités 1x), à partir de Debut,
// puis toutes les Periode ms.
procedure DessinerEclat(B: TBitmap; T, Debut, Periode, X0, X1: Integer);
var U: Extended;
begin
  if T < Debut then Exit;
  U := ((T - Debut) mod Periode) / 950.0;
  if U >= 1 then Exit;
  SpritePoser(B, SpriteEclat, 0, Round((X0 + (X1 - X0) * Courbe(cDouce, U)) * Echelle) - SpriteEclat.L div 2, 0, 255);
end;

// --------------------------------------------------------------------- la minuterie ----
procedure MoteurArreter;
begin
  if Minuterie <> 0 then begin
    KillTimer(0, Minuterie);
    Minuterie := 0;
  end;
end;

procedure Panne(Raison: String);
begin
  if MoteurEnPanne then Exit;
  MoteurEnPanne := True;
  MoteurArreter;
  Log('Habillage : le mouvement s''arrête (' + Raison + ') ; l''installation continue.');
  TransitionsFinir;
  RecapTout;
end;

// ------------------------------------------------ la colonne : les étapes, le décor ----
// L'état d'une étape d'après la progression P : 2 faite (la ligne a rejoint l'étape suivante),
// 1 en cours, 0 à venir.
function EtatEtape(I: Integer; P: Extended): Integer;
begin
  if P >= I + 1 - 0.001 then Result := 2
  else if P >= I - 0.001 then Result := 1
  else Result := 0;
end;

function CouleurEtat(Etat: Integer): Integer;
begin
  if Etat = 2 then Result := CouleurFaite
  else if Etat = 1 then Result := clWhite
  else Result := CouleurAVenir;
end;

// Les marques du rail (à gauche de RailCoupe) : le bout gauche de la pastille de l'étape en cours (elle
// glisse d'une étape à l'autre), les liens (ils se remplissent), les marques (l'arrivée, la pulsation,
// la coche qui se dessine ; pendant la copie, l'anneau qui se remplit). À l'ouverture, tout apparaît
// de haut en bas.
procedure DessinerMarques(Ms: Integer);
var B: TBitmap;
    I, E, X, Y, Y1, Y2, Yf, T, A: Integer;
    Entree: Extended;
begin
  E := Echelle;
  B := Rail.Travail;
  Remplir(B.Canvas, 0, 0, Rail.LT, Rail.HT, CouleurColonne);
  A := Round(255 * Borne((Ms - Rail.Debut) / 400.0));
  SpritePoser(B, SpritePastille, 0, 2 * E, Round((RailPremier + RailPas * RailPastille) * E) - SpritePastille.H div 2, A);
  for I := 0 to NbEtapes - 2 do begin
    Entree := Borne((Ms - Rail.Debut - I * 60) / 260.0);
    Y1 := (RailPremier + RailPas * I + 11) * E;
    Y2 := (RailPremier + RailPas * (I + 1) - 11) * E;
    Yf := Y1 + Round((Y2 - Y1) * Borne(RailP - I));
    Remplir(B.Canvas, (RailMarqueX - 1) * E, Y1, (RailMarqueX + 1) * E, Yf, CouleurEntre(CouleurColonne, CouleurTurquoise, Entree));
    Remplir(B.Canvas, (RailMarqueX - 1) * E, Yf, (RailMarqueX + 1) * E, Y2, CouleurEntre(CouleurColonne, CouleurLienAVenir, Entree));
  end;
  X := RailMarqueX * E - SpriteAvenir.L div 2;
  for I := 0 to NbEtapes - 1 do begin
    Y := (RailPremier + RailPas * I) * E - SpriteAvenir.H div 2;
    T := Ms - RailDepuis[I];
    A := Round(255 * Borne((Ms - Rail.Debut - I * 60) / 260.0));
    case RailEtat[I] of
      2: SpritePoser(B, SpriteCoche, T * SpriteCoche.N div 380, X, Y, A);
      1: begin
           if T < 320 then SpritePoser(B, SpriteArrivee, T * SpriteArrivee.N div 320, X, Y, A)
           else if (I = EtapeCopie) and CopieEnCours then SpritePoser(B, SpriteAnneau, Round(BarreAffiche * (SpriteAnneau.N - 1)), X, Y, A)
           else SpritePoser(B, SpritePulse, ((T - 320) div 50) mod SpritePulse.N, X, Y, A);
         end;
    else
      SpritePoser(B, SpriteAvenir, 0, X, Y, A);
    end;
  end;
  SceneMontrer(Rail);
  Rail.Image.Repaint;
end;

// Les noms des étapes (à droite de RailCoupe) : le reste de la pastille, puis les noms, à la taille
// de l'écran (leur couleur passe d'un état à l'autre, l'étape en cours s'avance un peu). Redessinés
// seulement quand quelque chose y change (voir TicRail).
procedure DessinerNoms(Ms: Integer);
var B: TBitmap;
    C: TCanvas;
    I, E, T, Dx, Couleur, H, A: Integer;
    F: Extended;
    Graisse: String;
begin
  E := Echelle;
  B := RailNoms.Travail;
  Remplir(B.Canvas, 0, 0, RailNoms.LT, RailNoms.HT, CouleurColonne);
  A := Round(255 * Borne((Ms - Rail.Debut) / 400.0));
  SpritePoser(B, SpritePastille, 0, (2 - RailCoupe) * E, Round((RailPremier + RailPas * RailPastille) * E) - SpritePastille.H div 2, A);
  SceneMontrer(RailNoms);
  C := RailNoms.Image.Bitmap.Canvas;
  for I := 0 to NbEtapes - 1 do begin
    T := Ms - RailDepuis[I];
    F := Courbe(cSortie, T / 300.0);
    Couleur := CouleurEntre(RailCouleur[I], CouleurEtat(RailEtat[I]), F);
    Dx := 0;
    if RailEtat[I] = 1 then begin
      Graisse := 'SemiBold';
      Dx := Round(ScaleX(4) * F);
    end else begin
      Graisse := '';
      if RailAvant[I] = 1 then Dx := Round(ScaleX(4) * (1 - F));
    end;
    Ecrire(C.Font, Graisse, 11, Couleur);
    H := C.TextHeight('Ég');
    C.Brush.Style := bsClear;
    C.TextOut(ScaleX(RailTexteX - RailCoupe) + Dx, ScaleY(RailPremier + RailPas * I) - H div 2, Morceau(EtapesNoms, I));
  end;
  RailNoms.Image.Repaint;
  RailNomsVu := Ms;
end;

// La progression rejoint sa cible : d'une page à l'autre, en douceur (RailDuree) ; pendant la
// copie, elle suit la barre (vite, puis doucement). La pastille, elle, va d'étape en étape. Une
// étape qui change d'état part de là. Les marques se redessinent pendant un mouvement, sinon au
// rythme de la pulsation (20 images/s) ; les noms, seulement quand la pastille bouge, qu'une étape
// change d'état (300 ms), et à l'ouverture.
procedure TicRail(Ms: Integer);
var I, Etat, Dt: Integer;
    Ecart, F, Pastille: Extended;
    Bouge, Noms: Boolean;
begin
  if Rail.Image = nil then Exit;
  if Rail.Debut < 0 then begin
    Rail.Debut := Ms;
    RailHorloge := Ms;
    for I := 0 to NbEtapes - 1 do begin
      RailEtat[I] := EtatEtape(I, RailP);
      RailAvant[I] := 0;
      RailCouleur[I] := CouleurColonne;
      RailDepuis[I] := Ms + I * 60;
    end;
  end;
  Dt := Mini(100, Maxi(0, Ms - RailHorloge));
  RailHorloge := Ms;
  Bouge := False;
  Pastille := RailPastille;
  if RailDuree > 0 then begin
    if RailDepart < 0 then RailDepart := Ms;
    F := (Ms - RailDepart) * 1.0 / RailDuree;
    if F >= 1 then begin
      RailP := RailCible;
      RailPastille := RailCible;
      RailDuree := 0;
    end else begin
      RailP := RailP0 + (RailCible - RailP0) * Courbe(cDouce, F);
      RailPastille := RailPastille0 + (RailCible - RailPastille0) * Courbe(cDouce, F);
    end;
    Bouge := True;
  end else begin
    Ecart := RailCible - RailP;
    if Ecart <> 0 then begin
      if (Ecart < 0.003) and (Ecart > -0.003) then RailP := RailCible
      else RailP := RailP + Ecart * Dt / (Dt + 85.0);
      Bouge := True;
    end;
    // pendant la copie, la pastille reste sur son étape
    Ecart := Trunc(RailCible + 0.001) - RailPastille;
    if Ecart <> 0 then begin
      if (Ecart < 0.003) and (Ecart > -0.003) then RailPastille := Trunc(RailCible + 0.001)
      else RailPastille := RailPastille + Ecart * Dt / (Dt + 70.0);
      Bouge := True;
    end;
  end;
  for I := 0 to NbEtapes - 1 do begin
    Etat := EtatEtape(I, RailP);
    if Etat <> RailEtat[I] then begin
      RailCouleur[I] := CouleurEtat(RailEtat[I]);
      RailAvant[I] := RailEtat[I];
      RailEtat[I] := Etat;
      RailDepuis[I] := Ms;
      RailTemps := Ms;
    end;
  end;
  Noms := (Pastille <> RailPastille) or (RailNomsVu < RailTemps + 320) or (Ms - Rail.Debut < NbEtapes * 60 + 700);
  if Noms then DessinerNoms(Ms);
  if Noms or Bouge or (Ms div 50 <> RailVu) then begin
    RailVu := Ms div 50;
    DessinerMarques(Ms);
  end;
end;

// Sans le mouvement (une panne) : le rail à son état, sans animation, une fois.
procedure RailFige;
var I: Integer;
begin
  if (Rail.Image = nil) or (NbEtapes = 0) then Exit;
  try
    RailP := RailCible;
    RailPastille := Trunc(RailCible + 0.001);
    RailDuree := 0;
    Rail.Debut := -100000;
    for I := 0 to NbEtapes - 1 do begin
      RailEtat[I] := EtatEtape(I, RailP);
      RailAvant[I] := RailEtat[I];
      RailDepuis[I] := -100000;
      RailCouleur[I] := CouleurEtat(RailEtat[I]);
    end;
    DessinerMarques(0);
    DessinerNoms(0);
  except
    Log('Habillage, colonne : ' + GetExceptionMessage);
  end;
end;

// Les pistes du décor (habillage.ini : 'x,y x,y...|...', unités 1x de l'image de 150 de haut,
// dont on voit le bas) : leurs points, à la suite.
procedure LirePistes;
var S, P, Q: String;
    I, J, N, M: Integer;
begin
  S := '{#DecorPistes}';
  N := NombreDe(S, '|');
  SetArrayLength(PistePremier, N);
  SetArrayLength(PisteNb, N);
  M := 0;
  for I := 0 to N - 1 do begin
    P := Trim(MorceauDe(S, I, '|'));
    PistePremier[I] := M;
    PisteNb[I] := NombreDe(P, ' ');
    SetArrayLength(PisteX, M + PisteNb[I]);
    SetArrayLength(PisteY, M + PisteNb[I]);
    for J := 0 to PisteNb[I] - 1 do begin
      Q := MorceauDe(P, J, ' ');
      PisteX[M + J] := Nombre(Q, 0);
      PisteY[M + J] := Nombre(Q, 1) - {#DecorHaut};
    end;
    M := M + PisteNb[I];
  end;
  SetArrayLength(ImpPiste, 3);
  SetArrayLength(ImpDebut, 3);
  SetArrayLength(ImpDuree, 3);
  SetArrayLength(ImpSens, 3);
  for I := 0 to 2 do ImpPiste[I] := -1;
end;

function SegmentLongueur(K: Integer): Extended;
begin
  Result := Sqrt((PisteX[K + 1] - PisteX[K]) * (PisteX[K + 1] - PisteX[K]) + (PisteY[K + 1] - PisteY[K]) * (PisteY[K + 1] - PisteY[K]));
end;

function PisteLongueur(I: Integer): Extended;
var J: Integer;
begin
  Result := 0;
  for J := 0 to PisteNb[I] - 2 do Result := Result + SegmentLongueur(PistePremier[I] + J);
end;

// Le point à la distance D (unités 1x) le long de la piste I.
procedure PistePoint(I: Integer; D: Extended; var X, Y: Extended);
var J, K: Integer;
    L: Extended;
begin
  K := PistePremier[I];
  X := PisteX[K];
  Y := PisteY[K];
  for J := 0 to PisteNb[I] - 2 do begin
    K := PistePremier[I] + J;
    L := SegmentLongueur(K);
    if (D <= L) and (L > 0) then begin
      X := PisteX[K] + (PisteX[K + 1] - PisteX[K]) * D / L;
      Y := PisteY[K] + (PisteY[K + 1] - PisteY[K]) * D / L;
      Exit;
    end;
    D := D - L;
    X := PisteX[K + 1];
    Y := PisteY[K + 1];
  end;
end;

// Le décor : des impulsions parcourent les pistes du circuit (30 images/s).
procedure TicDecor(Ms: Integer);
var B: TBitmap;
    K, E, T, L: Integer;
    D, X, Y, U, A, Long: Extended;
begin
  if (Decor.Image = nil) or (GetArrayLength(PisteNb) = 0) then Exit;
  if Ms - Decor.Dernier < 33 then Exit;
  Decor.Dernier := Ms;
  if Ms >= ImpProchaine then
    for K := 0 to 2 do
      if ImpPiste[K] < 0 then begin
        ImpPiste[K] := Random(GetArrayLength(PisteNb));
        ImpDebut[K] := Ms;
        ImpDuree[K] := Maxi(400, Round(PisteLongueur(ImpPiste[K]) * 1000 / 60));
        if Random(2) = 0 then ImpSens[K] := 1 else ImpSens[K] := -1;
        ImpProchaine := Ms + 700 + Random(1300);
        Break;
      end;
  E := Echelle;
  B := Decor.Travail;
  BitBlt(B.Canvas.Handle, 0, 0, Decor.LT, Decor.HT, FondDecor.Canvas.Handle, 0, 0, SRCCOPY);
  for K := 0 to 2 do
    if ImpPiste[K] >= 0 then begin
      T := Ms - ImpDebut[K];
      if T >= ImpDuree[K] then ImpPiste[K] := -1
      else begin
        U := T * 1.0 / ImpDuree[K];
        Long := PisteLongueur(ImpPiste[K]);
        if ImpSens[K] > 0 then D := U * Long else D := (1 - U) * Long;
        PistePoint(ImpPiste[K], D, X, Y);
        // elle naît et s'éteint doucement, et s'efface vers le haut, comme le décor
        A := Borne(U / 0.1) * Borne((1 - U) / 0.1) * Borne((Y - 4) / 36.0);
        L := 18 * E;
        SpritePoserTaille(B, SpriteLueurT, 0, Round(X * E) - L div 2, Round(Y * E) - L div 2, L, L, Round(240 * A));
        // une traîne, plus petite
        PistePoint(ImpPiste[K], D - 7 * ImpSens[K], X, Y);
        L := 11 * E;
        SpritePoserTaille(B, SpriteLueurT, 0, Round(X * E) - L div 2, Round(Y * E) - L div 2, L, L, Round(110 * A));
      end;
    end;
  SceneMontrer(Decor);
  Decor.Image.Repaint;
end;

// La colonne : l'assistant s'élargit d'autant, ses pages et ses boutons glissent à droite.
// 1.12.29 : la taille de l'assistant - FenetreL x FenetreH (912 x 580) à 96 ppp : avec sa police
// (Tahoma 8), ScaleX et ScaleY y sont des pixels ; à 125, 150, 200 %, elle grandit d'autant. Ramenée
// à la zone de travail de l'écran (sans la barre des tâches) s'il est plus petit : l'écran a le
// dernier mot (même sous la taille de la 1.12.28, 750 x 504 : la mise en page se resserre). Les pages
// et les boutons d'Inno Setup suivent (leurs ancrages) ; le reste se pose ensuite, à cette taille.
procedure Dimensionner;
var Zone: TRect;
    L, H, BordL, BordH, ZoneL, ZoneH: Integer;
begin
  L := ScaleX(FenetreL);
  H := ScaleY(FenetreH);
  BordL := WizardForm.Width - WizardForm.ClientWidth;
  BordH := WizardForm.Height - WizardForm.ClientHeight;
  ZoneL := 0;
  ZoneH := 0;
  if SystemParametersInfo(SPI_GETWORKAREA, 0, Zone, 0) then begin
    ZoneL := Zone.Right - Zone.Left;
    ZoneH := Zone.Bottom - Zone.Top;
    if (ZoneL > 0) and (L > ZoneL - BordL - ScaleX(8)) then L := Maxi(ZoneL - BordL - ScaleX(8), 640);
    if (ZoneH > 0) and (H > ZoneH - BordH - ScaleY(8)) then H := Maxi(ZoneH - BordH - ScaleY(8), 400);
  end;
  WizardForm.ClientWidth := L;
  WizardForm.ClientHeight := H;
  Dimensionne := True;
  Log(Format('Habillage : fenêtre %d x %d ; écran (zone de travail) %d x %d, bords %d x %d ; ScaleX(100) = %d, ScaleY(100) = %d', [WizardForm.ClientWidth,
             WizardForm.ClientHeight, ZoneL, ZoneH, BordL, BordH, ScaleX(100), ScaleY(100)]));
end;

// 1.12.29 : la fenêtre se dessine d'un bloc (WS_EX_COMPOSITED : Windows peint la fenêtre et tout ce
// qu'elle contient dans un tampon, puis l'affiche) - un texte posé sur une image ne s'efface plus
// avant de se redessiner. De nouveau si la fenêtre a été recréée (son handle change) ; jamais avec
// /COMPOSITION=non.
procedure Composer;
var H: HWND;
begin
  if (ColonneEtapes = nil) or SansComposition then Exit;
  try
    H := WizardForm.Handle;
    if H = FenetreComposee then Exit;
    SetWindowLong(H, GWL_EXSTYLE, GetWindowLong(H, GWL_EXSTYLE) or WS_EX_COMPOSITED);
    FenetreComposee := H;
    Log('Habillage : la fenêtre se dessine d''un bloc (WS_EX_COMPOSITED).');
  except
    Log('Habillage, WS_EX_COMPOSITED : ' + GetExceptionMessage);
  end;
end;

// La colonne, à gauche de l'assistant (à sa taille : voir Dimensionner) : les pages et les boutons
// glissent à droite.
procedure CreerColonne;
var L, I, X, Y: Integer;
    Pied: TLabel;
begin
  LargeurColonne := ScaleX(ColonneL);
  L := WizardForm.ClientWidth - LargeurColonne;
  WizardForm.OuterNotebook.Left := LargeurColonne;
  WizardForm.OuterNotebook.Width := L;
  WizardForm.Bevel.Left := LargeurColonne;
  WizardForm.Bevel.Width := L;
  WizardForm.BeveledLabel.Left := WizardForm.BeveledLabel.Left + LargeurColonne;

  ColonneEtapes := NouveauPanneau(WizardForm, 0, 0, LargeurColonne, WizardForm.ClientHeight, CouleurColonne);
  ColonneEtapes.Anchors := [akLeft, akTop, akBottom];
  // 1.12.28 : le décor (le bas de la colonne) est une scène : des impulsions parcourent ses pistes
  FondDecor := FondCharger('decor');
  ScenePreparer(Decor, ColonneEtapes, 0, ColonneEtapes.Height - ScaleY(DecorH), LargeurColonne, ScaleY(DecorH),
                FondDecor.Width, FondDecor.Height);
  Decor.Image.Anchors := [akLeft, akBottom];
  BitBlt(Decor.Travail.Canvas.Handle, 0, 0, Decor.LT, Decor.HT, FondDecor.Canvas.Handle, 0, 0, SRCCOPY);
  SceneMontrer(Decor);
  SpriteCharger(SpriteLueurT, 'lueur-turquoise');
  LirePistes;
  X := ScaleX(24);
  NouvelleImage(ColonneEtapes, 'logo', X, ScaleY(26), ScaleX(88), ScaleY(40));
  NouveauTexte(ColonneEtapes, '{#Nom}', X, ScaleY(78), LargeurColonne - X - ScaleX(8), ScaleY(30), 'SemiBold', 15, clWhite);
  NouveauTexte(ColonneEtapes, 'Version {#Version}', X, ScaleY(108), LargeurColonne - X - ScaleX(8), ScaleY(18), '', 9, CouleurAVenir);

  // les étapes : deux images, les marques et les noms (le rail), redessinées par la minuterie
  NbEtapes := NombreDeMorceaux(EtapesNoms);
  EtapeCopie := EtapeDePage(wpInstalling);
  SetArrayLength(RailEtat, NbEtapes);
  SetArrayLength(RailAvant, NbEtapes);
  SetArrayLength(RailDepuis, NbEtapes);
  SetArrayLength(RailCouleur, NbEtapes);
  for I := 0 to NbEtapes - 1 do begin
    RailEtat[I] := 0;
    RailAvant[I] := 0;
    RailDepuis[I] := 0;
    RailCouleur[I] := CouleurColonne;
  end;
  SpriteCharger(SpriteAvenir, 'rail-avenir');
  SpriteCharger(SpriteArrivee, 'rail-arrivee');
  SpriteCharger(SpritePulse, 'rail-pulse');
  SpriteCharger(SpriteCoche, 'rail-coche');
  SpriteCharger(SpriteAnneau, 'rail-anneau');
  SpriteCharger(SpritePastille, 'rail-pastille');
  ScenePreparer(Rail, ColonneEtapes, ScaleX(RailX), ScaleY(RailY), ScaleX(RailX + RailCoupe) - ScaleX(RailX), ScaleY(RailH),
                RailCoupe * Echelle, RailH * Echelle);
  Remplir(Rail.Image.Bitmap.Canvas, 0, 0, Rail.Image.Bitmap.Width, Rail.Image.Bitmap.Height, CouleurColonne);
  ScenePreparer(RailNoms, ColonneEtapes, ScaleX(RailX + RailCoupe), ScaleY(RailY), ScaleX(RailX + RailL) - ScaleX(RailX + RailCoupe),
                ScaleY(RailH), (RailL - RailCoupe) * Echelle, RailH * Echelle);
  Remplir(RailNoms.Image.Bitmap.Canvas, 0, 0, RailNoms.Image.Bitmap.Width, RailNoms.Image.Bitmap.Height, CouleurColonne);
  RailP := 0;
  RailCible := 0;
  RailPastille := 0;
  // 1.12.29 : la note, AU-DESSUS du décor (les impulsions ne passent plus sous elle) ; elle se retire
  // si la fenêtre est trop basse pour elle (l'écran est petit)
  Y := ColonneEtapes.Height - ScaleY(DecorH) - ScaleY(40);
  Pied := NouveauTexte(ColonneEtapes, 'Tout est inclus : rien d''autre à installer, pas besoin d''Internet.', X, Y,
                       LargeurColonne - X - ScaleX(14), ScaleY(36), '', 8, CouleurAVenir);
  Pied.Anchors := [akLeft, akBottom];
  Pied.Visible := Y >= ScaleY(RailY + RailH + 4);
end;

// L'étape en cours : la progression y va en douceur (plus longtemps si elle saute des étapes).
procedure MajEtapes(Courante: Integer);
begin
  if ColonneEtapes = nil then Exit;
  if Courante <> RailCible then begin
    RailP0 := RailP;
    RailPastille0 := RailPastille;
    RailCible := Courante;
    RailDepart := -1;
    RailDuree := Mini(820, 320 + Round(140 * Abs(Courante - RailP)));
  end;
  if MoteurEnPanne then RailFige;
  if EnTeteIcone <> nil then EnTeteIcone.Bitmap.LoadFromFile(ImageUi('icone-' + Morceau(EtapesIcones, Courante)));
end;

// L'en-tête des pages : le titre, la description, l'icône de l'étape, un trait en dégradé (1.12.28 :
// dans un cadre dont la largeur s'anime - le trait se dessine à chaque page).
procedure HabillerEnTete;
var T: Integer;
begin
  WizardForm.MainPanel.ParentBackground := False;
  WizardForm.MainPanel.Color := clWhite;
  // plus haut de 14 points : la description tient sur deux lignes ; les pages descendent d'autant
  WizardForm.MainPanel.Height := WizardForm.MainPanel.Height + ScaleY(14);
  WizardForm.InnerNotebook.Top := WizardForm.InnerNotebook.Top + ScaleY(14);
  WizardForm.InnerNotebook.Height := WizardForm.InnerNotebook.Height - ScaleY(14);
  Ecrire(WizardForm.PageNameLabel.Font, 'SemiBold', 13, CouleurTitre);
  WizardForm.PageNameLabel.Height := ScaleY(24);
  Ecrire(WizardForm.PageDescriptionLabel.Font, '', 10, CouleurTexte);
  WizardForm.PageDescriptionLabel.Top := WizardForm.PageNameLabel.Top + ScaleY(24);
  WizardForm.PageDescriptionLabel.Height := WizardForm.MainPanel.Height - WizardForm.PageDescriptionLabel.Top - ScaleY(2);
  WizardForm.WizardSmallBitmapImage.Visible := False;
  T := ScaleY(44);
  EnTeteIcone := NouvelleImage(WizardForm.MainPanel, '', WizardForm.MainPanel.Width - T - ScaleX(18),
                               (WizardForm.MainPanel.Height - T) div 2, T, T);
  EnTeteIcone.Anchors := [akTop, akRight];
  WizardForm.Bevel1.Visible := False;
  EnTeteCadre := NouveauPanneau(WizardForm.InnerPage, 0, WizardForm.MainPanel.Top + WizardForm.MainPanel.Height,
                                WizardForm.InnerPage.Width, ScaleY(3), clWhite);
  EnTeteTrait := NouvelleImage(EnTeteCadre, 'degrade', 0, 0, WizardForm.InnerPage.Width, ScaleY(3));
  // 1.12.28 : les positions de repos de ce qui glisse à chaque page
  NotebookX := WizardForm.InnerNotebook.Left;
  NotebookY := WizardForm.InnerNotebook.Top;
  TitreX := WizardForm.PageNameLabel.Left;
  TitreY := WizardForm.PageNameLabel.Top;
  DescX := WizardForm.PageDescriptionLabel.Left;
  DescY := WizardForm.PageDescriptionLabel.Top;
  IconeX := EnTeteIcone.Left;
  IconeY := EnTeteIcone.Top;
  IconeT := T;
end;

// ------------------------------------------------------------------------ l'accueil ----
function NouvelleCarte(Parent: TWinControl; K, X, Y, L, H: Integer; Logo, Titre, Texte: String): TPanel;
begin
  Result := NouveauPanneau(Parent, X, Y, L, H, CouleurCarte);
  CartePanneau[K] := Result;
  CarteX[K] := X;
  CarteY[K] := Y;
  CarteLogoX[K] := ScaleX(16);
  CarteLogoY[K] := (H - ScaleY(48)) div 2;
  CarteLogoL[K] := ScaleX(48);
  CarteLogoH[K] := ScaleY(48);
  CarteLogo[K] := NouvelleImage(Result, Logo, CarteLogoX[K], CarteLogoY[K], CarteLogoL[K], CarteLogoH[K]);
  CarteTitre[K] := NouveauTexte(Result, Titre, ScaleX(78), ScaleY(15), L - ScaleX(88), ScaleY(24), 'SemiBold', 12, CouleurTitre);
  CarteTexte[K] := NouveauTexte(Result, Texte, ScaleX(78), ScaleY(41), L - ScaleX(88), H - ScaleY(45), '', 10, CouleurTexte);
  Arrondir(Result, ScaleX(14));
end;

// L'accueil : le bandeau (une scène : le réseau s'allume, les deux logos arrivent, le lien se
// fait, l'éclat passe), le titre, ce que fait l'assistant, les deux applications.
procedure HabillerAccueil;
var M, L, Y, H, LC, LB, Bloc, PH, CarteH: Integer;
begin
  if Heros.Image <> nil then Exit;
  M := ScaleX(30);
  L := WizardForm.WelcomePage.Width - 2 * M;
  PH := WizardForm.WelcomePage.Height;
  WizardForm.WizardBitmapImage.Visible := False;
  // 1.12.29 : le bandeau garde ses proportions (images\ui\habillage.ini : 620 x 196) ; sous lui, le
  // titre, le texte et les cartes (Bloc ; une carte étroite - un écran petit - a la place d'une ligne
  // de plus). Sur un écran bas, le bandeau rétrécit (centré). Le tout, centré en hauteur.
  LC := (L - ScaleX(14)) div 2;
  if LC < ScaleX(292) then CarteH := ScaleY(110) else CarteH := ScaleY(92);
  Bloc := ScaleY(18) + ScaleY(40) + ScaleY(58) + CarteH;
  H := L * Nombre('{#AccueilTaille}', 1) div Nombre('{#AccueilTaille}', 0);
  LB := L;
  if H + Bloc > PH - ScaleY(24) then begin
    H := Maxi(ScaleY(80), PH - ScaleY(24) - Bloc);
    LB := H * Nombre('{#AccueilTaille}', 0) div Nombre('{#AccueilTaille}', 1);
  end;
  Y := (PH - (H + Bloc)) div 2;
  if Y < ScaleY(12) then Y := ScaleY(12);
  FondAccueil := FondCharger('accueil-fond');
  AccEclat0 := Nombre('{#AccueilEclat}', 0);
  AccEclat1 := Nombre('{#AccueilEclat}', 1);
  ScenePreparer(Heros, WizardForm.WelcomePage, M + (L - LB) div 2, Y, LB, H, FondAccueil.Width, FondAccueil.Height);
  BitBlt(Heros.Travail.Canvas.Handle, 0, 0, Heros.LT, Heros.HT, FondAccueil.Canvas.Handle, 0, 0, SRCCOPY);
  SceneMontrer(Heros);
  SpriteCharger(SpriteApi, 'heros-api');
  SpriteCharger(SpriteIhm, 'heros-ihm');
  SpriteCharger(SpritePoints, 'heros-points');
  SpriteCharger(SpriteEclat, 'eclat');
  SpriteCharger(SpriteLueur, 'lueur');
  LireNoeuds('{#AccueilNoeuds}', HerosNX, HerosNY, HerosNR);
  LireCouples('{#AccueilLiens}', HerosLA, HerosLB);
  LireCouples('{#AccueilPoints}', HerosPX, HerosPY);
  HerosApiX := Nombre(MorceauDe('{#AccueilLogos}', 0, ';'), 0);
  HerosApiY := Nombre(MorceauDe('{#AccueilLogos}', 0, ';'), 1);
  HerosIhmX := Nombre(MorceauDe('{#AccueilLogos}', 1, ';'), 0);
  HerosIhmY := Nombre(MorceauDe('{#AccueilLogos}', 1, ';'), 1);
  SetArrayLength(HerosImpLien, 3);
  SetArrayLength(HerosImpDebut, 3);
  HerosImpLien[0] := -1;
  HerosImpLien[1] := -1;
  HerosImpLien[2] := -1;
  Y := Y + H + ScaleY(18);
  WizardForm.WelcomeLabel1.SetBounds(M, Y, L, ScaleY(34));
  Ecrire(WizardForm.WelcomeLabel1.Font, 'SemiBold', 18, CouleurTitre);
  WizardForm.WelcomeLabel1.Caption := 'Bienvenue dans {#Nom} {#Version}';
  AccTitreX := M;
  AccTitreY := Y;
  Y := Y + ScaleY(40);
  WizardForm.WelcomeLabel2.SetBounds(M, Y, L, ScaleY(50));
  Ecrire(WizardForm.WelcomeLabel2.Font, '', 11, CouleurTexte);
  WizardForm.WelcomeLabel2.Caption := 'L''assistant installe les deux applications et tout ce dont elles ont besoin, étape par étape. '
                                    + 'Tes données et tes réglages sont gardés.';
  AccTexteX := M;
  AccTexteY := Y;
  Y := Y + ScaleY(58);
  SetArrayLength(CartePanneau, 2);
  SetArrayLength(CarteLogo, 2);
  SetArrayLength(CarteTitre, 2);
  SetArrayLength(CarteTexte, 2);
  SetArrayLength(CarteX, 2);
  SetArrayLength(CarteY, 2);
  SetArrayLength(CarteLogoX, 2);
  SetArrayLength(CarteLogoY, 2);
  SetArrayLength(CarteLogoL, 2);
  SetArrayLength(CarteLogoH, 2);
  NouvelleCarte(WizardForm.WelcomePage, 0, M, Y, LC, CarteH, 'logo-api', '{#NomApi}', 'Analyse et simulation du programme de l''automate');
  NouvelleCarte(WizardForm.WelcomePage, 1, M + LC + ScaleX(14), Y, LC, CarteH, 'logo-ihm', '{#NomIhm}', 'Vues, scripts et équipements de l''IHM, et leur simulation');
end;

// Des impulsions parcourent les liens du réseau de l'accueil (des données qui circulent).
procedure DessinerImpulsions(B: TBitmap; T: Integer);
var K, I, J, L, A: Integer;
    U, X, Y: Extended;
begin
  for K := 0 to 2 do begin
    if (HerosImpLien[K] < 0) and (T > 1400) and (T >= HerosImpProchaine) and (GetArrayLength(HerosLA) > 0) then begin
      HerosImpLien[K] := Random(GetArrayLength(HerosLA));
      if Random(2) = 1 then HerosImpLien[K] := HerosImpLien[K] + 1000;   // dans l'autre sens
      HerosImpDebut[K] := T;
      HerosImpProchaine := T + 300 + Random(500);
    end;
    if HerosImpLien[K] >= 0 then begin
      U := (T - HerosImpDebut[K]) / 950.0;
      if U >= 1 then HerosImpLien[K] := -1
      else begin
        I := HerosLA[HerosImpLien[K] mod 1000];
        J := HerosLB[HerosImpLien[K] mod 1000];
        if HerosImpLien[K] >= 1000 then begin
          L := I;
          I := J;
          J := L;
        end;
        A := Round(230 * Sin(Pi * U));
        X := HerosNX[I] + (HerosNX[J] - HerosNX[I]) * U;
        Y := HerosNY[I] + (HerosNY[J] - HerosNY[I]) * U;
        L := 15 * Echelle;
        SpritePoserTaille(B, SpriteLueur, 0, Round(X * Echelle) - L div 2, Round(Y * Echelle) - L div 2, L, L, A);
      end;
    end;
  end;
end;

// Le bandeau de l'accueil à l'instant T (ms depuis son entrée).
procedure DessinerHeros(T: Integer);
var B: TBitmap;
    K, E, A, L: Integer;
    F, Vague: Extended;
begin
  E := Echelle;
  B := Heros.Travail;
  BitBlt(B.Canvas.Handle, 0, 0, Heros.LT, Heros.HT, FondAccueil.Canvas.Handle, 0, 0, SRCCOPY);
  DessinerNoeuds(B, HerosNX, HerosNY, HerosNR, T);
  DessinerImpulsions(B, T);
  // le lien : une vague part de l'automate toutes les 1,6 s et arrive à l'écran, qui s'éclaire
  if T > 1300 then begin
    F := ((T - 1300) mod 1600) / 1600.0;
    A := Round(170 * Borne(1 - Abs(F - 0.40) / 0.22));
    L := {#AccueilLueur} * E;
    SpritePoserTaille(B, SpriteLueurT, 0, HerosIhmX * E + SpriteIhm.L div 2 - L div 2, HerosIhmY * E + SpriteIhm.H div 2 - L div 2, L, L, A);
  end;
  // les deux logos arrivent, de chaque côté, en fondu
  F := Courbe(cSortie, (T - 100) / 650.0);
  if F > 0 then SpritePoser(B, SpriteApi, 0, Round((HerosApiX - 40 * (1 - F)) * E), HerosApiY * E, Round(255 * Borne(F * 1.4)));
  F := Courbe(cSortie, (T - 260) / 650.0);
  if F > 0 then SpritePoser(B, SpriteIhm, 0, Round((HerosIhmX + 40 * (1 - F)) * E), HerosIhmY * E, Round(255 * Borne(F * 1.4)));
  // les trois points, l'un après l'autre ; ensuite, la vague les fait briller
  for K := 0 to GetArrayLength(HerosPX) - 1 do begin
    A := Round(255 * Borne((T - 820 - K * 120) / 220.0));
    SpritePoser(B, SpritePoints, K, (HerosPX[K] - 5) * E, (HerosPY[K] - 5) * E, A);
    if T > 1300 then begin
      F := ((T - 1300) mod 1600) / 1600.0;
      Vague := Borne(1 - Abs(F - 0.08 - K * 0.09) / 0.07);
      if Vague > 0 then begin
        L := 18 * E;
        SpritePoserTaille(B, SpriteLueur, 0, HerosPX[K] * E - L div 2, HerosPY[K] * E - L div 2, L, L, Round(255 * Vague));
      end;
    end;
  end;
  DessinerEclat(B, T, 1150, 5200, AccEclat0, AccEclat1);
  SceneMontrer(Heros);
  Heros.Image.Repaint;
end;

procedure TicHeros(Ms: Integer);
var T: Integer;
begin
  if Heros.Debut < 0 then Heros.Debut := Ms;
  T := Ms - Heros.Debut;
  // l'entrée à 60 images par seconde, ensuite 30
  if (T > 2400) and (Ms - Heros.Dernier < 30) then Exit;
  Heros.Dernier := Ms;
  DessinerHeros(T);
end;

// ---------------------------------------------------------------------------- la fin ----
// La fin : l'assistant y place ses textes juste avant de l'afficher ; on les range ensuite.
procedure HabillerFin;
var M, L, Y, H, I, LB, Bloc, PH: Integer;
    S: String;
begin
  M := ScaleX(30);
  L := WizardForm.FinishedPage.Width - 2 * M;
  PH := WizardForm.FinishedPage.Height;
  WizardForm.WizardBitmapImage2.Visible := False;
  // 1.12.29 : comme l'accueil - le bandeau garde ses proportions (620 x 196), rétrécit sur un écran
  // bas ; sous lui, le titre, le texte et les cases (Bloc) ; le tout centré en hauteur
  Bloc := ScaleY(18) + ScaleY(40) + ScaleY(72) + ScaleY(54);
  H := L * Nombre('{#FinTaille}', 1) div Nombre('{#FinTaille}', 0);
  LB := L;
  if H + Bloc > PH - ScaleY(24) then begin
    H := Maxi(ScaleY(80), PH - ScaleY(24) - Bloc);
    LB := H * Nombre('{#FinTaille}', 0) div Nombre('{#FinTaille}', 1);
  end;
  Y := (PH - (H + Bloc)) div 2;
  if Y < ScaleY(12) then Y := ScaleY(12);
  if Succes.Image = nil then begin
    // 1.12.28 : le bandeau est une scène (le cercle surgit, la coche se dessine, les confettis)
    FondFin := FondCharger('fin-fond');
    FinH1 := Nombre('{#FinTaille}', 1);
    FinEclat0 := Nombre('{#FinEclat}', 0);
    FinEclat1 := Nombre('{#FinEclat}', 1);
    ScenePreparer(Succes, WizardForm.FinishedPage, M + (L - LB) div 2, Y, LB, H, FondFin.Width, FondFin.Height);
    BitBlt(Succes.Travail.Canvas.Handle, 0, 0, Succes.LT, Succes.HT, FondFin.Canvas.Handle, 0, 0, SRCCOPY);
    SceneMontrer(Succes);
    SpriteCharger(SpriteDisque, 'succes-disque');
    SpriteCharger(SpriteCocheFin, 'succes-coche');
    SpriteCharger(SpriteEtincA, 'etincelle-ambre');
    SpriteCharger(SpriteEtincB, 'etincelle-blanche');
    if SpriteLueur.Bmp = nil then SpriteCharger(SpriteLueur, 'lueur');
    if SpriteEclat.Bmp = nil then SpriteCharger(SpriteEclat, 'eclat');
    LireNoeuds('{#FinNoeuds}', FinNX, FinNY, FinNR);
    S := '{#FinEtincelles}';
    SetArrayLength(FinEX, NombreDe(S, ';'));
    SetArrayLength(FinEY, NombreDe(S, ';'));
    SetArrayLength(FinES, NombreDe(S, ';'));
    SetArrayLength(FinEC, NombreDe(S, ';'));
    for I := 0 to NombreDe(S, ';') - 1 do begin
      FinEX[I] := Nombre(MorceauDe(S, I, ';'), 0);
      FinEY[I] := Nombre(MorceauDe(S, I, ';'), 1);
      FinES[I] := Nombre(MorceauDe(S, I, ';'), 2);
      FinEC[I] := Nombre(MorceauDe(S, I, ';'), 3);
    end;
    FinDisqueX := Nombre('{#FinDisque}', 0);
    FinDisqueY := Nombre('{#FinDisque}', 1);
    FinCocheX := Nombre('{#FinCoche}', 0);
    FinCocheY := Nombre('{#FinCoche}', 1);
    FinCX := Nombre('{#FinCentre}', 0);
    FinCY := Nombre('{#FinCentre}', 1);
  end else Y := Succes.Image.Top;
  Y := Y + H + ScaleY(18);
  WizardForm.FinishedHeadingLabel.SetBounds(M, Y, L, ScaleY(34));
  Ecrire(WizardForm.FinishedHeadingLabel.Font, 'SemiBold', 18, CouleurTitre);
  Y := Y + ScaleY(40);
  WizardForm.FinishedLabel.SetBounds(M, Y, L, ScaleY(66));
  Ecrire(WizardForm.FinishedLabel.Font, '', 10, CouleurTexte);
  Y := Y + ScaleY(72);
  WizardForm.RunList.SetBounds(M, Y, L, PH - Y - ScaleY(6));
  Ecrire(WizardForm.RunList.Font, '', 10, CouleurTitre);
end;

// Les confettis partent du cercle, en éventail vers le haut, puis retombent hors du bandeau.
procedure LancerConfettis;
var I: Integer;
    Angle, Vitesse: Extended;
begin
  SetArrayLength(Confettis, 96);
  for I := 0 to GetArrayLength(Confettis) - 1 do begin
    Angle := Pi * (1.04 + 0.92 * Random(1000) / 1000.0);
    Vitesse := 0.14 + 0.26 * Random(1000) / 1000.0;
    Confettis[I].X := FinCX - 12 + Random(25);
    Confettis[I].Y := FinCY - 10 + Random(21);
    Confettis[I].VX := Cos(Angle) * Vitesse * 1.6;
    Confettis[I].VY := Sin(Angle) * Vitesse * 1.25;
    Confettis[I].Tour := 0.006 + 0.016 * Random(1000) / 1000.0;
    Confettis[I].Phase := 2 * Pi * Random(1000) / 1000.0;
    Confettis[I].Taille := 4 + Random(5);
    case Random(5) of
      0: Confettis[I].Couleur := CouleurAmbre;
      1: Confettis[I].Couleur := CouleurTurquoise;
      2: Confettis[I].Couleur := CouleurBleu;
      3: Confettis[I].Couleur := CouleurRose;
    else
      Confettis[I].Couleur := clWhite;
    end;
  end;
end;

procedure DessinerConfettis(B: TBitmap; T: Integer);
var I, E, X, Y, L, H: Integer;
    D: Extended;
begin
  if (T < 0) or (T > 4000) then Exit;
  E := Echelle;
  D := T / (1 + T / 430.0);
  for I := 0 to GetArrayLength(Confettis) - 1 do begin
    Y := Round((Confettis[I].Y + Confettis[I].VY * D + 0.5 * 0.00045 * T * T) * E);
    if Y > (FinH1 + 8) * E then Continue;
    X := Round((Confettis[I].X + Confettis[I].VX * D) * E);
    // il tourne sur lui-même : sa largeur bat
    L := Maxi(E, Round(Confettis[I].Taille * E * Abs(Cos(Confettis[I].Phase + Confettis[I].Tour * T))));
    H := Maxi(E, Round(Confettis[I].Taille * 0.6 * E));
    Remplir(B.Canvas, X - L div 2, Y - H div 2, X - L div 2 + L, Y - H div 2 + H, Confettis[I].Couleur);
  end;
end;

// Le bandeau de la fin à l'instant T (ms depuis son entrée).
procedure DessinerSucces(T: Integer);
var B: TBitmap;
    E, K, L, M: Integer;
    F: Extended;
begin
  E := Echelle;
  B := Succes.Travail;
  BitBlt(B.Canvas.Handle, 0, 0, Succes.LT, Succes.HT, FondFin.Canvas.Handle, 0, 0, SRCCOPY);
  DessinerNoeuds(B, FinNX, FinNY, FinNR, T);
  // les confettis (derrière le cercle : ils en jaillissent)
  DessinerConfettis(B, T - 600);
  // le cercle surgit (avec un rebond), puis la coche se dessine
  F := Borne((T - 150) / 480.0);
  if F >= 1 then SpritePoser(B, SpriteDisque, 0, FinDisqueX * E, FinDisqueY * E, 255)
  else if F > 0 then begin
    L := Round(SpriteDisque.L * Courbe(cRebond, F));
    SpritePoserTaille(B, SpriteDisque, 0, FinCX * E - L div 2, FinCY * E - L div 2, L, L, Round(255 * Borne(F * 3)));
  end;
  if T >= 560 then SpritePoser(B, SpriteCocheFin, (T - 560) * SpriteCocheFin.N div 420, FinCocheX * E, FinCocheY * E, 255);
  // les étincelles s'allument l'une après l'autre, puis scintillent
  for K := 0 to GetArrayLength(FinEX) - 1 do begin
    F := Borne((T - 700 - K * 110) / 260.0);
    if F > 0 then begin
      M := Round((FinES[K] / 8.0 - 0.25) / 0.75 * 7);
      if F < 1 then L := Round(M * Courbe(cRebond, F))
      else L := Round(M * (0.62 + 0.38 * (0.5 + 0.5 * Sin(2 * Pi * ((T - 960 - K * 110) / 1500.0 + K * 0.29)))));
      if FinEC[K] = 0 then SpritePoser(B, SpriteEtincA, L, FinEX[K] * E - SpriteEtincA.L div 2, FinEY[K] * E - SpriteEtincA.H div 2, 255)
      else SpritePoser(B, SpriteEtincB, L, FinEX[K] * E - SpriteEtincB.L div 2, FinEY[K] * E - SpriteEtincB.H div 2, 255);
    end;
  end;
  DessinerEclat(B, T, 2600, 6000, FinEclat0, FinEclat1);
  SceneMontrer(Succes);
  Succes.Image.Repaint;
end;

procedure TicSucces(Ms: Integer);
var T: Integer;
begin
  if Succes.Debut < 0 then begin
    Succes.Debut := Ms;
    LancerConfettis;
  end;
  T := Ms - Succes.Debut;
  if (T > 3300) and (Ms - Succes.Dernier < 30) then Exit;
  Succes.Dernier := Ms;
  DessinerSucces(T);
end;

// ------------------------------------------------------------------------ la copie ----
// Une diapositive, dessinée une fois à la taille de l'écran : son image, son titre, son texte.
procedure DiapoRendre(I: Integer);
var B, Img: TBitmap;
    C: TCanvas;
    L, H: Integer;
begin
  L := DiapoImage.Bitmap.Width;
  H := DiapoImage.Bitmap.Height;
  B := TBitmap.Create;
  B.Width := L;
  B.Height := H;
  C := B.Canvas;
  Remplir(C, 0, 0, L, H, CouleurCarte);
  Img := FondCharger(Morceau(DiaposImages, I));
  Reduire(C.Handle, ScaleX(14), (H - ScaleY(52)) div 2, ScaleX(52), ScaleY(52), Img.Canvas.Handle, Img.Width, Img.Height);
  Img.Free;
  Ecrire(C.Font, 'SemiBold', 11, CouleurTitre);
  C.Brush.Style := bsClear;
  C.TextOut(ScaleX(80), ScaleY(12), Morceau(DiaposTitres, I));
  Ecrire(C.Font, '', 10, CouleurTexte);
  TexteCoupe(C, ScaleX(80), ScaleY(35), L - ScaleX(80) - ScaleX(18), Morceau(DiaposTextes, I));
  DiapoCache[I] := B;
end;

// ================================================== 1.12.29 : les détails, à l'écran ====
//  UNE image (une scène : le fond et les marques à l'échelle des images, les textes à la taille de
//  l'écran), redessinée quand une ligne arrive ou change - au plus toutes les 150 ms - et pendant
//  qu'elle défile (une ligne nouvelle pousse les autres vers le haut). La roue, à part (une petite
//  image posée sur la marque de la ligne en cours la plus récente), tourne sans redessiner un texte.
var
  SpriteJEnCours, SpriteJFait, SpriteJAttention, SpriteJErreur, SpriteJInfo: TSprite;
  // la hauteur de la carte à l'écran (1x) et sa cible : après la copie, le diaporama s'en va et les
  // détails prennent sa place (JournalAgrandir)
  FacteurY, JournalHVue, JournalHCible: Extended;

procedure JournalCreer(Parent: TWinControl; X, Y, L, H: Integer);
begin
  JournalL1 := L * 100 div ScaleX(100);
  JournalH1 := H * 100 div ScaleY(100);
  FacteurY := ScaleY(1000) / 1000.0;
  ScenePreparer(Journal, Parent, X, Y, L, H, JournalL1 * Echelle, JournalH1 * Echelle);
  SpriteCharger(SpriteJEnCours, 'journal-encours');
  SpriteCharger(SpriteJFait, 'journal-fait');
  SpriteCharger(SpriteJAttention, 'journal-attention');
  SpriteCharger(SpriteJErreur, 'journal-erreur');
  SpriteCharger(SpriteJInfo, 'journal-info');
  SpriteCharger(SpriteJRoue, 'journal-roue');
  ScenePreparer(JournalRoue, Parent, X, Y, ScaleX(16), ScaleY(16), SpriteJRoue.L, SpriteJRoue.H);
  JournalRoue.Image.Visible := False;
  JournalHVue := JournalH1;
  JournalHCible := JournalH1;
  JournalPos := GetArrayLength(Lignes) - 1;
  JournalSale := True;
  JournalVu := -100000;
  JournalRoueVu := -1;
end;

procedure JournalMarque(B: TBitmap; Etat, X, Y: Integer);
begin
  case Etat of
    jEnCours: SpritePoser(B, SpriteJEnCours, 0, X, Y, 255);
    jFait: SpritePoser(B, SpriteJFait, 0, X, Y, 255);
    jAttention: SpritePoser(B, SpriteJAttention, 0, X, Y, 255);
    jErreur: SpritePoser(B, SpriteJErreur, 0, X, Y, 255);
  else
    SpritePoser(B, SpriteJInfo, 0, X, Y, 255);
  end;
end;

// Les lignes, la dernière en bas (pendant le défilement, tout est décalé d'une fraction de ligne) ;
// l'heure, la marque, le texte (en cours : en gras ; attention, erreur : en couleur ; le détail d'une
// ligne : en retrait). La roue va sur la ligne en cours la plus récente (cachée pendant le défilement).
procedure JournalDessiner(Ms: Integer);
var B: TBitmap;
    C: TCanvas;
    N, K, I, E, Y, X, HT, Roue, HV: Integer;
    Premier, Y1: Extended;
begin
  E := Echelle;
  B := Journal.Travail;
  N := GetArrayLength(Lignes);
  HV := Round(JournalHVue);
  K := Maxi(1, (HV - JournalTeteH - 6) div JournalLigneH);
  Premier := JournalPos - K + 1;
  if Premier < 0 then Premier := 0;
  // le fond : la carte, arrondie, sur la page blanche (de la hauteur visible) ; le trait sous l'en-tête
  Remplir(B.Canvas, 0, 0, Journal.LT, Journal.HT, clWhite);
  B.Canvas.Brush.Style := bsSolid;
  B.Canvas.Brush.Color := CouleurCarte;
  B.Canvas.Pen.Color := CouleurCarte;
  B.Canvas.RoundRect(0, 0, Journal.LT, HV * E, 16 * E, 16 * E);
  Remplir(B.Canvas, 14 * E, (JournalTeteH - 1) * E, Journal.LT - 14 * E, JournalTeteH * E, CouleurPiste);
  // les marques, sous l'en-tête seulement
  IntersectClipRect(B.Canvas.Handle, 0, JournalTeteH * E, Journal.LT, (HV - 3) * E);
  for I := Trunc(Premier) to N - 1 do begin
    Y1 := JournalTeteH + 3 + (I - Premier) * JournalLigneH;
    if Y1 >= HV then Break;
    X := (JournalMarqueX + Lignes[I].Niveau * JournalRetrait) * E - SpriteJFait.L div 2;
    JournalMarque(B, Lignes[I].Etat, X, Round((Y1 + JournalLigneH / 2) * E) - SpriteJFait.H div 2);
  end;
  SelectClipRgn(B.Canvas.Handle, 0);
  SceneMontrer(Journal);
  // les textes, à la taille de l'écran
  C := Journal.Image.Bitmap.Canvas;
  C.Brush.Style := bsClear;
  Ecrire(C.Font, 'SemiBold', 10, CouleurTitre);
  C.TextOut(ScaleX(JournalHeureX), (ScaleY(JournalTeteH) - C.TextHeight('Ég')) div 2, 'Détails');
  if JournalPhase <> '' then begin
    Ecrire(C.Font, '', 9, CouleurHeure);
    C.TextOut(Journal.Image.Width - ScaleX(14) - C.TextWidth(JournalPhase), (ScaleY(JournalTeteH) - C.TextHeight('Ég')) div 2, JournalPhase);
  end;
  IntersectClipRect(C.Handle, 0, ScaleY(JournalTeteH), Journal.Image.Width, Round((HV - 3) * FacteurY));
  Roue := -1;
  for I := Trunc(Premier) to N - 1 do begin
    Y1 := JournalTeteH + 3 + (I - Premier) * JournalLigneH;
    if Y1 >= HV then Break;
    Y := Round(Y1 * FacteurY);
    Ecrire(C.Font, '', 9, CouleurHeure);
    HT := C.TextHeight('Ég');
    C.TextOut(ScaleX(JournalHeureX), Y + (ScaleY(JournalLigneH) - HT) div 2, Lignes[I].Heure);
    case Lignes[I].Etat of
      jEnCours: Ecrire(C.Font, 'SemiBold', 9, CouleurTitre);
      jAttention: Ecrire(C.Font, 'Medium', 9, CouleurAttention);
      jErreur: Ecrire(C.Font, 'Medium', 9, CouleurErreur);
      jInfo: Ecrire(C.Font, '', 9, CouleurTexte);
    else
      Ecrire(C.Font, '', 9, CouleurTitre);
    end;
    X := ScaleX(JournalTexteX + Lignes[I].Niveau * JournalRetrait);
    C.TextOut(X, Y + (ScaleY(JournalLigneH) - HT) div 2, TexteTenu(C, Lignes[I].Texte, Journal.Image.Width - X - ScaleX(14)));
    if Lignes[I].Etat = jEnCours then Roue := I;
  end;
  SelectClipRgn(C.Handle, 0);
  Journal.Image.Repaint;
  if (Roue >= 0) and (Abs(JournalPos - (N - 1)) < 0.02) and (Abs(JournalHVue - JournalHCible) < 0.5) then begin
    Y1 := JournalTeteH + 3 + (Roue - Premier) * JournalLigneH + JournalLigneH / 2;
    X := Journal.Image.Left + ScaleX(JournalMarqueX + Lignes[Roue].Niveau * JournalRetrait) - JournalRoue.Image.Width div 2;
    Y := Journal.Image.Top + Round(Y1 * FacteurY) - JournalRoue.Image.Height div 2;
    if (JournalRoue.Image.Left <> X) or (JournalRoue.Image.Top <> Y) then
      JournalRoue.Image.SetBounds(X, Y, JournalRoue.Image.Width, JournalRoue.Image.Height);
    if not JournalRoue.Image.Visible then begin
      JournalRoueVu := -1;
      JournalRoue.Image.Visible := True;
    end;
  end else if JournalRoue.Image.Visible then JournalRoue.Image.Visible := False;
end;

// La roue : une image de sa bande toutes les 70 ms (rien d'autre n'est redessiné).
procedure JournalRoueDessiner(Ms: Integer);
var K: Integer;
begin
  if (JournalRoue.Image = nil) or not JournalRoue.Image.Visible then Exit;
  K := (Ms div 70) mod SpriteJRoue.N;
  if K = JournalRoueVu then Exit;
  JournalRoueVu := K;
  Remplir(JournalRoue.Travail.Canvas, 0, 0, JournalRoue.LT, JournalRoue.HT, CouleurCarte);
  SpritePoser(JournalRoue.Travail, SpriteJRoue, K, 0, 0, 255);
  SceneMontrer(JournalRoue);
  JournalRoue.Image.Repaint;
end;

// Le tic des détails : les lignes de l'étape PowerShell en cours (toutes les 150 ms), le défilement,
// un dessin si une ligne a changé (au plus toutes les 150 ms), la roue.
procedure TicJournal(Ms: Integer);
var Dt, N: Integer;
    Bouge: Boolean;
begin
  if (ProgresFichier <> '') and (Ms - ProgresVu >= 150) then begin
    ProgresVu := Ms;
    ProgresLire(False);
  end;
  if (Journal.Image = nil) or not JournalActif then Exit;
  Dt := Mini(100, Maxi(0, Ms - JournalHorloge));
  JournalHorloge := Ms;
  N := GetArrayLength(Lignes);
  Bouge := False;
  if JournalPos <> N - 1 then begin
    if Abs(N - 1 - JournalPos) < 0.02 then JournalPos := N - 1
    else JournalPos := JournalPos + (N - 1 - JournalPos) * Dt / (Dt + 55.0);
    Bouge := True;
  end;
  if JournalHVue <> JournalHCible then begin
    if Abs(JournalHCible - JournalHVue) < 0.5 then JournalHVue := JournalHCible
    else JournalHVue := JournalHVue + (JournalHCible - JournalHVue) * Dt / (Dt + 90.0);
    Bouge := True;
  end;
  if Bouge or (JournalSale and (Ms - JournalVu >= 150)) then begin
    JournalVu := Ms;
    JournalSale := False;
    JournalDessiner(Ms);
  end;
  JournalRoueDessiner(Ms);
end;

// Après la copie : le diaporama descend et s'en va, les détails prennent sa place (plus de lignes à
// l'écran pour tes données, la reprise, la vérification).
procedure JournalAgrandir;
begin
  if (Journal.Image = nil) or (DiapoCarte = nil) or MoteurEnPanne or (JournalHCible >= JournalH1) then Exit;
  JournalHCible := JournalH1;
  DiapoActif := False;
  TransitionAjouter(DiapoCarte, gPosition, cSortie, 0, 520, 0, DiapoY, 0, 0, 0, WizardForm.InnerNotebook.Height + ScaleY(24), 0, 0);
end;

// Tout de suite (avant un moment où l'assistant ne répond pas : la sortie des étapes PowerShell ; ou
// sans le mouvement), sans défilement.
procedure JournalMontrerMaintenant;
begin
  if Journal.Image = nil then Exit;
  try
    JournalPos := GetArrayLength(Lignes) - 1;
    if MoteurEnPanne then JournalHVue := JournalHCible;
    JournalSale := False;
    JournalVu := Horloge;
    JournalDessiner(JournalVu);
  except
    Log('Détails : ' + GetExceptionMessage);
  end;
end;

// La copie : l'animation (une bande d'images, une scène), l'état, la barre (son dégradé et son
// éclat), le fichier en cours, le temps qui reste, les détails (1.12.29), le diaporama. Les tailles :
// celles de la page intérieure (la même pour la préparation et la copie).
procedure HabillerInstallation;
var P: TNewNotebookPage;
    L, H, Y, I, PL, PH, HJ: Integer;
    Bmp: TBitmap;
begin
  if BarrePiste <> nil then Exit;
  P := WizardForm.InstallingPage;
  PL := WizardForm.InnerNotebook.Width;
  PH := WizardForm.InnerNotebook.Height;
  AnimN := {#AnimImages};
  // 1.12.29 : l'animation au cinq sixièmes (300 x 100 à 96 ppp) : la place va aux détails
  L := ScaleX({#AnimLargeur} * 5 div 6);
  H := ScaleY({#AnimHauteur} * 5 div 6);
  try
    AnimBande := TBitmap.Create;
    AnimBande.LoadFromFile(ImageUi('animation'));
    AnimL := AnimBande.Width;
    AnimH := AnimBande.Height div AnimN;
    CopieX := (PL - L) div 2;
    CopieY := 0;
    ScenePreparer(Copie, P, CopieX, CopieY, L, H, AnimL, AnimH);
    BitBlt(Copie.Travail.Canvas.Handle, 0, 0, AnimL, AnimH, AnimBande.Canvas.Handle, 0, 0, SRCCOPY);
    SceneMontrer(Copie);
  except
    Log('Animation : ' + GetExceptionMessage);
    Copie.Image := nil;
  end;
  Y := H + ScaleY(6);
  WizardForm.StatusLabel.SetBounds(0, Y, PL - ScaleX(84), ScaleY(22));
  Ecrire(WizardForm.StatusLabel.Font, 'SemiBold', 11, CouleurTitre);
  // 1.12.29 : le pourcentage, le fichier, le temps qui reste : des images (TTexteVif), pas des TLabel
  TexteVifCreer(VifPourcent, P, PL - ScaleX(84), Y, ScaleX(84), ScaleY(22), 'SemiBold', 11, CouleurTitre, clWhite, 1);
  Y := Y + ScaleY(26);
  WizardForm.ProgressGauge.Visible := False;
  BarrePiste := NouveauPanneau(P, 0, Y, PL, ScaleY(10), CouleurPiste);
  Arrondir(BarrePiste, ScaleY(10));
  BarrePleine := NouveauPanneau(BarrePiste, 0, 0, 0, BarrePiste.Height, CouleurPiste);
  BarreDegrade := NouvelleImage(BarrePleine, '', 0, 0, BarrePiste.Width, BarrePiste.Height);
  BarreDegrade.Stretch := False;
  BarreDegrade.Bitmap.Width := BarrePiste.Width;
  BarreDegrade.Bitmap.Height := BarrePiste.Height;
  // le dégradé, mis une fois pour toutes à la taille de la barre
  BarreFond := TBitmap.Create;
  BarreFond.Width := BarrePiste.Width;
  BarreFond.Height := BarrePiste.Height;
  Bmp := FondCharger('barre-degrade');
  Reduire(BarreFond.Canvas.Handle, 0, 0, BarreFond.Width, BarreFond.Height, Bmp.Canvas.Handle, Bmp.Width, Bmp.Height);
  Bmp.Free;
  BitBlt(BarreDegrade.Bitmap.Canvas.Handle, 0, 0, BarreFond.Width, BarreFond.Height, BarreFond.Canvas.Handle, 0, 0, SRCCOPY);
  SpriteCharger(SpriteBarreEclat, 'barre-eclat');
  Y := Y + ScaleY(16);
  // le fichier en cours (court : relatif au dossier du programme) et le temps qui reste. 1.12.29 : le
  // nom du fichier, Inno Setup le raccourcit (« ... ») à la largeur de son étiquette (cachée) : très
  // large, elle garde le chemin entier (les détails savent ainsi où va le fichier)
  WizardForm.FilenameLabel.Visible := False;
  WizardForm.FilenameLabel.Width := ScaleX(4000);
  TexteVifCreer(VifFichier, P, 0, Y, PL - ScaleX(200), ScaleY(18), '', 9, CouleurTexte, clWhite, 0);
  TexteVifCreer(VifReste, P, PL - ScaleX(200), Y, ScaleX(200), ScaleY(18), '', 9, CouleurTexte, clWhite, 1);
  Y := Y + ScaleY(28);
  // 1.12.29 : les détails ; le diaporama en bas, s'il reste de la place (un écran bas : les détails
  // d'abord)
  JournalY := Y;
  DiapoH := ScaleY(76);
  if PH - Y - DiapoH - ScaleY(10) >= ScaleY(120) then begin
    DiapoY := PH - DiapoH;
    HJ := DiapoY - ScaleY(10) - Y;
  end else begin
    DiapoY := -1;
    HJ := PH - Y;
  end;
  // l'image des détails va jusqu'en bas de la page ; tant que le diaporama est là (par-dessus), seule sa
  // partie du haut est une carte (JournalHVue)
  JournalCreer(P, 0, Y, PL, PH - Y);
  JournalHVue := HJ * 100 div ScaleY(100);
  JournalHCible := JournalHVue;
  if DiapoY >= 0 then begin
    // le diaporama : ce que t'apportent les deux applications, en fondu enchaîné
    DiapoCarte := NouveauPanneau(P, 0, DiapoY, PL, DiapoH, CouleurCarte);
    DiapoImage := NouvelleImage(DiapoCarte, '', 0, 0, PL, DiapoH);
    DiapoImage.Stretch := False;
    DiapoImage.Bitmap.Width := PL;
    DiapoImage.Bitmap.Height := DiapoH;
    Remplir(DiapoImage.Bitmap.Canvas, 0, 0, PL, DiapoH, CouleurCarte);
    Arrondir(DiapoCarte, ScaleX(14));
    SetArrayLength(DiapoCache, NombreDeMorceaux(DiaposImages));
    // toutes les diapositives tout de suite : pendant la copie, l'extracteur d'Inno Setup est
    // occupé (une image sortie à ce moment-là : « Cannot call file extractor recursively »)
    for I := 0 to GetArrayLength(DiapoCache) - 1 do DiapoRendre(I);
  end;
  DiapoDebut := -1;
  DiapoVu := -1;
  CopieDebut := -1;
  CopieEtaLisse := -1;
  AppDossier := ExpandConstant('{app}');
  ReserveDossier := DossierReserve('');
end;

// Les points du diaporama, en haut à droite : celui de la diapositive en cours s'allonge.
procedure DessinerPoints(C: TCanvas; Courante, Suivante: Integer; F: Extended);
var I, X, D, W, Y, Couleur: Integer;
begin
  D := ScaleX(6);
  Y := ScaleY(16);
  X := DiapoImage.Bitmap.Width - ScaleX(16);
  for I := GetArrayLength(DiapoCache) - 1 downto 0 do begin
    W := D;
    Couleur := CouleurPoint;
    if I = Courante then begin
      W := D + Round(ScaleX(10) * (1 - F));
      Couleur := CouleurEntre(CouleurTurquoise, CouleurPoint, F);
    end else if I = Suivante then begin
      W := D + Round(ScaleX(10) * F);
      Couleur := CouleurEntre(CouleurPoint, CouleurTurquoise, F);
    end;
    X := X - W;
    C.Brush.Style := bsSolid;
    C.Brush.Color := Couleur;
    C.Pen.Color := Couleur;
    C.RoundRect(X, Y, X + W, Y + D, D, D);
    X := X - ScaleX(5);
  end;
end;

// Une diapositive toutes les 5,6 s ; la suivante arrive en fondu (700 ms).
procedure TicDiapos(Ms: Integer);
var N, T, I, J: Integer;
    F: Extended;
    B: TBitmap;
begin
  N := GetArrayLength(DiapoCache);
  if (N = 0) or (DiapoImage = nil) then Exit;
  if DiapoDebut < 0 then DiapoDebut := Ms;
  T := Ms - DiapoDebut;
  I := (T div DiapoDuree) mod N;
  J := (I + 1) mod N;
  F := Courbe(cDouce, ((T mod DiapoDuree) - (DiapoDuree - DiapoFondu)) * 1.0 / DiapoFondu);
  if (F <= 0) and (DiapoVu = I) then Exit;
  DiapoVu := I;
  if DiapoCache[I] = nil then DiapoRendre(I);
  if (F > 0) and (DiapoCache[J] = nil) then DiapoRendre(J);
  B := DiapoImage.Bitmap;
  BitBlt(B.Canvas.Handle, 0, 0, B.Width, B.Height, DiapoCache[I].Canvas.Handle, 0, 0, SRCCOPY);
  if F > 0 then GdiAlphaBlend(B.Canvas.Handle, 0, 0, B.Width, B.Height, DiapoCache[J].Canvas.Handle, 0, 0, B.Width, B.Height, Round(255 * F) shl 16);
  DessinerPoints(B.Canvas, I, J, F);
  DiapoImage.Repaint;
end;

// Le chemin d'un fichier, court : relatif au dossier du programme (ou à la réserve).
function FichierCourt(S: String): String;
begin
  if (AppDossier <> '') and (CompareText(Copy(S, 1, Length(AppDossier) + 1), AppDossier + '\') = 0) then
    Result := Copy(S, Length(AppDossier) + 2, Length(S))
  else if (ReserveDossier <> '') and (CompareText(Copy(S, 1, Length(ReserveDossier) + 1), ReserveDossier + '\') = 0) then
    Result := 'Réserve de réparation : ' + Copy(S, Length(ReserveDossier) + 2, Length(S))
  else
    Result := ExtractFileName(S);
end;

function TempsRestant(N: Integer): String;
begin
  if N < 5 then Result := 'Presque fini…'
  else if N < 60 then Result := 'Encore environ ' + IntToStr(((N + 4) div 5) * 5) + ' s'
  else Result := 'Encore environ ' + IntToStr((N + 30) div 60) + ' min';
end;

// La barre suit la progression en douceur, son éclat la parcourt. 1.12.29 : son pourcentage, le
// fichier en cours et le temps qui reste, au plus cinq fois par seconde, et seulement s'ils changent
// (dans leurs images : voir TTexteVif).
procedure TicBarre(Ms: Integer);
var W, X, LE, Dt: Integer;
    F, Reste: Extended;
    S: String;
begin
  if BarrePiste = nil then Exit;
  Dt := Mini(100, Maxi(0, Ms - BarreVu));
  BarreVu := Ms;
  BarreAffiche := BarreAffiche + (BarreCible - BarreAffiche) * Dt / (Dt + 140.0);
  if Abs(BarreCible - BarreAffiche) < 0.0005 then BarreAffiche := BarreCible;
  W := Round(BarrePiste.Width * BarreAffiche);
  if W <> BarrePleine.Width then BarrePleine.Width := W;
  if W > 0 then begin
    BitBlt(BarreDegrade.Bitmap.Canvas.Handle, 0, 0, BarreFond.Width, BarreFond.Height, BarreFond.Canvas.Handle, 0, 0, SRCCOPY);
    F := (Ms mod 2300) / 1500.0;
    if F < 1 then begin
      LE := ScaleX(64);
      X := Round((W + LE) * Courbe(cDouce, F)) - LE;
      SpritePoserTaille(BarreDegrade.Bitmap, SpriteBarreEclat, 0, X, 0, LE, BarreFond.Height, 255);
    end;
    BarreDegrade.Repaint;
  end;
  if Ms - CopieVu < 200 then Exit;
  CopieVu := Ms;
  TexteVifPoser(VifPourcent, IntToStr(Round(100 * BarreAffiche)) + ' %');
  if WizardForm.FilenameLabel.Caption = '' then S := '' else S := FichierCourt(WizardForm.FilenameLabel.Caption);
  TexteVifPoser(VifFichier, S);
  if ProgresFichier <> '' then S := 'Les dernières étapes…'
  else if (BarreCible >= 0.995) or (CopieDebut < 0) then S := ''
  else if (BarreCible > 0.03) and (Ms - CopieDebut > 2500) then begin
    Reste := (Ms - CopieDebut) * (1 - BarreCible) / BarreCible / 1000;
    if CopieEtaLisse < 0 then CopieEtaLisse := Reste
    else CopieEtaLisse := 0.88 * CopieEtaLisse + 0.12 * Reste;
    S := TempsRestant(Round(CopieEtaLisse));
  end else
    S := 'Calcul du temps restant…';
  TexteVifPoser(VifReste, S);
end;

// L'animation de la copie (aussi pendant la préparation) : une image de la bande à la fois.
procedure TicCopie(Ms: Integer);
var K: Integer;
begin
  if (Copie.Image = nil) or not Copie.Active then Exit;
  K := (Ms div {#AnimDelai}) mod AnimN;
  if K = Copie.Dernier then Exit;
  Copie.Dernier := K;
  BitBlt(Copie.Travail.Canvas.Handle, 0, 0, AnimL, AnimH, AnimBande.Canvas.Handle, 0, K * AnimH, SRCCOPY);
  SceneMontrer(Copie);
  Copie.Image.Repaint;
end;

// ------------------------------------------------------------- la copie, dans les détails ----
// 1.12.29 : la copie telle qu'Inno Setup la fait - le programme (dans {app}), puis les mêmes fichiers
// dans la réserve de réparation, puis les raccourcis, le registre, la désinstallation (son état dans
// la page). Un groupe a sa ligne (ses fichiers et leur taille : d'après le manifeste, à la
// compilation) ; un gros fichier du programme (un Mo et plus) a la sienne. Aussi sans assistant : les
// lignes vont dans le journal. CopieFraction : la progression (0 à 1), qu'il y ait une barre ou non.
const
  CopieNbFichiers = {#NbFichiers};
  CopieTaille = {#TailleFichiers};

var
  CopieFraction, CopieOctets: Extended;
  CopieVuTexte: Integer;
  CopieChemin: String;

// La taille d'un gros fichier du programme (relatif à {app}), 0 s'il n'en est pas un.
function CopieTailleGros(Rel: String): Integer;
var S, Un: String;
    P, Q: Integer;
begin
  Result := 0;
  S := '{#GrosFichiers}';
  while S <> '' do begin
    P := Pos('|', S);
    if P = 0 then P := Length(S) + 1;
    Un := Copy(S, 1, P - 1);
    Delete(S, 1, P);
    Q := Pos(':', Un);
    if (Q > 0) and (CompareText(Copy(Un, 1, Q - 1), Rel) = 0) then begin
      Result := StrToIntDef(Copy(Un, Q + 1, 20), 0);
      Exit;
    end;
  end;
end;

function CopieCompte: String;
begin
  if CopieNbFichiers > 0 then Result := IntToStr(Mini(CopieN, CopieNbFichiers)) + ' / ' + IntToStr(CopieNbFichiers) + ' fichiers'
  else Result := IntToStr(CopieN) + ' fichiers';
end;

// Le texte de la ligne du groupe G (1 le programme, 2 la réserve), en cours ou fini. La taille
// copiée : celle des fichiers finis (CopieOctets) ; la totale : celle du manifeste.
function CopieTexte(G: Integer; Fini: Boolean): String;
var Total, Fait: Extended;
begin
  Total := CopieTaille;
  Fait := CopieOctets;
  if Fait > Total then Fait := Total;
  if G = 1 then begin
    if Fini then Result := 'Le programme est copié : ' + IntToStr(CopieNbFichiers) + ' fichiers, ' + TailleLisible(Total)
    else Result := 'Le programme : ' + CopieCompte + ' (' + TailleLisible(Fait) + ' sur ' + TailleLisible(Total) + ')';
  end else begin
    if Fini then Result := 'La réserve de réparation est prête : ' + IntToStr(CopieNbFichiers) + ' fichiers, ' + TailleLisible(Total) + ', pour réparer sans Internet'
    else Result := 'La réserve de réparation : ' + CopieCompte + ' (' + TailleLisible(Fait) + ' sur ' + TailleLisible(Total) + ')';
  end;
end;

// Le fichier d'avant est fini (le suivant commence) : sa taille compte.
procedure CopieFichierFini;
var T: Integer;
begin
  if (CopieChemin <> '') and FileSize(CopieChemin, T) then CopieOctets := CopieOctets + T;
  CopieChemin := '';
end;

function Ruche: String;
begin
  if IsAdminInstallMode then Result := 'HKLM' else Result := 'HKCU';
end;

// Le groupe en cours est fini : sa ligne (et celle de son gros fichier) passent à « fait ».
procedure CopieFinGroupe;
begin
  if CopieGros <> '' then begin
    JournalEcrire(jFait, 1, CopieGros, CopieGrosTexte, '');
    CopieGros := '';
  end;
  if (CopieGroupe = 1) or (CopieGroupe = 2) then JournalEcrire(jFait, 0, 'copie' + IntToStr(CopieGroupe), CopieTexte(CopieGroupe, True), '')
  else if CopieGroupe = 4 then JournalEcrire(jFait, 0, 'registre', 'Le registre : ' + Ruche + '\Software\{#Cle} (InstallDir, Version, Portee, DossierDonnees)', '');
end;

// Un fichier de plus (son chemin, tel qu'Inno Setup l'affiche) : son groupe, son compte, sa ligne.
procedure CopieFichierVu(S: String);
var G, T: Integer;
    Rel: String;
begin
  if AppDossier = '' then AppDossier := ExpandConstant('{app}');
  if ReserveDossier = '' then ReserveDossier := DossierReserve('');
  G := 0;
  Rel := '';
  if CompareText(Copy(S, 1, Length(AppDossier) + 1), AppDossier + '\') = 0 then begin
    G := 1;
    Rel := Copy(S, Length(AppDossier) + 2, Length(S));
  end else if CompareText(Copy(S, 1, Length(ReserveDossier) + 1), ReserveDossier + '\') = 0 then G := 2;
  if G = 0 then Exit;
  CopieFichierFini;
  if G <> CopieGroupe then begin
    CopieFinGroupe;
    CopieGroupe := G;
    CopieN := 0;
    CopieOctets := 0;
  end else if CopieGros <> '' then begin
    JournalEcrire(jFait, 1, CopieGros, CopieGrosTexte, '');
    CopieGros := '';
  end;
  CopieN := CopieN + 1;
  CopieChemin := S;
  CopieVuTexte := Horloge;
  JournalEcrire(jEnCours, 0, 'copie' + IntToStr(G), CopieTexte(G, False), '');
  // où vont le programme et la réserve, sur leur ligne (avec un long chemin, celle du compte était coupée)
  if G = 1 then JournalEcrire(jInfo, 1, 'copie1:dans', 'dans ' + AppDossier, '')
  else JournalEcrire(jInfo, 1, 'copie2:dans', 'dans ' + ReserveDossier, '');
  if G = 1 then begin
    T := CopieTailleGros(Rel);
    if T > 0 then begin
      CopieGros := 'gros:' + Rel;
      CopieGrosTexte := Rel + ' · ' + TailleLisible(T);
      JournalEcrire(jEnCours, 1, CopieGros, CopieGrosTexte, '');
    end;
  end;
end;

// L'état de la copie a changé (la phrase d'Inno Setup) : les raccourcis, le registre, la désinstallation.
procedure CopieEtatVu(S: String);
begin
  if S = SetupMessage(msgStatusCreateIcons) then begin
    CopieFinGroupe;
    CopieGroupe := 3;
    JournalNote(jEnCours, 'raccourcis', 'Les raccourcis : menu Démarrer > {#Nom}…');
  end else if S = SetupMessage(msgStatusCreateRegistryEntries) then begin
    if CopieGroupe <> 4 then begin
      CopieFinGroupe;
      CopieGroupe := 4;
      JournalNote(jEnCours, 'registre', 'Le registre : ' + Ruche + '\Software\{#Cle}…');
    end;
  end else if S = SetupMessage(msgStatusSavingUninstall) then begin
    CopieFinGroupe;
    CopieGroupe := 5;
    JournalNote(jEnCours, 'desinst', 'La désinstallation s''enregistre…');
  end;
end;

// Pendant PrepareToInstall (la sauvegarde de la version installée) : l'animation de la copie, une
// phrase et (1.12.29) les détails, sur la page « Préparation » ; ils passent ensuite sur la page de
// la copie. La page reprend ses propres textes (une erreur, des applications à fermer...).
procedure MontrerPreparation(Oui: Boolean);
var P: TNewNotebookPage;
begin
  if (ColonneEtapes = nil) or MoteurEnPanne then Exit;
  try
    HabillerInstallation;
    if Copie.Image = nil then Exit;
    P := WizardForm.PreparingPage;
    if Oui then begin
      JournalPhase := 'Avant la copie';
      Copie.Image.Parent := P;
      Copie.Image.SetBounds(CopieX, CopieY, Copie.Image.Width, Copie.Image.Height);
      if PreparationTexte = nil then begin
        PreparationTexte := NouveauTexte(P, 'L''assistant prépare la copie' + #160 + ': la version déjà installée, s''il y en a une, est d''abord sauvegardée.',
                                         0, Copie.Image.Height + ScaleY(12), WizardForm.InnerNotebook.Width, ScaleY(40), '', 10, CouleurTexte);
        PreparationTexte.Alignment := taCenter;
      end;
      PreparationTexte.Visible := True;
      Copie.Image.Visible := True;
      Copie.Active := True;
      if Journal.Image <> nil then begin
        Journal.Image.Parent := P;
        JournalRoue.Image.Parent := P;
        JournalActif := True;
      end;
      // la suite (sortir les étapes PowerShell) bloque l'assistant un moment : la page s'affiche
      // d'abord entière, à son état final (pas à moitié arrivée)
      TransitionsFinir;
      RailP := RailCible;
      RailPastille := RailCible;
      RailDuree := 0;
      TicRail(Horloge);
      Copie.Dernier := -1;
      TicCopie(Horloge);
      JournalMontrerMaintenant;
      RedrawWindow(WizardForm.Handle, 0, 0, RDW_TOUT);
    end else begin
      Copie.Active := False;
      if PreparationTexte <> nil then PreparationTexte.Visible := False;
      Copie.Image.Parent := WizardForm.InstallingPage;
      Copie.Image.SetBounds(CopieX, CopieY, Copie.Image.Width, Copie.Image.Height);
      if Journal.Image <> nil then begin
        Journal.Image.Parent := WizardForm.InstallingPage;
        JournalRoue.Image.Visible := False;
        JournalRoue.Image.Parent := WizardForm.InstallingPage;
        JournalActif := False;
      end;
    end;
  except
    Log('Habillage, préparation : ' + GetExceptionMessage);
  end;
end;

// ----------------------------------------------------- le récapitulatif s'écrit ----
// « Ce qui a été installé » : les lignes arrivent l'une après l'autre (ce qui se voit), le reste
// d'un coup (EcrireRecap a mis tout le texte : on repart de rien).
procedure RecapCommencer;
begin
  if (ColonneEtapes = nil) or MoteurEnPanne or (PageRecap = nil) or (GetArrayLength(RecapTexte) = 0) then Exit;
  RecapDebut := -1;
  RecapVu := 0;
  PageRecap.RichEditViewer.Lines.Text := '';
end;

procedure TicRecap(Ms: Integer);
var N, K, I: Integer;
    S: String;
begin
  N := GetArrayLength(RecapTexte);
  if (PageRecap = nil) or (RecapVu >= N) then Exit;
  if RecapDebut < 0 then RecapDebut := Ms;
  K := (Ms - RecapDebut) div 28 + 1;
  if K > 24 then K := N;
  if K <= RecapVu then Exit;
  if K > N then K := N;
  if K >= N then begin
    // tout le texte, normalement : le RichEdit refait alors ses barres de défilement
    RecapVu := N;
    PageRecap.RichEditViewer.Lines.Text := Joindre(RecapTexte);
    Exit;
  end;
  RecapVu := K;
  S := '';
  for I := 0 to K - 1 do begin
    if I > 0 then S := S + #13#10;
    S := S + RecapTexte[I];
  end;
  SendMessage(PageRecap.RichEditViewer.Handle, WM_SETREDRAW, 0, 0);
  PageRecap.RichEditViewer.Lines.Text := S;
  SendMessage(PageRecap.RichEditViewer.Handle, WM_SETREDRAW, 1, 0);
  PageRecap.RichEditViewer.Invalidate;
end;

// ------------------------------------------------------------------- sans mouvement ----
// Après une panne du mouvement : chaque scène à son état final, une fois (les instants choisis :
// ni éclat ni vague en cours).
procedure Figer;
begin
  try
    RailFige;
    if Heros.Image <> nil then DessinerHeros(101950);
    if Succes.Image <> nil then DessinerSucces(100000);
    if (DiapoImage <> nil) and (GetArrayLength(DiapoCache) > 0) then
      if DiapoCache[0] <> nil then begin
        BitBlt(DiapoImage.Bitmap.Canvas.Handle, 0, 0, DiapoImage.Bitmap.Width, DiapoImage.Bitmap.Height, DiapoCache[0].Canvas.Handle, 0, 0, SRCCOPY);
        DessinerPoints(DiapoImage.Bitmap.Canvas, 0, 1, 0);
        DiapoImage.Repaint;
      end;
    JournalMontrerMaintenant;
  except
    Log('Habillage, figer : ' + GetExceptionMessage);
  end;
end;

// ------------------------------------------------------------ chaque page arrive ----
// La page qui s'affiche entre en scène : une page intérieure glisse du côté où l'on va (Suivant :
// de la droite, Précédent : de la gauche), son titre et sa description apparaissent, l'icône de
// l'étape grandit, le trait se dessine ; l'accueil et la fin ont leur propre entrée.
procedure MouvementPage(CurPageID: Integer);
var S, I: Integer;
begin
  S := SensPage;
  SensPage := 1;
  if ColonneEtapes = nil then Exit;
  if MoteurEnPanne then begin
    Figer;
    Exit;
  end;
  Heros.Active := CurPageID = wpWelcome;
  Succes.Active := CurPageID = wpFinished;
  CopieEnCours := CurPageID = wpInstalling;
  DiapoActif := CurPageID = wpInstalling;
  if Copie.Image <> nil then Copie.Active := CurPageID = wpInstalling;
  // 1.12.29 : les détails vivent sur la page de la copie (et sur celle de la préparation, le temps
  // de PrepareToInstall : voir MontrerPreparation)
  if CurPageID = wpInstalling then begin
    JournalActif := Journal.Image <> nil;
    JournalPhase := 'La copie';
    JournalSale := True;
  end else if CurPageID <> wpPreparing then JournalActif := False;
  if CurPageID = PageAvant then Exit;   // la même page, encore (la préparation, après les applications à fermer)
  PageAvant := CurPageID;
  if CurPageID = wpWelcome then begin
    // 1.12.29 : les textes et les cartes apparaissent, puis montent (sans changer de couleur : ils se
    // redessinaient à chaque pas)
    Heros.Debut := -1;
    Apparaitre(WizardForm.WelcomeLabel1, 320);
    Arriver(WizardForm.WelcomeLabel1, AccTitreX, AccTitreY, 0, ScaleY(12), 320, 520, cSortie);
    Apparaitre(WizardForm.WelcomeLabel2, 420);
    Arriver(WizardForm.WelcomeLabel2, AccTexteX, AccTexteY, 0, ScaleY(12), 420, 520, cSortie);
    for I := 0 to GetArrayLength(CartePanneau) - 1 do begin
      Apparaitre(CartePanneau[I], 560 + I * 130);
      Arriver(CartePanneau[I], CarteX[I], CarteY[I], 0, ScaleY(18), 560 + I * 130, 560, cSortie);
      Grandir(CarteLogo[I], CarteLogoX[I], CarteLogoY[I], CarteLogoL[I], CarteLogoH[I], 30, 640 + I * 130, 520);
    end;
  end else if CurPageID = wpFinished then begin
    Succes.Debut := -1;
    Apparaitre(WizardForm.FinishedHeadingLabel, 650);
    Arriver(WizardForm.FinishedHeadingLabel, WizardForm.FinishedHeadingLabel.Left, WizardForm.FinishedHeadingLabel.Top, 0, ScaleY(12), 650, 520, cSortie);
    Apparaitre(WizardForm.FinishedLabel, 780);
    Arriver(WizardForm.FinishedLabel, WizardForm.FinishedLabel.Left, WizardForm.FinishedLabel.Top, 0, ScaleY(12), 780, 520, cSortie);
    Apparaitre(WizardForm.RunList, 900);
    Arriver(WizardForm.RunList, WizardForm.RunList.Left, WizardForm.RunList.Top, 0, ScaleY(14), 900, 520, cSortie);
  end else begin
    Arriver(WizardForm.InnerNotebook, NotebookX, NotebookY, S * ScaleX(32), 0, 0, 360, cSortie);
    Arriver(WizardForm.PageNameLabel, TitreX, TitreY, S * ScaleX(16), 0, 0, 420, cSortie);
    Arriver(WizardForm.PageDescriptionLabel, DescX, DescY, S * ScaleX(16), 0, 70, 420, cSortie);
    // (les tailles d'ici : celles de la page à l'écran ; avant le premier affichage, la page
    // intérieure garde la largeur de sa conception)
    IconeX := WizardForm.MainPanel.Width - IconeT - ScaleX(18);
    Grandir(EnTeteIcone, IconeX, IconeY, IconeT, IconeT, 45, 40, 480);
    EnTeteTrait.Width := WizardForm.InnerPage.Width;
    Elargir(EnTeteCadre, 0, WizardForm.InnerPage.Width, 60, 620);
    if (CurPageID = wpInstalling) and (DiapoCarte <> nil) then
      Arriver(DiapoCarte, 0, DiapoY, 0, ScaleY(18), 260, 560, cSortie);
  end;
end;

// ----------------------------------------------------------------------- le tic ----
// Toutes les 15 ms : les transitions, puis les scènes à l'écran. Rien tant que l'assistant
// n'est pas visible (ou s'il est réduit).
procedure Tic(Fenetre, Message, Id, Temps: LongWord);
var Ms: Integer;
begin
  if MoteurEnPanne or EnTic or (ColonneEtapes = nil) then Exit;
  EnTic := True;
  try
    try
      if WizardForm.Visible and (IsIconic(WizardForm.Handle) = 0) then begin
        Ms := Horloge;
        TicTransitions(Ms);
        TicRail(Ms);
        TicDecor(Ms);
        if Heros.Active and (Heros.Image <> nil) then TicHeros(Ms);
        if Succes.Active and (Succes.Image <> nil) then TicSucces(Ms);
        TicCopie(Ms);
        if CopieEnCours then TicBarre(Ms);
        if DiapoActif then TicDiapos(Ms);
        TicJournal(Ms);
        TicRecap(Ms);
      end;
    except
      Panne(GetExceptionMessage);
      Figer;
    end;
  finally
    EnTic := False;
  end;
end;

procedure MoteurDemarrer;
begin
  if MoteurEnPanne or (Minuterie <> 0) or (ColonneEtapes = nil) then Exit;
  if RappelTic = 0 then RappelTic := CreateCallback(@Tic);
  Minuterie := SetTimer(0, 0, 15, RappelTic);
end;

// La progression de la copie (Inno Setup l'appelle) : 1.12.29 : ses détails (aussi sans assistant :
// le journal) ; la barre et l'anneau de l'étape la suivent.
procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
var W: Integer;
    S: String;
begin
  if MaxProgress <= 0 then Exit;
  CopieFraction := Borne(CurProgress * 1.0 / MaxProgress);
  try
    S := WizardForm.StatusLabel.Caption;
    if S <> CopieVuStatut then begin
      CopieVuStatut := S;
      CopieEtatVu(S);
    end;
    S := WizardForm.FilenameLabel.Caption;
    if S <> CopieVuFichier then begin
      CopieVuFichier := S;
      if S <> '' then CopieFichierVu(S);
    end else if ((CopieGroupe = 1) or (CopieGroupe = 2)) and (Horloge - CopieVuTexte >= 250) then begin
      // un gros fichier : sa taille avance sans qu'un autre fichier commence
      CopieVuTexte := Horloge;
      JournalEcrire(jEnCours, 0, 'copie' + IntToStr(CopieGroupe), CopieTexte(CopieGroupe, False), '');
    end;
  except
    Log('Détails, copie : ' + GetExceptionMessage);
  end;
  if BarrePiste = nil then Exit;
  BarreCible := CopieFraction;
  if CopieDebut < 0 then CopieDebut := Horloge;
  // l'anneau de l'étape suit la barre ; le lien vers la dernière étape se remplit
  RailCible := EtapeCopie + 0.92 * BarreCible;
  RailDuree := 0;
  if MoteurEnPanne then begin
    // sans le mouvement : la barre, son pourcentage, le fichier en cours et les détails, directement
    W := Round(BarrePiste.Width * BarreCible);
    if BarrePleine.Width <> W then BarrePleine.Width := W;
    TexteVifPoser(VifPourcent, IntToStr(Round(100 * BarreCible)) + ' %');
    TexteVifPoser(VifFichier, FichierCourt(WizardForm.FilenameLabel.Caption));
    if JournalSale and (Horloge - JournalVu >= 500) then JournalMontrerMaintenant;
  end;
end;

procedure Habiller;
begin
  if WizardSilent then Exit;
  Ui2x := ScaleX(100) > 100;
  if Ui2x then Echelle := 2 else Echelle := 1;
  // 1.12.28 : la police, puis les images de l'échelle de l'écran, d'un coup (leur bloc compressé
  // est à part, dans cet ordre) ; sans les images, l'assistant reste celui d'Inno Setup
  ChargerPolices;
  try
    if Ui2x then ExtractTemporaryFiles('{tmp}\ui2x\*') else ExtractTemporaryFiles('{tmp}\ui\*');
  except
    Log('Habillage : les images ne sortent pas (' + GetExceptionMessage + ') ; l''assistant reste sobre.');
    Exit;
  end;
  HorlogeBase := GetTickCount;
  SensPage := 1;
  PageAvant := -1;
  SansComposition := Lowercase(ExpandConstant('{param:COMPOSITION|oui}')) = 'non';
  if PolicesOk then WizardForm.Font.Name := 'Barlow';
  // une erreur ici n'arrête jamais l'assistant (InitializeWizard) : le mouvement, seulement
  try
    // 1.12.29 : la taille d'abord (les pages et la colonne se posent à cette taille)
    Dimensionner;
    CreerColonne;
    HabillerEnTete;
    // l'accueil, la copie et la fin s'habillent quand ils s'affichent : la fenêtre a alors sa taille
    WizardForm.ReadyMemo.Color := CouleurCarte;
    Ecrire(WizardForm.ReadyMemo.Font, '', 10, CouleurTitre);
    MajEtapes(0);
    MoteurDemarrer;
  except
    Panne('habillage : ' + GetExceptionMessage);
    Figer;
  end;
  Composer;
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
      MsgBox('Une installation précédente d''{#Nom} a été interrompue avant la fin.' + #13#10 + #13#10 +
             'Celle-ci va la reprendre : les fichiers seront recopiés et vérifiés. Tes données ne sont pas touchées.',
             mbInformation, MB_OK);
  end;
end;

procedure CreerPageBiblio;
var Intro, Aide: TNewStaticText;
    BAjouter, BDossier, BRetirer: TNewButton;
    Haut, HListe, I: Integer;
begin
  PageBiblio := CreateCustomPage(PageAutre.ID, 'Ta bibliothèque', 'Les blocs livrés avec {#NomApi}, et les tiens : DFB, DDT, macros.');
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
  Intro.Height := ScaleY(46);   { 1.12.27 : trois lignes (la police Barlow) }
  Intro.Caption := 'Glisse ici tes fichiers .dfb, .ddt, .mac (ou un dossier entier), ou utilise Ajouter. Rangés (sinon : Perso) :';
  Haut := ScaleY(52);
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
  // 1.12.27 : le compteur sous les boutons, a droite (a cote, il etait coupe)
  TexteCompte.Width := ScaleX(150);
  TexteCompte.Left := PageBiblio.SurfaceWidth - TexteCompte.Width;
  TexteCompte.Top := BAjouter.Top + ScaleY(27);
  TexteCompte.Height := ScaleY(16);
  Aide := TNewStaticText.Create(PageBiblio);
  Aide.Parent := PageBiblio.Surface;
  Aide.AutoSize := False;
  Aide.WordWrap := True;
  Aide.Left := 0;
  Aide.Top := BAjouter.Top + ScaleY(27);
  Aide.Width := PageBiblio.SurfaceWidth - ScaleX(156);
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
                   + 'tes blocs s''ajoutent aussi plus tard, dans {#NomApi} (Bibliothèque).';
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
  // ---- 1.12.27 : l'habillage (avant les pages : elles prennent sa police) ----
  Habiller;
  // 1.12.29 : sans habillage (sans assistant, ou ses images n'ont pas pu sortir), la taille de la
  // 1.12.28 (WizardSizePercent=130,140, qui s'appliquait ensuite) : /SILENT ne change pas
  if not Dimensionne then begin
    WizardForm.ClientWidth := WizardForm.ClientWidth * 130 div 100;
    WizardForm.ClientHeight := WizardForm.ClientHeight * 140 div 100;
  end;
  // ---- Tes donnees ----
  S := 'Ce dossier reste à toi : une mise à jour, une réparation ou une désinstallation ne l''effacent jamais sans ta confirmation.' + #13#10 +
       'Il ne peut pas être dans le dossier du programme. Tu pourras le changer plus tard : accueil d''{#NomApi} ou d''{#NomIhm} > Dossiers.';
  if IsAdminInstallMode then
    S := S + #13#10 + #13#10 + 'Installation pour tous les comptes : avec le dossier proposé, chaque compte Windows a le sien, dans ses propres Documents.';
  PageDonnees := CreateInputDirPage(wpSelectDir, 'Tes données', 'Où ranger tes projets, ta bibliothèque, ton catalogue et tes captures ?', S, False, '{#Cle}');
  PageDonnees.Add('Dossier des données :');
  PageDonnees.Values[0] := DonneesDefautReel;
  if RegQueryStringValue(HKA, 'Software\{#Cle}', 'DossierDonnees', Precedent) and (Precedent <> '') then
    PageDonnees.Values[0] := DevelopperJetons(Precedent);

  // ---- Reprendre tes anciennes donnees ----
  ChercherAnciennesVersions;
  PageReprise := CreateInputOptionPage(PageDonnees.ID, 'Reprendre tes anciennes données',
    'Trouvées sur ce PC : elles sont copiées dans ton dossier des données, les originaux restent intacts.',
    'Coche ce qu''il faut reprendre (projets, bibliothèque, catalogue, captures). Un fichier de bibliothèque que tu as modifié garde ta version ; la version livrée est rangée à côté.',
    False, False);
  for I := 0 to GetArrayLength(Sources) - 1 do begin
    if I = SourceLot8 then S := 'Version précédente : ' else S := 'Ancien dossier : ';
    PageReprise.Add(S + Sources[I]);
    PageReprise.Values[I] := True;
  end;
  // 1.12.29 : la page est créée à la taille de l'assistant (Dimensionner, avant) ; sa liste, posée par
  // Inno Setup à la hauteur d'origine, descend jusqu'en bas
  PageReprise.CheckListBox.Height := PageReprise.SurfaceHeight - PageReprise.CheckListBox.Top;
  if SourceLot8 >= 0 then begin
    OptionLot8 := PageReprise.Add('Puis désinstaller cette version précédente (son programme seulement)');
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
  // 1.12.29 : le texte descend jusqu'en bas de la page (Inno Setup le pose à la hauteur d'origine)
  PageRecap.RichEditViewer.Height := PageRecap.SurfaceHeight - PageRecap.RichEditViewer.Top;
  PageRecap.RichEditViewer.Font.Name := 'Consolas';
  PageRecap.RichEditViewer.Font.Size := 9;
  PageRecap.RichEditViewer.WordWrap := False;
  PageRecap.RichEditViewer.ScrollBars := ssBoth;
  BoutonCopier := TNewButton.Create(WizardForm);
  BoutonCopier.Parent := WizardForm;
  BoutonCopier.Caption := '&Copier';
  // la marge de droite, a gauche (1.12.27 : apres la colonne des etapes)
  BoutonCopier.Left := WizardForm.ClientWidth - WizardForm.CancelButton.Left - WizardForm.CancelButton.Width;
  if BoutonCopier.Left < ScaleX(10) then BoutonCopier.Left := ScaleX(10);
  BoutonCopier.Left := BoutonCopier.Left + LargeurColonne;
  BoutonCopier.Anchors := [akLeft, akBottom];
  BoutonCopier.Top := WizardForm.NextButton.Top;
  BoutonCopier.Width := WizardForm.NextButton.Width;
  BoutonCopier.Height := WizardForm.NextButton.Height;
  BoutonCopier.OnClick := @CopierRecapClick;
  BoutonCopier.Visible := False;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // 1.12.29 : la fenetre se dessine toujours d'un bloc (si elle a ete recreee, son handle a change)
  Composer;
  // 1.12.27 : l'etape en cours a gauche, l'icone de l'en-tete. 1.12.28 : la page entre en scene
  // (MouvementPage) ; une erreur de l'habillage arrete le mouvement, jamais l'assistant.
  if ColonneEtapes <> nil then
    try
      if CurPageID = wpWelcome then HabillerAccueil;
      if CurPageID = wpInstalling then HabillerInstallation;
      if CurPageID = wpFinished then HabillerFin;
    except
      Panne('page ' + IntToStr(CurPageID) + ' : ' + GetExceptionMessage);
      Figer;
    end;
  MajEtapes(EtapeDePage(CurPageID));
  MouvementPage(CurPageID);
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
    RecapCommencer;
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
      MsgBox('Ce dossier n''a ni projets\, ni libs\, ni captures\ : ce n''est pas une ancienne version d''{#Nom}.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// 1.12.28 : Précédent - la page arrive de la gauche (voir MouvementPage).
function BackButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  SensPage := -1;
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
  if RegQueryStringValue(HKA, 'Software\{#Cle}', 'Version', Prec) and (Prec <> '') then
    S := S + NewLine + 'Mise à jour de la version ' + Prec + ' :' + NewLine +
         Space + 'elle est d''abord sauvegardée (programme, réglages, registre).' + NewLine;
  if MemoTasksInfo <> '' then S := S + NewLine + MemoTasksInfo;
  // 1.8.0 : toujours, dans le menu Demarrer (la recherche Windows le trouve).
  S := S + NewLine + 'Menu Démarrer :' + NewLine + Space + '{#NomApi}, {#NomIhm}, Désinstaller {#Nom}, Maintenance' + NewLine;
  Result := S;
end;

// --------------------------------------------------------------- installer ----
// 1.12.29 : les raccourcis, vérifiés (les détails) : ceux du menu Démarrer, ceux du Bureau (demandés).
procedure RaccourcisVerifier;
var G, D, Noms, Manque, S, L: String;
    I, N, Total, NB, K: Integer;
begin
  G := ExpandConstant('{autoprograms}\{#Nom}');
  Noms := '{#NomApi}|{#NomIhm}|Désinstaller {#Nom}|Maintenance\Réparer|Maintenance\Vérifier l''installation|' +
          'Maintenance\Reprendre une installation interrompue|Maintenance\Restaurer une sauvegarde|' +
          'Maintenance\Collecter les journaux|Maintenance\Réinitialiser mes réglages';
  if not IsAdminInstallMode then Noms := Noms + '|Tes données';
  Total := NombreDeMorceaux(Noms);
  N := 0;
  Manque := '';
  for I := 0 to Total - 1 do
    if FileExists(G + '\' + Morceau(Noms, I) + '.lnk') then N := N + 1
    else begin
      if Manque <> '' then Manque := Manque + '|';
      Manque := Manque + Morceau(Noms, I);
    end;
  S := IntToStr(N) + ' dans le menu Démarrer';
  if WizardIsTaskSelected('bureau') then begin
    D := ExpandConstant('{autodesktop}');
    NB := 0;
    for I := 0 to 1 do begin
      Total := Total + 1;
      if FileExists(D + '\' + Morceau('{#NomApi}|{#NomIhm}', I) + '.lnk') then begin
        N := N + 1;
        NB := NB + 1;
      end else begin
        if Manque <> '' then Manque := Manque + '|';
        Manque := Manque + Morceau('{#NomApi}|{#NomIhm}', I) + ' (Bureau)';
      end;
    end;
    S := S + ', ' + IntToStr(NB) + ' sur le Bureau';
  end;
  // (le dossier du menu, sur sa ligne : il est long)
  if Manque = '' then begin
    JournalNote(jFait, 'raccourcis', 'Raccourcis : ' + S);
    JournalSous(jInfo, 'raccourcis', 'raccourcis:dossier', 'dans ' + G);
  end else begin
    // ceux qui manquent, un par ligne (quatre au plus ; tous dans le journal)
    K := NombreDeMorceaux(Manque);
    JournalNote(jAttention, 'raccourcis', 'Raccourcis : ' + IntToStr(N) + ' sur ' + IntToStr(Total) + ', il en manque ' + IntToStr(K));
    JournalSous(jInfo, 'raccourcis', 'raccourcis:dossier', 'dans ' + G);
    L := Manque;
    StringChangeEx(L, '|', ', ', True);
    Log('Raccourcis manquants : ' + L);
    for I := 0 to Mini(K, 4) - 1 do JournalSous(jInfo, 'raccourcis', 'raccourcis:' + IntToStr(I), 'il manque « ' + Morceau(Manque, I) + ' »');
    if K > 4 then JournalSous(jInfo, 'raccourcis', 'raccourcis:plus', 'et ' + IntToStr(K - 4) + ' autre(s) : voir le journal de l''installation');
  end;
end;

// La sauvegarde de la version installée, avant la copie (l'étape PowerShell avant-copie). 1.12.29 :
// chaque action dans les détails.
function PreparerCopie: String;
var Code: Integer;
    Lance, Demarre: Boolean;
    Prec, PrecDossier, L: String;
begin
  Result := '';
  InstallDebut := GetTickCount;
  JournalNote(jEnCours, 'prep', 'L''assistant prépare l''installation d''{#Nom} {#Version}…');
  if RegQueryStringValue(HKA, 'Software\{#Cle}', 'Version', Prec) and (Prec <> '') then begin
    if not RegQueryStringValue(HKA, 'Software\{#Cle}', 'InstallDir', PrecDossier) or (PrecDossier = '') then PrecDossier := ExpandConstant('{app}');
    if Prec = '{#Version}' then JournalDetail(jInfo, '', 'La version ' + Prec + ' est déjà installée : elle est réinstallée')
    else JournalDetail(jInfo, '', 'La version ' + Prec + ' est installée : la {#Version} la remplace');
    if CompareText(RemoveBackslashUnlessRoot(PrecDossier), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) <> 0 then
      JournalDetail(jInfo, '', 'Elle était dans ' + PrecDossier);
  end else
    JournalDetail(jInfo, '', 'Pas de version installée ici : c''est une première installation');
  JournalDetail(jInfo, '', 'Le programme ira dans ' + ExpandConstant('{app}'));
  JournalDetail(jInfo, '', 'Tes données : ' + DonneesReel(''));
  L := ListeSources;
  if (L <> '') and (Lowercase(ExpandConstant('{param:MIGRER|}')) <> 'non') then begin
    StringChangeEx(L, '|', ', ', True);
    JournalDetail(jInfo, '', 'À reprendre après la copie : ' + L);
  end;
  JournalNote(jEnCours, 'outils', 'Les étapes de maintenance (PowerShell) sortent de l''installateur…');
  JournalMontrerMaintenant;
  ExtractTemporaryFiles('{tmp}\xpg\lib\*');
  ExtractTemporaryFiles('{tmp}\xpg\config.ini');
  if not FileExists(PowerShell) then begin
    JournalNote(jErreur, 'outils', 'Windows PowerShell 5.1 est introuvable : ' + PowerShell);
    Result := 'Windows PowerShell 5.1 est introuvable (' + PowerShell + ') : il fait partie de Windows 10 et 11.';
    Exit;
  end;
  JournalNote(jFait, 'outils', 'Les étapes de maintenance (PowerShell) sont prêtes');
  Lance := EtapePowerShell(ExpandConstant('{tmp}\xpg\lib\installer_etape.ps1'),
       '/etape:avant-copie /dossier:"' + ExpandConstant('{app}') + '" /version:{#Version} /etat:"' + DossierEtat('') + '"', Code, Demarre);
  if not Demarre then begin
    JournalNote(jAttention, 'prep', 'Windows refuse les scripts PowerShell : pas de sauvegarde, de reprise ni de vérification ; la copie se fait quand même');
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
  if Code <> 0 then begin
    JournalNote(jErreur, 'prep', 'La sauvegarde de la version installée a échoué (code ' + IntToStr(Code) + ') : rien n''a été modifié');
    Result := 'La sauvegarde de la version installée n''a pas pu être faite (code ' + IntToStr(Code) + '). Rien n''a été modifié.' + #13#10 +
              'Journal : ' + JournalInstallation;
  end else
    JournalNote(jFait, 'prep', 'Préparation terminée : la copie commence');
end;

// 1.12.28 : pendant la sauvegarde, l'animation de la copie et une phrase (MontrerPreparation).
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  MontrerPreparation(True);
  try
    Result := PreparerCopie;
  finally
    MontrerPreparation(False);
  end;
end;

procedure EcrireInstallationIni;
var L: TArrayOfString;
    Bureau, Barre: String;
begin
  if WizardIsTaskSelected('bureau') then Bureau := 'oui' else Bureau := 'non';
  // 1.8.0 : l'application demande l'epinglage a Windows au premier lancement (une fois par compte).
  if WizardIsTaskSelected('barre') then Barre := 'oui' else Barre := 'non';
  SetArrayLength(L, 22);
  L[0] := '; installation.ini - écrit par l''installateur d''{#Nom} {#Version}. Ne pas modifier :';
  L[1] := '; tes dossiers se changent dans %APPDATA%\XpgAnalyzer\XPGAnalyser.ini (accueil > Dossiers).';
  L[2] := '[Installation]';
  L[3] := 'nom = {#Nom}' + #13#10 + 'cle = {#Cle}';
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
  // 1.12.29 : la copie est finie (ses dernières lignes, les raccourcis vérifiés) ; puis chaque action,
  // dans les détails
  try
    CopieFinGroupe;
    CopieGroupe := 0;
    // (Inno Setup ne redit pas sa progression en l'enregistrant : la ligne vient ici, vérifiée)
    if RegKeyExists(HKA, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + #123 + '{#AppGuid}' + #125 + '_is1') then
      JournalNote(jFait, 'desinst', 'La désinstallation est enregistrée : Paramètres > Applications')
    else
      JournalNote(jAttention, 'desinst', 'Désinstallation : absente de Paramètres > Applications ; unins000.exe la fait');
    RaccourcisVerifier;
  except
    Log('Détails : ' + GetExceptionMessage);
  end;
  EcrireInstallationIni;
  JournalNote(jFait, 'ini', 'installation.ini est écrit : la version, tes dossiers, la maintenance');
  // L'installateur lui-meme dans la reserve : repair.bat (reinstaller) et la reprise s'en servent.
  // (1.12.29 : CopyFile depuis Inno Setup 6.4 - FileCopy, son ancien nom, y donne un avertissement.)
  Setup := ExpandConstant('{srcexe}');
#if Ver >= EncodeVer(6, 4, 0)
  if not CopyFile(Setup, DossierReserve('') + '\' + ExtractFileName(Setup), False) then begin
#else
  if not FileCopy(Setup, DossierReserve('') + '\' + ExtractFileName(Setup), False) then begin
#endif
    Log('Copie de l''installateur dans la reserve : echec');
    JournalNote(jAttention, 'setup', 'L''installateur n''a pas pu être rangé dans la réserve de réparation');
  end else
    JournalNote(jFait, 'setup', 'L''installateur est rangé dans la réserve : réparer n''aura pas besoin d''Internet');
  if PowerShellBloque then begin
    Log('Etape apres-copie non lancee : Windows refuse les scripts PowerShell (voir plus haut).');
    JournalNote(jAttention, 'fin', 'Installé, sans les étapes PowerShell (Windows les refuse), en ' + DureeLisible(GetTickCount - InstallDebut));
    JournalMontrerMaintenant;
    Exit;
  end;
  JournalPhase := 'Après la copie';
  JournalAgrandir;
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
    JournalNote(jErreur, 'fin', 'Une étape n''a pas abouti (code ' + IntToStr(Code) + ') : tes anciennes données ne sont pas touchées');
    JournalMontrerMaintenant;
    if not WizardSilent then
      MsgBox('{#Nom} est installé, mais une étape n''a pas abouti (tes données, la reprise ou la vérification ; code ' + IntToStr(Code) + ').' + #13#10 + #13#10 +
             'Tes anciennes données n''ont pas été touchées. Pour finir : menu Démarrer > {#Nom} > Maintenance > Reprendre une installation interrompue.' + #13#10 + #13#10 +
             'Journal : ' + JournalInstallation, mbError, MB_OK);
  end else begin
    JournalNote(jFait, 'fin', 'Tout est installé, en ' + DureeLisible(GetTickCount - InstallDebut));
    JournalPhase := 'Terminé';
    JournalMontrerMaintenant;
  end;
end;

// 1.12.27 : plus aucune minuterie quand l'assistant se ferme (meme annule au milieu d'une page).
procedure DeinitializeSetup;
begin
  MoteurArreter;
  if MinuterieListe <> 0 then begin
    KillTimer(0, MinuterieListe);
    MinuterieListe := 0;
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
