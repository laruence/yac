$ErrorActionPreference = 'Stop'

$php = Get-ChildItem -Path build -Recurse -Filter php.exe |
    Where-Object { $_.FullName -match '[\\/]php-bin[\\/]php\.exe$' } |
    Select-Object -First 1 -ExpandProperty FullName
$dll = Get-ChildItem -Path build -Recurse -Filter php_yac.dll |
    Where-Object { $_.FullName -match '[\\/]Release(?:_[^\\/]+)?[\\/]php_yac\.dll$' } |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $php -or -not $dll) {
    throw 'Unable to locate the PHP executable or php_yac.dll produced by the build action.'
}

$root = Join-Path $env:RUNNER_TEMP "yac-file-mapping-$env:GITHUB_RUN_ID-$env:GITHUB_RUN_ATTEMPT-$PID"
New-Item -ItemType Directory -Path $root | Out-Null
$ready = Join-Path $root 'writer-ready'
$readerDone = Join-Path $root 'reader-done'
$writerOut = Join-Path $root 'writer.out'
$writerErr = Join-Path $root 'writer.err'

$ini = @('-n', "-dextension=$dll", '-dyac.enable=1', '-dyac.enable_cli=1', '-dyac.keys_memory_size=4M', '-dyac.values_memory_size=32M')
$writerPath = Join-Path $root 'writer.php'
$readerPath = Join-Path $root 'reader.php'
@'
<?php
$yac = new Yac('windows-file-mapping');
$yac->set('writer', 'writer', 30);
file_put_contents($argv[1], 'ready');
for ($i = 0; $i < 100 && !file_exists($argv[2]); $i++) {
    usleep(50000);
}
echo $yac->get('reader', 'MISS'), PHP_EOL;
'@ | Set-Content -NoNewline $writerPath
@'
<?php
$yac = new Yac('windows-file-mapping');
echo $yac->get('writer', 'MISS'), PHP_EOL;
$yac->set('reader', 'reader', 30);
file_put_contents($argv[1], 'done');
'@ | Set-Content -NoNewline $readerPath

try {
    $process = Start-Process -FilePath $php -ArgumentList ($ini + @($writerPath, $ready, $readerDone)) -PassThru -NoNewWindow -RedirectStandardOutput $writerOut -RedirectStandardError $writerErr

    for ($i = 0; $i -lt 100 -and -not (Test-Path $ready); $i++) {
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 50
    }
    if (-not (Test-Path $ready)) {
        $stderr = if (Test-Path $writerErr) { Get-Content -Raw $writerErr } else { '' }
        throw "Writer did not initialize Yac. $stderr"
    }

    $readerOut = & $php @ini $readerPath $readerDone 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Reader failed with exit code $LASTEXITCODE. $readerOut"
    }
    if ($readerOut.Trim() -ne 'writer') {
        throw "Independent Windows PHP processes did not share Yac data: $readerOut"
    }

    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw 'Writer did not terminate.'
    }
    if ($process.ExitCode -ne 0) {
        throw "Writer failed with exit code $($process.ExitCode)."
    }

    $writerValue = (Get-Content -Raw $writerOut).Trim()
    if ($writerValue -ne 'reader') {
        throw "The writer did not observe the reader's shared Yac value: $writerValue"
    }

    Write-Host 'Windows Yac file-mapping sharing passed.'
} finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
    }
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
