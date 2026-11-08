# API Studio et IHM Studio 1.40.0 : l'Admin en direct, son installateur, la fenêtre Licence sans chevauchement, la maquette du nouveau design

Faite à partir de ton retour du 08/11 sur la 1.39.0 (une capture et ta liste) :
- « Un client qui libère un poste : on ne voit pas la mise à jour sur l'admin » ;
- « gros chevauchement » (la fenêtre Licence, onglet Demander ma licence, sous « Ta licence est activée ») ;
- « que proposes-tu pour améliorer énormément le design de l'admin, IHM et API ; envoie une maquette en priorité, il faut que ce soit magnifique » ;
- « je veux un installateur pour l'admin aussi, pas un zip avec plein de choses : un .exe setup qui gère tout pareil, avec des options ».

Elle se met à jour par-dessus la 1.39.0 et garde tes données. **Le service en ligne (le Worker) change** : redéploie-le (§ 1).

## 0. En bref

- **La maquette** (envoyée en premier, puis en une seule page HTML ; tu l'as validée) : un seul langage pour les trois logiciels — une couleur par logiciel (Admin violet, API Studio bleu, IHM Studio turquoise), Inter et JetBrains Mono, des cartes à coins arrondis, plus d'air, une action principale par écran, l'état en direct, rien qui se chevauche. Elle est dans `docs/maquettes/1.40.0/` (`maquette-1.40.0.html`). **Elle se fait à partir de la 1.41.0**, par étapes : le socle commun d'abord (les couleurs, la police, les boutons, les champs, les cartes), puis l'Admin, puis les Studios.
- **L'Admin en direct** : tant qu'il est ouvert, il regarde ton service **toutes les 15 secondes**. Un PC activé, ou **libéré par son client** (Licence › Mes appareils › Libérer ce PC), apparaît **tout seul** sur la page Licences — plus besoin de Relever — et un avis le dit : « Client … a libéré le PC « … » (licence XPG-…) : sa place est libre. » Une nouvelle demande aussi. En haut : « Service : … · en direct ». Ce regard ne bloque rien : un bouton cliqué pendant qu'il tourne part juste après lui.
- **La fenêtre Licence** : quand ta licence arrive pendant que l'onglet Demander ma licence est ouvert (« Ta licence est activée » par-dessus), les lignes de la dernière demande ne passent plus sur le formulaire. La cause était générale — un écran sous une fenêtre se dessinait sans se remettre en page — et la correction vaut pour toutes les fenêtres.
- **L'installateur de l'Admin** : un seul fichier, `XPG-License-Admin-Setup-1.40.0-EDITEUR.exe`, à la place du zip. Ses étapes : Bienvenue, Emplacement, **Ce que tu installes** (l'application ; la console ; le service en ligne ; les notes), **Les options** (un raccourci sur le Bureau ; ouvrir les .xpgreq, .xpglic, .xpgreg, .xpgrev d'un double-clic ; la console dans le PATH), **Ta clé et ton archive** (ce qu'il a trouvé sur le PC : rien n'y est touché), Récapitulatif, Installation, Terminé. Pour toi seul, sans droits d'administrateur (pour tous les comptes : le choix au début). Le menu Démarrer : l'Admin, **Déployer le service en ligne**, le dossier du service, la console, les notes, Désinstaller. Une nouvelle version s'installe par-dessus et reprend tes choix. **Ta clé, ton archive et tes réglages ne sont jamais touchés**, ni à l'installation ni à la désinstallation.

## 1. Ce qu'il te reste à faire

1. **Installe l'Admin** avec `XPG-License-Admin-Setup-1.40.0-EDITEUR.exe` (le zip **à part, pour toi seul**). Ton ancien dossier du zip (editeur-1.39.0) ne sert plus : supprime-le une fois l'Admin lancé (ta clé n'y est pas : elle est dans `%LOCALAPPDATA%\XPG\LicenseAdmin`).
2. **Redéploie le service** : menu Démarrer › **Déployer le service en ligne**. Ton fichier `cle-publique.txt` : Service en ligne › « Écrire cle-publique.txt… » l'écrit maintenant directement dans le dossier du service installé (`%LOCALAPPDATA%\XPG\ServiceLicences`). Sans redéployer, l'Admin relit les licences toutes les 30 secondes (au lieu de regarder un compteur toutes les 15).
3. **Installe la 1.40.0** chez tes clients (la fenêtre Licence).

## 2. Les essais

- **Linux : 160 suites sur 160 réussies** (`ctest`, sur la version livrée). Celles de cette version :
  - le **service** (`npm test` dans `service/`, le vrai Worker 1.40.0 sous `workerd`) : **94 essais** (3 de plus) — `GET /admin/etat` refusé sans la signature de l'éditeur ; le compteur `activite` monte quand un PC s'active, quand son client le libère, quand il s'active de nouveau, pas à un renouvellement ni pour un PC déjà libéré ; `demandes` monte à une demande déposée ou annulée ; rien de tout ça dans l'état public des Studios ;
  - `adminguidemandes` (130 contrôles, 14 de plus) : l'Admin contre le vrai service — le premier regard (« en direct » en haut), un regard sans changement ne dit rien, le client libère son PC, le regard le voit, les licences relues le montrent libéré, l'avis le dit ; un travail lancé pendant le regard attend sa fin puis part seul ;
  - `adminguishell` (230 contrôles) : les phrases de l'avis (libéré, activé, place reprise ; « Le client » sans client connu ; rien sans relevé d'avant) ;
  - `dialogwindow` (le cadre des fenêtres) : un écran sous une fenêtre dont les données changent se remet en page avant de se dessiner (avant : l'ancienne mise en page — la cause du chevauchement).
- **Sous Wine, de bout en bout**, avec les programmes Windows et le vrai programme du service 1.40.0 lancé sur ce poste (`workerd`) (journal : `essai-1.40.0-wine.txt`, le code masqué ; captures : `captures-1.40.0.zip`). **L'Admin est celui installé par son installateur** (la console et l'application) ; le Studio est une copie avec la clé d'essai, jamais livrée :
  - **le PC libéré, vu seul** : le PC A active une licence PERSONAL ; l'Admin s'ouvre sur Licences (sans jamais cliquer Relever) ; le PC A se libère (Licence › Mes appareils) ; l'Admin le montre « libérée » et dit en bas « Client d'essai Barré a libéré le PC « Poste essai A » (licence XPG-2026-00391) : sa place est libre. », en haut « Service : … · en direct » (capture `1400_01`) ;
  - **plus de chevauchement** : le PC B, la fenêtre Licence ouverte sur Demander ma licence, envoie une demande (`1400_03`) ; l'éditeur l'accepte (une licence en ligne PROFESSIONAL) ; « Ta licence en ligne est activée » s'ouvre par-dessus, et l'onglet dessous est à sa place (`1400_04` ; le code d'essai y est masqué), puis une fois fermée (`1400_05`).
- **L'installateur de l'Admin** (sous Wine, pour toi seul, en silencieux) : l'installation passe ; les programmes installés sont ceux construits ; le service dans `%LOCALAPPDATA%\XPG\ServiceLicences` ; le menu Démarrer, le raccourci du Bureau, les quatre types de fichiers associés à l'Admin, la console dans le PATH ; **le dossier de la clé intact** (son empreinte, avant et après) ; une mise à jour par-dessus reprend les choix ; la désinstallation retire le programme, le service, le menu, les associations et le PATH, et **laisse le dossier de la clé intact**. Ses pages : captures `1400_11` à `1400_16`.
- **Les Studios** : l'installation en silencieux par-dessus la 1.39.0 passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.40.0), le bac à sable tourne, le code laissé par `/CODEFICHIER=` n'est pas dans le journal. Le Studio livré (ton service compilé) s'ouvre, sa fenêtre Licence aussi (`1400_00a`, `1400_00b`) : il a été lancé avec une adresse de service locale où rien ne répond, **ton service n'a pas été appelé**.
- **Corrigé en chemin**, grâce aux essais : l'installateur de l'Admin échouait sur un petit écran (sa fenêtre plus haute que l'écran : elle est maintenant bornée à l'écran) ; le PATH avait un « ; » en trop quand il était vide ; la case « Lancer l'Admin » apparaissait deux fois sous Wine ; la dernière page vouvoyait.

## 3. Installer

**L'outil de l'éditeur** : double-clique `XPG-License-Admin-Setup-1.40.0-EDITEUR.exe` (dans le zip **à part, pour toi seul**). Puis redéploie le service (§ 1).

**Les Studios** : lance `API-IHM-Studio-Setup-1.40.0.exe` (dans le zip) par-dessus la 1.39.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.40.0.exe` | `E56DDD5310748DFC322D773F283A0558E5DEB438673BA6DCB1DABD3DB64AD55F` | 56 836 718 octets |
| `API-Studio-1.40.0-portable.zip` | `FD55DBF26D65A840DCAE3FD14CE5EEE9C9B18164A13C0A74C6C3F7CAE878F351` | 214 fichiers |
| `IHM-Studio-1.40.0-portable.zip` | `4E2AA2FB2135DB523BE84CEEAAF661BD93C0E59D319AD49DB608CB0E98C2B1C0` | 215 fichiers |
| `XPG-License-Admin-Setup-1.40.0-EDITEUR.exe` (**à part, pour toi seul**) | `6BD64E14B9B85F89156E90D660B81AD442C38CDAD094725EE7597FDE2337D2AE` | 6 150 184 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne n'a pas été appelé.
- **« En direct », c'est dans les 15 secondes**, tant que l'Admin est ouvert et qu'aucune fenêtre de dialogue n'est ouverte par-dessus (le regard reprend à sa fermeture). Il coûte environ 8 appels par minute au service (le plan gratuit de Cloudflare en permet 100 000 par jour). Un PC qui se libère hors ligne (sans Internet) ne le dit au service qu'à sa prochaine connexion.
- **L'installateur de l'Admin** a l'habillage de celui des Studios (la colonne des étapes, la police Barlow) ; ses listes (les composants, les options) sont celles d'Inno Setup, pas les cartes de la maquette. Sous Wine, le choix « pour toi seul / pour tous » du début est en anglais (le texte de Windows).
- **Sous Wine, en silencieux**, deux raccourcis du menu Démarrer dont le nom a un accent (Déployer le service en ligne, Désinstaller l'Admin) ne se créent pas — Wine, comme pour les Studios ; installés à la main, ils sont là.
- **La maquette n'est pas encore dans les logiciels** : elle commence en 1.41.0.
