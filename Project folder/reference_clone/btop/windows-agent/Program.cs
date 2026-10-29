using System.Diagnostics;
using System.Text;
using System.Text.Json;
using SysmonHost;
using SysmonHost.Collectors;

if(!OperatingSystem.IsWindows()) { Console.Error.WriteLine("Windows is required."); return 1; }
int interval=2000, frames=0;
for(int i=0;i<args.Length;i++)
{
    if(args[i]=="--stream") continue;
    if(args[i]=="--interval-ms" && i+1<args.Length && int.TryParse(args[++i],out interval)) continue;
    if(args[i]=="--frames" && i+1<args.Length && int.TryParse(args[++i],out frames)) continue;
    Console.Error.WriteLine("Usage: sysmon-host --stream [--interval-ms 1000..60000] [--frames count]"); return 2;
}
interval=Math.Clamp(interval,1000,60000);
Console.OutputEncoding=new UTF8Encoding(false);
var options=new JsonSerializerOptions { PropertyNamingPolicy=JsonNamingPolicy.SnakeCaseLower };
var clock=Stopwatch.StartNew();
var system=new SystemCollector(); var network=new NetworkCollector();
// Slow device/sensor calls have their own single workers. No queued overlapping enumerations.
SlowData inventory=new(0,[],[],"Enumerating devices");
SensorData hardware=new(0,[],"Starting hardware sensors");
using var cancel=new CancellationTokenSource();
Console.CancelKeyPress+=(_,e)=> { e.Cancel=true; cancel.Cancel(); };
var deviceTask=Task.Run(async()=> {
    using var collector=new DeviceCollector();
    while(!cancel.IsCancellationRequested) {
        try { Volatile.Write(ref inventory,collector.Read(clock.Elapsed.TotalSeconds)); }
        catch(Exception e) { Console.Error.WriteLine("Device collection: "+e.GetType().Name); }
        await Task.Delay(5000,cancel.Token);
    }
});
var sensorTask=Task.Run(async()=> {
    using var collector=new SensorCollector();
    while(!cancel.IsCancellationRequested) {
        Volatile.Write(ref hardware,collector.Read(clock.Elapsed.TotalSeconds));
        await Task.Delay(3000,cancel.Token);
    }
});
try
{
    for(int count=0;!cancel.IsCancellationRequested && (frames<=0 || count<frames);count++)
    {
        double began=clock.Elapsed.TotalSeconds;
        var cpu=system.ReadCpu(); var memory=system.ReadMemory();
        var networks=network.Read(clock.Elapsed.TotalSeconds);
        var processes=system.ReadProcesses(clock);
        var host=new HostData(cpu,memory,networks,Volatile.Read(ref inventory),Volatile.Read(ref hardware));
        var frame=new { schema_version=1,source="windows",sample_seconds=clock.Elapsed.TotalSeconds,processes,host };
        await Console.Out.WriteLineAsync(JsonSerializer.Serialize(frame,options));
        await Console.Out.FlushAsync();
        await Task.Delay(Math.Max(100,interval-(int)((clock.Elapsed.TotalSeconds-began)*1000)),cancel.Token);
    }
}
catch(OperationCanceledException) { }
catch(IOException) { } // WSL reader closed its pipe.
finally
{
    cancel.Cancel();
    try { await Task.WhenAll(deviceTask,sensorTask).WaitAsync(TimeSpan.FromSeconds(2)); } catch { }
}
return 0;
