using System.Text;

namespace War2ContentStudio.Pud;

public static class PudFactory
{
    public static PudDocument CreateEmpty(int mapSize = 32, int era = 0, string description = "New map")
    {
        var doc = new PudDocument();
        var type = new byte[16];
        Encoding.ASCII.GetBytes("WAR2 MAP").CopyTo(type, 0);
        type[10] = 0x0A;
        type[11] = 0xFF;
        doc.SetSection(PudSectionNames.Type, type);
        doc.SetSection(PudSectionNames.Ver, BitConverter.GetBytes((ushort)0x0013));
        doc.Description = description;
        doc.SetSection(PudSectionNames.Ownr, Enumerable.Repeat((byte)0x03, 16).ToArray());
        doc.Era = era;
        doc.MapSize = mapSize;

        var tileCount = mapSize * mapSize;
        var grassTile = (ushort)0x0050;
        var tiles = Enumerable.Repeat(grassTile, tileCount).ToArray();
        doc.SetTiles(tiles);

        var sqm = new byte[tileCount];
        Array.Fill(sqm, (byte)0x01);
        doc.SetSection(PudSectionNames.Sqm, sqm);

        doc.SetUnits([]);
        doc.SetSection(PudSectionNames.Udta, CreateMinimalUdta());
        doc.SetSection(PudSectionNames.Ugrd, CreateDefaultUgrd());
        doc.SetSection(PudSectionNames.Side, new byte[16]);
        doc.SetSection(PudSectionNames.Aipl, new byte[16]);
        doc.SetSection(PudSectionNames.Alow, PudSettings.CreateDefaultAllowances());
        doc.SetSection(PudSectionNames.Sgld, CreateStartingResources(2000));
        doc.SetSection(PudSectionNames.Slbr, CreateStartingResources(1000));
        doc.SetSection(PudSectionNames.Soil, CreateStartingResources(0));
        return doc;
    }

    private static byte[] CreateStartingResources(ushort amount)
    {
        var data = new byte[32];
        for (var i = 0; i < 8; i++)
        {
            var b = BitConverter.GetBytes(amount);
            data[i * 2] = b[0];
            data[i * 2 + 1] = b[1];
        }

        return data;
    }

    private static byte[] CreateDefaultUgrd()
    {
        var data = new byte[782];
        data[0] = 0x01;
        data[1] = 0x00;
        return data;
    }

    /// <summary>Minimal UDTA blob so the game accepts custom maps (mirrors editor defaults).</summary>
    private static byte[] CreateMinimalUdta()
    {
        // Preserve a conservative default: many maps use 742 byte UDTA for v1.3.
        var data = new byte[742];
        for (var i = 0; i < data.Length; i++)
        {
            data[i] = 0xFF;
        }

        return data;
    }
}
