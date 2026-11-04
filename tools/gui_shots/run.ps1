# run.ps1 - regenerate the user-guide screenshots (docs/guide/images).
#
# Reads every docs/guide/images/manifest_*.json and runs build\gui_shots.exe
# once per entry that has a "script" array (one fresh process per screenshot,
# so every shot starts from default settings). See README.md in this folder.
#
# Usage (PowerShell 5.1):
#   powershell -ExecutionPolicy Bypass -File tools\gui_shots\run.ps1
#   ... -Filter "09-gpu-*"        # only entries whose file name matches
#   ... -Lang zh                  # only one language
#   ... -Jobs 4                   # run up to 4 shots in parallel
#   ... -List                     # print the entries that would run
#
# Build the tool first (it is off by default):
#   $env:RIPES_BUILD_GUI_SHOTS = "ON"
#   powershell -ExecutionPolicy Bypass -File P:\workspace\soc\build.ps1 -Configure -Target gui_shots

[CmdletBinding()]
param(
    [string]$Filter = "*",
    [ValidateSet("", "zh", "en")]
    [string]$Lang = "",
    [int]$Jobs = 1,
    [switch]$List,
    [string]$Repo = "",               # default: the checkout containing this script
    [string]$QtBin = "P:\workspace\soc\toolchain\Qt\6.8.3\mingw_64\bin"
)

$ErrorActionPreference = "Continue"
if ($Repo -eq "") { $Repo = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\..")).Path }
$exe = Join-Path $Repo "build\gui_shots.exe"
$imgDir = Join-Path $Repo "docs\guide\images"
if (-not (Test-Path $exe)) { throw "gui_shots.exe not found: $exe (build the gui_shots target first)" }

# Session-only environment.
if (Test-Path $QtBin) { $env:PATH = $QtBin + ";" + $env:PATH }
$env:QT_QPA_PLATFORM = "offscreen"
if (-not $env:QT_QPA_FONTDIR -and (Test-Path "C:\Windows\Fonts")) { $env:QT_QPA_FONTDIR = "C:\Windows\Fonts" }
$env:GUI_SHOTS_REPO = $Repo

# Collect the work list.
$work = @()
$archWork = @()   # entries rendered with tools/arch_render instead of the GUI
foreach ($m in Get-ChildItem -Path $imgDir -Filter "manifest_*.json" | Sort-Object Name) {
    $entries = Get-Content -Raw -Encoding UTF8 $m.FullName | ConvertFrom-Json
    foreach ($e in $entries) {
        if ($e.file -notlike $Filter) { continue }
        if ($Lang -ne "" -and $e.lang -ne $Lang) { continue }
        if ($null -ne $e.arch_render) {
            $archWork += [pscustomobject]@{ File = $e.file; Spec = $e.arch_render }
            continue
        }
        if ($null -eq $e.script) {
            Write-Host ("skip (no script): {0}" -f $e.file) -ForegroundColor DarkGray
            continue
        }
        $work += [pscustomobject]@{ Manifest = $m.FullName; File = $e.file }
    }
}

if ($List) { $work + $archWork | Format-Table -AutoSize | Out-String | Write-Host; exit 0 }

# Diagrams rendered by tools/arch_render (build it with: bash tools/arch_render/build.sh).
# The topology JSON is the first ```json block after the given heading of a chapter.
$archExe = Join-Path $Repo "build\arch_render\arch_render.exe"
foreach ($a in $archWork) {
    if (-not (Test-Path $archExe)) {
        Write-Host ("skip {0}: {1} not built (bash tools/arch_render/build.sh)" -f $a.File, $archExe) -ForegroundColor DarkGray
        continue
    }
    $lines = Get-Content -Encoding UTF8 (Join-Path $Repo $a.Spec.json_from)
    $start = [Array]::IndexOf($lines, ($lines | Where-Object { $_.StartsWith($a.Spec.heading) } | Select-Object -First 1))
    $json = @(); $in = $false
    for ($i = $start; $i -ge 0 -and $i -lt $lines.Count; $i++) {
        if (-not $in -and $lines[$i] -eq '```json') { $in = $true; continue }
        if ($in -and $lines[$i] -eq '```') { break }
        if ($in) { $json += $lines[$i] }
    }
    if ($json.Count -eq 0) { Write-Host ("[ERR] {0}: no json block after '{1}'" -f $a.File, $a.Spec.heading) -ForegroundColor Red; $failedArch = $true; continue }
    $tmpJson = Join-Path ([System.IO.Path]::GetTempPath()) "gui_shots_arch_example.json"
    [System.IO.File]::WriteAllLines($tmpJson, [string[]]$json, (New-Object System.Text.UTF8Encoding($false)))
    $out = Join-Path $imgDir $a.File
    & $archExe @($a.Spec.args) $tmpJson $out | Out-Null
    $status = if ($LASTEXITCODE -eq 0) { "ok " } else { "ERR" }
    Write-Host ("[{0}] {1} (arch_render {2})" -f $status, $a.File, ($a.Spec.args -join " ")) -ForegroundColor $(if ($LASTEXITCODE -eq 0) { "Green" } else { "Red" })
    if ($LASTEXITCODE -ne 0) { $failedArch = $true }
    Remove-Item $tmpJson -ErrorAction SilentlyContinue
}
Write-Host ("{0} screenshot(s) to generate" -f $work.Count)

$failed = @()
$running = @()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$logDir = Join-Path ([System.IO.Path]::GetTempPath()) ("gui_shots_logs_" + $PID)
New-Item -ItemType Directory -Force $logDir | Out-Null

function Wait-Slot([int]$max) {
    while (@($script:running | Where-Object { -not $_.Proc.HasExited }).Count -ge $max) { Start-Sleep -Milliseconds 200 }
    foreach ($r in @($script:running | Where-Object { $_.Proc.HasExited })) {
        $r.Proc.WaitForExit()
        $code = $r.Proc.ExitCode
        $status = if ($code -eq 0) { "ok " } else { "ERR" }
        $color = if ($code -eq 0) { "Green" } else { "Red" }
        Write-Host ("[{0}] {1}" -f $status, $r.File) -ForegroundColor $color
        if ($code -ne 0) {
            Get-Content $r.Log | ForEach-Object { Write-Host ("      " + $_) }
            Get-Content $r.Err | ForEach-Object { Write-Host ("      " + $_) }
            $script:failed += $r.File
        }
        $script:running = @($script:running | Where-Object { $_ -ne $r })
    }
}

foreach ($w in $work) {
    Wait-Slot $Jobs
    $log = Join-Path $logDir ($w.File + ".out.txt")
    $err = Join-Path $logDir ($w.File + ".err.txt")
    $argList = @("--manifest", ('"' + $w.Manifest + '"'), "--entry", $w.File)
    $p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $Repo `
        -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError $err
    $null = $p.Handle   # PS 5.1: keeps ExitCode readable after the process exits
    $running += [pscustomobject]@{ Proc = $p; File = $w.File; Log = $log; Err = $err }
}
Wait-Slot 1

$sw.Stop()
$pngs = Get-ChildItem -Path $imgDir -Filter "*.png"
$total = ($pngs | Measure-Object -Property Length -Sum).Sum
Write-Host ""
Write-Host ("done in {0:n0} s; {1} failed; docs/guide/images: {2} PNG, {3:n2} MB" -f `
    $sw.Elapsed.TotalSeconds, $failed.Count, $pngs.Count, ($total / 1MB))
Remove-Item -Recurse -Force $logDir -ErrorAction SilentlyContinue
if ($failedArch) { $failed += "arch_render" }
if ($failed.Count -gt 0) { Write-Host ("failed: " + ($failed -join ", ")) -ForegroundColor Red; exit 1 }
exit 0
