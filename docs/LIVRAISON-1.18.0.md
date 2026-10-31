# API Studio et IHM Studio 1.18.0 : les outils de l'IA, et tes clés API gardées

Livrée le 31/10/2026 dans l'après-midi (avancée : annoncée « ce soir vers 23 h, au plus tard demain 9 h »). Elle suit la 1.17.0 (`docs/LIVRAISON-1.17.0.md`), livrée le matin même.

Tes demandes :
- « ajoute ces outils aux IA : pouvoir créer des types génériques avec template<T>, plusieurs <T,K> possibles, pouvoir créer des symboles, pour créer des vues modèles en tête pied de page, pouvoir créer des popups, pouvoir créer les équipements avec une option pour détecter tout seul tout ce qui répond sur chaque port, pouvoir créer des scripts, fonctions, donner accès aux variables systèmes pour explorer plus d'options, accès aux ressources, pouvoir créer des ressources ou importer à partir des fichiers joints, créer des alarmes, créer des dossiers, ranger, donne lui accès aux natives » ;
- « ajoute le fait de pouvoir faire des adresses et lier des variables aux équipements + optimiser si besoin avec des formules de lien dans les structures » ;
- « quand j'ai réinstallé ça m'a viré la clé API, ajoute un gestionnaire des clés API » (la maquette, vue en début d'après-midi) ;
- « dans l'installateur ajoute un point "IA" parce que ça amène sur Terminé » ;
- le rapport de plantage de la 1.16.1 (« basic_string::_M_create », à l'ouverture de Semeru) : la 1.17.0 avait le même défaut, il est corrigé ici.

En bref :
- **L'Assistant crée tout un projet**, pas seulement la vue ouverte : symboles, popups, écrans modèles, en-têtes, pieds de page (avec leurs paramètres et leurs génériques `<T>`, `<T, K>`), types IHM génériques, équipements, adresses et formules de lien, scripts, fonctions, alarmes, ressources (à partir d'un fichier joint), dossiers et rangement.
- **Détecter ce qui répond** : l'IA le **propose**, la carte ouvre le Scanner IP avec la plage remplie, **c'est toi qui lances**.
- L'Assistant **connaît** les équipements, ressources, scripts, fonctions, alarmes et dossiers du projet ; les **variables système** et les **natives** quand ta demande en parle.
- **L'IA intégrée** comprend ces outils en français simple.
- **Le gestionnaire des clés API** : plusieurs clés nommées, celle utilisée par fournisseur, Tester, Remplacer, Supprimer ; **mémorisée par défaut** - une réinstallation ne la perd plus.
- **L'installateur** a une étape « IA » et dit dans son journal les clés gardées.
- **Corrigé** : la flèche d'un membre déplié dans Types IHM ne ferme plus le logiciel.

## 1. Ce qui change

### Les outils du projet

L'Assistant (toutes les IA : Claude, un service compatible, l'IA intégrée) peut maintenant proposer :

| Outil | Ce qu'il crée |
| --- | --- |
| Symbole, popup | Une vue de ce rôle (un symbole 200 × 200, une popup 600 × 400, son titre, modale ou non), ses **paramètres** (nom et type) et ses **génériques** `<T>` ou `<T, K>`. |
| Écran modèle, en-tête, pied de page | Une vue de ce rôle (l'en-tête et le pied : la largeur du projet, 100 de haut). |
| Type générique | Un type IHM `T_Pile<T>`, `<T, K>` : ses membres peuvent être « en T ». |
| Équipement | Modbus TCP ou Ethernet TCP : adresse IP, port, esclave (Unit Id), période, simulé ou non. |
| Adresses | Lier une variable IHM à un équipement, à une adresse (`%MW100`) ; sans adresse, la suivante libre ; pour une structure ou un tableau de structures, une **formule de lien** (`100 + 10*i`, motif `[*]`) comme « Modifier l'adresse dans toutes les cases… ». Les chevauchements sont signalés. |
| Script général | ST, C ou C++ ; au démarrage, cyclique, sur changement ou appelé ; **compilé avant d'être proposé**. |
| Fonction IHM | Ses génériques, son retour, ses entrées, son calcul ; compilée avant d'être proposée. |
| Alarme | Sa condition (compilée : chaque nom lu existe), son message, sa priorité, son groupe. |
| Ressource | À partir d'un **fichier joint** à la demande (une image, un PDF…), lu seulement à Appliquer. |
| Dossiers, ranger | Un dossier dans une liste (vues, popups, symboles, scripts, types, variables…) et y ranger ce qui existe. |

Tout reste une **proposition** : vérifiée, montrée dans l'aperçu (une section « Aussi créé ou modifié ») et appliquée en **un seul Ctrl+Z**. Rien n'est inventé : une variable, un équipement, un élément à ranger doit exister (sinon l'Assistant le dit, avec les noms proches). **Aucun code n'est exécuté par l'Assistant** : un script, une fonction ou une condition est compilé pour être vérifié, et ne tourne que quand tu lances l'IHM.

### Détecter ce qui répond : proposé, lancé par toi

Un équipement proposé avec « détecte ce qui répond » ne déclenche rien : l'aperçu a une section « Détection proposée (lancée par toi, après Appliquer) ». Après Appliquer, la carte offre **Détecter ce qui répond…** : Communication › Scanner IP s'ouvre, la plage remplie (`192.168.1.0/24`). **Rien ne part sur le réseau tant que tu n'as pas cliqué Démarrer.** Après un Ctrl+Z, le bouton disparaît.

### Ce que l'Assistant sait du projet

Avec chaque demande, en plus de la vue : la liste des équipements, ressources, scripts généraux, fonctions IHM, alarmes et dossiers du projet (en quelques lignes). Les **variables système** (`SYS.…`) et les **fonctions natives**, par thème, ne partent que si ta demande en parle (pour garder la demande légère : 31 à 40 Ko).

### L'IA intégrée comprend les outils

Sans connexion, par exemple :
- « Crée un type générique T_Pile<T> avec Valeur en T et Nombre en INT » ;
- « Crée un symbole Sym_Pompe<T> avec les paramètres Etat en BOOL et Mesure en T » ;
- « Crée une popup modale Reglage_Pompe « Réglage de la pompe » » ; « Ajoute un en-tête » ; « Crée un pied de page Pied_Station » ; « Crée un écran modèle Modele_Process » ;
- « Crée un équipement Centrale_1 en 192.168.1.40, port 502, esclave 3, et détecte ce qui répond » ;
- « Lie Niveau_Cuve_1 et Niveau_Cuve_2 à l'équipement Centrale_1 en %MW100 » (la seconde : à la suite) ;
- « Crée une alarme Niveau_Haut quand Niveau_Cuve_1 dépasse 90 avec le message « Cuve 1 pleine », priorité 1 » ;
- « Crée un script Surveillance toutes les 5 secondes », « Crée un script Init au démarrage » ;
- « Crée une fonction Plus_Grand<T> avec les paramètres A en T et B en T qui rend T » ;
- « Range Vue_1 dans le dossier Process », « Crée un dossier Recettes pour les scripts » ;
- « Quelles variables système ? ».

Une phrase qui ne demande pas un outil (« fais clignoter Bouton_1 quand l'alarme… ») n'en crée pas.

### Le gestionnaire des clés API

Paramètres de l'Assistant › **Gérer les clés…** : la fenêtre **Clés API**.
- La liste : nom, fournisseur, la clé **masquée** (son début et ses 4 derniers caractères, jamais plus), ajoutée, dernier essai (acceptée, refusée), **★ utilisée**.
- **Ajouter une clé**, **★ Utiliser**, **Tester** (la liste des modèles du fournisseur : gratuit, sur un fil - l'écran ne gèle pas), **Remplacer la clé**, **Supprimer** (un second clic confirme).
- **Mémoriser sur ce PC (protégée par Windows)** est **cochée au départ**, ici et dans les Paramètres : c'est ce qui manquait - ta clé n'était gardée que si la case était cochée.
- Les clés sont dans tes réglages (`%APPDATA%\XpgAnalyzer\IA\cles`), chiffrées par Windows : **une réinstallation n'y touche pas**. La clé mémorisée avant la 1.18.0 est reprise au premier lancement, sous le nom « Ma clé (Anthropic) ».
- Dans les Paramètres, une liste **Clés** choisit celle de l'Assistant ; une clé saisie et enregistrée y entre toute seule.

### L'installateur : l'étape « IA »

La colonne des étapes a une étape **IA** (son icône) entre Bibliothèque et Options : les pages « L'Assistant IA » et « Un apprentissage d'un autre PC » ne montrent plus « Terminé ». Le journal de l'installation dit **« Clés API : N gardées »** quand tu en as.

### Corrigé : la flèche d'un membre déplié (Types IHM)

Ton rapport de plantage (1.16.1, « basic_string::_M_create » en ouvrant Semeru) : un clic sur la flèche d'un membre (une structure dans une structure) lisait un texte déjà libéré. La 1.17.0 avait le même code ; il est corrigé et un essai refait exactement ce clic.

## 2. Les essais

**102 suites sur 102.** Les vérifications ajoutées :
- `aioptions` (224 vérifications) : les outils (types génériques, symbole, popup, en-tête, équipement + adresses + formule de lien + détection + Ctrl+Z, script, fonction et alarme compilés, ressource d'un fichier joint, dossiers et ranger, le JSON aller-retour) ;
- `aiintegree` (647) : 26 phrases des outils (et une qui ne doit pas en être un) ;
- `aiassistant` (298) : le gestionnaire des clés (migration de l'ancienne clé, deux fournisseurs, une clé de séance, remplacer, la fenêtre, la liste des Paramètres) et le bouton **Détecter ce qui répond…** (absent avant Appliquer, n'ouvre que le scanner, disparaît après Ctrl+Z) ;
- `airegistre` (251) : 19 opérations, la demande entre 31 et 40 Ko ;
- `hmieditor` : la flèche d'un membre déplié ; 74 versions, 524 notes.

L'aide : 267 sujets, le guide et son C++ disent la même chose. Le projet Visual Studio compte les mêmes fichiers que CMake.

## 3. Les captures

À ta demande, pas de passe de captures. L'installateur est vérifié sous Wine en silencieux (voir plus bas).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.18.0.exe` : l'installateur ;
- `API-Studio-1.18.0-portable.zip` et `IHM-Studio-1.18.0-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.18.0.exe`. Il met à jour la version installée au même endroit, garde tes données (le dossier `IA\`, tes conversations, ton apprentissage et **tes clés API**) et reprend ton choix d'applications.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».
3. Si ta clé avait été perdue à la réinstallation de la 1.17.0 : saisis-la une dernière fois (Paramètres de l'Assistant, ou Gérer les clés…) ; elle est maintenant mémorisée par défaut.

Le format des projets ne change pas (33).

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.18.0.exe` (41,4 Mo, 43 374 591 octets) :
`2A029F72E6923D893B5B50DCB2793B3F9540F1D4030FB573D95E0FEB0F79FEFA`

Les zips portables (167 fichiers chacun) :
- `API-Studio-1.18.0-portable.zip` (32,1 Mo) :
  `BA00791F41B70E49DE6BFF183332BF1E6199D6CD9B6119B4D10EE66D78B2CEA0`
- `IHM-Studio-1.18.0-portable.zip` (32,1 Mo) :
  `53F1D50A938A1847C053EC6F9E7600D12243FC9CDF1DFF6C7373870DDDFA4149`

### Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.18.0` et `IHM Studio 1.18.0` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

**L'installateur** (Inno Setup 6.4.1 sous Wine) a été lancé en silencieux, pour tous les comptes, par-dessus la 1.17.0 livrée, avec **une clé API d'essai** déposée dans `%APPDATA%\XpgAnalyzer\IA\cles` : « Installation process succeeded ».
- La clé est **intacte** après l'installation (même empreinte avant et après) ; le journal dit « Clés API : 1 gardée (protégée par Windows ; la réinstallation n'y touche pas) ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été essayés ; le manifeste compte 167 fichiers ; les associations `.xihm` et `.xapi` sont là ; la copie de secours de la 1.17.0 est faite.

## 5. Les fichiers modifiés

Depuis la 1.17.0, 34 fichiers :
- `AiOperations` (.cpp, .hpp) : les outils (le modèle, la lecture JSON, la vérification, la compilation des codes créés, l'application en un pas d'annulation, les détections proposées) ;
- `AiPrompt.cpp`, `AiContext` (.cpp, .hpp) : le schéma et les consignes des outils ; ce que l'Assistant sait du projet ;
- `AiPreviewDialog.cpp`, `AiPanel` (.cpp, .hpp), `AiWorkspace.cpp` : l'aperçu (Aussi créé, Détection proposée) ; le bouton de la carte et l'ouverture du scanner ;
- `AiLocal.cpp` : les phrases de l'IA intégrée ;
- `AiKeys` et `AiKeysDialog` (nouveaux), `AiSecret`, `AiSettingsDialog`, `AiAssistant` : le gestionnaire des clés ;
- `HmiFolderTable.cpp`, `HmiVariablePanes` (.cpp, .hpp) : la flèche d'un membre déplié ;
- `installateur/XPGAnalyser.iss`, son icône, `outils/installateur_images.py` : l'étape IA, le journal des clés ;
- la version, les notes, l'aide ; le projet Visual Studio ;
- les essais : `ai_options_test`, `ai_integree_test`, `ai_assistant_test`, `ai_registre_test`, `hmi_editor_test`.

## 6. Les limites

- La détection n'est jamais lancée par l'IA : elle remplit la plage du scanner (un /24 autour de l'adresse de l'équipement) ; le balayage de chaque port reste celui du Scanner IP (ports et délai réglables là).
- Le code d'un script ou d'une fonction proposé par l'IA intégrée est un squelette (`(* à compléter *)`, les entrées déclarées) : c'est une IA connectée qui écrit un vrai calcul.
- Modbus RTU n'est pas encore un type d'équipement que l'Assistant peut créer.
- Rien n'a été essayé sur un vrai Windows ; l'étape IA de l'installateur n'a pas été capturée à l'écran (l'installation en silencieux est vérifiée).
