(* Section generee par ImporterReports. A placer en QUEUE de tache. *)

(* TM   P0 - Fbk   %MW1000:X5 *)
%MW1000:X5 := Pompes[0].Fbk;

(* TM   P0 - Thermal   %MW1000:X6 *)
%MW1000:X6 := Pompes[0].Thermal;

(* TM   P0 - Run   %MW1000:X7 *)
%MW1000:X7 := Pompes[0].Run;

(* TM   P0 - Running   %MW1000:X8 *)
%MW1000:X8 := Pompes[0].Running;

(* TM   P0 - HasFbk   %MW1000:X9 *)
%MW1000:X9 := Pompes[0].HasFbk;

(* TM   P0 - SuctionOk   %MW1000:X10 *)
%MW1000:X10 := Pompes[0].SuctionOk;

(* TM   P0 - Pressure   %MW1006 *)
%MW1006 := REAL_TO_INT(Pompes[0].Pressure);

(* TM   P0 - MaxPressure   %MW1007 *)
%MW1007 := REAL_TO_INT(Pompes[0].MaxPressure);

(* TA   P0 - Fault   %MW1000:X11 *)
%MW1000:X11 := Pompes[0].Fault;

(* TA   P0 - FaultCode   %MW1008 *)
%MW1008 := Pompes[0].FaultCode;

(* TA   P0 - FaultLatch   %MW1000:X12 *)
%MW1000:X12 := Pompes[0].FaultLatch;

(* TM   P0 - Ready   %MW1000:X13 *)
%MW1000:X13 := Pompes[0].Ready;

(* TM   P0 - RunMs   %MW1009 *)
%MW1009 := DINT_TO_INT(Pompes[0].RunMs);

(* TM   P0 - Starts   %MW1010 *)
%MW1010 := UDINT_TO_INT(Pompes[0].Starts);

(* TM   P1 - Fbk   %MW1012:X5 *)
%MW1012:X5 := Pompes[1].Fbk;

(* TM   P1 - Thermal   %MW1012:X6 *)
%MW1012:X6 := Pompes[1].Thermal;

(* TM   P1 - Run   %MW1012:X7 *)
%MW1012:X7 := Pompes[1].Run;

(* TM   P1 - Running   %MW1012:X8 *)
%MW1012:X8 := Pompes[1].Running;

(* TM   P1 - HasFbk   %MW1012:X9 *)
%MW1012:X9 := Pompes[1].HasFbk;

(* TM   P1 - SuctionOk   %MW1012:X10 *)
%MW1012:X10 := Pompes[1].SuctionOk;

(* TM   P1 - Pressure   %MW1018 *)
%MW1018 := REAL_TO_INT(Pompes[1].Pressure);

(* TM   P1 - MaxPressure   %MW1019 *)
%MW1019 := REAL_TO_INT(Pompes[1].MaxPressure);

(* TA   P1 - Fault   %MW1012:X11 *)
%MW1012:X11 := Pompes[1].Fault;

(* TA   P1 - FaultCode   %MW1020 *)
%MW1020 := Pompes[1].FaultCode;

(* TA   P1 - FaultLatch   %MW1012:X12 *)
%MW1012:X12 := Pompes[1].FaultLatch;

(* TM   P1 - Ready   %MW1012:X13 *)
%MW1012:X13 := Pompes[1].Ready;

(* TM   P1 - RunMs   %MW1021 *)
%MW1021 := DINT_TO_INT(Pompes[1].RunMs);

(* TM   P1 - Starts   %MW1022 *)
%MW1022 := UDINT_TO_INT(Pompes[1].Starts);

(* TM   P2 - Fbk   %MW1024:X5 *)
%MW1024:X5 := Pompes[2].Fbk;

(* TM   P2 - Thermal   %MW1024:X6 *)
%MW1024:X6 := Pompes[2].Thermal;

(* TM   P2 - Run   %MW1024:X7 *)
%MW1024:X7 := Pompes[2].Run;

(* TM   P2 - Running   %MW1024:X8 *)
%MW1024:X8 := Pompes[2].Running;

(* TM   P2 - HasFbk   %MW1024:X9 *)
%MW1024:X9 := Pompes[2].HasFbk;

(* TM   P2 - SuctionOk   %MW1024:X10 *)
%MW1024:X10 := Pompes[2].SuctionOk;

(* TM   P2 - Pressure   %MW1030 *)
%MW1030 := REAL_TO_INT(Pompes[2].Pressure);

(* TM   P2 - MaxPressure   %MW1031 *)
%MW1031 := REAL_TO_INT(Pompes[2].MaxPressure);

(* TA   P2 - Fault   %MW1024:X11 *)
%MW1024:X11 := Pompes[2].Fault;

(* TA   P2 - FaultCode   %MW1032 *)
%MW1032 := Pompes[2].FaultCode;

(* TA   P2 - FaultLatch   %MW1024:X12 *)
%MW1024:X12 := Pompes[2].FaultLatch;

(* TM   P2 - Ready   %MW1024:X13 *)
%MW1024:X13 := Pompes[2].Ready;

(* TM   P2 - RunMs   %MW1033 *)
%MW1033 := DINT_TO_INT(Pompes[2].RunMs);

(* TM   P2 - Starts   %MW1034 *)
%MW1034 := UDINT_TO_INT(Pompes[2].Starts);
