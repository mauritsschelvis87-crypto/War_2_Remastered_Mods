using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Forms = System.Windows.Forms;
using WpfMessageBox = System.Windows.MessageBox;

namespace PlayerColorStudio;

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private readonly string _enginePath;
    private readonly string _configPath;
    private string _status = "Ready. Close Warcraft II before applying colors.";

    public ObservableCollection<ColorCard> Cards { get; } = [];

    public string Status
    {
        get => _status;
        set { _status = value; OnPropertyChanged(); }
    }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

        try
        {
            _enginePath = FindEnginePath();
            _configPath = Path.Combine(Path.GetDirectoryName(_enginePath)!, "player-colors.json");
            LoadCards();
        }
        catch (Exception ex)
        {
            _enginePath = string.Empty;
            _configPath = string.Empty;
            Status = ex.Message;
            WpfMessageBox.Show(ex.Message, "Modding Studio", MessageBoxButton.OK, MessageBoxImage.Error);
        }
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
        throw new FileNotFoundException("The patch engine was not found. Reinstall Modding Studio.");
    }

    private void LoadCards()
    {
        var defaults = ReadDefaultConfig();
        var saved = File.Exists(_configPath)
            ? JsonSerializer.Deserialize<ColorConfig>(File.ReadAllText(_configPath)) ?? defaults
            : defaults;

        foreach (var vanilla in defaults.Players.OrderBy(p => p.Player))
        {
            var disabled = vanilla.Player is 2 or 8;
            // Disabled players keep their authentic in-game display color (e.g. Player 8 yellow).
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
            card.Hex = $"#{dialog.Color.R:X2}{dialog.Color.G:X2}{dialog.Color.B:X2}";
    }

    private void Hex_LostFocus(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is not ColorCard card) return;
        if (!ColorCard.IsValidHex(card.Hex))
        {
            WpfMessageBox.Show($"{card.Name}: use a six-digit hex color, for example #3B82F6.", "Invalid color",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            card.Hex = card.LastValidHex;
            return;
        }
        card.Hex = card.Hex;
    }

    private void ResetCard_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is ColorCard card) card.Reset();
    }

    private void Apply_Click(object sender, RoutedEventArgs e)
    {
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

            var config = new ColorConfig { Players = Cards.Select(card => new PlayerColor(card.Player, card.Hex)).ToList() };
            File.WriteAllText(_configPath, JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true }));
            Status = "Applying colors…";
            var result = RunEngine("-ApplySavedConfigOnly");
            if (result.ExitCode != 0)
            {
                var details = string.Join(Environment.NewLine,
                    new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
                throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                    ? "Apply failed with no error details."
                    : details);
            }

            Status = "Applied. Restart Warcraft II to see the changes.";
            WpfMessageBox.Show("Colors were applied to the minimap and victory/ally bars.\n\nClose Warcraft II completely and restart it to see the changes.",
                "Modding Studio", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex)
        {
            Status = "Apply failed.";
            WpfMessageBox.Show(ex.Message, "Apply failed", MessageBoxButton.OK, MessageBoxImage.Error);
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

    private static string Normalize(string value) =>
        value.Trim().StartsWith('#') ? value.Trim().ToUpperInvariant() : $"#{value.Trim().ToUpperInvariant()}";

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
