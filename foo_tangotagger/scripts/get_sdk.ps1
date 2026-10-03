<#
.SYNOPSIS
    Downloads and unpacks the build prerequisites into external\.

.DESCRIPTION
    foo_tangotagger builds against three things that are not in the repository:

        external\foobar2000_sdk\   foobar2000 SDK 2025-03-07
        external\wtl\              WTL 10.01
        external\lzma_sdk\         LZMA SDK 26.03 (7-Zip's LZMA codec, public domain)

    The LZMA SDK supplies the codec the embedded lyrics are compressed with:
    LzmaEnc for the build-time packer, LzmaDec for the component.

    Both are fetched from their upstream homes, checked against a pinned
    SHA256 and unpacked here. CMake runs this script by itself when either is
    missing, so you normally never need to run it by hand.

    WTL is a separate download because the SDK's helpers include <atlapp.h> and
    <atlctrls.h> but do not ship them. ATL itself comes with Visual Studio.

    Nothing but Windows is needed: tar.exe (bsdtar, in Windows 10 1803 and
    later) reads both the SDK's .7z and WTL's .zip, so no 7-Zip install is
    involved. CMake's own downloader is deliberately not used - its TLS stack
    cannot complete a handshake with SourceForge on some corporate networks,
    where PowerShell, going through Windows' certificate store, can.

.PARAMETER Destination
    Where to unpack. Default: external\ next to this repository.

.PARAMETER Force
    Re-download and re-unpack even if everything is already there.

.PARAMETER Only
    Fetch only these dependencies, by the folder they unpack into - lzma_sdk
    is all deadbeef_tangotagger needs.

.EXAMPLE
    .\scripts\get_sdk.ps1

.EXAMPLE
    .\scripts\get_sdk.ps1 -Force

.EXAMPLE
    .\scripts\get_sdk.ps1 -Only lzma_sdk
#>

[CmdletBinding()]
param(
    [string] $Destination = '',
    [switch] $Force,
    [string[]] $Only = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $root 'external' }

# Old PowerShell defaults to TLS 1.0, which neither host accepts any more.
[Net.ServicePointManager]::SecurityProtocol =
    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$dependencies = @(
    [pscustomobject] @{
        Name    = 'foobar2000 SDK'
        Version = '2025-03-07'
        Dir     = 'foobar2000_sdk'
        Archive = 'SDK-2025-03-07.7z'
        Url     = 'https://www.foobar2000.org/downloads/SDK-2025-03-07.7z'
        Sha256  = 'CCDA3C5840E66E0E28A7E4FE36407C4E78581AA30C40C362A188FCBAAE799A3E'
        # Unpacks with no wrapper folder: foobar2000\, pfc\ and libPPUI\ at the root.
        Expect  = @('foobar2000\SDK\foobar2000.h',
                    'foobar2000\shared\shared-Win32.lib',
                    'foobar2000\shared\shared-x64.lib',
                    'pfc\pfc.h',
                    'libPPUI\listview_helper.h')
    },
    [pscustomobject] @{
        Name    = 'WTL'
        Version = '10.01'
        Dir     = 'wtl'
        Archive = 'WTL10_01_Release.zip'
        Url     = 'https://downloads.sourceforge.net/project/wtl/WTL%2010/WTL%2010.01%20Release/WTL10_01_Release.zip'
        Sha256  = '1A62EA728D088C7C5C7CFC76DB445E5C9F04923F92CADCF27B5D7678D85826A2'
        Expect  = @('Include\atlapp.h', 'Include\atlctrls.h')
    },
    [pscustomobject] @{
        Name    = 'LZMA SDK'
        Version = '26.03'
        Dir     = 'lzma_sdk'
        Archive = 'lzma2603.7z'
        Url     = 'https://github.com/ip7z/7zip/releases/download/26.03/lzma2603.7z'
        Sha256  = '86C213F752520AB5325C310F50BEF63EC344B56DD1C80B0246D06DC6CEC953B2'
        Expect  = @('C\LzmaDec.c', 'C\LzmaEnc.c', 'C\LzFind.c')
    }
)

function Get-Sha256([string] $path) {
    (Get-FileHash -Path $path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Expand-Any([string] $archive, [string] $into) {
    # bsdtar reads .7z and .zip alike; Expand-Archive only manages .zip, so it
    # is the fallback rather than the first choice.
    # Windows' own tar.exe first: when this runs from Git Bash, the GNU tar on
    # its PATH comes earlier and cannot read .7z.
    $tarPath = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (-not (Test-Path $tarPath)) {
        $found = Get-Command tar.exe -ErrorAction SilentlyContinue
        $tarPath = if ($found) { $found.Source } else { $null }
    }
    if ($tarPath) {
        & $tarPath -xf $archive -C $into
        if ($LASTEXITCODE -eq 0) { return }
        Write-Host "  tar could not read it, falling back to Expand-Archive" -ForegroundColor Yellow
    }
    if ([System.IO.Path]::GetExtension($archive) -ne '.zip') {
        throw "tar.exe is needed to unpack $archive and is not available. Install Windows 10 1803 or later, or unpack it by hand."
    }
    Expand-Archive -Path $archive -DestinationPath $into -Force
}

New-Item -ItemType Directory -Force $Destination | Out-Null

foreach ($dep in $dependencies) {
    if ($Only.Count -gt 0 -and $Only -notcontains $dep.Dir) { continue }
    $dir     = Join-Path $Destination $dep.Dir
    $archive = Join-Path $Destination $dep.Archive
    $stamp   = Join-Path $dir (".{0}-{1}.stamp" -f $dep.Dir, $dep.Version)

    if ((Test-Path $stamp) -and -not $Force) {
        Write-Host ("{0} {1} already unpacked in {2}" -f $dep.Name, $dep.Version, $dir) -ForegroundColor DarkGray
        continue
    }

    # --- fetch --------------------------------------------------------------
    $haveArchive = $false
    if (Test-Path $archive) {
        if ((Get-Sha256 $archive) -eq $dep.Sha256) {
            $haveArchive = $true
            Write-Host "Reusing $archive" -ForegroundColor DarkGray
        }
        else {
            Write-Host "Discarding $archive (checksum mismatch)" -ForegroundColor Yellow
            Remove-Item -Force $archive
        }
    }

    if (-not $haveArchive) {
        Write-Host ("Downloading {0}" -f $dep.Url) -ForegroundColor Cyan
        $progress = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'    # an order of magnitude faster
        try {
            # PowerShell's default User-Agent looks like a browser, and SourceForge
            # answers those with its "your download will start shortly" HTML page
            # rather than the file. Ask as a plain downloader instead.
            Invoke-WebRequest -Uri $dep.Url -OutFile $archive -UseBasicParsing -UserAgent 'curl/8.4.0'
        }
        catch {
            if (Test-Path $archive) { Remove-Item -Force $archive }
            throw ("Failed to download {0}: {1}`nFetch {2} by hand, drop it in {3}, and re-run." -f
                   $dep.Name, $_.Exception.Message, $dep.Url, $Destination)
        }
        finally { $ProgressPreference = $progress }

        $got = Get-Sha256 $archive
        if ($got -ne $dep.Sha256) {
            Remove-Item -Force $archive
            throw ("{0}: checksum mismatch.`n  expected {1}`n  got      {2}`nThe download was corrupted, or upstream changed the file." -f
                   $dep.Name, $dep.Sha256, $got)
        }
    }

    # --- unpack -------------------------------------------------------------
    # Into a scratch directory first, so a half-finished unpack can never be
    # mistaken for a usable dependency.
    $tmp = Join-Path $Destination (".unpack-" + $dep.Dir)
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null

    Write-Host ("Unpacking into {0}" -f $dir) -ForegroundColor Cyan
    try {
        Expand-Any -archive $archive -into $tmp
    }
    catch {
        Remove-Item -Recurse -Force $tmp
        throw ("Could not unpack {0}: {1}" -f $archive, $_.Exception.Message)
    }

    foreach ($need in $dep.Expect) {
        if (-not (Test-Path (Join-Path $tmp $need))) {
            Remove-Item -Recurse -Force $tmp
            throw ("Unexpected archive layout in {0}: {1} is missing" -f $dep.Archive, $need)
        }
    }

    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Move-Item $tmp $dir
    Set-Content -Path $stamp -Value @($dep.Version, $dep.Url) -Encoding utf8

    Write-Host ("{0} {1} ready in {2}" -f $dep.Name, $dep.Version, $dir) -ForegroundColor Green
}
