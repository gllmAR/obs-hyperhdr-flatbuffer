; Inno Setup script for the obs-hyperhdr Windows installer.
; Built by CI from the repository root:
;   ISCC.exe /DAppVersion=0.1.0 installer\windows\obs-hyperhdr.iss
; OBS 32 loads Windows plugins from %ProgramData%\obs-studio\plugins\<module>\bin\64bit,
; so the installer needs administrator rights and targets that folder.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{B6D3F1A2-7C4E-4F8A-9E21-5A0C3D7E8F40}
AppName=obs-hyperhdr
AppVersion={#AppVersion}
AppPublisher=gllmAR
DefaultDirName={commonappdata}\obs-studio\plugins\obs-hyperhdr
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=obs-hyperhdr-windows-x64-setup
Compression=lzma2
SolidCompression=yes

[Files]
Source: "..\..\stage\obs-hyperhdr\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
