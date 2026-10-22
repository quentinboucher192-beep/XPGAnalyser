# API Studio et IHM Studio 1.12.25 : la barre du plein écran ne couvre plus la vue

Livrée le 22/10/2026. Elle suit la 1.12.24 du même jour (`docs/LIVRAISON-1.12.24.md`).

Elle répond à :

> « attention ça se chevauche les PID, et les boutons » (une capture de la vue Réglages PID en plein écran)

> « voilà les adresses IP des automates et IHM, ajoute ca dans une vue 'Architecure' + sur les variables on prend l'adresse de l'API primaire donc CPU IP1, on corrige la partie equipement »

## 1. Ce qui change

### La barre du plein écran ne couvre plus la vue

En plein écran, la petite barre flottante (le nom de la vue, le zoom, Remplir l'écran…, Quitter) revenait au moindre mouvement de la souris, et dès que la souris approchait du haut de l'écran. Elle se posait alors sur le bandeau de la vue, là où l'IHM de l'affaire a ses boutons (AMF, LUNA, API A, API B, Forçages, l'utilisateur) : au moment de les toucher, la barre les cachait et prenait le clic.

Maintenant, elle se montre à l'entrée en plein écran, puis seulement quand la souris touche le bord du haut de l'écran (ou un doigt au bord du haut, sur un écran tactile). Elle reste tant que la souris est dessus et s'efface une seconde après. La souris sur la vue, même sur son bandeau, ne la ramène pas. Échap et F11 sortent toujours du plein écran.

### Les heures sous les courbes restent dans la courbe

La dernière graduation du temps d'une courbe (l'heure) dépassait de l'objet de la moitié de sa largeur : sur une courbe étroite (les quatre cartes de la vue Régulation), la carte voisine la coupait. Les graduations tiennent dans la courbe, et celle qui toucherait sa voisine n'est pas écrite.

### L'historique des modifications des objets commence

À chaque Enregistrer, chaque objet créé, modifié ou supprimé depuis le dernier enregistrement est noté dans le projet (`ihm/historique-edition`, un fichier par vue), propriété par propriété, avant et après, signé de la date, du nom du PC, du compte Windows et de la version. La fenêtre Historique (clic droit) arrive avec la 1.12.26 : elle lira ce qui est noté dès maintenant.

## 2. L'affaire 2025_06_118 : le projet IHM

Le projet de l'affaire (dossier `2025_06_118_IHM`, et sa démonstration `2025_06_118_IHM_DEMO`) est régénéré ; il s'ouvre avec la 1.12.24 comme avec la 1.12.25 (le format ne change pas), mais la 1.12.25 est conseillée pour le plein écran.

- **Les adresses IP.** L'équipement `M580_HSBY`, celui des variables, interroge **192.125.6.40** : l'adresse principale de la paire Hot Standby (CPU IP 1), toujours portée par la CPU primaire. La CPU de secours porte 192.125.6.41 (IP 2) ; masque 255.255.255.0, passerelle 192.125.6.254. Jusque-là, l'adresse était provisoire (192.168.10.20).
- **La vue Architecture** (nouvelle, menu Maintenance, avant Diagnostic API) : le réseau Ethernet TCP/Modbus (IHM A et IHM B, MAESTRO, CIMVIEW, la passerelle, le switch et l'état de la liaison IHM ↔ automate), la paire Hot Standby (le rôle de chaque CPU et l'adresse qu'elle porte en ce moment, la liaison Hot Standby), l'Ethernet RIO et ses deux stations (AMF, drop 1 ; LUNA, drop 2), puis le tableau des adresses IP.
- **Les pupitres** : IHM A 192.125.6.45, IHM B **192.125.6.46** (le 192.145.6.46 du mail était une faute de frappe, corrigé le soir même : le projet a été relivré). Tout est sur le même réseau (masque 255.255.255.0, passerelle 192.125.6.254). Les adresses de MAESTRO et de CIMVIEW restent à communiquer.
- **Les chevauchements.** Vue Réglages PID : la pastille EN SERVICE couvrait la case Td (12 px) ; les trois cases et la pastille sont maintenant côte à côte. Les voiles « boucle hors service » (posés sur les cartes Réglage PID, montrés en marche quand une boucle est hors service) couvraient les cartes dans l'éditeur : ils sont sur un calque à eux, « Voiles hors service », caché dans l'éditeur ; en marche rien ne change. Aussi : fenêtre Extracteur, le total des démarrages touchait la colonne 24 h (il a sa ligne) ; vue Régulation, les valeurs des cartes ; synoptiques, les pastilles des sondes. Toutes les vues ont été passées au crible : plus aucun objet n'en chevauche un autre.

## 3. Les essais

80 suites sur 80. Ce qui change dans les essais :
- `hmiedits` (46 contrôles), nouvelle : l'historique des modifications des objets (la comparaison propriété par propriété, revenir, les entrées d'un enregistrement, le fichier, 100 entrées par objet, l'enregistrement sur le disque, la signature).
- `hmieditor` (7674 contrôles) : le plein écran - la souris sur le bandeau de la vue, puis ailleurs, ne ramène pas la barre ; le bord du haut la ramène, elle reste tant que la souris est dessus et s'efface 1 s après ; effacée, un clic sous elle ne la ramène pas ; les notes (390) et les versions (58).

## 4. Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur la démonstration de l'affaire.

**Le plein écran** (Vue_PID, Remplir l'écran, 1920 × 1080) :
- `1225_30_pid_plein_ecran_entree.png` : à l'entrée, la barre ;
- `1225_31_pid_plein_ecran_souris_sur_le_bandeau.png` : la souris sur Forçages, dans le bandeau : la barre est partie, les boutons AMF, LUNA, API A, API B et Forçages sont dégagés ;
- `1225_32_pid_plein_ecran_souris_au_bord.png` : la souris au bord du haut : la barre revient ;
- `1225_33_pid_plein_ecran_barre_effacee.png` : la souris partie : elle s'efface.

**L'éditeur** :
- `1225_50_editeur_vue_pid.png` : la Vue_PID dans l'éditeur, les quatre cartes Réglage PID visibles, le calque « Voiles hors service » caché ;
- `1225_51_editeur_architecture.png` : la vue Architecture ;
- `1225_52_equipement_m580.png` : l'équipement M580_HSBY, 192.125.6.40.

**L'affaire** (les captures de sa livraison, refaites : la navigation a une entrée de plus) : `14a_architecture.png` (la vue Architecture), `07_pid.png` (la case Td dégagée), `05_regulation.png` (les heures sous les courbes dans les cartes), `03_popup_extracteur.png` (les compteurs).

## 5. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.25.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.25-portable.zip` et `IHM-Studio-1.12.25-portable.zip` : chaque application, sans installation ;
- `2025_06_118_IHM_Studio.zip` et `2025_06_118_IHM_DEMO.zip` : le projet IHM de l'affaire et sa démonstration ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.25.exe`. Il met à jour la version installée (la 1.12.24 ou une plus ancienne) en place : même dossier, données reprises. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».
3. Le projet de l'affaire : décompresse `2025_06_118_IHM_Studio.zip` (ou la démonstration) **à la place** de l'ancien dossier, qu'il remplace en entier (vues, variables, équipement) ; si tu avais modifié le projet de ton côté, garde une copie avant.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.25.exe` (34,6 Mo, 36 260 174 octets) :
`95DE7991059687313A3E186914070FC7AF4DF45F737A4DF5C4D214ED9C2F8F5D`

Les zips portables (151 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.25-portable.zip` (25,6 Mo) :
  `DFF10D5E6E96002C610659247A6A7AC2B0ADB1D82D5A41099621CB7CB68305F5`
- `IHM-Studio-1.12.25-portable.zip` (25,6 Mo) :
  `C5A27ABFA70DF6232A18A2C778F2381112BAB1BCA87B6B010C25456CB071117F`

## 6. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.25` et `IHM Studio 1.12.25` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ; trois sessions scriptées ont pris les captures ci-dessus.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.24 (installée avant : `IHM Studio 1.12.24`) : « Installation process succeeded » ; le dossier d'installation a `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur ; le menu Démarrer garde son dossier **API IHM Studio** ; les deux exe installés sont identiques à ceux qui ont été testés et répondent `1.12.25`.

**Le projet IHM de l'affaire** : Générer, 0 erreur et 0 avertissement ; Compiler, 0 erreur (le projet de livraison et la démonstration) ; 20 vues (17 écrans, le modèle, 2 fenêtres).

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## 7. La suite

- Communiquer les adresses de MAESTRO et de CIMVIEW ; reporter les adresses dans l'AF § 6 (champs « XXX ») à sa prochaine révision.
- La fenêtre Historique des objets (clic droit : la liste des enregistrements, Avant / Après, Revenir, Restaurer, CSV ; la section Suivi de Propriétés ; la date dans l'Explorateur) : 1.12.26.
- En attente : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.
