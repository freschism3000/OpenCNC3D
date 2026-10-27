; OpenCNC3D Setup installs the empty launcher. Games and optional media are
; selected inside its Module Browser after installation.
; Empty installs perform no network calls; Complete variants embed one verified game
; and offer its movies/music inside this installer.
; Keep ANSI until the documented Homebrew Unicode writer crash is resolved.
Unicode false
!ifndef PAYLOAD
  !error "Run tools/win/make-installer-win.sh (PAYLOAD missing)."
!endif
!ifndef VERSION
  !error "VERSION missing."
!endif
!ifndef OUTFILE
  !error "OUTFILE missing."
!endif
!ifndef ICONFILE
  !error "ICONFILE missing."
!endif
!ifndef WIZARDBMP
  !error "WIZARDBMP missing."
!endif
!define APPNAME "OpenCNC3D"
!define COMPANY "Slipgate Ironworks"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenCNC3D"
!define LAUNCHER "OpenCNC3D.exe"
Name "${PACKAGE_TITLE}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\OpenCNC3D"
InstallDirRegKey HKCU "Software\OpenCNC3D" "InstallDir"
RequestExecutionLevel user
!ifdef MODULE
  SetCompressor lzma
!else
  SetCompressor /SOLID lzma
!endif
BrandingText "${APPNAME} Setup ${VERSION}"
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!ifdef MODULE
  !include "TextFunc.nsh"
  !include "Sections.nsh"
  !include "nsDialogs.nsh"
  !include "WinMessages.nsh"
  !include "media-state.nsh"
!endif
!define MUI_ICON "${ICONFILE}"
!define MUI_UNICON "${ICONFILE}"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${WIZARDBMP}"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${WIZARDBMP}"
!define MUI_ABORTWARNING
!ifdef MODULE
  !define MUI_CUSTOMFUNCTION_ABORT OnSetupAbort
  !define MUI_WELCOMEPAGE_TEXT "Setup installs ${GAME_TITLE} and the OpenCNC3D launcher.$\r$\n$\r$\nThe game is included. Choose optional movies and music on the next page. Existing saves and settings are preserved."
!else
!define MUI_WELCOMEPAGE_TEXT "Setup installs the OpenCNC3D launcher.$\r$\n$\r$\nAfter installation, open Add Game to choose Dawn3D or RA3D. Each game downloads separately. Movies and music are optional.$\r$\n$\r$\nNo game is downloaded by Setup. Nothing needs administrator rights.$\r$\n$\r$\nClick Next to continue."
!endif
!insertmacro MUI_PAGE_WELCOME
!ifdef MODULE
  !define MUI_COMPONENTSPAGE_TEXT_TOP "The game is included. Movies and music are optional downloads; choose either, both or neither."
  !insertmacro MUI_PAGE_COMPONENTS
!endif
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!ifdef MODULE
  Page custom DlPageCreate DlPageLeave
!endif
!ifdef MODULE
  !define MUI_FINISHPAGE_TEXT "$FinishText"
!else
!define MUI_FINISHPAGE_TEXT "The OpenCNC3D launcher is installed.$\r$\n$\r$\nOpen Add Game to download a game. Installed games, saves and settings from an existing installation are preserved."
!endif
!define MUI_FINISHPAGE_RUN "$INSTDIR\${LAUNCHER}"
!define MUI_FINISHPAGE_RUN_TEXT "Start OpenCNC3D"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\READ-ME-WINDOWS.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Read the notes for this build"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "${GAME_TITLE}" SecMain
!ifdef MODULE
  AddSize ${MODULE_KB}
!endif
  SectionIn RO
  Call InstDirIsRoot
  Pop $0
  ${If} $0 == 1
    MessageBox MB_ICONSTOP "Choose a folder below the root of the drive or network share." /SD IDOK
    SetErrorLevel 15
    Quit
  ${EndIf}
  ; An interrupted module/update transaction must recover with its own launcher.
  ; Do not discard its journal or silently overwrite files it may roll back.
  ${If} ${FileExists} "$INSTDIR\opencnc3d-update.journal"
  ${OrIf} ${FileExists} "$INSTDIR\opencnc3d-module-removal.txt"
    MessageBox MB_ICONSTOP "Open the existing launcher once to finish recovering its interrupted update, then run Setup again." /SD IDOK
    SetErrorLevel 16
    Quit
  ${EndIf}
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}/*"
!ifdef MODULE
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  SetCompress off
  File /oname=module.zip "${MODULE_ZIP}"
  SetCompress auto
  DetailPrint "Installing ${GAME_TITLE} from the included verified package..."
  ClearErrors
  ExecWait '"$INSTDIR\${LAUNCHER}" --dir "$INSTDIR" --install-module ${MODULE} --module-file "$PLUGINSDIR\module.zip" --module-sha ${MODULE_SHA}' $0
  ${If} ${Errors}
  ${OrIf} $0 != 0
    MessageBox MB_ICONSTOP "The included game could not be installed. Existing saves and settings were preserved." /SD IDOK
    SetErrorLevel 20
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
!endif
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${LAUNCHER}" "" "$INSTDIR\opencnc3d.ico" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${LAUNCHER}" "" "$INSTDIR\opencnc3d.ico" 0
  WriteRegStr HKCU "Software\OpenCNC3D" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${REGKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${REGKEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKCU "${REGKEY}" "DisplayIcon" "$INSTDIR\opencnc3d.ico"
  WriteRegStr HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${REGKEY}" "EstimatedSize" "$0"
  SetErrorLevel 0
SectionEnd

!ifdef MODULE
Section "Movies (optional download)" SecMovies
SectionEnd
Section "Music (optional download)" SecMusic
SectionEnd
Section "-optional media when silent" SecSilent
  ${If} ${Silent}
    Call MediaChoice
    ClearErrors
    ExecWait '"$INSTDIR\${LAUNCHER}" --dir "$INSTDIR" --media-install $MediaArg --media-game ${MEDIA_GAME} --media-add-only' $0
    ${If} ${Errors}
      SetErrorLevel 14
    ${ElseIf} $0 != 0
      IntOp $0 $0 + 10
      SetErrorLevel $0
    ${EndIf}
  ${EndIf}
SectionEnd
!include "media-page.nsh"
!endif

Function InstDirIsRoot
  Push $0
  Push $1
  Push $2
  StrCpy $0 "$INSTDIR"
  ${Do}
    StrCpy $1 "$0" "" -1
    ${If} $1 != "\"
    ${AndIf} $1 != "/"
      ${ExitDo}
    ${EndIf}
    StrCpy $0 "$0" -1
  ${Loop}
  ${GetRoot} "$0" $1
  StrCpy $2 "$0" 2
  ${If} $0 == ""
  ${OrIf} $0 == $1
    StrCpy $0 1
  ${ElseIf} $1 == ""
  ${AndIf} $2 == "\\"
    StrCpy $0 1
  ${Else}
    StrCpy $0 0
  ${EndIf}
  Pop $2
  Pop $1
  Exch $0
FunctionEnd

; The directory page's Next stays grey for a root.
Function .onVerifyInstDir
  Push $0
  Call InstDirIsRoot
  Pop $0
  ${If} $0 == 1
    Pop $0
    Abort
  ${EndIf}
  Pop $0
FunctionEnd


Section "Uninstall"
  IfFileExists "$INSTDIR\${LAUNCHER}" +3 0
    MessageBox MB_ICONSTOP "That does not look like an OpenCNC3D launcher installation."
    Abort
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"
  ; Remove only the launcher's own files. Modules, optional media, saves and
  ; settings are retained; no recursive removal of a user-selected directory.
  Delete "$INSTDIR\${LAUNCHER}"
  Delete "$INSTDIR\opencnc3d.ico"
  Delete "$INSTDIR\READ-ME-WINDOWS.txt"
  Delete "$INSTDIR\uninstall.exe"
  ; The SDL/art/licence files may also serve an installed game. Leave them
  ; when either native brain exists, otherwise remove the bootstrap's files.
  ${IfNot} ${FileExists} "$INSTDIR\TiberianDawn.dll"
  ${AndIfNot} ${FileExists} "$INSTDIR\ra3d\RedAlert.dll"
    Delete "$INSTDIR\SDL2.dll"
    Delete "$INSTDIR\dosmenu.pack"
    Delete "$INSTDIR\enhanced-menu.pack"
    Delete "$INSTDIR\ecur\ecur.bin"
    RMDir "$INSTDIR\ecur"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\NOTICE.md"
  ${EndIf}
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${REGKEY}"
  DeleteRegKey HKCU "Software\OpenCNC3D"
SectionEnd
