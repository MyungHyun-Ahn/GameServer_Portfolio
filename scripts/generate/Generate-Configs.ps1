param(
    [string]$Configuration = "Debug",
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$refactoringServerRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $refactoringServerRoot "Tools\\ConfigGenerator\\ConfigGenerator.csproj"

if (-not (Test-Path $projectPath))
{
    throw "ConfigGenerator project not found: $projectPath"
}

Write-Host "Building ConfigGenerator ($Configuration)..."
dotnet build $projectPath -c $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "ConfigGenerator build failed."
}

if ($BuildOnly)
{
    Write-Host "BuildOnly specified. Generation step skipped."
    exit 0
}

Write-Host "Running ConfigGenerator..."
dotnet run --project $projectPath -c $Configuration --no-build
if ($LASTEXITCODE -ne 0)
{
    throw "Config generation failed."
}

Write-Host "Config generation completed."
