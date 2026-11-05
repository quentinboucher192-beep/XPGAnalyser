# API Studio et IHM Studio 1.25.0 : la mise en page des rapports, les sons et les images de la bibliothèque, les limites corrigées

Faite à partir de tes demandes du 05/11. Tes mots :
- « corrige les limites principales et fait les mises en page, la maquette est valide » ;
- « pourquoi je ne vois pas la quantité des sons dans bibliothèques de sons, faire le déballage ; ajouter un sous-onglet sons du coup » ;
- « ajouter bibliothèques d'images aussi dans le déballage de Ressources, ça utilise les images du sous-onglet Images de l'éditeur de vues » ;
- « pourquoi il n'y a pas "aller à" ou "ouvrir dans l'explorateur de fichiers" ici ? » (Rapports › Écrits).

Elle se met à jour par-dessus la 1.24.0 et garde tes données. Tes projets se relisent tels quels : un rapport sans mise en page prend Standard, celle d'avant.

## 1. Ce qui change pour toi

### La mise en page des rapports (ta maquette)

Configuration › Rapports, un troisième onglet : **Mise en page**.

- **À gauche, les mises en page** :
  - **les fournies**, verrouillées (un cadenas) : **Standard** (celle d'avant, la mise en page par défaut), **Compacte** (A4 paysage, petits caractères, avec les courbes) et **Synthèse seule** (la synthèse, le graphique, les cases de visa) ;
  - **celles du projet**, avec une personne ;
  - **Nouvelle** fait une copie de Standard ; **Dupliquer à partir de…** copie n'importe laquelle ;
  - **Par défaut** (★) choisit celle d'un nouveau rapport ;
  - **Supprimer** en retire plusieurs d'un coup, avec un seul Ctrl+Z ;
  - sous la liste : les rapports qui l'emploient.
- **Au milieu, les réglages** :
  - **la page** : A4, A3, A5 ou Lettre US ; portrait ou paysage ; les quatre marges ;
  - **l'en-tête** : le logo (une image des ressources, à gauche ou à droite), le titre et le sous-titre à trous, la couleur d'accent (#RRGGBB ou @Nom) ;
  - **le pied de page** : à gauche, au centre, à droite, à trous ; les cases de visa (Établi par ; Vérifié par…) ;
  - **le contenu** : les parties à imprimer et leur ordre. Coche ; glisse une ligne, ou Alt+↑ ↓. Une partie est nouvelle : **les courbes des mesures** ;
  - **les tableaux** : leur taille, les lignes alternées, l'en-tête répété ;
  - **le texte** : la police (Helvetica, Times, Courier), le titre, le corps.
  - Les trous : {Titre} {Rapport} {Nom} {Periode} {Site} {Projet} {Debut} {Fin} {Date} {Heure} {Page} {Pages}.
- **À droite, l'aperçu** :
  - la page telle que le PDF l'écrira (le même calcul), page par page, avec un zoom ;
  - avec des **données d'exemple** ou la **dernière période réelle**.
- **En bas** :
  - « Exploitation : modifiée (non enregistrée) · utilisée par 1 rapport » ;
  - **Ctrl+Z** défait le dernier réglage ;
  - **Enregistrer la mise en page** (Ctrl+S) la garde dans le projet, et Ctrl+Z la rend ensuite comme avant ;
  - **Annuler les modifications** revient à l'enregistrée.
- **Chaque rapport a sa mise en page** : sa case « Mise en page » à droite et sa colonne dans la liste. Les rapports écrits à l'heure dite la prennent.
- Une partie s'imprime si la mise en page la coche **et** si le rapport la recueille (ses cases Alarmes, Production, Événements ; ses mesures).

### Générer le rapport maintenant (ta maquette)

**Générer maintenant…** et **Dernière période…** ouvrent une fenêtre :
- **la période** : la dernière (« 04/11 06:00 → 05/11 06:00 »), en cours, ou **Choisir…** (du … au …) ;
- **le format** : PDF ou Excel ;
- **la mise en page** : celle du rapport proposée d'abord, puis celles du projet et les fournies. **Modifier les mises en page…** ouvre l'onglet ;
- **après l'écriture** : ouvrir le fichier ; l'envoyer ou non aux destinataires du rapport ;
- **l'aperçu de la page 1**, qui suit la mise en page choisie (◀ ▶ : les autres pages) ;
- le nom du fichier dans exports/.

**Générer** l'écrit dans exports/, **Ailleurs…** demande où, **Échap** ne fait rien.

### Les sons et les images de la bibliothèque

- **Dans l'arbre du projet** : Ressources › **Bibliothèque de sons [N]** et **Bibliothèque d'images [N]** disent combien elles en ont, et **se déplient** :
  - Mes sons, Alarmes [7]… ;
  - Mes images, puis les catégories du sous-onglet Images, chacune avec son nombre ;
  - puis les sons et les images, avec un cadenas (fourni) ou une personne (à toi).
- **Un clic** :
  - sur un son : la bibliothèque de sons, sur lui ;
  - sur une image : l'éditeur de vues, sous-onglet Images, l'image montrée et choisie.
- **Dans l'éditeur de vues**, la BIBLIOTHÈQUE a un sous-onglet **Sons** (Objets | Images | **Sons** | Variables) :
  - les dossiers et leur nombre, Écouter (Espace), Importer (Entrée), + (Mes sons) ;
  - la recherche de la bibliothèque cherche aussi les sons ;
  - **Au clic** : l'objet choisi dans la vue joue le son quand on clique dessus. Le son est importé s'il ne l'est pas, une action « Jouer un son » s'ajoute ; un seul Ctrl+Z annule.

### Les limites de la 1.24.0, corrigées

**Supprimer plusieurs, là où c'était encore une ligne à la fois :**
- **les enregistrements** : ils vont dans la corbeille du projet (`ihm/corbeille`), donc **un seul Ctrl+Z** les rend (un seul enregistrement supprimé revient aussi) ;
- **les bibliothèques reliées** : « Délier les N », un seul Ctrl+Z ;
- **côté automate**, avec un seul Ctrl+Z :
  - les tâches (MAST et une tâche qui exécute encore des sections restent) ;
  - les modules des racks (le processeur et l'alimentation restent) ;
  - les unités et leurs sections ;
  - les blocs DFB ;
  - Suppr marche aussi dans les DDT ;
- **Mes sons** : plusieurs d'un coup, un son natif reste (le compte rendu le dit), un seul Ctrl+Z les rend ;
- **les incidents** et **les listes de l'Assistant IA** (Apprentissage, Mémoire, Souvenirs, les conversations du panneau) : plusieurs d'un coup, avec une question qui dit que c'est **définitif**. Ces listes n'ont pas de corbeille.
- **Corrigé** : choisir plusieurs tables d'animation n'en gardait qu'une.

**Les chemins dans les champs et les libellés** : clic droit Ouvrir dans l'explorateur, Aller à, Copier le chemin.
- **Les champs de chemin avec leur bouton « … »** gardent Couper, Copier, Coller et Tout choisir :
  - Exporter vers… (chaque export) ;
  - Paramètres › Dossiers ;
  - Nouveau projet, Nouvelle IHM, Nouvelle machine, Dupliquer ;
  - Archives, Versions, Importer ;
  - Chercher des fichiers ;
  - les champs de fichier des formulaires de macros.
- **Les lignes et libellés de chemin** :
  - les dossiers de Paramètres › Dossiers ;
  - le pied des paramètres de l'Assistant IA et des clés API ;
  - le titre du code d'une macro ;
  - la source de l'import en cours, la ligne Fichier de l'import de l'IHM ;
  - l'en-tête de l'aperçu des fichiers.
- **Des tableaux de plus** : Bibliothèques reliées (Chemin), Générer › Diagnostics (Fichier), Versions, l'Assistant IA (Mémoire, Souvenirs, Apprentissage).
- **Chercher des fichiers**, **Incidents** et **À propos** proposent maintenant les mêmes entrées que partout ailleurs.

**Les volets de l'automate** (variables, tables d'animation, DDT, tâches, modules, unités, blocs) ont maintenant leur essai automatique de la suppression à plusieurs : la nouvelle suite `apiplusieurs`.

### Les chemins : Rapports › Écrits

Le clic droit d'un rapport écrit propose maintenant **Ouvrir dans l'explorateur**, **Aller à** et **Copier le chemin**.

## 2. Les essais

Sous Linux : **134 suites d'essais sur 134** réussies (CTest), dont une nouvelle.

Au premier passage, `hmieditor` avait 3 échecs. Les trois venaient des essais eux-mêmes ; le logiciel n'a pas été changé pour les corriger, et l'installateur vérifié reste valable :
- un compte de sujets d'aide (247 → 248 : le nouveau sujet « La mise en page des rapports ») ;
- la liste des fichiers qui ont le droit d'écrire des octets bruts (l'en-tête binaire du PDF est passé de HmiReports.cpp à HmiReportLayout.cpp).

Après correction, `hmieditor` repasse en entier.

- **Nouvel essai : `apiplusieurs`** (65 contrôles). Il couvre la suppression à plusieurs côté automate (variables, tables d'animation, DDT, tâches, modules, unités et sections, blocs DFB) et dans les listes de l'Assistant IA.
- **`hmi_test`** : 6 912 contrôles, dont 52 nouveaux pour la mise en page. Ils couvrent :
  - les fournies, les formats et l'orientation, l'ordre et les parties, les trous ;
  - ce qui ne va pas (nom, marges, tailles, police, couleur, logo, aucune partie), la couleur d'accent @Nom ;
  - celle d'un rapport et celle par défaut, renommer, retirer ;
  - les données d'exemple, la page calculée et le PDF qui ont les mêmes pages, les courbes, les cases de visa, A3 paysage en Courier, Times ;
  - les lignes alternées, l'en-tête répété, le logo sur chaque page et à droite ;
  - Excel dans l'ordre de la mise en page, l'enregistrement et la relecture ;
  - le moteur qui écrit avec la mise en page du rapport ou celle choisie, une période choisie, sans envoi, en Excel.
- **`hmi_editor_test`** : 10 552 contrôles (9 972 en 1.24.0). Ce qui est nouveau :
  - **l'onglet Mise en page** : les fournies verrouillées, Nouvelle, le brouillon et l'aperçu, une marge refusée, les parties cochées et déplacées, Ctrl+Z dans l'onglet, Enregistrer (une commande), Annuler les modifications, la colonne et la case Mise en page du rapport, renommer (le rapport suit), Dupliquer, Par défaut, supprimer deux mises en page (un seul Ctrl+Z), l'aperçu sur la dernière période ;
  - **la fenêtre Générer maintenant** : la mise en page du rapport proposée d'abord, l'aperçu qui suit, une période choisie, Excel, sans envoi, le fichier ouvert, Échap, Modifier les mises en page… ; Écrits : la colonne Fichier est un chemin ;
  - **le sous-onglet Sons** : sa place, les dossiers et leur nombre, la recherche, Au clic (puis un seul Ctrl+Z) ;
  - **l'arbre** : la bibliothèque de sons et la bibliothèque d'images, leur nombre, leurs dossiers, leurs éléments ;
  - **les limites** : les chemins des champs et des libellés, supprimer plusieurs (enregistrements, Mes sons, bibliothèques…).
- `hmi_incident_test` : supprimer plusieurs incidents.
- Les suites de l'Assistant IA (mémoire, souvenirs, l'Assistant) : supprimer plusieurs.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.25.0.exe` : l'installateur (49 369 201 octets). Il met à jour la 1.24.0 au même endroit ;
- `API-Studio-1.25.0-portable.zip` et `IHM-Studio-1.25.0-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.25.0.exe` | `DA0395EC8FF77B7891D8FD0C53A4D0D7735330CB9106BE0A92AF6A9DD6151B07` |
| `API-Studio-1.25.0-portable.zip` | `251FE2ABB549D447F164B2C76968E2EA00B2B64A5230E9AC5DF0B3EDB1DDB224` |
| `IHM-Studio-1.25.0-portable.zip` | `7FEFF3340D35D5FC166E127307F6E154C5C53F5E4EBB6DB76F7E4DAF469E7F87` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.24.0 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.25.0 » et « IHM Studio 1.25.0 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.24.0 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **La mise en page** :
  - le PDF emploie les trois polices de tout PDF (Helvetica, Times, Courier) : pas d'autre police ;
  - les courbes des mesures ne vont pas dans Excel (seuls les tableaux et la synthèse y vont) ;
  - l'aperçu dessine le texte avec la police de l'écran : à petite échelle, il peut différer d'un pixel du PDF. Les pages, les coupures et les positions sont les mêmes.
- **Les chemins** : ne sont pas couverts, chacun pour une raison :
  - les cartes des projets récents de l'écran d'accueil : leur menu a déjà « Ouvrir le dossier » ;
  - l'écran de rapport de plantage, qui a ses boutons ;
  - les noms des fichiers déposés : des noms temporaires ;
  - les rapports du centre d'aide, qui ont leurs boutons ;
  - les messages de la barre d'état ;
  - les chemins à l'intérieur d'une archive (Échanges) : ils ne sont pas sur le disque ;
  - les colonnes « Chemin » des variables publiques et des références croisées : des noms, pas des fichiers.
- **Supprimer plusieurs** :
  - les requêtes et les jeux de l'outil Modbus se suppriment encore un à la fois ;
  - les macros et les versions supprimées depuis l'arbre ne reviennent pas par Ctrl+Z (la question le dit) ;
  - les équipements supprimés à plusieurs gardent toujours leurs variables, déliées.
- **Les titres et les intertitres** prennent maintenant la couleur d'accent, bleue dans Standard : c'est le seul changement visible d'un rapport en Standard.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.** Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais : l'application n'a pas été ouverte dans une fenêtre.
