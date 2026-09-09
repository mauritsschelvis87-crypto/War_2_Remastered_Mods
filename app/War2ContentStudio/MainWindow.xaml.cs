using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using Microsoft.Win32;
using War2ContentStudio.Campaign;
using War2ContentStudio.Editor;
using War2ContentStudio.Pud;

namespace War2ContentStudio;

public partial class MainWindow : Window
{
    private MapEditorSession _session = new(PudFactory.CreateEmpty());
    private CampaignProject _campaign = new();
    private string? _campaignPath;
    private bool _isPainting;
    private bool _uiReady;
    private bool _suppressZoomSlider;
    private const int MinZoom = 1;
    private const int MaxZoom = 256;
    private const double UnitVisualScale = 1.35;
    private List<PaletteEntry> _paletteEntries = [];
    private readonly UnitSpriteCache _unitSprites = new();

    public MainWindow()
    {
        InitializeComponent();
        _unitSprites.SpritesUpdated += () => Dispatcher.Invoke(RenderMapCanvas);
        _uiReady = true;
        ToolCombo.SelectedIndex = 0;
        PopulateCombos();
        LoadPalette();
        _session.History.Push(EditorSnapshot.FromDocument(_session.Document));
        RefreshUi();
        TryDefaultGameRoot();
    }

    private void TryDefaultGameRoot()
    {
        var candidates = new[]
        {
            @"C:\Program Files (x86)\Warcraft II Remastered",
            System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Warcraft II Remastered"),
        };
        foreach (var path in candidates)
        {
            if (Directory.Exists(System.IO.Path.Combine(path, "x86")))
            {
                GameRootBox.Text = path;
                _campaign.GameRootPath = path;
                break;
            }
        }
    }

    private void PopulateCombos()
    {
        for (var i = 0; i < 8; i++)
        {
            OwnerCombo.Items.Add(new ComboBoxItem { Content = $"Player {i + 1}", Tag = (byte)i });
        }
        OwnerCombo.SelectedIndex = 0;
        EraCombo.SelectedIndex = 0;
    }

    private void LoadPalette()
    {
        var terrain = _session.ActiveTool == EditorTool.Terrain;
        PaletteHeader.Text = terrain ? "Terrain palette" : "Units & buildings";
        PaletteFilterBox.Text = string.Empty;
        _paletteEntries = terrain
            ? PudCatalog.TerrainEntries.ToList()
            : PudCatalog.AllUnitEntries().ToList();
        ApplyPaletteFilter();
        if (PaletteList.Items.Count > 0)
        {
            PaletteList.SelectedIndex = 0;
            ApplySelectedPaletteEntry();
        }
    }

    private void ApplyPaletteFilter()
    {
        var filter = PaletteFilterBox.Text?.Trim() ?? string.Empty;
        IEnumerable<PaletteEntry> items = _paletteEntries;
        if (!string.IsNullOrEmpty(filter))
        {
            items = items.Where(e =>
                e.Name.Contains(filter, StringComparison.OrdinalIgnoreCase) ||
                e.Category.Contains(filter, StringComparison.OrdinalIgnoreCase));
        }

        var view = new ListCollectionView(items.ToList());
        view.GroupDescriptions.Add(new PropertyGroupDescription(nameof(PaletteEntry.Category)));
        PaletteList.ItemsSource = view;
    }

    private void ApplySelectedPaletteEntry()
    {
        if (PaletteList.SelectedItem is not PaletteEntry entry) return;
        if (entry.TerrainTile is { } tile) _session.TerrainTile = tile;
        if (entry.UnitType is { } type) _session.UnitType = type;
    }

    private void UpdateToolUi()
    {
        var tool = _session.ActiveTool;
        OwnerPanel.Visibility = tool == EditorTool.Unit ? Visibility.Visible : Visibility.Collapsed;
        PaletteHeader.Visibility = tool == EditorTool.Eraser ? Visibility.Collapsed : Visibility.Visible;
        PaletteFilterBox.Visibility = tool == EditorTool.Eraser ? Visibility.Collapsed : Visibility.Visible;
        PaletteList.Visibility = tool == EditorTool.Eraser ? Visibility.Collapsed : Visibility.Visible;
        if (tool != EditorTool.Eraser) LoadPalette();
    }

    private void RefreshUi()
    {
        if (!_uiReady) return;
        var doc = _session.Document;
        MapPathText.Text = string.IsNullOrEmpty(_session.FilePath) ? "(unsaved)" : _session.FilePath;
        DescriptionBox.Text = doc.Description;
        EraCombo.SelectedIndex = Math.Clamp(doc.Era, 0, 3);
        UdtaStatusText.Text = doc.HasUdta
            ? "UDTA present — per-map unit stats preserved on save."
            : "No UDTA section — save adds minimal defaults.";
        StatusText.Text = $"{doc.MapSize}×{doc.MapSize}  |  {doc.GetUnits().Count} units  |  zoom {_session.Zoom:0}×";
        MapSettings.BindDocument(doc);
        _unitSprites.Prefetch(doc.Era, doc.GetUnits().Select(u => u.Type));
        RenderMapCanvas();
        PreviewImage.Source = PudThumbnail.Render(doc, scale: ComputeMinimapScale(doc.MapSize));
    }

    private static int ComputeMinimapScale(int mapSize)
    {
        // Keep minimap preview roughly 160–220 px wide.
        var target = 192;
        var scale = Math.Max(2, (target + mapSize - 1) / mapSize);
        return Math.Min(scale, 8);
    }

    private void RenderMapCanvas()
    {
        if (!_uiReady || MapCanvas is null) return;
        MapCanvas.Children.Clear();

        var doc = _session.Document;
        var size = doc.MapSize;
        var zoom = _session.Zoom;
        var tiles = doc.GetTiles();

        MapCanvas.Width = size * zoom;
        MapCanvas.Height = size * zoom;

        for (var y = 0; y < size; y++)
        {
            for (var x = 0; x < size; x++)
            {
                var tile = tiles[y * size + x];
                var rect = new Rectangle
                {
                    Width = zoom,
                    Height = zoom,
                    Fill = new SolidColorBrush(PudCatalog.ColorForTile(tile)),
                    Stroke = new SolidColorBrush(Color.FromArgb(32, 255, 255, 255)),
                    StrokeThickness = zoom >= 4 ? 0.5 : 0,
                };
                Canvas.SetLeft(rect, x * zoom);
                Canvas.SetTop(rect, y * zoom);
                MapCanvas.Children.Add(rect);
            }
        }

        foreach (var unit in doc.GetUnits())
        {
            var (tileW, tileH) = UnitFootprints.Get(unit.Type);
            var footprintW = tileW * zoom;
            var footprintH = tileH * zoom;
            var displayW = footprintW * UnitVisualScale;
            var displayH = footprintH * UnitVisualScale;
            var left = unit.X * zoom - (displayW - footprintW) / 2;
            var top = unit.Y * zoom - (displayH - footprintH) / 2;

            var sprite = _unitSprites.TryGetTileSprite(doc.Era, unit.Type);
            if (sprite is not null)
            {
                var img = new Image
                {
                    Source = sprite,
                    Width = displayW,
                    Height = displayH,
                    Stretch = Stretch.Fill,
                    SnapsToDevicePixels = true,
                };
                RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.NearestNeighbor);
                Canvas.SetLeft(img, left);
                Canvas.SetTop(img, top);
                MapCanvas.Children.Add(img);
                continue;
            }

            _unitSprites.Ensure(doc.Era, unit.Type);
            var marker = new Rectangle
            {
                Width = footprintW,
                Height = footprintH,
                Fill = BrushForUnit(unit),
                Stroke = Brushes.Black,
                StrokeThickness = 1,
                Opacity = 0.55,
            };
            Canvas.SetLeft(marker, unit.X * zoom);
            Canvas.SetTop(marker, unit.Y * zoom);
            MapCanvas.Children.Add(marker);
        }
    }

    private static Brush BrushForUnit(PudUnit unit) => unit.Type switch
    {
        PudUnit.TypeGoldMine => Brushes.Gold,
        PudUnit.TypeOilPatch => Brushes.Black,
        PudUnit.TypeHumanStart => Brushes.DodgerBlue,
        PudUnit.TypeOrcStart => Brushes.OrangeRed,
        >= 0x3A and <= 0x68 => Brushes.BurlyWood,
        >= 0x00 and <= 0x13 => unit.Type % 2 == 0 ? Brushes.SteelBlue : Brushes.OrangeRed,
        _ => Brushes.White,
    };

    private (int x, int y)? GetMapCell(MouseEventArgs e)
    {
        var pos = e.GetPosition(MapCanvas);
        var zoom = _session.Zoom;
        var x = (int)(pos.X / zoom);
        var y = (int)(pos.Y / zoom);
        var size = _session.Document.MapSize;
        if (x < 0 || y < 0 || x >= size || y >= size) return null;
        return (x, y);
    }

    private void ApplyToolAt(int x, int y)
    {
        switch (_session.ActiveTool)
        {
            case EditorTool.Terrain:
                _session.PaintTile(x, y);
                break;
            case EditorTool.Unit:
                _session.PlaceUnit(x, y);
                break;
            case EditorTool.Eraser:
                _session.EraseAt(x, y);
                break;
        }
        RefreshUi();
    }

    private void MapCanvas_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (GetMapCell(e) is not { } cell) return;
        _isPainting = true;
        MapCanvas.CaptureMouse();
        ApplyToolAt(cell.x, cell.y);
    }

    private void MapCanvas_MouseMove(object sender, MouseEventArgs e)
    {
        if (!_isPainting || e.LeftButton != MouseButtonState.Pressed) return;
        if (GetMapCell(e) is not { } cell) return;
        ApplyToolAt(cell.x, cell.y);
    }

    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        if (_isPainting)
        {
            _isPainting = false;
            MapCanvas.ReleaseMouseCapture();
        }
        base.OnMouseLeftButtonUp(e);
    }

    private void NewMap_Click(object sender, RoutedEventArgs e)
    {
        _session = new MapEditorSession(PudFactory.CreateEmpty());
        _session.History.Push(EditorSnapshot.FromDocument(_session.Document));
        RefreshUi();
        LoadPalette();
    }

    private void OpenPud_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "Warcraft II maps (*.pud)|*.pud|All files|*.*" };
        if (dlg.ShowDialog() != true) return;
        try
        {
            var doc = PudReader.ReadFile(dlg.FileName);
            _session.Load(doc, dlg.FileName);
            RefreshUi();
            LoadPalette();
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Open failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void SavePud_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_session.FilePath))
        {
            SaveAsPud_Click(sender, e);
            return;
        }

        try
        {
            MapSettings.SaveToDocument();
            PudWriter.WriteFile(_session.Document, _session.FilePath);
            StatusText.Text = "Saved.";
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Save failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void SaveAsPud_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new SaveFileDialog { Filter = "Warcraft II maps (*.pud)|*.pud", FileName = "map.pud" };
        if (dlg.ShowDialog() != true) return;
        _session.FilePath = dlg.FileName;
        SavePud_Click(sender, e);
        RefreshUi();
    }

    private void Undo_Click(object sender, RoutedEventArgs e)
    {
        _session.Undo();
        RefreshUi();
    }

    private void Redo_Click(object sender, RoutedEventArgs e)
    {
        _session.Redo();
        RefreshUi();
    }

    private void ToolCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady || ToolCombo.SelectedItem is not ComboBoxItem item || item.Tag is not string tag) return;
        _session.ActiveTool = tag switch
        {
            "Unit" => EditorTool.Unit,
            "Eraser" => EditorTool.Eraser,
            _ => EditorTool.Terrain,
        };
        UpdateToolUi();
    }

    private void PaletteList_SelectionChanged(object sender, SelectionChangedEventArgs e) =>
        ApplySelectedPaletteEntry();

    private void PaletteFilterBox_TextChanged(object sender, TextChangedEventArgs e) =>
        ApplyPaletteFilter();

    private void OwnerCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (OwnerCombo.SelectedItem is ComboBoxItem item && item.Tag is byte owner)
        {
            _session.UnitOwner = owner;
        }
    }

    private void SetZoom(int zoom, Point? anchorInViewer = null)
    {
        zoom = Math.Clamp(zoom, MinZoom, MaxZoom);
        var oldZoom = (int)_session.Zoom;
        if (zoom == oldZoom) return;

        double? mapAnchorX = null;
        double? mapAnchorY = null;
        if (anchorInViewer is { } anchor)
        {
            var canvasX = MapScroll.HorizontalOffset + anchor.X;
            var canvasY = MapScroll.VerticalOffset + anchor.Y;
            mapAnchorX = canvasX / oldZoom;
            mapAnchorY = canvasY / oldZoom;
        }

        _session.Zoom = zoom;
        _suppressZoomSlider = true;
        ZoomSlider.Value = zoom;
        _suppressZoomSlider = false;
        ZoomLabel.Text = $"{zoom}×";
        RenderMapCanvas();

        var doc = _session.Document;
        StatusText.Text = $"{doc.MapSize}×{doc.MapSize}  |  {doc.GetUnits().Count} units  |  zoom {zoom}×";

        if (mapAnchorX is not null && mapAnchorY is not null)
        {
            MapScroll.UpdateLayout();
            var newCanvasX = mapAnchorX.Value * zoom;
            var newCanvasY = mapAnchorY.Value * zoom;
            MapScroll.ScrollToHorizontalOffset(Math.Max(0, newCanvasX - anchorInViewer!.Value.X));
            MapScroll.ScrollToVerticalOffset(Math.Max(0, newCanvasY - anchorInViewer.Value.Y));
        }
    }

    private void MapScroll_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (Keyboard.Modifiers != ModifierKeys.Control) return;
        e.Handled = true;
        var delta = e.Delta > 0 ? 1 : -1;
        var anchor = e.GetPosition(MapScroll);
        SetZoom((int)_session.Zoom + delta, anchor);
    }

    private void ZoomSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!_uiReady || _suppressZoomSlider) return;
        SetZoom((int)e.NewValue);
    }

    private void DescriptionBox_LostFocus(object sender, RoutedEventArgs e)
    {
        var text = DescriptionBox.Text ?? string.Empty;
        if (text == _session.Document.Description) return;
        _session.CommitSnapshot();
        _session.Document.Description = text;
        RefreshUi();
    }

    private void EraCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady || EraCombo.SelectedIndex < 0) return;
        var era = EraCombo.SelectedIndex;
        if (era == _session.Document.Era) return;
        _session.CommitSnapshot();
        _session.Document.Era = era;
        _unitSprites.Prefetch(era, _session.Document.GetUnits().Select(u => u.Type));
        RefreshUi();
    }

    private void SyncCampaignFromUi()
    {
        _campaign.Name = CampaignNameBox.Text;
        _campaign.CampaignFolderName = CampaignFolderBox.Text;
        _campaign.GameRootPath = GameRootBox.Text;
        _campaign.Missions = MissionGrid.Items.Cast<CampaignMission>().ToList();
    }

    private void SyncUiFromCampaign()
    {
        CampaignNameBox.Text = _campaign.Name;
        CampaignFolderBox.Text = _campaign.CampaignFolderName;
        GameRootBox.Text = _campaign.GameRootPath;
        MissionGrid.ItemsSource = null;
        MissionGrid.ItemsSource = _campaign.Missions;
    }

    private void NewCampaign_Click(object sender, RoutedEventArgs e)
    {
        _campaign = new CampaignProject();
        _campaignPath = null;
        SyncUiFromCampaign();
    }

    private void OpenCampaign_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "Campaign project (*.json)|*.json|All files|*.*" };
        if (dlg.ShowDialog() != true) return;
        try
        {
            _campaign = CampaignProject.Load(dlg.FileName);
            _campaignPath = dlg.FileName;
            SyncUiFromCampaign();
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Open failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void SaveCampaign_Click(object sender, RoutedEventArgs e)
    {
        SyncCampaignFromUi();
        if (string.IsNullOrEmpty(_campaignPath))
        {
            var dlg = new SaveFileDialog { Filter = "Campaign project (*.json)|*.json", FileName = "campaign.json" };
            if (dlg.ShowDialog() != true) return;
            _campaignPath = dlg.FileName;
        }

        _campaign.Save(_campaignPath);
        DeployLogBox.Text = $"Saved {_campaignPath}";
    }

    private void DeployCampaign_Click(object sender, RoutedEventArgs e)
    {
        SyncCampaignFromUi();
        try
        {
            var result = CampaignDeployService.Deploy(_campaign);
            DeployLogBox.Text = string.Join(Environment.NewLine, result.Messages);
            DeployLogBox.Text += Environment.NewLine + $"Backup: {result.BackupRoot}";
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Deploy failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
