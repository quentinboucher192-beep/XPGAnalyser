(* Genere par PreparerGrafcets (ImporterGrafcet, GenererGrafcet). Le temps
   des grafcets avance une fois par cycle, puis chaque grafcet tourne dans sa
   sous-routine Grafcet_xxx : ses receptivites, son moteur, ses sorties.
   Place apres les modes et avant le controle et les equipements :
   les interverrouillages ont le dernier mot sur ce que les grafcets commandent. *)
GC_Temps := GC_Temps + DINT_TO_TIME(Horloge.DtMs);
