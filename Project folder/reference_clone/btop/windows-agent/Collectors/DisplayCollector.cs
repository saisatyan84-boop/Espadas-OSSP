using System.Runtime.InteropServices;
using System.ComponentModel;

namespace SysmonHost.Collectors;
public static class DisplayCollector
{
    [StructLayout(LayoutKind.Sequential)] struct Luid { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)] struct Source { public Luid Adapter; public uint Id,Mode,Flags; }
    [StructLayout(LayoutKind.Sequential)] struct Rational { public uint Numerator,Denominator; }
    [StructLayout(LayoutKind.Sequential)] struct Target {
        public Luid Adapter; public uint Id,Mode,Technology,Rotation,Scaling;
        public Rational Refresh; public uint Scanline,Available,Flags;
    }
    [StructLayout(LayoutKind.Sequential)] struct DisplayPath { public Source Source; public Target Target; public uint Flags; }
    // DISPLAYCONFIG_MODE_INFO has a 48-byte union after its 16-byte header.
    [StructLayout(LayoutKind.Explicit,Size=64)] struct Mode {
        [FieldOffset(0)] public uint Type;
        [FieldOffset(16)] public uint Width;
        [FieldOffset(20)] public uint Height;
    }
    [StructLayout(LayoutKind.Sequential)] struct Header { public uint Type,Size; public Luid Adapter; public uint Id; }
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct TargetName {
        public Header Header; public uint Flags,Technology; public ushort Manufacturer,Product; public uint Connector;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=64)] public string Friendly;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)] public string Path;
    }
    [DllImport("user32.dll")] static extern int GetDisplayConfigBufferSizes(uint flags,out uint paths,out uint modes);
    [DllImport("user32.dll")] static extern int QueryDisplayConfig(uint flags,ref uint paths,[Out] DisplayPath[] path,ref uint modes,[Out] Mode[] mode,IntPtr topology);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int DisplayConfigGetDeviceInfo(ref TargetName name);

    public static Device[] Read()
    {
        const uint activeOnly=2;
        for(int attempt=0;attempt<3;attempt++) {
            int error=GetDisplayConfigBufferSizes(activeOnly,out uint np,out uint nm);
            if(error!=0) throw new Win32Exception(error);
            if(np>1024 || nm>8192) throw new InvalidOperationException("Display buffer limit");
            var paths=new DisplayPath[np]; var modes=new Mode[nm];
            error=QueryDisplayConfig(activeOnly,ref np,paths,ref nm,modes,IntPtr.Zero);
            if(error==122) continue; // Monitor hotplug changed the required buffers.
            if(error!=0) throw new Win32Exception(error);
            var result=new List<Device>();
            foreach(var p in paths.Take((int)np)) {
                var name=new TargetName { Header=new Header { Type=2,Size=(uint)Marshal.SizeOf<TargetName>(),Adapter=p.Target.Adapter,Id=p.Target.Id },Friendly="",Path="" };
                int named=DisplayConfigGetDeviceInfo(ref name);
                string connector=p.Target.Technology switch { 0=>"VGA",4=>"DVI",5=>"HDMI",6=>"LVDS / internal",10=>"DisplayPort (USB-C not distinguishable)",11=>"eDP / internal",15=>"Miracast / wireless",16=>"Indirect wired",17=>"Indirect virtual",18=>"DisplayPort USB tunnel",0x80000000=>"Internal",_=>"Connection N/A" };
                string resolution=p.Source.Mode<nm && modes[p.Source.Mode].Type==1?$"{modes[p.Source.Mode].Width}x{modes[p.Source.Mode].Height}":"resolution N/A";
                string refresh=p.Target.Refresh.Denominator>0?$"{(double)p.Target.Refresh.Numerator/p.Target.Refresh.Denominator:0.##} Hz":"refresh N/A";
                string id=$"display:{p.Target.Adapter.High}:{p.Target.Adapter.Low}:{p.Target.Id}";
                result.Add(new(id,"Displays",named==0 && name.Friendly.Length>0?name.Friendly:"Monitor",p.Target.Available!=0?"Active":"Unavailable",null,$"{resolution} | {refresh} | {connector}"));
            }
            return result.ToArray();
        }
        throw new InvalidOperationException("Display topology changing");
    }
}
