#!/usr/bin/env python3
# =============================================================================
#  tests/session_e2e.py - 1.50.1 : LES SESSIONS DE BOUT EN BOUT, DEUX VRAIS STUDIO
# -----------------------------------------------------------------------------
#  La 1.50.0 a essaye repondre / reduire / copier au niveau du protocole et des
#  widgets ; le client : << pas par une vraie session entre deux PC >>. Ici, DEUX
#  PROCESSUS IHM-Studio sur ce PC, chacun avec son dossier de reglages, son
#  identite et sa licence (XPG_DOSSIER_COMMUN, HOME) - deux PC vus par les
#  sessions - qui rejouent un script (--script), par la boucle locale :
#
#    l'hote      cree un projet fictif (Essai_Session_E2E), demarre la session
#                (j'accepte chaque entree ; port libre, decouverte sur un port
#                libre), accepte l'equipier, voit son long message REDUIT, y
#                REPOND (<< Repondre >> de l'onglet Session, la barre << En
#                reponse a ... >>, le champ) ;
#    l'equipier  entre (la recherche sur 127.0.0.1), ecrit un long message (9
#                lignes), voit la reponse de l'hote AVEC SA CITATION (l'extrait
#                au-dessus du texte ; un clic : son message), son message reduit
#                (<< ... voir tout >>), le deballe, << Tout reduire >>, le COPIE
#                (Copier, avec l'auteur et l'heure, Ctrl+C) et relit le
#                presse-papiers, puis part.
#
#  SANS ECRAN : la construction Linux des essais n'a pas SDL ; le Studio tourne
#  entier avec XPG_SANS_ECRAN=1 (app/HeadlessRenderer.hpp : la vraie police
#  mesure, rien ne se dessine, le presse-papiers en memoire). Aucun serveur X.
#  Une construction sans SDL n'est jamais livree : ses licences ne sont pas armees
#  (App.cpp ; une session demande << Travail a plusieurs >>, l'offre ENTERPRISE).
#  Le service des licences : http://127.0.0.1:9 (rien n'y repond ; jamais le vrai).
#
#  SAUTE (<< SAUTE : >>, ctest le compte comme saute) : pas d'executable IHM-Studio
#  (construire la cible IHM-Studio), ou un executable qui ne tourne pas sans ecran.
#  Robuste : chaque Studio a son delai (240 s ; tue au-dela, par son PID) ; les
#  dossiers temporaires sont effaces (XPG_E2E_GARDER=1 : gardes, pour regarder) ;
#  le code d'une session n'est jamais ecrit (l'hote accepte chaque entree : pas
#  de code ; le journal est verifie : aucun code au format K7Q4-M2XP-T9).
#
#  Usage : session_e2e.py <IHM-Studio> <dossier des sources>
# =============================================================================
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

TIMEOUT = 240.0          # chaque Studio, au plus
START_TIMEOUT = 150.0    # l'hote : jusqu'a la session demarree

FIRST_RUN = """attendre 30
clic-id dialog.masterKey.ok
attendre 10
touche Escape
attendre 5
"""

LONG = ("Début du long message : la mise en service de la station de pompage\\n"
        "ligne 2 : vérifier les capteurs de niveau\\n"
        "ligne 3 : purger la pompe P2 avant le premier démarrage\\n"
        "ligne 4 : contrôler le sens de rotation\\n"
        "ligne 5 : régler la consigne à 2,5 m\\n"
        "ligne 6 : essayer l'arrêt d'urgence\\n"
        "ligne 7 : noter les valeurs dans le cahier\\n"
        "ligne 8 : prévenir l'exploitant\\n"
        "ligne 9 : fin de la liste")

HOST = FIRST_RUN + """action project.new
attendre 15
champ 1 "Essai_Session_E2E"
champ 2 "${XPG_E2E_PROJET}"
attendre 3
bouton "Créer"
attendre-projet
attendre 30
session-demarrer "Essai de bout en bout" accepter modifier port=0 decouverte=${XPG_E2E_DECOUVERTE}
session-attendre frappe
session-accepter
session-attendre membres 2
session-onglet
session-attendre message "Début du long message"
attendre 10
session-discussion
session-reduit 1 oui
session-repondre 1 "Bien reçu : je purge P2 (ta ligne 3) puis je règle la consigne."
session-attendre message "Vérifications faites"
attendre 10
session-discussion
session-cite 2 "Début du long message"
session-arreter
attendre 20
quitter
"""

GUEST = FIRST_RUN + """session-rejoindre 127.0.0.1 decouverte=${XPG_E2E_DECOUVERTE}
session-attendre dedans
attendre 30
session-onglet
session-ecrire "%s"
session-attendre message "Bien reçu"
attendre 10
session-discussion
session-cite 2 "Début du long message"
session-reduit 1 oui
session-deballer 1
session-reduit 1 non
session-reduire tout
session-reduit 1 oui
session-copier 1
session-presse-papiers "ligne 9 : fin de la liste"
session-copier 2 auteur
session-presse-papiers "Bien reçu : je purge P2"
session-copier 1 ctrl+c
session-presse-papiers "Début du long message"
session-ecrire "Vérifications faites, je pars."
attendre 30
session-quitter
session-attendre accueil
quitter
""" % LONG

# Ce que chaque journal doit dire (le texte tel que le script l'ecrit).
HOST_EXPECTS = [
    "session-demarrer : « Essai de bout en bout » démarrée",
    "session-accepter :",
    "session-reduit 1 : réduit (4 lignes sur 9",
    "session-repondre 1 : En réponse à ",
    "session-repondre 1 : envoyée (la barre : partie)",
    "session-cite 2 : « ",
    "session-arreter",
]
GUEST_EXPECTS = [
    "session-rejoindre : « Essai de bout en bout »",
    "session-attendre dedans : oui",
    "session-ecrire : envoyé",
    "session-cite 2 : « ",
    "Début du long message",
    "session-reduit 1 : réduit (4 lignes sur 9",
    "session-deballer 1 : déballé (9 lignes",
    "session-reduit 1 : déballé",
    "session-reduire tout : fait",
    "session-copier 2 : copié avec l'auteur et l'heure",
    "session-copier 1 : copié (Ctrl+C)",
    "session-presse-papiers : « Début du long message",
    "session-attendre accueil : oui",
]
# Le format d'un code de session (par Internet : K7Q4-M2XP-T9 ; le reseau du bureau : 6 a 10 caracteres apres << code >>).
CODE_RE = re.compile(r"\b[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{2}\b|\bcode [A-Z0-9]{4,}\b")


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("0.0.0.0", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def environment(root, name, extra):
    home = os.path.join(root, name)
    os.makedirs(home)
    env = dict(os.environ)
    for k in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XPG_DOSSIER_COMMUN"):
        env.pop(k, None)
    env.update({
        "HOME": home,
        "XDG_CONFIG_HOME": os.path.join(home, ".config"),
        "XDG_DATA_HOME": os.path.join(home, ".local", "share"),   # le dossier des licences de ce << PC >>
        "XPG_DOSSIER_COMMUN": os.path.join(home, "commun"),   # ses reglages, son identite, sa licence : un PC
        "XPG_SERVICE": "http://127.0.0.1:9",                  # jamais le vrai service
        "XPG_SANS_ECRAN": "1",
        "XPG_INSTANCES_MULTIPLES": "1",
        "LC_ALL": "C.UTF-8",
        "LANG": "C.UTF-8",
    })
    env.update(extra)
    return env, home


def launch(exe, src, root, name, script, extra):
    env, home = environment(root, name, extra)
    # Les ressources du logiciel (la police Inter, les bibliotheques) : celles des sources, par un lien (en lecture).
    try:
        os.symlink(os.path.join(src, "resources"), os.path.join(home, "resources"))
    except OSError:
        pass
    path = os.path.join(home, name + ".txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(script)
    log = open(os.path.join(home, name + ".log"), "wb")
    p = subprocess.Popen([exe, "--script", path, "--size", "1400x900"], cwd=home, env=env, stdout=log, stderr=subprocess.STDOUT)
    return p, log, os.path.join(home, name + ".log")


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def wait_for(p, path, needle, deadline):
    while time.time() < deadline:
        if needle in read(path):
            return True
        if p.poll() is not None:
            return needle in read(path)
        time.sleep(0.25)
    return False


def finish(p, deadline):
    while time.time() < deadline:
        if p.poll() is not None:
            return p.returncode
        time.sleep(0.25)
    p.kill()   # ce processus seul (son PID)
    p.wait()
    return None


def shown(text):
    # Le journal tel qu'on peut le montrer : un code (s'il y en avait un) cache.
    return CODE_RE.sub("***", text)


def tail(text, n=60):
    lines = []
    for l in shown(text).splitlines():
        if (l.startswith("[script]") or "ECHEC" in l or "chou" in l) and (not lines or lines[-1] != l):
            lines.append(l)   # une attente repete sa ligne a chaque image : une fois
    return "\n".join(lines[-n:])


def main():
    if len(sys.argv) < 3:
        print("usage : session_e2e.py <IHM-Studio> <dossier des sources>")
        return 2
    exe, src = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    if not os.path.isfile(exe) and os.path.isfile(exe + ".exe"):
        exe += ".exe"
    if not os.path.isfile(exe):
        print("SAUTE : pas d'exécutable %s (construire la cible IHM-Studio)" % exe)
        return 0
    keep = os.environ.get("XPG_E2E_GARDER") == "1"
    root = tempfile.mkdtemp(prefix="xpg_sessione2e_")
    procs, logs = [], []
    failures = []
    try:
        port = free_udp_port()
        extra = {"XPG_E2E_DECOUVERTE": str(port), "XPG_E2E_PROJET": os.path.join(root, "projets", "Essai_Session_E2E")}
        t0 = time.time()
        host, hlog_f, hlog = launch(exe, src, root, "hote", HOST, extra)
        procs.append(host)
        logs.append(hlog_f)
        print("l'hôte : PID %d (découverte sur le port %d)" % (host.pid, port))
        started = wait_for(host, hlog, "session-demarrer : «", t0 + START_TIMEOUT)
        text = read(hlog)
        if not started:
            if "compiled without SDL3" in text or "without SDL3 support" in text:
                print("SAUTE : cet IHM-Studio ne tourne pas sans écran (une construction d'avant la 1.50.1)")
                return 0
            print("ECHEC : la session de l'hôte n'a pas démarré en %.0f s" % START_TIMEOUT)
            print(tail(text))
            return 1
        if "démarrée" not in text.split("session-demarrer : «", 1)[1].split("\n", 1)[0]:
            print("ECHEC : l'hôte ne démarre pas la session")
            print(tail(text))
            return 1
        print("la session de l'hôte : démarrée après %.0f s" % (time.time() - t0))
        guest, glog_f, glog = launch(exe, src, root, "invite", GUEST, extra)
        procs.append(guest)
        logs.append(glog_f)
        print("l'équipier : PID %d" % guest.pid)
        deadline = time.time() + TIMEOUT
        rc_guest = finish(guest, deadline)
        rc_host = finish(host, deadline + 30)
        htext, gtext = read(hlog), read(glog)
        print("l'équipier : %s ; l'hôte : %s ; en tout %.0f s" % (
            "fini (code %d)" % rc_guest if rc_guest is not None else "TUE (trop long)",
            "fini (code %d)" % rc_host if rc_host is not None else "TUE (trop long)", time.time() - t0))
        if rc_guest != 0:
            failures.append("l'équipier : code %s" % rc_guest)
        if rc_host != 0:
            failures.append("l'hôte : code %s" % rc_host)
        for want in HOST_EXPECTS:
            if want not in htext:
                failures.append("l'hôte n'a pas dit : " + want)
        for want in GUEST_EXPECTS:
            if want not in gtext:
                failures.append("l'équipier n'a pas dit : " + want)
        for who, text in (("l'hôte", htext), ("l'équipier", gtext)):
            if CODE_RE.search(text):
                failures.append(who + " : un code de session est écrit dans le journal")
            print("%s : %d ligne(s) au journal du script" % (who, len([l for l in text.splitlines() if l.startswith("[script] ")])))
        # Ce que l'equipier a vu (la discussion, la citation, le presse-papiers) - au journal de l'essai.
        for l in shown(gtext).splitlines():
            if l.startswith("[script] session-") and not l.startswith("[script] session-attendre"):
                print("  équipier " + l[len("[script] "):])
        for l in shown(htext).splitlines():
            if l.startswith("[script] session-discussion") or l.startswith("[script]   ") and "message" not in l or l.startswith("[script] session-cite"):
                print("  hôte " + l[len("[script] "):])
        if failures:
            for f in failures:
                print("ECHEC : " + f)
            print("---- l'hôte ----")
            print(tail(htext))
            print("---- l'équipier ----")
            print(tail(gtext))
            return 1
        print("sessione2e : %d vérifications, 0 échec" % (len(HOST_EXPECTS) + len(GUEST_EXPECTS) + 4))
        return 0
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        for f in logs:
            f.close()
        if keep:
            print("gardé : " + root)
        else:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
