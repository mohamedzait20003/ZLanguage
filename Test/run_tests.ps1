<#
.SYNOPSIS
    Z language test runner (PowerShell).

.DESCRIPTION
    Behavioural twin of Test/run_tests.sh, for Windows shells without bash.
    Both runners discover the same fixtures and must agree on every result.

    Suites:
      Test/parser/*.z   + .expected-ast    --dump-ast output must match exactly
      Test/codegen/*.z  + .expected        compile, run, compare stdout
      Test/sema/*.z     + .expected-error  must FAIL to compile, and the
                                           diagnostic must contain the text

    Exits non-zero if any test fails, so it works as a CI gate.

.PARAMETER Filter
    Substring; only tests whose name contains it are run.

.PARAMETER OptLevels
    Optimisation levels for the codegen suite. Every level must produce
    identical output — that equivalence is the correctness check on the pass
    pipeline. Defaults to all four.

.PARAMETER BuildDir
    Build directory to test, relative to the repo root. Defaults to "build".

.EXAMPLE
    .\Test\run_tests.ps1
.EXAMPLE
    .\Test\run_tests.ps1 -Filter switch -OptLevels '-O0','-O2'
#>

[CmdletBinding()]
param(
    [string]   $Filter    = '',
    [string[]] $OptLevels = @('-O0', '-O1', '-O2', '-O3'),
    [string]   $BuildDir  = 'build'
)

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$work  = Join-Path $build 'testwork'

$zc = Join-Path $build 'zc.exe'
if (-not (Test-Path $zc)) { $zc = Join-Path $build 'zc' }

if (-not (Test-Path $zc)) {
    Write-Output "error: compiler not found at $zc"
    Write-Output "build it first:  cmake --build $BuildDir"
    exit 1
}

$llvmBinFile = Join-Path $build 'llvm_bin_dir.txt'
if (Test-Path $llvmBinFile) {
    $llvmBin = (Get-Content $llvmBinFile -Raw).Trim()
    if ($llvmBin) { $env:PATH = "$llvmBin;$env:PATH" }
}

# Run a native command, capturing stdout+stderr and the exit code together.
# PowerShell 5.1 turns native stderr into ErrorRecords when redirected inside
# the pipeline, so the redirect is done by cmd and the output read back.
function Invoke-Captured {
    param([string] $Exe, [string[]] $Arguments)

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()

    try {
        # Start-Process rejects an empty -ArgumentList, so it has to be omitted
        $common = @{
            FilePath = $Exe
            NoNewWindow = $true
            Wait = $true
            PassThru = $true
            RedirectStandardOutput = $stdout
            RedirectStandardError = $stderr
        }

        if ($Arguments -and $Arguments.Count -gt 0) { $common['ArgumentList'] = $Arguments }

        $p = Start-Process @common

        $out = ''
        if (Test-Path $stdout) { $out += (Get-Content $stdout -Raw -ErrorAction SilentlyContinue) }
        if (Test-Path $stderr) { $out += (Get-Content $stderr -Raw -ErrorAction SilentlyContinue) }

        return [pscustomobject]@{ ExitCode = $p.ExitCode; Output = $out }
    }
    finally {
        Remove-Item $stdout, $stderr -Force -ErrorAction SilentlyContinue
    }
}

$probe = Invoke-Captured $zc @('--dump-tokens', (Join-Path $root 'Test/codegen/m0_hello.z'))
if ($probe.ExitCode -ne 0) {
    Write-Output "error: $zc cannot run (missing shared libraries?)"
    Write-Output "if zc links the shared libLLVM, LLVM's bin directory must be on PATH"
    exit 1
}

$script:pass = 0
$script:fail = 0
$script:skip = 0
$script:failures = New-Object System.Collections.Generic.List[string]

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work -Force | Out-Null

function Note-Pass { param([string] $Name)
    $script:pass++
    Write-Output ("  ok    {0}" -f $Name)
}

function Note-Fail { param([string] $Name, [string] $Detail)
    $script:fail++
    $script:failures.Add($Name) | Out-Null
    Write-Output ("  FAIL  {0}" -f $Name)
    foreach ($line in ($Detail -split "`r?`n")) { Write-Output ("          {0}" -f $line) }
}

# Compiled programs emit CRLF on Windows; .expected files are stored as LF.
# Normalise both so the suite behaves the same on either platform.
function Normalize { param([string] $Text)
    if ($null -eq $Text) { return '' }
    return ($Text -replace "`r", '').TrimEnd("`n")
}

function Show-Diff {
    param([string] $Want, [string] $Got)

    $w = $Want -split "`n"
    $g = $Got  -split "`n"
    $lines = New-Object System.Collections.Generic.List[string]

    for ($i = 0; $i -lt [Math]::Max($w.Count, $g.Count); $i++) {
        $lw = if ($i -lt $w.Count) { $w[$i] } else { '<missing>' }
        $lg = if ($i -lt $g.Count) { $g[$i] } else { '<missing>' }
        if ($lw -ne $lg) { $lines.Add(("line {0}: want '{1}', got '{2}'" -f ($i + 1), $lw, $lg)) | Out-Null }
        if ($lines.Count -ge 15) { break }
    }

    return ($lines -join "`n")
}

function Run-OutputTest {
    param([string] $Src, [string] $Expected, [string] $Name)

    $want = Normalize (Get-Content $Expected -Raw)

    foreach ($opt in $OptLevels) {
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($Src)
        $exe  = Join-Path $work ($stem + $opt + '.exe')

        $compile = Invoke-Captured $zc @($opt, $Src, '-o', $exe)
        if ($compile.ExitCode -ne 0) {
            Note-Fail "$Name $opt" ("compilation failed:`n" + $compile.Output)
            continue
        }

        $run = Invoke-Captured $exe @()
        $got = Normalize $run.Output

        if ($got -eq $want) { Note-Pass "$Name $opt" }
        else { Note-Fail "$Name $opt" ("output mismatch:`n" + (Show-Diff $want $got)) }
    }
}

function Run-ErrorTest {
    param([string] $Src, [string] $Expected, [string] $Name)

    $result = Invoke-Captured $zc @($Src, '-o', (Join-Path $work 'unexpected.exe'))

    if ($result.ExitCode -eq 0) {
        Note-Fail $Name 'expected compilation to fail, but it succeeded'
        return
    }

    $want = Normalize (Get-Content $Expected -Raw)
    $got  = Normalize $result.Output

    # Substring, not exact match: this pins which diagnostic fired without
    # making every wording tweak a test failure.
    if ($got.Contains($want)) { Note-Pass $Name }
    else { Note-Fail $Name ("wrong diagnostic`nexpected to contain: $want`nactual:              $got") }
}

function Run-AstTest {
    param([string] $Src, [string] $Expected, [string] $Name)

    $result = Invoke-Captured $zc @('--dump-ast', $Src)
    if ($result.ExitCode -ne 0) {
        Note-Fail $Name ("--dump-ast failed:`n" + $result.Output)
        return
    }

    $want = Normalize (Get-Content $Expected -Raw)
    $got  = Normalize $result.Output

    if ($got -eq $want) { Note-Pass $Name }
    else { Note-Fail $Name ("AST mismatch:`n" + (Show-Diff $want $got)) }
}

function Invoke-Suite {
    param([string] $Label, [string] $Dir, [string] $Ext, [string] $Runner)

    Write-Output ''
    Write-Output $Label

    $path = Join-Path $root $Dir
    $found = $false

    if (Test-Path $path) {
        foreach ($src in (Get-ChildItem -Path $path -Filter '*.z' | Sort-Object Name)) {
            $name = $src.BaseName
            if ($Filter -and ($name -notlike "*$Filter*")) { continue }

            $found = $true
            $expected = Join-Path $src.DirectoryName ($name + '.' + $Ext)

            if (-not (Test-Path $expected)) {
                $script:skip++
                Write-Output ("  skip  {0} (no {1})" -f $name, (Split-Path $expected -Leaf))
                continue
            }

            & $Runner $src.FullName $expected $name
        }
    }

    if (-not $found) { Write-Output '  (none)' }
}

Write-Output ("compiler:    {0}" -f $zc)
Write-Output ("opt levels:  {0}" -f ($OptLevels -join ' '))

Invoke-Suite 'parser (--dump-ast)'              'Test/parser'  'expected-ast'   'Run-AstTest'
Invoke-Suite 'codegen (per optimisation level)' 'Test/codegen' 'expected'       'Run-OutputTest'
Invoke-Suite 'sema (must fail)'                 'Test/sema'    'expected-error' 'Run-ErrorTest'

Write-Output ''
Write-Output '----------------------------------------'
$summary = "{0} passed, {1} failed" -f $script:pass, $script:fail
if ($script:skip -gt 0) { $summary += ", {0} skipped" -f $script:skip }
Write-Output $summary

if ($script:fail -gt 0) {
    Write-Output ''
    Write-Output 'failed:'
    foreach ($f in $script:failures) { Write-Output ("  {0}" -f $f) }
    exit 1
}

exit 0
