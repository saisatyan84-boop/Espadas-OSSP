namespace SysmonHost;

// Nullable measurements mean unavailable, never an invented zero.
public record ProcessRow(int Pid, string Name, long? RamBytes, double? CpuSeconds, string? StartId, double SampleSeconds);
public record Cpu(double? Usage, string Name, int LogicalProcessors, double UptimeSeconds);
public record Memory(ulong Total, ulong Available);
public record Network(string Id, string Name, string Kind, string State, string Address, bool Virtual, double? Rx, double? Tx);
public record Drive(string Name, string Kind, ulong Total, ulong Free);
public record Device(string Id, string Category, string Name, string State, double? Battery, string Detail);
public record Sensor(string Id, string Hardware, string Kind, string Name, string Unit, double? Value);
public record SlowData(double SampleSeconds, Device[] Devices, Drive[] Drives, string Status, double? DiskRead=null, double? DiskWrite=null);
public record SensorData(double SampleSeconds, Sensor[] Sensors, string Status);
public record HostData(Cpu Cpu, Memory? Memory, Network[] Networks, SlowData Inventory, SensorData Hardware);
