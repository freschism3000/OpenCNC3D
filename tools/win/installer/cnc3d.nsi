; cnc3d.nsi -- the Windows install wizard for C&C 3D.
;
; Built from the Mac with makensis (brew install makensis), from the same staged
; folder tools/win/make-build-win.sh already produces, so the installer and the
; zip carry byte-identical contents and there is no second packaging path to keep
; in step. tools/win/make-installer-win.sh is the wrapper that passes the defines.
;
; IT INSTALLS PER USER, NOT INTO PROGRAM FILES, and that is the load-bearing
; decision in this file.
;
;   - The launcher UPDATES THE GAME IN PLACE: it downloads a zip and unpacks it
;     over the install. In Program Files that write is denied unless the launcher
;     runs elevated, so a Program Files install would mean a UAC prompt on every
;     update, or a launcher that asks for administrator rights every time it
;     starts. Neither is acceptable for a game.
;   - The game writes its settings and its log beside itself. Same problem, and
;     Windows would silently redirect those writes into VirtualStore, where the
;     next build would not find them.
;   - A per-user install needs no administrator rights at all, so the whole UAC
;     prompt disappears from the experience.
;
; $LOCALAPPDATA\Programs\<app> is where Windows itself puts per-user applications
; and where every other per-user installer goes. The directory page still lets
; anyone put it wherever they like.
;
; ON SMARTSCREEN. This installer is unsigned, so Windows will show "Windows
; protected your PC" the first time. That is an absence of reputation and not a
; detection; docs/windows-signing.md has the research and the costed options. The
; free half is done here: the finish page and the README both tell the player the
; one step that actually clears it (Unblock the download's Properties BEFORE
; extracting or running).

; ANSI, NOT UNICODE, AND IT IS NOT A PREFERENCE.
;
; The Homebrew makensis 3.12 bottle on this Mac crashes with std::bad_alloc while
; writing ANY unicode installer, including a three line one that installs nothing:
;
;   $ printf 'OutFile "m.exe"\nSection\nSectionEnd\n' > m.nsi && makensis m.nsi
;   Processed 1 file, writing output (x86-unicode):
;   libc++abi: terminating due to uncaught exception of type std::bad_alloc
;
; The same script with the ANSI target builds and runs. So this line is a
; workaround for a broken toolchain, not a decision about what the installer
; should be, and it has one real consequence that is registered in
; known-gap notes: an ANSI installer resolves paths through the system codepage,
; so a Windows account whose NAME contains characters outside that codepage would
; get a mangled install path. Everything ASCII, which is every path this
; installer chooses for itself, is unaffected.
;
; Flip this back to `Unicode true` the moment makensis can write one.
Unicode false

!ifndef PAYLOAD
  !error "PAYLOAD is not defined. Run tools/win/make-installer-win.sh, which passes it."
!endif
!ifndef VERSION
  !error "VERSION is not defined. Run tools/win/make-installer-win.sh."
!endif
!ifndef OUTFILE
  !error "OUTFILE is not defined. Run tools/win/make-installer-win.sh."
!endif

!define APPNAME    "C&C 3D"
!define COMPANY    "Slipgate Ironworks"
!define REGKEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\CNC3D"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\CNC3D"
; A reinstall or an upgrade finds the previous folder rather than proposing the
; default again, so a player who moved it once does not have to move it twice.
InstallDirRegKey HKCU "Software\CNC3D" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APPNAME} ${VERSION}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define MUI_ICON   "${ICONFILE}"
!define MUI_UNICON "${ICONFILE}"
!define MUI_ABORTWARNING

; The finish page offers to start the launcher, which is the thing this whole
; installer exists to put on the machine.
!define MUI_FINISHPAGE_RUN "$INSTDIR\C&C3D.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Play C&C 3D"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\READ-ME-WINDOWS.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Read the notes for this build"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
; The README, shown as an information page rather than as terms to accept:
; MUI_LICENSEPAGE_BUTTON and _TEXT_TOP turn "I Agree" into "Next", because
; nobody is agreeing to anything and a fake EULA is a lie in a dialog box.
!define MUI_LICENSEPAGE_BUTTON "Next >"
!define MUI_LICENSEPAGE_TEXT_TOP "What this build is, and how to play it."
!define MUI_LICENSEPAGE_TEXT_BOTTOM " "
!insertmacro MUI_PAGE_LICENSE "${INFOFILE}"
; The components page is not decoration: the desktop shortcut is optional, and
; without this page the MUI_DESCRIPTION_TEXT block below has nowhere to draw
; itself. makensis says so, seven times, which is how it was noticed.
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------- install

Section "C&C 3D" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; The whole staged folder, recursively. It is the same tree the zip is made
  ; from, so anything the zip carries the installer carries.
  File /r "${PAYLOAD}/*"

  ; The Start Menu entry points at the LAUNCHER, because that is the thing to
  ; double-click: it shows which build is installed, offers the update, and
  ; starts the game. cnc3d.exe beside it is the engine and is not a shortcut
  ; anybody should be given.
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\C&C3D.exe" \
                 "" "$INSTDIR\C&C3D.exe" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\uninstall.exe"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\CNC3D" "InstallDir" "$INSTDIR"

  ; Add/Remove Programs. EstimatedSize is read back off the folder rather than
  ; guessed, so the entry does not claim 0 KB for a 500 MB install.
  WriteRegStr HKCU "${REGKEY}" "DisplayName"     "${APPNAME}"
  WriteRegStr HKCU "${REGKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKCU "${REGKEY}" "Publisher"       "${COMPANY}"
  WriteRegStr HKCU "${REGKEY}" "DisplayIcon"     "$INSTDIR\C&C3D.exe"
  WriteRegStr HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${REGKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\C&C3D.exe" "" "$INSTDIR\C&C3D.exe" 0
SectionEnd

LangString DESC_SecMain    ${LANG_ENGLISH} \
  "The game, its data, and the launcher that keeps it up to date."
LangString DESC_SecDesktop ${LANG_ENGLISH} \
  "Put a C&C 3D shortcut on the desktop."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}    $(DESC_SecMain)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------- uninstall

Section "Uninstall"
  ; RMDir /r ON A USER-CHOSEN PATH IS A LOADED GUN, so it is only ever fired at a
  ; folder this installer wrote its own marker into. Without that check, an
  ; $INSTDIR that had been edited to C:\ would delete C:\.
  IfFileExists "$INSTDIR\C&C3D.exe" +3 0
    MessageBox MB_ICONSTOP "That does not look like a C&C 3D install, so nothing was removed."
    Abort

  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir  "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"

  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "${REGKEY}"
  DeleteRegKey HKCU "Software\CNC3D"
SectionEnd
