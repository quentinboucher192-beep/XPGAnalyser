# API Studio et IHM Studio 1.37.0 : la boutique, ton profil, le code livré au Studio et le mail

Faite à partir de ton retour du 08/11 sur la 1.36.0 (trois captures et ta liste) :
- « ça se chevauche » (l'assistant Nouvelle licence, étape 1) ;
- « si je sélectionne une ligne j'aimerais bien voir un exemple de ce que c'est, visuel » (l'onglet Éditions) ;
- « quand on demande une licence, choisir la licence souhaitée, et on verrait la boutique » ;
- « dans l'onglet des demandes, un rappel de ce que contiendra la future licence » ;
- « dans le mode admin, quand on accepte : voir la licence que le client voulait, accepter une seule fois, voir le code d'activation » ;
- « quand j'accepte, un mail part automatiquement ; s'il échoue, une notification sur le logiciel du client avec son code, qui lui demande de vérifier son adresse » ;
- « un profil : adresse mail, nom, société, facultatifs, qui remplit les demandes (à l'accueil, dans le logiciel, dans l'onglet licence) » ;
- et tes réponses : le mail par **ta boîte**, le code **au Studio et par mail** (et revu dans ton outil), la boutique **avec des prix, sans paiement**, de **vraies captures**.

Elle se met à jour par-dessus la 1.36.0 et garde tes données. **Le service en ligne (le Worker) ne change pas** : rien à redéployer.

## 0. En bref

- **Le chevauchement est corrigé** : dans l'assistant, la phrase « N demande(s) valide(s) — ou glisse… » est sur la ligne du bouton Ajouter, sous les deux choix.
- **La boutique** : dans le Studio, Licence… › Demander ma licence montre tes éditions **en cartes** : leur nom, **leur prix**, leur phrase, ce qu'elles donnent (PC, durée, nombre de fonctions). Un clic choisit la sienne (un second clic la rend) ; elle part avec la demande. Aucun paiement en ligne : tu contactes le client.
- **Le prix et la phrase** se règlent dans ton outil : Éditions › une édition › « Prix (boutique) » et « Description ». Signe le registre ensuite.
- **Le rappel pendant l'attente** : l'onglet de la demande dit « Licence demandée : PROFESSIONAL · 490 € HT / an » et ce que contiendra la licence une fois acceptée (PC, durée, les fonctions par leur nom), « l'éditeur peut l'ajuster ».
- **Mon profil** (tout facultatif) : courriel, nom, société. Sur l'**accueil** (« Mon profil… » dans l'encart de la licence), dans **Aide › Mon profil…**, et dans l'onglet **Mon profil** de la fenêtre Licence. Il remplit la demande ; le courriel reçoit ton mail.
- **L'aperçu des fonctionnalités** : fenêtre Licence › Éditions, choisis une ligne : à droite, **une vraie capture** et deux lignes sur ce qu'elle fait, et dans quelles éditions elle est. 21 captures, faites sur un projet de démonstration fictif, livrées avec le logiciel.
- **Dans ton outil, Demandes en ligne** : une colonne **Édition voulue** (et son prix), le courriel du client dans Message. **Accepter** ouvre l'assistant avec cette édition **déjà choisie** (« Le client a demandé : … ») ; le récapitulatif la rappelle.
- **Une seule acceptation** : dès que la licence est signée, la demande est « Acceptée ici » ; Accepter, Accepter en ligne et Refuser ne marchent plus pour elle. Si sa réponse n'est pas partie (service injoignable), **Renvoyer la réponse** la renvoie (la même licence, le même code).
- **Le code d'activation** : il est **gardé chiffré** sur ton PC (pour ton compte Windows). Licences › **Voir le code…** le remontre quand tu veux (console : `code-licence --licence ID`).
- **Accepter en ligne** : le code part **scellé pour le PC du client** (le service ne le lit pas) : son Studio **s'active tout seul** et lui montre la fenêtre « Ta licence en ligne est activée ».
- **Le mail** : à l'acceptation, un mail part tout seul **depuis ta boîte** (la licence, son édition, ce qu'elle ouvre, jusqu'à quand, et le code d'une licence en ligne). Il se règle une fois : page **Service en ligne › Le mail au client** (bouton Gmail, ton adresse, ton **mot de passe d'application**, « Envoyer un mail d'essai »).
- **Le mail n'est pas parti** (adresse fausse, boîte injoignable) : ton outil le dit ; le Studio du client reçoit **un avis**, et la fenêtre de confirmation lui montre **son code** et lui demande de **vérifier son adresse** (son bouton ouvre Mon profil). Sans mail réglé : la fenêtre montre aussi le code.
- **Corrigé** : une demande acceptée « en ligne » par l'outil 1.36 restait « Envoyée — en attente » chez le client (ta 3ᵉ capture) : elle est maintenant acceptée, et la phrase dit d'entrer le code.

## 1. Ce qu'il te reste à faire

1. **Installe la 1.37.0** sur ton PC et chez tes clients **avant** de mettre des prix : un registre avec un prix ou une description est **refusé par les Studios 1.36** (leur lecture est stricte).
2. **Règle le mail** (Service en ligne › Le mail au client) : avec Gmail, active la validation en deux étapes de ton compte Google, crée un « mot de passe d'application » (myaccount.google.com › Sécurité › Mots de passe des applications) et colle-le. Puis « Envoyer un mail d'essai ». Je ne l'ai pas fait : le mot de passe, c'est toi qui le tapes.
3. **Donne tes prix** (Éditions › Prix, Description), puis **Signer le registre** : il part sur le service et dans les Studios.

## 2. Les essais

- **Linux : 160 suites sur 160 réussies** (`ctest`, sur la version livrée ; une de plus qu'en 1.36 : `adminmail`). Celles de cette version :
  - `service_studios` (146 contrôles, 13 de plus) : le Studio contre le vrai service. La demande garde l'**édition choisie** et le **courriel** (scellés : le service ne les lit pas) ; un courriel sans forme d'adresse ou une édition illisible : rien ne part. **Acceptée en ligne** : le code arrive scellé pour ce PC, le Studio s'active seul (« Valide », en ligne), la fenêtre dit « Ta licence en ligne est activée », montre le code quand le mail n'est pas parti et demande de vérifier l'adresse (bouton vers Mon profil) ; un avis, **sans le code** ; le code n'est **pas dans l'historique**. Vérifiée de nouveau : rien de neuf, pas de seconde activation. **Acceptée sans code** (ton outil 1.36) : la demande n'est plus en attente, la phrase dit d'entrer le code ;
  - `adminservice` (173) : la réponse en ligne porte le code scellé, l'identifiant de la licence et l'état du mail (le mailer est appelé avant la publication) ; **une seconde réponse à la même demande est refusée** ; « acceptée ici » : ni refus ni seconde licence, la réponse se renvoie ; le code gardé et relu (et `code-licence` le remontre) ; le texte du mail (la licence, l'édition, le code et comment l'activer, ta signature) ;
  - `adminmail` (153) : le mail contre un serveur SMTP d'essai lancé sur ce poste : STARTTLS (TLS 1.3, AUTH PLAIN) et TLS d'emblée (TLS 1.2, AUTH LOGIN), ce que le serveur reçoit (en-têtes, accents, corps), et chaque refus avec sa phrase : certificat inconnu, nom qui ne correspond pas, mauvais mot de passe, Microsoft (5.7.139), destinataire refusé, pas de STARTTLS (rien n'est parti en clair), serveur muet, port fermé. Le mot de passe n'apparaît dans aucun message ;
  - `adminguidemandes` (116) : la colonne Édition voulue, le code gardé sur ton poste et dans aucun autre fichier, l'état scellé avec le code ;
  - `licenceui` (75) : Mon profil (vide, enregistré, relu, un courriel faux refusé, tout vide permis) ; l'aperçu d'une fonctionnalité (sa capture, sa phrase ; un identifiant qui serait un chemin : rien) ;
  - `licensing` (321), `activation` (213), `adminguilicences` (277), `adminguishell` (223) : sans changement.
- **Sous Wine, avec les programmes Windows, le vrai programme du service lancé sur ce poste (`workerd`) et un serveur de mail d'essai sur ce poste** (journal : `essai-demande-wine.txt` ; captures : `captures-1.37.0.zip`). Ton outil (console et application) tourne avec une clé d'essai ; le Studio est une copie avec la clé d'essai, jamais livrée.
  - **PC A** : Mon profil rempli ; la **boutique** (PERSONAL 190 € HT / an, PROFESSIONAL 490 €, ENTERPRISE 1 290 €, chacune sa phrase) ; l'onglet Éditions avec la **capture** du Débogueur et sa phrase ; la demande part avec PROFESSIONAL et le courriel ; l'onglet rappelle « Licence demandée : PROFESSIONAL · 490 € HT / an » et ce qu'elle contiendra (5 PC, 1 an, les 18 fonctions par leur nom).
  - **Ton application** relève la demande toute seule (« Édition voulue : PROFESSIONAL · 490 € HT… ») ; la page Service en ligne montre le mail réglé ; « Envoyer un mail d'essai » part.
  - **La console l'accepte en ligne** (`licence-en-ligne --demande 1`) : « Mail : le courriel est parti », la réponse publiée avec le code scellé. Une **seconde acceptation est refusée** (« n'attend pas de décision (Acceptée) »). `code-licence` remontre le code. Le serveur d'essai a reçu le mail : la licence, le code, « Activer avec un code ».
  - **Le Studio, resté ouvert, s'active tout seul** (« Valide / PROFESSIONAL, activée en ligne ») : la fenêtre « Ta licence en ligne est activée » (la licence, le client, jusqu'au 08/11/2027, ce qu'elle ouvre, « l'éditeur t'a aussi envoyé un mail (à …) avec ton code d'activation »), puis l'accueil vert avec « Mon profil… ».
  - **PC B** : PERSONAL et un courriel que le serveur refuse ; acceptée en ligne : « Mail : le serveur refuse le destinataire… ». Le Studio **s'active quand même**, la fenêtre montre **son code** et « vérifie ton adresse dans ton profil » ; « Vérifier mon adresse… » ouvre Mon profil.
  - **Le Studio livré** (ton service compilé) : l'accueil en Découverte, la fenêtre Licence sur Demander ma licence. Sous Wine, il n'a pas pu joindre ton service (le réseau de mon poste le refuse).
  - **L'installation en silencieux par-dessus la 1.36.0** : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.37.0), les 21 captures des fonctionnalités sont installées, le code laissé par `/CODEFICHIER=` n'est pas dans le journal.
- **Corrigé en chemin**, grâce aux captures :
  - avec le 9ᵉ onglet (Mon profil), les onglets de la fenêtre Licence n'affichaient plus que des icônes : la fenêtre est plus large (1320 px au plus), leurs noms tiennent ;
  - le bouton « Mon profil… » de l'accueil était coupé ; la phrase des cartes de la boutique aussi : deux lignes ;
  - dans ton outil, Édition voulue et État étaient hors de vue : ils sont juste après le client ;
  - les lignes du bas de l'onglet Demander ma licence ne peuvent plus passer sous les boutons de la fenêtre.

## 3. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Ta clé, ton archive, ton service et tes réglages ne bougent pas.

**Les Studios** : lance `API-IHM-Studio-Setup-1.37.0.exe` (dans le zip) par-dessus la 1.36.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.37.0.exe` | `93EE97AFC14DB80CA66754005EE8D4973F9639BDB6839325CEDC7B978439017A` | 56 698 397 octets |
| `API-Studio-1.37.0-portable.zip` | `90253249B4BDEEA8CBCBA4D9D1B02C4477802B65DA4FD51D0C10F04BBE2D1A6F` | 214 fichiers |
| `IHM-Studio-1.37.0-portable.zip` | `29727F2F4E613D4B3EB5E2AB0D1686097EC61ABB59C18467D2D525A4D7CB4BC7` | 215 fichiers |
| `XPG-License-Admin-1.37.0-EDITEUR.zip` (**à part, pour toi seul**) | `B38274B8873D6F22B4B0225CE1801A30DA86AAB94AB501E1E922785E2933AF9E` | 7 170 611 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`) et un serveur de mail d'essai sur ce poste. Ton service en ligne et ta boîte mail n'ont pas été appelés ; **Gmail n'a pas été essayé** (le premier vrai mail, c'est ton « Envoyer un mail d'essai »).
- **L'acceptation en ligne avec le mail** a été essayée par la **console** (`licence-en-ligne --demande`) ; l'application fait le même appel (le même code), mais son assistant n'a pas été conduit jusqu'au mail sous Wine.
- **Le mail : Gmail (ou une boîte qui accepte un mot de passe d'application)**. Outlook / Hotmail / Microsoft 365 ne marchent pas : Microsoft exige une autre connexion (OAuth), que l'outil ne fait pas ; il le dit clairement. Un compte Google Workspace d'entreprise peut avoir les mots de passe d'application désactivés par son administrateur.
- **Le mot de passe du mail est gardé pour ton compte Windows sur ce PC** : sur un autre PC ou un autre compte, tape-le de nouveau.
- **Un PC tout neuf** peut ne pas encore avoir le certificat racine de Gmail : l'outil demande alors à Windows de le compléter (ce chemin n'a pas pu être essayé ici).
- **Les adresses avec accents** (é@…) sont refusées.
- **Le code dans la fenêtre** : il n'est montré qu'une fois, dans la fenêtre de confirmation (jamais écrit sur le PC du client). Fermée sans le noter : le client te le redemande, tu le revois dans ton outil (Voir le code…).
- **Une licence en ligne créée avant la 1.37.0** : son code n'est pas gardé dans ton outil (Voir le code… le dit) : « Nouveau code… » en tire un neuf.
- **Une capture n'existe pas encore pour une fonctionnalité que tu ajouterais au registre** : l'onglet Éditions dit « Pas encore de capture ». La « Compilation » montre API › Statistiques (API Studio n'a pas d'écran de compilation à part) ; « Licences flottantes » montre la fenêtre Licence. Les captures ont été faites avec les programmes 1.36.0 (l'écran n'a pas changé).
- **Mon profil est celui du compte Windows** : deux personnes sur le même compte partagent le même profil.
- **La fenêtre Licence fait 1320 px de large au plus** : sur un écran de moins de 1360 px de large, les onglets repassent en icônes (le nom de l'onglet ouvert reste écrit).
- **La boutique n'encaisse rien** : le prix est un texte que tu écris ; le client choisit, tu le contactes.
