param(
    [Parameter(Mandatory)][string]$Build,
    [string]$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [string]$Config = 'Debug',
    [string]$Plynth = '',
    [string]$Tools = '',
    [string]$Blender = '',
    [string]$Endpoint = '239.0.0.1:6866',
    [string[]]$Samples = @('house', 'apartment'),
    [string]$ChangeSample = 'house',
    [int]$FirstStartMinutes = 90,
    [int]$StepMinutes = 40
)

# Runs the Unreal Engine link against a real Unreal Editor with no window: a copy of the test project, every
# sample project sent with the self check, a second send after a plan change, Pack with materials off and
# Grouped on. Every answer, report and screenshot lands in one run folder. Works in Windows PowerShell 5.1.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceProject = Join-Path $repo 'engines\unreal\tests\PlynthLinkTest'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$script:steps = New-Object System.Collections.ArrayList
$script:answerCount = 0
$Samples = @($Samples | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

# Records one step of the run with its result and a short note, and prints it.
function Add-Step([string]$name, [bool]$passed, [string]$note) {
    $result = if ($passed) { 'pass' } else { 'fail' }
    [void]$script:steps.Add([pscustomobject]@{ Step = $name; Result = $result; Note = $note })
    Write-Host ("{0,-4}  {1}  {2}" -f $result.ToUpper(), $name, $note)
}

# Finds the notes tools folder: beside the checkout, or beside the main checkout when this is a worktree.
function Find-ToolsFolder {
    $here = Join-Path $repo 'notes\tools'
    if (Test-Path $here) { return $here }
    $common = ''
    try { $common = git -C $repo rev-parse --path-format=absolute --git-common-dir } catch { $common = '' }
    if ($LASTEXITCODE -eq 0 -and $common) {
        $main = Join-Path (Split-Path ($common.Trim() -replace '/', '\')) 'notes\tools'
        if (Test-Path $main) { return $main }
    }
    throw 'The notes tools folder was not found. Give it with -Tools.'
}

# Finds plynth.com in the build folder.
function Find-Plynth {
    $places = @((Join-Path $Build "app\$Config\plynth.com"), (Join-Path $Build 'app\plynth.com'), (Join-Path $Build 'plynth.com'))
    foreach ($place in $places) {
        if (Test-Path $place) { return (Resolve-Path $place).Path }
    }
    throw "plynth.com was not found in $Build. Build Plynth first or give it with -Plynth."
}

# Makes a folder when it is missing and gives its full path.
function New-Folder([string]$path) {
    New-Item -ItemType Directory -Force $path | Out-Null
    return (Resolve-Path $path).Path
}

# Quotes one command line argument the way Windows programs read it.
function ConvertTo-Argument([string]$text) {
    if ($text -ne '' -and $text -notmatch '[\s"]') { return $text }
    $escaped = $text -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

# Gives a list without empty entries, also for a missing value.
function Get-List($value) {
    return @($value | Where-Object { $null -ne $_ })
}

# Stops a program and every program it started.
function Stop-Tree([int]$id) {
    $ErrorActionPreference = 'Continue'
    & taskkill.exe /T /F /PID $id *> $null
    $global:LASTEXITCODE = 0
}

# Runs one plynth command with the scratch folders, saves its answer in the run folder and gives it back.
function Invoke-Plynth([string]$name, [string[]]$arguments, [int]$minutes = $StepMinutes) {
    $script:answerCount++
    $stem = '{0:D2}-{1}' -f $script:answerCount, $name
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $script:plynthExe
    $info.Arguments = (@($arguments | ForEach-Object { ConvertTo-Argument $_ })) -join ' '
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $info.StandardOutputEncoding = $utf8
    $info.StandardErrorEncoding = $utf8
    $info.WorkingDirectory = $script:run
    foreach ($key in $script:plynthEnv.Keys) { $info.EnvironmentVariables[$key] = $script:plynthEnv[$key] }
    Write-Host "      plynth $($info.Arguments)"
    $process = [System.Diagnostics.Process]::Start($info)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($minutes * 60000)
    if ($timedOut) { Stop-Tree $process.Id }
    $process.WaitForExit()
    $text = $out.Result
    $file = Join-Path $script:answers "$stem.json"
    [System.IO.File]::WriteAllText($file, $text, $utf8)
    if ($err.Result) { [System.IO.File]::WriteAllText((Join-Path $script:answers "$stem.stderr.txt"), $err.Result, $utf8) }
    $answer = $null
    $line = @($text -split "`r?`n" | Where-Object { $_.Trim().StartsWith('{') }) | Select-Object -Last 1
    if ($line) {
        try { $answer = $line | ConvertFrom-Json } catch { $answer = $null }
    }
    $code = if ($timedOut) { -1 } else { $process.ExitCode }
    return [pscustomobject]@{ Name = $stem; Exit = $code; TimedOut = $timedOut; Answer = $answer; File = $file }
}

# Says in a few words why a plynth answer is not ok.
function Get-AnswerProblem($call) {
    if ($call.TimedOut) { return "no answer after $StepMinutes minutes" }
    if ($null -eq $call.Answer) { return "no JSON answer, exit code $($call.Exit)" }
    if ((Get-Value $call.Answer 'ok') -eq $true) { return '' }
    $problem = Get-Value $call.Answer 'error'
    if ($problem) { return "$(Get-Value $problem 'code'): $(Get-Value $problem 'message')" }
    return "not ok, exit code $($call.Exit)"
}

# Reads a value of an object, or gives null when it has no such value.
function Get-Value($object, [string]$name) {
    if ($null -eq $object) { return $null }
    $property = $object.PSObject.Properties[$name]
    if ($property) { return $property.Value }
    return $null
}

# Copies the link's own report, job, result and editor log of one project into the run folder.
function Save-LinkFiles([string]$project, [string]$stem) {
    $folder = Join-Path $project 'build\links\unreal'
    if (-not (Test-Path $folder)) { return }
    $target = New-Folder (Join-Path $script:answers "$stem-link")
    Get-ChildItem -File $folder | Where-Object { $_.Extension -in '.json', '.log', '.txt' } | Copy-Item -Destination $target
}

# Checks the answer of one send with the self check and gives pass or fail with a note.
function Test-Send($call, [string]$shot) {
    $problem = Get-AnswerProblem $call
    if ($problem) { return @{ Passed = $false; Note = $problem } }
    $result = Get-Value $call.Answer 'result'
    if (Get-Value $result 'skipped') { return @{ Passed = $false; Note = "skipped: $(Get-Value $result 'skipped_reason')" } }
    $checks = @(Get-List (Get-Value $result 'checks'))
    $failed = @($checks | Where-Object { $_.status -eq 'fail' })
    $warned = @($checks | Where-Object { $_.status -eq 'warning' })
    $actors = @(Get-List (Get-Value $result 'actors'))
    $notes = @("$($checks.Count) checks, $($warned.Count) warnings, $($failed.Count) failed", "$($actors.Count) actors")
    foreach ($check in $failed) { $notes += "$($check.id) failed: $($check.message)" }
    $hasShot = Test-Path $shot
    if (-not $hasShot) { $notes += 'no screenshot' }
    $passed = $checks.Count -gt 0 -and $failed.Count -eq 0 -and $hasShot
    return @{ Passed = $passed; Note = ($notes -join '; ') }
}

# Copies a sample project into the run folder, with a new project name when one is given.
function Copy-Sample([string]$sample, [string]$folderName, [string]$newName = '') {
    $source = Join-Path $repo "samples\projects\$sample"
    if (-not (Test-Path $source)) { throw "There is no sample project $sample." }
    $target = Join-Path $script:samplesFolder $folderName
    Copy-Item -Recurse $source $target
    if ($newName) {
        # Each copy needs its own name, so its building gets its own folder in the Unreal project.
        $file = Join-Path $target 'project.json'
        $text = [System.IO.File]::ReadAllText($file)
        $pattern = New-Object System.Text.RegularExpressions.Regex('(?m)^(\s*"name"\s*:\s*)"[^"]*"')
        $text = $pattern.Replace($text, '$1"' + $newName + '"', 1)
        [System.IO.File]::WriteAllText($file, $text, $utf8)
    }
    return (Resolve-Path $target).Path
}

# Builds one project copy and records the step.
function Invoke-Build([string]$label, [string]$project) {
    $call = Invoke-Plynth "$label-build" @('build', '--project', $project)
    $problem = Get-AnswerProblem $call
    Add-Step "${label}: build" (-not $problem) $problem
    return $call
}

# Sends one project copy to the test editor with the self check and a screenshot, and records the step.
function Invoke-Send([string]$label, [string]$project) {
    $shot = Join-Path $script:shots "$label.png"
    $call = Invoke-Plynth "$label-send" @('link', 'unreal', 'send', '--project', $project, '--editor', $script:nodeId, '--self-check', '--screenshot', $shot)
    Save-LinkFiles $project $call.Name
    $check = Test-Send $call $shot
    Add-Step "${label}: send with the self check" $check.Passed $check.Note
    return $call
}

# Gives the assets of a send that are still there after it, of the kinds named.
function Get-Assets($call, [string[]]$kinds) {
    $assets = Get-List (Get-Value (Get-Value $call.Answer 'result') 'assets')
    return @($assets | Where-Object { $_.action -ne 'removed' -and $kinds -contains $_.kind })
}

# Gives the actors of a send that are still in the level after it.
function Get-Actors($call) {
    $actors = Get-List (Get-Value (Get-Value $call.Answer 'result') 'actors')
    return @($actors | Where-Object { $_.action -ne 'removed' })
}

# Builds one remote execution message of Unreal's Python plugin.
function New-RemoteMessage([string]$type, [string]$dest, $data) {
    $message = [ordered]@{ version = 1; magic = 'ue_py'; type = $type; source = $script:clientId }
    if ($dest) { $message.dest = $dest }
    if ($null -ne $data) { $message.data = $data }
    return $utf8.GetBytes(($message | ConvertTo-Json -Compress -Depth 5))
}

# Opens the multicast socket of the test group, on this PC only.
function Open-RemoteSocket {
    $ip = [System.Net.Sockets.SocketOptionLevel]::IP
    $socket = New-Object System.Net.Sockets.Socket([System.Net.Sockets.AddressFamily]::InterNetwork,
        [System.Net.Sockets.SocketType]::Dgram, [System.Net.Sockets.ProtocolType]::Udp)
    $socket.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket, [System.Net.Sockets.SocketOptionName]::ReuseAddress, $true)
    $socket.Bind((New-Object System.Net.IPEndPoint($script:bindAddress, $script:groupPort)))
    $socket.SetSocketOption($ip, [System.Net.Sockets.SocketOptionName]::AddMembership,
        (New-Object System.Net.Sockets.MulticastOption($script:groupAddress, $script:bindAddress)))
    $socket.SetSocketOption($ip, [System.Net.Sockets.SocketOptionName]::MulticastInterface, $script:bindAddress.GetAddressBytes())
    $socket.SetSocketOption($ip, [System.Net.Sockets.SocketOptionName]::MulticastTimeToLive, 0)
    $socket.SetSocketOption($ip, [System.Net.Sockets.SocketOptionName]::MulticastLoopback, $true)
    $socket.ReceiveTimeout = 1000
    return $socket
}

# Sends one message to the test group.
function Send-Remote($socket, [string]$type, [string]$dest, $data) {
    $bytes = New-RemoteMessage $type $dest $data
    [void]$socket.SendTo($bytes, (New-Object System.Net.IPEndPoint($script:groupAddress, $script:groupPort)))
}

# Says whether a pong comes from the editor this run started, by its project folder or name.
function Test-OurEditor($data) {
    $root = Get-Value $data 'project_root'
    if ($root) {
        $seen = ($root -replace '\\', '/').TrimEnd('/')
        $expected = ($script:project -replace '\\', '/').TrimEnd('/')
        return ($seen -ieq $expected) -or $seen.EndsWith("$stamp/PlynthLinkTest", [System.StringComparison]::OrdinalIgnoreCase)
    }
    return (Get-Value $data 'project_name') -eq 'PlynthLinkTest'
}

# Pings the test group until the editor this run started answers, and gives its node id, or empty on timeout.
function Wait-Pong([int]$minutes) {
    $deadline = (Get-Date).AddMinutes($minutes)
    $started = Get-Date
    $lastNote = $started
    $buffer = New-Object byte[] 65536
    $socket = Open-RemoteSocket
    try {
        while ((Get-Date) -lt $deadline) {
            if ($script:editorProcess.HasExited) { return '' }
            Send-Remote $socket 'ping' '' $null
            $until = (Get-Date).AddSeconds(5)
            while ((Get-Date) -lt $until) {
                $count = 0
                try { $count = $socket.Receive($buffer) } catch [System.Net.Sockets.SocketException] { $count = 0 }
                if ($count -le 0) { continue }
                $message = $null
                try { $message = $utf8.GetString($buffer, 0, $count) | ConvertFrom-Json } catch { continue }
                if ((Get-Value $message 'magic') -ne 'ue_py' -or (Get-Value $message 'type') -ne 'pong') { continue }
                if ((Get-Value $message 'dest') -ne $script:clientId) { continue }
                if (Test-OurEditor (Get-Value $message 'data')) { return [string](Get-Value $message 'source') }
            }
            if (((Get-Date) - $lastNote).TotalMinutes -ge 1) {
                $lastNote = Get-Date
                Write-Host ("      waiting for the editor, {0:N0} minutes so far" -f ((Get-Date) - $started).TotalMinutes)
            }
        }
        return ''
    } finally {
        $socket.Close()
    }
}

# Asks the editor to quit through a remote execution command, then waits for it to end.
function Stop-EditorCleanly([int]$minutes) {
    if ($script:editorProcess.HasExited) { return $true }
    if (-not $script:nodeId) { return $false }
    $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $socket = Open-RemoteSocket
    $client = $null
    try {
        $port = $listener.LocalEndpoint.Port
        Send-Remote $socket 'open_connection' $script:nodeId @{ command_ip = '127.0.0.1'; command_port = $port }
        $accept = $listener.AcceptTcpClientAsync()
        if ($accept.Wait(30000)) {
            $client = $accept.Result
            $stream = $client.GetStream()
            $stream.ReadTimeout = 30000
            $command = @{ command = 'import unreal; unreal.SystemLibrary.quit_editor()'; unattended = $true; exec_mode = 'ExecuteStatement' }
            $bytes = New-RemoteMessage 'command' $script:nodeId $command
            $stream.Write($bytes, 0, $bytes.Length)
            $reply = New-Object byte[] 65536
            # The editor may close the connection while it quits instead of answering.
            try { [void]$stream.Read($reply, 0, $reply.Length) } catch { }
        }
        Send-Remote $socket 'close_connection' $script:nodeId $null
    } finally {
        if ($client) { $client.Close() }
        $socket.Close()
        $listener.Stop()
    }
    return $script:editorProcess.WaitForExit($minutes * 60000)
}

# Starts the editor offscreen on the run's copy of the test project, with Unreal's normal user folders.
function Start-Editor {
    $section = '[/Script/PythonScriptPlugin.PythonScriptPluginSettings]'
    $settings = @('bRemoteExecution=True', "RemoteExecutionMulticastGroupEndpoint=$Endpoint",
        'RemoteExecutionMulticastBindAddress=127.0.0.1', 'RemoteExecutionMulticastTtl=0')
    $ini = '-ini:Engine:' + ((@($settings | ForEach-Object { $section + ':' + $_ })) -join ',')
    $arguments = @((Join-Path $script:project 'PlynthLinkTest.uproject'), '-RenderOffscreen', '-unattended', '-nosplash',
        '-nopause', '-nosound', $ini, ('-abslog=' + (Join-Path $script:logs 'editor.log')))
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Editor
    $info.Arguments = (@($arguments | ForEach-Object { ConvertTo-Argument $_ })) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WorkingDirectory = $script:run
    # Unreal keeps its shader cache in its normal user folders, so the editor never gets the scratch folders.
    $info.EnvironmentVariables['APPDATA'] = [Environment]::GetFolderPath('ApplicationData')
    $info.EnvironmentVariables['LOCALAPPDATA'] = [Environment]::GetFolderPath('LocalApplicationData')
    $info.EnvironmentVariables['USERPROFILE'] = [Environment]::GetFolderPath('UserProfile')
    foreach ($key in @($info.EnvironmentVariables.Keys)) {
        if ($key -like 'PLYNTH_*' -or $key -like 'BLENDER_*') { $info.EnvironmentVariables.Remove($key) }
    }
    [System.IO.File]::WriteAllText((Join-Path $script:logs 'editor-command.txt'), "$Editor $($info.Arguments)", $utf8)
    return [System.Diagnostics.Process]::Start($info)
}

# Prints the table of every step and saves it in the run folder, and gives the number of failed steps.
function Write-Summary {
    $lines = @('', 'Headless Unreal run', "Run folder: $script:run", '')
    foreach ($step in $script:steps) { $lines += ("{0,-4}  {1}  {2}" -f $step.Result.ToUpper(), $step.Step, $step.Note) }
    $failed = @($script:steps | Where-Object { $_.Result -ne 'pass' }).Count
    $lines += ''
    $lines += "$($script:steps.Count - $failed) of $($script:steps.Count) steps passed."
    $lines | ForEach-Object { Write-Host $_ }
    [System.IO.File]::WriteAllText((Join-Path $script:run 'summary.txt'), (($lines -join "`r`n") + "`r`n"), $utf8)
    $json = ConvertTo-Json -Depth 4 @($script:steps)
    [System.IO.File]::WriteAllText((Join-Path $script:run 'summary.json'), $json, $utf8)
    return $failed
}

$parts = $Endpoint -split ':'
if ($parts.Count -ne 2) { throw "The endpoint $Endpoint is not group:port." }
$script:groupAddress = [System.Net.IPAddress]::Parse($parts[0])
$script:groupPort = [int]$parts[1]
if ($script:groupPort -eq 6766) { throw 'Port 6766 is the Unreal default that everyday editors use. The test editor needs its own port.' }
$script:bindAddress = [System.Net.IPAddress]::Loopback
$script:clientId = [guid]::NewGuid().ToString()
$script:nodeId = ''
$script:editorProcess = $null

if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe was not found at $Editor." }
if (-not (Test-Path (Join-Path $sourceProject 'PlynthLinkTest.uproject'))) { throw "The test project is missing in $sourceProject." }
$script:plynthExe = if ($Plynth) { (Resolve-Path $Plynth).Path } else { Find-Plynth }
if (-not $Tools) { $Tools = Find-ToolsFolder }
$Tools = (Resolve-Path $Tools).Path

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:run = New-Folder (Join-Path $Tools "runs\unreal\$stamp")
$script:answers = New-Folder (Join-Path $script:run 'answers')
$script:shots = New-Folder (Join-Path $script:run 'shots')
$script:logs = New-Folder (Join-Path $script:run 'logs')
$script:samplesFolder = New-Folder (Join-Path $script:run 'samples')
$scripts = New-Folder (Join-Path $script:run 'scripts')
$scratch = @{}
foreach ($name in 'config', 'appdata', 'local', 'profile', 'temp') { $scratch[$name] = New-Folder (Join-Path $script:run $name) }
$private = New-Folder (Join-Path $Tools 'blender-private')
foreach ($name in 'config', 'scripts', 'extensions', 'datafiles', 'xdg') { New-Folder (Join-Path $private $name) | Out-Null }

Copy-Item -Recurse $sourceProject $script:run
$script:project = (Resolve-Path (Join-Path $script:run 'PlynthLinkTest')).Path

# The editor runs a copy of the shipped scripts, so compiled Python files never land in the source tree.
Copy-Item (Join-Path $repo 'engines\unreal\plynth_link.py') $scripts
Copy-Item -Recurse (Join-Path $repo 'engines\unreal\plynth_unreal') $scripts
Get-ChildItem -Recurse -Directory $scripts -Filter '__pycache__' | Remove-Item -Recurse -Force

$script:plynthEnv = [ordered]@{
    PLYNTH_CONFIG_DIR = $scratch['config']
    APPDATA = $scratch['appdata']
    LOCALAPPDATA = $scratch['local']
    USERPROFILE = $scratch['profile']
    HOME = $scratch['profile']
    TEMP = $scratch['temp']
    TMP = $scratch['temp']
    BLENDER_USER_RESOURCES = $private
    BLENDER_USER_CONFIG = Join-Path $private 'config'
    BLENDER_USER_SCRIPTS = Join-Path $private 'scripts'
    BLENDER_USER_EXTENSIONS = Join-Path $private 'extensions'
    BLENDER_USER_DATAFILES = Join-Path $private 'datafiles'
    XDG_DATA_HOME = Join-Path $private 'xdg'
    PLYNTH_BLENDER_PRIVATE = $private
    PLYNTH_UNREAL_MULTICAST = $Endpoint
    PLYNTH_UNREAL_SCRIPTS = $scripts
}
if ($Blender) { $script:plynthEnv['PLYNTH_BLENDER'] = (Resolve-Path $Blender).Path }

Write-Host "Run folder: $script:run"
Write-Host "Plynth: $script:plynthExe"
Write-Host "Editor: $Editor on $Endpoint"

$failedSteps = 0
try {
    $script:editorProcess = Start-Editor
    Add-Step 'start the editor' $true "process $($script:editorProcess.Id)"

    $waitStart = Get-Date
    $script:nodeId = Wait-Pong $FirstStartMinutes
    $waited = '{0:N1} minutes' -f ((Get-Date) - $waitStart).TotalMinutes
    if (-not $script:nodeId) {
        $why = if ($script:editorProcess.HasExited) { "the editor ended with code $($script:editorProcess.ExitCode)" } else { "no pong in $FirstStartMinutes minutes" }
        Add-Step 'the editor answers on the test group' $false $why
        throw 'stop'
    }
    Add-Step 'the editor answers on the test group' $true "node $($script:nodeId) after $waited"

    $first = @{}
    foreach ($sample in $Samples) { $first[$sample] = Copy-Sample $sample $sample }
    $anyProject = $first[$Samples[0]]

    $list = Invoke-Plynth 'link-list' @('link', 'list', '--project', $anyProject) 5
    if (Get-AnswerProblem $list) { $list = Invoke-Plynth 'link-list' @('link', 'list') 5 }
    $problem = Get-AnswerProblem $list
    Add-Step 'plynth link list' (-not $problem) $problem

    $status = Invoke-Plynth 'link-status' @('link', 'unreal', 'status', '--project', $anyProject) 5
    if (Get-AnswerProblem $status) { $status = Invoke-Plynth 'link-status' @('link', 'unreal', 'status') 5 }
    $problem = Get-AnswerProblem $status
    $sees = (-not $problem) -and ([System.IO.File]::ReadAllText($status.File)).Contains($script:nodeId)
    $note = if ($problem) { $problem } elseif ($sees) { 'the test editor is listed' } else { 'the test editor is not in the answer' }
    Add-Step 'plynth link unreal status finds the test editor' $sees $note

    $firstSends = @{}
    foreach ($sample in $Samples) {
        $built = Invoke-Build $sample $first[$sample]
        if (Get-AnswerProblem $built) { continue }
        $firstSends[$sample] = Invoke-Send $sample $first[$sample]
    }

    if ($first.ContainsKey($ChangeSample) -and $firstSends.ContainsKey($ChangeSample)) {
        $label = "$ChangeSample-changed"
        $copy = $first[$ChangeSample]
        $plan = [System.IO.File]::ReadAllText((Join-Path $copy 'plan.json')) | ConvertFrom-Json
        $floor = @($plan.floors)[0]
        $wall = @($floor.walls)[0]
        $ceiling = Get-Value $floor 'ceiling_height'
        if (-not $ceiling) { $ceiling = 2.8 }
        $height = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, '{0:0.00}', [double]$ceiling + 0.4)
        $change = Invoke-Plynth "$label-wall-set" @('wall', 'set', $wall.id, '--height', $height, '--project', $copy)
        $problem = Get-AnswerProblem $change
        Add-Step "${label}: wall $($wall.id) raised to $height m" (-not $problem) $problem
        $built = Invoke-Build $label $copy
        if (-not (Get-AnswerProblem $built)) {
            $second = Invoke-Send $label $copy
            if (-not (Get-AnswerProblem $second)) {
                $before = @(Get-Actors $firstSends[$ChangeSample] | ForEach-Object { $_.label } | Sort-Object)
                $after = @(Get-Actors $second | ForEach-Object { $_.label } | Sort-Object)
                $spawned = @(Get-Actors $second | Where-Object { $_.action -eq 'spawned' })
                $meshes = @(Get-Assets $second @('static_mesh'))
                $created = @($meshes | Where-Object { $_.action -eq 'created' })
                $updated = @($meshes | Where-Object { $_.action -eq 'updated' })
                $same = ($before -join '|') -eq ($after -join '|')
                $passed = $same -and $before.Count -gt 0 -and $spawned.Count -eq 0 -and $created.Count -eq 0
                $note = "$($after.Count) actors, same labels $same, $($spawned.Count) spawned; meshes $($updated.Count) updated, $($created.Count) created"
                Add-Step "${label}: actors kept and meshes updated in place" $passed $note
            }
        }
    }

    $baseName = Get-Value ([System.IO.File]::ReadAllText((Join-Path $repo "samples\projects\$ChangeSample\project.json")) | ConvertFrom-Json) 'name'
    $variants = @(
        @{ Label = "$ChangeSample-pack-off"; Name = "$baseName Pack Off"; Pack = 'off'; Grouped = 'auto' },
        @{ Label = "$ChangeSample-grouped"; Name = "$baseName Grouped"; Pack = 'on'; Grouped = 'on' }
    )
    foreach ($variant in $variants) {
        $label = $variant.Label
        $copy = Copy-Sample $ChangeSample $label $variant.Name
        $set = Invoke-Plynth "$label-set" @('link', 'unreal', 'set', '--pack-materials', $variant.Pack, '--grouped', $variant.Grouped, '--project', $copy) 5
        $problem = Get-AnswerProblem $set
        Add-Step "${label}: link unreal set --pack-materials $($variant.Pack) --grouped $($variant.Grouped)" (-not $problem) $problem
        if ($problem) { continue }
        $built = Invoke-Build $label $copy
        if (Get-AnswerProblem $built) { continue }
        $send = Invoke-Send $label $copy
        if (Get-AnswerProblem $send) { continue }
        $result = Get-Value $send.Answer 'result'
        if ($variant.Pack -eq 'off') {
            $made = @(Get-Assets $send @('texture', 'material', 'material_instance'))
            $passed = (Get-Value $result 'pack_materials') -eq $false -and $made.Count -eq 0
            Add-Step "${label}: no texture or material asset" $passed "pack_materials $(Get-Value $result 'pack_materials'), $($made.Count) texture or material assets"
        } else {
            $meshes = @(Get-Assets $send @('static_mesh'))
            $actors = @(Get-Actors $send)
            $used = @($actors | ForEach-Object { $_.asset } | Sort-Object -Unique)
            $passed = (Get-Value $result 'grouped') -eq $true -and $meshes.Count -gt 1 -and $actors.Count -eq $meshes.Count -and $used.Count -eq $actors.Count
            Add-Step "${label}: one actor per group" $passed "grouped $(Get-Value $result 'grouped'), $($meshes.Count) meshes, $($actors.Count) actors"
        }
    }
} catch {
    if ($_.Exception.Message -ne 'stop') { Add-Step 'the run' $false "stopped: $($_.Exception.Message)" }
} finally {
    if ($script:editorProcess) {
        $quit = $false
        try { $quit = Stop-EditorCleanly 5 } catch { $quit = $false }
        if ($quit) {
            Add-Step 'the editor quits cleanly' $true "exit code $($script:editorProcess.ExitCode)"
        } else {
            if (-not $script:editorProcess.HasExited) { Stop-Tree $script:editorProcess.Id }
            Add-Step 'the editor quits cleanly' $false 'it had to be stopped'
        }
    }
    $failedSteps = Write-Summary
}

if ($failedSteps -gt 0) { exit 1 }
exit 0
