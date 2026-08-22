namespace PlayerColorStudio;

public static class ColorBlindPresets
{
    public const string DefaultHint =
        "Pick a preset to load suggested player colors for that type of color blindness. Press Apply to use them in-game.";

    public const string CustomHint =
        "Custom player colors. Edit each slot (or use the pickers), then press Apply to use them in-game.";

    private static readonly Dictionary<string, Preset> All = new(StringComparer.OrdinalIgnoreCase)
    {
        ["deuteranopia"] = new Preset(
            "Deuteranopia",
            "Green-blind (most common): suggested colors avoid red/green pairs and lean on blue, orange, yellow, and magenta.",
            "#0072B2", "#E69F00", "#F0E442", "#CC79A7", "#56B4E9", "#D55E00", "#999999"),
        ["protanopia"] = new Preset(
            "Protanopia",
            "Red-blind: suggested colors emphasize blue, yellow, and cyan so red and green no longer look alike.",
            "#0072B2", "#F0E442", "#009E73", "#CC79A7", "#E69F00", "#56B4E9", "#666666"),
        ["tritanopia"] = new Preset(
            "Tritanopia",
            "Blue-yellow blind: suggested colors avoid blue/yellow confusion and use red, orange, teal, and purple instead.",
            "#D55E00", "#CC79A7", "#009E73", "#E69F00", "#C0392B", "#785EF0", "#555555"),
    };

    public static IReadOnlyList<string> Keys { get; } = All.Keys.ToList();

    public static bool TryGet(string key, out string[] playerHexes, out string hint)
    {
        if (All.TryGetValue(key, out var preset))
        {
            playerHexes = preset.PlayerHexes;
            hint = preset.Hint;
            return true;
        }

        playerHexes = [];
        hint = DefaultHint;
        return false;
    }

    public static string LabelFor(string key)
    {
        if (key.Equals("custom", StringComparison.OrdinalIgnoreCase) ||
            key.Equals("original", StringComparison.OrdinalIgnoreCase))
            return "Custom";
        return All.TryGetValue(key, out var preset) ? preset.Label : key;
    }

    private sealed record Preset(string Label, string Hint, params string[] PlayerHexes);
}
