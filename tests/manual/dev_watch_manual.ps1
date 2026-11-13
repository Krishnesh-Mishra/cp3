# =============================================================================
# tests/manual/dev_watch_manual.ps1 - manual end-to-end test for `cp3c dev`.
# Builds a scratch project under $env:TEMP\cp3-dev-watch-<pid> (a .cp3 app
# importing a sibling library, printing "Hello, N!" then looping), starts
# `cp3c dev --verbose`, and drives five edit scenarios: old child survives
# failing builds, no-change saves do not restart, good builds do.
# Requirements: PowerShell 7+, finished Windows release build at
# build\windows-release\bin\cp3c.exe (do NOT run while cp3c is building).
# Never touches the repo working tree; artifacts live in the scratch dir and
# are removed on exit. Idempotent (unique per PID).
# Markers relied on (cp3c's stderr unless noted):
#   "Hello, N!"                        child app stdout (the only app output)
#   "cp3c: running "                   child was (re)started (diagnostic only)
#   "cp3c: linking failed"             link failed (e.g. exe image is running)
#   "cp3c: build reported problems"    build failed; CP3 codes precede it
# Exit code: 0 when every test passes, 1 otherwise, 2 on environment errors.
# =============================================================================
#requires -Version 7.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cp3c = Join-Path $repo 'build\windows-release\bin\cp3c.exe'
if (-not (Test-Path $cp3c)) {
    Write-Host "error: cp3c.exe not found at $cp3c (build the release tree first)"
    exit 2
}

$scratch   = Join-Path $env:TEMP "cp3-dev-watch-$PID"
$srcDir    = Join-Path $scratch 'src'
$libDir    = Join-Path $scratch 'lib'
$entryPath = Join-Path $scratch 'src\index.cp3'
$utilPath  = Join-Path $scratch 'lib\util.cp3'
$exePath   = Join-Path $scratch '.cp3\out\index.exe'
$outLog    = Join-Path $scratch 'dev.out.log'
$errLog    = Join-Path $scratch 'dev.err.log'

# --- Scratch project templates ---------------------------------------------
# The app prints one line then loops forever so the child stays alive (this
# is what long-running apps under dev look like); every test then detects
# restarts via new "Hello, N!" lines, exe mtime and child PID.
function Get-EntryText([string]$prefix) {
    "import console from `"std/console`";`n" +
    "import { answer } from `"../lib/util.cp3`";`n" +
    "`n" +
    'console.log(`' + $prefix + ', ${answer()}!`);' + "`n" +
    "`n" +
    "setInterval(() => {}, 1000);`n"
}
function Get-UtilText([int]$constant) {
    "export function answer(): number {`n    return $constant;`n}`n"
}
# Deliberately unparsable: a missing parameter name is a hard syntax error.
$BrokenUtil = "export function answer(: number {`n    return 1;`n}`n"
function Write-Utf8NoBom([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))
}

# --- Log / process helpers --------------------------------------------------
function Get-CombinedLog {
    $text = ''
    foreach ($f in @($outLog, $errLog)) {
        if (Test-Path $f) { $text += [System.IO.File]::ReadAllText($f) + "`n" }
    }
    return $text
}
function Count-LogOccurrences([string]$needle) {
    return ([regex]::Matches((Get-CombinedLog), [regex]::Escape($needle))).Count
}
function Wait-ForLogCount([string]$needle, [int]$minCount, [int]$timeoutSec) {
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSec)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Count-LogOccurrences $needle) -ge $minCount) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}
function Get-ScratchExePid {
    # Match by image path, not name: another cp3 project's index.exe may run.
    $procs = Get-CimInstance Win32_Process -Filter "Name = 'index.exe'" -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
        if ($p.ExecutablePath -and
            $p.ExecutablePath.StartsWith($scratch, [StringComparison]::OrdinalIgnoreCase)) {
            return $p.ProcessId
        }
    }
    return $null
}
function Get-ExeMtime { return ([System.IO.File]::GetLastWriteTimeUtc($exePath)).ToString('o') }
function Kill-ScratchProcesses {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($scratch,
            [StringComparison]::OrdinalIgnoreCase)) -or
        ($_.Name -eq 'cp3c.exe' -and $_.CommandLine -and
            $_.CommandLine.IndexOf($scratch, [StringComparison]::OrdinalIgnoreCase) -ge 0)
    } | ForEach-Object { try { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } catch {} }
}

# --- Test bookkeeping ---------------------------------------------------------
$results = New-Object System.Collections.Generic.List[object]
function Add-Result([string]$Name, [bool]$Pass, [string]$Detail, [double]$Seconds) {
    $results.Add([pscustomobject]@{ Name = $Name; Pass = $Pass; Detail = $Detail; Seconds = $Seconds })
    Write-Host ("{0,-44} {1}  ({2:N1}s)  {3}" -f $Name, $(if ($Pass) { 'PASS' } else { 'FAIL' }),
        $Seconds, $Detail)
}

# --- Setup: scratch dir ------------------------------------------------------
New-Item -ItemType Directory -Force -Path $srcDir, $libDir | Out-Null
Write-Utf8NoBom (Join-Path $scratch 'package.json') @"
{
    "name": "cp3-dev-watch-manual",
    "version": "1.0.0",
    "type": "application",
    "main": "src/index.cp3",
    "typeSystem": "typescript"
}
"@
Write-Utf8NoBom $entryPath (Get-EntryText 'Hello')
Write-Utf8NoBom $utilPath  (Get-UtilText 41)
[System.IO.File]::Delete($outLog); [System.IO.File]::Delete($errLog)

$dev = $null
try {
    # --- Start `cp3c dev` and wait for the first good run -------------------
    $t0 = [DateTime]::UtcNow
    $dev = Start-Process -FilePath $cp3c `
        -ArgumentList @('dev', '--verbose', 'src/index.cp3') `
        -WorkingDirectory $scratch `
        -RedirectStandardOutput $outLog -RedirectStandardError $errLog `
        -PassThru -NoNewWindow
    $ready = Wait-ForLogCount 'Hello, 41!' 1 60
    $appPid = $null
    if ($ready) {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while (-not $appPid -and [DateTime]::UtcNow -lt $deadline) {
            $appPid = Get-ScratchExePid; Start-Sleep -Milliseconds 200
        }
    }
    if (-not $ready -or -not $appPid) {
        Add-Result 'prerequisite: first build prints Hello, 41!' $false `
            ("log:`n" + (Get-CombinedLog).Trim()) (([DateTime]::UtcNow - $t0).TotalSeconds)
        foreach ($r in $results) {
            Write-Host ("{0,-44} {1}" -f $r.Name, $(if ($r.Pass) { 'PASS' } else { 'FAIL' }))
        }
        Write-Host ("TOTAL: {0}/{1} passed" -f 0, $results.Count)
        exit 1
    }
    Add-Result 'prerequisite: first build prints Hello, 41!' $true "app pid $appPid" `
        (([DateTime]::UtcNow - $t0).TotalSeconds)

    # === TEST 1: edited library is rebuilt; old child survives the rebuild ===
    $t1 = [DateTime]::UtcNow
    $exeBefore = Get-ExeMtime
    Write-Utf8NoBom $utilPath (Get-UtilText 42)
    Start-Sleep -Milliseconds 50
    $aliveAt50 = [bool](Get-Process -Id $appPid -ErrorAction SilentlyContinue)
    Start-Sleep -Milliseconds 100
    $aliveAt150 = [bool](Get-Process -Id $appPid -ErrorAction SilentlyContinue)
    $restarted = Wait-ForLogCount 'Hello, 42!' 2 90
    $elapsed = ([DateTime]::UtcNow - $t1).TotalSeconds
    $log = Get-CombinedLog
    $newPid = Get-ScratchExePid
    $restarted = $restarted -and ($newPid -ne $null) -and ($newPid -ne $appPid)
    $errs = @()
    if (-not $aliveAt50)  { $errs += 'old child NOT running at t+50ms after edit' }
    if (-not $aliveAt150) { $errs += 'old child NOT running at t+150ms after edit' }
    if ((Count-LogOccurrences 'Hello, 42!') -lt 2) { $errs += 'no restart output for constant 42' }
    if ($log -match 'build reported problems|linking failed') {
        $errs += 'build failed (see log; on Windows the running exe image cannot be overwritten)'
    }
    Add-Result 'TEST 1 restart on imported-module edit' `
        (($errs.Count -eq 0) -and $restarted) `
        $(if ($errs.Count) { $errs -join '; ' } else { "pid $appPid -> $newPid, exe mtime $exeBefore -> $(Get-ExeMtime); spawns=$(Count-LogOccurrences 'cp3c: running')" }) `
        $elapsed
    if ($restarted) { $appPid = $newPid }

    # === TEST 2: identical rewrite of the entry is not a change =============
    $t2 = [DateTime]::UtcNow
    $exeBefore = Get-ExeMtime
    $rebuildsBefore = Count-LogOccurrences 'rebuilding...'
    $bytes = [System.IO.File]::ReadAllBytes($entryPath)   # rewrite identical bytes (mtime only)
    [System.IO.File]::WriteAllBytes($entryPath, $bytes)
    Start-Sleep -Seconds 4   # debounce (25ms) + rebuild window; coarse fs safe
    $exeAfter = Get-ExeMtime
    $rebuildsAfter = Count-LogOccurrences 'rebuilding...'
    $noRestart = ($exeBefore -eq $exeAfter) -and ($rebuildsAfter -eq $rebuildsBefore)
    $verboseNoChange = if (Get-CombinedLog -match 'no source content changed') { ' (verbose: no-change marker seen)' } else { '' }
    $detail = "exe mtime $(if ($exeBefore -eq $exeAfter) { 'unchanged' } else { 'CHANGED' }); " +
              "rebuild lines $rebuildsBefore -> $rebuildsAfter$verboseNoChange"
    Add-Result 'TEST 2 no-change save does not restart' $noRestart $detail `
        (([DateTime]::UtcNow - $t2).TotalSeconds)

    # === TEST 3: broken build keeps the old app; restore recovers ===========
    $t3 = [DateTime]::UtcNow
    Write-Utf8NoBom $utilPath $BrokenUtil
    $brokeSeen = Wait-ForLogCount 'build reported problems' 1 90
    $appPidAfterErr = Get-ScratchExePid
    $oldAlive = $brokeSeen -and [bool](Get-Process -Id $appPidAfterErr -ErrorAction SilentlyContinue) -and ($appPidAfterErr -eq $appPid)
    $lastHello = (Get-CombinedLog -split "`n" | Where-Object { $_ -match 'Hello,' } | Select-Object -Last 1)
    $predates = $brokeSeen -and ($lastHello -match 'Hello, 42!') -and
                ((Count-LogOccurrences 'Hello, 42!') -eq 2)
    # Restore the good library: content differs from the failed build's baseline.
    Write-Utf8NoBom $utilPath (Get-UtilText 42)
    $recovered = Wait-ForLogCount 'Hello, 42!' 3 90
    $elapsed3 = ([DateTime]::UtcNow - $t3).TotalSeconds
    $t3ok = $brokeSeen -and $oldAlive -and $predates -and $recovered
    $p3note = @()
    if (-not $brokeSeen) { $p3note += 'no build-error line seen after the break' }
    if (-not $oldAlive)  { $p3note += "old child pid $appPid not alive after the break" }
    if (-not $predates)  { $p3note += "last Hello line was '$lastHello', expected pre-break 'Hello, 42!'" }
    if (-not $recovered) { $p3note += 'no Hello, 42! after restore' }
    Add-Result 'TEST 3 syntax error keeps old app; restore restarts' $t3ok `
        $(if ($p3note.Count) { $p3note -join '; ' } else { "error seen, pid $appPid kept, restore restarted" }) `
        $elapsed3
    if ($recovered) { $appPid = Get-ScratchExePid }

    # === TEST 4: imported module edit restarts (named case of TEST 1) =======
    $t4 = [DateTime]::UtcNow
    Write-Utf8NoBom $utilPath (Get-UtilText 43)
    $ok4 = Wait-ForLogCount 'Hello, 43!' 1 90
    $newPid4 = Get-ScratchExePid
    $restart4 = $ok4 -and $newPid4 -and ($newPid4 -ne $appPid)
    Add-Result 'TEST 4 edit to imported lib/util.cp3 restarts' $restart4 `
        $(if ($restart4) { "pid $appPid -> $newPid4; spawns=$(Count-LogOccurrences 'cp3c: running')" } else { 'no new Hello, 43! line or same pid' }) `
        (([DateTime]::UtcNow - $t4).TotalSeconds)
    if ($restart4) { $appPid = $newPid4 }

    # === TEST 5: entry edit restarts with the new string ====================
    $t5 = [DateTime]::UtcNow
    Write-Utf8NoBom $entryPath (Get-EntryText 'Renamed')
    $ok5 = Wait-ForLogCount 'Renamed, 43!' 1 90
    $newPid5 = Get-ScratchExePid
    $restart5 = $ok5 -and $newPid5 -and ($newPid5 -ne $appPid)
    Add-Result 'TEST 5 entry edit restarts with new log string' $restart5 `
        $(if ($restart5) { "pid $appPid -> $newPid5; spawns=$(Count-LogOccurrences 'cp3c: running')" } else { 'no Renamed, 43! line or same pid' }) `
        (([DateTime]::UtcNow - $t5).TotalSeconds)

    # === Summary ==============================================================
    Write-Host ""
    Write-Host "===== dev watch manual test summary ====="
    foreach ($r in $results) {
        Write-Host ("{0,-44} {1}" -f $r.Name, $(if ($r.Pass) { 'PASS' } else { 'FAIL' }))
    }
    $failed = @($results | Where-Object { -not $_.Pass }).Count
    Write-Host ("TOTAL: {0}/{1} passed" -f ($results.Count - $failed), $results.Count)
    exit $(if ($failed -eq 0) { 0 } else { 1 })
}
finally {
    # Always kill dev + the app (it spawns in its own process group) and sweep
    # the scratch dir. The repo working tree is never touched.
    if ($dev) {
        try { Stop-Process -Id $dev.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
    Kill-ScratchProcesses
    try { Remove-Item -Recurse -Force $scratch -ErrorAction Stop } catch {
        Write-Host "warning: could not remove $scratch (locked by a live process?)"
    }
}
