# Runs Ripes in CLI mode on one program and reports the peak working set of
# the Ripes process, sampled every 50 ms. Windows has no /usr/bin/time -l;
# PeakWorkingSet64 is the kernel's own high-water mark, so sampling only has
# to catch the process before it exits.
#   powershell -File measure/peak.ps1 -Ripes C:\path\Ripes.exe -Src mem_4096.s
param(
    [string]$Ripes,
    [string]$Src,
    [string]$Proc = "RV32_ISS"
)
$p = Start-Process -FilePath $Ripes -PassThru -NoNewWindow `
    -RedirectStandardOutput "$env:TEMP\peak_out.txt" `
    -ArgumentList "--mode", "cli", "--src", $Src, "-t", "asm", "--proc", $Proc, "--iret", "--exectime"
$peak = 0
while (-not $p.HasExited) {
    try {
        $p.Refresh()
        if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 }
    } catch {}
    Start-Sleep -Milliseconds 50
}
$out = Get-Content "$env:TEMP\peak_out.txt" -Raw
$iret = if ($out -match "instructions retired\s+(\d+)") { $Matches[1] } else { "?" }
$ms = if ($out -match "execution time \(ms\)\s+(\d+)") { $Matches[1] } else { "?" }
"{0}`t{1}`tpeak_bytes={2}`tiret={3}`tms={4}" -f $Src, $Proc, $peak, $iret, $ms
