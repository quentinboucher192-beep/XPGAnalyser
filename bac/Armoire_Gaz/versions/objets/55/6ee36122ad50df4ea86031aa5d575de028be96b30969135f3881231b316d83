Gc_PurgeB.Runtime.GC_MAX_STEPS := 28;
Gc_PurgeB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_PurgeB.Runtime.GC_MAX_ACTIONS := 28;
Gc_PurgeB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_PurgeB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_PurgeB.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_PurgeB[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_PurgeB[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_PurgeB[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_PurgeB[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_PurgeB[4]); 
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_PurgeB[5]); 
Builder(T:=1,Text:='id=6|n=X6', 						_Step:=Steps_PurgeB[6]); 
Builder(T:=1,Text:='id=7|n=X7', 						_Step:=Steps_PurgeB[7]); 
Builder(T:=1,Text:='id=8|n=X8|f=1',						_Step:=Steps_PurgeB[8]); 
Builder(T:=1,Text:='id=9|n=X9', 						_Step:=Steps_PurgeB[9]); 
Builder(T:=1,Text:='id=10|n=X10', 						_Step:=Steps_PurgeB[10]); 

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=2|c=N2', 							_Trans:=Trans_PurgeB[0]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[0]);
Builder(T:=2,Text:='id=1|s=0|d=1|c=!N2', 							_Trans:=Trans_PurgeB[1]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[1]); 
Builder(T:=2,Text:='id=2|s=1|d=2|c=N2+IHM', 						_Trans:=Trans_PurgeB[2]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[2]);
Builder(T:=2,Text:='id=3|s=2|d=3|c=PT1<=SPUR4', 						_Trans:=Trans_PurgeB[3]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[3]);
Builder(T:=2,Text:='id=4|s=2|d=10|c=PT1>SPUR4', 						_Trans:=Trans_PurgeB[4]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[4]);
Builder(T:=2,Text:='id=5|s=3|d=4|c=PT1<=SPUR5', 						_Trans:=Trans_PurgeB[5]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[5]);
Builder(T:=2,Text:='id=6|s=3|d=10|c=PT1>SPUR5', 						_Trans:=Trans_PurgeB[6]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[6]);
Builder(T:=2,Text:='id=7|s=4|d=5|c=TRUE', 							_Trans:=Trans_PurgeB[7]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[7]);
Builder(T:=2,Text:='id=8|s=5|d=6|c=PT1<=SPUR6', 						_Trans:=Trans_PurgeB[8]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[8]);
Builder(T:=2,Text:='id=9|s=5|d=9|c=PT1>SPUR6', 						_Trans:=Trans_PurgeB[9]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[9]);
Builder(T:=2,Text:='id=10|s=6|d=7|c=PT1<=SPUR5', 						_Trans:=Trans_PurgeB[10]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[10]);
Builder(T:=2,Text:='id=11|s=6|d=10|c=PT1>SPUR5', 						_Trans:=Trans_PurgeB[11]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[11]);
Builder(T:=2,Text:='id=12|s=7|d=8|c=NB==NBSURP', 						_Trans:=Trans_PurgeB[12]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[12]);
Builder(T:=2,Text:='id=13|s=7|d=5|c=NB!=NBSURP', 						_Trans:=Trans_PurgeB[13]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[13]);
Builder(T:=2,Text:='id=14|s=8|d=0|c=TRUE', 							_Trans:=Trans_PurgeB[14]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[14]);
Builder(T:=2,Text:='id=15|s=9|d=0|c=IHM', 							_Trans:=Trans_PurgeB[15]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[15]);
Builder(T:=2,Text:='id=16|s=10|d=0|c=IHM', 							_Trans:=Trans_PurgeB[16]);
Builder(T:=2,d:=t#0s, 									_Trans:=Trans_PurgeB[16]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=OV7|k=4|s=2', 								_Act:=Acts_PurgeB[0]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL4)*1000.0), 	_Act:=Acts_PurgeB[0]);
Builder(T:=3,Text:='id=1|n=OV3V4|k=11|s=2', 							_Act:=Acts_PurgeB[1]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL5)*1000.0), 	_Act:=Acts_PurgeB[1]);
Builder(T:=3,Text:='id=2|n=FV3|k=4|s=4', 								_Act:=Acts_PurgeB[2]);
Builder(T:=3,d:=t#5s, 										_Act:=Acts_PurgeB[2]);
Builder(T:=3,Text:='id=3|n=OV2|k=4|s=5', 								_Act:=Acts_PurgeB[3]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL7)*1000.0),		_Act:=Acts_PurgeB[3]);
Builder(T:=3,Text:='id=4|n=NB++|k=1|s=5', 								_Act:=Acts_PurgeB[4]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeB[4]);
Builder(T:=3,Text:='id=5|n=ATT|k=4|s=6', 								_Act:=Acts_PurgeB[5]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL8)*1000.0),		_Act:=Acts_PurgeB[5]);
Builder(T:=3,Text:='id=6|n=FV2|k=11|s=6', 								_Act:=Acts_PurgeB[6]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL9)*1000.0),		_Act:=Acts_PurgeB[6]);
Builder(T:=3,Text:='id=7|n=OV3|k=11|s=6', 								_Act:=Acts_PurgeB[7]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL10)*1000.0),	_Act:=Acts_PurgeB[7]);
Builder(T:=3,Text:='id=8|n=ATT|k=4|s=7', 								_Act:=Acts_PurgeB[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL11)*1000.0),	_Act:=Acts_PurgeB[8]);
Builder(T:=3,Text:='id=9|n=FV3|k=11|s=7', 								_Act:=Acts_PurgeB[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL12)*1000.0),	_Act:=Acts_PurgeB[9]);
Builder(T:=3,Text:='id=10|n=FV7|k=0|s=8', 								_Act:=Acts_PurgeB[10]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeB[10]);
Builder(T:=3,Text:='id=11|n=FTT|k=0|s=9', 								_Act:=Acts_PurgeB[11]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeB[11]);
Builder(T:=3,Text:='id=12|n=FTT|k=0|s=10', 								_Act:=Acts_PurgeB[12]);
Builder(T:=3,d:=t#0s, 										_Act:=Acts_PurgeB[12]);

Ctrl_PurgeB.Cmd.ForceStepFromId := -1;
Ctrl_PurgeB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_PurgeB[0].EnableCond := TRUE;
Acts_PurgeB[1].EnableCond := Acts_PurgeB[0].Started AND Acts_PurgeB[0].Finished;
Acts_PurgeB[2].EnableCond := TRUE;
Acts_PurgeB[3].EnableCond := TRUE;
Acts_PurgeB[4].EnableCond := TRUE;
Acts_PurgeB[5].EnableCond := TRUE;
Acts_PurgeB[6].EnableCond := Acts_PurgeB[5].Started AND Acts_PurgeB[5].Finished;
Acts_PurgeB[7].EnableCond := Acts_PurgeB[6].Started AND Acts_PurgeB[6].Finished;
Acts_PurgeB[8].EnableCond := TRUE;
Acts_PurgeB[9].EnableCond := Acts_PurgeB[8].Started AND Acts_PurgeB[8].Finished;
Acts_PurgeB[10].EnableCond := TRUE;
Acts_PurgeB[11].EnableCond := TRUE;
Acts_PurgeB[12].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_PurgeB[0].Condition  := armoires[1].entrees.span_N2;
Trans_PurgeB[1].Condition  := NOT(armoires[1].entrees.span_N2);
Trans_PurgeB[2].Condition  := armoires[1].entrees.span_N2 and chgt_valider[0];
Trans_PurgeB[3].Condition  := Acts_PurgeB[1].Started AND Acts_PurgeB[1].Finished AND armoires[1].ana.PT1.mes <= INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR4);
Trans_PurgeB[4].Condition  := Acts_PurgeB[1].Started AND Acts_PurgeB[1].Finished AND armoires[1].ana.PT1.mes > INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR4);
Trans_PurgeB[5].Condition  := Steps_PurgeB[3].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL6)*1000.0) AND armoires[1].ana.PT1.mes <= INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR5);
Trans_PurgeB[6].Condition  := Steps_PurgeB[3].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.purge[index_purge_B].DL6)*1000.0) AND armoires[1].ana.PT1.mes > INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR5);
Trans_PurgeB[7].Condition  := Steps_PurgeB[4].ActiveTime >= t#5s;
Trans_PurgeB[8].Condition  := Acts_PurgeB[3].Started AND Acts_PurgeB[3].Finished AND armoires[1].ana.PT1.mes >= INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR6);
Trans_PurgeB[9].Condition  := Acts_PurgeB[3].Started AND Acts_PurgeB[3].Finished AND armoires[1].ana.PT1.mes < INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR6);
Trans_PurgeB[10].Condition  := Acts_PurgeB[7].Started AND Acts_PurgeB[7].Finished AND armoires[1].ana.PT1.mes <= INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR5);
Trans_PurgeB[11].Condition  := Acts_PurgeB[7].Started AND Acts_PurgeB[7].Finished AND armoires[1].ana.PT1.mes > INT_TO_REAL(config_utilisee.purge[index_purge_B].SPUR5);
Trans_PurgeB[12].Condition  := Acts_PurgeB[9].Started AND Acts_PurgeB[9].Finished AND armoires[1].compteur_cycle_purge >= config_utilisee.purge[index_purge_B].NBPUR;
Trans_PurgeB[13].Condition  := Acts_PurgeB[9].Started AND Acts_PurgeB[9].Finished AND armoires[1].compteur_cycle_purge < config_utilisee.purge[index_purge_B].NBPUR;
Trans_PurgeB[14].Condition  := TRUE;
Trans_PurgeB[15].Condition  := chgt_valider[0];
Trans_PurgeB[16].Condition  := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_PurgeB.Cmd.ModeReq = 0 THEN Ctrl_PurgeB.Cmd.ModeReq := 2; END_IF;
Ctrl_PurgeB.Cmd.Enable := TRUE and not(armoires[1].manu);

Gc_PurgeB(
    Enable := Ctrl_PurgeB.Cmd.Enable,
    ModeReq := Ctrl_PurgeB.Cmd.ModeReq,
    FreezeReq := Ctrl_PurgeB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_PurgeB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_PurgeB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_PurgeB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_PurgeB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_PurgeB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_PurgeB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_PurgeB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_PurgeB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_PurgeB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_PurgeB,
    Transitions := Trans_PurgeB,
    Actions := Acts_PurgeB
);

(* Retour statut *)
Ctrl_PurgeB.Stat.Cycle := Gc_PurgeB.Runtime.Cycle;
Ctrl_PurgeB.Stat.Phase := Gc_PurgeB.Runtime.Phase;
Ctrl_PurgeB.Stat.Mode := Gc_PurgeB.Runtime.Mode;
Ctrl_PurgeB.Stat.ActiveStepCount := Gc_PurgeB.Debug.ActiveStepCount;
Ctrl_PurgeB.Stat.LastTransitionFiredId := Gc_PurgeB.Debug.LastTransitionFiredId;
Ctrl_PurgeB.Stat.ConfigFault := Gc_PurgeB.Debug.ConfigFault;
Ctrl_PurgeB.Stat.WatchdogDetected := Gc_PurgeB.Debug.WatchdogDetected;
Ctrl_PurgeB.Stat.NoActiveStepFault := Gc_PurgeB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_PurgeB.Cmd.StepReq := FALSE;
Ctrl_PurgeB.Cmd.InitReq := FALSE;
Ctrl_PurgeB.Cmd.ResetInitialReq := FALSE;
Ctrl_PurgeB.Cmd.ResetAllReq := FALSE;
Ctrl_PurgeB.Cmd.ForceBackReq := FALSE;
Ctrl_PurgeB.Cmd.ForceNextReq := FALSE;
