# Smoke test for Mosics.exe on a real Windows machine (used by CI): runs first-time setup, searches,
# plays a song, visits every page, and saves a screenshot of each plus memory/CPU numbers.
# Usage: pwsh smoke.ps1 -Exe path\to\Mosics.exe -Out screenshots
param([Parameter(Mandatory)] [string]$Exe, [string]$Out = "screenshots")
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class U {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint f);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    public static string Describe(uint pid) {
        var sb = new System.Text.StringBuilder();
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid) {
                var c = new System.Text.StringBuilder(256); var t = new System.Text.StringBuilder(512);
                GetClassName(h, c, 256); GetWindowText(h, t, 512);
                sb.AppendLine("  window class=" + c + " title=" + t + " visible=" + IsWindowVisible(h));
            }
            return true;
        }, IntPtr.Zero);
        return sb.ToString();
    }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

function Shot($name) {
    $r = New-Object U+RECT
    [U]::GetClientRect($script:win, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ($r.Right), ($r.Bottom)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc()
    [U]::PrintWindow($script:win, $dc, 1) | Out-Null   # PW_CLIENTONLY: Mosics draws itself via WM_PRINTCLIENT
    $g.ReleaseHdc($dc)
    $g.Dispose()
    $bmp.Save("$Out\$name.png")
    $script:last = $bmp
    Write-Host "screenshot $name"
}
function Lp($x, $y) { [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }
function Click($x, $y) {
    [U]::PostMessage($script:win, 0x0200, [IntPtr]0, (Lp $x $y)) | Out-Null   # WM_MOUSEMOVE
    Start-Sleep -Milliseconds 150
    [U]::PostMessage($script:win, 0x0201, [IntPtr]1, (Lp $x $y)) | Out-Null   # WM_LBUTTONDOWN
    [U]::PostMessage($script:win, 0x0202, [IntPtr]0, (Lp $x $y)) | Out-Null   # WM_LBUTTONUP
    Start-Sleep -Milliseconds 600
}
function DoubleClick($x, $y) {
    Click $x $y
    [U]::PostMessage($script:win, 0x0203, [IntPtr]1, (Lp $x $y)) | Out-Null   # WM_LBUTTONDBLCLK
    [U]::PostMessage($script:win, 0x0202, [IntPtr]0, (Lp $x $y)) | Out-Null
}
# The primary (red) button of a sheet: move the mouse away (no hover colour), then scan up from the bottom along x.
function FindRed($x, $name) {
    [U]::PostMessage($script:win, 0x0200, [IntPtr]0, (Lp 2 2)) | Out-Null
    Start-Sleep -Milliseconds 500
    Shot $name
    for ($y = $script:last.Height - 1; $y -gt 0; $y--) {
        $c = $script:last.GetPixel($x, $y)
        if ([Math]::Abs($c.R - 215) -lt 14 -and [Math]::Abs($c.G - 38) -lt 14 -and [Math]::Abs($c.B - 30) -lt 14) { return $y - 14 }
    }
    return -1
}
function Memory($label) {
    $p = Get-Process -Id $script:proc.Id
    $line = "{0,-28} working set {1,6:N1} MB   private {2,6:N1} MB   threads {3}" -f $label, ($p.WorkingSet64 / 1MB), ($p.PrivateMemorySize64 / 1MB), $p.Threads.Count
    Write-Host $line
    Add-Content "$Out\stats.txt" $line
}
function Cpu($label, $seconds) {
    $p = Get-Process -Id $script:proc.Id
    $t0 = $p.TotalProcessorTime
    Start-Sleep -Seconds $seconds
    $p.Refresh()
    $pct = ($p.TotalProcessorTime - $t0).TotalSeconds / $seconds / [Environment]::ProcessorCount * 100
    $line = "{0,-28} CPU {1:N2}% of the machine ({2:N1}% of one core)" -f $label, $pct, ($pct * [Environment]::ProcessorCount)
    Write-Host $line
    Add-Content "$Out\stats.txt" $line
}

# Run it like a fresh download.
$dl = Join-Path $env:TEMP "Downloads"
New-Item -ItemType Directory -Force -Path $dl | Out-Null
Copy-Item $Exe "$dl\Mosics.exe" -Force
$script:proc = Start-Process "$dl\Mosics.exe" -PassThru
for ($i = 0; $i -lt 40 -and -not $script:win; $i++) { Start-Sleep -Milliseconds 250; $w = [U]::FindWindow("MosicsMain", [NullString]::Value); if ($w -ne [IntPtr]::Zero) { $script:win = $w } }
if (-not $script:win) {
    Start-Sleep -Seconds 2
    if ($script:proc.HasExited) { Write-Host "Mosics exited with code $($script:proc.ExitCode)" }
    else { Write-Host "Mosics is running but has no main window:"; Write-Host ([U]::Describe([uint32]$script:proc.Id)) }
    throw "Mosics window didn't appear"
}
[U]::SetWindowPos($script:win, [IntPtr]::Zero, 0, 0, 1180, 780, 0x0014) | Out-Null
Start-Sleep -Seconds 4
$r = New-Object U+RECT; [U]::GetClientRect($script:win, [ref]$r) | Out-Null
$W = $r.Right; $H = $r.Bottom
Write-Host "client $W x $H"
Memory "launch (setup sheet)"

# Set up: install + download yt-dlp and Deno.
$bx = [int](($W + 580) / 2 - 28 - 30)
$by = FindRed $bx "01-setup"
if ($by -lt 0) { throw "Set up button not found" }
Click $bx $by
$bin = Join-Path $env:LOCALAPPDATA "Mosics\bin"
for ($i = 0; $i -lt 120; $i++) {
    if ((Test-Path "$bin\yt-dlp.exe") -and (Test-Path "$bin\deno.exe")) { break }
    if ($i -eq 6) { Shot "02-setup-working" }
    Start-Sleep -Seconds 2
}
Start-Sleep -Seconds 3
"installed exe:   $(Test-Path "$env:LOCALAPPDATA\Programs\Mosics\Mosics.exe")" | Tee-Object -Append "$Out\stats.txt"
"start menu link: $(Test-Path "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Mosics.lnk")" | Tee-Object -Append "$Out\stats.txt"
"yt-dlp:          $(Test-Path "$bin\yt-dlp.exe")   deno: $(Test-Path "$bin\deno.exe")" | Tee-Object -Append "$Out\stats.txt"
& "$bin\yt-dlp.exe" --version | Tee-Object -Append "$Out\stats.txt"
$by = FindRed $bx "03-setup-done"
if ($by -lt 0) { throw "Start listening button not found" }
Click $bx $by                      # Start listening
Start-Sleep -Seconds 25            # Home mixes load
Shot "04-home"
Memory "home"

# Search and play.
Click 100 134                      # sidebar: Search
$edit = [U]::FindWindowEx($script:win, [IntPtr]::Zero, "Edit", [NullString]::Value)
[U]::SendMessage($edit, 0x000C, [IntPtr]0, "Oasis Wonderwall") | Out-Null   # WM_SETTEXT
Start-Sleep -Seconds 6
Shot "05-search"
DoubleClick ([int]($W / 2)) 214
Start-Sleep -Seconds 30
Shot "06-play-from-youtube"

# YouTube often asks cloud machines to sign in, so also play a cached test tone through the same path.
$lib = Get-Content "$env:APPDATA\Mosics\library.json" -Raw | ConvertFrom-Json
$id = $lib.recent[0].id
Copy-Item "$PSScriptRoot\tone.aac" "$env:LOCALAPPDATA\Mosics\cache\$id.aac" -Force
"test tone cached as $id" | Tee-Object -Append "$Out\stats.txt"
DoubleClick ([int]($W / 2)) 214
$cx = [int]((328 + $W - 258) / 2)
Click ($cx + 92) ($H - 88 + 32)    # repeat: all
Click ($cx + 92) ($H - 88 + 32)    # repeat: one (keeps the tone looping)
Start-Sleep -Seconds 6
Shot "06-playing"
Memory "playing"
Cpu "playing, Search page" 10

Click 100 93                       # Home
Start-Sleep -Seconds 4
Shot "07-home-playing"
Cpu "playing, Home page" 10
Click 140 ($H - 44)                # player bar: open Now Playing
Start-Sleep -Seconds 3
Shot "08-now-playing"
Cpu "playing, Now Playing" 10
Click ($W - 45) 55                 # close Now Playing
Click 100 175                      # Liked Songs
Start-Sleep -Seconds 2
Shot "09-liked"
Click 100 257                      # Queue
Start-Sleep -Seconds 3
Shot "10-queue"
Click 100 216                      # Recently Played
Start-Sleep -Seconds 3
Shot "11-recent"

# New playlist (accept the suggested name in the rename prompt).
Click 116 ($H - 88 - 16 - 21)
Start-Sleep -Seconds 1
$prompt = [U]::FindWindow("MosicsPrompt", [NullString]::Value)
if ($prompt -ne [IntPtr]::Zero) { [U]::PostMessage($prompt, 0x0111, [IntPtr]1, [IntPtr]0) | Out-Null }   # WM_COMMAND IDOK
Start-Sleep -Seconds 2
Shot "12-playlist"

# Close to the tray: the music keeps going, the window's memory is released.
[U]::PostMessage($script:win, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null   # WM_CLOSE
Start-Sleep -Seconds 6
"window visible after close: $([U]::IsWindowVisible($script:win))" | Tee-Object -Append "$Out\stats.txt"
Memory "closed to tray"
Cpu "closed to tray" 10

Stop-Process -Id $script:proc.Id
Write-Host "done"
