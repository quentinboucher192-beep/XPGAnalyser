Attribute VB_Name = "Automation"
Option Explicit

'==============================================================================
'  Automation - un seul module pour un seul classeur.
'
'  CE QUI A CHANGE PAR RAPPORT A LA VERSION EN DEUX FICHIERS. Les E/S et les
'  reports vivaient dans deux classeurs qui devaient s'ouvrir l'un l'autre pour
'  tenir un lien a jour : trois cas a gerer, un chemin a renseigner, et un etat
'  incoherent possible des que l'un des deux n'etait pas accessible. Tout ce
'  mecanisme a disparu. Un lien est une ligne dans une feuille du meme classeur.
'
'  TOUTES LES DECLARATIONS DE MODULE SONT ICI, avant la premiere procedure. VBA
'  l'exige, et le seul symptome quand on l'oublie est "Variable non definie" a
'  deux cents lignes de la cause.
'
'  LE PREFIXE SH_ N'EST PAS DECORATIF. Une constante nommee CAT et une variable
'  locale nommee cat sont le MEME identifiant pour VBA, qui ignore la casse : la
'  locale masque la constante, et on recoit une "Incompatibilite de type" sans
'  aucune indication de la cause. Personne n'appellera une variable SH_CAT.
'==============================================================================

Private Const SH_CONFIG As String = "Config"
Private Const SH_CARTES As String = "Cartes API"
Private Const SH_RACK   As String = "Vue Rack"
Private Const SH_ES     As String = "ES"
Private Const SH_CAT    As String = "Catalogue"
Private Const SH_EQUIP  As String = "Equipements"
Private Const SH_CABLE  As String = "Cablage"
Private Const SH_LIENS  As String = "Liens"
Private Const SH_ASSIST As String = "_ASSISTANT"
Private Const SH_ETAT   As String = "_ETAT"

Private Const HEAD_ROW  As Long = 7
Private Const FIRST_ROW As Long = 9
Private Const A_FIRST   As Long = 18     ' premiere ligne de parametre dans l'assistant

' A_FIRST VAUT 18. Les champs de l'equipement occupent les lignes 7 a 13, et
' RemplirParametres efface a partir de A_FIRST - 3 : a 14, il effacait Index et
' Instance a chaque changement de famille. Le nombre de champs a change une fois
' depuis, et il rechangera : quatre lignes de marge plutot que zero.


'======================================================== outils communs ======
' Les colonnes sont trouvees PAR LEUR TITRE, jamais par leur position. Quelqu'un
' inserera une colonne, et une macro qui compte les colonnes se met alors a lire
' l'unite dans la case de la resolution - sans rien dire.
Private Function ColOf(ws As Worksheet, ByVal titre As String) As Long
    Dim c As Long, last As Long
    last = ws.Cells(HEAD_ROW, ws.Columns.Count).End(xlToLeft).Column
    For c = 1 To last
        If StrComp(Trim$(CStr(ws.Cells(HEAD_ROW, c).Value)), titre, vbTextCompare) = 0 Then
            ColOf = c
            Exit Function
        End If
    Next c
End Function

Private Function LastRow(ws As Worksheet, ByVal col As Long) As Long
    LastRow = ws.Cells(ws.Rows.Count, col).End(xlUp).Row
    If LastRow < FIRST_ROW Then LastRow = FIRST_ROW - 1
End Function

Private Function Sh(ByVal nom As String) As Worksheet
    Set Sh = ThisWorkbook.Worksheets(nom)
End Function

Private Sub Etat(ByVal ligne As Long, ByVal valeur As Variant)
    Sh(SH_ETAT).Cells(ligne, 2).Value = valeur
End Sub

' Une ligne de journal. Il dit CE QUI A ETE FAIT ET QUAND ; l'indice, juste
' au-dessus dans la meme feuille, dit ce qu'on a voulu faire et pourquoi. Sur une
' affaire qui vit deux ans, c'est le journal qui repond a "depuis quand cette
' adresse est-elle la".
Private Sub Journal(ByVal quoi As String, ByVal detail As String)
    On Error Resume Next
    Dim ws As Worksheet
    Set ws = Sh("Indices")
    If ws Is Nothing Then Exit Sub

    Dim debut As Long, r As Long
    debut = ws.Range("JournalDebut").Row
    r = debut
    Do While Len(Trim$(CStr(ws.Cells(r, 1).Value))) > 0
        r = r + 1
    Loop

    ws.Cells(r, 1).Value = Format$(Now, "dd/mm/yyyy hh:nn")
    ws.Cells(r, 2).Value = quoi
    ws.Cells(r, 3).Value = detail
    ws.Cells(r, 4).Value = Application.UserName
    ws.Cells(r, 1).Font.Size = 8
    ws.Cells(r, 2).Font.Size = 8
    ws.Cells(r, 3).Font.Size = 8
    ws.Cells(r, 4).Font.Size = 8
    On Error GoTo 0
End Sub

' La couleur d'une famille. Une carte DI est bleue partout ou on la voit.
Private Function CouleurFamille(ByVal f As String) As Long
    Select Case UCase$(Trim$(f))
        Case "DI": CouleurFamille = RGB(221, 235, 247)
        Case "DO": CouleurFamille = RGB(226, 239, 218)
        Case "AI": CouleurFamille = RGB(255, 242, 204)
        Case "AO": CouleurFamille = RGB(233, 223, 245)
        Case Else: CouleurFamille = RGB(255, 255, 255)
    End Select
End Function

Private Function CouleurFamilleForte(ByVal f As String) As Long
    Select Case UCase$(Trim$(f))
        Case "DI": CouleurFamilleForte = RGB(46, 117, 182)
        Case "DO": CouleurFamilleForte = RGB(84, 130, 53)
        Case "AI": CouleurFamilleForte = RGB(191, 143, 0)
        Case "AO": CouleurFamilleForte = RGB(112, 48, 160)
        Case Else: CouleurFamilleForte = RGB(128, 128, 128)
    End Select
End Function


'============================================================ 1. INIT =========
'  Lit le dossier libs et remplit le Catalogue.
'
'  C'EST TOUT LE LIEN AVEC LE PROJET. Un .ddt et un .dfb sont des fichiers
'  TEXTE : des lignes "nom ; type ; portee ; defaut ; commentaire". Pas d'API,
'  pas de format binaire, rien a installer.
'==============================================================================
Public Sub INIT()
    Dim dossier As String
    dossier = Trim$(CStr(Sh(SH_CONFIG).Range("CheminLibs").Value))
    If Len(dossier) = 0 Or Dir(dossier, vbDirectory) = "" Then
        dossier = ChoisirDossier()
        If Len(dossier) = 0 Then Exit Sub
        Sh(SH_CONFIG).Range("CheminLibs").Value = dossier
    End If

    Dim ws As Worksheet
    Set ws = Sh(SH_CAT)

    ' Les choix deja faits, releves AVANT d'effacer. Sans ca, relancer INIT
    ' apres avoir regle trente parametres les effacerait tous les trente.
    Dim garde As Object
    Set garde = CreateObject("Scripting.Dictionary")
    Dim r As Long, last As Long
    last = LastRow(ws, 1)
    For r = FIRST_ROW To last
        Dim cle As String
        cle = CStr(ws.Cells(r, 1).Value) & "|" & CStr(ws.Cells(r, 4).Value)
        If Len(cle) > 1 Then
            garde(cle) = CStr(ws.Cells(r, 8).Value) & "|" & _
                         CStr(ws.Cells(r, 9).Value) & "|" & CStr(ws.Cells(r, 10).Value)
        End If
    Next r

    ' Un gestionnaire d'erreur qui REMET ScreenUpdating. Sans lui, une erreur en
    ' cours de lecture laisse l'affichage fige : la feuille parait vide alors
    ' qu'elle est remplie, et on cherche un bug qui n'existe pas.
    On Error GoTo Rate
    Application.ScreenUpdating = False
    If last >= FIRST_ROW Then
        ws.Range(ws.Cells(FIRST_ROW, 1), ws.Cells(last, 11)).ClearContents
    End If

    Dim sortie As Long, fichiers As Long
    sortie = FIRST_ROW
    LireDossier dossier, dossier, ws, sortie, fichiers, garde
    Application.ScreenUpdating = True
    On Error GoTo 0

    Etat 2, Now
    ws.Range("G3").Value = "Construit le " & Format$(Now, "dd/mm/yyyy hh:nn") & _
                           " depuis " & dossier & " : " & fichiers & " fichier(s), " & _
                           (sortie - FIRST_ROW) & " parametre(s)."

    If fichiers = 0 Then
        MsgBox "Aucun fichier .ddt ou .dfb dans :" & vbLf & dossier & vbLf & vbLf & _
               "Le dossier attendu contient des sous-dossiers IO, Equipment, " & _
               "Alarms, Control.", vbExclamation
        Exit Sub
    End If
    If sortie = FIRST_ROW Then
        MsgBox fichiers & " fichier(s) ouverts, et AUCUN parametre n'en est sorti." & _
               vbLf & vbLf & "Un .ddt contient des lignes de la forme" & vbLf & _
               "    nom ; type ; portee ; defaut ; commentaire", vbExclamation
        Exit Sub
    End If

    ws.Activate
    ws.Cells(FIRST_ROW, 1).Select
    MsgBox "Catalogue construit." & vbLf & vbLf & _
           fichiers & " fichier(s) lus" & vbLf & _
           (sortie - FIRST_ROW) & " parametre(s)" & vbLf & _
           garde.Count & " choix conserves", vbInformation
    Exit Sub
Rate:
    Application.ScreenUpdating = True
    MsgBox "INIT s'est arrete : " & Err.Description & vbLf & vbLf & _
           fichiers & " fichier(s) lus avant l'arret.", vbExclamation
End Sub

Private Function ChoisirDossier() As String
    Dim fd As Object
    Set fd = Application.FileDialog(4)
    fd.title = "Ou se trouve le dossier libs ?"
    If fd.Show = -1 Then ChoisirDossier = fd.SelectedItems(1)
End Function

Private Sub LireDossier(ByVal chemin As String, ByVal racine As String, _
                        ws As Worksheet, ByRef sortie As Long, ByRef fichiers As Long, _
                        garde As Object)
    Dim fso As Object, dossier As Object, f As Object, sous As Object
    Set fso = CreateObject("Scripting.FileSystemObject")
    If Not fso.FolderExists(chemin) Then Exit Sub
    Set dossier = fso.GetFolder(chemin)

    For Each f In dossier.Files
        Dim ext As String
        ext = LCase$(fso.GetExtensionName(f.name))
        If ext = "ddt" Or ext = "dfb" Then
            LireFichier f.Path, racine, ext, ws, sortie, garde
            fichiers = fichiers + 1
        End If
    Next f
    For Each sous In dossier.SubFolders
        LireDossier sous.Path, racine, ws, sortie, fichiers, garde
    Next sous
End Sub

Private Sub LireFichier(ByVal chemin As String, ByVal racine As String, _
                        ByVal genre As String, ws As Worksheet, ByRef sortie As Long, _
                        garde As Object)
    Dim num As Integer, tout As String
    Dim famille As String, relatif As String
    relatif = Mid$(chemin, Len(racine) + 2)

    ' LE FICHIER EN ENTIER, ET ON LE DECOUPE SOI-MEME.
    '
    ' Line Input cherche un retour chariot. Les fichiers de la bibliotheque sont
    ' ecrits sous Linux, donc en LF seul : Line Input rendait alors le FICHIER
    ' ENTIER comme une seule ligne. Elle commence par "#", partait dans la
    ' branche commentaire, et quarante-cinq fichiers donnaient zero parametre
    ' sans que rien ne le signale.
    num = FreeFile
    Open chemin For Input As #num
    If LOF(num) > 0 Then tout = Input$(LOF(num), #num)
    Close #num

    tout = Replace$(tout, vbCr, "")
    Dim lignes As Variant, idx As Long
    lignes = Split(tout, vbLf)

    For idx = LBound(lignes) To UBound(lignes)
        Dim ligne As String
        ligne = Trim$(CStr(lignes(idx)))

        If Left$(ligne, 5) = "name " Then
            famille = Trim$(Mid$(ligne, InStr(ligne, "=") + 1))
        ElseIf Left$(ligne, 1) = "#" Or Len(ligne) = 0 Then
            ' commentaire ou ligne vide
        ElseIf Left$(ligne, 3) = "<<<" Then
            Exit For          ' le corps ST commence : ce ne sont plus des declarations
        ElseIf InStr(ligne, ";") > 0 Then
            Dim p As Variant
            p = Split(ligne, ";")
            If UBound(p) >= 2 Then
                Dim nom As String, typ As String, portee As String
                Dim defaut As String, com As String
                nom = Trim$(p(0)): typ = Trim$(p(1)): portee = Trim$(p(2))
                defaut = "": com = ""
                If UBound(p) >= 3 Then defaut = Trim$(p(3))
                If UBound(p) >= 4 Then com = Trim$(p(4))

                If Len(nom) > 0 And Len(typ) > 0 And Len(famille) > 0 Then
                    ws.Cells(sortie, 1).Value = famille
                    ws.Cells(sortie, 2).Value = genre
                    ws.Cells(sortie, 3).Value = relatif
                    ws.Cells(sortie, 4).Value = nom
                    ws.Cells(sortie, 5).Value = typ
                    ws.Cells(sortie, 6).Value = portee
                    ws.Cells(sortie, 7).Value = defaut
                    ws.Cells(sortie, 11).Value = com

                    Dim cle2 As String
                    cle2 = famille & "|" & nom
                    If garde.Exists(cle2) Then
                        Dim anc As Variant
                        anc = Split(garde(cle2), "|")
                        ws.Cells(sortie, 8).Value = anc(0)
                        ws.Cells(sortie, 9).Value = anc(1)
                        If UBound(anc) >= 2 Then ws.Cells(sortie, 10).Value = anc(2)
                    Else
                        ws.Cells(sortie, 8).Value = Reportable(nom, typ, portee, com)
                        ws.Cells(sortie, 9).Value = TypeReport(nom)
                        ws.Cells(sortie, 10).Value = CablageDe(nom, typ)
                    End If
                    sortie = sortie + 1
                End If
            End If
        End If
    Next idx
End Sub

' Les trois propositions. Ce sont des PROPOSITIONS : elles vivent dans les
' colonnes vertes du catalogue, et INIT ne les ecrase plus jamais.
Private Function Reportable(ByVal nom As String, ByVal typ As String, _
                            ByVal portee As String, ByVal com As String) As String
    Dim n As String
    n = LCase$(nom)
    If Left$(n, 4) = "prev" Then Reportable = "N": Exit Function
    If InStr(n, "elapsed") > 0 Then Reportable = "N": Exit Function
    If n = "last" Or n = "size" Or n = "i" Or n = "j" Or n = "k" Or n = "n" Then
        Reportable = "N": Exit Function
    End If
    If InStr(LCase$(com), "runtime") > 0 Then Reportable = "N": Exit Function
    If InStr(LCase$(portee), "local") > 0 Then Reportable = "N": Exit Function
    If Left$(UCase$(typ), 5) = "ARRAY" Then Reportable = "N": Exit Function
    If Left$(UCase$(typ), 6) = "STRING" Then Reportable = "N": Exit Function
    Reportable = "O"
End Function

' TM information, TA alarme, TC commande.
'
' L'ORDRE DES TESTS COMPTE, et les tests sont PRECIS. AckFault contient "ack" ET
' "fault" : c'est un acquittement, donc une commande. Et un premier jet cherchait
' "low" n'importe ou dans le nom, ce qui faisait d'Overflow un reglage.
Private Function TypeReport(ByVal nom As String) As String
    Dim n As String
    n = LCase$(nom)
    If Left$(n, 3) = "man" Or InStr(n, "ack") > 0 Or InStr(n, "req") > 0 _
       Or InStr(n, "cmd") > 0 Or n = "enable" Or n = "locked" Or n = "reset" Then
        TypeReport = "TC": Exit Function
    End If
    If InStr(n, "fault") > 0 Or InStr(n, "alarm") > 0 Or n = "violation" _
       Or n = "anyviolation" Or n = "short" Or n = "dryrun" Or n = "overflow" _
       Or n = "held" Then
        TypeReport = "TA": Exit Function
    End If
    If Left$(n, 3) = "run" Or Left$(n, 5) = "total" Or Left$(n, 5) = "since" _
       Or n = "starts" Or n = "entries" Or n = "rotations" Or n = "transitions" _
       Or InStr(n, "count") > 0 Then
        TypeReport = "TM": Exit Function
    End If
    If Left$(n, 2) = "is" Then TypeReport = "TM": Exit Function
    If Right$(n, 2) = "ms" Or Right$(n, 3) = "min" Or Right$(n, 3) = "max" _
       Or InStr(n, "delay") > 0 Or InStr(n, "hyst") > 0 Or InStr(n, "band") > 0 _
       Or InStr(n, "filt") > 0 Or n = "capacity" Or n = "demand" _
       Or Right$(n, 4) = "high" Or Right$(n, 3) = "low" Or n = "priority" Then
        TypeReport = "TC": Exit Function
    End If
    TypeReport = "TM"
End Function

' Ce qui se branche sur une voie physique, et dans quel sens. Vide veut dire que
' ce parametre ne vient d'aucune voie : un defaut calcule par le bloc, une
' consigne qui vient de l'IHM.
Private Function CablageDe(ByVal nom As String, ByVal typ As String) As String
    Dim n As String
    n = LCase$(nom)
    If UCase$(typ) = "BOOL" Then
        Select Case n
            Case "fdcopen", "fdcclose", "fbk", "thermal", "emergstop", "pulse", _
                 "suctionok", "fdcextended", "fdcretracted", "driveready", "drivefault"
                CablageDe = "DI"
            Case "run", "runrev", "cmdextend", "cmdretract", "out"
                CablageDe = "DO"
        End Select
    ElseIf UCase$(typ) = "REAL" Or UCase$(typ) = "INT" Then
        Select Case n
            Case "level", "flow", "pressure", "speedfbk": CablageDe = "AI"
            Case "rawout": CablageDe = "AO"
        End Select
    End If
End Function


'=================================== 2. CARTES : creer les voies, voir le rack =
'  Declarer une carte cree ses voies. Le bouton N'EFFACE JAMAIS : une voie deja
'  reglee reste telle quelle, et reduire une carte de 16 a 8 voies laisse les
'  huit autres en place - la colonne Etat le signale, et c'est a vous de decider.
'==============================================================================
Public Sub CreerVoies()
    Dim ca As Worksheet, es As Worksheet
    Set ca = Sh(SH_CARTES)
    Set es = Sh(SH_ES)

    Dim cFam As Long, cCarte As Long, cVoie As Long, cIdx As Long
    cFam = ColOf(es, "Famille"): cCarte = ColOf(es, "Carte")
    cVoie = ColOf(es, "Voie"):   cIdx = ColOf(es, "Index")

    ' Ce qui existe deja, releve une fois : carte + voie.
    Dim deja As Object
    Set deja = CreateObject("Scripting.Dictionary")
    Dim r As Long, last As Long
    last = LastRow(es, cCarte)
    For r = FIRST_ROW To last
        Dim k As String
        k = LCase$(Trim$(CStr(es.Cells(r, cCarte).Value))) & "|" & _
            CStr(es.Cells(r, cVoie).Value)
        If Len(k) > 1 Then deja(k) = r
    Next r

    Application.ScreenUpdating = False
    Dim sortie As Long, ajoutees As Long, cartes As Long
    sortie = last + 1
    If sortie < FIRST_ROW Then sortie = FIRST_ROW

    Dim cr As Long, clast As Long
    clast = LastRow(ca, 1)
    For cr = FIRST_ROW To clast
        Dim nom As String, fam As String, voies As Long
        nom = Trim$(CStr(ca.Cells(cr, 1).Value))
        fam = UCase$(Trim$(CStr(ca.Cells(cr, 4).Value)))
        voies = CLng(Val(CStr(ca.Cells(cr, 6).Value)))
        If Len(nom) > 0 And voies > 0 And UCase$(Trim$(CStr(ca.Cells(cr, 7).Value))) <> "N" Then
            cartes = cartes + 1
            Dim v As Long
            For v = 0 To voies - 1
                If Not deja.Exists(LCase$(nom) & "|" & v) Then
                    es.Cells(sortie, cFam).Value = fam
                    es.Cells(sortie, cCarte).Value = nom
                    es.Cells(sortie, cVoie).Value = v
                    es.Cells(sortie, cIdx).Value = v
                    ' La couleur de la famille dans la colonne A : les quatre
                    ' parties se voient sans avoir a filtrer.
                    es.Cells(sortie, cFam).Interior.Color = CouleurFamilleForte(fam)
                    es.Cells(sortie, cFam).Font.Color = RGB(255, 255, 255)
                    es.Cells(sortie, cFam).Font.Bold = True
                    es.Cells(sortie, cFam).HorizontalAlignment = xlCenter
                    sortie = sortie + 1
                    ajoutees = ajoutees + 1
                End If
            Next v
        End If
    Next cr
    Application.ScreenUpdating = True

    Etat 3, Now
    RafraichirVueRack
    es.Activate
    Journal "Voies creees", ajoutees & " voie(s) pour " & cartes & " carte(s)"
    MsgBox ajoutees & " voie(s) creee(s) pour " & cartes & " carte(s) active(s)." & _
           vbLf & vbLf & "Rien n'a ete efface : une voie deja reglee reste telle " & _
           "quelle, et une carte reduite laisse ses anciennes voies en place.", _
           vbInformation
End Sub


Public Sub RafraichirVueRack()
    Dim ca As Worksheet, rk As Worksheet
    Set ca = Sh(SH_CARTES)
    Set rk = Sh(SH_RACK)

    Application.ScreenUpdating = False
    Dim rack As Long, slot As Long, base As Long, k As Long
    For rack = 0 To 7
        base = FIRST_ROW + rack * 7
        For slot = 0 To 15
            For k = 1 To 5
                rk.Cells(base + k, 2 + slot).Value = ""
                rk.Cells(base + k, 2 + slot).Interior.Pattern = xlNone
            Next k
        Next slot
    Next rack

    Dim cr As Long, clast As Long, posees As Long
    clast = LastRow(ca, 1)
    For cr = FIRST_ROW To clast
        Dim nom As String
        nom = Trim$(CStr(ca.Cells(cr, 1).Value))
        If Len(nom) > 0 Then
            Dim rq As Long, sq As Long, fam As String
            rq = CLng(Val(CStr(ca.Cells(cr, 2).Value)))
            sq = CLng(Val(CStr(ca.Cells(cr, 3).Value)))
            fam = UCase$(Trim$(CStr(ca.Cells(cr, 4).Value)))
            If rq >= 0 And rq <= 7 And sq >= 0 And sq <= 15 Then
                base = FIRST_ROW + rq * 7
                rk.Cells(base + 1, 2 + sq).Value = nom
                rk.Cells(base + 2, 2 + sq).Value = fam
                rk.Cells(base + 3, 2 + sq).Value = ca.Cells(cr, 5).Value
                rk.Cells(base + 4, 2 + sq).Value = ca.Cells(cr, 6).Value & " voies"
                rk.Cells(base + 5, 2 + sq).Value = ca.Cells(cr, 15).Value
                For k = 1 To 5
                    rk.Cells(base + k, 2 + sq).Interior.Color = CouleurFamille(fam)
                Next k
                rk.Cells(base + 1, 2 + sq).Font.Bold = True
                ' L'etat en rouge quand il manque des voies : c'est la seule
                ' chose qu'on vient chercher ici.
                If InStr(CStr(ca.Cells(cr, 15).Value), "manque") > 0 _
                   Or InStr(CStr(ca.Cells(cr, 15).Value), "TROP") > 0 _
                   Or InStr(CStr(ca.Cells(cr, 15).Value), "aucune") > 0 Then
                    rk.Cells(base + 5, 2 + sq).Font.Color = RGB(192, 0, 0)
                    rk.Cells(base + 5, 2 + sq).Font.Bold = True
                Else
                    rk.Cells(base + 5, 2 + sq).Font.Color = RGB(0, 128, 0)
                    rk.Cells(base + 5, 2 + sq).Font.Bold = False
                End If
                posees = posees + 1
            End If
        End If
    Next cr
    Application.ScreenUpdating = True
    rk.Activate
End Sub


'============================= 3. ASSISTANT DE VOIE (double-clic sur une ligne) =
'  Regler une voie, c'est remplir six champs pour une TOR et trente-cinq pour une
'  ANA. Les trente-cinq sont dans des blocs replies : les chercher en deroulant
'  cinquante colonnes est ce qui fait qu'on ne les regle pas.
'
'  L'assistant ne montre QUE ce qui concerne la famille de la ligne. Une voie TOR
'  n'a pas d'echelle, et lui proposer quand meme quatre seuils invite a en
'  remplir un.
'==============================================================================
Public Sub OuvrirAssistantES(ByVal targetRow As Long)
    Dim es As Worksheet, a As Worksheet
    Set es = Sh(SH_ES)
    Set a = Sh(SH_ASSIST)

    Dim fam As String
    fam = UCase$(Trim$(CStr(es.Cells(targetRow, ColOf(es, "Famille")).Value)))
    If Len(fam) = 0 Then
        MsgBox "Cette ligne n'a pas de famille : ce n'est pas une voie.", vbInformation
        Exit Sub
    End If

    Application.EnableEvents = False
    a.Visible = xlSheetVisible
    a.Cells.Clear
    ClearShapes a
    a.Columns("A").ColumnWidth = 3
    a.Columns("B").ColumnWidth = 22
    a.Columns("C").ColumnWidth = 26
    a.Columns("D").ColumnWidth = 3
    a.Columns("E").ColumnWidth = 62

    a.Range("B2").Value = "Reglage de la voie"
    a.Range("B2").Font.Size = 15
    a.Range("B2").Font.Bold = True
    a.Range("B3").Value = fam & "   ligne " & targetRow & "   " & _
                          es.Cells(targetRow, ColOf(es, "Adresse")).Value
    a.Range("B3").Font.Italic = True
    a.Range("B4").Value = "MODE"          ' ce que valide le bouton
    a.Range("C4").Value = "ES"
    a.Range("B5").Value = "LIGNE"
    a.Range("C5").Value = targetRow
    a.Rows(4).Hidden = True
    a.Rows(5).Hidden = True

    ' Les champs communs, puis ceux de la famille. L'ordre suit celui de
    ' l'onglet : on retrouve la meme chose au meme endroit.
    Dim champs As Variant
    If fam = "DI" Or fam = "DO" Then
        champs = Array("Designation", "Repere", "Index", "Commentaire", "", _
                       "Inv", "DebounceMs", "AlarmEn", "AlarmState", "AlarmDelayMs", "Sev")
    Else
        champs = Array("Designation", "Repere", "Index", "Commentaire", "", _
                       "Unite", "RawMin", "RawMax", "EngMin", "EngMax", "Clamp", _
                       "DeadBand", "FiltN", "RateMax", "", _
                       "S0_En", "S0_Dir", "S0_Val", "S0_Hyst", "S0_DelayMs", "S0_Sev", "", _
                       "S1_En", "S1_Dir", "S1_Val", "S1_Hyst", "S1_DelayMs", "S1_Sev", "", _
                       "S2_En", "S2_Dir", "S2_Val", "S2_Hyst", "S2_DelayMs", "S2_Sev", "", _
                       "S3_En", "S3_Dir", "S3_Val", "S3_Hyst", "S3_DelayMs", "S3_Sev")
    End If

    Dim r As Long, i As Long
    r = 7
    For i = LBound(champs) To UBound(champs)
        Dim titre As String
        titre = CStr(champs(i))
        If Len(titre) = 0 Then
            r = r + 1                       ' une ligne vide separe les blocs
        Else
            Dim col As Long
            col = ColOf(es, titre)
            If col > 0 Then
                a.Cells(r, 2).Value = titre
                a.Cells(r, 2).Font.Bold = True
                a.Cells(r, 3).Value = es.Cells(targetRow, col).Value
                a.Cells(r, 3).Interior.Color = RGB(255, 253, 231)
                a.Cells(r, 3).Borders.LineStyle = xlContinuous
                ' L'aide de la colonne, reprise du sous-titre de l'onglet : elle
                ' est ecrite une fois, la ou la colonne est definie.
                a.Cells(r, 5).Value = CStr(es.Cells(8, col).Value)
                a.Cells(r, 5).Font.Size = 8
                a.Cells(r, 5).Font.Italic = True
                a.Cells(r, 5).Font.Color = RGB(128, 128, 128)
                If InStr(CStr(es.Cells(8, col).Value), "O/N") > 0 Then
                    With a.Cells(r, 3).Validation
                        .Delete
                        .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
                             Formula1:="O,N"
                    End With
                ElseIf InStr(CStr(es.Cells(8, col).Value), "H/B") > 0 Then
                    With a.Cells(r, 3).Validation
                        .Delete
                        .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
                             Formula1:="H,B"
                    End With
                End If
                a.Cells(r, 1).Value = titre     ' la cle, pour le retour
                a.Columns("A").Hidden = True
                r = r + 1
            End If
        End If
    Next i

    PoserBouton a, r + 1, 2, "Appliquer", "Automation.ValiderAssistant", 110
    PoserBouton a, r + 1, 3, "Annuler", "Automation.FermerAssistant", 90
    DessinerVoie a, es, targetRow, fam, r + 4

    Application.EnableEvents = True
    a.Activate
    a.Range("C7").Select
End Sub


Private Sub AppliquerVoie()
    Dim a As Worksheet, es As Worksheet
    Set a = Sh(SH_ASSIST)
    Set es = Sh(SH_ES)

    Dim ligne As Long
    ligne = CLng(Val(CStr(a.Range("C5").Value)))
    If ligne < FIRST_ROW Then Exit Sub

    Application.EnableEvents = False
    Dim r As Long, poses As Long
    For r = 7 To 80
        Dim titre As String
        titre = Trim$(CStr(a.Cells(r, 1).Value))
        If Len(titre) > 0 Then
            Dim col As Long
            col = ColOf(es, titre)
            If col > 0 Then
                es.Cells(ligne, col).Value = a.Cells(r, 3).Value
                poses = poses + 1
            End If
        End If
    Next r
    Application.EnableEvents = True

    FermerAssistant
    es.Activate
    es.Cells(ligne, 4).Select
    Application.StatusBar = poses & " champ(s) ecrits sur la ligne " & ligne
End Sub


' Le dessin de la voie, dans l'assistant.
'
' VOIR OU UN SEUIL COUPE LA RAMPE dit en un instant ce que quatre nombres ne
' disent pas. Et une temporisation dessinee se compare a l'oeil : "2000 ms" et
' "20 ms" se ressemblent dans un tableau, pas dans un graphique.
'
' LES POINTS SONT A DROITE, PAS MASQUES. Excel ne trace pas ce qui est masque -
' PlotVisibleOnly vaut True par defaut - et un premier jet rangeait ses points
' dans des colonnes qu'il cachait ensuite pour faire propre : le graphique etait
' vide, sans le moindre message.
Private Sub DessinerVoie(a As Worksheet, es As Worksheet, ByVal ligne As Long, _
                         ByVal fam As String, ByVal haut As Long)
    ' PAS DE On Error Resume Next ICI. Il y en avait un, qui enveloppait toute la
    ' procedure : des qu'une ligne echouait, aucun graphique n'apparaissait et
    ' RIEN NE LE DISAIT. Un dessin absent sans explication est pire qu'un message
    ' d'erreur - on cherche dans le classeur ce qui est dans le code.
    On Error GoTo Rate
    Dim ch As ChartObject

    If fam = "AI" Or fam = "AO" Then
        Dim rawMin As Double, rawMax As Double, engMin As Double, engMax As Double
        rawMin = Val(CStr(es.Cells(ligne, ColOf(es, "RawMin")).Value))
        rawMax = Val(CStr(es.Cells(ligne, ColOf(es, "RawMax")).Value))
        engMin = Val(CStr(es.Cells(ligne, ColOf(es, "EngMin")).Value))
        engMax = Val(CStr(es.Cells(ligne, ColOf(es, "EngMax")).Value))

        If rawMax = rawMin Then
            a.Cells(haut, 2).Value = "RawMin = RawMax : echelle inutilisable, rien a " & _
                "tracer. Le bloc marquera Fault et sautera la voie."
            a.Cells(haut, 2).Font.Color = RGB(192, 0, 0)
            a.Cells(haut, 2).Font.Bold = True
            Exit Sub
        End If

        a.Cells(1, 20).Value = "Points carte"
        a.Cells(1, 21).Value = "Unites physiques"
        Dim i As Long
        For i = 0 To 20
            a.Cells(2 + i, 20).Value = rawMin + (rawMax - rawMin) * i / 20
            a.Cells(2 + i, 21).Value = engMin + (engMax - engMin) * i / 20
        Next i

        Set ch = a.ChartObjects.Add(a.Cells(haut, 2).Left, a.Cells(haut, 2).top, 430, 260)
        With ch.Chart
            .ChartType = xlXYScatterLinesNoMarkers
            .SetSourceData Source:=a.Range(a.Cells(1, 20), a.Cells(22, 21))
            .PlotVisibleOnly = False
            .HasTitle = True
            .ChartTitle.Text = "Mise a l'echelle  -  " & _
                               CStr(es.Cells(ligne, ColOf(es, "Unite")).Value)
            .HasLegend = True
            .Legend.Position = xlLegendPositionBottom
            .SeriesCollection(1).name = "Physique"
        End With

        ' Chaque seuil ACTIF devient une droite horizontale. Voir ou elle coupe
        ' la rampe est exactement ce qu'on vient chercher.
        Dim k As Long, col As Long
        col = 22
        For k = 0 To 3
            If UCase$(Trim$(CStr(es.Cells(ligne, ColOf(es, "S" & k & "_En")).Value))) = "O" Then
                a.Cells(1, col).Value = "Seuil " & k
                Dim v As Double
                v = Val(CStr(es.Cells(ligne, ColOf(es, "S" & k & "_Val")).Value))
                For i = 0 To 20
                    a.Cells(2 + i, col).Value = v
                Next i
                With ch.Chart.SeriesCollection.NewSeries
                    .name = a.Cells(1, col)
                    .XValues = a.Range(a.Cells(2, 20), a.Cells(22, 20))
                    .Values = a.Range(a.Cells(2, col), a.Cells(22, col))
                    .Format.Line.DashStyle = msoLineDash
                End With
                col = col + 1
            End If
        Next k
        a.Range(a.Columns(20), a.Columns(26)).ColumnWidth = 7

    Else
        ' TOR : les deux temporisations, cote a cote. Vingt millisecondes et deux
        ' secondes se ressemblent dans un tableau ; pas sur deux barres.
        Dim deb As Double, alarme As Double
        deb = Val(CStr(es.Cells(ligne, ColOf(es, "DebounceMs")).Value))
        alarme = Val(CStr(es.Cells(ligne, ColOf(es, "AlarmDelayMs")).Value))
        If deb = 0 And alarme = 0 Then
            a.Cells(haut, 2).Value = "Aucune temporisation reglee : la voie suit " & _
                "l'entree telle quelle, et l'alarme sort des qu'elle apparait."
            a.Cells(haut, 2).Font.Italic = True
            a.Cells(haut, 2).Font.Color = RGB(128, 128, 128)
            Exit Sub
        End If

        a.Cells(1, 20).Value = "ms"
        a.Cells(2, 20).Value = "Anti-rebond"
        a.Cells(2, 21).Value = deb
        a.Cells(3, 20).Value = "Retard alarme"
        a.Cells(3, 21).Value = alarme

        Set ch = a.ChartObjects.Add(a.Cells(haut, 2).Left, a.Cells(haut, 2).top, 380, 170)
        With ch.Chart
            .ChartType = xlBarClustered
            .SetSourceData Source:=a.Range(a.Cells(2, 20), a.Cells(3, 21))
            .PlotVisibleOnly = False
            .HasTitle = True
            .ChartTitle.Text = "Temporisations de la voie, en ms"
            .HasLegend = False
        End With
        a.Range(a.Columns(20), a.Columns(21)).ColumnWidth = 14
    End If
    On Error GoTo 0
    Exit Sub

Rate:
    On Error GoTo 0
    a.Cells(haut, 2).Value = "Le dessin n'a pas pu etre trace : " & Err.Description
    a.Cells(haut, 2).Font.Color = RGB(192, 0, 0)
    a.Cells(haut + 1, 2).Value = "Les valeurs de la voie restent modifiables ci-dessus."
    a.Cells(haut + 1, 2).Font.Italic = True
    a.Cells(haut + 1, 2).Font.Size = 8
End Sub


Private Sub ClearShapes(ws As Worksheet)
    Dim i As Long
    For i = ws.Shapes.Count To 1 Step -1
        ws.Shapes(i).Delete
    Next i
End Sub

Private Sub PoserBouton(ws As Worksheet, ByVal r As Long, ByVal c As Long, _
                        ByVal texte As String, ByVal macro As String, ByVal larg As Single)
    Dim b As Object
    Set b = ws.Buttons.Add(ws.Cells(r, c).Left, ws.Cells(r, c).top, larg, 26)
    b.Caption = texte
    b.OnAction = macro
End Sub

Public Sub FermerAssistant()
    Sh(SH_ASSIST).Visible = xlSheetHidden
End Sub

' Un seul bouton Appliquer pour les deux assistants : il regarde le mode.
Public Sub ValiderAssistant()
    If UCase$(Trim$(CStr(Sh(SH_ASSIST).Range("C4").Value))) = "ES" Then
        AppliquerVoie
    Else
        AppliquerEquipement
    End If
End Sub


'================================================= 4. ASSISTANT D'EQUIPEMENT ===
'  Choisir une famille, puis decider POUR CHAQUE PARAMETRE s'il se branche sur
'  une voie et s'il remonte a l'IHM. RIEN N'EST OBLIGATOIRE : un parametre peut
'  avoir une voie sans report, un report sans voie, ou ni l'un ni l'autre.
'==============================================================================
Public Sub AjouterEquipement()
    OuvrirAssistantEquipement 0
End Sub

Public Sub OuvrirAssistantEquipement(ByVal targetRow As Long)
    Dim a As Worksheet, catWs As Worksheet, eq As Worksheet
    Set a = Sh(SH_ASSIST)
    Set catWs = Sh(SH_CAT)
    Set eq = Sh(SH_EQUIP)

    If Len(Trim$(CStr(catWs.Cells(FIRST_ROW, 1).Value))) = 0 Then
        MsgBox "Le catalogue est vide." & vbLf & vbLf & _
               "Lancez d'abord INIT : sans lui l'assistant n'a aucune famille " & _
               "a proposer.", vbExclamation
        Exit Sub
    End If

    Application.EnableEvents = False
    a.Visible = xlSheetVisible
    a.Cells.Clear
    ClearShapes a
    a.Cells.EntireColumn.Hidden = False

    a.Columns("A").ColumnWidth = 3
    a.Columns("B").ColumnWidth = 24
    a.Columns("C").ColumnWidth = 24
    a.Columns("D").ColumnWidth = 8
    a.Columns("E").ColumnWidth = 8
    a.Columns("F").ColumnWidth = 34
    a.Columns("G").ColumnWidth = 11
    a.Columns("H").ColumnWidth = 40
    a.Columns("I").ColumnWidth = 7
    a.Columns("J").ColumnWidth = 9
    a.Columns("K").ColumnWidth = 6
    a.Columns("L").ColumnWidth = 32
    a.Columns("M").ColumnWidth = 30
    a.Columns("N").ColumnWidth = 40

    a.Range("B2").Value = IIf(targetRow = 0, "Nouvel equipement", "Modifier l'equipement")
    a.Range("B2").Font.Size = 15
    a.Range("B2").Font.Bold = True
    a.Range("B4").Value = "MODE"
    a.Range("C4").Value = "EQ"
    a.Range("B5").Value = "LIGNE"
    a.Range("C5").Value = targetRow
    a.Rows(4).Hidden = True
    a.Rows(5).Hidden = True

    Dim etiquettes As Variant, i As Long
    ' UNE DDT EST UN ELEMENT DE TABLEAU, OU UNE VARIABLE SEULE. Pompes[0] et
    ' Pompes sont deux choses differentes, et les proposer ensemble sans le dire
    ' laissait ecrire un index sur une variable qui n'en a pas.
    etiquettes = Array("Nom", "Famille", "Repere", "Forme", "Variable", "Index", "Instance")
    For i = 0 To 6
        a.Cells(7 + i, 2).Value = etiquettes(i)
        a.Cells(7 + i, 2).Font.Bold = True
        a.Cells(7 + i, 3).Interior.Color = RGB(255, 253, 231)
        a.Cells(7 + i, 3).Borders.LineStyle = xlContinuous
    Next i
    a.Range("E8").Value = "changez la famille : la liste se refait"
    a.Range("E8").Font.Italic = True
    a.Range("E8").Font.Size = 8

    With a.Range("C8").Validation
        .Delete
        .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, Formula1:="=CatFamilles"
    End With
    With a.Range("C10").Validation
        .Delete
        .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
             Formula1:="Tableau,Variable seule"
    End With
    a.Range("C10").Value = "Tableau"
    a.Range("E10").Value = "Variable seule : pas d'index, l'acces est le nom tout court"
    a.Range("E10").Font.Italic = True
    a.Range("E10").Font.Size = 8
    a.Range("E12").Value = "l'index est verifie : deux equipements ne peuvent pas " & _
                           "occuper le meme"
    a.Range("E12").Font.Italic = True
    a.Range("E12").Font.Size = 8

    If targetRow > 0 Then
        a.Range("C7").Value = eq.Cells(targetRow, 1).Value
        a.Range("C8").Value = eq.Cells(targetRow, 2).Value
        a.Range("C9").Value = eq.Cells(targetRow, 3).Value
        a.Range("C10").Value = eq.Cells(targetRow, 5).Value
        a.Range("C11").Value = eq.Cells(targetRow, 6).Value
        a.Range("C12").Value = eq.Cells(targetRow, 7).Value
        a.Range("C13").Value = eq.Cells(targetRow, 8).Value
    End If

    ChargerVoies a
    Application.EnableEvents = True
    RafraichirChampsEquipement
    RemplirParametres
    a.Activate
    a.Range("C7").Select
End Sub


' Les voies disponibles, rangees dans des colonnes de travail et pointees par la
' validation. Excel refuse une liste litterale de plus de 255 caracteres, et
' quarante designations en font mille.
Private Sub ChargerVoies(a As Worksheet)
    Dim es As Worksheet
    Set es = Sh(SH_ES)
    a.Range(a.Columns(27), a.Columns(34)).Clear

    Dim cFam As Long, cDes As Long, cAcces As Long, cAdr As Long, cCarte As Long
    cFam = ColOf(es, "Famille"):  cDes = ColOf(es, "Designation")
    cAcces = ColOf(es, "Acces ST"): cAdr = ColOf(es, "Adresse")
    cCarte = ColOf(es, "Carte")

    Dim compteur(3) As Long, k As Long
    For k = 0 To 3
        compteur(k) = 2
        a.Cells(1, 27 + k).Value = Array("DI", "DO", "AI", "AO")(k)
    Next k

    Dim r As Long, last As Long
    last = LastRow(es, cCarte)
    For r = FIRST_ROW To last
        Dim fam As String
        fam = UCase$(Trim$(CStr(es.Cells(r, cFam).Value)))
        Dim col As Long
        col = -1
        Select Case fam
            Case "DI": col = 0
            Case "DO": col = 1
            Case "AI": col = 2
            Case "AO": col = 3
        End Select
        If col >= 0 Then
            Dim des As String
            des = Trim$(CStr(es.Cells(r, cDes).Value))
            If Len(des) = 0 Then des = "(sans designation)"
            ' Le LIBELLE d'abord : choisir "CarteDI_R0S4[1]" sans savoir que
            ' c'est le retour de marche demande d'aller verifier a chaque ligne.
            a.Cells(compteur(col), 27 + col).Value = _
                des & "   [" & es.Cells(r, cAcces).Value & "  " & _
                es.Cells(r, cAdr).Value & "]"
            a.Cells(compteur(col), 31 + col).Value = r     ' la ligne de l'onglet ES
            compteur(col) = compteur(col) + 1
        End If
    Next r
    a.Range(a.Columns(27), a.Columns(34)).EntireColumn.Hidden = True
End Sub

Private Sub PoserListeVoies(a As Worksheet, ByVal ligne As Long, ByVal sens As String)
    Dim col As Long
    Select Case sens
        Case "DI": col = 27
        Case "DO": col = 28
        Case "AI": col = 29
        Case "AO": col = 30
        Case Else: Exit Sub
    End Select
    Dim derniere As Long
    derniere = a.Cells(a.Rows.Count, col).End(xlUp).Row
    If derniere < 2 Then Exit Sub
    With a.Cells(ligne, 6).Validation
        .Delete
        .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
             Formula1:="=" & a.name & "!" & _
                       a.Range(a.Cells(2, col), a.Cells(derniere, col)).Address
    End With
End Sub

Private Function LigneESDe(a As Worksheet, ByVal sens As String, _
                           ByVal libelle As String) As Long
    Dim col As Long
    Select Case sens
        Case "DI": col = 27
        Case "DO": col = 28
        Case "AI": col = 29
        Case "AO": col = 30
        Case Else: Exit Function
    End Select
    Dim r As Long, derniere As Long
    derniere = a.Cells(a.Rows.Count, col).End(xlUp).Row
    For r = 2 To derniere
        If StrComp(CStr(a.Cells(r, col).Value), libelle, vbTextCompare) = 0 Then
            LigneESDe = CLng(Val(CStr(a.Cells(r, col + 4).Value)))
            Exit Function
        End If
    Next r
End Function


' Les champs qui ont un sens, et EUX SEULS.
'
' Un DFB s'instancie : il a un nom d'instance et rien d'autre. Une DDT est un
' element de tableau - variable et index - ou une variable seule - variable
' seulement. Montrer les six champs dans tous les cas invitait a remplir un index
' sur un bloc qui n'en a pas, et une instance sur une structure qui n'en a pas
' non plus.
Public Sub RafraichirChampsEquipement()
    Dim a As Worksheet, catWs As Worksheet
    Set a = Sh(SH_ASSIST)
    Set catWs = Sh(SH_CAT)
    If UCase$(Trim$(CStr(a.Range("C4").Value))) <> "EQ" Then Exit Sub

    Dim famille As String, genre As String
    famille = Trim$(CStr(a.Range("C8").Value))
    genre = GenreDe(famille)

    ' Le genre, a droite de la famille : c'est lui qui explique pourquoi les
    ' champs du dessous changent.
    a.Range("D8").Value = UCase$(genre)
    a.Range("D8").Font.Bold = True
    a.Range("D8").HorizontalAlignment = xlCenter
    If genre = "dfb" Then
        a.Range("D8").Font.Color = RGB(112, 48, 160)
    ElseIf genre = "ddt" Then
        a.Range("D8").Font.Color = RGB(46, 117, 182)
    Else
        a.Range("D8").Value = ""
    End If

    Dim forme As String
    forme = UCase$(Trim$(CStr(a.Range("C10").Value)))

    Application.EnableEvents = False
    Dim r As Long
    For r = 10 To 13
        a.Rows(r).Hidden = False
    Next r

    If genre = "dfb" Then
        ' Un bloc : une instance, point. Ni forme ni index.
        a.Rows(10).Hidden = True
        a.Rows(11).Hidden = True
        a.Rows(12).Hidden = True
        a.Range("C10").Value = "Variable seule"
        a.Range("C11").Value = a.Range("C13").Value
    ElseIf genre = "ddt" Then
        ' Une structure : jamais d'instance de bloc.
        a.Rows(13).Hidden = True
        a.Range("C13").Value = ""
        If forme = "VARIABLE SEULE" Then
            a.Rows(12).Hidden = True
            a.Range("C12").Value = ""
        End If
    End If
    Application.EnableEvents = True
End Sub

Private Function GenreDe(ByVal famille As String) As String
    If Len(famille) = 0 Then Exit Function
    Dim catWs As Worksheet
    Set catWs = Sh(SH_CAT)
    Dim r As Long, last As Long
    last = LastRow(catWs, 1)
    For r = FIRST_ROW To last
        If StrComp(Trim$(CStr(catWs.Cells(r, 1).Value)), famille, vbTextCompare) = 0 Then
            GenreDe = LCase$(Trim$(CStr(catWs.Cells(r, 2).Value)))
            Exit Function
        End If
    Next r
End Function


Public Sub RemplirParametres()
    Dim a As Worksheet, catWs As Worksheet
    Set a = Sh(SH_ASSIST)
    Set catWs = Sh(SH_CAT)
    If UCase$(Trim$(CStr(a.Range("C4").Value))) <> "EQ" Then Exit Sub

    Application.EnableEvents = False
    Application.ScreenUpdating = False

    ' Ce qui est deja saisi, garde : changer de famille par erreur puis revenir
    ' ne doit pas effacer vingt minutes de travail.
    Dim deja As Object
    Set deja = CreateObject("Scripting.Dictionary")
    Dim r As Long
    r = A_FIRST
    Do While Len(CStr(a.Cells(r, 2).Value)) > 0
        deja(CStr(a.Cells(r, 2).Value)) = CStr(a.Cells(r, 6).Value) & "|" & _
            CStr(a.Cells(r, 7).Value) & "|" & CStr(a.Cells(r, 9).Value) & "|" & _
            CStr(a.Cells(r, 10).Value) & "|" & CStr(a.Cells(r, 11).Value) & "|" & _
            CStr(a.Cells(r, 12).Value)
        r = r + 1
    Loop

    a.Range(a.Cells(A_FIRST - 3, 1), a.Cells(400, 14)).Clear

    Dim famille As String
    famille = Trim$(CStr(a.Range("C8").Value))
    If Len(famille) = 0 Then
        Application.EnableEvents = True
        Application.ScreenUpdating = True
        Exit Sub
    End If

    Dim titres As Variant, c As Long
    titres = Array("", "Parametre", "Type", "Report", "Cablage", "Voie E/S", _
                   "Attribut", "Ce que fait l'attribut", "Zone", "Numero", "Bit", _
                   "Designation IHM", "Etat de l'adresse", "Commentaire")
    For c = 1 To 14
        With a.Cells(A_FIRST - 1, c)
            .Value = titres(c - 1)
            .Font.Bold = True
            .Font.Color = RGB(255, 255, 255)
            .Interior.Color = RGB(68, 84, 106)
            .HorizontalAlignment = xlCenter
        End With
    Next c

    Dim sortie As Long, last As Long, n As Long
    sortie = A_FIRST
    last = LastRow(catWs, 1)

    For r = FIRST_ROW To last
        If StrComp(Trim$(CStr(catWs.Cells(r, 1).Value)), famille, vbTextCompare) = 0 Then
            If UCase$(Trim$(CStr(catWs.Cells(r, 8).Value))) = "O" Then
                Dim nom As String, sens As String
                nom = CStr(catWs.Cells(r, 4).Value)
                sens = UCase$(Trim$(CStr(catWs.Cells(r, 10).Value)))

                a.Cells(sortie, 2).Value = nom
                a.Cells(sortie, 3).Value = catWs.Cells(r, 5).Value
                a.Cells(sortie, 4).Value = catWs.Cells(r, 9).Value
                a.Cells(sortie, 4).HorizontalAlignment = xlCenter
                a.Cells(sortie, 4).Font.Bold = True
                Select Case UCase$(CStr(catWs.Cells(r, 9).Value))
                    Case "TA": a.Cells(sortie, 4).Font.Color = RGB(192, 0, 0)
                    Case "TC": a.Cells(sortie, 4).Font.Color = RGB(0, 112, 192)
                    Case Else: a.Cells(sortie, 4).Font.Color = RGB(89, 89, 89)
                End Select
                a.Cells(sortie, 5).Value = sens
                a.Cells(sortie, 5).HorizontalAlignment = xlCenter
                a.Cells(sortie, 14).Value = catWs.Cells(r, 11).Value
                a.Cells(sortie, 14).Font.Size = 8
                a.Cells(sortie, 14).Font.Italic = True

                For c = 9 To 12
                    a.Cells(sortie, c).Interior.Color = RGB(255, 253, 231)
                    a.Cells(sortie, c).Borders.LineStyle = xlContinuous
                Next c
                With a.Cells(sortie, 9).Validation
                    .Delete
                    .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
                         Formula1:="MW,MX"
                End With

                If Len(sens) > 0 Then
                    For c = 6 To 7
                        a.Cells(sortie, c).Interior.Color = RGB(234, 244, 234)
                        a.Cells(sortie, c).Borders.LineStyle = xlContinuous
                    Next c
                    PoserListeVoies a, sortie, sens
                    With a.Cells(sortie, 7).Validation
                        .Delete
                        If sens = "DI" Or sens = "DO" Then
                            .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
                                 Formula1:="Val,Raw,Rise,Fall,Alarm,Fault,OnMs,OffMs"
                        Else
                            .Add Type:=xlValidateList, AlertStyle:=xlValidAlertStop, _
                                 Formula1:="Val,Eng,Raw,Rate,MinHold,MaxHold,Fault"
                        End If
                    End With
                    ' Val par defaut : la valeur finale, inversee et anti-rebondie
                    ' pour une TOR, mise a l'echelle et filtree pour une ANA.
                    a.Cells(sortie, 7).Value = "Val"
                    ' LA DESCRIPTION DE L'ATTRIBUT, prise dans le catalogue -
                    ' c'est-a-dire dans le commentaire du .ddt. Aucune recopie.
                    a.Cells(sortie, 8).Formula = _
                        "=IF(G" & sortie & "="""","""",IFERROR(INDEX(Catalogue!$K:$K," & _
                        "MATCH(1,INDEX((Catalogue!$A:$A=""" & _
                        IIf(sens = "DI" Or sens = "DO", "ST_IO_Dig", "ST_IO_Ana") & _
                        """)*(Catalogue!$D:$D=G" & sortie & "),0),0)),""""))"
                    a.Cells(sortie, 8).Font.Size = 8
                    a.Cells(sortie, 8).Font.Italic = True
                    a.Cells(sortie, 8).Font.Color = RGB(89, 89, 89)
                End If

                If deja.Exists(nom) Then
                    Dim p As Variant
                    p = Split(deja(nom), "|")
                    a.Cells(sortie, 6).Value = p(0)
                    If Len(p(1)) > 0 Then a.Cells(sortie, 7).Value = p(1)
                    a.Cells(sortie, 9).Value = p(2)
                    a.Cells(sortie, 10).Value = p(3)
                    a.Cells(sortie, 11).Value = p(4)
                    a.Cells(sortie, 12).Value = p(5)
                Else
                    a.Cells(sortie, 12).Formula = _
                        "=IF($C$7="""","""",$C$7&"" - ""&B" & sortie & ")"
                End If
                sortie = sortie + 1
                n = n + 1
            End If
        End If
    Next r

    a.Cells(A_FIRST - 3, 2).Value = n & " parametre(s) pour " & famille & _
        " - RIEN N'EST OBLIGATOIRE : laissez vide ce qui ne se cable pas et ce " & _
        "qui ne remonte pas."
    a.Cells(A_FIRST - 3, 2).Font.Italic = True
    a.Cells(A_FIRST - 3, 2).Font.Size = 9
    a.Cells(A_FIRST - 3, 2).Font.Color = RGB(89, 89, 89)

    RafraichirChampsEquipement
    ClearShapes a
    PoserBouton a, sortie + 1, 2, "Valider", "Automation.ValiderAssistant", 110
    PoserBouton a, sortie + 1, 4, "Annuler", "Automation.FermerAssistant", 90
    PoserBouton a, sortie + 1, 6, "Attribuer les adresses libres", _
                "Automation.AttribuerAdresses", 200

    VerifierAdresses
    Application.EnableEvents = True
    Application.ScreenUpdating = True
End Sub


' Attribue les adresses libres a tout ce qui n'en a pas encore.
'
' LA TACHE LA PLUS FASTIDIEUSE, ET CELLE OU L'ON SE TROMPE LE PLUS. Quarante
' parametres, une plage de mille mots, et il faut a chaque fois retrouver ce qui
' est libre. Ici c'est calcule.
'
' LES BITS D'UN MEME EQUIPEMENT VONT DANS LE MEME MOT. Ce n'est pas une
' coquetterie : l'IHM lit un mot d'un coup, et une pompe dont les huit etats
' sont eparpilles sur huit mots coute huit lectures la ou une suffirait.
Public Sub AttribuerAdresses()
    Dim a As Worksheet, li As Worksheet
    Set a = Sh(SH_ASSIST)
    Set li = Sh(SH_LIENS)
    If UCase$(Trim$(CStr(a.Range("C4").Value))) <> "EQ" Then Exit Sub

    Dim moi As String
    moi = Trim$(CStr(a.Range("C7").Value))

    Dim mwDebut As Long, mwFin As Long
    mwDebut = CLng(Val(CStr(Sh(SH_CONFIG).Range("MW_Debut").Value)))
    mwFin = CLng(Val(CStr(Sh(SH_CONFIG).Range("MW_Fin").Value)))
    If mwFin <= mwDebut Then
        MsgBox "La plage %MW de l'onglet Config est vide ou a l'envers.", vbExclamation
        Exit Sub
    End If

    ' D'OU PARTIR. La plage de Config dit ou l'on a le droit d'aller ; elle ne
    ' dit pas ou l'on veut commencer. Ranger les pompes a partir de 1100 et les
    ' vannes a partir de 1200 est une decision d'organisation, pas un hasard, et
    ' repartir systematiquement du premier trou libre l'empeche.
    Dim depart As String
    depart = InputBox( _
        "A partir de quelle adresse %MW attribuer ?" & vbLf & vbLf & _
        "Plage autorisee : " & mwDebut & " a " & mwFin & vbLf & _
        "Laissez tel quel pour continuer au premier trou libre." & vbLf & vbLf & _
        "Les bits d'un meme equipement seront groupes dans un meme mot.", _
        "Attribuer les adresses", CStr(mwDebut))
    If Len(Trim$(depart)) = 0 Then Exit Sub

    Dim depuis As Long
    depuis = CLng(Val(depart))
    If depuis < mwDebut Or depuis > mwFin Then
        MsgBox depuis & " est hors de la plage " & mwDebut & " - " & mwFin & "." & _
               vbLf & vbLf & "Elargissez la plage dans l'onglet Config, ou " & _
               "choisissez une adresse dedans.", vbExclamation
        Exit Sub
    End If
    mwDebut = depuis

    ' Ce qui est occupe : les mots entiers, et les bits mot par mot.
    Dim motPlein As Object, bitsPris As Object
    Set motPlein = CreateObject("Scripting.Dictionary")
    Set bitsPris = CreateObject("Scripting.Dictionary")

    Dim cZ As Long, cN As Long, cB As Long, cD As Long
    cZ = ColOf(li, "Zone"): cN = ColOf(li, "Numero")
    cB = ColOf(li, "Bit"):  cD = ColOf(li, "Designation")

    Dim r As Long, last As Long
    last = LastRow(li, cD)
    For r = FIRST_ROW To last
        If StrComp(Trim$(CStr(li.Cells(r, 1).Value)), moi, vbTextCompare) <> 0 Then
            If UCase$(Trim$(CStr(li.Cells(r, cZ).Value))) = "MW" Then
                Dim nu As String, bi As String
                nu = Trim$(CStr(li.Cells(r, cN).Value))
                bi = Trim$(CStr(li.Cells(r, cB).Value))
                If Len(nu) > 0 Then
                    If Len(bi) = 0 Then
                        motPlein(nu) = 1
                    Else
                        bitsPris(nu & ":" & bi) = 1
                        bitsPris(nu) = 1          ' ce mot porte des bits
                    End If
                End If
            End If
        End If
    Next r

    ' Deux passes : les booleens d'abord, pour qu'ils se groupent dans les
    ' premiers mots, puis les mots entiers. Dans l'autre ordre, un mot entier
    ' pose au milieu couperait un groupe de bits en deux.
    Dim motCourant As Long, bitCourant As Long, poses As Long, sautes As Long
    motCourant = -1
    bitCourant = 0

    Dim ligne As Long, pass As Long
    For pass = 0 To 1
        ligne = A_FIRST
        Do While Len(CStr(a.Cells(ligne, 2).Value)) > 0
            Dim estBool As Boolean
            estBool = (UCase$(Trim$(CStr(a.Cells(ligne, 3).Value))) = "BOOL")

            ' On ne touche QUE les lignes vides : une adresse posee a la main
            ' est un choix, pas un oubli.
            If Len(Trim$(CStr(a.Cells(ligne, 10).Value))) = 0 _
               And Len(Trim$(CStr(a.Cells(ligne, 12).Value))) > 0 Then

                If pass = 0 And estBool Then
                    If motCourant < 0 Or bitCourant > 15 Then
                        motCourant = MotAvecBitsLibres(motPlein, bitsPris, mwDebut, mwFin)
                        bitCourant = 0
                        If motCourant < 0 Then
                            sautes = sautes + 1
                            GoTo Suivante
                        End If
                    End If
                    Do While bitsPris.Exists(CStr(motCourant) & ":" & CStr(bitCourant))
                        bitCourant = bitCourant + 1
                        If bitCourant > 15 Then
                            motCourant = MotAvecBitsLibres(motPlein, bitsPris, mwDebut, mwFin)
                            bitCourant = 0
                            If motCourant < 0 Then
                                sautes = sautes + 1
                                GoTo Suivante
                            End If
                        End If
                    Loop
                    a.Cells(ligne, 9).Value = "MW"
                    a.Cells(ligne, 10).Value = motCourant
                    a.Cells(ligne, 11).Value = bitCourant
                    bitsPris(CStr(motCourant) & ":" & CStr(bitCourant)) = 1
                    bitsPris(CStr(motCourant)) = 1
                    bitCourant = bitCourant + 1
                    poses = poses + 1

                ElseIf pass = 1 And Not estBool Then
                    Dim mot As Long
                    mot = MotEntierLibre(motPlein, bitsPris, mwDebut, mwFin)
                    If mot < 0 Then
                        sautes = sautes + 1
                    Else
                        a.Cells(ligne, 9).Value = "MW"
                        a.Cells(ligne, 10).Value = mot
                        a.Cells(ligne, 11).Value = ""
                        motPlein(CStr(mot)) = 1
                        poses = poses + 1
                    End If
                End If
            End If
Suivante:
            ligne = ligne + 1
        Loop
    Next pass

    VerifierAdresses
    Dim message As String
    message = poses & " adresse(s) attribuee(s)."
    If sautes > 0 Then
        message = message & vbLf & vbLf & sautes & " parametre(s) n'ont rien recu : " & _
                  "la plage %MW " & mwDebut & " - " & mwFin & " est pleine." & vbLf & _
                  "Elargissez-la dans l'onglet Config."
    End If
    message = message & vbLf & vbLf & "Les lignes deja renseignees n'ont pas ete " & _
              "touchees : une adresse posee a la main est un choix, pas un oubli."
    Journal "Adresses attribuees", moi & " : " & poses & " adresse(s) a partir de %MW" & mwDebut
    MsgBox message, vbInformation
End Sub

Private Function MotAvecBitsLibres(motPlein As Object, bitsPris As Object, _
                                   ByVal debut As Long, ByVal fin As Long) As Long
    Dim m As Long, b As Long
    For m = debut To fin
        If Not motPlein.Exists(CStr(m)) Then
            For b = 0 To 15
                If Not bitsPris.Exists(CStr(m) & ":" & CStr(b)) Then
                    MotAvecBitsLibres = m
                    Exit Function
                End If
            Next b
        End If
    Next m
    MotAvecBitsLibres = -1
End Function

Private Function MotEntierLibre(motPlein As Object, bitsPris As Object, _
                                ByVal debut As Long, ByVal fin As Long) As Long
    Dim m As Long
    For m = debut To fin
        ' Un mot entier ne peut pas se poser sur un mot dont on utilise deja des
        ' bits : les deux se recouvrent.
        If Not motPlein.Exists(CStr(m)) And Not bitsPris.Exists(CStr(m)) Then
            MotEntierLibre = m
            Exit Function
        End If
    Next m
    MotEntierLibre = -1
End Function


' Colorie en ROUGE toute adresse deja prise, avec la raison en clair.
' Decouvrir un chevauchement a la cartographie, c'est le decouvrir apres avoir
' saisi quarante lignes. Le voir en le tapant, c'est le corriger tout de suite.
Public Sub VerifierAdresses()
    Dim a As Worksheet, li As Worksheet
    Set a = Sh(SH_ASSIST)
    Set li = Sh(SH_LIENS)
    If UCase$(Trim$(CStr(a.Range("C4").Value))) <> "EQ" Then Exit Sub

    Dim moi As String
    moi = Trim$(CStr(a.Range("C7").Value))

    Dim pris As Object, motsPleins As Object, motsBits As Object
    Set pris = CreateObject("Scripting.Dictionary")
    Set motsPleins = CreateObject("Scripting.Dictionary")
    Set motsBits = CreateObject("Scripting.Dictionary")

    Dim cZ As Long, cN As Long, cB As Long, cD As Long
    cZ = ColOf(li, "Zone"): cN = ColOf(li, "Numero")
    cB = ColOf(li, "Bit"):  cD = ColOf(li, "Designation")

    Dim r As Long, last As Long
    last = LastRow(li, cD)
    For r = FIRST_ROW To last
        If StrComp(Trim$(CStr(li.Cells(r, 1).Value)), moi, vbTextCompare) <> 0 Then
            Dim z As String, nu As String, bi As String
            z = UCase$(Trim$(CStr(li.Cells(r, cZ).Value)))
            nu = Trim$(CStr(li.Cells(r, cN).Value))
            bi = Trim$(CStr(li.Cells(r, cB).Value))
            If Len(z) > 0 And Len(nu) > 0 Then
                If Len(bi) > 0 Then
                    pris(z & nu & ":" & bi) = CStr(li.Cells(r, 1).Value)
                    motsBits(z & nu) = CStr(li.Cells(r, 1).Value)
                Else
                    pris(z & nu) = CStr(li.Cells(r, 1).Value)
                    motsPleins(z & nu) = CStr(li.Cells(r, 1).Value)
                End If
            End If
        End If
    Next r

    Dim vues As Object
    Set vues = CreateObject("Scripting.Dictionary")
    Dim ligne As Long, conflits As Long
    ligne = A_FIRST
    Do While Len(CStr(a.Cells(ligne, 2).Value)) > 0
        Dim zone As String, num As String, bit_ As String, cle As String, raison As String
        zone = UCase$(Trim$(CStr(a.Cells(ligne, 9).Value)))
        num = Trim$(CStr(a.Cells(ligne, 10).Value))
        bit_ = Trim$(CStr(a.Cells(ligne, 11).Value))
        raison = ""

        If Len(zone) = 0 Or Len(num) = 0 Then
            a.Cells(ligne, 13).Value = ""
            Dim c2 As Long
            For c2 = 9 To 11
                a.Cells(ligne, c2).Interior.Color = RGB(255, 253, 231)
            Next c2
        Else
            If Len(bit_) > 0 Then
                cle = zone & num & ":" & bit_
                If pris.Exists(cle) Then
                    raison = "PRIS par " & pris(cle)
                ElseIf motsPleins.Exists(zone & num) Then
                    raison = "le mot entier est pris par " & motsPleins(zone & num)
                End If
            Else
                cle = zone & num
                If pris.Exists(cle) Then
                    raison = "PRIS par " & pris(cle)
                ElseIf motsBits.Exists(zone & num) Then
                    raison = "des bits de ce mot sont pris par " & motsBits(zone & num)
                End If
            End If
            If Len(raison) = 0 And vues.Exists(cle) Then
                raison = "deja saisi ligne " & vues(cle) & " ci-dessus"
            End If

            Dim c3 As Long
            If Len(raison) = 0 Then
                vues(cle) = ligne
                a.Cells(ligne, 13).Value = "libre"
                a.Cells(ligne, 13).Font.Color = RGB(0, 128, 0)
                For c3 = 9 To 11
                    a.Cells(ligne, c3).Interior.Color = RGB(226, 239, 218)
                Next c3
            Else
                conflits = conflits + 1
                a.Cells(ligne, 13).Value = raison
                a.Cells(ligne, 13).Font.Color = RGB(192, 0, 0)
                For c3 = 9 To 11
                    a.Cells(ligne, c3).Interior.Color = RGB(255, 199, 206)
                Next c3
            End If
            a.Cells(ligne, 13).Font.Size = 8
        End If
        ligne = ligne + 1
    Loop

    If conflits = 0 Then
        a.Range("G5").Value = ""
    Else
        a.Range("G5").Value = conflits & " adresse(s) en conflit"
        a.Range("G5").Font.Color = RGB(192, 0, 0)
        a.Range("G5").Font.Bold = True
    End If
    a.Rows(5).Hidden = False
End Sub


Private Sub AppliquerEquipement()
    Dim a As Worksheet, eq As Worksheet, li As Worksheet, cab As Worksheet
    Set a = Sh(SH_ASSIST)
    Set eq = Sh(SH_EQUIP)
    Set li = Sh(SH_LIENS)
    Set cab = Sh(SH_CABLE)

    Dim nom As String, famille As String
    nom = Trim$(CStr(a.Range("C7").Value))
    famille = Trim$(CStr(a.Range("C8").Value))
    If Len(nom) = 0 Or Len(famille) = 0 Then
        MsgBox "Il faut au moins un nom et une famille.", vbExclamation
        Exit Sub
    End If

    Dim cible As Long
    cible = CLng(Val(CStr(a.Range("C5").Value)))

    ' Un homonyme rend tous les liens ambigus : ils designent par le nom.
    Dim r As Long, last As Long
    last = LastRow(eq, 1)
    For r = FIRST_ROW To last
        If r <> cible And StrComp(Trim$(CStr(eq.Cells(r, 1).Value)), nom, vbTextCompare) = 0 Then
            MsgBox "Un equipement s'appelle deja " & nom & " (ligne " & r & ").", vbExclamation
            Exit Sub
        End If
    Next r

    ' L'INDEX EST-IL LIBRE ? Deux equipements sur Pompes[0] ecriraient dans la
    ' meme structure : le second effacerait ce que le premier vient de calculer,
    ' a chaque cycle, sans qu'aucun des deux ne s'en apercoive.
    Dim forme As String, variable As String, index As String
    forme = UCase$(Trim$(CStr(a.Range("C10").Value)))
    variable = Trim$(CStr(a.Range("C11").Value))
    index = Trim$(CStr(a.Range("C12").Value))

    If Len(variable) = 0 Then
        MsgBox "Il faut un nom de variable ST.", vbExclamation
        Exit Sub
    End If

    ' Un DFB n'a pas de forme : c'est toujours une instance seule.
    If GenreDe(famille) = "dfb" Then forme = "VARIABLE SEULE"

    If forme = "VARIABLE SEULE" Then
        ' Une variable seule ne partage rien : c'est son NOM qui doit etre libre.
        For r = FIRST_ROW To last
            If r <> cible And StrComp(Trim$(CStr(eq.Cells(r, 6).Value)), variable, _
                                      vbTextCompare) = 0 Then
                If UCase$(Trim$(CStr(eq.Cells(r, 5).Value))) = "VARIABLE SEULE" Then
                    MsgBox "La variable " & variable & " est deja celle de " & _
                           eq.Cells(r, 1).Value & " (ligne " & r & ")." & vbLf & vbLf & _
                           "Une variable seule n'a qu'un proprietaire ; si vous " & _
                           "voulez plusieurs equipements, passez en Tableau.", _
                           vbExclamation
                    Exit Sub
                End If
            End If
        Next r
    Else
        If Len(index) = 0 Then
            MsgBox "En forme Tableau, l'index est obligatoire." & vbLf & vbLf & _
                   "Sinon choisissez Variable seule : l'acces sera " & variable & _
                   " tout court.", vbExclamation
            Exit Sub
        End If
        For r = FIRST_ROW To last
            If r <> cible Then
                If StrComp(Trim$(CStr(eq.Cells(r, 6).Value)), variable, vbTextCompare) = 0 _
                   And CStr(eq.Cells(r, 7).Value) = index _
                   And UCase$(Trim$(CStr(eq.Cells(r, 5).Value))) <> "VARIABLE SEULE" Then
                    MsgBox variable & "[" & index & "] est deja occupe par " & _
                           eq.Cells(r, 1).Value & " (ligne " & r & ")." & vbLf & vbLf & _
                           "Le premier index libre de ce tableau est " & _
                           PremierIndexLibre(eq, variable, cible) & ".", vbExclamation
                    Exit Sub
                End If
            End If
        Next r
    End If

    ' UNE SORTIE N'A QU'UN SEUL ECRIVAIN. Deux equipements qui lisent la meme
    ' entree est courant ; deux qui commandent la meme sortie est une
    ' contradiction que l'automate tranchera au hasard du dernier ecrivain.
    Dim conflit As String
    conflit = SortieDejaEcrite(a, nom)
    If Len(conflit) > 0 Then
        MsgBox conflit & vbLf & vbLf & _
               "Deux equipements qui LISENT la meme entree, c'est normal. Deux " & _
               "qui ECRIVENT la meme sortie, non.", vbExclamation
        Exit Sub
    End If

    VerifierAdresses
    If Len(Trim$(CStr(a.Range("G5").Value))) > 0 Then
        MsgBox a.Range("G5").Value & "." & vbLf & vbLf & _
               "Les cases rouges disent qui occupe deja ces adresses. Corrigez-les, " & _
               "ou videz Zone et Numero pour ne pas reporter ces parametres.", _
               vbExclamation
        Exit Sub
    End If

    Application.EnableEvents = False
    If cible = 0 Then
        cible = last + 1
        If cible < FIRST_ROW Then cible = FIRST_ROW
    End If

    eq.Cells(cible, 1).Value = nom
    eq.Cells(cible, 2).Value = famille
    eq.Cells(cible, 3).Value = a.Range("C9").Value
    eq.Cells(cible, 4).Value = "O"
    eq.Cells(cible, 5).Value = a.Range("C10").Value
    eq.Cells(cible, 6).Value = a.Range("C11").Value
    If UCase$(Trim$(CStr(a.Range("C10").Value))) = "VARIABLE SEULE" Then
        eq.Cells(cible, 7).Value = ""       ' une variable seule n'a pas d'index
    Else
        eq.Cells(cible, 7).Value = a.Range("C12").Value
    End If
    eq.Cells(cible, 8).Value = a.Range("C13").Value
    eq.Cells(cible, 9).Formula = "=IF(F" & cible & "="""","""",IF(OR(E" & cible & _
        "=""Variable seule"",G" & cible & "=""""),F" & cible & ",F" & cible & _
        "&""[""&G" & cible & "&""]""))"

    ' Reecrits, pas ajoutes : retirer une adresse doit retirer le report.
    SupprimerLignes li, nom
    SupprimerLignes cab, nom

    Dim ligne As Long, reports As Long, voies As Long
    ligne = A_FIRST
    Do While Len(CStr(a.Cells(ligne, 2).Value)) > 0
        Dim param As String, sens As String, libelle As String, attribut As String
        param = CStr(a.Cells(ligne, 2).Value)
        sens = UCase$(Trim$(CStr(a.Cells(ligne, 5).Value)))
        libelle = Trim$(CStr(a.Cells(ligne, 6).Value))
        attribut = Trim$(CStr(a.Cells(ligne, 7).Value))

        ' --- le cablage, s'il y en a un
        If Len(sens) > 0 And Len(libelle) > 0 Then
            Dim ligneES As Long
            ligneES = LigneESDe(a, sens, libelle)
            If ligneES > 0 Then
                EcrireCablage cab, nom, param, sens, ligneES, attribut
                voies = voies + 1
            End If
        End If

        ' --- le report, s'il y en a un
        Dim zone As String, num As String
        zone = UCase$(Trim$(CStr(a.Cells(ligne, 9).Value)))
        num = Trim$(CStr(a.Cells(ligne, 10).Value))
        If Len(zone) > 0 And Len(num) > 0 Then
            EcrireLien li, nom, param, a.Cells(ligne, 12).Value, _
                       CStr(a.Cells(ligne, 4).Value), zone, num, _
                       CStr(a.Cells(ligne, 11).Value)
            reports = reports + 1
        End If
        ligne = ligne + 1
    Loop
    Application.EnableEvents = True

    FermerAssistant
    Application.ScreenUpdating = False
    ToutRecalculerSilencieux
    Application.ScreenUpdating = True

    Journal IIf(cible > last, "Equipement ajoute", "Equipement modifie"), _
            nom & " (" & famille & ") : " & voies & " voie(s), " & reports & " report(s)"
    MsgBox nom & " enregistre." & vbLf & vbLf & _
           voies & " voie(s) cablee(s)" & vbLf & _
           reports & " report(s)" & vbLf & vbLf & _
           "Les onglets de reports et les cartographies sont a jour.", vbInformation
End Sub


Private Sub EcrireCablage(cab As Worksheet, ByVal nom As String, ByVal param As String, _
                          ByVal sens As String, ByVal ligneES As Long, ByVal attribut As String)
    Dim es As Worksheet
    Set es = Sh(SH_ES)
    Dim outRow As Long
    outRow = LastRow(cab, 1) + 1
    If outRow < FIRST_ROW Then outRow = FIRST_ROW

    cab.Cells(outRow, 1).Value = nom
    cab.Cells(outRow, 2).Value = param
    cab.Cells(outRow, 3).Formula = "=IFERROR(IF(OR(A" & outRow & "="""",B" & outRow & _
        "=""""),"""",INDEX(Equipements!$H:$H,MATCH(A" & outRow & _
        ",Equipements!$A:$A,0))&"".""&B" & outRow & "),"""")"
    cab.Cells(outRow, 4).Value = sens
    cab.Cells(outRow, 5).Value = ligneES
    ' L'acces est RECONSTRUIT depuis Tableau et Index, pas relu dans la colonne
    ' "Acces ST" de l'onglet ES. Celle-ci porte une formule, et .Value rend son
    ' dernier resultat CALCULE : sur une voie creee dans la meme execution,
    ' Excel n'a pas encore recalcule et on recopiait une chaine vide.
    cab.Cells(outRow, 6).Value = _
        CStr(es.Cells(ligneES, ColOf(es, "Tableau")).Value) & "[" & _
        CStr(es.Cells(ligneES, ColOf(es, "Index")).Value) & "]"
    cab.Cells(outRow, 7).Value = attribut
    cab.Cells(outRow, 8).Formula = "=IF(F" & outRow & "="""","""",F" & outRow & _
        "&IF(G" & outRow & "="""","""","".""&G" & outRow & "))"
    cab.Cells(outRow, 9).Value = es.Cells(ligneES, ColOf(es, "Adresse")).Value
    If Len(Trim$(CStr(cab.Cells(outRow, 9).Value))) = 0 Then
        ' Meme raison : l'adresse est une formule. Si elle n'est pas encore
        ' calculee, on force le recalcul de cette cellule-la plutot que de
        ' laisser une case vide que personne ne comprendra.
        es.Cells(ligneES, ColOf(es, "Adresse")).Calculate
        cab.Cells(outRow, 9).Value = es.Cells(ligneES, ColOf(es, "Adresse")).Value
    End If
    cab.Cells(outRow, 10).Value = es.Cells(ligneES, ColOf(es, "Designation")).Value
End Sub


Private Sub EcrireLien(li As Worksheet, ByVal nom As String, ByVal param As String, _
                       ByVal designation As String, ByVal typeReport As String, _
                       ByVal zone As String, ByVal num As String, ByVal bit_ As String)
    Dim outRow As Long
    outRow = LastRow(li, ColOf(li, "Designation")) + 1
    If outRow < FIRST_ROW Then outRow = FIRST_ROW

    li.Cells(outRow, 1).Value = nom
    li.Cells(outRow, 2).Value = param
    li.Cells(outRow, 3).Formula = "=IFERROR(IF(OR(A" & outRow & "="""",B" & outRow & _
        "=""""),"""",INDEX(Equipements!$H:$H,MATCH(A" & outRow & _
        ",Equipements!$A:$A,0))&"".""&B" & outRow & "),"""")"
    li.Cells(outRow, 4).Value = designation
    ' Une commande se lit depuis l'IHM, tout le reste part vers elle.
    li.Cells(outRow, 5).Value = IIf(UCase$(typeReport) = "TC", "W", "R")
    li.Cells(outRow, 6).Value = typeReport
    li.Cells(outRow, 7).Value = zone
    li.Cells(outRow, 8).Value = Val(num)
    If Len(bit_) > 0 Then li.Cells(outRow, 9).Value = Val(bit_)
    li.Cells(outRow, 10).Formula = "=IF(OR(G" & outRow & "="""",H" & outRow & _
        "=""""),"""",""%""&G" & outRow & "&H" & outRow & ")"
    li.Cells(outRow, 15).Formula = "=IF(H" & outRow & "="""","""",IF(I" & outRow & _
        "="""",""mot entier"",""bit ""&I" & outRow & "))"
End Sub


Private Sub SupprimerLignes(ws As Worksheet, ByVal nom As String)
    Dim r As Long, last As Long
    last = LastRow(ws, 1)
    For r = last To FIRST_ROW Step -1
        If StrComp(Trim$(CStr(ws.Cells(r, 1).Value)), nom, vbTextCompare) = 0 Then
            ws.Rows(r).Delete
        End If
    Next r
End Sub


' Le premier index libre d'un tableau. Dire "pris" sans dire lequel prendre
' oblige a aller compter a la main.
Private Function PremierIndexLibre(eq As Worksheet, ByVal variable As String, _
                                   ByVal saufLigne As Long) As Long
    Dim pris As Object
    Set pris = CreateObject("Scripting.Dictionary")
    Dim r As Long, last As Long
    last = LastRow(eq, 1)
    For r = FIRST_ROW To last
        If r <> saufLigne Then
            If StrComp(Trim$(CStr(eq.Cells(r, 6).Value)), variable, vbTextCompare) = 0 Then
                pris(CStr(eq.Cells(r, 7).Value)) = 1
            End If
        End If
    Next r
    Dim i As Long
    For i = 0 To 255
        If Not pris.Exists(CStr(i)) Then
            PremierIndexLibre = i
            Exit Function
        End If
    Next i
    PremierIndexLibre = -1
End Function

Private Function SortieDejaEcrite(a As Worksheet, ByVal nom As String) As String
    Dim cab As Worksheet
    Set cab = Sh(SH_CABLE)
    Dim ligne As Long
    ligne = A_FIRST
    Do While Len(CStr(a.Cells(ligne, 2).Value)) > 0
        Dim sens As String, libelle As String
        sens = UCase$(Trim$(CStr(a.Cells(ligne, 5).Value)))
        libelle = Trim$(CStr(a.Cells(ligne, 6).Value))
        If (sens = "DO" Or sens = "AO") And Len(libelle) > 0 Then
            Dim ligneES As Long
            ligneES = LigneESDe(a, sens, libelle)
            If ligneES > 0 Then
                Dim r As Long, last As Long
                last = LastRow(cab, 1)
                For r = FIRST_ROW To last
                    If StrComp(Trim$(CStr(cab.Cells(r, 1).Value)), nom, vbTextCompare) <> 0 Then
                        If CLng(Val(CStr(cab.Cells(r, 5).Value))) = ligneES Then
                            SortieDejaEcrite = "La sortie " & libelle & _
                                " est deja commandee par " & cab.Cells(r, 1).Value & _
                                "." & cab.Cells(r, 2).Value & "."
                            Exit Function
                        End If
                    End If
                Next r
            End If
        End If
        ligne = ligne + 1
    Loop
End Function


'============================================ 5. RECALCUL, SUPPRESSION, ST =====
Public Sub ToutRecalculer()
    ToutRecalculerCore True
End Sub

Public Sub ToutRecalculerSilencieux()
    ToutRecalculerCore False
End Sub

' Les trois onglets de reports et les deux cartographies, ensemble.
' Les separer voulait dire qu'un jour on n'en ferait qu'un, et qu'un onglet
' montrerait un equipement de moins que la feuille Liens. Un classeur a moitie
' a jour est pire que pas a jour : on ne sait plus lequel croire.
Private Sub ToutRecalculerCore(ByVal parler As Boolean)
    Dim li As Worksheet
    Set li = Sh(SH_LIENS)

    Dim cDes As Long, cType As Long, cZ As Long, cN As Long, cB As Long
    Dim cAdr As Long, cU As Long, cMin As Long, cMax As Long, cRes As Long, cCom As Long
    cDes = ColOf(li, "Designation"): cType = ColOf(li, "Type")
    cZ = ColOf(li, "Zone"):          cN = ColOf(li, "Numero")
    cB = ColOf(li, "Bit"):           cAdr = ColOf(li, "Adr API")
    cU = ColOf(li, "Unite"):         cMin = ColOf(li, "Mini")
    cMax = ColOf(li, "Maxi"):        cRes = ColOf(li, "Resolution")
    cCom = ColOf(li, "Commentaire")

    Application.ScreenUpdating = False
    ViderReport "Reports TOR"
    ViderReport "Reports bits"
    ViderReport "Reports ANA"

    Dim nT As Long, nB As Long, nA As Long
    nT = FIRST_ROW: nB = FIRST_ROW: nA = FIRST_ROW

    Dim r As Long, last As Long
    last = LastRow(li, cDes)
    For r = FIRST_ROW To last
        Dim des As String, zone As String, bit_ As String, ty As String
        des = Trim$(CStr(li.Cells(r, cDes).Value))
        If Len(des) > 0 Then
            zone = UCase$(Trim$(CStr(li.Cells(r, cZ).Value)))
            bit_ = Trim$(CStr(li.Cells(r, cB).Value))
            ty = UCase$(Trim$(CStr(li.Cells(r, cType).Value)))

            ' Le tri est decide par la ZONE et le BIT, jamais par une case a
            ' cocher : un report sur un bit de mot EST un report de bit.
            If zone = "MX" Then
                With Sh("Reports TOR")
                    .Cells(nT, 1).Value = des
                    .Cells(nT, 2).Value = li.Cells(r, cAdr).Value
                    .Cells(nT, 5).Value = IIf(ty = "TM", "X", "")
                    .Cells(nT, 6).Value = IIf(ty = "TA", "X", "")
                    .Cells(nT, 7).Value = IIf(ty = "TC", "X", "")
                    .Cells(nT, 12).Value = li.Cells(r, cCom).Value
                End With
                nT = nT + 1
            ElseIf zone = "MW" And Len(bit_) > 0 Then
                With Sh("Reports bits")
                    .Cells(nB, 1).Value = des
                    .Cells(nB, 2).Value = "%MW" & li.Cells(r, cN).Value
                    .Cells(nB, 3).Value = li.Cells(r, cB).Value
                    .Cells(nB, 4).Value = IIf(ty = "TM", "X", "")
                    .Cells(nB, 5).Value = IIf(ty = "TA", "X", "")
                    .Cells(nB, 6).Value = IIf(ty = "TC", "X", "")
                    .Cells(nB, 12).Value = li.Cells(r, cCom).Value
                End With
                nB = nB + 1
            ElseIf zone = "MW" Then
                With Sh("Reports ANA")
                    .Cells(nA, 1).Value = des
                    .Cells(nA, 2).Value = li.Cells(r, cAdr).Value
                    .Cells(nA, 3).Value = IIf(ty = "TC", "", "X")
                    .Cells(nA, 4).Value = IIf(ty = "TC", "X", "")
                    .Cells(nA, 5).Value = li.Cells(r, cU).Value
                    .Cells(nA, 6).Value = li.Cells(r, cMin).Value
                    .Cells(nA, 7).Value = li.Cells(r, cMax).Value
                    .Cells(nA, 8).Value = li.Cells(r, cRes).Value
                    .Cells(nA, 12).Value = li.Cells(r, cCom).Value
                End With
                nA = nA + 1
            End If
        End If
    Next r

    Dim conflits As Long, details As String
    conflits = Cartographier(details)
    Application.ScreenUpdating = True

    Etat 4, Now
    Etat 5, Now
    Etat 6, conflits

    ' Un chevauchement se dit TOUJOURS, meme en mode silencieux : c'est
    ' exactement le genre de nouvelle qu'on ne tait pas pour ne pas deranger.
    If conflits > 0 Then
        MsgBox conflits & " CHEVAUCHEMENT(S) :" & details & vbLf & vbLf & _
               "Les cases rouges des cartographies les montrent.", vbExclamation
    ElseIf parler Then
        MsgBox "A jour." & vbLf & vbLf & _
               "TOR (%MX)        : " & (nT - FIRST_ROW) & vbLf & _
               "Bits de mots     : " & (nB - FIRST_ROW) & vbLf & _
               "Analogique (%MW) : " & (nA - FIRST_ROW) & vbLf & vbLf & _
               "Aucun chevauchement.", vbInformation
    End If
End Sub

Private Sub ViderReport(ByVal nom As String)
    Dim ws As Worksheet
    Set ws = Sh(nom)
    Dim last As Long
    last = ws.Cells(ws.Rows.Count, 1).End(xlUp).Row
    If last >= FIRST_ROW Then
        ws.Range(ws.Cells(FIRST_ROW, 1), ws.Cells(last, 14)).ClearContents
    End If
End Sub


' La cartographie, et les trois formes de chevauchement.
Private Function Cartographier(ByRef details As String) As Long
    Dim li As Worksheet
    Set li = Sh(SH_LIENS)
    Dim cZ As Long, cN As Long, cB As Long, cD As Long
    cZ = ColOf(li, "Zone"): cN = ColOf(li, "Numero")
    cB = ColOf(li, "Bit"):  cD = ColOf(li, "Designation")

    Dim mw As Object, mx As Object, bits As Object
    Set mw = CreateObject("Scripting.Dictionary")
    Set mx = CreateObject("Scripting.Dictionary")
    Set bits = CreateObject("Scripting.Dictionary")

    Dim conflits As Long, r As Long, last As Long
    last = LastRow(li, cD)
    For r = FIRST_ROW To last
        Dim zone As String, num As String, bit_ As String
        zone = UCase$(Trim$(CStr(li.Cells(r, cZ).Value)))
        num = Trim$(CStr(li.Cells(r, cN).Value))
        bit_ = Trim$(CStr(li.Cells(r, cB).Value))
        If Len(num) > 0 Then
            If zone = "MX" Then
                If mx.Exists(num) Then
                    conflits = conflits + 1
                    details = details & vbLf & "  %MX" & num & " : deux reports"
                Else
                    mx(num) = r
                End If
            ElseIf zone = "MW" Then
                If Len(bit_) = 0 Then
                    If mw.Exists(num) Or bits.Exists(num) Then
                        conflits = conflits + 1
                        details = details & vbLf & "  %MW" & num & _
                                  " : mot entier sur une adresse deja utilisee"
                    Else
                        mw(num) = r
                    End If
                Else
                    Dim cle As String
                    cle = num & ":" & bit_
                    If mw.Exists(num) Then
                        conflits = conflits + 1
                        details = details & vbLf & "  %MW" & num & " bit " & bit_ & _
                                  " : le mot entier est deja pris"
                    ElseIf bits.Exists(cle) Then
                        conflits = conflits + 1
                        details = details & vbLf & "  %MW" & num & " bit " & bit_ & _
                                  " : deux reports"
                    Else
                        bits(cle) = r
                        bits(num) = r
                    End If
                End If
            End If
        End If
    Next r

    Peindre "Memoire MW", mw, bits
    Peindre "Memoire MX", mx, Nothing
    Cartographier = conflits
End Function

Private Sub Peindre(ByVal nom As String, plein As Object, partiel As Object)
    Dim ws As Worksheet
    Set ws = Sh(nom)
    Dim lastLine As Long
    lastLine = ws.Cells(ws.Rows.Count, 1).End(xlUp).Row
    ws.Range(ws.Cells(FIRST_ROW, 2), ws.Cells(lastLine, 70)).Interior.Pattern = xlNone

    ' LA BASE EST LUE DANS L'EN-TETE DE LA LIGNE, pas calculee depuis le haut de
    ' la feuille. Les en-tetes sont des formules qui partent de la plage de
    ' Config : supposer que la premiere ligne commence a zero faisait peindre la
    ' bonne couleur sur la mauvaise case des qu'on changeait la plage.
    Dim line As Long, base As Long, k As Long
    For line = FIRST_ROW To lastLine
        Dim entete As String
        entete = CStr(ws.Cells(line, 1).Value)
        If Len(entete) = 0 Then GoTo LigneSuivante
        ' "%MW1000" et "%MX512" : trois caracteres de prefixe dans les deux cas.
        base = CLng(Val(Mid$(entete, 4)))
        For k = 0 To 63
            Dim addr As String
            addr = CStr(base + k)
            If plein.Exists(addr) Then
                ws.Cells(line, 2 + k).Interior.Color = RGB(189, 215, 238)
            ElseIf Not partiel Is Nothing Then
                If partiel.Exists(addr) Then
                    ws.Cells(line, 2 + k).Interior.Color = RGB(198, 239, 206)
                End If
            End If
        Next k
LigneSuivante:
    Next line
End Sub


'  Supprimer : l'equipement, ses reports, son cablage. Le bouton DEMANDE lequel.
Public Sub SupprimerEquipement()
    Dim eq As Worksheet
    Set eq = Sh(SH_EQUIP)
    Dim last As Long, r As Long, n As Long
    last = LastRow(eq, 1)

    Dim cible As Long
    If ActiveSheet.name = SH_EQUIP And ActiveCell.Row >= FIRST_ROW Then
        If Len(Trim$(CStr(eq.Cells(ActiveCell.Row, 1).Value))) > 0 Then
            cible = ActiveCell.Row
        End If
    End If

    If cible = 0 Then
        Dim liste As String
        For r = FIRST_ROW To last
            If Len(Trim$(CStr(eq.Cells(r, 1).Value))) > 0 Then
                n = n + 1
                liste = liste & vbLf & "  " & n & ".  " & eq.Cells(r, 1).Value & _
                        "   (" & eq.Cells(r, 2).Value & ")"
            End If
        Next r
        If n = 0 Then
            MsgBox "Aucun equipement a supprimer.", vbInformation
            Exit Sub
        End If
        Dim choix As String
        choix = InputBox("Quel equipement supprimer ?" & vbLf & liste & vbLf & vbLf & _
                         "Son numero, ou son nom.", "Supprimer")
        If Len(Trim$(choix)) = 0 Then Exit Sub
        Dim k As Long
        For r = FIRST_ROW To last
            If Len(Trim$(CStr(eq.Cells(r, 1).Value))) > 0 Then
                k = k + 1
                If CStr(k) = Trim$(choix) Or _
                   StrComp(Trim$(CStr(eq.Cells(r, 1).Value)), Trim$(choix), vbTextCompare) = 0 Then
                    cible = r
                    Exit For
                End If
            End If
        Next r
        If cible = 0 Then
            MsgBox "Aucun equipement ne correspond a """ & choix & """.", vbExclamation
            Exit Sub
        End If
    End If

    Dim nom As String
    nom = Trim$(CStr(eq.Cells(cible, 1).Value))
    Dim reports As Long, voies As Long
    reports = Application.WorksheetFunction.CountIf(Sh(SH_LIENS).Columns(1), nom)
    voies = Application.WorksheetFunction.CountIf(Sh(SH_CABLE).Columns(1), nom)

    If MsgBox("Supprimer " & nom & " ?" & vbLf & vbLf & _
              reports & " report(s) et " & voies & " voie(s) cablee(s) partiront avec." & _
              vbLf & "Les adresses et les voies redeviendront libres.", _
              vbYesNo + vbQuestion, "Supprimer") <> vbYes Then Exit Sub

    Application.EnableEvents = False
    SupprimerLignes Sh(SH_LIENS), nom
    SupprimerLignes Sh(SH_CABLE), nom
    eq.Rows(cible).Delete
    Application.EnableEvents = True

    Application.ScreenUpdating = False
    ToutRecalculerSilencieux
    Application.ScreenUpdating = True
    Journal "Equipement supprime", nom & " : " & reports & " report(s) et " & _
            voies & " voie(s) liberes"
    MsgBox nom & " supprime, avec " & reports & " report(s) et " & voies & " voie(s).", _
           vbInformation
End Sub


'  Enlever un seul lien de cablage, sans toucher a l'equipement.
Public Sub EnleverLien()
    Dim cab As Worksheet
    Set cab = Sh(SH_CABLE)
    If ActiveSheet.name <> SH_CABLE Or ActiveCell.Row < FIRST_ROW Then
        cab.Activate
        MsgBox "Selectionnez la ligne du cablage a enlever, puis relancez.", vbExclamation
        Exit Sub
    End If
    Dim r As Long
    r = ActiveCell.Row
    If Len(Trim$(CStr(cab.Cells(r, 1).Value))) = 0 Then Exit Sub

    If MsgBox("Enlever le cablage" & vbLf & vbLf & "   " & cab.Cells(r, 3).Value & _
              "   <-   " & cab.Cells(r, 8).Value & vbLf & vbLf & _
              "La voie redeviendra libre. L'equipement et ses reports ne bougent pas.", _
              vbYesNo + vbQuestion, "Enlever le lien") <> vbYes Then Exit Sub

    Application.EnableEvents = False
    cab.Rows(r).Delete
    Application.EnableEvents = True
    MsgBox "Cablage enleve.", vbInformation
End Sub


'  Le ST, dans l'ordre ou il doit tourner.
Public Sub GenererST()
    Dim cab As Worksheet, li As Worksheet, out As Worksheet
    Set cab = Sh(SH_CABLE)
    Set li = Sh(SH_LIENS)
    Set out = Sh("ST genere")
    out.Cells.ClearContents

    Dim n As Long
    n = 1
    ' L'ORDRE N'EST PAS UNE QUESTION DE GOUT. Une voie alimente l'equipement,
    ' l'equipement calcule, le resultat part a l'IHM et aux sorties. L'inverser
    ' ferait remonter ce que l'equipement avait calcule au cycle PRECEDENT.
    n = Ecrire(out, n, "(* ===== 1. Cablage : voies d'ENTREE -> equipements ===== *)")
    Dim r As Long, last As Long
    last = LastRow(cab, 1)
    For r = FIRST_ROW To last
        Dim sens As String
        sens = UCase$(Trim$(CStr(cab.Cells(r, 4).Value)))
        If sens = "DI" Or sens = "AI" Then
            n = Ecrire(out, n, cab.Cells(r, 3).Value & " := " & cab.Cells(r, 8).Value & _
                       ";   (* " & cab.Cells(r, 10).Value & "  " & _
                       cab.Cells(r, 9).Value & " *)")
        End If
    Next r

    n = Ecrire(out, n, "")
    n = Ecrire(out, n, "(* ===== 2. Vos blocs d'equipement : a ecrire ici ===== *)")
    n = Ecrire(out, n, "")
    n = Ecrire(out, n, "(* ===== 3. Reports en LECTURE : API -> IHM ===== *)")
    last = LastRow(li, ColOf(li, "Designation"))
    Dim pass As Long
    For pass = 0 To 1
        If pass = 1 Then
            n = Ecrire(out, n, "")
            n = Ecrire(out, n, "(* ===== 5. Reports en ECRITURE : IHM -> API ===== *)")
        End If
        For r = FIRST_ROW To last
            Dim sensR As String, chem As String, adr As String, bit_ As String
            sensR = UCase$(Trim$(CStr(li.Cells(r, 5).Value)))
            chem = Trim$(CStr(li.Cells(r, 3).Value))
            bit_ = Trim$(CStr(li.Cells(r, 9).Value))
            If Len(chem) > 0 Then
                adr = "%" & UCase$(Trim$(CStr(li.Cells(r, 7).Value))) & _
                      Trim$(CStr(li.Cells(r, 8).Value))
                If Len(bit_) > 0 Then adr = adr & "." & bit_
                If pass = 0 And (sensR = "R" Or sensR = "RW") Then
                    n = Ecrire(out, n, adr & " := " & chem & ";")
                ElseIf pass = 1 And (sensR = "W" Or sensR = "RW") Then
                    n = Ecrire(out, n, chem & " := " & adr & ";")
                End If
            End If
        Next r
        If pass = 0 Then
            n = Ecrire(out, n, "")
            n = Ecrire(out, n, "(* ===== 4. Cablage : equipements -> voies de SORTIE ===== *)")
            Dim cr As Long, clast As Long
            clast = LastRow(cab, 1)
            For cr = FIRST_ROW To clast
                Dim s2 As String
                s2 = UCase$(Trim$(CStr(cab.Cells(cr, 4).Value)))
                If s2 = "DO" Or s2 = "AO" Then
                    n = Ecrire(out, n, cab.Cells(cr, 8).Value & " := " & _
                               cab.Cells(cr, 3).Value & ";   (* " & _
                               cab.Cells(cr, 10).Value & " *)")
                End If
            Next cr
        End If
    Next pass

    out.Visible = xlSheetVisible
    out.Activate
    MsgBox (n - 1) & " ligne(s). Copiez chaque bloc dans sa section, dans l'ordre.", _
           vbInformation
End Sub

Private Function Ecrire(out As Worksheet, ByVal n As Long, ByVal texte As String) As Long
    out.Cells(n, 1).Value = texte
    Ecrire = n + 1
End Function


'  Les boutons. A lancer une fois apres l'import : c'est la seule fois ou l'on a
'  besoin d'Alt+F8.
Public Sub InstallerBoutons()
    Dim ws As Worksheet
    Set ws = Sh(SH_CONFIG)
    Dim i As Long
    For i = ws.Buttons.Count To 1 Step -1
        ws.Buttons(i).Delete
    Next i

    Dim libelles As Variant, macros As Variant
    libelles = Array("1. INIT : lire le dossier libs", "2. Creer les voies des cartes", _
                     "3. Rafraichir la vue du rack", "4. Ajouter un equipement", _
                     "5. Supprimer un equipement", "6. Tout recalculer", _
                     "7. Generer le ST")
    macros = Array("Automation.INIT", "Automation.CreerVoies", _
                   "Automation.RafraichirVueRack", "Automation.AjouterEquipement", _
                   "Automation.SupprimerEquipement", "Automation.ToutRecalculer", _
                   "Automation.GenererST")

    Dim haut As Single
    haut = ws.Cells(6, 7).top
    For i = LBound(libelles) To UBound(libelles)
        Dim b As Object
        Set b = ws.Buttons.Add(ws.Cells(6, 7).Left, haut, 210, 26)
        b.Caption = libelles(i)
        b.OnAction = macros(i)
        haut = haut + 30
    Next i

    ' Le bouton qui n'a de sens que LA OU on s'en sert : enlever un cablage se
    ' decide en regardant la feuille Cablage, pas depuis Config.
    Dim cab As Worksheet
    Set cab = Sh(SH_CABLE)
    For i = cab.Buttons.Count To 1 Step -1
        cab.Buttons(i).Delete
    Next i
    Dim bc As Object
    Set bc = cab.Buttons.Add(cab.Cells(3, 12).Left, cab.Cells(3, 12).top, 200, 26)
    bc.Caption = "Enlever le lien selectionne"
    bc.OnAction = "Automation.EnleverLien"

    ' Et celui de l'onglet ES, pour la meme raison.
    Dim es As Worksheet
    Set es = Sh(SH_ES)
    For i = es.Buttons.Count To 1 Step -1
        es.Buttons(i).Delete
    Next i
    Dim be As Object
    Set be = es.Buttons.Add(es.Cells(3, 12).Left, es.Cells(3, 12).top, 200, 26)
    be.Caption = "Regler la voie selectionnee"
    be.OnAction = "Automation.ReglerVoieSelectionnee"

    PoserCasesOnglets
    ws.Activate
    MsgBox "Sept boutons sur Config, un sur Cablage, un sur ES." & vbLf & _
           "Et onze cases pour choisir les onglets visibles." & vbLf & vbLf & _
           "Vous n'aurez plus besoin d'Alt+F8.", vbInformation
End Sub


' Les onglets a montrer, par cases a cocher.
'
' QUINZE ONGLETS, C'EST TROP POUR UNE BARRE. Et la plupart ne servent qu'a un
' moment : la cartographie quand on attribue des adresses, les trois onglets de
' reports quand on les imprime, le catalogue presque jamais une fois INIT passe.
'
' Les cases vivent sur Config, et l'etat est relu a chaque changement. Config et
' ES ne sont pas proposees : masquer la feuille ou sont les cases n'aiderait
' personne, et masquer les E/S revient a masquer le classeur.
Public Sub PoserCasesOnglets()
    Dim ws As Worksheet
    Set ws = Sh(SH_CONFIG)

    Dim i As Long
    For i = ws.CheckBoxes.Count To 1 Step -1
        ws.CheckBoxes(i).Delete
    Next i

    Dim onglets As Variant
    onglets = Array(SH_CARTES, SH_RACK, SH_CAT, SH_EQUIP, SH_CABLE, SH_LIENS, _
                    "Reports TOR", "Reports bits", "Reports ANA", _
                    "Memoire MW", "Memoire MX")

    ws.Cells(6, 10).Value = "ONGLETS VISIBLES"
    ws.Cells(6, 10).Font.Bold = True
    ws.Cells(7, 10).Value = "Config et ES restent toujours la."
    ws.Cells(7, 10).Font.Italic = True
    ws.Cells(7, 10).Font.Size = 8

    Dim haut As Single
    haut = ws.Cells(8, 10).top
    For i = LBound(onglets) To UBound(onglets)
        Dim cb As Object
        Set cb = ws.CheckBoxes.Add(ws.Cells(8, 10).Left, haut, 170, 18)
        cb.Caption = CStr(onglets(i))
        cb.OnAction = "Automation.AppliquerVisibilite"
        cb.Value = IIf(Sh(CStr(onglets(i))).Visible = xlSheetVisible, 1, -4146)
        haut = haut + 20
    Next i

    ws.Activate
    MsgBox (UBound(onglets) + 1) & " cases posees sur Config." & vbLf & vbLf & _
           "Decochez ce dont vous ne vous servez pas : l'onglet disparait de la " & _
           "barre, et son contenu ne bouge pas d'un pouce.", vbInformation
End Sub

Public Sub AppliquerVisibilite()
    Dim ws As Worksheet
    Set ws = Sh(SH_CONFIG)
    Dim cb As Object, caches As Long

    For Each cb In ws.CheckBoxes
        Dim nom As String
        nom = CStr(cb.Caption)
        On Error Resume Next
        If cb.Value = 1 Then
            Sh(nom).Visible = xlSheetVisible
        Else
            ' xlSheetHidden et pas xlSheetVeryHidden : on doit pouvoir remontrer
            ' l'onglet a la main si les cases se perdent un jour.
            Sh(nom).Visible = xlSheetHidden
            caches = caches + 1
        End If
        On Error GoTo 0
    Next cb

    Application.StatusBar = caches & " onglet(s) masque(s)"
End Sub


' Le double-clic marche, mais un bouton se voit. Les deux menent au meme endroit.
Public Sub ReglerVoieSelectionnee()
    If ActiveSheet.name <> SH_ES Or ActiveCell.Row < FIRST_ROW Then
        Sh(SH_ES).Activate
        MsgBox "Selectionnez la ligne de la voie, puis relancez." & vbLf & vbLf & _
               "Un double-clic sur la ligne fait la meme chose.", vbExclamation
        Exit Sub
    End If
    OuvrirAssistantES ActiveCell.Row
End Sub
