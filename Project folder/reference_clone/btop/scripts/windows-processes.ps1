param([int]$IntervalMs = 2000)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$clock = [Diagnostics.Stopwatch]::StartNew()
# Prototype producer only. Replace with the native agent in Phase 3.
try {
    while ($true) {
        $begin = $clock.Elapsed.TotalSeconds
        $rows = @(
            foreach ($proc in [Diagnostics.Process]::GetProcesses()) {
                try {
                    $procId = $proc.Id
                    $name = $proc.ProcessName
                    $ram = $null
                    $cpu = $null
                    $startId = $null
                    try { $ram = $proc.WorkingSet64 } catch {}
                    try { $cpu = $proc.TotalProcessorTime.TotalSeconds } catch {}
                    try { $startId = $proc.StartTime.ToUniversalTime().Ticks.ToString() } catch {}
                    [PSCustomObject]@{
                        pid = $procId
                        name = $name
                        ram_bytes = $ram
                        cpu_seconds = $cpu
                        start_id = $startId
                        sample_seconds = $clock.Elapsed.TotalSeconds
                    }
                } catch {
                    # A process can exit while it is being queried.
                } finally { $proc.Dispose() }
            }
        )
        $frame = @{
            schema_version = 1
            source = 'windows'
            sample_seconds = $clock.Elapsed.TotalSeconds
            processes = @($rows)
        }
        [Console]::WriteLine(($frame | ConvertTo-Json -Depth 5 -Compress))
        $delay = [Math]::Max(100, $IntervalMs - (($clock.Elapsed.TotalSeconds - $begin) * 1000))
        Start-Sleep -Milliseconds ([int]$delay)
    }
} catch {
    [Console]::Error.WriteLine('Windows process telemetry stopped: ' + $_.Exception.Message)
    exit 1
}
