param(
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $repositoryRoot "Tools\RpcGenerator\RpcGenerator.csproj"
$goldenRoot = Join-Path $repositoryRoot "Tools\RpcGenerator\Tests\Golden"
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("GameServerPortfolio-RpcGenerator-" + [Guid]::NewGuid().ToString("N"))

function Invoke-Generator
{
    param(
        [Parameter(Mandatory = $true)][string]$SchemaRoot,
        [Parameter(Mandatory = $true)][string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$LockFile,
        [switch]$Check,
        [switch]$ExpectFailure,
        [string]$ExpectedError = ""
    )

    $arguments = @(
        "run", "--project", $projectPath, "-c", $Configuration, "--no-build", "--",
        "--schema-root", $SchemaRoot,
        "--output-root", $OutputRoot,
        "--lock-file", $LockFile
    )
    if ($Check)
    {
        $arguments += "--check"
    }

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try
    {
        $output = (& dotnet @arguments 2>&1 | Out-String)
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($ExpectFailure)
    {
        if ($exitCode -eq 0)
        {
            throw "Generator unexpectedly succeeded. Expected error: $ExpectedError"
        }

        if (-not [string]::IsNullOrWhiteSpace($ExpectedError) -and $output -notlike "*$ExpectedError*")
        {
            throw "Generator failed with an unexpected message. Expected '$ExpectedError'. Output: $output"
        }

        return
    }

    if ($exitCode -ne 0)
    {
        throw "Generator failed. Output: $output"
    }
}

function Assert-TextEqual
{
    param(
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$ActualPath
    )

    if (-not (Test-Path -LiteralPath $ActualPath))
    {
        throw "Generated golden file is missing: $ActualPath"
    }

    $expected = [System.IO.File]::ReadAllText($ExpectedPath).Replace("`r`n", "`n")
    $actualBytes = [System.IO.File]::ReadAllBytes($ActualPath)
    if ($actualBytes.Length -ge 3 -and $actualBytes[0] -eq 0xEF -and $actualBytes[1] -eq 0xBB -and $actualBytes[2] -eq 0xBF)
    {
        throw "Generated file contains UTF-8 BOM: $ActualPath"
    }

    $actual = [System.Text.Encoding]::UTF8.GetString($actualBytes).Replace("`r`n", "`n")
    if ($expected -cne $actual)
    {
        throw "Golden output mismatch: $ActualPath"
    }
}

function Assert-TextOrder
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Before,
        [Parameter(Mandatory = $true)][string]$After,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $text = [System.IO.File]::ReadAllText($Path)
    $beforeIndex = $text.IndexOf($Before, [System.StringComparison]::Ordinal)
    $afterIndex = $text.IndexOf($After, [System.StringComparison]::Ordinal)
    if ($beforeIndex -lt 0 -or $afterIndex -lt 0 -or $beforeIndex -ge $afterIndex)
    {
        throw "Forward-reference order check failed ($Description): '$Before' must precede '$After'."
    }
}

function New-NegativeCase
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Yaml,
        [Parameter(Mandatory = $true)][string]$ExpectedError
    )

    $caseRoot = Join-Path $temporaryRoot $Name
    $schemaRoot = Join-Path $caseRoot "Schema"
    $outputRoot = Join-Path $caseRoot "Output"
    $lockFile = Join-Path $caseRoot "rpc-schema.lock.json"
    New-Item -ItemType Directory -Path $schemaRoot -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $schemaRoot "Case.rpc.yaml"), $Yaml, [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $schemaRoot -OutputRoot $outputRoot -LockFile $lockFile -ExpectFailure -ExpectedError $ExpectedError
}

try
{
    if (-not (Test-Path -LiteralPath $projectPath) -or -not (Test-Path -LiteralPath $goldenRoot))
    {
        throw "RpcGenerator project or golden fixture was not found."
    }

    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    dotnet build $projectPath -c $Configuration
    if ($LASTEXITCODE -ne 0)
    {
        throw "RpcGenerator build failed."
    }

    $goldenSchema = Join-Path $goldenRoot "Schema"
    $goldenExpected = Join-Path $goldenRoot "Expected"
    $goldenOutput = Join-Path $temporaryRoot "Golden\Output"
    $goldenLock = Join-Path $temporaryRoot "Golden\rpc-schema.lock.json"
    Invoke-Generator -SchemaRoot $goldenSchema -OutputRoot $goldenOutput -LockFile $goldenLock
    $goldenHeader = Join-Path $goldenOutput "Example\ExampleRpcMethods.h"
    Assert-TextEqual -ExpectedPath (Join-Path $goldenExpected "Example\ExampleRpcMethods.h") -ActualPath $goldenHeader
    Assert-TextEqual -ExpectedPath (Join-Path $goldenExpected "RpcMethodCatalog.h") -ActualPath (Join-Path $goldenOutput "RpcMethodCatalog.h")
    Assert-TextOrder -Path $goldenHeader -Before "enum class EResult : std::uint8_t;" -After "using FPayloadAlias = FPayload;" -Description "enum forward declaration"
    Assert-TextOrder -Path $goldenHeader -Before "struct FPayload;" -After "using FPayloadAlias = FPayload;" -Description "struct forward declaration"
    Assert-TextOrder -Path $goldenHeader -Before "using FUserId = std::uint64_t;" -After "using FCommandId = FUserId;" -Description "alias dependency"
    Assert-TextOrder -Path $goldenHeader -Before "struct FPayload final" -After "struct FEnvelope final" -Description "struct value dependency"
    Invoke-Generator -SchemaRoot $goldenSchema -OutputRoot $goldenOutput -LockFile $goldenLock -Check

    $inputScopeRoot = Join-Path $temporaryRoot "InputScope"
    $inputScopeSchema = Join-Path $inputScopeRoot "Schema"
    $inputScopeOutput = Join-Path $inputScopeRoot "Output"
    $inputScopeLock = Join-Path $inputScopeRoot "rpc-schema.lock.json"
    New-Item -ItemType Directory -Path $inputScopeSchema -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $goldenSchema "Example.rpc.yaml") -Destination (Join-Path $inputScopeSchema "Example.rpc.yaml")
    [System.IO.File]::WriteAllText(
        (Join-Path $inputScopeSchema "Application.yaml"),
        "application:`n  name: ThisIsNotAnRpcSchema`n",
        [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $inputScopeSchema -OutputRoot $inputScopeOutput -LockFile $inputScopeLock
    if (-not (Test-Path -LiteralPath (Join-Path $inputScopeOutput "Example\ExampleRpcMethods.h")))
    {
        throw "RPC input-scope test failed: the .rpc.yaml fixture was not generated."
    }

    $trackedFile = Join-Path $goldenOutput "Example\ExampleRpcMethods.h"
    $writeTimeBefore = (Get-Item -LiteralPath $trackedFile).LastWriteTimeUtc.Ticks
    Start-Sleep -Milliseconds 1200
    Invoke-Generator -SchemaRoot $goldenSchema -OutputRoot $goldenOutput -LockFile $goldenLock
    $writeTimeAfter = (Get-Item -LiteralPath $trackedFile).LastWriteTimeUtc.Ticks
    if ($writeTimeBefore -ne $writeTimeAfter)
    {
        throw "Changed-only write policy failed: unchanged output was rewritten."
    }

    $staleFile = Join-Path $goldenOutput "Stale\Old.h"
    New-Item -ItemType Directory -Path (Split-Path -Parent $staleFile) -Force | Out-Null
    [System.IO.File]::WriteAllText($staleFile, "stale", [System.Text.UTF8Encoding]::new($false))
    $manifestPath = Join-Path $goldenOutput ".rpc-generator-manifest.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.files += "Stale/Old.h"
    [System.IO.File]::WriteAllText(
        $manifestPath,
        (($manifest | ConvertTo-Json -Depth 8) + "`n"),
        [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $goldenSchema -OutputRoot $goldenOutput -LockFile $goldenLock
    if (Test-Path -LiteralPath $staleFile)
    {
        throw "Stale manifest cleanup failed."
    }

    New-NegativeCase -Name "UnknownKey" -ExpectedError "unexpected-key" -Yaml @"
schema-version: 1
namespace: Test::Protocol
output: Test.h
unexpected-key: true
service: { name: Test, id: 10, reserved-method-ids: [] }
methods:
  - name: Ping
    id: 1
    request: { fields: [] }
    response: { fields: [] }
"@

    New-NegativeCase -Name "DuplicateYamlKey" -ExpectedError "Duplicate key id" -Yaml @"
schema-version: 1
namespace: Test::Protocol
output: Test.h
service:
  name: Test
  id: 10
  id: 11
  reserved-method-ids: []
methods:
  - name: Ping
    id: 1
    request: { fields: [] }
    response: { fields: [] }
"@

    New-NegativeCase -Name "RequestOnly" -ExpectedError "must define request and response together" -Yaml @"
schema-version: 1
namespace: Test::Protocol
output: Test.h
service: { name: Test, id: 11, reserved-method-ids: [] }
methods:
  - name: Ping
    id: 1
    request: { fields: [] }
"@

    New-NegativeCase -Name "BorrowedString" -ExpectedError "Borrowed RPC type" -Yaml @"
schema-version: 1
namespace: Test::Protocol
output: Test.h
service: { name: Test, id: 12, reserved-method-ids: [] }
methods:
  - name: Changed
    id: 1
    noti:
      fields:
        - { name: text, type: string_view }
"@

    New-NegativeCase -Name "CppKeyword" -ExpectedError "Invalid C++ identifier" -Yaml @"
schema-version: 1
namespace: Test::Protocol
output: Test.h
service: { name: Test, id: 15, reserved-method-ids: [] }
methods:
  - name: class
    id: 1
    request: { fields: [] }
    response: { fields: [] }
"@

    $duplicateRoot = Join-Path $temporaryRoot "DuplicateService"
    $duplicateSchema = Join-Path $duplicateRoot "Schema"
    New-Item -ItemType Directory -Path $duplicateSchema -Force | Out-Null
    $duplicateTemplate = @"
schema-version: 1
namespace: Test::{0}
output: {0}.h
service: {{ name: {0}, id: 13, reserved-method-ids: [] }}
methods:
  - name: Ping
    id: 1
    request: {{ fields: [] }}
    response: {{ fields: [] }}
"@
    [System.IO.File]::WriteAllText((Join-Path $duplicateSchema "A.rpc.yaml"), ($duplicateTemplate -f "A"), [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText((Join-Path $duplicateSchema "B.rpc.yaml"), ($duplicateTemplate -f "B"), [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $duplicateSchema -OutputRoot (Join-Path $duplicateRoot "Output") -LockFile (Join-Path $duplicateRoot "lock.json") -ExpectFailure -ExpectedError "Duplicate global service id"

    $lockCaseRoot = Join-Path $temporaryRoot "LockMutation"
    $lockSchema = Join-Path $lockCaseRoot "Schema"
    New-Item -ItemType Directory -Path $lockSchema -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $goldenSchema "Example.rpc.yaml") -Destination (Join-Path $lockSchema "Example.rpc.yaml")
    $lockOutput = Join-Path $lockCaseRoot "Output"
    $lockPath = Join-Path $lockCaseRoot "rpc-schema.lock.json"
    Invoke-Generator -SchemaRoot $lockSchema -OutputRoot $lockOutput -LockFile $lockPath
    $mutated = [System.IO.File]::ReadAllText((Join-Path $lockSchema "Example.rpc.yaml")).Replace("type: FUserId", "type: uint32")
    [System.IO.File]::WriteAllText((Join-Path $lockSchema "Example.rpc.yaml"), $mutated, [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $lockSchema -OutputRoot $lockOutput -LockFile $lockPath -ExpectFailure -ExpectedError "wire signature changed"

    $sameTypeReorderRoot = Join-Path $temporaryRoot "SameTypeFieldReorder"
    $sameTypeReorderSchema = Join-Path $sameTypeReorderRoot "Schema"
    New-Item -ItemType Directory -Path $sameTypeReorderSchema -Force | Out-Null
    $sameTypeReorderPath = Join-Path $sameTypeReorderSchema "Order.rpc.yaml"
    $sameTypeSchema = @"
schema-version: 1
namespace: Test::Order
output: Order.h
service: { name: Order, id: 14, reserved-method-ids: [] }
methods:
  - name: Submit
    id: 1
    routing-key: userId
    request:
      fields:
        - { name: sequence, type: uint64 }
        - { name: userId, type: uint64 }
    response: { fields: [] }
"@
    $sameTypeOriginalSchema = $sameTypeSchema
    [System.IO.File]::WriteAllText($sameTypeReorderPath, $sameTypeSchema, [System.Text.UTF8Encoding]::new($false))
    $sameTypeReorderOutput = Join-Path $sameTypeReorderRoot "Output"
    $sameTypeReorderLock = Join-Path $sameTypeReorderRoot "rpc-schema.lock.json"
    Invoke-Generator -SchemaRoot $sameTypeReorderSchema -OutputRoot $sameTypeReorderOutput -LockFile $sameTypeReorderLock
    $sameTypeSchema = $sameTypeSchema.Replace("name: sequence", "name: temporaryField")
    $sameTypeSchema = $sameTypeSchema.Replace("name: userId", "name: sequence")
    $sameTypeSchema = $sameTypeSchema.Replace("name: temporaryField", "name: userId")
    [System.IO.File]::WriteAllText($sameTypeReorderPath, $sameTypeSchema, [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $sameTypeReorderSchema -OutputRoot $sameTypeReorderOutput -LockFile $sameTypeReorderLock `
        -ExpectFailure -ExpectedError "wire signature changed"

    $routingKeyMutation = $sameTypeOriginalSchema.Replace("routing-key: userId", "routing-key: sequence")
    [System.IO.File]::WriteAllText($sameTypeReorderPath, $routingKeyMutation, [System.Text.UTF8Encoding]::new($false))
    Invoke-Generator -SchemaRoot $sameTypeReorderSchema -OutputRoot $sameTypeReorderOutput -LockFile $sameTypeReorderLock `
        -ExpectFailure -ExpectedError "wire signature changed"

    Write-Host "[PASS] RpcGenerator golden, input-scope, forward-reference, check, changed-only, stale-cleanup, and negative tests passed." -ForegroundColor Green
}
finally
{
    $resolvedTemp = [System.IO.Path]::GetFullPath($temporaryRoot)
    $resolvedSystemTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedTemp.StartsWith($resolvedSystemTemp, [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedTemp -like "*GameServerPortfolio-RpcGenerator-*")
    {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force -ErrorAction SilentlyContinue
    }
}
