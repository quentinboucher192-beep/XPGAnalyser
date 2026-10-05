stockage_nom[0] := config[0];
stockage_nom[1] := config[1];
stockage_nom[2] := config[2];
stockage_nom[3] := config[3];
stockage_public.nom_gaz := ASCII_TO_STRING(stockage_nom);

stockage_unite[0] := config[5];
stockage_unite[1] := config[6];
stockage_unite[2] := config[7];
stockage_unite[3] := config[8];
stockage_public.unite := ASCII_TO_STRING(stockage_unite);

stockage_public.basc.SPBASC1 := (config[10]);
stockage_public.basc.SPBASC2 := (config[11]);
stockage_public.basc.SEUILPREAL1 := (config[12]);
stockage_public.basc.SEUILPREAL2 := (config[13]);
stockage_public.basc.DLBASC1 := (config[14]);
stockage_public.basc.DLBASC2 := (config[15]);

stockage_public.chgt.SPV := (config[20]);

stockage_public.purge[0].NBPUR := (config[25]);
stockage_public.purge[0].SPUR4 := (config[26]);
stockage_public.purge[0].SPUR5 := (config[27]);
stockage_public.purge[0].SPUR6 := (config[28]);
stockage_public.purge[0].DL4 := (config[29]);
stockage_public.purge[0].DL5 := (config[30]);
stockage_public.purge[0].DL6 := (config[31]);
stockage_public.purge[0].DL7 := (config[32]);
stockage_public.purge[0].DL8 := (config[33]);
stockage_public.purge[0].DL9 := (config[34]);
stockage_public.purge[0].DL10 := (config[35]);
stockage_public.purge[0].DL11 := (config[36]);
stockage_public.purge[0].DL12 := (config[37]);

stockage_public.purge[1].NBPUR := (config[40]);
stockage_public.purge[1].SPUR4 := (config[41]);
stockage_public.purge[1].SPUR5 := (config[42]);
stockage_public.purge[1].SPUR6 := (config[43]);
stockage_public.purge[1].DL4 := (config[44]);
stockage_public.purge[1].DL5 := (config[45]);
stockage_public.purge[1].DL6 := (config[46]);
stockage_public.purge[1].DL7 := (config[47]);
stockage_public.purge[1].DL8 := (config[48]);
stockage_public.purge[1].DL9 := (config[49]);
stockage_public.purge[1].DL10 := (config[50]);
stockage_public.purge[1].DL11 := (config[51]);
stockage_public.purge[1].DL12 := (config[52]);

stockage_public.purge[2].NBPUR := (config[55]);
stockage_public.purge[2].SPUR4 := (config[56]);
stockage_public.purge[2].SPUR5 := (config[57]);
stockage_public.purge[2].SPUR6 := (config[58]);
stockage_public.purge[2].DL4 := (config[59]);
stockage_public.purge[2].DL5 := (config[60]);
stockage_public.purge[2].DL6 := (config[61]);
stockage_public.purge[2].DL7 := (config[62]);
stockage_public.purge[2].DL8 := (config[63]);
stockage_public.purge[2].DL9 := (config[64]);
stockage_public.purge[2].DL10 := (config[65]);
stockage_public.purge[2].DL11 := (config[66]);
stockage_public.purge[2].DL12 := (config[67]);

stockage_public.pompage[0].NBPPE := (config[70]);
stockage_public.pompage[0].SPPE5 := (config[71]);
stockage_public.pompage[0].SPPE6 := (config[72]);
stockage_public.pompage[0].DLP1 := (config[73]);
stockage_public.pompage[0].DLP2 := (config[74]);
stockage_public.pompage[0].DLP3 := (config[75]);
stockage_public.pompage[0].DLP4 := (config[76]);
stockage_public.pompage[0].DLP5 := (config[77]);
stockage_public.pompage[0].DLP6 := (config[78]);
stockage_public.pompage[0].DLP7 := (config[79]);
stockage_public.pompage[0].DLP8 := (config[80]);
stockage_public.pompage[0].DLP9 := (config[81]);
stockage_public.pompage[0].DLP10 := (config[82]);
stockage_public.pompage[0].DLP11 := (config[83]);

stockage_public.pompage[1].NBPPE := (config[85]);
stockage_public.pompage[1].SPPE5 := (config[86]);
stockage_public.pompage[1].SPPE6 := (config[87]);
stockage_public.pompage[1].DLP1 := (config[88]);
stockage_public.pompage[1].DLP2 := (config[89]);
stockage_public.pompage[1].DLP3 := (config[90]);
stockage_public.pompage[1].DLP4 := (config[91]);
stockage_public.pompage[1].DLP5 := (config[92]);
stockage_public.pompage[1].DLP6 := (config[93]);
stockage_public.pompage[1].DLP7 := (config[94]);
stockage_public.pompage[1].DLP8 := (config[95]);
stockage_public.pompage[1].DLP9 := (config[96]);
stockage_public.pompage[1].DLP10 := (config[97]);
stockage_public.pompage[1].DLP11 := (config[98]);

stockage_public.pompage[2].NBPPE := (config[100]);
stockage_public.pompage[2].SPPE5 := (config[101]);
stockage_public.pompage[2].SPPE6 := (config[102]);
stockage_public.pompage[2].DLP1 := (config[103]);
stockage_public.pompage[2].DLP2 := (config[104]);
stockage_public.pompage[2].DLP3 := (config[105]);
stockage_public.pompage[2].DLP4 := (config[106]);
stockage_public.pompage[2].DLP5 := (config[107]);
stockage_public.pompage[2].DLP6 := (config[108]);
stockage_public.pompage[2].DLP7 := (config[109]);
stockage_public.pompage[2].DLP8 := (config[110]);
stockage_public.pompage[2].DLP9 := (config[111]);
stockage_public.pompage[2].DLP10 := (config[112]);
stockage_public.pompage[2].DLP11 := (config[113]);

stockage_public.surpression.SP3H := (config[115]);
stockage_public.surpression.SP3 := (config[116]);
stockage_public.surpression.SP3B := (config[117]);
stockage_public.surpression.NBS := (config[118]);
stockage_public.surpression.DLSURV := (config[119]);
stockage_public.surpression.DLSURP1 := (config[120]);
stockage_public.surpression.DLSURP2 := (config[121]);
stockage_public.surpression.DLSURP3 := (config[122]);
stockage_public.surpression.DLSURP4 := (config[123]);
stockage_public.surpression.DLSURP5 := (config[124]);

stockage_public.detoxal.SPDETOX := (config[130]);
stockage_public.detoxal.DLDETOX := (config[131]);

stockage_public.helium.DLHE1 := (config[140]);
stockage_public.helium.DLHE2 := (config[141]);
stockage_public.helium.DLHE3 := (config[142]);
stockage_public.helium.DLHE4 := (config[143]);
stockage_public.helium.DLHE5 := (config[144]);

stockage_public.utilise := config[149].0;
