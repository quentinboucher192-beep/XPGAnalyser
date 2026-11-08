# API Studio et IHM Studio 1.38.0 : les notifications, les offres qui changent, Outlook et tes 20 PC

Faite à partir de ton retour du 08/11 sur la 1.37.0 (quatre captures et ta liste) :
- « là je peux pas copier le code » (la fenêtre de la licence activée) ;
- « dans le profil, une flèche cochée si le mail est valide » ;
- « dans l'admin, pouvoir supprimer les licences révoquées » ;
- « le menu à droite doit accepter les zooms, aussi dans Fonctionnalités, et quand le client choisit l'offre avant de demander une licence » ;
- « fais en sorte d'avoir Outlook, Hotmail » ;
- « dans les Studios, dans les menus principaux, un système de notifications générales, avec les notifications des licences » ;
- « dans les 20 PC, prendre en compte le PC qui fait l'activation ; il doit se voir dans Mes appareils » ;
- « si je modifie les fonctionnalités d'une offre, les clients reçoivent une notification avec l'avant / l'après ; si je fais des réductions, je peux choisir de les notifier ; hors connexion, ils les reçoivent après ; sur le logiciel, pas par mail » ;
- et tes réponses : Outlook par **la connexion Microsoft**, **tu redéploies** le service, la réduction avec **un nouveau prix et une date de fin**.

Elle se met à jour par-dessus la 1.37.0 et garde tes données. **Le service en ligne (le Worker) change** : redéploie-le (§ 1).

## 0. En bref

- **Copier le code** : la fenêtre « Ta licence en ligne est activée » a un bouton **Copier le code** (il devient « Code copié ✓ »).
- **Mon profil** : à côté du courriel, « ✓ adresse valide » quand il a la forme d'une adresse, « ✗ pas une adresse » sinon, et « ✓ vérifiée » dès qu'un mail de l'éditeur y est parti.
- **Les captures s'agrandissent** : fenêtre Licence › Éditions et **Fonctionnalités** (le même volet à droite, maintenant) : la **molette** zoome autour de la souris (jusqu'à × 4), **glisser** déplace, **double clic** ajuste, **un clic** l'ouvre **en grand** (Échap ferme).
- **La boutique** (Demander ma licence) : à droite, **l'offre choisie** — ses fonctionnalités, et la capture de celle qu'on clique (zoom, en grand). Rien de choisi : toutes les fonctionnalités des offres.
- **Les notifications** dans les Studios : sur l'**accueil** (« Notifications », avec le nombre non lu), dans **Aide › Notifications…**, et dans **la cloche** du projet. Ta licence (installée, renouvelée, passée dans une autre offre, révoquée, finie, **bientôt finie** 14 jours avant), tes offres et tes annonces y arrivent. **Jamais par mail.**
- **Une offre qui change** : quand tu changes les fonctionnalités d'une édition et signes le registre, chaque Studio le dit dans ses notifications : « Offre « PROFESSIONAL » modifiée (ta licence) », **ce qui est ajouté, ce qui est retiré, l'avant et l'après**. Une offre nouvelle ou retirée aussi. Rien à cocher.
- **Une réduction** : Éditions › « Réduction » : le **prix réduit** et **jusqu'au** (le dernier jour). La carte de la boutique **barre l'ancien prix**, montre le nouveau et « Réduction jusqu'au … » ; après cette date, elle reprend le prix normal d'elle-même. **« Prévenir mes clients »** (une case) : à la signature, une annonce part avec le registre ; chaque Studio la montre **une fois**. **Hors connexion** : le client la reçoit à sa prochaine connexion (elle voyage dans le registre signé).
- **Tes 20 PC** : le service dit maintenant **tous les PC de la licence**. **Mes appareils** les montre, **ce PC d'abord** (et il compte) : « 3 PC sur 20 utilisés — ce PC compris », quand chacun a été activé et vu, jusqu'à quand, une place gardée par un PC libéré. La liste est relevée au service **à l'ouverture de l'onglet** (toutes les 10 minutes au plus).
- **Outlook, Hotmail, Microsoft 365** : page Service en ligne › Le mail au client › Connexion « Compte Microsoft » (Outlook / Hotmail se remplit seul ; bouton **Microsoft 365** pour une boîte pro), l'**ID d'application** (à créer une fois, § 1), puis **Se connecter à Microsoft…** : l'outil montre **un code** à taper dans ton navigateur (microsoft.com/devicelogin) ; la connexion est gardée chiffrée sur ton PC (jamais ton mot de passe). Console : `mail-config --microsoft outlook …`, `mail-microsoft-connecter`.
- **Supprimer une licence révoquée** : Licences › une licence révoquée › **Supprimer…** : elle quitte la liste ; son fichier va dans la **corbeille** de l'outil (rien n'est effacé) ; **sa révocation reste** (les Studios la refusent toujours) ; la synchro de l'archive ne la ramène pas.

## 1. Ce qu'il te reste à faire

1. **Redéploie le service** : double-clique `deployer.bat` dans le dossier `service-licences` du zip de ton outil (comme d'habitude). Sans ça, Mes appareils ne voit que ce PC (« la liste des autres PC de la licence arrive du service des licences dès qu'il répond »).
2. **Installe la 1.38.0** sur ton PC et chez tes clients **avant** de mettre une réduction ou une annonce : un registre qui en porte est **refusé par les Studios 1.37** (ils gardent leur registre d'avant : rien ne casse, mais ils ne voient pas le nouveau).
3. **Outlook / Hotmail** (si tu veux ta boîte Microsoft plutôt que Gmail), une fois pour toutes, sur portal.azure.com (gratuit) :
   - Microsoft Entra ID › **Inscriptions d'applications** › Nouvelle inscription ; types de comptes : « Comptes dans un annuaire et comptes Microsoft personnels » ; pas d'URI de redirection ;
   - **Authentification** › « Autoriser les flux clients publics » : **Oui** ;
   - **Autorisations d'API** › Ajouter › Microsoft Graph › Autorisations déléguées : **SMTP.Send** et **offline_access** ;
   - copie son « **ID d'application (client)** » dans ton outil, Enregistrer le mail, **Se connecter à Microsoft…**, puis « Envoyer un mail d'essai ».

   Je ne l'ai pas fait : la connexion Microsoft, c'est toi qui la fais dans ton navigateur.

## 2. Les essais

- **Linux : 160 suites sur 160 réussies** (`ctest`, sur la version livrée). Celles de cette version :
  - le **service** (`npm test` dans `service/`, le vrai Worker sous `workerd`) : **90 essais** (6 de plus) — `postes` dans `/v1/renouveler` : ce PC d'abord et listé même s'il est le plus ancien, `utilises` = les places réellement prises (un 3ᵉ PC sur 2 est refusé), une place gardée par un PC libéré, une licence flottante (les postes pris), jamais la station ni le numéro d'un autre PC, jamais un PC révoqué ou retiré, une autre licence jamais montrée, 500 PC au plus ;
  - `licenceui` (102 contrôles, 27 de plus) : le **registre** avec une réduction et une annonce (écrit puis relu à l'identique ; refusés : un prix réduit sans fin, un identifiant d'annonce invalide, une annonce d'une offre absente) ; **les notifications** de l'avant à l'après (une offre modifiée — ajoutée, retirée, l'avant, l'après, « ta licence » en attention ; une offre nouvelle, une offre retirée ; l'annonce en cours, pas celle qui est finie ; une seule fois ; la licence renouvelée, révoquée, bientôt finie une fois ; le premier lancement : seulement l'annonce) et leur garde sur le disque ; **le carnet** des notifications générales (séparé de l'Assistant, la même clé remplace, lue, effacée, écrit puis relu) ; **les PC d'une licence en ligne** (le champ `postes` lu, « 2 sur 20 », une place gardée) ;
  - `adminworkspace` (77) : la réduction et l'annonce signées et relues de l'archive ; une annonce d'une offre inconnue refusée ; **supprimer une licence** : refusé si elle n'est pas révoquée ; révoquée, elle quitte la liste, va dans la corbeille, sa révocation reste, elle est retenue comme supprimée (une émission plus récente reviendrait), une seconde fois refusée ;
  - `adminmail` (306, 153 de plus) : **la connexion Microsoft** contre un faux serveur de Microsoft et un serveur de mail d'essai lancés sur ce poste (le code, l'attente, le jeton gardé chiffré, l'envoi XOAUTH2, le jeton renouvelé, chaque refus avec sa phrase : application inconnue, flux public désactivé, consentement manquant, connexion refusée ou expirée, « SMTP authentifié » désactivé (5.7.139), un autre compte, un serveur sans XOAUTH2) ; jamais un jeton, un code ou un mot de passe dans un message ; un `mail.json` de la 1.37 relu et réécrit à l'identique ;
  - `adminguidonnees` (226) : la page Éditions avec les champs de la réduction garde sa liste des fonctionnalités à 860 × 600 ;
  - `service_local` : le service répond « 1.38.0 » ;
  - `licensing` (321), `activation` (213), `service_studios` (146), `adminservice` (173), `adminguilicences` (277), `adminguishell` (223) : sans changement, toujours réussies.
- **Sous Wine, de bout en bout**, avec les programmes Windows et le vrai programme du service lancé sur ce poste (`workerd`) (journal : `essai-1.38.0-wine.txt`, le code masqué ; captures : `captures-1.38.0.zip`). Ton outil (console et application) tourne avec une clé d'essai ; le Studio est une copie avec la clé d'essai, jamais livrée. Une licence en ligne PROFESSIONAL de **20 PC** :
  - **PC A** s'active avec le code. **Mes appareils**, ouvert sans rien d'autre : « 1 PC sur 20 utilisé — ce PC compris », la liste relevée au service par l'onglet lui-même. Ses notifications : « Licence installée : PROFESSIONAL ».
  - **PC B**, le même code : « 2 PC sur 20 utilisés », lui d'abord.
  - **Ta console** change l'offre PROFESSIONAL (registre n° 319 : − Apprentissage, + Multi-utilisateur). Puis **ton application** y met une réduction (390 € HT / an jusqu'au 31/12/2026), coche « Prévenir mes clients » et signe : registre n° 320, déposé sur le service.
  - **PC A, plus tard** : le registre arrive. L'accueil dit « Notifications · du nouveau » ; la fenêtre des notifications : « Offre « PROFESSIONAL » modifiée (ta licence) » (ajoutée : Multi-utilisateur ; retirée : Apprentissage ; l'avant, l'après), l'annonce de la réduction, la licence installée. La boutique : 490 € barré, **390 € HT / an**, « Réduction jusqu'au 31/12/2026 » ; à droite, l'offre choisie. Fonctionnalités : la capture zoomée à la molette, puis ouverte en grand. Mes appareils : les deux PC.
  - **PC C** : une demande en ligne (PERSONAL) ; ta console l'accepte ; la fenêtre « Ta licence en ligne est activée », **Copier le code** → « Code copié ✓ ».
  - **Ton application** : la licence de C révoquée ; « Supprimer… » demande confirmation ; supprimée, elle quitte la liste, son fichier est dans la corbeille de l'outil et `licences-supprimees.json` la retient.
  - **La page Service en ligne** : « Compte Microsoft », Outlook / Hotmail rempli tout seul, un ID d'application (fictif), les étapes du portail. (Pas de connexion : Microsoft n'a pas été appelé.)
- **Le Studio livré** (ton service compilé) : l'accueil et la fenêtre Licence s'ouvrent (captures `1380_00a`, `1380_00b`). Il a été lancé avec une adresse de service locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'installation en silencieux par-dessus la 1.37.0** : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.38.0), le bac à sable tourne, le code laissé par `/CODEFICHIER=` n'est pas dans le journal.
- **Corrigé en chemin**, grâce aux captures :
  - la date de fin de la réduction était coupée sur la carte de la boutique : elle a sa ligne (« Réduction jusqu'au … »), la phrase de l'offre une ligne ;
  - l'annonce répétait la date de fin ; « Retirée (plus proposée) » était coupé dans ton outil ;
  - la capture en grand disait « clic : en grand » ; elle dit maintenant molette, glisser, double clic ;
  - la page Service en ligne : « Compte Microsoft » laissait le serveur vide (Outlook / Hotmail se remplit seul) ; l'ID d'application était coupé ;
  - Licences : « Choisis une licence… » passait sur le trait de la grille ;
  - **Mes appareils ne relevait les autres PC qu'au renouvellement du bail** (7 jours avant sa fin) : un PC ne voyait pas celui activé la veille. L'onglet relève maintenant la liste à son ouverture (l'essai du PC A le prouve : aucun renouvellement à la main).

## 3. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Ta clé, ton archive, ton service et tes réglages ne bougent pas. Puis **redéploie le service** (§ 1).

**Les Studios** : lance `API-IHM-Studio-Setup-1.38.0.exe` (dans le zip) par-dessus la 1.37.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.38.0.exe` | `A383AFC245435AD20EE69B7703FE9BF440CAA4C288B67F5478EE1266699EFD00` | 56 782 254 octets |
| `API-Studio-1.38.0-portable.zip` | `87EFCDFEF8B637AFE374E97AD728F8CD080E1386E866DDB9E04B5B62A2716077` | 214 fichiers |
| `IHM-Studio-1.38.0-portable.zip` | `9A895BCC666C017C119C083FFCE887E8DC9B578F6395F23E3E556EFA152AB3B3` | 215 fichiers |
| `XPG-License-Admin-1.38.0-EDITEUR.zip` (**à part, pour toi seul**) | `DF60AFF3E6E18547B5E5A7AB516E24F394DAEA76BC410E0A96A12D73E747D026` | 7 297 953 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne n'a pas été appelé.
- **Microsoft n'a pas été appelé** : la connexion (le code, le jeton, l'envoi XOAUTH2) a été essayée contre un faux serveur de Microsoft et un serveur de mail d'essai lancés sur ce poste, d'après la documentation de Microsoft. Le premier vrai essai, c'est ton « Se connecter à Microsoft… » puis « Envoyer un mail d'essai ». **Microsoft 365** : l'administrateur doit permettre « SMTP authentifié » pour ta boîte.
- **Un registre avec une réduction ou une annonce est refusé par les Studios 1.37** : installe la 1.38 partout avant d'en mettre (§ 1).
- **Mes appareils** relit la liste au service quand l'onglet est ouvert, au plus toutes les 10 minutes (sinon, au renouvellement du bail) : sans Internet, il montre la dernière liste reçue, avec sa date. Avec le service pas encore redéployé, il ne montre que ce PC.
- **Les notifications sont celles de chaque Studio** (API Studio et IHM Studio ont chacun leur carnet) et de chaque compte Windows.
- **Une annonce** est montrée une fois par Studio ; elle disparaît du registre quand sa réduction est finie (à la signature suivante). Il y en a 20 au plus dans le registre.
- **Sous Wine, les Studios d'un même compte partagent leurs réglages** : dans l'essai, les PC A, B et C ont chacun leur dossier des licences, mais un seul carnet de notifications (sur de vrais PC, chacun a le sien).
- **Supprimer une licence** n'est possible que pour une licence révoquée ; son fichier reste dans la corbeille de l'outil (`corbeille-licences` dans l'archive) : rien n'est effacé pour de bon.
