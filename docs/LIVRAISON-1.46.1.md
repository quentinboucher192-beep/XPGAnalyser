# L'Admin 1.46.1 (correctif de la boutique) : le paiement accepté par Stripe, les réductions suivies, tes produits dans Stripe, Versions accepte un .zip

Fait à partir de ton essai avec ton vrai compte Stripe (mode test) : « The `payment_method_types` parameter is no longer
supported when creating a Checkout Session », « pourquoi les réductions ne sont pas automatiquement liées ?? »,
« pourquoi sur stripe quand je fais enregistrer je vois pas mes produits ??? », et « dans les versions publiables, si
j'envoie un zip avec le .exe dedans il extrait automatiquement le .exe ».

**Seuls l'Admin et le service changent** (1.46.1). Les Studios restent en 1.46.0 : rien à réinstaller chez tes clients.

## 0. En bref

- **Le paiement est de nouveau accepté par Stripe.** Depuis sa version d'API du 30/09/2026 (« endive »), Stripe refuse
  `payment_method_types` ; le service demande maintenant « la carte seulement » par `allowed_payment_method_types`,
  cache Link (qui propose d'enregistrer la carte) par l'API même, et fixe sa version d'API (`Stripe-Version :
  2026-09-30.endive`) sur chaque appel. Toujours aucun client Stripe créé, aucune carte gardée : elle est redemandée à
  chaque achat.
- **Les réductions sont suivies toutes seules.** Le montant en ligne suit le prix affiché du registre (HT + 20 % = TTC) ;
  pendant une réduction, c'est le prix réduit, jusqu'à sa date de fin, puis le prix normal, sans rien faire. Ton
  ENTERPRISE à 50 € HT réduit à 10 € : « Acheter — 12,00 € TTC » jusqu'au 22/12, puis 60,00 €. La Boutique le montre
  (« 12,00 € TTC (réduit jusqu'au 22/12, puis 60,00 €) »). Taper un montant le rend « libre » (il ne suit plus le
  registre) ; l'effacer le fait suivre de nouveau. Signer un registre met la boutique à jour d'elle-même.
- **Tes produits apparaissent dans Stripe.** À « Enregistrer » (et après « Brancher Stripe »), le service crée ou met à
  jour un produit Stripe par article, avec son prix (et le prix réduit à part) ; la page de paiement utilise ces prix.
  Si ta clé ne peut pas écrire les produits (une clé restreinte sans « Products / Prices : écriture »), la vente marche
  quand même et la Boutique dit pourquoi tes produits ne sont pas visibles.
- **Versions accepte un .zip** : l'Admin en extrait l'installateur attendu (et rien d'autre) puis publie comme avec le
  .exe (« extrait de API-IHM-Studio-Setup-1.46.0.zip »). Un zip douteux (plusieurs installateurs, chemins piégés,
  chiffré, énorme) est refusé.

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin 1.46.1** (par-dessus la 1.46.0).
2. **Service en ligne › Déployer le service** : l'Admin dit « la 1.46.1 est prête dans l'Admin : déploie-la ».
3. **Boutique › Enregistrer** une fois : tes produits se créent dans Stripe.
4. (Facultatif) **Rebrancher Stripe…** avec ta clé de test : le point d'arrivée des événements prend la même version
   d'API.
5. Refaire ton achat d'essai depuis un Studio, avec la carte de test de Stripe (4242 4242 4242 4242).

## 2. Les essais

- **Le service** : 182 essais sur 182 (`npm test`), avec un faux Stripe qui refuse maintenant `payment_method_types`
  exactement comme le vrai, et qui imite les produits, les prix et une clé restreinte sans ces droits.
- **L'Admin** (sous Linux) : `adminboutique`, `adminguiboutique`, `adminguiversions` et `licenceui` réussis.
- **Sous Wine, de bout en bout** (journal `essai-1.46.1-wine.txt`, captures `captures-1.46.1.zip`) : l'Admin 1.46.1
  installé par-dessus la 1.46.0 ; le service 1.46.1 lancé sur ce poste et le faux Stripe ; un IHM Studio **1.46.0**
  (copie avec la clé d'essai, jamais livrée). Après Enregistrer, le faux Stripe a reçu 5 produits et 6 prix (dont
  ENTERPRISE à 60,00 € et son prix réduit à 12,00 €) ; tous les appels portaient la version d'API fixée. Le Studio
  affiche « Acheter — 12,00 € TTC » (`1461_01`, `1461_02`) ; la demande de page de paiement n'avait pas
  `payment_method_types`, la carte seule, Link caché, le prix Stripe ; payée par le faux Stripe, livrée seule par
  l'Admin ouvert (`1461_10`), la licence active sur le PC (`1461_03`, le code masqué). La Boutique (`1461_11`) ;
  Versions avec un .zip accepté (`1461_12`) et un .zip refusé (`1461_13`).
  Sur les captures de l'Admin, la barre du bas dit « la 1.47.0 est prête » : c'est un reste de l'essai de la 1.47 sur
  ce poste, pas de la 1.46.1.

## 3. Installer

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.46.1-EDITEUR.exe` (dans le zip) par-dessus
la 1.46.0 ; le dossier de ta clé n'est pas touché.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.46.1-EDITEUR.exe` | `CDEE6BFC49B4DCA7761160F4650F47459A66A5C924F2F6D0BD468BCC4ABE51ED` | 7 414 495 octets |

## 4. Limites

- **Le vrai Stripe n'a pas été appelé** par les essais (un faux qui suit la version d'API lue dans la spécification
  publique de Stripe, 2026-09-30.endive : la documentation en ligne de Stripe n'était pas joignable d'ici). Ton achat
  d'essai (§ 1) est la vraie vérification.
- **Rien sur un vrai Windows** : sous Wine seulement.
- Un prix réduit expiré reste actif chez Stripe jusqu'au prochain Enregistrer (sans effet : la page de paiement prend
  toujours le prix du moment).
- Ces corrections sont aussi dans la 1.47 (en préparation).
