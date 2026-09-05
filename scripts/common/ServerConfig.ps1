function ConvertTo-ServerConfigYamlString
{
	param(
		[AllowEmptyString()]
		[Parameter(Mandatory = $true)]
		[string]$Value
	)

	$escaped = $Value.Replace('\', '\\').Replace('"', '\"')
	return '"' + $escaped + '"'
}

function New-ServerConfigFile
{
	[CmdletBinding()]
	param(
		[Parameter(Mandatory = $true)]
		[string]$TemplatePath,

		[Parameter(Mandatory = $true)]
		[string]$DestinationPath,

		[Parameter(Mandatory = $true)]
		[hashtable]$Overrides
	)

	if (-not (Test-Path -LiteralPath $TemplatePath -PathType Leaf))
	{
		throw "Server config template not found: $TemplatePath"
	}

	$remainingOverrides = @{}
	foreach ($entry in $Overrides.GetEnumerator())
	{
		$remainingOverrides[[string]$entry.Key] = $entry.Value
	}

	$sectionPattern = [regex]::new('^([A-Za-z_][A-Za-z0-9_]*):(?:\s*#.*)?$')
	$keyPattern = [regex]::new('^  ([A-Za-z_][A-Za-z0-9_]*):')
	$lines = [System.IO.File]::ReadAllLines($TemplatePath)
	$currentSection = ""

	for ($index = 0; $index -lt $lines.Length; ++$index)
	{
		$sectionMatch = $sectionPattern.Match($lines[$index])
		if ($sectionMatch.Success)
		{
			$currentSection = $sectionMatch.Groups[1].Value
			continue
		}

		$keyMatch = $keyPattern.Match($lines[$index])
		if (-not $keyMatch.Success -or [string]::IsNullOrEmpty($currentSection))
		{
			continue
		}

		$key = $keyMatch.Groups[1].Value
		$qualifiedKey = "$currentSection.$key"
		if (-not $remainingOverrides.ContainsKey($qualifiedKey))
		{
			continue
		}

		$value = $remainingOverrides[$qualifiedKey]
		if ($value -is [bool])
		{
			$valueText = $value.ToString().ToLowerInvariant()
		}
		else
		{
			$valueText = [string]$value
		}

		$lines[$index] = "  ${key}: $valueText"
		$remainingOverrides.Remove($qualifiedKey)
	}

	if ($remainingOverrides.Count -ne 0)
	{
		$missingKeys = ($remainingOverrides.Keys | Sort-Object) -join ', '
		throw "Server config override key was not found in the template: $missingKeys"
	}

	$destinationDirectory = Split-Path -Parent $DestinationPath
	if (-not [string]::IsNullOrEmpty($destinationDirectory))
	{
		[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
	}

	[System.IO.File]::WriteAllLines(
		$DestinationPath,
		$lines,
		[System.Text.UTF8Encoding]::new($false))

	return $DestinationPath
}
