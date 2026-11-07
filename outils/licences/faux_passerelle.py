#!/usr/bin/env python3
# =============================================================================
#  outils/licences/faux_passerelle.py - 1.34.0 : un faux GitHub local pour la
#  PASSERELLE des licences (le depot public : fichiers, raw, issues)
# -----------------------------------------------------------------------------
#  Pour essayer le vrai transport HTTP des Studios et de l'outil de l'editeur
#  (WinHTTP sous Wine, libcurl sous Linux) sur 127.0.0.1, sans toucher au vrai
#  GitHub. Bibliotheque standard seulement, tout en memoire.
#
#      python3 -I outils/licences/faux_passerelle.py --port 0 \
#          --depot proprietaire/nom [--branche main] \
#          --jeton-editeur JETON --jeton-demandes JETON [--etat DOSSIER]
#      python3 -I outils/licences/faux_passerelle.py --auto-essai
#
#  Les adresses (base = http://127.0.0.1:PORT) :
#    <base>/raw/<proprietaire>/<nom>/<branche>/<chemin>     le fichier, SANS jeton
#    <base>/api/repos/<proprietaire>/<nom>                   public ; permissions selon le jeton
#    <base>/api/repos/.../contents/<chemin>                  GET (sans jeton) ; PUT, DELETE :
#                                                            le jeton de l'EDITEUR (sha exige
#                                                            pour remplacer : 409 sinon)
#    <base>/api/repos/.../issues                             POST, GET (state, per_page, page)
#    <base>/api/repos/.../issues/<n>                         GET, PATCH (state, body)
#    <base>/api/repos/.../issues/<n>/comments                POST, GET
#  Ecrire une issue : le jeton des DEMANDES ou celui de l'editeur ; le jeton
#  des demandes n'ecrit AUCUN fichier (403, comme un jeton a grain fin limite
#  aux issues).
#  --etat DOSSIER : apres chaque ecriture, les fichiers du depot sont recopies
#  dans DOSSIER/fichiers/ et les issues dans DOSSIER/issues.json.
#  Le port choisi est ecrit sur stdout ("port N"). Les jetons ne sont jamais
#  affiches : une ligne par requete sur stderr, "METHODE chemin -> code".
# =============================================================================
import argparse
import base64
import binascii
import hmac
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

DOC = "https://docs.github.com/rest"


class ErreurApi(Exception):
    def __init__(self, code, message):
        Exception.__init__(self, message)
        self.code = code
        self.message = message


def nettoyer(texte):
    return re.sub(r"[^\x20-\x7e]", "?", texte)[:300]


def verifier_chemin(chemin):
    if not chemin or chemin.startswith("/") or chemin.endswith("/") or "\\" in chemin:
        raise ErreurApi(422, "path is invalid")
    for morceau in chemin.split("/"):
        if morceau in ("", ".", ".."):
            raise ErreurApi(422, "path is invalid")


class Depot:
    def __init__(self, proprietaire, nom, branche, dossier_etat):
        self.proprietaire, self.nom, self.branche = proprietaire, nom, branche
        self.fichiers = {}      # chemin -> octets
        self.shas = {}          # chemin -> sha
        self.issues = []        # dicts
        self.commits = 0
        self.horloge = 0
        self.prochain_commentaire = 1000
        self.dossier_etat = dossier_etat
        self.verrou = threading.Lock()

    def maintenant(self):
        self.horloge += 1
        return "2026-11-07T10:%02d:%02dZ" % ((self.horloge // 60) % 60, self.horloge % 60)

    def json_issue(self, i):
        return {"number": i["number"], "title": i["title"], "body": i["body"], "state": i["state"],
                "comments": len(i["commentaires"]), "created_at": i["cree"], "updated_at": i["maj"],
                "html_url": "https://github.com/%s/%s/issues/%d" % (self.proprietaire, self.nom, i["number"]),
                "user": {"login": self.proprietaire}}

    def ecrire_etat(self):
        if not self.dossier_etat:
            return
        racine = os.path.abspath(self.dossier_etat)
        cible = os.path.join(racine, "fichiers")
        if os.path.isdir(cible):
            shutil.rmtree(cible)
        os.makedirs(cible, exist_ok=True)
        for chemin, donnees in self.fichiers.items():
            complet = os.path.abspath(os.path.join(cible, *chemin.split("/")))
            if not complet.startswith(cible + os.sep):
                continue
            os.makedirs(os.path.dirname(complet), exist_ok=True)
            with open(complet, "wb") as f:
                f.write(donnees)
        with open(os.path.join(racine, "issues.json"), "w", encoding="utf-8") as f:
            json.dump(self.issues, f, ensure_ascii=False, indent=1)


class Gestionnaire(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "FaussePasserelle/1.0"

    def log_request(self, code="-", size="-"):
        pass

    def log_message(self, format, *args):
        self.server.journaliser("faux_passerelle : " + nettoyer(format % args))

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
            code, donnees, type_ = 400, b'{"message":"Problems parsing request body"}', "application/json"
        else:
            code, donnees, type_ = self.server.repondre(self.command, self.path, self.headers.get("Authorization"), corps)
        self.send_response(code)
        self.send_header("Content-Type", type_)
        self.send_header("Content-Length", str(len(donnees)))
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(donnees)
        self.server.journaliser("%s %s -> %d" % (self.command, nettoyer(urllib.parse.urlsplit(self.path).path), code))

    do_GET = do_HEAD = do_POST = do_PUT = do_PATCH = do_DELETE = _traiter


class FaussePasserelle(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port, depot, branche, jeton_editeur, jeton_demandes, etat=None, journal=None):
        if not jeton_editeur or not jeton_demandes or jeton_editeur == jeton_demandes:
            raise ValueError("deux jetons differents sont exiges")
        parties = depot.split("/")
        if len(parties) != 2 or not all(re.match(r"^[A-Za-z0-9._-]+$", p) for p in parties):
            raise ValueError("depot : proprietaire/nom")
        self.jeton_editeur = jeton_editeur.encode("utf-8")
        self.jeton_demandes = jeton_demandes.encode("utf-8")
        self.journal = journal if journal is not None else sys.stderr
        self.verrou_journal = threading.Lock()
        ThreadingHTTPServer.__init__(self, ("127.0.0.1", port), Gestionnaire)
        self.depot = Depot(parties[0], parties[1], branche, etat)
        with self.depot.verrou:
            self.depot.ecrire_etat()

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]

    def journaliser(self, ligne):
        with self.verrou_journal:
            try:
                self.journal.write(ligne + "\n")
                self.journal.flush()
            except (OSError, ValueError):
                pass

    def _qui(self, valeur):
        """None : sans jeton ; "editeur", "demandes" ; False : un mauvais jeton."""
        if not valeur:
            return None
        parties = valeur.strip().split(None, 1)
        if len(parties) != 2 or parties[0].lower() not in ("bearer", "token"):
            return False
        jeton = parties[1].strip().encode("utf-8")
        if hmac.compare_digest(jeton, self.jeton_editeur):
            return "editeur"
        if hmac.compare_digest(jeton, self.jeton_demandes):
            return "demandes"
        return False

    def repondre(self, methode, cible, autorisation, corps):
        qui = self._qui(autorisation)
        depot = self.depot
        with depot.verrou:
            try:
                if qui is False:
                    raise ErreurApi(401, "Bad credentials")
                resultat = self._router(methode, cible, qui, corps)
            except ErreurApi as e:
                resultat = (e.code, {"message": e.message, "documentation_url": DOC})
            code, objet = resultat
            if methode in ("POST", "PUT", "PATCH", "DELETE") and code < 300:
                depot.ecrire_etat()
        if isinstance(objet, bytes):
            return code, objet, "text/plain; charset=utf-8"
        return code, json.dumps(objet, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8"

    def _router(self, methode, cible, qui, corps):
        d = self.depot
        decoupe = urllib.parse.urlsplit(cible)
        requete = urllib.parse.parse_qs(decoupe.query, keep_blank_values=True)
        chemin = urllib.parse.unquote(decoupe.path)
        methode = "GET" if methode == "HEAD" else methode
        prefixe_raw = "/raw/%s/%s/%s/" % (d.proprietaire, d.nom, d.branche)
        if chemin.startswith(prefixe_raw):
            if methode != "GET":
                raise ErreurApi(405, "raw : GET seulement")
            fichier = chemin[len(prefixe_raw):]
            if fichier not in d.fichiers:
                return 404, b"404: Not Found"
            return 200, d.fichiers[fichier]
        prefixe_api = "/api/repos/%s/%s" % (d.proprietaire, d.nom)
        if not (chemin == prefixe_api or chemin.startswith(prefixe_api + "/")):
            raise ErreurApi(404, "Not Found")
        reste = chemin[len(prefixe_api):]
        donnees = None
        if corps:
            try:
                donnees = json.loads(corps.decode("utf-8"))
            except (UnicodeDecodeError, ValueError):
                raise ErreurApi(400, "Problems parsing JSON")
            if not isinstance(donnees, dict):
                raise ErreurApi(400, "Problems parsing JSON")
        donnees = donnees or {}

        if reste == "" and methode == "GET":
            objet = {"full_name": "%s/%s" % (d.proprietaire, d.nom), "private": False, "default_branch": d.branche}
            if qui:
                objet["permissions"] = {"pull": True, "push": qui == "editeur", "admin": qui == "editeur"}
            return 200, objet

        if reste.startswith("/contents/"):
            fichier = reste[len("/contents/"):]
            verifier_chemin(fichier)
            if methode == "GET":
                if fichier not in d.fichiers:
                    raise ErreurApi(404, "Not Found")
                return 200, {"type": "file", "path": fichier, "sha": d.shas[fichier], "encoding": "base64",
                             "content": base64.encodebytes(d.fichiers[fichier]).decode("ascii")}
            if methode in ("PUT", "DELETE"):
                if qui is None:
                    raise ErreurApi(401, "Requires authentication")
                if qui != "editeur":
                    raise ErreurApi(403, "Resource not accessible by personal access token")
                if donnees.get("branch", d.branche) != d.branche:
                    raise ErreurApi(404, "Branch not found")
                existe = fichier in d.fichiers
                sha = donnees.get("sha") or ""
                if existe and sha != d.shas[fichier]:
                    raise ErreurApi(409, "%s does not match %s" % (fichier, sha))
                if methode == "DELETE":
                    if not existe:
                        raise ErreurApi(404, "Not Found")
                    del d.fichiers[fichier], d.shas[fichier]
                    d.commits += 1
                    return 200, {"commit": {"sha": "commit%d" % d.commits}}
                if not existe and sha:
                    raise ErreurApi(422, "sha wasn't supplied")
                try:
                    contenu = base64.b64decode(donnees.get("content") or "", validate=False)
                except (binascii.Error, ValueError):
                    raise ErreurApi(422, "content is not valid Base64")
                d.fichiers[fichier] = contenu
                d.commits += 1
                d.shas[fichier] = "sha%d" % d.commits
                return (200 if existe else 201), {"content": {"path": fichier, "sha": d.shas[fichier]},
                                                  "commit": {"sha": "commit%d" % d.commits}}
            raise ErreurApi(405, "methode")

        if reste == "/issues":
            if methode == "POST":
                if qui is None:
                    raise ErreurApi(401, "Requires authentication")
                titre = donnees.get("title") or ""
                if not isinstance(titre, str) or not titre:
                    raise ErreurApi(422, "title missing")
                moment = d.maintenant()
                i = {"number": len(d.issues) + 1, "title": titre, "body": donnees.get("body") or "", "state": "open",
                     "cree": moment, "maj": moment, "commentaires": []}
                d.issues.append(i)
                return 201, d.json_issue(i)
            if methode == "GET":
                etat = (requete.get("state") or ["open"])[0]
                par_page = max(1, min(100, int((requete.get("per_page") or ["30"])[0] or 30)))
                page = max(1, int((requete.get("page") or ["1"])[0] or 1))
                liste = [i for i in reversed(d.issues) if etat == "all" or i["state"] == etat]
                return 200, [d.json_issue(i) for i in liste[(page - 1) * par_page:page * par_page]]
            raise ErreurApi(405, "methode")

        m = re.match(r"^/issues/(\d+)(/comments)?$", reste)
        if m:
            numero = int(m.group(1))
            i = next((x for x in d.issues if x["number"] == numero), None)
            if i is None:
                raise ErreurApi(404, "Not Found")
            if not m.group(2):
                if methode == "GET":
                    return 200, d.json_issue(i)
                if methode == "PATCH":
                    if qui is None:
                        raise ErreurApi(401, "Requires authentication")
                    if "state" in donnees:
                        if donnees["state"] not in ("open", "closed"):
                            raise ErreurApi(422, "state")
                        i["state"] = donnees["state"]
                    if "body" in donnees:
                        i["body"] = donnees["body"]
                    i["maj"] = d.maintenant()
                    return 200, d.json_issue(i)
                raise ErreurApi(405, "methode")
            if methode == "POST":
                if qui is None:
                    raise ErreurApi(401, "Requires authentication")
                c = {"id": d.prochain_commentaire, "body": donnees.get("body") or "", "created_at": d.maintenant(),
                     "user": {"login": d.proprietaire}}
                d.prochain_commentaire += 1
                i["commentaires"].append(c)
                i["maj"] = c["created_at"]
                return 201, c
            if methode == "GET":
                return 200, list(i["commentaires"])
            raise ErreurApi(405, "methode")
        raise ErreurApi(404, "Not Found")


# -----------------------------------------------------------------------------
#  Auto-essai
# -----------------------------------------------------------------------------
def auto_essai():
    echecs = []
    total = [0]

    def controle(condition, libelle):
        total[0] += 1
        if not condition:
            echecs.append(libelle)
            print("  ECHEC  " + libelle)

    editeur, demandes = "essai_jeton_EDITEUR_auto", "essai_jeton_DEMANDES_auto"
    dossier = tempfile.mkdtemp(prefix="faux-passerelle-")
    journal = open(os.path.join(dossier, "journal.txt"), "w", encoding="utf-8")
    serveur = FaussePasserelle(0, "editeur-essai/passerelle-essai", "main", editeur, demandes,
                               os.path.join(dossier, "etat"), journal)
    fil = threading.Thread(target=serveur.serve_forever, daemon=True)
    fil.start()
    base = "http://127.0.0.1:%d" % serveur.server_address[1]

    def appel(methode, chemin, corps=None, jeton=None):
        donnees = json.dumps(corps).encode("utf-8") if corps is not None else None
        req = urllib.request.Request(base + chemin, data=donnees, method=methode)
        if jeton:
            req.add_header("Authorization", "Bearer " + jeton)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    api = "/api/repos/editeur-essai/passerelle-essai"
    raw = "/raw/editeur-essai/passerelle-essai/main/"
    try:
        code, _ = appel("GET", raw + "passerelle.xpgpas")
        controle(code == 404, "raw : un fichier absent rend 404")
        b64 = base64.b64encode(b"configuration").decode("ascii")
        code, _ = appel("PUT", api + "/contents/passerelle.xpgpas", {"message": "m", "content": b64}, demandes)
        controle(code == 403, "le jeton des demandes n'ecrit pas de fichier")
        code, _ = appel("PUT", api + "/contents/passerelle.xpgpas", {"message": "m", "content": b64})
        controle(code == 401, "sans jeton : 401")
        code, corps = appel("PUT", api + "/contents/passerelle.xpgpas", {"message": "m", "content": b64}, editeur)
        controle(code == 201, "le jeton de l'editeur cree le fichier")
        code, corps = appel("GET", raw + "passerelle.xpgpas")
        controle(code == 200 and corps == b"configuration", "raw sans jeton : le contenu")
        code, _ = appel("PUT", api + "/contents/passerelle.xpgpas", {"message": "m", "content": b64}, editeur)
        controle(code == 409, "remplacer sans le sha : 409")
        code, corps = appel("GET", api + "/contents/passerelle.xpgpas")
        sha = json.loads(corps)["sha"]
        code, _ = appel("PUT", api + "/contents/passerelle.xpgpas", {"message": "m", "content": b64, "sha": sha}, editeur)
        controle(code == 200, "remplacer avec le sha : 200")
        code, corps = appel("GET", api)
        controle(code == 200 and json.loads(corps)["private"] is False and "permissions" not in json.loads(corps), "le depot est public")
        code, corps = appel("GET", api, jeton=editeur)
        controle(json.loads(corps)["permissions"]["push"] is True, "l'editeur peut pousser")
        code, corps = appel("POST", api + "/issues", {"title": "t", "body": "b"})
        controle(code == 401, "une issue sans jeton : 401")
        code, corps = appel("POST", api + "/issues", {"title": "Demande", "body": "XPG"}, demandes)
        controle(code == 201 and json.loads(corps)["number"] == 1, "le jeton des demandes ouvre une issue")
        code, corps = appel("POST", api + "/issues/1/comments", {"body": "annulation"}, demandes)
        controle(code == 201, "un commentaire")
        code, corps = appel("PATCH", api + "/issues/1", {"state": "closed"}, demandes)
        controle(code == 200 and json.loads(corps)["state"] == "closed", "fermee")
        code, corps = appel("GET", api + "/issues?state=all&per_page=100&page=1", jeton=editeur)
        liste = json.loads(corps)
        controle(code == 200 and len(liste) == 1 and liste[0]["comments"] == 1, "la liste des issues")
        code, corps = appel("GET", api + "/issues?state=open", jeton=editeur)
        controle(json.loads(corps) == [], "aucune ouverte")
        code, _ = appel("GET", api, jeton="mauvais_jeton_essai")
        controle(code == 401, "un mauvais jeton : 401")
        with open(os.path.join(dossier, "etat", "issues.json"), encoding="utf-8") as f:
            controle(len(json.load(f)) == 1, "l'etat ecrit sur le disque")
    finally:
        serveur.shutdown()
        serveur.server_close()
        journal.close()
    with open(os.path.join(dossier, "journal.txt"), encoding="utf-8") as f:
        texte = f.read()
    controle(editeur not in texte and demandes not in texte, "aucun jeton dans le journal")
    shutil.rmtree(dossier, ignore_errors=True)
    print("%d controles, %d echec(s)" % (total[0], len(echecs)))
    return 1 if echecs else 0


def principal(argv=None):
    p = argparse.ArgumentParser(description="faux GitHub local pour la passerelle des licences")
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--depot", default="editeur-essai/passerelle-essai")
    p.add_argument("--branche", default="main")
    p.add_argument("--jeton-editeur")
    p.add_argument("--jeton-demandes")
    p.add_argument("--etat")
    p.add_argument("--auto-essai", action="store_true")
    a = p.parse_args(argv)
    if a.auto_essai:
        return auto_essai()
    if not a.jeton_editeur or not a.jeton_demandes:
        p.error("--jeton-editeur et --jeton-demandes sont exiges")
    serveur = FaussePasserelle(a.port, a.depot, a.branche, a.jeton_editeur, a.jeton_demandes, a.etat)
    print("port %d" % serveur.server_address[1], flush=True)
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(principal())
