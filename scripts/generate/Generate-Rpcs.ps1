param(
    [string]$Configuration = "Debug",
    [switch]$BuildOnly,
    [switch]$Check
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $repositoryRoot "Tools\RpcGenerator\RpcGenerator.csproj"

if (-not (Test-Path -LiteralPath $projectPath))
{
    throw "RpcGenerator project not found: $projectPath"
}

Write-Host "Building RpcGenerator ($Configuration)..."
dotnet build $projectPath -c $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "RpcGenerator build failed."
}

if ($BuildOnly)
{
    Write-Host "BuildOnly specified. Generation step skipped."
    exit 0
}

$generatorArguments = @()
if ($Check)
{
    $generatorArguments += "--check"
}

Write-Host $(if ($Check) { "Checking generated RPC code..." } else { "Running RpcGenerator..." })
dotnet run --project $projectPath -c $Configuration --no-build -- @generatorArguments
if ($LASTEXITCODE -ne 0)
{
    throw $(if ($Check) { "RPC generation check failed." } else { "RPC generation failed." })
}

Write-Host $(if ($Check) { "RPC generation check completed." } else { "RPC generation completed." })
