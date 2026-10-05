#!/bin/bash
# =============================================================================
#  reseau-labo.sh - le reseau de laboratoire des sessions du lot 15 (Linux, root)
# -----------------------------------------------------------------------------
#  Un "PC" (l'espace de noms reseau "poste") a quatre ports Ethernet, et trois
#  reseaux d'equipements derriere (chacun dans son espace de noms, relie par une
#  paire veth ; chaque equipement a sa propre adresse MAC, par macvlan) :
#
#    eth1  192.168.1.20/24   atelier : automate 192.168.1.10 (Modbus), centrale
#                            de mesure .30 (Modbus), imprimante .40 (port 9100) ;
#                            le variateur .31 n'existe pas (injoignable) ;
#                            inconnus du projet (le scanner les trouve) : une
#                            passerelle Modbus .50, un pupitre web .60
#    eth2  10.10.0.5/16      process : analyseur de gaz 10.10.0.50 (Modbus),
#                            switch 10.10.0.1 (ping seulement)
#    eth3  169.254.12.7/16   quai : camera 172.16.0.8 (port 554) - hors reseau
#                            tant que le port n'a pas d'adresse en 172.16.x.x
#    eth4  (sans adresse)    cable debranche (l'autre bout est coupe)
#
#  Les adresses MAC des equipements reprennent des prefixes de fabricants
#  (pour montrer la colonne Fabricant du scanner IP) : ce sont des equipements
#  simules par banc-modbus, pas du materiel reel.
#
#    reseau-labo.sh start [chemin/banc-modbus]   |   reseau-labo.sh stop
#
#  L'application se lance ensuite DANS le PC :  ip netns exec poste xpg_analyzer ...
# =============================================================================
set -u
BANC=${2:-$(dirname "$0")/banc-modbus}
stop() {
    pkill -f "banc-modbus --" 2>/dev/null
    for n in poste atelier process quai vide; do ip netns del $n 2>/dev/null; done
    return 0
}
if [ "${1:-start}" = "stop" ]; then stop; exit 0; fi
stop
set -e
for n in poste atelier process quai vide; do ip netns add $n; ip -n $n link set lo up; done
# Chaque carte ne repond a l'ARP que pour sa propre adresse (sinon la premiere
# carte de l'espace de noms repond pour toutes : de fausses MAC).
for n in atelier process quai; do
    ip netns exec $n sysctl -qw net.ipv4.conf.all.arp_ignore=1 net.ipv4.conf.all.arp_announce=2 net.ipv4.conf.default.arp_ignore=1 net.ipv4.conf.default.arp_announce=2
done
# Un port du PC et l'autre bout dans le reseau `ns`.
port() {   # port <nom> <mac> <ns> <bout>
    ip link add $1 type veth peer name $4
    ip link set $1 netns poste
    ip link set $4 netns $3
    ip -n poste link set $1 address $2
    ip -n $3 link set $4 up
}
# Un equipement : une carte macvlan (sa MAC, son adresse) sur le bout du reseau.
equip() {  # equip <ns> <bout> <nom> <mac> <ip/pre>
    ip -n $1 link add link $2 name $3 type macvlan mode bridge
    ip -n $1 link set $3 address $4
    ip -n $1 addr add $5 dev $3
    ip -n $1 link set $3 up
}
port eth1 00:1b:21:3a:4f:10 atelier at0
port eth2 00:e0:4c:68:12:9a process pr0
port eth3 a0:36:9f:22:5c:07 quai qu0
port eth4 a0:36:9f:22:5c:08 vide vd0
ip -n poste addr add 192.168.1.20/24 dev eth1
ip -n poste addr add 10.10.0.5/16 dev eth2
ip -n poste addr add 169.254.12.7/16 dev eth3
for p in eth1 eth2 eth3 eth4; do ip -n poste link set $p up; done
ip -n vide link set vd0 down                       # eth4 : cable debranche
equip atelier at0 plc 00:80:f4:0c:12:10 192.168.1.10/24
equip atelier at0 pm  00:00:54:3a:10:30 192.168.1.30/24
equip atelier at0 imp 02:42:ac:11:00:40 192.168.1.40/24
equip atelier at0 gw  00:90:e8:5a:01:32 192.168.1.50/24
equip atelier at0 hmi 02:42:ac:11:00:60 192.168.1.60/24
equip process pr0 gaz 00:90:e8:5a:00:50 10.10.0.50/16
equip process pr0 sw  00:80:63:10:00:01 10.10.0.1/16
equip quai    qu0 cam 02:42:ac:16:00:08 172.16.0.8/16
# Les services.
ip netns exec atelier "$BANC" --modbus 192.168.1.10:502:automate --modbus 192.168.1.30:502:centrale --tcp 192.168.1.40:9100 \
    --modbus 192.168.1.50:502:passerelle --tcp 192.168.1.60:80 > /tmp/banc-atelier.log 2>&1 &
ip netns exec process "$BANC" --modbus 10.10.0.50:502:analyseur > /tmp/banc-process.log 2>&1 &
ip netns exec quai "$BANC" --tcp 172.16.0.8:554 > /tmp/banc-quai.log 2>&1 &
sleep 1
echo "reseau de laboratoire pret : ip netns exec poste ..."
