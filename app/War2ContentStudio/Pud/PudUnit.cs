namespace War2ContentStudio.Pud;

public sealed class PudUnit
{
    public ushort X { get; set; }
    public ushort Y { get; set; }
    public byte Type { get; set; }
    public byte Owner { get; set; }
    public ushort Ai { get; set; }

    public const byte TypeGoldMine = 0x5C;
    public const byte TypeOilPatch = 0x5D;
    public const byte TypeHumanStart = 0x5E;
    public const byte TypeOrcStart = 0x5F;

    public byte[] ToBytes() =>
    [
        (byte)(X & 0xFF),
        (byte)(X >> 8),
        (byte)(Y & 0xFF),
        (byte)(Y >> 8),
        Type,
        Owner,
        (byte)(Ai & 0xFF),
        (byte)(Ai >> 8),
    ];

    public static PudUnit FromBytes(byte[] data, int offset = 0) => new()
    {
        X = (ushort)(data[offset] | (data[offset + 1] << 8)),
        Y = (ushort)(data[offset + 2] | (data[offset + 3] << 8)),
        Type = data[offset + 4],
        Owner = data[offset + 5],
        Ai = (ushort)(data[offset + 6] | (data[offset + 7] << 8)),
    };

    public PudUnit Clone() => new()
    {
        X = X,
        Y = Y,
        Type = Type,
        Owner = Owner,
        Ai = Ai,
    };
}
