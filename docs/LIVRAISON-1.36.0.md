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

- **Linux : 159 suites sur 159 réussies** (`ctest`, sur la version livrée). Celles de ce parcours :
  - `service_studios` (133 contrôles) : les Studios contre le vrai service. Demande envoyée ; elle dit quand elle est partie ; vérifiée toutes les minutes pendant deux heures, puis toutes les 5 minutes. **Acceptée** : la licence s'installe, la réponse remonte avec ton mot, et la fenêtre dit la licence, l'édition, le client, la date de fin et les jours, ce qu'elle ouvre. Vérifiée de nouveau : rien de neuf, pas de seconde fenêtre. **Refusée** : la raison remonte, la licence déjà là reste, la fenêtre propose une nouvelle demande ;
  - `licenceui` (65) : l'encart de l'accueil sans service (le code, comme avant) et avec un service (« Demander ma licence… ») ; une demande en attente (« Demande envoyée : en attente de l'éditeur », « Voir ma demande ») ; aucun projet en attendant ; les réponses montrées une à une, dans l'ordre ;
  - `licensing` (321) : ta clé compilée est exactement celle qu'écrit ton outil ; `adminguidemandes` (114), `adminguishell` (223), `adminservice`, `activation` (213), `evaluation` : sans changement.
- `adminguidemandes` a échoué une fois dans une série complète : 4 contrôles qui relisent l'état d'un PC sur le service local, pendant que deux essais tournaient en même temps. Il a réussi à chaque autre passage (seul, avec les essais des licences, et dans la série complète finale). C'est la même instabilité qu'en 1.35.0 ; la cause n'est pas trouvée.
- **Sous Wine, avec les programmes Windows et le vrai programme du service lancé sur ce poste** (`workerd` ; journal joint : `essai-demande-wine.txt` ; captures : `captures-1.36.0.zip`). Ton outil (sa console et son application) tourne avec une clé d'essai. Le Studio est une copie compilée avec la clé d'essai, jamais livrée, avec un registre sans Découverte : un PC neuf est donc sans licence.
  - **PC A** :
    - sans licence : l'encart rouge « Demander ma licence… » ; la fenêtre « Licence nécessaire » sur Nouveau projet ; l'onglet **Demander ma licence** ouvert d'office ;
    - la demande part (n° 1) : l'accueil dit « Demande envoyée : en attente de l'éditeur », l'onglet montre la demande (numéro, date, état, Annuler, Vérifier maintenant), et « Licence nécessaire » dit qu'elle attend ;
    - **ton application s'ouvre** et relève la demande toute seule : « 1 » sur Demandes en ligne, et l'avis « Nouvelle demande de licence : Client d'essai Demande (PC « Poste essai A ») » ;
    - la console l'accepte (licence PROFESSIONAL liée au PC, un an, publiée) ;
    - **le Studio, resté ouvert, l'a reçue toute seule** à sa vérification suivante (moins de deux minutes) : « Valide / PROFESSIONAL », la fenêtre « Ta licence est activée » (la licence, le client, jusqu'au 07/11/2027 (365 jours), 1 PC, les 18 fonctions par logiciel, ton mot), puis l'encart vert « Licence : PROFESSIONAL · jusqu'au 07/11/2027 ».
  - **PC B** : la demande part (n° 2) ; la console la refuse avec une raison ; le Studio, resté ouvert, montre la fenêtre « Ta demande de licence est refusée » avec ta raison, et l'encart propose de nouveau « Demander ma licence… ».
  - **Le Studio livré** (ton service compilé) : l'accueil en Découverte avec « Demander ma licence… », et la fenêtre Licence ouverte sur le formulaire. Sous Wine, il n'a pas pu joindre ton service (le réseau de mon poste le refuse).
  - **L'installation en silencieux par-dessus la 1.35.1** : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.36.0), le code laissé par `/CODEFICHIER=` n'est pas dans le journal.
- **Corrigé en chemin**, grâce à cet essai :
  - une copie d'essai croit deux clés (la tienne et celle d'essai) et scellait la demande pour la tienne : le service local l'avait refusée. La demande est maintenant scellée pour **la clé qui a certifié le service**, ce qui vaut aussi chez toi ;
  - la fenêtre de confirmation s'affichait comme un éditeur, avec des numéros de ligne : c'est maintenant un paragraphe ;
  - derrière elle, l'encart de l'accueil gardait son ancien titre : il est maintenant remis en page tout de suite ;
  - la phrase de la Découverte était coupée sur l'accueil : elle est raccourcie.

## 3. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Ta clé, ton archive, ton service et tes réglages ne bougent pas.

**Les Studios** : lance `API-IHM-Studio-Setup-1.36.0.exe` (dans le zip) par-dessus la 1.35.1. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.36.0.exe` | `397D78DFE29B3AC886FE6A6B8D4D3205FA6F622F56E90FB55E8B9F43DD344264` | 55 233 826 octets |
| `API-Studio-1.36.0-portable.zip` | `8E4636D4D2D3E85AEFED9291E1B6539C715F06593E26A5673D9122C64EFFFCD1` | 191 fichiers |
| `IHM-Studio-1.36.0-portable.zip` | `E8DEFD2CC0226FADD7F6DEB17060E0497549E6984862E20A75394118A0D21FB2` | 192 fichiers |
| `XPG-License-Admin-1.36.0-EDITEUR.zip` (**à part, pour toi seul**) | `137D0F3BC999F05A60F331FE8EF3FBF2F65F7900C4C84ADE2C33706D96E323A2` | 6 093 572 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne, lui, n'a pas été appelé : le réseau de mon poste ne le joint pas.
- **Les Studios livrés ont essayé de joindre ton service sous Wine, et n'ont pas pu** (le réseau de mon poste le refuse) : ils ont fait comme hors ligne, comme prévu. Rien n'a été créé chez toi.
- **La fenêtre se ferme à la souris** : Entrée n'appuie pas sur son bouton (comme les autres fenêtres du logiciel).
- **La fenêtre de confirmation s'affiche dans le Studio qui a reçu la licence** : si API Studio et IHM Studio sont ouverts tous les deux, l'autre voit la licence (l'accueil, la pastille) sans fenêtre.
- **Ton outil relève les demandes tant qu'il est ouvert** : fermé, rien ne te prévient (pas de courriel ni de notification Windows).
