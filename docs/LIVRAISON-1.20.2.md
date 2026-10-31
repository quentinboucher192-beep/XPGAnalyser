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

INCIDENTS-VOLET

## 3. Les enregistrements fidèles

ENREGISTREMENTS

## 4. Les alarmes et l'arrêt des incidents

ALARMES

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
