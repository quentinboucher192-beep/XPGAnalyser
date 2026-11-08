Certificats et cles d'ESSAI SEULEMENT (1.37.0) - pour tests/admin_mail_test.cpp et tests/outils/smtp_essai.py, le
serveur SMTP local de l'essai du courriel de l'editeur (license_admin/Mail). Fictifs, publics : ne jamais leur faire
confiance ailleurs, ne jamais les utiliser pour un vrai serveur.

  ac-essai.pem            l'autorite d'essai (RSA 2048, 20 ans) ; l'essai la donne par Config::caFile. Sa cle n'est
                          PAS gardee (detruite apres la signature) : pour refaire le jeu, tout refaire (ci-dessous).
  serveur-essai.pem/.key  le serveur "localhost" (DNS:localhost seulement, pas d'adresse IP : se connecter a
                          127.0.0.1 doit etre refuse - le nom ne correspond pas), signe par l'autorite d'essai.
  nonfiable-essai.pem/.key  un serveur "localhost" auto-signe (ECDSA P-256), d'aucune autorite : refuse.

Refaire (OpenSSL 3) :
  openssl req -x509 -newkey rsa:2048 -nodes -keyout ac.key -out ac-essai.pem -days 7300 -sha256 \
    -subj "/O=XPG essais/CN=Autorite d'essai XPG (tests seulement)" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -addext "subjectKeyIdentifier=hash"
  openssl req -new -newkey rsa:2048 -nodes -keyout serveur-essai.key -out serveur.csr -subj "/O=XPG essais/CN=localhost"
  (ext.cnf : basicConstraints=critical,CA:FALSE / keyUsage=critical,digitalSignature,keyEncipherment /
   extendedKeyUsage=serverAuth / subjectAltName=DNS:localhost / subjectKeyIdentifier=hash / authorityKeyIdentifier=keyid)
  openssl x509 -req -in serveur.csr -CA ac-essai.pem -CAkey ac.key -set_serial 0x1037 -out serveur-essai.pem -days 7300 -sha256 -extfile ext.cnf
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout nonfiable-essai.key -out nonfiable-essai.pem \
    -days 7300 -sha256 -subj "/O=XPG essais/CN=localhost" -addext "subjectAltName=DNS:localhost" \
    -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=serverAuth"
  puis effacer ac.key, serveur.csr.
