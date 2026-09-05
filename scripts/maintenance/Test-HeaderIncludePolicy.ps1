$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot

$ownedRoots = @(
    "Libraries\Foundation",
    "Libraries\NetworkLib",
    "Libraries\ClientNetworkLib",
    "Libraries\Connector",
    "Libraries\ContentsRuntime",
    "Libraries\RpcLib",
    "Libraries\ServerProtocol",
    "Libraries\GameData",
    "Chatting",
    "Echo",
    "Auction",
    "Cache",
    "World",
    "Generated",
    "SmokeTests"
)

$violations = foreach ($ownedRoot in $ownedRoots)
{
    $absoluteRoot = Join-Path $repositoryRoot $ownedRoot
    if (-not (Test-Path -LiteralPath $absoluteRoot))
    {
        continue
    }

    Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File -Filter *.h |
        Where-Object {
            $_.Name -notlike "*Pch.h" -and
            $_.FullName -notmatch "\\(Intermediate|Out|HttpLib|includes|ThirdParty|vcpkg_installed)\\"
        } |
        Select-String -Pattern '^\s*#\s*include\b'
}

if ($violations)
{
    $violations | ForEach-Object {
        Write-Error ("Header include policy violation: {0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim())
    }
    exit 1
}

Write-Host "Header include policy passed: no includes in owned non-PCH headers."
