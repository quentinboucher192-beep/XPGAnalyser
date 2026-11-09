# API Studio et IHM Studio 1.43.0 : les sessions — plusieurs PC sur le même projet IHM (réseau du bureau)

Faite à partir de la maquette des sessions que tu as validée avec la 1.42.0 (« je valide tout, c'est parti ») et de ta demande : « l'historique des objets sur les vues doit prendre en compte quand c'est une modif dans les sessions avec l'id du PC qui a changé ».

Elle se met à jour par-dessus la 1.42.0 et garde tes données. **Le service en ligne ne change pas** : rien à redéployer. **L'Admin ne change pas** (il reste en 1.42.0) : la maquette 1.43 de l'Admin (plusieurs admins et l'admin+, le journal des admins, les fichiers dans les notifications, les mises à jour poussées), que tu as validée, sera la **1.44.0** : je l'enchaîne.

## 0. En bref

- **Démarrer une session** (IHM Studio, Projet › Démarrer une session…) : le projet ouvert s'ouvre à tes équipiers, sur le réseau du bureau. Tu es l'**hôte** : ton PC garde le projet et l'enregistre. Tu choisis qui entre (**j'accepte chaque entrée**, ou **il faut le code** — 6 chiffres, jamais transmis : il se prouve) et ce qu'ils font en entrant (**modifier**, ou **regarder seulement**, qui se change personne par personne). Il faut « Travail à plusieurs » dans la licence (l'offre ENTERPRISE, ou l'option).
- **Rejoindre une session** (Projet › Rejoindre une session…) : celles du réseau du bureau apparaissent seules (ou l'adresse du PC de l'hôte) ; le projet arrive une fois, **à part de tes projets**, puis seulement les changements, **objet par objet** : deux équipiers sur deux objets de la même vue ne s'écrasent pas. Chacun a sa propre licence d'IHM Studio.
- **Chacun son objet** : un objet choisi par l'un est **pris pour lui** (son contour et son nom, à sa couleur, chez les autres) ; les autres ne peuvent pas le choisir (la barre d'état dit par qui il est pris).
- **Ctrl+Z annule tes gestes, pas ceux des autres** : annuler ne remet que ce que ton geste a touché (une variable ajoutée par un équipier pendant ce temps reste).
- **La pastille « Session · 3 »** en haut : l'équipe (où chacun est, Suivre, Regarder seulement / Laisser modifier), ceux qui veulent entrer (Faire entrer / Refuser), le code (Copier le code), **la discussion**, Arrêter / Quitter.
- **L'historique des objets** signe chaque objet par le PC qui l'a modifié : en session, celui de l'équipier (son nom, son PC, son identifiant stable « M-7F21 », la session).
- **L'option qui arrive** : sa fenêtre s'appelle maintenant « Ton option est arrivée » (avant : « Ta licence est activée »).

## 1. Ce qu'il te reste à faire

1. Installe la 1.43.0 **sur chaque PC** qui travaillera en session (une 1.42 ne voit pas les sessions).
2. **Le pare-feu de Windows** : au premier « Démarrer une session », Windows demande s'il laisse IHM Studio recevoir sur le réseau : réponds **Autoriser** (réseau privé). Ports : **TCP 47863** (le lien), **UDP 47864** (la découverte). Si tu as coché « Laisser IHM Studio recevoir des connexions » à l'installation, la règle est déjà là (elle couvre aussi les sessions).
3. Rien à redéployer : le service en ligne est le même ; l'Admin reste la 1.42.0.

## 2. Les essais

@@ESSAIS@@

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.43.0.exe` (dans le zip) par-dessus la 1.42.0. Tes projets, tes réglages et ta clé d'API restent.

@@EMPREINTES@@

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows, ni sur deux vrais PC** : sous Linux (les sockets réelles, le vrai TLS, sur ce poste) et sous Wine (deux IHM Studio côte à côte sur ce poste, chacun ses dossiers, son certificat et son profil, la découverte par la boucle locale). Le réseau d'un vrai bureau (la diffusion, le pare-feu) reste à voir chez toi.
- **IHM Studio seulement**, **le réseau du bureau seulement** : API Studio en session et les sessions par Internet (le service, chiffré de bout en bout) viennent ensuite, comme dans la maquette.
- **Le reste du projet** (variables, scripts généraux, réglages) passe ligne par ligne : deux équipiers qui changent **la même ligne** en même temps — c'est l'état de l'hôte qui l'emporte (l'autre le voit revenir).
- **Une coupure** : un équipier dont le lien tombe sort de la session (il la rejoint de nouveau) ; l'hôte qui ferme IHM Studio arrête la session pour tout le monde (chacun garde sa copie).
- **8 personnes** au plus par session.
