using System.Windows.Media;

namespace War2ContentStudio.Editor;

public sealed class PaletteEntry
{
    public required string Category { get; init; }
    public required string Name { get; init; }
    public ushort? TerrainTile { get; init; }
    public byte? UnitType { get; init; }
    public string? PresetId { get; init; }
    public Brush Swatch { get; init; } = Brushes.Gray;

    public string Detail => TerrainTile is { } t
        ? $"0x{t:X4}"
        : UnitType is { } u
            ? $"0x{u:X2}"
            : string.Empty;

    public bool IsTerrain => TerrainTile.HasValue;
}

public static class PudCatalog
{
    public static IReadOnlyList<PaletteEntry> TerrainEntries { get; } = BuildTerrain();
    public static IReadOnlyList<PaletteEntry> UnitEntries { get; } = BuildUnits();

    public static IReadOnlyList<PaletteEntry> AllUnitEntries()
    {
        var custom = UnitPresetStore.ToPaletteEntries();
        if (custom.Count == 0) return UnitEntries;
        return custom.Concat(UnitEntries).ToList();
    }

    private static PaletteEntry Terrain(string category, ushort tile, string name, Color color) =>
        new()
        {
            Category = category,
            Name = name,
            TerrainTile = tile,
            Swatch = new SolidColorBrush(color),
        };

    private static PaletteEntry Unit(string category, byte type, string name, Color color) =>
        new()
        {
            Category = category,
            Name = name,
            UnitType = type,
            Swatch = new SolidColorBrush(color),
        };

    private static IReadOnlyList<PaletteEntry> BuildTerrain()
    {
        var list = new List<PaletteEntry>();

        void Solid(string cat, ushort baseTile, string label, Color color)
        {
            list.Add(Terrain(cat, baseTile, $"{label} (solid)", color));
        }

        void Transitions(string cat, ushort prefix, string label, Color color)
        {
            // Common autotile shapes from pudspec Appendix D (filled + clear variants).
            ushort[] shapes =
            [
                0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
                0x9, 0xA, 0xB, 0xC, 0xD,
            ];
            foreach (var shape in shapes)
            {
                var tile = (ushort)(prefix | shape);
                list.Add(Terrain(cat, tile, $"{label} #{shape:X1}", color));
            }
        }

        Solid("Water", 0x0010, "Light water", Color.FromRgb(0x44, 0x88, 0xCC));
        Solid("Water", 0x0020, "Dark water", Color.FromRgb(0x33, 0x66, 0xAA));
        Transitions("Water", 0x0100, "Dark water / water", Color.FromRgb(0x3A, 0x78, 0xBB));

        Solid("Coast", 0x0030, "Light coast", Color.FromRgb(0xC8, 0xB0, 0x80));
        Solid("Coast", 0x0040, "Dark coast", Color.FromRgb(0xA0, 0x80, 0x60));
        Transitions("Coast", 0x0200, "Water / coast", Color.FromRgb(0x55, 0x99, 0xBB));
        Transitions("Coast", 0x0300, "Dark coast / coast", Color.FromRgb(0x88, 0x70, 0x50));

        Solid("Grass", 0x0050, "Light grass", Color.FromRgb(0x44, 0xAA, 0x44));
        Solid("Grass", 0x0060, "Dark grass", Color.FromRgb(0x33, 0x88, 0x33));
        Transitions("Grass", 0x0500, "Coast / grass", Color.FromRgb(0x66, 0x99, 0x55));
        Transitions("Grass", 0x0600, "Dark grass / grass", Color.FromRgb(0x3A, 0x99, 0x3A));

        Solid("Forest", 0x0070, "Forest", Color.FromRgb(0x22, 0x66, 0x22));
        Transitions("Forest", 0x0700, "Forest / grass", Color.FromRgb(0x2A, 0x77, 0x2A));

        Solid("Mountains", 0x0080, "Mountains", Color.FromRgb(0x88, 0x88, 0x88));
        Transitions("Mountains", 0x0400, "Mountains / coast", Color.FromRgb(0x77, 0x77, 0x66));

        Solid("Human walls", 0x0090, "Human wall", Color.FromRgb(0x99, 0x99, 0xAA));
        Solid("Human walls", 0x00B0, "Human wall (alt)", Color.FromRgb(0x88, 0x88, 0x99));
        Transitions("Human walls", 0x0800, "Human wall / grass", Color.FromRgb(0x77, 0x88, 0x77));

        Solid("Orc walls", 0x00A0, "Orc wall", Color.FromRgb(0x66, 0x44, 0x33));
        Solid("Orc walls", 0x00C0, "Orc wall (alt)", Color.FromRgb(0x55, 0x33, 0x22));
        Transitions("Orc walls", 0x0900, "Orc wall / grass", Color.FromRgb(0x66, 0x55, 0x44));

        // Black Plague tiles (no swamp counterpart — pudspec).
        list.Add(Terrain("Special", 0x003A, "Coast (plague A)", Color.FromRgb(0xAA, 0x88, 0x66)));
        list.Add(Terrain("Special", 0x003B, "Coast (plague B)", Color.FromRgb(0x99, 0x77, 0x55)));
        list.Add(Terrain("Special", 0x004A, "Dark coast (plague A)", Color.FromRgb(0x88, 0x66, 0x44)));
        list.Add(Terrain("Special", 0x004B, "Dark coast (plague B)", Color.FromRgb(0x77, 0x55, 0x33)));

        return list;
    }

    private static IReadOnlyList<PaletteEntry> BuildUnits()
    {
        var human = Color.FromRgb(0x55, 0x99, 0xFF);
        var orc = Color.FromRgb(0xFF, 0x66, 0x33);
        var hero = Color.FromRgb(0xFF, 0xCC, 0x33);
        var ship = Color.FromRgb(0x66, 0xCC, 0xCC);
        var air = Color.FromRgb(0xCC, 0x99, 0xFF);
        var neutral = Color.FromRgb(0xAA, 0xAA, 0xAA);
        var building = Color.FromRgb(0xBB, 0x88, 0x55);
        var special = Color.FromRgb(0xFF, 0xFF, 0x66);

        PaletteEntry U(string cat, byte id, string name, Color color) => Unit(cat, id, name, color);

        return
        [
            // Human units
            U("Human units", 0x00, "Footman", human),
            U("Human units", 0x02, "Peasant", human),
            U("Human units", 0x04, "Ballista", human),
            U("Human units", 0x06, "Knight", human),
            U("Human units", 0x08, "Archer", human),
            U("Human units", 0x0A, "Mage", human),
            U("Human units", 0x0C, "Paladin", human),
            U("Human units", 0x0E, "Dwarves", human),
            U("Human units", 0x10, "Attack Peasant", human),
            U("Human units", 0x12, "Ranger", human),

            // Orc units
            U("Orc units", 0x01, "Grunt", orc),
            U("Orc units", 0x03, "Peon", orc),
            U("Orc units", 0x05, "Catapult", orc),
            U("Orc units", 0x07, "Ogre", orc),
            U("Orc units", 0x09, "Axethrower", orc),
            U("Orc units", 0x0B, "Death Knight", orc),
            U("Orc units", 0x0D, "Ogre-Mage", orc),
            U("Orc units", 0x0F, "Goblin Sapper", orc),
            U("Orc units", 0x11, "Attack Peon", orc),
            U("Orc units", 0x13, "Berserker", orc),

            // Heroes
            U("Heroes", 0x14, "Alleria", hero),
            U("Heroes", 0x15, "Teron Gorefiend", hero),
            U("Heroes", 0x16, "Kurdan & Sky'ree", hero),
            U("Heroes", 0x17, "Dentarg", hero),
            U("Heroes", 0x18, "Khadgar", hero),
            U("Heroes", 0x19, "Grom Hellscream", hero),
            U("Heroes", 0x23, "Deathwing", hero),
            U("Heroes", 0x2C, "Turalyon", hero),
            U("Heroes", 0x2E, "Danath", hero),
            U("Heroes", 0x2F, "Korgath Bladefist", hero),
            U("Heroes", 0x31, "Cho'gall", hero),
            U("Heroes", 0x32, "Lothar", hero),
            U("Heroes", 0x33, "Gul'dan", hero),
            U("Heroes", 0x34, "Uther Lightbringer", hero),
            U("Heroes", 0x35, "Zul'jin", hero),

            // Ships
            U("Ships", 0x1A, "Human Tanker", ship),
            U("Ships", 0x1B, "Orc Tanker", ship),
            U("Ships", 0x1C, "Human Transport", ship),
            U("Ships", 0x1D, "Orc Transport", ship),
            U("Ships", 0x1E, "Elven Destroyer", ship),
            U("Ships", 0x1F, "Troll Destroyer", ship),
            U("Ships", 0x20, "Battleship", ship),
            U("Ships", 0x21, "Juggernaught", ship),
            U("Ships", 0x26, "Gnomish Submarine", ship),
            U("Ships", 0x27, "Giant Turtle", ship),

            // Air
            U("Air units", 0x28, "Flying Machine", air),
            U("Air units", 0x29, "Goblin Zeppelin", air),
            U("Air units", 0x2A, "Gryphon Rider", air),
            U("Air units", 0x2B, "Dragon", air),
            U("Air units", 0x2D, "Eye of Kilrogg", air),

            // Neutral
            U("Neutral", 0x37, "Skeleton", neutral),
            U("Neutral", 0x38, "Daemon", neutral),
            U("Neutral", 0x39, "Critter", neutral),

            // Human buildings
            U("Human buildings", 0x3A, "Farm", building),
            U("Human buildings", 0x3C, "Barracks", building),
            U("Human buildings", 0x3E, "Church", building),
            U("Human buildings", 0x40, "Scout Tower", building),
            U("Human buildings", 0x42, "Stables", building),
            U("Human buildings", 0x44, "Gnomish Inventor", building),
            U("Human buildings", 0x46, "Gryphon Aviary", building),
            U("Human buildings", 0x48, "Shipyard", building),
            U("Human buildings", 0x4A, "Town Hall", building),
            U("Human buildings", 0x4C, "Lumber Mill", building),
            U("Human buildings", 0x4E, "Foundry", building),
            U("Human buildings", 0x50, "Mage Tower", building),
            U("Human buildings", 0x52, "Blacksmith", building),
            U("Human buildings", 0x54, "Refinery", building),
            U("Human buildings", 0x56, "Oil Platform", building),
            U("Human buildings", 0x58, "Keep", building),
            U("Human buildings", 0x5A, "Castle", building),
            U("Human buildings", 0x60, "Guard Tower", building),
            U("Human buildings", 0x62, "Cannon Tower", building),
            U("Human buildings", 0x67, "Wall", building),

            // Orc buildings
            U("Orc buildings", 0x3B, "Pig Farm", building),
            U("Orc buildings", 0x3D, "Barracks", building),
            U("Orc buildings", 0x3F, "Altar of Storms", building),
            U("Orc buildings", 0x41, "Scout Tower", building),
            U("Orc buildings", 0x43, "Ogre Mound", building),
            U("Orc buildings", 0x45, "Goblin Alchemist", building),
            U("Orc buildings", 0x47, "Dragon Roost", building),
            U("Orc buildings", 0x49, "Shipyard", building),
            U("Orc buildings", 0x4B, "Great Hall", building),
            U("Orc buildings", 0x4D, "Lumber Mill", building),
            U("Orc buildings", 0x4F, "Foundry", building),
            U("Orc buildings", 0x51, "Temple of the Damned", building),
            U("Orc buildings", 0x53, "Blacksmith", building),
            U("Orc buildings", 0x55, "Refinery", building),
            U("Orc buildings", 0x57, "Oil Platform", building),
            U("Orc buildings", 0x59, "Stronghold", building),
            U("Orc buildings", 0x5B, "Fortress", building),
            U("Orc buildings", 0x61, "Guard Tower", building),
            U("Orc buildings", 0x63, "Cannon Tower", building),
            U("Orc buildings", 0x68, "Wall", building),

            // Special / map objects
            U("Special", 0x5C, "Gold Mine", special),
            U("Special", 0x5D, "Oil Patch", special),
            U("Special", 0x5E, "Human Start Location", special),
            U("Special", 0x5F, "Orc Start Location", special),
            U("Special", 0x64, "Circle of Power", special),
            U("Special", 0x65, "Dark Portal", special),
            U("Special", 0x66, "Runestone", special),
        ];
    }

    public static Color ColorForTile(ushort tile)
    {
        var prefix = tile & 0xFF00;
        var nibble = (tile >> 4) & 0xF;
        if (tile < 0x0100)
        {
            return nibble switch
            {
                1 => Color.FromRgb(0x44, 0x88, 0xCC),
                2 => Color.FromRgb(0x33, 0x66, 0xAA),
                3 => Color.FromRgb(0xC8, 0xA0, 0x70),
                4 => Color.FromRgb(0xA0, 0x78, 0x50),
                5 => Color.FromRgb(0x44, 0xAA, 0x44),
                6 => Color.FromRgb(0x33, 0x88, 0x33),
                7 => Color.FromRgb(0x22, 0x66, 0x22),
                8 => Color.FromRgb(0x88, 0x88, 0x88),
                9 => Color.FromRgb(0x99, 0x99, 0xAA),
                0xA => Color.FromRgb(0x66, 0x44, 0x33),
                0xB => Color.FromRgb(0x88, 0x88, 0x99),
                0xC => Color.FromRgb(0x55, 0x33, 0x22),
                _ => Color.FromRgb(0x55, 0x55, 0x55),
            };
        }

        return prefix switch
        {
            0x0100 => Color.FromRgb(0x3A, 0x78, 0xBB),
            0x0200 => Color.FromRgb(0x55, 0x99, 0xBB),
            0x0300 => Color.FromRgb(0x88, 0x70, 0x50),
            0x0400 => Color.FromRgb(0x77, 0x77, 0x66),
            0x0500 => Color.FromRgb(0x66, 0x99, 0x55),
            0x0600 => Color.FromRgb(0x3A, 0x99, 0x3A),
            0x0700 => Color.FromRgb(0x2A, 0x77, 0x2A),
            0x0800 => Color.FromRgb(0x77, 0x88, 0x77),
            0x0900 => Color.FromRgb(0x66, 0x55, 0x44),
            _ => Color.FromRgb(0x66, 0x66, 0x66),
        };
    }
}
