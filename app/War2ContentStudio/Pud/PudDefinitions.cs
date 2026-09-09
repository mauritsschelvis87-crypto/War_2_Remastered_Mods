namespace War2ContentStudio.Pud;

public static class PudDefinitions
{
    public static readonly (byte Value, string Label)[] Controllers =
    [
        (0x02, "Passive computer"),
        (0x03, "Nobody"),
        (0x04, "Computer"),
        (0x05, "Human"),
        (0x06, "Rescue (passive)"),
        (0x07, "Rescue (active)"),
    ];

    public static readonly (byte Value, string Label)[] Races =
    [
        (0x00, "Human"),
        (0x01, "Orc"),
        (0x02, "Neutral"),
    ];

    public static readonly (byte Value, string Label)[] AiTypes =
    [
        (0x00, "Land attack"),
        (0x01, "Passive"),
        (0x02, "Orc AI 3"),
        (0x03, "Human AI 4"),
        (0x04, "Orc AI 4"),
        (0x05, "Human AI 5"),
        (0x06, "Orc AI 5"),
        (0x07, "Human AI 6"),
        (0x08, "Orc AI 6"),
        (0x09, "Human AI 7"),
        (0x0A, "Orc AI 7"),
        (0x0B, "Human AI 8"),
        (0x0C, "Orc AI 8"),
        (0x0D, "Human AI 9"),
        (0x0E, "Orc AI 9"),
        (0x0F, "Human AI 10"),
        (0x10, "Orc AI 10"),
        (0x11, "Human AI 11"),
        (0x12, "Orc AI 11"),
        (0x13, "Human AI 12"),
        (0x14, "Orc AI 12"),
        (0x15, "Human AI 13"),
        (0x16, "Orc AI 13"),
        (0x17, "Human AI 14 (Orange)"),
        (0x18, "Orc AI 14 (Blue)"),
        (0x19, "Sea attack"),
        (0x1A, "Air attack"),
    ];

    /// <summary>ALOW unit/building bit order (32 bits).</summary>
    public static readonly string[] UnitAllowanceBits =
    [
        "Footman / Grunt", "Peasant / Peon", "Ballista / Catapult", "Knight / Ogre",
        "Archer / Axethrower", "Mage / Death Knight", "Tanker", "Destroyer",
        "Transport", "Battleship / Juggernaught", "Submarine / Turtle", "Flying machine / Zeppelin",
        "Gryphon / Dragon", "(unused)", "Demo squad / Sapper", "Aviary / Roost",
        "Farm", "Barracks", "Lumber mill", "Stables / Ogre mound",
        "Mage tower / Temple", "Foundry", "Refinery", "Inventor / Alchemist",
        "Church / Altar of storms", "Scout tower", "Town hall / Great hall", "Keep / Stronghold",
        "Castle / Fortress", "Blacksmith", "Shipyard", "(unused)",
    ];

    /// <summary>ALOW spell bit order.</summary>
    public static readonly string[] SpellAllowanceBits =
    [
        "Holy vision", "Healing", "(unused)", "Exorcism", "Flame shield", "Fireball",
        "Slow", "Invisibility", "Polymorph", "Blizzard", "Eye of Kilrogg", "Bloodlust",
        "(unused)", "Raise dead", "Death coil", "Whirlwind", "Haste", "Unholy armor",
        "Runes", "Death and decay",
    ];

    /// <summary>UGRD / ALOW upgrade indices (Appendix B).</summary>
    public static readonly string[] UpgradeNames =
    [
        "Sword 1", "Sword 2", "Axe 1", "Axe 2", "Arrow 1", "Arrow 2", "Spear 1", "Spear 2",
        "Human shield 1", "Human shield 2", "Orc shield 1", "Orc shield 2",
        "Human ship cannon 1", "Human ship cannon 2", "Orc ship cannon 1", "Orc ship cannon 2",
        "Human ship armor 1", "Human ship armor 2", "Orc ship armor 1", "Orc ship armor 2",
        "Catapult 1", "Catapult 2", "Ballista 1", "Ballista 2",
        "Train rangers", "Longbow", "Ranger scouting", "Ranger marksmanship",
        "Train berserkers", "Lighter axes", "Berserker scouting", "Berserker regeneration",
        "Train ogre-mages", "Train paladins", "Holy vision", "Healing", "Exorcism",
        "Flame shield", "Fireball", "Slow", "Invisibility", "Polymorph", "Blizzard",
        "Eye of Kilrogg", "Bloodlust", "Raise dead", "Death coil", "Whirlwind", "Haste",
        "Unholy armor", "Runes", "Death and decay",
    ];

    public const int PlayerSlotCount = 8;
    public const int UpgradeCount = 52;
}
