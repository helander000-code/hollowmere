param([string]$name, [string]$a)
Add-Type -AssemblyName System.Drawing
$exe = "C:\Users\helan\hollowmere\hollowmere.exe"
$bmp = "C:\Users\helan\hollowmere\shots\$name.bmp"
Remove-Item $bmp -ErrorAction SilentlyContinue
$argline = "--shot `"$bmp`" " + $a
$p = Start-Process $exe -ArgumentList $argline -PassThru
if (-not $p.WaitForExit(40000)) { $p.Kill(); "TIMEOUT" }
if (Test-Path $bmp) { $img = [System.Drawing.Image]::FromFile($bmp); $img.Save("C:\Users\helan\hollowmere\shots\$name.png", [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose(); Remove-Item $bmp; "ok $name  args: $argline" } else { "no output; exit=" + $p.ExitCode }
