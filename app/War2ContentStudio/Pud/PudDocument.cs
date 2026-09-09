using System.Text;

namespace War2ContentStudio.Pud;

public sealed class PudDocument
{
    public List<PudSection> Sections { get; set; } = [];

    public PudSection? GetSection(string name) =>
        Sections.FirstOrDefault(s => s.Name == name);

    public byte[] GetSectionData(string name) =>
        GetSection(name)?.Data ?? [];

    public void SetSection(string name, byte[] data)
    {
        var existing = GetSection(name);
        if (existing is null)
        {
            Sections.Add(new PudSection { Name = name, Data = data });
            return;
        }

        existing.Data = data;
    }

    public void ValidateTypeSection()
    {
        var type = GetSectionData(PudSectionNames.Type);
        if (type.Length < 8)
        {
            throw new InvalidDataException("Missing or invalid TYPE section");
        }

        var tag = Encoding.ASCII.GetString(type, 0, 8);
        if (tag != "WAR2 MAP")
        {
            throw new InvalidDataException($"Not a WAR2 MAP file (TYPE={tag})");
        }
    }

    public string Description
    {
        get
        {
            var data = GetSectionData(PudSectionNames.Desc);
            if (data.Length == 0) return string.Empty;
            var nul = Array.IndexOf(data, (byte)0);
            var len = nul >= 0 ? nul : data.Length;
            return Encoding.ASCII.GetString(data, 0, len);
        }
        set
        {
            var bytes = new byte[32];
            var raw = Encoding.ASCII.GetBytes(value ?? string.Empty);
            Array.Copy(raw, bytes, Math.Min(raw.Length, 31));
            SetSection(PudSectionNames.Desc, bytes);
        }
    }

    public int Era
    {
        get
        {
            var erax = GetSectionData(PudSectionNames.Erax);
            if (erax.Length >= 2) return BitConverter.ToUInt16(erax, 0);
            var era = GetSectionData(PudSectionNames.Era);
            return era.Length >= 2 ? BitConverter.ToUInt16(era, 0) : 0;
        }
        set
        {
            var bytes = BitConverter.GetBytes((ushort)value);
            SetSection(PudSectionNames.Era, bytes);
            SetSection(PudSectionNames.Erax, bytes);
        }
    }

    public int MapSize
    {
        get
        {
            var dim = GetSectionData(PudSectionNames.Dim);
            if (dim.Length < 4) return 32;
            return BitConverter.ToUInt16(dim, 2);
        }
        set
        {
            var size = (ushort)value;
            SetSection(PudSectionNames.Dim, BitConverter.GetBytes(size).Concat(BitConverter.GetBytes(size)).ToArray());
        }
    }

    public ushort[] GetTiles()
    {
        var size = MapSize;
        var expected = size * size * 2;
        var data = GetSectionData(PudSectionNames.Mtxm);
        var tiles = new ushort[size * size];
        var copy = Math.Min(expected, data.Length);
        for (var i = 0; i < copy / 2; i++)
        {
            tiles[i] = BitConverter.ToUInt16(data, i * 2);
        }

        return tiles;
    }

    public void SetTiles(ushort[] tiles)
    {
        var size = MapSize;
        if (tiles.Length != size * size)
        {
            throw new ArgumentException($"Expected {size * size} tiles");
        }

        var data = new byte[tiles.Length * 2];
        for (var i = 0; i < tiles.Length; i++)
        {
            var b = BitConverter.GetBytes(tiles[i]);
            data[i * 2] = b[0];
            data[i * 2 + 1] = b[1];
        }

        SetSection(PudSectionNames.Mtxm, data);
    }

    public List<PudUnit> GetUnits()
    {
        var data = GetSectionData(PudSectionNames.Unit);
        var units = new List<PudUnit>();
        for (var i = 0; i + 8 <= data.Length; i += 8)
        {
            units.Add(PudUnit.FromBytes(data, i));
        }

        return units;
    }

    public void SetUnits(IReadOnlyList<PudUnit> units)
    {
        var data = new byte[units.Count * 8];
        for (var i = 0; i < units.Count; i++)
        {
            var b = units[i].ToBytes();
            Buffer.BlockCopy(b, 0, data, i * 8, 8);
        }

        SetSection(PudSectionNames.Unit, data);
    }

    public bool HasUdta => GetSection(PudSectionNames.Udta) is not null;

    public PudDocument Clone()
    {
        return new PudDocument
        {
            Sections = Sections.Select(s => new PudSection
            {
                Name = s.Name,
                Data = s.Data.ToArray(),
            }).ToList(),
        };
    }
}
