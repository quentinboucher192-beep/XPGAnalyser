Gc_PompageB.Runtime.GC_MAX_STEPS := 28;
Gc_PompageB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_PompageB.Runtime.GC_MAX_ACTIONS := 28;
Gc_PompageB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_PompageB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_PompageB.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 								_Step:=Steps_PompageB[0]);
Builder(T:=1,Text:='id=1|n=X1', 									_Step:=Steps_PompageB[1]); 
Builder(T:=1,Text:='id=2|n=X2', 									_Step:=Steps_PompageB[2]); 
Builder(T:=1,Text:='id=3|n=X3', 									_Step:=Steps_PompageB[3]); 
Builder(T:=1,Text:='id=4|n=X4', 									_Step:=Steps_PompageB[4]); 
Builder(T:=1,Text:='id=5|n=X5', 									_Step:=Steps_PompageB[5]); 
Builder(T:=1,Text:='id=6|n=X6', 									_Step:=Steps_PompageB[6]); 
Builder(T:=1,Text:='id=7|n=X7', 									_Step:=Steps_PompageB[7]); 
Builder(T:=1,Text:='id=8|n=X8',									_Step:=Steps_PompageB[8]); 
Builder(T:=1,Text:='id=9|n=X9|f=1', 								_Step:=Steps_PompageB[9]); 
Builder(T:=1,Text:='id=10|n=X10', 									_Step:=Steps_PompageB[10]); 
Builder(T:=1,Text:='id=11|n=X11', 									_Step:=Steps_PompageB[11]); 
Builder(T:=1,Text:='id=12|n=X12', 									_Step:=Steps_PompageB[12]); 
Builder(T:=1,Text:='id=13|n=X13', 									_Step:=Steps_PompageB[13]); 

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=TRUE', 								_Trans:=Trans_PompageB[0]); 
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=PIRANI', 							_Trans:=Trans_PompageB[1]);
Builder(T:=2,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP2)*1000.0), 	_Trans:=Trans_PompageB[1]); 
Builder(T:=2,Text:='id=2|s=1|d=10|c=!PIRANI', 							_Trans:=Trans_PompageB[2]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[2]);
Builder(T:=2,Text:='id=3|s=2|d=3|c=PIRANI', 							_Trans:=Trans_PompageB[3]);
Builder(T:=2,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP4)*1000.0), 	_Trans:=Trans_PompageB[3]);
Builder(T:=2,Text:='id=4|s=2|d=10|c=!PIRANI', 							_Trans:=Trans_PompageB[4]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[4]);
Builder(T:=2,Text:='id=5|s=3|d=4|c=AP', 								_Trans:=Trans_PompageB[5]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[5]);
Builder(T:=2,Text:='id=6|s=3|d=5|c=!AP', 								_Trans:=Trans_PompageB[6]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[6]);
Builder(T:=2,Text:='id=7|s=4|d=5|c=PIRANI', 							_Trans:=Trans_PompageB[7]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[7]);
Builder(T:=2,Text:='id=8|s=4|d=11|c=!PIRANI', 							_Trans:=Trans_PompageB[8]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[8]);
Builder(T:=2,Text:='id=9|s=5|d=6|c=TRUE', 								_Trans:=Trans_PompageB[9]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[9]);
Builder(T:=2,Text:='id=10|s=6|d=7|c=PT1>=SPPE6', 							_Trans:=Trans_PompageB[10]);
Builder(T:=2,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP6)*1000.0), 	_Trans:=Trans_PompageB[10]);
Builder(T:=2,Text:='id=11|s=6|d=12|c=PT1<SPPE6', 							_Trans:=Trans_PompageB[11]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[11]);
Builder(T:=2,Text:='id=12|s=7|d=8|c=PT1<=SPPE5', 							_Trans:=Trans_PompageB[12]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[12]);
Builder(T:=2,Text:='id=13|s=7|d=13|c=PT1>SPPE5', 							_Trans:=Trans_PompageB[13]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[13]);
Builder(T:=2,Text:='id=14|s=8|d=9|c=NB==', 								_Trans:=Trans_PompageB[14]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[14]);
Builder(T:=2,Text:='id=15|s=8|d=6|c=NB!=', 								_Trans:=Trans_PompageB[15]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[15]);
Builder(T:=2,Text:='id=16|s=9|d=0|c=TRUE', 								_Trans:=Trans_PompageB[16]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[16]);
Builder(T:=2,Text:='id=17|s=10|d=0|c=IHM', 								_Trans:=Trans_PompageB[17]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[17]);
Builder(T:=2,Text:='id=18|s=11|d=0|c=IHM', 								_Trans:=Trans_PompageB[18]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[18]);
Builder(T:=2,Text:='id=19|s=12|d=5|c=IHM', 								_Trans:=Trans_PompageB[19]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[19]);
Builder(T:=2,Text:='id=20|s=13|d=0|c=IHM', 								_Trans:=Trans_PompageB[20]);
Builder(T:=2,d:=t#0s, 										_Trans:=Trans_PompageB[20]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=FTTOV4|k=4|s=1', 							_Act:=Acts_PompageB[0]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP1)*1000.0), 	_Act:=Acts_PompageB[0]);
Builder(T:=3,Text:='id=1|n=OPPE|k=11|s=1', 								_Act:=Acts_PompageB[1]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP1)*1000.0), 	_Act:=Acts_PompageB[1]);
Builder(T:=3,Text:='id=2|n=OV9|k=4|s=2', 								_Act:=Acts_PompageB[2]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP3)*1000.0), 	_Act:=Acts_PompageB[2]);
Builder(T:=3,Text:='id=3|n=FPPE|k=0|s=3', 								_Act:=Acts_PompageB[3]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[3]);
Builder(T:=3,Text:='id=4|n=NB=0|k=1|s=5', 								_Act:=Acts_PompageB[4]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[4]);
Builder(T:=3,Text:='id=5|n=NB=FV9OV4NB++|k=1|s=6', 							_Act:=Acts_PompageB[5]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[5]);
Builder(T:=3,Text:='id=6|n=OV2|k=4|s=6', 								_Act:=Acts_PompageB[6]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP6)*1000.0), 	_Act:=Acts_PompageB[6]);
Builder(T:=3,Text:='id=7|n=FV2|k=4|s=7', 								_Act:=Acts_PompageB[7]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP8)*1000.0), 	_Act:=Acts_PompageB[7]);
Builder(T:=3,Text:='id=8|n=OV9|k=11|s=7', 								_Act:=Acts_PompageB[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP9 + config_utilisee.pompage[index_pompage_B].DLP10)*1000.0), 	_Act:=Acts_PompageB[8]);
Builder(T:=3,Text:='id=9|n=FV9|k=4|s=8', 								_Act:=Acts_PompageB[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP11)*1000.0), 	_Act:=Acts_PompageB[9]);
Builder(T:=3,Text:='id=10|n=FV2V9PPE|k=1|s=9', 							_Act:=Acts_PompageB[10]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[10]);
Builder(T:=3,Text:='id=11|n=FTT|k=1|s=10', 								_Act:=Acts_PompageB[11]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[11]);
Builder(T:=3,Text:='id=12|n=FV9|k=4|s=11', 								_Act:=Acts_PompageB[12]);
Builder(T:=3,d:=t#1s, 										_Act:=Acts_PompageB[12]);
Builder(T:=3,Text:='id=13|n=OV2|k=0|s=11', 								_Act:=Acts_PompageB[13]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[13]);
Builder(T:=3,Text:='id=14|n=FTT|k=1|s=12', 								_Act:=Acts_PompageB[14]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[14]);
Builder(T:=3,Text:='id=15|n=FTT|k=1|s=13', 								_Act:=Acts_PompageB[15]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PompageB[15]);

Ctrl_PompageB.Cmd.ForceStepFromId := -1;
Ctrl_PompageB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_PompageB[0].EnableCond :=  TRUE;
Acts_PompageB[1].EnableCond :=  Acts_PompageB[0].Started AND Acts_PompageB[0].Finished;
Acts_PompageB[2].EnableCond :=  TRUE;
Acts_PompageB[3].EnableCond :=  TRUE;
Acts_PompageB[4].EnableCond :=  TRUE;
Acts_PompageB[5].EnableCond :=  TRUE;
Acts_PompageB[6].EnableCond :=  TRUE;
Acts_PompageB[7].EnableCond :=  TRUE;
Acts_PompageB[8].EnableCond :=  Acts_PompageB[7].Started AND Acts_PompageB[7].Finished;
Acts_PompageB[9].EnableCond :=  TRUE;
Acts_PompageB[10].EnableCond := TRUE;
Acts_PompageB[11].EnableCond := TRUE;
Acts_PompageB[12].EnableCond := TRUE;
Acts_PompageB[13].EnableCond := Acts_PompageB[12].Started AND Acts_PompageB[12].Finished;
Acts_PompageB[14].EnableCond := TRUE;
Acts_PompageB[15].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_PompageB[0].Condition  :=  TRUE;
Trans_PompageB[1].Condition  :=  Acts_PompageB[1].Started AND Acts_PompageB[1].Finished AND armoires[1].entrees.pirani;
Trans_PompageB[2].Condition  :=  Acts_PompageB[1].Started AND Acts_PompageB[1].Finished AND NOT(armoires[1].entrees.pirani);
Trans_PompageB[3].Condition  :=  Acts_PompageB[2].Started AND Acts_PompageB[2].Finished AND armoires[1].entrees.pirani;
Trans_PompageB[4].Condition  :=  Acts_PompageB[2].Started AND Acts_PompageB[2].Finished AND NOT(armoires[1].entrees.pirani);
Trans_PompageB[5].Condition  :=  armoires[1].en_cours_de_chgt_apres;
Trans_PompageB[6].Condition  :=  NOT(armoires[1].en_cours_de_chgt_apres);
Trans_PompageB[7].Condition  :=  Steps_PompageB[4].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP5)*1000.0) AND armoires[1].entrees.pirani;
Trans_PompageB[8].Condition  :=  Steps_PompageB[4].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.pompage[index_pompage_B].DLP5)*1000.0) AND NOT(armoires[1].entrees.pirani);
Trans_PompageB[9].Condition  :=  TRUE;
Trans_PompageB[10].Condition  := Acts_PompageB[6].Started AND Acts_PompageB[6].Finished AND armoires[1].ana.PT1.Mes >= INT_TO_REAL(config_utilisee.pompage[index_purge_B].SPPE6);
Trans_PompageB[11].Condition  := Acts_PompageB[6].Started AND Acts_PompageB[6].Finished AND armoires[1].ana.PT1.Mes < INT_TO_REAL(config_utilisee.pompage[index_purge_B].SPPE6);
Trans_PompageB[12].Condition  := Acts_PompageB[8].Started AND Acts_PompageB[8].Finished AND armoires[1].ana.PT1.Mes <= INT_TO_REAL(config_utilisee.pompage[index_purge_B].SPPE5) AND armoires[1].entrees.pirani;
Trans_PompageB[13].Condition  := Acts_PompageB[8].Started AND Acts_PompageB[8].Finished AND NOT(armoires[1].ana.PT1.Mes <= INT_TO_REAL(config_utilisee.pompage[index_purge_B].SPPE5) AND armoires[1].entrees.pirani);
Trans_PompageB[14].Condition  := Acts_PompageB[9].Started AND Acts_PompageB[9].Finished AND armoires[1].compteur_cycle_pompage >= config_utilisee.pompage[index_pompage_B].NBPPE;
Trans_PompageB[15].Condition  := Acts_PompageB[9].Started AND Acts_PompageB[9].Finished AND armoires[1].compteur_cycle_pompage < config_utilisee.pompage[index_pompage_B].NBPPE;
Trans_PompageB[16].Condition  := TRUE;
Trans_PompageB[17].Condition  := chgt_valider[0];
Trans_PompageB[18].Condition  := chgt_valider[0];
Trans_PompageB[19].Condition  := chgt_valider[0];
Trans_PompageB[20].Condition  := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_PompageB.Cmd.ModeReq = 0 THEN Ctrl_PompageB.Cmd.ModeReq := 2; END_IF;
Ctrl_PompageB.Cmd.Enable := TRUE and not(armoires[1].manu);

Gc_PompageB(
    Enable := Ctrl_PompageB.Cmd.Enable,
    ModeReq := Ctrl_PompageB.Cmd.ModeReq,
    FreezeReq := Ctrl_PompageB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_PompageB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_PompageB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_PompageB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_PompageB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_PompageB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_PompageB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_PompageB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_PompageB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_PompageB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_PompageB,
    Transitions := Trans_PompageB,
    Actions := Acts_PompageB
);

(* Retour statut *)
Ctrl_PompageB.Stat.Cycle := Gc_PompageB.Runtime.Cycle;
Ctrl_PompageB.Stat.Phase := Gc_PompageB.Runtime.Phase;
Ctrl_PompageB.Stat.Mode := Gc_PompageB.Runtime.Mode;
Ctrl_PompageB.Stat.ActiveStepCount := Gc_PompageB.Debug.ActiveStepCount;
Ctrl_PompageB.Stat.LastTransitionFiredId := Gc_PompageB.Debug.LastTransitionFiredId;
Ctrl_PompageB.Stat.ConfigFault := Gc_PompageB.Debug.ConfigFault;
Ctrl_PompageB.Stat.WatchdogDetected := Gc_PompageB.Debug.WatchdogDetected;
Ctrl_PompageB.Stat.NoActiveStepFault := Gc_PompageB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_PompageB.Cmd.StepReq := FALSE;
Ctrl_PompageB.Cmd.InitReq := FALSE;
Ctrl_PompageB.Cmd.ResetInitialReq := FALSE;
Ctrl_PompageB.Cmd.ResetAllReq := FALSE;
Ctrl_PompageB.Cmd.ForceBackReq := FALSE;
Ctrl_PompageB.Cmd.ForceNextReq := FALSE;
