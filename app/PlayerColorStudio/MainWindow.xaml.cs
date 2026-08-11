using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Forms = System.Windows.Forms;
using WpfMessageBox = System.Windows.MessageBox;

namespace PlayerColorStudio;

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private const string DefaultGameRootPath = @"C:\Program Files (x86)\Warcraft II Remastered";
    private const string IncorrectPathStatusMessage =
        "Incorrect path, select your Warcraft II install folder to continue";

    private readonly string _enginePath;
    private readonly string _configPath;
    private readonly string _extraConfigPath;
    private readonly string _settingsPath = string.Empty;
    private readonly string _nativeDir;
    private readonly DispatcherTimer _hookWatchTimer;
    private bool _allyLeaveMarkComputers;
    private bool _allyLeaveMarkHumans;
    private bool _appliedAllyLeaveMarkComputers;
    private bool _appliedAllyLeaveMarkHumans;
    private bool _chatDuringPauseScreen;
    private bool _appliedChatDuringPauseScreen;
    private bool _chatColoredNames;
    private bool _appliedChatColoredNames;
    private bool _appliedDragSelectColorEnabled;
    private bool _hookInjectedForRunningGame;
    private bool _pauseChatInjectedForRunningGame;
    private bool _chatNameColorInjectedForRunningGame;
    private bool _dragSelectInjectedForRunningGame;
    private bool _isApplying;
    private bool _skipNextStatusRefresh;
    private string _activeTab = "colors";
    private string _gameInstallPath = DefaultGameRootPath;
    private string _appliedGameInstallPath = DefaultGameRootPath;
    private System.Windows.Media.Brush _applyButtonBrush = CreateBrush(0x2E, 0xA8, 0x5C);
    private System.Windows.Media.Brush _applyButtonBorderBrush = CreateBrush(0x4C, 0xC3, 0x7A);
    private readonly Dictionary<int, string> _appliedHexByPlayer = new();
    private readonly Dictionary<string, string> _appliedOtherHexByKey = new(StringComparer.OrdinalIgnoreCase);

    public ObservableCollection<ColorCard> Cards { get; } = [];
    public ObservableCollection<ColorCard> OtherCards { get; } = [];
    public ObservableCollection<StatusLineItem> StatusLines { get; } = [];

    public bool AllyLeaveMarkComputers
    {
        get => _allyLeaveMarkComputers;
        set
        {
            if (_allyLeaveMarkComputers == value) return;
            _allyLeaveMarkComputers = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool AllyLeaveMarkHumans
    {
        get => _allyLeaveMarkHumans;
        set
        {
            if (_allyLeaveMarkHumans == value) return;
            _allyLeaveMarkHumans = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    private bool AnyAllyLeaveApplied =>
        _appliedAllyLeaveMarkComputers || _appliedAllyLeaveMarkHumans;

    public bool ChatDuringPauseScreen
    {
        get => _chatDuringPauseScreen;
        set
        {
            if (_chatDuringPauseScreen == value) return;
            _chatDuringPauseScreen = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool ChatColoredNames
    {
        get => _chatColoredNames;
        set
        {
            if (_chatColoredNames == value) return;
            _chatColoredNames = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool IsApplying
    {
        get => _isApplying;
        set
        {
            if (_isApplying == value) return;
            _isApplying = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsApplyEnabled));
            // Restart indeterminate animation when Apply/Restore begins.
            if (value && ApplyProgressBar is not null)
            {
                ApplyProgressBar.IsIndeterminate = false;
                ApplyProgressBar.IsIndeterminate = true;
            }
        }
    }

    public bool IsApplyEnabled => !_isApplying;

    public bool IsApplyVisible => _activeTab is "colors" or "feature" or "bugfixes" or "path";

    public bool IsRestoreVisible => _activeTab == "info";

    public string GameInstallPath
    {
        get => _gameInstallPath;
        set
        {
            var next = value ?? string.Empty;
            if (_gameInstallPath == next) return;
            _gameInstallPath = next;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
    };

    public string AppVersionText
    {
        get
        {
            var version = typeof(MainWindow).Assembly.GetName().Version;
            return version is null
                ? "Version unknown"
                : $"Version {version.Major}.{version.Minor}.{version.Build}";
        }
    }

    public System.Windows.Media.Brush ApplyButtonBrush
    {
        get => _applyButtonBrush;
        set
        {
            _applyButtonBrush = value;
            OnPropertyChanged();
        }
    }

    public System.Windows.Media.Brush ApplyButtonBorderBrush
    {
        get => _applyButtonBorderBrush;
        set
        {
            _applyButtonBorderBrush = value;
            OnPropertyChanged();
        }
    }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

        _hookWatchTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _hookWatchTimer.Tick += (_, _) => UpdateExtraHookStatus(forceInject: false);

        try
        {
            _enginePath = FindEnginePath();
            var modDir = Path.GetDirectoryName(_enginePath)!;
            _configPath = Path.Combine(modDir, "player-colors.json");
            _extraConfigPath = Path.Combine(modDir, "extra-features.json");
            _settingsPath = Path.Combine(modDir, "studio-settings.json");
            _nativeDir = Path.Combine(modDir, "native");
            LoadGameInstallPath(modDir);
            LoadCards();
            CaptureAppliedColors();
            CaptureAppliedUtilColors();
            foreach (var card in Cards)
            {
                card.PropertyChanged += (_, args) =>
                {
                    if (_activeTab != "colors") return;
                    if (args.PropertyName is null or nameof(ColorCard.Hex))
                        RefreshTabStatus();
                };
            }
            WireOtherCards();
            LoadExtraFeatures();
            _hookWatchTimer.Start();
            UpdateExtraHookStatus(forceInject: AnyAllyLeaveApplied || _appliedChatDuringPauseScreen ||
                _appliedChatColoredNames || _appliedDragSelectColorEnabled);
            RefreshTabStatus();
        }
        catch (Exception ex)
        {
            _enginePath = string.Empty;
            _configPath = string.Empty;
            _extraConfigPath = string.Empty;
            _nativeDir = string.Empty;
            SetStatusLines(StatusLine("✕", PendingIconBrush, ex.Message));
            WpfMessageBox.Show(ex.Message, "Quality of Life Modding", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void MainTabs_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        // TabControl bubbles SelectionChanged from child controls — only handle tab switches.
        if (!ReferenceEquals(e.Source, MainTabs)) return;
        if (MainTabs.SelectedItem is not TabItem tab) return;
        _activeTab = tab.Tag as string ?? "colors";
        OnPropertyChanged(nameof(IsApplyVisible));
        OnPropertyChanged(nameof(IsRestoreVisible));
        RefreshTabStatus();
    }

    private void CaptureAppliedColors()
    {
        _appliedHexByPlayer.Clear();
        foreach (var card in Cards)
            _appliedHexByPlayer[card.Player] = ColorCard.NormalizeHex(card.Hex);
    }

    private void WireOtherCards()
    {
        foreach (var card in OtherCards)
            WireUtilCard(card);
    }

    private void WireUtilCard(ColorCard? card)
    {
        if (card is null) return;
        card.PropertyChanged += (_, args) =>
        {
            if (_activeTab != "colors") return;
            if (args.PropertyName is null or nameof(ColorCard.Hex))
                RefreshTabStatus();
        };
    }

    private void CaptureAppliedUtilColors()
    {
        _appliedOtherHexByKey.Clear();
        foreach (var card in OtherCards)
            _appliedOtherHexByKey[card.ConfigKey] = ColorCard.NormalizeHex(card.Hex);
    }

    private bool HasPendingUtilChanges()
    {
        foreach (var card in OtherCards.Where(c => c.IsEnabled))
        {
            if (!_appliedOtherHexByKey.TryGetValue(card.ConfigKey, out var applied))
                return true;
            if (!string.Equals(ColorCard.NormalizeHex(card.Hex), applied, StringComparison.OrdinalIgnoreCase))
                return true;
        }
        return false;
    }
    private bool HasPendingColorChanges()
    {
        foreach (var card in Cards.Where(c => c.IsEnabled))
        {
            var current = ColorCard.NormalizeHex(card.Hex);
            if (!_appliedHexByPlayer.TryGetValue(card.Player, out var applied))
                return true;
            if (!string.Equals(current, applied, StringComparison.OrdinalIgnoreCase))
                return true;
        }
        return HasPendingUtilChanges();
    }

    private bool HasManualAppliedUtilColors() =>
        OtherCards.Any(card =>
            card.IsEnabled &&
            _appliedOtherHexByKey.TryGetValue(card.ConfigKey, out var applied) &&
            !string.Equals(applied, ColorCard.NormalizeHex(card.VanillaHex), StringComparison.OrdinalIgnoreCase));

    private bool HasManualAppliedColors() =>
        Cards.Any(card =>
            card.IsEnabled &&
            _appliedHexByPlayer.TryGetValue(card.Player, out var applied) &&
            !string.Equals(applied, ColorCard.NormalizeHex(card.VanillaHex), StringComparison.OrdinalIgnoreCase))
        || HasManualAppliedUtilColors();

    private static readonly System.Windows.Media.Brush PendingIconBrush = CreateBrush(0xE5, 0x3E, 0x3E);
    private static readonly System.Windows.Media.Brush ReadyIconBrush = CreateBrush(0x2E, 0xC8, 0x5A);
    private static readonly System.Windows.Media.Brush PendingButtonBrush = CreateBrush(0xC4, 0x2B, 0x2B);
    private static readonly System.Windows.Media.Brush PendingButtonBorderBrush = CreateBrush(0xE5, 0x5A, 0x5A);
    private static readonly System.Windows.Media.Brush ReadyButtonBrush = CreateBrush(0x2E, 0xA8, 0x5C);
    private static readonly System.Windows.Media.Brush ReadyButtonBorderBrush = CreateBrush(0x4C, 0xC3, 0x7A);

    private static System.Windows.Media.Brush CreateBrush(byte r, byte g, byte b)
    {
        var brush = new SolidColorBrush(System.Windows.Media.Color.FromRgb(r, g, b));
        brush.Freeze();
        return brush;
    }

    private bool HasPendingMarkGone() =>
        _allyLeaveMarkComputers != _appliedAllyLeaveMarkComputers ||
        _allyLeaveMarkHumans != _appliedAllyLeaveMarkHumans;
    private bool HasPendingChatPause() => _chatDuringPauseScreen != _appliedChatDuringPauseScreen;
    private bool HasPendingChatColoredNames() => _chatColoredNames != _appliedChatColoredNames;
    private bool HasPendingFeatureChanges() => HasPendingMarkGone() || HasPendingChatColoredNames();
    private bool HasPendingBugFixChanges() => HasPendingChatPause();

    private static StatusLineItem StatusLine(string icon, System.Windows.Media.Brush brush, string text) =>
        new(icon, brush, text);

    private void SetStatusLines(params StatusLineItem[] lines)
    {
        StatusLines.Clear();
        foreach (var line in lines)
            StatusLines.Add(line);
    }

    private StatusLineItem BuildColorsLine()
    {
        if (HasPendingColorChanges())
        {
            return StatusLine("✕", PendingIconBrush,
                "For the mod \"player colors\" to take effect, please apply first. Please restart Warcraft II for the mods to take effect");
        }

        return HasManualAppliedColors()
            ? StatusLine("✓", ReadyIconBrush,
                "Manual colors are being used. Please restart Warcraft II Remastered for the mods to take effect.")
            : StatusLine("✓", ReadyIconBrush, "Using the original Warcraft II colors");
    }

    private bool HasPendingChangesForActiveTab() => _activeTab switch
    {
        "colors" => HasPendingColorChanges(),
        "feature" => HasPendingFeatureChanges(),
        "bugfixes" => HasPendingBugFixChanges(),
        "path" => HasPendingPathChanges(),
        _ => false,
    };

    private void RefreshTabStatus()
    {
        if (_isApplying) return;

        var pending = HasPendingChangesForActiveTab();
        ApplyButtonBrush = pending ? PendingButtonBrush : ReadyButtonBrush;
        ApplyButtonBorderBrush = pending ? PendingButtonBorderBrush : ReadyButtonBorderBrush;

        if (_activeTab == "feature")
        {
            if (HasPendingChangesForActiveTab())
            {
                SetStatusLines(StatusLine("✕", PendingIconBrush,
                    "Changes have been made, please apply for the changes to take place."));
            }
            else
            {
                SetStatusLines(StatusLine("✓", ReadyIconBrush,
                    "Mods have been installed. Restart Warcraft II Remastered to take effect"));
            }
            return;
        }

        if (_activeTab == "bugfixes")
        {
            if (HasPendingChangesForActiveTab())
            {
                SetStatusLines(StatusLine("✕", PendingIconBrush,
                    "Changes have been made, please apply for the changes to take place."));
            }
            else
            {
                SetStatusLines(StatusLine("✓", ReadyIconBrush,
                    "Mods have been installed. Restart Warcraft II Remastered to take effect"));
            }
            return;
        }

        if (_activeTab == "path")
        {
            SetStatusLines(BuildPathLine());
            return;
        }

        if (_activeTab == "info")
        {
            SetStatusLines(StatusLine("✓", ReadyIconBrush, AppVersionText));
            return;
        }

        SetStatusLines(BuildColorsLine());
    }

    private static string FindEnginePath()
    {
        var folder = new DirectoryInfo(AppContext.BaseDirectory);
        while (folder is not null)
        {
            var candidate = Path.Combine(folder.FullName, "mod", "Apply-PlayerColors.ps1");
            if (File.Exists(candidate)) return candidate;
            folder = folder.Parent;
        }
        throw new FileNotFoundException("The patch engine was not found. Reinstall Quality of Life Modding.");
    }

    private void LoadGameInstallPath(string modDir)
    {
        var fromSettings = TryReadSavedGameRoot();
        var inferred = TryInferGameRootFromModDir(modDir);
        var chosen = !string.IsNullOrWhiteSpace(fromSettings)
            ? fromSettings!
            : (!string.IsNullOrWhiteSpace(inferred) ? inferred! : DefaultGameRootPath);
        _gameInstallPath = NormalizeGameRoot(chosen);
        _appliedGameInstallPath = _gameInstallPath;
        OnPropertyChanged(nameof(GameInstallPath));
    }

    private string? TryReadSavedGameRoot()
    {
        if (!File.Exists(_settingsPath)) return null;
        try
        {
            var settings = JsonSerializer.Deserialize<StudioSettings>(File.ReadAllText(_settingsPath), JsonOpts);
            return string.IsNullOrWhiteSpace(settings?.GameRootPath) ? null : settings!.GameRootPath.Trim();
        }
        catch
        {
            return null;
        }
    }

    private static string? TryInferGameRootFromModDir(string modDir)
    {
        // ...\GameRoot\x86\Mods\PlayerColorStudio\mod
        var studio = Directory.GetParent(modDir);
        var mods = studio?.Parent;
        var x86 = mods?.Parent;
        var root = x86?.Parent;
        if (root is null) return null;
        return IsValidGameRoot(root.FullName) ? root.FullName : null;
    }

    private static string NormalizeGameRoot(string path)
    {
        var trimmed = path.Trim().TrimEnd('\\', '/');
        return string.IsNullOrWhiteSpace(trimmed) ? DefaultGameRootPath : trimmed;
    }

    private static bool IsValidGameRoot(string path) =>
        !string.IsNullOrWhiteSpace(path) &&
        Directory.Exists(Path.Combine(path, "x86", "Data"));

    private bool HasPendingPathChanges() =>
        !string.Equals(
            NormalizeGameRoot(GameInstallPath),
            NormalizeGameRoot(_appliedGameInstallPath),
            StringComparison.OrdinalIgnoreCase);

    private StatusLineItem BuildPathLine()
    {
        var path = NormalizeGameRoot(GameInstallPath);
        if (HasPendingPathChanges())
        {
            return StatusLine("✕", PendingIconBrush,
                "Path changed — press Apply to save it for color and feature patches.");
        }

        if (!IsValidGameRoot(path))
        {
            return StatusLine("✕", PendingIconBrush,
                "Install folder not found (need an x86\\Data folder). Browse to your Warcraft II Remastered folder.");
        }

        return StatusLine("✓", ReadyIconBrush, $"Using game install: {path}");
    }

    private void ShowIncorrectPathStatus()
    {
        _skipNextStatusRefresh = true;
        SetStatusLines(StatusLine("✕", PendingIconBrush, IncorrectPathStatusMessage));
        ApplyButtonBrush = PendingButtonBrush;
        ApplyButtonBorderBrush = PendingButtonBorderBrush;
    }

    private bool IsCurrentApplyPathValid()
    {
        // Path tab validates what the user typed; other tabs use the last saved path.
        var path = _activeTab == "path"
            ? NormalizeGameRoot(GameInstallPath)
            : NormalizeGameRoot(_appliedGameInstallPath);
        return IsValidGameRoot(path);
    }

    private void SaveGameInstallPath(string path)
    {
        var normalized = NormalizeGameRoot(path);
        if (!IsValidGameRoot(normalized))
        {
            throw new InvalidOperationException(IncorrectPathStatusMessage);
        }

        var settings = new StudioSettings { GameRootPath = normalized };
        File.WriteAllText(_settingsPath, JsonSerializer.Serialize(settings, JsonOpts));
        _gameInstallPath = normalized;
        _appliedGameInstallPath = normalized;
        OnPropertyChanged(nameof(GameInstallPath));
    }

    private void BrowseGamePath_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new Forms.FolderBrowserDialog
        {
            Description = "Select the Warcraft II Remastered install folder",
            UseDescriptionForTitle = true,
            SelectedPath = Directory.Exists(NormalizeGameRoot(GameInstallPath))
                ? NormalizeGameRoot(GameInstallPath)
                : DefaultGameRootPath,
        };
        if (dialog.ShowDialog() != Forms.DialogResult.OK) return;
        GameInstallPath = dialog.SelectedPath;
    }

    private void DefaultGamePath_Click(object sender, RoutedEventArgs e)
    {
        GameInstallPath = DefaultGameRootPath;
    }

    private void GameInstallPath_LostFocus(object sender, RoutedEventArgs e) =>
        RefreshTabStatus();

    private string RequireAppliedGameRoot()
    {
        var path = NormalizeGameRoot(_appliedGameInstallPath);
        if (!IsValidGameRoot(path))
            throw new InvalidOperationException(IncorrectPathStatusMessage);
        return path;
    }

    private (int ExitCode, string Output, string Error) RunEngine(string arguments, bool requireValidGameRoot = true)
    {
        var gameRoot = requireValidGameRoot
            ? RequireAppliedGameRoot()
            : NormalizeGameRoot(_appliedGameInstallPath);
        var start = new ProcessStartInfo("powershell.exe",
            $"-NoProfile -ExecutionPolicy Bypass -File \"{_enginePath}\" {arguments} -GameRootPath \"{gameRoot}\"")
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start PowerShell.");
        var output = process.StandardOutput.ReadToEnd();
        var error = process.StandardError.ReadToEnd();
        process.WaitForExit();
        return (process.ExitCode, output.Trim(), error.Trim());
    }

    private void LoadCards()
    {
        var defaults = ReadDefaultConfig();
        var saved = File.Exists(_configPath)
            ? JsonSerializer.Deserialize<ColorConfig>(File.ReadAllText(_configPath), JsonOpts) ?? defaults
            : defaults;
        if (saved.Players.Count == 0) saved = defaults;

        foreach (var vanilla in defaults.Players.OrderBy(p => p.Player))
        {
            var disabled = vanilla.Player is 8;
            // Player 8 stays locked (shared yellow band). Player 2 patches exclusive 212-215.
            var current = disabled
                ? vanilla.Color
                : (saved.Players.FirstOrDefault(p => p.Player == vanilla.Player)?.Color ?? vanilla.Color);
            var description = disabled
                ? "This player color cannot be changed yet because it is shared with other game elements."
                : "Changes this player's color on the minimap, units/buildings, and the victory/ally bars.";
            Cards.Add(new ColorCard(vanilla.Player, current, vanilla.Color, !disabled, description: description));
        }

        OtherCards.Clear();
        OtherCards.Add(MakeOtherCard(
            defaults.SelectionHighlight, saved.SelectionHighlight, "#00FF00",
            "selectionHighlight", "Self highlight",
            "Changes the glow around your own units and buildings when they are selected, " +
            "the box you drag to select many units at once, and the matching selected dots on the minimap. " +
            "These always use the same color.",
            enabled: true));
        OtherCards.Add(MakeOtherCard(
            defaults.CritterHighlight, saved.CritterHighlight, "#A2A2A6",
            "critterHighlight", "Critter minimap",
            "Changes the minimap color for critters.",
            enabled: true));
    }

    private static ColorCard MakeOtherCard(
        string? vanillaSource,
        string? currentSource,
        string fallback,
        string configKey,
        string name,
        string description,
        bool enabled)
    {
        var vanilla = ColorCard.NormalizeHex(string.IsNullOrWhiteSpace(vanillaSource) ? fallback : vanillaSource!);
        // Locked slots always show vanilla; only Self highlight is editable for now.
        var current = enabled
            ? ColorCard.NormalizeHex(string.IsNullOrWhiteSpace(currentSource) ? vanilla : currentSource!)
            : vanilla;
        return new ColorCard(0, current, vanilla, enabled, name, description, configKey);
    }

    private ColorConfig ReadDefaultConfig()
    {
        var result = RunEngine("-GetDefaultConfig", requireValidGameRoot: false);
        if (result.ExitCode == 0)
        {
            var json = result.Output.Split(["\r\n", "\n"], StringSplitOptions.RemoveEmptyEntries)
                .LastOrDefault(line => line.TrimStart().StartsWith("{", StringComparison.Ordinal));
            if (json is not null)
            {
                var parsed = JsonSerializer.Deserialize<ColorConfig>(json, JsonOpts);
                if (parsed is not null) return parsed;
            }
        }

        // App still opens if the game path is wrong; Path tab can fix it.
        return new ColorConfig
        {
            Players =
            [
                new PlayerColor(1, "#C00000"),
                new PlayerColor(2, "#0094FC"),
                new PlayerColor(3, "#00A800"),
                new PlayerColor(4, "#C04800"),
                new PlayerColor(5, "#A800A8"),
                new PlayerColor(6, "#00A8A8"),
                new PlayerColor(7, "#C0C0C0"),
                new PlayerColor(8, "#FFF759"),
            ],
            SelectionHighlight = "#00FF00",
            EnemySelectionHighlight = "#FF0000",
            AllyHighlight = "#FFFF00",
            CritterHighlight = "#A2A2A6",
            GoldMineHighlight = "#694114",
            OilPatchHighlight = "#FFFBF3",
        };
    }

    private string OtherCurrent(string key, string fallback)
    {
        var card = OtherCards.FirstOrDefault(c =>
            string.Equals(c.ConfigKey, key, StringComparison.OrdinalIgnoreCase));
        return card is null ? fallback : ColorCard.NormalizeHex(card.Hex);
    }

    private ColorConfig BuildColorConfigForColorsApply() => new()
    {
        Players = Cards.Select(card => new PlayerColor(card.Player, card.Hex)).ToList(),
        SelectionHighlight = OtherCurrent("selectionHighlight", "#00FF00"),
        // Keep vanilla for unused highlight slots so Apply script stays stable.
        EnemySelectionHighlight = "#FF0000",
        AllyHighlight = "#FFFF00",
        CritterHighlight = OtherCurrent("critterHighlight", "#A2A2A6"),
        GoldMineHighlight = "#694114",
        OilPatchHighlight = "#FFFBF3",
    };

    private void WriteColorConfigAndApply(ColorConfig config)
    {
        File.WriteAllText(_configPath, JsonSerializer.Serialize(config, JsonOpts));
        var result = RunEngine("-ApplySavedConfigOnly");
        if (result.ExitCode != 0)
        {
            var details = string.Join(Environment.NewLine,
                new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                ? "Apply failed with no error details."
                : details);
        }
    }

    private void WriteExtraFeaturesFile(
        bool markComputers,
        bool markHumans,
        bool chatPause,
        bool chatColoredNames,
        bool dragEnabled,
        string dragHex)
    {
        var extraJson = JsonSerializer.Serialize(new ExtraFeaturesConfig
        {
            AllyLeaveMarkComputers = markComputers,
            AllyLeaveMarkHumans = markHumans,
            // Legacy key: watcher / older builds treat this as "any ally-leave feature".
            AllyLeaveRedNames = markComputers || markHumans,
            ChatDuringPauseScreen = chatPause,
            ChatColoredNames = chatColoredNames,
            DragSelectColorEnabled = dragEnabled,
            DragSelectColor = dragHex,
        }, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(_extraConfigPath, extraJson);

        // Keep the game-install copy in sync so AllyLeaveWatch / Setup installs
        // do not keep stale OFF flags and disable hooks after Studio enables them.
        try
        {
            var installMod = Path.Combine(
                NormalizeGameRoot(_appliedGameInstallPath),
                "x86", "Mods", "PlayerColorStudio", "mod", "extra-features.json");
            if (!string.IsNullOrWhiteSpace(installMod) &&
                !string.Equals(Path.GetFullPath(installMod), Path.GetFullPath(_extraConfigPath),
                    StringComparison.OrdinalIgnoreCase))
            {
                var dir = Path.GetDirectoryName(installMod);
                if (!string.IsNullOrEmpty(dir))
                    Directory.CreateDirectory(dir);
                File.WriteAllText(installMod, extraJson);
            }
        }
        catch { /* best-effort mirror */ }
    }

    private void ChooseColor_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is not ColorCard card) return;
        using var dialog = new Forms.ColorDialog { FullOpen = true, Color = System.Drawing.ColorTranslator.FromHtml(card.Hex) };
        if (dialog.ShowDialog() == Forms.DialogResult.OK)
        {
            card.Hex = $"#{dialog.Color.R:X2}{dialog.Color.G:X2}{dialog.Color.B:X2}";
            RefreshTabStatus();
        }
    }

    private void Hex_LostFocus(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is not ColorCard card) return;
        if (!ColorCard.IsValidHex(card.Hex))
        {
            WpfMessageBox.Show($"{card.Name}: use a six-digit hex color, for example #3B82F6.", "Invalid color",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            card.Hex = card.LastValidHex;
            RefreshTabStatus();
            return;
        }
        card.Hex = card.Hex;
        RefreshTabStatus();
    }

    private void ResetCard_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is not ColorCard card) return;
        card.Reset();
        RefreshTabStatus();
    }

    private void LoadExtraFeatures()
    {
        var markComputers = false;
        var markHumans = false;
        var chatPause = false;
        var chatColored = false;
        const bool dragEnabled = false;

        if (File.Exists(_extraConfigPath))
        {
            var extra = JsonSerializer.Deserialize<ExtraFeaturesConfig>(File.ReadAllText(_extraConfigPath),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            markComputers = extra?.AllyLeaveMarkComputers ?? false;
            markHumans = false; // Human leave feature not ready — keep off.
            // Migrate legacy single toggle → computers (NPC focus).
            if (!markComputers && (extra?.AllyLeaveRedNames ?? false))
                markComputers = true;
            chatPause = extra?.ChatDuringPauseScreen ?? false;
            chatColored = extra?.ChatColoredNames ?? false;
        }

        _allyLeaveMarkComputers = markComputers;
        _appliedAllyLeaveMarkComputers = markComputers;
        _allyLeaveMarkHumans = markHumans;
        _appliedAllyLeaveMarkHumans = markHumans;
        _chatDuringPauseScreen = chatPause;
        _appliedChatDuringPauseScreen = chatPause;
        _chatColoredNames = chatColored;
        _appliedChatColoredNames = chatColored;
        _appliedDragSelectColorEnabled = dragEnabled;
        OnPropertyChanged(nameof(AllyLeaveMarkComputers));
        OnPropertyChanged(nameof(AllyLeaveMarkHumans));
        OnPropertyChanged(nameof(ChatDuringPauseScreen));
        OnPropertyChanged(nameof(ChatColoredNames));

        try
        {
            WriteExtraFeaturesFile(markComputers, markHumans, chatPause, chatColored,
                dragEnabled: false, dragHex: "#00FF00");
        }
        catch { /* best-effort persist normalized flags */ }
    }

    private static bool IsWarcraftIiRunning() =>
        Process.GetProcessesByName("Warcraft II").Length > 0;

    private void UpdateExtraHookStatus(bool forceInject)
    {
        UpdateAllyLeaveHookStatus(forceInject);
        UpdatePauseChatHookStatus(forceInject);
        UpdateChatNameColorHookStatus(forceInject);
        UpdateDragSelectHookStatus(forceInject);
    }

    private void UpdateAllyLeaveHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!AnyAllyLeaveApplied)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncAllyLeaveHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            if (_activeTab is "feature" or "bugfixes") RefreshTabStatus();
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _hookInjectedForRunningGame = false;
            if (_activeTab is "feature" or "bugfixes") RefreshTabStatus();
            return;
        }

        if (_hookInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncAllyLeaveHook(throwOnError: false);
            _hookInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _hookInjectedForRunningGame = false;
        }

        if (_activeTab is "feature" or "bugfixes") RefreshTabStatus();
    }

    private void UpdatePauseChatHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedChatDuringPauseScreen)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncPauseChatHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _pauseChatInjectedForRunningGame = false;
            return;
        }

        if (_pauseChatInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncPauseChatHook(throwOnError: false);
            _pauseChatInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _pauseChatInjectedForRunningGame = false;
        }
    }

    private void SyncAllyLeaveWatch()
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;
        var watch = Path.Combine(_nativeDir, "AllyLeaveWatch.exe");
        if (!File.Exists(watch)) return;

        try
        {
            var anyExtra = AnyAllyLeaveApplied || _appliedChatDuringPauseScreen ||
                _appliedChatColoredNames || _appliedDragSelectColorEnabled;
            var args = anyExtra ? "--install-startup" : "--uninstall-startup";
            var start = new ProcessStartInfo(watch, args)
            {
                WorkingDirectory = _nativeDir,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            using var process = Process.Start(start);
            process?.WaitForExit(5000);

            if (anyExtra)
            {
                // Ensure a watcher instance is running (second start is a no-op via mutex).
                var run = new ProcessStartInfo(watch)
                {
                    WorkingDirectory = _nativeDir,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                Process.Start(run)?.Dispose();
            }
        }
        catch
        {
            // Watcher is best-effort; inject from Studio still works while open.
        }
    }

    private string SyncAllyLeaveHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectAllyLeave.exe");
        var dll = Path.Combine(_nativeDir, "AllyLeaveHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Extra hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            _hookInjectedForRunningGame = false;
            return AnyAllyLeaveApplied
                ? "Ally-leave ON — watcher auto-injects when Warcraft II starts (Studio can close). F11 Alliances."
                : "Ally-leave setting saved.";
        }

        var args = AnyAllyLeaveApplied ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start the Extra hook injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            _hookInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Extra hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _hookInjectedForRunningGame = AnyAllyLeaveApplied;
        return string.IsNullOrWhiteSpace(output) ? "Extra hook updated." : output;
    }

    private string SyncPauseChatHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectPauseChat.exe");
        var dll = Path.Combine(_nativeDir, "PauseChatHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Pause-chat hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            _pauseChatInjectedForRunningGame = false;
            return _appliedChatDuringPauseScreen
                ? "Chat-during-pause ON — watcher auto-injects when Warcraft II starts."
                : "Chat-during-pause setting saved.";
        }

        var args = _appliedChatDuringPauseScreen ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start the pause-chat injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            _pauseChatInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Pause-chat hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _pauseChatInjectedForRunningGame = _appliedChatDuringPauseScreen;
        return string.IsNullOrWhiteSpace(output) ? "Pause-chat hook updated." : output;
    }

    private void UpdateChatNameColorHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedChatColoredNames)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncChatNameColorHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _chatNameColorInjectedForRunningGame = false;
            return;
        }

        if (_chatNameColorInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncChatNameColorHook(throwOnError: false);
            _chatNameColorInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _chatNameColorInjectedForRunningGame = false;
        }
    }

    private string SyncChatNameColorHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectChatNameColor.exe");
        var dll = Path.Combine(_nativeDir, "ChatNameColorHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Chat-name-color hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            _chatNameColorInjectedForRunningGame = false;
            return _appliedChatColoredNames
                ? "Chat name colors ON — watcher auto-injects when Warcraft II starts."
                : "Chat name colors setting saved.";
        }

        var args = _appliedChatColoredNames ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start the chat-name-color injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            _chatNameColorInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Chat-name-color hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _chatNameColorInjectedForRunningGame = _appliedChatColoredNames;
        return string.IsNullOrWhiteSpace(output) ? "Chat-name-color hook updated." : output;
    }

    private void UpdateDragSelectHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedDragSelectColorEnabled)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncDragSelectHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _dragSelectInjectedForRunningGame = false;
            return;
        }

        if (_dragSelectInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncDragSelectHook(throwOnError: false);
            _dragSelectInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _dragSelectInjectedForRunningGame = false;
        }
    }

    private string SyncDragSelectHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectDragSelect.exe");
        var dll = Path.Combine(_nativeDir, "DragSelectHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var message = "Drag-select hook binaries are missing. Rebuild native tools.";
            if (throwOnError) throw new FileNotFoundException(message);
            return message;
        }

        if (!IsWarcraftIiRunning())
        {
            _dragSelectInjectedForRunningGame = false;
            return _appliedDragSelectColorEnabled
                ? "Drag-select hook will inject when Warcraft II starts."
                : "Drag-select hook idle (game not running).";
        }

        var args = _appliedDragSelectColorEnabled ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start InjectDragSelect.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();

        if (process.ExitCode == 2)
        {
            _dragSelectInjectedForRunningGame = false;
            return "Warcraft II is not running.";
        }

        if (process.ExitCode != 0)
        {
            _dragSelectInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Drag-select hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _dragSelectInjectedForRunningGame = _appliedDragSelectColorEnabled;
        return string.IsNullOrWhiteSpace(output) ? "Drag-select hook updated." : output;
    }

    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        if (_isApplying || !IsApplyVisible) return;

        try
        {
            // Commit any hex TextBox still focused so the latest typed value is saved.
            if (Keyboard.FocusedElement is UIElement focused)
                focused.MoveFocus(new TraversalRequest(FocusNavigationDirection.Next));

            IsApplying = true;
            SetStatusLines(StatusLine("", ReadyIconBrush, "Installing mod…"));
            ApplyButtonBrush = ReadyButtonBrush;
            ApplyButtonBorderBrush = ReadyButtonBorderBrush;
            // Let the progress bar paint/animate before blocking work starts.
            await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Render);
            await Task.Yield();

            if (!IsCurrentApplyPathValid())
            {
                ShowIncorrectPathStatus();
                return;
            }

            if (_activeTab == "colors")
        {
            foreach (var card in Cards)
            {
                if (!ColorCard.IsValidHex(card.Hex))
                    throw new InvalidOperationException($"{card.Name} has an invalid color.");
            }
                foreach (var card in OtherCards)
                {
                    if (!ColorCard.IsValidHex(card.Hex))
                        throw new InvalidOperationException($"{card.Name} has an invalid color.");
                }

                var config = BuildColorConfigForColorsApply();
                var markComputers = _appliedAllyLeaveMarkComputers;
                var markHumans = _appliedAllyLeaveMarkHumans;
                var chat = _appliedChatDuringPauseScreen;
                var chatNames = _appliedChatColoredNames;
                SetStatusLines(StatusLine("", ReadyIconBrush, "Writing player colors…"));
                await Task.Run(() =>
                {
                    // Keep drag-select hook off — Self highlight owns shared palette index 250.
                    WriteExtraFeaturesFile(
                        markComputers, markHumans, chat, chatNames,
                        dragEnabled: false,
                        dragHex: "#00FF00");
                    WriteColorConfigAndApply(config);
                });
                CaptureAppliedColors();
                CaptureAppliedUtilColors();
                _appliedDragSelectColorEnabled = false;
                _dragSelectInjectedForRunningGame = false;
                if (_appliedChatColoredNames)
                {
                    SetStatusLines(StatusLine("", ReadyIconBrush, "Updating name colors…"));
                    await Task.Run(() =>
                    {
                        try { SyncChatNameColorHook(throwOnError: false); }
                        catch { /* optional while game closed */ }
                    });
                }
            }
            else if (_activeTab == "feature")
            {
                var markComputers = AllyLeaveMarkComputers;
                var markHumans = false; // Human leave feature not ready.
                var chatPauseEnabled = _appliedChatDuringPauseScreen;
                var chatNamesEnabled = ChatColoredNames;
                SetStatusLines(StatusLine("", ReadyIconBrush, "Saving Feature mod settings…"));
                await Task.Run(() => WriteExtraFeaturesFile(
                    markComputers, markHumans, chatPauseEnabled, chatNamesEnabled,
                    dragEnabled: false,
                    dragHex: "#00FF00"));

                _appliedAllyLeaveMarkComputers = markComputers;
                _appliedAllyLeaveMarkHumans = markHumans;
                _appliedChatColoredNames = chatNamesEnabled;
                _hookInjectedForRunningGame = false;
                _chatNameColorInjectedForRunningGame = false;
                SetStatusLines(StatusLine("", ReadyIconBrush, "Updating hooks…"));
                await Task.Run(() =>
                {
                    SyncAllyLeaveWatch();
                    try { SyncAllyLeaveHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                    try { SyncChatNameColorHook(throwOnError: false); }
                    catch { /* optional while Feature is off / game closed */ }
                    try { SyncDragSelectHook(throwOnError: false); }
                    catch { /* keep drag hook disabled */ }
                });
            }
            else if (_activeTab == "bugfixes")
            {
                var markComputers = _appliedAllyLeaveMarkComputers;
                var markHumans = _appliedAllyLeaveMarkHumans;
                var chatPauseEnabled = ChatDuringPauseScreen;
                var chatNamesEnabled = _appliedChatColoredNames;
                SetStatusLines(StatusLine("", ReadyIconBrush, "Saving Bug fix settings…"));
                await Task.Run(() => WriteExtraFeaturesFile(
                    markComputers, markHumans, chatPauseEnabled, chatNamesEnabled,
                    dragEnabled: false,
                    dragHex: "#00FF00"));

                _appliedChatDuringPauseScreen = chatPauseEnabled;
                _pauseChatInjectedForRunningGame = false;
                SetStatusLines(StatusLine("", ReadyIconBrush, "Updating hooks…"));
                await Task.Run(() =>
                {
                    SyncAllyLeaveWatch();
                    try { SyncPauseChatHook(throwOnError: false); }
                    catch { /* optional while Bug fix is off */ }
                    try { SyncDragSelectHook(throwOnError: false); }
                    catch { /* keep drag hook disabled */ }
                });
            }
            else if (_activeTab == "path")
            {
                SetStatusLines(StatusLine("", ReadyIconBrush, "Saving game install path…"));
                SaveGameInstallPath(GameInstallPath);
            }
        }
        catch (Exception ex)
        {
            if (string.Equals(ex.Message, IncorrectPathStatusMessage, StringComparison.Ordinal) ||
                !IsCurrentApplyPathValid())
            {
                ShowIncorrectPathStatus();
            }
            else
            {
            WpfMessageBox.Show(ex.Message, "Apply failed", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
        finally
        {
            IsApplying = false;
            if (_skipNextStatusRefresh)
                _skipNextStatusRefresh = false;
            else
                RefreshTabStatus();
        }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    private async void Restore_Click(object sender, RoutedEventArgs e)
    {
        if (_isApplying) return;

        var confirm = WpfMessageBox.Show(
            "Restore all modded game files to the local vanilla backup and turn Extra features off?\n\n" +
            "This is the fast restore. You can also use Battle.net Scan and Repair (slower).\n\n" +
            "Restart Warcraft II afterwards.",
            "Restore clean install",
            MessageBoxButton.YesNo,
            MessageBoxImage.Question);
        if (confirm != MessageBoxResult.Yes) return;

        try
        {
            IsApplying = true;
            SetStatusLines(StatusLine("", ReadyIconBrush, "Restoring vanilla files…"));
            await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Render);
            await Task.Yield();

            await Task.Run(() =>
            {
                var result = RunEngine("-RestoreOnly");
                if (result.ExitCode != 0)
                {
                    var details = string.Join(Environment.NewLine,
                        new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
                    throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                        ? "Restore failed with no error details."
                        : details);
                }

                var extraJson = JsonSerializer.Serialize(new ExtraFeaturesConfig
                {
                    AllyLeaveMarkComputers = false,
                    AllyLeaveMarkHumans = false,
                    AllyLeaveRedNames = false,
                    ChatDuringPauseScreen = false,
                    ChatColoredNames = false,
                    DragSelectColorEnabled = false,
                    DragSelectColor = "#00FF00",
                }, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(_extraConfigPath, extraJson);
            });

            _allyLeaveMarkComputers = false;
            _appliedAllyLeaveMarkComputers = false;
            _allyLeaveMarkHumans = false;
            _appliedAllyLeaveMarkHumans = false;
            _chatDuringPauseScreen = false;
            _appliedChatDuringPauseScreen = false;
            _chatColoredNames = false;
            _appliedChatColoredNames = false;
            _appliedDragSelectColorEnabled = false;
            OnPropertyChanged(nameof(AllyLeaveMarkComputers));
            OnPropertyChanged(nameof(AllyLeaveMarkHumans));
            OnPropertyChanged(nameof(ChatDuringPauseScreen));
            OnPropertyChanged(nameof(ChatColoredNames));

            Cards.Clear();
            LoadCards();
            CaptureAppliedColors();
            CaptureAppliedUtilColors();
            foreach (var card in Cards)
            {
                card.PropertyChanged += (_, args) =>
                {
                    if (_activeTab != "colors") return;
                    if (args.PropertyName is null or nameof(ColorCard.Hex))
                        RefreshTabStatus();
                };
            }
            WireOtherCards();

            _hookInjectedForRunningGame = false;
            _pauseChatInjectedForRunningGame = false;
            _chatNameColorInjectedForRunningGame = false;
            _dragSelectInjectedForRunningGame = false;
            SetStatusLines(StatusLine("", ReadyIconBrush, "Updating hooks…"));
            await Task.Run(() =>
            {
                SyncAllyLeaveWatch();
                try { SyncAllyLeaveHook(throwOnError: false); } catch { /* off */ }
                try { SyncPauseChatHook(throwOnError: false); } catch { /* off */ }
                try { SyncChatNameColorHook(throwOnError: false); } catch { /* off */ }
                try { SyncDragSelectHook(throwOnError: false); } catch { /* off */ }
            });

            WpfMessageBox.Show(
                "Vanilla files restored and Extra features turned off.\nRestart Warcraft II Remastered to finish.",
                "Restore complete",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
        }
        catch (Exception ex)
        {
            WpfMessageBox.Show(ex.Message, "Restore failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            IsApplying = false;
            RefreshTabStatus();
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

public sealed class ColorCard : INotifyPropertyChanged
{
    private string _hex;
    public int Player { get; }
    public string Name { get; }
    public string Description { get; }
    public string ConfigKey { get; }
    public bool IsEnabled { get; }
    public string VanillaHex { get; }
    public string LastValidHex { get; private set; }

    public string Hex
    {
        get => _hex;
        set
        {
            var normalized = Normalize(value);
            _hex = normalized;
            if (IsValidHex(normalized)) LastValidHex = normalized;
            OnPropertyChanged();
            OnPropertyChanged(nameof(CurrentBrush));
        }
    }

    public System.Windows.Media.Brush CurrentBrush => ToBrush(IsValidHex(Hex) ? Hex : LastValidHex);
    public System.Windows.Media.Brush VanillaBrush => ToBrush(VanillaHex);

    public ColorCard(
        int player,
        string hex,
        string vanillaHex,
        bool enabled,
        string? name = null,
        string? description = null,
        string? configKey = null)
    {
        Player = player;
        Name = name ?? $"Color player {player}";
        Description = description ?? string.Empty;
        ConfigKey = configKey ?? string.Empty;
        IsEnabled = enabled;
        VanillaHex = Normalize(vanillaHex);
        LastValidHex = IsValidHex(hex) ? Normalize(hex) : VanillaHex;
        _hex = LastValidHex;
    }

    public void Reset() => Hex = VanillaHex;

    public static bool IsValidHex(string? value) =>
        value is not null && System.Text.RegularExpressions.Regex.IsMatch(value.Trim(), "^#?[0-9a-fA-F]{6}$");

    public static string NormalizeHex(string value) =>
        value.Trim().StartsWith('#') ? value.Trim().ToUpperInvariant() : $"#{value.Trim().ToUpperInvariant()}";

    private static string Normalize(string value) => NormalizeHex(value);

    private static System.Windows.Media.Brush ToBrush(string hex) =>
        new SolidColorBrush((System.Windows.Media.Color)System.Windows.Media.ColorConverter.ConvertFromString(hex)!);

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

public sealed class ColorConfig
{
    public List<PlayerColor> Players { get; set; } = [];
    public string? SelectionHighlight { get; set; }
    public string? EnemySelectionHighlight { get; set; }
    public string? AllyHighlight { get; set; }
    public string? CritterHighlight { get; set; }
    public string? GoldMineOilHighlight { get; set; }
    public string? GoldMineHighlight { get; set; }
    public string? OilPatchHighlight { get; set; }
}

public sealed record PlayerColor(int Player, string Color);

public sealed class ExtraFeaturesConfig
{
    public bool AllyLeaveMarkComputers { get; set; }
    public bool AllyLeaveMarkHumans { get; set; }
    /// <summary>Legacy: true when either mark mode is on (watcher / older hooks).</summary>
    public bool AllyLeaveRedNames { get; set; }
    public bool ChatDuringPauseScreen { get; set; }
    public bool ChatColoredNames { get; set; }
    public bool DragSelectColorEnabled { get; set; }
    public string? DragSelectColor { get; set; }
}

public sealed class StudioSettings
{
    public string? GameRootPath { get; set; }
}

public sealed class StatusLineItem
{
    public string Icon { get; }
    public System.Windows.Media.Brush IconBrush { get; }
    public string Text { get; }

    public StatusLineItem(string icon, System.Windows.Media.Brush iconBrush, string text)
    {
        Icon = icon;
        IconBrush = iconBrush;
        Text = text;
    }
}
