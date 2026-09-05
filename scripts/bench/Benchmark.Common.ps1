Set-StrictMode -Version Latest

function Test-BenchmarkDictionary
{
    param($Value)

    return $null -ne $Value -and $Value -is [System.Collections.IDictionary]
}

function Get-BenchmarkMapValue
{
    param(
        [System.Collections.IDictionary]$Map,
        [string]$Key,
        $DefaultValue = $null
    )

    if ($null -ne $Map -and $Map.Contains($Key))
    {
        return $Map[$Key]
    }

    return $DefaultValue
}

function ConvertTo-BenchmarkBoolean
{
    param(
        $Value,
        [bool]$DefaultValue = $false
    )

    if ($null -eq $Value)
    {
        return $DefaultValue
    }

    if ($Value -is [bool])
    {
        return $Value
    }

    $text = $Value.ToString().Trim().ToLowerInvariant()
    switch ($text)
    {
        "true" { return $true }
        "1" { return $true }
        "yes" { return $true }
        "on" { return $true }
        "false" { return $false }
        "0" { return $false }
        "no" { return $false }
        "off" { return $false }
        default { return $DefaultValue }
    }
}

function ConvertTo-BenchmarkInt
{
    param(
        $Value,
        [int]$DefaultValue = 0
    )

    if ($null -eq $Value)
    {
        return $DefaultValue
    }

    if ($Value -is [int])
    {
        return $Value
    }

    try
    {
        return [int]$Value
    }
    catch
    {
        return $DefaultValue
    }
}

function ConvertTo-BenchmarkDouble
{
    param(
        $Value,
        [double]$DefaultValue = 0.0
    )

    if ($null -eq $Value)
    {
        return $DefaultValue
    }

    if ($Value -is [double] -or $Value -is [float] -or $Value -is [decimal])
    {
        return [double]$Value
    }

    if ($Value -is [int] -or $Value -is [long])
    {
        return [double]$Value
    }

    try
    {
        return [double]::Parse(
            $Value.ToString(),
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture)
    }
    catch
    {
        return $DefaultValue
    }
}

function Resolve-BenchmarkPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootPath,
        [Parameter(Mandatory = $true)]
        [string]$PathText
    )

    if ([string]::IsNullOrWhiteSpace($PathText))
    {
        throw "PathText must not be empty."
    }

    if ([System.IO.Path]::IsPathRooted($PathText))
    {
        return [System.IO.Path]::GetFullPath($PathText)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RootPath $PathText))
}

function New-BenchmarkDirectory
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    return $Path
}

function Write-BenchmarkUtf8File
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory))
    {
        New-BenchmarkDirectory -Path $directory | Out-Null
    }

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Copy-BenchmarkObject
{
    param($Value)

    if (Test-BenchmarkDictionary -Value $Value)
    {
        $clone = [ordered]@{}
        foreach ($key in $Value.Keys)
        {
            $clone[$key] = Copy-BenchmarkObject -Value $Value[$key]
        }

        return $clone
    }

    if ($Value -is [System.Array])
    {
        $items = @()
        foreach ($item in $Value)
        {
            $items += ,(Copy-BenchmarkObject -Value $item)
        }

        return ,$items
    }

    return $Value
}

function Merge-BenchmarkObjects
{
    param(
        $BaseValue,
        $OverrideValue
    )

    if ($null -eq $BaseValue)
    {
        return Copy-BenchmarkObject -Value $OverrideValue
    }

    if ($null -eq $OverrideValue)
    {
        return Copy-BenchmarkObject -Value $BaseValue
    }

    if ((Test-BenchmarkDictionary -Value $BaseValue) -and (Test-BenchmarkDictionary -Value $OverrideValue))
    {
        $merged = [ordered]@{}
        foreach ($key in $BaseValue.Keys)
        {
            $merged[$key] = Copy-BenchmarkObject -Value $BaseValue[$key]
        }

        foreach ($key in $OverrideValue.Keys)
        {
            if ($merged.Contains($key))
            {
                $merged[$key] = Merge-BenchmarkObjects -BaseValue $merged[$key] -OverrideValue $OverrideValue[$key]
            }
            else
            {
                $merged[$key] = Copy-BenchmarkObject -Value $OverrideValue[$key]
            }
        }

        return $merged
    }

    return Copy-BenchmarkObject -Value $OverrideValue
}

function Split-BenchmarkInlineList
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $items = New-Object System.Collections.Generic.List[string]
    $builder = New-Object System.Text.StringBuilder
    $quoteChar = [char]0

    for ($index = 0; $index -lt $Text.Length; ++$index)
    {
        $character = $Text[$index]
        if ($quoteChar -ne [char]0)
        {
            if ($character -eq $quoteChar)
            {
                $quoteChar = [char]0
            }

            [void]$builder.Append($character)
            continue
        }

        if ($character -eq '"' -or $character -eq "'")
        {
            $quoteChar = $character
            [void]$builder.Append($character)
            continue
        }

        if ($character -eq ',')
        {
            $items.Add($builder.ToString().Trim())
            $builder.Clear() | Out-Null
            continue
        }

        [void]$builder.Append($character)
    }

    $items.Add($builder.ToString().Trim())
    return ,$items.ToArray()
}

function ConvertFrom-BenchmarkYamlScalar
{
    param(
        [string]$Text
    )

    if ($null -eq $Text)
    {
        return $null
    }

    $trimmed = $Text.Trim()
    if ($trimmed.Length -eq 0)
    {
        return ""
    }

    if ($trimmed.StartsWith("[") -and $trimmed.EndsWith("]"))
    {
        $inner = $trimmed.Substring(1, $trimmed.Length - 2).Trim()
        if ($inner.Length -eq 0)
        {
            return @()
        }

        $items = @()
        foreach ($itemText in (Split-BenchmarkInlineList -Text $inner))
        {
            $items += ,(ConvertFrom-BenchmarkYamlScalar -Text $itemText)
        }

        return ,$items
    }

    if (($trimmed.StartsWith('"') -and $trimmed.EndsWith('"')) -or
        ($trimmed.StartsWith("'") -and $trimmed.EndsWith("'")))
    {
        return $trimmed.Substring(1, $trimmed.Length - 2)
    }

    switch ($trimmed.ToLowerInvariant())
    {
        "true" { return $true }
        "false" { return $false }
        "null" { return $null }
    }

    $intValue = 0
    if ([int]::TryParse($trimmed, [ref]$intValue))
    {
        return $intValue
    }

    $doubleValue = 0.0
    if ([double]::TryParse(
            $trimmed,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$doubleValue))
    {
        return $doubleValue
    }

    return $trimmed
}

function Split-BenchmarkYamlKeyValue
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [int]$LineNumber
    )

    $match = [regex]::Match($Text, '^(?<key>[^:]+):(?:\s*(?<value>.*))?$')
    if (-not $match.Success)
    {
        throw ("Invalid YAML mapping at line {0}: {1}" -f $LineNumber, $Text)
    }

    return [ordered]@{
        Key = $match.Groups["key"].Value.Trim()
        HasInlineValue = $match.Groups["value"].Success
        ValueText = $match.Groups["value"].Value
    }
}

function Get-BenchmarkYamlTokens
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $tokens = New-Object System.Collections.Generic.List[object]
    $lines = $Content -split "`r?`n"
    for ($lineIndex = 0; $lineIndex -lt $lines.Length; ++$lineIndex)
    {
        $rawLine = $lines[$lineIndex]
        if ($null -eq $rawLine)
        {
            continue
        }

        if ($rawLine.Contains("`t"))
        {
            throw "Tabs are not supported in benchmark manifest YAML. line=$($lineIndex + 1)"
        }

        $trimmed = $rawLine.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#"))
        {
            continue
        }

        $indent = $rawLine.Length - $rawLine.TrimStart(' ').Length
        $tokens.Add([ordered]@{
                LineNumber = $lineIndex + 1
                Indent = $indent
                Text = $rawLine.Substring($indent)
            })
    }

    return ,$tokens.ToArray()
}

function Parse-BenchmarkYamlNode
{
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Tokens,
        [Parameter(Mandatory = $true)]
        [ref]$Index,
        [Parameter(Mandatory = $true)]
        [int]$Indent
    )

    if ($Index.Value -ge $Tokens.Length)
    {
        return $null
    }

    $token = $Tokens[$Index.Value]
    if ($token.Indent -ne $Indent)
    {
        throw "Unexpected indentation at line $($token.LineNumber). expected=$Indent actual=$($token.Indent)"
    }

    if ($token.Text.StartsWith("- "))
    {
        return Parse-BenchmarkYamlSequence -Tokens $Tokens -Index $Index -Indent $Indent
    }

    return Parse-BenchmarkYamlMap -Tokens $Tokens -Index $Index -Indent $Indent
}

function Parse-BenchmarkYamlMap
{
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Tokens,
        [Parameter(Mandatory = $true)]
        [ref]$Index,
        [Parameter(Mandatory = $true)]
        [int]$Indent
    )

    $map = [ordered]@{}
    while ($Index.Value -lt $Tokens.Length)
    {
        $token = $Tokens[$Index.Value]
        if ($token.Indent -lt $Indent)
        {
            break
        }

        if ($token.Indent -gt $Indent)
        {
            throw "Unexpected indentation at line $($token.LineNumber)."
        }

        if ($token.Text.StartsWith("- "))
        {
            break
        }

        $pair = Split-BenchmarkYamlKeyValue -Text $token.Text -LineNumber $token.LineNumber
        $Index.Value++

        if ($pair.HasInlineValue -and $pair.ValueText.Length -gt 0)
        {
            $map[$pair.Key] = ConvertFrom-BenchmarkYamlScalar -Text $pair.ValueText
            continue
        }

        if ($Index.Value -ge $Tokens.Length -or $Tokens[$Index.Value].Indent -le $Indent)
        {
            $map[$pair.Key] = $null
            continue
        }

        $childIndent = $Tokens[$Index.Value].Indent
        $map[$pair.Key] = Parse-BenchmarkYamlNode -Tokens $Tokens -Index $Index -Indent $childIndent
    }

    return $map
}

function Parse-BenchmarkYamlSequence
{
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Tokens,
        [Parameter(Mandatory = $true)]
        [ref]$Index,
        [Parameter(Mandatory = $true)]
        [int]$Indent
    )

    $items = New-Object System.Collections.Generic.List[object]
    while ($Index.Value -lt $Tokens.Length)
    {
        $token = $Tokens[$Index.Value]
        if ($token.Indent -lt $Indent)
        {
            break
        }

        if ($token.Indent -gt $Indent)
        {
            throw "Unexpected indentation at line $($token.LineNumber)."
        }

        if (-not $token.Text.StartsWith("- "))
        {
            break
        }

        $rest = $token.Text.Substring(2).Trim()
        $Index.Value++

        if ([string]::IsNullOrWhiteSpace($rest))
        {
            if ($Index.Value -ge $Tokens.Length -or $Tokens[$Index.Value].Indent -le $Indent)
            {
                $items.Add($null)
            }
            else
            {
                $items.Add((Parse-BenchmarkYamlNode -Tokens $Tokens -Index $Index -Indent $Tokens[$Index.Value].Indent))
            }

            continue
        }

        if ($rest -match '^[^:]+:\s*')
        {
            $itemMap = [ordered]@{}
            $pair = Split-BenchmarkYamlKeyValue -Text $rest -LineNumber $token.LineNumber
            if ($pair.HasInlineValue -and $pair.ValueText.Length -gt 0)
            {
                $itemMap[$pair.Key] = ConvertFrom-BenchmarkYamlScalar -Text $pair.ValueText
            }
            elseif ($Index.Value -lt $Tokens.Length -and $Tokens[$Index.Value].Indent -gt $Indent)
            {
                $itemMap[$pair.Key] = Parse-BenchmarkYamlNode -Tokens $Tokens -Index $Index -Indent $Tokens[$Index.Value].Indent
            }
            else
            {
                $itemMap[$pair.Key] = $null
            }

            if ($Index.Value -lt $Tokens.Length -and $Tokens[$Index.Value].Indent -gt $Indent)
            {
                $extraMap = Parse-BenchmarkYamlMap -Tokens $Tokens -Index $Index -Indent $Tokens[$Index.Value].Indent
                foreach ($extraKey in $extraMap.Keys)
                {
                    $itemMap[$extraKey] = $extraMap[$extraKey]
                }
            }

            $items.Add($itemMap)
            continue
        }

        $items.Add((ConvertFrom-BenchmarkYamlScalar -Text $rest))
    }

    return ,$items.ToArray()
}

function ConvertFrom-BenchmarkYamlFile
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $content = Get-Content -Path $Path -Raw -Encoding utf8
    $tokens = Get-BenchmarkYamlTokens -Content $content
    if ($tokens.Length -eq 0)
    {
        return [ordered]@{}
    }

    $index = 0
    return Parse-BenchmarkYamlNode -Tokens $tokens -Index ([ref]$index) -Indent $tokens[0].Indent
}

function Format-BenchmarkYamlScalar
{
    param($Value)

    if ($null -eq $Value)
    {
        return "null"
    }

    if ($Value -is [bool])
    {
        return $Value.ToString().ToLowerInvariant()
    }

    if ($Value -is [int] -or $Value -is [long] -or $Value -is [double] -or $Value -is [float] -or $Value -is [decimal])
    {
        return [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0}", $Value)
    }

    if ($Value -is [System.Array])
    {
        $formattedItems = @()
        foreach ($item in $Value)
        {
            $formattedItems += (Format-BenchmarkYamlScalar -Value $item)
        }

        return "[{0}]" -f ($formattedItems -join ", ")
    }

    $text = $Value.ToString()
    if ($text.Length -eq 0)
    {
        return '""'
    }

    if ($text -match '^[A-Za-z0-9_\-\.\/]+$' -and
        $text.ToLowerInvariant() -notin @("true", "false", "null"))
    {
        return $text
    }

    $escaped = $text.Replace('\', '/').Replace('"', '\"')
    return '"' + $escaped + '"'
}

function Set-BenchmarkYamlScalarValue
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content,
        [Parameter(Mandatory = $true)]
        [string]$SectionName,
        [Parameter(Mandatory = $true)]
        [string]$Key,
        [Parameter(Mandatory = $true)]
        $Value
    )

    $escapedSection = [regex]::Escape($SectionName)
    $escapedKey = [regex]::Escape($Key)
    $pattern = "(?ms)(^${escapedSection}:\r?\n(?:^[ ]{2}.*\r?\n)*)"
    $sectionMatch = [regex]::Match($Content, $pattern)
    if (-not $sectionMatch.Success)
    {
        throw "Section not found in YAML: $SectionName"
    }

    $sectionBlock = $sectionMatch.Groups[1].Value
    $linePattern = "(?m)^  ${escapedKey}:.*$"
    if (-not [regex]::IsMatch($sectionBlock, $linePattern))
    {
        throw "Key '$Key' not found in section '$SectionName'"
    }

    $updatedSection = [regex]::Replace(
        $sectionBlock,
        $linePattern,
        ("  {0}: {1}" -f $Key, (Format-BenchmarkYamlScalar -Value $Value)),
        1)

    return $Content.Substring(0, $sectionMatch.Index) +
        $updatedSection +
        $Content.Substring($sectionMatch.Index + $sectionMatch.Length)
}

function Set-BenchmarkYamlOverrides
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$TemplateContent,
        [System.Collections.IDictionary]$Overrides
    )

    if ($null -eq $Overrides)
    {
        return $TemplateContent
    }

    $yaml = $TemplateContent
    foreach ($path in $Overrides.Keys)
    {
        $parts = $path.ToString().Split('.', 2)
        if ($parts.Length -ne 2)
        {
            throw "Override path must be in '<Section>.<Key>' form: $path"
        }

        $yaml = Set-BenchmarkYamlScalarValue `
            -Content $yaml `
            -SectionName $parts[0] `
            -Key $parts[1] `
            -Value $Overrides[$path]
    }

    return $yaml
}

function Start-BenchmarkProcess
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,
        [Parameter(Mandatory = $true)]
        [string]$StdOutPath,
        [Parameter(Mandatory = $true)]
        [string]$StdErrPath
    )

    if (-not (Test-Path $ExecutablePath))
    {
        throw "Executable not found: $ExecutablePath"
    }

    New-BenchmarkDirectory -Path $WorkingDirectory | Out-Null
    $process = Start-Process `
        -FilePath $ExecutablePath `
        -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory `
        -RedirectStandardOutput $StdOutPath `
        -RedirectStandardError $StdErrPath `
        -PassThru
    # Windows PowerShell can lose the native process handle after exit and then
    # expose an empty ExitCode. Cache the handle while the process is alive.
    [void]$process.Handle
    return $process
}

function Wait-BenchmarkProcessExit
{
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds
    )

    $Process.Refresh()
    if ($Process.HasExited)
    {
        return $true
    }

    try
    {
        Wait-Process -Id $Process.Id -Timeout $TimeoutSeconds -ErrorAction Stop
        return $true
    }
    catch
    {
        return $false
    }
}

function Stop-BenchmarkProcess
{
    param(
        [System.Diagnostics.Process]$Process
    )

    if ($null -eq $Process)
    {
        return
    }

    try
    {
        $Process.Refresh()
        if (-not $Process.HasExited)
        {
            Stop-Process -Id $Process.Id -Force -ErrorAction Stop
        }
    }
    catch
    {
    }
}

function Remove-BenchmarkFileIfExists
{
    param(
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path))
    {
        return
    }

    if (-not (Test-Path -LiteralPath $Path))
    {
        return
    }

    try
    {
        Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
    }
    catch
    {
    }
}

function Wait-BenchmarkServerReady
{
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)]
        [string]$ReadyLogPath,
        [Parameter(Mandatory = $true)]
        [string]$ReadyPattern,
        [int]$PollIntervalMilliseconds = 100
    )

    $deadline = (Get-Date).AddSeconds([Math]::Max(1, $TimeoutSeconds))
    while ((Get-Date) -lt $deadline)
    {
        $Process.Refresh()
        if ($Process.HasExited)
        {
            return $false
        }

        if (Test-Path -LiteralPath $ReadyLogPath)
        {
            try
            {
                $readyLog = Get-Content -LiteralPath $ReadyLogPath -Raw -ErrorAction Stop
                if (-not [string]::IsNullOrEmpty($readyLog) -and $readyLog -match $ReadyPattern)
                {
                    return $true
                }
            }
            catch
            {
                # The redirected stdout file can be momentarily unavailable while
                # the server is flushing the ready marker. Retry until timeout.
            }
        }

        Start-Sleep -Milliseconds ([Math]::Max(10, $PollIntervalMilliseconds))
    }

    return $false
}

function ConvertFrom-BenchmarkMetricLine
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line
    )

    $metrics = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[A-Za-z][A-Za-z0-9]*)=(?<value>[^\s]+)'))
    {
        $key = $match.Groups["key"].Value
        $valueText = $match.Groups["value"].Value
        $metrics[$key] = ConvertFrom-BenchmarkYamlScalar -Text $valueText
    }

    return $metrics
}

function Get-BenchmarkLastMetricMap
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Pattern
    )

    if (-not (Test-Path $Path))
    {
        return $null
    }

    $lines = Select-String -Path $Path -Pattern $Pattern | Select-Object -ExpandProperty Line
    if ($null -eq $lines)
    {
        return $null
    }

    $lineArray = @($lines)
    if ($lineArray.Count -eq 0)
    {
        return $null
    }

    return ConvertFrom-BenchmarkMetricLine -Line $lineArray[-1]
}

function Get-BenchmarkMetricMaps
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Pattern
    )

    if (-not (Test-Path $Path))
    {
        return @()
    }

    $matches = @(Select-String -Path $Path -Pattern $Pattern)
    if ($matches.Count -eq 0)
    {
        return @()
    }

    $metricMaps = New-Object System.Collections.Generic.List[object]
    foreach ($match in $matches)
    {
        $metricMaps.Add((ConvertFrom-BenchmarkMetricLine -Line $match.Line))
    }

    return $metricMaps.ToArray()
}

function Get-BenchmarkMetricAggregateMap
{
    param(
        [object[]]$MetricMaps = @(),
        [string[]]$AverageKeys = @(),
        [string[]]$MaxKeys = @(),
        [string]$FilterKey = "",
        [double]$MinimumFilterValue = [double]::NegativeInfinity
    )

    $filteredMaps = New-Object System.Collections.Generic.List[object]
    foreach ($metricMap in @($MetricMaps))
    {
        if (-not (Test-BenchmarkDictionary -Value $metricMap))
        {
            continue
        }

        if ([string]::IsNullOrWhiteSpace($FilterKey))
        {
            $filteredMaps.Add($metricMap)
            continue
        }

        $filterValue = Get-BenchmarkMapValue -Map $metricMap -Key $FilterKey -DefaultValue $null
        if ($null -eq $filterValue)
        {
            continue
        }

        $filterDouble = ConvertTo-BenchmarkDouble -Value $filterValue -DefaultValue ([double]::NaN)
        if ([double]::IsNaN($filterDouble))
        {
            continue
        }

        if ($filterDouble -ge $MinimumFilterValue)
        {
            $filteredMaps.Add($metricMap)
        }
    }

    $result = [ordered]@{
        metricSampleCount = $filteredMaps.Count
    }

    foreach ($key in @($AverageKeys))
    {
        $sum = 0.0
        $count = 0
        foreach ($metricMap in $filteredMaps)
        {
            $value = Get-BenchmarkMapValue -Map $metricMap -Key $key -DefaultValue $null
            if ($null -eq $value)
            {
                continue
            }

            $doubleValue = ConvertTo-BenchmarkDouble -Value $value -DefaultValue ([double]::NaN)
            if ([double]::IsNaN($doubleValue))
            {
                continue
            }

            $sum += $doubleValue
            ++$count
        }

        $propertyName = "avg{0}{1}" -f $key.Substring(0, 1).ToUpperInvariant(), $key.Substring(1)
        $result[$propertyName] = if ($count -gt 0) { [Math]::Round($sum / $count, 3) } else { $null }
    }

    foreach ($key in @($MaxKeys))
    {
        $maxValue = $null
        foreach ($metricMap in $filteredMaps)
        {
            $value = Get-BenchmarkMapValue -Map $metricMap -Key $key -DefaultValue $null
            if ($null -eq $value)
            {
                continue
            }

            $doubleValue = ConvertTo-BenchmarkDouble -Value $value -DefaultValue ([double]::NaN)
            if ([double]::IsNaN($doubleValue))
            {
                continue
            }

            if ($null -eq $maxValue -or $doubleValue -gt $maxValue)
            {
                $maxValue = $doubleValue
            }
        }

        $propertyName = "max{0}{1}" -f $key.Substring(0, 1).ToUpperInvariant(), $key.Substring(1)
        $result[$propertyName] = $maxValue
    }

    return $result
}

function Get-BenchmarkLastLine
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Pattern
    )

    if (-not (Test-Path $Path))
    {
        return $null
    }

    $lines = Select-String -Path $Path -Pattern $Pattern | Select-Object -ExpandProperty Line
    if ($null -eq $lines)
    {
        return $null
    }

    $lineArray = @($lines)
    if ($lineArray.Count -eq 0)
    {
        return $null
    }

    return $lineArray[-1]
}

function ConvertTo-BenchmarkSafeName
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $safe = [regex]::Replace($Text, '[^A-Za-z0-9._-]+', '_')
    return $safe.Trim('_')
}
