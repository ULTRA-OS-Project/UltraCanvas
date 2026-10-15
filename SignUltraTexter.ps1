# SignUltraTexter.ps1
# Self-signed code signing certificate generator and EXE signer for UltraTexter
# Run once as Administrator to create the cert, then use sign mode for each build
# Usage:
#   First time:  powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode CreateAndSign -ExePath "path\to\app.exe"
#   Subsequent:  powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode Sign       -ExePath "path\to\app.exe"
#   Verify only: powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode Verify     -ExePath "path\to\app.exe"
#   New password: powershell -ExecutionPolicy Bypass -File SignUltraTexter.ps1 -Mode RotatePassword
#
# The PFX password is not kept in this file: set ULTRACANVAS_SIGN_PFX_PASSWORD
# in the environment (a CI secret on a build machine), or pass -PfxPassword,
# before CreateAndSign or Sign. Anything written here is readable by everyone
# who can read the repository, and with the PFX it unlocks the private key.
#
# -Mode RotatePassword puts the PFX under a new password: set the new one in
# ULTRACANVAS_SIGN_PFX_PASSWORD, and - when the certificate is not in this
# account's store - the old one in ULTRACANVAS_SIGN_OLD_PFX_PASSWORD (or
# -OldPfxPassword), so the PFX can be read. The same certificate and key are
# exported again under the new password, the new file is opened once to prove
# it, and it replaces the old one. The password this file used to contain,
# which is in the repository's history, is refused. Copies of the old PFX
# elsewhere (backups, other machines) still open with the old password:
# delete them. -Description is the name the signature shows (what Windows names
# in its prompts), "UltraTexter" unless given.

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("CreateAndSign", "Sign", "Verify", "RotatePassword")]
    [string]$Mode,

    # Every mode but RotatePassword signs or checks this file.
    [string]$ExePath,

    [string]$CertSubject    = "CN=ULTRA OS Development GmbH, O=ULTRA OS Development GmbH, L=Unknown, C=DE",
    [string]$CertFriendly   = "UltraCanvasTexter Code Signing",
    [string]$CertStore      = "Cert:\CurrentUser\My",
    [string]$PfxPath        = "$PSScriptRoot\UltraCanvasTexterSign.pfx",
    [string]$PfxPassword    = $env:ULTRACANVAS_SIGN_PFX_PASSWORD,
    [string]$OldPfxPassword = $env:ULTRACANVAS_SIGN_OLD_PFX_PASSWORD,
    [string]$Description    = "UltraTexter",
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

# The PFX password, from -PfxPassword or ULTRACANVAS_SIGN_PFX_PASSWORD - never
# a default in this file.
function Get-PfxPassword {
    if ([string]::IsNullOrEmpty($PfxPassword)) {
        throw "No PFX password: set ULTRACANVAS_SIGN_PFX_PASSWORD (or pass -PfxPassword)."
    }
    return $PfxPassword
}

function Export-Pfx($cert) {
    $secPwd = ConvertTo-SecureString -String (Get-PfxPassword) -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $PfxPath -Password $secPwd | Out-Null
    Write-Ok "PFX exported to: $PfxPath"
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
    # Before anything is created: a certificate without its PFX is no use.
    Get-PfxPassword | Out-Null

    $existing = Get-ExistingCert
    if ($existing) {
        Write-Warn "Valid cert already exists (expires $($existing.NotAfter.ToString('yyyy-MM-dd')))"
        Write-Warn "Thumbprint: $($existing.Thumbprint)"
        if (-not (Test-Path $PfxPath)) {
            # A new password: the same certificate, exported again under it.
            Export-Pfx $existing
        } else {
            Write-Warn "Skipping creation. Delete it manually to recreate."
        }
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
    Export-Pfx $cert

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

# ===== MODE: A NEW PFX PASSWORD =====

# SHA-256 of the password this script once contained, published with the
# repository's history: never to be used again. Only the hash is kept here.
$PublishedPasswordHash = "c16616c02453714733fd24bf24620a8a5f61c5bfb8fa8411e373fd0e10cc2a7d"

function Get-Sha256Hex([string]$text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($text))
        return -join ($bytes | ForEach-Object { $_.ToString("x2") })
    } finally {
        $sha.Dispose()
    }
}

function Invoke-RotatePassword {
    Write-Step "Putting the PFX under a new password"
    $newPassword = Get-PfxPassword
    if ((Get-Sha256Hex $newPassword) -eq $PublishedPasswordHash) {
        throw "That is the password published in the repository's history. Choose another one."
    }
    if ($newPassword.Length -lt 12) {
        throw "The new password is shorter than 12 characters."
    }

    # The certificate with its key: from this account's store, else from the
    # PFX under its old password.
    $cert = Get-ExistingCert
    if ($cert -and -not $cert.HasPrivateKey) { $cert = $null }
    if (-not $cert) {
        if (-not (Test-Path $PfxPath)) {
            throw "No certificate in $CertStore and no PFX at ${PfxPath}, so there is nothing to put under a new password. Run -Mode CreateAndSign for a new certificate."
        }
        if ([string]::IsNullOrEmpty($OldPfxPassword)) {
            throw "The certificate is not in $CertStore. Set ULTRACANVAS_SIGN_OLD_PFX_PASSWORD (or pass -OldPfxPassword) to the PFX's current password so it can be read."
        }
        $oldSecure = ConvertTo-SecureString -String $OldPfxPassword -Force -AsPlainText
        $cert = Import-PfxCertificate -FilePath $PfxPath -CertStoreLocation $CertStore -Password $oldSecure -Exportable |
                Where-Object { $_.HasPrivateKey } | Select-Object -First 1
        if (-not $cert) { throw "The PFX at $PfxPath holds no private key." }
        Write-Ok "Read the PFX with its old password (thumbprint $($cert.Thumbprint))"
    } else {
        Write-Ok "Certificate in $CertStore (thumbprint $($cert.Thumbprint))"
    }

    # Written beside the old file, proven, then swapped in: the old PFX is
    # never gone before the new one opens.
    $newPath = "$PfxPath.new"
    $secure = ConvertTo-SecureString -String $newPassword -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $newPath -Password $secure | Out-Null
    $check = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($newPath, $newPassword)
    if ($check.Thumbprint -ne $cert.Thumbprint -or -not $check.HasPrivateKey) {
        Remove-Item $newPath -Force
        throw "The new PFX did not open as the same certificate with its key; the old one is left as it was."
    }
    if (Test-Path $PfxPath) { Remove-Item $PfxPath -Force }
    Move-Item $newPath $PfxPath
    Write-Ok "PFX at $PfxPath is under the new password (thumbprint $($check.Thumbprint))"
    Write-Warn "Copies of the old PFX elsewhere still open with the old password: delete them."
    Write-Warn "Update ULTRACANVAS_SIGN_PFX_PASSWORD wherever it is kept (a CI secret, a build machine)."
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
        $password = Get-PfxPassword
        & $signtool sign `
            /fd   SHA256 `
            /f    $PfxPath `
            /p    $password `
            /d    $Description `
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
            /d   $Description `
            /du  "https://ultracanvas.dev" `
            /t   "http://timestamp.digicert.com" `
            $path
    }

    if ($LASTEXITCODE -ne 0) {
        # Timestamp server may be unreachable — retry without timestamp
        Write-Warn "Timestamp server unreachable, signing without timestamp..."
        if (Test-Path $PfxPath) {
            & $signtool sign /fd SHA256 /f $PfxPath /p $password /d $Description $path
        } else {
            $cert = Get-ExistingCert
            & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /d $Description $path
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
Write-Host "  Code Signing Tool: $Description" -ForegroundColor Magenta
Write-Host "  Mode: $Mode" -ForegroundColor Magenta
Write-Host "==========================================" -ForegroundColor Magenta

if ($Mode -ne "RotatePassword" -and [string]::IsNullOrEmpty($ExePath)) {
    throw "-ExePath is needed for -Mode $Mode."
}

switch ($Mode) {
    "RotatePassword" {
        Invoke-RotatePassword
    }
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
