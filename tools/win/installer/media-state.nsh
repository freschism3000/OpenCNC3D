; Complete installer media worker state.
Var MediaArg        ; what --media is given: movies,music | movies | music | none
Var MediaWords      ; the same as a list, ", the movies and the music"
Var MediaWith       ; and as a result, ", with the movies and the music"
Var Installed       ; 1 once the files page has run
Var SetupMadeDir    ; 1 when the install folder was absent or empty before Setup
Var Registered      ; 1 once the shortcuts and the Add/Remove Programs entry exist
Var Playable        ; 1 when the folder holds a game, so Next may lead to the end
Var GameVersion     ; read back from the install record the download wrote
Var FinishText

Var DlDialog
Var DlHeading
Var DlHeadFont
Var DlItem
Var DlBar
Var DlSize
Var DlNote
Var DlStop
Var DlRetry
Var DlProc          ; the launcher's process handle while it runs, 0 when none
Var DlRunning
Var DlExit          ; its exit code, or "stall" or "nostart", once it has ended
Var DlQuiet         ; polls since the beat last moved
Var DlLastBeat
Var DlPctShown
Var DlItemShown
Var DlTextShown
Var DlOk            ; 1 when the last read of the progress file was complete
Var DlRState
Var DlRPct
Var DlRText
Var DlRItem
Var DlRBeat
Var DlRReason
Var DlStopping      ; polls since Stop was pressed, -1 while it has not been

!define DL_TICK_MS 250
!define DL_STALL_TICKS 120
!define DL_STOP_TICKS 180
!define DL_STOP_MS 45000
