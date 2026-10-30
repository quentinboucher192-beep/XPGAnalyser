# API Studio et IHM Studio 1.14.0 : les enregistrements, le son, le chercheur de fichiers, l'installateur

Livrée le 30/10/2026. Elle suit la 1.13.1 (`docs/LIVRAISON-1.13.1.md`).

Elle répond à :

> « petit bug dans Utilisations, il faut checker les [0] [1] etc, à adapter avec tous les types composés existants
> ajouter dans la bibliothèques d'objet un objet pour couper, annuler le mute sur tous les sons possibles, un melangeur de colume avec curseur, et possibilité de couper certains canaux si ca marche avec des canaux (envoie un png)
> j'aimerais pouvoir créer des enregistrements en mode poste d'exploitation ou simulation avec des objets pour lancer des enregistrements, les enregistrements sont stockés sous forme de fichiers, on peut lancer des enregistrements d'une application externe, possibilité d'attribuer un mot de passe et en gros un enregistrement ca fait comme si qu'on avait filmé l'application qui tournait et ca rejouait le scénario
> dans les resources ajouter un dossier par défaut natif 'enregistrements' et on peut importer/exporter des enregistrements
> ajouter des actions dans les actions des objets liées aux enregistrements et des fonctions IHM_ liées aux enregistrements, des objets dans la biblio qui gére des enregistrements, demarrage arret sauvegarde etc jouer, on peut à tout moment quitter un replay avec echap et y'a des options pour accelerer revenir en arriere pause, -10s +10s etc, on voit une progressbar
> ajouter des fonctions dans natives sur les enregistrements et des vars systemes
> fait une maquette interactive en prenant le vrai visuel du logiciel
> dans l'installateur fait 1 et 2 »
>
> « dans le menu principal ajoute un chercheur de fichiers compatibles sur TOUT le pc des extensions spécifiques de l'application avec un filtre pour les extensions »

Tout est fait. La maquette interactive validée avant le codage reste en ligne (même lien, mise à jour : raccourci du chercheur, codes de sortie).

## 1. Ce qui change

### Enregistrer et rejouer l'IHM, comme un film

**Enregistrer.** Le bouton **● Enregistrer** de la barre de Simulation · IHM (ou l'objet **Enregistreur** sur le poste d'exploitation) filme l'IHM qui tourne :
- les valeurs, dix fois par seconde ;
- la vue et les popups ouvertes ;
- les alarmes, les sons, le journal ;
- le pointeur et les clics de l'opérateur.

À l'arrêt, la fenêtre **Arrêter l'enregistrement** dit sa durée, ses clics, ses alarmes et sa taille. Elle demande le nom, une description et un mot de passe, puis **Jeter** ou **Enregistrer**.
- Un enregistrement protégé dès le départ le reste si on ne retape pas le mot de passe. Seule la case « Retirer la protection » l'enlève.
- L'enregistrement est un fichier `.xrec`, écrit au fil de l'eau : une appli arrêtée net ne perd que la dernière seconde.
- Avec un mot de passe, la piste est chiffrée : sans lui, il ne se rejoue pas et ses valeurs ne se lisent pas. Son nom, sa date et sa description restent lisibles.

**Rejouer.** L'IHM montre ce qui a été enregistré, sous un liséré et le bandeau **RELECTURE** :
- les objets suivent les valeurs d'alors, la vue et les popups changent ;
- le pointeur de l'opérateur passe en fantôme et chaque clic fait un cercle ;
- **la barre du bas** : la progression (avec ses repères d'alarmes et de clics) qu'on clique ou qu'on tire, Début, **−10 s**, **Pause**, **+10 s**, Fin, les vitesses **×0,25 à ×8**, Quitter ;
- **au clavier** : Espace, les flèches, + et −, et **Échap** pour quitter à tout moment.

Pendant la relecture, l'IHM **ne commande rien** : aucune écriture vers les variables, l'automate, les équipements ou les recettes, et aucun clic n'est reçu. Les courbes et les chronogrammes se refont depuis la piste. L'API simulée est en pause. Quitter rend l'IHM exactement comme elle était, à l'heure du PC.

**Ressources › Enregistrements** : le dossier natif `ressources/enregistrements` du projet.
- Le tableau : nom, durée, vue de départ, date, où, par qui, taille, cadenas.
- Le détail : l'écran à 40 %, les clics, les alarmes, la description (modifiable).
- Importer…, Exporter… (avec ou sans le mot de passe), Rejouer (ou double-clic), Renommer (les actions et les scripts qui le citent suivent ; Ctrl+Z rend le tout), Mot de passe…, Supprimer (vers la corbeille du projet), Ouvrir le dossier.
- **Configuration › Enregistrements** règle le dossier, la durée maximale (30 min), la taille maximale du dossier (2 Go), les prises par seconde, le pointeur, les sons et les popups.
- L'archive `.xihm` les emporte si on coche « Avec les enregistrements ».

**Les objets, les actions, les natives, les variables système.**
- Dans la bibliothèque, la famille **Enregistrements** : l'**Enregistreur** (un bouton rond, le chrono, son nom, sa durée et son mot de passe) et le **Lecteur d'enregistrements** (la liste d'un préfixe et son ▶ Rejouer, ou un enregistrement fixe).
- Les actions des groupes Enregistrement, Relecture et Fichiers.
- Les fonctions `IHM_ENREG_DEMARRER`, `IHM_ENREG_ARRETER`, `IHM_ENREG_EN_COURS`, `IHM_REJOUER`, `IHM_RELECTURE_PAUSE`, `IHM_RELECTURE_ALLER`, `IHM_RELECTURE_VITESSE`, `IHM_RELECTURE_QUITTER`, `IHM_ENREG_LISTE`, `IHM_ENREG_EXISTE`, `IHM_ENREG_DUREE`, `IHM_ENREG_EXPORTER`, `IHM_ENREG_IMPORTER`, `IHM_ENREG_SUPPRIMER`.
- Les variables `SYS.Recording`, `SYS.RecordingName`, `SYS.RecordingTime`, `SYS.Replay`, `SYS.ReplayPosition`, `SYS.ReplayLength`, `SYS.ReplaySpeed`, `SYS.ReplayPaused`.
- Générer signale un enregistrement cité qui n'est pas dans le dossier, et une action Pause, Avancer, Vitesse ou Quitter seule sur un clic.

**Depuis une autre application** :
```
IHM-Studio.exe --enregistrer "Nom" [--duree 10m] [--mot-de-passe …]
IHM-Studio.exe --arreter-enregistrement [--jeter]
IHM-Studio.exe --rejouer "Nom" [--vitesse 2]
IHM-Studio.exe --quitter-relecture
```
La commande va à IHM Studio déjà ouvert (la simulation ou le poste) et attend sa réponse. Codes de sortie : 0 fait, 2 aucune IHM en marche, 3 nom introuvable, 4 mot de passe faux, 5 refusé, 6 occupée, 7 fichier. Le projet doit cocher « Accepter les commandes d'une autre application » (Configuration › Enregistrements, décochée par défaut).

### Couper le son, le mélangeur de volume, les canaux

Le PNG demandé t'a été envoyé pendant la nuit (`m3-apercu/planche.png`) ; les captures Windows sont au § 3.

- **Les canaux** : chaque son part sur le canal de sa source - **Alarmes** (et leur répétition), **Scripts** (`IHM_SON`), **Actions** (Jouer un son), **Vidéos**, **Relecture** (les sons rejoués d'un enregistrement). Chaque canal a son volume et son muet. Un son joue à volume général × volume du canal, et pas du tout si l'un des deux est coupé. Couper un canal fait taire tout de suite ses sons en cours.
- **Le Coupe-son** (bibliothèque › Commandes, après Paramètres système) : un bouton à bascule. Un clic coupe tous les sons (ce qui joue se tait, les suivants ne partent pas), le suivant les rétablit sans rien rejouer. Haut-parleur et « Son actif » sur fond bleu ; haut-parleur barré et « Son coupé » sur fond rouge ; libellés vides : l'icône seule. Il peut ne viser qu'un canal. Pilotable, niveau d'accès et autorisation comme les autres commandes.
- **Le Mélangeur de volume** : un curseur Général, puis une piste par canal, chacune avec son curseur (vertical ou horizontal), son pourcentage, son bouton **M** (muet, rouge quand le canal est coupé) et un vumètre qui suit ce qui joue. Souris, molette (5 % par cran) ou doigt. « Canaux affichés » : tous, ou une liste (`General;Alarmes;Videos`).
- **Par script, action ou variable** : `IHM_SON_MUET(TRUE)`, `IHM_SON_EST_MUET()`, `IHM_SON_VOLUME('Alarmes', 80)`, `IHM_SON_CANAL_MUET('Videos', TRUE)` ; les actions du groupe Son (Couper tout le son, Rétablir le son, Basculer le son, Régler le volume, Couper ou rétablir un canal) ; les variables `SYS.VolumeAlarms`, `SYS.VolumeScripts`, `SYS.VolumeActions`, `SYS.VolumeVideos`, `SYS.VolumeReplay`, `SYS.MuteAlarms` … `SYS.MuteReplay`, en lecture et en écriture, comme désormais `SYS.SoundEnabled` et `SYS.Volume`.
- Les réglages sont ceux du poste, comme le volume de Paramètres système : ils restent au redémarrage de l'IHM, pas à la fermeture de l'appli.

### La colonne Utilisations (le bug)

Les constantes d'une vue ou d'une popup (sous-onglet Constantes) disaient **0 utilisation**, même employées partout. La colonne lit désormais tout ce que la vue lit : les formules ƒx des objets, les textes à trous `{…}`, les actions, les scripts et les fonctions de la vue, les autres constantes, et `Vue.Nom` ailleurs. Une constante d'un script général compte aussi `Script.Nom` dans les vues et les autres scripts.
- **Chaque accès compte, quel que soit le type composé** : `VannesStr[0]`, `VannesStr[i]`, `Grille[1, 2]`, `Liste.Count`, `Liste.First`, `Dico['clé']`, `Paire.Item1`, `S.Sous.Membre`, `Tab[0].Membre`, `M['a'][0]`, `$Repère$[0]`.
- Ne comptent pas : un nom plus long (`NomsBis`), une chaîne, un commentaire, le membre homonyme d'une autre variable (`Autre.VannesStr`).
- **Voir les usages** ouvre sous la table la liste de ces endroits (Où, Texte, Fois) ; un double-clic y mène.
- **Corrigé en passant** : renommer une constante d'un script général met maintenant `Script.Nom` à jour dans les formules ƒx, les textes à trous, les actions et les alarmes (avant : les scripts et les fonctions seulement).

### Chercher des fichiers sur ce PC

**Ctrl+Maj+P** (Ctrl+Maj+O, prévu dans la maquette, était déjà pris par « Importer la configuration (.XHW) »), le menu principal (« Chercher sur ce PC… »), le menu Projet et Aller à / Faire.
- **Les extensions**, cochables par groupes avec leur nombre : projets (`project.xpgproj`, `.xihm`, `.xapi`), exports Control Expert (`.xpg`, `.xhw`, `.xdb`, `.xef`), paquets IHM (`.xpgvues`, `.xpgsymboles`, `.xpgtypes`, `.xpgfonctions`, `.xpgscripts`, `.xpgst`, `.xpgmodele`), bibliothèques (`.xpgbib`, `.dfb`, `.ddt`, `.mac`), thèmes et enregistrements. Tout, Rien, « Celles d'IHM Studio » / « Celles d'API Studio ». Le choix est gardé.
- **Où** : tout le PC (lecteurs réseau, clés USB et disques externes selon leurs cases), C:, Mes documents ou un dossier. Windows, Program Files, ProgramData, la Corbeille et les dossiers cachés sont passés ; les `.zip` sont lus si on le demande.
- **La recherche** tourne en arrière-plan, en priorité basse : les fichiers arrivent au fil de l'eau, **Arrêter** l'arrête tout de suite, les liens et les jonctions ne sont pas suivis, la fin compte les dossiers refusés.
- **Les résultats** : Nom, Type, Dossier, Taille, Modifié le, Version (lue dans l'en-tête), triables ; une puce par extension trouvée ; « Nom contient » filtre au fil de la frappe.
- **Les gestes** (boutons, double-clic, clic droit) : Ouvrir avec la bonne application (l'autre est lancée avec le fichier), Ouvrir le dossier, Copier le chemin, Importer selon le genre (un paquet IHM, un modèle, une bibliothèque IHM, un .XPG / .XHW, un bloc ou une macro, un thème, **un enregistrement dans Ressources › Enregistrements**), Ajouter aux récents. Une entrée grisée dit pourquoi.

### L'installateur (« fait 1 et 2 »)

**1. Que veux-tu installer ?** Une nouvelle page après l'accueil :
- **Les deux** (le choix habituel), **API Studio seul**, **IHM Studio seul**, ou **Poste d'exploitation** (IHM Studio sans l'éditeur, qui ouvre un projet en plein écran).
- Seul ce qui sert s'installe : l'exe de l'autre application, ses raccourcis et l'ouverture de ses fichiers restent dehors.
- Le poste a sa page : le dossier du projet (vérifié : un projet IHM Studio en FINISH), « Démarrer le poste à l'ouverture de la session Windows » (cochée) et « Le relancer s'il se ferme » (un petit lanceur le relance 5 s après ; trois fermetures de suite en moins d'une minute l'arrêtent, et `poste.log` le dit). Dans le menu Démarrer, seulement « Poste d'exploitation ».
- Une mise à jour reprend le choix d'avant ; en changer retire ce qui ne sert plus, et le récapitulatif le dit.

**2. Les options**, à cocher :
- un raccourci sur le Bureau par application ;
- ouvrir les `.xihm` avec IHM Studio et les `.xapi` avec API Studio ;
- « Ouvrir avec API Studio » dans le clic droit des `.XPG` (Control Expert reste leur programme) ;
- une règle du pare-feu Windows pour IHM Studio (esclaves Modbus simulés, poste) : pour tous les comptes seulement, décochée par défaut ;
- la copie de secours pour Réparer (environ 120 Mo).

Les animations de l'assistant suivent le réglage de Windows « Afficher les animations ».

**Le bug des copies de secours** : chaque version laissait sa copie de secours et aucune ne partait (19 versions, 2,6 Go sur la machine d'essai). L'installateur garde maintenant celle de la version qu'il installe et celle de la précédente, retire les autres et le dit (« 17 anciennes copies de secours retirées, 2,3 Go libérés ») ; la désinstallation les retire toutes.

**En silencieux** : `/PROFIL=deux|api|ihm|poste`, `/POSTE="…"`, `/POSTEAUTO=oui|non`, `/POSTERELANCE=oui|non`, `/ANIMATIONS=non`, et les cases par `/TASKS` ou `/MERGETASKS` (documentés dans `installateur/LISEZ-MOI.txt`).

### L'aide et les notes

Six nouveaux sujets F1 (254 → 260) : Enregistrements, l'Enregistreur, le Lecteur d'enregistrements, le Coupe-son, le Mélangeur de volume, Chercher des fichiers sur ce PC. Et deux sections : « Le son, canal par canal » (Paramètres système) et « Installer un PC d'atelier » (le poste d'exploitation). Les passages nouveaux sont marqués « nouveau en 1.14.0 ». Les notes de version 1.14.0 (14 notes) résument tout.

## 2. Les essais

**93 suites sur 93**, sur le tout fusionné et en version 1.14.0 (une suite de plus que la 1.13.1 : `hmi_recording_test`). `hmieditor` passe de 9152 à 9420 contrôles. Il compte 463 notes, 67 versions et 260 sujets d'aide, et le guide de l'aide et son C++ disent la même chose.

Les essais ajoutés :
- `hmi_recording_test` (nouveau) : le format `.xrec` (l'en-tête, la piste par blocs, le chiffrement), l'enregistrement et la relecture, le mot de passe, les fichiers hostiles (tailles bornées), la liste qui ne lit que les en-têtes, la reprise d'un fichier interrompu, les commandes d'une autre application et leurs codes de sortie ;
- `hmieditor` :
  - la colonne Utilisations et Voir les usages, sur une vue qui cite ses constantes par tous les accès composés ; renommer une constante globale qui suit les formules, les textes à trous et les actions ;
  - les enregistrements à l'écran : Enregistrer, la fenêtre d'arrêt, la relecture (bandeau, barre, touches, heure tenue, Quitter), Ressources › Enregistrements, Configuration › Enregistrements, les deux objets ;
  - le son à l'écran : les voix par canal, la bibliothèque, l'inspecteur, le Coupe-son et le Mélangeur pilotés à la souris (clic, glisser, molette, M), le thème jour ;
  - le chercheur de fichiers : un faux arbre de dossiers avec chaque extension (majuscules et minuscules), un lien en boucle, un dossier interdit, des dossiers à passer, l'arrêt en cours, les filtres, le tri, la version lue ;
- `hmi` : les canaux du son, le muet général et par canal, les variables SYS., les natives, les actions, Générer, le code brut et Dupliquer des nouveaux objets ;
- `dossiers` : l'`installation.ini` de la 1.14.0 (profil, poste) se lit comme avant.

**Le chemin jusqu'ici** :
1. Une maquette interactive, validée avant le codage (le vrai visuel du logiciel).
2. Cinq lots faits à part : la colonne Utilisations (M1), l'installateur (M2), le son (M3), les enregistrements (M4), le chercheur de fichiers (M5).
3. Une relecture contradictoire des enregistrements (le plus gros lot). Elle a trouvé 18 défauts, tous corrigés (M4b), dont six sérieux :
   - le mot de passe perdu quand on ne le retapait pas à l'arrêt ;
   - des écritures vers l'équipement possibles pendant la relecture ;
   - l'heure qui retardait après une relecture ;
   - un `.xrec` fabriqué qui pouvait faire allouer des gigaoctets ;
   - la liste qui relisait tout le dossier toutes les 2 secondes ;
   - les popups et les courbes absentes du film.
4. La fusion des cinq lots, puis tous les essais sur le tout. Corrigé à la fusion : deux caractères accentués écrits en clair dans une source (refusés par l'essai des glyphes), et trois échappements `\xHH` mal coupés, dont un dans une note de la 1.13.1 qui affichait un caractère faux.
5. Une première série de captures sous Wine. Elle a montré dix petits défauts, tous corrigés (avec leurs essais), puis repris à l'écran :
   - les fenêtres de formulaire (Arrêter l'enregistrement, le mot de passe…) : des champs qui ne commençaient pas au même endroit, et un grand vide dessous ;
   - « mot de passe incorrect. » sans majuscule ;
   - des colonnes et un détail coupés dans Ressources › Enregistrements, le titre de la colonne Utilisations, une option et la colonne Version du chercheur, la légende de la barre de relecture ;
   - un double-clic sur un enregistrement protégé, IHM arrêtée : la fenêtre du démarrage restait derrière la demande du mot de passe (maintenant le mot de passe est demandé d'abord) ;
   - la recherche « son » de la bibliothèque qui ne trouvait pas le Mélangeur ;
   - un Coupe-son de canal qui disait « son actif » alors que tout était coupé ;
   - « {variable} » comme exemple dans le mot de passe vide de l'Enregistreur.

## 3. Les captures

Dans `captures-1.14.0.zip` (75 images), prises sous Wine avec les exe livrés, à 1920 × 1200, sur une copie du projet de démonstration (elle n'est pas dans le dépôt). Un dossier par sujet :
- `1-utilisations/` (3) : le sous-onglet Constantes d'une vue, la colonne Utilisations qui compte chaque accès (VannesStr 6, Noms 3, Jamais 0) ; Voir les usages ouvert sous la table (Où, Texte, Fois), dont `Organes[0].Ouverture` dans une formule.
- `2-son/` (13) : la bibliothèque (Commandes : le Coupe-son, le Mélangeur) ; les inspecteurs (Canaux affichés, Orientation ; Canal visé = Alarmes) ; en simulation : son actif, un son qui joue (vumètres), Alarmes à 60 % et Vidéos coupé (M rouge), tout coupé (le Coupe-son des Alarmes barré et estompé), puis le même en thème jour ; la recherche « son » dans la bibliothèque, qui trouve les deux objets.
- `3-enregistrements/` (28) :
  - Enregistrer : la barre de Simulation, l'enregistrement en cours (● ENR 00:07), la fenêtre Arrêter l'enregistrement (Jeter / Enregistrer), l'Enregistreur et le Lecteur en marche ;
  - Ressources › Enregistrements : la liste, le détail, un enregistrement protégé ;
  - la relecture : le bandeau RELECTURE, la barre de progression et ses repères, le pointeur fantôme ; après +10 s, en ×2, en pause, après −10 s, puis Échap et le retour en direct ;
  - un enregistrement protégé : la demande du mot de passe, un mot de passe faux, sa relecture ; le bouton Rejouer d'une vue (×2) ; un double-clic dans Ressources, IHM arrêtée (le mot de passe d'abord, puis l'IHM démarre et la relecture part) ;
  - Configuration › Enregistrements ; la vue en édition, les inspecteurs de l'Enregistreur et du Lecteur ; les actions et leurs groupes (Enregistrement, Relecture, Fichiers) ; la famille Enregistrements de la bibliothèque.
- `4-chercheur/` (10) : le menu du logo (« Chercher des fichiers sur ce PC… Ctrl+Maj+P ») ; la liste Où ; 30 fichiers trouvés dans un dossier d'essai, avec les puces par extension, les groupes et leur nombre, la colonne Version ; un `.xrec` choisi (Importer dans Ressources › Enregistrements) ; le clic droit ; une puce décochée ; le filtre « Nom contient » ; l'accueil et sa page Chercher.
- `5-notes/` (5) : les notes de version 1.14.0 dans l'aide.
- `6-installateur/` (16) : les nouvelles pages de l'assistant - Que veux-tu installer (première installation, depuis le poste, IHM seul, poste), la page du poste, les options (les deux, le poste, pour toi seul avec le pare-feu grisé), les récapitulatifs (le poste, les copies de secours), la copie, Ce qui a été installé (copies de secours retirées, ce qui est retiré, le démarrage et la relance) et la fin.

Les captures ont été prises deux fois. La première série a montré dix petits défauts, tous corrigés puis repris à l'écran (voir § 2, étape 5).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.14.0.exe` : l'installateur ;
- `API-Studio-1.14.0-portable.zip` et `IHM-Studio-1.14.0-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.14.0.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.14.0.exe`. Il met à jour la version installée au même endroit et garde tes données. Il reprend ton choix d'applications (pour une version d'avant la 1.14.0 : les deux).
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33) : les projets de la 1.13.1 s'ouvrent tels quels. Les enregistrements `.xrec` sont des fichiers nouveaux, à part du projet.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.14.0.exe` (38,8 Mo, 40 668 546 octets) :
`5AEEDABF1829B0719C9958A2744BAA77A79FA6EC3EF1E8EA5B5A150197AD506F`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.14.0-portable.zip` (30,1 Mo) :
  `91BA079D87E010EC8CC55904333D1D425ECF6AD0C0176A3F394E8C28107A6E9A`
- `IHM-Studio-1.14.0-portable.zip` (30,1 Mo) :
  `1A0C7FA700CF5021A8A8C321617F4B6A99318005D6F68937AA6C7A6C11E92909`

## 5. Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.14.0` et `IHM Studio 1.14.0`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.13.1 livrée : « Installation process succeeded ».
- Le profil repris est « les deux » (« Profil : deux (avant : deux, version 1.13.1) »).
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont là, et le clic droit des `.XPG` propose API Studio (OpenWithProgids), sans prendre leur association.
- Il reste deux copies de secours : la 1.13.1 et la 1.14.0.

**Pendant le développement de l'installateur** (lot M2, sous Wine, en silencieux), chaque profil a été installé puis vérifié (fichiers, raccourcis, registre, démarrage, pare-feu) :
- les deux, par-dessus la 1.13.1 : 19 copies de secours ramenées à 2, 2,3 Go libérés ;
- IHM seul, puis API seul : l'exe, les raccourcis et l'association de l'autre application partent ;
- le poste, avec démarrage à la session et relance (le lanceur, essayé avec un faux exe, relance puis s'arrête après 3 fermetures rapides) ;
- pour toi seul (le pare-feu refusé, et le récapitulatif le dit) ; sans copie de secours ; le retour du poste aux deux ; la désinstallation.

Les 16 captures des nouvelles pages de l'assistant sont dans les captures (§ 3, `6-installateur/`).

**Pas fait (Wine seulement)** :
- un vrai écran Windows ;
- le son réel : le gain appliqué pendant qu'une voix joue, l'arrêt immédiat d'un canal, le vumètre pendant qu'une alarme sonne ;
- la vraie règle du pare-feu (le netsh de Wine ne fait rien) ; le poste au démarrage d'une vraie session ; les scripts de maintenance PowerShell (Réparer sans copie de secours) ;
- le geste au doigt sur l'écran tactile d'un poste.

## 6. Les limites

- **Le son des vidéos** : le lecteur vidéo ne joue pas leur son. La piste Vidéos du Mélangeur existe et garde son réglage, mais rien n'y passe (son vumètre reste à 0).
- **Les réglages du son** (volumes, muets) restent au redémarrage de l'IHM, pas à la fermeture de l'application, comme le volume d'avant.
- **La relecture** :
  - l'API simulée est en pause pendant la relecture ;
  - les flèches de tendance et les compteurs de production restent sur le direct (le bandeau le dit) ; les courbes ne sont refaites que pour les vues montrées ;
  - un enregistrement est limité à 1 Gio de piste et 12 millions de valeurs : une relecture de plusieurs heures sur un gros projet peut être refusée (l'aide le dit) ;
  - « Écran à 40 % » montre le nom de la vue de départ, pas une miniature.
- **Le poste au démarrage de session**, pour une installation pour tous les comptes : l'entrée est commune à tous les comptes, et la case « Démarrer avec la session » de l'appli ne la montre pas. Ne coche pas les deux : le poste s'ouvrirait deux fois (la note et l'aide le disent).
- **Le pare-feu** est décoché par défaut ; la règle vaut pour tous les réseaux.
- **Les petits défauts vus aux captures finales**, pour une prochaine version :
  - dans l'onglet Chercher des fichiers, la phrase d'aide du bas est coupée (« … (API Studio ou I… ») ; elle est entière dans la page plein écran ;
  - dans l'inspecteur de l'Enregistreur, la Description vide montre « {variable} » en gris.

**En attente**, comme avant :
- les raccourcis clavier avancés des éditeurs ;
- les requêtes HTTP, MQTT ou SQL depuis un script ;
- les grafcets côté IHM.
