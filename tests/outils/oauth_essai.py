#!/usr/bin/env python3
# =============================================================================
#  tests/outils/oauth_essai.py - 1.38.0 : UN FAUX SERVEUR OAUTH 2.0 (comme
#  login.microsoftonline.com), sur ce poste, pour tests/admin_mail_test.cpp
#  (license_admin/Mail : le compte Microsoft de l'editeur, flux a code RFC 8628)
# -----------------------------------------------------------------------------
#  Python 3, la bibliotheque standard seulement (http.server, json, base64).
#  Ecoute sur 127.0.0.1, un port choisi par le systeme, en HTTP (le client
#  n'accepte http:// que vers ce poste : c'est fait pour les essais). JAMAIS le
#  vrai Microsoft : tout est FICTIF (jetons et codes en essai_jeton_...).
#
#    POST /{locataire}/oauth2/v2.0/devicecode   client_id, scope
#    POST /{locataire}/oauth2/v2.0/token        grant_type = device_code (client_id,
#                                               device_code) ou refresh_token
#                                               (client_id, refresh_token, scope)
#
#  LE SCENARIO DEPEND DU LOCATAIRE (celui de la demande de code) :
#    consumers, organizations, common, ...   "attente" une fois, puis les jetons
#    ralentir.essai.invalid       slow_down une fois, puis les jetons
#    refus.essai.invalid          authorization_declined
#    expire.essai.invalid         expired_token
#    public.essai.invalid         401 invalid_client AADSTS7000218 (flux clients publics a Non)
#    inconnue.essai.invalid       devicecode : 400 unauthorized_client AADSTS700016
#    sans-permission.essai.invalid  les jetons, mais sans SMTP.Send dans "scope"
#    fuite.essai.invalid          400 dont error_description RECITE le code d'appareil
#    autre-compte.essai.invalid   les jetons, id_token d'un autre compte (autre@exemple.invalid)
#    panne.essai.invalid          devicecode : 503 en HTML (pas du JSON)
#  LE RAFRAICHISSEMENT : un jeton de rafraichissement donne n'est valable
#  qu'UNE fois (la rotation : un nouveau a chaque fois, l'ancien refuse ensuite :
#  invalid_grant AADSTS70008), et pour l'application qui l'a recu. Cas fixes :
#    essai_jeton_rafraichir_consentement  invalid_grant AADSTS65001 (consentement)
#    essai_jeton_rafraichir_fixe          un jeton d'acces, SANS nouveau jeton de rafraichissement
#  Chaque requete est ajoutee a SORTIE (JSON : "requetes" - le point, le
#  locataire, les champs du formulaire, le statut rendu, la reponse), reecrit
#  d'un coup. Le port est ecrit dans PORTS ({"oauth": N}) une fois pret.
#  S'arrete sur SIGTERM, si son parent disparait, ou apres 5 min.
#
#    python3 oauth_essai.py SORTIE PORTS
# =============================================================================
import base64
import json
import os
import signal
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

DEVICE_GRANT = "urn:ietf:params:oauth:grant-type:device_code"
USER = "essai@exemple.invalid"

_lock = threading.Lock()
_journal = {"requetes": []}
_sortie = None
_compteur = [0]
_appareils = {}      # code d'appareil -> {"scenario", "client", "sondages"}
_rafraichir = {}     # jeton de rafraichissement valable -> client_id


def _ecrire():
    tmp = _sortie + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(_journal, f, ensure_ascii=False, indent=1)
    os.replace(tmp, _sortie)


def suivant():
    _compteur[0] += 1
    return _compteur[0]


def b64url(octets):
    return base64.urlsafe_b64encode(octets).decode("ascii").rstrip("=")


def id_token(client, compte):
    entete = b64url(json.dumps({"typ": "JWT", "alg": "none"}).encode())
    corps = b64url(json.dumps({
        "aud": client,
        "iss": "https://exemple.invalid/essai/v2.0",
        "preferred_username": compte,
        "name": "Editeur d'essai",
        "tid": "00000000-0000-4000-8000-0000000000aa",
    }).encode())
    return entete + "." + corps + ".essai"


def jetons(client, scenario, avec_rafraichir=True, portee=None):
    n = suivant()
    rep = {
        "token_type": "Bearer",
        "scope": portee or "https://outlook.office.com/SMTP.Send",
        "expires_in": 3600,
        "ext_expires_in": 3600,
        "access_token": "essai_jeton_acces_%d" % n,
    }
    if avec_rafraichir:
        r = "essai_jeton_rafraichir_%d" % n
        _rafraichir[r] = client
        rep["refresh_token"] = r
    if scenario is not None:
        compte = "autre@exemple.invalid" if scenario == "autre-compte.essai.invalid" else USER
        rep["id_token"] = id_token(client, compte)
    return rep


def erreur(code, aadsts, texte):
    return {
        "error": code,
        "error_description": "AADSTS%d: %s Trace ID: 00000000-0000-0000-0000-000000000000 Correlation ID: essai Timestamp: 2026-01-01 00:00:00Z"
                             % (aadsts, texte),
        "error_codes": [aadsts],
        "timestamp": "2026-01-01 00:00:00Z",
    }


class Gestion(BaseHTTPRequestHandler):
    server_version = "oauth-essai/1.38"

    def log_message(self, *args):
        pass

    def repondre(self, statut, corps, entree):
        if isinstance(corps, (dict, list)):
            donnees = json.dumps(corps).encode("utf-8")
            type_ = "application/json; charset=utf-8"
        else:
            donnees = corps.encode("utf-8")
            type_ = "text/html; charset=utf-8"
        entree["statut"] = statut
        entree["reponse"] = corps if isinstance(corps, dict) else {"texte": corps}
        with _lock:
            _journal["requetes"].append(entree)
            _ecrire()
        self.send_response(statut)
        self.send_header("Content-Type", type_)
        self.send_header("Content-Length", str(len(donnees)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(donnees)

    def do_GET(self):
        self.repondre(405, {"error": "invalid_request", "error_description": "POST seulement (essai)"},
                      {"point": "?", "chemin": self.path, "methode": "GET", "champs": {}})

    def do_POST(self):
        longueur = int(self.headers.get("Content-Length") or 0)
        brut = self.rfile.read(longueur).decode("utf-8", "replace") if longueur > 0 else ""
        champs = {k: v[0] for k, v in parse_qs(brut, keep_blank_values=True).items()}
        morceaux = self.path.split("?")[0].strip("/").split("/")
        entree = {
            "chemin": self.path,
            "methode": "POST",
            "type": self.headers.get("Content-Type", ""),
            "langue": self.headers.get("Accept-Language", ""),
            "champs": champs,
        }
        if len(morceaux) != 4 or morceaux[1:3] != ["oauth2", "v2.0"] or morceaux[3] not in ("devicecode", "token"):
            entree["point"] = "?"
            self.repondre(404, {"error": "invalid_request", "error_description": "chemin inconnu (essai)"}, entree)
            return
        locataire, point = morceaux[0], morceaux[3]
        entree["point"] = point
        entree["locataire"] = locataire
        with _lock:
            if point == "devicecode":
                statut, corps = self.code_appareil(locataire, champs)
            else:
                statut, corps = self.jeton(champs)
        self.repondre(statut, corps, entree)

    # ---- POST .../devicecode
    def code_appareil(self, locataire, champs):
        client = champs.get("client_id", "")
        if not client:
            return 400, erreur("invalid_request", 900144, "The request body must contain the following parameter: 'client_id'.")
        if locataire == "inconnue.essai.invalid":
            return 400, erreur("unauthorized_client", 700016, "Application with identifier '%s' was not found in the directory "
                                                              "'inconnue.essai.invalid'." % client)
        if locataire == "panne.essai.invalid":
            return 503, "<html><body>Service Unavailable (essai)</body></html>"
        n = suivant()
        code = "essai_jeton_appareil_%d" % n
        _appareils[code] = {"scenario": locataire, "client": client, "sondages": 0}
        utilisateur = "ESSAI%03d" % n
        return 200, {
            "user_code": utilisateur,
            "device_code": code,
            "verification_uri": "https://exemple.invalid/devicelogin",
            "expires_in": 900,
            "interval": 1,
            "message": "Pour vous connecter (essai), ouvrez https://exemple.invalid/devicelogin et entrez le code %s." % utilisateur,
        }

    # ---- POST .../token
    def jeton(self, champs):
        grant = champs.get("grant_type", "")
        client = champs.get("client_id", "")
        if grant == DEVICE_GRANT:
            code = champs.get("device_code", "")
            a = _appareils.get(code)
            if a is None or a["client"] != client:
                return 400, erreur("invalid_grant", 70000, "The provided value for the 'device_code' is not valid.")
            a["sondages"] += 1
            sc = a["scenario"]
            if sc == "refus.essai.invalid":
                return 400, erreur("authorization_declined", 70000, "The end user denied the authorization request.")
            if sc == "expire.essai.invalid":
                return 400, erreur("expired_token", 70020, "The provided value for the input parameter 'device_code' is not valid. "
                                                           "This device code has expired.")
            if sc == "public.essai.invalid":
                return 401, erreur("invalid_client", 7000218, "The request body must contain the following parameter: "
                                                              "'client_assertion' or 'client_secret'.")
            if sc == "fuite.essai.invalid":
                return 400, erreur("invalid_request", 90000, "Essai : le code %s est refuse." % code)
            if a["sondages"] == 1:
                if sc == "ralentir.essai.invalid":
                    return 400, erreur("slow_down", 70016, "OAuth 2.0 device flow error. Slow down (essai).")
                return 400, erreur("authorization_pending", 70016, "OAuth 2.0 device flow error. Authorization is pending. "
                                                                   "Continue polling.")
            del _appareils[code]   # un code ne sert qu'une fois
            portee = "openid profile offline_access" if sc == "sans-permission.essai.invalid" else \
                "https://outlook.office.com/SMTP.Send openid profile"
            return 200, jetons(client, sc, True, portee)
        if grant == "refresh_token":
            r = champs.get("refresh_token", "")
            if r == "essai_jeton_rafraichir_consentement":
                return 400, erreur("invalid_grant", 65001, "The user or administrator has not consented to use the application "
                                                           "with ID '%s' named 'XPG essai'." % client)
            if r == "essai_jeton_rafraichir_fixe":
                return 200, jetons(client, None, False)
            if _rafraichir.get(r) != client:
                return 400, erreur("invalid_grant", 70008, "The provided authorization code or refresh token has expired due to "
                                                           "inactivity.")
            del _rafraichir[r]   # la rotation : l'ancien ne vaut plus
            return 200, jetons(client, None, True)
        return 400, erreur("unsupported_grant_type", 70003, "The app requested an unsupported grant type '%s'." % grant)


def main():
    global _sortie
    if len(sys.argv) != 3:
        sys.stderr.write("usage : oauth_essai.py SORTIE PORTS\n")
        return 2
    _sortie, ports_fichier = sys.argv[1:3]
    signal.signal(signal.SIGTERM, lambda *_: os._exit(0))
    parent = os.getppid()
    serveur = ThreadingHTTPServer(("127.0.0.1", 0), Gestion)
    serveur.daemon_threads = True
    threading.Thread(target=serveur.serve_forever, daemon=True).start()
    with _lock:
        _ecrire()
    tmp = ports_fichier + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"oauth": serveur.server_address[1]}, f)
    os.replace(tmp, ports_fichier)
    fin = time.monotonic() + 300
    while time.monotonic() < fin and os.getppid() == parent:
        time.sleep(0.2)
    os._exit(0)


if __name__ == "__main__":
    sys.exit(main())
