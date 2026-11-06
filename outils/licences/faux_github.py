#!/usr/bin/env python3
# =============================================================================
#  outils/licences/faux_github.py - faux GitHub local pour les essais
# -----------------------------------------------------------------------------
#  Imite la petite partie de l'API REST de GitHub (Git Data et Contents) que
#  l'outil d'administration des licences appelle, pour essayer son vrai
#  transport HTTP (libcurl sous Linux, WinHTTP sous Wine) sur 127.0.0.1 sans
#  toucher au vrai GitHub. Bibliotheque standard seulement, tout en memoire.
#
#      python3 outils/licences/faux_github.py --port 8765 --jeton JETON \
#          --depot proprietaire/nom [--branche main] [--vide] [--public] \
#          [--etat DOSSIER] [--deplacer-ref-une-fois]
#      python3 outils/licences/faux_github.py --auto-essai
#
#  --vide                   depot sans commit ni branche (409 comme GitHub) ;
#                           sinon un commit avec README.md ("depot de test")
#  --public                 GET /repos/{proprietaire}/{nom} dit "private": false
#  --etat DOSSIER           apres chaque ecriture, l'arbre de la branche est
#                           recopie dans DOSSIER, avec DOSSIER/.commits.txt
#                           ("sha message", du plus ancien au plus recent)
#  --deplacer-ref-une-fois  au premier PATCH de la ref, un "commit concurrent"
#                           (concurrent.txt) passe avant : le client recoit 422,
#                           comme si un autre poste publiait en meme temps
#
#  N'ecoute que sur 127.0.0.1. Le jeton n'est jamais affiche ni journalise :
#  une ligne par requete sur stderr, "METHODE chemin -> code".
# =============================================================================
import argparse
import base64
import binascii
import hashlib
import hmac
import io
import json
import os
import re
import shutil
import socketserver
import sys
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DATE_ISO = "2026-11-06T12:00:00Z"
DATE_GIT = "1793966400 +0000"
AUTEUR = {"name": "Faux GitHub", "email": "faux-github@127.0.0.1"}
DOC = "https://docs.github.com/rest"
DEPOT_VIDE = "Git Repository is empty."
MODES_BLOB = ("100644", "100755", "120000")
MODES_ARBRE = ("040000", "40000")


class ErreurApi(Exception):
    def __init__(self, code, message):
        Exception.__init__(self, message)
        self.code = code
        self.message = message


def hacher_objet(genre, contenu):
    """Identifiant git : sha1 de "<genre> <taille>\\0" + contenu."""
    entete = ("%s %d" % (genre, len(contenu))).encode("ascii") + b"\0"
    return hashlib.sha1(entete + contenu).hexdigest()


def base64_github(donnees):
    """Base64 coupe toutes les 60 colonnes, chaque ligne finie par \\n."""
    brut = base64.b64encode(donnees).decode("ascii")
    return "".join(brut[i:i + 60] + "\n" for i in range(0, len(brut), 60))


def decoder_base64(texte):
    if not isinstance(texte, str):
        raise ErreurApi(422, "Invalid request.\n\n\"content\" is not a string.")
    try:
        return base64.b64decode("".join(texte.split()).encode("ascii"), validate=True)
    except (binascii.Error, ValueError, UnicodeEncodeError):
        raise ErreurApi(422, "Invalid request.\n\n\"content\" is not valid Base64.")


def verifier_chemin(chemin):
    if not isinstance(chemin, str) or not chemin:
        raise ErreurApi(422, "Invalid request.\n\n\"path\" wasn't supplied.")
    for partie in chemin.split("/"):
        if partie in ("", ".", "..", ".git") or "\0" in partie:
            raise ErreurApi(422, "tree.path contains a malformed path component")
    return chemin


def decouper(plat):
    """{chemin: (mode, sha)} -> fichiers {nom: (mode, sha)}, dossiers {nom: sous-plat}."""
    fichiers, dossiers = {}, {}
    for chemin, entree in plat.items():
        if "/" in chemin:
            tete, reste = chemin.split("/", 1)
            dossiers.setdefault(tete, {})[reste] = entree
        else:
            fichiers[chemin] = entree
    return fichiers, dossiers


def lire_json(octets):
    try:
        objet = json.loads(octets.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        raise ErreurApi(400, "Problems parsing JSON")
    if not isinstance(objet, dict):
        raise ErreurApi(400, "Problems parsing JSON")
    return objet


def decouper_depot(texte):
    parties = (texte or "").split("/")
    if len(parties) != 2 or not all(re.match(r"^[A-Za-z0-9._-]+$", p) for p in parties):
        raise ValueError(texte)
    return parties[0], parties[1]


def nettoyer(texte):
    return "".join(c if " " <= c <= "~" else "?" for c in texte)


def identite(corps, *cles):
    for cle in cles:
        valeur = corps.get(cle)
        if isinstance(valeur, dict) and isinstance(valeur.get("name"), str) \
                and isinstance(valeur.get("email"), str):
            return {"name": valeur["name"], "email": valeur["email"]}
    return None


# -----------------------------------------------------------------------------
#  Le depot en memoire. Toutes les methodes s'appellent sous self.verrou.
# -----------------------------------------------------------------------------
class Depot(object):
    def __init__(self, proprietaire, nom, branche, vide, prive, dossier_etat,
                 deplacer_ref_une_fois, racine_url):
        self.verrou = threading.RLock()
        self.proprietaire = proprietaire
        self.nom = nom
        self.branche = branche
        self.prive = prive
        self.dossier_etat = dossier_etat
        self.deplacer_ref_une_fois = deplacer_ref_une_fois
        self.base = "%s/repos/%s/%s" % (racine_url, proprietaire, nom)
        self.blobs = {}      # sha -> octets
        self.arbres = {}     # sha -> {chemin: (mode, sha du blob)}
        self.commits = {}    # sha -> {tree, parents, message, author}
        self.refs = {}       # branche -> sha du commit
        self.generation = 0  # augmente a chaque deplacement de branche
        self.ecrits = set()  # chemins recopies dans dossier_etat
        if not vide:
            blob = self._ajouter_blob(b"depot de test\n")
            arbre = self._ajouter_arbre({"README.md": ("100644", blob)})
            self._poser_ref(branche, self._ajouter_commit("Initial commit", arbre, []))

    # --- objets ---------------------------------------------------------------
    def _ajouter_blob(self, donnees):
        sha = hacher_objet("blob", donnees)
        self.blobs[sha] = donnees
        return sha

    def _ajouter_arbre(self, plat):
        """Hachage git reel ; chaque sous-dossier est enregistre aussi."""
        fichiers, dossiers = decouper(plat)
        entrees = [(n.encode("utf-8"), n.encode("utf-8"), m, s) for n, (m, s) in fichiers.items()]
        for nom, sous in dossiers.items():
            entrees.append((nom.encode("utf-8") + b"/", nom.encode("utf-8"), "40000",
                            self._ajouter_arbre(sous)))
        entrees.sort(key=lambda e: e[0])
        contenu = b"".join(m.encode("ascii") + b" " + n + b"\0" + bytes.fromhex(s)
                           for _, n, m, s in entrees)
        sha = hacher_objet("tree", contenu)
        self.arbres[sha] = dict(plat)
        return sha

    def _ajouter_commit(self, message, arbre, parents, auteur=None):
        auteur = auteur or AUTEUR
        ident = "%s <%s> %s" % (auteur["name"], auteur["email"], DATE_GIT)
        texte = "tree %s\n" % arbre + "".join("parent %s\n" % p for p in parents) \
            + "author %s\ncommitter %s\n\n%s" % (ident, ident, message)
        sha = hacher_objet("commit", texte.encode("utf-8", "surrogatepass"))
        self.commits[sha] = {"tree": arbre, "parents": list(parents),
                             "message": message, "author": dict(auteur)}
        return sha

    def _poser_ref(self, branche, sha):
        self.refs[branche] = sha
        self.generation += 1

    def _est_ancetre(self, ancetre, sha):
        vus, pile = set(), [sha]
        while pile:
            courant = pile.pop()
            if courant == ancetre:
                return True
            if courant not in vus:
                vus.add(courant)
                pile.extend(self.commits[courant]["parents"])
        return False

    def _historique(self, tete):
        """Les commits atteignables depuis tete, parents avant enfants."""
        ordre, vus, pile = [], set(), [(tete, False)]
        while pile:
            courant, fini = pile.pop()
            if fini:
                ordre.append(courant)
            elif courant not in vus:
                vus.add(courant)
                pile.append((courant, True))
                for parent in reversed(self.commits[courant]["parents"]):
                    pile.append((parent, False))
        return ordre

    def _exiger_non_vide(self):
        if not self.refs:
            raise ErreurApi(409, DEPOT_VIDE)

    @staticmethod
    def _retirer(plat, chemin):
        plat.pop(chemin, None)
        for cle in [k for k in plat if k.startswith(chemin + "/")]:
            del plat[cle]

    @staticmethod
    def _verifier_conflits(plat):
        for chemin in plat:
            parties = chemin.split("/")
            for i in range(1, len(parties)):
                if "/".join(parties[:i]) in plat:
                    raise ErreurApi(422, "tree.path %s conflicts with a file" % chemin)

    # --- JSON -----------------------------------------------------------------
    def _json_ref(self, branche):
        sha = self.refs[branche]
        return {"ref": "refs/heads/" + branche, "node_id": "x",
                "url": self.base + "/git/refs/heads/" + urllib.parse.quote(branche),
                "object": {"sha": sha, "type": "commit",
                           "url": self.base + "/git/commits/" + sha}}

    def _json_commit(self, sha):
        commit = self.commits[sha]
        ident = {"date": DATE_ISO, "name": commit["author"]["name"],
                 "email": commit["author"]["email"]}
        return {"sha": sha, "node_id": "x", "url": self.base + "/git/commits/" + sha,
                "author": ident, "committer": dict(ident), "message": commit["message"],
                "tree": {"sha": commit["tree"], "url": self.base + "/git/trees/" + commit["tree"]},
                "parents": [{"sha": p, "url": self.base + "/git/commits/" + p}
                            for p in commit["parents"]]}

    def _lister(self, plat, recursif, prefixe=""):
        fichiers, dossiers = decouper(plat)
        noms = sorted([(n.encode("utf-8"), n, False) for n in fichiers]
                      + [(n.encode("utf-8") + b"/", n, True) for n in dossiers])
        sortie = []
        for _, nom, est_dossier in noms:
            chemin = prefixe + nom
            if est_dossier:
                sha = self._ajouter_arbre(dossiers[nom])
                sortie.append({"path": chemin, "mode": "040000", "type": "tree", "sha": sha,
                               "url": self.base + "/git/trees/" + sha})
                if recursif:
                    sortie.extend(self._lister(dossiers[nom], True, chemin + "/"))
            else:
                mode, sha = fichiers[nom]
                sortie.append({"path": chemin, "mode": mode, "type": "blob", "sha": sha,
                               "size": len(self.blobs[sha]),
                               "url": self.base + "/git/blobs/" + sha})
        return sortie

    # --- points d'acces -------------------------------------------------------
    def lire_depot(self):
        return 200, {"id": 1, "node_id": "x", "name": self.nom,
                     "full_name": self.proprietaire + "/" + self.nom,
                     "owner": {"login": self.proprietaire},
                     "private": self.prive, "visibility": "private" if self.prive else "public",
                     "default_branch": self.branche, "url": self.base,
                     "permissions": {"admin": False, "push": True, "pull": True}}

    def lire_ref(self, branche):
        self._exiger_non_vide()
        if branche not in self.refs:
            raise ErreurApi(404, "Not Found")
        return 200, self._json_ref(branche)

    def lire_commit(self, sha):
        self._exiger_non_vide()
        if sha not in self.commits:
            raise ErreurApi(404, "Not Found")
        return 200, self._json_commit(sha)

    def lire_arbre(self, sha, recursif):
        self._exiger_non_vide()
        if sha in self.refs:                  # GitHub accepte aussi une branche
            sha = self.refs[sha]
        if sha in self.commits:               # ... ou un commit
            sha = self.commits[sha]["tree"]
        if sha not in self.arbres:
            raise ErreurApi(404, "Not Found")
        return 200, {"sha": sha, "url": self.base + "/git/trees/" + sha,
                     "tree": self._lister(self.arbres[sha], recursif), "truncated": False}

    def lire_blob(self, sha):
        self._exiger_non_vide()
        if sha not in self.blobs:
            raise ErreurApi(404, "Not Found")
        donnees = self.blobs[sha]
        return 200, {"sha": sha, "node_id": "x", "size": len(donnees),
                     "url": self.base + "/git/blobs/" + sha,
                     "encoding": "base64", "content": base64_github(donnees)}

    def creer_blob(self, corps):
        self._exiger_non_vide()
        contenu = corps.get("content")
        if not isinstance(contenu, str):
            raise ErreurApi(422, "Invalid request.\n\n\"content\" wasn't supplied.")
        codage = corps.get("encoding", "utf-8")
        if codage in ("utf-8", "utf8"):
            try:
                donnees = contenu.encode("utf-8")
            except UnicodeEncodeError:
                raise ErreurApi(422, "Invalid request.\n\n\"content\" is not valid UTF-8.")
        elif codage == "base64":
            donnees = decoder_base64(contenu)
        else:
            raise ErreurApi(422, "Invalid request.\n\n\"encoding\" must be utf-8 or base64.")
        sha = self._ajouter_blob(donnees)
        return 201, {"sha": sha, "url": self.base + "/git/blobs/" + sha}

    def creer_arbre(self, corps):
        self._exiger_non_vide()
        entrees = corps.get("tree")
        if not isinstance(entrees, list):
            raise ErreurApi(422, "Invalid request.\n\n\"tree\" wasn't supplied.")
        base = corps.get("base_tree")
        if base is None:
            plat = {}
        elif isinstance(base, str) and base in self.arbres:
            plat = dict(self.arbres[base])
        else:
            raise ErreurApi(422, "base_tree is not a valid tree")
        for entree in entrees:
            if not isinstance(entree, dict):
                raise ErreurApi(422, "Invalid request.\n\n\"tree\" must be an array of objects.")
            chemin = verifier_chemin(entree.get("path"))
            mode, genre = entree.get("mode"), entree.get("type", "blob")
            sha = entree.get("sha")
            if entree.get("content") is not None:
                if sha is not None or not isinstance(entree["content"], str) \
                        or genre != "blob" or mode not in MODES_BLOB:
                    raise ErreurApi(422, "tree.content needs type blob, a blob mode and no sha")
                self._retirer(plat, chemin)
                plat[chemin] = (mode, self._ajouter_blob(entree["content"].encode("utf-8", "surrogatepass")))
            elif "sha" in entree and sha is None:
                self._retirer(plat, chemin)
            elif isinstance(sha, str) and genre == "blob":
                if mode not in MODES_BLOB:
                    raise ErreurApi(422, "tree.mode %r is not a blob mode" % (mode,))
                if sha not in self.blobs:
                    raise ErreurApi(422, "tree.sha %s is not a valid blob" % sha)
                self._retirer(plat, chemin)
                plat[chemin] = (mode, sha)
            elif isinstance(sha, str) and genre == "tree":
                if mode not in MODES_ARBRE or sha not in self.arbres:
                    raise ErreurApi(422, "tree.sha %s is not a valid tree" % sha)
                self._retirer(plat, chemin)
                for sous, valeur in self.arbres[sha].items():
                    plat[chemin + "/" + sous] = valeur
            else:
                raise ErreurApi(422, "Must supply either tree.sha or tree.content")
        self._verifier_conflits(plat)
        sha = self._ajouter_arbre(plat)
        return 201, {"sha": sha, "url": self.base + "/git/trees/" + sha,
                     "tree": self._lister(plat, False), "truncated": False}

    def creer_commit(self, corps):
        self._exiger_non_vide()
        message = corps.get("message")
        if not isinstance(message, str):
            raise ErreurApi(422, "Invalid request.\n\n\"message\" wasn't supplied.")
        arbre = corps.get("tree")
        if not isinstance(arbre, str) or arbre not in self.arbres:
            raise ErreurApi(422, "Tree SHA does not exist")
        parents = corps.get("parents", [])
        if not isinstance(parents, list) or \
                not all(isinstance(p, str) and p in self.commits for p in parents):
            raise ErreurApi(422, "Parent SHA does not exist or is not a commit object")
        sha = self._ajouter_commit(message, arbre, parents, identite(corps, "author", "committer"))
        return 201, self._json_commit(sha)

    def maj_ref(self, branche, corps):
        self._exiger_non_vide()
        if branche not in self.refs:
            raise ErreurApi(422, "Reference does not exist")
        if self.deplacer_ref_une_fois:
            self.deplacer_ref_une_fois = False
            tete = self.refs[branche]
            plat = dict(self.arbres[self.commits[tete]["tree"]])
            plat["concurrent.txt"] = ("100644", self._ajouter_blob(b"un autre poste\n"))
            self._poser_ref(branche, self._ajouter_commit(
                "commit concurrent", self._ajouter_arbre(plat), [tete]))
        sha, force = corps.get("sha"), corps.get("force", False)
        if not isinstance(sha, str) or not isinstance(force, bool):
            raise ErreurApi(422, "Invalid request.\n\n\"sha\" wasn't supplied.")
        if sha not in self.commits:
            raise ErreurApi(422, "Object does not exist")
        if not force and not self._est_ancetre(self.refs[branche], sha):
            raise ErreurApi(422, "Update is not a fast forward")
        self._poser_ref(branche, sha)
        return 200, self._json_ref(branche)

    def ecrire_contenu(self, chemin, corps):
        verifier_chemin(chemin)
        message, branche = corps.get("message"), corps.get("branch") or self.branche
        if not isinstance(message, str):
            raise ErreurApi(422, "Invalid request.\n\n\"message\" wasn't supplied.")
        if "content" not in corps:
            raise ErreurApi(422, "Invalid request.\n\n\"content\" wasn't supplied.")
        if not isinstance(branche, str):
            raise ErreurApi(422, "Invalid request.\n\n\"branch\" is not a string.")
        donnees = decoder_base64(corps["content"])
        auteur = identite(corps, "committer", "author")
        if not self.refs:
            blob = self._ajouter_blob(donnees)
            arbre = self._ajouter_arbre({chemin: ("100644", blob)})
            self.branche = branche            # la premiere branche devient celle par defaut
            self._poser_ref(branche, self._ajouter_commit(message, arbre, [], auteur))
            return 201, self._json_contenu(chemin, blob, self.refs[branche])
        if branche not in self.refs:
            raise ErreurApi(404, "Branch %s not found" % branche)
        tete = self.refs[branche]
        plat = dict(self.arbres[self.commits[tete]["tree"]])
        existant = plat.get(chemin)
        if existant is not None and corps.get("sha") != existant[1]:
            raise ErreurApi(409, "%s does not match %s" % (chemin, corps.get("sha") or ""))
        if existant is None and any(k.startswith(chemin + "/") for k in plat):
            raise ErreurApi(422, "%s is a directory" % chemin)
        blob = self._ajouter_blob(donnees)
        plat[chemin] = (existant[0] if existant else "100644", blob)
        self._verifier_conflits(plat)
        commit = self._ajouter_commit(message, self._ajouter_arbre(plat), [tete], auteur)
        self._poser_ref(branche, commit)
        return (200 if existant else 201), self._json_contenu(chemin, blob, commit)

    def _json_contenu(self, chemin, blob, commit):
        return {"content": {"name": chemin.rsplit("/", 1)[-1], "path": chemin, "sha": blob,
                            "size": len(self.blobs[blob]), "type": "file",
                            "url": self.base + "/contents/" + urllib.parse.quote(chemin),
                            "git_url": self.base + "/git/blobs/" + blob},
                "commit": self._json_commit(commit)}

    # --- recopie de l'etat sur disque ----------------------------------------
    def ecrire_etat(self, journaliser):
        if not self.dossier_etat:
            return
        racine = os.path.realpath(self.dossier_etat)
        tete = self.refs.get(self.branche)
        plat = self.arbres[self.commits[tete]["tree"]] if tete else {}

        def cible(chemin):
            complet = os.path.realpath(os.path.join(racine, *chemin.split("/")))
            return complet if complet.startswith(racine + os.sep) else None

        try:
            os.makedirs(racine, exist_ok=True)
            for chemin in sorted(self.ecrits - set(plat), reverse=True):
                fichier = cible(chemin)
                if fichier and (os.path.isfile(fichier) or os.path.islink(fichier)):
                    os.remove(fichier)
                    dossier = os.path.dirname(fichier)
                    while dossier != racine and dossier.startswith(racine + os.sep):
                        try:
                            os.rmdir(dossier)
                        except OSError:
                            break
                        dossier = os.path.dirname(dossier)
            ecrits = set()
            for chemin in sorted(plat):
                fichier = cible(chemin)
                if fichier is None:
                    journaliser("faux_github : etat : chemin ignore " + nettoyer(chemin))
                    continue
                os.makedirs(os.path.dirname(fichier), exist_ok=True)
                with open(fichier, "wb") as f:
                    f.write(self.blobs[plat[chemin][1]])
                ecrits.add(chemin)
            self.ecrits = ecrits
            lignes = ["%s %s\n" % (c, self.commits[c]["message"].replace("\r", "")
                                   .replace("\n", "\\n"))
                      for c in (self._historique(tete) if tete else [])]
            with open(os.path.join(racine, ".commits.txt"), "w", encoding="utf-8",
                      errors="replace", newline="\n") as f:
                f.writelines(lignes)
        except OSError as e:
            journaliser("faux_github : etat : erreur d'ecriture (%s)" % nettoyer(str(e.strerror or e)))


# -----------------------------------------------------------------------------
#  Le serveur HTTP
# -----------------------------------------------------------------------------
class Gestionnaire(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "FauxGitHub/1.0"

    def log_request(self, code="-", size="-"):
        pass

    def log_message(self, format, *args):
        self.server.journaliser("faux_github : " + nettoyer(format % args))

    def _lire_corps(self):
        if "chunked" in (self.headers.get("Transfer-Encoding") or "").lower():
            morceaux = []
            while True:
                taille = int(self.rfile.readline(65537).split(b";")[0].strip(), 16)
                if taille == 0:
                    while self.rfile.readline(65537) not in (b"\r\n", b"\n", b""):
                        pass
                    return b"".join(morceaux)
                morceaux.append(self.rfile.read(taille))
                self.rfile.readline(65537)
        taille = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(taille) if taille > 0 else b""

    def _traiter(self):
        try:
            corps = self._lire_corps()
        except (ValueError, OSError):
            self.close_connection = True
            code, objet = 400, {"message": "Problems parsing request body", "documentation_url": DOC}
        else:
            code, objet = self.server.repondre(self.command, self.path,
                                               self.headers.get("Authorization"), corps)
        donnees = json.dumps(objet, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(donnees)))
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(donnees)
        self.server.journaliser("%s %s -> %d" % (
            self.command, nettoyer(urllib.parse.urlsplit(self.path).path), code))

    do_GET = do_HEAD = do_POST = do_PUT = do_PATCH = do_DELETE = do_OPTIONS = _traiter


class FauxGitHub(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port, jeton, depot, branche="main", vide=False, public=False,
                 etat=None, deplacer_ref_une_fois=False, journal=None):
        if not jeton:
            raise ValueError("jeton vide")
        proprietaire, nom = decouper_depot(depot)
        self.jeton = jeton.encode("utf-8")
        self.journal = journal if journal is not None else sys.stderr
        self.verrou_journal = threading.Lock()
        ThreadingHTTPServer.__init__(self, ("127.0.0.1", port), Gestionnaire)
        self.depot = Depot(proprietaire, nom, branche, vide, not public, etat,
                           deplacer_ref_une_fois, "http://127.0.0.1:%d" % self.server_address[1])
        with self.depot.verrou:
            self.depot.ecrire_etat(self.journaliser)

    def server_bind(self):
        # pas de getfqdn() (resolution de nom inutile sur 127.0.0.1)
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]

    def journaliser(self, ligne):
        with self.verrou_journal:
            try:
                self.journal.write(ligne + "\n")
                self.journal.flush()
            except (OSError, ValueError):
                pass

    def _autorise(self, valeur):
        parties = (valeur or "").strip().split(None, 1)
        if len(parties) != 2 or parties[0].lower() not in ("bearer", "token"):
            return False
        return hmac.compare_digest(parties[1].strip().encode("utf-8"), self.jeton)

    def repondre(self, methode, cible, autorisation, corps):
        if not self._autorise(autorisation):
            return 401, {"message": "Bad credentials", "documentation_url": DOC}
        depot = self.depot
        with depot.verrou:
            generation = depot.generation
            try:
                code, objet = self._router(methode, cible, corps)
            except ErreurApi as e:
                code, objet = e.code, {"message": e.message, "documentation_url": DOC}
            if depot.generation != generation or (
                    methode in ("POST", "PUT", "PATCH") and code < 300):
                depot.ecrire_etat(self.journaliser)
        return code, objet

    def _router(self, methode, cible, corps):
        depot = self.depot
        decoupe = urllib.parse.urlsplit(cible)
        requete = urllib.parse.parse_qs(decoupe.query, keep_blank_values=True)
        chemin = decoupe.path
        if not chemin.startswith("/repos/"):
            raise ErreurApi(404, "Not Found")
        parties = chemin[len("/repos/"):].split("/", 2)
        if len(parties) < 2 or \
                urllib.parse.unquote(parties[0]).lower() != depot.proprietaire.lower() or \
                urllib.parse.unquote(parties[1]).lower() != depot.nom.lower():
            raise ErreurApi(404, "Not Found")
        reste = parties[2] if len(parties) == 3 else ""
        methode = "GET" if methode == "HEAD" else methode

        def suite(prefixe):
            if reste.startswith(prefixe) and len(reste) > len(prefixe):
                return urllib.parse.unquote(reste[len(prefixe):])
            return None

        if methode == "GET":
            if reste == "":
                return depot.lire_depot()
            for prefixe in ("git/ref/heads/", "git/refs/heads/"):
                if suite(prefixe):
                    return depot.lire_ref(suite(prefixe))
            for prefixe, fonction in (("git/commits/", depot.lire_commit),
                                      ("git/blobs/", depot.lire_blob)):
                if suite(prefixe) and "/" not in suite(prefixe):
                    return fonction(suite(prefixe))
            if suite("git/trees/"):
                return depot.lire_arbre(suite("git/trees/"), "recursive" in requete)
        elif methode == "POST":
            fonctions = {"git/blobs": depot.creer_blob, "git/trees": depot.creer_arbre,
                         "git/commits": depot.creer_commit}
            if reste in fonctions:
                return fonctions[reste](lire_json(corps))
        elif methode == "PATCH":
            if suite("git/refs/heads/"):
                return depot.maj_ref(suite("git/refs/heads/"), lire_json(corps))
        elif methode == "PUT":
            if suite("contents/"):
                return depot.ecrire_contenu(suite("contents/"), lire_json(corps))
        raise ErreurApi(404, "Not Found")


# -----------------------------------------------------------------------------
#  Auto-essai : le serveur dans un fil, des appels urllib, sans reseau exterieur
# -----------------------------------------------------------------------------
def auto_essai():
    jeton = "jeton-d-essai-0123456789"
    R = "/repos/essai/registre"
    total = [0]
    echecs = []
    ouvreur = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    serveurs = []
    dossier = tempfile.mkdtemp(prefix="faux_github_")

    def controle(condition, libelle):
        total[0] += 1
        if not condition:
            echecs.append(libelle)

    def b64(donnees):
        return base64.b64encode(donnees).decode("ascii")

    def demarrer(**options):
        journal = io.StringIO()
        serveur = FauxGitHub(0, jeton, "essai/registre", journal=journal, **options)
        threading.Thread(target=serveur.serve_forever, daemon=True).start()
        serveurs.append((serveur, journal))
        return serveur

    def appel(serveur, methode, chemin, corps=None, jeton_=jeton, schema="Bearer"):
        url = "http://127.0.0.1:%d%s" % (serveur.server_address[1], chemin)
        donnees = None if corps is None else json.dumps(corps).encode("utf-8")
        req = urllib.request.Request(url, data=donnees, method=methode)
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        if jeton_ is not None:
            req.add_header("Authorization", "%s %s" % (schema, jeton_))
        if donnees is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with ouvreur.open(req, timeout=10) as reponse:
                return reponse.status, json.loads(reponse.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            texte = e.read()
            e.close()
            return e.code, (json.loads(texte.decode("utf-8")) if texte else {})

    def lire(chemin):
        try:
            with open(os.path.join(dossier, *chemin.split("/")), "rb") as f:
                return f.read()
        except OSError:
            return None

    try:
        controle(hacher_objet("blob", b"hello\n") == "ce013625030ba8dba906f756967f9e9ca394464a",
                 "hachage git d'un blob")
        controle(hacher_objet("tree", b"") == "4b825dc642cb6eb9a060e54bf8d69288fbee4904",
                 "hachage git de l'arbre vide")

        # 1. depot vide, initialise par Contents puis publie par Git Data
        s = demarrer(vide=True, etat=dossier)
        controle(s.server_address[0] == "127.0.0.1", "ecoute sur 127.0.0.1 seulement")
        c, r = appel(s, "GET", R)
        controle(c == 200 and r.get("private") is True and r.get("full_name") == "essai/registre"
                 and r.get("default_branch") == "main" and r.get("permissions", {}).get("push") is True,
                 "GET du depot : prive, branche par defaut")
        c, r = appel(s, "GET", "/repos/essai/autre")
        controle(c == 404 and r.get("message") == "Not Found", "GET d'un autre depot : 404")
        c, r = appel(s, "GET", R, jeton_="mauvais")
        controle(c == 401 and r.get("message") == "Bad credentials", "GET du depot, mauvais jeton : 401")
        c, r = appel(s, "GET", R + "/git/ref/heads/main")
        controle(c == 409 and r.get("message") == DEPOT_VIDE, "ref d'un depot vide : 409")
        c, r = appel(s, "POST", R + "/git/blobs", {"content": "eA==", "encoding": "base64"})
        controle(c == 409, "blob dans un depot vide : 409")
        c, r = appel(s, "GET", R + "/git/ref/heads/main", jeton_="mauvais")
        controle(c == 401 and r.get("message") == "Bad credentials"
                 and r.get("documentation_url") == DOC, "mauvais jeton : 401")
        c, r = appel(s, "GET", R + "/git/ref/heads/main", jeton_=None)
        controle(c == 401, "sans jeton : 401")
        c, r = appel(s, "GET", "/repos/essai/autre/git/ref/heads/main")
        controle(c == 404 and r.get("message") == "Not Found", "autre depot : 404")
        c, r = appel(s, "GET", R + "/branches")
        controle(c == 404, "point d'acces inconnu : 404")

        c, r = appel(s, "PUT", R + "/contents/registre/LISEZ-MOI.txt",
                     {"message": "initialisation", "content": b64(b"bonjour\n")})
        sha_lisez = hacher_objet("blob", b"bonjour\n")
        controle(c == 201 and r["content"]["sha"] == sha_lisez, "PUT contents sur depot vide : 201")
        c1 = r["commit"]["sha"]
        c, r = appel(s, "GET", R + "/git/ref/heads/main", schema="token")
        controle(c == 200 and r["object"]["sha"] == c1 and r["ref"] == "refs/heads/main",
                 "lecture de la ref (schema token)")
        c, r = appel(s, "GET", R + "/git/commits/" + c1)
        controle(c == 200 and r["parents"] == [] and r["message"] == "initialisation",
                 "lecture du commit")
        t1 = r["tree"]["sha"]
        c, r = appel(s, "GET", R + "/git/commits/" + "0" * 40)
        controle(c == 404, "commit inconnu : 404")

        binaire = bytes(range(256)) * 3
        c, r = appel(s, "POST", R + "/git/blobs", {"content": b64(binaire), "encoding": "base64"})
        controle(c == 201 and r["sha"] == hacher_objet("blob", binaire), "blob base64 : 201, sha git")
        sha_bin = r["sha"]
        c, r = appel(s, "GET", R + "/git/blobs/" + sha_bin)
        lignes = r.get("content", "").split("\n")
        controle(c == 200 and r["encoding"] == "base64" and r["size"] == len(binaire)
                 and len(lignes) > 2 and all(len(l) <= 60 for l in lignes)
                 and base64.b64decode("".join(lignes)) == binaire,
                 "blob relu : base64 coupe a 60, aller-retour exact")
        texte = "licence \u00e9diteur\n"
        c, r = appel(s, "POST", R + "/git/blobs", {"content": texte, "encoding": "utf-8"})
        controle(c == 201 and r["sha"] == hacher_objet("blob", texte.encode("utf-8")), "blob utf-8")
        c, r = appel(s, "POST", R + "/git/blobs", {"content": "pas du base64 !", "encoding": "base64"})
        controle(c == 422, "base64 invalide : 422")
        c, r = appel(s, "GET", R + "/git/blobs/" + "0" * 40)
        controle(c == 404, "blob inconnu : 404")

        c, r = appel(s, "POST", R + "/git/trees", {"base_tree": t1, "tree": [
            {"path": "registre/a/b/cle.bin", "mode": "100644", "type": "blob", "sha": sha_bin},
            {"path": "docs/note.txt", "mode": "100644", "type": "blob", "content": "note\n"}]})
        controle(c == 201 and sorted(e["path"] for e in r["tree"]) == ["docs", "registre"],
                 "creation d'arbre : 201, premier niveau")
        t2 = r["sha"]
        c, r = appel(s, "GET", R + "/git/trees/" + t2 + "?recursive=1")
        entrees = {e["path"]: e for e in r.get("tree", [])}
        controle(c == 200 and r["truncated"] is False
                 and entrees.get("registre/a", {}).get("type") == "tree"
                 and entrees.get("registre/a", {}).get("mode") == "040000"
                 and entrees.get("registre/a/b/cle.bin", {}).get("size") == len(binaire)
                 and entrees.get("registre/LISEZ-MOI.txt", {}).get("sha") == sha_lisez
                 and entrees.get("docs/note.txt", {}).get("sha") == hacher_objet("blob", b"note\n"),
                 "arbre recursif : dossiers et fichiers")
        c, r = appel(s, "GET", R + "/git/trees/" + t2)
        controle(c == 200 and sorted(e["path"] for e in r["tree"]) == ["docs", "registre"],
                 "arbre sans recursive : premier niveau")
        c, r = appel(s, "GET", R + "/git/trees/" + entrees["registre"]["sha"])
        controle(c == 200 and sorted(e["path"] for e in r["tree"]) == ["LISEZ-MOI.txt", "a"],
                 "sous-arbre lu par son sha")
        c, r = appel(s, "POST", R + "/git/trees", {"base_tree": "0" * 40, "tree": []})
        controle(c == 422, "base_tree inconnu : 422")
        c, r = appel(s, "POST", R + "/git/trees", {"tree": [
            {"path": "x", "mode": "100644", "type": "blob", "sha": "1" * 40}]})
        controle(c == 422, "blob inconnu dans un arbre : 422")

        c, r = appel(s, "POST", R + "/git/commits", {"message": "publication 1", "tree": t2, "parents": [c1]})
        controle(c == 201 and r["tree"]["sha"] == t2 and [p["sha"] for p in r["parents"]] == [c1],
                 "creation de commit : 201")
        c2 = r["sha"]
        c, r = appel(s, "POST", R + "/git/commits", {"message": "x", "tree": "2" * 40, "parents": [c1]})
        controle(c == 422, "commit sur arbre inconnu : 422")
        c, r = appel(s, "POST", R + "/git/commits", {"message": "x", "tree": t2, "parents": ["3" * 40]})
        controle(c == 422, "commit sur parent inconnu : 422")
        c, r = appel(s, "PATCH", R + "/git/refs/heads/main", {"sha": c2, "force": False})
        controle(c == 200 and r["object"]["sha"] == c2, "PATCH en avance rapide : 200")
        c, r = appel(s, "GET", R + "/git/ref/heads/main")
        controle(c == 200 and r["object"]["sha"] == c2, "ref deplacee")

        c, r = appel(s, "POST", R + "/git/commits", {"message": "branche morte", "tree": t1, "parents": [c1]})
        c3 = r["sha"]
        c, r = appel(s, "PATCH", R + "/git/refs/heads/main", {"sha": c3, "force": False})
        controle(c == 422 and r.get("message") == "Update is not a fast forward",
                 "PATCH sans avance rapide : 422")
        c, r = appel(s, "GET", R + "/git/ref/heads/main")
        controle(r["object"]["sha"] == c2, "ref inchangee apres le refus")
        c, r = appel(s, "PATCH", R + "/git/refs/heads/inconnue", {"sha": c2, "force": False})
        controle(c == 422 and r.get("message") == "Reference does not exist", "PATCH branche inconnue : 422")
        c, r = appel(s, "PATCH", R + "/git/refs/heads/main", {"sha": "4" * 40, "force": False})
        controle(c == 422, "PATCH commit inconnu : 422")

        c, r = appel(s, "POST", R + "/git/trees", {"base_tree": t2, "tree": [
            {"path": "docs/note.txt", "mode": "100644", "type": "blob", "sha": None}]})
        t4 = r["sha"]
        c, r = appel(s, "GET", R + "/git/trees/" + t4 + "?recursive=1")
        chemins = [e["path"] for e in r.get("tree", [])]
        controle(c == 200 and "docs" not in chemins and "docs/note.txt" not in chemins
                 and "registre/a/b/cle.bin" in chemins, "suppression par sha null")
        c, r = appel(s, "POST", R + "/git/commits", {"message": "suppression", "tree": t4, "parents": [c2]})
        c, r = appel(s, "PATCH", R + "/git/refs/heads/main", {"sha": r["sha"]})
        controle(c == 200, "PATCH sans force explicite : 200")
        c4 = r["object"]["sha"]

        chemin_lm = R + "/contents/registre/LISEZ-MOI.txt"
        c, r = appel(s, "PUT", chemin_lm, {"message": "maj", "content": b64(b"bonjour 2\n")})
        controle(c == 409 and "does not match" in r.get("message", ""), "PUT contents sans sha : 409")
        c, r = appel(s, "PUT", chemin_lm, {"message": "maj", "content": b64(b"bonjour 2\n"), "sha": "5" * 40})
        controle(c == 409, "PUT contents, mauvais sha : 409")
        c, r = appel(s, "PUT", chemin_lm, {"message": "maj LISEZ-MOI", "content": b64(b"bonjour 2\n"),
                                           "sha": sha_lisez, "branch": "main"})
        controle(c == 200 and [p["sha"] for p in r["commit"]["parents"]] == [c4],
                 "PUT contents, mise a jour : 200")
        c, r = appel(s, "PUT", R + "/contents/registre/nouveau.txt",
                     {"message": "nouveau fichier", "content": b64(b"neuf\n")})
        controle(c == 201, "PUT contents, nouveau fichier : 201")
        c_fin = r["commit"]["sha"]

        controle(lire("registre/LISEZ-MOI.txt") == b"bonjour 2\n" and lire("registre/nouveau.txt") == b"neuf\n"
                 and lire("registre/a/b/cle.bin") == binaire, "etat : fichiers publies recopies")
        controle(lire("docs/note.txt") is None and not os.path.isdir(os.path.join(dossier, "docs")),
                 "etat : fichier supprime retire")
        historique = (lire(".commits.txt") or b"").decode("utf-8").splitlines()
        controle(len(historique) == 5 and historique[0] == c1 + " initialisation"
                 and historique[-1] == c_fin + " nouveau fichier", "etat : .commits.txt")

        # 2. depot initialise, un autre poste publie au premier PATCH
        s2 = demarrer(deplacer_ref_une_fois=True, public=True)
        c, r = appel(s2, "GET", R)
        controle(c == 200 and r.get("private") is False, "--public : private false")
        c, r = appel(s2, "GET", R + "/git/ref/heads/main")
        h = r["object"]["sha"]
        c, r = appel(s2, "GET", R + "/git/commits/" + h)
        th = r["tree"]["sha"]
        c, r = appel(s2, "GET", R + "/git/trees/" + th + "?recursive=1")
        lisez = [e for e in r.get("tree", []) if e["path"] == "README.md"]
        c, r = appel(s2, "GET", R + "/git/blobs/" + (lisez[0]["sha"] if lisez else "x"))
        controle(c == 200 and base64.b64decode(r["content"]) == b"depot de test\n",
                 "depot initialise : README.md")
        c, r = appel(s2, "GET", R + "/git/ref/heads/autre")
        controle(c == 404, "branche inconnue : 404")

        def publier(base_commit, nom, texte_):
            _, rc = appel(s2, "GET", R + "/git/commits/" + base_commit)
            _, rb = appel(s2, "POST", R + "/git/blobs", {"content": texte_, "encoding": "utf-8"})
            _, rt = appel(s2, "POST", R + "/git/trees", {"base_tree": rc["tree"]["sha"], "tree": [
                {"path": nom, "mode": "100644", "type": "blob", "sha": rb["sha"]}]})
            _, rm = appel(s2, "POST", R + "/git/commits", {"message": "publie " + nom,
                                                          "tree": rt["sha"], "parents": [base_commit]})
            code, rr = appel(s2, "PATCH", R + "/git/refs/heads/main", {"sha": rm["sha"], "force": False})
            return code, rr, rm["sha"]

        c, r, _ = publier(h, "licences.csv", "v1\n")
        controle(c == 422 and r.get("message") == "Update is not a fast forward",
                 "--deplacer-ref-une-fois : premier PATCH refuse (422)")
        c, r = appel(s2, "GET", R + "/git/ref/heads/main")
        hx = r["object"]["sha"]
        c, r = appel(s2, "GET", R + "/git/commits/" + hx)
        controle(hx != h and r["message"] == "commit concurrent" and [p["sha"] for p in r["parents"]] == [h],
                 "commit concurrent sur la branche")
        c, r = appel(s2, "GET", R + "/git/trees/" + r["tree"]["sha"] + "?recursive=1")
        concurrent = [e for e in r.get("tree", []) if e["path"] == "concurrent.txt"]
        c, r = appel(s2, "GET", R + "/git/blobs/" + (concurrent[0]["sha"] if concurrent else "x"))
        controle(c == 200 and base64.b64decode(r["content"]) == b"un autre poste\n", "concurrent.txt")
        c, r, cb = publier(hx, "licences.csv", "v1\n")
        controle(c == 200 and r["object"]["sha"] == cb, "nouvel essai sur le commit concurrent : 200")
        c, r, cc = publier(cb, "licences.csv", "v2\n")
        controle(c == 200 and r["object"]["sha"] == cc, "PATCH suivant normal")

        # 3. depot vide, branche avec / et chemin encode
        s3 = demarrer(vide=True, branche="main")
        c, r = appel(s3, "PUT", R + "/contents/dossier%20x/f.txt",
                     {"message": "init", "content": b64(b"f\n"), "branch": "equipe/licences"})
        controle(c == 201 and r["content"]["path"] == "dossier x/f.txt", "PUT contents, chemin encode")
        c, r = appel(s3, "GET", R + "/git/ref/heads/equipe/licences")
        controle(c == 200 and r["ref"] == "refs/heads/equipe/licences", "branche avec /")
        c, r = appel(s3, "GET", R + "/git/ref/heads/main")
        controle(c == 404, "branche absente d'un depot non vide : 404")
        c, r = appel(s3, "GET", R)
        controle(c == 200 and r.get("default_branch") == "equipe/licences",
                 "la premiere branche devient celle par defaut")

        for serveur, journal in serveurs:
            texte_journal = journal.getvalue()
            controle(jeton not in texte_journal and "Bearer" not in texte_journal
                     and all(re.match(r"^[A-Z]+ \S+ -> \d{3}$", l) for l in texte_journal.splitlines()),
                     "journal sans jeton, une ligne par requete")
    except Exception as e:  # tout imprevu compte comme un echec
        echecs.append("exception %s : %s" % (type(e).__name__, nettoyer(str(e))))
    finally:
        for serveur, _ in serveurs:
            serveur.shutdown()
            serveur.server_close()
        shutil.rmtree(dossier, ignore_errors=True)

    for libelle in echecs:
        print("ECHEC : " + libelle)
    print("auto-essai : %d controles, %d echec%s" % (total[0], len(echecs), "s" if len(echecs) > 1 else ""))
    return 0 if not echecs else 1


def principal(argv=None):
    p = argparse.ArgumentParser(
        prog="faux_github.py",
        description="Faux GitHub local (API Git Data et Contents) pour essayer l'outil des "
                    "licences sans toucher au vrai GitHub. N'ecoute que sur 127.0.0.1.")
    p.add_argument("--port", type=int, default=8765,
                   help="port d'ecoute sur 127.0.0.1 (8765 ; 0 : un port libre, affiche au demarrage)")
    p.add_argument("--jeton", help="le seul jeton accepte (Authorization: Bearer JETON ou token JETON)")
    p.add_argument("--depot", help="le seul depot existant, proprietaire/nom")
    p.add_argument("--branche", default="main", help="branche par defaut (main)")
    p.add_argument("--vide", action="store_true", help="depot vide au depart : ni commit ni branche")
    p.add_argument("--public", action="store_true", help="le depot se dit public (\"private\": false)")
    p.add_argument("--etat", metavar="DOSSIER",
                   help="recopie l'arbre de la branche dans DOSSIER apres chaque ecriture, "
                        "avec DOSSIER/.commits.txt")
    p.add_argument("--deplacer-ref-une-fois", action="store_true",
                   help="au premier PATCH de la ref, un commit concurrent passe avant (422 au client)")
    p.add_argument("--auto-essai", action="store_true",
                   help="lance les controles internes puis quitte (code 0 : tout est bon)")
    a = p.parse_args(argv)
    if a.auto_essai:
        return auto_essai()
    if not a.jeton:
        p.error("--jeton est obligatoire")
    try:
        decouper_depot(a.depot)
    except ValueError:
        p.error("--depot attend proprietaire/nom")
    if not 0 <= a.port <= 65535 or not a.branche or a.branche.startswith("/") or a.branche.endswith("/"):
        p.error("--port ou --branche invalide")
    try:
        serveur = FauxGitHub(a.port, a.jeton, a.depot, a.branche, a.vide, a.public, a.etat,
                             a.deplacer_ref_une_fois)
    except OSError as e:
        sys.stderr.write("faux_github : impossible d'ecouter sur 127.0.0.1:%d (%s)\n"
                         % (a.port, nettoyer(str(e.strerror or e))))
        return 2
    sys.stderr.write("faux_github : ecoute sur http://127.0.0.1:%d - depot %s, branche %s, %s, %s\n" % (
        serveur.server_address[1], a.depot, a.branche, "public" if a.public else "prive",
        "vide" if a.vide else "un commit (README.md)"))
    sys.stderr.flush()
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("faux_github : arret\n")
    finally:
        serveur.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(principal())
