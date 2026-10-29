using System.Management;
namespace SysmonHost.Collectors;
public static class StorageCollector
{
    public static Drive[] Read()
    {
        var rows=new List<Drive>();
        foreach(var d in DriveInfo.GetDrives()) {
            if(d.DriveType is not (DriveType.Fixed or DriveType.Removable)) continue;
            try { if(d.IsReady) rows.Add(new(d.Name,d.DriveType.ToString(),(ulong)d.TotalSize,(ulong)d.TotalFreeSpace)); }
            catch(IOException) { } catch(UnauthorizedAccessException) { }
        }
        return rows.ToArray();
    }
    public static (double?,double?) Activity()
    {
        using var query=new ManagementObjectSearcher("SELECT DiskReadBytesPersec,DiskWriteBytesPersec FROM Win32_PerfFormattedData_PerfDisk_PhysicalDisk WHERE Name='_Total'");
        using var objects=query.Get();
        foreach(ManagementObject d in objects) using(d)
            return (Convert.ToDouble(d["DiskReadBytesPersec"]),Convert.ToDouble(d["DiskWriteBytesPersec"]));
        return (null,null);
    }
}
