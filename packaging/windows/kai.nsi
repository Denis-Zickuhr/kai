; kai.nsi — Instalador do Kai para Windows (NSIS 3 + Modern UI 2), gerado com
; makensis no Linux (Docker). É o instalador OFICIAL do release (kai.iss é só
; a rota legada do build nativo no Windows).
; Uso: makensis -DKAISRC=<pasta portável> -DKAIOUT=<saída> kai.nsi
;
; Os bitmaps (welcome.bmp / header.bmp) vêm de gen-installer-art.py.

!ifndef KAISRC
  !define KAISRC "dist\kai-windows"
!endif
!ifndef KAIOUT
  !define KAIOUT "dist"
!endif

!define APPNAME "Kai"
!define APPVERSION "1.0.1"
!define PUBLISHER "Kai"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "StrFunc.nsh"
!include "WinMessages.nsh"
!include "nsDialogs.nsh"

Unicode true
; Compressão zlib (em vez de /SOLID lzma): o stub resultante é mais simples
; e universalmente compatível.
SetCompressor /FINAL zlib
; CRCCheck off: o self-check de integridade do NSIS FALHA ("Installer
; integrity check has failed") quando o instalador é executado a partir do
; filesystem de REDE do WSL (\\wsl.localhost\... via 9P). A integridade real
; é garantida na geração (7z test).
CRCCheck off
ManifestDPIAware true

; ${StrStr}/${UnStrStr}/${UnStrRep} (StrFunc.nsh) SÓ podem ser declarados
; DEPOIS de Unicode/SetCompressor/CRCCheck (senão: "Can't change target
; charset after data already got compressed").
${StrStr}
${UnStrStr}
${UnStrRep}

Name "${APPNAME} ${APPVERSION}"
OutFile "${KAIOUT}\kai-setup.exe"
BrandingText "${APPNAME} ${APPVERSION}"

; INFORMAÇÃO DE VERSÃO no próprio instalador (reduz falso positivo do Defender).
VIProductVersion "1.0.1.0"
VIAddVersionKey /LANG=1033 "ProductName"     "${APPNAME}"
VIAddVersionKey /LANG=1033 "ProductVersion"  "1.0.1.0"
VIAddVersionKey /LANG=1033 "FileVersion"     "1.0.1.0"
VIAddVersionKey /LANG=1033 "FileDescription" "Kai Setup - developer command runner"
VIAddVersionKey /LANG=1033 "CompanyName"     "Kai"
VIAddVersionKey /LANG=1033 "LegalCopyright"  "Copyright (C) 2026"
VIAddVersionKey /LANG=1033 "OriginalFilename" "kai-setup.exe"
VIAddVersionKey /LANG=1046 "ProductName"     "${APPNAME}"
VIAddVersionKey /LANG=1046 "ProductVersion"  "1.0.1.0"
VIAddVersionKey /LANG=1046 "FileVersion"     "1.0.1.0"
VIAddVersionKey /LANG=1046 "FileDescription" "Instalador do Kai - executor de comandos"
VIAddVersionKey /LANG=1046 "CompanyName"     "Kai"
VIAddVersionKey /LANG=1046 "LegalCopyright"  "Copyright (C) 2026"
VIAddVersionKey /LANG=1046 "OriginalFilename" "kai-setup.exe"

; Instalação POR USUÁRIO (sem admin): %LocalAppData%\Programs\Kai, HKCU,
; atalhos no perfil. Uma instalação antiga (em %LocalAppData%\Kai) é mantida
; no mesmo lugar via InstallDirRegKey.
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"

; ---------------------------------------------------------------- estado ---
Var FreshCfg    ; 1 = ainda não existe settings.json (instalação nova)
Var ThemeIdx
Var LangIdx
Var OptAuto
Var OptMin
Var OptDesk
Var OptPath
Var hTheme
Var hLang
Var hAuto
Var hMin
Var hDesk
Var hPath

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
!define RUN_KEY    "Software\Microsoft\Windows\CurrentVersion\Run"

; ---------------------------------------------------------------- visual ----
!define MUI_ICON   "installer.ico"
!define MUI_UNICON "installer.ico"
!define MUI_ABORTWARNING
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"

; Idioma do instalador: o seletor abre pré-selecionando o idioma do sistema e
; a escolha fica lembrada (o desinstalador usa a mesma).
!define MUI_LANGDLL_REGISTRY_ROOT "HKCU"
!define MUI_LANGDLL_REGISTRY_KEY "Software\${APPNAME}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"

; ---------------------------------------------------------------- textos ----
; Os LangStrings vêm ANTES das páginas que os usam; os valores só são
; resolvidos depois de MUI_LANGUAGE, então ficam logo abaixo dela.
!define MUI_WELCOMEPAGE_TEXT "$(WelcomeText)"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "$(RunText)"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchKai
!define MUI_UNWELCOMEPAGE_TEXT "$(UnWelcomeText)"

; ---------------------------------------------------------------- páginas ---
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom PrefsCreate PrefsLeave
Page custom OptionsCreate OptionsLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "PortugueseBR"

LangString WelcomeText    ${LANG_ENGLISH}      "Kai is a light command runner and HTTP client that lives in the tray."
LangString WelcomeText    ${LANG_PORTUGUESEBR} "O Kai é um executor de comandos e cliente HTTP leve que vive na bandeja."
LangString UnWelcomeText  ${LANG_ENGLISH}      "Kai will be removed from this computer."
LangString UnWelcomeText  ${LANG_PORTUGUESEBR} "O Kai será removido deste computador."
LangString RunText        ${LANG_ENGLISH}      "Open Kai"
LangString RunText        ${LANG_PORTUGUESEBR} "Abrir o Kai"

LangString PrefsTitle     ${LANG_ENGLISH}      "Preferences"
LangString PrefsTitle     ${LANG_PORTUGUESEBR} "Preferências"
LangString PrefsSub       ${LANG_ENGLISH}      "How Kai looks and starts."
LangString PrefsSub       ${LANG_PORTUGUESEBR} "Aparência e inicialização do Kai."
LangString LblTheme       ${LANG_ENGLISH}      "THEME"
LangString LblTheme       ${LANG_PORTUGUESEBR} "TEMA"
LangString LblLang        ${LANG_ENGLISH}      "LANGUAGE"
LangString LblLang        ${LANG_PORTUGUESEBR} "IDIOMA"
LangString ChkAuto        ${LANG_ENGLISH}      "Start with Windows"
LangString ChkAuto        ${LANG_PORTUGUESEBR} "Iniciar com o Windows"
LangString ChkMin         ${LANG_ENGLISH}      "Start minimized in the tray"
LangString ChkMin         ${LANG_PORTUGUESEBR} "Iniciar minimizado na bandeja"

LangString OptsTitle      ${LANG_ENGLISH}      "Options"
LangString OptsTitle      ${LANG_PORTUGUESEBR} "Opções"
LangString OptsSub        ${LANG_ENGLISH}      "Shortcuts and terminal."
LangString OptsSub        ${LANG_PORTUGUESEBR} "Atalhos e terminal."
LangString ChkDesk        ${LANG_ENGLISH}      "Desktop shortcut"
LangString ChkDesk        ${LANG_PORTUGUESEBR} "Atalho na área de trabalho"
LangString ChkPath        ${LANG_ENGLISH}      "Use kai in the terminal (adds to PATH)"
LangString ChkPath        ${LANG_PORTUGUESEBR} "Usar o kai no terminal (adiciona ao PATH)"

LangString AskSame        ${LANG_ENGLISH}      "Kai ${APPVERSION} is already installed in:$\r$\n$INSTDIR$\r$\n$\r$\nReinstall over it?"
LangString AskSame        ${LANG_PORTUGUESEBR} "O Kai ${APPVERSION} já está instalado em:$\r$\n$INSTDIR$\r$\n$\r$\nReinstalar por cima?"
LangString AskUpdate      ${LANG_ENGLISH}      "Kai $1 is installed in:$\r$\n$INSTDIR$\r$\n$\r$\nUpdate to ${APPVERSION}?"
LangString AskUpdate      ${LANG_PORTUGUESEBR} "O Kai $1 está instalado em:$\r$\n$INSTDIR$\r$\n$\r$\nAtualizar para a ${APPVERSION}?"
LangString AskPurge       ${LANG_ENGLISH}      "Also delete your Kai settings and data?"
LangString AskPurge       ${LANG_PORTUGUESEBR} "Apagar também as configurações e os dados do Kai?"

; ----------------------------------------------------------------- init -----
Function .onInit
    ; Opções padrão (valem também na instalação silenciosa /S).
    StrCpy $ThemeIdx 0
    StrCpy $OptAuto 0
    StrCpy $OptMin 0
    StrCpy $OptDesk 1
    StrCpy $OptPath 1
    StrCpy $FreshCfg 1
    ${If} ${FileExists} "$LOCALAPPDATA\kai\settings.json"
    ${OrIf} ${FileExists} "$APPDATA\kai\settings.json"
        StrCpy $FreshCfg 0
    ${EndIf}

    !insertmacro MUI_LANGDLL_DISPLAY
    ${If} $LANGUAGE == ${LANG_PORTUGUESEBR}
        StrCpy $LangIdx 1
    ${Else}
        StrCpy $LangIdx 0
    ${EndIf}

    ; Instalação existente: mantém a pasta, pede confirmação e preserva as
    ; escolhas anteriores (atalho, PATH).
    ReadRegStr $0 HKCU "Software\${APPNAME}" "InstallDir"
    ${If} $0 != ""
        StrCpy $INSTDIR $0
        ReadRegStr $1 HKCU "${UNINST_KEY}" "DisplayVersion"
        ${If} $1 == "${APPVERSION}"
            MessageBox MB_YESNO|MB_ICONQUESTION "$(AskSame)" /SD IDYES IDYES +2
            Quit
        ${Else}
            MessageBox MB_YESNO|MB_ICONQUESTION "$(AskUpdate)" /SD IDYES IDYES +2
            Quit
        ${EndIf}
        ${IfNot} ${FileExists} "$DESKTOP\${APPNAME}.lnk"
            StrCpy $OptDesk 0
        ${EndIf}
        ReadRegStr $2 HKCU "Environment" "PATH"
        ${StrStr} $3 ";$2;" ";$INSTDIR;"
        ${If} $3 == ""
            StrCpy $OptPath 0
        ${EndIf}
    ${EndIf}
FunctionEnd

Function un.onInit
    !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Function LaunchKai
    Exec '"$INSTDIR\kai.exe"'
FunctionEnd

; ------------------------------------------------- página: preferências -----
; Só aparece em instalação NOVA: numa atualização o settings.json do usuário
; é dele e nunca é tocado.
Function PrefsCreate
    ${If} $FreshCfg == 0
        Abort
    ${EndIf}
    !insertmacro MUI_HEADER_TEXT "$(PrefsTitle)" "$(PrefsSub)"
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 9u "$(LblTheme)"
    Pop $0
    ${NSD_CreateDropList} 0 10u 100% 90u ""
    Pop $hTheme
    ${NSD_CB_AddString} $hTheme "Kai Dark"
    ${NSD_CB_AddString} $hTheme "Kai Light"
    ${NSD_CB_AddString} $hTheme "Kai Liquid"
    ${NSD_CB_AddString} $hTheme "Kai Material"
    ${NSD_CB_AddString} $hTheme "Dracula"
    ${NSD_CB_AddString} $hTheme "Light"
    ${NSD_CB_AddString} $hTheme "Xmas Dark"
    ${NSD_CB_AddString} $hTheme "Xmas Light"
    SendMessage $hTheme ${CB_SETCURSEL} $ThemeIdx 0

    ${NSD_CreateLabel} 0 34u 100% 9u "$(LblLang)"
    Pop $0
    ${NSD_CreateDropList} 0 44u 100% 40u ""
    Pop $hLang
    ${NSD_CB_AddString} $hLang "English"
    ${NSD_CB_AddString} $hLang "Português (Brasil)"
    SendMessage $hLang ${CB_SETCURSEL} $LangIdx 0

    ${NSD_CreateCheckbox} 0 72u 100% 10u "$(ChkAuto)"
    Pop $hAuto
    ${If} $OptAuto == 1
        ${NSD_Check} $hAuto
    ${EndIf}
    ${NSD_CreateCheckbox} 0 86u 100% 10u "$(ChkMin)"
    Pop $hMin
    ${If} $OptMin == 1
        ${NSD_Check} $hMin
    ${EndIf}
    nsDialogs::Show
FunctionEnd

Function PrefsLeave
    SendMessage $hTheme ${CB_GETCURSEL} 0 0 $ThemeIdx
    SendMessage $hLang ${CB_GETCURSEL} 0 0 $LangIdx
    ${NSD_GetState} $hAuto $OptAuto
    ${NSD_GetState} $hMin $OptMin
    ; BST_CHECKED = 1; qualquer outro estado vira 0.
    ${If} $OptAuto != 1
        StrCpy $OptAuto 0
    ${EndIf}
    ${If} $OptMin != 1
        StrCpy $OptMin 0
    ${EndIf}
FunctionEnd

; ------------------------------------------------------ página: opções ------
Function OptionsCreate
    !insertmacro MUI_HEADER_TEXT "$(OptsTitle)" "$(OptsSub)"
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}
    ${NSD_CreateCheckbox} 0 4u 100% 10u "$(ChkDesk)"
    Pop $hDesk
    ${If} $OptDesk == 1
        ${NSD_Check} $hDesk
    ${EndIf}
    ${NSD_CreateCheckbox} 0 20u 100% 10u "$(ChkPath)"
    Pop $hPath
    ${If} $OptPath == 1
        ${NSD_Check} $hPath
    ${EndIf}
    nsDialogs::Show
FunctionEnd

Function OptionsLeave
    ${NSD_GetState} $hDesk $OptDesk
    ${NSD_GetState} $hPath $OptPath
    ${If} $OptDesk != 1
        StrCpy $OptDesk 0
    ${EndIf}
    ${If} $OptPath != 1
        StrCpy $OptPath 0
    ${EndIf}
FunctionEnd

; --------------------------------------------------------------- instalar ---
; Grava o settings.json inicial com SÓ chaves que o Kai lê (active_theme,
; language, start_visible, autostart). Só roda se o arquivo não existe.
Function WriteInitialSettings
    ; Índices da combo de tema -> nome do tema em assets/themes.
    ${Switch} $ThemeIdx
        ${Case} 1
            StrCpy $R1 "kai-light"
            ${Break}
        ${Case} 2
            StrCpy $R1 "kai-liquid"
            ${Break}
        ${Case} 3
            StrCpy $R1 "kai-material"
            ${Break}
        ${Case} 4
            StrCpy $R1 "dracula"
            ${Break}
        ${Case} 5
            StrCpy $R1 "light"
            ${Break}
        ${Case} 6
            StrCpy $R1 "xmas-dark"
            ${Break}
        ${Case} 7
            StrCpy $R1 "xmas-light"
            ${Break}
        ${Default}
            StrCpy $R1 "kai-dark"
    ${EndSwitch}
    ${If} $LangIdx == 1
        StrCpy $R2 "pt"
    ${Else}
        StrCpy $R2 "en"
    ${EndIf}
    ${If} $OptMin == 1
        StrCpy $R3 "false"
    ${Else}
        StrCpy $R3 "true"
    ${EndIf}
    ${If} $OptAuto == 1
        StrCpy $R4 "true"
    ${Else}
        StrCpy $R4 "false"
    ${EndIf}

    CreateDirectory "$LOCALAPPDATA\kai"
    ClearErrors
    FileOpen $R0 "$LOCALAPPDATA\kai\settings.json" w
    ${IfNot} ${Errors}
        FileWrite $R0 "{$\r$\n"
        FileWrite $R0 '  "active_theme": "$R1",$\r$\n'
        FileWrite $R0 '  "language": "$R2",$\r$\n'
        FileWrite $R0 '  "start_visible": $R3,$\r$\n'
        FileWrite $R0 '  "autostart": $R4$\r$\n'
        FileWrite $R0 "}$\r$\n"
        FileClose $R0
    ${EndIf}

    ; O Kai também reconcilia essa chave na inicialização; gravá-la aqui só
    ; faz o "iniciar com o Windows" valer já no próximo logon.
    ${If} $OptAuto == 1
        WriteRegStr HKCU "${RUN_KEY}" "${APPNAME}" '"$INSTDIR\kai.exe"'
    ${EndIf}
FunctionEnd

Section "Install"
    ; Encerra qualquer instância do Kai já rodando ANTES de sobrescrever os
    ; arquivos (app de bandeja: sobrevive ao fechamento da janela; o Windows
    ; recusa sobrescrever um .exe em uso e o File /r falharia em silêncio).
    ; O código de erro (nenhum processo) é descartado de propósito.
    nsExec::ExecToLog 'taskkill /F /IM kai.exe /T'
    Pop $0
    ; kai.com: um `kai ...` de terminal ainda esperando também segura o arquivo.
    nsExec::ExecToLog 'taskkill /F /IM kai.com /T'
    Pop $0
    Sleep 300

    SetOutPath "$INSTDIR"
    ; Copia recursivamente toda a pasta portável (kai.exe + DLLs + plugins + assets).
    File /r "${KAISRC}\*.*"

    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\kai.exe"
    ${If} $OptDesk == 1
        CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\kai.exe"
    ${Else}
        Delete "$DESKTOP\${APPNAME}.lnk"
    ${EndIf}

    ; Configuração inicial: só em instalação nova, nunca sobre um settings.json existente.
    ${If} $FreshCfg == 1
        Call WriteInitialSettings
    ${EndIf}

    ; Registro POR USUÁRIO (HKCU) para "Adicionar ou remover programas".
    WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${APPVERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\kai.exe"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; PATH do USUÁRIO (HKCU, sem admin) para `kai` funcionar no terminal.
    ; StrStr com ";" nas duas pontas evita duplicar e acha entradas no meio.
    ; WM_SETTINGCHANGE avisa o shell: terminais NOVOS já enxergam o PATH.
    ${If} $OptPath == 1
        ReadRegStr $0 HKCU "Environment" "PATH"
        ${If} $0 == ""
            WriteRegExpandStr HKCU "Environment" "PATH" "$INSTDIR"
        ${Else}
            ${StrStr} $1 ";$0;" ";$INSTDIR;"
            ${If} $1 == ""
                WriteRegExpandStr HKCU "Environment" "PATH" "$0;$INSTDIR"
            ${EndIf}
        ${EndIf}
        SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
    ${EndIf}
SectionEnd

; ------------------------------------------------------------ desinstalar ---
Section "Uninstall"
    nsExec::ExecToLog 'taskkill /F /IM kai.exe /T'
    Pop $0
    nsExec::ExecToLog 'taskkill /F /IM kai.com /T'
    Pop $0
    Sleep 300

    Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
    RMDir "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"

    ; Tira o autostart e o PATH que o instalador/app registraram.
    DeleteRegValue HKCU "${RUN_KEY}" "${APPNAME}"
    ReadRegStr $0 HKCU "Environment" "PATH"
    ${If} $0 != ""
        StrCpy $0 ";$0;"
        ${UnStrRep} $1 $0 ";$INSTDIR;" ";"
        ${If} $1 != $0
            StrCpy $2 $1 1
            ${If} $2 == ";"
                StrCpy $1 $1 "" 1
            ${EndIf}
            StrCpy $2 $1 1 -1
            ${If} $2 == ";"
                StrCpy $1 $1 -1
            ${EndIf}
            ${If} $1 == ""
                DeleteRegValue HKCU "Environment" "PATH"
            ${Else}
                WriteRegExpandStr HKCU "Environment" "PATH" "$1"
            ${EndIf}
            SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
        ${EndIf}
    ${EndIf}

    ; Dados do usuário: o padrão é MANTER (reinstalar volta como estava).
    StrCpy $3 0
    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "$(AskPurge)" /SD IDNO IDNO +2
    StrCpy $3 1

    ; Instalações antigas ficavam em %LocalAppData%\Kai, que é a MESMA pasta
    ; da configuração do Kai (o Windows ignora maiúsculas). Aí o RMDir /r
    ; apagaria os dados: apaga tudo, menos *.json, backups e ai-conversations.
    ${If} $INSTDIR == "$LOCALAPPDATA\kai"
    ${AndIf} $3 == 0
        FindFirst $R0 $R1 "$INSTDIR\*.*"
        ${DoWhile} $R1 != ""
            ${If} $R1 != "."
            ${AndIf} $R1 != ".."
                ${If} ${FileExists} "$INSTDIR\$R1\*.*"
                    ${If} $R1 != "ai-conversations"
                        RMDir /r "$INSTDIR\$R1"
                    ${EndIf}
                ${Else}
                    StrCpy $R2 $R1 5 -5
                    ${UnStrStr} $R3 $R1 ".bak."
                    ${If} $R2 != ".json"
                    ${AndIf} $R3 == ""
                        Delete "$INSTDIR\$R1"
                    ${EndIf}
                ${EndIf}
            ${EndIf}
            FindNext $R0 $R1
        ${Loop}
        FindClose $R0
    ${Else}
        RMDir /r "$INSTDIR"
    ${EndIf}
    RMDir "$INSTDIR"

    ${If} $3 == 1
        RMDir /r "$LOCALAPPDATA\kai"
        RMDir /r "$APPDATA\kai"
    ${EndIf}

    DeleteRegKey HKCU "${UNINST_KEY}"
    DeleteRegKey HKCU "Software\${APPNAME}"
SectionEnd
