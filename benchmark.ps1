[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDirectory = $PSScriptRoot
$ComposeFile = Join-Path $ProjectDirectory 'compose.yaml'
$Docker = (Get-Command docker -ErrorAction Stop).Source

function Invoke-Compose {
    param([Parameter(Mandatory)][string[]] $Arguments)

    & $Docker compose -f $ComposeFile @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "docker compose $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

Push-Location $ProjectDirectory
try {
    Invoke-Compose -Arguments @('down', '-v', '--remove-orphans')
    Invoke-Compose -Arguments @('build')
    Invoke-Compose -Arguments @('up', '-d', '--wait')
    Invoke-Compose -Arguments @(
        'exec', '-T', 'postgres',
        'psql', '-X', '-v', 'ON_ERROR_STOP=1',
        '-U', 'postgres', '-d', 'mmddyyyy_lab',
        '-f', '/project/lab/compare-indexes.sql'
    )
}
finally {
    & $Docker compose -f $ComposeFile down -v --remove-orphans
    Pop-Location
}