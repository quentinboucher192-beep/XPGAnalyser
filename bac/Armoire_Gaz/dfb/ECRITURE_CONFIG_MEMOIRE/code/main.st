stockage_nom := STRING_TO_ASCII(config.nom_gaz);
stockage_public[0] := stockage_nom[0];
stockage_public[1] := stockage_nom[1];
stockage_public[2] := stockage_nom[2];
stockage_public[3] := stockage_nom[3];

stockage_unite := STRING_TO_ASCII(config.unite);
stockage_public[5] := stockage_unite[0];
stockage_public[6] := stockage_unite[1];
stockage_public[7] := stockage_unite[2];
stockage_public[8] := stockage_unite[3];

stockage_public[10] := (config.basc.SPBASC1);
stockage_public[11] := (config.basc.SPBASC2);
stockage_public[12] := (config.basc.SEUILPREAL1);
stockage_public[13] := (config.basc.SEUILPREAL2);
stockage_public[14] := (config.basc.DLBASC1);
stockage_public[15] := (config.basc.DLBASC2);

stockage_public[20] := (config.chgt.SPV);

stockage_public[25] := (config.purge[0].NBPUR);
stockage_public[26] := (config.purge[0].SPUR4);
stockage_public[27] := (config.purge[0].SPUR5);
stockage_public[28] := (config.purge[0].SPUR6);
stockage_public[29] := (config.purge[0].DL4);
stockage_public[30] := (config.purge[0].DL5);
stockage_public[31] := (config.purge[0].DL6);
stockage_public[32] := (config.purge[0].DL7);
stockage_public[33] := (config.purge[0].DL8);
stockage_public[34] := (config.purge[0].DL9);
stockage_public[35] := (config.purge[0].DL10);
stockage_public[36] := (config.purge[0].DL11);
stockage_public[37] := (config.purge[0].DL12);

stockage_public[40] := (config.purge[1].NBPUR);
stockage_public[41] := (config.purge[1].SPUR4);
stockage_public[42] := (config.purge[1].SPUR5);
stockage_public[43] := (config.purge[1].SPUR6);
stockage_public[44] := (config.purge[1].DL4);
stockage_public[45] := (config.purge[1].DL5);
stockage_public[46] := (config.purge[1].DL6);
stockage_public[47] := (config.purge[1].DL7);
stockage_public[48] := (config.purge[1].DL8);
stockage_public[49] := (config.purge[1].DL9);
stockage_public[50] := (config.purge[1].DL10);
stockage_public[51] := (config.purge[1].DL11);
stockage_public[52] := (config.purge[1].DL12);

stockage_public[55] := (config.purge[2].NBPUR);
stockage_public[56] := (config.purge[2].SPUR4);
stockage_public[57] := (config.purge[2].SPUR5);
stockage_public[58] := (config.purge[2].SPUR6);
stockage_public[59] := (config.purge[2].DL4);
stockage_public[60] := (config.purge[2].DL5);
stockage_public[61] := (config.purge[2].DL6);
stockage_public[62] := (config.purge[2].DL7);
stockage_public[63] := (config.purge[2].DL8);
stockage_public[64] := (config.purge[2].DL9);
stockage_public[65] := (config.purge[2].DL10);
stockage_public[66] := (config.purge[2].DL11);
stockage_public[67] := (config.purge[2].DL12);

stockage_public[70] := (config.pompage[0].NBPPE);
stockage_public[71] := (config.pompage[0].SPPE5);
stockage_public[72] := (config.pompage[0].SPPE6);
stockage_public[73] := (config.pompage[0].DLP1);
stockage_public[74] := (config.pompage[0].DLP2);
stockage_public[75] := (config.pompage[0].DLP3);
stockage_public[76] := (config.pompage[0].DLP4);
stockage_public[77] := (config.pompage[0].DLP5);
stockage_public[78] := (config.pompage[0].DLP6);
stockage_public[79] := (config.pompage[0].DLP7);
stockage_public[80] := (config.pompage[0].DLP8);
stockage_public[81] := (config.pompage[0].DLP9);
stockage_public[82] := (config.pompage[0].DLP10);
stockage_public[83] := (config.pompage[0].DLP11);

stockage_public[85] := (config.pompage[1].NBPPE);
stockage_public[86] := (config.pompage[1].SPPE5);
stockage_public[87] := (config.pompage[1].SPPE6);
stockage_public[88] := (config.pompage[1].DLP1);
stockage_public[89] := (config.pompage[1].DLP2);
stockage_public[90] := (config.pompage[1].DLP3);
stockage_public[91] := (config.pompage[1].DLP4);
stockage_public[92] := (config.pompage[1].DLP5);
stockage_public[93] := (config.pompage[1].DLP6);
stockage_public[94] := (config.pompage[1].DLP7);
stockage_public[95] := (config.pompage[1].DLP8);
stockage_public[96] := (config.pompage[1].DLP9);
stockage_public[97] := (config.pompage[1].DLP10);
stockage_public[98] := (config.pompage[1].DLP11);

stockage_public[100] := (config.pompage[2].NBPPE);
stockage_public[101] := (config.pompage[2].SPPE5);
stockage_public[102] := (config.pompage[2].SPPE6);
stockage_public[103] := (config.pompage[2].DLP1);
stockage_public[104] := (config.pompage[2].DLP2);
stockage_public[105] := (config.pompage[2].DLP3);
stockage_public[106] := (config.pompage[2].DLP4);
stockage_public[107] := (config.pompage[2].DLP5);
stockage_public[108] := (config.pompage[2].DLP6);
stockage_public[109] := (config.pompage[2].DLP7);
stockage_public[110] := (config.pompage[2].DLP8);
stockage_public[111] := (config.pompage[2].DLP9);
stockage_public[112] := (config.pompage[2].DLP10);
stockage_public[113] := (config.pompage[2].DLP11);

stockage_public[115] := (config.surpression.SP3H);
stockage_public[116] := (config.surpression.SP3);
stockage_public[117] := (config.surpression.SP3B);
stockage_public[118] := (config.surpression.NBS);
stockage_public[119] := (config.surpression.DLSURV);
stockage_public[120] := (config.surpression.DLSURP1);
stockage_public[121] := (config.surpression.DLSURP2);
stockage_public[122] := (config.surpression.DLSURP3);
stockage_public[123] := (config.surpression.DLSURP4);
stockage_public[124] := (config.surpression.DLSURP5);

stockage_public[130] := (config.detoxal.SPDETOX);
stockage_public[131] := (config.detoxal.DLDETOX);

stockage_public[140] := (config.helium.DLHE1);
stockage_public[141] := (config.helium.DLHE2);
stockage_public[142] := (config.helium.DLHE3);
stockage_public[143] := (config.helium.DLHE4);
stockage_public[144] := (config.helium.DLHE5);

stockage_public[149].0 := config.utilise;
