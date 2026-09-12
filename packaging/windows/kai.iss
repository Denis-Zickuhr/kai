; ============================================================================
;  kai.iss — Script do Inno Setup 6 para gerar o instalável do Kai (Windows 11)
;
;  Compile com:  iscc packaging\windows\kai.iss
;  (chamado automaticamente por build-windows.bat e pelo build via Docker,
;   que passam os diretórios via /D — veja abaixo.)
;
;  Diretórios configuráveis via linha de comando do ISCC (com defaults):
;    /DKaiSrcDir=<pasta com kai.exe + DLLs + assets>   (default: ..\..\dist\kai-windows)
;    /DKaiOutDir=<pasta de saída do instalador>        (default: ..\..\dist)
;  Ex.: iscc /DKaiSrcDir=/out/kai-windows /DKaiOutDir=/out packaging\windows\kai.iss
;
;  Pré-requisito: a KaiSrcDir já deve existir (gerada por windeployqt),
;  contendo kai.exe + DLLs do Qt + a pasta assets\.
; ============================================================================

#define AppName "Kai"
#define AppVersion "1.0.0"
#define AppPublisher "Kai"
#define AppExeName "kai.exe"

; Diretórios parametrizáveis (default aponta para a pasta portável real
; gerada pelo build: dist\kai-windows). Corrige a divergência histórica
; que apontava para dist\kai (nome que o build nunca produziu).
#ifndef KaiSrcDir
  #define KaiSrcDir "..\..\dist\kai-windows"
#endif
#ifndef KaiOutDir
  #define KaiOutDir "..\..\dist"
#endif

[Setup]
AppId={{7C1F1D2A-9B3E-4C6A-8E21-KAI0000RUNNER}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2
SolidCompression=yes
; Windows 11 é 64-bit; instala em Program Files (64-bit).
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
OutputDir={#KaiOutDir}
OutputBaseFilename=kai-setup
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Empacota toda a pasta de distribuição (kai.exe + DLLs do Qt + assets).
Source: "{#KaiSrcDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
