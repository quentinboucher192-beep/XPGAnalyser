Gc_ChangementB.Runtime.GC_MAX_STEPS := 28;
Gc_ChangementB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_ChangementB.Runtime.GC_MAX_ACTIONS := 28;
Gc_ChangementB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_ChangementB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_ChangementB.Runtime.GC_MAX_HIST := 8;

if (armoires[1].init_chgt) then
	armoires[1].init_chgt := false;
	armoires[1].en_cours_de_chgt := true;
	armoires[1].en_cours_de_chgt_avant := true;
	Ctrl_ChangementB.Cmd.ModeReq := 0;
	Ctrl_ChangementB.Cmd.InitReq := TRUE;
end_if;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_ChangementB[0]);
Builder(T:=1,Text:='id=1|n=X1', (* fermer toutes les vannes + valider *)	_Step:=Steps_ChangementB[1]); 
Builder(T:=1,Text:='id=2|n=X2', (* fin detoxal *)				_Step:=Steps_ChangementB[2]); 
Builder(T:=1,Text:='id=3|n=X3', (* fin purge *)				_Step:=Steps_ChangementB[3]); 
Builder(T:=1,Text:='id=4|n=X4', (* fin pompage *)				_Step:=Steps_ChangementB[4]); 
Builder(T:=1,Text:='id=5|n=X5', (* fin test helium *)			_Step:=Steps_ChangementB[5]); 
Builder(T:=1,Text:='id=6|n=X6', (* ouvrir V2 V4 + valider *)		_Step:=Steps_ChangementB[6]); 
Builder(T:=1,Text:='id=7|n=X7', (* fermer V2 V4 + valider *)		_Step:=Steps_ChangementB[7]); 
Builder(T:=1,Text:='id=8|n=X8', (* fin detoxal *)				_Step:=Steps_ChangementB[8]); 
Builder(T:=1,Text:='id=9|n=X9', (* fin purge *)				_Step:=Steps_ChangementB[9]); 
Builder(T:=1,Text:='id=10|n=X10', (* fin pompage *)				_Step:=Steps_ChangementB[10]); 
Builder(T:=1,Text:='id=11|n=X11', (* fin test helium *)			_Step:=Steps_ChangementB[11]); 
Builder(T:=1,Text:='id=12|n=X12', (* pression correcte + valider *)		_Step:=Steps_ChangementB[12]); 
Builder(T:=1,Text:='id=13|n=X13', (* pression incorrecte + valider *)	_Step:=Steps_ChangementB[13]); 

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=IHM', 					_Trans:=Trans_ChangementB[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=IHM', 					_Trans:=Trans_ChangementB[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[1]); 
Builder(T:=2,Text:='id=2|s=2|d=3|c=FDETOX', 				_Trans:=Trans_ChangementB[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[2]);
Builder(T:=2,Text:='id=3|s=3|d=4|c=FPURGE', 				_Trans:=Trans_ChangementB[3]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[3]);
Builder(T:=2,Text:='id=4|s=4|d=5|c=FPOMPAGE', 				_Trans:=Trans_ChangementB[4]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[4]);
Builder(T:=2,Text:='id=5|s=5|d=6|c=FHELIUM', 				_Trans:=Trans_ChangementB[5]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[5]);
Builder(T:=2,Text:='id=6|s=6|d=7|c=IHM', 					_Trans:=Trans_ChangementB[6]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[6]);
Builder(T:=2,Text:='id=7|s=7|d=8|c=IHM', 					_Trans:=Trans_ChangementB[7]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[7]);
Builder(T:=2,Text:='id=8|s=8|d=9|c=FDETOX', 				_Trans:=Trans_ChangementB[8]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[8]);
Builder(T:=2,Text:='id=9|s=9|d=10|c=FPURGE', 				_Trans:=Trans_ChangementB[9]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[9]);
Builder(T:=2,Text:='id=10|s=10|d=11|c=FPOMPAGE', 				_Trans:=Trans_ChangementB[10]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[10]);
Builder(T:=2,Text:='id=11|s=11|d=12|c=PT1<=SPV', 				_Trans:=Trans_ChangementB[11]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[11]);
Builder(T:=2,Text:='id=12|s=11|d=13|c=PT1>SPV', 				_Trans:=Trans_ChangementB[12]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[12]);
Builder(T:=2,Text:='id=13|s=12|d=0|c=IHM', 					_Trans:=Trans_ChangementB[13]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[13]);
Builder(T:=2,Text:='id=14|s=13|d=7|c=IHM', 					_Trans:=Trans_ChangementB[14]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_ChangementB[14]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=RAZ|k=1|s=0', 					_Act:=Acts_ChangementB[0]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[0]);
Builder(T:=3,Text:='id=1|n=FTT|k=1|s=1', 					_Act:=Acts_ChangementB[1]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[1]);
Builder(T:=3,Text:='id=2|n=IDETOX|k=1|s=2', 				_Act:=Acts_ChangementB[2]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[2]);
Builder(T:=3,Text:='id=3|n=IPURGE|k=1|s=3', 				_Act:=Acts_ChangementB[3]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[3]);
Builder(T:=3,Text:='id=4|n=IPOMPAGE|k=1|s=4', 				_Act:=Acts_ChangementB[4]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[4]);
Builder(T:=3,Text:='id=5|n=IHELIUM|k=1|s=5', 				_Act:=Acts_ChangementB[5]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[5]);
Builder(T:=3,Text:='id=6|n=OV2V4|k=1|s=6', 					_Act:=Acts_ChangementB[6]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[6]);
Builder(T:=3,Text:='id=7|n=FV2V4|k=1|s=7', 					_Act:=Acts_ChangementB[7]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[7]);
Builder(T:=3,Text:='id=8|n=SEUILS|k=5|s=7', 				_Act:=Acts_ChangementB[8]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[8]);
Builder(T:=3,Text:='id=9|n=IDETOX|k=1|s=8', 				_Act:=Acts_ChangementB[9]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[9]);
Builder(T:=3,Text:='id=10|n=IPURGE|k=1|s=9', 				_Act:=Acts_ChangementB[10]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[10]);
Builder(T:=3,Text:='id=11|n=IPOMPAGE|k=1|s=10', 				_Act:=Acts_ChangementB[11]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[11]);
Builder(T:=3,Text:='id=12|n=IHELIUM|k=1|s=11', 				_Act:=Acts_ChangementB[12]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[12]);
Builder(T:=3,Text:='id=13|n=X12|k=0|s=12', 					_Act:=Acts_ChangementB[13]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[13]);
Builder(T:=3,Text:='id=14|n=X13|k=0|s=13', 					_Act:=Acts_ChangementB[14]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[14]);
Builder(T:=3,Text:='id=15|n=RDETOX|k=5|s=2', 				_Act:=Acts_ChangementB[15]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[15]);
Builder(T:=3,Text:='id=16|n=RPURGE|k=5|s=3', 				_Act:=Acts_ChangementB[16]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[16]);
Builder(T:=3,Text:='id=17|n=RPOMPAGE|k=5|s=4', 				_Act:=Acts_ChangementB[17]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[17]);
Builder(T:=3,Text:='id=18|n=RHELIUM|k=5|s=5', 				_Act:=Acts_ChangementB[18]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[18]);
Builder(T:=3,Text:='id=19|n=RDETOX|k=5|s=8', 				_Act:=Acts_ChangementB[19]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[19]);
Builder(T:=3,Text:='id=20|n=RPURGE|k=5|s=9', 				_Act:=Acts_ChangementB[20]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[20]);
Builder(T:=3,Text:='id=21|n=RPOMPAGE|k=5|s=10', 				_Act:=Acts_ChangementB[21]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[21]);
Builder(T:=3,Text:='id=22|n=RHELIUM|k=5|s=11', 				_Act:=Acts_ChangementB[22]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_ChangementB[22]);


Ctrl_ChangementB.Cmd.ForceStepFromId := -1;
Ctrl_ChangementB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_ChangementB[0].EnableCond := TRUE;
Acts_ChangementB[1].EnableCond := TRUE;
Acts_ChangementB[2].EnableCond := TRUE;
Acts_ChangementB[3].EnableCond := TRUE;
Acts_ChangementB[4].EnableCond := TRUE;
Acts_ChangementB[5].EnableCond := TRUE;
Acts_ChangementB[6].EnableCond := TRUE;
Acts_ChangementB[7].EnableCond := TRUE;
Acts_ChangementB[8].EnableCond := TRUE;
Acts_ChangementB[9].EnableCond := TRUE;
Acts_ChangementB[10].EnableCond := TRUE;
Acts_ChangementB[11].EnableCond := TRUE;
Acts_ChangementB[12].EnableCond := TRUE;
Acts_ChangementB[13].EnableCond := TRUE;
Acts_ChangementB[14].EnableCond := TRUE;
Acts_ChangementB[15].EnableCond := TRUE;
Acts_ChangementB[16].EnableCond := TRUE;
Acts_ChangementB[17].EnableCond := TRUE;
Acts_ChangementB[18].EnableCond := TRUE;
Acts_ChangementB[19].EnableCond := TRUE;
Acts_ChangementB[20].EnableCond := TRUE;
Acts_ChangementB[21].EnableCond := TRUE;
Acts_ChangementB[22].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_ChangementB[0].Condition  := armoires[1].en_cours_de_chgt AND armoires[1].en_cours_de_chgt_avant;
Trans_ChangementB[1].Condition  := chgt_valider[0] and not(armoires[1].retour_sur_chgt) and not(armoires[1].retour_sur_chgt_ap_decl);
Trans_ChangementB[2].Condition  := Steps_ChangementB[2].ActiveTime >= t#1s AND Gc_DetoxalB.Finished OR NOT(generalites.pres_detoxal);
Trans_ChangementB[3].Condition  := Steps_ChangementB[3].ActiveTime >= t#1s AND Gc_PurgeB.Finished;
Trans_ChangementB[4].Condition  := Steps_ChangementB[4].ActiveTime >= t#1s AND Gc_PompageB.Finished;
Trans_ChangementB[5].Condition  := Steps_ChangementB[5].ActiveTime >= t#1s AND Gc_HeliumAVB.Finished OR NOT(generalites.pres_robinets);
Trans_ChangementB[6].Condition  := chgt_valider[0];
Trans_ChangementB[7].Condition  := chgt_valider[0] OR NOT(generalites.conf_poids);
Trans_ChangementB[8].Condition  := Steps_ChangementB[8].ActiveTime >= t#1s AND Gc_DetoxalB.Finished OR NOT(generalites.pres_detoxal);
Trans_ChangementB[9].Condition  := Steps_ChangementB[9].ActiveTime >= t#1s AND Gc_PurgeB.Finished;
Trans_ChangementB[10].Condition  := Steps_ChangementB[10].ActiveTime >= t#1s AND Gc_PompageB.Finished;
Trans_ChangementB[11].Condition  := (Steps_ChangementB[11].ActiveTime >= t#1s AND Gc_HeliumAPB.Finished OR NOT(generalites.pres_robinets)) AND armoires[1].ana.PT1.mes <= INT_TO_REAL(config_utilisee.chgt.SPV);
Trans_ChangementB[12].Condition  := (Steps_ChangementB[11].ActiveTime >= t#1s AND Gc_HeliumAPB.Finished OR NOT(generalites.pres_robinets)) AND armoires[1].ana.PT1.mes > INT_TO_REAL(config_utilisee.chgt.SPV);
Trans_ChangementB[13].Condition  := chgt_valider[0] AND NOT(armoires[1].Entrees.Extraction);
Trans_ChangementB[14].Condition  := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

Ctrl_ChangementB.Cmd.Enable := configuree and not(armoires[1].manu);
Ctrl_ChangementB.Cmd.ModeReq := 1;

Gc_ChangementB(
    Enable := Ctrl_ChangementB.Cmd.Enable,
    ModeReq := Ctrl_ChangementB.Cmd.ModeReq,
    FreezeReq := Ctrl_ChangementB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_ChangementB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_ChangementB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_ChangementB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_ChangementB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_ChangementB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_ChangementB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_ChangementB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_ChangementB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_ChangementB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_ChangementB,
    Transitions := Trans_ChangementB,
    Actions := Acts_ChangementB
);

(* Retour statut *)
Ctrl_ChangementB.Stat.Cycle := Gc_ChangementB.Runtime.Cycle;
Ctrl_ChangementB.Stat.Phase := Gc_ChangementB.Runtime.Phase;
Ctrl_ChangementB.Stat.Mode := Gc_ChangementB.Runtime.Mode;
Ctrl_ChangementB.Stat.ActiveStepCount := Gc_ChangementB.Debug.ActiveStepCount;
Ctrl_ChangementB.Stat.LastTransitionFiredId := Gc_ChangementB.Debug.LastTransitionFiredId;
Ctrl_ChangementB.Stat.ConfigFault := Gc_ChangementB.Debug.ConfigFault;
Ctrl_ChangementB.Stat.WatchdogDetected := Gc_ChangementB.Debug.WatchdogDetected;
Ctrl_ChangementB.Stat.NoActiveStepFault := Gc_ChangementB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_ChangementB.Cmd.StepReq := FALSE;
Ctrl_ChangementB.Cmd.InitReq := FALSE;
Ctrl_ChangementB.Cmd.ResetInitialReq := FALSE;
Ctrl_ChangementB.Cmd.ResetAllReq := FALSE;
Ctrl_ChangementB.Cmd.ForceBackReq := FALSE;
Ctrl_ChangementB.Cmd.ForceNextReq := FALSE;
