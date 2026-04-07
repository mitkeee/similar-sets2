# run-simd-bench.ps1
# Compare SIMD-accelerated vs scalar intra-block search.
#
# Compiles cskiplist.c + test-simd-benchmark.c twice:
#   1) With SIMD enabled  (default, SSE2 auto-detected)
#   2) With SIMD disabled (-DCSL_USE_SIMD=0)
#
# Then runs both and prints a side-by-side comparison table.
#
# Usage:
#   .\run-simd-bench.ps1                       # default cap=128
#   .\run-simd-bench.ps1 -BlockCap 256         # custom block size
#   .\run-simd-bench.ps1 -SimdThreshold 64     # custom SIMD scan threshold

param(
    [int]$BlockCap = 128,
    [int]$SimdThreshold = 32
)

$srcDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $srcDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  SIMD vs Scalar Search Benchmark"           -ForegroundColor Cyan
Write-Host "  Block Cap: $BlockCap   SIMD Threshold: $SimdThreshold" -ForegroundColor Cyan  
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Common compile flags: -O3 enables auto-vectorization, -msse2 ensures SSE2
$commonFlags = "-O3 -msse2 -DCSL_BLOCK_CAP=$BlockCap -DCSL_SIMD_SCAN_THRESHOLD=$SimdThreshold"

# --- Build SIMD version ---
Write-Host "Building SIMD version..." -ForegroundColor Yellow
$cmd = "gcc $commonFlags -o simd-bench.exe cskiplist.c test-simd-benchmark.c -lpsapi 2>&1"
$out = Invoke-Expression $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "  COMPILE FAILED (SIMD)" -ForegroundColor Red
    Write-Host $out
    Pop-Location; exit 1
}
Write-Host "  OK" -ForegroundColor Green

# --- Build scalar version (SIMD explicitly disabled) ---
Write-Host "Building scalar version..." -ForegroundColor Yellow
$cmd = "gcc $commonFlags -DCSL_USE_SIMD=0 -o scalar-bench.exe cskiplist.c test-simd-benchmark.c -lpsapi 2>&1"
$out = Invoke-Expression $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "  COMPILE FAILED (scalar)" -ForegroundColor Red
    Write-Host $out
    Pop-Location; exit 1
}
Write-Host "  OK" -ForegroundColor Green
Write-Host ""

# --- Run SIMD benchmark ---
Write-Host "Running SIMD benchmark..." -ForegroundColor Yellow
$simdOutput = & ".\simd-bench.exe" --csv-only 2>&1
$simdLines = @($simdOutput | Where-Object { $_ -match "^CSV,SIMD," })

# --- Run scalar benchmark ---
Write-Host "Running scalar benchmark..." -ForegroundColor Yellow
$scalarOutput = & ".\scalar-bench.exe" --csv-only 2>&1
$scalarLines = @($scalarOutput | Where-Object { $_ -match "^CSV,SCALAR," })

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Results (Block Cap = $BlockCap)"           -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Parse CSV lines into objects
# Format: CSV,SIMD|SCALAR,block_cap,N,nblocks,seq_ns,rnd_ns,seq_cyc,rnd_cyc
function Parse-CsvLine($line) {
    $p = $line -split ","
    [PSCustomObject]@{
        Mode    = $p[1]
        Cap     = [int]$p[2]
        N       = [int]$p[3]
        Blocks  = [int]$p[4]
        SeqNs   = [double]$p[5]
        RndNs   = [double]$p[6]
        SeqCyc  = [double]$p[7]
        RndCyc  = [double]$p[8]
    }
}

$simdResults = @($simdLines | ForEach-Object { Parse-CsvLine $_ })
$scalarResults = @($scalarLines | ForEach-Object { Parse-CsvLine $_ })

# Print comparison table
Write-Host ("{0,8} {1,12} {2,12} {3,12} {4,12} {5,10} {6,10}" -f `
    "N", "Scalar(ns)", "SIMD(ns)", "Speedup", "ScalarRnd", "SIMDRnd", "RndSpdup")
Write-Host ("{0,8} {1,12} {2,12} {3,12} {4,12} {5,10} {6,10}" -f `
    "------", "----------", "--------", "-------", "---------", "-------", "-------")

for ($i = 0; $i -lt $simdResults.Count -and $i -lt $scalarResults.Count; $i++) {
    $sc = $scalarResults[$i]
    $sm = $simdResults[$i]

    $seqSpeedup = if ($sm.SeqNs -gt 0) { $sc.SeqNs / $sm.SeqNs } else { 0 }
    $rndSpeedup = if ($sm.RndNs -gt 0) { $sc.RndNs / $sm.RndNs } else { 0 }

    $color = if ($seqSpeedup -gt 1.1) { "Green" } elseif ($seqSpeedup -gt 0.95) { "Yellow" } else { "Red" }

    Write-Host ("{0,8} {1,12:F1} {2,12:F1} {3,12:F2}x {4,12:F1} {5,10:F1} {6,10:F2}x" -f `
        $sc.N, $sc.SeqNs, $sm.SeqNs, $seqSpeedup, `
        $sc.RndNs, $sm.RndNs, $rndSpeedup) -ForegroundColor $color
}

Write-Host ""

# Summary
$avgSeqSpeedup = 0; $avgRndSpeedup = 0; $cnt = 0
for ($i = 0; $i -lt $simdResults.Count -and $i -lt $scalarResults.Count; $i++) {
    $sc = $scalarResults[$i]; $sm = $simdResults[$i]
    if ($sm.SeqNs -gt 0) { $avgSeqSpeedup += $sc.SeqNs / $sm.SeqNs }
    if ($sm.RndNs -gt 0) { $avgRndSpeedup += $sc.RndNs / $sm.RndNs }
    $cnt++
}
if ($cnt -gt 0) {
    $avgSeqSpeedup /= $cnt; $avgRndSpeedup /= $cnt
    $color = if ($avgSeqSpeedup -gt 1.1) { "Green" } elseif ($avgSeqSpeedup -gt 0.95) { "Yellow" } else { "Red" }
    Write-Host ("Average sequential speedup: {0:F2}x" -f $avgSeqSpeedup) -ForegroundColor $color
    $color = if ($avgRndSpeedup -gt 1.1) { "Green" } elseif ($avgRndSpeedup -gt 0.95) { "Yellow" } else { "Red" }
    Write-Host ("Average random speedup:     {0:F2}x" -f $avgRndSpeedup) -ForegroundColor $color
}

Write-Host ""

# Cleanup executables
Remove-Item "simd-bench.exe", "scalar-bench.exe" -ErrorAction SilentlyContinue

Pop-Location
