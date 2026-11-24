param(
    [Parameter(Mandatory)][string]$List,
    [Parameter(Mandatory)][string]$Out
)

# Downloads each license text that is not inside the installed packages, from its official source at the
# shipped version, and records where it came from with its SHA-256. The result is committed by hand.
$ErrorActionPreference = 'Stop'
$sources = Get-Content -Raw $List | ConvertFrom-Json
$texts = Join-Path $Out 'licenses/texts'
$work = Join-Path $env:RUNNER_TEMP 'plynth-license-work'
New-Item -ItemType Directory -Force $texts, $work | Out-Null
$records = [System.Collections.Generic.List[object]]::new()

# Returns the lower case SHA-256 of a file.
function Get-Sha([string]$file) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant() }

foreach ($f in $sources.files) {
    $target = Join-Path $texts $f.name
    Invoke-WebRequest -Uri $f.url -OutFile $target
    $records.Add([ordered]@{ file = "licenses/texts/$($f.name)"; component = $f.component; url = $f.url; sha256 = Get-Sha $target })
}

$qt = $sources.qt
$md5 = @{}
try {
    Invoke-WebRequest -Uri "$($qt.archive)/md5sums.txt" -OutFile (Join-Path $work 'md5sums.txt')
    foreach ($line in Get-Content (Join-Path $work 'md5sums.txt')) { if ($line -match '^([0-9a-f]{32})\s+\*?(\S+)$') { $md5[$Matches[2]] = $Matches[1] } }
} catch {
    Write-Host "::warning::Qt's md5sums.txt could not be read, so the Qt archives are recorded by SHA-256 only"
}

foreach ($module in $qt.modules) {
    $name = "$module-everywhere-src-$($qt.version)"
    $archive = Join-Path $work "$name.tar.xz"
    $url = "$($qt.archive)/$name.tar.xz"
    Invoke-WebRequest -Uri $url -OutFile $archive
    $checked = $md5.ContainsKey("$name.tar.xz")
    if ($checked -and (Get-FileHash -Algorithm MD5 $archive).Hash.ToLowerInvariant() -ne $md5["$name.tar.xz"]) {
        throw "$name.tar.xz does not match Qt's published checksum"
    }
    $records.Add([ordered]@{ file = $null; component = "Qt $module source archive"; url = $url; sha256 = Get-Sha $archive; qt_md5_checked = $checked })

    $tree = Join-Path $work $module
    New-Item -ItemType Directory -Force $tree | Out-Null
    $names = tar -tJf $archive
    if ($LASTEXITCODE -ne 0) { throw "could not list $archive" }
    $wanted = @($names | Where-Object { $_ -like '*/qt_attribution.json' -or $_ -like "$name/LICENSES/*" } |
        Where-Object { $_ -notlike '*/tests/*' -and $_ -notlike '*/examples/*' -and $_ -notlike '*/' })
    tar -xJf $archive -C $tree @wanted
    if ($LASTEXITCODE -ne 0) { throw "could not unpack $archive" }

    foreach ($license in Get-ChildItem (Join-Path $tree "$name/LICENSES") -File -ErrorAction SilentlyContinue) {
        $target = Join-Path $texts "qt/$module/$($license.Name)"
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item $license.FullName $target
        $records.Add([ordered]@{ file = "licenses/texts/qt/$module/$($license.Name)"; component = "Qt $module $($qt.version)"
            url = "$url, $name/LICENSES/$($license.Name)"; sha256 = Get-Sha $target })
    }

    # Each qt_attribution.json names third party parts and the files that hold their license texts.
    $more = @()
    $entries = @()
    foreach ($json in Get-ChildItem -Recurse (Join-Path $tree $name) -Filter qt_attribution.json) {
        $parsed = Get-Content -Raw $json.FullName | ConvertFrom-Json
        foreach ($e in @($parsed)) {
            $folder = [System.IO.Path]::GetRelativePath($tree, $json.DirectoryName).Replace('\', '/')
            $files = @()
            if ($e.LicenseFile) { $files += @($e.LicenseFile -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
            if ($e.LicenseFiles) { $files += @($e.LicenseFiles) }
            if ($e.CopyrightFile) { $files += @($e.CopyrightFile) }
            $paths = @($files | ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path $folder $_), '/x/').Substring(3).Replace('\', '/') })
            $more += $paths
            $entries += [pscustomobject]@{ Entry = $e; Paths = $paths; Folder = $folder }
        }
    }
    $more = @($more | Sort-Object -Unique | Where-Object { $names -contains $_ })
    if ($more.Count) {
        tar -xJf $archive -C $tree @more
        if ($LASTEXITCODE -ne 0) { throw "could not unpack the license files of $archive" }
    }
    foreach ($item in $entries) {
        $e = $item.Entry
        $id = if ($e.Id) { $e.Id } else { ($e.Name -replace '[^A-Za-z0-9]+', '-').ToLowerInvariant() }
        $saved = @()
        foreach ($path in $item.Paths) {
            $source = Join-Path $tree $path
            if (-not (Test-Path -LiteralPath $source)) { Write-Host "::warning::$module $id names $path, which is not in the archive"; continue }
            $target = Join-Path $texts "qt/$module/$id/$(Split-Path $path -Leaf)"
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item -LiteralPath $source $target
            $saved += [ordered]@{ file = [System.IO.Path]::GetRelativePath($Out, $target).Replace('\', '/'); from = "$url, $path"; sha256 = Get-Sha $target }
        }
        $records.Add([ordered]@{ file = $null; component = "$($e.Name) in Qt $module"; qt_id = $id; version = $e.Version
            license = $e.License; license_id = $e.LicenseId; copyright = $e.Copyright; qt_usage = $e.QtUsage
            attribution = "$($item.Folder)/qt_attribution.json"; texts = $saved })
    }
}

[ordered]@{ format = 'plynth-license-texts'; version = 1; retrieved = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    commit = $env:GITHUB_SHA; records = $records } | ConvertTo-Json -Depth 8 |
    Out-File -FilePath (Join-Path $Out 'licenses/license-texts.json') -Encoding utf8NoBOM
$count = (Get-ChildItem -Recurse -File $texts).Count
"Saved $count license texts" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
