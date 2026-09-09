using System.Windows;
using System.Windows.Controls;
using War2ContentStudio.Pud;

namespace War2ContentStudio.Views;

public partial class MapSettingsView : UserControl
{
    private PudDocument? _document;
    private PudPlayerSlot[] _players = [];
    private PudAllowanceSet[] _allowances = [];
    private PudUpgradeRow[] _upgrades = [];
    private CheckBox[] _unitChecks = [];
    private CheckBox[] _spellChecks = [];
    private CheckBox[] _upgradeChecks = [];
    private bool _loading;

    public MapSettingsView()
    {
        InitializeComponent();
        BuildStaticUi();
    }

    public void BindDocument(PudDocument? document)
    {
        _document = document;
        Reload();
    }

    public void SaveToDocument()
    {
        if (_document is null) return;
        SaveCurrentPlayerFields();
        PudSettings.WritePlayers(_document, _players);
        PudSettings.WriteAllowances(_document, _allowances);
        PudSettings.WriteUpgrades(_document, _upgrades, UseDefaultUpgradesBox.IsChecked == true);
    }

    private void Reload()
    {
        if (_document is null) return;
        _loading = true;
        _players = PudSettings.ReadPlayers(_document);
        _allowances = PudSettings.ReadAllowances(_document);
        _upgrades = PudSettings.ReadUpgrades(_document);
        UseDefaultUpgradesBox.IsChecked = PudSettings.ReadUpgradeUseDefault(_document);
        UpgradeGrid.IsEnabled = UseDefaultUpgradesBox.IsChecked != true;
        UpgradeGrid.ItemsSource = null;
        UpgradeGrid.ItemsSource = _upgrades;
        if (PlayerCombo.Items.Count == 0)
        {
            for (var i = 0; i < PudDefinitions.PlayerSlotCount; i++)
            {
                PlayerCombo.Items.Add(new ComboBoxItem { Content = $"Player {i + 1}", Tag = i });
            }
            PlayerCombo.SelectedIndex = 0;
        }
        LoadPlayerUi();
        _loading = false;
    }

    private void BuildStaticUi()
    {
        ControllerCombo.Items.Clear();
        foreach (var (value, label) in PudDefinitions.Controllers)
        {
            ControllerCombo.Items.Add(new ComboBoxItem { Content = label, Tag = value });
        }

        RaceCombo.Items.Clear();
        foreach (var (value, label) in PudDefinitions.Races)
        {
            RaceCombo.Items.Add(new ComboBoxItem { Content = label, Tag = value });
        }

        AiCombo.Items.Clear();
        foreach (var (value, label) in PudDefinitions.AiTypes)
        {
            AiCombo.Items.Add(new ComboBoxItem { Content = label, Tag = value });
        }

        _unitChecks = BuildChecks(UnitAllowPanel, PudDefinitions.UnitAllowanceBits, 280);
        _spellChecks = BuildChecks(SpellAllowPanel, PudDefinitions.SpellAllowanceBits, 220);
        _upgradeChecks = BuildChecks(UpgradeAllowPanel, PudDefinitions.UpgradeNames, 260);
        foreach (var cb in _unitChecks.Concat(_spellChecks).Concat(_upgradeChecks))
        {
            cb.Checked += AllowCheck_Changed;
            cb.Unchecked += AllowCheck_Changed;
        }
    }

    private static CheckBox[] BuildChecks(Panel panel, IReadOnlyList<string> labels, double width)
    {
        panel.Children.Clear();
        var checks = new CheckBox[labels.Count];
        for (var i = 0; i < labels.Count; i++)
        {
            var cb = new CheckBox
            {
                Content = labels[i],
                Width = width,
                Margin = new Thickness(0, 0, 12, 6),
                Tag = i,
            };
            checks[i] = cb;
            panel.Children.Add(cb);
        }

        return checks;
    }

    private int SelectedPlayerIndex =>
        PlayerCombo.SelectedItem is ComboBoxItem item && item.Tag is int idx ? idx : 0;

    private void LoadPlayerUi()
    {
        var p = SelectedPlayerIndex;
        var slot = _players[p];
        SelectCombo(ControllerCombo, slot.Controller);
        SelectCombo(RaceCombo, slot.Race);
        GoldBox.Text = slot.Gold.ToString();
        LumberBox.Text = slot.Lumber.ToString();
        OilBox.Text = slot.Oil.ToString();
        SelectCombo(AiCombo, slot.Ai);

        var allow = _allowances[p];
        SetChecks(_unitChecks, allow.Units);
        SetChecks(_spellChecks, allow.SpellsResearch);
        SetChecks(_upgradeChecks, allow.Upgrades, PudDefinitions.UpgradeCount);
    }

    private void SaveCurrentPlayerFields()
    {
        if (_loading || _players.Length == 0) return;
        var p = SelectedPlayerIndex;
        var slot = _players[p];
        slot.Controller = TagByte(ControllerCombo, slot.Controller);
        slot.Race = TagByte(RaceCombo, slot.Race);
        if (ushort.TryParse(GoldBox.Text, out var gold)) slot.Gold = gold;
        if (ushort.TryParse(LumberBox.Text, out var lumber)) slot.Lumber = lumber;
        if (ushort.TryParse(OilBox.Text, out var oil)) slot.Oil = oil;
        slot.Ai = TagByte(AiCombo, slot.Ai);

        var allow = _allowances[p];
        allow.Units = ReadChecks(_unitChecks);
        allow.SpellsResearch = ReadChecks(_spellChecks);
        allow.Upgrades = ReadChecks(_upgradeChecks, PudDefinitions.UpgradeCount);
    }

    private static void SelectCombo(ComboBox combo, byte value)
    {
        foreach (ComboBoxItem item in combo.Items)
        {
            if (item.Tag is byte b && b == value)
            {
                combo.SelectedItem = item;
                return;
            }
        }

        combo.SelectedIndex = combo.Items.Count > 0 ? 0 : -1;
    }

    private static byte TagByte(ComboBox combo, byte fallback) =>
        combo.SelectedItem is ComboBoxItem item && item.Tag is byte b ? b : fallback;

    private static void SetChecks(CheckBox[] checks, uint mask, int maxBits = 32)
    {
        for (var i = 0; i < checks.Length && i < maxBits; i++)
        {
            checks[i].IsChecked = PudSettings.GetBit(mask, i);
            checks[i].IsEnabled = i < 32;
        }
    }

    private static uint ReadChecks(CheckBox[] checks, int maxBits = 32)
    {
        uint mask = 0;
        for (var i = 0; i < checks.Length && i < maxBits; i++)
        {
            if (checks[i].IsChecked == true) mask |= 1u << i;
        }

        return mask;
    }

    private void PlayerCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loading) return;
        SaveCurrentPlayerFields();
        LoadPlayerUi();
    }

    private void UseDefaultUpgradesBox_Changed(object sender, RoutedEventArgs e)
    {
        UpgradeGrid.IsEnabled = UseDefaultUpgradesBox.IsChecked != true;
    }

    private void AllowCheck_Changed(object sender, RoutedEventArgs e)
    {
        if (_loading) return;
        SaveCurrentPlayerFields();
    }
}
