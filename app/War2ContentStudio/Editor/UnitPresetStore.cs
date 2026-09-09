using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows.Media;

namespace War2ContentStudio.Editor;

public sealed class UnitPresetStats
{
    public int HitPoints { get; set; } = 60;
    public int Armor { get; set; } = 2;
    public int BasicDamage { get; set; } = 6;
    public int PiercingDamage { get; set; } = 3;
    public int SightRange { get; set; } = 4;
    public int AttackRange { get; set; } = 1;
    public int Magic { get; set; }
    public int BuildTime { get; set; } = 60;
    public int GoldCostTenths { get; set; } = 60;
    public int LumberCostTenths { get; set; }
    public int OilCostTenths { get; set; }
    public int Priority { get; set; } = 50;
    public int PointValue { get; set; } = 50;
}

public sealed class UnitPreset
{
    public int Version { get; set; } = 1;
    public string Id { get; set; } = "";
    public string DisplayName { get; set; } = "";
    public int BaseUnitType { get; set; }
    public string BaseUnitLabel { get; set; } = "";
    public string AudioBank { get; set; } = "";
    public UnitPresetStats Stats { get; set; } = new();
    public string Notes { get; set; } = "";
}

public static class UnitPresetStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    public static IEnumerable<string> PresetDirectories()
    {
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        yield return Path.Combine(local, "WC2rAudioTool", "unit-presets");
        yield return Path.Combine(local, "War2VoiceCompare", "unit-presets");
    }

    public static string PresetsDirectory =>
        PresetDirectories().FirstOrDefault(Directory.Exists)
        ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WC2rAudioTool",
            "unit-presets");

    public static IReadOnlyList<UnitPreset> LoadAll()
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var list = new List<UnitPreset>();
        foreach (var dir in PresetDirectories())
        {
            if (!Directory.Exists(dir)) continue;
            foreach (var path in Directory.EnumerateFiles(dir, "*.json").OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    var json = File.ReadAllText(path);
                    var preset = JsonSerializer.Deserialize<UnitPreset>(json, JsonOptions);
                    if (preset is null || string.IsNullOrWhiteSpace(preset.DisplayName)) continue;
                    if (string.IsNullOrWhiteSpace(preset.Id))
                        preset.Id = Path.GetFileNameWithoutExtension(path);
                    if (!seen.Add(preset.Id)) continue;
                    list.Add(preset);
                }
                catch
                {
                    // Skip corrupt presets; palette should still load.
                }
            }
        }

        return list;
    }

    public static IReadOnlyList<PaletteEntry> ToPaletteEntries()
    {
        var color = Color.FromRgb(0x7B, 0xC9, 0x6F);
        return LoadAll()
            .Select(p => new PaletteEntry
            {
                Category = "Custom",
                Name = $"{p.DisplayName} ({p.BaseUnitLabel})",
                UnitType = (byte)Math.Clamp(p.BaseUnitType, 0, 255),
                Swatch = new SolidColorBrush(color),
                PresetId = p.Id,
            })
            .ToList();
    }
}
