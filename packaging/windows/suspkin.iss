; Inno Setup script for the Windows installer.
;
; Every path and version comes from the workflow via /D, so this file has no
; build numbers baked into it:
;
;   ISCC /DAppVersion=0.1.0-build.7 /DFileVersion=0.1.0.7 ^
;        /DSourceDir=...\dist /DOutputDir=...\out /DOutputName=... suspkin.iss

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef FileVersion
  #define FileVersion "0.0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\out"
#endif
#ifndef OutputName
  #define OutputName "SuspensionKinematics-setup"
#endif

#define AppName "SuspensionKinematics"
#define AppPublisher "Bremergy"
#define AppExe "suspkin.exe"

[Setup]
; Never reuse this GUID for another product -- it is what lets an installer
; recognise and upgrade a previous install in place.
AppId={{7C4B1E92-3A6D-4F58-9B0C-2E1D8A5F6C34}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppSupportURL=https://github.com/KeksBombe/SuspensionKinematics
VersionInfoVersion={#FileVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
; Per-user install when run without elevation, per-machine with it, so the
; installer never hard-fails on a locked-down machine.
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open .stl, .step and .stp files with {#AppName}"; GroupDescription: "File associations:"

[Files]
; The whole install tree: the exe, the Qt runtime, the OCCT DLLs and the
; plugins/ directory. recursesubdirs matters -- without plugins/platforms the
; app starts to "no Qt platform plugins could be initialized".
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.stl\OpenWithProgids"; ValueType: string; ValueName: "{#AppName}.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.step\OpenWithProgids"; ValueType: string; ValueName: "{#AppName}.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.stp\OpenWithProgids"; ValueType: string; ValueName: "{#AppName}.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#AppName}.Model"; ValueType: string; ValueName: ""; ValueData: "3D model"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#AppName}.Model\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
