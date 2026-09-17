using System;
using System.Threading;
using System.Net.Http;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Forms = System.Windows.Forms;
namespace PlayerColorStudio;

public partial class MainWindow : Window, INotifyPropertyChanged
{
    private const string DefaultGameRootPath = @"C:\Program Files (x86)\Warcraft II Remastered";
    // Fixed name the Blizzard installer uses — the editor only works via this exe.
    private const string MapEditorExeName = "Warcraft II Map Editor.exe";

    private readonly string _enginePath;
    private readonly string _configPath;
    private readonly string _extraConfigPath;
    private readonly string _settingsPath = string.Empty;
    private readonly string _nativeDir;
    private readonly DispatcherTimer _hookWatchTimer;
    private readonly DispatcherTimer _dropMonitorTimer;
    private readonly DispatcherTimer _selfMonitorTimer;
    private readonly DispatcherTimer _networkMonitorTimer;
    private string _dropMonitorLog = string.Empty;
    private string _dropMonitorStatus = "Monitor is off.";
    private string _dropMonitorLastCause = "No drop recorded.";
    private bool _dropMonitorEnabled;
    private string _selfMonitorLog = string.Empty;
    private string _selfMonitorStatus = "Monitor is off.";
    private string _selfMonitorLastCause = "No color change recorded.";
    private bool _selfMonitorEnabled;
    private string _networkMonitorLog = string.Empty;
    private string _networkMonitorStatus = "Monitor off — enable and Apply, then play a multiplayer match.";
    private string _networkMonitorSummary = string.Empty;
    private bool _networkMonitor;
    private bool _appliedNetworkMonitor;
    private bool _networkMonitorInjectedForRunningGame;
    private readonly Dictionary<string, string> _selfMonitorLastColors = new(StringComparer.OrdinalIgnoreCase);
    private bool _allyLeaveMarkComputers;
    private bool _allyLeaveMarkHumans;
    private bool _appliedAllyLeaveMarkComputers;
    private bool _appliedAllyLeaveMarkHumans;
    private bool _chatDuringPauseScreen;
    private bool _appliedChatDuringPauseScreen;
    private bool _chatColoredNames;
    private bool _appliedChatColoredNames;
    private bool _chatTimestamps;
    private bool _appliedChatTimestamps;
    private bool _chatHistory;
    private bool _appliedChatHistory;
    private bool _mpLobbyChatScrollFix;
    private bool _appliedMpLobbyChatScrollFix;
    private bool _castleGoldTooltipFix;
    private bool _appliedCastleGoldTooltipFix;
    private bool _endGameObserve;
    private bool _appliedEndGameObserve;
    private bool _allianceTeamNumbers;
    private bool _appliedAllianceTeamNumbers;
    private bool _computerAnnihilatedChat;
    private bool _appliedComputerAnnihilatedChat;
    private bool _blacksmithWorkCompleteChat;
    private bool _appliedBlacksmithWorkCompleteChat;
    private bool _lobbyMapClickOpen;
    private string _mapsSearchText = "";
    private string _mapsCategoryFilter = "All";
    private string _mapsSizeFilter = "All";
    private string _mapsSyncStatusText = "";
    private string _mapsPudSyncStatusText = "";
    private string _mapsDownloadProgressText = "";
    private string _mapsFilterSummary = "";
    private bool _mapsDownloadBusy;
    private bool _hasInitialMapImagesDownload;
    private CancellationTokenSource? _mapsDownloadCts;
    private List<MapCatalogEntry> _mapsCatalog = new();
    private string _mapsPlayersFilter = "All";
    private string _mapsTilesetFilter = "All";
    private string _mapsResultsCountText = "";
    private bool _mapsCatalogLoading;

    private bool _appliedLobbyMapClickOpen;
    private bool _lobbyMapClickInjectedForRunningGame;
    private bool _colorBlindMode;
    private string _selectedColorBlindPreset = string.Empty;
    private string _appliedColorBlindPreset = string.Empty;
    private string _colorBlindPresetHintText = string.Empty;
    private bool _suppressColorBlindPresetSync;
    private bool _unitSpriteColors;
    private bool _appliedUnitSpriteColors;
    private bool _appliedDragSelectColorEnabled;
    private bool _hookInjectedForRunningGame;
    private bool _pauseChatInjectedForRunningGame;
    private bool _chatNameColorInjectedForRunningGame;
    private bool _dragSelectInjectedForRunningGame;
    private bool _observeInjectedForRunningGame;
    private bool _isApplying;
    private bool _skipNextStatusRefresh;
    private string _activeTab = "colors";
    private string _gameInstallPath = DefaultGameRootPath;
    private string _appliedGameInstallPath = DefaultGameRootPath;
    private string _mapEditorPath = DefaultMapEditorPathFor(DefaultGameRootPath);
    private string _appliedMapEditorPath = DefaultMapEditorPathFor(DefaultGameRootPath);
    private string _mapsPath = DefaultMapsPathFor(DefaultGameRootPath);
    private string _appliedMapsPath = DefaultMapsPathFor(DefaultGameRootPath);
    private string _mapImagesPath = "";
    private string _appliedMapImagesPath = "";
    private System.Windows.Media.Brush _applyButtonBrush = CreateBrush(0x2E, 0xA8, 0x5C);
    private System.Windows.Media.Brush _applyButtonBorderBrush = CreateBrush(0x4C, 0xC3, 0x7A);
    private readonly Dictionary<int, string> _appliedHexByPlayer = new();
    private readonly Dictionary<string, string> _appliedOtherHexByKey = new(StringComparer.OrdinalIgnoreCase);

    public ObservableCollection<ColorCard> Cards { get; } = [];
    public ObservableCollection<ColorCard> OtherCards { get; } = [];
    public ObservableCollection<StatusLineItem> StatusLines { get; } = [];
    public ObservableCollection<NetworkSeatRow> NetworkSeats { get; } = [];

    public string DropMonitorLog
    {
        get => _dropMonitorLog;
        private set { _dropMonitorLog = value; OnPropertyChanged(); }
    }

    public string DropMonitorStatus
    {
        get => _dropMonitorStatus;
        private set { _dropMonitorStatus = value; OnPropertyChanged(); }
    }

    public string DropMonitorLastCause
    {
        get => _dropMonitorLastCause;
        private set { _dropMonitorLastCause = value; OnPropertyChanged(); }
    }

    public bool DropMonitorEnabled
    {
        get => _dropMonitorEnabled;
        private set
        {
            _dropMonitorEnabled = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(DropMonitorCanStart));
        }
    }

    public bool DropMonitorCanStart => !DropMonitorEnabled;

    public string NetworkMonitorLog
    {
        get => _networkMonitorLog;
        private set { _networkMonitorLog = value; OnPropertyChanged(); }
    }

    public string NetworkMonitorStatus
    {
        get => _networkMonitorStatus;
        private set { _networkMonitorStatus = value; OnPropertyChanged(); }
    }

    public string NetworkMonitorSummary
    {
        get => _networkMonitorSummary;
        private set { _networkMonitorSummary = value; OnPropertyChanged(); }
    }

    public bool NetworkMonitor
    {
        get => _networkMonitor;
        set
        {
            if (_networkMonitor == value) return;
            _networkMonitor = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public string SelfMonitorLog
    {
        get => _selfMonitorLog;
        private set { _selfMonitorLog = value; OnPropertyChanged(); }
    }

    public string SelfMonitorStatus
    {
        get => _selfMonitorStatus;
        private set { _selfMonitorStatus = value; OnPropertyChanged(); }
    }

    public string SelfMonitorLastCause
    {
        get => _selfMonitorLastCause;
        private set { _selfMonitorLastCause = value; OnPropertyChanged(); }
    }

    public bool SelfMonitorEnabled
    {
        get => _selfMonitorEnabled;
        private set
        {
            _selfMonitorEnabled = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelfMonitorCanStart));
        }
    }

    public bool SelfMonitorCanStart => !SelfMonitorEnabled;

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
        _appliedAllyLeaveMarkComputers || _appliedAllyLeaveMarkHumans ||
        _appliedAllianceTeamNumbers || _appliedComputerAnnihilatedChat;

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

    public bool ChatTimestamps
    {
        get => _chatTimestamps;
        set
        {
            if (_chatTimestamps == value) return;
            _chatTimestamps = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool ChatHistory
    {
        get => _chatHistory;
        set
        {
            if (_chatHistory == value) return;
            _chatHistory = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool MpLobbyChatScrollFix
    {
        get => false;
        set
        {
            // MP lobby chat scroll is greyed out — keep state forced off.
            if (_mpLobbyChatScrollFix)
            {
                _mpLobbyChatScrollFix = false;
                OnPropertyChanged();
                RefreshTabStatus();
            }
        }
    }

    public bool LobbyMapClickOpen
    {
        get => _lobbyMapClickOpen;
        set
        {
            if (_lobbyMapClickOpen == value) return;
            _lobbyMapClickOpen = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }


    public IReadOnlyList<string> MapsCategoryOptions { get; } = new[] { "All", "Classic", "BNE", "Community", "Expansion" };
    public IReadOnlyList<string> MapsSizeOptions { get; } = new[] { "All", "28x28", "32x32", "64x64", "96x96", "128x128" };

    public string MapsCategoryFilter
    {
        get => _mapsCategoryFilter;
        set
        {
            var next = string.IsNullOrWhiteSpace(value) ? "All" : value.Trim();
            if (_mapsCategoryFilter == next) return;
            _mapsCategoryFilter = next;
            OnPropertyChanged(nameof(MapsCategoryFilter));
            ApplyMapsFilters();
        }
    }

    public string MapsSizeFilter
    {
        get => _mapsSizeFilter;
        set
        {
            var next = string.IsNullOrWhiteSpace(value) ? "All" : value.Trim();
            if (_mapsSizeFilter == next) return;
            _mapsSizeFilter = next;
            OnPropertyChanged(nameof(MapsSizeFilter));
            ApplyMapsFilters();
        }
    }

    public string MapsSearchText
    {
        get => _mapsSearchText;
        set
        {
            var next = value ?? "";
            if (_mapsSearchText == next) return;
            _mapsSearchText = next;
            OnPropertyChanged(nameof(MapsSearchText));
            ApplyMapsFilters();
        }
    }

    public string MapsSyncStatusText
    {
        get => _mapsSyncStatusText;
        set { if (_mapsSyncStatusText == value) return; _mapsSyncStatusText = value; OnPropertyChanged(nameof(MapsSyncStatusText)); OnPropertyChanged(nameof(MapsImagesFeedbackText)); }
    }

    public string MapsDownloadProgressText
    {
        get => _mapsDownloadProgressText;
        set
        {
            if (_mapsDownloadProgressText == value) return;
            _mapsDownloadProgressText = value;
            OnPropertyChanged(nameof(MapsDownloadProgressText));
            OnPropertyChanged(nameof(MapsImagesFeedbackText));
            if (_activeTab == "path")
                PushMapsFeedbackToStatus();
        }
    }

    public string MapsFilterSummary
    {
        get => _mapsFilterSummary;
        set { if (_mapsFilterSummary == value) return; _mapsFilterSummary = value; OnPropertyChanged(nameof(MapsFilterSummary)); }
    }

    public bool IsMapsDownloadEnabled => IsApplyEnabled && !_mapsDownloadBusy && IsValidMapImagesPath(MapImagesPath);

    public bool IsMapsFolderDownloadEnabled => IsApplyEnabled && !_mapsDownloadBusy && IsValidMapsPath(MapsPath);

    public string MapsImagesActionLabel => Localization.Get("Maps.Download");

    public string MapsImagesFeedbackText
    {
        get
        {
            if (!string.IsNullOrWhiteSpace(_mapsDownloadProgressText))
                return _mapsDownloadProgressText;
            return _mapsSyncStatusText;
        }
    }

    public ObservableCollection<MapCatalogEntry> FilteredMaps { get; } = new();

    public IReadOnlyList<string> MapsPlayersOptions { get; } =
        new[] { "All", "2", "3", "4", "5", "6", "7", "8" };

    public IReadOnlyList<string> MapsTilesetOptions { get; } =
        new[] { "All", "Forest", "Winter", "Wasteland", "Swamp" };

    public string MapsPlayersFilter
    {
        get => _mapsPlayersFilter;
        set
        {
            var next = string.IsNullOrWhiteSpace(value) ? "All" : value.Trim();
            if (_mapsPlayersFilter == next) return;
            _mapsPlayersFilter = next;
            OnPropertyChanged(nameof(MapsPlayersFilter));
            ApplyMapsFilters();
        }
    }

    public string MapsTilesetFilter
    {
        get => _mapsTilesetFilter;
        set
        {
            var next = string.IsNullOrWhiteSpace(value) ? "All" : value.Trim();
            if (_mapsTilesetFilter == next) return;
            _mapsTilesetFilter = next;
            OnPropertyChanged(nameof(MapsTilesetFilter));
            ApplyMapsFilters();
        }
    }


    public string MapsResultsCountText
    {
        get => _mapsResultsCountText;
        set
        {
            if (_mapsResultsCountText == value) return;
            _mapsResultsCountText = value;
            OnPropertyChanged(nameof(MapsResultsCountText));
        }
    }

    private async Task EnsureMapsCatalogLoadedAsync()
    {
        if (_mapsCatalogLoading) return;
        if (_mapsCatalog.Count > 0)
        {
            MarkLocalImagesOnCatalog();
            ApplyMapsFilters();
            return;
        }

        _mapsCatalogLoading = true;
        MapsResultsCountText = "Loading map list…";
        try
        {
            EnsureMapsRootLayout();
            var dir = MapImagesDir();
            var cachePath = Path.Combine(dir, "_maps_catalog.json");
            if (!Directory.Exists(dir))
                cachePath = Path.Combine(Path.GetTempPath(), "war2_maps_catalog.json");

            if (File.Exists(cachePath))
            {
                try
                {
                    var raw = await File.ReadAllTextAsync(cachePath);
                    var cached = System.Text.Json.JsonSerializer.Deserialize<List<MapCatalogEntry>>(raw);
                    var hasEra = raw.Contains("\"Era\"", StringComparison.OrdinalIgnoreCase);
                    if (cached is { Count: > 0 } && hasEra)
                    {
                        _mapsCatalog = cached;
                        MarkLocalImagesOnCatalog();
                        ApplyMapsFilters();
                        return;
                    }
                }
                catch { /* fetch fresh */ }
            }

            await FetchMapsCatalogAsync(cachePath);
        }
        catch (Exception ex)
        {
            MapsResultsCountText = "Could not load maps: " + ex.Message;
        }
        finally
        {
            _mapsCatalogLoading = false;
        }
    }

    private async Task FetchMapsCatalogAsync(string cachePath)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("PlayerColorStudio-Maps/1.0");
        var list = new List<MapCatalogEntry>();
        long? cursor = null;
        for (var page = 0; page < 500; page++)
        {
            var url = "https://warcraft2.site/api/maps?pageSize=100" + (cursor is null ? "" : $"&cursor={cursor}");
            using var resp = await http.GetAsync(url);
            resp.EnsureSuccessStatusCode();
            using var doc = System.Text.Json.JsonDocument.Parse(await resp.Content.ReadAsStringAsync());
            var rootEl = doc.RootElement;
            if (rootEl.TryGetProperty("items", out var items))
            {
                foreach (var it in items.EnumerateArray())
                {
                    var filename = it.TryGetProperty("filename", out var fn) ? fn.GetString() ?? "" : "";
                    if (string.IsNullOrWhiteSpace(filename)) continue;
                    var title = it.TryGetProperty("title", out var t) ? t.GetString() ?? "" : "";
                    var category = it.TryGetProperty("category", out var c) ? c.GetString() ?? "" : "";
                    var size = it.TryGetProperty("size", out var s) ? s.GetString() ?? "" : "";
                    var players = it.TryGetProperty("playerCount", out var p) && p.TryGetInt32(out var pc) ? pc : 0;
                    var era = it.TryGetProperty("era", out var er) && er.TryGetInt32(out var eraVal) ? eraVal : -1;
                    list.Add(new MapCatalogEntry
                    {
                        Filename = filename,
                        Title = title,
                        Category = category,
                        Size = size,
                        PlayerCount = players,
                        Era = era,
                    });
                }
            }
            MapsResultsCountText = $"Loading map list… {list.Count}";
            var hasMore = rootEl.TryGetProperty("hasMore", out var hm) && hm.GetBoolean();
            if (!hasMore) break;
            if (rootEl.TryGetProperty("nextCursor", out var nc) && nc.ValueKind == System.Text.Json.JsonValueKind.Number)
                cursor = nc.GetInt64();
            else break;
        }

        _mapsCatalog = list
            .GroupBy(x => x.Filename, StringComparer.OrdinalIgnoreCase)
            .Select(g => g.First())
            .OrderBy(x => x.MapName, StringComparer.OrdinalIgnoreCase)
            .ToList();

        try
        {
            var dir = Path.GetDirectoryName(cachePath);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            await File.WriteAllTextAsync(cachePath,
                System.Text.Json.JsonSerializer.Serialize(_mapsCatalog, new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
        }
        catch { /* cache best-effort */ }

        MarkLocalImagesOnCatalog();
        ApplyMapsFilters();
    }

    private void MarkLocalImagesOnCatalog()
    {
        var dir = MapImagesDir();
        foreach (var entry in _mapsCatalog)
        {
            var stem = Path.GetFileNameWithoutExtension(entry.Filename);
            var webp = Path.Combine(dir, stem + ".webp");
            var ok = File.Exists(webp) && new FileInfo(webp).Length >= 2048;
            entry.HasLocalImage = ok;
            entry.PreviewPath = ok ? webp : null;
        }
    }


    private void MapsPreviewMap_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.Button { Tag: MapCatalogEntry entry })
            return;
        var path = entry.PreviewPath;
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            StudioDialog.Show(this, "No local preview image for this map.", "Preview map", StudioDialogKind.Warning);
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            StudioDialog.Show(this, ex.Message, "Preview map", StudioDialogKind.Error);
        }
    }

    private void MapsOpenInEditor_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.Button { Tag: MapCatalogEntry entry })
            return;
        var pud = FindLocalPudForMap(entry.Filename);
        if (string.IsNullOrWhiteSpace(pud))
        {
            StudioDialog.Show(this,
                $"Could not find \"{entry.Filename}\" under your Maps folder.",
                "Open in editor", StudioDialogKind.Warning);
            return;
        }
        var editor = MapEditorPath.Trim();
        if (!IsValidMapEditorPath(editor))
        {
            StudioDialog.Show(this,
                $"Map editor not found. The path must point to \"{MapEditorExeName}\" inside your install.",
                "Open in editor", StudioDialogKind.Warning);
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(editor, $"\"{pud}\"")
            {
                WorkingDirectory = Path.GetDirectoryName(editor) ?? string.Empty,
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            StudioDialog.Show(this, ex.Message, "Open in editor", StudioDialogKind.Error);
        }
    }

    private string? FindLocalPudForMap(string? filename)
    {
        if (string.IsNullOrWhiteSpace(filename)) return null;
        var mapsRoot = NormalizePathText(MapsPath);
        if (!Directory.Exists(mapsRoot)) return null;
        var want = Path.GetFileName(filename);
        try
        {
            return Directory.EnumerateFiles(mapsRoot, want, SearchOption.AllDirectories)
                .FirstOrDefault(File.Exists);
        }
        catch
        {
            return null;
        }
    }

    private void MapsFiltersReset_Click(object sender, RoutedEventArgs e)
    {
        MapsSearchText = "";
        MapsCategoryFilter = "All";
        MapsSizeFilter = "All";
        MapsPlayersFilter = "All";
        MapsTilesetFilter = "All";
        ApplyMapsFilters();
    }

    private void ApplyMapsFilters()
    {
        // Only maps with a local preview image.
        IEnumerable<MapCatalogEntry> q = _mapsCatalog.Where(m => m.HasLocalImage);

        var search = (MapsSearchText ?? "").Trim();
        var categoryActive = !string.Equals(MapsCategoryFilter, "All", StringComparison.OrdinalIgnoreCase);
        var sizeActive = !string.Equals(MapsSizeFilter, "All", StringComparison.OrdinalIgnoreCase);
        var playersWanted = 0;
        var playersActive = !string.Equals(MapsPlayersFilter, "All", StringComparison.OrdinalIgnoreCase) &&
                            int.TryParse(MapsPlayersFilter, out playersWanted);
        var tilesetActive = !string.Equals(MapsTilesetFilter, "All", StringComparison.OrdinalIgnoreCase);
        var anyFilter = categoryActive || sizeActive || playersActive || tilesetActive;

        // Empty search + no dropdown filters => empty list.
        // Empty search + at least one filter => show matching locals A-Z.
        if (search.Length == 0 && !anyFilter)
        {
            FilteredMaps.Clear();
            var localCount = _mapsCatalog.Count(m => m.HasLocalImage);
            MapsResultsCountText = _mapsCatalog.Count == 0
                ? "No maps loaded yet."
                : localCount == 0
                    ? "No local map images yet - download them under Paths."
                    : "Type in Search, or pick a filter, to list local map images.";
            RefreshMapsFilterSummary();
            return;
        }

        if (categoryActive)
            q = q.Where(m => string.Equals(m.Category, MapsCategoryFilter, StringComparison.OrdinalIgnoreCase));
        if (sizeActive)
            q = q.Where(m => string.Equals(m.Size, MapsSizeFilter, StringComparison.OrdinalIgnoreCase));
        if (playersActive)
            q = q.Where(m => m.PlayerCount == playersWanted);
        if (tilesetActive)
            q = q.Where(m => string.Equals(m.Tileset, MapsTilesetFilter, StringComparison.OrdinalIgnoreCase));

        List<MapCatalogEntry> list;
        if (search.Length == 0)
        {
            list = q.OrderBy(m => m.MapName, StringComparer.OrdinalIgnoreCase).ToList();
        }
        else
        {
            q = q.Where(m =>
                (m.MapName?.Contains(search, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (m.Filename?.Contains(search, StringComparison.OrdinalIgnoreCase) ?? false));

            // Prefer names that start with the query ("ga" -> Garden first).
            list = q
                .Select(m =>
                {
                    var name = m.MapName ?? "";
                    var starts = name.StartsWith(search, StringComparison.OrdinalIgnoreCase) ||
                                 (m.Filename?.StartsWith(search, StringComparison.OrdinalIgnoreCase) ?? false);
                    var wordStarts = name.Split(' ', StringSplitOptions.RemoveEmptyEntries)
                        .Any(w => w.StartsWith(search, StringComparison.OrdinalIgnoreCase));
                    var rank = starts ? 0 : wordStarts ? 1 : 2;
                    return (m, rank, name);
                })
                .OrderBy(t => t.rank)
                .ThenBy(t => t.name, StringComparer.OrdinalIgnoreCase)
                .Select(t => t.m)
                .ToList();
        }

        FilteredMaps.Clear();
        foreach (var m in list)
            FilteredMaps.Add(m);
        var localTotal = _mapsCatalog.Count(m => m.HasLocalImage);
        MapsResultsCountText = $"Showing {FilteredMaps.Count} of {localTotal} local maps.";
        RefreshMapsFilterSummary();
    }

    private void PushMapsFeedbackToStatus()
    {
        if (_activeTab != "path") return;
        void apply() => SetStatusLines(BuildPathLines());
        if (Dispatcher.CheckAccess()) apply();
        else Dispatcher.BeginInvoke(apply);
    }




    public bool CastleGoldTooltipFix
    {
        get => _castleGoldTooltipFix;
        set
        {
            if (_castleGoldTooltipFix == value) return;
            _castleGoldTooltipFix = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool EndGameObserve
    {
        get => false;
        set
        {
            // Feature 5 is greyed out for now — keep state forced off.
            if (_endGameObserve) {
                _endGameObserve = false;
                OnPropertyChanged();
                RefreshTabStatus();
            }
        }
    }

    public bool AllianceTeamNumbers
    {
        get => _allianceTeamNumbers;
        set
        {
            if (_allianceTeamNumbers == value) return;
            _allianceTeamNumbers = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool ComputerAnnihilatedChat
    {
        get => _computerAnnihilatedChat;
        set
        {
            if (_computerAnnihilatedChat == value) return;
            _computerAnnihilatedChat = value;
            OnPropertyChanged();
            RefreshTabStatus();
        }
    }

    public bool UpgradeNotifications
    {
        get => false;
        set
        {
            if (_blacksmithWorkCompleteChat)
            {
                _blacksmithWorkCompleteChat = false;
                OnPropertyChanged();
                OnPropertyChanged(nameof(UpgradeNotifications));
                RefreshTabStatus();
            }
        }
    }

    public bool BlacksmithWorkCompleteChat
    {
        get => false;
        set => UpgradeNotifications = value;
    }

    public bool ColorBlindMode
    {
        get => _colorBlindMode;
        set
        {
            if (_colorBlindMode == value) return;
            _colorBlindMode = value;
            OnPropertyChanged();
            AppTheme.ApplyColorBlindMode(value);
            RefreshCheckBoxVisuals();
            RefreshColorBlindModeButtons();
            RefreshColorBlindPresetButtons();
            SavePathSettings();
        }
    }

    public string ColorBlindPresetHintText
    {
        get => _colorBlindPresetHintText;
        private set
        {
            if (_colorBlindPresetHintText == value) return;
            _colorBlindPresetHintText = value;
            OnPropertyChanged();
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
            OnPropertyChanged(nameof(IsMapsOpenEnabled));
            // Restart indeterminate animation when Apply/Restore begins.
            if (value && ApplyProgressBar is not null)
            {
                ApplyProgressBar.IsIndeterminate = false;
                ApplyProgressBar.IsIndeterminate = true;
            }
        }
    }

    public bool IsApplyEnabled => !_isApplying;

    public bool IsApplyVisible => _activeTab is "colors" or "feature" or "bugfixes" or "network";

    public IReadOnlyList<LanguageOption> LanguageOptions => Localization.LanguageOptions;

    public string SelectedLanguageCode
    {
        get => Localization.CurrentCode;
        set
        {
            if (string.Equals(Localization.CurrentCode, value, StringComparison.OrdinalIgnoreCase)) return;
            if (!Localization.TrySetLanguage(value)) return;
            OnPropertyChanged();
            SavePathSettings();
        }
    }

    public bool IsColorsStatusSingleLine => _activeTab == "colors";

    public string GameInstallPath
    {
        get => _gameInstallPath;
        set
        {
            var next = value ?? string.Empty;
            if (_gameInstallPath == next) return;
            _gameInstallPath = next;
            OnPropertyChanged();
            OnPropertyChanged(nameof(GameInstallPathBorderBrush));
            SavePathSettings();
            RefreshTabStatus();
        }
    }

    public string MapEditorPath
    {
        get => _mapEditorPath;
        set
        {
            var next = value ?? string.Empty;
            if (_mapEditorPath == next) return;
            _mapEditorPath = next;
            OnPropertyChanged();
            OnPropertyChanged(nameof(MapEditorPathBorderBrush));
            SavePathSettings();
            RefreshTabStatus();
        }
    }

    public System.Windows.Media.Brush GameInstallPathBorderBrush =>
        IsValidGameRoot(NormalizeGameRoot(GameInstallPath)) ? DefaultPathBorderBrush : PendingIconBrush;

    public System.Windows.Media.Brush MapEditorPathBorderBrush =>
        IsValidMapEditorPath(MapEditorPath) ? DefaultPathBorderBrush : PendingIconBrush;

    public string MapsPath
    {
        get => _mapsPath;
        set
        {
            var next = value ?? string.Empty;
            if (_mapsPath == next) return;
            _mapsPath = next;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsMapsOpenEnabled));
            OnPropertyChanged(nameof(MapsPathBorderBrush));
            SavePathSettings();
            RefreshTabStatus();
        }
    }

    // Maps folder must actually contain .pud files (nested counts) before the
    // Open button works; an invalid path also gets the red error border.
    public bool IsMapsOpenEnabled => IsApplyEnabled && IsValidMapsPath(MapsPath);

    public System.Windows.Media.Brush MapsPathBorderBrush =>
        IsValidMapsPath(MapsPath) ? DefaultPathBorderBrush : PendingIconBrush;

    public string MapImagesPath
    {
        get => _mapImagesPath;
        set
        {
            var next = NormalizePathText(value);
            if (_mapImagesPath == next) return;
            _mapImagesPath = next;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsMapsDownloadEnabled));
            OnPropertyChanged(nameof(MapImagesPathBorderBrush));
            OnPropertyChanged(nameof(IsMapImagesOpenEnabled));
            RefreshMapsSyncStatus();
        }
    }

    public bool IsMapImagesOpenEnabled => IsApplyEnabled && IsValidMapImagesPath(MapImagesPath);

    public System.Windows.Media.Brush MapImagesPathBorderBrush =>
        IsValidMapImagesPath(MapImagesPath) ? DefaultPathBorderBrush : PendingIconBrush;



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
                ? "Beta Version unknown"
                : $"Beta Version {version.Major}.{version.Minor}.{version.Build}";
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

    private static string IncorrectPathMessage() => Localization.Get("Status.IncorrectPath");

    private void OnLocalizationChanged()
    {
        Title = Localization.Get("App.Title");
        OnPropertyChanged(nameof(SelectedLanguageCode));
        Loc.Refresh(this);
        if (IsCustomColorPreset(_selectedColorBlindPreset) || string.IsNullOrEmpty(_selectedColorBlindPreset))
            ColorBlindPresetHintText = Localization.Get("ColorBlind.CustomHint");
        else if (ColorBlindPresets.TryGet(_selectedColorBlindPreset, out _, out var hint))
            ColorBlindPresetHintText = hint;
        else
            ColorBlindPresetHintText = Localization.Get("ColorBlind.DefaultHint");
        RefreshTabStatus();
    }

    private string ColorBlindPresetLabel(string presetKey)
    {
        if (presetKey.Equals("custom", StringComparison.OrdinalIgnoreCase) ||
            presetKey.Equals("original", StringComparison.OrdinalIgnoreCase))
            return Localization.Get("Btn.Custom");
        var key = $"ColorBlind.{char.ToUpper(presetKey[0])}{presetKey.Substring(1)}";
        if (presetKey.Equals("deuteranopia", StringComparison.OrdinalIgnoreCase))
            key = "ColorBlind.Deuteranopia";
        else if (presetKey.Equals("protanopia", StringComparison.OrdinalIgnoreCase))
            key = "ColorBlind.Protanopia";
        else if (presetKey.Equals("tritanopia", StringComparison.OrdinalIgnoreCase))
            key = "ColorBlind.Tritanopia";
        return Localization.Get(key);
    }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;
        Localization.Changed += OnLocalizationChanged;

        _hookWatchTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _hookWatchTimer.Tick += (_, _) => UpdateExtraHookStatus(forceInject: false);
        _dropMonitorTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _dropMonitorTimer.Tick += (_, _) => RefreshDropMonitorLog();
        _selfMonitorTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _selfMonitorTimer.Tick += (_, _) => RefreshSelfMonitor();
        _networkMonitorTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _networkMonitorTimer.Tick += (_, _) => RefreshNetworkMonitorLog();

        try
        {
            _enginePath = FindEnginePath();
            var modDir = Path.GetDirectoryName(_enginePath)!;
            _configPath = Path.Combine(modDir, "player-colors.json");
            _extraConfigPath = Path.Combine(modDir, "extra-features.json");
            _settingsPath = Path.Combine(modDir, "studio-settings.json");
            _nativeDir = Path.Combine(modDir, "native");
            LoadGameInstallPath(modDir);
            Loc.Refresh(this);
            _colorBlindPresetHintText = Localization.Get("ColorBlind.DefaultHint");
            OnPropertyChanged(nameof(ColorBlindPresetHintText));
            LoadCards();
            CaptureAppliedColors();
            CaptureAppliedUtilColors();
            foreach (var card in Cards)
            {
                card.PropertyChanged += (_, args) =>
                {
                    if (_activeTab != "colors") return;
                    if (args.PropertyName is null or nameof(ColorCard.Hex))
                    {
                        // Manual slot edits leave colorblind presets; Apply must use the cards.
                        if (!_suppressColorBlindPresetSync &&
                            !IsCustomColorPreset(_selectedColorBlindPreset) &&
                            !string.IsNullOrEmpty(_selectedColorBlindPreset))
                        {
                            _selectedColorBlindPreset = "custom";
                            ColorBlindPresetHintText = Localization.Get("ColorBlind.CustomHint");
                            RefreshColorBlindPresetButtons();
                        }
                        RefreshTabStatus();
                    }
                };
            }
            WireOtherCards();
            LoadExtraFeatures();
            AppTheme.ApplyColorBlindMode(_colorBlindMode);
            RefreshCheckBoxVisuals();
            RefreshColorBlindModeButtons();
            RefreshColorBlindPresetButtons();
            _hookWatchTimer.Start();
            InitializeDropMonitor();
            _networkMonitorTimer.Start();
            RefreshNetworkMonitorLog();
            UpdateExtraHookStatus(forceInject: false);
            RefreshTabStatus();
        }
        catch (Exception ex)
        {
            _enginePath = string.Empty;
            _configPath = string.Empty;
            _extraConfigPath = string.Empty;
            _nativeDir = string.Empty;
            SetStatusLines(StatusLine("✕", PendingIconBrush, ex.Message));
            StudioDialog.Show(this, ex.Message, "Quality of Life Modding", StudioDialogKind.Error);
        }
    }

    private void MainTabs_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        // TabControl bubbles SelectionChanged from child controls — only handle tab switches.
        if (!ReferenceEquals(e.Source, MainTabs)) return;
        if (MainTabs.SelectedItem is not TabItem tab) return;
        _activeTab = tab.Tag as string ?? "colors";
        OnPropertyChanged(nameof(IsApplyVisible));
        OnPropertyChanged(nameof(IsColorsStatusSingleLine));
        RefreshTabStatus();
        if (_activeTab == "maps")
            _ = EnsureMapsCatalogLoadedAsync();
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
        return HasPendingUtilChanges() || HasPendingColorBlindPresetChanges();
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
    // Same border color the app-wide TextBox style uses.
    private static readonly System.Windows.Media.Brush DefaultPathBorderBrush = CreateBrush(0x5A, 0x73, 0x98);
    private static readonly System.Windows.Media.Brush ReadyIconBrush = CreateBrush(0x2E, 0xC8, 0x5A);
    private static readonly System.Windows.Media.Brush PendingButtonBrush = CreateBrush(0xC4, 0x2B, 0x2B);
    private static readonly System.Windows.Media.Brush PendingButtonBorderBrush = CreateBrush(0xE5, 0x5A, 0x5A);
    private static readonly System.Windows.Media.Brush ReadyButtonBrush = CreateBrush(0x2E, 0xA8, 0x5C);
    private static readonly System.Windows.Media.Brush ReadyButtonBorderBrush = CreateBrush(0x4C, 0xC3, 0x7A);
    private static readonly System.Windows.Media.Brush SegmentSelectedBrush = CreateBrush(0x2E, 0xA8, 0x5C);
    private static readonly System.Windows.Media.Brush SegmentSelectedBorderBrush = CreateBrush(0x4C, 0xC3, 0x7A);
    private static readonly System.Windows.Media.Brush SegmentUnselectedBrush = CreateBrush(0x33, 0x48, 0x68);
    private static readonly System.Windows.Media.Brush SegmentUnselectedBorderBrush = CreateBrush(0x5A, 0x73, 0x98);

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
    private bool HasPendingChatTimestamps() => _chatTimestamps != _appliedChatTimestamps;
    private bool HasPendingChatHistory() => _chatHistory != _appliedChatHistory;
    private bool HasPendingMpLobbyChatScrollFix() => false;
    private bool HasPendingLobbyMapClickOpen() =>
        _lobbyMapClickOpen != _appliedLobbyMapClickOpen;
    private bool HasPendingEndGameObserve() => false; // Feature 5 disabled
    private bool HasPendingAllianceTeamNumbers() =>
        _allianceTeamNumbers != _appliedAllianceTeamNumbers;
    private bool HasPendingComputerAnnihilatedChat() =>
        _computerAnnihilatedChat != _appliedComputerAnnihilatedChat;
    private bool HasPendingFeatureChanges() =>
        HasPendingMarkGone() || HasPendingChatColoredNames() || HasPendingChatTimestamps() ||
        HasPendingAllianceTeamNumbers() || HasPendingComputerAnnihilatedChat() ||
        HasPendingLobbyMapClickOpen();
    private bool HasPendingBugFixChanges() =>
        HasPendingChatPause() || HasPendingChatHistory() ||
        HasPendingMpLobbyChatScrollFix() || HasPendingCastleGoldTooltipFix();
    private bool HasPendingCastleGoldTooltipFix() =>
        _castleGoldTooltipFix != _appliedCastleGoldTooltipFix;
    private bool HasPendingNetworkMonitor() => _networkMonitor != _appliedNetworkMonitor;

    private static StatusLineItem StatusLine(string icon, System.Windows.Media.Brush brush, string text) =>
        new(icon, brush, text);

    private void SetStatusLines(params StatusLineItem[] lines)
    {
        StatusLines.Clear();
        foreach (var line in lines)
            StatusLines.Add(line);
    }

    private bool CurrentCardsMatchVanilla() =>
        Cards.Where(c => c.IsEnabled).All(c =>
            string.Equals(ColorCard.NormalizeHex(c.Hex), ColorCard.NormalizeHex(c.VanillaHex),
                StringComparison.OrdinalIgnoreCase))
        && OtherCards.Where(c => c.IsEnabled).All(c =>
            string.Equals(ColorCard.NormalizeHex(c.Hex), ColorCard.NormalizeHex(c.VanillaHex),
                StringComparison.OrdinalIgnoreCase));

    private static bool IsCustomColorPreset(string presetKey) =>
        string.Equals(presetKey, "custom", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(presetKey, "original", StringComparison.OrdinalIgnoreCase);

    private bool IsCustomOriginalMode() =>
        IsCustomColorPreset(_selectedColorBlindPreset) &&
        CurrentCardsMatchVanilla() &&
        !HasManualAppliedColors();

    private StatusLineItem BuildColorsLine()
    {
        if (HasPendingColorChanges())
        {
            return StatusLine("✕", PendingIconBrush,
                Localization.Get("Status.Colors.Pending"));
        }

        if (!string.IsNullOrEmpty(_appliedColorBlindPreset) &&
            !IsCustomColorPreset(_appliedColorBlindPreset))
        {
            var label = ColorBlindPresetLabel(_appliedColorBlindPreset);
            return StatusLine("✓", ReadyIconBrush,
                Localization.Format("Status.Colors.PresetUsed", label));
        }

        if (IsCustomOriginalMode() || IsCustomColorPreset(_appliedColorBlindPreset))
        {
            if (HasManualAppliedColors())
            {
                return StatusLine("✓", ReadyIconBrush,
                    Localization.Get("Status.Colors.Manual"));
            }

            return StatusLine("✓", ReadyIconBrush,
                Localization.Get("Status.Colors.Original"));
        }

        return HasManualAppliedColors()
            ? StatusLine("✓", ReadyIconBrush,
                Localization.Get("Status.Colors.Manual"))
            : StatusLine("✓", ReadyIconBrush, Localization.Get("Status.Colors.Vanilla"));
    }

    private bool HasPendingChangesForActiveTab() => _activeTab switch
    {
        "colors" => HasPendingColorChanges(),
        "feature" => HasPendingFeatureChanges(),
        "bugfixes" => HasPendingBugFixChanges(),
        "network" => HasPendingNetworkMonitor(),
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
                    Localization.Get("Status.ChangesPending")));
            }
            else
            {
                SetStatusLines(StatusLine("✓", ReadyIconBrush,
                    Localization.Get("Status.Feature.Installed")));
            }
            return;
        }

        if (_activeTab == "bugfixes")
        {
            if (HasPendingChangesForActiveTab())
            {
                SetStatusLines(StatusLine("✕", PendingIconBrush,
                    Localization.Get("Status.ChangesPending")));
            }
            else
            {
                SetStatusLines(StatusLine("✓", ReadyIconBrush,
                    Localization.Get("Status.Bugfixes.Installed")));
            }
            return;
        }

        if (_activeTab == "network")
        {
            if (HasPendingChangesForActiveTab())
            {
                SetStatusLines(StatusLine("✕", PendingIconBrush,
                    Localization.Get("Status.Network.Pending")));
            }
            else if (_appliedNetworkMonitor)
            {
                SetStatusLines(StatusLine("✓", ReadyIconBrush,
                    Localization.Get("Status.Network.On")));
            }
            else
            {
                SetStatusLines(StatusLine("✓", ReadyIconBrush,
                    Localization.Get("Status.Network.Off")));
            }
            return;
        }

        if (_activeTab == "path")
        {
            SetStatusLines(BuildPathLines());
            return;
        }

        if (_activeTab is "settings")
        {
            SetStatusLines(StatusLine("✓", ReadyIconBrush, AppVersionText));
            return;
        }

        if (_activeTab == "maps")
        {
            SetStatusLines(BuildMapsTabLines());
            return;
        }

        SetStatusLines(BuildColorsLine());
    }

    private StatusLineItem[] BuildMapsTabLines()
    {
        RefreshMapsSyncStatus();
        RefreshMapsPudSyncStatus();

        var lines = new List<StatusLineItem>();

        // Prefer live progress while a download is running.
        if (_mapsDownloadBusy && !string.IsNullOrWhiteSpace(_mapsDownloadProgressText))
        {
            lines.Add(StatusLine("✕", PendingIconBrush, _mapsDownloadProgressText.Trim()));
        }

        var imageLine = string.IsNullOrWhiteSpace(_mapsSyncStatusText)
            ? "Last image download: none yet (Paths → Map images → Download)."
            : (_mapsSyncStatusText.StartsWith("Last image download", StringComparison.OrdinalIgnoreCase)
                ? _mapsSyncStatusText
                : "Last image download: " + _mapsSyncStatusText);
        var imagePending = imageLine.Contains("none yet", StringComparison.OrdinalIgnoreCase) ||
                           imageLine.Contains("No download", StringComparison.OrdinalIgnoreCase) ||
                           imageLine.Contains("Could not", StringComparison.OrdinalIgnoreCase);
        lines.Add(imagePending
            ? StatusLine("✕", PendingIconBrush, imageLine)
            : StatusLine("✓", ReadyIconBrush, imageLine));

        var pudLine = string.IsNullOrWhiteSpace(_mapsPudSyncStatusText)
            ? "Last maps download: none yet (Paths → Maps folder path → Download)."
            : _mapsPudSyncStatusText;
        var pudPending = pudLine.Contains("none yet", StringComparison.OrdinalIgnoreCase) ||
                         pudLine.Contains("Could not", StringComparison.OrdinalIgnoreCase) ||
                         pudLine.Contains("login", StringComparison.OrdinalIgnoreCase) ||
                         pudLine.Contains("401", StringComparison.OrdinalIgnoreCase);
        lines.Add(pudPending
            ? StatusLine("✕", PendingIconBrush, pudLine)
            : StatusLine("✓", ReadyIconBrush, pudLine));

        return lines.ToArray();
    }

    private void RefreshMapsPudSyncStatus()
    {
        try
        {
            EnsureMapsRootLayout();
            var mapsRoot = NormalizePathText(MapsPath);
            var dir = Path.Combine(mapsRoot, "downloads");
            var count = Directory.Exists(dir)
                ? Directory.GetFiles(dir, "*.pud").Length
                : 0;
            var metaPath = Path.Combine(dir, "_pud_download_meta.json");
            string last = "never";
            if (File.Exists(metaPath))
            {
                try
                {
                    using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(metaPath));
                    if (doc.RootElement.TryGetProperty("lastDownloadUtc", out var p))
                    {
                        var raw = p.GetString();
                        if (!string.IsNullOrWhiteSpace(raw) &&
                            DateTime.TryParse(raw, null, System.Globalization.DateTimeStyles.RoundtripKind, out var utc))
                            last = utc.ToLocalTime().ToString("g");
                        else if (!string.IsNullOrWhiteSpace(raw))
                            last = raw!;
                    }
                }
                catch { /* ignore */ }
            }

            _mapsPudSyncStatusText = count > 0 || File.Exists(metaPath)
                ? $"Last maps download: {last} · {count} .pud files (downloads folder)"
                : "Last maps download: none yet (Paths → Maps folder path → Download).";
        }
        catch (Exception ex)
        {
            _mapsPudSyncStatusText = "Last maps download: could not read downloads folder: " + ex.Message;
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
        throw new FileNotFoundException("The patch engine was not found. Reinstall Quality of Life Modding.");
    }

    private void LoadGameInstallPath(string modDir)
    {
        var settings = TryReadSavedSettings();
        Localization.Initialize(settings?.Language);
        Title = Localization.Get("App.Title");
        OnPropertyChanged(nameof(LanguageOptions));
        OnPropertyChanged(nameof(SelectedLanguageCode));

        var fromSettings = string.IsNullOrWhiteSpace(settings?.GameRootPath) ? null : settings!.GameRootPath!.Trim();
        var inferred = TryInferGameRootFromModDir(modDir);
        var chosen = !string.IsNullOrWhiteSpace(fromSettings)
            ? fromSettings!
            : (!string.IsNullOrWhiteSpace(inferred) ? inferred! : DefaultGameRootPath);
        _gameInstallPath = NormalizeGameRoot(chosen);
        _appliedGameInstallPath = _gameInstallPath;
        OnPropertyChanged(nameof(GameInstallPath));
        OnPropertyChanged(nameof(GameInstallPathBorderBrush));

        // Saved path wins if it is still the real editor; otherwise pick up the
        // standard location the install wizard uses under the game root.
        var savedEditor = settings?.MapEditorPath?.Trim();
        _mapEditorPath = IsValidMapEditorPath(savedEditor ?? string.Empty)
            ? savedEditor!
            : (TryFindInstalledMapEditor(_gameInstallPath) ?? DefaultMapEditorPathFor(_gameInstallPath));
        _appliedMapEditorPath = _mapEditorPath;
        OnPropertyChanged(nameof(MapEditorPath));
        OnPropertyChanged(nameof(MapEditorPathBorderBrush));

        // Saved path wins only if it passes the .pud check; otherwise fall back
        // to a detected maps folder from the install.
        var savedMaps = settings?.MapsPath?.Trim();
        _mapsPath = IsValidMapsPath(savedMaps ?? string.Empty)
            ? savedMaps!
            : (TryFindInstalledMapsFolder(_gameInstallPath) ?? DefaultMapsPathFor(_gameInstallPath));
        _appliedMapsPath = _mapsPath;
        var savedImages = settings?.MapImagesPath?.Trim();
        _mapImagesPath = IsValidMapImagesPath(savedImages ?? string.Empty)
            ? NormalizePathText(savedImages!)
            : DefaultMapImagesPathFor(_mapsPath);
        _appliedMapImagesPath = _mapImagesPath;
        OnPropertyChanged(nameof(MapImagesPath));
        OnPropertyChanged(nameof(IsMapImagesOpenEnabled));
        OnPropertyChanged(nameof(MapImagesPathBorderBrush));
        OnPropertyChanged(nameof(IsMapsDownloadEnabled));
        RefreshMapsSyncStatus();
        OnPropertyChanged(nameof(MapsPath));
        OnPropertyChanged(nameof(IsMapsOpenEnabled));
        OnPropertyChanged(nameof(MapsPathBorderBrush));

        _colorBlindMode = settings?.ColorBlindMode == true;
        OnPropertyChanged(nameof(ColorBlindMode));
    }

    private StudioSettings? TryReadSavedSettings()
    {
        if (!File.Exists(_settingsPath)) return null;
        try
        {
            return JsonSerializer.Deserialize<StudioSettings>(File.ReadAllText(_settingsPath), JsonOpts);
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

    private static string DefaultMapEditorPathFor(string gameRoot) =>
        Path.Combine(gameRoot, "x86", MapEditorExeName);

    // The install wizard places the editor in x86 (and a copy in the root).
    private static string? TryFindInstalledMapEditor(string gameRoot)
    {
        var candidates = new[]
        {
            Path.Combine(gameRoot, "x86", MapEditorExeName),
            Path.Combine(gameRoot, MapEditorExeName),
        };
        return candidates.FirstOrDefault(File.Exists);
    }


    private static string DefaultMapImagesPathFor(string mapsPath)
    {
        var root = NormalizePathText(mapsPath);
        if (string.IsNullOrWhiteSpace(root))
            root = DefaultMapsPathFor(DefaultGameRootPath);
        return Path.Combine(root, "map_images");
    }

    private static bool IsValidMapImagesPath(string path)
    {
        var pth = NormalizePathText(path);
        if (string.IsNullOrWhiteSpace(pth)) return false;
        try
        {
            if (Directory.Exists(pth)) return true;
            var parent = Path.GetDirectoryName(pth);
            return !string.IsNullOrWhiteSpace(parent) && Directory.Exists(parent);
        }
        catch { return false; }
    }

    private static string DefaultMapsPathFor(string gameRoot) =>
        Path.Combine(gameRoot, "x86", "Maps");

    // Prefer a folder that actually holds .pud maps: some installs keep x86\Maps
    // empty while the shipped maps live in x86\Data\Maps.
    private static string? TryFindInstalledMapsFolder(string gameRoot)
    {
        var candidates = new[]
        {
            Path.Combine(gameRoot, "x86", "Maps"),
            Path.Combine(gameRoot, "x86", "Data", "Maps"),
        };
        return candidates.FirstOrDefault(c => Directory.Exists(c) && FolderContainsPudFiles(c));
    }

    private static string NormalizePathText(string path) =>
        (path ?? string.Empty).Trim().TrimEnd('\\', '/');

    // The editor only works via its fixed exe name — any other exe is rejected.
    private static bool IsValidMapEditorPath(string path)
    {
        var trimmed = (path ?? string.Empty).Trim();
        return trimmed.Length > 0 &&
               File.Exists(trimmed) &&
               string.Equals(Path.GetFileName(trimmed), MapEditorExeName, StringComparison.OrdinalIgnoreCase);
    }

    // Maps can sit in subfolders (e.g. x86\Maps\AllMaps) — search a few levels deep.
    private static bool FolderContainsPudFiles(string path, int depth = 4)
    {
        try
        {
            if (Directory.EnumerateFiles(path, "*.pud").Any()) return true;
            if (depth <= 0) return false;
            foreach (var sub in Directory.EnumerateDirectories(path))
            {
                if (FolderContainsPudFiles(sub, depth - 1)) return true;
            }
        }
        catch
        {
            // Unreadable folder — treat as no maps.
        }
        return false;
    }

    private static bool IsValidMapsPath(string path)
    {
        var normalized = NormalizePathText(path);
        return !string.IsNullOrWhiteSpace(normalized) &&
               Directory.Exists(normalized) &&
               FolderContainsPudFiles(normalized);
    }

    private StatusLineItem[] BuildPathLines()
    {
        var lines = new List<StatusLineItem>();
        var path = NormalizeGameRoot(GameInstallPath);

        if (!IsValidGameRoot(path))
        {
            lines.Add(StatusLine("✕", PendingIconBrush,
                "Install folder not found (need an x86\\Data folder). Browse to your Warcraft II Remastered folder."));
        }
        else
        {
            lines.Add(StatusLine("✓", ReadyIconBrush, $"Using game install: {path}."));
        }

        var mapsPath = NormalizePathText(MapsPath);
        if (!Directory.Exists(mapsPath))
        {
            lines.Add(StatusLine("✕", PendingIconBrush,
                "Maps folder not found - browse to your maps folder (x86\\Maps)."));
        }
        else if (!FolderContainsPudFiles(mapsPath))
        {
            lines.Add(StatusLine("✕", PendingIconBrush,
                "No .pud map files found in this folder - browse to your maps folder (x86\\Maps)."));
        }
        else
        {
            lines.Add(StatusLine("✓", ReadyIconBrush, "Maps folder found (contains .pud maps)."));
        }

        var imagesFeedback = MapsImagesFeedbackText?.Trim();
        if (!string.IsNullOrWhiteSpace(imagesFeedback))
        {
            var busy = _mapsDownloadBusy ||
                       imagesFeedback.StartsWith("Downloading", StringComparison.OrdinalIgnoreCase) ||
                       imagesFeedback.StartsWith("Listing", StringComparison.OrdinalIgnoreCase) ||
                       imagesFeedback.StartsWith("Checking", StringComparison.OrdinalIgnoreCase) ||
                       imagesFeedback.StartsWith("Map download", StringComparison.OrdinalIgnoreCase);
            var failed = imagesFeedback.StartsWith("Failed", StringComparison.OrdinalIgnoreCase) ||
                         imagesFeedback.StartsWith("Could not", StringComparison.OrdinalIgnoreCase) ||
                         imagesFeedback.Contains("login", StringComparison.OrdinalIgnoreCase);
            if (busy || failed)
                lines.Add(StatusLine("✕", PendingIconBrush, imagesFeedback));
            else
                lines.Add(StatusLine("✓", ReadyIconBrush, imagesFeedback));
        }

        lines.Add(IsValidMapEditorPath(MapEditorPath)
            ? StatusLine("✓", ReadyIconBrush, "Map editor found.")
            : StatusLine("✕", PendingIconBrush,
                $"Map editor not found - the path must point to \"{MapEditorExeName}\" inside your install."));

        return [.. lines];
    }

    private void ShowIncorrectPathStatus()
    {
        _skipNextStatusRefresh = true;
        SetStatusLines(StatusLine("✕", PendingIconBrush, IncorrectPathMessage()));
        ApplyButtonBrush = PendingButtonBrush;
        ApplyButtonBorderBrush = PendingButtonBorderBrush;
    }

    private bool IsCurrentApplyPathValid() =>
        IsValidGameRoot(NormalizeGameRoot(_appliedGameInstallPath));

    // Paths are saved the moment they change: whatever is set is remembered.
    // The game root used by the other tabs only advances when it validates,
    // so a half-typed path never breaks Apply/Restore.
    private void SavePathSettings()
    {
        var root = NormalizeGameRoot(GameInstallPath);
        if (IsValidGameRoot(root)) _appliedGameInstallPath = root;
        _appliedMapEditorPath = NormalizePathText(MapEditorPath);
        _appliedMapsPath = NormalizePathText(MapsPath);
        _appliedMapImagesPath = NormalizePathText(MapImagesPath);

        if (string.IsNullOrEmpty(_settingsPath)) return;
        try
        {
            var settings = new StudioSettings
            {
                GameRootPath = NormalizeGameRoot(_appliedGameInstallPath),
                MapEditorPath = _appliedMapEditorPath,
                MapsPath = _appliedMapsPath,
                MapImagesPath = _appliedMapImagesPath,
                ColorBlindMode = _colorBlindMode,
                Language = Localization.CurrentCode,
            };
            File.WriteAllText(_settingsPath, JsonSerializer.Serialize(settings, JsonOpts));
        }
        catch
        {
            // Saving settings must never break typing in the path boxes.
        }
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

    private void BrowseMapEditorPath_Click(object sender, RoutedEventArgs e)
    {
        var current = MapEditorPath.Trim();
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Select the Warcraft II map editor",
            // Only the fixed editor exe works — hide everything else.
            Filter = $"Warcraft II Map Editor|{MapEditorExeName}",
            FileName = MapEditorExeName,
            InitialDirectory = File.Exists(current)
                ? Path.GetDirectoryName(current)
                : NormalizeGameRoot(GameInstallPath),
        };
        if (dialog.ShowDialog() != true) return;
        MapEditorPath = dialog.FileName;
    }

    private void DefaultMapEditorPath_Click(object sender, RoutedEventArgs e)
    {
        var root = NormalizeGameRoot(GameInstallPath);
        MapEditorPath = TryFindInstalledMapEditor(root) ?? DefaultMapEditorPathFor(root);
    }

    private void OpenMapEditor_Click(object sender, RoutedEventArgs e)
    {
        var path = MapEditorPath.Trim();
        if (!IsValidMapEditorPath(path))
        {
            StudioDialog.Show(this,
                $"Map editor not found. The path must point to \"{MapEditorExeName}\" inside your install.",
                "Open map editor", StudioDialogKind.Warning);
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(path)
            {
                WorkingDirectory = Path.GetDirectoryName(path) ?? string.Empty,
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            StudioDialog.Show(this, ex.Message, "Open map editor", StudioDialogKind.Error);
        }
    }


    private void BrowseMapImagesPath_Click(object sender, RoutedEventArgs e)
    {
        var current = NormalizePathText(MapImagesPath);
        using var dialog = new System.Windows.Forms.FolderBrowserDialog
        {
            Description = "Select map images folder",
            SelectedPath = Directory.Exists(current) ? current : (Directory.Exists(MapsPath) ? MapsPath : ""),
            ShowNewFolderButton = true,
        };
        if (dialog.ShowDialog() != System.Windows.Forms.DialogResult.OK) return;
        MapImagesPath = dialog.SelectedPath;
    }

    private void DefaultMapImagesPath_Click(object sender, RoutedEventArgs e)
    {
        MapImagesPath = DefaultMapImagesPathFor(MapsPath);
    }

    private void BrowseMapsPath_Click(object sender, RoutedEventArgs e)
    {
        var current = NormalizePathText(MapsPath);
        using var dialog = new Forms.FolderBrowserDialog
        {
            Description = "Select your Warcraft II maps folder",
            UseDescriptionForTitle = true,
            SelectedPath = Directory.Exists(current)
                ? current
                : NormalizeGameRoot(GameInstallPath),
        };
        if (dialog.ShowDialog() != Forms.DialogResult.OK) return;
        MapsPath = dialog.SelectedPath;
    }

    private void DefaultMapsPath_Click(object sender, RoutedEventArgs e)
    {
        var root = NormalizeGameRoot(GameInstallPath);
        MapsPath = TryFindInstalledMapsFolder(root) ?? DefaultMapsPathFor(root);
    }

    private void OpenMapsFolder_Click(object sender, RoutedEventArgs e)
    {
        var path = NormalizePathText(MapsPath);
        if (!IsValidMapsPath(path))
        {
            StudioDialog.Show(this,
                "Maps folder not found. The path must point to a folder that contains .pud map files.",
                "Open maps folder", StudioDialogKind.Warning);
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo("explorer.exe", $"\"{path}\"")
            {
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            StudioDialog.Show(this, ex.Message, "Open maps folder", StudioDialogKind.Error);
        }
    }

    private void GameInstallPath_LostFocus(object sender, RoutedEventArgs e) =>
        RefreshTabStatus();

    private string RequireAppliedGameRoot()
    {
        var path = NormalizeGameRoot(_appliedGameInstallPath);
        if (!IsValidGameRoot(path))
            throw new InvalidOperationException(IncorrectPathMessage());
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
                : "Select your player color with 'Choose color...' or by typing a hex code, " +
                  "or press 'Reset' to set the color back to the vanilla settings.";
            Cards.Add(new ColorCard(vanilla.Player, current, vanilla.Color, !disabled, description: description));
        }

        OtherCards.Clear();
        OtherCards.Add(MakeOtherCard(
            defaults.SelectionHighlight, saved.SelectionHighlight, "#00FF00",
            "selectionHighlight", "Self highlight",
            "Changes the glow around your own units and buildings when they are selected, " +
            "the box you drag to select many units at once, and the matching selected dots on the minimap. " +
            "Select a color with 'Choose color...' or by typing a hex code, " +
            "or press 'Reset' to set the color back to the vanilla settings.",
            enabled: true));
        OtherCards.Add(MakeOtherCard(
            defaults.CritterHighlight, saved.CritterHighlight, "#A2A2A6",
            "critterHighlight", "Critter minimap",
            "Changes the minimap color for critters. " +
            "Select a color with 'Choose color...' or by typing a hex code, " +
            "or press 'Reset' to set the color back to the vanilla settings.",
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
                new PlayerColor(2, "#0C49CE"),
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
        Players = BuildPlayersForApply(),
        SelectionHighlight = OtherCurrent("selectionHighlight", "#00FF00"),
        // Keep vanilla for unused highlight slots so Apply script stays stable.
        EnemySelectionHighlight = "#FF0000",
        AllyHighlight = "#FFFF00",
        CritterHighlight = OtherCurrent("critterHighlight", "#A2A2A6"),
        GoldMineHighlight = "#694114",
        OilPatchHighlight = "#FFFBF3",
    };

    // Colorblind presets: take hexes from the preset table so stale TextBox focus
    // cannot overwrite them on Apply — but only while the cards still match that
    // preset. Any manual edit switches to Custom and Apply must use the cards.
    private List<PlayerColor> BuildPlayersForApply()
    {
        if (!IsCustomColorPreset(_selectedColorBlindPreset) &&
            ColorBlindPresets.TryGet(_selectedColorBlindPreset, out var presetHexes, out _) &&
            CardsMatchColorBlindPreset(presetHexes))
        {
            return Cards.Select(card =>
            {
                if (card.IsEnabled && card.Player >= 1 && card.Player <= presetHexes.Length)
                {
                    return new PlayerColor(card.Player,
                        ColorCard.NormalizeHex(presetHexes[card.Player - 1]));
                }
                return new PlayerColor(card.Player, ColorCard.NormalizeHex(card.Hex));
            }).ToList();
        }

        return Cards.Select(card => new PlayerColor(card.Player, ColorCard.NormalizeHex(card.Hex))).ToList();
    }

    private bool CardsMatchColorBlindPreset(string[] presetHexes)
    {
        foreach (var card in Cards.Where(c => c.IsEnabled))
        {
            if (card.Player < 1 || card.Player > presetHexes.Length)
                continue;
            if (!string.Equals(
                    ColorCard.NormalizeHex(card.Hex),
                    ColorCard.NormalizeHex(presetHexes[card.Player - 1]),
                    StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
        }
        return true;
    }

    private void SwitchToCustomColorMode(bool resetToVanilla)
    {
        _selectedColorBlindPreset = "custom";
        if (resetToVanilla)
        {
            _suppressColorBlindPresetSync = true;
            try
            {
                foreach (var card in Cards.Where(c => c.IsEnabled))
                    card.Reset();
            }
            finally
            {
                _suppressColorBlindPresetSync = false;
            }
        }

        // Cards hold preset colors after a colorblind selection; push them into every
        // hex TextBox (including any that still had focus) before Custom Apply reads cards.
        SyncHexTextBoxesFromModel();
        Keyboard.ClearFocus();

        ColorBlindPresetHintText = Localization.Get("ColorBlind.CustomHint");
        RefreshColorBlindPresetButtons();
        RefreshTabStatus();
    }

    private void ApplyPresetColorsToCards(string presetKey)
    {
        if (!ColorBlindPresets.TryGet(presetKey, out var hexes, out _)) return;
        _suppressColorBlindPresetSync = true;
        try
        {
            for (var i = 0; i < hexes.Length; i++)
            {
                var card = Cards.FirstOrDefault(c => c.Player == i + 1 && c.IsEnabled);
                if (card is not null)
                    card.Hex = hexes[i];
            }
        }
        finally
        {
            _suppressColorBlindPresetSync = false;
        }
    }

    private void SyncCardsFromConfig(ColorConfig config)
    {
        _suppressColorBlindPresetSync = true;
        try
        {
            foreach (var player in config.Players)
            {
                var card = Cards.FirstOrDefault(c => c.Player == player.Player);
                if (card is not null)
                    card.Hex = player.Color;
            }
            SyncHexTextBoxesFromModel();
        }
        finally
        {
            _suppressColorBlindPresetSync = false;
        }
    }

    private void SyncHexTextBoxesFromModel()
    {
        foreach (var textBox in FindVisualChildren<System.Windows.Controls.TextBox>(this))
        {
            if (textBox.DataContext is not ColorCard card) continue;
            // UpdateTarget alone does not refresh a focused TextBox; set Text from the
            // model so a stale box cannot write old hexes back on Apply.
            if (!string.Equals(textBox.Text, card.Hex, StringComparison.OrdinalIgnoreCase))
                textBox.Text = card.Hex;
            textBox.GetBindingExpression(System.Windows.Controls.TextBox.TextProperty)?.UpdateTarget();
        }
    }

    private bool HasPendingColorBlindPresetChanges() =>
        !string.Equals(_selectedColorBlindPreset, _appliedColorBlindPreset, StringComparison.OrdinalIgnoreCase);

    private void MirrorPlayerColorsJson(string json)
    {
        try
        {
            var installConfig = Path.Combine(
                NormalizeGameRoot(_appliedGameInstallPath),
                "x86", "Mods", "PlayerColorStudio", "mod", "player-colors.json");
            if (string.IsNullOrWhiteSpace(installConfig) ||
                string.Equals(Path.GetFullPath(installConfig), Path.GetFullPath(_configPath),
                    StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            var dir = Path.GetDirectoryName(installConfig);
            if (!string.IsNullOrEmpty(dir))
                Directory.CreateDirectory(dir);
            File.WriteAllText(installConfig, json);
        }
        catch { /* best-effort mirror for the in-game hook */ }
    }

    private void SyncNativeToGameInstall()
    {
        if (string.IsNullOrEmpty(_nativeDir) || !Directory.Exists(_nativeDir)) return;

        try
        {
            var installNative = Path.Combine(
                NormalizeGameRoot(_appliedGameInstallPath),
                "x86", "Mods", "PlayerColorStudio", "mod", "native");
            if (string.IsNullOrWhiteSpace(installNative) ||
                string.Equals(Path.GetFullPath(installNative), Path.GetFullPath(_nativeDir),
                    StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            Directory.CreateDirectory(installNative);
            foreach (var file in Directory.EnumerateFiles(_nativeDir))
            {
                var ext = Path.GetExtension(file);
                if (string.Equals(ext, ".lib", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(ext, ".exp", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                File.Copy(file, Path.Combine(installNative, Path.GetFileName(file)), overwrite: true);
            }
        }
        catch { /* best-effort mirror for AllyLeaveWatch / hooks in the game folder */ }
    }

    private void WriteColorConfigAndApply(ColorConfig config)
    {
        var json = JsonSerializer.Serialize(config, JsonOpts);
        File.WriteAllText(_configPath, json);
        MirrorPlayerColorsJson(json);
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

    private void ApplyBugFixes()
    {
        var result = RunEngine("-ApplyBugFixesFromExtra");
        if (result.ExitCode != 0)
        {
            var details = string.Join(Environment.NewLine,
                new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                ? "Bug fix install failed."
                : details);
        }
    }

    private void ApplyAudioMods()
    {
        var result = RunEngine("-ApplyAudioFromExtra");
        if (result.ExitCode != 0)
        {
            var details = string.Join(Environment.NewLine,
                new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                ? "Audio install failed."
                : details);
        }
    }

    private void ApplyAllyGoneSkullAtlas()
    {
        var result = RunEngine("-ApplyAllyGoneIconFromExtra");
        if (result.ExitCode != 0)
        {
            var details = string.Join(Environment.NewLine,
                new[] { result.Error, result.Output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(details)
                ? "Ally gone skull atlas install failed."
                : details);
        }
    }

    private void WriteExtraFeaturesFile(
        bool markComputers,
        bool markHumans,
        bool chatPause,
        bool chatColoredNames,
        bool chatTimestamps,
        bool chatHistory,
        bool mpLobbyChatScrollFix,
        bool endGameObserve,
        bool allianceTeamNumbers,
        bool computerAnnihilatedChat,
        bool blacksmithWorkCompleteChat,
        bool castleGoldTooltipFix,
        bool dragEnabled,
        string dragHex,
        bool networkMonitor = false,
        bool lobbyMapClickOpen = false)
    {
        // Feature 5 is greyed out — never persist Observe as enabled.
        endGameObserve = false;
        // Blacksmith upgrade chat is greyed out — never persist as enabled.
        blacksmithWorkCompleteChat = false;
        // MP lobby chat scroll is greyed out — never persist as enabled.
        mpLobbyChatScrollFix = false;
        // Audio unit voice overhaul removed from the app — always off.
        const bool voiceAudioEnhance = false;
        var extraJson = JsonSerializer.Serialize(new ExtraFeaturesConfig
        {
            AllyLeaveMarkComputers = markComputers,
            AllyLeaveMarkHumans = markHumans,
            // Legacy key: watcher / older builds treat this as "any ally-leave feature".
            AllyLeaveRedNames = markComputers || markHumans,
            ChatDuringPauseScreen = chatPause,
            ChatColoredNames = chatColoredNames,
            ChatTimestamps = chatTimestamps,
            ChatHistory = chatHistory,
            MpLobbyChatScrollFix = mpLobbyChatScrollFix,
            EndGameObserve = endGameObserve,
            AllianceTeamNumbers = allianceTeamNumbers,
            ComputerAnnihilatedChat = computerAnnihilatedChat,
            BlacksmithWorkCompleteChat = blacksmithWorkCompleteChat,
            VoiceAudioEnhance = voiceAudioEnhance,
            HumanFootmanAudio = voiceAudioEnhance,
            HumanKnightAudio = voiceAudioEnhance,
            CastleGoldTooltipFix = castleGoldTooltipFix,
            NetworkMonitor = networkMonitor,
            LobbyMapClickOpen = lobbyMapClickOpen,
            DragSelectColorEnabled = dragEnabled,
            DragSelectColor = dragHex,
            UnitSpriteColors = _unitSpriteColors,
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
        var dialog = new ColorPickerDialog(card.Hex, card.Name) { Owner = this };
        if (dialog.ShowDialog() == true)
        {
            card.Hex = dialog.SelectedHex;
            RefreshTabStatus();
        }
    }

    private void Hex_LostFocus(object sender, RoutedEventArgs e)
    {
        if (_isApplying) return;
        if ((sender as FrameworkElement)?.DataContext is not ColorCard card) return;
        if (!ColorCard.IsValidHex(card.Hex))
        {
            StudioDialog.Show(this,
                $"{card.Name}: use a six-digit hex color, for example #3B82F6.",
                "Invalid color", StudioDialogKind.Warning);
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
        var chatStamps = false;
        var chatHist = false;
        var mpLobbyChatScrollFix = false;
        var castleGoldTooltipFix = false;
        var endGameObserve = false;
        var allianceTeamNumbers = false;
        var computerAnnihilatedChat = false;
        var blacksmithWorkCompleteChat = false;
        var networkMonitor = false;
        var lobbyMapClickOpen = false;
        var unitColors = false;
        const bool dragEnabled = false;
        var hadModAudio = false;

        if (File.Exists(_extraConfigPath))
        {
            var extra = JsonSerializer.Deserialize<ExtraFeaturesConfig>(File.ReadAllText(_extraConfigPath),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            markComputers = extra?.AllyLeaveMarkComputers ?? false;
            markHumans = extra?.AllyLeaveMarkHumans ?? false;
            // Migrate legacy single toggle → computers (NPC focus).
            if (!markComputers && (extra?.AllyLeaveRedNames ?? false))
                markComputers = true;
            chatPause = extra?.ChatDuringPauseScreen ?? false;
            chatColored = extra?.ChatColoredNames ?? false;
            chatStamps = extra?.ChatTimestamps ?? false;
            chatHist = extra?.ChatHistory ?? false;
            mpLobbyChatScrollFix = false; // disabled — see scripts/research/mp-lobby-chat-scroll-findings.txt
            castleGoldTooltipFix = extra?.CastleGoldTooltipFix ?? false;
            // Feature 5 disabled — ignore persisted Observe flag.
            endGameObserve = false;
            allianceTeamNumbers = extra?.AllianceTeamNumbers ?? false;
            computerAnnihilatedChat = extra?.ComputerAnnihilatedChat ?? false;
            // Blacksmith disabled — ignore persisted flag.
            blacksmithWorkCompleteChat = false;
            // Voice overhaul removed — restore vanilla audio once if it was on.
            hadModAudio = (extra?.VoiceAudioEnhance ?? false) ||
                (extra?.HumanFootmanAudio ?? false) || (extra?.HumanKnightAudio ?? false);
            networkMonitor = extra?.NetworkMonitor ?? false;
            lobbyMapClickOpen = extra?.LobbyMapClickOpen ?? false;
            unitColors = extra?.UnitSpriteColors ?? false;
        }

        _allyLeaveMarkComputers = markComputers;
        _appliedAllyLeaveMarkComputers = markComputers;
        _allyLeaveMarkHumans = markHumans;
        _appliedAllyLeaveMarkHumans = markHumans;
        _chatDuringPauseScreen = chatPause;
        _appliedChatDuringPauseScreen = chatPause;
        _chatColoredNames = chatColored;
        _appliedChatColoredNames = chatColored;
        _chatTimestamps = chatStamps;
        _appliedChatTimestamps = chatStamps;
        _chatHistory = chatHist;
        _appliedChatHistory = chatHist;
        _mpLobbyChatScrollFix = mpLobbyChatScrollFix;
        _appliedMpLobbyChatScrollFix = mpLobbyChatScrollFix;
        _castleGoldTooltipFix = castleGoldTooltipFix;
        _appliedCastleGoldTooltipFix = castleGoldTooltipFix;
        _endGameObserve = endGameObserve;
        _appliedEndGameObserve = endGameObserve;
        _allianceTeamNumbers = allianceTeamNumbers;
        _appliedAllianceTeamNumbers = allianceTeamNumbers;
        _computerAnnihilatedChat = computerAnnihilatedChat;
        _appliedComputerAnnihilatedChat = computerAnnihilatedChat;
        _blacksmithWorkCompleteChat = blacksmithWorkCompleteChat;
        _appliedBlacksmithWorkCompleteChat = blacksmithWorkCompleteChat;
        _networkMonitor = networkMonitor;
        _appliedNetworkMonitor = networkMonitor;
        _lobbyMapClickOpen = lobbyMapClickOpen;
        _appliedLobbyMapClickOpen = lobbyMapClickOpen;
        _unitSpriteColors = unitColors;
        _appliedUnitSpriteColors = unitColors;
        _appliedDragSelectColorEnabled = dragEnabled;
        OnPropertyChanged(nameof(AllyLeaveMarkComputers));
        OnPropertyChanged(nameof(AllyLeaveMarkHumans));
        OnPropertyChanged(nameof(ChatDuringPauseScreen));
        OnPropertyChanged(nameof(ChatColoredNames));
        OnPropertyChanged(nameof(ChatTimestamps));
        OnPropertyChanged(nameof(ChatHistory));
        OnPropertyChanged(nameof(MpLobbyChatScrollFix));
        OnPropertyChanged(nameof(CastleGoldTooltipFix));
        OnPropertyChanged(nameof(EndGameObserve));
        OnPropertyChanged(nameof(AllianceTeamNumbers));
        OnPropertyChanged(nameof(ComputerAnnihilatedChat));
        OnPropertyChanged(nameof(UpgradeNotifications));
        OnPropertyChanged(nameof(BlacksmithWorkCompleteChat));
        OnPropertyChanged(nameof(LobbyMapClickOpen));
        OnPropertyChanged(nameof(NetworkMonitor));

        try
        {
            WriteExtraFeaturesFile(markComputers, markHumans, chatPause, chatColored, chatStamps,
                chatHist, mpLobbyChatScrollFix, endGameObserve, allianceTeamNumbers, computerAnnihilatedChat,
                blacksmithWorkCompleteChat, castleGoldTooltipFix,
                dragEnabled: false, dragHex: "#00FF00", networkMonitor: networkMonitor,
                lobbyMapClickOpen: lobbyMapClickOpen);
        }
        catch { /* optional on first run */ }

        if (hadModAudio && IsValidGameRoot(NormalizeGameRoot(_appliedGameInstallPath)))
        {
            try { ApplyAudioMods(); }
            catch { /* game path or backup may be unavailable */ }
        }
    }

    private static bool IsWarcraftIiRunning() =>
        Process.GetProcessesByName("Warcraft II").Length > 0;

    private void UpdateExtraHookStatus(bool forceInject)
    {
        UpdateAllyLeaveHookStatus(forceInject);
        UpdatePauseChatHookStatus(forceInject);
        UpdateChatNameColorHookStatus(forceInject);
        UpdateDragSelectHookStatus(forceInject);
        UpdateObserveHookStatus(forceInject);
        UpdateNetworkMonitorHookStatus(forceInject);
        UpdateLobbyMapClickHookStatus(forceInject);
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

        // Prefer the game-install copy so HKCU Run survives moves of the project folder
        // and always reads the mirrored extra-features.json next to it.
        SyncNativeToGameInstall();
        var installNative = Path.Combine(
            NormalizeGameRoot(_appliedGameInstallPath),
            "x86", "Mods", "PlayerColorStudio", "mod", "native");
        var watchDir = (!string.IsNullOrWhiteSpace(installNative) &&
                        File.Exists(Path.Combine(installNative, "AllyLeaveWatch.exe")))
            ? installNative
            : _nativeDir;
        var watch = Path.Combine(watchDir, "AllyLeaveWatch.exe");
        if (!File.Exists(watch)) return;

        try
        {
            var anyExtra = AnyAllyLeaveApplied || _appliedChatDuringPauseScreen ||
                _appliedChatColoredNames || _appliedChatTimestamps || _appliedChatHistory ||
                _appliedMpLobbyChatScrollFix ||
                _appliedAllianceTeamNumbers || _appliedComputerAnnihilatedChat ||
                _appliedBlacksmithWorkCompleteChat ||
                _appliedDragSelectColorEnabled || _appliedUnitSpriteColors ||
                _appliedNetworkMonitor || _appliedLobbyMapClickOpen;
            var args = anyExtra ? "--install-startup" : "--uninstall-startup";
            var start = new ProcessStartInfo(watch, args)
            {
                WorkingDirectory = watchDir,
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
                    WorkingDirectory = watchDir,
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

    private void UpdateObserveHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedEndGameObserve)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncObserveHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _observeInjectedForRunningGame = false;
            return;
        }

        if (_observeInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncObserveHook(throwOnError: false);
            _observeInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _observeInjectedForRunningGame = false;
        }
    }

    private string SyncObserveHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectObserve.exe");
        var dll = Path.Combine(_nativeDir, "ObserveHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Observe hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            _observeInjectedForRunningGame = false;
            return _appliedEndGameObserve
                ? "End-game Observe ON — watcher auto-injects when Warcraft II starts."
                : "End-game Observe setting saved.";
        }

        var args = _appliedEndGameObserve ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start the Observe hook injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            _observeInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Observe hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _observeInjectedForRunningGame = _appliedEndGameObserve;
        return string.IsNullOrWhiteSpace(output) ? "Observe hook updated." : output;
    }

    private void UpdateNetworkMonitorHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedNetworkMonitor)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncNetworkMonitorHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _networkMonitorInjectedForRunningGame = false;
            return;
        }

        if (_networkMonitorInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncNetworkMonitorHook(throwOnError: false);
            _networkMonitorInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _networkMonitorInjectedForRunningGame = false;
        }
    }

    private string SyncNetworkMonitorHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectNetworkMonitor.exe");
        var dll = Path.Combine(_nativeDir, "NetworkMonitorHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Network monitor hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            _networkMonitorInjectedForRunningGame = false;
            return _appliedNetworkMonitor
                ? "Network monitor ON — watcher auto-injects when Warcraft II starts."
                : "Network monitor setting saved.";
        }

        var args = _appliedNetworkMonitor ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start the network monitor injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            _networkMonitorInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Network monitor hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _networkMonitorInjectedForRunningGame = _appliedNetworkMonitor;
        return string.IsNullOrWhiteSpace(output) ? "Network monitor hook updated." : output;
    }

    private void UpdateLobbyMapClickHookStatus(bool forceInject)
    {
        if (string.IsNullOrWhiteSpace(_nativeDir) || !Directory.Exists(_nativeDir))
            return;

        if (!_appliedLobbyMapClickOpen)
        {
            if (IsWarcraftIiRunning() && forceInject)
            {
                try { SyncLobbyMapClickHook(throwOnError: false); }
                catch { /* keep tab status friendly */ }
            }
            _lobbyMapClickInjectedForRunningGame = false;
            return;
        }

        if (!IsWarcraftIiRunning())
        {
            _lobbyMapClickInjectedForRunningGame = false;
            return;
        }

        if (_lobbyMapClickInjectedForRunningGame && !forceInject) return;

        try
        {
            var message = SyncLobbyMapClickHook(throwOnError: false);
            _lobbyMapClickInjectedForRunningGame =
                !string.IsNullOrWhiteSpace(message) &&
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            _lobbyMapClickInjectedForRunningGame = false;
        }
    }


    private string MapImagesDir()
    {
        var dir = NormalizePathText(MapImagesPath);
        if (string.IsNullOrWhiteSpace(dir))
            dir = DefaultMapImagesPathFor(MapsPath);
        return dir;
    }

    private void EnsureMapsRootLayout()
    {
        var root = NormalizePathText(MapsPath);
        if (!string.IsNullOrWhiteSpace(root))
        {
            foreach (var name in new[] { "classics", "downloads" })
            {
                try { Directory.CreateDirectory(Path.Combine(root, name)); } catch { /* ignore */ }
            }
            try
            {
                File.WriteAllText(Path.Combine(Path.GetTempPath(), "war2_maps_root.txt"), root, System.Text.Encoding.Unicode);
            }
            catch { /* ignore */ }
        }

        var images = MapImagesDir();
        if (!string.IsNullOrWhiteSpace(images))
        {
            try { Directory.CreateDirectory(images); } catch { /* ignore */ }
            try
            {
                // AllyLeaveWatch / lobby feature read this exact preview folder.
                File.WriteAllText(Path.Combine(Path.GetTempPath(), "war2_map_images_path.txt"), images, System.Text.Encoding.Unicode);
            }
            catch { /* ignore */ }
        }
    }

    private void RefreshMapsSyncStatus()
    {
        try
        {
            EnsureMapsRootLayout();
            var dir = MapImagesDir();
            var count = Directory.Exists(dir)
                ? Directory.GetFiles(dir, "*.webp").Length
                : 0;
            var metaPath = Path.Combine(dir, "_download_meta.json");
            _hasInitialMapImagesDownload = File.Exists(metaPath) || count > 0;
            string last = "never";
            if (File.Exists(metaPath))
            {
                try
                {
                    using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(metaPath));
                    if (doc.RootElement.TryGetProperty("lastDownloadUtc", out var p))
                    {
                        var raw = p.GetString();
                        if (!string.IsNullOrWhiteSpace(raw) && DateTime.TryParse(raw, null, System.Globalization.DateTimeStyles.RoundtripKind, out var utc))
                            last = utc.ToLocalTime().ToString("g");
                        else if (!string.IsNullOrWhiteSpace(raw))
                            last = raw!;
                    }
                }
                catch { /* ignore */ }
            }
            MapsSyncStatusText = _hasInitialMapImagesDownload
                ? $"Last image download: {last} · {count} files"
                : "Last image download: none yet";
            OnPropertyChanged(nameof(MapsImagesActionLabel));
        }
        catch (Exception ex)
        {
            MapsSyncStatusText = "Could not read map images folder: " + ex.Message;
            OnPropertyChanged(nameof(MapsImagesActionLabel));
        }
        RefreshMapsFilterSummary();
        OnPropertyChanged(nameof(IsMapsDownloadEnabled));
        OnPropertyChanged(nameof(MapsImagesFeedbackText));
        PushMapsFeedbackToStatus();
    }

    private void RefreshMapsFilterSummary()
    {
        MapsFilterSummary = $"Showing filters: category={MapsCategoryFilter}, size={MapsSizeFilter}, search=\"{MapsSearchText}\" (metadata list comes from warcraft2.site on Download/Update).";
    }

    private async void MapsImagesAction_Click(object sender, RoutedEventArgs e) =>
        await RunMapsImageSyncAsync(incrementalOnly: _hasInitialMapImagesDownload);

    private async void MapsFolderDownload_Click(object sender, RoutedEventArgs e) =>
        await RunMapsPudDownloadAsync();

    private async Task RunMapsPudDownloadAsync()
    {
        if (_mapsDownloadBusy) return;
        if (!IsValidMapsPath(MapsPath))
        {
            System.Windows.MessageBox.Show(this, "Set a valid Maps folder path first.", "Maps",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _mapsDownloadBusy = true;
        OnPropertyChanged(nameof(IsMapsDownloadEnabled));
        OnPropertyChanged(nameof(IsMapsFolderDownloadEnabled));
        _mapsDownloadCts?.Cancel();
        _mapsDownloadCts = new CancellationTokenSource();
        var token = _mapsDownloadCts.Token;

        MapsDownloadProgressText = "Map download: listing warcraft2.site…";
        try
        {
            EnsureMapsRootLayout();
            var mapsRoot = NormalizePathText(MapsPath);
            var outDir = Path.Combine(mapsRoot, "downloads");
            Directory.CreateDirectory(outDir);

            // Public list API; authenticated .pud fetch uses embedded WebView2 (own login session).
            var jobs = await ListMapPudDownloadJobsAsync(outDir, token);
            if (jobs.Count == 0)
            {
                MapsDownloadProgressText = "Map download: all listed maps already present in downloads.";
                RefreshMapsPudSyncStatus();
                return;
            }

            MapsDownloadProgressText = $"Map download: {jobs.Count} maps need download — open login window…";
            var win = new Warcraft2MapDownloadWindow(this, outDir, jobs, token);
            win.ShowDialog();

            if (win.Completed || win.Ok > 0 || win.Skipped > 0)
            {
                var pudCount = Directory.Exists(outDir) ? Directory.GetFiles(outDir, "*.pud").Length : 0;
                var metaPath = Path.Combine(outDir, "_pud_download_meta.json");
                var meta = $"{{\"lastDownloadUtc\":\"{DateTime.UtcNow:o}\",\"fileCount\":{pudCount},\"ok\":{win.Ok},\"skipped\":{win.Skipped},\"failed\":{win.Fail}}}";
                File.WriteAllText(metaPath, meta);
                RefreshMapsPudSyncStatus();
                MapsDownloadProgressText =
                    $"Map download finished → downloads (new={win.Ok}, skipped={win.Skipped}, failed={win.Fail}).";
            }
            else
            {
                MapsDownloadProgressText = "Map download window closed before finishing.";
            }
        }
        catch (OperationCanceledException)
        {
            MapsDownloadProgressText = "Map download cancelled.";
        }
        catch (Exception ex)
        {
            MapsDownloadProgressText = "Failed map download: " + ex.Message;
            StudioDialog.Show(this, ex.Message, "Download maps", StudioDialogKind.Error);
        }
        finally
        {
            _mapsDownloadBusy = false;
            OnPropertyChanged(nameof(IsMapsDownloadEnabled));
            OnPropertyChanged(nameof(IsMapsFolderDownloadEnabled));
            PushMapsFeedbackToStatus();
        }
    }

    private async Task<List<(string Filename, string DownloadUrl)>> ListMapPudDownloadJobsAsync(
        string outDir, CancellationToken token)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(120) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("PlayerColorStudio-Maps/1.0");
        http.DefaultRequestHeaders.Referrer = new Uri("https://warcraft2.site/");

        var entries = new List<(string Filename, string Category, string DownloadUrl)>();
        long? cursor = null;
        for (var page = 0; page < 500; page++)
        {
            token.ThrowIfCancellationRequested();
            var url = "https://warcraft2.site/api/maps?pageSize=100" + (cursor is null ? "" : $"&cursor={cursor}");
            using var resp = await http.GetAsync(url, token);
            resp.EnsureSuccessStatusCode();
            using var doc = System.Text.Json.JsonDocument.Parse(await resp.Content.ReadAsStringAsync(token));
            var rootEl = doc.RootElement;
            if (rootEl.TryGetProperty("items", out var items))
            {
                foreach (var it in items.EnumerateArray())
                {
                    var filename = it.TryGetProperty("filename", out var fn) ? fn.GetString() ?? "" : "";
                    if (string.IsNullOrWhiteSpace(filename)) continue;
                    var category = it.TryGetProperty("category", out var c) ? c.GetString() ?? "" : "";
                    var downloadUrl = "";
                    if (it.TryGetProperty("links", out var links) && links.ValueKind == System.Text.Json.JsonValueKind.Array)
                    {
                        foreach (var link in links.EnumerateArray())
                        {
                            var rel = link.TryGetProperty("rel", out var r) ? r.GetString() : null;
                            if (!string.Equals(rel, "download", StringComparison.OrdinalIgnoreCase)) continue;
                            var href = link.TryGetProperty("href", out var h) ? h.GetString() : null;
                            if (!string.IsNullOrWhiteSpace(href))
                            {
                                downloadUrl = href!.StartsWith("http", StringComparison.OrdinalIgnoreCase)
                                    ? href!
                                    : "https://warcraft2.site" + href;
                                break;
                            }
                        }
                    }
                    if (string.IsNullOrWhiteSpace(downloadUrl) && !string.IsNullOrWhiteSpace(category))
                    {
                        downloadUrl =
                            $"https://warcraft2.site/api/maps/{Uri.EscapeDataString(category)}/{Uri.EscapeDataString(filename)}/download";
                    }
                    if (!string.IsNullOrWhiteSpace(downloadUrl))
                        entries.Add((filename, category, downloadUrl));
                }
            }
            MapsDownloadProgressText = $"Map download: listing… {entries.Count}";
            var hasMore = rootEl.TryGetProperty("hasMore", out var hm) && hm.GetBoolean();
            if (!hasMore) break;
            if (rootEl.TryGetProperty("nextCursor", out var nc) && nc.ValueKind == System.Text.Json.JsonValueKind.Number)
                cursor = nc.GetInt64();
            else break;
        }

        return entries
            .GroupBy(e => e.Filename, StringComparer.OrdinalIgnoreCase)
            .Select(g => g.First())
            .Where(e =>
            {
                var dest = Path.Combine(outDir, e.Filename);
                return !(File.Exists(dest) && new FileInfo(dest).Length >= 256);
            })
            .Select(e => (e.Filename, e.DownloadUrl))
            .ToList();
    }


    private sealed class PudDownloadResult
    {
        public int Ok { get; init; }
        public int Fail { get; init; }
        public int Skipped { get; init; }
        public bool AuthRequired { get; init; }
    }


    private string? TryLoadWarcraft2SiteCookieHeader()
    {
        try
        {
            var script = Path.Combine(_nativeDir, "get_w2_site_cookies.py");
            if (!File.Exists(script))
            {
                var projectNative = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "mod", "native", "get_w2_site_cookies.py"));
                if (File.Exists(projectNative)) script = projectNative;
            }
            if (!File.Exists(script)) return null;

            foreach (var py in new[] { "py", "python" })
            {
                try
                {
                    var args = py == "py" ? $"-3 \"{script}\"" : $"\"{script}\"";
                    var psi = new ProcessStartInfo
                    {
                        FileName = py,
                        Arguments = args,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        CreateNoWindow = true,
                    };
                    using var proc = Process.Start(psi);
                    if (proc is null) continue;
                    var output = proc.StandardOutput.ReadToEnd().Trim();
                    if (!proc.WaitForExit(20000))
                    {
                        try { proc.Kill(entireProcessTree: true); } catch { }
                        continue;
                    }
                    if (proc.ExitCode == 0 && !string.IsNullOrWhiteSpace(output))
                        return output;
                }
                catch { /* try next launcher */ }
            }
        }
        catch { /* optional auth helper */ }
        return null;
    }

    


    private async void MapsDownload_Click(object sender, RoutedEventArgs e) =>
        await RunMapsImageSyncAsync(incrementalOnly: false);

    private async void MapsUpdate_Click(object sender, RoutedEventArgs e) =>
        await RunMapsImageSyncAsync(incrementalOnly: true);

    private void MapsOpenImagesFolder_Click(object sender, RoutedEventArgs e)
    {
        EnsureMapsRootLayout();
        var dir = MapImagesDir();
        if (!Directory.Exists(dir))
        {
            System.Windows.MessageBox.Show(this, "map_images folder missing.", "Maps",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        Process.Start(new ProcessStartInfo { FileName = dir, UseShellExecute = true });
    }

    private async Task RunMapsImageSyncAsync(bool incrementalOnly)
    {
        if (_mapsDownloadBusy) return;
        if (!IsValidMapImagesPath(MapImagesPath))
        {
            System.Windows.MessageBox.Show(this, "Set a valid Map images path first (Paths tab).", "Maps",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        _mapsDownloadBusy = true;
        OnPropertyChanged(nameof(IsMapsDownloadEnabled));
        _mapsDownloadCts?.Cancel();
        _mapsDownloadCts = new CancellationTokenSource();
        var token = _mapsDownloadCts.Token;
        MapsDownloadProgressText = incrementalOnly ? "Checking for missing images…" : "Downloading all map images…";
        try
        {
            EnsureMapsRootLayout();
            var outDir = MapImagesDir();
            // Inline HTTP only — spawning py from the GUI often fails (exit 101).

            await DownloadMapImagesInlineAsync(outDir, incrementalOnly, token);

            var metaPath = Path.Combine(outDir, "_download_meta.json");
            var count = Directory.GetFiles(outDir, "*.webp").Length;
            var meta = $"{{\"lastDownloadUtc\":\"{DateTime.UtcNow:o}\",\"fileCount\":{count},\"incremental\":{(incrementalOnly ? "true" : "false")}}}";
            File.WriteAllText(metaPath, meta);
            RefreshMapsSyncStatus();
            MapsDownloadProgressText = incrementalOnly ? "Update finished." : "Download finished.";
        }
        catch (OperationCanceledException)
        {
            MapsDownloadProgressText = "Cancelled.";
        }
        catch (Exception ex)
        {
            MapsDownloadProgressText = "Failed: " + ex.Message;
            System.Windows.MessageBox.Show(this, ex.Message, "Maps download", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _mapsDownloadBusy = false;
            OnPropertyChanged(nameof(IsMapsDownloadEnabled));
            OnPropertyChanged(nameof(IsMapsFolderDownloadEnabled));
            PushMapsFeedbackToStatus();
        }
    }

    private async Task DownloadMapImagesInlineAsync(string outDir, bool incrementalOnly, CancellationToken token)
    {
        Directory.CreateDirectory(outDir);
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("PlayerColorStudio-Maps/1.0");
        var names = new List<string>();
        long? cursor = null;
        for (var page = 0; page < 500; page++)
        {
            token.ThrowIfCancellationRequested();
            var url = "https://warcraft2.site/api/maps?pageSize=100" + (cursor is null ? "" : $"&cursor={cursor}");
            using var resp = await http.GetAsync(url, token);
            resp.EnsureSuccessStatusCode();
            using var doc = System.Text.Json.JsonDocument.Parse(await resp.Content.ReadAsStringAsync(token));
            var rootEl = doc.RootElement;
            if (rootEl.TryGetProperty("items", out var items))
            {
                foreach (var it in items.EnumerateArray())
                {
                    if (it.TryGetProperty("filename", out var fn))
                        names.Add(fn.GetString() ?? "");
                }
            }
            var hasMore = rootEl.TryGetProperty("hasMore", out var hm) && hm.GetBoolean();
            if (!hasMore) break;
            if (rootEl.TryGetProperty("nextCursor", out var nc) && nc.ValueKind == System.Text.Json.JsonValueKind.Number)
                cursor = nc.GetInt64();
            else break;
            MapsDownloadProgressText = $"Listing maps… {names.Count}";
        }
        names = names.Where(n => !string.IsNullOrWhiteSpace(n)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var todo = new List<string>();
        foreach (var fn in names)
        {
            var stem = Path.GetFileNameWithoutExtension(fn);
            var dest = Path.Combine(outDir, stem + ".webp");
            if (incrementalOnly && File.Exists(dest) && new FileInfo(dest).Length >= 2048) continue;
            if (!incrementalOnly && File.Exists(dest) && new FileInfo(dest).Length >= 2048) continue;
            todo.Add(fn);
        }
        var ok = 0;
        var fail = 0;
        for (var i = 0; i < todo.Count; i++)
        {
            token.ThrowIfCancellationRequested();
            var fn = todo[i];
            var stem = Path.GetFileNameWithoutExtension(fn);
            var dest = Path.Combine(outDir, stem + ".webp");
            var tmp = dest + ".part";
            try
            {
                var enc = Uri.EscapeDataString(fn);
                var bytes = await http.GetByteArrayAsync($"https://warcraft2.site/api/maps/{enc}/thumbnail", token);
                if (bytes.Length < 2048) { fail++; continue; }
                await File.WriteAllBytesAsync(tmp, bytes, token);
                File.Move(tmp, dest, true);
                ok++;
            }
            catch
            {
                fail++;
                try { if (File.Exists(tmp)) File.Delete(tmp); } catch { }
            }
            if ((i + 1) % 10 == 0 || i + 1 == todo.Count)
                MapsDownloadProgressText = $"Downloading {i + 1}/{todo.Count} (ok={ok} fail={fail})";
                    PushMapsFeedbackToStatus();
        }
    }

    private string SyncLobbyMapClickHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectLobbyMapClick.exe");
        var dll = Path.Combine(_nativeDir, "LobbyMapClickHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Lobby map click hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            return _appliedLobbyMapClickOpen
                ? "Lobby map click will inject when Warcraft II is running."
                : "Lobby map click hook off (game not running).";
        }

        var args = _appliedLobbyMapClickOpen ? "--enable" : "--disable";
        var start = new ProcessStartInfo
        {
            FileName = injector,
            Arguments = args,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start the lobby map click injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            _lobbyMapClickInjectedForRunningGame = false;
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Lobby map click hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        _lobbyMapClickInjectedForRunningGame = _appliedLobbyMapClickOpen;
        return string.IsNullOrWhiteSpace(output) ? "Lobby map click hook updated." : output;
    }

    private static readonly string NetworkMonitorLogPath =
        Path.Combine(Path.GetTempPath(), "war2_network_monitor.log");

    private void RefreshNetworkMonitorLog()
    {
        try
        {
            if (!_appliedNetworkMonitor)
            {
                NetworkMonitorStatus = "Monitor off — enable and Apply, then play a multiplayer match.";
                NetworkMonitorSummary = string.Empty;
                NetworkSeats.Clear();
                if (!File.Exists(NetworkMonitorLogPath))
                {
                    NetworkMonitorLog = "No monitor log yet.";
                    return;
                }
            }

            if (!File.Exists(NetworkMonitorLogPath))
            {
                NetworkMonitorLog = "No monitor log yet — start a multiplayer match.";
                NetworkMonitorSummary = string.Empty;
                NetworkSeats.Clear();
                if (_appliedNetworkMonitor)
                    NetworkMonitorStatus = "Waiting for match data…";
                return;
            }

            var lines = File.ReadAllLines(NetworkMonitorLogPath);
            NetworkMonitorLog = string.Join(Environment.NewLine, lines.TakeLast(40));

            var summaryLine = lines.LastOrDefault(l => l.StartsWith("SUMMARY ", StringComparison.Ordinal));
            NetworkMonitorSummary = summaryLine ?? string.Empty;

            var seatRows = new List<NetworkSeatRow>();
            foreach (var line in lines)
            {
                if (!line.StartsWith("SEAT ", StringComparison.Ordinal)) continue;
                var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length < 5) continue;
                if (!int.TryParse(parts[1], out var seat)) continue;
                var name = string.Empty;
                var lastMs = string.Empty;
                var maxMs = string.Empty;
                foreach (var token in parts.Skip(2))
                {
                    if (token.StartsWith("name=", StringComparison.Ordinal))
                        name = token["name=".Length..];
                    else if (token.StartsWith("lastMs=", StringComparison.Ordinal))
                        lastMs = token["lastMs=".Length..];
                    else if (token.StartsWith("maxMs=", StringComparison.Ordinal))
                        maxMs = token["maxMs=".Length..];
                }
                seatRows.Add(new NetworkSeatRow(seat, name, lastMs, maxMs));
            }

            NetworkSeats.Clear();
            foreach (var row in seatRows.OrderBy(r => r.Seat))
                NetworkSeats.Add(row);

            if (_appliedNetworkMonitor)
            {
                NetworkMonitorStatus = seatRows.Count > 0
                    ? "Match active — updating every second."
                    : _networkMonitorInjectedForRunningGame
                        ? "Monitor injected — waiting for multiplayer match."
                        : "Monitor enabled — watcher will inject when Warcraft II is running.";
            }
        }
        catch
        {
            NetworkMonitorLog = "Could not read network monitor log.";
        }
    }

    private void UpdateChatNameColorHookStatus(bool forceInject)
    {
        if (string.IsNullOrEmpty(_nativeDir)) return;

        if (!_appliedChatColoredNames && !_appliedChatTimestamps && !_appliedChatHistory &&
            !_appliedMpLobbyChatScrollFix &&
            !_appliedAllyLeaveMarkHumans && !_appliedComputerAnnihilatedChat &&
            !_appliedBlacksmithWorkCompleteChat)
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
                message.Contains("enabled", StringComparison.OrdinalIgnoreCase) &&
                !message.Contains("disabled", StringComparison.OrdinalIgnoreCase);
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
            return _appliedChatColoredNames || _appliedChatTimestamps || _appliedChatHistory ||
                _appliedMpLobbyChatScrollFix ||
                _appliedAllyLeaveMarkHumans || _appliedComputerAnnihilatedChat ||
                _appliedBlacksmithWorkCompleteChat
                ? "Chat mods ON — watcher auto-injects when Warcraft II starts."
                : "Chat name colors setting saved.";
        }

        var args = (_appliedChatColoredNames ? "--enable" : "--disable") +
                   (_appliedChatTimestamps ? " --timestamps 1" : " --timestamps 0") +
                   (_appliedChatHistory ? " --history 1" : " --history 0") +
                   (_appliedMpLobbyChatScrollFix ? " --lobby-scroll 1" : " --lobby-scroll 0") +
                   (_appliedBlacksmithWorkCompleteChat ? " --blacksmith 1" : " --blacksmith 0");
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

        _chatNameColorInjectedForRunningGame =
            _appliedChatColoredNames || _appliedChatTimestamps || _appliedChatHistory ||
            _appliedMpLobbyChatScrollFix ||
            _appliedAllyLeaveMarkHumans || _appliedComputerAnnihilatedChat ||
            _appliedBlacksmithWorkCompleteChat;
        return string.IsNullOrWhiteSpace(output) ? "Chat-name-color hook updated." : output;
    }

    private string SyncUnitColorHook(bool throwOnError = true)
    {
        var injector = Path.Combine(_nativeDir, "InjectUnitColor.exe");
        var dll = Path.Combine(_nativeDir, "UnitColorHook.dll");
        if (!File.Exists(injector) || !File.Exists(dll))
        {
            var missing = "Unit-color hook files are missing. Rebuild mod/native.";
            if (throwOnError) throw new InvalidOperationException(missing);
            return missing;
        }

        if (!IsWarcraftIiRunning())
        {
            return _appliedUnitSpriteColors
                ? "Unit sprite colors ON — watcher auto-injects when Warcraft II starts."
                : "Unit sprite colors setting saved.";
        }

        var args = _appliedUnitSpriteColors ? "--enable" : "--disable";
        var start = new ProcessStartInfo(injector, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = _nativeDir
        };
        using var process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start the unit-color injector.");
        var output = process.StandardOutput.ReadToEnd().Trim();
        var error = process.StandardError.ReadToEnd().Trim();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            var details = string.Join(Environment.NewLine, new[] { error, output }.Where(s => !string.IsNullOrWhiteSpace(s)));
            var message = string.IsNullOrWhiteSpace(details)
                ? $"Unit-color hook sync failed (exit {process.ExitCode})."
                : details;
            if (throwOnError) throw new InvalidOperationException(message);
            return message;
        }

        return string.IsNullOrWhiteSpace(output) ? "Unit-color hook updated." : output;
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
            // Colorblind: snapshot from the preset table before focus moves so a
            // stale TextBox cannot win. Custom: commit focus first, then read cards
            // so typed/picked hexes are what get written.
            ColorConfig? preparedColorConfig = null;
            var prepareColors = _activeTab == "colors" || HasPendingColorChanges();
            var customColorsApply = prepareColors &&
                (IsCustomColorPreset(_selectedColorBlindPreset) ||
                 string.IsNullOrEmpty(_selectedColorBlindPreset));

            if (prepareColors && !customColorsApply)
                preparedColorConfig = BuildColorConfigForColorsApply();

            // Custom Apply reads cards after focus moves; sync first so unchanged slots
            // keep preset colors instead of stale TextBox text left over from before.
            if (prepareColors && customColorsApply)
                SyncHexTextBoxesFromModel();

            IsApplying = true;

            // Commit any hex TextBox still focused so the latest typed value is saved.
            if (Keyboard.FocusedElement is UIElement focused)
                focused.MoveFocus(new TraversalRequest(FocusNavigationDirection.Next));

            if (prepareColors && customColorsApply)
                preparedColorConfig = BuildColorConfigForColorsApply();

            SetStatusLines(StatusLine("", ReadyIconBrush,
                _activeTab switch
                {
                    "bugfixes" => Localization.Get("Status.Applying.Bugfixes"),
                    "network" => Localization.Get("Status.Applying.Network"),
                    _ => Localization.Get("Status.Applying.Default"),
                }));
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

                var config = preparedColorConfig ?? BuildColorConfigForColorsApply();
                var markComputers = _appliedAllyLeaveMarkComputers;
                var markHumans = _appliedAllyLeaveMarkHumans;
                var chat = _appliedChatDuringPauseScreen;
                var chatNames = _appliedChatColoredNames;
                var chatStamps = _appliedChatTimestamps;
                var chatHist = _appliedChatHistory;
                var mpLobbyChatScrollFix = _appliedMpLobbyChatScrollFix;
                var endGameObserve = _appliedEndGameObserve;
                var allianceTeamNumbers = _appliedAllianceTeamNumbers;
                var computerAnnihilatedChat = _appliedComputerAnnihilatedChat;
                var blacksmithWorkCompleteChat = _appliedBlacksmithWorkCompleteChat;
                // Unit sprites always follow the installed player colors.
                _unitSpriteColors = true;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.WritingColors")));
                await Task.Run(() =>
                {
                    // Keep drag-select hook off — Self highlight owns shared palette index 250.
                    WriteExtraFeaturesFile(
                        markComputers, markHumans, chat, chatNames, chatStamps, chatHist,
                        mpLobbyChatScrollFix,
                        endGameObserve, allianceTeamNumbers, computerAnnihilatedChat,
                        blacksmithWorkCompleteChat,
                        _appliedCastleGoldTooltipFix,
                        dragEnabled: false,
                        dragHex: "#00FF00",
                        networkMonitor: _appliedNetworkMonitor,
                        lobbyMapClickOpen: _appliedLobbyMapClickOpen);
                    WriteColorConfigAndApply(config);
                    SyncNativeToGameInstall();
                    if (markComputers || markHumans)
                        ApplyAllyGoneSkullAtlas();
                });
                SyncCardsFromConfig(config);
                CaptureAppliedColors();
                CaptureAppliedUtilColors();
                _appliedColorBlindPreset = IsCustomColorPreset(_selectedColorBlindPreset)
                    ? "custom"
                    : _selectedColorBlindPreset;
                _appliedUnitSpriteColors = true;
                _appliedDragSelectColorEnabled = false;
                _dragSelectInjectedForRunningGame = false;
                if (_appliedChatColoredNames)
                {
                    SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.NameColors")));
                    await Task.Run(() =>
                    {
                        try { SyncChatNameColorHook(throwOnError: false); }
                        catch { /* optional while game closed */ }
                    });
                }
                // Unit sprite recolor rides along with every colors install: keep
                // the watcher and hook in sync for now and the next game launch.
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.UnitColors")));
                await Task.Run(() =>
                {
                    SyncAllyLeaveWatch();
                    try { SyncUnitColorHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                });
            }
            else if (_activeTab == "feature")
            {
                var markComputers = AllyLeaveMarkComputers;
                var markHumans = AllyLeaveMarkHumans;
                var chatPauseEnabled = _appliedChatDuringPauseScreen;
                var chatNamesEnabled = ChatColoredNames;
                var chatStampsEnabled = ChatTimestamps;
                var chatHistoryEnabled = _appliedChatHistory; // owned by the Bug fixes tab
                var mpLobbyChatScrollFixEnabled = _appliedMpLobbyChatScrollFix;
                var endGameObserveEnabled = false; // Feature 5 greyed out
                var allianceTeamNumbersEnabled = AllianceTeamNumbers;
                var computerAnnihilatedChatEnabled = ComputerAnnihilatedChat;
                var lobbyMapClickOpenEnabled = LobbyMapClickOpen;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Feature")));
                await Task.Run(() =>
                {
                    WriteExtraFeaturesFile(
                        markComputers, markHumans, chatPauseEnabled, chatNamesEnabled, chatStampsEnabled,
                        chatHistoryEnabled, mpLobbyChatScrollFixEnabled, endGameObserveEnabled,
                        allianceTeamNumbersEnabled,
                        computerAnnihilatedChatEnabled, _appliedBlacksmithWorkCompleteChat,
                        _appliedCastleGoldTooltipFix,
                        dragEnabled: false,
                        dragHex: "#00FF00",
                        networkMonitor: _appliedNetworkMonitor,
                        lobbyMapClickOpen: lobbyMapClickOpenEnabled);
                    ApplyAllyGoneSkullAtlas();
                });

                _appliedAllyLeaveMarkComputers = markComputers;
                _appliedAllyLeaveMarkHumans = markHumans;
                _appliedChatColoredNames = chatNamesEnabled;
                _appliedChatTimestamps = chatStampsEnabled;
                _appliedEndGameObserve = false;
                _appliedAllianceTeamNumbers = allianceTeamNumbersEnabled;
                _appliedComputerAnnihilatedChat = computerAnnihilatedChatEnabled;
                _appliedLobbyMapClickOpen = lobbyMapClickOpenEnabled;
                _hookInjectedForRunningGame = false;
                _chatNameColorInjectedForRunningGame = false;
                _observeInjectedForRunningGame = false;
                _lobbyMapClickInjectedForRunningGame = false;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Hooks")));
                await Task.Run(() =>
                {
                    SyncAllyLeaveWatch();
                    try { SyncAllyLeaveHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                    try { SyncChatNameColorHook(throwOnError: false); }
                    catch { /* optional while Feature is off / game closed */ }
                    try { SyncDragSelectHook(throwOnError: false); }
                    catch { /* keep drag hook disabled */ }
                    try { SyncObserveHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                    try { SyncLobbyMapClickHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                });
            }
            else if (_activeTab == "bugfixes")
            {
                var markComputers = _appliedAllyLeaveMarkComputers;
                var markHumans = _appliedAllyLeaveMarkHumans;
                var chatPauseEnabled = ChatDuringPauseScreen;
                var chatNamesEnabled = _appliedChatColoredNames;
                var chatStampsEnabled = _appliedChatTimestamps;
                var chatHistoryEnabled = ChatHistory; // owned by the Bug fixes tab
                var mpLobbyChatScrollFixEnabled = MpLobbyChatScrollFix;
                var endGameObserveEnabled = false; // Feature 5 greyed out
                var allianceTeamNumbersEnabled = _appliedAllianceTeamNumbers;
                var computerAnnihilatedChatEnabled = _appliedComputerAnnihilatedChat;
                var blacksmithWorkCompleteChatEnabled = false;
                var castleGoldTooltipFixEnabled = CastleGoldTooltipFix;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Bugfixes")));
                await Task.Run(() =>
                {
                    WriteExtraFeaturesFile(
                        markComputers, markHumans, chatPauseEnabled, chatNamesEnabled, chatStampsEnabled,
                        chatHistoryEnabled, mpLobbyChatScrollFixEnabled, endGameObserveEnabled,
                        allianceTeamNumbersEnabled,
                        computerAnnihilatedChatEnabled, blacksmithWorkCompleteChatEnabled,
                        castleGoldTooltipFixEnabled,
                        dragEnabled: false,
                        dragHex: "#00FF00",
                        networkMonitor: _appliedNetworkMonitor,
                        lobbyMapClickOpen: _appliedLobbyMapClickOpen);
                    ApplyBugFixes();
                });

                _appliedChatDuringPauseScreen = chatPauseEnabled;
                _appliedChatHistory = chatHistoryEnabled;
                _appliedMpLobbyChatScrollFix = mpLobbyChatScrollFixEnabled;
                _appliedCastleGoldTooltipFix = castleGoldTooltipFixEnabled;
                _pauseChatInjectedForRunningGame = false;
                _chatNameColorInjectedForRunningGame = false;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Hooks")));
                await Task.Run(() =>
                {
                    SyncAllyLeaveWatch();
                    try { SyncPauseChatHook(throwOnError: false); }
                    catch { /* optional while Bug fix is off */ }
                    try { SyncChatNameColorHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                    try { SyncDragSelectHook(throwOnError: false); }
                    catch { /* keep drag hook disabled */ }
                });
            }
            else if (_activeTab == "network")
            {
                var markComputers = _appliedAllyLeaveMarkComputers;
                var markHumans = _appliedAllyLeaveMarkHumans;
                var chatPauseEnabled = _appliedChatDuringPauseScreen;
                var chatNamesEnabled = _appliedChatColoredNames;
                var chatStampsEnabled = _appliedChatTimestamps;
                var chatHistoryEnabled = _appliedChatHistory;
                var mpLobbyChatScrollFixEnabled = _appliedMpLobbyChatScrollFix;
                var endGameObserveEnabled = false;
                var allianceTeamNumbersEnabled = _appliedAllianceTeamNumbers;
                var computerAnnihilatedChatEnabled = _appliedComputerAnnihilatedChat;
                var blacksmithWorkCompleteChatEnabled = false;
                var networkMonitorEnabled = NetworkMonitor;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Network")));
                await Task.Run(() =>
                {
                    WriteExtraFeaturesFile(
                        markComputers, markHumans, chatPauseEnabled, chatNamesEnabled, chatStampsEnabled,
                        chatHistoryEnabled, mpLobbyChatScrollFixEnabled, endGameObserveEnabled,
                        allianceTeamNumbersEnabled,
                        computerAnnihilatedChatEnabled, blacksmithWorkCompleteChatEnabled,
                        _appliedCastleGoldTooltipFix,
                        dragEnabled: false,
                        dragHex: "#00FF00",
                        networkMonitor: networkMonitorEnabled,
                        lobbyMapClickOpen: _appliedLobbyMapClickOpen);
                });

                _appliedNetworkMonitor = networkMonitorEnabled;
                SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Hooks")));
                await Task.Run(() =>
                {
                    SyncAllyLeaveWatch();
                    try { SyncNetworkMonitorHook(throwOnError: false); }
                    catch { /* optional while game closed */ }
                });
                RefreshNetworkMonitorLog();
            }
        }
        catch (Exception ex)
        {
            if (string.Equals(ex.Message, IncorrectPathMessage(), StringComparison.Ordinal) ||
                !IsCurrentApplyPathValid())
            {
                ShowIncorrectPathStatus();
            }
            else
            {
            StudioDialog.Show(this, ex.Message, "Apply failed", StudioDialogKind.Error);
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

    private const string DropMonitorDll = "DropMonitor.dll";
    private static readonly string DropMonitorLogPath =
        Path.Combine(Path.GetTempPath(), "war2_drop_monitor.log");

    [DllImport(DropMonitorDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void DropMonitor_Init();

    [DllImport(DropMonitorDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void DropMonitor_Enable();

    [DllImport(DropMonitorDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void DropMonitor_Disable();

    [DllImport(DropMonitorDll, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool DropMonitor_IsEnabled();

    private void InitializeDropMonitor()
    {
        // Do not call the legacy native initializer on the WPF startup thread.
        // It can wait for a game-side event before the window is shown.
        DropMonitorStatus = "Monitor is off. Start it when you are ready to test.";
        RefreshDropMonitorLog();
        _dropMonitorTimer.Start();
    }

    private void StartDropMonitor_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            DropMonitor_Enable();
            DropMonitorEnabled = true;
            DropMonitorStatus = "Monitor is running. Reproduce the drop in-game.";
            RefreshDropMonitorLog();
        }
        catch (BadImageFormatException)
        {
            DropMonitorStatus = "Drop monitor requires the 32-bit app build.";
        }
        catch (DllNotFoundException)
        {
            DropMonitorStatus = "DropMonitor.dll is missing from the app folder.";
        }
        catch (Exception)
        {
            DropMonitorStatus = "Could not start monitor. Check that DropMonitor.dll is available.";
        }
    }

    private void StopDropMonitor_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            DropMonitor_Disable();
            DropMonitorEnabled = false;
            DropMonitorStatus = "Monitor is off.";
            RefreshDropMonitorLog();
        }
        catch (Exception)
        {
            DropMonitorStatus = "Could not stop monitor. Check that DropMonitor.dll is available.";
        }
    }

    private void RefreshDropMonitorLog()
    {
        try
        {
            if (!File.Exists(DropMonitorLogPath))
            {
                DropMonitorLog = "No monitor log yet.";
                DropMonitorLastCause = "No drop recorded.";
                return;
            }

            var lines = File.ReadLines(DropMonitorLogPath).TakeLast(250).ToArray();
            DropMonitorLog = string.Join(Environment.NewLine, lines);
            var eventLine = lines.LastOrDefault(line =>
                line.Contains("drop", StringComparison.OrdinalIgnoreCase) ||
                line.Contains("disconnect", StringComparison.OrdinalIgnoreCase) ||
                line.Contains("reason", StringComparison.OrdinalIgnoreCase) ||
                line.Contains("cause", StringComparison.OrdinalIgnoreCase));
            if (eventLine is not null)
                DropMonitorLastCause = ClassifyDropCause(eventLine);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private static string ClassifyDropCause(string line)
    {
        if (line.Contains("timeout", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("timed out", StringComparison.OrdinalIgnoreCase))
            return "Likely network timeout: " + line.Trim();
        if (line.Contains("disconnect", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("connection", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("network", StringComparison.OrdinalIgnoreCase))
            return "Likely network disconnect: " + line.Trim();
        if (line.Contains("error", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("exception", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("failed", StringComparison.OrdinalIgnoreCase))
            return "Hook/game error: " + line.Trim();
        if (line.Contains("left", StringComparison.OrdinalIgnoreCase) ||
            line.Contains("quit", StringComparison.OrdinalIgnoreCase))
            return "Player left voluntarily or the game reported a quit: " + line.Trim();
        return "Unclassified drop event: " + line.Trim();
    }

    private static readonly string[] SelfMonitorPaletteFiles =
    [
        @"x86\Data\Art\bgs\Forest\forest.ppl",
        @"x86\Data\Art\bgs\Iceland\iceland.ppl",
        @"x86\Data\Art\bgs\Swamp\swamp.ppl",
        @"x86\Data\Art\bgs\XSwamp\xswamp.ppl",
    ];

    private void StartSelfMonitor_Click(object sender, RoutedEventArgs e)
    {
        SelfMonitorEnabled = true;
        _selfMonitorLastColors.Clear();
        SelfMonitorStatus = "Monitor is running. Play until the self highlight changes.";
        SelfMonitorLog = "Self-highlight monitor started.";
        SelfMonitorLastCause = "No color change recorded.";
        RefreshSelfMonitor();
        _selfMonitorTimer.Start();
    }

    private void StopSelfMonitor_Click(object sender, RoutedEventArgs e)
    {
        _selfMonitorTimer.Stop();
        SelfMonitorEnabled = false;
        SelfMonitorStatus = "Monitor is off.";
    }

    private void RefreshSelfMonitor()
    {
        var root = NormalizeGameRoot(_appliedGameInstallPath);
        var expected = _appliedOtherHexByKey.TryGetValue("selectionHighlight", out var applied)
            ? applied
            : "#00FF00";
        var events = new List<string>();
        foreach (var relative in SelfMonitorPaletteFiles)
        {
            var path = Path.Combine(root, relative);
            if (!File.Exists(path)) continue;
            try
            {
                var bytes = File.ReadAllBytes(path);
                const int paletteOffset = 250 * 3;
                if (bytes.Length < paletteOffset + 3) continue;
                var actual = $"#{ScalePaletteChannel(bytes[paletteOffset]):X2}{ScalePaletteChannel(bytes[paletteOffset + 1]):X2}{ScalePaletteChannel(bytes[paletteOffset + 2]):X2}";
                var key = Path.GetFileName(path);
                if (!_selfMonitorLastColors.TryGetValue(key, out var previous))
                {
                    _selfMonitorLastColors[key] = actual;
                    continue;
                }
                if (string.Equals(previous, actual, StringComparison.OrdinalIgnoreCase)) continue;

                var cause = ExplainSelfColorChange(path, previous, actual, expected);
                events.Add($"[{DateTime.Now:HH:mm:ss}] {key}: {previous} -> {actual}. {cause}");
                _selfMonitorLastColors[key] = actual;
                SelfMonitorLastCause = cause;
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        if (events.Count > 0)
        {
            var existing = SelfMonitorLog == "Self-highlight monitor started." ? "" : SelfMonitorLog;
            SelfMonitorLog = string.Join(Environment.NewLine,
                (existing + Environment.NewLine + string.Join(Environment.NewLine, events))
                    .Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries)
                    .TakeLast(250));
        }
        else if (_selfMonitorLastColors.Count > 0)
        {
            SelfMonitorStatus = "Monitoring palette index 250. No change detected.";
        }
    }

    private string ExplainSelfColorChange(string palettePath, string previous, string actual, string expected)
    {
        var gameRunning = IsWarcraftIiRunning();
        var patchLog = Path.Combine(Path.GetDirectoryName(_enginePath)!, "war2_color_patch_log.txt");
        var recentPatch = File.Exists(patchLog)
            ? File.ReadLines(patchLog).TakeLast(1).FirstOrDefault() ?? ""
            : "";
        if (!string.Equals(actual, expected, StringComparison.OrdinalIgnoreCase))
        {
            if (gameRunning)
                return $"Runtime palette changed while the game was running (expected {expected}; likely game/HD palette selection).";
            return $"Palette differs from the configured self highlight (expected {expected}; likely an apply/restore operation).";
        }
        if (!string.IsNullOrWhiteSpace(recentPatch) && recentPatch.Contains("Applied", StringComparison.OrdinalIgnoreCase))
            return "Palette returned to the configured value after an apply operation.";
        return $"Palette changed outside the expected value; file timestamp is {File.GetLastWriteTime(palettePath):HH:mm:ss}.";
    }

    private static int ScalePaletteChannel(byte channel) => (int)Math.Round(channel * 255.0 / 63.0);

    private void ColorBlindOn_Click(object sender, RoutedEventArgs e)
    {
        if (_colorBlindMode) return;
        ColorBlindMode = true;
    }

    private void ColorBlindOff_Click(object sender, RoutedEventArgs e)
    {
        if (!_colorBlindMode) return;
        ColorBlindMode = false;
    }

    private void RefreshCheckBoxVisuals()
    {
        CheckBoxTheme.SetIsColorBlindMode(this, _colorBlindMode);
        Dispatcher.BeginInvoke(() =>
        {
            foreach (var checkBox in FindVisualChildren<System.Windows.Controls.CheckBox>(this))
            {
                CheckBoxTheme.SetIsColorBlindMode(checkBox, _colorBlindMode);
                checkBox.InvalidateProperty(CheckBoxTheme.IsColorBlindModeProperty);
            }
        }, DispatcherPriority.Loaded);
    }

    private static IEnumerable<T> FindVisualChildren<T>(DependencyObject parent) where T : DependencyObject
    {
        var count = VisualTreeHelper.GetChildrenCount(parent);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T match)
                yield return match;

            foreach (var descendant in FindVisualChildren<T>(child))
                yield return descendant;
        }
    }

    private void ColorBlindPreset_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.Button { Tag: string presetKey }) return;
        ApplyColorBlindPreset(presetKey);
    }

    private void ApplyColorBlindPreset(string presetKey)
    {
        if (IsCustomColorPreset(presetKey))
        {
            // Keep current slot colors — Custom is free edit mode, not a vanilla wipe.
            SwitchToCustomColorMode(resetToVanilla: false);
            return;
        }

        if (!ColorBlindPresets.TryGet(presetKey, out _, out var hint))
            return;

        _unitSpriteColors = true;
        _selectedColorBlindPreset = presetKey;
        ApplyPresetColorsToCards(presetKey);
        SyncHexTextBoxesFromModel();
        Keyboard.ClearFocus();

        ColorBlindPresetHintText = hint;
        RefreshColorBlindPresetButtons();
        RefreshTabStatus();
    }

    private static System.Windows.Media.Brush SegmentButtonForeground(bool selected, bool colorBlindMode, System.Windows.Media.Brush textBrush) =>
        selected || !colorBlindMode
            ? System.Windows.Media.Brushes.White
            : textBrush;

    private void RefreshColorBlindPresetButtons()
    {
        if (ColorBlindPresetPanel is null) return;
        var borderThickness = AppTheme.ControlBorderThickness;
        var unselectedBackground = _colorBlindMode
            ? (System.Windows.Media.Brush)FindResource("ButtonBackgroundBrush")
            : SegmentUnselectedBrush;
        var unselectedBorder = _colorBlindMode
            ? (System.Windows.Media.Brush)FindResource("ControlBorderBrush")
            : SegmentUnselectedBorderBrush;
        var textBrush = (System.Windows.Media.Brush)FindResource("TextBrush");
        foreach (var child in ColorBlindPresetPanel.Children)
        {
            if (child is not System.Windows.Controls.Button button) continue;
            var tag = button.Tag as string ?? "";
            var selected = string.Equals(tag, _selectedColorBlindPreset, StringComparison.OrdinalIgnoreCase) ||
                (IsCustomColorPreset(tag) && IsCustomColorPreset(_selectedColorBlindPreset));
            button.BorderThickness = borderThickness;
            button.Background = selected ? SegmentSelectedBrush : unselectedBackground;
            button.BorderBrush = selected ? SegmentSelectedBorderBrush : unselectedBorder;
            button.Foreground = SegmentButtonForeground(selected, _colorBlindMode, textBrush);
        }
    }

    private void RefreshColorBlindModeButtons()
    {
        if (ColorBlindOnButton is null || ColorBlindOffButton is null) return;
        var borderThickness = AppTheme.ControlBorderThickness;
        var unselectedBackground = _colorBlindMode
            ? (System.Windows.Media.Brush)FindResource("ButtonBackgroundBrush")
            : SegmentUnselectedBrush;
        var unselectedBorder = _colorBlindMode
            ? (System.Windows.Media.Brush)FindResource("ControlBorderBrush")
            : SegmentUnselectedBorderBrush;
        var textBrush = (System.Windows.Media.Brush)FindResource("TextBrush");
        var onSelected = _colorBlindMode;
        var offSelected = !_colorBlindMode;

        ColorBlindOnButton.BorderThickness = borderThickness;
        ColorBlindOffButton.BorderThickness = borderThickness;
        ColorBlindOnButton.Background = onSelected ? SegmentSelectedBrush : unselectedBackground;
        ColorBlindOnButton.BorderBrush = onSelected ? SegmentSelectedBorderBrush : unselectedBorder;
        ColorBlindOffButton.Background = offSelected ? SegmentSelectedBrush : unselectedBackground;
        ColorBlindOffButton.BorderBrush = offSelected ? SegmentSelectedBorderBrush : unselectedBorder;
        ColorBlindOnButton.Foreground = SegmentButtonForeground(onSelected, _colorBlindMode, textBrush);
        ColorBlindOffButton.Foreground = SegmentButtonForeground(offSelected, _colorBlindMode, textBrush);
    }

    private async void Restore_Click(object sender, RoutedEventArgs e)
    {
        if (_isApplying) return;

        var confirm = StudioDialog.Confirm(this,
            "Restore all modded game files to the local vanilla backup and turn Extra features off?\n\n" +
            "This is the fast restore. You can also use Battle.net Scan and Repair (slower).\n\n" +
            "Restart Warcraft II afterwards.",
            "Restore clean install");
        if (!confirm) return;

        try
        {
            IsApplying = true;
            SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Restoring")));
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
                    ChatTimestamps = false,
                    ChatHistory = false,
                    MpLobbyChatScrollFix = false,
                    EndGameObserve = false,
                    AllianceTeamNumbers = false,
                    ComputerAnnihilatedChat = false,
                    BlacksmithWorkCompleteChat = false,
                    VoiceAudioEnhance = false,
                    HumanFootmanAudio = false,
                    HumanKnightAudio = false,
                    CastleGoldTooltipFix = false,
                    NetworkMonitor = false,
                    LobbyMapClickOpen = false,
                    DragSelectColorEnabled = false,
                    DragSelectColor = "#00FF00",
                    UnitSpriteColors = false,
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
            _chatTimestamps = false;
            _appliedChatTimestamps = false;
            _chatHistory = false;
            _appliedChatHistory = false;
            _mpLobbyChatScrollFix = false;
            _appliedMpLobbyChatScrollFix = false;
            _castleGoldTooltipFix = false;
            _appliedCastleGoldTooltipFix = false;
            _networkMonitor = false;
            _appliedNetworkMonitor = false;
            _endGameObserve = false;
            _appliedEndGameObserve = false;
            _allianceTeamNumbers = false;
            _appliedAllianceTeamNumbers = false;
            _computerAnnihilatedChat = false;
            _appliedComputerAnnihilatedChat = false;
            _blacksmithWorkCompleteChat = false;
            _appliedBlacksmithWorkCompleteChat = false;
            _lobbyMapClickOpen = false;
            _appliedLobbyMapClickOpen = false;
            _unitSpriteColors = false;
            _appliedUnitSpriteColors = false;
            _appliedDragSelectColorEnabled = false;
            OnPropertyChanged(nameof(AllyLeaveMarkComputers));
            OnPropertyChanged(nameof(AllyLeaveMarkHumans));
            OnPropertyChanged(nameof(ChatDuringPauseScreen));
            OnPropertyChanged(nameof(ChatColoredNames));
            OnPropertyChanged(nameof(ChatTimestamps));
            OnPropertyChanged(nameof(ChatHistory));
            OnPropertyChanged(nameof(MpLobbyChatScrollFix));
            OnPropertyChanged(nameof(CastleGoldTooltipFix));
            OnPropertyChanged(nameof(NetworkMonitor));
            OnPropertyChanged(nameof(LobbyMapClickOpen));
            OnPropertyChanged(nameof(EndGameObserve));
            OnPropertyChanged(nameof(AllianceTeamNumbers));
            OnPropertyChanged(nameof(ComputerAnnihilatedChat));
            OnPropertyChanged(nameof(UpgradeNotifications));
            OnPropertyChanged(nameof(BlacksmithWorkCompleteChat));

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

            _selectedColorBlindPreset = "custom";
            _appliedColorBlindPreset = "custom";
            ColorBlindPresetHintText = Localization.Get("ColorBlind.CustomHint");
            RefreshColorBlindPresetButtons();

            _hookInjectedForRunningGame = false;
            _pauseChatInjectedForRunningGame = false;
            _chatNameColorInjectedForRunningGame = false;
            _dragSelectInjectedForRunningGame = false;
            _observeInjectedForRunningGame = false;
            _networkMonitorInjectedForRunningGame = false;
            _lobbyMapClickInjectedForRunningGame = false;
            SetStatusLines(StatusLine("", ReadyIconBrush, Localization.Get("Status.Applying.Hooks")));
            await Task.Run(() =>
            {
                SyncAllyLeaveWatch();
                try { SyncAllyLeaveHook(throwOnError: false); } catch { /* off */ }
                try { SyncPauseChatHook(throwOnError: false); } catch { /* off */ }
                try { SyncChatNameColorHook(throwOnError: false); } catch { /* off */ }
                try { SyncDragSelectHook(throwOnError: false); } catch { /* off */ }
                try { SyncUnitColorHook(throwOnError: false); } catch { /* off */ }
                try { SyncObserveHook(throwOnError: false); } catch { /* off */ }
                try { SyncNetworkMonitorHook(throwOnError: false); } catch { /* off */ }
                try { SyncLobbyMapClickHook(throwOnError: false); } catch { /* off */ }
            });

            StudioDialog.Show(this,
                "Vanilla files restored and Extra features turned off.\nRestart Warcraft II Remastered to finish.",
                "Restore complete");
        }
        catch (Exception ex)
        {
            StudioDialog.Show(this, ex.Message, "Restore failed", StudioDialogKind.Error);
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

public sealed class NetworkSeatRow
{
    public int Seat { get; }
    public string Name { get; }
    public string LastMs { get; }
    public string MaxMs { get; }

    public NetworkSeatRow(int seat, string name, string lastMs, string maxMs)
    {
        Seat = seat;
        Name = name;
        LastMs = lastMs;
        MaxMs = maxMs;
    }
}

public sealed class ExtraFeaturesConfig
{
    public bool AllyLeaveMarkComputers { get; set; }
    public bool AllyLeaveMarkHumans { get; set; }
    /// <summary>Legacy: true when either mark mode is on (watcher / older hooks).</summary>
    public bool AllyLeaveRedNames { get; set; }
    public bool ChatDuringPauseScreen { get; set; }
    public bool ChatColoredNames { get; set; }
    public bool ChatTimestamps { get; set; }
    /// <summary>PageUp/PageDown re-show earlier chat lines in a match.</summary>
    public bool ChatHistory { get; set; }
    /// <summary>Keep multiplayer lobby chat scroll when slots refresh (race change).</summary>
    public bool MpLobbyChatScrollFix { get; set; }
    /// <summary>Fix Castle/Fortress tooltip: +25% to +20% gold production text.</summary>
    public bool CastleGoldTooltipFix { get; set; }
    /// <summary>Observe button on the defeat popup (close screen, keep watching).</summary>
    public bool EndGameObserve { get; set; }
    /// <summary>Gold lobby team digit in front of alliances (F11) player names.</summary>
    public bool AllianceTeamNumbers { get; set; }
    /// <summary>Chat line "Name annihilated" when a computer AI is wiped.</summary>
    public bool ComputerAnnihilatedChat { get; set; }
    /// <summary>Gold chat line when a blacksmith upgrade completes.</summary>
    public bool BlacksmithWorkCompleteChat { get; set; }
    /// <summary>Mastered unit voice lines for all Gamesfx voice folders.</summary>
    public bool VoiceAudioEnhance { get; set; }
    /// <summary>Legacy alias for VoiceAudioEnhance.</summary>
    public bool HumanFootmanAudio { get; set; }
    /// <summary>Legacy alias for VoiceAudioEnhance.</summary>
    public bool HumanKnightAudio { get; set; }
    /// <summary>MP match frame-gap monitor for Studio Network tab.</summary>
    public bool NetworkMonitor { get; set; }
    /// <summary>Click map name in MP lobby to open the .pud in Explorer.</summary>
    public bool LobbyMapClickOpen { get; set; }
    public bool DragSelectColorEnabled { get; set; }
    public string? DragSelectColor { get; set; }
    /// <summary>Live-recolor HD unit sprites to the player colors.</summary>
    public bool UnitSpriteColors { get; set; }
}

public sealed class MapCatalogEntry
{
    public string Filename { get; set; } = "";
    public string Title { get; set; } = "";
    public string Category { get; set; } = "";
    public string Size { get; set; } = "";
    public int PlayerCount { get; set; }
    public int Era { get; set; }
    public bool HasLocalImage { get; set; }
    public string? PreviewPath { get; set; }
    /// <summary>Real map name from the .pud filename (no title/description).</summary>
    public string MapName =>
        Path.GetFileNameWithoutExtension(Filename ?? "") is { Length: > 0 } stem ? stem : (Filename ?? "");
    public string Tileset => Era switch
    {
        0 => "Forest",
        1 => "Winter",
        2 => "Wasteland",
        3 => "Swamp",
        _ => "Unknown",
    };
}

public sealed class StudioSettings
{
    public string? GameRootPath { get; set; }
    public string? MapEditorPath { get; set; }
    public string? MapsPath { get; set; }
        public string? MapImagesPath { get; set; }
    public bool ColorBlindMode { get; set; }
    public string? Language { get; set; }
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
