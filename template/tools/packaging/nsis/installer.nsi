; NSIS (MUI2) installer, game-agnostic. All values come in as makensis -D defines from release.sh:
;   GAME_ID GAME_NAME DIR_NAME VERSION VI_VERSION PUBLISHER WIN_EXE_NAME SETUP_NAME EXE_SRC ICO_SRC ESTIMATED_SIZE_KB
; Per-machine install (admin), Start-menu + optional desktop shortcut, uninstaller in "Apps".
; The per-user save directory (AppData) is deliberately left alone by the uninstaller.
Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "x64.nsh"

Name "${GAME_NAME}"
OutFile "${SETUP_NAME}"
InstallDir "$PROGRAMFILES64\${DIR_NAME}"
InstallDirRegKey HKLM "Software\${GAME_ID}" "InstallDir"
BrandingText "${GAME_NAME} ${VERSION}"

VIProductVersion "${VI_VERSION}"
VIAddVersionKey "ProductName" "${GAME_NAME}"
VIAddVersionKey "FileDescription" "${GAME_NAME} installer"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "${PUBLISHER}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"

!define MUI_ICON "${ICO_SRC}"
!define MUI_UNICON "${ICO_SRC}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${WIN_EXE_NAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${GAME_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_ID}"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${GAME_NAME} needs a 64-bit version of Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "${GAME_NAME}" SecGame
  SectionIn RO
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  File "/oname=${WIN_EXE_NAME}" "${EXE_SRC}"
  File "/oname=${GAME_ID}.ico" "${ICO_SRC}"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${DIR_NAME}"
  CreateShortcut "$SMPROGRAMS\${DIR_NAME}\${GAME_NAME}.lnk" "$INSTDIR\${WIN_EXE_NAME}" "" "$INSTDIR\${WIN_EXE_NAME}" 0
  CreateShortcut "$SMPROGRAMS\${DIR_NAME}\Uninstall ${GAME_NAME}.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "Software\${GAME_ID}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${GAME_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${WIN_EXE_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" ${ESTIMATED_SIZE_KB}
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SecDesktop
  SetShellVarContext all
  CreateShortcut "$DESKTOP\${GAME_NAME}.lnk" "$INSTDIR\${WIN_EXE_NAME}" "" "$INSTDIR\${WIN_EXE_NAME}" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecGame} "The game and a Start-menu shortcut."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetShellVarContext all
  Delete "$DESKTOP\${GAME_NAME}.lnk"
  Delete "$SMPROGRAMS\${DIR_NAME}\${GAME_NAME}.lnk"
  Delete "$SMPROGRAMS\${DIR_NAME}\Uninstall ${GAME_NAME}.lnk"
  RMDir "$SMPROGRAMS\${DIR_NAME}"
  Delete "$INSTDIR\${WIN_EXE_NAME}"
  Delete "$INSTDIR\${GAME_ID}.ico"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${GAME_ID}"
  ; The per-user save directory (AppData) is never touched here.
SectionEnd
