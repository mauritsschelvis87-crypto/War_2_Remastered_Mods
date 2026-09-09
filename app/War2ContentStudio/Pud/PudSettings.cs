namespace War2ContentStudio.Pud;

public sealed class PudPlayerSlot
{
    public int Index { get; init; }
    public byte Controller { get; set; } = 0x03;
    public byte Race { get; set; }
    public ushort Gold { get; set; } = 2000;
    public ushort Lumber { get; set; } = 1000;
    public ushort Oil { get; set; }
    public byte Ai { get; set; }
}

public sealed class PudAllowanceSet
{
    public uint Units { get; set; } = 0xFFFFFFFF;
    public uint SpellsResearch { get; set; } = 0xFFFFFFFF;
    public uint Upgrades { get; set; } = 0xFFFFFFFF;
}

public sealed class PudUpgradeRow
{
    public int Index { get; init; }
    public string Name => Index >= 0 && Index < PudDefinitions.UpgradeNames.Length
        ? PudDefinitions.UpgradeNames[Index]
        : $"Upgrade {Index}";
    public byte BuildTime { get; set; }
    public ushort Gold { get; set; }
    public ushort Lumber { get; set; }
    public ushort Oil { get; set; }
}

public static class PudSettings
{
    private const int AllowancePlayers = 16;
    private const int AllowanceGroups = 6;
    private const int AllowanceBytes = AllowanceGroups * AllowancePlayers * 4;

    public static PudPlayerSlot[] ReadPlayers(PudDocument doc)
    {
        var ownr = doc.GetSectionData(PudSectionNames.Ownr);
        var side = doc.GetSectionData(PudSectionNames.Side);
        var sgld = doc.GetSectionData(PudSectionNames.Sgld);
        var slbr = doc.GetSectionData(PudSectionNames.Slbr);
        var soil = doc.GetSectionData(PudSectionNames.Soil);
        var aipl = doc.GetSectionData(PudSectionNames.Aipl);

        var slots = new PudPlayerSlot[PudDefinitions.PlayerSlotCount];
        for (var i = 0; i < slots.Length; i++)
        {
            slots[i] = new PudPlayerSlot
            {
                Index = i,
                Controller = ownr.Length > i ? ownr[i] : (byte)0x03,
                Race = side.Length > i ? side[i] : (byte)0,
                Gold = ReadWord(sgld, i * 2),
                Lumber = ReadWord(slbr, i * 2),
                Oil = ReadWord(soil, i * 2),
                Ai = aipl.Length > i ? aipl[i] : (byte)0,
            };
        }

        return slots;
    }

    public static void WritePlayers(PudDocument doc, IReadOnlyList<PudPlayerSlot> slots)
    {
        var ownr = EnsureBytes(doc.GetSectionData(PudSectionNames.Ownr), 16, (byte)0x03);
        var side = EnsureBytes(doc.GetSectionData(PudSectionNames.Side), 16, (byte)0);
        var sgld = EnsureBytes(doc.GetSectionData(PudSectionNames.Sgld), 32, (byte)0);
        var slbr = EnsureBytes(doc.GetSectionData(PudSectionNames.Slbr), 32, (byte)0);
        var soil = EnsureBytes(doc.GetSectionData(PudSectionNames.Soil), 32, (byte)0);
        var aipl = EnsureBytes(doc.GetSectionData(PudSectionNames.Aipl), 16, (byte)0);

        for (var i = 0; i < PudDefinitions.PlayerSlotCount && i < slots.Count; i++)
        {
            var s = slots[i];
            ownr[i] = s.Controller;
            side[i] = s.Race;
            WriteWord(sgld, i * 2, s.Gold);
            WriteWord(slbr, i * 2, s.Lumber);
            WriteWord(soil, i * 2, s.Oil);
            aipl[i] = s.Ai;
        }

        doc.SetSection(PudSectionNames.Ownr, ownr);
        doc.SetSection(PudSectionNames.Side, side);
        doc.SetSection(PudSectionNames.Sgld, sgld);
        doc.SetSection(PudSectionNames.Slbr, slbr);
        doc.SetSection(PudSectionNames.Soil, soil);
        doc.SetSection(PudSectionNames.Aipl, aipl);
    }

    public static PudAllowanceSet[] ReadAllowances(PudDocument doc)
    {
        var data = doc.GetSectionData(PudSectionNames.Alow);
        if (data.Length < AllowanceBytes)
        {
            data = CreateDefaultAllowances();
        }

        var sets = new PudAllowanceSet[PudDefinitions.PlayerSlotCount];
        for (var p = 0; p < sets.Length; p++)
        {
            sets[p] = new PudAllowanceSet
            {
                Units = ReadLong(data, p * 4),
                SpellsResearch = ReadLong(data, 128 + p * 4),
                Upgrades = ReadLong(data, 256 + p * 4),
            };
        }

        return sets;
    }

    public static void WriteAllowances(PudDocument doc, IReadOnlyList<PudAllowanceSet> sets)
    {
        var data = EnsureBytes(doc.GetSectionData(PudSectionNames.Alow), AllowanceBytes, (byte)0xFF);
        for (var p = 0; p < PudDefinitions.PlayerSlotCount && p < sets.Count; p++)
        {
            var s = sets[p];
            WriteLong(data, p * 4, s.Units);
            WriteLong(data, 128 + p * 4, s.SpellsResearch);
            WriteLong(data, 256 + p * 4, s.Upgrades);
        }

        doc.SetSection(PudSectionNames.Alow, data);
    }

    public static bool ReadUpgradeUseDefault(PudDocument doc)
    {
        var data = doc.GetSectionData(PudSectionNames.Ugrd);
        return data.Length < 2 || BitConverter.ToUInt16(data, 0) != 0;
    }

    public static void WriteUpgradeUseDefault(PudDocument doc, bool useDefault)
    {
        var data = EnsureUpgradeBlob(doc.GetSectionData(PudSectionNames.Ugrd));
        WriteWord(data, 0, (ushort)(useDefault ? 1 : 0));
        doc.SetSection(PudSectionNames.Ugrd, data);
    }

    public static PudUpgradeRow[] ReadUpgrades(PudDocument doc)
    {
        var data = EnsureUpgradeBlob(doc.GetSectionData(PudSectionNames.Ugrd));
        var rows = new PudUpgradeRow[PudDefinitions.UpgradeCount];
        for (var i = 0; i < rows.Length; i++)
        {
            rows[i] = new PudUpgradeRow
            {
                Index = i,
                BuildTime = data.Length > 2 + i ? data[2 + i] : (byte)0,
                Gold = ReadWord(data, 54 + i * 2),
                Lumber = ReadWord(data, 158 + i * 2),
                Oil = ReadWord(data, 262 + i * 2),
            };
        }

        return rows;
    }

    public static void WriteUpgrades(PudDocument doc, IReadOnlyList<PudUpgradeRow> rows, bool useDefault)
    {
        var data = EnsureUpgradeBlob(doc.GetSectionData(PudSectionNames.Ugrd));
        WriteWord(data, 0, (ushort)(useDefault ? 1 : 0));
        for (var i = 0; i < PudDefinitions.UpgradeCount && i < rows.Count; i++)
        {
            var row = rows[i];
            data[2 + i] = row.BuildTime;
            WriteWord(data, 54 + i * 2, row.Gold);
            WriteWord(data, 158 + i * 2, row.Lumber);
            WriteWord(data, 262 + i * 2, row.Oil);
        }

        doc.SetSection(PudSectionNames.Ugrd, data);
    }

    public static byte[] CreateDefaultAllowances()
    {
        var data = new byte[AllowanceBytes];
        for (var g = 0; g < AllowanceGroups; g++)
        {
            for (var p = 0; p < AllowancePlayers; p++)
            {
                WriteLong(data, (g * AllowancePlayers + p) * 4, 0xFFFFFFFF);
            }
        }

        return data;
    }

    private static byte[] EnsureUpgradeBlob(byte[] data)
    {
        const int size = 782;
        if (data.Length >= size) return data;
        var buf = new byte[size];
        if (data.Length > 0) Buffer.BlockCopy(data, 0, buf, 0, data.Length);
        WriteWord(buf, 0, 1);
        return buf;
    }

    private static byte[] EnsureBytes(byte[] data, int length, byte fill)
    {
        if (data.Length >= length) return data.ToArray();
        var buf = new byte[length];
        if (data.Length > 0) Buffer.BlockCopy(data, 0, buf, 0, data.Length);
        Array.Fill(buf, fill, data.Length, length - data.Length);
        return buf;
    }

    private static ushort ReadWord(byte[] data, int offset) =>
        offset + 2 <= data.Length ? BitConverter.ToUInt16(data, offset) : (ushort)0;

    private static void WriteWord(byte[] data, int offset, ushort value)
    {
        if (offset + 2 > data.Length) return;
        var b = BitConverter.GetBytes(value);
        data[offset] = b[0];
        data[offset + 1] = b[1];
    }

    private static uint ReadLong(byte[] data, int offset) =>
        offset + 4 <= data.Length ? BitConverter.ToUInt32(data, offset) : 0xFFFFFFFF;

    private static void WriteLong(byte[] data, int offset, uint value)
    {
        if (offset + 4 > data.Length) return;
        var b = BitConverter.GetBytes(value);
        Buffer.BlockCopy(b, 0, data, offset, 4);
    }

    public static bool GetBit(uint mask, int bit) => (mask & (1u << bit)) != 0;

    public static uint SetBit(uint mask, int bit, bool enabled)
    {
        if (enabled) return mask | (1u << bit);
        return mask & ~(1u << bit);
    }
}
