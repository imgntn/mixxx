param(
    [string]$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$OutputRoot = (Join-Path ([System.IO.Path]::GetTempPath()) "mixxx-ableton-link-audio-preflight"),
    [string]$CaptureDevice = "",
    [int]$BaselineSeconds = 5,
    [int]$PlaybackSeconds = 8,
    [int]$PlaybackVolume = 35,
    [switch]$LaunchBrowser = $true
)

$ErrorActionPreference = "Stop"

function Get-DShowAudioDevices {
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
    return $devices
}

function ConvertTo-ProcessArgument {
    param([AllowNull()][string]$Argument)
    if ($null -eq $Argument) {
        return '""'
    }
    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $quoted = '"'
    $backslashes = 0
    foreach ($char in $Argument.ToCharArray()) {
        if ($char -eq '\') {
            $backslashes += 1
        } elseif ($char -eq '"') {
            $quoted += ('\' * (($backslashes * 2) + 1))
            $quoted += '"'
            $backslashes = 0
        } else {
            if ($backslashes -gt 0) {
                $quoted += ('\' * $backslashes)
                $backslashes = 0
            }
            $quoted += $char
        }
    }
    if ($backslashes -gt 0) {
        $quoted += ('\' * ($backslashes * 2))
    }
    $quoted += '"'
    return $quoted
}

function Join-ProcessArguments {
    param([string[]]$Arguments)
    return (($Arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join " ")
}

function Start-HiddenProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$StdoutPath,
        [string]$StderrPath
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.Arguments = Join-ProcessArguments -Arguments $Arguments
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    if (-not [string]::IsNullOrWhiteSpace($StdoutPath)) {
        $psi.RedirectStandardOutput = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($StderrPath)) {
        $psi.RedirectStandardError = $true
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()

    $stdoutTask = $null
    $stderrTask = $null
    if ($psi.RedirectStandardOutput) {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    }
    if ($psi.RedirectStandardError) {
        $stderrTask = $process.StandardError.ReadToEndAsync()
    }

    return [pscustomobject]@{
        Process = $process
        StdoutTask = $stdoutTask
        StderrTask = $stderrTask
        StdoutPath = $StdoutPath
        StderrPath = $StderrPath
        CommandLine = "$FilePath $($psi.Arguments)"
    }
}

function Wait-HiddenProcess {
    param([object]$Handle)
    $Handle.Process.WaitForExit()
    if ($Handle.StdoutTask) {
        [System.IO.File]::WriteAllText($Handle.StdoutPath, $Handle.StdoutTask.Result)
    }
    if ($Handle.StderrTask) {
        [System.IO.File]::WriteAllText($Handle.StderrPath, $Handle.StderrTask.Result)
    }
    return $Handle.Process.ExitCode
}

function Start-FfmpegRecord {
    param(
        [string]$Device,
        [string]$OutputPath,
        [int]$Seconds,
        [string]$StdoutPath,
        [string]$StderrPath
    )
    $args = @(
        "-hide_banner",
        "-y",
        "-f", "dshow",
        "-t", "$Seconds",
        "-i", "audio=$Device",
        "-ac", "1",
        "-ar", "48000",
        "-c:a", "pcm_s16le",
        $OutputPath
    )
    return Start-HiddenProcess `
        -FilePath "ffmpeg" `
        -Arguments $args `
        -StdoutPath $StdoutPath `
        -StderrPath $StderrPath
}

function Run-FfmpegRecord {
    param(
        [string]$Device,
        [string]$OutputPath,
        [int]$Seconds,
        [string]$StdoutPath,
        [string]$StderrPath
    )
    $handle = Start-FfmpegRecord `
        -Device $Device `
        -OutputPath $OutputPath `
        -Seconds $Seconds `
        -StdoutPath $StdoutPath `
        -StderrPath $StderrPath
    $exitCode = Wait-HiddenProcess -Handle $handle
    if ($exitCode -ne 0) {
        throw "ffmpeg recording failed with exit code $exitCode. See $StderrPath"
    }
}

function Start-Ffplay {
    param(
        [string]$InputPath,
        [int]$Volume,
        [string]$StdoutPath,
        [string]$StderrPath
    )
    $args = @(
        "-hide_banner",
        "-nodisp",
        "-autoexit",
        "-volume", "$Volume",
        $InputPath
    )
    return Start-HiddenProcess `
        -FilePath "ffplay" `
        -Arguments $args `
        -StdoutPath $StdoutPath `
        -StderrPath $StderrPath
}

function ConvertTo-SafeFileName {
    param([string]$Value)
    $safe = $Value -replace '[\\/:*?"<>|]', "_"
    $safe = $safe -replace '\s+', "_"
    if ($safe.Length -gt 80) {
        $safe = $safe.Substring(0, 80)
    }
    return $safe
}

function Test-PythonSoundcard {
    & python -c "import soundcard" *> $null
    return ($LASTEXITCODE -eq 0)
}

function Invoke-PythonChecked {
    param([string[]]$Arguments)
    & python @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "python $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Invoke-WasapiLoopbackPreflight {
    param(
        [string]$LoopbackDir,
        [string]$ClickPath,
        [string]$Generator,
        [int]$BaselineSeconds,
        [int]$PlaybackSeconds,
        [int]$PlaybackVolume
    )

    New-Item -ItemType Directory -Force -Path $LoopbackDir | Out-Null
    $baselinePath = Join-Path $LoopbackDir "baseline-loopback.wav"
    $playbackPath = Join-Path $LoopbackDir "playback-loopback.wav"
    $baselineMeta = Join-Path $LoopbackDir "baseline-loopback-metadata.json"
    $playbackMeta = Join-Path $LoopbackDir "playback-loopback-metadata.json"
    $resultJson = Join-Path $LoopbackDir "audio-preflight-results.json"
    $waveformPng = Join-Path $LoopbackDir "audio-preflight-waveforms.png"
    $deviceLog = Join-Path $LoopbackDir "device.log"

    "Testing WASAPI loopback capture" | Set-Content -LiteralPath $deviceLog -Encoding UTF8

    try {
        Invoke-PythonChecked -Arguments @(
            $Generator,
            "record-loopback",
            "--output", $baselinePath,
            "--seconds", "$BaselineSeconds",
            "--metadata-json", $baselineMeta
        )
        Invoke-PythonChecked -Arguments @(
            $Generator,
            "record-loopback",
            "--output", $playbackPath,
            "--seconds", "$PlaybackSeconds",
            "--play-file", $ClickPath,
            "--volume", "$PlaybackVolume",
            "--metadata-json", $playbackMeta
        )

        $metadata = Get-Content -LiteralPath $playbackMeta -Raw | ConvertFrom-Json
        $deviceName = "WASAPI loopback: $($metadata.speaker_name)"

        Invoke-PythonChecked -Arguments @(
            $Generator,
            "analyze",
            "--baseline", $baselinePath,
            "--playback", $playbackPath,
            "--output-json", $resultJson,
            "--output-png", $waveformPng,
            "--capture-device", $deviceName,
            "--output-dir", $LoopbackDir
        )

        $analysis = Get-Content -LiteralPath $resultJson -Raw | ConvertFrom-Json
        $status = if ($analysis.signal_detected -and $analysis.transients_detected) {
            "signal-and-transients-detected"
        } elseif ($analysis.signal_detected) {
            "signal-detected"
        } elseif ($analysis.transients_detected) {
            "transients-detected"
        } else {
            "captured-no-playback-signal"
        }
        "Status: $status" | Add-Content -LiteralPath $deviceLog -Encoding UTF8

        return [pscustomobject]@{
            Device = $deviceName
            Status = $status
            Error = ""
            Analysis = $analysis
            OutputDir = $LoopbackDir
            ResultJson = $resultJson
            WaveformPng = $waveformPng
            Backend = "WASAPI loopback"
        }
    } catch {
        $message = $_.Exception.Message
        "Status: failed" | Add-Content -LiteralPath $deviceLog -Encoding UTF8
        "Error: $message" | Add-Content -LiteralPath $deviceLog -Encoding UTF8
        return [pscustomobject]@{
            Device = "WASAPI loopback"
            Status = "failed"
            Error = $message
            Analysis = $null
            OutputDir = $LoopbackDir
            ResultJson = $resultJson
            WaveformPng = $waveformPng
            Backend = "WASAPI loopback"
        }
    }
}

function Invoke-AudioCapturePreflight {
    param(
        [string]$Device,
        [string]$DeviceDir,
        [string]$ClickPath,
        [string]$Generator,
        [int]$BaselineSeconds,
        [int]$PlaybackSeconds,
        [int]$PlaybackVolume
    )

    New-Item -ItemType Directory -Force -Path $DeviceDir | Out-Null
    $baselinePath = Join-Path $DeviceDir "baseline.wav"
    $playbackPath = Join-Path $DeviceDir "playback.wav"
    $resultJson = Join-Path $DeviceDir "audio-preflight-results.json"
    $waveformPng = Join-Path $DeviceDir "audio-preflight-waveforms.png"
    $deviceLog = Join-Path $DeviceDir "device.log"

    "Testing capture device: $Device" | Set-Content -LiteralPath $deviceLog -Encoding UTF8

    try {
        Run-FfmpegRecord `
            -Device $Device `
            -OutputPath $baselinePath `
            -Seconds $BaselineSeconds `
            -StdoutPath (Join-Path $DeviceDir "baseline-ffmpeg.out.log") `
            -StderrPath (Join-Path $DeviceDir "baseline-ffmpeg.err.log")

        $recordProcess = Start-FfmpegRecord `
            -Device $Device `
            -OutputPath $playbackPath `
            -Seconds $PlaybackSeconds `
            -StdoutPath (Join-Path $DeviceDir "playback-record.out.log") `
            -StderrPath (Join-Path $DeviceDir "playback-record.err.log")

        Start-Sleep -Milliseconds 700
        $playProcess = Start-Ffplay `
            -InputPath $ClickPath `
            -Volume $PlaybackVolume `
            -StdoutPath (Join-Path $DeviceDir "ffplay.out.log") `
            -StderrPath (Join-Path $DeviceDir "ffplay.err.log")

        $playExitCode = Wait-HiddenProcess -Handle $playProcess
        $recordExitCode = Wait-HiddenProcess -Handle $recordProcess
        if ($playExitCode -ne 0) {
            throw "Playback helper failed with exit code $playExitCode. See ffplay.err.log"
        }
        if ($recordExitCode -ne 0) {
            throw "Playback recording failed with exit code $recordExitCode. See playback-record.err.log"
        }

        & python $Generator analyze `
            --baseline $baselinePath `
            --playback $playbackPath `
            --output-json $resultJson `
            --output-png $waveformPng `
            --capture-device $Device `
            --output-dir $DeviceDir

        $analysis = Get-Content -LiteralPath $resultJson -Raw | ConvertFrom-Json
        $status = if ($analysis.signal_detected -and $analysis.transients_detected) {
            "signal-and-transients-detected"
        } elseif ($analysis.signal_detected) {
            "signal-detected"
        } else {
            "captured-no-playback-signal"
        }
        "Status: $status" | Add-Content -LiteralPath $deviceLog -Encoding UTF8

        return [pscustomobject]@{
            Device = $Device
            Status = $status
            Error = ""
            Analysis = $analysis
            OutputDir = $DeviceDir
            ResultJson = $resultJson
            WaveformPng = $waveformPng
            Backend = "DirectShow"
        }
    } catch {
        $message = $_.Exception.Message
        "Status: failed" | Add-Content -LiteralPath $deviceLog -Encoding UTF8
        "Error: $message" | Add-Content -LiteralPath $deviceLog -Encoding UTF8
        return [pscustomobject]@{
            Device = $Device
            Status = "failed"
            Error = $message
            Analysis = $null
            OutputDir = $DeviceDir
            ResultJson = $resultJson
            WaveformPng = $waveformPng
            Backend = "DirectShow"
        }
    }
}

$repo = Resolve-Path -LiteralPath $RepoRoot
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $OutputRoot $timestamp
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$devices = @(Get-DShowAudioDevices)
if (-not [string]::IsNullOrWhiteSpace($CaptureDevice) -and $devices -notcontains $CaptureDevice) {
    throw "Capture device '$CaptureDevice' was not found. Detected devices: $($devices -join ', ')"
}
$devicesToTest = if ([string]::IsNullOrWhiteSpace($CaptureDevice)) { $devices } else { @($CaptureDevice) }

$clickPath = Join-Path $outputDir "audio-preflight-click-test.wav"
$summaryJson = Join-Path $outputDir "audio-preflight-summary.json"
$checklistJson = Join-Path $outputDir "audio-preflight-checklist-state.json"
$sessionHtml = Join-Path $outputDir "audio-preflight-checklist-$timestamp.html"

$generator = Join-Path $repo "res\abletonlink\windows_audio_preflight_analyze.py"
& python $generator generate-click --output $clickPath --seconds $PlaybackSeconds

$wasapiLoopbackAvailable = Test-PythonSoundcard
"WASAPI loopback available through Python soundcard: $wasapiLoopbackAvailable" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log")
"Audio capture devices detected:" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
$devices | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
"Testing capture devices:" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
$devicesToTest | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append

$runs = @()
if ($wasapiLoopbackAvailable) {
    $loopbackRun = Invoke-WasapiLoopbackPreflight `
        -LoopbackDir (Join-Path $outputDir "WASAPI_loopback") `
        -ClickPath $clickPath `
        -Generator $generator `
        -BaselineSeconds $BaselineSeconds `
        -PlaybackSeconds $PlaybackSeconds `
        -PlaybackVolume $PlaybackVolume
    $runs += $loopbackRun
    "$($loopbackRun.Device): $($loopbackRun.Status)" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
    if (-not [string]::IsNullOrWhiteSpace($loopbackRun.Error)) {
        "  $($loopbackRun.Error)" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
    }
}
foreach ($device in $devicesToTest) {
    $deviceDir = Join-Path $outputDir (ConvertTo-SafeFileName -Value $device)
    $run = Invoke-AudioCapturePreflight `
        -Device $device `
        -DeviceDir $deviceDir `
        -ClickPath $clickPath `
        -Generator $generator `
        -BaselineSeconds $BaselineSeconds `
        -PlaybackSeconds $PlaybackSeconds `
        -PlaybackVolume $PlaybackVolume
    $runs += $run
    "$($run.Device): $($run.Status)" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
    if (-not [string]::IsNullOrWhiteSpace($run.Error)) {
        "  $($run.Error)" | Tee-Object -FilePath (Join-Path $outputDir "audio-preflight.log") -Append
    }
}

$capturedRuns = @($runs | Where-Object { $null -ne $_.Analysis })
$bestRun = $capturedRuns |
    Sort-Object `
        @{ Expression = { if ($_.Analysis.signal_detected) { 1 } else { 0 } }; Descending = $true },
        @{ Expression = { if ($_.Analysis.transients_detected) { 1 } else { 0 } }; Descending = $true },
        @{ Expression = { [double]$_.Analysis.rms_increase_db }; Descending = $true },
        @{ Expression = { [double]$_.Analysis.playback.peak }; Descending = $true } |
    Select-Object -First 1

$summary = [pscustomobject]@{
    output_dir = $outputDir
    tested_devices = $devicesToTest
    ffmpeg_capture_backend = "DirectShow"
    wasapi_loopback_available = $wasapiLoopbackAvailable
    playback_volume = $PlaybackVolume
    baseline_seconds = $BaselineSeconds
    playback_seconds = $PlaybackSeconds
    best_device = if ($bestRun) { $bestRun.Device } else { "" }
    runs = @($runs | ForEach-Object {
        [pscustomobject]@{
            device = $_.Device
            status = $_.Status
            backend = $_.Backend
            error = $_.Error
            output_dir = $_.OutputDir
            result_json = if ($_.Analysis) { $_.ResultJson } else { "" }
            waveform_png = if ($_.Analysis) { $_.WaveformPng } else { "" }
            rms_increase_db = if ($_.Analysis) { $_.Analysis.rms_increase_db } else { $null }
            signal_detected = if ($_.Analysis) { [bool]$_.Analysis.signal_detected } else { $false }
            transients_detected = if ($_.Analysis) { [bool]$_.Analysis.transients_detected } else { $false }
            transient_count = if ($_.Analysis) { $_.Analysis.transient_count } else { 0 }
        }
    })
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryJson -Encoding UTF8

$state = @{
    checks = @{}
    notes = @{}
    evidence = @{
        "s0-t5" = @()
        "s1-t9" = @()
    }
    meta = @{
        audioInterface = "WASAPI loopback through Python soundcard: $wasapiLoopbackAvailable`nFFmpeg capture backend: DirectShow`nWASAPI loopback in this FFmpeg build: no`nDetected capture devices:`n$($devices -join "`n")"
    }
}

$evidence = @()
foreach ($run in $capturedRuns) {
    if (Test-Path -LiteralPath $run.WaveformPng) {
        $evidence += @{
            label = "Audio preflight waveform: $($run.Device)"
            src = ([System.Uri]::new($run.WaveformPng)).AbsoluteUri
        }
    }
}
$state.evidence["s0-t5"] = $evidence
$state.evidence["s1-t9"] = $evidence

$bestAnalysis = if ($bestRun) { $bestRun.Analysis } else { $null }
$signalText = if ($bestAnalysis -and $bestAnalysis.signal_detected) { "PASS: playback signal detected above baseline." } else { "CHECK: playback signal was not clearly above baseline on any tested DirectShow capture device." }
$transientText = if ($bestAnalysis -and $bestAnalysis.transients_detected) { "PASS: click transients detected." } else { "CHECK: click transients were not clearly detected on any tested DirectShow capture device." }
$runLines = @($runs | ForEach-Object {
    $line = "- $($_.Device): $($_.Status)"
    if ($_.Analysis) {
        $line += " (RMS increase $($_.Analysis.rms_increase_db) dB, transients $($_.Analysis.transient_count))"
    }
    if (-not [string]::IsNullOrWhiteSpace($_.Error)) {
        $line += " - $($_.Error)"
    }
    $line
}) -join "`n"
$note = @"
Audio preflight output:
$outputDir

Best readable capture device:
$(if ($bestRun) { $bestRun.Device } else { "none" })

$signalText
$transientText

Per-device results:
$runLines

$(if ($bestAnalysis) {
"Baseline RMS dBFS: $($bestAnalysis.baseline.rms_dbfs)
Playback RMS dBFS: $($bestAnalysis.playback.rms_dbfs)
RMS increase dB: $($bestAnalysis.rms_increase_db)
Playback peak dBFS: $($bestAnalysis.playback.peak_dbfs)
Detected transient count: $($bestAnalysis.transient_count)
Estimated dropout windows: $($bestAnalysis.dropout_windows)"
} else {
"No tested capture device produced readable audio files."
})

This verifies captured audio activity when the active Windows capture path can hear the default playback path. Python soundcard WASAPI loopback available: $wasapiLoopbackAvailable. FFmpeg DirectShow capture remains a fallback for physical or virtual recording devices.
"@

$state.notes["s0-t5"] = $note
$state.notes["s1-t9"] = $note
$state.notes["s9-t5"] = "Audio preflight logs/results:`n$outputDir"
$state.checks["s0-t5"] = [bool]($bestAnalysis -and $bestAnalysis.signal_detected)
$state.checks["s1-t9"] = [bool]($bestAnalysis -and $bestAnalysis.signal_detected -and $bestAnalysis.transients_detected)

$state | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $checklistJson -Encoding UTF8

$checklistPath = Join-Path $repo "res\abletonlink\windows-real-world-validation-checklist.html"
$json = Get-Content -LiteralPath $checklistJson -Raw
$html = Get-Content -LiteralPath $checklistPath -Raw
$injection = "<script>`nwindow.__VALIDATION_PREFILL_FORCE__ = true;`nwindow.__VALIDATION_PREFILL__ = $json;`n</script>`n"
$html = $html -replace "</head>", "$injection</head>"
$html | Set-Content -LiteralPath $sessionHtml -Encoding UTF8

"Audio preflight summary: $summaryJson"
"Audio preflight checklist: $sessionHtml"

if ($LaunchBrowser) {
    Start-Process -FilePath $sessionHtml
}
