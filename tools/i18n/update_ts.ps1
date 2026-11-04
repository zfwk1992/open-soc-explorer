# Thin wrapper: python tools/i18n/update_ts.py <args>  (uses the toolchain venv if present)
$py = "P:\workspace\soc\toolchain\venv\Scripts\python.exe"
if (-not (Test-Path $py)) { $py = "python" }
& $py (Join-Path $PSScriptRoot "update_ts.py") @args
exit $LASTEXITCODE
