using System.Management;

namespace SysmonHost.Collectors;
public sealed class DeviceCollector : IDisposable
{
    readonly BluetoothCollector? bluetooth;
    readonly string bluetoothError="";
    public DeviceCollector()
    {
        try { bluetooth=new(); } catch(Exception e) { bluetoothError="Bluetooth unavailable: "+e.GetType().Name; }
    }
    public SlowData Read(double time)
    {
        var devices=new List<Device>(); var status=new List<string>();
        bool displays=false;
        var defaults=new Dictionary<string,string>();
        try { defaults=AudioCollector.DefaultEndpoints(); }
        catch(Exception e) { status.Add("Audio unavailable: "+e.GetType().Name); }
        try { devices.AddRange(DisplayCollector.Read()); displays=true; }
        catch(Exception e) { status.Add("DisplayConfig unavailable: "+e.GetType().Name); }
        if(bluetooth!=null) { var (rows,s)=bluetooth.Read(); devices.AddRange(rows); status.Add(s); }
        else status.Add(bluetoothError);
        try
        {
            // Presence describes USB/PnP only; Bluetooth connection comes from WinRT above.
            using var query=new ManagementObjectSearcher("SELECT PNPDeviceID,Name,PNPClass,Status FROM Win32_PnPEntity WHERE Present=True");
            using var objects=query.Get();
            foreach(ManagementObject d in objects)
            using(d)
            {
                string id=d["PNPDeviceID"]?.ToString()??"", name=d["Name"]?.ToString()??"Unknown", cls=d["PNPClass"]?.ToString()??"";
                string category=cls switch { "AudioEndpoint"=>"Audio", "Monitor"=>"Displays", "DiskDrive"=>"Storage", "Net"=>"Network", "Bluetooth"=>"Bluetooth radio", "Keyboard" or "Mouse"=>"Input peripherals", _=>id.StartsWith("USB\\",StringComparison.OrdinalIgnoreCase)?"USB":"" };
                if(category.Length==0) continue;
                if(category=="Displays" && displays) continue;
                if(category=="Network" && !id.StartsWith("PCI\\") && !id.StartsWith("USB\\")) continue;
                // Bluetooth child services are not additional radios.
                if(category=="Bluetooth radio" && !id.StartsWith("USB\\",StringComparison.OrdinalIgnoreCase)) continue;
                var role=category=="Audio"?defaults.FirstOrDefault(p=>id.EndsWith(p.Key,StringComparison.OrdinalIgnoreCase)).Value:null;
                devices.Add(new(id,category,name,role!=null?"Default":"Present",null,(role??cls)+" | "+(d["Status"]?.ToString()??"Unknown")+(category=="Displays"?" | connection N/A":"")));
            }
        }
        catch(Exception e) { status.Add("PnP unavailable: "+e.GetType().Name); }
        double? read=null,write=null;
        try { (read,write)=StorageCollector.Activity(); } catch(Exception e) { status.Add("Disk counters unavailable: "+e.GetType().Name); }
        return new(time,devices.OrderBy(x=>x.Category).ThenBy(x=>x.Name).ToArray(),StorageCollector.Read(),string.Join("; ",status),read,write);
    }
    public void Dispose()=>bluetooth?.Dispose();
}
