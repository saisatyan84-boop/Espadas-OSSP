using LibreHardwareMonitor.Hardware;
using System.Security.Principal;

namespace SysmonHost.Collectors;
public sealed class SensorCollector : IDisposable
{
    readonly Computer computer=new() { IsCpuEnabled=true, IsGpuEnabled=true, IsStorageEnabled=true, IsMotherboardEnabled=true, IsControllerEnabled=true };
    bool opened;
    readonly bool elevated=new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
    public SensorData Read(double time)
    {
        try
        {
            if(!opened) { computer.Open(); opened=true; }
            var rows=new List<Sensor>();
            void Visit(IHardware h)
            {
                h.Update();
                foreach(var s in h.Sensors)
                {
                    string unit=s.SensorType switch { SensorType.Temperature=>"C", SensorType.Load=>"%", SensorType.Clock=>"MHz", SensorType.Power=>"W", SensorType.Fan=>"RPM", SensorType.Voltage=>"V", SensorType.SmallData=>"MiB", SensorType.Data=>"GiB", SensorType.Throughput=>"B/s", _=>"" };
                    if(unit.Length==0) continue;
                    double? value=s.Value is float v && float.IsFinite(v)?v:null;
                    // AMD low-level reads can return zero/default values without driver access.
                    // CPU load uses Windows counters; register-based CPU sensors need elevation.
                    if(h.HardwareType==HardwareType.Cpu && !elevated && s.SensorType!=SensorType.Load) value=null;
                    rows.Add(new(s.Identifier.ToString(),h.Name,h.HardwareType.ToString(),s.Name,unit,value));
                }
                foreach(var child in h.SubHardware) Visit(child);
            }
            foreach(var hardware in computer.Hardware) Visit(hardware);
            return new(time,rows.ToArray(),"LibreHardwareMonitor | unavailable values may require hardware support or optional elevation");
        }
        catch(Exception e) { return new(time,[],"Sensors unavailable: "+e.GetType().Name); }
    }
    public void Dispose() { if(opened) computer.Close(); }
}
