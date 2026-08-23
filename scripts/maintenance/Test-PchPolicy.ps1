$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot

$pchFiles = Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File -Filter *Pch.h |
	Where-Object { $_.FullName -notmatch "\\(Intermediate|Out|ThirdParty|vcpkg_installed|node_modules)\\" }

$forbiddenPatterns = @(
	@{ Pattern = '#include\s+["<].*Pch\.h[">]'; Reason = "A project PCH must not include another project PCH." },
	@{ Pattern = '#include\s+["<].*Generated[/\\]'; Reason = "Generated code changes frequently and must stay out of PCH." },
	@{ Pattern = '#include\s+["<].*[/\\]Service[/\\]'; Reason = "Service implementations must stay out of PCH." },
	@{ Pattern = '#include\s+["<].*Repository\.h[">]'; Reason = "Repository implementations must stay out of PCH." },
	@{ Pattern = '#include\s+<mysql\.h>'; Reason = "MySQL SDK belongs in its implementation cpp." },
	@{ Pattern = '#include\s+<cpp_redis/'; Reason = "Redis SDK belongs in its implementation cpp." },
	@{ Pattern = '#include\s+["<].*Contents[/\\](Auth|Lobby|Echo|Command|Expiration|Session)[/\\]F.*\.h[">]'; Reason = "Concrete content classes must stay out of PCH." },
	@{ Pattern = '#include\s+["<].*Contents[/\\]Room[/\\]F.*\.h[">]'; Reason = "Concrete room classes must stay out of PCH." }
)

$violations = @()
foreach ($pchFile in $pchFiles)
{
	foreach ($forbidden in $forbiddenPatterns)
	{
		$matches = Select-String -LiteralPath $pchFile.FullName -Pattern $forbidden.Pattern
		foreach ($match in $matches)
		{
			$violations += [pscustomobject]@{
				Path = $pchFile.FullName
				LineNumber = $match.LineNumber
				Line = $match.Line.Trim()
				Reason = $forbidden.Reason
			}
		}
	}
}

if ($violations.Count -ne 0)
{
	$violations | ForEach-Object {
		Write-Error ("PCH policy violation: {0}:{1}: {2} ({3})" -f $_.Path, $_.LineNumber, $_.Line, $_.Reason)
	}
	exit 1
}

Write-Host "PCH policy passed. files=$($pchFiles.Count)"
