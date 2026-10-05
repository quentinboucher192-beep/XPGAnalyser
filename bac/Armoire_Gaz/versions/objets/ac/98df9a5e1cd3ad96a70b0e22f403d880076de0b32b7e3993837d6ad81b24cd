Gc_Surveillance.Runtime.GC_MAX_STEPS := 28;
Gc_Surveillance.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_Surveillance.Runtime.GC_MAX_ACTIONS := 28;
Gc_Surveillance.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_Surveillance.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_Surveillance.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_Surv[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_Surv[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_Surv[2]); 

(* =========================
TRANSITIONS Surv
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=TRUE', 					_Trans:=Trans_Surv[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_Surv[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=SURV_X1.t>=DLSURV', 			_Trans:=Trans_Surv[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_Surv[1]);
Builder(T:=2,Text:='id=2|s=2|d=0|c=TRUE', 					_Trans:=Trans_Surv[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_Surv[2]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=RAZ CPT|k=0|s=0', 				_Act:=Acts_Surv[0]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_Surv[0]);
Builder(T:=3,Text:='id=1|n=VERIF CPT|k=0|s=1', 				_Act:=Acts_Surv[1]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_Surv[1]);
Builder(T:=3,Text:='id=2|n=RAZ CPT|k=5|s=2', 				_Act:=Acts_Surv[2]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_Surv[2]);

Ctrl_Surv.Cmd.ForceStepFromId := -1;
Ctrl_Surv.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_Surv[0].EnableCond := TRUE;
Acts_Surv[1].EnableCond := TRUE;
Acts_Surv[2].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_Surv[0].Condition  := true;
Trans_Surv[1].Condition  := Steps_Surv[1].ActiveTime >= REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURV) * 60.0 * 1000.0);     				
Trans_Surv[2].Condition  := true;

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

Ctrl_Surv.Cmd.Enable := configuree;
Ctrl_Surv.Cmd.ModeReq := 1;

Gc_Surveillance(
    Enable := Ctrl_Surv.Cmd.Enable,
    ModeReq := Ctrl_Surv.Cmd.ModeReq,
    FreezeReq := Ctrl_Surv.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_Surv.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_Surv.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_Surv.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_Surv.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_Surv.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_Surv.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_Surv.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_Surv.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_Surv.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_Surv,
    Transitions := Trans_Surv,
    Actions := Acts_Surv
);

(* Retour statut *)
Ctrl_Surv.Stat.Cycle := Gc_Surveillance.Runtime.Cycle;
Ctrl_Surv.Stat.Phase := Gc_Surveillance.Runtime.Phase;
Ctrl_Surv.Stat.Mode := Gc_Surveillance.Runtime.Mode;
Ctrl_Surv.Stat.ActiveStepCount := Gc_Surveillance.Debug.ActiveStepCount;
Ctrl_Surv.Stat.LastTransitionFiredId := Gc_Surveillance.Debug.LastTransitionFiredId;
Ctrl_Surv.Stat.ConfigFault := Gc_Surveillance.Debug.ConfigFault;
Ctrl_Surv.Stat.WatchdogDetected := Gc_Surveillance.Debug.WatchdogDetected;
Ctrl_Surv.Stat.NoActiveStepFault := Gc_Surveillance.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_Surv.Cmd.StepReq := FALSE;
Ctrl_Surv.Cmd.InitReq := FALSE;
Ctrl_Surv.Cmd.ResetInitialReq := FALSE;
Ctrl_Surv.Cmd.ResetAllReq := FALSE;
Ctrl_Surv.Cmd.ForceBackReq := FALSE;
Ctrl_Surv.Cmd.ForceNextReq := FALSE;
