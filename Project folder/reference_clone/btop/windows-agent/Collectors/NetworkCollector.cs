using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace SysmonHost.Collectors;
public sealed class NetworkCollector
{
    readonly Dictionary<string,(long Rx,long Tx,double Time)> previous=new();
    public Network[] Read(double time)
    {
        var result=new List<Network>();
        var live=new HashSet<string>();
        foreach(var nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if(nic.NetworkInterfaceType==NetworkInterfaceType.Loopback) continue;
            try
            {
                var properties=nic.GetIPProperties();
                // Windows may return NDIS filter-stack pseudo-interfaces as well.
                // They have no unicast addresses and duplicate the parent counters.
                if(properties.UnicastAddresses.Count==0) continue;
                var stats=nic.GetIPStatistics();
                double? rx=null,tx=null;
                if(previous.TryGetValue(nic.Id,out var p) && time>p.Time && stats.BytesReceived>=p.Rx && stats.BytesSent>=p.Tx)
                { rx=(stats.BytesReceived-p.Rx)/(time-p.Time); tx=(stats.BytesSent-p.Tx)/(time-p.Time); }
                previous[nic.Id]=(stats.BytesReceived,stats.BytesSent,time); live.Add(nic.Id);
                var address=properties.UnicastAddresses.FirstOrDefault(x=>x.Address.AddressFamily==AddressFamily.InterNetwork)?.Address.ToString() ?? properties.UnicastAddresses.FirstOrDefault()?.Address.ToString() ?? "N/A";
                bool virt=nic.Description.Contains("Virtual",StringComparison.OrdinalIgnoreCase) || nic.Name.Contains("vEthernet",StringComparison.OrdinalIgnoreCase) || nic.NetworkInterfaceType==NetworkInterfaceType.Tunnel;
                result.Add(new(nic.Id,nic.Name,nic.NetworkInterfaceType.ToString(),nic.OperationalStatus.ToString(),address,virt,rx,tx));
            }
            catch(NetworkInformationException) { }
        }
        foreach(var id in previous.Keys.Where(x=>!live.Contains(x)).ToArray()) previous.Remove(id);
        return result.OrderBy(x=>x.Virtual).ThenByDescending(x=>x.State=="Up").ThenByDescending(x=>x.Kind=="Wireless80211").ThenBy(x=>x.Name).ToArray();
    }
}
