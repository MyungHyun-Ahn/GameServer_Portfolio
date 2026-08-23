param(
    [string]$Configuration = "Debug",
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$refactoringServerRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $refactoringServerRoot "Tools\\PacketGenerator\\PacketGenerator.csproj"

if (-not (Test-Path $projectPath))
{
    throw "PacketGenerator project not found: $projectPath"
}

Write-Host "Building PacketGenerator ($Configuration)..."
dotnet build $projectPath -c $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "PacketGenerator build failed."
}

if ($BuildOnly)
{
    Write-Host "BuildOnly specified. Generation step skipped."
    exit 0
}

Write-Host "Running PacketGenerator..."
dotnet run --project $projectPath -c $Configuration --no-build
if ($LASTEXITCODE -ne 0)
{
    throw "Packet generation failed."
}

Write-Host "Packet generation completed."
