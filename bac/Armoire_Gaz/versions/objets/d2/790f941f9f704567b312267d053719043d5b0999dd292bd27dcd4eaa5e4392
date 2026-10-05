(* Le mode nuit : la vue des commandes s'assombrit, le programmateur s'efface *)
IF Mode_Nuit THEN
    Vue_Commandes.Background := '#0B0F14';
    Vue_Commandes.Prog_Chauffage.Visible := FALSE;
    Vue_Commandes.Inter_Pompe.ColorOn := '#F2994A';
    Vue_Commandes.Titre_Commandes.Text := 'Pompe de transfert : mode nuit';
    Vue_Commandes.Texte_Valeurs.TextColor := '#F2C94C';
ELSE
    Vue_Commandes.Background := '#20242B';
    Vue_Commandes.Prog_Chauffage.Visible := TRUE;
    Vue_Commandes.Inter_Pompe.ColorOn := '#2ECC71';
    Vue_Commandes.Titre_Commandes.Text := 'Pompe de transfert : commandes';
    Vue_Commandes.Texte_Valeurs.TextColor := '#9CC3E6';
END_IF;
IHM_JOURNAL('Mode nuit {Mode_Nuit:activé|désactivé} par {SYS.UserName} à {SYS.Time}');