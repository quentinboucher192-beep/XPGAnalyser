#!/usr/bin/env python3
# =============================================================================
#  tests/outils/smtp_essai.py - 1.37.0 : UN SERVEUR SMTP D'ESSAI, sur ce poste,
#  pour tests/admin_mail_test.cpp (license_admin/Mail : le courriel de l'editeur)
# -----------------------------------------------------------------------------
#  Python 3, la bibliotheque standard seulement (socket, ssl, threading, base64,
#  json). Ecoute sur 127.0.0.1, des ports choisis par le systeme :
#
#    starttls       en clair puis STARTTLS (TLS 1.2 ou 1.3), AUTH PLAIN LOGIN
#    tls            TLS des la connexion (TLS 1.2 au plus), AUTH LOGIN seulement
#    nonfiable      TLS des la connexion, un certificat auto-signe (d'aucune autorite)
#    sans_starttls  en clair, sans STARTTLS (le client doit refuser d'aller plus loin)
#    oauth          STARTTLS, AUTH XOAUTH2 seulement (pas de mot de passe)
#    microsoft      1.38.0 : STARTTLS, AUTH LOGIN XOAUTH2 comme smtp-mail.outlook.com :
#                   LOGIN est toujours refuse (535 5.7.139, mot de passe retire)
#    muet           accepte la connexion et ne dit rien
#
#  Un seul compte, FICTIF : essai@exemple.invalid / mdp-essai-local. AUTH est
#  refuse avant TLS. Un destinataire en @refuse.invalid est refuse (550).
#  L'identifiant oauth@exemple.invalid recoit la reponse de Microsoft quand
#  l'identification par mot de passe est retiree (535 5.7.139).
#
#  1.38.0 : AUTH XOAUTH2 (en deux temps, << AUTH XOAUTH2 >> puis 334, ou avec la
#  reponse initiale) : base64("user=U\x01auth=Bearer JETON\x01\x01"). Accepte
#  pour essai@exemple.invalid et un jeton d'ESSAI (essai_jeton_acces_...) ;
#  sinon le defi d'erreur de Microsoft/Google (334 + JSON en base64), la ligne
#  vide du client attendue, puis 535 5.7.3. Cas particuliers :
#    smtpauth@exemple.invalid   535 5.7.139 SmtpClientAuthentication is disabled
#                               (Microsoft 365 : SMTP AUTH coupe pour le locataire)
#    essai_jeton_acces_refuse   le defi d'erreur, puis 535 5.7.3
#    essai_jeton_acces_fuite    535 qui RECITE le jeton (le client doit le taire)
#  Le jeton recu et la forme sont gardes dans le message ("jeton", "forme").
#  Chaque message recu est ajoute a SORTIE (JSON : "messages", "evenements"),
#  reecrit d'un coup a chaque fois. Les ports sont ecrits dans PORTS (JSON) une
#  fois tout pret. S'arrete sur SIGTERM, si son parent disparait, ou apres 5 min.
#
#    python3 smtp_essai.py FIXTURES SORTIE PORTS
#  (FIXTURES : tests/fixtures/mail). Code 3 : le module ssl manque (l'essai saute).
# =============================================================================
import base64
import json
import os
import signal
import socket
import sys
import threading
import time

try:
    import ssl
except ImportError:  # un Python sans OpenSSL
    sys.stderr.write("module ssl absent\n")
    sys.exit(3)

USER = "essai@exemple.invalid"
PASSWORD = "mdp-essai-local"

_lock = threading.Lock()
_journal = {"messages": [], "evenements": []}
_sortie = None


def _ecrire():
    tmp = _sortie + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(_journal, f, ensure_ascii=False, indent=1)
    os.replace(tmp, _sortie)


def noter(cle, valeur):
    with _lock:
        _journal[cle].append(valeur)
        _ecrire()


class Fin(Exception):
    pass


class Session:
    def __init__(self, sock, nom, spec):
        self.sock = sock
        self.nom = nom
        self.spec = spec
        self.buf = b""
        self.tls = False
        self.ehlo = 0
        self.ehlo_tls = 0
        self.auth = None  # (mecanisme, utilisateur) une fois accepte
        self.de = None
        self.a = []
        self.jeton = ""   # XOAUTH2 : le jeton d'acces recu
        self.forme = ""   # XOAUTH2 : "deux_temps" ou "initiale"

    def envoyer(self, ligne):
        self.sock.sendall(ligne.encode("ascii") + b"\r\n")

    def ligne(self):
        while b"\n" not in self.buf:
            if len(self.buf) > 16384:  # 1.38.0 : une ligne XOAUTH2 (un vrai jeton : 2 a 4 Kio en base64)
                raise Fin("ligne trop longue")
            data = self.sock.recv(4096)
            if not data:
                raise Fin("ferme")
            self.buf += data
        ligne, self.buf = self.buf.split(b"\n", 1)
        return ligne.rstrip(b"\r").decode("utf-8", "replace")

    def passer_en_tls(self):
        self.sock = self.spec["contexte"].wrap_socket(self.sock, server_side=True)
        self.tls = True

    def capacites(self):
        caps = ["localhost bonjour"]
        if self.spec["starttls"] and not self.tls:
            caps.append("STARTTLS")
        if self.tls:
            caps.append("AUTH " + " ".join(self.spec["auth"]))
        caps.append("SIZE 10485760")
        caps.append("HELP")
        return caps

    def authentifier(self, args):
        parts = args.split(" ", 1)
        meca = parts[0].upper()
        initial = parts[1].strip() if len(parts) > 1 else None
        if not self.tls:
            noter("evenements", {"ecoute": self.nom, "evenement": "auth_sans_tls"})
            self.envoyer("530 5.7.0 TLS requis")
            return
        if meca not in self.spec["auth"]:
            self.envoyer("504 5.5.4 Mecanisme non propose")
            return
        if meca == "XOAUTH2":
            self.xoauth2(initial)
            return
        try:
            if meca == "PLAIN":
                if initial is None:
                    self.envoyer("334 ")
                    initial = self.ligne()
                champs = base64.b64decode(initial, validate=True).split(b"\0")
                if len(champs) != 3:
                    raise ValueError("PLAIN mal forme")
                user, mdp = champs[1].decode("utf-8"), champs[2].decode("utf-8")
            else:
                if initial is None:
                    self.envoyer("334 VXNlcm5hbWU6")
                    initial = self.ligne()
                user = base64.b64decode(initial, validate=True).decode("utf-8")
                self.envoyer("334 UGFzc3dvcmQ6")
                mdp = base64.b64decode(self.ligne(), validate=True).decode("utf-8")
        except (ValueError, UnicodeDecodeError):
            self.envoyer("501 5.5.2 Base64 invalide")
            return
        if user == "oauth@exemple.invalid" or self.spec.get("mdp_retire"):
            noter("evenements", {"ecoute": self.nom, "evenement": "mdp_retire", "mecanisme": meca})
            self.envoyer("535 5.7.139 Authentication unsuccessful, basic authentication is disabled. (essai)")
            return
        if user == USER and mdp == PASSWORD:
            self.auth = (meca, user)
            self.envoyer("235 2.7.0 Authentification reussie")
        else:
            noter("evenements", {"ecoute": self.nom, "evenement": "auth_refusee", "mecanisme": meca})
            self.envoyer("535 5.7.8 Username and Password not accepted (essai)")

    def xoauth2(self, initial):
        forme = "initiale"
        if initial is None:
            forme = "deux_temps"
            self.envoyer("334 ")
            initial = self.ligne()
        try:
            brut = base64.b64decode(initial, validate=True).decode("utf-8")
        except (ValueError, UnicodeDecodeError):
            self.envoyer("501 5.5.2 Base64 invalide")
            return
        champs = brut.split("\x01")
        if (len(champs) != 4 or champs[2] != "" or champs[3] != "" or not champs[0].startswith("user=")
                or not champs[1].startswith("auth=Bearer ")):
            noter("evenements", {"ecoute": self.nom, "evenement": "xoauth2_mal_forme"})
            self.envoyer("501 5.5.2 XOAUTH2 mal forme (essai)")
            return
        user = champs[0][len("user="):]
        jeton = champs[1][len("auth=Bearer "):]
        if user == "smtpauth@exemple.invalid":
            noter("evenements", {"ecoute": self.nom, "evenement": "smtp_auth_coupe"})
            self.envoyer("535 5.7.139 Authentication unsuccessful, SmtpClientAuthentication is disabled for the Tenant. "
                         "Visit https://aka.ms/smtp_auth_disabled for more information. (essai)")
            return
        if jeton == "essai_jeton_acces_fuite":
            noter("evenements", {"ecoute": self.nom, "evenement": "xoauth2_fuite"})
            self.envoyer("535 5.7.3 Authentication unsuccessful (essai : le jeton " + jeton + " est refuse)")
            return
        if user == USER and jeton.startswith("essai_jeton_acces_") and jeton != "essai_jeton_acces_refuse":
            self.auth = ("XOAUTH2", user)
            self.jeton = jeton
            self.forme = forme
            self.envoyer("235 2.7.0 Authentication successful")
            return
        noter("evenements", {"ecoute": self.nom, "evenement": "xoauth2_refuse", "utilisateur": user})
        defi = {"status": "401", "schemes": "bearer", "scope": "https://outlook.office.com/SMTP.Send"}
        self.envoyer("334 " + base64.b64encode(json.dumps(defi).encode("ascii")).decode("ascii"))
        reponse = self.ligne()
        noter("evenements", {"ecoute": self.nom, "evenement": "defi_vide_recu" if reponse == "" else "defi_non_vide"})
        self.envoyer("535 5.7.3 Authentication unsuccessful (essai)")

    def donnees(self):
        lignes = []
        while True:
            l = self.ligne()
            if l == ".":
                break
            if l.startswith("."):
                l = l[1:]
            lignes.append(l)
        noter("messages", {
            "ecoute": self.nom,
            "tls": self.tls,
            "version_tls": self.sock.version() if self.tls else "",
            "mecanisme": self.auth[0] if self.auth else "",
            "utilisateur": self.auth[1] if self.auth else "",
            "ehlo": self.ehlo,
            "ehlo_apres_tls": self.ehlo_tls,
            "jeton": self.jeton,
            "forme": self.forme,
            "de": self.de,
            "a": list(self.a),
            "donnees": "\r\n".join(lignes) + "\r\n",
        })
        self.de = None
        self.a = []
        self.envoyer("250 2.0.0 OK message accepte")

    def servir(self):
        if self.spec["mode"] == "tls":
            try:
                self.passer_en_tls()
            except (ssl.SSLError, OSError) as e:
                noter("evenements", {"ecoute": self.nom, "evenement": "tls_refuse", "detail": str(e)})
                return
        self.envoyer("220 localhost ESMTP essai XPG")
        while True:
            l = self.ligne()
            verbe, _, args = l.partition(" ")
            verbe = verbe.upper()
            args = args.strip()
            if verbe in ("EHLO", "HELO"):
                self.ehlo += 1
                if self.tls:
                    self.ehlo_tls += 1
                if verbe == "HELO":
                    self.envoyer("250 localhost")
                    continue
                caps = self.capacites()
                for c in caps[:-1]:
                    self.envoyer("250-" + c)
                self.envoyer("250 " + caps[-1])
            elif verbe == "STARTTLS":
                if not self.spec["starttls"] or self.tls:
                    self.envoyer("502 5.5.1 Commande non disponible")
                    continue
                if self.buf:
                    self.buf = b""  # rien d'envoye avant la poignee de main ne compte
                self.envoyer("220 2.0.0 Pret pour TLS")
                try:
                    self.passer_en_tls()
                except (ssl.SSLError, OSError) as e:
                    noter("evenements", {"ecoute": self.nom, "evenement": "tls_refuse", "detail": str(e)})
                    return
                self.auth = None
                self.jeton = ""
                self.de = None
                self.a = []
            elif verbe == "AUTH":
                self.authentifier(args)
            elif verbe == "MAIL":
                if not self.auth:
                    self.envoyer("530 5.7.0 Authentification requise")
                    continue
                self.de = args[5:].strip() if args.upper().startswith("FROM:") else args
                self.a = []
                self.envoyer("250 2.1.0 OK")
            elif verbe == "RCPT":
                if not self.de:
                    self.envoyer("503 5.5.1 MAIL d'abord")
                    continue
                dest = args[3:].strip() if args.upper().startswith("TO:") else args
                if dest.lower().rstrip(">").endswith("@refuse.invalid"):
                    self.envoyer("550 5.1.1 Destinataire inconnu (essai)")
                    continue
                self.a.append(dest)
                self.envoyer("250 2.1.5 OK")
            elif verbe == "DATA":
                if not self.a:
                    self.envoyer("503 5.5.1 RCPT d'abord")
                    continue
                self.envoyer("354 Fin par une ligne avec un point")
                self.donnees()
            elif verbe in ("RSET", "NOOP"):
                if verbe == "RSET":
                    self.de = None
                    self.a = []
                self.envoyer("250 2.0.0 OK")
            elif verbe == "QUIT":
                self.envoyer("221 2.0.0 Au revoir")
                return
            else:
                self.envoyer("500 5.5.2 Commande inconnue")


def servir_connexion(conn, nom, spec):
    conn.settimeout(30)
    try:
        if spec["mode"] == "muet":
            while conn.recv(4096):
                pass
            return
        Session(conn, nom, spec).servir()
    except (Fin, OSError, ssl.SSLError):
        pass
    finally:
        try:
            conn.close()
        except OSError:
            pass


def ecouter(nom, spec, ecoute):
    while True:
        conn, _ = ecoute.accept()
        threading.Thread(target=servir_connexion, args=(conn, nom, spec), daemon=True).start()


def contexte(cert, cle, tls12):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, cle)
    if tls12:
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    return ctx


def main():
    global _sortie
    if len(sys.argv) != 4:
        sys.stderr.write("usage : smtp_essai.py FIXTURES SORTIE PORTS\n")
        return 2
    fixtures, _sortie, ports_fichier = sys.argv[1:4]
    signal.signal(signal.SIGTERM, lambda *_: os._exit(0))
    parent = os.getppid()
    sur = contexte(os.path.join(fixtures, "serveur-essai.pem"), os.path.join(fixtures, "serveur-essai.key"), False)
    sur12 = contexte(os.path.join(fixtures, "serveur-essai.pem"), os.path.join(fixtures, "serveur-essai.key"), True)
    douteux = contexte(os.path.join(fixtures, "nonfiable-essai.pem"), os.path.join(fixtures, "nonfiable-essai.key"), False)
    specs = {
        "starttls": {"mode": "starttls", "starttls": True, "auth": ["PLAIN", "LOGIN"], "contexte": sur},
        "tls": {"mode": "tls", "starttls": False, "auth": ["LOGIN"], "contexte": sur12},
        "nonfiable": {"mode": "tls", "starttls": False, "auth": ["PLAIN", "LOGIN"], "contexte": douteux},
        "sans_starttls": {"mode": "starttls", "starttls": False, "auth": ["PLAIN", "LOGIN"], "contexte": sur},
        "oauth": {"mode": "starttls", "starttls": True, "auth": ["XOAUTH2"], "contexte": sur},
        "microsoft": {"mode": "starttls", "starttls": True, "auth": ["LOGIN", "XOAUTH2"], "contexte": sur, "mdp_retire": True},
        "muet": {"mode": "muet", "starttls": False, "auth": [], "contexte": None},
    }
    ports = {}
    for nom, spec in specs.items():
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(("127.0.0.1", 0))
        s.listen(16)
        ports[nom] = s.getsockname()[1]
        threading.Thread(target=ecouter, args=(nom, spec, s), daemon=True).start()
    with _lock:
        _ecrire()
    tmp = ports_fichier + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(ports, f)
    os.replace(tmp, ports_fichier)
    fin = time.monotonic() + 300
    while time.monotonic() < fin and os.getppid() == parent:
        time.sleep(0.2)
    os._exit(0)


if __name__ == "__main__":
    sys.exit(main())
