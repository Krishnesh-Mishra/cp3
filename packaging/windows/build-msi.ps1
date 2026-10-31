# =============================================================================
# packaging/windows/build-msi.ps1 - build a single per-user Windows MSI.
#
# Input: the relocatable tree staged by package.ps1 (<StageDir>\bin\cp3c.exe,
# include\, src\, third_party\, share\, manifest.json, README.txt).
# Output: <OutDir>\cp3-<version>-windows-<arch>.msi.
#
# The MSI is intentionally self-contained:
#   * per-user scope, default %LOCALAPPDATA%\Programs\cp3 (no admin/UAC);
#   * every staged file is a component with a deterministic GUID;
#   * <install>\bin is appended to the user PATH, so `cp3` and `cp3c` work in
#     new terminals without any manual setup; uninstall removes that entry;
#   * a Start Menu shortcut opens a command prompt in <install>\bin;
#   * Apps & Features shows cp3 and its install location.
#
# WiX 5 or 6 is required. The repository pins WiX 5.0.2 in
# packaging\windows\.config\dotnet-tools.json; `dotnet tool restore` makes the
# build reproducible without installing a global tool. The WiX extension cache
# lives in packaging\windows\.wix (ignored by git), keeping the repo root clean.
# WiX 7 is refused because it requires the OSMF EULA.
#
# Usage (from the repository root):
#   powershell -ExecutionPolicy Bypass -File packaging\windows\build-msi.ps1
#   powershell ... build-msi.ps1 -StageDir out\windows-x64\cp3
#   powershell ... build-msi.ps1 -WixPath C:\tools\wix5\wix.exe
#   powershell ... build-msi.ps1 -NoUi        # no WixUI extension/UI
# =============================================================================
#Requires -Version 5.1
[CmdletBinding()]
param(
    # Staged distribution; default out\windows-<arch>\cp3.
    [string]$StageDir,

    # Directory that receives the MSI; default the stage's parent.
    [string]$OutDir,

    # Artifact version; default version.txt at the repository root.
    [string]$Version,

    [ValidateSet('x64', 'arm64', 'x86')]
    [string]$Arch = 'x64',

    # Explicit wix.exe. When omitted, the repo-local WiX tool is restored.
    [string]$WixPath,

    # Build without the WixUI extension (no wizard, silent/basic MSI only).
    [switch]$NoUi
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Fail([string]$Message) { throw ("build-msi.ps1: " + $Message) }
function Info([string]$Message) { Write-Host ("msi: " + $Message) }
function Warn([string]$Message) { Write-Warning ("msi: " + $Message) }

$WindowsDir = $PSScriptRoot
$PackageDir = Split-Path -Parent $WindowsDir
$RepoRoot = Split-Path -Parent $PackageDir

# --- version -----------------------------------------------------------------
if (-not $Version) {
    $versionFile = Join-Path $RepoRoot 'version.txt'
    if (-not (Test-Path -LiteralPath $versionFile)) {
        Fail "version.txt is missing at the repository root"
    }
    $Version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.\-]+)?$') {
    Fail "version '$Version' is not <major>.<minor>.<patch>[-pre]"
}
# Windows Installer ProductVersion is numeric major.minor.build only.
$numericVersion = ($Version -split '-', 2)[0]
if ($numericVersion -notmatch '^\d+\.\d+\.\d+$') {
    Fail "version '$Version' does not have a numeric major.minor.patch prefix"
}

# --- inputs ------------------------------------------------------------------
if (-not $StageDir) { $StageDir = Join-Path $RepoRoot "out\windows-$Arch\cp3" }
$StageDir = [System.IO.Path]::GetFullPath($StageDir).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $StageDir 'bin\cp3c.exe'))) {
    Fail "no staged compiler at '$StageDir\bin\cp3c.exe'; run package.ps1 first"
}

if (-not $OutDir) { $OutDir = Split-Path -Parent $StageDir }
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$scratch = Join-Path $RepoRoot 'build\scratch\packaging\msi'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$wxs = Join-Path $scratch ("cp3-" + $Arch + ".wxs")
$msi = Join-Path $OutDir ("cp3-" + $Version + "-windows-" + $Arch + ".msi")
if (Test-Path -LiteralPath $msi) { Remove-Item -LiteralPath $msi -Force }

# --- stable IDs --------------------------------------------------------------
function Get-StableId([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text))
        return ([System.BitConverter]::ToString($bytes) -replace '-', '').Substring(0, 16).ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function New-StableGuid([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes('cp3-msi:' + $Text))
        # Set RFC 4122 version 5 and variant bits. The exact namespace is
        # irrelevant here; determinism is what keeps component GUIDs stable.
        $bytes[6] = [byte](($bytes[6] -band 0x0F) -bor 0x50)
        $bytes[8] = [byte](($bytes[8] -band 0x3F) -bor 0x80)
        $hex = -join ($bytes[0..15] | ForEach-Object { $_.ToString('x2') })
        $guid = $hex.Substring(0, 8) + '-' + $hex.Substring(8, 4) + '-' +
                $hex.Substring(12, 4) + '-' + $hex.Substring(16, 4) + '-' +
                $hex.Substring(20, 12)
        return ('{' + $guid + '}').ToUpperInvariant()
    } finally {
        $sha.Dispose()
    }
}

# --- WiX tool ----------------------------------------------------------------
function Get-WixTool([string]$ExplicitPath) {
    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            Fail "wix.exe was not found at '$ExplicitPath'"
        }
        $path = (Resolve-Path -LiteralPath $ExplicitPath).Path
        $versionLines = @(& $path --version 2>&1)
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0 -or $versionLines.Count -eq 0) {
            Fail "could not run '$path --version'"
        }
        return @{ Kind = 'exe'; Path = $path; Version = "$($versionLines[0])" }
    }

    $manifest = Join-Path $WindowsDir '.config\dotnet-tools.json'
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $manifest) -and $dotnet) {
        Info "restoring repo-local WiX tool"
        # Run from packaging\windows so dotnet discovers .config\dotnet-tools.json
        # and WiX discovers the local .wix extension cache.
        Push-Location -LiteralPath $WindowsDir
        try {
            $restore = @(& $dotnet.Source tool restore 2>&1)
            $restoreExit = $LASTEXITCODE
            if ($restoreExit -eq 0) {
                $versionLines = @(& $dotnet.Source tool run wix -- --version 2>&1)
                $runExit = $LASTEXITCODE
                if ($runExit -eq 0 -and $versionLines.Count -gt 0) {
                    return @{
                        Kind = 'dotnet'
                        Dotnet = $dotnet.Source
                        Version = "$($versionLines[0])"
                    }
                }
            }
        } finally {
            Pop-Location
        }
        Warn "the repo-local WiX tool could not be restored; looking on PATH"
    }

    $wix = Get-Command wix.exe -ErrorAction SilentlyContinue
    if (-not $wix) { $wix = Get-Command wix -ErrorAction SilentlyContinue }
    if (-not $wix) {
        Fail ("WiX Toolset 5/6 was not found. From packaging\windows run " +
              "'dotnet tool restore' (the repo pins WiX 5.0.2), or install it with " +
              "'dotnet tool install --global wix --version 5.0.2'.")
    }
    $versionLines = @(& $wix.Source --version 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $versionLines.Count -eq 0) {
        Fail "could not run '$($wix.Source) --version'"
    }
    return @{ Kind = 'exe'; Path = $wix.Source; Version = "$($versionLines[0])" }
}

function Invoke-WixTool([hashtable]$Tool, [string[]]$Arguments) {
    # Run from packaging\windows so dotnet and WiX use the local manifest and
    # extension cache instead of creating .config/.wix at the repo root.
    Push-Location -LiteralPath $WindowsDir
    try {
        if ($Tool.Kind -eq 'dotnet') {
            $dotnetArgs = @('tool', 'run', 'wix', '--') + $Arguments
            & $Tool.Dotnet @dotnetArgs
        } else {
            & $Tool.Path @Arguments
        }
    } finally {
        Pop-Location
    }
}

$tool = Get-WixTool $WixPath
$tool.Version = (($tool.Version -split '\+')[0]).Trim()
if ($tool.Version -notmatch '^(5|6)\.\d+\.\d+$') {
    Fail ("WiX $($tool.Version) is not supported. Use WiX 5 or 6. " +
          "WiX 7 requires the OSMF EULA; this repository pins WiX 5.0.2 in " +
          "packaging\windows\.config\dotnet-tools.json. Run 'dotnet tool restore' " +
          "from packaging\windows.")
}
Info "WiX $($tool.Version)"

if (-not $NoUi) {
    $list = @(Invoke-WixTool -Tool $tool -Arguments @('extension', 'list') 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Fail ("could not list WiX extensions:`n" + ($list -join "`n"))
    }
    $hasUi = (($list -join "`n") -match ('WixToolset\.UI\.wixext\s+' +
        [regex]::Escape($tool.Version)))
    if (-not $hasUi) {
        Info "adding WiX UI extension $($tool.Version)"
        $add = @(Invoke-WixTool -Tool $tool -Arguments @(
            'extension', 'add', ("WixToolset.UI.wixext/" + $tool.Version)) 2>&1)
        if ($LASTEXITCODE -ne 0) {
            Fail ("could not add WixToolset.UI.wixext/$($tool.Version):`n" + ($add -join "`n"))
        }
    }
}

# --- enumerate the stage -----------------------------------------------------
$stagePrefix = $StageDir + [System.IO.Path]::DirectorySeparatorChar
$root = @{ Name = ''; Rel = ''; Dirs = @{}; Files = @() }
foreach ($file in (Get-ChildItem -LiteralPath $StageDir -Recurse -File -Force | Sort-Object FullName)) {
    if (-not $file.FullName.StartsWith($stagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        Fail "staged file '$($file.FullName)' is outside '$StageDir'"
    }
    $rel = $file.FullName.Substring($stagePrefix.Length).Replace('\', '/')
    $parts = $rel -split '/'
    $node = $root
    for ($i = 0; $i -lt $parts.Length - 1; $i++) {
        $name = $parts[$i]
        if (-not $node.Dirs.ContainsKey($name)) {
            $node.Dirs[$name] = @{
                Name = $name
                Rel  = (($node.Rel + '/' + $name).TrimStart('/'))
                Dirs = @{}
                Files = @()
            }
        }
        $node = $node.Dirs[$name]
    }
    $node.Files += [pscustomobject]@{
        Name = $file.Name
        Rel  = $rel
        File = $file
    }
}

$componentIds = New-Object System.Collections.Generic.List[string]
$wixNs = 'http://wixtoolset.org/schemas/v4/wxs'
$uiNs = 'http://wixtoolset.org/schemas/v4/wxs/ui'

# Never change this GUID: it is what ties every cp3 MSI release together so a
# newer version can find and replace an older one. ProductCode is per-version.
$upgradeCode = '{D315919E-130C-50B6-8EC1-FE73F4D05C92}'

function Write-FileComponent($Entry) {
    $id = Get-StableId $Entry.Rel
    $componentId = 'cmp_' + $id
    $fileId = 'fil_' + $id
    $writer.WriteStartElement('Component', $wixNs)
    $writer.WriteAttributeString('Id', $componentId)
    $writer.WriteAttributeString('Guid', (New-StableGuid ('component:' + $Entry.Rel)))
    $writer.WriteStartElement('File', $wixNs)
    $writer.WriteAttributeString('Id', $fileId)
    $writer.WriteAttributeString('Name', $Entry.Name)
    $writer.WriteAttributeString('Source', $Entry.File.FullName)
    $writer.WriteAttributeString('KeyPath', 'yes')
    $writer.WriteEndElement()
    if ($Entry.Rel -ieq 'bin/cp3c.exe') {
        # This is the single environment change the MSI makes. Windows
        # Installer writes it to HKCU\Environment (System="no"), appends it to
        # the existing user PATH, and removes it again on uninstall.
        $writer.WriteStartElement('Environment', $wixNs)
        $writer.WriteAttributeString('Id', 'UserPath')
        $writer.WriteAttributeString('Name', 'PATH')
        $writer.WriteAttributeString('Value', '[INSTALLDIR]bin')
        $writer.WriteAttributeString('Action', 'set')
        $writer.WriteAttributeString('Part', 'last')
        $writer.WriteAttributeString('System', 'no')
        $writer.WriteAttributeString('Permanent', 'no')
        $writer.WriteEndElement()
    }
    $writer.WriteEndElement()
    $componentIds.Add($componentId)
}

function Write-DirectoryChildren($Node) {
    foreach ($name in (@($Node.Dirs.Keys) | Sort-Object)) {
        $child = $Node.Dirs[$name]
        $writer.WriteStartElement('Directory', $wixNs)
        $writer.WriteAttributeString('Id', ('dir_' + (Get-StableId $child.Rel)))
        $writer.WriteAttributeString('Name', $child.Name)
        Write-DirectoryChildren $child
        $writer.WriteEndElement()
    }
    foreach ($entry in (@($Node.Files) | Sort-Object Name)) {
        Write-FileComponent $entry
    }
}

# --- generate the WiX source -------------------------------------------------
$settings = New-Object System.Xml.XmlWriterSettings
$settings.Indent = $true
$settings.IndentChars = '  '
$settings.NewLineChars = "`r`n"
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writer = [System.Xml.XmlWriter]::Create($wxs, $settings)
try {
    $writer.WriteStartDocument()
    $writer.WriteStartElement('Wix', $wixNs)
    if (-not $NoUi) {
        $writer.WriteAttributeString('xmlns', 'ui', $null, $uiNs)
    }

    $writer.WriteStartElement('Package', $wixNs)
    $writer.WriteAttributeString('Name', 'cp3')
    $writer.WriteAttributeString('Manufacturer', 'cp3')
    $writer.WriteAttributeString('Version', $numericVersion)
    $writer.WriteAttributeString('UpgradeCode', (New-StableGuid 'upgrade-code'))
    $writer.WriteAttributeString('ProductCode', (New-StableGuid ('product:' + $Version)))
    $writer.WriteAttributeString('Scope', 'perUser')
    $writer.WriteAttributeString('Compressed', 'yes')

    $writer.WriteStartElement('MajorUpgrade', $wixNs)
    # Permit reinstalling/replacing another build with the same MSI version.
    # Downgrades to a lower major.minor.patch version remain blocked.
    $writer.WriteAttributeString('AllowSameVersionUpgrades', 'yes')
    $writer.WriteAttributeString('DowngradeErrorMessage',
        'A newer version of cp3 is already installed.')
    $writer.WriteEndElement()

    $writer.WriteStartElement('MediaTemplate', $wixNs)
    $writer.WriteAttributeString('EmbedCab', 'yes')
    $writer.WriteEndElement()

    if (-not $NoUi) {
        $writer.WriteStartElement('WixUI', $uiNs)
        $writer.WriteAttributeString('Id', 'WixUI_InstallDir')
        $writer.WriteEndElement()
        $writer.WriteStartElement('Property', $wixNs)
        $writer.WriteAttributeString('Id', 'WIXUI_INSTALLDIR')
        $writer.WriteAttributeString('Value', 'INSTALLDIR')
        $writer.WriteEndElement()
    }

    # Default install root: %LOCALAPPDATA%\Programs\cp3.
    $writer.WriteStartElement('StandardDirectory', $wixNs)
    $writer.WriteAttributeString('Id', 'LocalAppDataFolder')
    $writer.WriteStartElement('Directory', $wixNs)
    $writer.WriteAttributeString('Id', 'ProgramsFolder')
    $writer.WriteAttributeString('Name', 'Programs')
    $writer.WriteStartElement('Directory', $wixNs)
    $writer.WriteAttributeString('Id', 'INSTALLDIR')
    $writer.WriteAttributeString('Name', 'cp3')
    Write-DirectoryChildren $root
    $writer.WriteEndElement()
    $writer.WriteEndElement()
    $writer.WriteEndElement()

    # Start Menu > cp3 > cp3 Command Prompt. The shortcut uses cmd.exe so the
    # console stays open; the working directory contains cp3.cmd.
    $writer.WriteStartElement('StandardDirectory', $wixNs)
    $writer.WriteAttributeString('Id', 'ProgramMenuFolder')
    $writer.WriteStartElement('Directory', $wixNs)
    $writer.WriteAttributeString('Id', 'ProgramMenuCp3')
    $writer.WriteAttributeString('Name', 'cp3')
    $writer.WriteEndElement()
    $writer.WriteEndElement()

    $startMenuId = 'cmp_startmenu'
    $writer.WriteStartElement('Component', $wixNs)
    $writer.WriteAttributeString('Id', $startMenuId)
    $writer.WriteAttributeString('Guid', (New-StableGuid 'component:startmenu'))
    $writer.WriteAttributeString('Directory', 'ProgramMenuCp3')
    $writer.WriteStartElement('Shortcut', $wixNs)
    $writer.WriteAttributeString('Id', 'Cp3CommandPrompt')
    $writer.WriteAttributeString('Name', 'cp3 Command Prompt')
    $writer.WriteAttributeString('Description', 'Open a terminal with the cp3 toolchain')
    # Use the 64-bit System32 cmd even though the shortcut component itself is
    # not a 64-bit file component; [SystemFolder] would resolve to SysWOW64.
    $writer.WriteAttributeString('Target', '[WindowsFolder]System32\cmd.exe')
    $writer.WriteAttributeString('Arguments', '/k')
    $writer.WriteAttributeString('WorkingDirectory', ('dir_' + (Get-StableId 'bin')))
    $writer.WriteEndElement()
    $writer.WriteStartElement('RemoveFolder', $wixNs)
    $writer.WriteAttributeString('Id', 'RemoveProgramMenuCp3')
    $writer.WriteAttributeString('Directory', 'ProgramMenuCp3')
    $writer.WriteAttributeString('On', 'uninstall')
    $writer.WriteEndElement()
    $writer.WriteStartElement('RegistryValue', $wixNs)
    $writer.WriteAttributeString('Root', 'HKCU')
    $writer.WriteAttributeString('Key', 'Software\cp3')
    $writer.WriteAttributeString('Name', 'StartMenu')
    $writer.WriteAttributeString('Type', 'integer')
    $writer.WriteAttributeString('Value', '1')
    $writer.WriteAttributeString('KeyPath', 'yes')
    $writer.WriteEndElement()
    $writer.WriteEndElement()
    $componentIds.Add($startMenuId)

    $writer.WriteStartElement('ComponentGroup', $wixNs)
    $writer.WriteAttributeString('Id', 'ProductComponents')
    foreach ($componentId in $componentIds) {
        $writer.WriteStartElement('ComponentRef', $wixNs)
        $writer.WriteAttributeString('Id', $componentId)
        $writer.WriteEndElement()
    }
    $writer.WriteEndElement()

    $writer.WriteStartElement('Feature', $wixNs)
    $writer.WriteAttributeString('Id', 'MainFeature')
    $writer.WriteAttributeString('Title', 'cp3')
    $writer.WriteAttributeString('Level', '1')
    $writer.WriteStartElement('ComponentGroupRef', $wixNs)
    $writer.WriteAttributeString('Id', 'ProductComponents')
    $writer.WriteEndElement()
    $writer.WriteEndElement()

    $writer.WriteEndElement()  # Package
    $writer.WriteEndElement()  # Wix
    $writer.WriteEndDocument()
} finally {
    $writer.Close()
}
Info "WiX source: $wxs"

# --- build -------------------------------------------------------------------
$wixArgs = @('build', '-arch', $Arch, '-pdbtype', 'none', '-out', $msi)
if (-not $NoUi) { $wixArgs += @('-ext', 'WixToolset.UI.wixext') }
$wixArgs += $wxs

Info "building $msi"
$buildOutput = @(Invoke-WixTool -Tool $tool -Arguments $wixArgs 2>&1)
if ($LASTEXITCODE -ne 0) {
    Fail ("WiX build failed (exit $LASTEXITCODE):`n" + ($buildOutput -join "`n"))
}
if (-not (Test-Path -LiteralPath $msi)) {
    Fail "WiX reported success but '$msi' is missing"
}

$sizeMb = [math]::Round((Get-Item -LiteralPath $msi).Length / 1MB, 2)
Info "msi: $msi ($sizeMb MB)"
Info "install: double-click the MSI, or run: msiexec /i `"$msi`" /qn"
Info "uninstall: msiexec /x `"$msi`" /qn"
