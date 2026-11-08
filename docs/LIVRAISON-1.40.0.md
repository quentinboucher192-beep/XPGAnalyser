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

@ESSAIS@

## 3. Installer

**L'outil de l'éditeur** : double-clique `XPG-License-Admin-Setup-1.40.0-EDITEUR.exe` (dans le zip **à part, pour toi seul**). Puis redéploie le service (§ 1).

**Les Studios** : lance `API-IHM-Studio-Setup-1.40.0.exe` (dans le zip) par-dessus la 1.39.0. Tes projets, tes réglages et ta clé d'API restent.

@EMPREINTES@

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne n'a pas été appelé.
- **« En direct », c'est dans les 15 secondes**, tant que l'Admin est ouvert et qu'aucune fenêtre de dialogue n'est ouverte par-dessus (le regard reprend à sa fermeture). Il coûte environ 8 appels par minute au service (le plan gratuit de Cloudflare en permet 100 000 par jour). Un PC qui se libère hors ligne (sans Internet) ne le dit au service qu'à sa prochaine connexion.
- **L'installateur de l'Admin** a l'habillage de celui des Studios (la colonne des étapes, la police Barlow) ; ses listes (les composants, les options) sont celles d'Inno Setup, pas les cartes de la maquette. Sous Wine, le choix « pour toi seul / pour tous » du début est en anglais (le texte de Windows).
- **La maquette n'est pas encore dans les logiciels** : elle commence en 1.41.0.
