# Read-only native probe. Aggregate Cue.exe plus its thumbnail helper.
# Example: powershell -File tools\measure_resources.ps1 -Seconds 120
# Read/Write counters are logical process I/O, NOT physical disk bandwidth/active time.
[CmdletBinding()]
param(
  [ValidateRange(1,3600)][int]$Seconds = 60,
  [string]$OutputPath = (Join-Path (Get-Location) 'Cue-resource-samples.csv')
)
$ErrorActionPreference = 'Stop'
if (-not ('CueResourceProbe.Native' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace CueResourceProbe {
  [StructLayout(LayoutKind.Sequential)]
  public struct IoCounters {
    public ulong ReadOperations, WriteOperations, OtherOperations;
    public ulong ReadBytes, WriteBytes, OtherBytes;
  }
  public static class Native {
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetProcessIoCounters(IntPtr process, out IoCounters counters);
  }
}
'@
}
$watch = [Diagnostics.Stopwatch]::StartNew()
$previous = @{}
$rows = [Collections.Generic.List[object]]::new()
while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
  foreach ($process in [Diagnostics.Process]::GetProcessesByName('Cue')) {
    try {
      $process.Refresh()
      $io = [CueResourceProbe.IoCounters]::new()
      if (-not [CueResourceProbe.Native]::GetProcessIoCounters($process.Handle,[ref]$io)) { continue }
      $time = $watch.Elapsed.TotalSeconds
      $key = '{0}:{1}' -f $process.Id,$process.StartTime.Ticks
      $readRate = 0.0; $writeRate = 0.0
      if ($previous.ContainsKey($key)) {
        $last = $previous[$key]; $delta = $time - $last.Time
        if ($delta -gt 0) {
          $readRate = [Math]::Max(0.0,([double]$io.ReadBytes-$last.Read)/$delta/1MB)
          $writeRate = [Math]::Max(0.0,([double]$io.WriteBytes-$last.Write)/$delta/1MB)
        }
      }
      $previous[$key] = @{Time=$time; Read=[double]$io.ReadBytes; Write=[double]$io.WriteBytes}
      $rows.Add([pscustomobject]@{
        Seconds=[Math]::Round($time,2); ProcessId=$process.Id
        PrivateMiB=[Math]::Round($process.PrivateMemorySize64/1MB,2)
        WorkingSetMiB=[Math]::Round($process.WorkingSet64/1MB,2)
        LogicalReadMiBPerSecond=[Math]::Round($readRate,3)
        LogicalWriteMiBPerSecond=[Math]::Round($writeRate,3)
        ReadOperations=$io.ReadOperations; WriteOperations=$io.WriteOperations
      })
    } catch {
      # A helper may exit between enumeration and sampling; continue with live processes.
      Write-Verbose $_.Exception.Message
    } finally { $process.Dispose() }
  }
  Start-Sleep -Milliseconds 1000
}
$rows | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8
Write-Output "Saved $($rows.Count) process samples to $OutputPath"
Write-Output 'Private memory, working sets and process I/O are separate measures. Use Resource Monitor for physical-disk active time.'
