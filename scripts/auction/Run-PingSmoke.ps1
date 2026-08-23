param(
    [int]$Port = 19100,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$serverProject = Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj"
$serverExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Debug\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Debug\AuctionDummyClient.exe"

if ($Port -le 0 -or $Port -gt 65535)
{
    throw "Port must be in range 1..65535."
}

if (-not $SkipBuild)
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere))
    {
        throw "vswhere.exe not found: $vswhere"
    }

    $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ([string]::IsNullOrWhiteSpace($visualStudioPath))
    {
        throw "Visual Studio with MSBuild was not found."
    }

    $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
    foreach ($project in @($serverProject, $clientProject))
    {
        & $msbuild $project /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
        if ($LASTEXITCODE -ne 0)
        {
            throw "Build failed: $project"
        }
    }
}

if (-not (Test-Path -LiteralPath $serverExecutable) -or
    -not (Test-Path -LiteralPath $clientExecutable))
{
    throw "Auction ping executables are missing. Build the projects first."
}

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionPingTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStandardOutput = Join-Path $testDirectory "server.stdout.log"
$serverStandardError = Join-Path $testDirectory "server.stderr.log"
$clientStandardOutput = Join-Path $testDirectory "client.stdout.log"
$clientStandardError = Join-Path $testDirectory "client.stderr.log"

$serverProcess = $null
try
{
    $serverProcess = Start-Process `
        -FilePath $serverExecutable `
        -ArgumentList @("--port", "$Port", "--run-seconds", "5") `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $serverStandardOutput `
        -RedirectStandardError $serverStandardError `
        -WindowStyle Hidden `
        -PassThru

    Start-Sleep -Milliseconds 1000

    & $clientExecutable --port $Port 1> $clientStandardOutput 2> $clientStandardError
    $clientExitCode = $LASTEXITCODE

    if (-not $serverProcess.WaitForExit(10000))
    {
        throw "AuctionHouseServer did not exit within the timeout."
    }

    $clientOutput = Get-Content -LiteralPath $clientStandardOutput -Raw
    if ($clientExitCode -ne 0 -or $clientOutput -notmatch "SHARD_ROUTING_TEST_SUCCESS")
    {
        $clientError = Get-Content -LiteralPath $clientStandardError -Raw -ErrorAction SilentlyContinue
        throw "Ping smoke test failed. exit=$clientExitCode error=$clientError"
    }

    Write-Host $clientOutput.Trim()
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited)
    {
        Stop-Process -Id $serverProcess.Id -Force
    }
}
