# SignUltraTexter.ps1
# Self-signed code signing certificate generator and EXE signer for UltraTexter
# Run once as Administrator to create the cert, then use sign mode for each build
# Usage:
#   First time:  powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode CreateAndSign -ExePath "path\to\UltraTexter.exe"
#   Subsequent:  powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode Sign       -ExePath "path\to\UltraTexter.exe"
#   Verify only: powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode Verify     -ExePath "path\to\UltraTexter.exe"

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("CreateAndSign", "Sign", "Verify")]
    [string]$Mode,

    [Parameter(Mandatory=$true)]
    [string]$ExePath,

    [string]$CertSubject    = "CN=Cloverleaf UG, O=Cloverleaf UG, L=Unknown, C=DE",
    [string]$CertFriendly   = "UltraCanvasDemo Code Signing",
    [string]$CertStore      = "Cert:\CurrentUser\My",
    [string]$PfxPath        = "$PSScriptRoot\UltraDemoSign.pfx",
    [string]$PfxPassword    = "UltraCanvas2026!",
    [int]$CertYears         = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ===== HELPERS =====

function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host ">>> $msg" -ForegroundColor Cyan
}

function Write-Ok([string]$msg) {
    Write-Host "    [OK] $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
    Write-Host "    [!!] $msg" -ForegroundColor Yellow
}

function Find-SignTool {
    # Search common SDK locations newest-first
    $searchRoots = @(
        "C:\Program Files (x86)\Windows Kits\10\bin",
        "C:\Program Files\Windows Kits\10\bin",
        "C:\msys64\home\User\SignTool"
    )
    foreach ($root in $searchRoots) {
        if (Test-Path $root) {
            $found = Get-ChildItem -Path $root -Filter "signtool.exe" -Recurse -ErrorAction SilentlyContinue |
                     Where-Object { $_.FullName -match "x64" } |
                     Sort-Object FullName -Descending |
                     Select-Object -First 1
            if ($found) { return $found.FullName }
        }
    }
    # Fallback: PATH
    $onPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

function Get-ExistingCert {
    return Get-ChildItem -Path $CertStore -ErrorAction SilentlyContinue |
           Where-Object { $_.Subject -eq $CertSubject -and $_.NotAfter -gt (Get-Date) } |
           Sort-Object NotAfter -Descending |
           Select-Object -First 1
}

# ===== MODE: CREATE CERTIFICATE =====

function New-SelfSignedCodeCert {
    Write-Step "Creating self-signed code signing certificate"

    $existing = Get-ExistingCert
    if ($existing) {
        Write-Warn "Valid cert already exists (expires $($existing.NotAfter.ToString('yyyy-MM-dd')))"
        Write-Warn "Thumbprint: $($existing.Thumbprint)"
        Write-Warn "Skipping creation. Delete it manually to recreate."
        return $existing
    }

    $cert = New-SelfSignedCertificate `
        -Subject         $CertSubject `
        -FriendlyName    $CertFriendly `
        -Type            CodeSigningCert `
        -KeyUsage        DigitalSignature `
        -KeyAlgorithm    RSA `
        -KeyLength       4096 `
        -HashAlgorithm   SHA256 `
        -NotAfter        (Get-Date).AddYears($CertYears) `
        -CertStoreLocation $CertStore

    Write-Ok "Certificate created"
    Write-Ok "Thumbprint : $($cert.Thumbprint)"
    Write-Ok "Expires    : $($cert.NotAfter.ToString('yyyy-MM-dd'))"

    # Export PFX for use in post-build signing without admin rights
    $secPwd = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $PfxPath -Password $secPwd | Out-Null
    Write-Ok "PFX exported to: $PfxPath"

    # Trust the cert on this machine (adds to Trusted Publishers + Root)
    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store("Root", "LocalMachine")
    $rootStore.Open("ReadWrite")
    $rootStore.Add($cert)
    $rootStore.Close()
    Write-Ok "Added to Trusted Root CA (LocalMachine)"

    $pubStore = New-Object System.Security.Cryptography.X509Certificates.X509Store("TrustedPublisher", "LocalMachine")
    $pubStore.Open("ReadWrite")
    $pubStore.Add($cert)
    $pubStore.Close()
    Write-Ok "Added to Trusted Publishers (LocalMachine)"

    return $cert
}

# ===== MODE: SIGN EXE =====

function Invoke-SignExe([string]$path) {
    Write-Step "Signing: $path"

    if (-not (Test-Path $path)) {
        throw "EXE not found: $path"
    }

    $signtool = Find-SignTool
    if (-not $signtool) {
        throw "signtool.exe not found. Install Windows SDK or add it to PATH."
    }
    Write-Ok "Using signtool: $signtool"

    # Prefer PFX file (works without admin, good for CI/post-build)
    if (Test-Path $PfxPath) {
        Write-Ok "Signing with PFX file"
        & $signtool sign `
            /fd   SHA256 `
            /f    $PfxPath `
            /p    $PfxPassword `
            /d    "UltraTexter" `
            /du   "https://ultracanvas.dev" `
            /t    "http://timestamp.digicert.com" `
            $path

    } else {
        # Fall back to cert store lookup
        $cert = Get-ExistingCert
        if (-not $cert) {
            throw "No valid cert in store and no PFX found. Run -Mode CreateAndSign first."
        }
        Write-Ok "Signing with cert store (thumbprint $($cert.Thumbprint))"
        & $signtool sign `
            /fd  SHA256 `
            /sha1 $cert.Thumbprint `
            /d   "UltraTexter" `
            /du  "https://ultracanvas.dev" `
            /t   "http://timestamp.digicert.com" `
            $path
    }

    if ($LASTEXITCODE -ne 0) {
        # Timestamp server may be unreachable — retry without timestamp
        Write-Warn "Timestamp server unreachable, signing without timestamp..."
        if (Test-Path $PfxPath) {
            & $signtool sign /fd SHA256 /f $PfxPath /p $PfxPassword /d "UltraTexter" $path
        } else {
            $cert = Get-ExistingCert
            & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /d "UltraTexter" $path
        }
        if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
        Write-Warn "Signed without timestamp (signature will expire with the cert)"
    }

    Write-Ok "Signed successfully"
}

# ===== MODE: VERIFY =====

function Invoke-Verify([string]$path) {
    Write-Step "Verifying signature: $path"

    $signtool = Find-SignTool
    if (-not $signtool) { throw "signtool.exe not found." }

    & $signtool verify /pa $path
    if ($LASTEXITCODE -eq 0) {
        Write-Ok "Signature is valid"
    } else {
        Write-Warn "Signature verification failed or no signature present"
    }
}

# ===== ENTRY POINT =====

Write-Host ""
Write-Host "==========================================" -ForegroundColor Magenta
Write-Host "  UltraTexter Code Signing Tool" -ForegroundColor Magenta
Write-Host "  Mode: $Mode" -ForegroundColor Magenta
Write-Host "==========================================" -ForegroundColor Magenta

switch ($Mode) {
    "CreateAndSign" {
        New-SelfSignedCodeCert | Out-Null
        Invoke-SignExe $ExePath
        Invoke-Verify  $ExePath
    }
    "Sign" {
        Invoke-SignExe $ExePath
        Invoke-Verify  $ExePath
    }
    "Verify" {
        Invoke-Verify $ExePath
    }
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
