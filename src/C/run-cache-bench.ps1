# run-cache-bench.ps1
# Sweep CSL_BLOCK_CAP across multiple values and collect cache benchmark results.
#
# How it works:
#   1. For each block size, recompiles test-cache-benchmark.c with -DCSL_BLOCK_CAP=X
#   2. Runs the benchmark in --csv-only mode
#   3. Collects CSV output lines and builds a summary table
#
# Usage:
#   .\run-cache-bench.ps1                    # default sizes
#   .\run-cache-bench.ps1 -Sizes 16,32,64   # custom sizes
#
# Output: summary table + CSV file (cache-bench-results.csv)

param(
    # Block sizes to test. Covers range from 1 cache line to L2-filling blocks.
    # 8 items = 96 bytes (1-2 cache lines), 2048 = 24KB (fills L1)
    [int[]]$Sizes = @(8, 16, 32, 64, 128, 256, 512, 1024, 2048),

    # Number of keys to insert for the benchmark
    [int]$N = 500000,

    # Output CSV file path
    [string]$CsvFile = "cache-bench-results.csv"
)

$srcDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $srcDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Cache Benchmark Sweep - Block Size Tuning" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# CSV header for output file
$csvHeader = "block_cap,n_items,n_blocks,seq_ns_op,rnd_ns_op,rnd_seq_ratio,iter_ns_op,mem_mb"
$csvHeader | Out-File -FilePath $CsvFile -Encoding utf8

# Collect results for summary table
$results = @()

foreach ($cap in $Sizes) {
    Write-Host "--- Building with CSL_BLOCK_CAP=$cap ---" -ForegroundColor Yellow

    # Compile with the specific block cap.
    # -DCSL_BLOCK_CAP overrides the #ifndef in cskiplist.h.
    # -O2 for realistic optimization (not -O0 debug, not -O3 auto-vectorize).
    # -lpsapi needed on Windows for GetProcessMemoryInfo.
    $exeName = "cache-bench-$cap.exe"
    $compileCmd = "gcc -O2 -DCSL_BLOCK_CAP=$cap -o $exeName cskiplist.c test-cache-benchmark.c -lpsapi 2>&1"
    $compileOutput = Invoke-Expression $compileCmd

    if ($LASTEXITCODE -ne 0) {
        Write-Host "  COMPILE FAILED for cap=$cap" -ForegroundColor Red
        Write-Host $compileOutput
        continue
    }

    Write-Host "  Running benchmark (N=$N)..." -ForegroundColor Gray

    # Run with --csv-only flag; parse CSV lines from output
    $output = & ".\$exeName" --csv-only 2>&1
    $csvLines = $output | Where-Object { $_ -match "^CSV," }

    # Find the line matching our target N (closest)
    $bestLine = $null
    foreach ($line in $csvLines) {
        $parts = $line -split ","
        if ($parts.Count -ge 8) {
            $lineN = [int]$parts[2]
            # Pick the line with N closest to our target
            if ($null -eq $bestLine -or [Math]::Abs($lineN - $N) -lt [Math]::Abs($bestN - $N)) {
                $bestLine = $line
                $bestN = $lineN
            }
        }
    }

    if ($bestLine) {
        # Parse: CSV,block_cap,n_items,n_blocks,seq_ns,rnd_ns,ratio,iter_ns,mem_mb
        $p = $bestLine -split ","
        $result = [PSCustomObject]@{
            BlockCap   = [int]$p[1]
            Items      = [int]$p[2]
            Blocks     = [int]$p[3]
            SeqNs      = [double]$p[4]
            RndNs      = [double]$p[5]
            Ratio      = [double]$p[6]
            IterNs     = [double]$p[7]
            MemMB      = [double]$p[8]
        }
        $results += $result

        # Append to CSV file (without the "CSV," prefix)
        $csvData = $bestLine.Substring(4)  # strip "CSV,"
        $csvData | Out-File -FilePath $CsvFile -Append -Encoding utf8

        Write-Host ("  cap={0,5}  seq={1,7:F1}ns  rnd={2,7:F1}ns  ratio={3,5:F2}x  blocks={4}" -f `
            $result.BlockCap, $result.SeqNs, $result.RndNs, $result.Ratio, $result.Blocks) -ForegroundColor Green
    } else {
        Write-Host "  No CSV output captured for cap=$cap" -ForegroundColor Red
        # Dump raw output for debugging
        $output | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    }

    # Clean up the per-size executable
    Remove-Item $exeName -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Summary Table (N=$N)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Print formatted summary table
Write-Host ("{0,8} {1,8} {2,8} {3,10} {4,10} {5,8} {6,10} {7,8}" -f `
    "BlockCap", "Blocks", "Items", "Seq(ns)", "Rnd(ns)", "Ratio", "Iter(ns)", "Mem(MB)")
Write-Host ("{0,8} {1,8} {2,8} {3,10} {4,10} {5,8} {6,10} {7,8}" -f `
    "--------", "------", "-----", "-------", "-------", "-----", "--------", "------")

foreach ($r in $results) {
    # Highlight the ratio column: green if good, red if bad
    $color = if ($r.Ratio -lt 1.5) { "Green" } elseif ($r.Ratio -lt 3.0) { "Yellow" } else { "Red" }
    Write-Host ("{0,8} {1,8} {2,8} {3,10:F1} {4,10:F1} {5,8:F2} {6,10:F1} {7,8:F2}" -f `
        $r.BlockCap, $r.Blocks, $r.Items, $r.SeqNs, $r.RndNs, $r.Ratio, $r.IterNs, $r.MemMB) -ForegroundColor $color
}

Write-Host ""

# Find optimal block size (lowest ratio with reasonable performance)
$optimal = $results | Sort-Object Ratio | Select-Object -First 1
if ($optimal) {
    Write-Host "Optimal block size: CSL_BLOCK_CAP = $($optimal.BlockCap)" -ForegroundColor Cyan
    Write-Host "  (lowest random/sequential ratio = $($optimal.Ratio.ToString('F2'))x)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Results saved to: $CsvFile" -ForegroundColor Gray
Write-Host "Run individual sizes with: gcc -O2 -DCSL_BLOCK_CAP=<N> -o bench.exe cskiplist.c test-cache-benchmark.c -lpsapi" -ForegroundColor Gray

Pop-Location
