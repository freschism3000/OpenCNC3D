# Windows PowerShell 5.1. This script copies; it never patches the original installation.
[CmdletBinding()]
param(
    [string]$InstallDir,
    [string]$OutputRoot,
    [switch]$Control
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$bundleRoot = $PSScriptRoot
$manifestPath = Join-Path $bundleRoot 'diagnostic-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'diagnostic-manifest.json is missing. Extract the complete diagnostic ZIP before running.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.format -ne 1) { throw 'Unsupported diagnostic manifest.' }
function Test-RuntimeAssetFile([string]$Path, [object]$Entry) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-Item -LiteralPath $Path).Length -ne $Entry.size -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Entry.sha256) {
        throw 'The verified factory runtime cache is missing or mismatched. Extract the complete diagnostic ZIP again.'
    }
}
function Get-VerifiedRuntimeAssets([object]$Manifest, [string]$BundleRoot) {
    # R2 and older manifests have no optional asset member; strict mode must accept them.
    $property = $Manifest.PSObject.Properties['runtime_assets']
    if (-not $property) { return }
    if ($null -eq $property.Value -or $property.Value -isnot [pscustomobject]) {
        throw 'Invalid diagnostic runtime_assets metadata.'
    }
    foreach ($asset in $property.Value.PSObject.Properties) {
        if ($asset.Name -cne 'ebld/FACT.ebld.fclv') {
            throw 'This bundle requests an unsupported runtime asset path.'
        }
        $size = $asset.Value.PSObject.Properties['size']
        $hash = $asset.Value.PSObject.Properties['sha256']
        if (-not $size -or -not $hash -or [string]$size.Value -cne '149365' -or
            [string]$hash.Value -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Invalid factory runtime cache fingerprint.'
        }
        $relative = 'ebld/FACT.ebld.fclv'
        $path = Join-Path (Join-Path $BundleRoot 'assets') $relative
        $entry = [pscustomobject]@{ path = $relative; size = [long]$size.Value;
            sha256 = [string]$hash.Value; source = $path }
        Test-RuntimeAssetFile $path $entry
        $entry
    }
}
function Copy-VerifiedRuntimeAssets([object[]]$Assets, [string]$PrivateGameRoot, [string]$OriginalInstallRoot) {
    $private = [IO.Path]::GetFullPath($PrivateGameRoot).TrimEnd('\', '/')
    $original = [IO.Path]::GetFullPath($OriginalInstallRoot).TrimEnd('\', '/')
    if ($private.Equals($original, [StringComparison]::OrdinalIgnoreCase) -or
        $private.StartsWith($original + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A runtime asset can only be copied into the separate private game.'
    }
    foreach ($asset in $Assets) {
        if ($asset.path -cne 'ebld/FACT.ebld.fclv') { throw 'Unsupported private runtime asset path.' }
        Test-RuntimeAssetFile $asset.source $asset
        $parent = Join-Path $private 'ebld'
        foreach ($path in @($private, $parent, (Join-Path $parent 'FACT.ebld.fclv'))) {
            if ((Test-Path -LiteralPath $path) -and
                ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw 'A private runtime asset path must not be a link or junction.'
            }
        }
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        $destination = Join-Path $parent 'FACT.ebld.fclv'
        Copy-Item -LiteralPath $asset.source -Destination $destination -Force
        Test-RuntimeAssetFile $destination $asset
    }
}
$verifiedRuntimeAssets = @(Get-VerifiedRuntimeAssets $manifest $bundleRoot)
$binaryNames = @('opencnc3d-game.exe', 'cnc_eyes.exe')
foreach ($name in $binaryNames) {
    $path = Join-Path (Join-Path $bundleRoot 'binaries') $name
    $entry = $manifest.binaries.PSObject.Properties[$name]
    if (-not $entry -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "The verified diagnostic binary is missing: $name"
    }
    if ((Get-Item -LiteralPath $path).Length -ne $entry.Value.size -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value.sha256) {
        throw "Diagnostic binary verification failed: $name. Extract the ZIP again."
    }
}
if (-not $InstallDir) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    $picker.Title = 'Choose OpenCNC3D.exe in your existing v0.8.4 game folder'
    $picker.Filter = 'OpenCNC3D game|OpenCNC3D.exe;opencnc3d-game.exe'
    $picker.CheckFileExists = $true
    if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        throw 'Cancelled before any diagnostic game was created.'
    }
    $InstallDir = Split-Path -Parent $picker.FileName
    $picker.Dispose()
}
$InstallDir = (Get-Item -LiteralPath $InstallDir).FullName.TrimEnd('\', '/')
if ($InstallDir -match '^[A-Za-z]:$') { throw 'Choose the game folder, rather than the root of a drive.' }
foreach ($name in @('opencnc3d-game.exe', 'cnc_eyes.exe', 'TiberianDawn.dll', 'SDL2.dll', 'dosmenu.pack')) {
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $name) -PathType Leaf)) {
        throw "This is not a complete game installation: $name is missing. Select the extracted v0.8.4 game folder."
    }
}
foreach ($name in @('missions', 'content')) {
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $name) -PathType Container)) {
        throw "This installation is missing its $name folder."
    }
}
$installRecord = Join-Path $InstallDir 'opencnc3d-install.txt'
$buildRecord = Join-Path $InstallDir 'BUILD-ID.txt'
$versionMatches = $false
if (Test-Path -LiteralPath $installRecord -PathType Leaf) {
    $versionMatches = (Get-Content -LiteralPath $installRecord -Raw) -match '(?m)^version\s+0\.8\.4\s*$'
}
if (-not $versionMatches -and (Test-Path -LiteralPath $buildRecord -PathType Leaf)) {
    $versionMatches = (Get-Content -LiteralPath $buildRecord -Raw) -match '^v0\.8\.4(?:\s|$)'
}
if (-not $versionMatches) {
    throw 'Select the complete Windows v0.8.4 installation. This diagnostic reuses its engine and SDL libraries.'
}
function Test-BaseLibrary([string]$Path, [object]$Entry, [string]$Name) {
    # Accept only exact fingerprints of published variants; never replace a library.
    $variantsProperty = $Entry.PSObject.Properties['variants']
    $variants = @($Entry)
    if ($variantsProperty) { $variants = @($variantsProperty.Value) }
    if ($variants.Count -eq 0) { throw "The diagnostic manifest has no library fingerprints for $Name." }
    $file = Get-Item -LiteralPath $Path
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $matched = $null
    foreach ($variant in $variants) {
        $sizeProperty = $variant.PSObject.Properties['size']
        $hashProperty = $variant.PSObject.Properties['sha256']
        if (-not $sizeProperty -or -not $hashProperty -or
            [long]$sizeProperty.Value -le 0 -or
            [string]$hashProperty.Value -notmatch '^[0-9a-f]{64}$') {
            throw "The diagnostic manifest has an invalid library fingerprint for $Name."
        }
        if ($file.Length -eq [long]$sizeProperty.Value -and $actualHash -eq [string]$hashProperty.Value) {
            $matched = $variant
        }
    }
    if (-not $matched) {
        throw "$Name is not listed in this diagnostic bundle's published v0.8.4 fingerprints. Observed size: $($file.Length); SHA256: $actualHash. Use the latest diagnostic ZIP, or include this fingerprint when reporting the error."
    }
    $packageProperty = $matched.PSObject.Properties['package']
    $package = if ($packageProperty) { [string]$packageProperty.Value } else { 'legacy_single_fingerprint' }
    return [pscustomobject]@{ name = $Name; size = $file.Length; sha256 = $actualHash; release_variant = $package }
}
$selectedLibraries = [ordered]@{}
foreach ($name in @('TiberianDawn.dll', 'SDL2.dll')) {
    $entry = $manifest.base_libraries.PSObject.Properties[$name]
    if (-not $entry) { throw "The diagnostic manifest is missing its $name fingerprints." }
    $selectedLibraries[$name] = Test-BaseLibrary (Join-Path $InstallDir $name) $entry.Value $name
}
if (@(Get-Process -Name 'opencnc3d-game', 'cnc_eyes' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Close other OpenCNC3D game/editor processes before copying their settings and saves.'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'OpenCNC3D Diagnostics'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if ($OutputRoot.StartsWith($InstallDir + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $OutputRoot.Equals($InstallDir, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The diagnostic output must be outside the original game folder.'
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$modeName = if ($Control) { 'control' } else { 'trace' }
$session = Join-Path $OutputRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $modeName + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$gameCopy = Join-Path $session 'game'
$savesCopy = Join-Path $session 'saves'
$reportDir = Join-Path $session 'report'
$traceDir = Join-Path $gameCopy 'performance-trace'
New-Item -ItemType Directory -Path $gameCopy, $savesCopy, $reportDir, $traceDir | Out-Null
$normalPref = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Slipgate Ironworks\OpenCNC3D'
$legacyPref = Join-Path (Split-Path -Parent $normalPref) 'CNC3D'

function Get-ProtectedSnapshot {
    $out = @{}
    foreach ($name in @('opencnc3d-game.exe', 'cnc_eyes.exe', 'TiberianDawn.dll', 'SDL2.dll',
                        'BUILD-ID.txt', 'opencnc3d-install.txt', 'opencnc3d-log.txt', 'opencnc3d-fx.cfg', 'opencnc3d-controls.cfg',
                        'opencnc3d-multiplayer.cfg', 'cnc3d-fx.cfg', 'cnc3d-controls.cfg', 'cnc3d-multiplayer.cfg', 'ebld/FACT.ebld.fclv')) {
        $path = Join-Path $InstallDir $name
        $out['game/' + $name] = if (Test-Path -LiteralPath $path -PathType Leaf) {
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        } else { '<absent>' }
    }
    if (Test-Path -LiteralPath $normalPref -PathType Container) {
        Get-ChildItem -LiteralPath $normalPref -File | Where-Object {
            $_.Name -match '^(savegame\.\d+(?:\.enh)?|OPENCNC\.IDX|HALLFAME\.DAT|opencnc3d-log\.txt)$'
        } | ForEach-Object {
            $out['saves/' + $_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    }
    if (Test-Path -LiteralPath $legacyPref -PathType Container) {
        Get-ChildItem -LiteralPath $legacyPref -File | Where-Object {
            $_.Name -match '^(savegame\.\d+(?:\.enh)?|CNC3DSAV\.IDX|HALLFAME\.DAT)$'
        } | ForEach-Object {
            $out['legacy-saves/' + $_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    }
    return $out
}
$before = Get-ProtectedSnapshot
foreach ($name in @('TiberianDawn.dll', 'SDL2.dll')) {
    if ($before['game/' + $name].ToLowerInvariant() -ne $selectedLibraries[$name].sha256) {
        throw 'An installed library changed while preparing the diagnostic. Close other game/update processes and run it again.'
    }
}
$sourceFiles = @(Get-ChildItem -LiteralPath $InstallDir -File -Recurse -Force)
$gameBytes = [long](($sourceFiles | Measure-Object Length -Sum).Sum)
$targetDrive = New-Object System.IO.DriveInfo ([IO.Path]::GetPathRoot($session))
if ($targetDrive.AvailableFreeSpace -lt $gameBytes + 1073741824) {
    throw ('A safe game copy needs {0:N1} GB of free space here. Choose another OutputRoot.' -f (($gameBytes + 1073741824) / 1GB))
}
Write-Host ('Copying your game into a separate diagnostic folder ({0:N1} GB).' -f ($gameBytes / 1GB))
Write-Host 'Your installed executables, settings and saves will stay where they are.'
# /XJ refuses junction traversal. No /MIR, /MOVE or /PURGE: original data is read only.
$copyLog = Join-Path $session 'copy.log'
& robocopy.exe $InstallDir $gameCopy /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XJ /NFL /NDL /NJH /NJS /NP "/LOG:$copyLog" | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'The game copy was incomplete. Nothing was launched. See the local copy.log.' }
# Fail closed if a regular source file did not arrive with the same size. Runtime-critical
# binaries/configs receive full digest checks below. Junctions are not copied as aliases.
foreach ($file in $sourceFiles) {
    $relative = $file.FullName.Substring($InstallDir.Length).TrimStart('\', '/')
    $destination = Join-Path $gameCopy $relative
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or
        (Get-Item -LiteralPath $destination).Length -ne $file.Length) {
        throw 'A source file was not copied completely. Nothing was launched; the original is unchanged.'
    }
}
foreach ($key in @($before.Keys | Where-Object { $_.StartsWith('game/') })) {
    if ($before[$key] -eq '<absent>') { continue }
    $copied = Join-Path $gameCopy $key.Substring(5)
    if ((Get-FileHash -LiteralPath $copied -Algorithm SHA256).Hash -ne $before[$key]) {
        throw 'A runtime binary or setting failed copy verification. Nothing was launched.'
    }
}
# Runtime overlays arrive only after the complete original copy and protected hashes passed.
Copy-VerifiedRuntimeAssets $verifiedRuntimeAssets $gameCopy $InstallDir
function Copy-PrivateSave([IO.FileInfo]$file, [string]$sourceKey, [string]$destinationName) {
    $destination = Join-Path $savesCopy $destinationName
    if (Test-Path -LiteralPath $destination -PathType Leaf) { return }
    Copy-Item -LiteralPath $file.FullName -Destination $destination
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $before[$sourceKey]) {
        throw 'A private save copy failed verification. Nothing was launched.'
    }
}
if (Test-Path -LiteralPath $normalPref -PathType Container) {
    Get-ChildItem -LiteralPath $normalPref -File | Where-Object {
        $_.Name -match '^(savegame\.\d+(?:\.enh)?|OPENCNC\.IDX|HALLFAME\.DAT)$'
    } | ForEach-Object { Copy-PrivateSave $_ ('saves/' + $_.Name) $_.Name }
}
# Match the normal app's one-time carry-over, while writing only the private copies.
$normalIndexOrFame = (Test-Path -LiteralPath (Join-Path $normalPref 'OPENCNC.IDX') -PathType Leaf) -or
                     (Test-Path -LiteralPath (Join-Path $normalPref 'HALLFAME.DAT') -PathType Leaf)
if (-not $normalIndexOrFame -and (Test-Path -LiteralPath $legacyPref -PathType Container)) {
    Get-ChildItem -LiteralPath $legacyPref -File | Where-Object {
        $_.Name -match '^(savegame\.\d+(?:\.enh)?|CNC3DSAV\.IDX|HALLFAME\.DAT)$'
    } | ForEach-Object {
        $name = if ($_.Name -ieq 'CNC3DSAV.IDX') { 'OPENCNC.IDX' } else { $_.Name }
        Copy-PrivateSave $_ ('legacy-saves/' + $_.Name) $name
    }
}
foreach ($name in $binaryNames) {
    $binarySource = Join-Path (Join-Path $bundleRoot 'binaries') $name
    Copy-Item -LiteralPath $binarySource -Destination (Join-Path $gameCopy $name) -Force
    if ((Get-FileHash -LiteralPath (Join-Path $gameCopy $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne
        $manifest.binaries.PSObject.Properties[$name].Value.sha256) {
        throw 'The private diagnostic executable failed its final digest check.'
    }
}
# Source read-only flags must not route game logging back into the original user folder.
# Change only private metadata, then prove the copied log directory is writable.
foreach ($folder in @($gameCopy, $savesCopy)) {
    Get-ChildItem -LiteralPath $folder -File | Where-Object {
        $_.Name -match '\.cfg$' -or $folder -eq $savesCopy
    } | ForEach-Object { $_.Attributes = $_.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly) }
}
$privateLog = Join-Path $gameCopy 'opencnc3d-log.txt'
if (Test-Path -LiteralPath $privateLog -PathType Leaf) { Remove-Item -LiteralPath $privateLog -Force }
[IO.File]::WriteAllText($privateLog, '', (New-Object Text.UTF8Encoding($false)))

$hardware = [ordered]@{ collected_at = (Get-Date).ToString('o'); notes = @() }
try {
    $hardware.os = @(Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture)
    $hardware.cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, AddressWidth, DataWidth)
    $hardware.ram_bytes = [long]((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory)
    $hardware.gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, DriverDate,
        CurrentHorizontalResolution, CurrentVerticalResolution, CurrentRefreshRate)
} catch { $hardware.notes += 'Some non-identifying hardware fields were unavailable.' }
$hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDir 'hardware.json') -Encoding UTF8
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDir 'build.json') -Encoding UTF8
$selectedLibraries | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reportDir 'library-fingerprints.json') -Encoding UTF8
$settings = Join-Path $gameCopy 'opencnc3d-fx.cfg'
if (Test-Path -LiteralPath $settings -PathType Leaf) {
    Copy-Item -LiteralPath $settings -Destination (Join-Path $reportDir 'visual-settings-before.cfg')
}
$sourceSummary = [ordered]@{
    mode = $modeName
    game_file_count = $sourceFiles.Count
    game_bytes = $gameBytes
    original_binary_sha256 = [ordered]@{
        'opencnc3d-game.exe' = $before['game/opencnc3d-game.exe']
        'cnc_eyes.exe' = $before['game/cnc_eyes.exe']
        'TiberianDawn.dll' = $before['game/TiberianDawn.dll']
        'SDL2.dll' = $before['game/SDL2.dll']
    }
    forced_quality_or_vsync_changes = $false
}
$sourceSummary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reportDir 'session.json') -Encoding UTF8

function Get-AssetFingerprints([string]$runtimeRoot) {
    $records = @()
    foreach ($group in @(
        @('ebld', '*.ebld'), @('einf', '*.c3m'), @('models3d', '*.c3m')
    )) {
        $folder = Join-Path $runtimeRoot $group[0]
        if (-not (Test-Path -LiteralPath $folder -PathType Container)) { continue }
        foreach ($file in @(Get-ChildItem -LiteralPath $folder -File -Filter $group[1] | Sort-Object Name)) {
            $records += [ordered]@{ path = $group[0] + '/' + $file.Name; size = $file.Length;
                sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        }
    }
    foreach ($relative in @('efx.pack', 'efx_enh.pack', 'tib3d.pack', 'tib3d_rings.pack',
                           'ebld/manifest.json', 'einf/manifest.json', 'models3d/manifest.json', 'uvfx/manifest.json',
                           'BUILD-ID.txt', 'opencnc3d-install.txt', 'ebld/FACT.ebld.fclv')) {
        $path = Join-Path $runtimeRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $file = Get-Item -LiteralPath $path
        $records += [ordered]@{ path = $relative; size = $file.Length;
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    return [ordered]@{ format = 1; asset_bytes_included = $false; asset_version_is_a_rejection_gate = $false;
        file_count = $records.Count; files = $records }
}
# Read/hash once before gameplay, outside measured frames. Different legitimate assets
# are recorded rather than rejected. Relative names identify revisions without copying art.
Get-AssetFingerprints $gameCopy | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $reportDir 'asset-fingerprints.json') -Encoding UTF8

function Quote-WindowsArgument([string]$value) {
    # Windows CRT parsing: double backslashes before a quote or the closing quote.
    return '"' + [regex]::Replace([regex]::Replace($value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
$info = New-Object System.Diagnostics.ProcessStartInfo
$info.FileName = Join-Path $gameCopy 'opencnc3d-game.exe'
$info.WorkingDirectory = $gameCopy
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$argsList = @('--dir', 'missions/', '--content', 'content/',
              '--savedir', '../saves', '--famefile', '../saves/HALLFAME.DAT')
$info.Arguments = ($argsList | ForEach-Object { Quote-WindowsArgument $_ }) -join ' '
foreach ($name in @('OPENCNC3D_FRAMEPROF', 'OPENCNC3D_GPUPROF', 'OPENCNC3D_PERFDETAIL',
                    'OPENCNC3D_BACKGROUND', 'OPENCNC3D_PERFTRACE')) {
    $info.EnvironmentVariables.Remove($name)
}
if (-not $Control) { $info.EnvironmentVariables['OPENCNC3D_PERFTRACE'] = 'performance-trace/frames.csv' }
Write-Host ''
Write-Host 'First capture 30 seconds at normal settings, then return to the menu to preserve that baseline.'
Write-Host 'Play for 3-5 minutes to reproduce the slowdown or building destruction. Return to the menu or quit within 30 seconds of it.'
Write-Host 'Keep your usual Enhanced settings for the first run. Do not change several settings together.'
Write-Host ('Local results: ' + $session)
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $info
$watch = [Diagnostics.Stopwatch]::StartNew()
$samplePath = Join-Path $reportDir 'process-memory.csv'
$writer = New-Object IO.StreamWriter ($samplePath, $false, (New-Object Text.UTF8Encoding($false)))
$writer.WriteLine('elapsed_s,working_set_bytes,private_bytes,virtual_bytes,cpu_total_ms,cpu_delta_ms')
$started = $false
$lastCpu = 0.0
try {
    $started = $process.Start()
    if (-not $started) { throw 'The diagnostic game could not start.' }
    while (-not $process.WaitForExit(1000)) {
        try {
            $process.Refresh()
            $cpu = $process.TotalProcessorTime.TotalMilliseconds
            $line = [string]::Format([Globalization.CultureInfo]::InvariantCulture,
                '{0:F3},{1},{2},{3},{4:F3},{5:F3}', [object[]]@($watch.Elapsed.TotalSeconds,
                $process.WorkingSet64, $process.PrivateMemorySize64, $process.VirtualMemorySize64, $cpu, ($cpu - $lastCpu)))
            $writer.WriteLine($line)
            $lastCpu = $cpu
            if ([int]$watch.Elapsed.TotalSeconds % 10 -eq 0) { $writer.Flush() }
        } catch {
            if ($process.HasExited) { break }
            throw
        }
    }
    $process.WaitForExit()
    $exitCode = $process.ExitCode
} finally {
    $writer.Dispose()
    $watch.Stop()
}
if (-not $started) { throw 'Nothing was launched.' }

$after = Get-ProtectedSnapshot
$allKeys = @(@($before.Keys) + @($after.Keys) | Sort-Object -Unique)
$changed = @($allKeys | Where-Object { $before[$_] -ne $after[$_] })
[ordered]@{ original_files_checked = $allKeys.Count; original_files_unchanged = ($changed.Count -eq 0);
    changed_file_count = $changed.Count; game_exit_code = $exitCode; elapsed_seconds = $watch.Elapsed.TotalSeconds
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportDir 'original-protection.json') -Encoding UTF8
if (Test-Path -LiteralPath $settings -PathType Leaf) {
    Copy-Item -LiteralPath $settings -Destination (Join-Path $reportDir 'visual-settings-after.cfg')
}
Get-ChildItem -LiteralPath $traceDir -File | Where-Object {
    $_.Name -match '^frames-mission-\d+\.csv(?:\.settings-(?:start|end)\.cfg)?$'
} | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $reportDir $_.Name) }
$rawLog = Join-Path $gameCopy 'opencnc3d-log.txt'
if (Test-Path -LiteralPath $rawLog -PathType Leaf) {
    # Keep diagnostics relevant to performance. Full gameplay/network logs remain local.
    $prefixes = '^(OpenCNC3D |GL_VERSION|GL_RENDERER|GL_SWAP|GL_GOT|GL_MULTITEX|GL WARNING|RESIZE\||PERFTRACE\||PERFWARM\||STATS\||GPUPROF\||GPUFRAMEPROF\||FRAMEPROF\||FRAMESPIKE\||FATAL:|FX\|graphics mode\||FX\|enhanced\||FX\|filter\||EBLDMESH\||EBLDART\||EBLD\|(?:wreck|damaged|sink)\||ebld: (?:[A-Za-z0-9_]+ (?:cut meshes built|textures on the card)|(?:ebld[\\/])?[A-Za-z0-9_]+\.ebld read at first sight)|M3(?:E)?\|[A-Za-z0-9_]+\|(?:tris |gpu\|)|M3\|loaded\||ran )'
    $log = ([IO.File]::ReadAllLines($rawLog) | Where-Object { $_ -match $prefixes }) -join "`r`n"
    foreach ($prefix in @($gameCopy, $session, $InstallDir, $normalPref, $env:USERPROFILE) | Sort-Object Length -Descending) {
        if ($prefix) {
            $log = $log.Replace($prefix, '<local-path>').Replace($prefix.Replace('\', '/'), '<local-path>')
        }
    }
    [IO.File]::WriteAllText((Join-Path $reportDir 'performance-log.txt'), $log, (New-Object Text.UTF8Encoding($false)))
}
@'
Describe the map/mission, normal FPS, lowest FPS, and what happened when it slowed down:

If you changed one Visuals option, record its name and the approximate minute:

Was VSync originally on, off, or adaptive? Which display mode and monitor refresh rate?

Were there noticeable pauses in camera motion, audio or unit movement?

'@ | Set-Content -LiteralPath (Join-Path $reportDir 'player-notes.txt') -Encoding UTF8
$zipPath = Join-Path $session ('OpenCNC3D-Windows-' + $modeName + '-report.zip')
Compress-Archive -Path (Join-Path $reportDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
Write-Host ''
Write-Host ('Finished. Review report files and add observations to: ' + (Join-Path $reportDir 'player-notes.txt'))
Write-Host 'The ZIP contains hardware fields, timings, copied visual settings and filtered performance log lines.'
Write-Host 'It contains no save games, user-map files, serial numbers or environment dump. Review it before sharing.'
Write-Host ('Report ZIP: ' + $zipPath)
Write-Host 'If you edit player-notes.txt, right-click the report folder and ZIP it again before sharing.'
if ($changed.Count -gt 0) {
    Write-Warning 'An original protected file changed during the run. This can happen if another game process was running. See original-protection.json; the script did not write those paths.'
}
if (-not $Control -and @(Get-ChildItem -LiteralPath $reportDir -Filter 'frames-mission-*.csv' -File).Count -eq 0) {
    Write-Warning 'No frame trace was produced. Keep the report anyway; check the game log before rerunning.'
}
