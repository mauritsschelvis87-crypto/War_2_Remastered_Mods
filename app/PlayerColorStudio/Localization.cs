using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace PlayerColorStudio;

public static class Localization
{
    public static event Action? Changed;

    public static string CurrentCode { get; private set; } = "en";

    public static IReadOnlyList<LanguageOption> LanguageOptions { get; } = new[]
    {
        new LanguageOption("en", "English"),
        new LanguageOption("nl", "Nederlands"),
        new LanguageOption("fr", "Français"),
        new LanguageOption("de", "Deutsch"),
        new LanguageOption("pl", "Polski"),
        new LanguageOption("es", "Español"),
    };

    private static readonly Dictionary<string, Dictionary<string, string>> Tables = new(StringComparer.OrdinalIgnoreCase);
    private static Dictionary<string, string> _active = new();

    public static void Initialize(string? preferredCode = null)
    {
        Tables.Clear();
        foreach (var option in LanguageOptions)
            LoadTable(option.Code);

        if (Tables.Count == 0)
            Tables["en"] = BuildFallbackEnglish();

        TrySetLanguage(preferredCode ?? "en");
    }

    public static bool TrySetLanguage(string? code)
    {
        var normalized = NormalizeCode(code);
        if (!Tables.TryGetValue(normalized, out var table))
            normalized = "en";
        if (!Tables.TryGetValue(normalized, out table))
            return false;

        CurrentCode = normalized;
        _active = table;
        Changed?.Invoke();
        return true;
    }

    public static string Get(string key)
    {
        if (_active.TryGetValue(key, out var value)) return value;
        if (Tables.TryGetValue("en", out var en) && en.TryGetValue(key, out var english)) return english;
        return key;
    }

    public static string Format(string key, params object[] args) =>
        string.Format(Get(key), args);

    private static void LoadTable(string code)
    {
        foreach (var baseDir in GetLangSearchRoots())
        {
            var path = Path.Combine(baseDir, $"{code}.json");
            if (!File.Exists(path)) continue;
            try
            {
                var json = File.ReadAllText(path);
                var dict = JsonSerializer.Deserialize<Dictionary<string, string>>(json);
                if (dict is { Count: > 0 })
                {
                    Tables[code] = dict;
                    return;
                }
            }
            catch
            {
                // try next root
            }
        }
    }

    private static IEnumerable<string> GetLangSearchRoots()
    {
        var exeDir = AppContext.BaseDirectory;
        if (!string.IsNullOrWhiteSpace(exeDir))
            yield return Path.Combine(exeDir, "lang");

        var projectLang = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "lang"));
        if (Directory.Exists(projectLang))
            yield return projectLang;
    }

    private static string NormalizeCode(string? code)
    {
        if (string.IsNullOrWhiteSpace(code)) return "en";
        var c = code.Trim().ToLowerInvariant();
        return c switch
        {
            "en" or "eng" or "english" => "en",
            "nl" or "dutch" or "nederlands" => "nl",
            "fr" or "french" or "français" or "francais" => "fr",
            "de" or "german" or "deutsch" => "de",
            "pl" or "polish" or "polski" => "pl",
            "es" or "spanish" or "español" or "espanol" => "es",
            _ => c.Length >= 2 ? c[..2] : "en",
        };
    }

    private static Dictionary<string, string> BuildFallbackEnglish() => new()
    {
        ["App.Title"] = "Warcraft II Remastered — Quality of Life Mods",
        ["Tab.PlayerColors"] = "Player colors",
        ["Tab.FeatureMods"] = "Feature mods",
        ["Tab.BugFixes"] = "Bug fixes",
        ["Tab.Network"] = "Network",
        ["Tab.Settings"] = "Settings",
        ["Tab.About"] = "About",
        ["Tab.Paths"] = "Paths",
        ["Btn.Apply"] = "Apply",
        ["Btn.Close"] = "Close",
        ["Settings.Language"] = "Language",
        ["Settings.Language.Help"] = "Choose the language for this app. Your choice is saved automatically.",
    };
}

public sealed record LanguageOption(string Code, string NativeName);
