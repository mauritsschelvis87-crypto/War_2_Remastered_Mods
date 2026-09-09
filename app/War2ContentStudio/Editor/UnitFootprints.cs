namespace War2ContentStudio.Editor;

/// <summary>Map footprint in tiles (from jcfieldsdev/warcraft2-map-editor defaults).</summary>
public static class UnitFootprints
{
    private static readonly (byte W, byte H)[] Sizes = BuildSizes();

    public static (int Width, int Height) Get(byte unitType)
    {
        var i = unitType;
        if (i < Sizes.Length)
        {
            var (w, h) = Sizes[i];
            if (w > 0 && h > 0) return (w, h);
        }

        return (1, 1);
    }

    public static bool IsBuilding(byte unitType) => unitType is >= 0x3A and <= 0x68;

    private static (byte W, byte H)[] BuildSizes()
    {
        var sizes = new (byte W, byte H)[106];
        for (var i = 0; i < sizes.Length; i++)
        {
            sizes[i] = (1, 1);
        }

        void Set(int id, byte w, byte h) => sizes[id] = (w, h);

        Set(0x3A, 2, 2); Set(0x3B, 2, 2);
        Set(0x3C, 3, 3); Set(0x3D, 3, 3); Set(0x3E, 3, 3); Set(0x3F, 3, 3);
        Set(0x40, 2, 2); Set(0x41, 2, 2);
        Set(0x42, 3, 3); Set(0x43, 3, 3); Set(0x44, 3, 3); Set(0x45, 3, 3);
        Set(0x46, 3, 3); Set(0x47, 3, 3); Set(0x48, 3, 3); Set(0x49, 3, 3);
        Set(0x4A, 4, 4); Set(0x4B, 4, 4);
        Set(0x4C, 3, 3); Set(0x4D, 3, 3); Set(0x4E, 3, 3); Set(0x4F, 3, 3);
        Set(0x50, 3, 3); Set(0x51, 3, 3); Set(0x52, 3, 3); Set(0x53, 3, 3);
        Set(0x54, 3, 3); Set(0x55, 3, 3); Set(0x56, 3, 3); Set(0x57, 3, 3);
        Set(0x58, 4, 4); Set(0x59, 4, 4); Set(0x5A, 4, 4); Set(0x5B, 4, 4);
        Set(0x5C, 3, 3); Set(0x5D, 3, 3);
        Set(0x60, 2, 2); Set(0x61, 2, 2); Set(0x62, 2, 2); Set(0x63, 2, 2);
        Set(0x64, 2, 2); Set(0x65, 4, 4); Set(0x66, 2, 2);
        Set(0x67, 3, 3); Set(0x68, 3, 3);

        return sizes;
    }
}
