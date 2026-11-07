#!/usr/bin/env bash
# =============================================================================
#  outils/service/essais-worker.sh - 1.35.0 : les essais du Worker du service
#  des licences (service/test, `npm test` : le VRAI runtime, workerd, par
#  lancer-local.sh), pour ctest
# -----------------------------------------------------------------------------
#  essais-worker.sh [dossier-du-depot]
#  Sans Node.js 22+ ou sans service/node_modules (`npm ci` dans service/) :
#  "SAUTE : ..." et 0. Sinon : le code de `npm test` (0 : tout passe).
# =============================================================================
set -u
ICI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPOT="${1:-$ICI/../..}"
SERVICE="$(cd "$DEPOT/service" 2>/dev/null && pwd)" || { echo "SAUTE : le dossier service/ est introuvable"; exit 0; }
if ! command -v node >/dev/null 2>&1; then
    echo "SAUTE : Node.js manque sur ce poste (les essais du Worker demandent Node.js 22 ou plus recent)"
    exit 0
fi
if ! node -e 'process.exit(+process.versions.node.split(".")[0] >= 22 ? 0 : 1)'; then
    echo "SAUTE : Node.js $(node --version) est trop ancien (22 ou plus recent)"
    exit 0
fi
if [ ! -f "$SERVICE/node_modules/wrangler/wrangler-dist/cli.js" ]; then
    echo "SAUTE : wrangler manque (lance \`npm ci\` dans $SERVICE)"
    exit 0
fi
cd "$SERVICE" && exec node --test --test-concurrency=1 --test-reporter=spec "test/*.test.js"
