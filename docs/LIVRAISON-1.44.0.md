# API Studio, IHM Studio et l'Admin 1.44.0 : plusieurs admins, leurs droits, le journal, les fichiers, les mises à jour, la sauvegarde de secours

Faite à partir de la maquette de l'Admin que tu as validée (« je valide toutes les maquettes » : planches 00 à 07), de la
planche 08 (les droits des admins : « le admin+ doit pouvoir autoriser ou non certaines fonctions aux autres admins,
il peut créer des profils templates genre 'Admin LV1', 'Admin LV2'… »), de ta demande de secours (« une sauvegarde des
licences doit se faire avec le contenu exact, plus qu'à importer et toutes les configurations seront restaurées »), de
« ajoute 'client supprimé' dans les historiques, journaux sur l'admin », et de l'image des sessions pour
Multi-utilisateur.

Elle se met à jour par-dessus la 1.43.0 (les Studios) et la 1.42.0 (l'Admin), et garde tes données. **Le service en
ligne change** : il faut le redéployer (`deployer.bat`), voir § 1.

## 0. En bref

@@BREF@@

## 1. Ce qu'il te reste à faire

1. **Redéployer le service** (`deployer.bat`, comme d'habitude) : il passe en 1.44.0 (ses données restent : la mise à
   jour de sa base ne fait qu'ajouter). Tant qu'il n'est pas redéployé, l'Admin 1.44 te le dit et les nouveautés en
   ligne attendent.
2. Installer **l'Admin 1.44.0** sur ton PC (tu es l'admin+ : tu as la clé de l'éditeur), puis les **Studios 1.44.0**.
3. **La sauvegarde de secours** : Paramètres de sécurité › Sauvegarde de secours : choisis le dossier (une clé USB, un
   disque à part, un dossier synchronisé) et **ta phrase de passe** (toi seul la connais ; sans elle, la sauvegarde ne
   s'ouvre pas). Elle se fait ensuite chaque jour.
4. Pour ajouter un admin : Administrateurs › Ajouter un admin…

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

## 4. Limites

@@LIMITES@@
