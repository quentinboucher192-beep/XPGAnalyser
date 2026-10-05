Gc_PurgeA.Runtime.GC_MAX_STEPS := 28;
Gc_PurgeA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_PurgeA.Runtime.GC_MAX_ACTIONS := 28;
Gc_PurgeA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_PurgeA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_PurgeA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_PurgeA[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_PurgeA[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_PurgeA[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_PurgeA[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_PurgeA[4]); 
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_PurgeA[5]); 
Builder(T:=1,Text:='id=6|n=X6', 						_Step:=Steps_PurgeA[6]); 
Builder(T:=1,Text:='id=7|n=X7', 						_Step:=Steps_PurgeA[7]); 
Builder(T:=1,Text:='id=8|n=X8|f=1',						_Step:=Steps_PurgeA[8]); 
Builder(T:=1,Text:='id=9|n=X9', 						_Step:=Steps_PurgeA[9]); 
Builder(T:=1,Text:='id=10|n=X10', 						_Step:=Steps_PurgeA[10]); 

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=2|c=N2', 							_Trans:=Trans_PurgeA[0]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[0]);
Builder(T:=2,Text:='id=1|s=0|d=1|c=!N2', 							_Trans:=Trans_PurgeA[1]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[1]); 
Builder(T:=2,Text:='id=2|s=1|d=2|c=N2+IHM', 						_Trans:=Trans_PurgeA[2]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[2]);
Builder(T:=2,Text:='id=3|s=2|d=3|c=PT1<=SPUR4', 						_Trans:=Trans_PurgeA[3]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[3]);
Builder(T:=2,Text:='id=4|s=2|d=10|c=PT1>SPUR4', 						_Trans:=Trans_PurgeA[4]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[4]);
Builder(T:=2,Text:='id=5|s=3|d=4|c=PT1<=SPUR5', 						_Trans:=Trans_PurgeA[5]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[5]);
Builder(T:=2,Text:='id=6|s=3|d=10|c=PT1>SPUR5', 						_Trans:=Trans_PurgeA[6]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[6]);
Builder(T:=2,Text:='id=7|s=4|d=5|c=TRUE', 							_Trans:=Trans_PurgeA[7]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[7]);
Builder(T:=2,Text:='id=8|s=5|d=6|c=PT1<=SPUR6', 						_Trans:=Trans_PurgeA[8]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[8]);
Builder(T:=2,Text:='id=9|s=5|d=9|c=PT1>SPUR6', 						_Trans:=Trans_PurgeA[9]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[9]);
Builder(T:=2,Text:='id=10|s=6|d=7|c=PT1<=SPUR5', 						_Trans:=Trans_PurgeA[10]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[10]);
Builder(T:=2,Text:='id=11|s=6|d=10|c=PT1>SPUR5', 						_Trans:=Trans_PurgeA[11]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[11]);
Builder(T:=2,Text:='id=12|s=7|d=8|c=NB==NBSURP', 						_Trans:=Trans_PurgeA[12]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[12]);
Builder(T:=2,Text:='id=13|s=7|d=5|c=NB!=NBSURP', 						_Trans:=Trans_PurgeA[13]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[13]);
Builder(T:=2,Text:='id=14|s=8|d=0|c=TRUE', 							_Trans:=Trans_PurgeA[14]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[14]);
Builder(T:=2,Text:='id=15|s=9|d=0|c=IHM', 							_Trans:=Trans_PurgeA[15]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[15]);
Builder(T:=2,Text:='id=16|s=10|d=0|c=IHM', 							_Trans:=Trans_PurgeA[16]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeA[16]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=OV7|k=4|s=2', 								_Act:=Acts_PurgeA[0]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL4)*1000.0), 	_Act:=Acts_PurgeA[0]);
Builder(T:=3,Text:='id=1|n=OV3V4|k=11|s=2', 							_Act:=Acts_PurgeA[1]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL5)*1000.0), 	_Act:=Acts_PurgeA[1]);
Builder(T:=3,Text:='id=2|n=FV3|k=4|s=4', 								_Act:=Acts_PurgeA[2]);
Builder(T:=3,d:=t#5s, 										_Act:=Acts_PurgeA[2]);
Builder(T:=3,Text:='id=3|n=OV2|k=4|s=5', 								_Act:=Acts_PurgeA[3]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL7)*1000.0),		_Act:=Acts_PurgeA[3]);
Builder(T:=3,Text:='id=4|n=NB++|k=1|s=5', 								_Act:=Acts_PurgeA[4]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeA[4]);
Builder(T:=3,Text:='id=5|n=ATT|k=4|s=6', 								_Act:=Acts_PurgeA[5]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL8)*1000.0),		_Act:=Acts_PurgeA[5]);
Builder(T:=3,Text:='id=6|n=FV2|k=11|s=6', 								_Act:=Acts_PurgeA[6]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL9)*1000.0),		_Act:=Acts_PurgeA[6]);
Builder(T:=3,Text:='id=7|n=OV3|k=11|s=6', 								_Act:=Acts_PurgeA[7]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL10)*1000.0),	_Act:=Acts_PurgeA[7]);
Builder(T:=3,Text:='id=8|n=ATT|k=4|s=7', 								_Act:=Acts_PurgeA[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL11)*1000.0),	_Act:=Acts_PurgeA[8]);
Builder(T:=3,Text:='id=9|n=FV3|k=11|s=7', 								_Act:=Acts_PurgeA[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL12)*1000.0),	_Act:=Acts_PurgeA[9]);
Builder(T:=3,Text:='id=10|n=FV7|k=0|s=8', 								_Act:=Acts_PurgeA[10]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeA[10]);
Builder(T:=3,Text:='id=11|n=FTT|k=0|s=9', 								_Act:=Acts_PurgeA[11]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeA[11]);
Builder(T:=3,Text:='id=12|n=FTT|k=0|s=10', 								_Act:=Acts_PurgeA[12]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeA[12]);

Ctrl_PurgeA.Cmd.ForceStepFromId := -1;
Ctrl_PurgeA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_PurgeA[0].EnableCond := TRUE;
Acts_PurgeA[1].EnableCond := Acts_PurgeA[0].Started AND Acts_PurgeA[0].Finished;
Acts_PurgeA[2].EnableCond := TRUE;
Acts_PurgeA[3].EnableCond := TRUE;
Acts_PurgeA[4].EnableCond := TRUE;
Acts_PurgeA[5].EnableCond := TRUE;
Acts_PurgeA[6].EnableCond := Acts_PurgeA[5].Started AND Acts_PurgeA[5].Finished;
Acts_PurgeA[7].EnableCond := Acts_PurgeA[6].Started AND Acts_PurgeA[6].Finished;
Acts_PurgeA[8].EnableCond := TRUE;
Acts_PurgeA[9].EnableCond := Acts_PurgeA[8].Started AND Acts_PurgeA[8].Finished;
Acts_PurgeA[10].EnableCond := TRUE;
Acts_PurgeA[11].EnableCond := TRUE;
Acts_PurgeA[12].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_PurgeA[0].Condition  := armoires[0].entrees.span_N2;
Trans_PurgeA[1].Condition  := NOT(armoires[0].entrees.span_N2);
Trans_PurgeA[2].Condition  := armoires[0].entrees.span_N2 and chgt_valider[0];
Trans_PurgeA[3].Condition  := Acts_PurgeA[1].Started AND Acts_PurgeA[1].Finished AND armoires[0].ana.PT1.mes <= INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR4);
Trans_PurgeA[4].Condition  := Acts_PurgeA[1].Started AND Acts_PurgeA[1].Finished AND armoires[0].ana.PT1.mes > INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR4);
Trans_PurgeA[5].Condition  := Steps_PurgeA[3].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL6)*1000.0) AND armoires[0].ana.PT1.mes <= INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR5);
Trans_PurgeA[6].Condition  := Steps_PurgeA[3].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_A].DL6)*1000.0) AND armoires[0].ana.PT1.mes > INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR5);
Trans_PurgeA[7].Condition  := Steps_PurgeA[4].ActiveTime >= t#5s;
Trans_PurgeA[8].Condition  := Acts_PurgeA[3].Started AND Acts_PurgeA[3].Finished AND armoires[0].ana.PT1.mes >= INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR6);
Trans_PurgeA[9].Condition  := Acts_PurgeA[3].Started AND Acts_PurgeA[3].Finished AND armoires[0].ana.PT1.mes < INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR6);
Trans_PurgeA[10].Condition  := Acts_PurgeA[7].Started AND Acts_PurgeA[7].Finished AND armoires[0].ana.PT1.mes <= INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR5);
Trans_PurgeA[11].Condition  := Acts_PurgeA[7].Started AND Acts_PurgeA[7].Finished AND armoires[0].ana.PT1.mes > INT_TO_REAL(config_utilisee.purge[index_purge_A].SPUR5);
Trans_PurgeA[12].Condition  := Acts_PurgeA[9].Started AND Acts_PurgeA[9].Finished AND armoires[0].compteur_cycle_purge >= config_utilisee.purge[index_purge_A].NBPUR;
Trans_PurgeA[13].Condition  := Acts_PurgeA[9].Started AND Acts_PurgeA[9].Finished AND armoires[0].compteur_cycle_purge < config_utilisee.purge[index_purge_A].NBPUR;
Trans_PurgeA[14].Condition  := TRUE;
Trans_PurgeA[15].Condition  := chgt_valider[0];
Trans_PurgeA[16].Condition  := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_PurgeA.Cmd.ModeReq = 0 THEN Ctrl_PurgeA.Cmd.ModeReq := 2; END_IF;
Ctrl_PurgeA.Cmd.Enable := TRUE and not(armoires[0].manu) and not(modification_majeure);

Gc_PurgeA(
    Enable := Ctrl_PurgeA.Cmd.Enable,
    ModeReq := Ctrl_PurgeA.Cmd.ModeReq,
    FreezeReq := Ctrl_PurgeA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_PurgeA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_PurgeA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_PurgeA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_PurgeA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_PurgeA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_PurgeA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_PurgeA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_PurgeA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_PurgeA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_PurgeA,
    Transitions := Trans_PurgeA,
    Actions := Acts_PurgeA
);

(* Retour statut *)
Ctrl_PurgeA.Stat.Cycle := Gc_PurgeA.Runtime.Cycle;
Ctrl_PurgeA.Stat.Phase := Gc_PurgeA.Runtime.Phase;
Ctrl_PurgeA.Stat.Mode := Gc_PurgeA.Runtime.Mode;
Ctrl_PurgeA.Stat.ActiveStepCount := Gc_PurgeA.Debug.ActiveStepCount;
Ctrl_PurgeA.Stat.LastTransitionFiredId := Gc_PurgeA.Debug.LastTransitionFiredId;
Ctrl_PurgeA.Stat.ConfigFault := Gc_PurgeA.Debug.ConfigFault;
Ctrl_PurgeA.Stat.WatchdogDetected := Gc_PurgeA.Debug.WatchdogDetected;
Ctrl_PurgeA.Stat.NoActiveStepFault := Gc_PurgeA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_PurgeA.Cmd.StepReq := FALSE;
Ctrl_PurgeA.Cmd.InitReq := FALSE;
Ctrl_PurgeA.Cmd.ResetInitialReq := FALSE;
Ctrl_PurgeA.Cmd.ResetAllReq := FALSE;
Ctrl_PurgeA.Cmd.ForceBackReq := FALSE;
Ctrl_PurgeA.Cmd.ForceNextReq := FALSE;
