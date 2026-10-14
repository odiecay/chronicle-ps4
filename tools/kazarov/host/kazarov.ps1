# Kazarov entry point for Windows hosts, run by host/kazarov.cmd.
# Usage: kazarov.ps1 ROOT build|run [config] [args...]
# Windows configurations run through KZ_WINDOWS_HOST_SCRIPT; the others run in the container.

param(
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][ValidateSet('build', 'run')][string]$Command,
    [Parameter(ValueFromRemainingArguments)][string[]]$Rest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The Kazarov checkout this script lives in.
$Kz = Split-Path -Parent $PSScriptRoot

# The project directory, without a trailing separator.
$fullRoot = [System.IO.Path]::GetFullPath($Root)
$trimmedRoot = $fullRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
if ($trimmedRoot) { $Root = $trimmedRoot } else { $Root = $fullRoot }

if ($null -eq $Rest) { $Rest = @() }

. (Join-Path $PSScriptRoot 'kazarov-quote.ps1')

function Fail([string]$Message) {
    [Console]::Error.WriteLine("kazarov: $Message")
    exit 1
}

# Runs a native program with an argument array, leaving the output on the console and its exit code in
# $script:NativeExit. Before 7.3, PowerShell joins the array without quoting, so there the command line
# is quoted by ConvertTo-WindowsArgument and the program is started as a process.
function Invoke-Native([string]$FilePath, [string[]]$NativeArgs) {
    if ($PSVersionTable.PSVersion -ge [version]'7.3') {
        # A session may have chosen Legacy passing; this call needs array passing.
        $PSNativeCommandArgumentPassing = 'Standard'
        & $FilePath @NativeArgs
        $script:NativeExit = $LASTEXITCODE
        return
    }
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.Arguments = (@($NativeArgs | ForEach-Object { ConvertTo-WindowsArgument $_ })) -join ' '
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    if ($null -eq $proc) { Fail "cannot start $FilePath" }
    $proc.WaitForExit()
    $script:NativeExit = $proc.ExitCode
}

# Words of a space-separated configuration list.
function Split-Words([string]$Text) {
    return @($Text -split '\s+' | Where-Object { $_ -ne '' })
}

$confPath = Join-Path $Root 'kazarov.conf'
if (-not (Test-Path -LiteralPath $confPath -PathType Leaf)) {
    Fail 'kazarov.conf not found in the project root'
}
$confLines = [System.IO.File]::ReadAllLines($confPath)

# The value of NAME= in kazarov.conf. The last such line wins; quotes and parentheses are removed.
function Get-ConfValue([string]$Name) {
    $value = ''
    foreach ($line in $confLines) {
        if ($line.StartsWith("$Name=", [StringComparison]::Ordinal)) {
            $value = $line.Substring($Name.Length + 1)
        }
    }
    return ($value -replace '["()]', '').Trim()
}

# The first PCSX2 that exists: PCSX2 from the environment, then the PATH, then the default install.
function Find-Pcsx2 {
    if ($env:PCSX2) { return $env:PCSX2 }
    $onPath = Get-Command pcsx2-qt.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    if ($env:ProgramFiles) {
        $installed = Join-Path $env:ProgramFiles 'PCSX2\pcsx2-qt.exe'
        if (Test-Path -LiteralPath $installed) { return $installed }
    }
    return ''
}

function Invoke-WindowsHost([string]$Config, [string[]]$HostArgs) {
    if (-not $windowsHostScript) { Fail 'KZ_WINDOWS_HOST_SCRIPT is not set in kazarov.conf' }
    $ps = ''
    if (Get-Command pwsh -ErrorAction SilentlyContinue) {
        $ps = 'pwsh'
    }
    elseif (Get-Command powershell -ErrorAction SilentlyContinue) {
        $ps = 'powershell'
    }
    if (-not $ps) { Fail 'neither pwsh nor powershell is on PATH' }

    $hostPath = Join-Path $Root $windowsHostScript
    Push-Location -LiteralPath $Root
    try {
        & $ps -NoProfile -File $hostPath $Command $Config @HostArgs
        $code = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    exit $code
}

function Invoke-Container([string]$Config, [string[]]$ContainerArgs) {
    if (-not $project) { Fail 'KZ_PROJECT is not set in kazarov.conf' }

    # Builder: BUILDER, then podman, then docker.
    $bld = $env:BUILDER
    if (-not $bld -and (Get-Command podman -ErrorAction SilentlyContinue)) { $bld = 'podman' }
    if (-not $bld -and (Get-Command docker -ErrorAction SilentlyContinue)) { $bld = 'docker' }
    if (-not $bld) { Fail 'no container builder found: set BUILDER, or install podman or docker' }

    # Image tag: the first 12 hex digits of the image/ tree. Uncommitted edits or no commit give
    # "dirty", which is rebuilt every time. The content-hash tags live in the bash driver.
    $tag = 'dirty'
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $full = ''
        $head = & git -C $Kz rev-parse 'HEAD:image' 2>$null
        if ($LASTEXITCODE -eq 0 -and $head) { $full = ([string]($head | Select-Object -First 1)).Trim() }
        $dirtyText = (& git -C $Kz status --porcelain -- image 2>$null) -join ''
        if ($full -and -not $dirtyText) { $tag = $full.Substring(0, [Math]::Min(12, $full.Length)) }
    }

    $img = "kazarov-dev:$tag"
    $need = ($tag -eq 'dirty') -or ($env:REBUILD_IMAGE -eq '1')
    if (-not $need) {
        & $bld image inspect $img *> $null
        $need = ($LASTEXITCODE -ne 0)
    }
    if ($need) {
        $imageDir = Join-Path $Kz 'image'
        Invoke-Native $bld @('build', '--platform', 'linux/amd64', '--build-arg', "KZ_IMAGE_ID=$tag",
            '--label', "org.themoonpeople.kazarov.image=$tag", '-t', $img, $imageDir)
        if ($script:NativeExit -ne 0) { Fail 'image build failed' }
    }

    # The container runs the image ID, not the tag, so it is the image that was just ensured.
    $imgId = ''
    $idOut = & $bld image inspect --format '{{.Id}}' $img 2>$null
    if ($LASTEXITCODE -eq 0 -and $idOut) { $imgId = ([string]($idOut | Select-Object -First 1)).Trim() }
    if (-not $imgId) { Fail "image $img not found after the build" }

    # The run descriptor is written inside the project so the host can read it back.
    $kzDir = Join-Path $Root '.kazarov'
    if (-not (Test-Path -LiteralPath $kzDir)) { New-Item -ItemType Directory -Path $kzDir | Out-Null }
    $desc = 'run-' + [guid]::NewGuid().ToString('N') + '.txt'

    $runArgs = @('run', '--rm', '--init',
        '-v', "${Root}:/work/$project",
        '-w', "/work/$project",
        '-e', 'HOME=/tmp',
        '-e', 'KZ_HOST_OS=windows',
        '-e', 'CLEAN',
        '-e', 'JOBS',
        '-e', "KZ_RUN_OUT=.kazarov/$desc",
        $imgId, 'tools/kazarov/bin/kazarov', '--inside', $Command)
    if ($Config) { $runArgs += $Config }
    $runArgs += $ContainerArgs
    Invoke-Native $bld $runArgs
    $rc = $script:NativeExit
    if ($rc -ne 0) { exit $rc }

    if ($Command -ne 'run') { exit 0 }
    $descPath = Join-Path $kzDir $desc
    if (-not (Test-Path -LiteralPath $descPath)) { exit 0 }
    $dline = [string](Get-Content -LiteralPath $descPath -TotalCount 1)
    Remove-Item -LiteralPath $descPath

    if ($dline.StartsWith('iso:')) {
        $pcsx2 = Find-Pcsx2
        if (-not $pcsx2) { Fail 'pcsx2-qt.exe not found: set PCSX2 or install PCSX2' }
        $target = Join-Path $Root $dline.Substring(4)
        # One command-line string, so the path is quoted the same way in every PowerShell edition.
        $pcsxArgs = '-fastboot -- ' + (ConvertTo-WindowsArgument $target)
        Start-Process -FilePath $pcsx2 -ArgumentList $pcsxArgs | Out-Null
        exit 0
    }
    if ($dline.StartsWith('exe:')) {
        $target = Join-Path $Root $dline.Substring(4)
        Invoke-Native $target $ContainerArgs
        exit $script:NativeExit
    }
    Fail 'unreadable run descriptor'
}

$project = Get-ConfValue 'KZ_PROJECT'
$defaultBuild = Get-ConfValue 'KZ_DEFAULT_BUILD'
$defaultBuildWindows = Get-ConfValue 'KZ_DEFAULT_BUILD_WINDOWS'
$defaultRun = Get-ConfValue 'KZ_DEFAULT_RUN'
$defaultRunWindows = Get-ConfValue 'KZ_DEFAULT_RUN_WINDOWS'
$windowsHostScript = Get-ConfValue 'KZ_WINDOWS_HOST_SCRIPT'
$windowsConfigs = Split-Words (Get-ConfValue 'KZ_WINDOWS_CONFIGS')
$containerConfigsText = Get-ConfValue 'KZ_CONTAINER_CONFIGS'
$containerConfigs = Split-Words $containerConfigsText
$hasContainerList = [bool]$containerConfigsText

# The Windows default is the per-OS default for the command, then the generic one.
if ($Command -eq 'run') {
    $defaultCfg = $defaultRunWindows
    if (-not $defaultCfg) { $defaultCfg = $defaultRun }
}
else {
    $defaultCfg = $defaultBuildWindows
    if (-not $defaultCfg) { $defaultCfg = $defaultBuild }
}

# The first argument is the config when it is listed in KZ_WINDOWS_CONFIGS or KZ_CONTAINER_CONFIGS.
# Otherwise it is a program argument or target and the default config is used with all of Rest.
# Without KZ_CONTAINER_CONFIGS, any first argument that does not start with - is the config.
$arg1 = ''
if ($Rest.Count -gt 0) { $arg1 = $Rest[0] }
$tail = @()
if ($Rest.Count -gt 1) { $tail = @($Rest[1..($Rest.Count - 1)]) }

$useWindows = ($arg1 -ne '') -and ($windowsConfigs -contains $arg1)
$useContainer = ($arg1 -ne '') -and (
    ($hasContainerList -and ($containerConfigs -contains $arg1)) -or
    (-not $hasContainerList -and -not $arg1.StartsWith('-')))

if ($useWindows) { Invoke-WindowsHost $arg1 $tail }
if ($useContainer) { Invoke-Container $arg1 $tail }

if ($defaultCfg -and ($windowsConfigs -contains $defaultCfg)) {
    Invoke-WindowsHost $defaultCfg $Rest
}
if (-not $defaultCfg) { Fail "no default $Command configuration for this OS" }
Invoke-Container $defaultCfg $Rest
