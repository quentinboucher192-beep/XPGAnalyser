// =============================================================================
//  license_admin/AdminMain.cpp - 1.30.0 (prepare en 1.29.0) : XPG-License-Admin,
//  l'outil de l'EDITEUR pour les licences, en console
// -----------------------------------------------------------------------------
//  JAMAIS LIVRE : cible a part (xpg_license_admin), construite sur le poste Admin
//  seulement ; ni dans l'installateur, ni dans un zip (outils/mingw/package_mingw.py
//  arrete la construction s'il trouve son nom ou sa marque dans le dossier a
//  livrer). Voir docs/LICENCES.md et docs/SERVICE-LICENCES.md.
//
//    XPG-License-Admin creer-cle [--essai] [--nom N] [--dossier D]
//    XPG-License-Admin cle [--dossier D]
//    XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp [--garder] [--dossier D]
//                      (une cle d'essai : --sortie src/licensing/TestPublisherKey.hpp)
//    XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp --vide
//    XPG-License-Admin signer-registre --entree registre.json --sortie registre.xpgreg [--dossier D]
//    XPG-License-Admin signer-revocations --entree revocations.json --sortie revocations.xpgrev [--dossier D]
//    XPG-License-Admin signer-licence --parametres p.json --demande pc.xpgreq [--demande ...]
//                      [--licence ancienne.xpglic] --registre registre.xpgreg
//                      [--revocations revocations.xpgrev] --sortie licence.xpglic [--dossier D]
//                      1.33.0 : [--retirer-pc CLE] (avec --licence : une nouvelle emission sans ce PC)
//    XPG-License-Admin verifier FICHIER [--dossier D]
//    XPG-License-Admin enregistrer-demande FICHIER.xpgreq [--dossier D]       (1.30.0)
//    XPG-License-Admin liste [--licence ID] [--client TEXTE] [--csv F.csv] [--dossier D]   (1.30.0)
//  1.32.0 : LA SAUVEGARDE DE LA CLE PAR UNE PHRASE DE PASSE (KeyBackup.hpp) - relisible sur un autre PC :
//    XPG-License-Admin sauvegarder-cle --sortie F.xpgcle [--phrase-variable NOM]
//    XPG-License-Admin verifier-sauvegarde F.xpgcle [--phrase-variable NOM]
//    XPG-License-Admin restaurer-cle --entree F.xpgcle [--phrase-variable NOM] [--dossier D]
//  La phrase se tape sans echo (deux fois pour sauvegarder) ; --phrase-variable la lit dans une variable
//  d'environnement (les essais, un script). Elle n'est jamais affichee ni ecrite.
//  1.30.0 : le registre des activations (activations.jsonl, dans le dossier de la cle) : signer-licence,
//  signer-revocations et enregistrer-demande y ajoutent leurs lignes ; liste montre tous les PC connus.
//  1.31.0 : l'ARCHIVE du poste (chaque fichier signe y est range). 1.35.0 : l'archive se
//  synchronise par le SERVICE DES LICENCES (ArchiveSync.hpp), qui recoit aussi les demandes des clients, active les
//  licences en ligne par un code, tient les licences flottantes (ServiceClient.hpp, docs/SERVICE-LICENCES.md) :
//    XPG-License-Admin archiver FICHIER... [--dossier D]
//    XPG-License-Admin archive-cle [--code-secours CODE [--remplacer]]
//    XPG-License-Admin archive-code-secours --afficher
//    XPG-License-Admin archive-synchroniser [--recevoir-seulement] [--a-blanc]
//    XPG-License-Admin service-config --url https://...
//    XPG-License-Admin service-verifier
//    XPG-License-Admin service-certifier
//    XPG-License-Admin service-registre
//    XPG-License-Admin service-revocations [--entree F.xpgrev]
//    XPG-License-Admin licence-en-ligne --parametres P.json [--bail-jours N] [--sortie L.xpglic] [--etendre [--nouveau-code]]
//    XPG-License-Admin service-licences
//    XPG-License-Admin service-retirer --licence ID --poste CLE
//    XPG-License-Admin service-desactiver --licence ID [--reactiver]
//    XPG-License-Admin service-demandes [--toutes]
//    XPG-License-Admin service-refuser --numero N --raison TEXTE
//    XPG-License-Admin service-publier-licence --licence F.xpglic
//    XPG-License-Admin service-stats
//    XPG-License-Admin service-deployer [--dossier-kit D] [--essai]   (1.45.0 : le kit installe avec l'Admin ; la sortie, son code)
//    1.46.0, la boutique (docs/BOUTIQUE.md) : boutique-etat, boutique-config --parametres F.json, stripe-brancher
//    --cle-variable NOM (la cle secrete lue dans une variable d'environnement, jamais en argument ni affichee), commandes
//    [--etat E], commande-livrer [--numero N], commande-revoir --numero N --raison T, commande-rembourser --numero N
//    1.47.0, les statistiques et le globe (docs/STATISTIQUES.md) : stats-serie --mesures a,b [--du D --au D] [--pas P]
//    [--par X] [--comparer C] [--client C] [--admin ID] [--licence L] [--logiciel S], stats-mises-a-jour, stats-admins,
//    stats-releve, globe [--periode N], globe-direct [--depuis T] ; --json : la reponse du service telle quelle
//    XPG-License-Admin kit-studios --sortie DOSSIER      (les fichiers PUBLICS a compiler dans les Studios)
//    XPG-License-Admin --version
//  1.44.0 : PLUSIEURS ADMINS, LES FICHIERS, LES VERSIONS, LA SAUVEGARDE COMPLETE (docs/ADMINS.md ; par le Workspace de
//  l'application, gui/Workspace.hpp : les memes fonctions) - chacune ecrit une ligne de resultat :
//    XPG-License-Admin admin-demander --nom N --courriel C
//    XPG-License-Admin admin-etat
//    XPG-License-Admin admin-accepter --cle HEX|--empreinte "B7E2 ..." [--profil P]
//    XPG-License-Admin admin-retirer --cle HEX [--revoquer]
//    XPG-License-Admin admin-droits --cle HEX --droits a,b,c [--profil P]
//    XPG-License-Admin profil-enregistrer --nom P --droits a,b,c
//    XPG-License-Admin journal-relever
//    XPG-License-Admin fichier-envoyer --poste ID [--fichier F] --titre T [--texte T] [--admin]
//    XPG-License-Admin version-publier --studios F --admin F [--notes "..."] [--importante]
//    XPG-License-Admin sauvegarde-configurer --dossier D --phrase-fichier F   (ici --dossier : celui des sauvegardes ;
//                      le dossier de la cle : --dossier-cle)
//    XPG-License-Admin sauvegarder
//    XPG-License-Admin restaurer --fichier F --phrase-fichier P [--service URL] [--remplacer] [--remplacer-cle] [--service-seulement]
//  La phrase de passe de la sauvegarde est lue dans un FICHIER (jamais sur la ligne de commande), jamais affichee.
//  Les commandes github-* et passerelle-* (1.31.0 a 1.34.0) n'existent plus : le service des licences les remplace.
//  LE CODE D'ACTIVATION (licence-en-ligne) est montre UNE fois, a l'ecran, puis oublie : le service n'en garde que
//  l'empreinte ; il n'est ecrit dans aucun fichier ni journal.
//  Code de sortie : 0 fait ; 1 refuse (la raison est ecrite) ; 2 mauvais usage ; 3 signe et range, mais la
//  publication (--publier) a echoue (ne pas signer de nouveau : relancer archive-synchroniser).
// =============================================================================
#include "Admin.hpp"
#include "ArchiveSync.hpp"
#include "KeyBackup.hpp"
#include "Mail.hpp"   // 1.37.0 : le mail au client
#include "ServiceClient.hpp"
#include "ServiceDeploy.hpp"   // 1.45.0 : service-deployer
#include "gui/Workspace.hpp"   // 1.44.0 : les commandes des admins, des fichiers, des versions, de la sauvegarde

#include "../core/SystemOpen.hpp"
#include "../deploy/Crypto.hpp"
#include "../licensing/Service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

namespace crypto = deploy::crypto;
using namespace license_admin;

constexpr const char* kToolVersion = "1.3.0";   // 1.31.0 : l'archive ; 1.34.0 : la passerelle ; 1.35.0 : le service des licences

void say(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

void complain(const std::string& text) {
    const std::string line = text + "\n";
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

std::string usage() {
    return std::string("XPG-License-Admin ") + kToolVersion + " - l'outil de l'\xC3\xA9" "diteur pour les licences (" + kAdminMarker + ")\n"
           "Il ne se livre jamais : il reste sur le poste Admin.\n\n"
           "  sauvegarder-cle --sortie F.xpgcle              une sauvegarde de la cl\xC3\xA9 chiffr\xC3\xA9" "e par une phrase de passe (tap\xC3\xA9" "e deux fois,\n"
           "                                                 sans \xC3\xA9" "cho) : elle se relit sur un autre PC, contrairement au fichier DPAPI\n"
           "  verifier-sauvegarde F.xpgcle                   v\xC3\xA9rifie la phrase et la sauvegarde, sans rien \xC3\xA9" "crire\n"
           "  restaurer-cle --entree F.xpgcle                remet la cl\xC3\xA9 sur ce PC (prot\xC3\xA9g\xC3\xA9" "e pour ce compte) ; une cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 reste\n"
           "  creer-cle [--essai] [--nom N] [--dossier D]   cr\xC3\xA9" "e la cl\xC3\xA9 de signature de l'\xC3\xA9" "diteur (prot\xC3\xA9g\xC3\xA9" "e sur ce poste)\n"
           "  cle [--dossier D]                              montre la cl\xC3\xA9 (identifiant, empreinte, essai ou non)\n"
           "  exporter-cle --sortie FICHIER.hpp [--garder]   \xC3\xA9" "crit la cl\xC3\xA9 PUBLIQUE en C++ : src/licensing/PublisherKey.hpp\n"
           "                                                 (une cl\xC3\xA9 d'essai : src/licensing/TestPublisherKey.hpp) ;\n"
           "                                                 --garder : les cl\xC3\xA9s d\xC3\xA9j\xC3\xA0 dans le fichier restent (une rotation)\n"
           "  exporter-cle --sortie PublisherKey.hpp --vide   le fichier SANS cl\xC3\xA9 (les licences ne sont pas actives)\n"
           "  signer-registre --entree R.json --sortie R.xpgreg\n"
           "  signer-revocations --entree V.json --sortie V.xpgrev\n"
           "                                                 V.json : number, revoked[] ; chaque entr\xC3\xA9" "e : license_id, reason,\n"
           "                                                 [device] (un seul PC), [issued_before] (les \xC3\xA9missions sign\xC3\xA9" "es avant cette date)\n"
           "  signer-licence --parametres P.json --demande PC.xpgreq [--demande ...] [--licence ANCIENNE.xpglic]\n"
           "                 --registre R.xpgreg [--revocations V.xpgrev] --sortie L.xpglic\n"
           "                 [--retirer-pc CLE_DU_PC ...] (avec --licence : une nouvelle \xC3\xA9mission sans ce PC)\n"
           "                 une licence LI\xC3\x89" "E \xC3\xA0 ces PC (un fichier \xC3\xA0 leur envoyer) ; en ligne, sans PC : licence-en-ligne\n"
           "  verifier FICHIER                               lit un fichier sign\xC3\xA9 et dit ce qu'il contient\n"
           "  enregistrer-demande PC.xpgreq                  note une demande re\xC3\xA7ue (pas encore sign\xC3\xA9" "e) dans le registre des activations\n"
           "  liste [--licence ID] [--client TEXTE] [--csv F.csv]\n"
           "                                                 tous les PC connus (un par ligne, leur dernier \xC3\xA9tat), les totaux ;\n"
           "                                                 --csv : le m\xC3\xAAme tableau pour Excel (UTF-8, s\xC3\xA9parateur ;)\n"
           "\nL'archive du poste (les licences, le registre, les r\xC3\xA9vocations, le registre des activations) :\n"
           "  archiver FICHIER...                            range des fichiers sign\xC3\xA9s (registre, r\xC3\xA9vocations, licences) dans l'archive ;\n"
           "                                                 signer-* le fait tout seul (--publier : synchronise juste apr\xC3\xA8s)\n"
           "  archive-cle                                    cr\xC3\xA9" "e la cl\xC3\xA9 de synchro de l'archive et montre son CODE DE SECOURS (une fois)\n"
           "  archive-cle --code-secours XPGS1-... [--remplacer]\n"
           "                                                 un autre poste (ou apr\xC3\xA8s une panne) : redonne la cl\xC3\xA9 de synchro ;\n"
           "                                                 --remplacer : une autre cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 est mise de c\xC3\xB4t\xC3\xA9 (jamais effac\xC3\xA9" "e)\n"
           "  archive-code-secours --afficher                remontre le code de secours de la cl\xC3\xA9 de synchro\n"
           "  archive-synchroniser [--recevoir-seulement] [--a-blanc]\n"
           "                                                 re\xC3\xA7oit ce que l'archive en ligne (le service) a de plus r\xC3\xA9" "cent, puis envoie\n"
           "                                                 le reste, chiffr\xC3\xA9 ; --a-blanc : dit ce qui serait fait, sans rien changer\n"
           "\nLe service des licences (ton Worker Cloudflare ; outils/service/LISEZ-MOI-SERVICE.txt pour le mettre en ligne) :\n"
           "  service-config --url https://NOM.COMPTE.workers.dev\n"
           "                                                 son adresse (http:// seulement vers ce poste, pour les essais)\n"
           "  service-verifier                               il r\xC3\xA9pond, ta cl\xC3\xA9 est accept\xC3\xA9" "e, sa cl\xC3\xA9 et son certificat\n"
           "  service-certifier                              montre l'empreinte de la cl\xC3\xA9 du service et la certifie (un an) ;\n"
           "                                                 relance-la avant la fin pour renouveler\n"
           "  service-registre                               d\xC3\xA9pose le registre de l'archive (s'il est plus r\xC3\xA9" "cent)\n"
           "  service-revocations [--entree F.xpgrev]        d\xC3\xA9pose la liste de r\xC3\xA9vocation (PRIV\xC3\x89" "E : seuls les PC concern\xC3\xA9s la re\xC3\xA7oivent)\n"
           "  licence-en-ligne --parametres P.json [--bail-jours N] [--sortie L.xpglic]\n"
           "                                                 signe une licence EN LIGNE (aucun PC : chaque PC l'active avec un code),\n"
           "                                                 la d\xC3\xA9pose avec l'empreinte d'un CODE D'ACTIVATION neuf, et montre ce code\n"
           "                                                 UNE FOIS (note-le pour ton client : il n'est gard\xC3\xA9 nulle part) ;\n"
           "                                                 P.json : comme signer-licence, \"type\" peut \xC3\xAAtre \"floating\" (flottante) ;\n"
           "                                                 --bail-jours : la dur\xC3\xA9" "e d'une activation sans connexion (30 par d\xC3\xA9" "faut)\n"
           "  licence-en-ligne --parametres P.json --etendre [--nouveau-code]\n"
           "                                                 une \xC3\xA9mission plus r\xC3\xA9" "cente (prolonger, plus de postes) ; le m\xC3\xAAme code,\n"
           "                                                 ou un neuf avec --nouveau-code (l'ancien ne marche plus)\n"
           "  service-licences                               les licences en ligne, leurs PC (vu le, bail jusqu'au) et jetons flottants\n"
           "  service-retirer --licence ID --poste CLE       ce PC n'est plus renouvel\xC3\xA9 (sa place se lib\xC3\xA8re)\n"
           "  service-desactiver --licence ID [--reactiver]  la licence ne s'active plus et ses PC ne sont plus renouvel\xC3\xA9s\n"
           "  service-demandes [--toutes]                    rel\xC3\xA8ve les demandes manuelles (rang\xC3\xA9" "es dans demandes-en-ligne/)\n"
           "  service-refuser --numero N --raison TEXTE      refuse cette demande (la raison scell\xC3\xA9" "e pour le PC)\n"
           "  service-publier-licence --licence F.xpglic     une licence li\xC3\xA9" "e \xC3\xA0 des PC : \xC3\xA0 chacun (sa demande en attente : accept\xC3\xA9" "e)\n"
           "  service-stats                                  les compteurs du service\n"
           "  service-deployer [--dossier-kit D] [--essai]   1.45.0 : met le service en ligne, ou \xC3\xA0 jour, sur ton compte Cloudflare avec le\n"
           "                                                 kit install\xC3\xA9 avec l'Admin (deployer.mjs ; Node.js) : sa sortie s'affiche, le\n"
           "                                                 code de sortie est le sien ; --dossier-kit : un autre kit (le dossier de\n"
           "                                                 deployer.bat) ; --essai : tout, sans rien envoyer \xC3\xA0 Cloudflare\n"
           "  licence-en-ligne ... --demande N                1.37.0 : en r\xC3\xA9ponse \xC3\xA0 la demande N : son code part scell\xC3\xA9 pour son PC (et le mail)\n"
           "  code-licence --licence ID                      1.37.0 : revoir le code d'activation d'une licence en ligne (gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste)\n"
           "  mail-config --hote H --expediteur A [--port P] [--securite starttls|tls] [--nom N] [--identifiant I]\n"
           "                                                 1.37.0 : le mail au client \xC3\xA0 l'acceptation (ta bo\xC3\xAEte ; l'application l'envoie)\n"
           "  mail-mot-de-passe                              le mot de passe d'application, tap\xC3\xA9 sans \xC3\xA9" "cho (gard\xC3\xA9 chiffr\xC3\xA9)\n"
           "  mail-essai [--a ADRESSE]                       un mail d'essai (par d\xC3\xA9" "faut \xC3\xA0 ton adresse d'exp\xC3\xA9" "dition)\n"
           "  mail-oublier                                   retire le r\xC3\xA9glage du mail et son mot de passe\n"
           "  mail-config --microsoft outlook|365 --expediteur A --id-application GUID [--nom N]\n"
           "                                                 1.38.0 : une bo\xC3\xAEte Outlook.com / Hotmail (outlook) ou Microsoft 365 (365)\n"
           "  mail-microsoft-connecter                       la connexion Microsoft : un code \xC3\xA0 taper dans ton navigateur\n"
           "  mail-microsoft-oublier                         oublie la connexion Microsoft de ce poste\n"
           "  kit-studios --sortie DOSSIER                   PublisherKey.hpp, registre.xpgreg, ServiceAddress.hpp, cle-publique.txt\n"
           "                                                 (et LISEZ-MOI.txt) : les fichiers PUBLICS des Studios (cl\xC3\xA9 d'essai refus\xC3\xA9" "e)\n"
           "\n1.44.0 : plusieurs admins, les fichiers, les versions, la sauvegarde compl\xC3\xA8te (une ligne de r\xC3\xA9sultat chacune) :\n"
           "  admin-demander --nom N --courriel C            demande \xC3\xA0 devenir admin (la cl\xC3\xA9 d'admin na\xC3\xAEt sur ce poste) ; dit l'empreinte\n"
           "  admin-etat                                     le r\xC3\xB4le de ce poste (rel\xC3\xA8ve la liste des admins, la demande, le journal)\n"
           "  admin-accepter --cle HEX|--empreinte \"B7E2 ...\" [--profil P]\n"
           "                                                 l'admin+ : accepte la demande (l'empreinte lue au t\xC3\xA9l\xC3\xA9phone) ;\n"
           "                                                 le profil \"Tous les droits\" par d\xC3\xA9" "faut\n"
           "  admin-retirer --cle HEX [--revoquer]           l'admin+ : retire un admin (--revoquer : ses licences en vigueur aussi)\n"
           "  admin-droits --cle HEX --droits a,b,c [--profil P]\n"
           "                                                 l'admin+ : les droits d'un admin (vide : aucun)\n"
           "  profil-enregistrer --nom P --droits a,b,c      l'admin+ : un profil de droits\n"
           "  journal-relever                                l'admin+ : rel\xC3\xA8ve le journal des admins et l'\xC3\xA9" "crit (une ligne chacune, sa criticit\xC3\xA9)\n"
           "  fichier-envoyer --poste ID [--fichier F] --titre T [--texte T] [--admin]\n"
           "                                                 l'admin+ : un message (et un fichier) au Studio de ce PC ; --admin :\n"
           "                                                 l'installateur de l'Admin (sans --fichier : celui de la derni\xC3\xA8re version)\n"
           "  version-publier --studios F --admin F [--notes \"...\"] [--importante]\n"
           "                                                 l'admin+ : publie les deux installateurs (leur version lue dans le .exe)\n"
           "  sauvegarde-configurer --dossier D --phrase-fichier F\n"
           "                                                 l'admin+ : la sauvegarde compl\xC3\xA8te de chaque jour (ici --dossier : celui des\n"
           "                                                 sauvegardes ; la cl\xC3\xA9 : --dossier-cle) ; la phrase lue dans F, jamais affich\xC3\xA9" "e\n"
           "  sauvegarder                                    l'admin+ : une sauvegarde compl\xC3\xA8te maintenant\n"
           "  restaurer --fichier F --phrase-fichier P [--service URL] [--remplacer] [--remplacer-cle] [--service-seulement]\n"
           "                                                 remet le dossier du poste et la cl\xC3\xA9 ; --service : aussi le service (\xC3\xA0 cette\n"
           "                                                 adresse) ; --remplacer : un service qui n'est pas vide ; --remplacer-cle : une\n"
           "                                                 autre cl\xC3\xA9 de l'\xC3\xA9" "diteur d\xC3\xA9j\xC3\xA0 l\xC3\xA0 (mise de c\xC3\xB4t\xC3\xA9) ; --service-seulement : le\n"
           "                                                 dossier de ce poste reste tel quel (sa cl\xC3\xA9 : celle de la sauvegarde)\n"
           "\n1.46.0 : la boutique et les commandes (docs/BOUTIQUE.md ; Stripe encaisse, l'Admin livre) :\n"
           "  boutique-etat                                  la boutique : vente en ligne, livraison automatique, le catalogue\n"
           "                                                 (montants TTC), Stripe (branch\xC3\xA9, mode test ou r\xC3\xA9" "el), les commandes \xC3\xA0 livrer\n"
           "  boutique-config --parametres F.json            l'admin+ : le catalogue entier (le JSON de PUT /admin/boutique :\n"
           "                                                 active, livraison_auto, devise, articles [{genre, edition|option,\n"
           "                                                 jours, montant en centimes TTC, libelle}])\n"
           "  stripe-brancher --cle-variable NOM [--dossier-kit D]\n"
           "                                                 l'admin+ : branche Stripe - la cl\xC3\xA9 secr\xC3\xA8te est lue dans la variable\n"
           "                                                 d'environnement NOM (jamais sur la ligne de commande, jamais affich\xC3\xA9" "e) ;\n"
           "                                                 le webhook cr\xC3\xA9\xC3\xA9 chez Stripe, les deux secrets pos\xC3\xA9s sur ton service\n"
           "                                                 par le kit (npx wrangler secret put)\n"
           "  commandes [--etat E]                           les commandes (E : a_payer, payee, livree, a_revoir, remboursee...)\n"
           "  commande-livrer [--numero N]                   livre la commande N (pay\xC3\xA9" "e ou \xC3\xA0 revoir) ; sans --numero : toutes les pay\xC3\xA9" "es\n"
           "  commande-revoir --numero N --raison TEXTE      met la commande N \xC3\xA0 revoir (la raison)\n"
           "  commande-rembourser --numero N                 l'admin+ : demande le remboursement \xC3\xA0 Stripe\n"
           "\n1.47.0 : les statistiques et le globe (docs/STATISTIQUES.md ; un service 1.47.0) - --json : la r\xC3\xA9ponse telle quelle :\n"
           "  stats-serie --mesures a,b [--du AAAA-MM-JJ --au AAAA-MM-JJ] [--pas jour|semaine|mois] [--par offre|option|article|admin|client|\n"
           "              logiciel|version|type|licence] [--comparer avant|an|rien] [--client NOM] [--admin xpg-...] [--licence ID]\n"
           "              [--logiciel studios|admins]\n"
           "                                                 les courbes : ventes_montant, licences_actives, pc_actifs, demandes_traitees...\n"
           "  stats-mises-a-jour [--du D --au D] [--pas P] [--jours N] [--logiciel studios|admins|tous]\n"
           "                                                 les versions, l'adoption jour apr\xC3\xA8s jour, le d\xC3\xA9lai, les PC pas \xC3\xA0 jour\n"
           "  stats-admins [--du D --au D]                   les jours de connexion et les heures habituelles des admins\n"
           "  stats-releve                                   l'admin+ : le relev\xC3\xA9 du jour (s'il n'est pas encore pris)\n"
           "  globe [--periode N]                            les villes des clients, des PC, des admins (la derni\xC3\xA8re connexion)\n"
           "  globe-direct [--depuis T]                      les connexions des 15 derni\xC3\xA8res minutes (T : secondes Unix)\n"
           "\n1.47.0 : la connexion au service et la d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB (docs/ADMINS.md, par. 11) :\n"
           "  service-connexion [--fichier F.xpgsvc]         la connexion sign\xC3\xA9" "e par l'\xC3\xA9" "diteur (le certificat du service) : d'o\xC3\xB9 vient\n"
           "                                                 l'adresse de ce poste ; sans adresse, la cherche (\xC3\xA0 c\xC3\xB4t\xC3\xA9 du programme, le\n"
           "                                                 Studio de ce PC) ; --fichier : celle-ci (un fichier modifi\xC3\xA9 est refus\xC3\xA9)\n"
           "  service-connexion-exporter --sortie F.xpgsvc   le certificat v\xC3\xA9rifi\xC3\xA9 du service (\xC3\xA0 c\xC3\xB4t\xC3\xA9 de l'installateur de l'Admin)\n"
           "  derogation-accorder --cle HEX --heures 1|4|24|72\n"
           "                                                 l'admin+ : \xC2\xAB G\xC3\xA9rer le service \xC2\xBB \xC3\xA0 cet admin, pour un temps court\n"
           "  derogation-revoquer                            l'admin+ : la d\xC3\xA9rogation en cours s'arr\xC3\xAAte tout de suite\n"
           "  derogation-etat                                la d\xC3\xA9rogation (l'admin+ : celle en place ; un admin : la sienne)\n"
           "  admin-lien [--fichier SETUP.exe]               l'admin+ : un lien de 48 h vers l'installateur de l'Admin (celui de la\n"
           "                                                 derni\xC3\xA8re version publi\xC3\xA9" "e, sinon ce fichier) et un vers la connexion au service\n"
           "  --version\n\n"
           "Les commandes github-* et passerelle-* (1.31 \xC3\xA0 1.34) n'existent plus : le service des licences les remplace.\n"
           "--dossier : le dossier de la cl\xC3\xA9 et du registre des activations (par d\xC3\xA9" "faut " + defaultKeyFolder() + ").\n"
           "Le registre des activations (activations.jsonl) : chaque licence sign\xC3\xA9" "e, chaque demande enregistr\xC3\xA9" "e, chaque r\xC3\xA9vocation ;\n"
           "seulement ce que les clients t'envoient (rien de leur mat\xC3\xA9riel).\n"
           "Code de sortie : 0 fait, 1 refus\xC3\xA9, 2 mauvais usage, 3 sign\xC3\xA9 et rang\xC3\xA9 mais pas publi\xC3\xA9 (relance archive-synchroniser).\n";
}

bool readText(const std::string& path, std::string& out, std::string& why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, &why, 16u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

bool writeText(const std::string& path, const std::string& text, std::string& why) {
    crypto::Bytes bytes(text.begin(), text.end());
    return crypto::writePrivateFile(path, bytes, &why);
}

// Une ligne tapee sans echo (le jeton) ; lue telle quelle si l'entree n'est pas une console (un tube).
std::string readSecretLine() {
    std::string line;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
    if (console) SetConsoleMode(h, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    std::getline(std::cin, line);
    if (console) {
        SetConsoleMode(h, mode);
        std::fputs("\n", stderr);
    }
#else
    termios old{};
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios quiet = old;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    std::getline(std::cin, line);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        std::fputs("\n", stderr);
    }
#endif
    return line;
}

struct Args {
    std::string                                     command;
    std::map<std::string, std::vector<std::string>> options;   // --cle -> valeurs
    std::vector<std::string>                        positional;
    std::vector<std::string>                        flags;      // --essai
    [[nodiscard]] std::string one(const std::string& k) const {
        auto it = options.find(k);
        return it == options.end() || it->second.empty() ? std::string() : it->second.back();
    }
    [[nodiscard]] bool flag(const std::string& f) const {
        for (const auto& x : flags)
            if (x == f) return true;
        return false;
    }
};

// ---- 1.44.0 : les commandes qui passent par le Workspace de l'application (une ligne de resultat chacune) ----
// Une liste "a,b,c" (les espaces ignores, les vides sautes).
std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text + ",") {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (c != ' ' && c != '\t') {
            cur += c;
        }
    }
    return out;
}

// La phrase de passe lue dans un fichier (sa premiere ligne, sans la fin de ligne) ; jamais affichee.
bool readPhraseFile(const std::string& path, std::string& phrase, std::string& why) {
    phrase.clear();
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, &why, 4096)) return false;
    std::size_t n = 0;
    while (n < raw.size() && raw[n] != '\n' && raw[n] != '\r') ++n;
    phrase.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(n));
    crypto::wipe(raw.data(), raw.size());
    if (phrase.empty()) {
        why = "le fichier de la phrase est vide";
        return false;
    }
    return true;
}

// Une empreinte lue au telephone : les hexadecimaux seulement, en majuscules.
std::string fingerprintKey(const std::string& text) {
    std::string out;
    for (char c : text)
        if (std::isxdigit(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

int oneLine(const license_admin::gui::OpResult& r) {
    if (r.ok) {
        say(r.message + "\n");
        return 0;
    }
    complain(r.message);
    return 1;
}

// 1.47.0 : les commandes des statistiques et du globe (le Workspace ; une ligne par chiffre, ou --json : la reponse du
// service telle quelle).
std::string statNumber(double v) {
    if (!license_admin::stats::isNumber(v)) return "-";
    if (v == static_cast<double>(static_cast<long long>(v))) return std::to_string(static_cast<long long>(v));
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

int runStatsCommand(const Args& a, license_admin::gui::Workspace& ws) {
    namespace gui = license_admin::gui;
    namespace st = license_admin::stats;
    const bool json = a.flag("--json");
    std::string raw;
    const auto fail = [&](const gui::OpResult& r) {
        complain(r.message);
        return 1;
    };
    st::Period period{a.one("--du"), a.one("--au")};
    if (a.command == "stats-serie") {
        st::SeriesQuery q;
        q.measures = splitList(a.one("--mesures"));
        if (q.measures.empty()) {
            complain("stats-serie : l'option --mesures manque (par exemple --mesures ventes_montant,pc_actifs)");
            return 2;
        }
        q.period = period;
        if (!a.one("--pas").empty() && !st::parseStep(a.one("--pas"), q.step)) {
            complain("stats-serie : --pas vaut jour, semaine ou mois");
            return 2;
        }
        if (!a.one("--comparer").empty() && !st::parseCompare(a.one("--comparer"), q.compare)) {
            complain("stats-serie : --comparer vaut avant, an ou rien");
            return 2;
        }
        q.split = a.one("--par");
        q.client = a.one("--client");
        q.admin = a.one("--admin");
        q.licence = a.one("--licence");
        q.software = a.one("--logiciel");
        st::SeriesReply rep;
        const auto r = ws.statsSeries(q, rep, &raw);
        if (!r.ok) return fail(r);
        if (json) {
            say(raw + "\n");
            return 0;
        }
        say("Du " + rep.period.from + " au " + rep.period.to + " (" + st::stepKey(rep.step) + ")" +
            (rep.beforePeriod.from.empty() ? std::string() : ", compar\xC3\xA9 au " + rep.beforePeriod.from + " \xE2\x86\x92 " + rep.beforePeriod.to) +
            " ; relev\xC3\xA9s depuis le " + rep.recordedSince + "\n");
        for (const auto& s : rep.series) {
            if (s.refused) {
                say(s.name + " : refus\xC3\xA9 (" + (s.filter.empty() ? "il faut le droit " + (s.rights.empty() ? std::string("?") : s.rights.front()) : "pas de filtre " + s.filter) + ")\n");
                continue;
            }
            say(s.name + " (" + st::measureLabel(s.name) + ", " + s.unit + ", " + s.kind + ", " + s.source + ") : total " + statNumber(s.total) +
                (s.hasComparison() ? " ; avant " + statNumber(s.beforeTotal) + (st::isNumber(s.change) ? " (" + st::changeText(s.change) + ")" : std::string()) : std::string()) + "\n");
            for (std::size_t i = 0; i < s.points.size() && i < rep.buckets.size(); ++i)
                say("  " + rep.buckets[i].from + (rep.buckets[i].to != rep.buckets[i].from ? " \xE2\x86\x92 " + rep.buckets[i].to : std::string()) + " : " + statNumber(s.points[i]) +
                    (i < s.before.size() ? " (avant " + statNumber(s.before[i]) + ")" : std::string()) + "\n");
            for (const auto& p : s.parts) say("  \xC2\xB7 " + (p.key.empty() ? std::string("(sans)") : p.key) + " : " + statNumber(p.total) + "\n");
        }
        return 0;
    }
    if (a.command == "stats-mises-a-jour") {
        st::UpdatesQuery q;
        q.period = period;
        if (!a.one("--pas").empty() && !st::parseStep(a.one("--pas"), q.step)) {
            complain("stats-mises-a-jour : --pas vaut jour, semaine ou mois");
            return 2;
        }
        if (!a.one("--jours").empty()) q.days = std::atoi(a.one("--jours").c_str());
        if (!a.one("--logiciel").empty()) q.software = a.one("--logiciel");
        st::UpdatesReply rep;
        const auto r = ws.statsUpdates(q, rep, &raw);
        if (!r.ok) return fail(r);
        if (json) {
            say(raw + "\n");
            return 0;
        }
        say("Derni\xC3\xA8re version : " + (rep.latest.empty() ? std::string("aucune") : rep.latest + " (publi\xC3\xA9" "e le " + gui::dateFr(rep.latestPublished) + ")") + "\n");
        say("\xC3\x80 jour : Studios " + std::to_string(rep.upToDateStudios) + " sur " + std::to_string(rep.seenStudios) + ", Admins " + std::to_string(rep.upToDateAdmins) +
            " sur " + std::to_string(rep.seenAdmins) + " (vus depuis le " + rep.seenSince + ")\n");
        for (const auto& v : rep.versions)
            say("  " + v.version + (v.published ? " publi\xC3\xA9" "e le " + gui::dateFr(v.published) : std::string(" (pas publi\xC3\xA9" "e)")) + " : Studios " +
                std::to_string(v.studios) + ", Admins " + std::to_string(v.admins) + "\n");
        for (const auto& c : rep.adoption) {
            std::string line = "  adoption " + c.version + " " + c.software + " (sur " + std::to_string(c.base) + ") :";
            for (int n : c.days) line += " " + std::to_string(n);
            say(line + "\n");
        }
        for (const auto& d : rep.delays)
            say("  d\xC3\xA9lai " + d.version + " " + d.software + " : m\xC3\xA9" "dian " + st::duration(d.median) + ", moyen " + st::duration(d.mean) + " (" + std::to_string(d.count) + " PC)\n");
        say("PC pas \xC3\xA0 jour : " + std::to_string(rep.behindTotal) + "\n");
        for (std::size_t i = 0; i < rep.behind.size() && i < 20; ++i) {
            const auto& b = rep.behind[i];
            say("  " + (b.name.empty() ? b.key.substr(0, 16) : b.name) + (b.customer.empty() ? std::string() : " \xC2\xB7 " + b.customer) + " \xC2\xB7 " + b.software + " " + b.version +
                " (" + std::to_string(b.behind) + " version(s) de retard, vu le " + gui::dateTimeFr(b.seen) + ")\n");
        }
        return 0;
    }
    if (a.command == "stats-admins") {
        st::AdminsReply rep;
        const auto r = ws.statsAdmins(period, rep, &raw);
        if (!r.ok) return fail(r);
        if (json) {
            say(raw + "\n");
            return 0;
        }
        say("Du " + rep.period.from + " au " + rep.period.to + " ; relev\xC3\xA9s depuis le " + rep.recordedSince + "\n");
        for (const auto& u : rep.admins) {
            const auto hour = [](int h) { return h < 0 ? std::string("-") : std::to_string(h) + " h"; };
            say((u.name.empty() ? std::string(u.role == "admin+" ? "admin+" : "admin") : u.name) + " (" + u.id + ", " + u.role + (u.retired ? ", retir\xC3\xA9" : "") + ") : " +
                std::to_string(u.days.size()) + " jour(s), " + std::to_string(u.calls) + " appel(s), d\xC3\xA9" "but " + hour(u.usualStart) + ", fin " + hour(u.usualEnd) + " (UTC)" +
                (u.lastSeen ? ", vu le " + gui::dateTimeFr(u.lastSeen) : std::string()) +
                (u.software.empty() ? std::string() : ", " + u.software + " " + u.softwareVersion) + (u.city.empty() ? std::string() : ", " + u.city + " " + u.country) + "\n");
        }
        return 0;
    }
    if (a.command == "stats-releve") {
        st::SnapshotReply rep;
        const auto r = ws.statsSnapshot(rep, &raw);
        if (!r.ok) return fail(r);
        say(json ? raw + "\n" : r.message + "\n");
        return 0;
    }
    if (a.command == "globe") {
        st::GlobeReply rep;
        const auto r = ws.statsGlobe(a.one("--periode").empty() ? 30 : std::atoi(a.one("--periode").c_str()), rep, &raw);
        if (!r.ok) return fail(r);
        if (json) {
            say(raw + "\n");
            return 0;
        }
        const auto counts = [](const st::GlobeCity& c) {
            return "clients " + std::to_string(c.clients) + ", PC " + std::to_string(c.pcs) + ", actifs " + std::to_string(c.active) + ", pas \xC3\xA0 jour " +
                   std::to_string(c.outdated) + ", admins " + std::to_string(c.admins) + ", D\xC3\xA9" "couvertes " + std::to_string(c.discoveries) + ", commandes " +
                   std::to_string(c.orders) + ", demandes " + std::to_string(c.requests);
        };
        say(std::to_string(rep.cityCount) + " ville(s), " + std::to_string(rep.countries) + " pays : " + counts(rep.totals) + " ; sans lieu : " +
            std::to_string(rep.unplacedPcs) + " PC, " + std::to_string(rep.unplacedAdmins) + " admin(s)\n");
        for (const auto& c : rep.cities) {
            char ll[48];
            std::snprintf(ll, sizeof ll, "%.2f %.2f", c.lat, c.lon);
            say("  " + (c.city.empty() ? std::string("?") : c.city) + ", " + c.country + (c.region.empty() ? std::string() : " (" + c.region + ")") + " [" + ll + "] : " + counts(c) + "\n");
        }
        return 0;
    }
    // globe-direct
    st::LiveReply rep;
    const licensing::Seconds since = a.one("--depuis").empty() ? 0 : static_cast<licensing::Seconds>(std::atoll(a.one("--depuis").c_str()));
    const auto r = ws.statsLive(since, rep, &raw);
    if (!r.ok) return fail(r);
    if (json) {
        say(raw + "\n");
        return 0;
    }
    say(std::to_string(rep.pings.size()) + " connexion(s) ; " + std::to_string(rep.active) + " PC actif(s) maintenant\n");
    for (const auto& p : rep.pings) say("  " + gui::dateTimeFr(p.at) + " " + p.city + " (" + p.kind + ")\n");
    return 0;
}

int runWorkspaceCommand(const Args& a, const std::string& folder) {
    namespace gui = license_admin::gui;
    const auto need = [&](const std::string& opt) -> bool {
        if (a.one(opt).empty()) {
            complain(a.command + " : l'option " + opt + " manque (XPG-License-Admin aide)");
            return false;
        }
        return true;
    };
    // 1.47.0 : chercher la connexion signee comme l'application au lancement (a cote du programme, le Studio de ce PC)
    if (a.command == "service-connexion")
        license_admin::connexion::setPlaces(license_admin::connexion::defaultPlaces(license_admin::connexion::programFolder(), {}));
    gui::Workspace ws(folder);
    ws.reload();
    if (a.command == "service-connexion") {
        gui::OpResult r = gui::OpResult::done("");
        if (!a.one("--fichier").empty()) r = ws.useConnectionFile(a.one("--fichier"));
        const auto& s = ws.service();
        if (!r.ok) complain(r.message);
        if (s.hasConnection && s.configured && s.connection.url == s.config.url)
            say("Connexion au service : " + s.connection.url + "\n  re\xC3\xA7ue : " + s.connection.source + "\n  sign\xC3\xA9" "e par la cl\xC3\xA9 de l'\xC3\xA9" "diteur " +
                s.connection.editorKeyId + "\n  cl\xC3\xA9 du service : " + online::keyFingerprint(s.connection.serviceKey) + "\n");
        else if (s.configured)
            say("Adresse du service : " + s.config.url + " (r\xC3\xA9gl\xC3\xA9" "e \xC3\xA0 la main, sans connexion sign\xC3\xA9" "e)\n");
        else
            say("Aucune connexion sign\xC3\xA9" "e trouv\xC3\xA9" "e : ni \xC3\xA0 c\xC3\xB4t\xC3\xA9 du programme, ni dans le dossier des licences du Studio de ce PC.\n");
        for (const auto& x : s.connectionRefused) say("Refus\xC3\xA9" "e : " + x.source + " : " + x.why + "\n");
        return r.ok && s.configured ? 0 : 1;
    }
    if (a.command == "service-connexion-exporter") {
        if (!need("--sortie")) return 2;
        return oneLine(ws.exportConnection(a.one("--sortie")));
    }
    if (a.command == "derogation-accorder") {
        if (!need("--cle") || !need("--heures")) return 2;
        return oneLine(ws.grantDerogation(a.one("--cle"), std::atoi(a.one("--heures").c_str())));
    }
    if (a.command == "derogation-revoquer") return oneLine(ws.revokeDerogation());
    if (a.command == "derogation-etat") {
        gui::Derogation d;
        return oneLine(ws.readDerogation(d));
    }
    if (a.command == "admin-demander") {
        if (!need("--nom") || !need("--courriel")) return 2;
        std::string fingerprint;
        return oneLine(ws.requestAdmin(a.one("--nom"), a.one("--courriel"), &fingerprint));
    }
    if (a.command == "admin-etat") {
        std::string state;
        gui::AdminRefresh x;
        gui::OpResult r = gui::OpResult::done("");
        if (ws.role() != gui::Role::None && online::configured(folder)) r = ws.refreshAdminState(nullptr, &x);
        ws.reload();
        std::string line = "R\xC3\xB4le : " + gui::roleLabel(ws.role());
        if (ws.role() == gui::Role::Plus || ws.role() == gui::Role::Admin) line += " (" + std::to_string(ws.myRights().size()) + " droits sur 13)";
        if (ws.role() == gui::Role::Pending && !x.requestState.empty()) line += " ; demande : " + x.requestState;
        if (ws.adminList()) line += " ; liste des admins n\xC2\xB0 " + std::to_string(ws.adminList()->number) + " (" + std::to_string(ws.adminList()->admins.size()) + ")";
        if (ws.adminKey().present) line += " ; empreinte " + ws.adminKey().fingerprint;
        line += " ; journal \xC3\xA0 envoyer : " + std::to_string(ws.journalWaiting());
        if (!r.ok) line += " ; service : " + r.message;
        say(line + "\n");
        return r.ok ? 0 : 1;
    }
    if (a.command == "admin-accepter") {
        std::string key = a.one("--cle");
        if (key.empty()) {
            if (!need("--empreinte")) return 2;
            std::vector<gui::AdminRequest> list;
            const auto p = ws.pendingAdminRequests(list);
            if (!p.ok) return oneLine(p);
            const std::string want = fingerprintKey(a.one("--empreinte"));
            for (const auto& q : list)
                if (!q.fingerprintMismatch && fingerprintKey(q.fingerprint) == want) key = q.key;
            if (key.empty()) {
                complain("Refus\xC3\xA9 : aucune demande d'admin en attente n'a cette empreinte.");
                return 1;
            }
        }
        const std::string profile = a.one("--profil").empty() ? std::string(licensing::admins::kAllRightsProfile) : a.one("--profil");
        return oneLine(ws.acceptAdmin(key, profile, nullptr, true));
    }
    if (a.command == "admin-retirer") {
        if (!need("--cle")) return 2;
        return oneLine(ws.retireAdmin(a.one("--cle"), a.flag("--revoquer")));
    }
    if (a.command == "admin-droits") {
        if (!need("--cle")) return 2;
        if (a.options.find("--droits") == a.options.end()) {
            complain("admin-droits : l'option --droits manque (une liste a,b,c ; \"\" : aucun droit)");
            return 2;
        }
        return oneLine(ws.setAdminRights(a.one("--cle"), splitList(a.one("--droits")), a.one("--profil")));
    }
    if (a.command == "profil-enregistrer") {
        if (!need("--nom")) return 2;
        licensing::admins::Profile p;
        p.name = a.one("--nom");
        p.rights = splitList(a.one("--droits"));
        return oneLine(ws.saveProfile(p));
    }
    if (a.command == "journal-relever") {
        int added = 0;
        const auto r = ws.fetchJournal(&added);
        if (!r.ok) return oneLine(r);
        const auto view = ws.journalLines();
        for (const auto& row : view.rows)
            say("n\xC2\xB0 " + std::to_string(row.line.number) + " [" + licensing::admins::levelLabel(row.level) + "] " +
                (row.who.empty() ? std::string("?") : row.who) + " " + row.line.parsed.action + " : " + row.line.parsed.summary +
                (row.verified ? std::string() : " (NON V\xC3\x89RIFI\xC3\x89" "E : " + row.problem + ")") + "\n");
        for (const auto& p : view.problems) say("ATTENTION : " + p + "\n");
        return oneLine(r);
    }
    if (a.command == "admin-lien") {   // 1.47.0 : << Ajouter un admin... par un lien >> (48 h, une fois) et la connexion au service
        std::string url;
        const auto r = ws.adminInstallerLink(a.one("--fichier"), url);
        if (!r.ok) return oneLine(r);
        say(r.text() + "\n");
        return 0;
    }
    if (a.command == "fichier-envoyer") {
        if (!need("--poste")) return 2;
        if (a.one("--fichier").empty() && a.flag("--admin")) return oneLine(ws.sendAdminInstaller(a.one("--poste"), a.one("--nom"), ""));
        if (!need("--fichier") || !need("--titre")) return 2;
        licensing::admins::MessageFile f;
        const auto up = ws.attachFile(a.one("--fichier"), f);
        if (!up.ok) return oneLine(up);
        return oneLine(ws.sendMessageToPc(a.one("--poste"), a.one("--titre"), a.one("--texte"), {f}, a.flag("--admin") ? "admin" : ""));
    }
    if (a.command == "version-publier") {
        if (!need("--studios") || !need("--admin")) return 2;
        std::vector<std::string> notes;
        if (const auto it = a.options.find("--notes"); it != a.options.end())
            for (const auto& n : it->second) {
                std::size_t pos = 0;   // une note par ligne (ou un | entre deux notes)
                std::string text = n;
                for (auto& c : text)
                    if (c == '|') c = '\n';
                while (pos <= text.size()) {
                    const std::size_t end = text.find('\n', pos);
                    notes.push_back(text.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
                    if (end == std::string::npos) break;
                    pos = end + 1;
                }
            }
        return oneLine(ws.publishVersion(a.one("--studios"), a.one("--admin"), notes, a.flag("--importante")));
    }
    if (a.command == "sauvegarde-configurer") {
        if (!need("--dossier") || !need("--phrase-fichier")) return 2;
        std::string phrase, w;
        if (!readPhraseFile(a.one("--phrase-fichier"), phrase, w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        const auto r = ws.configureBackup(a.one("--dossier"), phrase);
        crypto::wipe(phrase.data(), phrase.size());
        return oneLine(r);
    }
    if (a.command == "sauvegarder") return oneLine(ws.backupNow());
    if (a.command == "archive-synchroniser") {
        // 1.44.0 : un ADMIN (sans la cle de l'editeur) : sa cle d'admin signe les appels, comme l'application.
        const auto r = ws.archiveSync(a.flag("--recevoir-seulement"), a.flag("--a-blanc"));
        std::string text = r.text();
        if (r.ok) {
            say(text);
            return 0;
        }
        if (!text.empty() && text.back() == '\n') text.pop_back();
        complain(text);
        return 1;
    }
    if (a.command == "licence-en-ligne") {
        // 1.44.0 : un ADMIN (sans la cle de l'editeur) : sa cle d'admin signe, comme l'application (Nouvelle licence).
        if (!need("--parametres")) return 2;
        std::string text, w;
        if (!readText(a.one("--parametres"), text, w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        core::json::Value p;
        if (!core::json::parse(text, p, nullptr) || !p.isObject()) {
            complain("Refus\xC3\xA9 : " + a.one("--parametres") + " n'est pas un objet JSON");
            return 1;
        }
        gui::LicenceDraft d;
        d.online = true;
        d.licenseId = p.str("license_id");
        d.customer = p.str("customer");
        d.edition = p.str("edition");
        d.maxDevices = static_cast<int>(p.integer("max_devices", 0));
        d.durationDays = static_cast<int>(p.integer("duration_days", -1));
        if (!a.one("--bail-jours").empty()) d.leaseDays = std::atoi(a.one("--bail-jours").c_str());
        gui::ServiceJob job;
        std::string code;
        const auto s = ws.prepareOnlineLicence(d, {}, job, code);
        if (!s.ok) {
            crypto::wipe(code.data(), code.size());
            return oneLine(s);
        }
        say("Sign\xC3\xA9" "e (en ligne) par " + ws.adminKey().fingerprint + " : " + s.message + "\n");
        const auto r = ws.serviceRunOnline(job, code);
        if (!r.ok) {
            crypto::wipe(code.data(), code.size());
            return oneLine(r);
        }
        say(r.message + "\n\n  CODE D'ACTIVATION :\n\n      " + code + "\n\n");
        crypto::wipe(code.data(), code.size());
        return 0;
    }
    // ---- 1.46.0 : la boutique et les commandes ----
    if (a.command == "boutique-etat") {
        license_admin::shop::Settings s;
        const auto r = ws.shopSettings(s);
        if (!r.ok) return oneLine(r);
        namespace sh = license_admin::shop;
        say("Vente en ligne : " + std::string(s.active ? "ouverte" : "arr\xC3\xAAt\xC3\xA9" "e") + (s.open ? "" : " (la boutique est ferm\xC3\xA9" "e pour les Studios)") + "\n");
        say("Livraison automatique : " + std::string(s.autoDeliver ? "oui (l'Admin ouvert livre seul les commandes pay\xC3\xA9" "es)" : "non") + "\n");
        say(sh::modeLabel(s.stripe.mode) + (s.stripe.connected && !s.stripe.webhook ? " (le secret du webhook manque)" : "") +
            (s.stripe.lastEvent ? " ; dernier \xC3\xA9v\xC3\xA9nement re\xC3\xA7u le " + gui::dateTimeFr(s.stripe.lastEvent) : std::string()) + "\n");
        say("Catalogue (r\xC3\xA9glage n\xC2\xB0 " + std::to_string(s.number) + ") : " + std::to_string(s.articles.size()) + " article(s)\n");
        // 1.46.1 : le montant du moment (une reduction en cours : jusqu'a quand, puis le montant de base ; libre : tape)
        for (const auto& x : s.articles)
            say("  " + x.id + " : " + (x.legacy ? sh::amountText(x.amount, s.currency) + " TTC" : sh::articleAmountText(x, ws.now(), s.currency)) + " \xC2\xB7 " + x.label + "\n");
        if (!s.catalog.state.empty()) say("Produits chez Stripe : " + s.catalog.message + "\n");
        if (ws.can("demandes")) {
            sh::OrdersView v;
            if (ws.shopOrders(v).ok) say("Commandes : " + std::to_string(v.orders.size()) + ", \xC3\xA0 livrer : " + std::to_string(v.toDeliver) + "\n");
        }
        return 0;
    }
    if (a.command == "boutique-config") {
        if (!need("--parametres")) return 2;
        std::string text, w;
        if (!readText(a.one("--parametres"), text, w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        core::json::Value p;
        license_admin::shop::Settings s;
        if (!core::json::parse(text, p, nullptr) || !license_admin::shop::parseSettings(p, s, &w)) {
            complain("Refus\xC3\xA9 : " + a.one("--parametres") + " n'est pas le JSON de la boutique" + (w.empty() ? std::string() : " (" + w + ")"));
            return 1;
        }
        return oneLine(ws.saveShop(s));
    }
    if (a.command == "stripe-brancher") {
        if (!need("--cle-variable")) return 2;
        const std::string var = a.one("--cle-variable");
        const char* v = std::getenv(var.c_str());
        if (!v || !*v) {
            complain("Refus\xC3\xA9 : la variable d'environnement " + var + " est vide (mets-y ta cl\xC3\xA9 secr\xC3\xA8te Stripe, puis relance).");
            return 1;
        }
        std::string key = v;
        deployer::Options o;
        o.kit = a.one("--dossier-kit");
        const auto r = ws.connectStripe(key, o, [](const std::string& line) { say(line + "\n"); });
        crypto::wipe(key.data(), key.size());
        if (r.ok) {
            say(r.message + "\n");
            for (const auto& d : r.details)
                if (d.rfind("Ouvre", 0) == 0) say(d + "\n");
            return 0;
        }
        complain(r.message);
        for (const auto& d : r.details)
            if (d.rfind("Clique", 0) == 0 || d.rfind("Installe", 0) == 0 || d.rfind("https://", 0) == 0 || d.rfind("Sur ", 0) == 0 || d.rfind("R\xC3\xA8gle", 0) == 0)
                complain(d);
        return 1;
    }
    if (a.command == "commandes") {
        namespace sh = license_admin::shop;
        sh::OrdersView v;
        const auto r = ws.shopOrders(v);
        if (!r.ok) return oneLine(r);
        const std::string want = a.one("--etat");
        int shown = 0;
        for (const auto& o : v.orders) {
            if (!want.empty() && o.state != want) continue;
            ++shown;
            say("n\xC2\xB0 " + std::to_string(o.number) + " " + gui::dateTimeFr(o.created) + " [" + sh::stateLabel(o.state) + "] " + o.company +
                (o.name.empty() ? std::string() : " (" + o.name + ")") + " \xC2\xB7 " + (o.label.empty() ? o.article : o.label) + " \xC2\xB7 " +
                sh::amountText(o.amount, o.currency) + (o.mode == "test" ? " (essai)" : "") + (o.licence.empty() ? std::string() : " \xC2\xB7 licence " + o.licence) +
                (o.error.empty() ? std::string() : " \xC2\xB7 " + o.error) + "\n");
        }
        say(std::to_string(shown) + " commande(s) ; \xC3\xA0 livrer : " + std::to_string(v.toDeliver) + "\n");
        return 0;
    }
    if (a.command == "commande-livrer") {
        const int n = a.one("--numero").empty() ? 0 : std::atoi(a.one("--numero").c_str());
        gui::DeliveryReport rep;
        const auto r = ws.deliverOrders(n, false, &rep);
        say(r.message + "\n");
        for (const auto& d : r.details) say("  " + d + "\n");
        return r.ok ? 0 : 1;
    }
    if (a.command == "commande-revoir") {
        if (!need("--numero") || !need("--raison")) return 2;
        return oneLine(ws.reviewOrder(std::atoi(a.one("--numero").c_str()), a.one("--raison")));
    }
    if (a.command == "commande-rembourser") {
        if (!need("--numero")) return 2;
        return oneLine(ws.refundOrder(std::atoi(a.one("--numero").c_str())));
    }
    // ---- 1.47.0 : les statistiques et le globe (docs/STATISTIQUES.md) ----
    if (a.command == "stats-serie" || a.command == "stats-mises-a-jour" || a.command == "stats-admins" || a.command == "stats-releve" ||
        a.command == "globe" || a.command == "globe-direct")
        return runStatsCommand(a, ws);
    if (a.command == "restaurer") {
        if (!need("--fichier") || !need("--phrase-fichier")) return 2;
        std::string phrase, w;
        if (!readPhraseFile(a.one("--phrase-fichier"), phrase, w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        gui::RestoreOptions o;
        o.restoreService = !a.one("--service").empty();
        o.serviceUrl = a.one("--service");
        o.replaceService = a.flag("--remplacer");
        o.replaceKey = a.flag("--remplacer-cle");
        o.restoreFolder = !a.flag("--service-seulement");
        gui::RestoreReport rep;
        auto r = ws.restoreBackup(a.one("--fichier"), phrase, o, &rep);
        crypto::wipe(phrase.data(), phrase.size());
        if (r.ok && rep.serviceRestored) {
            std::uint64_t lines = 0;
            for (const auto& t : rep.tables) lines += t.second;
            r.message += " " + std::to_string(rep.tables.size()) + " tables, " + std::to_string(lines) + " lignes ; fichiers \xC3\xA0 renvoyer : " +
                         std::to_string(rep.filesToResend) + ".";
        }
        return oneLine(r);
    }
    complain("commande inconnue : " + a.command);
    return 2;
}

int run(const std::vector<std::string>& argv) {
    if (argv.empty() || argv[0] == "aide" || argv[0] == "--aide" || argv[0] == "-h" || argv[0] == "--help") {
        say(usage());
        return argv.empty() ? 2 : 0;
    }
    if (argv[0] == "--version") {
        say(std::string("XPG-License-Admin ") + kToolVersion + " (" + kAdminMarker + ")\n");
        return 0;
    }
    Args a;
    a.command = argv[0];
    static const std::vector<std::string> kFlags = {"--essai",     "--garder",    "--vide",     "--publier",     "--afficher", "--remplacer",
                                                    "--recevoir-seulement", "--a-blanc", "--toutes", "--etendre", "--nouveau-code", "--reactiver",
                                                    "--revoquer", "--importante", "--remplacer-cle", "--service-seulement",   // 1.44.0
                                                    "--json"};   // 1.47.0 : la reponse du service telle quelle (stats-*, globe)
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& x = argv[i];
        if (x.rfind("--", 0) == 0) {
            // 1.44.0 : --admin est un drapeau pour fichier-envoyer (l'installateur de l'Admin), une valeur pour version-publier.
            if (std::find(kFlags.begin(), kFlags.end(), x) != kFlags.end() || (a.command == "fichier-envoyer" && x == "--admin")) {
                a.flags.push_back(x);
            } else if (i + 1 < argv.size()) {
                a.options[x].push_back(argv[++i]);
            } else {
                complain("l'option " + x + " attend une valeur");
                return 2;
            }
        } else {
            a.positional.push_back(x);
        }
    }
    // 1.44.0 : --dossier-cle nomme le dossier de la cle pour toutes les commandes ; sauvegarde-configurer prend --dossier
    // pour celui des sauvegardes.
    const std::string folder = !a.one("--dossier-cle").empty()                                  ? a.one("--dossier-cle")
                               : (a.command == "sauvegarde-configurer" || a.one("--dossier").empty()) ? defaultKeyFolder()
                                                                                                       : a.one("--dossier");
    const licensing::Seconds now = licensing::systemNow();
    std::string why;
    if (!crypto::init()) {
        complain("libsodium ne d\xC3\xA9marre pas (le hasard du syst\xC3\xA8me est indisponible)");
        return 1;
    }
    const auto need = [&](const std::string& opt) -> bool {
        if (a.one(opt).empty()) {
            complain(a.command + " : l'option " + opt + " manque\n\n" + usage());
            return false;
        }
        return true;
    };
    const auto keyOrFail = [&]() -> std::unique_ptr<SigningKey> {
        auto key = loadPublisherKey(folder, &why);
        if (!key) complain("Refus\xC3\xA9 : " + why + "\n(cr\xC3\xA9" "er la cl\xC3\xA9 : XPG-License-Admin creer-cle --dossier " + folder + ")");
        return key;
    };
    // ---- 1.45.0 : mettre le service en ligne avec le kit installe (deployer::deploy : node deployer.mjs, sans fenetre) ----
    if (a.command == "service-deployer") {
        deployer::Options o;
        o.kit = a.one("--dossier-kit");
        o.dryRun = a.flag("--essai");
        // 1.47.0 : gerer le service est a l'admin+ ; un admin, seulement sous derogation - le service dit d'abord si elle court
        // (et l'ecrit au journal), puis le kit recoit les cles PUBLIQUES de l'editeur que le service croit (jamais la sienne).
        {
            license_admin::gui::Workspace ws(folder);
            const auto role = ws.role();
            if (role == license_admin::gui::Role::Admin || role == license_admin::gui::Role::Retired || role == license_admin::gui::Role::Pending) {
                const auto u = ws.useDerogation("deployer", o.dryRun ? "service-deployer --essai" : "service-deployer");
                if (!u.ok) {
                    complain("Refus\xC3\xA9 : " + u.message);
                    return 1;
                }
                say(u.message + "\n");
                deployer::Kit k;
                std::string kw;
                if (deployer::findKit(o.kit, k, &kw)) {
                    const auto w2 = ws.writeEditorKeysFile(k.folder);
                    if (!w2.ok) {
                        complain("Refus\xC3\xA9 : " + w2.message);
                        return 1;
                    }
                    say(w2.message + "\n");
                }
                say("Cloudflare : connecte-toi avec TON compte (membre du compte de l'\xC3\xA9" "diteur), jamais un jeton partag\xC3\xA9.\n");
            }
        }
        // cle-publique.txt manque dans le kit : la cle PUBLIQUE de l'editeur de ce poste l'ecrit (rien de secret).
        deployer::Kit kit;
        if (deployer::findKit(o.kit, kit, nullptr) && !kit.publicKey) {
            std::string w;
            if (auto key = loadPublisherKey(folder, &w)) {
                const auto u8 = (std::filesystem::path(std::u8string(kit.folder.begin(), kit.folder.end())) / "cle-publique.txt").u8string();
                const std::string path(u8.begin(), u8.end());
                if (crypto::replaceFile(path, online::publicKeyText(*key), false, &w)) say("cle-publique.txt \xC3\xA9" "crite dans le kit : " + path + "\n");
                else complain("cle-publique.txt n'est pas \xC3\xA9" "crite : " + w);
            }
        }
        const deployer::Report r = deployer::deploy(o, [](const std::string& line) { say(line + "\n"); });
        if (r.ok()) {
            say("\n" + r.reason + "\n" + (o.dryRun ? std::string() : std::string("Ensuite : XPG-License-Admin service-verifier (sa version, ta cl\xC3\xA9, son certificat).\n")));
            return 0;
        }
        complain("\n" + r.text());
        return r.exitCode > 0 ? r.exitCode : 1;
    }
    // Le registre des activations : les lignes d'une signature (un echec ne defait pas la signature : il est dit).
    const auto noteActivations = [&](const std::vector<ActivationRecord>& records) {
        if (records.empty()) return;
        std::string w;
        if (appendActivations(folder, records, &w))
            say("Registre des activations : " + std::to_string(records.size()) + " ligne(s) ajout\xC3\xA9" "e(s) (" + activationsPath(folder) + ")\n");
        else
            complain("ATTENTION : le registre des activations n'a pas \xC3\xA9t\xC3\xA9 mis \xC3\xA0 jour (" + w + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    };
    const auto showKey = [&](const SigningKey& key) {
        say("Cl\xC3\xA9 de l'\xC3\xA9" "diteur : " + key.name() + "\n  identifiant " + key.keyId() + "\n  empreinte   " + key.fingerprint(0) + "\n  fichier     " + key.path()
            + "\n  protection  " + (crypto::protectedHere() ? "DPAPI (ce compte Windows)" : "fichier lisible par son seul propri\xC3\xA9taire (pas de DPAPI ici)") + "\n"
            + (key.test() ? "  !!! CL\xC3\x89 D'ESSAI : jamais pour des licences de clients ; un programme qui l'embarque n'est pas livrable !!!\n" : ""));
    };

    // 1.31.0 : ranger un fichier signe dans l'archive du poste (un echec ne defait pas la signature : il est dit).
    const auto archiveFile = [&](const std::string& bytes, const SigningKey& key) {
        std::string what, w;
        if (archive::archiveSigned(folder, bytes, archive::trustedKeys(&key), &what, &w))
            say("Archive : " + what + " (" + archive::archiveFolder(folder) + ")\n");
        else
            complain("ATTENTION : le fichier n'a pas \xC3\xA9t\xC3\xA9 rang\xC3\xA9 dans l'archive (" + w + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    };
    // 1.35.0 : la synchro de l'archive par le service (la cle de l'editeur signe les appels).
    const auto syncReport = [&](const SigningKey& key, bool receiveOnly, bool dryRun) -> int {
        archive::SyncOptions o;
        o.receiveOnly = receiveOnly;
        o.dryRun = dryRun;
        o.now = licensing::systemNow();
        o.tool = std::string("XPG-License-Admin ") + kToolVersion;
        const auto r = archive::synchronize(folder, key, o);
        std::string text = r.text();
        if (r.ok) {
            say(text);
        } else {
            if (!text.empty() && text.back() == '\n') text.pop_back();
            complain(text);
        }
        return r.ok ? 0 : 1;
    };
    const auto serviceOptions = [&] {
        online::Options o;
        o.now = licensing::systemNow();
        o.tool = std::string("XPG-License-Admin/") + kToolVersion;
        return o;
    };
    const auto showResult = [&](const online::Result& r) -> int {
        std::string text = r.text();
        if (r.ok) {
            say(text + "\n");
            return 0;
        }
        complain(text);
        return 1;
    };
    // Apres une signature (--publier) : l'archive synchronisee ; le registre ou la liste de revocation deposes aussi sur
    // le service. Sans --publier : le rappeler si le service est regle.
    const auto afterSigning = [&](const SigningKey& key, const char* kind) -> int {
        const bool registry = std::string(kind) == "registre", revocations = std::string(kind) == "revocations";
        if (!a.flag("--publier")) {
            if (online::configured(folder)) {
                say("\xC3\x80 publier : XPG-License-Admin archive-synchroniser");
                if (registry) say(", puis service-registre");
                if (revocations) say(", puis service-revocations");
                say("\n");
            }
            return 0;
        }
        say("\nSynchro de l'archive (--publier) :\n");
        int rc = syncReport(key, false, false);
        if (rc == 0 && registry) rc = showResult(online::publishRegistry(folder, key, serviceOptions()));
        if (rc == 0 && revocations) rc = showResult(online::publishRevocations(folder, key, serviceOptions()));
        if (rc == 0) return 0;
        complain("Le fichier est sign\xC3\xA9 et rang\xC3\xA9 dans l'archive ; seule la publication a \xC3\xA9" "chou\xC3\xA9 : ne signe pas de nouveau, relance archive-synchroniser"
                 + std::string(registry ? " et service-registre" : revocations ? " et service-revocations" : "") + ".");
        return 3;
    };

    if (a.command == "creer-cle") {
        auto key = createPublisherKey(folder, a.one("--nom"), a.flag("--essai"), &why);
        if (!key) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cr\xC3\xA9\xC3\xA9" "e.\n");
        showKey(*key);
        say("Sauvegarde : garde une copie chiffr\xC3\xA9" "e hors ligne de cette cl\xC3\xA9 ; sans elle, plus aucune licence ne peut \xC3\xAAtre sign\xC3\xA9" "e.\n");
        return 0;
    }
    if (a.command == "cle") {
        auto key = keyOrFail();
        if (!key) return 1;
        showKey(*key);
        return 0;
    }
    if (a.command == "exporter-cle") {
        if (!need("--sortie")) return 2;
        const bool empty = a.flag("--vide"), keep = a.flag("--garder");
        if (empty && keep) {
            complain("exporter-cle : --vide et --garder ensemble ? choisis l'un ou l'autre");
            return 2;
        }
        std::unique_ptr<SigningKey> key;
        if (!empty) {
            key = keyOrFail();
            if (!key) return 1;
        }
        // Le fichier actuel : ses cles sont gardees avec --garder, et dites quand elles en sortent.
        const std::string outPath = a.one("--sortie");
        std::string existing;
        std::error_code ec;
        if (std::filesystem::is_regular_file(std::filesystem::path(std::u8string(outPath.begin(), outPath.end())), ec) && !readText(outPath, existing, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string text, summary;
        if (!exportPublisherKeys(key.get(), existing, outPath, keep, empty, now, text, &why, &summary) || !writeText(outPath, text, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9" "s publiques \xC3\xA9" "crites : " + outPath + "\n" + summary
            + (key && key->test() ? "!!! CL\xC3\x89 D'ESSAI : compil\xC3\xA9" "e seulement avec l'option CMake XPG_LICENCES_CLE_ESSAI, jamais livr\xC3\xA9" "e !!!\n" : ""));
        return 0;
    }
    if (a.command == "signer-registre" || a.command == "signer-revocations") {
        if (!need("--entree") || !need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        std::string input, out;
        if (!readText(a.one("--entree"), input, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::vector<ActivationRecord> records;
        const bool ok = a.command == "signer-registre" ? signRegistry(*key, input, now, out, &why) : signRevocations(*key, input, now, out, &why, &records);
        if (!ok || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string text;
        (void)describeFile(out, key.get(), text);
        say("Sign\xC3\xA9 : " + a.one("--sortie") + "\n" + text);
        noteActivations(records);
        archiveFile(out, *key);
        return afterSigning(*key, a.command == "signer-registre" ? "registre" : "revocations");
    }
    if (a.command == "signer-licence") {
        if (!need("--parametres") || !need("--registre") || !need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        LicenseInputs in;
        if (!readText(a.one("--parametres"), in.params, why) || !readText(a.one("--registre"), in.registry, why)
            || (!a.one("--revocations").empty() && !readText(a.one("--revocations"), in.revocations, why))
            || (!a.one("--licence").empty() && !readText(a.one("--licence"), in.existing, why))) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        // 1.33.0 : retirer des PC de la licence etendue (--licence) : un PC remplace ou rendu.
        if (auto drop = a.options.find("--retirer-pc"); drop != a.options.end()) in.dropDevices = drop->second;
        auto it = a.options.find("--demande");
        if (it != a.options.end())
            for (const auto& path : it->second) {
                std::string req;
                if (!readText(path, req, why)) {
                    complain("Refus\xC3\xA9 : " + why);
                    return 1;
                }
                in.requests.push_back(std::move(req));
            }
        std::string out, summary;
        std::vector<ActivationRecord> records;
        if (!signLicense(*key, in, now, out, &why, &summary, &records) || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sign\xC3\xA9" "e : " + a.one("--sortie") + "\n" + summary + (key->test() ? "!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI !!!\n" : ""));
        noteActivations(records);
        archiveFile(out, *key);
        // 1.35.0 : le service regle, la licence part a ses PC (et repond a leur demande) par une commande.
        if (online::configured(folder))
            say("\xC3\x80 publier \xC3\xA0 ses PC (chacun la r\xC3\xA9" "cup\xC3\xA8re seul) : XPG-License-Admin service-publier-licence --licence "
                + a.one("--sortie") + "\n");
        return afterSigning(*key, "licence");
    }
    if (a.command == "enregistrer-demande") {
        if (a.positional.empty()) {
            complain("enregistrer-demande : quelle demande (.xpgreq) ?\n\n" + usage());
            return 2;
        }
        std::vector<ActivationRecord> records;
        for (const auto& path : a.positional) {
            std::string bytes;
            ActivationRecord r;
            if (!readText(path, bytes, why) || !requestRecord(bytes, now, r, &why)) {
                complain("Refus\xC3\xA9 (" + path + ") : " + why);
                return 1;
            }
            say("Demande enregistr\xC3\xA9" "e : PC " + (r.deviceName.empty() ? std::string("(sans nom)") : "\xC2\xAB " + r.deviceName + " \xC2\xBB") + " (" + r.fingerprint
                + "), demand\xC3\xA9" "e le " + licensing::isoTime(r.requestedAt) + (r.licenseId.empty() ? "" : ", licence " + r.licenseId)
                + (r.software.empty() ? "" : ", " + r.software) + "\n");
            records.push_back(std::move(r));
        }
        if (!appendActivations(folder, records, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Registre des activations : " + activationsPath(folder) + "\n");
        if (a.flag("--publier")) {
            auto key = keyOrFail();
            if (!key) return 3;
            say("\nSynchro de l'archive (--publier) :\n");
            if (syncReport(*key, false, false) == 0) return 0;
            complain("La demande est not\xC3\xA9" "e ; seule la publication a \xC3\xA9" "chou\xC3\xA9 : relance archive-synchroniser.");
            return 3;
        }
        if (online::configured(folder)) say("\xC3\x80 publier : XPG-License-Admin archive-synchroniser\n");
        return 0;
    }
    if (a.command == "liste") {
        std::vector<ActivationRecord> records;
        int skipped = 0;
        if (!readActivations(folder, records, &skipped, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        const auto devices = filterDevices(knownDevices(records, now), a.one("--licence"), a.one("--client"));
        std::string head = "Registre des activations : " + activationsPath(folder) + " (le " + licensing::isoDate(now) + ")";
        if (!a.one("--licence").empty()) head += ", licence " + a.one("--licence");
        if (!a.one("--client").empty()) head += ", client \xC2\xAB " + a.one("--client") + " \xC2\xBB";
        say(head + "\n\n" + devicesTable(devices));
        if (skipped > 0) complain("Attention : " + std::to_string(skipped) + " ligne(s) illisible(s) du registre ignor\xC3\xA9" "e(s).");
        if (!a.one("--csv").empty()) {
            if (!writeText(a.one("--csv"), devicesCsv(devices), why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("CSV \xC3\xA9" "crit : " + a.one("--csv") + "\n");
        }
        return 0;
    }
    // 1.32.0 : la phrase de passe d'une sauvegarde (jamais affichee) : la variable donnee, ou tapee sans echo.
    const auto readPhrase = [&](const char* prompt, std::string& phrase) -> bool {
        if (!a.one("--phrase-variable").empty()) {
            const char* v = std::getenv(a.one("--phrase-variable").c_str());
            if (!v || !*v) {
                complain("Refus\xC3\xA9 : la variable " + a.one("--phrase-variable") + " est vide ou absente");
                return false;
            }
            phrase = v;
            return true;
        }
        complain(prompt);
        phrase = readSecretLine();
        while (!phrase.empty() && (phrase.back() == '\r' || phrase.back() == '\n')) phrase.pop_back();
        return true;
    };
    const auto wipeText = [](std::string& s) {
        std::fill(s.begin(), s.end(), '\0');
        s.clear();
    };
    if (a.command == "sauvegarder-cle") {
        if (!need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        std::string p1, p2, out;
        if (!readPhrase("Phrase de passe de la sauvegarde (12 caract\xC3\xA8res au moins ; elle ne s'affiche pas) : ", p1)) return 1;
        if (a.one("--phrase-variable").empty()) {
            if (!readPhrase("La m\xC3\xAAme phrase, une seconde fois : ", p2)) return 1;
            if (p1 != p2) {
                wipeText(p1);
                wipeText(p2);
                complain("Refus\xC3\xA9 : les deux phrases ne sont pas les m\xC3\xAAmes ; rien n'est \xC3\xA9" "crit.");
                return 1;
            }
            wipeText(p2);
        }
        say("Chiffrement de la sauvegarde (Argon2id : quelques secondes, 256 Mio de m\xC3\xA9moire)...\n");
        const bool ok = writeKeyBackup(*key, p1, out, &why);
        wipeText(p1);
        if (!ok || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sauvegarde \xC3\xA9" "crite : " + a.one("--sortie") + "\n  cl\xC3\xA9 " + key->keyId() + (key->test() ? " (CL\xC3\x89 D'ESSAI)" : "") + "\n"
            "Range-la HORS de ce PC (cl\xC3\xA9 USB au coffre, un second support) ; note la phrase \xC3\xA0 part (gestionnaire de mots de passe).\n"
            "Sans la phrase, la sauvegarde ne sert \xC3\xA0 rien ; avec elle, n'importe qui peut signer des licences : garde les deux s\xC3\xA9par\xC3\xA9ment.\n"
            "V\xC3\xA9rifie-la maintenant : XPG-License-Admin verifier-sauvegarde " + a.one("--sortie") + "\n");
        return 0;
    }
    if (a.command == "verifier-sauvegarde" || a.command == "restaurer-cle") {
        const bool restore = a.command == "restaurer-cle";
        const std::string file = restore ? a.one("--entree") : (a.positional.empty() ? std::string() : a.positional[0]);
        if (file.empty()) {
            complain(a.command + " : quelle sauvegarde (.xpgcle) ?\n\n" + usage());
            return 2;
        }
        std::string bytes, keyId, text, phrase, name;
        if (!readText(file, bytes, why) || !describeKeyBackup(bytes, keyId, text, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say(file + " : " + text + "\n");
        if (!readPhrase("Phrase de passe de la sauvegarde (elle ne s'affiche pas) : ", phrase)) return 1;
        if (!restore) {
            const bool ok = checkKeyBackup(bytes, phrase, keyId, name, &why);
            wipeText(phrase);
            if (!ok) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Bonne : la phrase ouvre la sauvegarde de la cl\xC3\xA9 " + keyId + (name.empty() ? std::string() : " (" + name + ")") + ".\n");
            return 0;
        }
        auto key = restoreKeyBackup(folder, bytes, phrase, &why);
        wipeText(phrase);
        if (!key) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9 restaur\xC3\xA9" "e sur ce PC.\n");
        showKey(*key);
        return 0;
    }
    if (a.command == "archiver") {
        if (a.positional.empty()) {
            complain("archiver : quels fichiers sign\xC3\xA9s (registre, r\xC3\xA9vocations, licences) ?\n\n" + usage());
            return 2;
        }
        auto key = keyOrFail();
        if (!key) return 1;
        int refused = 0;
        for (const auto& path : a.positional) {
            std::string bytes, what, w;
            if (!readText(path, bytes, w) || !archive::archiveSigned(folder, bytes, archive::trustedKeys(key.get()), &what, &w)) {
                complain("Refus\xC3\xA9 (" + path + ") : " + w);
                ++refused;
                continue;
            }
            say(path + " : " + what + "\n");
        }
        say("Archive : " + archive::archiveFolder(folder) + "\n");
        if (refused == 0 && online::configured(folder)) say("\xC3\x80 publier : XPG-License-Admin archive-synchroniser\n");
        return refused ? 1 : 0;
    }
    // ---- 1.35.0 : la cle de synchro de l'archive (ses fichiers partent chiffres par elle) ----
    if (a.command == "archive-cle") {
        archive::SyncKey sk{};
        if (!a.one("--code-secours").empty()) {
            std::string kept;
            if (!archive::restoreSyncKey(folder, a.one("--code-secours"), sk, &why, a.flag("--remplacer"), &kept)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Cl\xC3\xA9 de synchro redonn\xC3\xA9" "e par le code de secours : " + archive::syncKeyId(sk) + "\n"
                + (kept.empty() ? std::string() : "  (l'ancienne est gard\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9 : " + kept + ")\n") + "Ensuite : XPG-License-Admin archive-synchroniser --recevoir-seulement\n");
            crypto::wipe(sk);
            return 0;
        }
        if (archive::hasSyncKey(folder)) {
            if (!archive::loadSyncKey(folder, sk, &why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Cl\xC3\xA9 de synchro d\xC3\xA9j\xC3\xA0 sur ce poste : " + archive::syncKeyId(sk) + " (archive-code-secours --afficher remontre son code).\n");
            crypto::wipe(sk);
            return 0;
        }
        if (!archive::createSyncKey(folder, sk, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9 de synchro cr\xC3\xA9\xC3\xA9" "e : " + archive::syncKeyId(sk) + "\n"
            "  (L'archive en ligne a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 envoy\xC3\xA9" "e par un autre PC ? Il te faut SON code de secours :\n"
            "   archive-cle --code-secours XPGS1-... --remplacer)\n\n"
            "  !!! NOTE CE CODE DE SECOURS, hors de ce PC (gestionnaire de mots de passe, papier au coffre) !!!\n\n"
            "      " + archive::recoveryCode(sk) + "\n\n"
            "  Sans lui, apr\xC3\xA8s une panne de ce PC, les licences et le registre des activations de l'archive en ligne restent illisibles.\n"
            "  Ne le mets jamais dans un fichier envoy\xC3\xA9, ni dans un courriel. archive-code-secours --afficher le remontre.\n");
        crypto::wipe(sk);
        return 0;
    }
    if (a.command == "archive-code-secours") {
        archive::SyncKey sk{};
        if (!archive::loadSyncKey(folder, sk, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        if (!a.flag("--afficher")) {
            say("Cl\xC3\xA9 de synchro : " + archive::syncKeyId(sk) + "\nLe code de secours ne s'affiche qu'avec --afficher (personne derri\xC3\xA8re toi ?).\n");
            crypto::wipe(sk);
            return 0;
        }
        say("Cl\xC3\xA9 de synchro " + archive::syncKeyId(sk) + "\nCode de secours :\n\n      " + archive::recoveryCode(sk) + "\n\n"
            "Garde-le hors de ce PC ; ne l'envoie jamais.\n");
        crypto::wipe(sk);
        return 0;
    }
    // 1.44.0 : licence-en-ligne et archive-synchroniser chez un ADMIN (sa cle d'admin signe, pas celle de l'editeur)
    if ((a.command == "licence-en-ligne" && a.flags.empty() && a.one("--demande").empty()) || a.command == "archive-synchroniser") {
        std::error_code ec;
        const auto u8 = [](const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); };
        const bool editor = std::filesystem::exists(u8(publisherKeyPath(folder)), ec);
        const bool admin = std::filesystem::exists(u8(adminKeyPath(folder)), ec);
        // archive-synchroniser : toujours comme l'application (la liste des admins : une licence signee par un admin est
        // recue chez l'admin+ aussi) ; licence-en-ligne : par l'application chez un admin seulement.
        if ((a.command == "archive-synchroniser" && (editor || admin)) || (!editor && admin)) return runWorkspaceCommand(a, folder);
    }
    if (a.command == "archive-synchroniser") {
        auto key = keyOrFail();
        if (!key) return 1;
        return syncReport(*key, a.flag("--recevoir-seulement"), a.flag("--a-blanc"));
    }
    // ---- 1.35.0 : le service des licences ----
    if (a.command == "service-config") {
        if (!need("--url")) return 2;
        online::Config c;
        c.url = a.one("--url");
        if (!online::writeConfig(folder, c, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        online::Config back;
        (void)online::readConfig(folder, back, &why);
        say("Service des licences : " + back.url + "\n  (" + online::configPath(folder) + ")\nEnsuite : XPG-License-Admin service-verifier, puis service-certifier.\n");
        return 0;
    }
    if (a.command == "service-verifier" || a.command == "service-certifier" || a.command == "service-registre" || a.command == "service-revocations" ||
        a.command == "service-licences" || a.command == "service-retirer" || a.command == "service-desactiver" || a.command == "service-demandes" ||
        a.command == "service-refuser" || a.command == "service-publier-licence" || a.command == "service-stats" || a.command == "licence-en-ligne") {
        if (a.command == "service-retirer" && (!need("--licence") || !need("--poste"))) return 2;
        if (a.command == "service-desactiver" && !need("--licence")) return 2;
        if (a.command == "service-refuser" && (!need("--numero") || !need("--raison"))) return 2;
        if (a.command == "service-publier-licence" && !need("--licence")) return 2;
        if (a.command == "licence-en-ligne" && !need("--parametres")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        const online::Options o = serviceOptions();
        if (a.command == "service-verifier") {
            // 1.47.0 : le certificat verifie est garde (la connexion a exporter : service-connexion-exporter)
            online::ServiceState st;
            const online::Result r = online::verify(folder, *key, o, &st);
            if (r.ok && st.certOk && !st.certBlock.empty()) (void)license_admin::connexion::keepCertificate(folder, st.certBlock, nullptr);
            return showResult(r);
        }
        if (a.command == "service-registre") return showResult(online::publishRegistry(folder, *key, o));
        if (a.command == "service-stats") return showResult(online::stats(folder, *key, o));
        if (a.command == "service-retirer") return showResult(online::retireDevice(folder, *key, a.one("--licence"), a.one("--poste"), o));
        if (a.command == "service-refuser") return showResult(online::refuse(folder, *key, a.one("--numero"), a.one("--raison"), o));
        if (a.command == "service-certifier") {
            std::string serviceKey;
            const online::Result r = online::readServiceKey(folder, *key, o, serviceKey);
            if (!r.ok) return showResult(r);
            say("Cl\xC3\xA9 du service : " + online::keyFingerprint(serviceKey) + "\n  (" + serviceKey + ")\n");
            return showResult(online::certify(folder, *key, o, serviceKey));
        }
        if (a.command == "service-revocations") {
            if (!a.one("--entree").empty()) {
                std::string bytes, what;
                if (!readText(a.one("--entree"), bytes, why) || !archive::archiveSigned(folder, bytes, archive::trustedKeys(key.get()), &what, &why)) {
                    complain("Refus\xC3\xA9 : " + why);
                    return 1;
                }
                say(a.one("--entree") + " : " + what + "\n");
            }
            return showResult(online::publishRevocations(folder, *key, o));
        }
        if (a.command == "service-publier-licence") {
            std::string text;
            if (!readText(a.one("--licence"), text, why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            return showResult(online::publishBoundLicence(folder, *key, text, false, o));
        }
        if (a.command == "service-desactiver") {
            return showResult(online::setActive(folder, *key, a.one("--licence"), a.flag("--reactiver"), 0, o));
        }
        if (a.command == "service-licences") {
            online::Snapshot s;
            const online::Result r = online::listLicences(folder, *key, o, s);
            const int rc = showResult(r);
            if (rc != 0) return rc;
            for (const auto& l : s.licences) {
                std::string line = "\n" + l.licenseId + "  " + (l.floating() ? "flottante" : l.type) + ", " + std::to_string(l.seats) +
                                   (l.floating() ? " postes en m\xC3\xAAme temps" : " PC au plus") + ", bail " + std::to_string(l.leaseDays) + " j" +
                                   (l.active ? std::string() : ", D\xC3\x89SACTIV\xC3\x89" "E") + (l.hasCode ? std::string() : ", sans code") + "\n  \xC3\xA9mission " +
                                   licensing::isoTime(l.issuedAt);
                if (l.activations.empty()) line += "\n  aucun PC activ\xC3\xA9";
                for (const auto& act : l.activations)
                    line += "\n  PC \xC2\xAB " + (act.name.empty() ? std::string("(sans nom)") : act.name) + " \xC2\xBB " + act.deviceKey + "\n     " + act.stateLabel() +
                            ", activ\xC3\xA9 le " + (act.activatedAt ? licensing::isoTime(act.activatedAt) : std::string("?")) + ", vu le " +
                            (act.seenAt ? licensing::isoTime(act.seenAt) : std::string("?")) + ", bail jusqu'au " + (act.until ? licensing::isoTime(act.until) : std::string("?"));
                for (const auto& t : l.leases)
                    line += "\n  jeton pris par " + t.deviceKey.substr(0, 16) + "... jusqu'\xC3\xA0 " + licensing::isoTime(t.until);
                say(line + "\n");
            }
            if (!s.licences.empty()) say("\nRetirer un PC : service-retirer --licence ID --poste CLE ; d\xC3\xA9sactiver : service-desactiver --licence ID.\n");
            return 0;
        }
        if (a.command == "service-demandes") {
            const online::Result r = online::fetchRequests(folder, *key, o);
            const int rc = showResult(r);
            const auto all = online::listRequests(folder);
            int hidden = 0;
            bool waiting = false, join = false;
            for (const auto& e : all) {
                waiting = waiting || e.waiting();
                join = join || (e.waiting() && e.joinRequest());
                if (!a.flag("--toutes") && !e.waiting()) {
                    ++hidden;
                    continue;
                }
                std::string line = "\nn\xC2\xB0 " + std::to_string(e.number) + "  " + (e.createdAt ? licensing::isoTime(e.createdAt) : std::string("?")) + "  " +
                                   online::requestStateLabel(e.state) + "\n  poste " + e.device;
                if (e.state != online::RequestState::Unreadable) {
                    line += "\n  client " + e.customer + (e.contact.empty() ? std::string() : " (" + e.contact + ")") + "\n  PC \xC2\xAB " +
                            (e.deviceName.empty() ? std::string("(sans nom)") : e.deviceName) + " \xC2\xBB " + e.fingerprint + (e.software.empty() ? std::string() : ", " + e.software) +
                            "\n  licence demand\xC3\xA9" "e " + (e.licenseId.empty() ? std::string("(nouvelle)") : e.licenseId) + "\n  demande " + e.requestPath;
                    if (!e.message.empty()) line += "\n  message " + e.message;
                    if (!e.answer.empty()) line += "\n  r\xC3\xA9ponse " + e.answer;
                    if (e.joinRequest() && e.waiting()) line += "\n  !! " + online::joinWarning(e);
                }
                for (const auto& p : e.problems) line += "\n  ! " + p;
                say(line + "\n");
            }
            if (hidden > 0) say("\n(" + std::to_string(hidden) + " demande(s) d\xC3\xA9j\xC3\xA0 trait\xC3\xA9" "e(s) : --toutes pour les voir)\n");
            if (waiting)
                say("\nAccepter : signer-licence --demande <le fichier demande> ... puis service-publier-licence --licence <la licence> ;\n"
                    "refuser : service-refuser --numero N --raison \"...\".\n");
            if (join)
                say("Une demande d'ajout \xC3\xA0 une licence existante (!!) : n'importe qui peut taper un identifiant de licence et un nom de client - "
                    "confirme avec ton client avant de l'accepter.\n");
            return rc;
        }
        // ---- licence-en-ligne : signer une licence SANS PC, la deposer avec l'empreinte d'un code neuf, montrer le code ----
        online::Config cfg;
        if (!online::readConfig(folder, cfg, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        int lease = a.flag("--etendre") ? 0 : online::kDefaultLeaseDays;   // etendre : le service garde son bail, sauf --bail-jours
        if (!a.one("--bail-jours").empty()) {
            const std::string t = a.one("--bail-jours");
            if (t.empty() || t.size() > 3 || !std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; }) || std::atoi(t.c_str()) < 1 ||
                std::atoi(t.c_str()) > 366) {
                complain("licence-en-ligne : --bail-jours va de 1 \xC3\xA0 366");
                return 2;
            }
            lease = std::atoi(t.c_str());
        }
        // 1.37.0 : --demande N - la reponse a cette demande (son code scelle pour son PC, et le mail) : verifiee d'abord.
        const std::string demande = a.one("--demande");
        if (!demande.empty()) {
            online::Entry waiting;
            if (a.flag("--etendre")) {
                complain("licence-en-ligne : --demande repond par une licence NEUVE (sans --etendre)");
                return 2;
            }
            if (!online::findWaiting(folder, demande, waiting, &why) || waiting.acceptedHere()) {
                complain("Refus\xC3\xA9 : " + (why.empty() ? "la demande n\xC2\xB0 " + demande + " est d\xC3\xA9j\xC3\xA0 accept\xC3\xA9" "e ici (licence " + waiting.answer + ")" : why));
                return 1;
            }
        }
        LicenseInputs in;
        if (!readText(a.one("--parametres"), in.params, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        in.service = cfg.url;
        in.registry = archive::archivedText(folder, "registre/registre.xpgreg");
        in.revocations = archive::archivedText(folder, "revocations/revocations.xpgrev");
        if (in.registry.empty()) {
            complain("Refus\xC3\xA9 : pas de registre sign\xC3\xA9 dans l'archive (signer-registre d'abord)");
            return 1;
        }
        core::json::Value p;
        (void)core::json::parse(in.params, p, nullptr);
        const std::string id = p.isObject() ? p.str("license_id") : std::string();
        const std::string existing = id.empty() ? std::string() : archive::archivedText(folder, "licences/" + archive::safeName(id) + ".xpglic");
        if (a.flag("--etendre")) {
            if (existing.empty()) {
                complain("Refus\xC3\xA9 : la licence " + id + " n'est pas dans l'archive : rien \xC3\xA0 \xC3\xA9tendre (sans --etendre : une licence neuve)");
                return 1;
            }
            in.existing = existing;
        } else if (!existing.empty()) {
            complain("Refus\xC3\xA9 : la licence " + id + " est d\xC3\xA9j\xC3\xA0 dans l'archive : --etendre pour une \xC3\xA9mission plus r\xC3\xA9" "cente (le m\xC3\xAAme code, ou --nouveau-code)");
            return 1;
        }
        std::string out, summary;
        std::vector<ActivationRecord> records;
        if (!signLicense(*key, in, now, out, &why, &summary, &records)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        if (!a.one("--sortie").empty() && !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sign\xC3\xA9" "e (en ligne) : " + summary + (key->test() ? "!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI !!!\n" : ""));
        noteActivations(records);
        archiveFile(out, *key);
        const bool newCode = !a.flag("--etendre") || a.flag("--nouveau-code");
        std::string code, normalized, hash;
        if (newCode) {
            code = licensing::service::newActivationCode();
            (void)licensing::service::normalizeCode(code, normalized);
            hash = licensing::service::codeHash(normalized);
            crypto::wipe(normalized.data(), normalized.size());
        }
        const online::Result r = online::putLicence(folder, *key, out, hash, lease, true, o);
        if (!r.ok) {
            crypto::wipe(code.data(), code.size());
            complain(r.text() + "\nLa licence est sign\xC3\xA9" "e et rang\xC3\xA9" "e dans l'archive, mais PAS sur le service : " +
                     (newCode ? std::string("son code n'est pas montr\xC3\xA9 (il ne servirait \xC3\xA0 rien). ") : std::string()) +
                     "Relance plus tard : licence-en-ligne --parametres " + a.one("--parametres") + " --etendre --nouveau-code");
            return 1;
        }
        say(r.text() + "\n");
        if (newCode) {
            // 1.37.0 : garde chiffre sur ce poste (pour ce compte) : il se revoit (code-licence --licence ID)
            std::string keepWhy;
            const bool kept = online::storeCode(folder, id, code, &keepWhy);
            say("\n  CODE D'ACTIVATION :\n\n      " + code + "\n\n"
                "  Le client le tape dans Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA Activer avec un code (ou dans l'installateur).\n" +
                (kept ? std::string("  Il est gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste (pour ce compte) : code-licence --licence ID pour le revoir.\n")
                      : "  ATTENTION : il n'a pas pu \xC3\xAAtre gard\xC3\xA9 sur ce poste (" + keepWhy + ") : note-le.\n") +
                "  Le service n'en garde que l'empreinte. Perdu : licence-en-ligne ... --etendre --nouveau-code (l'ancien ne marche plus).\n");
            if (!demande.empty()) {   // 1.37.0 : la reponse a la demande - le code scelle pour son PC (il s'active seul), et le mail
                std::string w;
                if (!online::markAcceptedHere(folder, std::atoi(demande.c_str()), id, &w)) say("ATTENTION : " + w + "\n");
                licensing::Registry reg;
                bool hasReg = false;
                std::vector<licensing::SignedBlock> rb;
                if (licensing::readBlocks(in.registry, rb, nullptr))
                    for (const auto& blk : rb)
                        if (blk.kind == licensing::DocKind::Registry) hasReg = licensing::parseRegistry(blk.document, reg, nullptr);
                const online::Result ar = online::answerOnline(folder, *key, std::atoi(demande.c_str()), id, code, out,
                                                               online::smtpMailer(folder, hasReg ? &reg : nullptr), o);
                if (!ar.ok) {
                    crypto::wipe(code.data(), code.size());
                    complain(ar.text() + "\nLa licence est d\xC3\xA9pos\xC3\xA9" "e ; la r\xC3\xA9ponse n'est pas partie : l'application la renvoie (Demandes en ligne "
                                         "\xE2\x80\xBA Renvoyer la r\xC3\xA9ponse).");
                    return 3;
                }
                say(ar.text() + "\n");
            }
            crypto::wipe(code.data(), code.size());
        } else {
            say("Le code d'activation reste le m\xC3\xAAme.\n");
        }
        return 0;
    }
    // 1.37.0 : revoir le code d'activation d'une licence en ligne (garde chiffre sur ce poste)
    if (a.command == "code-licence") {
        if (!need("--licence")) return 2;
        std::string code, w;
        if (!online::readCode(folder, a.one("--licence"), code, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("Code d'activation de " + a.one("--licence") + " : " + code + "\n");
        crypto::wipe(code.data(), code.size());
        return 0;
    }
    // 1.37.0 : le mail au client (le reglage, le mot de passe d'application, un essai)
    if (a.command == "mail-config" && !a.one("--microsoft").empty()) {   // 1.38.0 : Outlook, Hotmail, Microsoft 365
        if (!need("--expediteur") || !need("--id-application")) return 2;
        const std::string kind = a.one("--microsoft");
        if (kind != "outlook" && kind != "365") {
            complain("Refus\xC3\xA9 : --microsoft outlook (Outlook.com, Hotmail) ou --microsoft 365 (Microsoft 365)");
            return 2;
        }
        license_admin::mail::Config base;
        base.from = a.one("--expediteur");
        base.fromName = a.one("--nom");
        base.user = a.one("--identifiant").empty() ? base.from : a.one("--identifiant");
        base.clientId = a.one("--id-application");
        for (char& ch : base.clientId) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const auto cfg = license_admin::mail::microsoftPreset(kind == "outlook", base);
        std::string w;
        if (!license_admin::mail::saveConfig(folder, cfg, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("Mail r\xC3\xA9gl\xC3\xA9 : " + cfg.from + " par " + cfg.host + ":" + std::to_string(cfg.port) + " (connexion Microsoft).\n" +
            (license_admin::mail::microsoftConnected(folder) ? std::string("Essaie-le : mail-essai.\n") : std::string("Connecte-toi : mail-microsoft-connecter.\n")));
        return 0;
    }
    if (a.command == "mail-microsoft-connecter") {
        license_admin::mail::Config cfg;
        std::string w;
        if (!license_admin::mail::loadConfig(folder, cfg, &w) || cfg.auth != license_admin::mail::kAuthMicrosoft) {
            complain("Refus\xC3\xA9 : " + (w.empty() ? std::string("le mail n'est pas r\xC3\xA9gl\xC3\xA9 pour Microsoft") : w) + " (mail-config --microsoft d'abord)");
            return 1;
        }
        license_admin::mail::DeviceCode dc;
        if (!license_admin::mail::microsoftStart(cfg, dc, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say(license_admin::mail::microsoftInstructions(dc) + "\nJ'attends ta connexion (Ctrl+C pour arr\xC3\xAAter)...\n");
        const auto p = license_admin::mail::microsoftWait(folder, cfg, dc);
        if (p.state != license_admin::mail::PollState::Done) {
            complain(p.message.empty() ? std::string("La connexion Microsoft n'a pas abouti.") : p.message);
            return 1;
        }
        say("Connect\xC3\xA9 \xC3\xA0 Microsoft" + (p.account.empty() ? std::string(".") : " : " + p.account + ".") + " Essaie-le : mail-essai.\n");
        return 0;
    }
    if (a.command == "mail-microsoft-oublier") {
        std::string w;
        if (!license_admin::mail::microsoftForget(folder, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("Connexion Microsoft oubli\xC3\xA9" "e sur ce poste.\n");
        return 0;
    }
    if (a.command == "mail-config") {
        if (!need("--hote") || !need("--expediteur")) return 2;
        license_admin::mail::Config cfg;
        cfg.host = a.one("--hote");
        cfg.security = a.one("--securite").empty() ? std::string("starttls") : a.one("--securite");
        cfg.port = a.one("--port").empty() ? license_admin::mail::defaultPort(cfg.security) : std::atoi(a.one("--port").c_str());
        cfg.from = a.one("--expediteur");
        cfg.fromName = a.one("--nom");
        cfg.user = a.one("--identifiant").empty() ? cfg.from : a.one("--identifiant");
        std::string w;
        if (!license_admin::mail::saveConfig(folder, cfg, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("Mail r\xC3\xA9gl\xC3\xA9 : " + cfg.from + " par " + cfg.host + ":" + std::to_string(cfg.port) + ".\n" +
            (license_admin::mail::hasPassword(folder) ? std::string("Essaie-le : mail-essai.\n") : std::string("Il manque le mot de passe d'application : mail-mot-de-passe.\n")));
        return 0;
    }
    if (a.command == "mail-mot-de-passe") {
        std::fputs("Mot de passe d'application (rien ne s'affiche) : ", stderr);
        std::string pw = readSecretLine();
        std::string w;
        const bool ok = !pw.empty() && license_admin::mail::savePassword(folder, pw, &w);
        crypto::wipe(pw.data(), pw.size());
        if (!ok) {
            complain("Refus\xC3\xA9 : " + (w.empty() ? std::string("aucun mot de passe tap\xC3\xA9") : w));
            return 1;
        }
        say("Mot de passe gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste.\n");
        return 0;
    }
    if (a.command == "mail-essai") {
        license_admin::mail::Config cfg;
        std::string w;
        if (!license_admin::mail::loadConfig(folder, cfg, &w)) {
            complain("Refus\xC3\xA9 : " + w + " (mail-config d'abord)");
            return 1;
        }
        license_admin::mail::Message m;
        m.to = a.one("--a").empty() ? cfg.from : a.one("--a");
        m.subject = "Essai du mail de l'outil de l'\xC3\xA9" "diteur XPG";
        m.body = "Bonjour,\n\nCe mail d'essai est parti de ton outil de l'\xC3\xA9" "diteur (XPG-License-Admin) : le r\xC3\xA9glage marche.\n";
        const auto r = license_admin::mail::send(folder, m);
        if (!r.ok) {
            complain(r.message);
            return 1;
        }
        say(r.message + "\n");
        return 0;
    }
    if (a.command == "mail-oublier") {
        std::string w;
        if (!license_admin::mail::forget(folder, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("R\xC3\xA9glage du mail oubli\xC3\xA9 : aucun mail ne part plus.\n");
        return 0;
    }
    if (a.command == "kit-studios") {
        if (!need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        return showResult(online::studioKit(folder, key.get(), a.one("--sortie"), licensing::systemNow()));
    }
    // ---- 1.44.0 : les admins, les fichiers, les versions, la sauvegarde (le Workspace de l'application) ----
    static const std::vector<std::string> kWorkspaceCommands = {"admin-demander", "admin-etat",      "admin-accepter",        "admin-retirer",
                                                                "admin-droits",   "profil-enregistrer", "journal-relever",     "fichier-envoyer",
                                                                "version-publier", "sauvegarde-configurer", "sauvegarder", "restaurer",
                                                                // 1.46.0 : la boutique et les commandes
                                                                "boutique-etat", "boutique-config", "stripe-brancher", "commandes", "commande-livrer",
                                                                "commande-revoir", "commande-rembourser",
                                                                // 1.47.0 : les statistiques et le globe
                                                                "stats-serie", "stats-mises-a-jour", "stats-admins", "stats-releve", "globe", "globe-direct",
                                                                // 1.47.0 : la connexion au service, la derogation << Gerer le service >>
                                                                "service-connexion", "service-connexion-exporter", "derogation-accorder", "admin-lien",
                                                                "derogation-revoquer", "derogation-etat"};
    if (std::find(kWorkspaceCommands.begin(), kWorkspaceCommands.end(), a.command) != kWorkspaceCommands.end())
        return runWorkspaceCommand(a, folder);
    if (a.command.rfind("github-", 0) == 0 || a.command.rfind("passerelle-", 0) == 0) {
        complain("La commande " + a.command + " (1.31 \xC3\xA0 1.34) n'existe plus. L'archive se synchronise par le service des licences "
                 "(archive-cle, archive-synchroniser) et les demandes des clients y arrivent (service-demandes).\n\n" + usage());
        return 2;
    }
    if (a.command == "verifier") {
        if (a.positional.empty()) {
            complain("verifier : quel fichier ?\n\n" + usage());
            return 2;
        }
        std::string bytes;
        if (!readText(a.positional[0], bytes, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string keyWhy;
        auto key = loadPublisherKey(folder, &keyWhy);
        std::string text;
        const bool ok = describeFile(bytes, key.get(), text);
        say(text);
        return ok ? 0 : 1;
    }
    complain("commande inconnue : " + a.command + "\n\n" + usage());
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    core::hardenLibrarySearch();   // 1.32.0 : aucune DLL du dossier courant ni du PATH
    std::vector<std::string> args;
#ifdef _WIN32
    // Les arguments en UTF-8 (un dossier accentue passe), la console en UTF-8.
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; w && i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 1 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    if (w) LocalFree(w);
#else
    args.assign(argv + 1, argv + argc);
#endif
    return run(args);
}

