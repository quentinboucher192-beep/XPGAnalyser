# API Studio et IHM Studio 1.36.0 : la licence par une demande en ligne

Faite à partir de ton retour du 07/11 : « sur IHM je veux activer mes licences mais pas avec des fichiers ou quoi, ça envoie une demande aux services et elle reste en attente et visualisable par le client dans son onglet de demande, et l'admin lui doit accepter sur le admin.exe ; quand il accepte, le client est notifié et il voit sa nouvelle licence, il a un modal qui lui confirme l'activation, le contenu, le délai ».

Avec tes fichiers `ServiceAddress.hpp` (l'adresse de ton service) et `PublisherKey.hpp` (ta clé, exportée de nouveau : la même). Elle se met à jour par-dessus la 1.35.1 et garde tes données.

## 0. En bref

- **Le client demande sa licence, tu l'acceptes, elle s'installe.** Sans licence, l'accueil, la fenêtre « Licence nécessaire » et la fenêtre Licence mènent tous à **Demander ma licence…** : sa société (le reste est facultatif), et c'est parti. Aucun fichier, aucun code à taper.
- **La demande reste visible** tant qu'elle attend : son numéro, sa date, son état (« en attente de l'éditeur », puis « reçue par l'éditeur »), sur l'accueil (« Demande envoyée : en attente de l'éditeur »), dans « Licence nécessaire » et dans l'onglet **Demander ma licence** (avec Annuler la demande et Vérifier maintenant).
- **Le Studio regarde tout seul** si tu as répondu : toutes les minutes pendant deux heures, puis toutes les 5 minutes ; fermé, à son prochain lancement.
- **Dans ton outil** (`XPG-License-Manager-Admin.exe`), les demandes sont **relevées toutes seules** (à l'ouverture, puis toutes les 2 minutes) : une nouvelle demande donne un avis (« Nouvelle demande de licence : le client (PC « … ») — Demandes en ligne › Accepter ») et leur nombre s'affiche sur **Demandes en ligne**. Tu acceptes comme avant : **Accepter (licence liée)…** (l'édition, la durée, le nombre de PC), ou **Refuser…** (avec la raison).
- **Le client est prévenu par une fenêtre** dès que sa licence est là : « Ta licence est activée », avec la licence et son édition, le client, **jusqu'à quand (et les jours qui restent)**, le nombre de PC, **ce qu'elle ouvre** (par logiciel) et ton mot ; **Voir ma licence**. Refusée : la fenêtre donne ta raison, et **Nouvelle demande…**.
- **Les fichiers restent pour un PC sans Internet**, en second : « Demande par fichier (sans Internet)… » et « Fichier de licence… » en bas de la fenêtre Licence. Le code aussi (onglet Activation, page Licence de l'installateur).
- **Une copie d'essai n'appelle jamais ton vrai service** : seuls les Studios livrés ont son adresse (une construction avec la clé d'essai l'ignore).

## 1. Ce qu'il te reste à faire

1. **Signe ton registre** (Éditions › Signer le registre) : il part sur le service (« Registre déposé : aucun » sur ta page Service en ligne). Ce n'est pas bloquant.
2. **Fais-toi une licence pour ton propre PC** : depuis ton Studio 1.36.0, Demander ma licence, puis accepte-la dans ton outil. C'est aussi le meilleur essai du parcours, sur un vrai Windows.

## 2. Les essais

À COMPLÉTER

## 3. Installer

À COMPLÉTER

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne, lui, n'a pas été appelé : le réseau de mon poste ne le joint pas.
- **Les Studios livrés ont essayé de joindre ton service sous Wine, et n'ont pas pu** (le réseau de mon poste le refuse) : ils ont fait comme hors ligne, comme prévu. Rien n'a été créé chez toi.
- **La fenêtre de confirmation s'affiche dans le Studio qui a reçu la licence** : si API Studio et IHM Studio sont ouverts tous les deux, l'autre voit la licence (l'accueil, la pastille) sans fenêtre.
- **Ton outil relève les demandes tant qu'il est ouvert** : fermé, rien ne te prévient (pas de courriel ni de notification Windows).
