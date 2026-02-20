#Requires -Version 5.1
<#
.SYNOPSIS
  Realistic checkpoint/resume QA for differential + linear single-run flows,
  differential single-run collector resume, plus hull batch-resume regression checks.

.DESCRIPTION
  Runs a real time-only search (maxnodes=0, maxsec>0), writes a binary checkpoint,
  resumes from it, and verifies node-count continuity:

    resume_start.total_nodes_visited == checkpoint_stop.total_nodes_visited
    resume_stop.total_nodes_visited  >  resume_start.total_nodes_visited
    (resume_stop.total - resume_start.total) == resume_stop.run_nodes_visited

  This avoids the old tiny-maxnodes smoke style that can finish instantly on a fast machine.
  If the linear single-run detail case completes before the budget on a fast machine,
  the script logs that fact and relies on the linear auto/batch resume coverage instead
  of treating it as a hard failure.
  The script also verifies single-run differential collector resume continuity and runs the hull-wrapper batch-resume selftests, which cover both
  source-selection checkpoints and selected-source strict-hull checkpoints.
#>
param(
    [int] $CheckpointSeconds = 300,
    [int] $ResumeSeconds = 60,
    [int] $CheckpointEverySeconds = 60,
    [int] $ResumeCheckpointEverySeconds = 30,
    [int] $AutoCheckpointEverySeconds = 2,
    [int] $AutoResumeCheckpointEverySeconds = 2,
    [int] $AutoEventTimeoutSeconds = 45,
    [int] $AutoBreadthJobs = 512,
    [int] $AutoBreadthThreads = 1,
    [int] $AutoBreadthMaxNodes = 20000000,
    [int] $AutoDeepBreadthJobs = 8,
    [int] $AutoDeepBreadthMaxNodes = 5000000,
    [int] $AutoDeepResumeSeconds = 12,
    [int] $CheckpointProgressSeconds = 60,
    [int] $ResumeProgressSeconds = 20,
    [int] $RoundCount = 4,
    [int] $Threads = 1,
    [string] $DifferentialDeltaA = '0x1',
    [string] $DifferentialDeltaB = '0x2',
    [string] $LinearMaskA = '0x1',
    [string] $LinearMaskB = '0x2',
    [string] $ProjectRoot = ''
)

$ErrorActionPreference = 'Stop'
$script:QaLogPath = $null
$script:DiffAutoDeepBreadthMaxNodesFloor = [UInt64]100000000
$script:CheckpointMagic = 0x4B435A4E
$script:CheckpointEndianTag = 0x01020304
$script:CheckpointVersion = 1
$script:CheckpointKinds = @{
    LinearResidualFrontierBest = 13
    DifferentialResidualFrontierBest = 14
    LinearResidualFrontierCollector = 15
    DifferentialResidualFrontierCollector = 16
    DifferentialHullBatch = 6
    LinearHullBatch = 5
}

function Write-Log([string]$Message) {
    $line = "[$((Get-Date).ToString('o'))] $Message"
    if (-not [string]::IsNullOrWhiteSpace($script:QaLogPath)) {
        Add-Content -LiteralPath $script:QaLogPath -Value $line -Encoding UTF8
    }
    Write-Host $line
}

function Invoke-QaStep {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][string] $ExePath,
        [Parameter(Mandatory = $true)][string[]] $ExeArguments,
        [string] $WorkingDirectory = ''
    )

    Write-Log "BEGIN $Name"
    Write-Log ("  exe: " + $ExePath)
    Write-Log ("  args: " + ($ExeArguments -join ' '))

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $code = 0
    try {
        if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
            & $ExePath @ExeArguments 2>&1 | ForEach-Object {
                $s = "$_"
                if (-not [string]::IsNullOrWhiteSpace($script:QaLogPath)) {
                    Add-Content -LiteralPath $script:QaLogPath -Value $s -Encoding UTF8
                }
                Write-Host $s
            }
            $code = $LASTEXITCODE
        }
        else {
            Push-Location -LiteralPath $WorkingDirectory
            try {
                & $ExePath @ExeArguments 2>&1 | ForEach-Object {
                    $s = "$_"
                    if (-not [string]::IsNullOrWhiteSpace($script:QaLogPath)) {
                        Add-Content -LiteralPath $script:QaLogPath -Value $s -Encoding UTF8
                    }
                    Write-Host $s
                }
                $code = $LASTEXITCODE
            }
            finally {
                Pop-Location
            }
        }
    }
    finally {
        $sw.Stop()
    }

    Write-Log "END $Name  exit=$code  wall_sec=$([math]::Round($sw.Elapsed.TotalSeconds, 2))"
    if ($code -ne 0) {
        throw "Step failed: $Name (exit $code)"
    }
}

function Read-RuntimeEvents {
    param([Parameter(Mandatory = $true)][string] $Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing runtime log: $Path"
    }

    $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    $blocks = $raw -split "(?m)^=== runtime_event ===\r?\n"
    $events = @()

    foreach ($block in $blocks) {
        if ([string]::IsNullOrWhiteSpace($block)) { continue }

        $map = @{}
        foreach ($line in ($block -split "\r?\n")) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $idx = $line.IndexOf('=')
            if ($idx -lt 0) { continue }
            $key = $line.Substring(0, $idx).Trim()
            $value = $line.Substring($idx + 1).Trim()
            $map[$key] = $value
        }

        if ($map.ContainsKey('event')) {
            $events += [pscustomobject]$map
        }
    }

    return ,$events
}

function Get-LatestRuntimeEvent {
    param(
        [Parameter(Mandatory = $true)][object[]] $Events,
        [Parameter(Mandatory = $true)][string] $EventName
    )

    $matches = @($Events | Where-Object { $_.event -eq $EventName })
    if ($matches.Count -eq 0) { return $null }
    return $matches[-1]
}

function Read-UInt64Field {
    param(
        [Parameter(Mandatory = $true)][object] $Event,
        [Parameter(Mandatory = $true)][string] $FieldName
    )

    $raw = "$($Event.$FieldName)".Trim()
    [UInt64] $value = 0
    if (-not [UInt64]::TryParse($raw, [ref]$value)) {
        throw "Unable to parse UInt64 field '$FieldName' from runtime event '$($Event.event)': '$raw'"
    }
    return $value
}

function Read-StringField {
    param(
        [Parameter(Mandatory = $true)][object] $Event,
        [Parameter(Mandatory = $true)][string] $FieldName
    )

    if (-not $Event.PSObject.Properties.Name.Contains($FieldName)) {
        throw "Missing field '$FieldName' in runtime event '$($Event.event)'"
    }
    return "$($Event.$FieldName)".Trim()
}

function Get-LatestMatchingRuntimeEvent {
    param(
        [Parameter(Mandatory = $true)][object[]] $Events,
        [Parameter(Mandatory = $true)][string] $EventName,
        [string] $FieldName = '',
        [string] $ExpectedValue = ''
    )

    $matches = @($Events | Where-Object {
        if ($_.event -ne $EventName) { return $false }
        if ([string]::IsNullOrWhiteSpace($FieldName)) { return $true }
        return ("$($_.$FieldName)".Trim() -eq $ExpectedValue)
    })
    if ($matches.Count -eq 0) { return $null }
    return $matches[-1]
}

function Get-LatestSuccessfulCheckpointWrite {
    param(
        [Parameter(Mandatory = $true)][object[]] $Events,
        [Parameter(Mandatory = $true)][string] $EventName,
        [string] $Stage = '',
        [string] $CheckpointPath = ''
    )

    $matches = @($Events | Where-Object {
        if ($_.event -ne $EventName) { return $false }
        if ("$($_.binary_checkpoint_write_result)".Trim() -ne 'success') { return $false }
        if (-not [string]::IsNullOrWhiteSpace($Stage) -and "$($_.stage)".Trim() -ne $Stage) { return $false }
        if (-not [string]::IsNullOrWhiteSpace($CheckpointPath) -and "$($_.checkpoint_path)".Trim() -ne $CheckpointPath) { return $false }
        return $true
    })
    if ($matches.Count -eq 0) { return $null }
    return $matches[-1]
}

function Get-LatestSuccessfulBatchCheckpointWrite {
    param(
        [Parameter(Mandatory = $true)][object[]] $Events,
        [string] $CheckpointKind = '',
        [string] $CheckpointReason = '',
        [string] $CheckpointPath = ''
    )

    $matches = @($Events | Where-Object {
        if ($_.event -ne 'batch_checkpoint_write') { return $false }
        if ("$($_.checkpoint_write_result)".Trim() -ne 'success') { return $false }
        if (-not [string]::IsNullOrWhiteSpace($CheckpointKind) -and "$($_.checkpoint_kind)".Trim() -ne $CheckpointKind) { return $false }
        if (-not [string]::IsNullOrWhiteSpace($CheckpointReason) -and "$($_.checkpoint_reason)".Trim() -ne $CheckpointReason) { return $false }
        if (-not [string]::IsNullOrWhiteSpace($CheckpointPath) -and "$($_.checkpoint_path)".Trim() -ne $CheckpointPath) { return $false }
        return $true
    })
    if ($matches.Count -eq 0) { return $null }
    return $matches[-1]
}

function Assert-PeriodicCheckpointActivity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][object[]] $Events,
        [Parameter(Mandatory = $true)][string[]] $EventNames,
        [string] $Stage = ''
    )

    $matches = @($Events | Where-Object {
        if ($EventNames -notcontains $_.event) { return $false }
        if ("$($_.binary_checkpoint_write_result)".Trim() -ne 'success') { return $false }
        if ("$($_.checkpoint_reason)".Trim() -ne 'periodic_timer') { return $false }
        if ([string]::IsNullOrWhiteSpace($Stage)) { return $true }
        return ("$($_.stage)".Trim() -eq $Stage)
    })

    if ($matches.Count -eq 0) {
        $eventList = ($EventNames -join ', ')
        if ([string]::IsNullOrWhiteSpace($Stage)) {
            throw "$Label failed: no periodic_timer checkpoint write found in events {$eventList}"
        }
        throw "$Label failed: no periodic_timer checkpoint write found in stage '$Stage' for events {$eventList}"
    }

    Write-Log "$Label periodic checkpoint verified: count=$($matches.Count)"
    return $matches[-1]
}

function Assert-SingleRunResumeFingerprintContinuity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $CheckpointRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeRuntimeLogPath,
        [Parameter(Mandatory = $true)][UInt64] $ExpectedResumeCheckpointEverySeconds,
        [switch] $Differential
    )

    $checkpointEvents = Read-RuntimeEvents -Path $CheckpointRuntimeLogPath
    $resumeEvents = Read-RuntimeEvents -Path $ResumeRuntimeLogPath

    $checkpointWrite = Get-LatestSuccessfulCheckpointWrite -Events $checkpointEvents -EventName 'checkpoint_write'
    $resumeStart = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'resume_start'
    if (-not $checkpointWrite) { throw "$Label failed: missing successful checkpoint_write" }
    if (-not $resumeStart) { throw "$Label failed: missing resume_start" }

    $checkpointHash = Read-StringField -Event $checkpointWrite -FieldName 'resume_fingerprint_hash'
    $resumeHash = Read-StringField -Event $resumeStart -FieldName 'resume_fingerprint_hash'
    if ($checkpointHash -ne $resumeHash) {
        throw "$Label failed: resume fingerprint mismatch checkpoint=$checkpointHash resume=$resumeHash"
    }

    $resumeRunNodes = Read-UInt64Field -Event $resumeStart -FieldName 'run_nodes_visited'
    if ($resumeRunNodes -ne 0) {
        throw "$Label failed: resume_start.run_nodes_visited=$resumeRunNodes, expected 0"
    }

    $resumeCheckpointEvery = Read-UInt64Field -Event $resumeStart -FieldName 'runtime_checkpoint_every_seconds'
    if ($resumeCheckpointEvery -ne $ExpectedResumeCheckpointEverySeconds) {
        throw "$Label failed: resume_start.runtime_checkpoint_every_seconds=$resumeCheckpointEvery, expected $ExpectedResumeCheckpointEverySeconds"
    }

    Assert-PeriodicCheckpointActivity -Label $Label -Events $resumeEvents -EventNames @('checkpoint_write') | Out-Null

    if ($Differential) {
        $checkpointShellEntries = Read-UInt64Field -Event $checkpointWrite -FieldName 'resume_fingerprint_modular_add_shell_cache_entries'
        $resumeShellEntries = Read-UInt64Field -Event $resumeStart -FieldName 'resume_fingerprint_modular_add_shell_cache_entries'
        if ($checkpointShellEntries -ne $resumeShellEntries) {
            throw "$Label failed: modular-add shell cache entry count mismatch checkpoint=$checkpointShellEntries resume=$resumeShellEntries"
        }
        $checkpointShellHash = Read-StringField -Event $checkpointWrite -FieldName 'resume_fingerprint_modular_add_shell_cache_hash'
        $resumeShellHash = Read-StringField -Event $resumeStart -FieldName 'resume_fingerprint_modular_add_shell_cache_hash'
        if ($checkpointShellHash -ne $resumeShellHash) {
            throw "$Label failed: modular-add shell cache hash mismatch checkpoint=$checkpointShellHash resume=$resumeShellHash"
        }
    }

    Write-Log "$Label fingerprint continuity verified: hash=$resumeHash"
}

function Assert-AutoPipelineResumeContinuity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $CheckpointRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeCheckpointPath,
        [Parameter(Mandatory = $true)][string] $ExpectedStage,
        [Parameter(Mandatory = $true)][UInt64] $ExpectedResumeCheckpointEverySeconds,
        [Parameter(Mandatory = $true)][string[]] $PeriodicEventNames,
        [switch] $SkipPeriodicRequirement
    )

    $checkpointEvents = Read-RuntimeEvents -Path $CheckpointRuntimeLogPath
    $resumeEvents = Read-RuntimeEvents -Path $ResumeRuntimeLogPath

    $checkpointWrite = Get-LatestSuccessfulCheckpointWrite -Events $checkpointEvents -EventName 'auto_checkpoint_write' -Stage $ExpectedStage -CheckpointPath $ResumeCheckpointPath
    $resumeStart = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'auto_resume_start'
    if (-not $checkpointWrite) { throw "$Label failed: missing successful auto_checkpoint_write for stage '$ExpectedStage' and checkpoint_path '$ResumeCheckpointPath'" }
    if (-not $resumeStart) { throw "$Label failed: missing auto_resume_start" }

    $resumeStage = Read-StringField -Event $resumeStart -FieldName 'stage'
    if ($resumeStage -ne $ExpectedStage) {
        throw "$Label failed: auto_resume_start.stage=$resumeStage, expected $ExpectedStage"
    }

    $checkpointHash = Read-StringField -Event $checkpointWrite -FieldName 'auto_pipeline_fingerprint_hash'
    $resumeHash = Read-StringField -Event $resumeStart -FieldName 'auto_pipeline_fingerprint_hash'
    if ($checkpointHash -ne $resumeHash) {
        $resumeStageStart = Get-LatestMatchingRuntimeEvent -Events $resumeEvents -EventName 'auto_checkpoint_write' -FieldName 'checkpoint_reason' -ExpectedValue 'resume_stage_start'
        if ($ExpectedStage -eq 'breadth' -and $resumeStageStart) {
            $resumeStageStartStage = Read-StringField -Event $resumeStageStart -FieldName 'stage'
            $resumeStageStartPath = Read-StringField -Event $resumeStageStart -FieldName 'checkpoint_path'
            $resumeStageStartHash = Read-StringField -Event $resumeStageStart -FieldName 'auto_pipeline_fingerprint_hash'
            if ($resumeStageStartStage -eq $ExpectedStage -and $resumeStageStartPath -eq $ResumeCheckpointPath -and $resumeStageStartHash -eq $resumeHash) {
                Write-Log "$Label note: latest checkpoint runtime event likely raced forced-stop; resume_stage_start hash=$resumeHash matches loaded checkpoint state."
            }
            else {
                throw "$Label failed: auto pipeline fingerprint mismatch checkpoint=$checkpointHash resume=$resumeHash"
            }
        }
        else {
            throw "$Label failed: auto pipeline fingerprint mismatch checkpoint=$checkpointHash resume=$resumeHash"
        }
    }

    $resumeCheckpointEvery = Read-UInt64Field -Event $resumeStart -FieldName 'runtime_checkpoint_every_seconds'
    if ($resumeCheckpointEvery -ne $ExpectedResumeCheckpointEverySeconds) {
        throw "$Label failed: auto_resume_start.runtime_checkpoint_every_seconds=$resumeCheckpointEvery, expected $ExpectedResumeCheckpointEverySeconds"
    }

    if ($SkipPeriodicRequirement) {
        $periodicMatches = @($resumeEvents | Where-Object {
            if ($PeriodicEventNames -notcontains $_.event) { return $false }
            if ("$($_.checkpoint_reason)".Trim() -ne 'periodic_timer') { return $false }
            return ("$($_.stage)".Trim() -eq $ExpectedStage)
        })
        if ($periodicMatches.Count -gt 0) {
            Write-Log "$Label note: periodic auto-pipeline checkpoint observed during resume (count=$($periodicMatches.Count))."
        }
        else {
            Write-Log "$Label note: no periodic auto-pipeline checkpoint was observed before the resume run ended; continuity is accepted based on resume fingerprint/stage matching."
        }
    }
    else {
        Assert-PeriodicCheckpointActivity -Label $Label -Events $resumeEvents -EventNames $PeriodicEventNames -Stage $ExpectedStage | Out-Null
    }
    Write-Log "$Label auto pipeline continuity verified: stage=$ExpectedStage hash=$resumeHash"
}

function Assert-WrapperBatchResumeContinuity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $CheckpointRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeCheckpointPath,
        [Parameter(Mandatory = $true)][string] $ExpectedCheckpointKind,
        [string] $ExpectedStage = ''
    )

    $checkpointEvents = Read-RuntimeEvents -Path $CheckpointRuntimeLogPath
    $resumeEvents = Read-RuntimeEvents -Path $ResumeRuntimeLogPath

    $checkpointWrite = Get-LatestSuccessfulBatchCheckpointWrite -Events $checkpointEvents -CheckpointKind $ExpectedCheckpointKind -CheckpointPath $ResumeCheckpointPath
    $resumeStart = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'batch_resume_start'
    $resumeStop = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'batch_stop'

    if (-not $checkpointWrite) { throw "$Label failed: missing successful batch_checkpoint_write for kind '$ExpectedCheckpointKind' and checkpoint_path '$ResumeCheckpointPath'" }
    if (-not $resumeStart) { throw "$Label failed: missing batch_resume_start" }
    if (-not $resumeStop) { throw "$Label failed: missing batch_stop" }

    $resumeKind = Read-StringField -Event $resumeStart -FieldName 'checkpoint_kind'
    if ($resumeKind -ne $ExpectedCheckpointKind) {
        throw "$Label failed: batch_resume_start.checkpoint_kind=$resumeKind, expected $ExpectedCheckpointKind"
    }

    $checkpointStage = Read-StringField -Event $checkpointWrite -FieldName 'stage'
    $expectedStageResolved = if ([string]::IsNullOrWhiteSpace($ExpectedStage)) { $checkpointStage } else { $ExpectedStage }
    $resumeStage = Read-StringField -Event $resumeStart -FieldName 'stage'
    if ($resumeStage -ne $expectedStageResolved) {
        throw "$Label failed: batch_resume_start.stage=$resumeStage, expected $expectedStageResolved"
    }

    $checkpointHash = Read-StringField -Event $checkpointWrite -FieldName 'batch_resume_fingerprint_hash'
    $resumeHash = Read-StringField -Event $resumeStart -FieldName 'batch_resume_fingerprint_hash'
    if ($checkpointHash -ne $resumeHash) {
        throw "$Label failed: batch fingerprint mismatch checkpoint=$checkpointHash resume=$resumeHash"
    }

    $checkpointCompletedJobs = Read-UInt64Field -Event $checkpointWrite -FieldName 'batch_resume_fingerprint_completed_jobs'
    $resumeCompletedJobs = Read-UInt64Field -Event $resumeStart -FieldName 'batch_resume_fingerprint_completed_jobs'
    if ($checkpointCompletedJobs -ne $resumeCompletedJobs) {
        throw "$Label failed: completed job count mismatch checkpoint=$checkpointCompletedJobs resume=$resumeCompletedJobs"
    }

    $stopJobs = Read-UInt64Field -Event $resumeStop -FieldName 'jobs'
    if ($stopJobs -lt 1) {
        throw "$Label failed: batch_stop.jobs=$stopJobs, expected >= 1"
    }

    Write-Log "$Label wrapper batch resume continuity verified: kind=$ExpectedCheckpointKind stage=$expectedStageResolved hash=$resumeHash"
}

function Assert-WrapperBatchResumeStartContinuity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $CheckpointRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeCheckpointPath,
        [Parameter(Mandatory = $true)][string] $ExpectedCheckpointKind,
        [string] $ExpectedStage = ''
    )

    $checkpointEvents = Read-RuntimeEvents -Path $CheckpointRuntimeLogPath
    $resumeEvents = Read-RuntimeEvents -Path $ResumeRuntimeLogPath

    $checkpointWrite = Get-LatestSuccessfulBatchCheckpointWrite -Events $checkpointEvents -CheckpointKind $ExpectedCheckpointKind -CheckpointPath $ResumeCheckpointPath
    $resumeStart = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'batch_resume_start'

    if (-not $checkpointWrite) { throw "$Label failed: missing successful batch_checkpoint_write for kind '$ExpectedCheckpointKind' and checkpoint_path '$ResumeCheckpointPath'" }
    if (-not $resumeStart) { throw "$Label failed: missing batch_resume_start" }

    $resumeKind = Read-StringField -Event $resumeStart -FieldName 'checkpoint_kind'
    if ($resumeKind -ne $ExpectedCheckpointKind) {
        throw "$Label failed: batch_resume_start.checkpoint_kind=$resumeKind, expected $ExpectedCheckpointKind"
    }

    $checkpointStage = Read-StringField -Event $checkpointWrite -FieldName 'stage'
    $expectedStageResolved = if ([string]::IsNullOrWhiteSpace($ExpectedStage)) { $checkpointStage } else { $ExpectedStage }
    $resumeStage = Read-StringField -Event $resumeStart -FieldName 'stage'
    if ($resumeStage -ne $expectedStageResolved) {
        throw "$Label failed: batch_resume_start.stage=$resumeStage, expected $expectedStageResolved"
    }

    $checkpointHash = Read-StringField -Event $checkpointWrite -FieldName 'batch_resume_fingerprint_hash'
    $resumeHash = Read-StringField -Event $resumeStart -FieldName 'batch_resume_fingerprint_hash'
    if ($checkpointHash -ne $resumeHash) {
        throw "$Label failed: batch fingerprint mismatch checkpoint=$checkpointHash resume=$resumeHash"
    }

    $checkpointCompletedJobs = Read-UInt64Field -Event $checkpointWrite -FieldName 'batch_resume_fingerprint_completed_jobs'
    $resumeCompletedJobs = Read-UInt64Field -Event $resumeStart -FieldName 'batch_resume_fingerprint_completed_jobs'
    if ($checkpointCompletedJobs -ne $resumeCompletedJobs) {
        throw "$Label failed: completed job count mismatch checkpoint=$checkpointCompletedJobs resume=$resumeCompletedJobs"
    }

    Write-Log "$Label wrapper batch resume start continuity verified: kind=$ExpectedCheckpointKind stage=$expectedStageResolved hash=$resumeHash"
}

function Append-ProcessArtifacts {
    param(
        [Parameter(Mandatory = $true)][string] $StdoutPath,
        [Parameter(Mandatory = $true)][string] $StderrPath
    )

    foreach ($path in @($StdoutPath, $StderrPath)) {
        if (Test-Path -LiteralPath $path) {
            Get-Content -LiteralPath $path -Encoding UTF8 | ForEach-Object {
                $s = "$_"
                if (-not [string]::IsNullOrWhiteSpace($script:QaLogPath)) {
                    Add-Content -LiteralPath $script:QaLogPath -Value $s -Encoding UTF8
                }
                Write-Host $s
            }
        }
    }
}

function Wait-ForRuntimeEvent {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $EventName,
        [int] $TimeoutSeconds = 30,
        [string] $FieldName = '',
        [string] $ExpectedValue = ''
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $Path) {
            $events = Read-RuntimeEvents -Path $Path
            $match = Get-LatestMatchingRuntimeEvent -Events $events -EventName $EventName -FieldName $FieldName -ExpectedValue $ExpectedValue
            if ($match) { return $match }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    if ([string]::IsNullOrWhiteSpace($FieldName)) {
        throw "Timed out waiting for runtime event '$EventName' in $Path"
    }
    throw "Timed out waiting for runtime event '$EventName' with $FieldName='$ExpectedValue' in $Path"
}

function ConvertTo-ProcessArgumentString {
    param([Parameter(Mandatory = $true)][string[]] $Arguments)

    $quoted = foreach ($arg in $Arguments) {
        '"' + ($arg -replace '"', '\"') + '"'
    }
    return ($quoted -join ' ')
}

function Invoke-QaInterruptibleStep {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][string] $ExePath,
        [Parameter(Mandatory = $true)][string[]] $ExeArguments,
        [Parameter(Mandatory = $true)][string] $RuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $WaitEventName,
        [string] $WaitFieldName = '',
        [string] $WaitFieldValue = '',
        [int] $TimeoutSeconds = 30,
        [int] $GraceSeconds = 2,
        [switch] $AllowEarlyCompletion
    )

    Write-Log "BEGIN $Name"
    Write-Log ("  exe: " + $ExePath)
    Write-Log ("  args: " + ($ExeArguments -join ' '))

    $exeResolved = (Get-Item -LiteralPath $ExePath).FullName
    Write-Log ("  exe_resolved: " + $exeResolved)
    $argumentLine = ConvertTo-ProcessArgumentString -Arguments $ExeArguments
    Write-Log ("  args_quoted: " + $argumentLine)

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    $workingDirectory = (Get-Item -LiteralPath ([System.IO.Path]::GetDirectoryName($stdoutPath))).FullName
    Write-Log ("  working_dir: " + $workingDirectory)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = $null
    $forcedStop = $false
    $earlyCompleted = $false
    try {
        $proc = Start-Process -FilePath $exeResolved -ArgumentList $argumentLine -WorkingDirectory $workingDirectory -PassThru -NoNewWindow -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        try {
            $null = Wait-ForRuntimeEvent -Path $RuntimeLogPath -EventName $WaitEventName -FieldName $WaitFieldName -ExpectedValue $WaitFieldValue -TimeoutSeconds $TimeoutSeconds
            if ($GraceSeconds -gt 0) {
                Start-Sleep -Seconds $GraceSeconds
            }
        }
        catch {
            if ($AllowEarlyCompletion -and $proc) {
                try { Wait-Process -Id $proc.Id -Timeout 1 | Out-Null } catch { }
                if ($proc.HasExited -and $proc.ExitCode -eq 0) {
                    $earlyCompleted = $true
                    Write-Log "$Name note: process completed successfully before runtime event '$WaitEventName' was observed; treating this as an early-completion case."
                }
                else {
                    throw
                }
            }
            else {
                throw
            }
        }
    }
    finally {
        if ($proc -and -not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force
            $forcedStop = $true
        }
        if ($proc) {
            try { Wait-Process -Id $proc.Id -Timeout 10 | Out-Null } catch { }
        }
        $sw.Stop()
        Append-ProcessArtifacts -StdoutPath $stdoutPath -StderrPath $stderrPath
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
    }

    $exitCode = if ($proc) { $proc.ExitCode } else { -1 }
    $forcedStopInt = if ($forcedStop) { 1 } else { 0 }
    $earlyCompletedInt = if ($earlyCompleted) { 1 } else { 0 }
    Write-Log "END $Name  exit=$exitCode  forced_stop=$forcedStopInt  early_completed=$earlyCompletedInt  wall_sec=$([math]::Round($sw.Elapsed.TotalSeconds, 2))"
    return [pscustomobject]@{
        ExitCode = $exitCode
        ForcedStop = $forcedStop
        EarlyCompleted = $earlyCompleted
    }
}

function Assert-CheckpointFile {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $Path,
        [string] $ExpectedKind = ''
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label failed: checkpoint file does not exist: $Path"
    }

    $item = Get-Item -LiteralPath $Path
    if ($item.Length -le 0) {
        throw "$Label failed: checkpoint file is empty: $Path"
    }

    $header = Read-CheckpointHeader -Path $Path
    if ($header.Magic -ne $script:CheckpointMagic) {
        throw "$Label failed: checkpoint magic=0x{0:X8}, expected 0x{1:X8}" -f $header.Magic, $script:CheckpointMagic
    }
    if ($header.Version -ne $script:CheckpointVersion) {
        throw "$Label failed: checkpoint version=$($header.Version), expected $($script:CheckpointVersion)"
    }
    if ($header.Endian -ne $script:CheckpointEndianTag) {
        throw "$Label failed: checkpoint endian=0x{0:X8}, expected 0x{1:X8}" -f $header.Endian, $script:CheckpointEndianTag
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedKind)) {
        if (-not $script:CheckpointKinds.ContainsKey($ExpectedKind)) {
            throw "$Label failed: unknown expected checkpoint kind '$ExpectedKind'"
        }
        $expectedKindValue = [UInt16]$script:CheckpointKinds[$ExpectedKind]
        if ($header.Kind -ne $expectedKindValue) {
            throw "$Label failed: checkpoint kind=$($header.Kind), expected $expectedKindValue ($ExpectedKind)"
        }
    }

    Write-Log "$Label checkpoint file ok: bytes=$($item.Length) path=$Path"
}

function Read-CheckpointHeader {
    param(
        [Parameter(Mandatory = $true)][string] $Path
    )

    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        try {
            return [pscustomobject]@{
                Magic = $br.ReadUInt32()
                Version = $br.ReadUInt16()
                Kind = $br.ReadUInt16()
                Endian = $br.ReadUInt32()
            }
        }
        finally {
            $br.Close()
        }
    }
    finally {
        $fs.Close()
    }
}

function Convert-HexTokenToUInt32 {
    param(
        [Parameter(Mandatory = $true)][string] $Value
    )

    $raw = $Value.Trim()
    [UInt32] $parsed = 0
    if ($raw -match '^0[xX][0-9a-fA-F]+$') {
        if (-not [UInt32]::TryParse($raw.Substring(2), [System.Globalization.NumberStyles]::AllowHexSpecifier, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)) {
            throw "Unable to parse hex UInt32 token: $Value"
        }
        return $parsed
    }
    if (-not [UInt32]::TryParse($raw, [ref]$parsed)) {
        throw "Unable to parse UInt32 token: $Value"
    }
    return $parsed
}

function Format-Hex8Token {
    param(
        [Parameter(Mandatory = $true)][string] $Value
    )

    return ('{0:x8}' -f (Convert-HexTokenToUInt32 -Value $Value))
}

function Assert-TextArtifact {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $Path,
        [string[]] $RequiredPatterns = @(),
        [int] $MinimumBytes = 1
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label failed: missing file $Path"
    }

    $item = Get-Item -LiteralPath $Path
    if ($item.Length -lt $MinimumBytes) {
        throw "$Label failed: file too small ($($item.Length) bytes) path=$Path"
    }

    $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    foreach ($pattern in $RequiredPatterns) {
        if ($raw -notmatch $pattern) {
            throw "$Label failed: file '$Path' missing pattern '$pattern'"
        }
    }

    Write-Log "$Label text artifact ok: bytes=$($item.Length) path=$Path"
    return [pscustomobject]@{
        Path = $item.FullName
        Length = $item.Length
        Raw = $raw
    }
}

function Resolve-DiffHistoryLogPath {
    param(
        [Parameter(Mandatory = $true)][int] $RoundCount,
        [Parameter(Mandatory = $true)][string] $DeltaA,
        [Parameter(Mandatory = $true)][string] $DeltaB,
        [Parameter(Mandatory = $true)][string] $ProjectRoot
    )

    $leaf = 'auto_checkpoint_R{0}_DiffA{1}_DiffB{2}.log' -f $RoundCount, (Format-Hex8Token -Value $DeltaA), (Format-Hex8Token -Value $DeltaB)
    return Resolve-TimestampedArtifactPath -RequestedPath (Join-Path $ProjectRoot $leaf)
}

function Resolve-LinHistoryLogPath {
    param(
        [Parameter(Mandatory = $true)][int] $RoundCount,
        [Parameter(Mandatory = $true)][string] $MaskA,
        [Parameter(Mandatory = $true)][string] $MaskB,
        [Parameter(Mandatory = $true)][string] $ProjectRoot
    )

    $leaf = 'auto_checkpoint_R{0}_MaskA{1}_MaskB{2}.log' -f $RoundCount, (Format-Hex8Token -Value $MaskA), (Format-Hex8Token -Value $MaskB)
    return (Join-Path $ProjectRoot $leaf)
}

function Assert-BestHistoryLog {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $Path
    )

    $artifact = Assert-TextArtifact -Label $Label -Path $Path -RequiredPatterns @('=== checkpoint ===', 'checkpoint_reason=', 'run_nodes_visited=', 'visited_node_count=')
    return $artifact
}

function Assert-HistoryLogGrowth {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][UInt64] $MinimumPreviousLength
    )

    $artifact = Assert-TextArtifact -Label $Label -Path $Path -RequiredPatterns @('checkpoint_reason=|resume_reason=')
    if ([UInt64]$artifact.Length -le $MinimumPreviousLength) {
        throw "$Label failed: history log did not grow after resume/checkpoint activity (old=$MinimumPreviousLength new=$($artifact.Length))"
    }
    return $artifact
}

function Resolve-TimestampedArtifactPath {
    param(
        [Parameter(Mandatory = $true)][string] $RequestedPath
    )

    if (Test-Path -LiteralPath $RequestedPath) {
        return (Get-Item -LiteralPath $RequestedPath).FullName
    }

    $parent = Split-Path -Parent $RequestedPath
    if ([string]::IsNullOrWhiteSpace($parent)) {
        $parent = (Get-Location).Path
    }
    if (-not (Test-Path -LiteralPath $parent)) {
        throw "Resolve-TimestampedArtifactPath failed: parent directory does not exist: $parent"
    }

    $leaf = Split-Path -Leaf $RequestedPath
    $extension = [System.IO.Path]::GetExtension($leaf)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($leaf)
    $regexExtension = [Regex]::Escape($extension)
    $regexStem = [Regex]::Escape($stem)
    $pattern = '^' + $regexStem + '_\d{8}_\d{6}' + $regexExtension + '$'

    $matches = @(
        Get-ChildItem -LiteralPath $parent -File |
            Where-Object { $_.Name -match $pattern } |
            Sort-Object LastWriteTime
    )

    if ($matches.Count -eq 0) {
        throw "Resolve-TimestampedArtifactPath failed: no artifact found for requested path '$RequestedPath'"
    }

    return $matches[-1].FullName
}

function Assert-TimeOnlyCheckpointRun {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $RuntimeLogPath,
        [Parameter(Mandatory = $true)][UInt64] $ExpectedCheckpointEverySeconds,
        [switch] $AllowCompleted
    )

    $events = Read-RuntimeEvents -Path $RuntimeLogPath
    $start = Get-LatestRuntimeEvent -Events $events -EventName 'best_search_start'
    $stop = Get-LatestRuntimeEvent -Events $events -EventName 'best_search_stop'

    if (-not $start) { throw "$Label failed: missing best_search_start" }
    if (-not $stop) { throw "$Label failed: missing best_search_stop" }

    $startMaxNodes = Read-UInt64Field -Event $start -FieldName 'runtime_maximum_search_nodes'
    $startMaxSeconds = Read-UInt64Field -Event $start -FieldName 'runtime_maximum_search_seconds'
    $startCheckpointEvery = Read-UInt64Field -Event $start -FieldName 'runtime_checkpoint_every_seconds'
    $stopTotalNodes = Read-UInt64Field -Event $stop -FieldName 'total_nodes_visited'
    $stopRunNodes = Read-UInt64Field -Event $stop -FieldName 'run_nodes_visited'

    if ("$($start.runtime_budget_mode)" -ne 'time_only') {
        throw "$Label failed: runtime_budget_mode='$($start.runtime_budget_mode)' instead of 'time_only'"
    }
    if ($startMaxNodes -ne 0) {
        throw "$Label failed: runtime_maximum_search_nodes=$startMaxNodes, expected 0"
    }
    if ($startMaxSeconds -lt 1) {
        throw "$Label failed: runtime_maximum_search_seconds=$startMaxSeconds, expected > 0"
    }
    if ($startCheckpointEvery -ne $ExpectedCheckpointEverySeconds) {
        throw "$Label failed: runtime_checkpoint_every_seconds=$startCheckpointEvery, expected $ExpectedCheckpointEverySeconds"
    }
    $stopReason = "$($stop.stop_reason)"
    if ($stopReason -ne 'hit_time_limit') {
        if (-not $AllowCompleted -or $stopReason -ne 'completed') {
            throw "$Label failed: stop_reason='$stopReason' instead of 'hit_time_limit'"
        }
        Write-Log "$Label note: run completed before hitting the time budget on this machine; treating checkpoint generation as valid but skipping same-run interruption continuity requirements."
    }
    if ($stopTotalNodes -lt 1) {
        throw "$Label failed: total_nodes_visited stayed at 0"
    }

    Write-Log "$Label checkpoint verified: total_nodes=$stopTotalNodes run_nodes=$stopRunNodes best_weight=$($stop.best_weight) maxsec=$startMaxSeconds"
    return [pscustomobject]@{
        TotalNodes = $stopTotalNodes
        RunNodes = $stopRunNodes
        BestWeight = "$($stop.best_weight)"
        StopReason = $stopReason
        HitTimeLimit = ($stopReason -eq 'hit_time_limit')
    }
}

function Assert-CollectorResumeContinuity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $CheckpointRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeRuntimeLogPath
    )

    $checkpointEvents = Read-RuntimeEvents -Path $CheckpointRuntimeLogPath
    $resumeEvents = Read-RuntimeEvents -Path $ResumeRuntimeLogPath

    $checkpointStop = Get-LatestRuntimeEvent -Events $checkpointEvents -EventName 'collector_stop'
    $checkpointWrite = Get-LatestMatchingRuntimeEvent -Events $checkpointEvents -EventName 'collector_checkpoint_write' -FieldName 'checkpoint_reason' -ExpectedValue 'collector_stop'
    $resumeStart = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'collector_resume_start'
    $resumeStop = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'collector_stop'

    if (-not $checkpointStop) { throw "$Label failed: missing checkpoint collector_stop" }
    if (-not $checkpointWrite) { throw "$Label failed: missing checkpoint collector_checkpoint_write(reason=collector_stop)" }
    if (-not $resumeStart) { throw "$Label failed: missing collector_resume_start" }
    if (-not $resumeStop) { throw "$Label failed: missing resume collector_stop" }

    $checkpointNodes = Read-UInt64Field -Event $checkpointStop -FieldName 'nodes_visited'
    $checkpointDepth = Read-UInt64Field -Event $checkpointStop -FieldName 'cursor_stack_depth'
    $checkpointWriteNodes = Read-UInt64Field -Event $checkpointWrite -FieldName 'nodes_visited'
    $resumeStartNodes = Read-UInt64Field -Event $resumeStart -FieldName 'nodes_visited'
    $resumeStartDepth = Read-UInt64Field -Event $resumeStart -FieldName 'cursor_stack_depth'
    $resumeStopNodes = Read-UInt64Field -Event $resumeStop -FieldName 'nodes_visited'

    if ($checkpointWriteNodes -ne $checkpointNodes) {
        throw "$Label failed: checkpoint stop nodes=$checkpointNodes but collector_stop checkpoint write nodes=$checkpointWriteNodes"
    }
    if ($resumeStartNodes -ne $checkpointNodes) {
        throw "$Label failed: collector_resume_start.nodes_visited=$resumeStartNodes, expected checkpoint nodes $checkpointNodes"
    }
    if ($resumeStartDepth -ne $checkpointDepth) {
        throw "$Label failed: collector_resume_start.cursor_stack_depth=$resumeStartDepth, expected checkpoint depth $checkpointDepth"
    }
    if ($checkpointDepth -lt 1) {
        throw "$Label failed: checkpoint collector_stop.cursor_stack_depth=$checkpointDepth, expected in-flight depth > 0"
    }
    if ($resumeStopNodes -le $resumeStartNodes) {
        throw "$Label failed: collector resume did not advance nodes (start=$resumeStartNodes stop=$resumeStopNodes)"
    }

    Write-Log "$Label collector resume continuity verified: checkpoint_nodes=$checkpointNodes resume_stop_nodes=$resumeStopNodes depth=$checkpointDepth"
    return [pscustomobject]@{
        CheckpointNodes = $checkpointNodes
        ResumeStopNodes = $resumeStopNodes
        CursorDepth = $checkpointDepth
    }
}

function Assert-ResumeNodeContinuity {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $CheckpointRuntimeLogPath,
        [Parameter(Mandatory = $true)][string] $ResumeRuntimeLogPath,
        [Parameter(Mandatory = $true)][UInt64] $ExpectedResumeCheckpointEverySeconds
    )

    $checkpointEvents = Read-RuntimeEvents -Path $CheckpointRuntimeLogPath
    $resumeEvents = Read-RuntimeEvents -Path $ResumeRuntimeLogPath

    $checkpointStop = Get-LatestRuntimeEvent -Events $checkpointEvents -EventName 'best_search_stop'
    $resumeStart = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'resume_start'
    $resumeStop = Get-LatestRuntimeEvent -Events $resumeEvents -EventName 'resume_stop'

    if (-not $checkpointStop) { throw "$Label failed: missing checkpoint best_search_stop" }
    if (-not $resumeStart) { throw "$Label failed: missing resume_start" }
    if (-not $resumeStop) { throw "$Label failed: missing resume_stop" }

    $checkpointTotalNodes = Read-UInt64Field -Event $checkpointStop -FieldName 'total_nodes_visited'
    $resumeStartTotalNodes = Read-UInt64Field -Event $resumeStart -FieldName 'total_nodes_visited'
    $resumeStopTotalNodes = Read-UInt64Field -Event $resumeStop -FieldName 'total_nodes_visited'
    $resumeStopRunNodes = Read-UInt64Field -Event $resumeStop -FieldName 'run_nodes_visited'
    $resumeStartMaxNodes = Read-UInt64Field -Event $resumeStart -FieldName 'runtime_maximum_search_nodes'
    $resumeStartRunNodes = Read-UInt64Field -Event $resumeStart -FieldName 'run_nodes_visited'
    $resumeStartCheckpointEvery = Read-UInt64Field -Event $resumeStart -FieldName 'runtime_checkpoint_every_seconds'

    if ("$($resumeStart.runtime_budget_mode)" -ne 'time_only') {
        throw "$Label failed: resume_start.runtime_budget_mode='$($resumeStart.runtime_budget_mode)' instead of 'time_only'"
    }
    if ($resumeStartMaxNodes -ne 0) {
        throw "$Label failed: resume_start.runtime_maximum_search_nodes=$resumeStartMaxNodes, expected 0"
    }
    if ($resumeStartRunNodes -ne 0) {
        throw "$Label failed: resume_start.run_nodes_visited=$resumeStartRunNodes, expected 0"
    }
    if ($resumeStartCheckpointEvery -ne $ExpectedResumeCheckpointEverySeconds) {
        throw "$Label failed: resume_start.runtime_checkpoint_every_seconds=$resumeStartCheckpointEvery, expected $ExpectedResumeCheckpointEverySeconds"
    }
    if ($resumeStartTotalNodes -ne $checkpointTotalNodes) {
        throw "$Label failed: resume_start.total_nodes_visited=$resumeStartTotalNodes, expected checkpoint total $checkpointTotalNodes"
    }
    if ($resumeStopTotalNodes -le $resumeStartTotalNodes) {
        throw "$Label failed: resume_stop.total_nodes_visited=$resumeStopTotalNodes did not advance past resume_start.total_nodes_visited=$resumeStartTotalNodes"
    }

    $deltaNodes = $resumeStopTotalNodes - $resumeStartTotalNodes
    if ($deltaNodes -ne $resumeStopRunNodes) {
        throw "$Label failed: delta_nodes=$deltaNodes but resume_stop.run_nodes_visited=$resumeStopRunNodes"
    }

    Write-Log "$Label resume verified: start_total_nodes=$resumeStartTotalNodes stop_total_nodes=$resumeStopTotalNodes delta_nodes=$deltaNodes resume_run_nodes=$resumeStopRunNodes stop_reason=$($resumeStop.stop_reason)"
    return [pscustomobject]@{
        StartTotalNodes = $resumeStartTotalNodes
        StopTotalNodes = $resumeStopTotalNodes
        DeltaNodes = $deltaNodes
        ResumeRunNodes = $resumeStopRunNodes
        StopReason = "$($resumeStop.stop_reason)"
    }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $cwd = (Get-Location).Path
    $parentOfScript = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { '' } else { (Split-Path -Parent $PSScriptRoot) }
    $tryRoots = @($cwd, $PSScriptRoot, $parentOfScript)
    $ProjectRoot = $null
    foreach ($r in $tryRoots) {
        if ([string]::IsNullOrWhiteSpace($r)) { continue }
        $exeTest = Join-Path $r 'test_neoalzette_differential_best_search.exe'
        if (Test-Path -LiteralPath $exeTest) {
            $ProjectRoot = $r
            break
        }
    }
    if (-not $ProjectRoot) {
        throw "Cannot locate test_neoalzette_differential_best_search.exe. Pass -ProjectRoot '...NeoAlzette_ARX_CryptoAnalysis_AutoSearch'."
    }
}

$diffExe = Join-Path $ProjectRoot 'test_neoalzette_differential_best_search.exe'
$linExe = Join-Path $ProjectRoot 'test_neoalzette_linear_best_search.exe'
$diffHullExe = Join-Path $ProjectRoot 'test_neoalzette_differential_hull_wrapper.exe'
$linHullExe = Join-Path $ProjectRoot 'test_neoalzette_linear_hull_wrapper.exe'
foreach ($p in @($diffExe, $linExe, $diffHullExe, $linHullExe)) {
    if (-not (Test-Path -LiteralPath $p)) {
        throw "Missing executable: $p  (build first, e.g. build_and_test.bat --build-only --no-pause)"
    }
}

Set-Location -LiteralPath $ProjectRoot

$ts = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir = Join-Path $ProjectRoot "_QA_ckpt_resume_real_$ts"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$script:QaLogPath = Join-Path $outDir "QA_checkpoint_resume_$ts.log"

$ckDiff = Join-Path $outDir 'diff_detail.ckpt'
$diffCheckpointRuntimeLog = Join-Path $outDir 'diff_detail_checkpoint.runtime.log'
$diffResumeRuntimeLog = Join-Path $outDir 'diff_detail_resume.runtime.log'
$diffHistoryLogRequest = Join-Path $ProjectRoot ('auto_checkpoint_R{0}_DiffA{1}_DiffB{2}.log' -f $RoundCount, (Format-Hex8Token -Value $DifferentialDeltaA), (Format-Hex8Token -Value $DifferentialDeltaB))

$ckLin = Join-Path $outDir 'lin_detail.ckpt'
$linCheckpointRuntimeLog = Join-Path $outDir 'lin_detail_checkpoint.runtime.log'
$linResumeRuntimeLog = Join-Path $outDir 'lin_detail_resume.runtime.log'
$linHistoryLog = Resolve-LinHistoryLogPath -RoundCount $RoundCount -MaskA $LinearMaskA -MaskB $LinearMaskB -ProjectRoot $ProjectRoot

$requestedDiffCollectorCheckpointPath = Join-Path $outDir 'diff_single_collector.ckpt'
$requestedDiffCollectorRuntimeLogPath = Join-Path $outDir 'diff_single_collector.runtime.log'
$requestedDiffCollectorResumeCheckpointPath = Join-Path $outDir 'diff_single_collector_resume.ckpt'
$requestedDiffCollectorResumeRuntimeLogPath = Join-Path $outDir 'diff_single_collector_resume.runtime.log'

$ckDiffAutoBreadth = Join-Path $outDir 'diff_auto_breadth.ckpt'
$diffAutoBreadthCheckpointRuntimeLog = Join-Path $outDir 'diff_auto_breadth_checkpoint.runtime.log'
$diffAutoBreadthResumeRuntimeLog = Join-Path $outDir 'diff_auto_breadth_resume.runtime.log'
$ckDiffAutoDeep = Join-Path $outDir 'diff_auto_deep.ckpt'
$diffAutoDeepCheckpointRuntimeLog = Join-Path $outDir 'diff_auto_deep_checkpoint.runtime.log'
$diffAutoDeepResumeRuntimeLog = Join-Path $outDir 'diff_auto_deep_resume.runtime.log'

$ckLinAutoBreadth = Join-Path $outDir 'lin_auto_breadth.ckpt'
$linAutoBreadthCheckpointRuntimeLog = Join-Path $outDir 'lin_auto_breadth_checkpoint.runtime.log'
$linAutoBreadthResumeRuntimeLog = Join-Path $outDir 'lin_auto_breadth_resume.runtime.log'
$ckLinAutoDeep = Join-Path $outDir 'lin_auto_deep.ckpt'
$linAutoDeepCheckpointRuntimeLog = Join-Path $outDir 'lin_auto_deep_checkpoint.runtime.log'
$linAutoDeepResumeRuntimeLog = Join-Path $outDir 'lin_auto_deep_resume.runtime.log'

$diffHullBatchJobFile = Join-Path $outDir 'diff_hull_batch_jobs.txt'
$linHullBatchJobFile = Join-Path $outDir 'lin_hull_batch_jobs.txt'

$ckDiffHullSelection = Join-Path $outDir 'diff_hull_selection.ckpt'
$diffHullSelectionCheckpointRuntimeLog = Join-Path $outDir 'diff_hull_selection_checkpoint.runtime.log'
$diffHullSelectionResumeRuntimeLog = Join-Path $outDir 'diff_hull_selection_resume.runtime.log'
$ckDiffHullStrict = Join-Path $outDir 'diff_hull_strict.ckpt'
$diffHullStrictCheckpointRuntimeLog = Join-Path $outDir 'diff_hull_strict_checkpoint.runtime.log'
$diffHullStrictResumeRuntimeLog = Join-Path $outDir 'diff_hull_strict_resume.runtime.log'

$ckLinHullSelection = Join-Path $outDir 'lin_hull_selection.ckpt'
$linHullSelectionCheckpointRuntimeLog = Join-Path $outDir 'lin_hull_selection_checkpoint.runtime.log'
$linHullSelectionResumeRuntimeLog = Join-Path $outDir 'lin_hull_selection_resume.runtime.log'
$ckLinHullStrict = Join-Path $outDir 'lin_hull_strict.ckpt'
$linHullStrictCheckpointRuntimeLog = Join-Path $outDir 'lin_hull_strict_checkpoint.runtime.log'
$linHullStrictResumeRuntimeLog = Join-Path $outDir 'lin_hull_strict_resume.runtime.log'

[UInt64] $effectiveDiffAutoDeepBreadthMaxNodes = [Math]::Max( [UInt64] $AutoDeepBreadthMaxNodes, $script:DiffAutoDeepBreadthMaxNodesFloor )
[UInt64] $effectiveLinAutoDeepBreadthMaxNodes = [UInt64] $AutoDeepBreadthMaxNodes

Write-Log '=== Realistic checkpoint/resume QA (detail + auto pipeline) ==='
Write-Log "ProjectRoot=$ProjectRoot"
Write-Log "Artifacts=$outDir"
Write-Log "CheckpointSeconds=$CheckpointSeconds ResumeSeconds=$ResumeSeconds RoundCount=$RoundCount Threads=$Threads"
Write-Log "AutoCheckpointEverySeconds=$AutoCheckpointEverySeconds AutoResumeCheckpointEverySeconds=$AutoResumeCheckpointEverySeconds AutoEventTimeoutSeconds=$AutoEventTimeoutSeconds"
Write-Log "Differential=(delta_a=$DifferentialDeltaA, delta_b=$DifferentialDeltaB) Linear=(mask_a=$LinearMaskA, mask_b=$LinearMaskB)"
if ( $effectiveDiffAutoDeepBreadthMaxNodes -ne [UInt64] $AutoDeepBreadthMaxNodes ) {
    Write-Log "Differential auto deep breadth maxnodes raised from $AutoDeepBreadthMaxNodes to $effectiveDiffAutoDeepBreadthMaxNodes to guarantee deep-stage candidate selection before resume QA."
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines(
    $diffHullBatchJobFile,
    @(
        "$RoundCount $DifferentialDeltaA $DifferentialDeltaB",
        "$RoundCount 0x1 0x0",
        "$RoundCount 0x0 0x1",
        "$RoundCount 0x1 0x1"
    ),
    $utf8NoBom
)
[System.IO.File]::WriteAllLines(
    $linHullBatchJobFile,
    @(
        "$RoundCount $LinearMaskA $LinearMaskB",
        "$RoundCount 0x1 0x0",
        "$RoundCount 0x0 0x1",
        "$RoundCount 0x1 0x1"
    ),
    $utf8NoBom
)

Invoke-QaStep 'DIFF_detail_checkpoint_time_only' $diffExe @(
    'detail',
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--threads', "$Threads",
    '--maxsec', "$CheckpointSeconds",
    '--maxnodes', '0',
    '--progress-sec', "$CheckpointProgressSeconds",
    '--checkpoint-out', $ckDiff,
    '--checkpoint-every-seconds', "$CheckpointEverySeconds",
    '--runtime-log', $diffCheckpointRuntimeLog
)
Assert-CheckpointFile 'DIFF_detail_checkpoint_time_only' $ckDiff 'DifferentialResidualFrontierBest'
Assert-TextArtifact 'DIFF_detail_checkpoint_runtime_log' $diffCheckpointRuntimeLog @('=== runtime_event ===', 'event=best_search_start', 'event=best_search_stop', 'event=checkpoint_write') | Out-Null
$diffHistoryLog = Resolve-TimestampedArtifactPath -RequestedPath $diffHistoryLogRequest
$diffHistoryArtifact = Assert-BestHistoryLog 'DIFF_detail_history_log' $diffHistoryLog
$diffHistoryLengthBeforeResume = [UInt64]$diffHistoryArtifact.Length
$diffCheckpointSummary = Assert-TimeOnlyCheckpointRun 'DIFF_detail_checkpoint_time_only' $diffCheckpointRuntimeLog $CheckpointEverySeconds

Invoke-QaStep 'DIFF_detail_resume_time_only' $diffExe @(
    'detail',
    '--resume', $ckDiff,
    '--threads', "$Threads",
    '--maxsec', "$ResumeSeconds",
    '--maxnodes', '0',
    '--progress-sec', "$ResumeProgressSeconds",
    '--checkpoint-out', $ckDiff,
    '--checkpoint-every-seconds', "$ResumeCheckpointEverySeconds",
    '--runtime-log', $diffResumeRuntimeLog
)
Assert-CheckpointFile 'DIFF_detail_resume_time_only' $ckDiff 'DifferentialResidualFrontierBest'
Assert-TextArtifact 'DIFF_detail_resume_runtime_log' $diffResumeRuntimeLog @('=== runtime_event ===', 'event=resume_start', 'event=resume_stop') | Out-Null
$diffResumeSummary = Assert-ResumeNodeContinuity 'DIFF_detail_resume_time_only' $diffCheckpointRuntimeLog $diffResumeRuntimeLog $ResumeCheckpointEverySeconds
Assert-SingleRunResumeFingerprintContinuity 'DIFF_detail_resume_time_only' $diffCheckpointRuntimeLog $diffResumeRuntimeLog $ResumeCheckpointEverySeconds -Differential
Assert-HistoryLogGrowth 'DIFF_detail_history_log_after_resume' $diffHistoryLog $diffHistoryLengthBeforeResume | Out-Null

Invoke-QaStep 'DIFF_single_collector_checkpoint_time_only' $diffHullExe @(
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--collect-weight-cap', '520',
    '--maximum-search-seconds', '3',
    '--progress-sec', '1',
    '--collector-checkpoint-out', $requestedDiffCollectorCheckpointPath,
    '--collector-checkpoint-every-seconds', '1',
    '--collector-runtime-log', $requestedDiffCollectorRuntimeLogPath
)
$effectiveDiffCollectorCheckpointPath = Resolve-TimestampedArtifactPath $requestedDiffCollectorCheckpointPath
$effectiveDiffCollectorRuntimeLogPath = Resolve-TimestampedArtifactPath $requestedDiffCollectorRuntimeLogPath
Assert-CheckpointFile 'DIFF_single_collector_checkpoint_time_only' $effectiveDiffCollectorCheckpointPath 'DifferentialResidualFrontierCollector'
Assert-TextArtifact 'DIFF_single_collector_runtime_log' $effectiveDiffCollectorRuntimeLogPath @('=== runtime_event ===', 'event=collector_start', 'event=collector_stop') | Out-Null

Invoke-QaStep 'DIFF_single_collector_resume_time_only' $diffHullExe @(
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--collector-resume', $effectiveDiffCollectorCheckpointPath,
    '--maximum-search-seconds', '2',
    '--progress-sec', '1',
    '--collector-checkpoint-out', $requestedDiffCollectorResumeCheckpointPath,
    '--collector-checkpoint-every-seconds', '1',
    '--collector-runtime-log', $requestedDiffCollectorResumeRuntimeLogPath
)
$effectiveDiffCollectorResumeRuntimeLogPath = Resolve-TimestampedArtifactPath $requestedDiffCollectorResumeRuntimeLogPath
Assert-TextArtifact 'DIFF_single_collector_resume_runtime_log' $effectiveDiffCollectorResumeRuntimeLogPath @('=== runtime_event ===', 'event=collector_resume_start', 'event=collector_stop') | Out-Null
$diffCollectorResumeSummary = Assert-CollectorResumeContinuity 'DIFF_single_collector_resume_time_only' $effectiveDiffCollectorRuntimeLogPath $effectiveDiffCollectorResumeRuntimeLogPath

Invoke-QaStep 'LIN_detail_checkpoint_time_only' $linExe @(
    'detail',
    '--round-count', "$RoundCount",
    '--output-branch-a-mask', $LinearMaskA,
    '--output-branch-b-mask', $LinearMaskB,
    '--threads', "$Threads",
    '--maxsec', "$CheckpointSeconds",
    '--maxnodes', '0',
    '--progress-sec', "$CheckpointProgressSeconds",
    '--checkpoint-out', $ckLin,
    '--checkpoint-every-seconds', "$CheckpointEverySeconds",
    '--runtime-log', $linCheckpointRuntimeLog
)
Assert-CheckpointFile 'LIN_detail_checkpoint_time_only' $ckLin 'LinearResidualFrontierBest'
Assert-TextArtifact 'LIN_detail_checkpoint_runtime_log' $linCheckpointRuntimeLog @('=== runtime_event ===', 'event=best_search_start', 'event=best_search_stop') | Out-Null
$linHistoryArtifact = Assert-BestHistoryLog 'LIN_detail_history_log' $linHistoryLog
$linHistoryLengthBeforeResume = [UInt64]$linHistoryArtifact.Length
$linCheckpointSummary = Assert-TimeOnlyCheckpointRun 'LIN_detail_checkpoint_time_only' $linCheckpointRuntimeLog $CheckpointEverySeconds -AllowCompleted

$linResumeSummary = $null
if ($linCheckpointSummary.HitTimeLimit) {
    Invoke-QaStep 'LIN_detail_resume_time_only' $linExe @(
        'detail',
        '--resume', $ckLin,
        '--threads', "$Threads",
        '--maxsec', "$ResumeSeconds",
        '--maxnodes', '0',
        '--progress-sec', "$ResumeProgressSeconds",
        '--checkpoint-out', $ckLin,
        '--checkpoint-every-seconds', "$ResumeCheckpointEverySeconds",
        '--runtime-log', $linResumeRuntimeLog
    )
    Assert-CheckpointFile 'LIN_detail_resume_time_only' $ckLin 'LinearResidualFrontierBest'
    Assert-TextArtifact 'LIN_detail_resume_runtime_log' $linResumeRuntimeLog @('=== runtime_event ===', 'event=resume_start', 'event=resume_stop') | Out-Null
    $linResumeSummary = Assert-ResumeNodeContinuity 'LIN_detail_resume_time_only' $linCheckpointRuntimeLog $linResumeRuntimeLog $ResumeCheckpointEverySeconds
    Assert-SingleRunResumeFingerprintContinuity 'LIN_detail_resume_time_only' $linCheckpointRuntimeLog $linResumeRuntimeLog $ResumeCheckpointEverySeconds
    Assert-HistoryLogGrowth 'LIN_detail_history_log_after_resume' $linHistoryLog $linHistoryLengthBeforeResume | Out-Null
}
else {
    Write-Log 'LIN_detail_resume_time_only note: single-run linear detail completed before the time budget on this machine; skipping same-run interruption continuity and relying on linear auto/batch resume coverage below.'
    Assert-BestHistoryLog 'LIN_detail_history_log_completed' $linHistoryLog | Out-Null
}

Invoke-QaInterruptibleStep 'DIFF_auto_breadth_checkpoint_interrupt' $diffExe @(
    'auto',
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--auto-breadth-jobs', "$AutoBreadthJobs",
    '--auto-breadth-threads', "$AutoBreadthThreads",
    '--auto-breadth-maxnodes', "$AutoBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time', '120',
    '--checkpoint-out', $ckDiffAutoBreadth,
    '--checkpoint-every-seconds', "$AutoCheckpointEverySeconds",
    '--runtime-log', $diffAutoBreadthCheckpointRuntimeLog,
    '--progress-sec', '1'
) $diffAutoBreadthCheckpointRuntimeLog 'auto_checkpoint_write' 'checkpoint_reason' 'periodic_timer' $AutoEventTimeoutSeconds 1 | Out-Null
Assert-CheckpointFile 'DIFF_auto_breadth_checkpoint_interrupt' $ckDiffAutoBreadth

Invoke-QaInterruptibleStep 'DIFF_auto_breadth_resume_interrupt' $diffExe @(
    'auto',
    '--resume', $ckDiffAutoBreadth,
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--auto-breadth-jobs', "$AutoBreadthJobs",
    '--auto-breadth-threads', "$AutoBreadthThreads",
    '--auto-breadth-maxnodes', "$AutoBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time', '120',
    '--checkpoint-out', $ckDiffAutoBreadth,
    '--checkpoint-every-seconds', "$AutoResumeCheckpointEverySeconds",
    '--runtime-log', $diffAutoBreadthResumeRuntimeLog,
    '--progress-sec', '1'
) $diffAutoBreadthResumeRuntimeLog 'auto_checkpoint_write' 'checkpoint_reason' 'periodic_timer' $AutoEventTimeoutSeconds 1 | Out-Null
Assert-AutoPipelineResumeContinuity 'DIFF_auto_breadth_resume_interrupt' $diffAutoBreadthCheckpointRuntimeLog $diffAutoBreadthResumeRuntimeLog $ckDiffAutoBreadth 'breadth' $AutoResumeCheckpointEverySeconds @('auto_checkpoint_write')

$linAutoBreadthInterrupt = Invoke-QaInterruptibleStep 'LIN_auto_breadth_checkpoint_interrupt' $linExe @(
    'auto',
    '--round-count', "$RoundCount",
    '--output-branch-a-mask', $LinearMaskA,
    '--output-branch-b-mask', $LinearMaskB,
    '--auto-breadth-jobs', "$AutoBreadthJobs",
    '--auto-breadth-threads', "$AutoBreadthThreads",
    '--auto-breadth-maxnodes', "$AutoBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time', '120',
    '--checkpoint-out', $ckLinAutoBreadth,
    '--checkpoint-every-seconds', "$AutoCheckpointEverySeconds",
    '--runtime-log', $linAutoBreadthCheckpointRuntimeLog,
    '--progress-sec', '1'
) $linAutoBreadthCheckpointRuntimeLog 'auto_checkpoint_write' '' '' $AutoEventTimeoutSeconds 1 -AllowEarlyCompletion
Assert-CheckpointFile 'LIN_auto_breadth_checkpoint_interrupt' $ckLinAutoBreadth

if ($linAutoBreadthInterrupt.ForcedStop) {
    Invoke-QaInterruptibleStep 'LIN_auto_breadth_resume_interrupt' $linExe @(
        'auto',
        '--resume', $ckLinAutoBreadth,
        '--round-count', "$RoundCount",
        '--output-branch-a-mask', $LinearMaskA,
        '--output-branch-b-mask', $LinearMaskB,
        '--auto-breadth-jobs', "$AutoBreadthJobs",
        '--auto-breadth-threads', "$AutoBreadthThreads",
        '--auto-breadth-maxnodes', "$AutoBreadthMaxNodes",
        '--auto-deep-maxnodes', '0',
        '--auto-max-time', '120',
        '--checkpoint-out', $ckLinAutoBreadth,
        '--checkpoint-every-seconds', "$AutoResumeCheckpointEverySeconds",
        '--runtime-log', $linAutoBreadthResumeRuntimeLog,
        '--progress-sec', '1'
    ) $linAutoBreadthResumeRuntimeLog 'auto_checkpoint_write' '' '' $AutoEventTimeoutSeconds 1 -AllowEarlyCompletion | Out-Null
    Assert-AutoPipelineResumeContinuity 'LIN_auto_breadth_resume_interrupt' $linAutoBreadthCheckpointRuntimeLog $linAutoBreadthResumeRuntimeLog $ckLinAutoBreadth 'breadth' $AutoResumeCheckpointEverySeconds @('auto_checkpoint_write') -SkipPeriodicRequirement
}
else {
    Write-Log 'LIN_auto_breadth_resume_interrupt note: linear auto breadth completed before the interrupt event on this machine; skipping breadth interruption continuity and relying on deep/batch resume coverage.'
}

Invoke-QaInterruptibleStep 'DIFF_auto_deep_checkpoint_interrupt' $diffExe @(
    'auto',
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--auto-breadth-jobs', "$AutoDeepBreadthJobs",
    '--auto-breadth-threads', '1',
    '--auto-breadth-maxnodes', "$effectiveDiffAutoDeepBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time', '120',
    '--checkpoint-out', $ckDiffAutoDeep,
    '--checkpoint-every-seconds', "$AutoCheckpointEverySeconds",
    '--runtime-log', $diffAutoDeepCheckpointRuntimeLog,
    '--progress-sec', '1'
) $diffAutoDeepCheckpointRuntimeLog 'checkpoint_write' 'checkpoint_reason' 'periodic_timer' $AutoEventTimeoutSeconds 1 | Out-Null
Assert-CheckpointFile 'DIFF_auto_deep_checkpoint_interrupt' $ckDiffAutoDeep

Invoke-QaStep 'DIFF_auto_deep_resume_time_only' $diffExe @(
    'auto',
    '--resume', $ckDiffAutoDeep,
    '--round-count', "$RoundCount",
    '--delta-a', $DifferentialDeltaA,
    '--delta-b', $DifferentialDeltaB,
    '--auto-breadth-jobs', "$AutoDeepBreadthJobs",
    '--auto-breadth-threads', '1',
    '--auto-breadth-maxnodes', "$effectiveDiffAutoDeepBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time', "$AutoDeepResumeSeconds",
    '--checkpoint-out', $ckDiffAutoDeep,
    '--checkpoint-every-seconds', "$AutoResumeCheckpointEverySeconds",
    '--runtime-log', $diffAutoDeepResumeRuntimeLog,
    '--progress-sec', '1'
)
Assert-AutoPipelineResumeContinuity 'DIFF_auto_deep_resume_time_only' $diffAutoDeepCheckpointRuntimeLog $diffAutoDeepResumeRuntimeLog $ckDiffAutoDeep 'deep' $AutoResumeCheckpointEverySeconds @('auto_checkpoint_write')

$linAutoDeepInterrupt = Invoke-QaInterruptibleStep 'LIN_auto_deep_checkpoint_interrupt' $linExe @(
    'auto',
    '--round-count', "$RoundCount",
    '--output-branch-a-mask', $LinearMaskA,
    '--output-branch-b-mask', $LinearMaskB,
    '--auto-breadth-jobs', "$AutoDeepBreadthJobs",
    '--auto-breadth-threads', '1',
    '--auto-breadth-maxnodes', "$effectiveLinAutoDeepBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time', '120',
    '--checkpoint-out', $ckLinAutoDeep,
    '--checkpoint-every-seconds', "$AutoCheckpointEverySeconds",
    '--runtime-log', $linAutoDeepCheckpointRuntimeLog,
    '--progress-sec', '1'
) $linAutoDeepCheckpointRuntimeLog 'checkpoint_write' '' '' $AutoEventTimeoutSeconds 1 -AllowEarlyCompletion
Assert-CheckpointFile 'LIN_auto_deep_checkpoint_interrupt' $ckLinAutoDeep

if ($linAutoDeepInterrupt.ForcedStop) {
    Invoke-QaStep 'LIN_auto_deep_resume_time_only' $linExe @(
        'auto',
        '--resume', $ckLinAutoDeep,
        '--round-count', "$RoundCount",
        '--output-branch-a-mask', $LinearMaskA,
        '--output-branch-b-mask', $LinearMaskB,
        '--auto-breadth-jobs', "$AutoDeepBreadthJobs",
        '--auto-breadth-threads', '1',
        '--auto-breadth-maxnodes', "$effectiveLinAutoDeepBreadthMaxNodes",
        '--auto-deep-maxnodes', '0',
        '--auto-max-time', "$AutoDeepResumeSeconds",
        '--checkpoint-out', $ckLinAutoDeep,
        '--checkpoint-every-seconds', "$AutoResumeCheckpointEverySeconds",
        '--runtime-log', $linAutoDeepResumeRuntimeLog,
        '--progress-sec', '1'
    )
    Assert-AutoPipelineResumeContinuity 'LIN_auto_deep_resume_time_only' $linAutoDeepCheckpointRuntimeLog $linAutoDeepResumeRuntimeLog $ckLinAutoDeep 'deep' $AutoResumeCheckpointEverySeconds @('auto_checkpoint_write') -SkipPeriodicRequirement
}
else {
    Write-Log 'LIN_auto_deep_resume_time_only note: linear auto deep completed before the interrupt event on this machine; skipping deep interruption continuity and relying on breadth/batch resume coverage.'
}

Invoke-QaInterruptibleStep 'DIFF_hull_selection_checkpoint_interrupt' $diffHullExe @(
    '--round-count', "$RoundCount",
    '--batch-file', $diffHullBatchJobFile,
    '--thread-count', '1',
    '--auto-breadth-maxnodes', "$effectiveDiffAutoDeepBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time-seconds', '120',
    '--batch-checkpoint-out', $ckDiffHullSelection,
    '--batch-runtime-log', $diffHullSelectionCheckpointRuntimeLog,
    '--progress-sec', '1'
) $diffHullSelectionCheckpointRuntimeLog 'batch_checkpoint_write' 'checkpoint_reason' 'breadth_job_completed' $AutoEventTimeoutSeconds 0 | Out-Null
Assert-CheckpointFile 'DIFF_hull_selection_checkpoint_interrupt' $ckDiffHullSelection

Invoke-QaInterruptibleStep 'DIFF_hull_selection_resume' $diffHullExe @(
    '--batch-resume', $ckDiffHullSelection,
    '--thread-count', '1',
    '--auto-breadth-maxnodes', "$effectiveDiffAutoDeepBreadthMaxNodes",
    '--batch-runtime-log', $diffHullSelectionResumeRuntimeLog
) $diffHullSelectionResumeRuntimeLog 'batch_resume_start' '' '' $AutoEventTimeoutSeconds 0 | Out-Null
Assert-WrapperBatchResumeStartContinuity 'DIFF_hull_selection_resume' $diffHullSelectionCheckpointRuntimeLog $diffHullSelectionResumeRuntimeLog $ckDiffHullSelection 'differential_hull_batch_selection'

Invoke-QaInterruptibleStep 'DIFF_hull_strict_checkpoint_interrupt' $diffHullExe @(
    '--round-count', "$RoundCount",
    '--batch-file', $diffHullBatchJobFile,
    '--thread-count', '1',
    '--collect-weight-cap', '0',
    '--batch-checkpoint-out', $ckDiffHullStrict,
    '--batch-runtime-log', $diffHullStrictCheckpointRuntimeLog,
    '--progress-sec', '1'
) $diffHullStrictCheckpointRuntimeLog 'batch_checkpoint_write' 'checkpoint_reason' 'strict_hull_job_completed' $AutoEventTimeoutSeconds 0 | Out-Null
Assert-CheckpointFile 'DIFF_hull_strict_checkpoint_interrupt' $ckDiffHullStrict

Invoke-QaStep 'DIFF_hull_strict_resume' $diffHullExe @(
    '--batch-resume', $ckDiffHullStrict,
    '--thread-count', '1',
    '--batch-runtime-log', $diffHullStrictResumeRuntimeLog
)
Assert-WrapperBatchResumeContinuity 'DIFF_hull_strict_resume' $diffHullStrictCheckpointRuntimeLog $diffHullStrictResumeRuntimeLog $ckDiffHullStrict 'differential_hull_batch'

Invoke-QaInterruptibleStep 'LIN_hull_selection_checkpoint_interrupt' $linHullExe @(
    '--round-count', "$RoundCount",
    '--batch-file', $linHullBatchJobFile,
    '--thread-count', '1',
    '--auto-breadth-maxnodes', "$AutoBreadthMaxNodes",
    '--auto-deep-maxnodes', '0',
    '--auto-max-time-seconds', '120',
    '--batch-checkpoint-out', $ckLinHullSelection,
    '--batch-runtime-log', $linHullSelectionCheckpointRuntimeLog,
    '--progress-sec', '1'
) $linHullSelectionCheckpointRuntimeLog 'batch_checkpoint_write' 'checkpoint_reason' 'breadth_job_completed' $AutoEventTimeoutSeconds 0 | Out-Null
Assert-CheckpointFile 'LIN_hull_selection_checkpoint_interrupt' $ckLinHullSelection

Invoke-QaInterruptibleStep 'LIN_hull_selection_resume' $linHullExe @(
    '--batch-resume', $ckLinHullSelection,
    '--thread-count', '1',
    '--batch-runtime-log', $linHullSelectionResumeRuntimeLog
) $linHullSelectionResumeRuntimeLog 'batch_resume_start' '' '' $AutoEventTimeoutSeconds 0 | Out-Null
Assert-WrapperBatchResumeStartContinuity 'LIN_hull_selection_resume' $linHullSelectionCheckpointRuntimeLog $linHullSelectionResumeRuntimeLog $ckLinHullSelection 'linear_hull_batch_selection'

Invoke-QaInterruptibleStep 'LIN_hull_strict_checkpoint_interrupt' $linHullExe @(
    '--round-count', "$RoundCount",
    '--batch-file', $linHullBatchJobFile,
    '--thread-count', '1',
    '--collect-weight-cap', '0',
    '--batch-checkpoint-out', $ckLinHullStrict,
    '--batch-runtime-log', $linHullStrictCheckpointRuntimeLog,
    '--progress-sec', '1'
) $linHullStrictCheckpointRuntimeLog 'batch_checkpoint_write' 'checkpoint_reason' 'strict_hull_job_completed' $AutoEventTimeoutSeconds 0 | Out-Null
Assert-CheckpointFile 'LIN_hull_strict_checkpoint_interrupt' $ckLinHullStrict

Invoke-QaStep 'LIN_hull_strict_resume' $linHullExe @(
    '--batch-resume', $ckLinHullStrict,
    '--thread-count', '1',
    '--batch-runtime-log', $linHullStrictResumeRuntimeLog
)
Assert-WrapperBatchResumeContinuity 'LIN_hull_strict_resume' $linHullStrictCheckpointRuntimeLog $linHullStrictResumeRuntimeLog $ckLinHullStrict 'linear_hull_batch'

Invoke-QaStep 'DIFF_hull_batch_resume_selftest' $diffHullExe @(
    '--selftest',
    '--selftest-scope', 'batch-resume'
)

Invoke-QaStep 'LIN_hull_batch_resume_selftest' $linHullExe @(
    '--selftest',
    '--selftest-scope', 'batch-resume'
)

Write-Log '=== QA PASS ==='
Write-Log "DIFF: checkpoint_total_nodes=$($diffCheckpointSummary.TotalNodes) resume_stop_total_nodes=$($diffResumeSummary.StopTotalNodes) delta_nodes=$($diffResumeSummary.DeltaNodes)"
if ($linResumeSummary) {
    Write-Log "LIN : checkpoint_total_nodes=$($linCheckpointSummary.TotalNodes) resume_stop_total_nodes=$($linResumeSummary.StopTotalNodes) delta_nodes=$($linResumeSummary.DeltaNodes)"
}
else {
    Write-Log "LIN : checkpoint_total_nodes=$($linCheckpointSummary.TotalNodes) stop_reason=$($linCheckpointSummary.StopReason) note=completed_before_budget; auto/batch resume coverage used instead of same-run interruption continuity"
}
Write-Log "DIFF collector: checkpoint_nodes=$($diffCollectorResumeSummary.CheckpointNodes) resume_stop_nodes=$($diffCollectorResumeSummary.ResumeStopNodes) cursor_depth=$($diffCollectorResumeSummary.CursorDepth)"
Write-Log "DIFF auto breadth/deep checkpoints: $ckDiffAutoBreadth , $ckDiffAutoDeep"
Write-Log "LIN  auto breadth/deep checkpoints: $ckLinAutoBreadth , $ckLinAutoDeep"
Write-Log "DIFF hull selection/strict checkpoints: $ckDiffHullSelection , $ckDiffHullStrict"
Write-Log "LIN  hull selection/strict checkpoints: $ckLinHullSelection , $ckLinHullStrict"
Write-Log 'Hull wrapper batch-resume selftests: differential=OK linear=OK'
Write-Log 'Single-run collector resume coverage: differential=OK linear=not_forced_in_this_script'
Write-Host "Artifacts under: $outDir"
Write-Host "Full log: $script:QaLogPath"
