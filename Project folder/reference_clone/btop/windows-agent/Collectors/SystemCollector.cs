using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace SysmonHost.Collectors;

public sealed class SystemCollector
{
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetSystemTimes(out ulong idle, out ulong kernel, out ulong user);
    [StructLayout(LayoutKind.Sequential)]
    struct MemoryStatus
    {
        public uint Length, Load;
        public ulong TotalPhys, AvailPhys, TotalPage, AvailPage, TotalVirtual, AvailVirtual, AvailExtended;
    }
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);
    ulong previousIdle, previousTotal;
    bool haveCpu;
    readonly string name = Registry.GetValue(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0", "ProcessorNameString", "Windows CPU")?.ToString()?.Trim() ?? "Windows CPU";

    public Cpu ReadCpu()
    {
        double? usage=null;
        // GetSystemTimes covers one processor group. Don't mislabel a partial reading on >64 CPUs.
        if (Environment.ProcessorCount<=64 && GetSystemTimes(out var idle, out var kernel, out var user))
        {
            ulong total=kernel+user;
            if(haveCpu && total>previousTotal && idle>=previousIdle)
                usage=Math.Clamp(100.0*(1.0-(double)(idle-previousIdle)/(total-previousTotal)),0,100);
            previousTotal=total; previousIdle=idle; haveCpu=true;
        }
        return new(usage,name,Environment.ProcessorCount,Environment.TickCount64/1000.0);
    }
    public Memory? ReadMemory()
    {
        var s=new MemoryStatus { Length=(uint)Marshal.SizeOf<MemoryStatus>() };
        return GlobalMemoryStatusEx(ref s) ? new(s.TotalPhys,s.AvailPhys) : null;
    }
    public ProcessRow[] ReadProcesses(Stopwatch clock)
    {
        var rows=new List<ProcessRow>();
        foreach(var p in Process.GetProcesses())
        using(p)
        {
            try
            {
                string name=p.ProcessName;
                long? memory=null; double? cpu=null; string? start=null;
                try { memory=p.WorkingSet64; } catch { }
                try { cpu=p.TotalProcessorTime.TotalSeconds; start=p.StartTime.ToUniversalTime().Ticks.ToString(); } catch { }
                rows.Add(new(p.Id,name,memory,cpu,start,clock.Elapsed.TotalSeconds));
            }
            catch (InvalidOperationException) { } // Process exited during enumeration.
            catch (System.ComponentModel.Win32Exception) { }
        }
        return rows.ToArray();
    }
}
