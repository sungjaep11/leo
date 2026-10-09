param(
    [ValidateSet('all','smoke','dynamic','congestion','sharing')]
    [string]$Suite = 'all',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]*$')]
    [string]$OutputName = (Get-Date -Format 'yyyyMMdd-HHmmss'),
    [ValidateRange(1,16)]
    [int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$bundleDir = $PSScriptRoot
$environment = Get-Content -LiteralPath (Join-Path $bundleDir 'environment.json') -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath (Join-Path $bundleDir "output/$OutputName")) {
    throw 'Output already exists. Choose a new -OutputName.'
}
& docker info --format '{{.ServerVersion}}'
if ($LASTEXITCODE -ne 0) { throw 'Start Docker Desktop and run this command again.' }
# Only this directory is mounted; existing lab containers and volumes are not used.
& docker run --rm --entrypoint python3 --mount "type=bind,source=$bundleDir,target=/study" $environment.image /study/reproduce.py --suite $Suite --output "/study/output/$OutputName" --jobs $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Reproduction failed. See output logs; completed results were preserved.' }
Write-Host "Results: $bundleDir/output/$OutputName"
