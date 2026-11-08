# API Studio et IHM Studio 1.41.0 : le nouveau design, première étape — le socle commun et l'Admin

Faite à partir de ton retour du 08/11 sur la maquette de la 1.40.0 : « tout est propre, je valide ».

Comme annoncé, par étapes :
1. **le socle commun** des trois logiciels (les couleurs, la police, les contrôles arrondis) — **fait** ;
2. **l'Admin comme sur la maquette** (la barre latérale sombre, le tableau de bord, la page Licences avec trois boutons et le menu « … ») — **fait** ;
3. les Studios, écran par écran — à partir de la 1.42.0.

Elle se met à jour par-dessus la 1.40.0 et garde tes données. **Le service en ligne ne change pas** : rien à redéployer.

## 0. En bref

- **Le socle commun** (Admin, API Studio, IHM Studio) :
  - **la police Inter**, fournie avec les logiciels (en normale et en demi-grasse, licence libre SIL OFL) : c'est le texte de toute l'interface. Les titres, les en-têtes de tableaux, les onglets actifs et les titres de l'aide sont en **vraie demi-grasse** (avant : le même texte tracé deux fois, qui bavait). **Le texte de tes IHM garde sa police** : rien ne change dans tes écrans d'exploitation ;
  - **le thème XPG**, les couleurs de la maquette : le fond gris bleuté, des cartes blanches, l'encre ardoise, et **une couleur par logiciel** — **violet** pour l'Admin, **bleu** pour API Studio, **turquoise** pour IHM Studio. C'est le thème par défaut ; à la mise à jour, il remplace **une fois** le Clair (celui par défaut d'avant). Le Clair d'avant et tous les autres thèmes restent dans Affichage › Thème… ;
  - **les boutons, les champs, les listes déroulantes, les cases, les bascules et les menus à coins arrondis** ; dans un menu, un geste dangereux est **en rouge**.
- **L'Admin comme sur la maquette** :
  - **la barre latérale de nuit** : la marque (la clé sur le carré violet), les groupes **Pilotage** (Tableau de bord, Licences, Clients, Demandes en ligne, Activations), **Catalogue** (Éditions, Fonctionnalités, Licences flottantes), la vente en ligne (pas encore), **Sécurité** (Révocations, Service en ligne, Historique, Paramètres de sécurité) ; à droite de chaque entrée, **son nombre** (les licences, les clients, les PC ; les demandes qui attendent ta décision en ambre) ; en bas, **la carte « Service en ligne »** (le point vert et « relevé il y a 4 s » quand il est en direct ; un clic : sa page) ;
  - **la barre du haut** : où tu es (Admin › la page), l'état du service (**vert, avec un point qui pulse, quand il est en direct**), ta clé, et **« Nouvelle licence » en violet, toujours là** — l'action principale de l'Admin ;
  - **le tableau de bord en cartes** : quatre grands chiffres (Licences actives ; PC activés avec la barre des places prises ; Demandes en attente, la carte ambrée quand il y en a ; Finissent dans 30 jours), quatre petits dessous (Clients, Licences en ligne, Révoquées, Licences flottantes) ; **l'Activité** en fil (qui, quoi, la licence et le PC, « il y a 9 min ») ; **les licences par édition** en barres violettes ; l'état du poste. Un clic sur un chiffre ouvre sa page, comme avant ;
  - **la page Licences** : la recherche et **des pastilles de filtre** (Toutes 3, Actives 2…), **la liste aérée** à gauche, **la fiche de la licence choisie** à droite — l'avatar et le client, l'identifiant, l'état en pastille (Active, Finit bientôt, Expirée, Révoquée), **trois boutons** (Prolonger… ou Ajouter un PC…, Exporter le fichier…, Voir le code… pour une licence en ligne) et **le menu « … »** (Retirer ce PC…, Nouveau code…, Relever, Ouvrir le dossier de l'archive, puis **Désactiver… et Révoquer… en rouge**) ; dessous, les chiffres (édition, fin et jours restants, type, PC, hors ligne, registre), **ses PC** et le détail complet. « Nouvelle licence en ligne… » est en haut à droite de la page.
- **Les Studios** reçoivent le socle (le thème XPG dans leur couleur, Inter, les contrôles arrondis, la vraie demi-grasse) ; leurs écrans gardent leur disposition — ils se refont un par un à partir de la 1.42.0.

## 1. Ce qu'il te reste à faire

1. **Installe l'Admin** avec `XPG-License-Admin-Setup-1.41.0-EDITEUR.exe` (le zip **à part, pour toi seul**), par-dessus la 1.40.0 : il reprend tes choix ; ta clé, ton archive et tes réglages ne sont pas touchés.
2. **Installe la 1.41.0** chez toi et chez tes clients (`API-IHM-Studio-Setup-1.41.0.exe`).
3. Rien à redéployer : le service en ligne est le même que celui de la 1.40.0.

## 2. Les essais

- **Linux : 160 suites sur 160 réussies** (`ctest`, sur la version livrée). Celles que cette version touche :
  - `themecatalog` : **le thème XPG** tient toutes les règles du catalogue (le texte à 7:1 sur les fonds, le texte secondaire, la sélection, les états et l'accent à 4,5:1, le texte du bouton principal) — c'est pourquoi le turquoise d'IHM Studio est un cran plus sombre que celui de la maquette ;
  - `adminguishell` (231 contrôles) : le menu dans l'ordre de la maquette (Pilotage, Catalogue, la vente en ligne, Sécurité), tout visible à 1100 × 700 ; « Nouvelle licence » en haut, grisée sans clé, qui ouvre l'assistant ; les cadres du tableau de bord (Activité, Licences par édition, État du poste), ses tuiles et leurs pages ;
  - `adminguilicences` (279 contrôles) : la page Licences — les pastilles de filtre et leurs comptes, la liste, la fiche, et **le menu « … »** (Retirer ce PC, Nouveau code, Désactiver, Révoquer à la fin) ;
  - `hmieditor` : la note de la 1.41.0 dans Nouveautés (114 versions) ; l'étiquette SURCHARGÉ tracée une fois, en vraie demi-grasse.
- **Sous Wine, de bout en bout**, avec les programmes Windows et le vrai programme du service lancé sur ce poste (`workerd`, le même que celui de la 1.40.0) (journal : `essai-1.41.0-wine.txt`, le code masqué ; captures : `captures-1.41.0.zip`). **L'Admin est celui installé par son installateur** ; le Studio est une copie avec la clé d'essai, jamais livrée :
  - **l'Admin en direct, dans son nouveau design** : le PC A active une licence PERSONAL, puis se libère ; l'Admin, ouvert sur Licences sans jamais cliquer Relever, montre la fiche de la licence — le PC « Poste essai A » **libéré**, « vu le 08/11 21:24 » —, la pastille verte « Service : … · en direct », la carte du menu « Connecté · relevé il y a 14 s », et l'avis en bas (capture `1410_01`) ;
  - le PC B demande sa licence, l'éditeur l'accepte : « Ta licence en ligne est activée » (`1410_03` à `1410_05`, le code d'essai masqué sur `1410_04`) ;
  - **le tableau de bord en direct** (`1410_06`), **la fiche** d'une licence (`1410_07`) et **son menu « … »** (`1410_08`) ;
  - **IHM Studio** dans le thème XPG turquoise (`1410_00`, `1410_02`) — le thème a bien remplacé le Clair à la mise à jour.
- **Les Studios** : l'installation en silencieux par-dessus la 1.40.0 passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.41.0), le bac à sable tourne, **la police Inter est installée** (`resources\fonts`), le code laissé par `/CODEFICHIER=` n'est pas dans le journal. Le Studio livré (ton service compilé) s'ouvre, sa fenêtre Licence aussi (`1410_00a`, `1410_00b`) ; API Studio livré en bleu (`1410_00c`, au premier lancement sur ce poste d'essai : la fenêtre de la clé administrateur par-dessus l'accueil). Ils ont été lancés avec une adresse de service locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'installateur de l'Admin** (sous Wine, pour toi seul, en silencieux) : l'installation passe, avec la police ; les programmes installés sont ceux construits ; le menu Démarrer, le Bureau, les quatre types de fichiers, la console dans le PATH ; une mise à jour par-dessus reprend les choix ; la désinstallation retire le programme, le service, les associations et le PATH ; **le dossier de ta clé reste intact** (son empreinte, avant et après). Ses pages : captures `1410_11` à `1410_17`.

## 3. Installer

**L'outil de l'éditeur** : double-clique `XPG-License-Admin-Setup-1.41.0-EDITEUR.exe` (dans le zip **à part, pour toi seul**).

**Les Studios** : lance `API-IHM-Studio-Setup-1.41.0.exe` (dans le zip) par-dessus la 1.40.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.41.0.exe` | `2E2309F3A02275F0E534091939A79B26F9BECB7BD5744EB745E5E45525912CA7` | 57 164 443 octets |
| `API-Studio-1.41.0-portable.zip` | `61E3CDCB4C9FCEBD036A39D6DEFF77281D4F7C60960586956CFCEAF20A4F08A2` | 217 fichiers |
| `IHM-Studio-1.41.0-portable.zip` | `52D591AEC1FFE5EEDD88EA91E0139FC2A8E4CF2695B61E65D1D2A6ABD6512F03` | 218 fichiers |
| `XPG-License-Admin-Setup-1.41.0-EDITEUR.exe` (**à part, pour toi seul**) | `CC522A920A02D565A3CDBBB49606091827ADE87E268DB720BED37DF89F451EDC` | 6 485 445 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne n'a pas été appelé.
- **Les écrans des Studios ne sont pas encore refaits** : ils ont le socle (le thème, la police, les contrôles), pas encore la disposition de la maquette (la barre d'icônes, l'arbre du programme, la simulation à droite…). C'est la suite, écran par écran.
- **Les autres pages de l'Admin** (Clients, Éditions, Fonctionnalités, Activations, Demandes en ligne, Licences flottantes, Révocations, Service en ligne, Historique, Paramètres de sécurité) ont le nouveau thème et les contrôles arrondis, mais gardent leur disposition d'avant.
- **Pas encore faits, de la maquette de l'Admin** : la recherche globale en haut (Ctrl K), la cloche des notifications, la courbe des activations sur 12 mois, la petite courbe dans « Licences actives », le prix d'une licence et l'historique dans la fiche. Le bandeau du bas reste (le dernier résultat et le journal), à la place des avis flottants.
- **Sous Wine**, après la désinstallation de l'Admin, les deux raccourcis du menu Démarrer dont le nom a un accent (Déployer le service en ligne, Désinstaller l'Admin) restent (Wine, comme en 1.40.0 où ils ne se créaient pas) ; le reste part.
- **Une police choisie par toi** (Réglages › Thème › Police du logiciel) n'a qu'une graisse : les titres sont alors en normale (avant : le faux gras).
