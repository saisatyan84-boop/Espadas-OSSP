using System.Runtime.InteropServices;

namespace SysmonHost.Collectors;
public static class AudioCollector
{
    [ComImport,Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class Enumerator { }
    [ComImport,Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IEnumerator {
        [PreserveSig] int EnumAudioEndpoints(int flow,uint mask,out IntPtr collection);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow,int role,out IDevice device);
    }
    [ComImport,Guid("D666063F-1587-4E43-81F1-B948E807363F"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IDevice {
        [PreserveSig] int Activate(ref Guid iid,uint context,IntPtr parameters,out IntPtr instance);
        [PreserveSig] int OpenPropertyStore(uint access,out IntPtr store);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        [PreserveSig] int GetState(out uint state);
    }
    public static Dictionary<string,string> DefaultEndpoints() {
        var result=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        var enumerator=(IEnumerator)new Enumerator();
        try {
            for(int flow=0;flow<2;flow++) {
                if(enumerator.GetDefaultAudioEndpoint(flow,0,out var endpoint)<0) continue;
                try { if(endpoint.GetId(out string id)>=0) result[id]=flow==0?"Default output":"Default microphone"; }
                finally { Marshal.ReleaseComObject(endpoint); }
            }
        }
        finally { Marshal.ReleaseComObject(enumerator); }
        return result;
    }
}
