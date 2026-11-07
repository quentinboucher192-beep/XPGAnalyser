#!/usr/bin/env bash
# =============================================================================
#  outils/service/lancer-local.sh - 1.35.0 : le service des licences SUR CE
#  POSTE, pour les essais : le VRAI Worker (service/, dans workerd, par
#  `wrangler dev --local`), hors ligne, sans telemetrie, sans compte Cloudflare
# -----------------------------------------------------------------------------
#  lancer-local.sh <port|0> <dossier-d-etat> <cles-editeur-hexa[,autre...]|"">
#
#    port            0 : un port libre, choisi ici (repris si un autre programme
#                    le prend entre-temps) ; sinon ce port (127.0.0.1 seulement)
#    dossier-d-etat  cree s'il le faut ; il garde les tables SQLite du Durable
#                    Object (la cle du service, les licences...) d'un lancement
#                    a l'autre, et le journal (journal-wrangler.txt)
#    cles-editeur    PUBLISHER_KEYS : les cles PUBLIQUES de l'editeur (64
#                    hexadecimaux, separees par des virgules) ; "" : aucune
#
#  Quand GET /v1/etat repond, ecrit sur la sortie standard :
#      service pret sur le port N
#  puis reste la jusqu'a ce qu'on l'arrete (SIGTERM, SIGINT, SIGHUP : wrangler
#  et ses deux workerd s'arretent avec lui). Pour tout arreter d'un coup, le
#  lancer dans son propre groupe (setsid) et tuer le groupe ; ou arreter-local.sh
#  <dossier-d-etat> (le lanceur ecrit son pid dans lanceur.pid, son port dans port).
#
#  Variables facultatives (les essais seulement ; le Worker ne les ecoute que pour
#  une adresse 127.0.0.1 ou localhost - en ligne, elles ne font rien) :
#    XPG_DECALAGE_HORLOGE_ESSAI=<secondes>  l'horloge du service avancee (un bail
#                                           echu, un jeton expire) ; jamais en ligne
#    XPG_DECOUVERTE_JOURS=<n>               la duree de la Decouverte (30)
#    XPG_LIMITES_ESSAI=sans                 les limites par adresse et par PC levees (tous
#                                           les appels d'un essai viennent de 127.0.0.1)
#    XPG_SERVICE_DOSSIER=<dossier>          le dossier du Worker (defaut : service/
#                                           a la racine du depot)
#    XPG_SERVICE_ATTENTE=<secondes>         le delai de demarrage (60)
#
#  Codes de sortie : 2 mauvais arguments ; 3 Node.js ou wrangler manquent
#  (`npm ci` dans service/) ; 4 le service n'a pas demarre (le journal le dit).
#  Demarrage : 1,5 a 3 s. Memoire : ~450 Mo (node + 2 workerd).
#  Aucun reseau : les variables de proxy sont retirees, Request.cf n'est pas
#  telecharge, ni metriques ni rapports d'erreur.
# =============================================================================
set -u

usage() {
    echo "usage : lancer-local.sh <port|0> <dossier-d-etat> <cles-editeur-hexa,...|\"\">" >&2
    exit 2
}
[ $# -eq 3 ] || usage
PORT_DEMANDE="$1"
ETAT="$2"
CLES="$3"
case "$PORT_DEMANDE" in '' | *[!0-9]*) usage ;; esac
[ "$PORT_DEMANDE" -le 65535 ] || usage
case "$CLES" in *[!0-9a-fA-F,]*) echo "lancer-local.sh : les cles de l'editeur doivent etre en hexadecimal, separees par des virgules" >&2; exit 2 ;; esac
DECALAGE="${XPG_DECALAGE_HORLOGE_ESSAI:-0}"
[[ "$DECALAGE" =~ ^-?[0-9]{1,12}$ ]] || { echo "lancer-local.sh : XPG_DECALAGE_HORLOGE_ESSAI doit etre un nombre de secondes" >&2; exit 2; }
JOURS="${XPG_DECOUVERTE_JOURS:-30}"
case "$JOURS" in '' | *[!0-9]*) echo "lancer-local.sh : XPG_DECOUVERTE_JOURS doit etre un nombre de jours" >&2; exit 2 ;; esac
ATTENTE="${XPG_SERVICE_ATTENTE:-60}"
LIMITES="${XPG_LIMITES_ESSAI:-}"
[ -z "$LIMITES" ] || [ "$LIMITES" = "sans" ] || { echo "lancer-local.sh : XPG_LIMITES_ESSAI vaut \"sans\" ou rien" >&2; exit 2; }
[[ "$ATTENTE" =~ ^[0-9]{1,4}$ ]] || { echo "lancer-local.sh : XPG_SERVICE_ATTENTE doit etre un nombre de secondes" >&2; exit 2; }

ICI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE="${XPG_SERVICE_DOSSIER:-$ICI/../../service}"
SERVICE="$(cd "$SERVICE" 2>/dev/null && pwd)" || { echo "lancer-local.sh : le dossier du Worker est introuvable" >&2; exit 3; }

NODE="$(command -v node || true)"
if [ -z "$NODE" ]; then
    echo "lancer-local.sh : Node.js manque (il faut Node.js 22 ou plus recent)" >&2
    exit 3
fi
CLI="$SERVICE/node_modules/wrangler/wrangler-dist/cli.js"
if [ ! -f "$CLI" ]; then
    echo "lancer-local.sh : wrangler manque : lance \`npm ci\` dans $SERVICE" >&2
    exit 3
fi
if ! "$NODE" -e 'process.exit(+process.versions.node.split(".")[0] >= 22 ? 0 : 1)'; then
    echo "lancer-local.sh : Node.js $("$NODE" --version) est trop ancien (22 ou plus recent)" >&2
    exit 3
fi

mkdir -p "$ETAT" || { echo "lancer-local.sh : impossible de creer $ETAT" >&2; exit 2; }
ETAT="$(cd "$ETAT" && pwd)"
JOURNAL="$ETAT/journal-wrangler.txt"

port_libre() {
    "$NODE" -e 'const s=require("net").createServer();s.listen(0,"127.0.0.1",()=>{console.log(s.address().port);s.close()})'
}

# GET /v1/etat repond-il ? (curl s'il est la, sinon Node.js)
repond() {
    if command -v curl >/dev/null 2>&1; then
        curl -sf -o /dev/null --max-time 5 --noproxy '*' "http://127.0.0.1:$1/v1/etat" 2>/dev/null
    else
        "$NODE" -e 'fetch("http://127.0.0.1:"+process.argv[1]+"/v1/etat").then(r=>process.exit(r.ok?0:1),()=>process.exit(1))' "$1"
    fi
}

WPID=""
arreter() {
    if [ -n "$WPID" ] && kill -0 "$WPID" 2>/dev/null; then
        kill -TERM "$WPID" 2>/dev/null
        for _ in $(seq 1 30); do
            kill -0 "$WPID" 2>/dev/null || break
            sleep 0.1
        done
        # ce qui resterait (workerd) : tue
        pkill -KILL -P "$WPID" 2>/dev/null
        kill -KILL "$WPID" 2>/dev/null
    fi
    WPID=""
}
trap 'arreter; exit 0' TERM INT HUP
trap 'arreter; rm -f "$ETAT/lanceur.pid" "$ETAT/port" 2>/dev/null' EXIT

# Ni reseau, ni telemetrie, ni fichier hors du dossier d'etat.
export WRANGLER_SEND_METRICS=false WRANGLER_SEND_ERROR_REPORTS=false DO_NOT_TRACK=1 CLOUDFLARE_CF_FETCH_ENABLED=false
export WRANGLER_HIDE_BANNER=true WRANGLER_NO_SKILLS_UPDATE_PROMPTS=true
export WRANGLER_LOG_PATH="$ETAT/journaux" WRANGLER_REGISTRY_PATH="$ETAT/registre-dev"
export WRANGLER_CACHE_DIR="$ETAT/cache-wrangler"
unset HTTPS_PROXY https_proxy HTTP_PROXY http_proxy ALL_PROXY all_proxy npm_config_https_proxy GLOBAL_AGENT_HTTPS_PROXY

for essai in 1 2 3 4 5; do
    PORT="$PORT_DEMANDE"
    [ "$PORT" = "0" ] && PORT="$(port_libre)"
    : >"$JOURNAL"
    (cd "$SERVICE" && exec "$NODE" --no-warnings "$CLI" dev --local --ip 127.0.0.1 --port "$PORT" \
        --persist-to "$ETAT/etat" \
        --var "PUBLISHER_KEYS:$CLES" --var "DECOUVERTE_JOURS:$JOURS" --var "DECALAGE_HORLOGE_ESSAI:$DECALAGE" --var "LIMITES_ESSAI:$LIMITES" \
        --log-level info --show-interactive-dev-session=false) >>"$JOURNAL" 2>&1 </dev/null &
    WPID=$!
    pret=0
    occupe=0
    fin=$(($(date +%s) + ATTENTE))
    while [ "$(date +%s)" -lt "$fin" ]; do
        if ! kill -0 "$WPID" 2>/dev/null; then
            grep -q "Address already in use" "$JOURNAL" && occupe=1
            break
        fi
        # "Ready on" : c'est bien CE wrangler qui tient le port (pas un autre programme)
        if grep -q "Ready on http://127.0.0.1:$PORT" "$JOURNAL" && repond "$PORT"; then
            pret=1
            break
        fi
        sleep 0.1
    done
    if [ "$pret" = 1 ]; then
        echo "$$" >"$ETAT/lanceur.pid"
        echo "$PORT" >"$ETAT/port"
        echo "service pret sur le port $PORT"
        wait "$WPID"
        code=$?
        WPID=""
        echo "lancer-local.sh : le service s'est arrete (code $code) ; voir $JOURNAL" >&2
        exit 4
    fi
    arreter
    if [ "$occupe" = 1 ] && [ "$PORT_DEMANDE" = "0" ]; then
        continue   # le port libre a ete pris entre-temps : un autre
    fi
    break
done
echo "lancer-local.sh : le service n'a pas demarre ; voir $JOURNAL" >&2
tail -n 20 "$JOURNAL" | sed 's/\x1b\[[0-9;]*m//g' >&2
exit 4
