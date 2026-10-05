Gc_ManuA.Runtime.GC_MAX_STEPS := 28;
Gc_ManuA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_ManuA.Runtime.GC_MAX_ACTIONS := 28;
Gc_ManuA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_ManuA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_ManuA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_ManuA[0]);
Builder(T:=1,Text:='id=1|n=X1', (* mode auto *)				_Step:=Steps_ManuA[1]); 
Builder(T:=1,Text:='id=2|n=X2', (* mode manu *)				_Step:=Steps_ManuA[2]);  

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=AUTO', 					_Trans:=Trans_ManuA[0]);
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ManuA[0]); 
Builder(T:=2,Text:='id=1|s=1|d=2|c=MANU', 					_Trans:=Trans_ManuA[1]);
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ManuA[1]); 
Builder(T:=2,Text:='id=2|s=2|d=0|c=AUTO', 					_Trans:=Trans_ManuA[2]);
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ManuA[2]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=FEAUTO|k=5|s=1', 				_Act:=Acts_ManuA[0]);
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ManuA[0]);
Builder(T:=3,Text:='id=1|n=FEMANU|k=5|s=2', 				_Act:=Acts_ManuA[1]);
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ManuA[1]);
Builder(T:=3,Text:='id=2|n=ENMANU|k=0|s=2', 				_Act:=Acts_ManuA[2]);
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ManuA[2]);


Ctrl_ManuA.Cmd.ForceStepFromId := -1;
Ctrl_ManuA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_ManuA[0].EnableCond := TRUE;
Acts_ManuA[1].EnableCond := TRUE;
Acts_ManuA[2].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_ManuA[0].Condition  := not(armoires[0].manu);
Trans_ManuA[1].Condition  := armoires[0].manu;
Trans_ManuA[2].Condition  := not(armoires[0].manu);

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

Ctrl_ManuA.Cmd.Enable := configuree and not(modification_majeure);
Ctrl_ManuA.Cmd.ModeReq := 1;

Gc_ManuA(
    Enable := Ctrl_ManuA.Cmd.Enable,
    ModeReq := Ctrl_ManuA.Cmd.ModeReq,
    FreezeReq := Ctrl_ManuA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_ManuA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_ManuA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_ManuA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_ManuA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_ManuA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_ManuA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_ManuA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_ManuA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_ManuA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_ManuA,
    Transitions := Trans_ManuA,
    Actions := Acts_ManuA
);

(* Retour statut *)
Ctrl_ManuA.Stat.Cycle := Gc_ManuA.Runtime.Cycle;
Ctrl_ManuA.Stat.Phase := Gc_ManuA.Runtime.Phase;
Ctrl_ManuA.Stat.Mode := Gc_ManuA.Runtime.Mode;
Ctrl_ManuA.Stat.ActiveStepCount := Gc_ManuA.Debug.ActiveStepCount;
Ctrl_ManuA.Stat.LastTransitionFiredId := Gc_ManuA.Debug.LastTransitionFiredId;
Ctrl_ManuA.Stat.ConfigFault := Gc_ManuA.Debug.ConfigFault;
Ctrl_ManuA.Stat.WatchdogDetected := Gc_ManuA.Debug.WatchdogDetected;
Ctrl_ManuA.Stat.NoActiveStepFault := Gc_ManuA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_ManuA.Cmd.StepReq := FALSE;
Ctrl_ManuA.Cmd.InitReq := FALSE;
Ctrl_ManuA.Cmd.ResetInitialReq := FALSE;
Ctrl_ManuA.Cmd.ResetAllReq := FALSE;
Ctrl_ManuA.Cmd.ForceBackReq := FALSE;
Ctrl_ManuA.Cmd.ForceNextReq := FALSE;
