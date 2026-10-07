; R-Wallet installer (NSIS 3).
; Built by tools/build-installer.sh:
;   makensis -DVERSION=1.0.0 -DDIST=<dist folder> -DOUTFILE=<exe>            all users (Program Files, admin)
;   makensis -DVERSION=1.0.0 -DDIST=<dist folder> -DOUTFILE=<exe> -DPERUSER  just me (no admin)
;
; Installs the app and its libraries, a Start-menu entry, an optional desktop
; shortcut, an Apps & Features entry and an uninstaller. Your wallets live in
; %APPDATA%\Ratrix and are never touched by installing, updating or removing.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
SetCompressorDictSize 64

!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include x64.nsh

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!define APP "R-Wallet"
!define EXE "RatrixWallet.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\R-Wallet"
!define SETTINGS_KEY "Software\Ratrix\Install"

!ifdef PERUSER
  RequestExecutionLevel user
  InstallDir "$LOCALAPPDATA\Programs\R-Wallet"
  !define ROOT HKCU
  !define MODE_TEXT "for you only (no administrator needed)"
!else
  RequestExecutionLevel admin
  InstallDir "$PROGRAMFILES64\R-Wallet"
  !define ROOT HKLM
  !define MODE_TEXT "for all users of this computer"
!endif

Name "R-Wallet"
OutFile "${OUTFILE}"
InstallDirRegKey ${ROOT} "${SETTINGS_KEY}" "InstallDir"
BrandingText "R-Wallet ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "R-Wallet"
VIAddVersionKey "FileDescription" "R-Wallet installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "Ratrix"
VIAddVersionKey "LegalCopyright" "Ratrix"

; ---------------------------------------------------------------- pages
!define MUI_ICON "ratrix.ico"
!define MUI_UNICON "ratrix.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "wizard.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "wizard.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC

!define MUI_WELCOMEPAGE_TITLE "Install R-Wallet ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "R-Wallet is a self-custody, multi-chain crypto wallet for Ethereum, BNB Smart Chain and Bitcoin.$\r$\n$\r$\nR-Wallet will be installed ${MODE_TEXT}.$\r$\n$\r$\nYour wallets are encrypted and stored in %APPDATA%\Ratrix. Installing, updating or uninstalling never touches them."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start R-Wallet"
!define MUI_FINISHPAGE_TEXT "R-Wallet is installed.$\r$\n$\r$\nYour encrypted wallets live in %APPDATA%\Ratrix. Back up each wallet's recovery phrase or private key offline; without it, a forgotten passphrase cannot be recovered."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------- helpers
!macro SetContext
  !ifdef PERUSER
    SetShellVarContext current
  !else
    SetShellVarContext all
  !endif
!macroend

; A running .exe can be renamed but not opened for writing; test the latter and
; ask the user to close R-Wallet before its files are replaced or removed.
!macro EnsureClosed
  retry_closed:
  ${If} ${FileExists} "$INSTDIR\${EXE}"
    ClearErrors
    FileOpen $9 "$INSTDIR\${EXE}" a
    ${If} ${Errors}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "R-Wallet is still running. Please close it, then press Retry." /SD IDCANCEL IDRETRY retry_closed
      Abort
    ${EndIf}
    FileClose $9
  ${EndIf}
!macroend

; ---------------------------------------------------------------- sections
Section "R-Wallet (required)" SecCore
  SectionIn RO
  !insertmacro SetContext
  !insertmacro EnsureClosed
  SetOutPath "$INSTDIR"
  File /r "${DIST}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateShortcut "$SMPROGRAMS\R-Wallet.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0 SW_SHOWNORMAL "" "Multi-chain crypto wallet"
  WriteRegStr ${ROOT} "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}" "" "$INSTDIR\${EXE}"
  WriteRegStr ${ROOT} "${SETTINGS_KEY}" "InstallDir" "$INSTDIR"

  ; Apps & features entry
  WriteRegStr ${ROOT} "${UNINST_KEY}" "DisplayName" "R-Wallet"
  WriteRegStr ${ROOT} "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr ${ROOT} "${UNINST_KEY}" "Publisher" "Ratrix"
  WriteRegStr ${ROOT} "${UNINST_KEY}" "DisplayIcon" "$\"$INSTDIR\${EXE}$\",0"
  WriteRegStr ${ROOT} "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr ${ROOT} "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr ${ROOT} "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD ${ROOT} "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD ${ROOT} "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD ${ROOT} "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\R-Wallet.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

Section "-Finish"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "The R-Wallet application and the libraries it needs."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Add an R-Wallet shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; The install folder must end in \R-Wallet so the uninstaller can never remove a shared folder.
Function .onVerifyInstDir
  ${GetFileName} "$INSTDIR" $0
  ${If} $0 != "R-Wallet"
    Abort
  ${EndIf}
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "R-Wallet needs 64-bit Windows 10 or 11." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64
  !insertmacro SetContext
FunctionEnd

; ---------------------------------------------------------------- uninstall
Section "Uninstall"
  !insertmacro SetContext
  !insertmacro EnsureClosed

  Delete "$SMPROGRAMS\R-Wallet.lnk"
  Delete "$DESKTOP\R-Wallet.lnk"
  DeleteRegKey ${ROOT} "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}"

  RMDir /r "$INSTDIR"

  DeleteRegKey ${ROOT} "${UNINST_KEY}"
  DeleteRegKey ${ROOT} "${SETTINGS_KEY}"
  DeleteRegKey /ifempty ${ROOT} "Software\Ratrix"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Function un.onInit
  SetRegView 64
  !insertmacro SetContext
FunctionEnd
