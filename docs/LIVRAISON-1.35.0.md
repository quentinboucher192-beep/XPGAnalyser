# API Studio et IHM Studio 1.35.0 : ton service des licences sur Cloudflare, et on sort de GitHub

Faite à partir de tes demandes du 07/11 :
- « utilise Cloudflare workers, code toute la gestion des licences, l'admin, le license manager côté client, les fonctionnalités bloqués si pas de licence, la licence decouverte par défaut, la licence active visible sur le menu principal, la possibilité de prendre une licence directement dans l'installateur, on sort totalement de github maintenant » ;
- « il faut gérer les desactivations meme si un pc reste hors ligne à vie, injecté un timer dans l'activation sur le logiciel, et si il recupere internet il reverifie que la synchronisation de la durée est bonne sinon il ajuste ».

Tes choix : activation **automatique** par code ; la **Découverte** est l'essai de 30 jours ; ton archive passe aussi sur Cloudflare ; tu mets le service en ligne **toi-même**, guidé.

Elle se met à jour par-dessus la 1.34.0 et garde tes données.

## 0. En bref

- **Ton service des licences** : un petit programme (un « Worker ») sur **ton** compte Cloudflare, gratuit. Il remplace GitHub pour tout : les activations, les licences flottantes, l'heure, la Découverte, les demandes des clients, la synchro de ton archive. Plus rien ne passe par GitHub.
- **Activer avec un code.** Tu crées une licence « en ligne » dans ton outil : il te montre **une fois** un code `XPG-XXXX-XXXX-XXXX-XXXX` pour ton client. Le client le tape dans le Studio, ou dès l'installation : son PC s'active seul, sans aucun fichier. Le service ne garde que l'empreinte du code.
- **Le bail (ta question des PC hors ligne).** Une activation dure **30 jours** (réglable par licence) et se renouvelle seule dès que le PC est en ligne. Un PC que tu retires, ou qui ne revient jamais en ligne, **s'arrête à la fin de son bail**. Le Studio a un compteur à lui : reculer l'horloge ne prolonge rien ; en ligne, l'heure signée du service le recale (§ 4).
- **Les licences flottantes** : N postes à la fois. Le Studio prend un poste au démarrage et le rend en se fermant ; s'il n'y en a plus, il dit dans combien de temps réessayer.
- **La Découverte** : sans licence, 30 jours d'essai avec toutes les fonctions, puis les fonctions de l'édition sans licence (les autres sont verrouillées ; le projet reste ouvert). Le service la retient par machine.
- **La licence active toujours en vue** : en haut à droite de chaque Studio, dans la barre d'état et sur l'accueil (« Licence : PROFESSIONAL · jusqu'au … », « Découverte · 23 j », « Bail : 5 j — connecte-toi »…). Un clic ouvre la fenêtre Licence.
- **Dans l'installateur** : une page « Ta licence » : la Découverte (par défaut) ou « J'ai un code de licence ».
- **Ton outil** : une page **Service en ligne** (relier, vérifier, certifier, la synchro de l'archive), les **licences en ligne** dans l'assistant de licence, une vraie page **Licences flottantes**, les **Demandes en ligne** par le service. La console a les mêmes gestes.
- **Une relecture de sécurité** a été faite après coup : un problème grave, deux importants, trois moyens et cinq mineurs. Ils sont corrigés (§ 5).
- **Ce qu'il te reste à faire** (§ 1) : mettre le service en ligne (une demi-heure, guidé), le relier à ton outil, puis **me donner trois fichiers publics** pour que les Studios livrés s'en servent.
- **Rien n'a été essayé sur un vrai Windows ni chez Cloudflare** : sous Linux, et sous Wine contre le vrai programme du service lancé sur ce poste (§ 5).

## 1. Ce qu'il te reste à faire (une fois)

Le pas à pas complet : `service-licences\LISEZ-MOI-SERVICE.txt`, dans le zip de ton outil (**à part, pour toi seul**). En court :

1. **Installe Node.js** (version LTS, 22 ou plus) : nodejs.org.
2. **Crée ton compte Cloudflare** (gratuit) : dash.cloudflare.com/sign-up. Dans « Workers et Pages », choisis ton sous-domaine. **Active la double authentification** : qui entre dans ce compte peut lire la clé du service.
3. **Copie le dossier `service-licences`** du zip sur ton PC. Dans ton outil, **Service en ligne › Écrire cle-publique.txt…**, dans ce dossier.
4. **Dans un terminal ouvert dans `service-licences\service`** : `npx wrangler login` (une fois par PC).
5. **Double-clique `deployer.bat`** : il met le service en ligne et écrit son adresse, `https://xpg-licences.<ton-compte>.workers.dev` (gardée aussi dans `adresse-du-service.txt`).
6. **Dans ton outil, Service en ligne** : colle l'adresse, **Enregistrer l'adresse**, **Vérifier**, puis **Certifier la clé du service** (l'outil te montre son empreinte ; un certificat vaut un an, l'outil te rappelle de le renouveler).
7. **Signe ton registre** (Éditions : il part sur le service), **synchronise ton archive**, puis crée tes **licences en ligne** (Licences › Nouvelle licence › « En ligne : code d'activation »).
8. **Paramètres de sécurité › Préparer les fichiers pour les Studios…** écrit trois fichiers **publics, sans aucun secret** :
   - `PublisherKey.hpp`, ta clé publique ;
   - `registre.xpgreg`, ton registre signé ;
   - `ServiceAddress.hpp`, l'adresse de ton service.

   **Envoie-les-moi**, ou commite-les aux chemins indiqués dans le `LISEZ-MOI.txt` du dossier. Je les compile dans la version suivante des Studios.

> **Tant que l'étape 8 n'est pas faite, les Studios livrés n'ont ni ta clé ni l'adresse de ton service** : leurs licences sont « non activées » (tout est ouvert), comme jusqu'ici. Ils n'ont donc ni Découverte, ni activation par code, ni verrous. Dans cette 1.35.0, tout a été essayé avec une clé d'**essai** et le service lancé sur ce poste (§ 5) : le code est prêt, il attend ta clé et ton adresse.

## 2. Ce que voit ton client

- **À l'installation**, page « Ta licence » : **Découverte : 30 jours d'essai, sans rien saisir** (par défaut), ou **J'ai un code de licence** avec un champ. Le code est vérifié (sa forme) puis laissé au Studio, qui l'active à son premier lancement en ligne et l'efface. En installation silencieuse : `/CODEFICHIER="code.txt"` (jamais le code sur la ligne de commande : Inno Setup l'écrirait dans son journal).
- **En haut à droite de chaque écran**, une pastille colorée : la licence et sa fin, les jours de Découverte, un bail à renouveler, la licence flottante (poste pris, ou aucun poste). Un clic ouvre la fenêtre Licence. La même phrase est dans la barre d'état ; l'accueil a une carte Licence, avec **Activer avec un code…**.
- **Licence… › Activation** (nouvel onglet) : le code, le nom du PC, **Activer** ; puis la licence en ligne : son bail, la dernière synchronisation, **Renouveler maintenant**, **Libérer ce poste…** ; pour une flottante, **Prendre un poste** / **Rendre le poste** ; la Découverte et ce que le service en retient.
- **Sans Internet** : rien ne s'invente. Le Studio le dit en une phrase, et garde sa licence jusqu'à la fin de son bail. Sept jours avant, il prévient à chaque ouverture.
- **Licence… › Demande à l'éditeur** : la demande sans code reste (une par PC, annulable) ; elle passe maintenant par ton service.
- **Les fonctions verrouillées** : celles que la licence (ou la Découverte finie) n'ouvre pas sont grisées, avec la raison. Le projet reste ouvert ; Enregistrer et Exporter restent permis.

## 3. Ce que tu fais dans ton outil

- **Service en ligne** (à la place de Synchronisation GitHub) : l'adresse, **Vérifier**, **Certifier la clé du service** (et la renouveler), l'état du certificat, la synchro de ton archive (chiffrée par toi : Cloudflare ne lit rien), les compteurs du service.
- **Licences › Nouvelle licence**, « En ligne : code d'activation » : le nombre de PC, le bail (30 jours par défaut), fixe ou flottante. À la signature, elle part sur le service et l'outil te montre **le code, une seule fois** (masqué, **Afficher**, **Copier**). Il n'est gardé nulle part : s'il est perdu, **Nouveau code…** (l'ancien ne marche plus).
- **Licences**, pour une licence en ligne : ses PC (nom, vu le, bail jusqu'au), **Retirer ce PC…** (sa place revient tout de suite ; le PC s'arrête à la fin de son bail s'il ne revient pas en ligne), **Désactiver…**, **Nouveau code…**.
- **Licences flottantes** : les postes pris, jusqu'à quand.
- **Demandes en ligne** : **Relever**, **Accepter…** (une licence liée au PC, ou une licence en ligne : le code s'affiche), **Refuser…** (la raison, scellée pour ce PC).
- **La console** : `service-config`, `service-verifier`, `service-certifier`, `service-registre`, `service-revocations`, `licence-en-ligne`, `service-licences`, `service-retirer`, `service-desactiver`, `service-demandes`, `service-refuser`, `service-publier-licence`, `service-stats`, `archive-cle`, `archive-synchroniser`, `kit-studios`. Les commandes `github-*` et `passerelle-*` n'existent plus.

## 4. Le bail, les PC hors ligne et l'heure

- **Le bail** : chaque activation porte, signée par ton service, sa date de fin (`jusqu_a`). En ligne, le Studio la renouvelle seul (toutes les heures quand il reste 7 jours ou moins, à chaque lancement sinon).
- **Le compteur** : le Studio retient la dernière heure signée par le service et compte le temps écoulé depuis, en trois copies (dont une dans le registre de Windows). L'heure qu'il croit est la plus avancée entre celle du PC et ce compteur, et jamais avant les dates signées (l'émission de l'activation, du jeton).
- **Reculer l'horloge** ne rallonge rien : une licence en ligne passe en « Horloge reculée » jusqu'à la prochaine connexion. **Effacer le compteur** non plus : sans compteur lisible, une licence en ligne est « en pause » jusqu'à la prochaine connexion.
- **De retour en ligne**, le Studio relit l'heure signée du service et recale son compteur ; le renouvellement repart de là.
- **Un PC retiré, désactivé ou révoqué** que tu ne revois jamais : il s'arrête à la fin de son bail (30 jours au plus), même hors ligne à vie. En ligne, il s'arrête au renouvellement suivant.
- **Libérer ce poste** (le client) : la place revient tout de suite deux fois par mois (ou autant que la licence a de postes) ; au-delà, à la fin du bail du PC libéré. **Retirer un PC** (toi) la rend toujours tout de suite.

## 5. Les essais

- **Linux : 159 suites sur 159 réussies** (`ctest`, sur la version livrée). Celles des licences :
  - `service` (85 contrôles) : les formats partagés - l'appel signé, le code d'activation, l'empreinte de la machine, le certificat et la chaîne de confiance, l'adresse ;
  - `service_worker` (84 essais) : le service lui-même, contre `workerd` - chaque route, les limites, les places, le bail, la flottante, la Découverte, les demandes, l'archive, les attaques de la relecture ;
  - `service_local` (77) : le C++ et le service, octet pour octet ;
  - `activation` (213) : le magasin - l'activation, le bail et son compteur, l'horloge reculée, le compteur effacé, deux Studios ouverts, le poste, l'adresse du certificat, le code de l'installateur et qui l'a posé ;
  - `service_studios` (127) : les Studios contre le vrai service - activer, renouveler, libérer, la flottante, la Découverte, la demande, hors ligne, et le code jamais écrit ;
  - `adminservice` (158) : ton outil et sa console contre le vrai service ;
  - `adminguidemandes` (114) et `adminguisecurite` (162) : tes pages, dont Service en ligne, contre le vrai service ;
  - `licensing` (322), `evaluation` (86), `licenceui` (55), `formats_poste` (41) : les licences par fichier, la Découverte, la fenêtre Licence, les formats des demandes.
- `adminguidemandes` a échoué une fois (2 contrôles) dans une première série complète, puis a réussi dans neuf passages (seul, à trois en parallèle, sous forte charge, et dans la série complète finale). La cause n'est pas trouvée.
- **La relecture de sécurité**, faite après coup sur le service et les Studios, a trouvé :
  - **le grave** : en effaçant le compteur et en reculant l'horloge, une licence en ligne restait valable indéfiniment hors ligne → sans compteur, « en pause » ; l'heure a un plancher signé ; l'horloge reculée compte aussi pour les licences en ligne ;
  - **les importants** : un PC libéré rendait sa place tout en gardant son bail (une licence d'un poste sur dix PC) → au-delà de deux transferts par mois, la place revient à la fin du bail ; une identité de PC copiée sur d'autres machines → chaque activation, renouvellement et jeton est lié à l'empreinte de l'installation de Windows ;
  - **les moyens** : n'importe qui pouvait épuiser le plan gratuit de Cloudflare → statistiques en mémoire, compteurs sans parcours, purge des demandes, limiteur par adresse ; des activations sans limite par licence → un plafond par licence, purge des vieilles lignes ; un certificat pour une autre adresse était cru → refusé ;
  - **les mineurs** : le code dans le journal de l'installateur et dans l'écho des scripts → retirés ; un code déposé par un autre compte du PC → ignoré ; un renouvellement après la fin de la licence → refusé ; la liste de révocation entière envoyée à un PC révoqué → plus envoyée ; la Découverte annoncée « impossible à relancer » → la documentation dit honnêtement ses limites.
- **Sous Wine, avec les programmes Windows (WinHTTP), contre le vrai programme du service lancé sur ce poste** (le moteur de Cloudflare, `workerd` ; journal joint : `essai-service-wine.txt`, les codes y sont masqués) :
  - **ton outil** (la console, avec une clé d'essai) : relier le service, vérifier, **certifier sa clé** (un an), déposer le registre, créer la clé de synchro et **synchroniser l'archive**, créer une licence en ligne de 2 PC et une **flottante** d'un poste (chaque code montré une fois), lister, **retirer un PC**, relever une demande, signer et publier, les compteurs du service ;
  - **un IHM Studio compilé avec la clé d'essai** (une copie jamais livrée), cinq « PC » :
    - PC 1 : au premier lancement, **Découverte : il te reste 30 jours**, enregistrée par le service ; avec le code : « Licence XPG-2026-00201 (PROFESSIONAL) activée sur ce PC : bail jusqu'au 07/12/2026 » ;
    - PC 2 : **le code laissé par l'installateur** s'active seul au premier lancement, puis le fichier est effacé ;
    - PC 3 : le même code, les deux places prises : « Tous les postes de cette licence sont déjà pris : rien n'est installé » ; après le retrait de PC 1 par ton outil, le code passe ;
    - PC 1 relancé après son retrait : la licence part, il revient en Découverte ;
    - la flottante : PC 1 prend le poste, puis son Studio est tué (le poste reste pris 15 minutes) ; PC 4 s'active, mais « aucun poste libre — libre dans 13 min ; nouvel essai tout seul » : l'édition sans licence s'applique, le projet reste ouvert ;
    - PC 5 : **la demande sans code** ; ton outil la relève, signe, publie ; au lancement suivant, la licence est installée seule ;
  - **les secrets** : aucun code d'activation en clair dans les dossiers des PC, les journaux des Studios, l'état du service ni le dossier de ton outil ;
  - **ton application** (XPG License Manager Admin) sur le même service : les pages Service en ligne, Licences, Licences flottantes, Demandes en ligne, Tableau de bord ;
  - **l'installation par-dessus la 1.34.0**, en silencieux avec `/CODEFICHIER=` : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.35.0), le code est laissé (sa forme vérifiée, même tapé en minuscules avec des espaces) et **n'est pas dans le journal de l'installateur** ; sous Wine, les droits du fichier (icacls) ne sont pas réellement appliqués ;
  - **la page « Ta licence » de l'installateur**, en mode fenêtre : la Découverte choisie par défaut, ou le code pré-rempli ;
  - captures jointes : `captures-1.35.0.zip` (20 images).

## 6. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Il contient `XPG-License-Manager-Admin.exe`, `SDL3.dll`, `XPG-License-Admin.exe` (la console), `LISEZ-MOI-EDITEUR.txt`, `registre.json`, `parametres-exemple.json` et **`service-licences\`** (le service et sa mise en ligne). Ta clé et ton archive ne bougent pas ; la synchro de l'archive passe par ton service (refais `archive-synchroniser` une fois le service en ligne). Ne le mets jamais avec l'installateur.

**Les Studios** : lance `API-IHM-Studio-Setup-1.35.0.exe` (dans le zip) par-dessus la 1.34.0. Tes projets, tes réglages et ta clé d'API restent. Tant que je n'ai pas tes trois fichiers (§ 1), cette version se comporte chez tes clients comme avant : licences non activées, tout ouvert.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.35.0.exe` | `F202E8BFAEA9F0C0C9D112D088DE44974CA76C049B6D8FA09C1ACD51106D862E` | 55 202 151 octets |
| `API-Studio-1.35.0-portable.zip` | `6FA19DB160A659E682EA96007E6D91AF9197A1860D63063DCF60B9E41D77DDBE` | 190 fichiers |
| `IHM-Studio-1.35.0-portable.zip` | `EB6136AEC2089901D9EC717C1E029CB67024236ED74F2862BB7F7F0E30E3B077` | 191 fichiers |
| `XPG-License-Admin-1.35.0-EDITEUR.zip` (**à part, pour toi seul**) | `98C2C993EB81FF7B4BA19E7C50BCB82C0900AAC89FBAE8E2DCD3587A7314D4D7` | 6 060 834 octets |

## 7. Limites

- **Rien n'a été essayé sur un vrai Windows ni chez Cloudflare** : seulement sous Linux, et sous Wine contre le service lancé sur ce poste (le même programme, avec le moteur de Cloudflare, `workerd`). La mise en ligne (`deployer.bat`) n'a été essayée qu'« à blanc ».
- **Le limiteur par adresse** (`[[ratelimits]]` dans `wrangler.toml`) marche sur ce poste ; qu'il soit accepté par le plan gratuit n'a pas pu être vérifié. S'il est refusé à la mise en ligne, retire ses deux blocs de `wrangler.toml` : le service garde ses propres limites.
- **Le plan gratuit de Cloudflare** suffit pour quelques centaines de PC ; une licence flottante écrit plus (un jeton toutes les 5 minutes par poste ouvert). Au-delà de quelques centaines de PC flottants, le plan payant (5 $ par mois) lève les plafonds.
- **Hors ligne, une copie complète et ancienne de toutes les traces du compteur**, remise avec l'horloge reculée, ne peut pas être détectée ; elle ne donne au plus que la fin du bail de cette copie.
- **La Découverte freine l'essai répété, elle ne l'empêche pas** : réinstaller Windows (ou changer son identifiant d'installation) et effacer les copies recommence un essai.
- **La clé du service** est chez Cloudflare : qui entre dans ton compte pourrait signer des activations jusqu'à la fin du certificat (un an). Protège le compte (double authentification) ; en cas de doute, remets le service en ligne et certifie sa nouvelle clé.
- **Les boutons Boutique et Commandes** de ton outil restent des emplacements : le paiement en ligne n'est pas fait.
