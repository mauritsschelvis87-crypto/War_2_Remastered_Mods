using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace PlayerColorStudio;

public static class Localization
{
    public static event Action? Changed;

    public static string CurrentCode { get; private set; } = "en";

    private static List<LanguageOption> _languageOptions = new()
    {
        new("en", "English"),
    };

    public static IReadOnlyList<LanguageOption> LanguageOptions => _languageOptions;

    private static readonly Dictionary<string, Dictionary<string, string>> Tables = new(StringComparer.OrdinalIgnoreCase);
    private static Dictionary<string, string> _active = new();

    public static void Initialize(string? preferredCode = null)
    {
        Tables.Clear();
        _languageOptions = LoadLanguageOptions();

        foreach (var option in _languageOptions)
            LoadTable(option.Code);

        if (Tables.Count == 0)
        {
            Tables["en"] = BuildFallbackEnglish();
            if (_languageOptions.Count == 0)
                _languageOptions = new List<LanguageOption> { new("en", "English") };
        }

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

    private static List<LanguageOption> LoadLanguageOptions()
    {
        foreach (var baseDir in GetLangSearchRoots())
        {
            var path = Path.Combine(baseDir, "languages.json");
            if (!File.Exists(path)) continue;
            try
            {
                var json = File.ReadAllText(path);
                var entries = JsonSerializer.Deserialize<List<LanguageCatalogEntry>>(json,
                    new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
                if (entries is not { Count: > 0 }) continue;

                var options = new List<LanguageOption>();
                foreach (var entry in entries)
                {
                    var code = NormalizeCode(entry.Code);
                    var name = string.IsNullOrWhiteSpace(entry.NativeName) ? code : entry.NativeName.Trim();
                    if (string.IsNullOrWhiteSpace(code)) continue;
                    if (options.Any(o => string.Equals(o.Code, code, StringComparison.OrdinalIgnoreCase)))
                        continue;
                    options.Add(new LanguageOption(code, name));
                }

                if (options.Count > 0)
                    return options;
            }
            catch
            {
                // try next root
            }
        }

        // Fallback if languages.json is missing.
        return new List<LanguageOption>
        {
            new("en", "English"),
            new("nl", "Nederlands"),
            new("de", "Deutsch"),
            new("es", "Español"),
            new("fr", "Français"),
            new("pl", "Polski"),
        };
    }

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
            "de" or "german" or "deutsch" => "de",
            "es" or "spanish" or "español" or "espanol" => "es",
            "fr" or "french" or "français" or "francais" => "fr",
            "pl" or "polish" or "polski" => "pl",
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

    private sealed class LanguageCatalogEntry
    {
        public string? Code { get; set; }
        public string? NativeName { get; set; }
    }
}

public sealed record LanguageOption(string Code, string NativeName);
