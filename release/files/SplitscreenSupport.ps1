# Splitscreen Support for Left 4 Dead 2: launcher, shortcut installer, uninstaller.
#
# Start      copies the plugin, the settings and two packages into the game for
#            one session, starts the game through Steam, and takes everything
#            out again when the game has exited.
# Install    checks the game and creates Desktop and Start menu shortcuts.
# Uninstall  removes the shortcuts and anything a session left in the game.
# Cleanup    internal: what Start runs in the background after the game.
param(
    [ValidateSet('Start', 'Install', 'Uninstall', 'Cleanup')][string]$Action = 'Start',
    # Found through Steam when omitted.
    [string]$Game,
    [string]$Steam,
    # Cleanup only: wait for the game to start and to exit first.
    [switch]$Wait,
    # Report an error as text only (the .cmd files keep their window open).
    [switch]$NoDialog
)

$ErrorActionPreference = 'Stop'
$files = $PSScriptRoot
$packageRoot = Split-Path -Parent $files
$settingsFile = Join-Path $packageRoot 'config.ini'
$shortcutName = 'Left 4 Dead 2 - Local Co-op.lnk'
$hudPackage = 'splitscreen_support_hud.vpk'
$menuPackage = 'splitscreen_support_menus.vpk'
# Game modes whose addon restriction must be lifted for the game this PC hosts.
$teamModes = 'versus', 'scavenge', 'mutation15'
$latin1 = [Text.Encoding]::GetEncoding(28591)

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Find-Steam {
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        $properties = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if (-not $properties) { continue }
        foreach ($candidate in $properties.SteamExe, $(if ($properties.SteamPath) { Join-Path $properties.SteamPath 'steam.exe' }),
                $(if ($properties.InstallPath) { Join-Path $properties.InstallPath 'steam.exe' })) {
            if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return (Get-Item -LiteralPath $candidate).FullName }
        }
    }
    throw 'Steam was not found. Start this script with -Steam "<path to steam.exe>".'
}

function Find-Game([string]$SteamExe) {
    $steamRoot = Split-Path -Parent $SteamExe
    $libraries = @($steamRoot)
    $index = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $index -PathType Leaf) {
        foreach ($match in [regex]::Matches([IO.File]::ReadAllText($index), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($library in $libraries | Select-Object -Unique) {
        $manifest = Join-Path $library 'steamapps\appmanifest_550.acf'
        if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { continue }
        $folder = [regex]::Match([IO.File]::ReadAllText($manifest), '"installdir"\s+"([^"]+)"').Groups[1].Value
        $candidate = Join-Path $library (Join-Path 'steamapps\common' $folder)
        if ($folder -and (Test-Path -LiteralPath (Join-Path $candidate 'left4dead2.exe') -PathType Leaf)) { return $candidate }
    }
    throw 'Left 4 Dead 2 was not found in any Steam library. Start this script with -Game "<game folder>".'
}

function Resolve-Installation {
    if (-not $script:Steam) { $script:Steam = Find-Steam }
    if (-not (Test-Path -LiteralPath $script:Steam -PathType Leaf)) { throw "Steam was not found at $script:Steam." }
    if (-not $script:Game) { $script:Game = Find-Game $script:Steam }
    if (-not (Test-Path -LiteralPath (Join-Path $script:Game 'left4dead2.exe') -PathType Leaf)) {
        throw "The folder $script:Game does not contain left4dead2.exe."
    }
    $script:addons = Join-Path $script:Game 'left4dead2\addons'
    $script:session = Join-Path $script:addons 'splitscreen_support.session.json'
    $script:addonList = Join-Path $script:Game 'left4dead2\addonlist.txt'
    $script:policy = Join-Path $script:Game 'left4dead2\cfg\addonconfig.cfg'
}

# The plugin finds its places in the game by search patterns and refuses a game
# in which one is missing. The packages must not go onto such a game either:
# their menus need the plugin. A game whose files are the ones this version was
# made from is known to fit; any other is asked of the checker, which runs the
# plugin's own search on the files.
function Get-ChangedModules {
    $table = Get-Content -LiteralPath (Join-Path $files 'supported_build.json') -Raw | ConvertFrom-Json
    foreach ($module in $table.modules) {
        $path = Join-Path $script:Game $module.path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Sha256 $path) -ne $module.sha256) { $module.name }
    }
}

function Assert-SupportedBuild {
    $table = Get-Content -LiteralPath (Join-Path $files 'supported_build.json') -Raw | ConvertFrom-Json
    $changed = @(Get-ChangedModules)
    if (-not $changed.Count) { return }
    $report = @(& (Join-Path $files 'splitscreen_check.exe') $script:Game 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $detail = ($report | Select-Object -First 3) -join '; '
        throw ("Your game differs from Left 4 Dead 2 $($table.game), which this version was made for, " +
            "in a place Splitscreen Support needs ($detail). Nothing was changed; the game starts " +
            'normally through Steam. Look for a newer version of Splitscreen Support.')
    }
}

function Set-AddonListed([string[]]$Names, [bool]$Listed) {
    if (-not (Test-Path -LiteralPath $script:addonList -PathType Leaf)) { return }
    $text = [IO.File]::ReadAllText($script:addonList)
    $updated = $text
    foreach ($name in $Names) {
        $line = '(?m)^[ \t]*"' + [regex]::Escape($name) + '"[ \t]+"[01]"[ \t]*\r?\n'
        $updated = [regex]::Replace($updated, $line, '')
        if ($Listed) {
            $close = $updated.LastIndexOf('}')
            if ($close -lt 0) { throw 'Unexpected addonlist.txt format.' }
            $updated = $updated.Substring(0, $close) + "`t`"$name`"`t`t`"1`"`r`n" + $updated.Substring($close)
        }
    }
    if ($updated -ne $text) { [IO.File]::WriteAllText($script:addonList, $updated, (New-Object Text.UTF8Encoding $false)) }
}

function Remove-Quietly([string]$Path) {
    for ($try = 0; $try -lt 10 -and (Test-Path -LiteralPath $Path -PathType Leaf); $try++) {
        try { Remove-Item -LiteralPath $Path -Force } catch { Start-Sleep -Milliseconds 500 }
    }
}

# Takes out what a session put into the game. Safe to run at any time the game
# is closed: every step checks that the file is still the one that was put there.
function Remove-Session {
    if (-not (Test-Path -LiteralPath $script:session -PathType Leaf)) { return $false }
    $state = Get-Content -LiteralPath $script:session -Raw | ConvertFrom-Json
    foreach ($file in $state.files) {
        if ((Test-Path -LiteralPath $file.path -PathType Leaf) -and (Get-Sha256 $file.path) -eq $file.sha256) { Remove-Quietly $file.path }
    }
    Set-AddonListed @($hudPackage, $menuPackage) $false
    if ($state.policy -and (Test-Path -LiteralPath $state.policy.path -PathType Leaf) -and
            (Get-Sha256 $state.policy.path) -eq $state.policy.patched) {
        [IO.File]::WriteAllBytes($state.policy.path, [Convert]::FromBase64String($state.policy.original))
    }
    # The plugin writes its log beside itself; keep the last one with the package.
    $log = Join-Path $script:addons 'splitscreen_support.log'
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        try { Move-Item -LiteralPath $log -Destination (Join-Path $packageRoot 'splitscreen_support.log') -Force } catch { Remove-Quietly $log }
    }
    Remove-Quietly $script:session
    return $true
}

function Assert-GameClosed {
    if (Get-Process -Name left4dead2 -ErrorAction SilentlyContinue) { throw 'Left 4 Dead 2 is running. Close the game first.' }
}

function Start-Session {
    Assert-GameClosed
    Resolve-Installation
    [void](Remove-Session)   # a previous session that could not clean up (crash, power loss)
    Assert-SupportedBuild
    if (-not (Test-Path -LiteralPath $settingsFile -PathType Leaf)) { throw "The settings file $settingsFile is missing." }
    # ConsoleUI=0 is the PC look; its layouts are a second pair of packages.
    $pcLook = [bool](Select-String -LiteralPath $settingsFile -Pattern '^\s*ConsoleUI\s*=\s*0\s*$' -Quiet)
    $variant = if ($pcLook) { '_pc' } else { '' }
    # Extra game options from the same file, e.g. GameArguments=-novid
    $extra = [regex]::Match([IO.File]::ReadAllText($settingsFile), '(?m)^[ \t]*GameArguments[ \t]*=[ \t]*(.*?)[ \t]*\r?$').Groups[1].Value
    $copies = @(
        @{ source = Join-Path $files 'splitscreen_support.dll'; target = Join-Path $script:addons 'splitscreen_support.dll' },
        @{ source = $settingsFile; target = Join-Path $script:addons 'splitscreen_support.ini' },
        @{ source = Join-Path $files ('hud' + $variant + '.vpk'); target = Join-Path $script:addons $hudPackage },
        @{ source = Join-Path $files ('menus' + $variant + '.vpk'); target = Join-Path $script:addons $menuPackage })
    foreach ($copy in $copies) {
        if (-not (Test-Path -LiteralPath $copy.source -PathType Leaf)) { throw "The package is incomplete: $($copy.source) is missing." }
    }
    New-Item -ItemType Directory -Path $script:addons -Force | Out-Null
    $state = @{ files = @(); policy = $null }
    try {
        foreach ($copy in $copies) {
            Copy-Item -LiteralPath $copy.source -Destination $copy.target -Force
            $state.files += @{ path = $copy.target; sha256 = Get-Sha256 $copy.target }
        }
        Set-AddonListed @($hudPackage, $menuPackage) $true
        # Valve's host policy blocks addons in team modes this PC hosts; the
        # HUD and the script are addons. Lift it for this session only.
        if (Test-Path -LiteralPath $script:policy -PathType Leaf) {
            $original = [IO.File]::ReadAllBytes($script:policy)
            $pattern = '(?im)^([ \t]*"(?:' + ($teamModes -join '|') + ')"[ \t]+")1(")'
            $patched = $latin1.GetBytes([regex]::Replace($latin1.GetString($original), $pattern, '${1}0${2}'))
            if ([Convert]::ToBase64String($patched) -ne [Convert]::ToBase64String($original)) {
                [IO.File]::WriteAllBytes($script:policy, $patched)
                $state.policy = @{ path = $script:policy; patched = Get-Sha256 $script:policy; original = [Convert]::ToBase64String($original) }
            }
        }
    } finally {
        # Written even when a step above failed, so the next run can undo it.
        [IO.File]::WriteAllText($script:session, ($state | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
    }

    $arguments = @('-applaunch', '550', '-insecure') + $(if ($extra) { @($extra) } else { @() }) +
        @('+plugin_load', 'addons/splitscreen_support', '+splitscreen_enable')
    Start-Process -FilePath $script:Steam -ArgumentList $arguments
    $watcher = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', ('"' + $PSCommandPath + '"'),
        '-Action', 'Cleanup', '-Wait', '-Game', ('"' + $script:Game + '"'), '-Steam', ('"' + $script:Steam + '"'))
    Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList $watcher
    Write-Output ('Starting Left 4 Dead 2 with local co-op (' + $(if ($pcLook) { 'PC look' } else { 'Xbox look' }) + ').')
    Write-Output 'The game folder is put back as it was when the game exits.'
}

function Wait-ForGame {
    $deadline = (Get-Date).AddMinutes(5)
    while (-not (Get-Process -Name left4dead2 -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    while (Get-Process -Name left4dead2 -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 3 }
    Start-Sleep -Seconds 2
}

function Get-ShortcutPaths {
    [Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs') |
        Where-Object { $_ } | ForEach-Object { Join-Path $_ $shortcutName }
}

function Install-Shortcuts {
    Resolve-Installation
    Assert-SupportedBuild
    $shell = New-Object -ComObject WScript.Shell
    foreach ($path in Get-ShortcutPaths) {
        $shortcut = $shell.CreateShortcut($path)
        $shortcut.TargetPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $shortcut.Arguments = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $PSCommandPath + '" -Action Start'
        $shortcut.WorkingDirectory = $packageRoot
        $shortcut.IconLocation = (Join-Path $script:Game 'left4dead2.exe') + ',0'
        $shortcut.Description = 'Left 4 Dead 2 with local split-screen co-op'
        $shortcut.Save()
        Write-Output "Created $path"
    }
    Write-Output "Game: $script:Game"
    Write-Output 'The game build is supported. Nothing was copied into the game: that happens for each session.'
    foreach ($old in (Join-Path $script:addons 'splitscreen_support.vpk'), (Join-Path $script:addons 'workshop\458854947.vpk')) {
        if (Test-Path -LiteralPath $old -PathType Leaf) {
            Write-Output "Note: the older VPK-only Splitscreen Support is installed ($old). Remove or unsubscribe it; the two change the same menus."
        }
    }
    Write-Output 'Keep this folder where it is: the shortcuts start the game from here.'
}

function Uninstall-Everything {
    Assert-GameClosed
    Resolve-Installation
    if (Remove-Session) { Write-Output 'Removed what the last session left in the game.' }
    # Anything by these names is ours, session record or not.
    foreach ($name in 'splitscreen_support.dll', 'splitscreen_support.ini', 'splitscreen_support.log', $hudPackage, $menuPackage) {
        $path = Join-Path $script:addons $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Quietly $path; Write-Output "Removed $path" }
    }
    Set-AddonListed @($hudPackage, $menuPackage) $false
    foreach ($path in Get-ShortcutPaths) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force; Write-Output "Removed $path" }
    }
    Write-Output 'Left 4 Dead 2 is as it was. Delete this folder to remove Splitscreen Support completely.'
}

try {
    switch ($Action) {
        'Start' { Start-Session }
        'Install' { Install-Shortcuts }
        'Uninstall' { Uninstall-Everything }
        'Cleanup' {
            Resolve-Installation
            if ($Wait) { Wait-ForGame } else { Assert-GameClosed }
            [void](Remove-Session)
        }
    }
} catch {
    $message = $_.Exception.Message
    if ($Action -ne 'Cleanup' -and -not $NoDialog) {
        # A shortcut starts this script without a window: show the reason.
        try {
            Add-Type -AssemblyName System.Windows.Forms
            [void][System.Windows.Forms.MessageBox]::Show($message, 'Splitscreen Support', 'OK', 'Warning')
        } catch { }
    }
    Write-Output "Error: $message"
    exit 1
}
