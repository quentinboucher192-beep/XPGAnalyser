#!/usr/bin/env bash
# =============================================================================
#  outils/service/arreter-local.sh - 1.35.0 : arreter le service des licences
#  lance sur ce poste par lancer-local.sh
# -----------------------------------------------------------------------------
#  arreter-local.sh <dossier-d-etat | pid du lanceur>
#    Le lanceur (et wrangler, et ses workerd) recoit SIGTERM ; 5 s plus tard, ce
#    qui reste est tue. Le dossier d'etat (les tables) est garde.
# =============================================================================
set -u
[ $# -eq 1 ] || { echo "usage : arreter-local.sh <dossier-d-etat | pid du lanceur>" >&2; exit 2; }
if [[ "$1" =~ ^[0-9]+$ ]]; then
    PID="$1"
elif [ -f "$1/lanceur.pid" ]; then
    PID="$(cat "$1/lanceur.pid")"
else
    echo "arreter-local.sh : aucun service lance avec ce dossier ($1/lanceur.pid manque)" >&2
    exit 1
fi
[[ "$PID" =~ ^[0-9]+$ ]] || { echo "arreter-local.sh : pid illisible" >&2; exit 1; }
if ! kill -0 "$PID" 2>/dev/null; then
    echo "arreter-local.sh : le service n'est plus lance"
    exit 0
fi
case "$(cat "/proc/$PID/cmdline" 2>/dev/null | tr '\0' ' ')" in
    *lancer-local.sh*) ;;
    *) echo "arreter-local.sh : le processus $PID n'est pas lancer-local.sh" >&2; exit 1 ;;
esac
kill -TERM "$PID"
for _ in $(seq 1 50); do
    kill -0 "$PID" 2>/dev/null || { echo "service arrete"; exit 0; }
    sleep 0.1
done
pkill -KILL -P "$PID" 2>/dev/null
kill -KILL "$PID" 2>/dev/null
echo "service arrete (de force)"
