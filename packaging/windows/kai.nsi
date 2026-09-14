; kai.nsi — Instalador do Kai para Windows gerado via NSIS (makensis no Linux).
; Alternativa robusta ao Inno Setup (que exigia download instável via wine).
; Uso: makensis -DKAISRC=<pasta portável> -DKAIOUT=<saída> kai.nsi

!ifndef KAISRC
  !define KAISRC "dist\kai-windows"
!endif
!ifndef KAIOUT
  !define KAIOUT "dist"
!endif

!define APPNAME "Kai"
!define APPVERSION "1.0.1"
!define PUBLISHER "Kai"

!include "LogicLib.nsh"
!include "StrFunc.nsh"
!include "WinMessages.nsh"

Unicode true
; Compressão zlib (em vez de /SOLID lzma): o stub resultante é mais simples
; e universalmente compatível.
SetCompressor /FINAL zlib
; CRCCheck off: o self-check de integridade do NSIS FALHA ("Installer
; integrity check has failed") quando o instalador é executado a partir do
; filesystem de REDE do WSL (\\wsl.localhost\... via 9P), pois o stub não
; consegue re-ler os próprios bytes de forma confiável nesse FS — mesmo com
; o arquivo íntegro (validado por 7z). Desligar o CRC evita esse falso
; positivo. A integridade real é garantida na geração (7z test) e o usuário
; ainda deve, idealmente, copiar o .exe para um caminho nativo do Windows.
CRCCheck off

; ${StrStr} (StrFunc.nsh) SÓ pode ser invocado DEPOIS de Unicode/
; SetCompressor/CRCCheck — antes disso o NSIS ainda aceita mudar o charset
; alvo, e a macro já gera código/dados na hora (erro real visto ao mover
; pra cima: "Can't change target charset after data already got
; compressed or header already changed!").
${StrStr}

Name "${APPNAME} ${APPVERSION}"
OutFile "${KAIOUT}\kai-setup.exe"

; INFORMAÇÃO DE VERSÃO no próprio instalador. Instaladores NSIS sem metadados
; são um perfil clássico de falso positivo no Windows Defender (o stub é o
; mesmo usado por muito software indesejado, então a reputação do arquivo pesa).
; Declarar produto, versão, empresa e descrição reduz o escore heurístico.
VIProductVersion "1.0.1.0"
VIAddVersionKey /LANG=1033 "ProductName"     "${APPNAME}"
VIAddVersionKey /LANG=1033 "ProductVersion"  "1.0.1.0"
VIAddVersionKey /LANG=1033 "FileVersion"     "1.0.1.0"
VIAddVersionKey /LANG=1033 "FileDescription" "Instalador do Kai - developer command runner"
VIAddVersionKey /LANG=1033 "CompanyName"     "Kai"
VIAddVersionKey /LANG=1033 "LegalCopyright"  "Copyright (C) 2026"
VIAddVersionKey /LANG=1033 "OriginalFilename" "kai-setup.exe"
; Instalação POR USUÁRIO (não exige admin — máquinas sem root):
;  - instala em %LocalAppData%\Kai
;  - registro em HKCU (não HKLM)
;  - atalhos no perfil do usuário
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\${APPNAME}"
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

; DETECTA uma instalação já existente (pedido do usuário: "queria a
; possibilidade que o setup soubesse reinstalar/atualizar o app") — antes
; o instalador sempre corria em silêncio como se fosse a primeira vez;
; rodar o setup por cima de uma instalação existente sobrescrevia os
; arquivos sem avisar nada (funcionava, mas sem feedback nenhum de que
; era uma ATUALIZAÇÃO, e sem chance de cancelar). InstallDirRegKey acima
; já pré-preenche o diretório da instalação anterior na página seguinte;
; aqui só adiciona a CONFIRMAÇÃO com a versão antiga/nova antes de chegar
; lá, e reusa o mesmo InstallDir sem precisar redigitar.
Function .onInit
    ReadRegStr $0 HKCU "Software\${APPNAME}" "InstallDir"
    ${If} $0 != ""
        ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion"
        ${If} $1 == "${APPVERSION}"
            MessageBox MB_YESNO|MB_ICONQUESTION \
                "Kai ${APPVERSION} já está instalado em:$\r$\n$0$\r$\n$\r$\nDeseja reinstalar por cima da instalação atual?" \
                IDYES oninit_proceed
            Quit
        ${Else}
            MessageBox MB_YESNO|MB_ICONQUESTION \
                "Kai $1 está instalado em:$\r$\n$0$\r$\n$\r$\nDeseja atualizar para a versão ${APPVERSION}?" \
                IDYES oninit_proceed
            Quit
        ${EndIf}
        oninit_proceed:
        StrCpy $INSTDIR $0
    ${EndIf}
FunctionEnd

Section "Install"
    ; Encerra qualquer instância do Kai já rodando ANTES de sobrescrever os
    ; arquivos (bug relatado: instalar/testar builds seguidos sem fechar o
    ; Kai entre eles — como app de bandeja, ele sobrevive ao fechamento da
    ; janela). Sem isto, o Windows recusa sobrescrever um .exe em uso: o
    ; File /r abaixo falha silenciosamente pro binário travado, o usuário
    ; segue testando a versão ANTIGA sem perceber, e mudanças de
    ; comportamento (ex: a config "Iniciar visível") parecem não fazer
    ; efeito quando na verdade nunca chegaram a rodar. taskkill sem
    ; /IM correspondente retorna código de erro — ignorado de propósito
    ; (Pop $0 descarta; nada a fazer se não havia processo algum).
    ; /T mata a árvore de processos filhos também.
    nsExec::ExecToLog 'taskkill /F /IM kai.exe /T'
    Pop $0
    ; Dá um instante pro SO liberar de fato o handle do arquivo antes do
    ; File /r seguinte tentar sobrescrevê-lo.
    Sleep 300

    SetOutPath "$INSTDIR"
    ; Copia recursivamente toda a pasta portável (kai.exe + DLLs + plugins + assets).
    File /r "${KAISRC}\*.*"

    ; Atalhos no Menu Iniciar e Área de Trabalho.
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\kai.exe"
    CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\kai.exe"

    ; Registro POR USUÁRIO (HKCU) para "Adicionar ou remover programas".
    WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${APPVERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayIcon" "$INSTDIR\kai.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Adiciona $INSTDIR ao PATH do USUÁRIO (pedido: "queria usar por linha
    ; de comando"/"consigo rodar algo por linha de comando no term?") — sem
    ; isto, `kai` só funcionava chamando o caminho completo do .exe. HKCU
    ; (não HKLM): mesma política do resto do instalador, sem exigir admin.
    ; StrStr evita duplicar a entrada numa reinstalação/atualização por
    ; cima. WM_SETTINGCHANGE avisa o shell na hora — terminais NOVOS já
    ; enxergam o PATH atualizado; os que já estavam abertos continuam com o
    ; PATH antigo até reabrir (comportamento normal do Windows, não dá pra
    ; contornar de fora do próprio processo do shell).
    ReadRegStr $0 HKCU "Environment" "PATH"
    ${If} $0 == ""
        WriteRegExpandStr HKCU "Environment" "PATH" "$INSTDIR"
    ${Else}
        ; Delimitador nas DUAS pontas de ambos os lados — sem isto, uma
        ; entrada no MEIO ou no INÍCIO do PATH passaria batido (StrStr só
        ; acha substring literal, não segmento delimitado por ";").
        ${StrStr} $1 ";$0;" ";$INSTDIR;"
        ${If} $1 == ""
            WriteRegExpandStr HKCU "Environment" "PATH" "$0;$INSTDIR"
        ${EndIf}
    ${EndIf}
    SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
SectionEnd

Section "Uninstall"
    ; Mesmo motivo do Install acima: encerra o Kai antes de apagar os
    ; arquivos, senão RMDir /r falha pro binário em uso.
    nsExec::ExecToLog 'taskkill /F /IM kai.exe /T'
    Pop $0
    Sleep 300

    Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
    RMDir "$SMPROGRAMS\${APPNAME}"
    Delete "$DESKTOP\${APPNAME}.lnk"
    RMDir /r "$INSTDIR"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
    DeleteRegKey HKCU "Software\${APPNAME}"
SectionEnd
