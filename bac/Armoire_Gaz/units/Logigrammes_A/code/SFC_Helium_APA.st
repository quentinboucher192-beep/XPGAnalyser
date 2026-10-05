Gc_HeliumAPA.Runtime.GC_MAX_STEPS := 28;
Gc_HeliumAPA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_HeliumAPA.Runtime.GC_MAX_ACTIONS := 28;
Gc_HeliumAPA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_HeliumAPA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_HeliumAPA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_HeliumAPA[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_HeliumAPA[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_HeliumAPA[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_HeliumAPA[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_HeliumAPA[4]); 
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_HeliumAPA[5]);
Builder(T:=1,Text:='id=6|n=X6', 						_Step:=Steps_HeliumAPA[6]); 
Builder(T:=1,Text:='id=7|n=X7', 						_Step:=Steps_HeliumAPA[7]); 
Builder(T:=1,Text:='id=8|n=X8', 						_Step:=Steps_HeliumAPA[8]); 
Builder(T:=1,Text:='id=9|n=X9', 						_Step:=Steps_HeliumAPA[9]); 
Builder(T:=1,Text:='id=10|n=X10', 						_Step:=Steps_HeliumAPA[10]);
Builder(T:=1,Text:='id=11|n=X11', 						_Step:=Steps_HeliumAPA[11]);
Builder(T:=1,Text:='id=12|n=X12', 						_Step:=Steps_HeliumAPA[12]);
Builder(T:=1,Text:='id=13|n=X13|f=1', 					_Step:=Steps_HeliumAPA[13]);

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=IHM', 					_Trans:=Trans_HeliumAPA[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=FPURGE', 				_Trans:=Trans_HeliumAPA[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[1]);
Builder(T:=2,Text:='id=2|s=2|d=3|c=FPOMPAGE', 				_Trans:=Trans_HeliumAPA[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[2]);
Builder(T:=2,Text:='id=3|s=3|d=4|c=IHM', 					_Trans:=Trans_HeliumAPA[3]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[3]); 
Builder(T:=2,Text:='id=4|s=4|d=5|c=IHM', 					_Trans:=Trans_HeliumAPA[4]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[4]);
Builder(T:=2,Text:='id=5|s=5|d=6|c=TRUE', 					_Trans:=Trans_HeliumAPA[5]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[5]);
Builder(T:=2,Text:='id=6|s=6|d=7|c=IHM', 					_Trans:=Trans_HeliumAPA[6]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[6]); 
Builder(T:=2,Text:='id=7|s=6|d=5|c=!IHM', 					_Trans:=Trans_HeliumAPA[7]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[7]);
Builder(T:=2,Text:='id=8|s=7|d=8|c=!IHM', 					_Trans:=Trans_HeliumAPA[8]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[8]);
Builder(T:=2,Text:='id=9|s=7|d=4|c=IHM', 					_Trans:=Trans_HeliumAPA[9]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[9]);
Builder(T:=2,Text:='id=10|s=8|d=11|c=!IHM', 				_Trans:=Trans_HeliumAPA[10]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[10]);
Builder(T:=2,Text:='id=11|s=8|d=9|c=IHM', 					_Trans:=Trans_HeliumAPA[11]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[11]);
Builder(T:=2,Text:='id=12|s=9|d=10|c=FPURGE', 				_Trans:=Trans_HeliumAPA[12]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[12]);
Builder(T:=2,Text:='id=13|s=10|d=11|c=FPOMPAGE', 				_Trans:=Trans_HeliumAPA[13]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[13]);
Builder(T:=2,Text:='id=14|s=11|d=12|c=IHM', 				_Trans:=Trans_HeliumAPA[14]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[14]);
Builder(T:=2,Text:='id=15|s=12|d=13|c=IHM', 				_Trans:=Trans_HeliumAPA[15]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[15]);
Builder(T:=2,Text:='id=16|s=13|d=0|c=TRUE', 				_Trans:=Trans_HeliumAPA[16]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAPA[16]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=IPURGE|k=1|s=1', 						_Act:=Acts_HeliumAPA[0]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[0]);
Builder(T:=3,Text:='id=1|n=RPURGE|k=5|s=1', 						_Act:=Acts_HeliumAPA[1]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[1]);
Builder(T:=3,Text:='id=2|n=IPOMPAGE|k=1|s=2', 						_Act:=Acts_HeliumAPA[2]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[2]);
Builder(T:=3,Text:='id=3|n=RPOMPAGE|k=5|s=2', 						_Act:=Acts_HeliumAPA[3]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[3]);
Builder(T:=3,Text:='id=4|n=FV2V4|k=4|s=4', 							_Act:=Acts_HeliumAPA[4]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.helium.DLHE1)*1000.0), 		_Act:=Acts_HeliumAPA[4]);
Builder(T:=3,Text:='id=5|n=FVHE|k=1|s=5', 							_Act:=Acts_HeliumAPA[5]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[5]);
Builder(T:=3,Text:='id=6|n=OV7|k=4|s=6', 							_Act:=Acts_HeliumAPA[6]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.helium.DLHE2)*1000.0), 		_Act:=Acts_HeliumAPA[6]);
Builder(T:=3,Text:='id=7|n=OV3V4|k=11|s=6', 						_Act:=Acts_HeliumAPA[7]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.helium.DLHE3)*1000.0), 		_Act:=Acts_HeliumAPA[7]);
Builder(T:=3,Text:='id=8|n=FV3V4|k=11|s=6', 						_Act:=Acts_HeliumAPA[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.helium.DLHE4)*1000.0), 		_Act:=Acts_HeliumAPA[8]);
Builder(T:=3,Text:='id=9|n=FV7|k=11|s=6', 							_Act:=Acts_HeliumAPA[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.helium.DLHE5)*1000.0), 		_Act:=Acts_HeliumAPA[9]);
Builder(T:=3,Text:='id=10|n=OVHE|k=0|s=6', 							_Act:=Acts_HeliumAPA[10]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[10]);
Builder(T:=3,Text:='id=11|n=FVHE|k=1|s=7', 							_Act:=Acts_HeliumAPA[11]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[11]);
Builder(T:=3,Text:='id=12|n=IPURGE|k=1|s=9', 						_Act:=Acts_HeliumAPA[12]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[12]);
Builder(T:=3,Text:='id=13|n=RPURGE|k=5|s=9', 						_Act:=Acts_HeliumAPA[13]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[13]);
Builder(T:=3,Text:='id=14|n=IPOMPAGE|k=1|s=10', 						_Act:=Acts_HeliumAPA[14]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[14]);
Builder(T:=3,Text:='id=15|n=RPOMPAGE|k=5|s=10', 						_Act:=Acts_HeliumAPA[15]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_HeliumAPA[15]);

Ctrl_HeliumAPA.Cmd.ForceStepFromId := -1;
Ctrl_HeliumAPA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_HeliumAPA[0].EnableCond := TRUE;
Acts_HeliumAPA[1].EnableCond := TRUE;
Acts_HeliumAPA[2].EnableCond := TRUE;
Acts_HeliumAPA[3].EnableCond := TRUE;
Acts_HeliumAPA[4].EnableCond := TRUE;
Acts_HeliumAPA[5].EnableCond := armoires[0].sorties.VHE.F1;
Acts_HeliumAPA[6].EnableCond := TRUE;
Acts_HeliumAPA[7].EnableCond := Acts_HeliumAPA[6].Started AND Acts_HeliumAPA[6].Finished;
Acts_HeliumAPA[8].EnableCond := Acts_HeliumAPA[7].Started AND Acts_HeliumAPA[7].Finished;
Acts_HeliumAPA[9].EnableCond := Acts_HeliumAPA[8].Started AND Acts_HeliumAPA[8].Finished;
Acts_HeliumAPA[10].EnableCond := Acts_HeliumAPA[9].Started AND Acts_HeliumAPA[9].Finished;
Acts_HeliumAPA[11].EnableCond := TRUE;
Acts_HeliumAPA[12].EnableCond := TRUE;
Acts_HeliumAPA[13].EnableCond := TRUE;
Acts_HeliumAPA[14].EnableCond := TRUE;
Acts_HeliumAPA[15].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_HeliumAPA[0].Condition  := chgt_valider[0];
Trans_HeliumAPA[1].Condition  := Steps_HeliumAPA[1].ActiveTime >= t#1s AND Gc_PurgeA.Finished;
Trans_HeliumAPA[2].Condition  := Steps_HeliumAPA[2].ActiveTime >= t#1s AND Gc_PompageA.Finished;
Trans_HeliumAPA[3].Condition  := chgt_valider[0];
Trans_HeliumAPA[4].Condition  := Acts_HeliumAPA[4].Started AND Acts_HeliumAPA[4].Finished AND chgt_valider[0];
Trans_HeliumAPA[5].Condition  := TRUE;
Trans_HeliumAPA[6].Condition  := Acts_HeliumAPA[9].Started AND Acts_HeliumAPA[9].Finished AND chgt_valider[0];
Trans_HeliumAPA[7].Condition  := Acts_HeliumAPA[9].Started AND Acts_HeliumAPA[9].Finished AND chgt_valider[1];
Trans_HeliumAPA[8].Condition  := chgt_valider[1];
Trans_HeliumAPA[9].Condition  := chgt_valider[0];
Trans_HeliumAPA[10].Condition  := chgt_valider[1];
Trans_HeliumAPA[11].Condition  := chgt_valider[0];
Trans_HeliumAPA[12].Condition  := Steps_HeliumAPA[9].ActiveTime >= t#1s AND Gc_PurgeA.Finished;
Trans_HeliumAPA[13].Condition  := Steps_HeliumAPA[10].ActiveTime >= t#1s AND Gc_PompageA.Finished;
Trans_HeliumAPA[14].Condition  := chgt_valider[0];
Trans_HeliumAPA[15].Condition  := chgt_valider[0];
Trans_HeliumAPA[16].Condition  := TRUE;

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_HeliumAPA.Cmd.ModeReq = 0 THEN Ctrl_HeliumAPA.Cmd.ModeReq := 2; END_IF;
Ctrl_HeliumAPA.Cmd.Enable := TRUE and not(armoires[0].manu) and not(modification_majeure);

Gc_HeliumAPA(
    Enable := Ctrl_HeliumAPA.Cmd.Enable,
    ModeReq := Ctrl_HeliumAPA.Cmd.ModeReq,
    FreezeReq := Ctrl_HeliumAPA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_HeliumAPA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_HeliumAPA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_HeliumAPA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_HeliumAPA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_HeliumAPA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_HeliumAPA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_HeliumAPA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_HeliumAPA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_HeliumAPA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_HeliumAPA,
    Transitions := Trans_HeliumAPA,
    Actions := Acts_HeliumAPA
);

(* Retour statut *)
Ctrl_HeliumAPA.Stat.Cycle := Gc_HeliumAPA.Runtime.Cycle;
Ctrl_HeliumAPA.Stat.Phase := Gc_HeliumAPA.Runtime.Phase;
Ctrl_HeliumAPA.Stat.Mode := Gc_HeliumAPA.Runtime.Mode;
Ctrl_HeliumAPA.Stat.ActiveStepCount := Gc_HeliumAPA.Debug.ActiveStepCount;
Ctrl_HeliumAPA.Stat.LastTransitionFiredId := Gc_HeliumAPA.Debug.LastTransitionFiredId;
Ctrl_HeliumAPA.Stat.ConfigFault := Gc_HeliumAPA.Debug.ConfigFault;
Ctrl_HeliumAPA.Stat.WatchdogDetected := Gc_HeliumAPA.Debug.WatchdogDetected;
Ctrl_HeliumAPA.Stat.NoActiveStepFault := Gc_HeliumAPA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_HeliumAPA.Cmd.StepReq := FALSE;
Ctrl_HeliumAPA.Cmd.InitReq := FALSE;
Ctrl_HeliumAPA.Cmd.ResetInitialReq := FALSE;
Ctrl_HeliumAPA.Cmd.ResetAllReq := FALSE;
Ctrl_HeliumAPA.Cmd.ForceBackReq := FALSE;
Ctrl_HeliumAPA.Cmd.ForceNextReq := FALSE;
