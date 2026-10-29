param([string]$Runtime = 'win-x64')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'windows-agent/SysmonHost.csproj'
$destination = Join-Path $root 'windows-agent/publish'
& dotnet publish $project -c Release -r $Runtime --self-contained false -o $destination -p:RestoreLockedMode=true
if ($LASTEXITCODE -ne 0) { throw 'Windows agent build failed' }
Write-Host 'Windows agent built. In Ubuntu, run: chmod +x windows-agent/publish/sysmon-host.exe'
