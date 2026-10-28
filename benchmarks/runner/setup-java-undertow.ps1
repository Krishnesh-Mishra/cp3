# Downloads the pinned Undertow runtime jars used by the Java health-server
# entries (benchmarks/cases/health-server/java and
# benchmarks/cases/health-server-multi/java).
#
# Idempotent: an existing jar whose SHA-1 matches Maven Central is left alone.
# The jars land in benchmarks/.cache/deps/java/undertow, which is gitignored.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File benchmarks/runner/setup-java-undertow.ps1

[CmdletBinding()]
param(
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not $Destination) {
    $Destination = Join-Path $PSScriptRoot '..\.cache\deps\java\undertow'
}

$mavenBase = 'https://repo1.maven.org/maven2'

# Versions are the ones declared by undertow-core 2.3.27.Final's POM /
# undertow-parent 2.3.27.Final. jboss-threads is a direct dependency of
# undertow-core (xnio-api's copy is excluded by Undertow's dependency
# management), so it is required at runtime too. jboss-threads 3.7.0.Final
# additionally needs the SmallRye Common modules from its own POM (BOM 2.6.0).
$artifacts = @(
    'io/undertow/undertow-core/2.3.27.Final/undertow-core-2.3.27.Final.jar',
    'org/jboss/logging/jboss-logging/3.4.3.Final/jboss-logging-3.4.3.Final.jar',
    'org/jboss/xnio/xnio-api/3.8.16.Final/xnio-api-3.8.16.Final.jar',
    'org/jboss/xnio/xnio-nio/3.8.16.Final/xnio-nio-3.8.16.Final.jar',
    'org/jboss/threads/jboss-threads/3.7.0.Final/jboss-threads-3.7.0.Final.jar',
    'org/wildfly/common/wildfly-common/1.6.0.Final/wildfly-common-1.6.0.Final.jar',
    'io/smallrye/common/smallrye-common-annotation/2.6.0/smallrye-common-annotation-2.6.0.jar',
    'io/smallrye/common/smallrye-common-constraint/2.6.0/smallrye-common-constraint-2.6.0.jar',
    'io/smallrye/common/smallrye-common-cpu/2.6.0/smallrye-common-cpu-2.6.0.jar',
    'io/smallrye/common/smallrye-common-function/2.6.0/smallrye-common-function-2.6.0.jar'
)

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

foreach ($relativePath in $artifacts) {
    $url = "$mavenBase/$relativePath"
    $name = Split-Path $relativePath -Leaf
    $target = Join-Path $Destination $name

    $expectedSha1 = (Invoke-WebRequest -UseBasicParsing "$url.sha1").Content.Trim().Split(' ')[0].ToLowerInvariant()

    $upToDate = $false
    if (Test-Path -LiteralPath $target) {
        $actualSha1 = (Get-FileHash -LiteralPath $target -Algorithm SHA1).Hash.ToLowerInvariant()
        $upToDate = $actualSha1 -eq $expectedSha1
    }

    if ($upToDate) {
        Write-Host "up to date: $name"
        continue
    }

    Write-Host "downloading: $name"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $target

    $actualSha1 = (Get-FileHash -LiteralPath $target -Algorithm SHA1).Hash.ToLowerInvariant()
    if ($actualSha1 -ne $expectedSha1) {
        Remove-Item -LiteralPath $target -Force
        throw "SHA-1 mismatch for $name (expected $expectedSha1, got $actualSha1)"
    }
}

Write-Host "Undertow runtime jars ($Destination):"
Get-ChildItem -LiteralPath $Destination -Filter '*.jar' |
    Sort-Object Name |
    ForEach-Object { Write-Host "  $($_.Name)" }
