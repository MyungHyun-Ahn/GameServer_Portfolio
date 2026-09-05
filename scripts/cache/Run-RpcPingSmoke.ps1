param(
    [int]$Port = 19103,
    [int]$ShardCount = 4,
    [int]$PingCount = 16,
    [ValidateSet("iocp", "rio-direct", "rio-owner")]
    [string]$Backend = "iocp",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$serverProject = Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Cache\CacheRpcPingClient\CacheRpcPingClient.vcxproj"
$serverExecutable = Join-Path $repositoryRoot "Out\CacheServer\$Configuration\CacheServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\CacheRpcPingClient\$Configuration\CacheRpcPingClient.exe"
$cacheServerConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigHelper = Join-Path $scriptsRoot "common\ServerConfig.ps1"

if (-not (Test-Path -LiteralPath $serverConfigHelper -PathType Leaf))
{
    throw "Server config helper is missing: $serverConfigHelper"
}
. $serverConfigHelper

if ($Port -le 0 -or $Port -gt 65535)
{
    throw "Port must be in range 1..65535."
}
if ($ShardCount -le 0 -or $ShardCount -gt 64)
{
    throw "ShardCount must be in range 1..64."
}
if ($PingCount -le 0 -or $PingCount -gt 1000)
{
    throw "PingCount must be in range 1..1000."
}

if (-not $SkipBuild)
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = $null
    if (Test-Path -LiteralPath $vswhere)
    {
        $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($visualStudioPath))
        {
            $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
        }
    }

    if ($null -eq $msbuild -or -not (Test-Path -LiteralPath $msbuild))
    {
        $msbuildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
        if ($null -ne $msbuildCommand)
        {
            $msbuild = $msbuildCommand.Source
        }
    }
    if ($null -eq $msbuild -or -not (Test-Path -LiteralPath $msbuild))
    {
        throw "MSBuild.exe was not found."
    }

    foreach ($project in @($serverProject, $clientProject))
    {
        & $msbuild $project /t:Build /p:Configuration=$Configuration /p:Platform=x64 /m /nologo /v:minimal
        if ($LASTEXITCODE -ne 0)
        {
            throw "Build failed: $project"
        }
    }
}

if (-not (Test-Path -LiteralPath $serverExecutable) -or
    -not (Test-Path -LiteralPath $clientExecutable) -or
    -not (Test-Path -LiteralPath $cacheServerConfigTemplate -PathType Leaf))
{
    throw "Cache RPC Ping dependency is missing. Build the projects and verify the CacheServer config template."
}

$testDirectory = Join-Path $repositoryRoot ("Out\CacheRpcPingTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStandardOutput = Join-Path $testDirectory "server.stdout.log"
$serverStandardError = Join-Path $testDirectory "server.stderr.log"
$clientStandardOutput = Join-Path $testDirectory "client.stdout.log"
$clientStandardError = Join-Path $testDirectory "client.stderr.log"
$serverConfigPath = Join-Path $testDirectory "cache.server.yaml"

$cacheBackend = "Iocp"
$rioSendDispatchMode = "Direct"
switch ($Backend)
{
    "rio-direct"
    {
        $cacheBackend = "Rio"
    }
    "rio-owner"
    {
        $cacheBackend = "Rio"
        $rioSendDispatchMode = "OwnerThread"
    }
}

New-ServerConfigFile `
    -TemplatePath $cacheServerConfigTemplate `
    -DestinationPath $serverConfigPath `
    -Overrides @{
        "CacheServer.Backend" = $cacheBackend
        "CacheServer.RioSendDispatchMode" = $rioSendDispatchMode
        "CacheServer.Port" = $Port
        "CacheServer.PlayerCacheShardCount" = $ShardCount
        "CacheServer.DatabaseEnabled" = $false
        "Debug.RunSeconds" = 15
    } | Out-Null

$serverProcess = $null
try
{
    $serverProcess = Start-Process `
        -FilePath $serverExecutable `
        -ArgumentList @("--config", $serverConfigPath) `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $serverStandardOutput `
        -RedirectStandardError $serverStandardError `
        -WindowStyle Hidden `
        -PassThru

    $listenDeadline = [DateTime]::UtcNow.AddSeconds(5)
    $listening = $false
    while ([DateTime]::UtcNow -lt $listenDeadline -and -not $serverProcess.HasExited)
    {
        $listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
        if ($null -ne $listener)
        {
            $listening = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }

    if (-not $listening)
    {
        $serverError = Get-Content -LiteralPath $serverStandardError -Raw -ErrorAction SilentlyContinue
        throw "CacheServer did not enter Listen state. error=$serverError"
    }

    & $clientExecutable --port $Port --ping-count $PingCount --expected-shards $ShardCount `
        1> $clientStandardOutput 2> $clientStandardError
    $clientExitCode = $LASTEXITCODE

    $clientOutput = Get-Content -LiteralPath $clientStandardOutput -Raw -ErrorAction SilentlyContinue
    if ($clientExitCode -ne 0 -or $clientOutput -notmatch "\[PASS\] Cache RPC Ping")
    {
        $clientError = Get-Content -LiteralPath $clientStandardError -Raw -ErrorAction SilentlyContinue
        throw "Cache RPC Ping smoke test failed. exit=$clientExitCode error=$clientError"
    }

    if (-not $serverProcess.WaitForExit(20000))
    {
        throw "CacheServer did not exit cleanly within the timeout."
    }

	$serverOutput = Get-Content -LiteralPath $serverStandardOutput -Raw -ErrorAction SilentlyContinue
	$notificationCount = ([regex]::Matches($serverOutput, "CachePing notification handled\. sequence=900001 userId=1\b")).Count
	if ($notificationCount -ne 1)
	{
		throw "Expected exactly one routed CachePing notification, actual=$notificationCount"
	}

    Write-Host $clientOutput.Trim()
	Write-Host "[PASS] Cache RPC Notification routed to the target shard."
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited)
    {
        Stop-Process -Id $serverProcess.Id -Force
    }
}
