param(
    [string]$RepoRoot = "X:\mixxx_test\mixxx",
    [string]$OutputRoot = "X:\mixxx_test\ableton-link-peer-tools\logs\real_world_validation",
    [string]$EvidenceRoot = "",
    [switch]$RunTests,
    [switch]$LaunchBrowser = $true
)

$ErrorActionPreference = "Stop"

function Add-Note {
    param(
        [hashtable]$State,
        [string]$TaskId,
        [string]$Note
    )
    if ([string]::IsNullOrWhiteSpace($Note)) {
        return
    }
    if ($State.notes.ContainsKey($TaskId) -and -not [string]::IsNullOrWhiteSpace($State.notes[$TaskId])) {
        $State.notes[$TaskId] = "$($State.notes[$TaskId])`n$Note"
    } else {
        $State.notes[$TaskId] = $Note
    }
}

function Set-Checked {
    param(
        [hashtable]$State,
        [string]$TaskId,
        [bool]$Value = $true
    )
    $State.checks[$TaskId] = $Value
}

function Get-ProcessSummary {
    $names = @("mixxx.exe", "mixxx-test.exe", "mixxx-link-peer.exe", "Ableton Live.exe")
    $processes = Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $names -contains "$($_.ProcessName).exe" } |
        Select-Object Id, ProcessName, Path
    if (!$processes) {
        return "No Mixxx, mixxx-test, mixxx-link-peer, or Ableton Live processes detected."
    }
    return ($processes | ForEach-Object {
        "PID $($_.Id): $($_.ProcessName).exe $($_.Path)"
    }) -join "`n"
}

function Invoke-JobWithTimeout {
    param(
        [scriptblock]$ScriptBlock,
        [int]$TimeoutSeconds = 10,
        [string]$TimeoutMessage = "Timed out."
    )
    $job = Start-Job -ScriptBlock $ScriptBlock
    try {
        if (Wait-Job -Job $job -Timeout $TimeoutSeconds) {
            return Receive-Job -Job $job
        }
        return $TimeoutMessage
    } finally {
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
}

function Get-AudioDeviceSummary {
    $summary = Invoke-JobWithTimeout -TimeoutSeconds 10 -TimeoutMessage "Timed out while querying FFmpeg DirectShow audio devices." -ScriptBlock {
        $oldErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $output = & ffmpeg -hide_banner -list_devices true -f dshow -i dummy 2>&1
        $ErrorActionPreference = $oldErrorActionPreference
        $devices = @()
        foreach ($line in $output) {
            if ($line -match '"(.+)" \(audio\)') {
                $devices += $Matches[1]
            }
        }
        if ($devices.Count -eq 0) {
            return "No FFmpeg DirectShow audio capture devices detected."
        }
        return ($devices | ForEach-Object { "DirectShow capture: $_" }) -join "`n"
    }
    return ($summary -join "`n")
}

function Get-WindowsVersionSummary {
    try {
        $version = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
        $build = "$($version.CurrentBuildNumber)"
        if ($null -ne $version.UBR) {
            $build = "$build.$($version.UBR)"
        }
        return "$($version.ProductName) $($version.DisplayVersion) build $build"
    } catch {
        return Invoke-JobWithTimeout -TimeoutSeconds 10 -TimeoutMessage "Timed out while querying Win32_OperatingSystem." -ScriptBlock {
            $os = Get-CimInstance Win32_OperatingSystem
            "$($os.Caption) $($os.Version) build $($os.BuildNumber)"
        }
    }
}

function Get-AsioDriverSummary {
    $paths = @(
        "HKLM:\SOFTWARE\ASIO",
        "HKLM:\SOFTWARE\WOW6432Node\ASIO"
    )
    $drivers = foreach ($path in $paths) {
        if (Test-Path $path) {
            Get-ChildItem -LiteralPath $path | ForEach-Object { $_.PSChildName }
        }
    }
    $drivers = @($drivers | Sort-Object -Unique)
    if ($drivers.Count -eq 0) {
        return "No ASIO registry entries detected."
    }
    return ($drivers -join "`n")
}

function Test-XmlFiles {
    param([string]$Root)
    $files = @(
        "res\skins\LateNight\toolbar.xml",
        "res\skins\Tango\topbar.xml",
        "res\skins\Deere\tool_bar.xml",
        "res\skins\Shade\mixer_panel.xml",
        "src\preferences\dialog\dlgprefsyncdlg.ui"
    )
    $result = @()
    foreach ($file in $files) {
        $path = Join-Path $Root $file
        try {
            [xml](Get-Content -LiteralPath $path -Raw) | Out-Null
            $result += "OK $file"
        } catch {
            $result += "FAIL ${file}: $($_.Exception.Message)"
        }
    }
    return ($result -join "`n")
}

function Run-LinkTests {
    param([string]$Root)
    $exe = Join-Path $Root "build\x64__abletonlink\mixxx-test.exe"
    if (!(Test-Path -LiteralPath $exe)) {
        return "SKIPPED: mixxx-test.exe not found at $exe"
    }
    $output = & $exe --gtest_filter=EngineSyncTest.Link*:EngineSyncTest.AbletonLink* 2>&1
    $exitCode = $LASTEXITCODE
    return "Exit code: $exitCode`n$($output -join "`n")"
}

$repo = Resolve-Path -LiteralPath $RepoRoot
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $OutputRoot $timestamp
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Push-Location $repo
try {
    $branch = (git rev-parse --abbrev-ref HEAD).Trim()
    $commit = (git rev-parse --short=12 HEAD).Trim()
    $status = (git status --short --branch) -join "`n"
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $diffCheck = (git diff --check) 2>&1
    $diffCheckExit = $LASTEXITCODE
    $ErrorActionPreference = $oldErrorActionPreference
} finally {
    Pop-Location
}

$osSummary = Get-WindowsVersionSummary
$processSummary = Get-ProcessSummary
$audioSummary = Get-AudioDeviceSummary
$asioSummary = Get-AsioDriverSummary
$xmlSummary = Test-XmlFiles -Root $repo
$testSummary = if ($RunTests) { Run-LinkTests -Root $repo } else { "SKIPPED: run with -RunTests to execute Link-focused mixxx-test suite." }

$state = @{
    checks = @{}
    notes = @{}
    evidence = @{}
    meta = @{
        mixxxCommit = "$commit ($branch)"
        windowsVersion = "$osSummary"
        audioInterface = "Detected devices:`n$audioSummary"
        asioDriver = "Detected ASIO registry entries:`n$asioSummary"
        network = "Preflight did not change network state."
        operator = $env:USERNAME
    }
}

if (![string]::IsNullOrWhiteSpace($EvidenceRoot) -and (Test-Path -LiteralPath $EvidenceRoot)) {
    $evidenceFiles = Get-ChildItem -LiteralPath $EvidenceRoot -File |
        Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|webp)$' -and $_.BaseName -match '^(s\d+-t\d+)__(.+)$' }
    foreach ($file in $evidenceFiles) {
        $taskId = [regex]::Match($file.BaseName, '^(s\d+-t\d+)__').Groups[1].Value
        if (!$state.evidence.ContainsKey($taskId)) {
            $state.evidence[$taskId] = @()
        }
        $uri = [System.Uri]::new($file.FullName).AbsoluteUri
        $state.evidence[$taskId] += @{
            label = $file.Name
            src = $uri
        }
    }
}

Set-Checked -State $state -TaskId "s0-t0" -Value ($processSummary -match "^No Mixxx")
Set-Checked -State $state -TaskId "s0-t1" -Value $true
Set-Checked -State $state -TaskId "s8-t2" -Value $true
Set-Checked -State $state -TaskId "s9-t0" -Value $true
Set-Checked -State $state -TaskId "s9-t2" -Value $true

Add-Note -State $state -TaskId "s0-t0" -Note "Preflight process check:`n$processSummary"
Add-Note -State $state -TaskId "s0-t1" -Note "Validation folder created:`n$outputDir"
Add-Note -State $state -TaskId "s1-t1" -Note "ASIO drivers detected in registry:`n$asioSummary"
Add-Note -State $state -TaskId "s6-t0" -Note "Windows audio devices detected:`n$audioSummary"
Add-Note -State $state -TaskId "s5-t0" -Note "Edited XML/UI parse preflight:`n$xmlSummary"
Add-Note -State $state -TaskId "s9-t0" -Note "Git branch/commit:`n$branch $commit`n`nStatus:`n$status`n`nDiff check exit code: $diffCheckExit`n$($diffCheck -join "`n")"
Add-Note -State $state -TaskId "s9-t2" -Note "$osSummary"
Add-Note -State $state -TaskId "s9-t5" -Note "Preflight output folder:`n$outputDir"
Add-Note -State $state -TaskId "s1-t5" -Note "Link-focused automated test preflight:`n$testSummary"

$jsonPath = Join-Path $outputDir "preflight-state.json"
$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$checklistPath = Join-Path $repo "res\abletonlink\windows-real-world-validation-checklist.html"
$sessionPath = Join-Path $outputDir "windows-real-world-validation-checklist-$timestamp.html"
$html = Get-Content -LiteralPath $checklistPath -Raw
$json = Get-Content -LiteralPath $jsonPath -Raw
$injection = "<script>`nwindow.__VALIDATION_PREFILL_FORCE__ = true;`nwindow.__VALIDATION_PREFILL__ = $json;`n</script>`n"
$html = $html -replace "</head>", "$injection</head>"
$html | Set-Content -LiteralPath $sessionPath -Encoding UTF8

"Preflight JSON: $jsonPath"
"Preflight HTML: $sessionPath"

if ($LaunchBrowser) {
    Start-Process -FilePath $sessionPath
}
