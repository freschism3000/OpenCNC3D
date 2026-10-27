; The Complete installers only: native per-game optional media downloads.
Function MediaChoice
  ${If} ${SectionIsSelected} ${SecMovies}
  ${AndIf} ${SectionIsSelected} ${SecMusic}
    StrCpy $MediaArg "movies,music"
    StrCpy $MediaWords "the movies and the music"
    StrCpy $MediaWith ", with the movies and the music"
  ${ElseIf} ${SectionIsSelected} ${SecMovies}
    StrCpy $MediaArg "movies"
    StrCpy $MediaWords "the movies"
    StrCpy $MediaWith ", with the movies"
  ${ElseIf} ${SectionIsSelected} ${SecMusic}
    StrCpy $MediaArg "music"
    StrCpy $MediaWords "the music"
    StrCpy $MediaWith ", with the music"
  ${Else}
    StrCpy $MediaArg "none"
    StrCpy $MediaWords ""
    StrCpy $MediaWith ""
  ${EndIf}
FunctionEnd
Function .onInit
  InitPluginsDir
  StrCpy $DlProc 0
  StrCpy $DlRunning 0
  StrCpy $DlStopping -1
  StrCpy $Playable 1
  StrCpy $FinishText "${GAME_TITLE} is installed."
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/NOMOVIES" $R1
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SecMovies}
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/NOMUSIC" $R1
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SecMusic}
  ${EndIf}
FunctionEnd
Function OnSetupAbort
  ; Optional-media cancellation keeps the already verified game and player files.
  Call DlStopLauncher
FunctionEnd
Function WriteEstimatedSize
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${REGKEY}" "EstimatedSize" "$0"
FunctionEnd

Function DlStopLauncher
  ${If} $DlProc == 0
  ${OrIf} $DlProc == ""
    Return
  ${EndIf}
  FileOpen $0 "$PLUGINSDIR\c.txt" w
  FileWrite $0 "stop$\r$\n"
  FileClose $0
  ${If} $DlRunning == 1
    ${NSD_SetText} $DlNote "Stopping the download. This can take up to a minute."
    System::Call 'user32::UpdateWindow(i $DlNote) i .r0'
  ${EndIf}
  System::Call 'kernel32::WaitForSingleObject(i $DlProc, i ${DL_STOP_MS}) i .r0'
  ${If} $0 != 0
    System::Call 'kernel32::TerminateProcess(i $DlProc, i 2) i .r0'
    System::Call 'kernel32::WaitForSingleObject(i $DlProc, i 5000) i .r0'
  ${EndIf}
  System::Call 'kernel32::CloseHandle(i $DlProc) i .r0'
  StrCpy $DlProc 0
  StrCpy $DlRunning 0
FunctionEnd

Function DlStart
  Delete "$PLUGINSDIR\p.txt"
  Delete "$PLUGINSDIR\c.txt"
  StrCpy $DlQuiet 0
  StrCpy $DlLastBeat ""
  StrCpy $DlPctShown 0
  StrCpy $DlItemShown ""
  StrCpy $DlTextShown ""
  StrCpy $DlRState ""
  StrCpy $DlRReason ""
  StrCpy $DlExit ""
  StrCpy $DlStopping -1
  SendMessage $DlBar ${PBM_SETPOS} 0 0
  ${NSD_SetText} $DlItem "Starting the download..."
  ${NSD_SetText} $DlSize ""
  ${NSD_SetText} $DlNote ""

  StrCpy $0 '"$INSTDIR\${LAUNCHER}" --dir "$INSTDIR" --media-install $MediaArg --media-game ${MEDIA_GAME} --media-add-only --progress-file "$PLUGINSDIR\p.txt" --cancel-file "$PLUGINSDIR\c.txt"'
  ; STARTUPINFOA is 68 bytes on 32-bit Windows: its own size, then sixteen zeroed fields.
  System::Call '*(i 68, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0) i .r1'
  ; PROCESS_INFORMATION: the process and thread handles, then their ids.
  System::Call '*(i 0, i 0, i 0, i 0) i .r2'
  System::Call 'kernel32::CreateProcess(i 0, t r0, i 0, i 0, i 0, i 0, i 0, t "$INSTDIR", i r1, i r2) i .r3 ?e'
  Pop $4
  ${If} $3 == 0
    StrCpy $DlProc 0
    StrCpy $DlExit "nostart"
    StrCpy $DlRReason "Setup could not start ${LAUNCHER} (Windows error $4)."
  ${Else}
    System::Call '*$2(i .r5, i .r6, i, i)'
    System::Call 'kernel32::CloseHandle(i r6) i .r7'
    StrCpy $DlProc $5
    StrCpy $DlRunning 1
  ${EndIf}
  System::Free $1
  System::Free $2

  ShowWindow $DlRetry ${SW_HIDE}
  ShowWindow $DlStop ${SW_SHOW}
  EnableWindow $DlStop 1
  GetDlgItem $0 $HWNDPARENT 1
  EnableWindow $0 0
  ${If} $DlExit == "nostart"
    Call DlEnded
  ${EndIf}
FunctionEnd

; One read of the progress file into the DlR* variables, kept only when it is complete.
; The file is opened with every sharing flag, delete included, because the launcher
; replaces it by renaming a new copy over it, and a reader holding it without
; FILE_SHARE_DELETE (which is how FileOpen opens a file) would make that rename fail for
; as long as the read lasts. The handle is a plain Win32 handle, which FileRead accepts.
Function DlRead
  StrCpy $DlOk 0
  System::Call 'kernel32::CreateFile(t "$PLUGINSDIR\p.txt", i 0x80000000, i 7, i 0, i 3, i 0x80, i 0) i .r0'
  ${If} $0 == -1
  ${OrIf} $0 == 0
    Return
  ${EndIf}
  StrCpy $R1 ""
  StrCpy $R2 ""
  StrCpy $R3 ""
  StrCpy $R4 ""
  StrCpy $R5 ""
  StrCpy $R6 ""
  StrCpy $R9 0
  ${Do}
    ClearErrors
    FileRead $0 $R7
    ${IfThen} ${Errors} ${|} ${ExitDo} ${|}
    ${TrimNewLines} "$R7" $R7
    ${If} $R7 == ""
      ${Continue}
    ${EndIf}
    ; "end" counts only as the last line: anything after it means the read is not one
    ; whole write.
    ${If} $R7 == "end"
      StrCpy $R9 1
      ${Continue}
    ${EndIf}
    StrCpy $R9 0
    StrCpy $R8 $R7 6
    ${If} $R8 == "state="
      StrCpy $R1 $R7 "" 6
    ${EndIf}
    StrCpy $R8 $R7 4
    ${If} $R8 == "pct="
      StrCpy $R2 $R7 "" 4
    ${EndIf}
    StrCpy $R8 $R7 5
    ${If} $R8 == "text="
      StrCpy $R3 $R7 "" 5
    ${ElseIf} $R8 == "item="
      StrCpy $R4 $R7 "" 5
    ${ElseIf} $R8 == "beat="
      StrCpy $R5 $R7 "" 5
    ${EndIf}
    StrCpy $R8 $R7 7
    ${If} $R8 == "reason="
      StrCpy $R6 $R7 "" 7
    ${EndIf}
  ${Loop}
  FileClose $0
  ${If} $R9 == 1
    StrCpy $DlRState $R1
    StrCpy $DlRPct $R2
    StrCpy $DlRText $R3
    StrCpy $DlRItem $R4
    StrCpy $DlRBeat $R5
    StrCpy $DlRReason $R6
    StrCpy $DlOk 1
  ${EndIf}
FunctionEnd

; The last complete read, onto the page. Only what changed is set, so the labels do not
; flicker four times a second, and the bar never moves backwards within one run.
Function DlShow
  IntOp $0 $DlRPct + 0
  ${If} $0 < 0
    StrCpy $0 0
  ${ElseIf} $0 > 100
    StrCpy $0 100
  ${EndIf}
  ${If} $0 > $DlPctShown
    StrCpy $DlPctShown $0
    SendMessage $DlBar ${PBM_SETPOS} $0 0
  ${EndIf}
  ${If} $DlRItem != ""
  ${AndIf} $DlRItem != $DlItemShown
    StrCpy $DlItemShown $DlRItem
    ${NSD_SetText} $DlItem $DlRItem
  ${EndIf}
  ${If} $DlRText != $DlTextShown
    StrCpy $DlTextShown $DlRText
    ${NSD_SetText} $DlSize $DlRText
  ${EndIf}
FunctionEnd

Function DlTick
  ${If} $DlRunning != 1
    Return
  ${EndIf}
  Call DlRead
  ${If} $DlOk == 1
    Call DlShow
    ${If} $DlRBeat != $DlLastBeat
      StrCpy $DlLastBeat $DlRBeat
      StrCpy $DlQuiet 0
    ${Else}
      IntOp $DlQuiet $DlQuiet + 1
    ${EndIf}
  ${Else}
    IntOp $DlQuiet $DlQuiet + 1
  ${EndIf}

  ; The launcher's own end decides: its exit code is the verdict, and its last write
  ; supplies the words. 259 is STILL_ACTIVE.
  System::Call 'kernel32::GetExitCodeProcess(i $DlProc, *i .r0) i .r1'
  ${If} $1 != 0
  ${AndIf} $0 != 259
    StrCpy $DlExit $0
    Call DlRead
    ${If} $DlOk == 1
      Call DlShow
    ${EndIf}
    Call DlEnded
    Return
  ${EndIf}

  ; STOPPED, it is given DL_STOP_TICKS to end by itself, beat or no beat, because a network
  ; read can hold it for the 30 seconds of its receive timeout before it sees the cancel
  ; file (the note at DL_STOP_TICKS). Only then is it ended, and what it kept is resumed.
  ${If} $DlStopping >= 0
    IntOp $DlStopping $DlStopping + 1
    ${If} $DlStopping >= ${DL_STOP_TICKS}
      System::Call 'kernel32::TerminateProcess(i $DlProc, i 2) i .r0'
      System::Call 'kernel32::WaitForSingleObject(i $DlProc, i 5000) i .r0'
      StrCpy $DlExit 2
      Call DlEnded
    ${EndIf}
    Return
  ${EndIf}

  ; A launcher that has written nothing new for thirty seconds has stopped working: the
  ; beat moves on every write, whether bytes are arriving or not.
  ${If} $DlQuiet >= ${DL_STALL_TICKS}
    System::Call 'kernel32::TerminateProcess(i $DlProc, i 1) i .r0'
    System::Call 'kernel32::WaitForSingleObject(i $DlProc, i 5000) i .r0'
    StrCpy $DlExit "stall"
    Call DlEnded
  ${EndIf}
FunctionEnd

; The run is over. Decide what it came to, and say so.
Function DlEnded
  StrCpy $DlRunning 0
  ${If} $DlProc != 0
    System::Call 'kernel32::CloseHandle(i $DlProc) i .r0'
    StrCpy $DlProc 0
  ${EndIf}
  ShowWindow $DlStop ${SW_HIDE}

  StrCpy $R0 "failed"
  ${If} $DlExit == 0
    StrCpy $R0 "done"
  ${ElseIf} $DlExit == 2
    StrCpy $R0 "cancelled"
  ${ElseIf} $DlExit == 3
    ${If} $DlRReason == ""
      StrCpy $DlRReason "Setup could not reach the download servers. Check the internet connection, then press Retry."
    ${EndIf}
  ${ElseIf} $DlExit == 1
    ${If} $DlRReason == ""
      StrCpy $DlRReason "The download failed."
    ${EndIf}
  ${ElseIf} $DlExit == "stall"
    ${If} $DlRState == "done"
      StrCpy $R0 "done"
    ${Else}
      StrCpy $DlRReason "The download stopped responding for thirty seconds."
    ${EndIf}
  ${ElseIf} $DlExit == "nostart"
    ; the reason was set where the start failed
  ${Else}
    IntFmt $1 "0x%08X" $DlExit
    StrCpy $DlRReason "${LAUNCHER} stopped before the download finished (exit code $1)."
  ${EndIf}

  StrCpy $Playable 1

  ${If} $R0 == "done"
    StrCpy $Playable 1
    SendMessage $DlBar ${PBM_SETPOS} 100 0
    ${NSD_SetText} $DlHeading "${GAME_TITLE} is installed$MediaWith."
    ${NSD_SetText} $DlNote "Press Next to finish."
    StrCpy $FinishText "${GAME_TITLE} is installed$MediaWith.$\r$\n$\r$\nThe launcher keeps the game up to date. Its gear button adds or removes the movies and the music."
    ShowWindow $DlRetry ${SW_HIDE}
  ${Else}
    ${If} $R0 == "cancelled"
      ${NSD_SetText} $DlHeading "The download is stopped."
      ${NSD_SetText} $DlRetry "&Resume"
      StrCpy $1 "What has arrived is kept."
      StrCpy $2 "Resume"
    ${Else}
      ${NSD_SetText} $DlHeading "The download did not finish."
      ${NSD_SetText} $DlRetry "&Retry"
      StrCpy $1 "$DlRReason"
      StrCpy $2 "Retry"
    ${EndIf}
    ${If} $Playable == 1
      ${NSD_SetText} $DlNote "$1$\r$\n$\r$\nOpenCNC3D can already be played from this folder. $2 carries on from where the download stopped. Next finishes without what is missing, which the launcher fetches later."
      StrCpy $FinishText "${GAME_TITLE} is installed, but not everything was downloaded.$\r$\n$\r$\nThe game is already installed. Its gear button adds the movies and the music later."
    ${Else}
      ${NSD_SetText} $DlNote "$1$\r$\n$\r$\nOpenCNC3D is not installed yet. $2 carries on from where the download stopped; Cancel leaves Setup and removes what it installed."
    ${EndIf}
    ShowWindow $DlRetry ${SW_SHOW}
    EnableWindow $DlRetry 1
  ${EndIf}

  ${If} $Playable == 1
    Call WriteEstimatedSize
    GetDlgItem $0 $HWNDPARENT 1
    EnableWindow $0 1
    ${If} $R0 == "done"
      SendMessage $HWNDPARENT ${WM_NEXTDLGCTL} $0 1
    ${EndIf}
  ${EndIf}
FunctionEnd

Function DlOnStop
  Pop $0
  ${If} $DlRunning == 1
    FileOpen $0 "$PLUGINSDIR\c.txt" w
    FileWrite $0 "stop$\r$\n"
    FileClose $0
    EnableWindow $DlStop 0
    StrCpy $DlStopping 0
    ${NSD_SetText} $DlNote "Stopping the download. What has arrived is kept, and Resume carries on from there."
  ${EndIf}
FunctionEnd

Function DlOnRetry
  Pop $0
  ${If} $DlRunning != 1
    ${NSD_SetText} $DlHeading "Setup is downloading $MediaWords."
    Call DlStart
  ${EndIf}
FunctionEnd

Function DlPageCreate
  Call MediaChoice
  ${If} $MediaArg == "none"
    Abort
  ${EndIf}
  !insertmacro MUI_HEADER_TEXT "Optional movies and music" "The launcher is downloading $MediaWords."

  nsDialogs::Create 1018
  Pop $DlDialog
  ${If} $DlDialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 18u "Setup is downloading $MediaWords."
  Pop $DlHeading
  CreateFont $DlHeadFont "$(^Font)" "$(^FontSize)" "700"
  SendMessage $DlHeading ${WM_SETFONT} $DlHeadFont 1

  ${NSD_CreateLabel} 0 22u 100% 10u ""
  Pop $DlItem
  ${NSD_CreateProgressBar} 0 34u 100% 12u ""
  Pop $DlBar
  ${NSD_CreateLabel} 0 50u 100% 10u ""
  Pop $DlSize
  ; Room for seven lines: a reason of two, and what the player can do next.
  ${NSD_CreateLabel} 0 66u 100% 54u ""
  Pop $DlNote

  ${NSD_CreateButton} 0 124u 64u 14u "&Stop"
  Pop $DlStop
  ${NSD_OnClick} $DlStop DlOnStop
  ${NSD_CreateButton} 70u 124u 64u 14u "&Retry"
  Pop $DlRetry
  ${NSD_OnClick} $DlRetry DlOnRetry
  ShowWindow $DlRetry ${SW_HIDE}

  ; No way back into the install, and no way on until there is a game to go on to.
  GetDlgItem $0 $HWNDPARENT 3
  EnableWindow $0 0
  GetDlgItem $0 $HWNDPARENT 1
  EnableWindow $0 0

  Call DlStart
  ${NSD_CreateTimer} DlTick ${DL_TICK_MS}
  nsDialogs::Show
FunctionEnd

Function DlPageLeave
  ${If} $Playable != 1
  ${OrIf} $DlRunning == 1
    Abort
  ${EndIf}
  ${NSD_KillTimer} DlTick
FunctionEnd
