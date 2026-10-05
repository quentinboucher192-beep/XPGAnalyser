Gc_PompageA.Runtime.GC_MAX_STEPS := 28;
Gc_PompageA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_PompageA.Runtime.GC_MAX_ACTIONS := 28;
Gc_PompageA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_PompageA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_PompageA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 								_Step:=Steps_PompageA[0]);
Builder(T:=1,Text:='id=1|n=X1', 									_Step:=Steps_PompageA[1]); 
Builder(T:=1,Text:='id=2|n=X2', 									_Step:=Steps_PompageA[2]); 
Builder(T:=1,Text:='id=3|n=X3', 									_Step:=Steps_PompageA[3]); 
Builder(T:=1,Text:='id=4|n=X4', 									_Step:=Steps_PompageA[4]); 
Builder(T:=1,Text:='id=5|n=X5', 									_Step:=Steps_PompageA[5]); 
Builder(T:=1,Text:='id=6|n=X6', 									_Step:=Steps_PompageA[6]); 
Builder(T:=1,Text:='id=7|n=X7', 									_Step:=Steps_PompageA[7]); 
Builder(T:=1,Text:='id=8|n=X8',									_Step:=Steps_PompageA[8]); 
Builder(T:=1,Text:='id=9|n=X9|f=1', 								_Step:=Steps_PompageA[9]); 
Builder(T:=1,Text:='id=10|n=X10', 									_Step:=Steps_PompageA[10]); 
Builder(T:=1,Text:='id=11|n=X11', 									_Step:=Steps_PompageA[11]); 
Builder(T:=1,Text:='id=12|n=X12', 									_Step:=Steps_PompageA[12]); 
Builder(T:=1,Text:='id=13|n=X13', 									_Step:=Steps_PompageA[13]); 

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=TRUE', 								_Trans:=Trans_PompageA[0]); 
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=PIRANI', 							_Trans:=Trans_PompageA[1]);
Builder(T:=2,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP2)*1000.0), 	_Trans:=Trans_PompageA[1]); 
Builder(T:=2,Text:='id=2|s=1|d=10|c=!PIRANI', 							_Trans:=Trans_PompageA[2]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[2]);
Builder(T:=2,Text:='id=3|s=2|d=3|c=PIRANI', 							_Trans:=Trans_PompageA[3]);
Builder(T:=2,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP4)*1000.0), 	_Trans:=Trans_PompageA[3]);
Builder(T:=2,Text:='id=4|s=2|d=10|c=!PIRANI', 							_Trans:=Trans_PompageA[4]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[4]);
Builder(T:=2,Text:='id=5|s=3|d=4|c=AP', 								_Trans:=Trans_PompageA[5]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[5]);
Builder(T:=2,Text:='id=6|s=3|d=5|c=!AP', 								_Trans:=Trans_PompageA[6]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[6]);
Builder(T:=2,Text:='id=7|s=4|d=5|c=PIRANI', 							_Trans:=Trans_PompageA[7]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[7]);
Builder(T:=2,Text:='id=8|s=4|d=11|c=!PIRANI', 							_Trans:=Trans_PompageA[8]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[8]);
Builder(T:=2,Text:='id=9|s=5|d=6|c=TRUE', 								_Trans:=Trans_PompageA[9]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[9]);
Builder(T:=2,Text:='id=10|s=6|d=7|c=PT1>=SPPE6', 							_Trans:=Trans_PompageA[10]);
Builder(T:=2,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP6)*1000.0), 	_Trans:=Trans_PompageA[10]);
Builder(T:=2,Text:='id=11|s=6|d=12|c=PT1<SPPE6', 							_Trans:=Trans_PompageA[11]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[11]);
Builder(T:=2,Text:='id=12|s=7|d=8|c=PT1<=SPPE5', 							_Trans:=Trans_PompageA[12]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[12]);
Builder(T:=2,Text:='id=13|s=7|d=13|c=PT1>SPPE5', 							_Trans:=Trans_PompageA[13]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[13]);
Builder(T:=2,Text:='id=14|s=8|d=9|c=NB==', 								_Trans:=Trans_PompageA[14]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[14]);
Builder(T:=2,Text:='id=15|s=8|d=6|c=NB!=', 								_Trans:=Trans_PompageA[15]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[15]);
Builder(T:=2,Text:='id=16|s=9|d=0|c=TRUE', 								_Trans:=Trans_PompageA[16]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[16]);
Builder(T:=2,Text:='id=17|s=10|d=0|c=IHM', 								_Trans:=Trans_PompageA[17]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[17]);
Builder(T:=2,Text:='id=18|s=11|d=0|c=IHM', 								_Trans:=Trans_PompageA[18]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[18]);
Builder(T:=2,Text:='id=19|s=12|d=5|c=IHM', 								_Trans:=Trans_PompageA[19]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[19]);
Builder(T:=2,Text:='id=20|s=13|d=0|c=IHM', 								_Trans:=Trans_PompageA[20]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageA[20]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=FTTOV4|k=4|s=1', 							_Act:=Acts_PompageA[0]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP1)*1000.0), 	_Act:=Acts_PompageA[0]);
Builder(T:=3,Text:='id=1|n=OPPE|k=11|s=1', 								_Act:=Acts_PompageA[1]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP1)*1000.0), 	_Act:=Acts_PompageA[1]);
Builder(T:=3,Text:='id=2|n=OV9|k=4|s=2', 								_Act:=Acts_PompageA[2]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP3)*1000.0), 	_Act:=Acts_PompageA[2]);
Builder(T:=3,Text:='id=3|n=FPPE|k=0|s=3', 								_Act:=Acts_PompageA[3]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[3]);
Builder(T:=3,Text:='id=4|n=NB=0|k=1|s=5', 								_Act:=Acts_PompageA[4]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[4]);
Builder(T:=3,Text:='id=5|n=NB=FV9OV4NB++|k=1|s=6', 							_Act:=Acts_PompageA[5]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[5]);
Builder(T:=3,Text:='id=6|n=OV2|k=4|s=6', 								_Act:=Acts_PompageA[6]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP6)*1000.0), 	_Act:=Acts_PompageA[6]);
Builder(T:=3,Text:='id=7|n=FV2|k=4|s=7', 								_Act:=Acts_PompageA[7]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP8)*1000.0), 	_Act:=Acts_PompageA[7]);
Builder(T:=3,Text:='id=8|n=OV9|k=11|s=7', 								_Act:=Acts_PompageA[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP9 + config_utilisee.pompage[index_pompage_A].DLP10)*1000.0), 	_Act:=Acts_PompageA[8]);
Builder(T:=3,Text:='id=9|n=FV9|k=4|s=8', 								_Act:=Acts_PompageA[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP11)*1000.0), 	_Act:=Acts_PompageA[9]);
Builder(T:=3,Text:='id=10|n=FV2V9PPE|k=1|s=9', 							_Act:=Acts_PompageA[10]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[10]);
Builder(T:=3,Text:='id=11|n=FTT|k=1|s=10', 								_Act:=Acts_PompageA[11]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[11]);
Builder(T:=3,Text:='id=12|n=FV9|k=4|s=11', 								_Act:=Acts_PompageA[12]);
Builder(T:=3,d:=t#1s, 										_Act:=Acts_PompageA[12]);
Builder(T:=3,Text:='id=13|n=OV2|k=0|s=11', 								_Act:=Acts_PompageA[13]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[13]);
Builder(T:=3,Text:='id=14|n=FTT|k=1|s=12', 								_Act:=Acts_PompageA[14]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[14]);
Builder(T:=3,Text:='id=15|n=FTT|k=1|s=13', 								_Act:=Acts_PompageA[15]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageA[15]);

Ctrl_PompageA.Cmd.ForceStepFromId := -1;
Ctrl_PompageA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_PompageA[0].EnableCond :=  TRUE;
Acts_PompageA[1].EnableCond :=  Acts_PompageA[0].Started AND Acts_PompageA[0].Finished;
Acts_PompageA[2].EnableCond :=  TRUE;
Acts_PompageA[3].EnableCond :=  TRUE;
Acts_PompageA[4].EnableCond :=  TRUE;
Acts_PompageA[5].EnableCond :=  TRUE;
Acts_PompageA[6].EnableCond :=  TRUE;
Acts_PompageA[7].EnableCond :=  TRUE;
Acts_PompageA[8].EnableCond :=  Acts_PompageA[7].Started AND Acts_PompageA[7].Finished;
Acts_PompageA[9].EnableCond :=  TRUE;
Acts_PompageA[10].EnableCond := TRUE;
Acts_PompageA[11].EnableCond := TRUE;
Acts_PompageA[12].EnableCond := TRUE;
Acts_PompageA[13].EnableCond := Acts_PompageA[12].Started AND Acts_PompageA[12].Finished;
Acts_PompageA[14].EnableCond := TRUE;
Acts_PompageA[15].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_PompageA[0].Condition  :=  TRUE;
Trans_PompageA[1].Condition  :=  Acts_PompageA[1].Started AND Acts_PompageA[1].Finished AND armoires[0].entrees.pirani;
Trans_PompageA[2].Condition  :=  Acts_PompageA[1].Started AND Acts_PompageA[1].Finished AND NOT(armoires[0].entrees.pirani);
Trans_PompageA[3].Condition  :=  Acts_PompageA[2].Started AND Acts_PompageA[2].Finished AND armoires[0].entrees.pirani;
Trans_PompageA[4].Condition  :=  Acts_PompageA[2].Started AND Acts_PompageA[2].Finished AND NOT(armoires[0].entrees.pirani);
Trans_PompageA[5].Condition  :=  armoires[0].en_cours_de_chgt_apres;
Trans_PompageA[6].Condition  :=  NOT(armoires[0].en_cours_de_chgt_apres);
Trans_PompageA[7].Condition  :=  Steps_PompageA[4].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP5)*1000.0) AND armoires[0].entrees.pirani;
Trans_PompageA[8].Condition  :=  Steps_PompageA[4].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_A].DLP5)*1000.0) AND NOT(armoires[0].entrees.pirani);
Trans_PompageA[9].Condition  :=  TRUE;
Trans_PompageA[10].Condition  := Acts_PompageA[6].Started AND Acts_PompageA[6].Finished AND armoires[0].ana.PT1.Mes >= INT_TO_REAL(config_utilisee.pompage[index_purge_A].SPPE6);
Trans_PompageA[11].Condition  := Acts_PompageA[6].Started AND Acts_PompageA[6].Finished AND armoires[0].ana.PT1.Mes < INT_TO_REAL(config_utilisee.pompage[index_purge_A].SPPE6);
Trans_PompageA[12].Condition  := Acts_PompageA[8].Started AND Acts_PompageA[8].Finished AND armoires[0].ana.PT1.Mes <= INT_TO_REAL(config_utilisee.pompage[index_purge_A].SPPE5) AND armoires[0].entrees.pirani;
Trans_PompageA[13].Condition  := Acts_PompageA[8].Started AND Acts_PompageA[8].Finished AND NOT(armoires[0].ana.PT1.Mes <= INT_TO_REAL(config_utilisee.pompage[index_purge_A].SPPE5) AND armoires[0].entrees.pirani);
Trans_PompageA[14].Condition  := Acts_PompageA[9].Started AND Acts_PompageA[9].Finished AND armoires[0].compteur_cycle_pompage >= config_utilisee.pompage[index_pompage_A].NBPPE;
Trans_PompageA[15].Condition  := Acts_PompageA[9].Started AND Acts_PompageA[9].Finished AND armoires[0].compteur_cycle_pompage < config_utilisee.pompage[index_pompage_A].NBPPE;
Trans_PompageA[16].Condition  := TRUE;
Trans_PompageA[17].Condition  := chgt_valider[0];
Trans_PompageA[18].Condition  := chgt_valider[0];
Trans_PompageA[19].Condition  := chgt_valider[0];
Trans_PompageA[20].Condition  := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_PompageA.Cmd.ModeReq = 0 THEN Ctrl_PompageA.Cmd.ModeReq := 2; END_IF;
Ctrl_PompageA.Cmd.Enable := TRUE and not(armoires[0].manu) and not(modification_majeure);

Gc_PompageA(
    Enable := Ctrl_PompageA.Cmd.Enable,
    ModeReq := Ctrl_PompageA.Cmd.ModeReq,
    FreezeReq := Ctrl_PompageA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_PompageA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_PompageA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_PompageA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_PompageA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_PompageA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_PompageA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_PompageA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_PompageA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_PompageA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_PompageA,
    Transitions := Trans_PompageA,
    Actions := Acts_PompageA
);

(* Retour statut *)
Ctrl_PompageA.Stat.Cycle := Gc_PompageA.Runtime.Cycle;
Ctrl_PompageA.Stat.Phase := Gc_PompageA.Runtime.Phase;
Ctrl_PompageA.Stat.Mode := Gc_PompageA.Runtime.Mode;
Ctrl_PompageA.Stat.ActiveStepCount := Gc_PompageA.Debug.ActiveStepCount;
Ctrl_PompageA.Stat.LastTransitionFiredId := Gc_PompageA.Debug.LastTransitionFiredId;
Ctrl_PompageA.Stat.ConfigFault := Gc_PompageA.Debug.ConfigFault;
Ctrl_PompageA.Stat.WatchdogDetected := Gc_PompageA.Debug.WatchdogDetected;
Ctrl_PompageA.Stat.NoActiveStepFault := Gc_PompageA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_PompageA.Cmd.StepReq := FALSE;
Ctrl_PompageA.Cmd.InitReq := FALSE;
Ctrl_PompageA.Cmd.ResetInitialReq := FALSE;
Ctrl_PompageA.Cmd.ResetAllReq := FALSE;
Ctrl_PompageA.Cmd.ForceBackReq := FALSE;
Ctrl_PompageA.Cmd.ForceNextReq := FALSE;
