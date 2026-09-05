param(
    [string]$Configuration = "Debug",
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $repositoryRoot "Tools\\PacketGenerator\\PacketGenerator.csproj"
$formatCppScript = Join-Path (Join-Path $scriptsRoot "maintenance") "Format-Cpp.ps1"
$generatedCppRoot = Join-Path $repositoryRoot "Generated\\Packets\\Cpp"
$generatedCSharpProjectPath = Join-Path $repositoryRoot "Generated\\Packets\\CSharp\\GeneratedPackets.csproj"

if (-not (Test-Path $projectPath))
{
    throw "PacketGenerator project not found: $projectPath"
}

if (-not (Test-Path -LiteralPath $formatCppScript -PathType Leaf))
{
    throw "C++ format script not found: $formatCppScript"
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

Write-Host "Formatting generated C++ packet code..."
& powershell -ExecutionPolicy Bypass -File $formatCppScript -SourceRoot $generatedCppRoot
if ($LASTEXITCODE -ne 0)
{
    throw "Generated C++ packet formatting failed."
}

if (-not (Test-Path $generatedCSharpProjectPath))
{
    throw "Generated C# packet project not found: $generatedCSharpProjectPath"
}

Write-Host "Building generated C# packet contracts ($Configuration)..."
dotnet build $generatedCSharpProjectPath -c $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "Generated C# packet build failed."
}

Write-Host "Packet generation completed."
