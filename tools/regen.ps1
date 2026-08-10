# Native Windows/PowerShell equivalent of tools/regen.sh.
# Regenerates ROM-derived C sources without requiring Bash/MSYS2.
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('usa', 'jp', 'all')]
    [string]$Variant = 'usa',

    [switch]$NoTests,
    [switch]$StrictIdempotent,

    [ValidateSet('native', 'python', 'auto')]
    [string]$AnalysisBackend = $(
        if ($env:SNESRECOMP_ANALYSIS_BACKEND) {
            $env:SNESRECOMP_ANALYSIS_BACKEND
        } else {
            'native'
        }
    ),

    [string]$SnesrecompRoot = $(
        if ($env:SNESRECOMP_ROOT) { $env:SNESRECOMP_ROOT } else { 'snesrecomp' }
    )
)

$ErrorActionPreference = 'Stop'
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Find-Python {
    foreach ($name in @('py', 'python', 'python3')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    throw 'No Python interpreter found on PATH (tried py, python, python3).'
}

$Python = Find-Python

function Invoke-Python {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & $Python @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code $LASTEXITCODE: $Python $($Arguments -join ' ')"
    }
}

function Step([string]$Text) {
    Write-Host ''
    Write-Host "=== $Text ==="
}

function Invoke-RegenVariant([string]$Name) {
    if ($Name -eq 'usa') {
        $rom = 'mmx.sfc'
        $cfgDir = 'recomp'
        $outDir = 'src/gen'
        $funcsH = 'recomp/funcs.h'
        $extra = @('--profile-manifest', 'recomp/tier2_coverage.json')
    } else {
        $rom = 'variants/jp/roms/rockmanx.sfc'
        $cfgDir = 'variants/jp/config'
        $outDir = 'variants/jp/gen'
        $funcsH = 'variants/jp/config/funcs.h'
        $extra = @('--no-host-root-scan', '--profile-manifest', 'variants/jp/tier2_coverage.json')
    }

    if (-not (Test-Path -LiteralPath $rom -PathType Leaf)) {
        throw "$rom not found. Stage the verified $Name ROM first."
    }

    Step "Regenerating $Name banks"
    $emitArgs = @(
        "$SnesrecompRoot/tools/v2_emit.py",
        '--rom', $rom,
        '--cfg-dir', $cfgDir,
        '--out-dir', $outDir,
        '--cfg-roots',
        '--analysis-backend', $AnalysisBackend
    ) + $extra
    Invoke-Python @emitArgs

    Step "Syncing $Name funcs.h"
    Invoke-Python "$SnesrecompRoot/tools/v2_sync_funcs_h.py" '--cfg-dir' $cfgDir '--out' $funcsH

    if ($StrictIdempotent) {
        Step "Checking $Name idempotency"
        $tmpGen = Join-Path ([System.IO.Path]::GetTempPath()) ("snesrecomp-gen-" + [Guid]::NewGuid().ToString('N'))
        try {
            $checkArgs = @(
                "$SnesrecompRoot/tools/v2_emit.py",
                '--rom', $rom,
                '--cfg-dir', $cfgDir,
                '--out-dir', $tmpGen,
                '--cfg-roots',
                '--analysis-backend', $AnalysisBackend
            ) + $extra
            Invoke-Python @checkArgs
            Invoke-Python "$SnesrecompRoot/tools/v2_compare_output.py" '--expected' $outDir '--actual' $tmpGen
        } finally {
            if (Test-Path -LiteralPath $tmpGen) {
                Remove-Item -LiteralPath $tmpGen -Recurse -Force
            }
        }
    }

    if ($Name -eq 'usa') {
        Step "Applying $Name widescreen overrides"
        Invoke-Python 'tools/apply_overrides.py' '--gen-dir' $outDir
    } else {
        Step "Keeping $Name generated output authentic 4:3"
    }
}

Push-Location $Root
try {
    if (-not (Test-Path -LiteralPath "$SnesrecompRoot/tools/v2_emit.py" -PathType Leaf)) {
        throw "snesrecomp is not initialized. Run: git submodule sync --recursive; git submodule update --init --recursive"
    }

    if ($AnalysisBackend -eq 'native') {
        Step 'Building native analyzer'
        Invoke-Python "$SnesrecompRoot/tools/build_native_analyzer.py"
    }

    if ($Variant -eq 'all') {
        Invoke-RegenVariant 'usa'
        Invoke-RegenVariant 'jp'
    } else {
        Invoke-RegenVariant $Variant
    }

    if (-not $NoTests) {
        Step 'Framework tests'
        Invoke-Python "$SnesrecompRoot/tests/run_tests.py"
    }

    Step 'Done'
} finally {
    Pop-Location
}
