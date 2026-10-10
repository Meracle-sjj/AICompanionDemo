<#
[INPUT]: 项目当前工作树；PowerShell 5.1+ / .NET 压缩库；可选本机 Git 的只读 HEAD。
[OUTPUT]: 明确白名单的公开源码或私存工程 ZIP、逐文件 SHA256 清单；不修改 Git。
[POS]: 本地交付工具。PublicSource 用于代码评审，PrivateProject 保全可复建资产。
[PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md；发布范围见 Docs/REPOSITORY.md。
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet('PublicSource', 'PrivateProject')][string]$Mode = 'PublicSource',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$')]
    [string]$Name = 'AICompanionDemo-source-20261010',
    [switch]$PlanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd('\', '/')
$outRoot = [System.IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\', '/')
$separator = [System.IO.Path]::DirectorySeparatorChar
$rootPrefix = $root + $separator
if ($outRoot.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
    throw '输出目录不能是工程根目录。'
}
foreach ($selectedRoot in @('Source', 'Config', 'Docs', 'Tools', 'Content')) {
    $selectedPrefix = (Join-Path $root $selectedRoot) + $separator
    if (($outRoot + $separator).StartsWith($selectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw '输出目录不能放在被选入的源码、配置、文档、工具或内容目录内。'
    }
}
if ((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw '项目根不能是重解析点。'
}
$projectFile = Join-Path $root 'AICompanionDemo.uproject'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw '未找到 AICompanionDemo.uproject。'
}

$selected = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.FileInfo]' ([StringComparer]::OrdinalIgnoreCase)
$checkedDirectories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
function Add-SelectedFile([System.IO.FileInfo]$file) {
    if (-not $file.FullName.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw '候选文件超出工程根。'
    }
    $relative = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "候选文件是重解析点：$relative" }
    $ancestor = $file.Directory
    while ($ancestor.FullName -ne $root -and -not $checkedDirectories.Contains($ancestor.FullName)) {
        if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "候选路径含重解析点：$relative"
        }
        $null = $checkedDirectories.Add($ancestor.FullName)
        $ancestor = $ancestor.Parent
    }
    $selected[$relative] = $file
}

foreach ($rootName in @('AICompanionDemo.uproject', 'README.md', 'PLAYER_GUIDE.md', 'AGENTS.md', 'CLAUDE.md', '.gitignore', '.gitattributes')) {
    $path = Join-Path $root $rootName
    if (Test-Path -LiteralPath $path -PathType Leaf) { Add-SelectedFile (Get-Item -LiteralPath $path -Force) }
}
foreach ($dirName in @('Source', 'Docs')) {
    $path = Join-Path $root $dirName
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
    $allowed = if ($dirName -eq 'Source') { @('.h', '.cpp', '.cs', '.md') } else { @('.md', '.svg', '.png', '.jpg', '.jpeg', '.json', '.csv', '.py') }
    foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File -Force) {
        if ($allowed -contains $file.Extension.ToLowerInvariant()) { Add-SelectedFile $file }
    }
}
foreach ($configName in @('DefaultEngine.ini', 'DefaultGame.ini', 'DefaultInput.ini', 'DefaultGameUserSettings.ini')) {
    $path = Join-Path (Join-Path $root 'Config') $configName
    if (Test-Path -LiteralPath $path -PathType Leaf) { Add-SelectedFile (Get-Item -LiteralPath $path) }
}
# 不复制生成语音/下载/编辑器遥控等开发工具，避免其凭据和本机环境进入公开候选。
foreach ($toolName in @('Make-ReleaseSnapshot.ps1', 'Build-Windows.ps1')) {
    Add-SelectedFile (Get-Item -LiteralPath (Join-Path (Join-Path $root 'Tools') $toolName))
}

if ($Mode -eq 'PrivateProject') {
    foreach ($dirName in @('Source', 'Config', 'Content', 'Build')) {
        $path = Join-Path $root $dirName
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File -Force) {
            $relative = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
            if ($relative -match '^Content/(Developers|Collections)/' -or $relative -match '(^|/)(\.env[^/]*|[^/]*EditorPerProjectUserSettings\.ini|[^/]*Local[^/]*\.ini)$') { continue }
            if ($dirName -eq 'Build' -and $file.Extension -ne '.ico') { continue }
            Add-SelectedFile $file
        }
    }
}

$secretRules = [ordered]@{
    'private-key' = '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'
    'github-token' = '(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})'
    'provider-token' = '\bsk-[A-Za-z0-9_-]{24,}\b'
    'credential-assignment' = '(?im)^\s*(?:export\s+)?[A-Za-z_]*(?:api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret|securitytoken)[A-Za-z_]*\s*=\s*["'']?(?!\s*(?:["'']?\s*$|YOUR[_-]|EXAMPLE|PLACEHOLDER|<|\$))[A-Za-z0-9_.-]{16,}'
    'personal-home-path' = '(?i)[A-Za-z]:[\\/]Users[\\/][^\\/\s<>"'']+'
}
$textExtensions = @('.h', '.cpp', '.cs', '.ini', '.md', '.ps1', '.py', '.json', '.csv', '.svg')
$findings = New-Object 'System.Collections.Generic.List[object]'
foreach ($entry in $selected.GetEnumerator()) {
    if ($textExtensions -notcontains $entry.Value.Extension.ToLowerInvariant()) { continue }
    $lines = [System.IO.File]::ReadAllLines($entry.Value.FullName)
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
        foreach ($rule in $secretRules.GetEnumerator()) {
            if ($lines[$lineIndex] -match $rule.Value) {
                $findings.Add([pscustomobject]@{Path = $entry.Key; Line = $lineIndex + 1; Type = $rule.Key})
            }
        }
    }
}
if ($findings.Count -gt 0) {
    $findings | Format-Table -AutoSize | Out-Host
    throw '候选包含凭据或个人目录模式；仅输出位置/类型，清理后再执行。'
}

$inventory = @($selected.Keys | Sort-Object | ForEach-Object {
    $file = $selected[$_]
    $file.Refresh()
    [pscustomobject]@{Path = $_; Bytes = $file.Length; Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$head = $null
if (Get-Command git -ErrorAction SilentlyContinue) {
    $headLines = @(& git -C $root rev-parse --verify HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $headLines.Count -gt 0) { $head = $headLines[0] }
}
$manifest = [ordered]@{
    SchemaVersion = 1
    Mode = $Mode
    Name = $Name
    SourceHead = $head
    IncludesUncommittedWorkingTree = $true
    ContentAssetPolicy = $(if ($Mode -eq 'PublicSource') { 'Excluded: code review only; not a complete runnable/buildable project.' } else { 'Private local backup only; includes third-party raw assets; do not publish.' })
    FileCount = $inventory.Count
    UncompressedBytes = [long](($inventory | Measure-Object -Property Bytes -Sum).Sum)
    Files = $inventory
}
New-Item -ItemType Directory -Path $outRoot -Force | Out-Null
if ($PlanOnly) {
    $planPath = Join-Path $outRoot "$Name.plan.json"
    if (Test-Path -LiteralPath $planPath) {
        $previous = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
        if (($previous.Files | ConvertTo-Json -Compress -Depth 5) -ne ($inventory | ConvertTo-Json -Compress -Depth 5) -or $previous.Mode -ne $Mode) {
            throw '同名计划内容已变；请指定新 Name，保留原审计记录。'
        }
    } else {
        [IO.File]::WriteAllText($planPath, ($manifest | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
    }
    [pscustomobject]@{Status = 'PlanOnly'; Mode = $Mode; Files = $inventory.Count; Bytes = $manifest.UncompressedBytes; Plan = $planPath}
    return
}

$zipPath = Join-Path $outRoot "$Name.zip"
$manifestPath = Join-Path $outRoot "$Name.snapshot.json"
if ((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $manifestPath)) {
    if (-not ((Test-Path -LiteralPath $zipPath) -and (Test-Path -LiteralPath $manifestPath))) {
        throw '已有同名不完整输出；请检查记录并指定新 Name，脚本不覆盖或删除。'
    }
    $previous = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($previous.Mode -eq $Mode -and $previous.ArchiveSha256 -eq $zipHash -and
        ($previous.Files | ConvertTo-Json -Compress -Depth 5) -eq ($inventory | ConvertTo-Json -Compress -Depth 5)) {
        [pscustomobject]@{Status = 'ReusedVerified'; Mode = $Mode; Files = $inventory.Count; Bytes = (Get-Item -LiteralPath $zipPath).Length; Archive = $zipPath; Sha256 = $zipHash}
        return
    }
    throw '同名 ZIP 或工作树内容不同；请指定新 Name，脚本不覆盖或删除。'
}

$stream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
$archive = $null
try {
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
    foreach ($item in $inventory) {
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $selected[$item.Path].FullName, $item.Path, [IO.Compression.CompressionLevel]::Optimal)
    }
    $archive.Dispose()
    $archive = $null
} finally {
    if ($null -ne $archive) { $archive.Dispose() }
    $stream.Dispose()
}

# 解码整个 ZIP 并逐文件核 SHA256，同时复验源文件，防止并行编辑被当成稳定快照。
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($archive.Entries.Count -ne $inventory.Count) { throw 'ZIP 条目数不一致。' }
    foreach ($item in $inventory) {
        $zipEntry = $archive.GetEntry($item.Path)
        if ($null -eq $zipEntry -or $zipEntry.Length -ne $item.Bytes) { throw "ZIP 长度/路径不一致：$($item.Path)" }
        $entryStream = $zipEntry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $entryHash = [BitConverter]::ToString($sha.ComputeHash($entryStream)).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose(); $entryStream.Dispose() }
        if ($entryHash -ne $item.Sha256) { throw "ZIP 解码哈希不一致：$($item.Path)" }
        if ((Get-FileHash -LiteralPath $selected[$item.Path].FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.Sha256) {
            throw "打包期间源文件变化：$($item.Path)，此候选不可发布。"
        }
    }
} finally { $archive.Dispose() }
$manifest['ArchiveBytes'] = (Get-Item -LiteralPath $zipPath).Length
$manifest['ArchiveSha256'] = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
[IO.File]::WriteAllText((Join-Path $outRoot "$Name.sha256"), "$($manifest.ArchiveSha256)  $Name.zip`n", (New-Object Text.UTF8Encoding $false))
[pscustomobject]@{Status = 'CreatedVerified'; Mode = $Mode; Files = $inventory.Count; Bytes = $manifest.ArchiveBytes; Archive = $zipPath; Sha256 = $manifest.ArchiveSha256}
