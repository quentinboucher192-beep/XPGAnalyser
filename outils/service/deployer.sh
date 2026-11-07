#!/usr/bin/env bash
# =============================================================================
#  deployer.sh - 1.35.0 : mettre en ligne le service des licences sur TON
#  compte Cloudflare (gratuit), sous Linux ou macOS. Le pas a pas :
#  LISEZ-MOI-SERVICE.txt. Il verifie Node.js, puis lance deployer.mjs (a cote de
#  ce fichier) : la cle PUBLIQUE de l'editeur (cle-publique.txt), wrangler
#  deploy, l'adresse. Aucun secret ici.
#    ./deployer.sh [--cle <hexa>] [--service <dossier>] [--essai]
# =============================================================================
set -u
ICI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v node >/dev/null 2>&1; then
    echo "Node.js n'est pas installé : installe la version LTS (https://nodejs.org), puis relance ./deployer.sh" >&2
    exit 1
fi
if ! command -v npx >/dev/null 2>&1; then
    echo "npx manque : réinstalle Node.js (version LTS, https://nodejs.org), puis relance ./deployer.sh" >&2
    exit 1
fi
if [ ! -f "$ICI/deployer.mjs" ]; then
    echo "deployer.mjs manque : il doit être à côté de deployer.sh" >&2
    exit 1
fi
exec node "$ICI/deployer.mjs" "$@"
