using Windows.Devices.Enumeration;

namespace SysmonHost.Collectors;
public sealed class BluetoothCollector : IDisposable
{
    const string Connected="System.Devices.Aep.IsConnected", Paired="System.Devices.Aep.IsPaired", Battery="System.Devices.BatteryLife";
    static readonly string[] Properties=[Connected,Paired,Battery,"System.Devices.Aep.DeviceAddress"];
    const string Selector="System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\" OR System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";
    readonly DeviceWatcher watcher;
    readonly object gate=new();
    readonly Dictionary<string,DeviceInformation> items=new();
    bool complete;
    public BluetoothCollector()
    {
        watcher=DeviceInformation.CreateWatcher(Selector,Properties,DeviceInformationKind.AssociationEndpoint);
        watcher.Added+=(_,d)=> { lock(gate) items[d.Id]=d; };
        watcher.Updated+=(_,d)=> { lock(gate) if(items.TryGetValue(d.Id,out var old)) old.Update(d); };
        watcher.Removed+=(_,d)=> { lock(gate) items.Remove(d.Id); };
        watcher.EnumerationCompleted+=(_,_)=> { lock(gate) complete=true; };
        watcher.Start();
    }
    public (Device[],string) Read()
    {
        lock(gate)
        {
            var rows=new List<Device>();
            foreach(var d in items.Values)
            {
                bool? connected=d.Properties.TryGetValue(Connected,out var c) ? c as bool? : null;
                bool paired=d.Properties.TryGetValue(Paired,out var p) && p is true;
                if(!paired && connected!=true) continue; // No nearby strangers in the connected-device view.
                double? battery=null;
                if(connected==true && d.Properties.TryGetValue(Battery,out var b) && b!=null)
                {
                    try { double n=Convert.ToDouble(b); if(n>=0 && n<=100) battery=n; } catch { }
                }
                var address=d.Properties.TryGetValue("System.Devices.Aep.DeviceAddress",out var a)?a?.ToString():null;
                rows.Add(new(d.Id,"Bluetooth",d.Name,connected==true?"Connected":connected==false?"Disconnected":"Unknown",battery,
                    (paired?"Paired":"Unpaired")+" | "+(address??"address N/A")+" | "+(battery.HasValue?"Windows battery":"battery not exposed")));
            }
            // A dual-mode device can expose both Classic and LE association endpoints.
            var merged=rows.GroupBy(d=> {
                var address=d.Detail.Split('|')[1].Trim();
                return address=="address N/A"?d.Id:address;
            }).Select(g=>g.OrderByDescending(d=>d.State=="Connected").ThenByDescending(d=>d.Battery.HasValue).First()).ToArray();
            return (merged,complete?"Bluetooth ready":"Bluetooth scanning");
        }
    }
    public void Dispose() { if(watcher.Status is DeviceWatcherStatus.Started or DeviceWatcherStatus.EnumerationCompleted) watcher.Stop(); }
}
