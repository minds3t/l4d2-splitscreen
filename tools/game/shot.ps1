param([string]$Name)
# Screenshot of the running game, as %TEMP%\<Name>_full.jpg and a 1280x720 <Name>_small.jpg.
$g=Join-Path (Split-Path -Parent (Get-Process -Name left4dead2 -ErrorAction Stop | Select-Object -First 1).Path) 'left4dead2'
python -I (Join-Path $PSScriptRoot 'netcon.py') "jpeg" --wait 2 | Out-Null; Start-Sleep 1
$s=Get-ChildItem "$g\screenshots" | Sort-Object LastWriteTime | Select-Object -Last 1; Copy-Item $s.FullName "$env:TEMP\${Name}_full.jpg" -Force
Add-Type -AssemblyName System.Drawing; $i=[Drawing.Image]::FromFile("$env:TEMP\${Name}_full.jpg"); $b=New-Object Drawing.Bitmap 1280,720; $gfx=[Drawing.Graphics]::FromImage($b); $gfx.DrawImage($i,0,0,1280,720); $b.Save("$env:TEMP\${Name}_small.jpg",[Drawing.Imaging.ImageFormat]::Jpeg); $gfx.Dispose(); $i.Dispose(); $b.Dispose()
