namespace PlayerColorStudio;

public static class ColorBlindPresets
{
    private static readonly Dictionary<string, Preset> All = new(StringComparer.OrdinalIgnoreCase)
    {
        ["deuteranopia"] = new Preset(
            "ColorBlind.Deuteranopia",
            "ColorBlind.Deuteranopia.Hint",
            "#0072B2", "#E69F00", "#F0E442", "#CC79A7", "#56B4E9", "#D55E00", "#999999"),
        ["protanopia"] = new Preset(
            "ColorBlind.Protanopia",
            "ColorBlind.Protanopia.Hint",
            "#0072B2", "#F0E442", "#009E73", "#CC79A7", "#E69F00", "#56B4E9", "#666666"),
        ["tritanopia"] = new Preset(
            "ColorBlind.Tritanopia",
            "ColorBlind.Tritanopia.Hint",
            "#D55E00", "#CC79A7", "#009E73", "#E69F00", "#C0392B", "#785EF0", "#555555"),
    };

    public static IReadOnlyList<string> Keys { get; } = All.Keys.ToList();

    public static bool TryGet(string key, out string[] playerHexes, out string hint)
    {
        if (All.TryGetValue(key, out var preset))
        {
            playerHexes = preset.PlayerHexes;
            hint = Localization.Get(preset.HintKey);
            return true;
        }

        playerHexes = [];
        hint = Localization.Get("ColorBlind.DefaultHint");
        return false;
    }

    public static string LabelFor(string key)
    {
        if (key.Equals("custom", StringComparison.OrdinalIgnoreCase) ||
            key.Equals("original", StringComparison.OrdinalIgnoreCase))
            return Localization.Get("Btn.Custom");
        return All.TryGetValue(key, out var preset)
            ? Localization.Get(preset.LabelKey)
            : key;
    }

    private sealed record Preset(string LabelKey, string HintKey, params string[] PlayerHexes);
}
