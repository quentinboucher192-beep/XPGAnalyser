# API Studio et IHM Studio 1.21.2 : les notifications de l'Assistant, le modèle de fichier dans la conversation

Livrée le 01/11/2026, **dans le même installateur que la 1.21.3** (une grande demande n'est plus coupée : `docs/LIVRAISON-1.21.3.md`). Elle suit la 1.21.1 (`docs/LIVRAISON-1.21.1.md`), livrée le matin même.

Tes demandes :
1. « quand il crée un modèle de fichiers ça le mette dans le chat un peu comme dans des balises code » ;
2. « un onglet en haut notifications pour les choses importantes », précisé : « c'est des notifications que dans l'assistant IA que je parle ».

## 1. Le modèle de fichier dans la conversation

Quand tu enregistres un modèle de fichier (**Contexte de la discussion › Modèle… › Enregistrer comme modèle**), l'Assistant l'écrit dans la conversation :
- « **Modèle de fichier enregistré** : « Liste E/S A1 » — Liste d'entrées/sorties, feuille « E/S », en-tête ligne 3 », puis ses colonnes et où il est gardé (Mémoire et apprentissage › Apprentissage › Modèles de fichiers, partagé avec toutes les IA, emporté par le .xia) ;
- puis **le modèle lui-même dans un bloc de code JSON** (le nom, l'identifiant, le genre, la feuille, la ligne d'en-tête, chaque colonne avec son rôle, sa lettre et son intitulé, les règles, la signature des intitulés), avec le bouton **Copier** des blocs de code ;
- « **mis à jour** » quand un modèle du même nom et des mêmes intitulés existait déjà : il est mis à jour (ses utilisations et ses dates gardées). Avant la livraison, ce cas était refusé (« un modèle porte déjà ce nom ») : trouvé par les essais, corrigé.

Ce message vient du **logiciel**, pas de l'IA : il n'est jamais renvoyé à l'IA connectée comme une de ses réponses.

## 2. Les notifications de l'Assistant IA

Sous l'en-tête du panneau de l'Assistant, deux onglets : **Conversation** et **Notifications**.
- **La pastille** compte les notifications non lues : **rouge** s'il reste une erreur non lue, sinon la couleur d'accent ; rien à zéro. Au survol : le nombre et la plus récente.
- **Un clic** montre les notifications à la place de la conversation ; un autre clic, ou l'onglet Conversation, y revient (la saisie et une réponse en cours ne sont pas perdues).
- **La liste** : la plus récente en haut, rangée par jour (Aujourd'hui, Hier, la date) ; chaque ligne avec la couleur de son ton, l'heure, la catégorie, le titre (gras tant qu'elle n'est pas lue), deux lignes de détail, « ×3 » si elle s'est répétée, et **son bouton qui mène à la chose**.
- **Toutes / Non lues**, **Tout marquer comme lu**, **Effacer les lues** ; clic droit : Ouvrir, Marquer lue / non lue, Copier, Supprimer ; clavier : flèches, Entrée (le bouton), Espace (lue / non lue), Suppr, Ctrl+C. Une ligne choisie devient lue.
- **Gardées** dans le dossier IA (`IA/notifications.json`, 300 au plus, les plus anciennes lues partent d'abord), écrites sur un fil à part : l'interface n'attend jamais le disque.

Ce qui y arrive (seulement ce que fait l'Assistant) :

| Notification | Quand | Le bouton mène à |
|---|---|---|
| Réponse / proposition prête | sa conversation n'est pas à l'écran (une autre conversation, le panneau caché, l'onglet Notifications devant, la fenêtre sans le focus) | la conversation et le message, ou la carte de la proposition |
| Proposition appliquée / non appliquée | après l'application (de fond ou directe), avec ce qui a été créé ou laissé de côté | la carte |
| Erreur du service | clé refusée, limite atteinte, service indisponible ou injoignable (une ligne par fournisseur et par erreur, « ×N » si elle se répète) | le message en erreur |
| Pièce jointe illisible / classeur reconnu | l'analyse finie | le contexte de la discussion, ou la fenêtre Modèle… |
| Modèle de fichier enregistré | à l'enregistrement | la fenêtre Modèle… du classeur |
| Souvenirs à valider / conflit | quand l'IA en propose | Mémoire et apprentissage › Souvenirs, filtrés |
| Essai du bac à sable terminé | à la fin de l'essai | la réponse |
| Scan réseau terminé | un scan préparé par l'IA, une fois lancé et fini | Communication › Scanner IP |
| Analyse IA d'un incident | finie (ou restée en attente) | l'incident |

Ni le Compiler, ni la simulation, ni les plantages : seulement l'Assistant.

## 3. Les essais

La 1.21.2 a été essayée seule (114 suites sur 116 réussies : les deux échecs, le modèle réenregistré et la hauteur du volet dans un essai, sont corrigés), puis avec la 1.21.3 : voir `docs/LIVRAISON-1.21.3.md`, section 3.

## 4. Installer

La 1.21.2 n'a pas d'installateur à elle : elle est dans `API-IHM-Studio-Setup-1.21.3.exe` (et les portables 1.21.3), qui met à jour la 1.21.1 au même endroit et garde tes données.

Les empreintes et les vérifications sous Wine (**pas sur un vrai Windows**) : `docs/LIVRAISON-1.21.3.md`, section 4.

## 5. Les limites

- Les notifications ne se voient que **dans le panneau de l'Assistant** (comme demandé) : rien ne les signale ailleurs quand il est replié.
- La rangée d'onglets fait descendre le contenu du panneau de 30 pixels.
- Un **conflit de souvenirs** est reconnu à la réponse de l'outil de l'IA qui le signale ; un **scan** est attribué à l'IA s'il se termine dans les 30 minutes après qu'elle a ouvert la confirmation.
- Deux fenêtres d'IHM Studio ouvertes en même temps partagent le même journal : la dernière écriture l'emporte.
- Les notifications « prête » ne dépendent pas de l'option « M'avertir des réponses » (qui règle toujours la cloche et la barre des tâches).

## 6. Ce qui a été optimisé pour livrer plus tôt

- La 1.21.1 livrée d'abord (sa correction était prête), la 1.21.2 préparée en même temps.
- L'onglet Notifications confié à un agent dans sa propre copie du dépôt pendant que je faisais le modèle dans la conversation et la livraison de la 1.21.1 ; recadré dès ta précision (l'Assistant seul), avant qu'il ait touché à la barre de l'application.
- Les deux fichiers d'aide F1 compilés seuls, puis Linux et Windows en même temps ; l'installateur et la vérification sous Wine enchaînés.
- Pas de captures d'écran.
