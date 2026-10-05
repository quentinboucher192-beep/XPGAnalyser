Gc_ManuB.Runtime.GC_MAX_STEPS := 28;
Gc_ManuB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_ManuB.Runtime.GC_MAX_ACTIONS := 28;
Gc_ManuB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_ManuB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_ManuB.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_ManuB[0]);
Builder(T:=1,Text:='id=1|n=X1', (* mode auto *)				_Step:=Steps_ManuB[1]); 
Builder(T:=1,Text:='id=2|n=X2', (* mode manu *)				_Step:=Steps_ManuB[2]);  

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=AUTO', 					_Trans:=Trans_ManuB[0]);
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ManuB[0]); 
Builder(T:=2,Text:='id=1|s=1|d=2|c=MANU', 					_Trans:=Trans_ManuB[1]);
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ManuB[1]); 
Builder(T:=2,Text:='id=2|s=2|d=0|c=AUTO', 					_Trans:=Trans_ManuB[2]);
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ManuB[2]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=FEAUTO|k=5|s=1', 				_Act:=Acts_ManuB[0]);
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ManuB[0]);
Builder(T:=3,Text:='id=1|n=FEMANU|k=5|s=2', 				_Act:=Acts_ManuB[1]);
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ManuB[1]);
Builder(T:=3,Text:='id=2|n=ENMANU|k=0|s=2', 				_Act:=Acts_ManuB[2]);
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ManuB[2]);


Ctrl_ManuB.Cmd.ForceStepFromId := -1;
Ctrl_ManuB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_ManuB[0].EnableCond := TRUE;
Acts_ManuB[1].EnableCond := TRUE;
Acts_ManuB[2].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_ManuB[0].Condition  := not(armoires[1].manu);
Trans_ManuB[1].Condition  := armoires[1].manu;
Trans_ManuB[2].Condition  := not(armoires[1].manu);

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

Ctrl_ManuB.Cmd.Enable := configuree;
Ctrl_ManuB.Cmd.ModeReq := 1;

Gc_ManuB(
    Enable := Ctrl_ManuB.Cmd.Enable,
    ModeReq := Ctrl_ManuB.Cmd.ModeReq,
    FreezeReq := Ctrl_ManuB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_ManuB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_ManuB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_ManuB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_ManuB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_ManuB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_ManuB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_ManuB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_ManuB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_ManuB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_ManuB,
    Transitions := Trans_ManuB,
    Actions := Acts_ManuB
);

(* Retour statut *)
Ctrl_ManuB.Stat.Cycle := Gc_ManuB.Runtime.Cycle;
Ctrl_ManuB.Stat.Phase := Gc_ManuB.Runtime.Phase;
Ctrl_ManuB.Stat.Mode := Gc_ManuB.Runtime.Mode;
Ctrl_ManuB.Stat.ActiveStepCount := Gc_ManuB.Debug.ActiveStepCount;
Ctrl_ManuB.Stat.LastTransitionFiredId := Gc_ManuB.Debug.LastTransitionFiredId;
Ctrl_ManuB.Stat.ConfigFault := Gc_ManuB.Debug.ConfigFault;
Ctrl_ManuB.Stat.WatchdogDetected := Gc_ManuB.Debug.WatchdogDetected;
Ctrl_ManuB.Stat.NoActiveStepFault := Gc_ManuB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_ManuB.Cmd.StepReq := FALSE;
Ctrl_ManuB.Cmd.InitReq := FALSE;
Ctrl_ManuB.Cmd.ResetInitialReq := FALSE;
Ctrl_ManuB.Cmd.ResetAllReq := FALSE;
Ctrl_ManuB.Cmd.ForceBackReq := FALSE;
Ctrl_ManuB.Cmd.ForceNextReq := FALSE;
