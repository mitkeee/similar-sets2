# run-experiments.ps1
# Full experiment matrix for the thesis. Compiles the unified experiment
# driver once, then sweeps data-set size, block capacity, structure and
# workload. Every run writes its own CSV (parameters encoded in the file
# name) into results\; at the end all CSVs are merged into
# results\all-results.csv for analysis (pandas/Excel).
#
# Usage:
#   .\run-experiments.ps1                 # full matrix (~a few minutes)
#   .\run-experiments.ps1 -Quick         # reduced matrix for a fast look
#
# Experiments produced (per supervisor's specification):
#   1. Structure comparison + block-size sweep at several data-set sizes
#      that straddle L1/L2/L3/DRAM (search: 50% hits / 50% misses).
#   2. Tiny-set regime (set-trie leaf nodes: a handful of keys).
#   3. Random-order insert benchmark (incremental skip maintenance).

param(
    [switch]$Quick,
    [string]$OutDir = "results",
    [int]$Reps = 3,
    [int]$Queries = 500000,
    [int]$Seed = 42
)

$srcDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $srcDir

Write-Host "=== Building experiment driver (gcc -O3 -msse2) ===" -ForegroundColor Cyan
gcc -O3 -msse2 -o experiment.exe cskiplist.c skiplist.c test-experiment.c -lpsapi
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; Pop-Location; exit 1 }

New-Item -ItemType Directory -Force $OutDir | Out-Null

# --- Experiment 1: search performance across sizes and block caps ---
# Sizes chosen to straddle cache levels (8 B/key payload):
#   10K = 80 KB (L2), 100K = 0.8 MB (L2/L3), 1M = 8 MB (L3), 4M = 32 MB (DRAM)
$sizes = @(10000, 100000, 1000000, 4000000)
$caps  = "8,16,32,64,128,256,512,1024,2048,4096"
if ($Quick) { $sizes = @(100000, 1000000); $caps = "32,128,512,2048" }

foreach ($n in $sizes) {
    Write-Host "--- search sweep: n=$n ---" -ForegroundColor Yellow
    & .\experiment.exe -m search -n $n -q $Queries -b $caps -r $Reps -s $Seed -H 50 -o $OutDir
    if ($LASTEXITCODE -ne 0) { Write-Host "VERIFICATION FAILED at n=$n" -ForegroundColor Red }
}

# --- Experiment 2: tiny-set regime (set-trie leaf nodes) ---
if (-not $Quick) {
    foreach ($n in @(8, 32, 256)) {
        Write-Host "--- tiny-set: n=$n ---" -ForegroundColor Yellow
        & .\experiment.exe -m search -n $n -q $Queries -b "4,8,16,32,64" -r $Reps -s $Seed -H 50 -o $OutDir
    }
}

# --- Experiment 3: random-order inserts (incremental skip maintenance) ---
$insN = 200000
if ($Quick) { $insN = 50000 }
Write-Host "--- insert benchmark: n=$insN ---" -ForegroundColor Yellow
& .\experiment.exe -m insert -n $insN -q $Queries -b "32,128,512,2048" -r $Reps -s $Seed -H 50 -o $OutDir

# --- Merge all CSVs (single header) ---
$merged = Join-Path $OutDir "all-results.csv"
$first = $true
Get-ChildItem $OutDir -Filter "exp_*.csv" | Sort-Object Name | ForEach-Object {
    $lines = Get-Content $_.FullName
    if ($first) { $lines | Out-File $merged -Encoding utf8; $first = $false }
    else        { $lines | Select-Object -Skip 1 | Out-File $merged -Append -Encoding utf8 }
}

Write-Host ""
Write-Host "=== Done. Individual CSVs and merged all-results.csv are in $OutDir\ ===" -ForegroundColor Cyan
Pop-Location
