# API Studio et IHM Studio 1.21.3 : une grande demande n'est plus coupée (avec la 1.21.2)

Livrée le 01/11/2026, **dans le même installateur que la 1.21.2** (les notifications de l'Assistant, le modèle de fichier dans la conversation : `docs/LIVRAISON-1.21.2.md`). Elle suit la 1.21.1 (`docs/LIVRAISON-1.21.1.md`).

Ta question : « comment palier à ça ? » La capture : l'Assistant décrit la vue **Accueil** (les deux armoires, leurs voyants, les mesures, les compteurs, les 12 défauts de chacune, le gaz en service, l'horloge, le compteur d'alarmes, « Acquitter tout », le bandeau d'alarme, 31 alarmes, la surveillance de la communication), puis : « La réponse a été coupée (la limite de longueur est atteinte) : aucune opération n'est proposée. »

## 1. Pourquoi c'était coupé

- Une réponse de l'IA a une **longueur maximale**, demandée par le logiciel à chaque envoi (en jetons, sa réflexion comprise). Jusqu'à la 1.21.2 : **16 000 jetons**, fixe.
- Ta demande fait **plusieurs centaines d'opérations** (chaque objet, chaque propriété liée, chaque alarme). La proposition s'écrivait encore quand la limite est tombée.
- Une proposition coupée **n'est jamais appliquée**, même en partie : son contenu est incomplet. C'est voulu : rien de bancal n'arrive dans ton projet. Mais tout était perdu.
- Et l'IA **ne savait pas** qu'elle avait une limite : elle n'avait aucune raison de découper.

## 2. Ce qui change

### Une réponse quatre fois plus longue
- **Auto** : **64 000 jetons** (environ 500 opérations), la moitié de la fenêtre de contexte au plus (un petit modèle local n'est pas étouffé).
- Réglable : **Paramètres de l'Assistant IA › Réponse**, sur la ligne de « Fenêtre de contexte » (vide : Auto).
- Si le modèle en accepte moins, le service le dit (« 400 : au plus 32 000 ») : **la demande repart aussitôt** avec ce qu'il accepte, sans rien te demander, et cette limite est retenue pour la séance. Anthropic comme les services compatibles OpenAI. **Rien n'est codé en dur par modèle** : c'est le service qui dit sa limite.

### L'IA connaît sa limite et découpe d'elle-même
- Chaque demande lui dit sa longueur et environ combien d'opérations elle peut écrire.
- Une demande plus grande se fait **en plusieurs propositions** : la première **complète et utilisable seule** (par exemple la vue et ses objets), et le reste dans un nouveau champ **« suite »** (par exemple « les 31 alarmes et la surveillance de la communication »).
- La carte de la proposition affiche **Suite prévue : …**

### Proposer la suite
- Une fois la proposition appliquée, la carte montre le bouton **Proposer la suite**. Il envoie « Propose la suite : … » : l'IA propose la partie suivante sur une nouvelle carte, que tu vérifies et appliques comme la première.
- La suite est gardée avec la conversation (rouverte demain, le bouton est toujours là).

### Coupée quand même : la reprise automatique
- Si une proposition dépasse malgré tout la longueur **pendant qu'elle s'écrit**, elle n'est plus perdue : l'Assistant **la redemande aussitôt en plusieurs parties**. L'état le dit : « La proposition dépassait la longueur d'une réponse (64 000 jetons) : redemandée en plusieurs parties… ». La réponse coupée est remplacée par la nouvelle.
- Une seule reprise : coupée de nouveau, la note le dit et propose quoi faire (une partie à la fois, ou augmenter **Réponse**).
- Une réponse en texte seulement (une longue explication) n'est pas redemandée : la note le dit.

### Ce que tu peux faire tout de suite
- Avec la 1.21.3 : **refais ta demande telle quelle**. L'IA la découpe et la carte te propose la suite.
- Tu peux aussi découper toi-même : « la vue Accueil avec les deux armoires », puis « le bandeau d'alarme et les compteurs », puis « les 31 alarmes et la surveillance du mot de vie ».
- L'**Effort Haut** fait réfléchir l'IA plus longtemps : sa réflexion compte dans la longueur.

### Corrigé dans la 1.21.2 avant sa livraison
Les essais ont trouvé un défaut dans la 1.21.2 : **réenregistrer** un modèle de fichier sous le même nom était refusé (« un modèle porte déjà ce nom ») au lieu de le mettre à jour. C'est corrigé : le même nom et les mêmes intitulés mettent le modèle à jour (ses utilisations et ses dates sont gardées), et la conversation dit « mis à jour ». La 1.21.2 n'a pas été livrée seule pour ne pas te donner ce défaut ; tout est dans cet installateur.

## 3. Les essais

**116 suites d'essais, toutes réussies** sous Linux (la 1.21.2 et la 1.21.3 ensemble). Pour cette livraison :

| Suite | Ce qu'elle vérifie | Résultat |
|---|---|---|
| ai_reseau_test (section « longueur d'une réponse », nouvelle) | la règle des refus : « 64000 > 32000, maximum allowed » (Anthropic), « at most 16384 completion tokens », « Expected a value <= 32768 », « entrée + longueur > contexte », un refus sans chiffre (la moitié), un autre refus (rien) ; le fournisseur Anthropic refusé à 64 000 qui repart aussitôt à 32 000 ; Auto (64 000), la moitié d'une fenêtre de 32 000, la longueur réglée et gardée ; la limite dite par le service retenue ; la consigne de longueur dans les consignes ; une proposition coupée redemandée en plusieurs parties (la consigne de reprise, la même demande, la réponse coupée remplacée) ; la suite gardée, la carte et son bouton Proposer la suite, le clic qui la demande ; coupée deux fois : dit, sans boucle ; un texte seul coupé : pas de reprise | 287 vérifications, 0 échec |
| ai_compatible_test | le fournisseur compatible OpenAI refusé (« at most 16384 ») qui repart aussitôt à 16 384 | 190 vérifications, 0 échec |
| ai_modeles_fichiers_test | (1.21.2) le modèle dans la conversation, et **réenregistré : « mis à jour »** (le défaut trouvé, corrigé) | 164 vérifications, 0 échec |
| ai_notifications_test | (1.21.2) l'onglet Notifications : le journal, la pastille, les filtres, le clavier, le fichier | 101 vérifications, 0 échec |
| ai_assistant_test | (1.21.2) la carte Annulée pendant qu'une réponse s'écrit, avec la rangée d'onglets | réussie |
| hmi_editor_test | les notes de version (567, 82 versions), l'aide F1 (269 sujets) | réussie |

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.21.3.exe` : l'installateur (1.21.2 + 1.21.3 ; il met à jour la 1.21.1 au même endroit et garde tes données) ;
- `API-Studio-1.21.3-portable.zip` et `IHM-Studio-1.21.3-portable.zip` ;
- ce document et `LIVRAISON-1.21.2.md`.

Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ». Le format des projets ne change pas (33).

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.21.3.exe` (44,2 Mo, 46 373 850 octets) :
`109F1C161B95F6EEC2CAE11901F3F47233D7CB7DFE44E906843A71BD154E6300`

Les zips portables (167 fichiers chacun) :
- `API-Studio-1.21.3-portable.zip` (34,3 Mo) :
  `3E42EEEC3756736C4A43450C590062FDC5F1B1F1424C3E2E8EA6BED99824E734`
- `IHM-Studio-1.21.3-portable.zip` (34,3 Mo) :
  `0C65A7CCB6BE5FD2505454B6005454527DFCB1D6E497A0A8EACF74A8996141A6`

### Les vérifications sous Windows (Wine)

Les deux exe Windows (MinGW-w64) répondent `API Studio 1.21.3` et `IHM Studio 1.21.3` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

L'installateur (Inno Setup 6.4.1) a été lancé en silencieux **par-dessus la 1.21.1 livrée**, avec une clé API d'essai : « Installation process succeeded » ; la clé est intacte (même empreinte avant et après, « Clés API : 1 gardée ») ; les exe et `pdfium.dll` installés sont ceux qui ont été essayés ; les associations `.xihm` et `.xapi` sont là ; la copie de secours de la 1.21.1 est faite.

Le bac à sable de l'exe installé (`IHM-Studio.exe --bac-a-sable`) : analyse, compilation et exécution réussies, le cas juste réussi (3,0), le cas faux en échec comme prévu (1,0 au lieu de 5,0).

Ces vérifications sont faites sous Wine, **pas sur un vrai Windows**.

## 5. Les limites

- **Pas d'essai avec une vraie IA** dans cette session (aucune clé ici) : la reprise, la suite et le repli de la longueur sont vérifiés avec un faux fournisseur et un faux réseau qui rejouent les réponses et les refus des services (formats Anthropic et compatible OpenAI). À toi le premier vrai essai avec ta demande « page d'accueil ».
- Le découpage est fait **par l'IA** : elle peut encore viser trop gros ; la reprise automatique n'a lieu qu'une fois.
- La reprise coûte une seconde demande : les jetons de la réponse coupée sont comptés (la jauge, le budget, le cumul du message).
- Une réponse de 64 000 jetons prend du temps (souvent 10 à 20 minutes au maximum) ; le texte arrive au fil de l'eau, le **Délai** (l'inactivité) n'est pas concerné.
- La limite qu'un modèle a dite n'est retenue que pour la séance : à la séance suivante, la première demande essuie un refus rapide (sans frais), puis repart.

## 6. Ce qui a été optimisé pour livrer plus tôt

- La cause lue directement dans le code (la longueur fixe à 16 000, l'IA qui ne la connaissait pas), corrigée à la source : la requête, les deux fournisseurs, les consignes, la reprise, la carte.
- La 1.21.3 écrite pendant que la 1.21.2 compilait ; le défaut de la 1.21.2 trouvé par ses essais, corrigé, et **les deux livrées dans un seul installateur** plutôt que deux tours de compilation, d'installateur et de Wine.
- Les deux fichiers d'aide F1 compilés seuls, puis Linux et Windows en même temps ; l'installateur et la vérification sous Wine enchaînés.
- Pas de captures d'écran.
