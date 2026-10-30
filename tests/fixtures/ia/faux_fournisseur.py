#!/usr/bin/env python3
# =============================================================================
#  tests/fixtures/ia/faux_fournisseur.py - 1.15.0 : un faux fournisseur d'IA, local
# -----------------------------------------------------------------------------
#  POUR ESSAYER L'ASSISTANT IA SANS CLE NI RESEAU (les essais, les captures sous
#  Wine). Il imite l'API Messages d'Anthropic sur http://127.0.0.1:<port> et
#  REJOUE DES FLUX ENREGISTRES (SSE), choisis d'apres la demande :
#
#    station, pompage   du texte, puis un appel de proposer_operations : le
#                       synoptique de station de pompage (31 objets, vrais genres,
#                       vraies cles), les morceaux input_json_delta coupes ;
#    cuve               une cuve et son bargraphe (un petit appel d'outil) ;
#    inventee           une propriete inventee : la validation la refuse ;
#    illisible          une operation inconnue : l'entree est refusee a la lecture ;
#    longue             coupee par max_tokens au milieu de l'appel d'outil ;
#    refus              stop_reason "refusal" ;
#    surcharge          529 (retry-after: 1) deux fois, puis la reponse ;
#    surcharge totale   529 a chaque fois ;
#    coupure            du texte, puis la connexion coupee (sans message_stop) ;
#    erreur flux        "event: error" (overloaded_error) avant tout texte ;
#    lent               une reponse mot a mot, lente (pour Arreter) ;
#    sinon              une reponse de texte ; apres un appel d'outil, il dit le
#                       resultat recu (tool_result : appliquee, refusee...).
#
#  La cle "cle-refusee" rend 401 ; le modele "introuvable" rend 404 ;
#  GET /v1/models rend trois modeles d'essai. Un prefixe de relais est accepte
#  (/relais/v1/messages). Le journal (stderr) ne montre jamais la cle.
#
#  LANCER :  python3 -I tests/fixtures/ia/faux_fournisseur.py [--port 8765]
#  puis, dans IHM Studio (Parametres de l'Assistant IA, ou une seance) :
#     ia fournisseur anthropic
#     ia adresse "http://127.0.0.1:8765"
#     ia modele "modele-essai"
#     ia cle "cle-essai"
#  (sous Wine, l'exe Windows parle a ce serveur par WinHTTP, comme a un relais.)
# =============================================================================
import argparse
import json
import sys
import threading
import time
import unicodedata
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

COUNTERS = {}
LOCK = threading.Lock()


def fold(text):
    t = unicodedata.normalize("NFKD", text or "")
    return "".join(c for c in t if not unicodedata.combining(c)).lower()


def sse(event, data):
    return ("event: %s\ndata: %s\n\n" % (event, json.dumps(data, ensure_ascii=False))).encode("utf-8")


def create(kind, name, x, y, w, h, **more):
    props = {"x": str(x), "y": str(y), "w": str(w), "h": str(h)}
    props.update({k: str(v) for k, v in more.items()})
    return {"action": "creer", "genre": kind, "nom": name, "proprietes": props}


def station_ops():
    ops = [create("Text", "Titre_Station", 40, 24, 520, 40, text="Station de pompage", fontSize=26, align="gauche")]
    col_w = (900 - 80) / 3
    levels = ["65", "40", "80"]
    for col in range(3):
        cx = 40 + col * col_w
        n = str(col + 1)
        ops.append(create("Tank", "Cuve_" + n, cx + 40, 94, 150, 200, label="Cuve " + n, value=levels[col], max=100, unit="%"))
        ops.append(create("Bargraph", "Niveau_Cuve_" + n, cx + 214, 94, 56, 200, value=levels[col], unit="%"))
        ops.append(create("Valve", "Vanne_" + n, cx + 75, 324, 80, 64, label="V" + n))
        ops.append(create("Pipe", "Collecteur_" + n, cx + 20, 416, int(col_w - 80), 20))
        half = (col_w - 80) / 2
        for k in range(2):
            pn = n + str(k + 1)
            px = int(cx + 20 + k * half)
            ops.append(create("Pump", "Pompe_" + pn, px, 454, 90, 80, label="P" + pn))
            ops.append(create("PushButton", "Marche_P" + pn, px, 580, 96, 36, text="Marche", fill="#2F7D4F", stroke="#24603C", fontSize=14))
            ops.append(create("PushButton", "Arret_P" + pn, px, 624, 96, 36, text="Arrêt", fill="#7D3A36", stroke="#5E2B28", fontSize=14))
    return ops


def message_start(model):
    return sse("message_start", {"type": "message_start", "message": {
        "id": "msg_faux", "type": "message", "role": "assistant", "model": model, "content": [],
        "stop_reason": None, "usage": {"input_tokens": 100, "output_tokens": 1}}})


def text_block(index, text, words_per_chunk=3):
    out = [sse("content_block_start", {"type": "content_block_start", "index": index, "content_block": {"type": "text", "text": ""}})]
    words = text.split(" ")
    for i in range(0, len(words), words_per_chunk):
        piece = " ".join(words[i:i + words_per_chunk]) + (" " if i + words_per_chunk < len(words) else "")
        out.append(sse("content_block_delta", {"type": "content_block_delta", "index": index, "delta": {"type": "text_delta", "text": piece}}))
    out.append(sse("content_block_stop", {"type": "content_block_stop", "index": index}))
    return out


def tool_block(index, tool_id, payload, cut_at=None):
    raw = json.dumps(payload, ensure_ascii=False)
    if cut_at is not None:
        raw = raw[:cut_at]
    out = [sse("content_block_start", {"type": "content_block_start", "index": index,
                                       "content_block": {"type": "tool_use", "id": tool_id, "name": "proposer_operations", "input": {}}})]
    for i in range(0, len(raw), 97):   # des morceaux coupes n'importe ou
        out.append(sse("content_block_delta", {"type": "content_block_delta", "index": index,
                                               "delta": {"type": "input_json_delta", "partial_json": raw[i:i + 97]}}))
    if cut_at is None:
        out.append(sse("content_block_stop", {"type": "content_block_stop", "index": index}))
    return out


def finish(reason):
    return [sse("message_delta", {"type": "message_delta", "delta": {"stop_reason": reason, "stop_sequence": None},
                                  "usage": {"output_tokens": 50}}),
            sse("message_stop", {"type": "message_stop"})]


def last_user(body):
    msgs = body.get("messages") or []
    if not msgs or msgs[-1].get("role") != "user":
        return "", None
    content = msgs[-1].get("content")
    if isinstance(content, str):
        return content, None
    text, result = "", None
    for block in content or []:
        if block.get("type") == "text":
            text += block.get("text", "") + "\n"
        elif block.get("type") == "tool_result":
            c = block.get("content")
            result = c if isinstance(c, str) else json.dumps(c, ensure_ascii=False)
    return text, result


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[faux fournisseur] " + (fmt % args) + "\n")

    def error(self, status, kind, message, headers=None):
        body = json.dumps({"type": "error", "error": {"type": kind, "message": message}}).encode("utf-8")
        self.send_response(status)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def key_ok(self):
        key = self.headers.get("x-api-key")
        sys.stderr.write("[faux fournisseur] cle : %s, version : %s\n" % ("presente" if key else "absente", self.headers.get("anthropic-version")))
        if key == "cle-refusee":
            self.error(401, "authentication_error", "invalid x-api-key")
            return False
        return True

    def do_GET(self):
        if not self.path.split("?")[0].endswith("/v1/models"):
            self.error(404, "not_found_error", "Not Found")
            return
        if not self.key_ok():
            return
        data = {"data": [{"type": "model", "id": "modele-essai", "display_name": "Modèle d'essai"},
                         {"type": "model", "id": "modele-rapide", "display_name": "Modèle rapide"},
                         {"type": "model", "id": "modele-soigne", "display_name": "Modèle soigné"}],
                "has_more": False}
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def stream(self, parts, cut=False, delay=0.0):
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.send_header("cache-control", "no-cache")
        self.send_header("transfer-encoding", "chunked")
        self.end_headers()
        for p in parts:
            self.wfile.write(b"%x\r\n%s\r\n" % (len(p), p))
            self.wfile.flush()
            if delay:
                time.sleep(delay)
        if cut:
            self.close_connection = True
            self.wfile.flush()
            self.connection.shutdown(2)
            return
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()

    def do_POST(self):
        length = int(self.headers.get("content-length") or 0)
        raw = self.rfile.read(length)
        if not self.path.split("?")[0].endswith("/v1/messages"):
            self.error(404, "not_found_error", "Not Found")
            return
        if not self.key_ok():
            return
        try:
            body = json.loads(raw.decode("utf-8"))
        except Exception as e:  # noqa
            self.error(400, "invalid_request_error", "corps illisible : %s" % e)
            return
        for forbidden in ("temperature", "top_p", "top_k", "thinking", "tool_choice"):
            if forbidden in body:
                self.error(400, "invalid_request_error", "%s: champ refuse par ce modele" % forbidden)
                return
        model = body.get("model", "")
        if model == "introuvable":
            self.error(404, "not_found_error", "model: %s" % model)
            return
        text, result = last_user(body)
        f = fold(text)
        sys.stderr.write("[faux fournisseur] demande : %r%s\n" % (text.strip()[:80], " ; resultat : %r" % result if result else ""))
        if "surcharge" in f:
            with LOCK:
                COUNTERS[f] = COUNTERS.get(f, 0) + 1
                n = COUNTERS[f]
            if "totale" in f or n <= 2:
                self.error(529, "overloaded_error", "Overloaded", {"retry-after": "1"})
                return
        start = [message_start(model)]
        if "erreur flux" in f:
            self.stream(start + [sse("error", {"type": "error", "error": {"type": "overloaded_error", "message": "Overloaded"}})])
            return
        if "coupure" in f:
            self.stream(start + text_block(0, "Je commence a repondre, puis la connexion")[:-1], cut=True)
            return
        if "lent" in f:
            words = " ".join("mot%d" % i for i in range(1, 41))
            self.stream(start + text_block(0, words, 1) + finish("end_turn"), delay=0.15)
            return
        if "refus" in f:
            self.stream(start + text_block(0, "Je ne peux pas aider pour cela.") + finish("refusal"))
            return
        if "station" in f or "pompage" in f:
            payload = {"resume": "Synoptique de station de pompage", "operations": station_ops()}
            intro = ("Je propose un synoptique de station de pompage : trois cuves avec leur niveau, une vanne et un collecteur "
                     "par cuve, deux pompes par cuve et leurs boutons Marche et Arrêt.")
            self.stream(start + text_block(0, intro) + tool_block(1, "toolu_station_1", payload) + finish("tool_use"))
            return
        if "longue" in f:
            payload = {"resume": "Synoptique coupe", "operations": station_ops()}
            self.stream(start + text_block(0, "Je propose beaucoup d'objets.") + tool_block(1, "toolu_long_1", payload, cut_at=500)
                        + finish("max_tokens"))
            return
        if "inventee" in f:
            payload = {"resume": "Une cuve", "operations": [create("Tank", "Cuve_X", 100, 100, 150, 200, couleurInventee="#FF0000")]}
            self.stream(start + text_block(0, "Voici une cuve.") + tool_block(1, "toolu_inv_1", payload) + finish("tool_use"))
            return
        if "illisible" in f:
            payload = {"resume": "Rien", "operations": [{"action": "executer", "code": "system('x')"}]}
            self.stream(start + text_block(0, "Voici.") + tool_block(1, "toolu_ill_1", payload) + finish("tool_use"))
            return
        if "cuve" in f:
            payload = {"resume": "Une cuve et son niveau", "operations": [
                create("Tank", "Cuve_A", 120, 120, 150, 200, label="Cuve A", max=100, unit="%"),
                create("Bargraph", "Niveau_Cuve_A", 294, 120, 56, 200, unit="%")]}
            self.stream(start + text_block(0, "Je propose une cuve avec son indicateur de niveau.") + tool_block(1, "toolu_cuve_1", payload)
                        + finish("tool_use"))
            return
        if result is not None:
            reply = "Bien reçu : ma proposition précédente est « %s »." % result
        else:
            reply = "Bonjour, je suis le faux fournisseur local : je rejoue des réponses enregistrées (demande-moi une station de pompage)."
        self.stream(start + text_block(0, reply) + finish("end_turn"))


def main():
    ap = argparse.ArgumentParser(description="Faux fournisseur d'IA (API Messages) pour essayer l'Assistant sans cle.")
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    sys.stderr.write("[faux fournisseur] http://127.0.0.1:%d\n" % server.server_address[1])
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
