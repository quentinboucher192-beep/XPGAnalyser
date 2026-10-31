# API Studio et IHM Studio 1.20.2 : la conversation, les incidents, les enregistrements fidèles

Livrée le 31/10/2026 dans la soirée. Elle suit la 1.20.1 (`docs/LIVRAISON-1.20.1.md`), livrée le même soir.

Tes demandes, dans l'ordre :
1. « améliorer la partie conversation : voir le plan en haut en cours (avec déballage et avancement), avoir des balises de code que je peux copier dans les réponses » ;
2. « dans les incidents, si plusieurs incidents apparaissent sur la même alarme il faut faire un dossier, et aussi une recherche pour filtrer, trier par criticité si besoin, et la qualité des captures/vidéos est horrible » ;
3. « les enregistrements classiques doivent aussi rester fidèles même si l'appli a changé » ;
4. « dans les alarmes, avoir une case pour autoriser la création des incidents sur l'alarme concernée, faire pareil avec les groupes (ça cocherait toutes les alarmes dedans) et quelque chose pour arrêter totalement la génération des incidents ».

## 1. La conversation

### Le plan en cours, en haut

Une bande au-dessus de la discussion, qui **ne défile pas** avec elle, montre le plan de la proposition qui se vérifie ou s'applique sur un fil de fond ; sinon celui de la dernière proposition de la conversation.
- **Repliée** (au départ) : « Plan en cours », le plan (« Plan d'action — 80 étapes, 80 validées »), son état dans une pastille (Vérification…, Application…, Validation requise, Appliqué, Annulé (Ctrl+Z), Périmé, Non valide) et une **barre d'avancement** : le % du travail de fond, sinon les étapes vérifiées (« 80/80 vérifiées ») puis réussies (« 80/80 réussies »).
- **Un clic la déplie** : les phases (Analyse › Planification › Vérification › …) et **chaque étape avec son état** – En attente, Vérifiée, En cours, Réussie, Échec, Ignorée, Annulée, Retirée –, avec sa couleur ; la molette fait défiler les étapes ; elle prend au plus 45 % de la hauteur, la discussion garde sa place.
- **Un clic sur une étape** amène la discussion à la carte de la proposition, son plan déplié.
- **La croix** cache la bande pour ce plan ; la proposition suivante la remontre.
- Les états sont **réels** : pendant l'application de fond, seul l'avancement global est connu – les étapes disent « En cours », jamais une réussite qui n'a pas eu lieu ; à la fin, le compte rendu les remplace (Réussie, Échec…).

### Les blocs de code copiables

Un code entre balises dans une réponse (` ```st … ``` `, ` ```cpp `, ` ```json `…) est montré **à part** : sur son fond, avec sa langue (ST, C++, JSON, Python…) et un bouton **Copier** qui met **le code exact** dans le presse-papiers, sans les balises (la pastille dit « ✓ Copié » un instant, la barre d'état le nombre de lignes). Une longue ligne passe à la ligne à l'écran, mais Copier rend la ligne entière. Pendant que la réponse s'écrit, le bloc apparaît déjà (« en cours… ») ; seul le bloc JSON d'une proposition (les IA sans outils) reste caché dans sa carte, comme avant.

## 2. Les incidents

### La qualité des images et des vidéos

Ce qui la dégradait, et ce qui change :
- **JPEG qualité 75**, que le codeur fait en 4:2:0 (la couleur à demi-résolution : textes colorés et traits fins baveux) → **qualité 92 en 4:4:4** pour la vidéo, et **l'image du défaut (capture.jpg) codée à part en qualité 95** ;
- **une réduction par un facteur entier** (une vue de 1 300 pixels tombait à 650) → une réduction **à la juste taille** (moyenne pondérée), seulement au-delà de **1 920 pixels** de large (1 280 avant) ;
- **2 images par seconde** → **5 par défaut** (réglable de 1 à 10) ;
- **une cadence fausse** (les images, arrivées à intervalles irréguliers, étaient écrites à la suite : la vidéo allait trop vite ou par saccades) → chaque image est posée **à son heure réelle** : la vidéo dure ce qu'a duré la scène ;
- le tampon circulaire passe de 48 à 96 Mo (les images plus fines ne chassent pas les secondes d'avant) ; « avant » et « après » acceptent jusqu'à 120 s comme la fenêtre des réglages (60 au chargement avant).

### Un dossier par alarme, une recherche, le tri par criticité

Dans chaque onglet du volet Incidents (En attente, Validés) :
- **Les incidents d'une même alarme** (deux ou plus) se rangent sous **une ligne-dossier** : « Defaut_Pompe_2 — 3 incidents », sa criticité, le premier et le dernier, la miniature du plus récent. Fermée au départ : un clic, **Entrée** ou **→** la déplie (le plus récent d'abord), **←** la replie. Une alarme d'un seul incident reste une ligne simple.
- Choisir un dossier fermé, c'est choisir **son incident le plus récent** : les gestes (consulter, commenter, valider, supprimer) portent sur lui seul, jamais sur tout le dossier – la barre d'état et le menu le disent.
- **La recherche**, au-dessus de la liste : l'alarme, son message, l'équipement, la vue, la synthèse de l'analyse, les commentaires, la date affichée, le groupe, la catégorie, la criticité. Plusieurs mots : tous ; « entre guillemets » : la phrase ; -mot : l'exclut.
- **La criticité** : Toutes, Critique, Haute, Moyenne, Basse (la priorité de l'alarme).
- **Le tri** : plus récents d'abord, plus anciens, criticité (critique d'abord), alarme de A à Z, nombre d'incidents.
- Les badges des onglets comptent toujours tous les incidents ; la barre d'état dit « N incidents affichés sur M ».

### Arrêter les incidents

Le bouton **Arrêter les incidents** de la barre du volet coupe toute création de dossier, quelle que soit l'alarme : plus aucune image n'est demandée, le tampon des images est rendu, les vidéos en cours se terminent avec ce qu'elles ont. Les incidents déjà enregistrés restent. **Reprendre les incidents** la relance. C'est le réglage « surveillance active », gardé avec le projet (`reglages-incidents.json`). Au retour, la surveillance repart de zéro : une alarme disparue pendant l'arrêt puis revenue fait bien son incident.

## 3. Les enregistrements fidèles

Un enregistrement (`.xrec`) garde maintenant **une copie du projet de son moment** : les vues, les popups, les objets, les variables, les types, les scripts – compressée et chiffrée comme sa piste (avec son mot de passe s'il en a un).

- **La relecture se fait avec cette copie** : une vue renommée, un objet supprimé, une variable retirée depuis se rejouent tels qu'ils étaient ; la barre dit « rejoué avec le projet de son moment ».
- **En quittant la relecture**, l'IHM revient au projet d'aujourd'hui ; l'éditeur n'est jamais touché.
- Ressources › Enregistrements montre, pour chacun, « Projet gardé ».
- Ce qui n'est **pas** copié : les images et les sons (ils viennent du projet actuel ; les copier alourdirait chaque fichier).
- **Les enregistrements d'avant** (sans copie) se rejouent comme avant, avec le projet actuel ; une copie abîmée aussi, et c'est dit. Une copie de plus de 64 Mio (décompressée) n'est pas gardée, et c'est dit.
- Le format passe à 3 ; une version d'avant la 1.20.2 lit toujours la piste (elle ignore la copie).

## 4. Les alarmes et l'arrêt des incidents

### La case « Incidents » de chaque alarme

Dans **IHM › Alarmes**, chaque alarme a une case **Incidents** (une colonne du tableau, et sa fiche) :
- **cochée** (par défaut) : son apparition crée un dossier d'incident ;
- **décochée** : elle s'affiche, s'acquitte et passe à l'historique comme avant, mais **aucun incident** n'est créé pour elle (ni image, ni vidéo, rien d'écrit). Une alarme de routine (porte ouverte, maintenance) ne remplit plus le volet.
- **Plusieurs alarmes choisies** : un clic sur la case de l'une les change toutes, en **une seule modification** (Ctrl+Z la défait). Coller depuis Excel accepte oui / non / x.
- La barre d'état compte les alarmes « sans incident ».

### La case « Incidents » d'un groupe

Chaque groupe (partie **Groupes d'alarmes**) a sa case **Incidents** : la cocher ou la décocher le fait pour **toutes ses alarmes**, en une modification ; elle montre « – » quand ses alarmes sont mêlées. Les alarmes des **objets du synoptique et des symboles** suivent la case de leur groupe (celui de l'objet lié à un groupe de l'IHM, ou celui qu'elles déclarent) ; sans groupe connu, elles créent leurs incidents.

### Tout arrêter

En haut des groupes, la case **Surveillance des incidents** : décochée, plus aucun incident n'est créé, quelle que soit l'alarme. C'est le même réglage que **Arrêter les incidents** du volet Incidents, vu de deux endroits ; la barre d'état dit « surveillance des incidents arrêtée ».

### Les projets

La case ne s'écrit dans le projet que **décochée** (`incidents=0`) : un projet d'avant s'ouvre tout coché et s'enregistre à l'identique ; une version d'avant ignore la clé. L'Assistant IA sait créer une alarme sans incident (`incidents: false`).

## 5. Les essais

ESSAIS

## 6. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.20.2.exe` : l'installateur ;
- `API-Studio-1.20.2-portable.zip` et `IHM-Studio-1.20.2-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.20.2.exe` : il met à jour la version installée au même endroit et garde tes données.
2. Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ».

EMPREINTES

### Les vérifications sous Windows (Wine)

VERIFICATIONS

Ces vérifications sont faites sous Wine, pas sur un vrai Windows.

## 7. Les limites

LIMITES

## 8. Ce qui a été optimisé pour livrer plus tôt

- **Trois agents en parallèle** de mon travail, chacun dans ses fichiers (le volet Incidents ; les enregistrements ; les alarmes), vérifiés à la compilation sans toucher au dossier de construction ; relus, construits et essayés par moi avant d'être commités.
- **La 1.20.1 fabriquée et vérifiée sous Wine pendant que j'écrivais la 1.20.2.**
- Rien de recréé : le plan d'action et la vérification de fond de la 1.19/1.20, le presse-papiers et le bouton Copier de l'aide, le codeur JPEG et l'écrivain AVI, la recherche des listes du logiciel.
- Pas de captures ; l'installateur vérifié en silencieux sous Wine.
