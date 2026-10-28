# Tiny PowerShell wrapper around the real runner (runner/run_suite.py), so the
# suite can be started from a PowerShell prompt without typing "python " first.
# All arguments are forwarded, e.g.:
#   .\run.ps1 --only compute-single
#   .\run.ps1 --seconds 5 --concurrency 16
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $SuiteArgs
)
python (Join-Path $PSScriptRoot "run_suite.py") @SuiteArgs
exit $LASTEXITCODE
