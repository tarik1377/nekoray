# GreenRhythm live preview bridge for the Claude Code Browser pane.
# Native Qt windows can't render in the browser preview, so this tiny server
# captures the running greenrhythm.exe window (PrintWindow) on every request
# and serves it as an auto-refreshing page on http://127.0.0.1:8377/
param(
    [int]$Port = 8377,
    [string]$AppDir = "C:\Users\devops\AppData\Local\Temp\claude\C--nekoray-src\3a775c86-e119-487c-9f2d-e44554251d4c\scratchpad\gr1311\GreenRhythm"
)
$ErrorActionPreference = 'Continue'

$src = @"
using System;
using System.Runtime.InteropServices;
public class PWSrv {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
"@
if (-not ([System.Management.Automation.PSTypeName]'PWSrv').Type) { Add-Type -TypeDefinition $src }
Add-Type -AssemblyName System.Drawing

function Get-ShotBytes {
    param([string]$AppDir)
    try {
        $p = Get-Process greenrhythm -ErrorAction SilentlyContinue |
             Where-Object { $_.Path -like "$AppDir*" } | Select-Object -First 1
        if (-not $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { return $null }
        $r = New-Object PWSrv+RECT
        [PWSrv]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $h = $r.B - $r.T
        if ($w -le 0 -or $h -le 0) { return $null }
        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [PWSrv]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose()
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $bytes = $ms.ToArray(); $ms.Dispose()
        return $bytes
    } catch { return $null }
}

function Get-PlaceholderBytes {
    $bmp = New-Object System.Drawing.Bitmap(640, 200)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(22, 24, 29))
    $font = New-Object System.Drawing.Font("Segoe UI", 14)
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(154, 160, 168))
    $g.DrawString("GreenRhythm is not running. Run iterate.ps1 to build + launch.", $font, $brush, 20, 80)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $bytes = $ms.ToArray(); $ms.Dispose()
    return $bytes
}

$html = @"
<!doctype html><html><head><meta charset="utf-8"><title>GreenRhythm preview</title>
<style>
  body { margin:0; background:#0f1114; color:#9aa0a8; font:13px 'Segoe UI',sans-serif;
         display:flex; flex-direction:column; align-items:center; }
  .bar { padding:8px 14px; width:100%; box-sizing:border-box; display:flex; gap:14px;
         align-items:center; background:#16181d; border-bottom:1px solid #2f343b; }
  .dot { width:8px; height:8px; border-radius:4px; background:#3fb950; }
  img  { max-width:98%; margin:12px auto; border:1px solid #2f343b; border-radius:8px; }
</style></head><body>
<div class="bar"><div class="dot"></div><b style="color:#e4e6eb">GreenRhythm — live window</b>
<span id="ts"></span><span style="margin-left:auto">auto-refresh 1.5s</span></div>
<img id="shot" src="/shot.png">
<script>
  setInterval(function(){
    document.getElementById('shot').src = '/shot.png?t=' + Date.now();
    document.getElementById('ts').textContent = new Date().toLocaleTimeString();
  }, 1500);
</script></body></html>
"@
$htmlBytes = [System.Text.Encoding]::UTF8.GetBytes($html)

$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()
Write-Output "preview server listening on http://127.0.0.1:$Port/"

while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
        # Browsers open idle speculative connections; without timeouts one of those
        # blocks this single-threaded loop forever.
        $client.ReceiveTimeout = 2000
        $client.SendTimeout = 5000
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000
        $reader = New-Object System.IO.StreamReader($stream)
        $reqLine = $reader.ReadLine()
        while ($true) { $l = $reader.ReadLine(); if ($null -eq $l -or $l -eq '') { break } }
        $path = '/'
        if ($reqLine -match '^\w+\s+(\S+)') { $path = $Matches[1] }

        if ($path -like '/shot.png*') {
            $body = Get-ShotBytes -AppDir $AppDir
            if ($null -eq $body) { $body = Get-PlaceholderBytes }
            $ctype = 'image/png'
        } elseif ($path -eq '/' -or $path -like '/index*') {
            $body = $htmlBytes
            $ctype = 'text/html; charset=utf-8'
        } else {
            $body = [System.Text.Encoding]::ASCII.GetBytes('not found')
            $ctype = 'text/plain'
        }

        $hdr = "HTTP/1.1 200 OK`r`nContent-Type: $ctype`r`nContent-Length: $($body.Length)`r`nCache-Control: no-store`r`nConnection: close`r`n`r`n"
        $hdrBytes = [System.Text.Encoding]::ASCII.GetBytes($hdr)
        $stream.Write($hdrBytes, 0, $hdrBytes.Length)
        $stream.Write($body, 0, $body.Length)
        $stream.Flush()
    } catch { }
    finally { $client.Close() }
}
