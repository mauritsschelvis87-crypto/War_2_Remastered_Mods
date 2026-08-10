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
    private readonly string _enginePath;
    private readonly string _configPath;
    private readonly string _extraConfigPath;
    private readonly string _nativeDir;
    private readonly DispatcherTimer _hookWatchTimer;
    private bool _allyLeaveRedNames;
    private bool _appliedAllyLeaveRedNames;
    private bool _chatDuringPauseScreen;
    private bool _appliedChatDuringPauseScreen;
    private bool _hookInjectedForRunningGame;
    private bool _pauseChatInjectedForRunningGame;
    private bool _isApplying;
    private string _activeTab = "colors";
    private System.Windows.Media.Brush _applyButtonBrush = CreateBrush(0x2E, 0xA8, 0x5C);
    private System.Windows.Media.Brush _applyButtonBorderBrush = CreateBrush(0x4C, 0xC3, 0x7A);
    private readonly Dictionary<int, string> _appliedHexByPlayer = new();

    public ObservableCollection<ColorCard> Cards { get; } = [];
    public ObservableCollection<StatusLineItem> StatusLines { get; } = [];

    public bool AllyLeaveRedNames
    {
        get => _allyLeaveRedNames;
        set
        {
            if (_allyLeaveRedNames == value) return;
            _allyLeaveRedNames = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

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

    public bool IsApplying
    {
        get => _isApplying;
        set
        {
            if (_isApplying == value) return;
            _isApplying = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsApplyEnabled));
        }
    }

    public bool IsApplyEnabled => !_isApplying;

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
            _nativeDir = Path.Combine(modDir, "native");
            LoadCards();
            CaptureAppliedColors();
            foreach (var card in Cards)
            {
                card.PropertyChanged += (_, args) =>
                {
                    if (_activeTab != "colors") return;
                    if (args.PropertyName is null or nameof(ColorCard.Hex))
                        RefreshTabStatus();
                };
            }
            LoadExtraFeatures();
            _hookWatchTimer.Start();
            UpdateExtraHookStatus(forceInject: _appliedAllyLeaveRedNames || _appliedChatDuringPauseScreen);
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
        RefreshTabStatus();
    }

    private void CaptureAppliedColors()
    {
        _appliedHexByPlayer.Clear();
        foreach (var card in Cards)
            _appliedHexByPlayer[card.Player] = ColorCard.NormalizeHex(card.Hex);
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
        return false;
    }

    private bool HasManualAppliedColors() =>
        Cards.Any(card =>
            card.IsEnabled &&
            _appliedHexByPlayer.TryGetValue(card.Player, out var applied) &&
            !string.Equals(applied, ColorCard.NormalizeHex(card.VanillaHex), StringComparison.OrdinalIgnoreCase));

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

    private bool HasPendingMarkGone() => _allyLeaveRedNames != _appliedAllyLeaveRedNames;
    private bool HasPendingChatPause() => _chatDuringPauseScreen != _appliedChatDuringPauseScreen;
    private bool HasPendingExtraChanges() => HasPendingMarkGone() || HasPendingChatPause();

    private bool HasAnyPendingChanges() => HasPendingColorChanges() || HasPendingExtraChanges();

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

    private void RefreshTabStatus()
    {
        if (_isApplying) return;

        var pending = HasAnyPendingChanges();
        ApplyButtonBrush = pending ? PendingButtonBrush : ReadyButtonBrush;
        ApplyButtonBorderBrush = pending ? PendingButtonBorderBrush : ReadyButtonBorderBrush;

        if (_activeTab == "extra")
        {
            if (HasPendingExtraChanges())
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

        if (_activeTab == "util")
        {
            SetStatusLines(StatusLine("✓", ReadyIconBrush,
                "Util colors are not editable yet."));
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

    private void LoadCards()
    {
        var defaults = ReadDefaultConfig();
        var saved = File.Exists(_configPath)
            ? JsonSerializer.Deserialize<ColorConfig>(File.ReadAllText(_configPath)) ?? defaults
            : defaults;

        foreach (var vanilla in defaults.Players.OrderBy(p => p.Player))
        {
            var disabled = vanilla.Player is 8;
            // Player 8 stays locked (shared yellow band). Player 2 patches exclusive 212-215.
            var current = disabled
                ? vanilla.Color
                : (saved.Players.FirstOrDefault(p => p.Player == vanilla.Player)?.Color ?? vanilla.Color);
            Cards.Add(new ColorCard(vanilla.Player, current, vanilla.Color, !disabled));
        }
    }

    private ColorConfig ReadDefaultConfig()
    {
        var result = RunEngine("-GetDefaultConfig");
        if (result.ExitCode != 0)
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(result.Error) ? result.Output : result.Error);

        var json = result.Output.Split(["\r\n", "\n"], StringSplitOptions.RemoveEmptyEntries)
            .LastOrDefault(line => line.TrimStart().StartsWith("{", StringComparison.Ordinal))
            ?? throw new InvalidOperationException("The patch engine did not return default colors.");
        return JsonSerializer.Deserialize<ColorConfig>(json,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new InvalidOperationException("The patch engine returned an invalid default color configuration.");
    }

    private (int ExitCode, string Output, string Error) RunEngine(string arguments)
    {
        var start = new ProcessStartInfo("powershell.exe",
            $"-NoProfile -ExecutionPolicy Bypass -File \"{_enginePath}\" {arguments}")
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
        var markGone = false;
        var chatPause = false;
        if (File.Exists(_extraConfigPath))
        {
            var extra = JsonSerializer.Deserialize<ExtraFeaturesConfig>(File.ReadAllText(_extraConfigPath),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            markGone = extra?.AllyLeaveRedNames ?? false;
            chatPause = extra?.ChatDuringPauseScreen ?? false;
        }

        _allyLeaveRedNames = markGone;
        _appliedAllyLeaveRedNames = markGone;
        _chatDuringPauseScreen = chatPause;
        _appliedChatDuringPauseScreen = chatPause;
        OnPropertyChanged(nameof(AllyLeaveRedNames));
        OnPropertyChanged(nameof(ChatDuringPauseScreen));
    }

    private static bool IsWarcraftIiRunning() =>
        Process.GetProcessesByName("Warcraft II").Length > 0;

    private void UpdateExtraHookStatus(bool forceInject)
    {
        UpdateAllyLeaveHookStatus(forceInject);
        UpdatePauseChatHookStatus(forceInject);
    }

    private void UpdateAllyLeaveHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedAllyLeaveRedNames)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncAllyLeaveHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            if (_activeTab == "extra") RefreshTabStatus();
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _hookInjectedForRunningGame = false;
            if (_activeTab == "extra") RefreshTabStatus();
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

        if (_activeTab == "extra") RefreshTabStatus();
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
            var anyExtra = _appliedAllyLeaveRedNames || _appliedChatDuringPauseScreen;
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
            return _appliedAllyLeaveRedNames
                ? "Extra ON — watcher auto-injects when Warcraft II starts (Studio can close). F11 Alliances."
                : "Extra setting saved.";
        }

        var args = _appliedAllyLeaveRedNames ? "--enable" : "--disable";
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

        _hookInjectedForRunningGame = _appliedAllyLeaveRedNames;
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

    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        if (_isApplying) return;

        try
        {
            // Commit any hex TextBox still focused so the latest typed value is saved.
            if (Keyboard.FocusedElement is UIElement focused)
                focused.MoveFocus(new TraversalRequest(FocusNavigationDirection.Next));

            foreach (var card in Cards)
            {
                if (!ColorCard.IsValidHex(card.Hex))
                    throw new InvalidOperationException($"{card.Name} has an invalid color.");
            }

            IsApplying = true;
            SetStatusLines(StatusLine("", ReadyIconBrush, "Installing mod…"));
            ApplyButtonBrush = ReadyButtonBrush;
            ApplyButtonBorderBrush = ReadyButtonBorderBrush;

            var config = new ColorConfig { Players = Cards.Select(card => new PlayerColor(card.Player, card.Hex)).ToList() };
            var configJson = JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true });
            var markGoneEnabled = AllyLeaveRedNames;
            var chatPauseEnabled = ChatDuringPauseScreen;

            await Task.Run(() =>
            {
                File.WriteAllText(_configPath, configJson);
                File.WriteAllText(_extraConfigPath,
                    JsonSerializer.Serialize(new ExtraFeaturesConfig
                    {
                        AllyLeaveRedNames = markGoneEnabled,
                        ChatDuringPauseScreen = chatPauseEnabled,
                    }, new JsonSerializerOptions { WriteIndented = true }));

                var result = RunEngine("-ApplySavedConfigOnly");
                if (result.ExitCode != 0)
                {
                    var details = string.Join(Environment.NewLine,
                        new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
                    throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                        ? "Apply failed with no error details."
                        : details);
                }
            });

            _appliedAllyLeaveRedNames = markGoneEnabled;
            _appliedChatDuringPauseScreen = chatPauseEnabled;
            _hookInjectedForRunningGame = false;
            _pauseChatInjectedForRunningGame = false;
            SyncAllyLeaveWatch();
            try { SyncAllyLeaveHook(throwOnError: false); }
            catch { /* optional while Extra is off */ }
            try { SyncPauseChatHook(throwOnError: false); }
            catch { /* optional while Extra is off */ }

            CaptureAppliedColors();
        }
        catch (Exception ex)
        {
            WpfMessageBox.Show(ex.Message, "Apply failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            IsApplying = false;
            RefreshTabStatus();
        }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

public sealed class ColorCard : INotifyPropertyChanged
{
    private string _hex;
    public int Player { get; }
    public string Name => $"Player {Player}";
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

    public ColorCard(int player, string hex, string vanillaHex, bool enabled)
    {
        Player = player;
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
}

public sealed record PlayerColor(int Player, string Color);

public sealed class ExtraFeaturesConfig
{
    public bool AllyLeaveRedNames { get; set; }
    public bool ChatDuringPauseScreen { get; set; }
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
