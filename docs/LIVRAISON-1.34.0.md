# API Studio et IHM Studio 1.34.0 : GitHub devient la passerelle des licences, et l'évaluation commence à l'installation

Faite à partir de tes demandes du 07/11 :
- « je veux aussi avoir les demandes sur github, github doit servir de passerelle pour tous les echanges » ;
- « quand j'installe le logiciel classique je dois etre en periode d'evaluation » ;
- « une licence accepté n'a pas besoin de passer par un fichier que la personne doit importé ».

Elle se met à jour par-dessus la 1.33.0 et garde tes données.

## 0. En bref

- **L'évaluation dès l'installation.** Sans licence, un Studio est en évaluation depuis son premier lancement, pour la durée de ton édition d'évaluation (30 jours).
  - À **chaque ouverture**, il dit les jours qui restent : sur l'accueil, dans la cloche, et dans la fenêtre Licence.
  - Ensuite, il revient à l'édition FREE. Rien ne se ferme, et le projet reste ouvert.
- **La demande de licence en ligne**, dans la fenêtre Licence, nouvel onglet **Demande en ligne**.
  - Le client remplit sa société, le nom du PC, un contact et un message, puis **Envoyer à l'éditeur**.
  - **Une seule demande par PC.** Il voit où elle en est et peut **l'annuler**.
- **Tu la vois sans rien importer**, dans ton outil, nouvelle page **Demandes en ligne** : **Relever les demandes**, puis **Accepter…** ou **Refuser…** (avec une raison).
- **Une licence acceptée arrive seule.** Elle est signée et publiée pour ce PC. Son Studio l'installe au démarrage suivant, ou avec « Vérifier maintenant ». Aucun fichier n'est à importer.
- **Les licences pour plusieurs PC** : un client demande « ajouter ce PC à ma licence XPG-… ». Tu l'acceptes, et la nouvelle licence est publiée à tous ses PC, qui la récupèrent seuls. Une révocation, un PC retiré et un nouveau registre passent aussi par la passerelle.
- **Rien d'un client n'est lisible sur GitHub.** Les noms, les licences et les messages sont scellés. Tout ce que lit un Studio est signé par ta clé.
- **Ce qu'il te reste à faire** (§ 1) : créer le dépôt public et le jeton des demandes, régler la passerelle dans ton outil, puis **me donner trois fichiers publics** pour que la version suivante des Studios s'en serve.
- **Rien n'a été essayé sur un vrai Windows ni contre le vrai GitHub** : seulement sous Linux, et sous Wine contre un faux GitHub local (§ 4).

@SUITE@
