using System.Collections.Concurrent;
using System.IO;
using System.Net.Http;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace War2ContentStudio.Editor;

/// <summary>
/// Loads map unit sprites from jcfieldsdev/warcraft2-map-editor (MIT), cached locally.
/// </summary>
public sealed class UnitSpriteCache
{
    private static readonly HttpClient Http = new();
    private static readonly string[] EraNames = ["forest", "winter", "wasteland", "swamp"];
    private static readonly string CacheRoot = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "War2ContentStudio",
        "unit-sprites");

    private const string BaseUrl =
        "https://raw.githubusercontent.com/jcfieldsdev/warcraft2-map-editor/master/www/units";

    private readonly ConcurrentDictionary<string, BitmapSource?> _memory = new();
    private readonly HashSet<string> _pending = [];
    private readonly object _pendingLock = new();

    public event Action? SpritesUpdated;

    public BitmapSource? TryGet(int era, byte unitType)
    {
        var key = Key(era, unitType);
        if (_memory.TryGetValue(key, out var cached)) return cached;

        var path = LocalPath(era, unitType);
        if (!File.Exists(path)) return null;

        var bmp = LoadBitmap(path);
        _memory[key] = bmp;
        return bmp;
    }

    /// <summary>One map tile = one cell; mobile units use first idle frame from vertical strip.</summary>
    public BitmapSource? TryGetTileSprite(int era, byte unitType)
    {
        var full = TryGet(era, unitType);
        if (full is null) return null;

        var w = full.PixelWidth;
        var h = full.PixelHeight;
        if (h > w)
        {
            var frame = new CroppedBitmap(full, new Int32Rect(0, 0, w, w));
            frame.Freeze();
            return frame;
        }

        return full;
    }

    public void Ensure(int era, byte unitType)
    {
        var key = Key(era, unitType);
        if (_memory.ContainsKey(key) || File.Exists(LocalPath(era, unitType))) return;

        lock (_pendingLock)
        {
            if (!_pending.Add(key)) return;
        }

        _ = Task.Run(async () =>
        {
            BitmapSource? bmp = null;
            try
            {
                var path = LocalPath(era, unitType);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                var url = RemoteUrl(era, unitType);
                var bytes = await Http.GetByteArrayAsync(url);
                await File.WriteAllBytesAsync(path, bytes);
                bmp = LoadBitmap(path);
            }
            catch
            {
                bmp = null;
            }

            _memory[key] = bmp;
            lock (_pendingLock) _pending.Remove(key);
            SpritesUpdated?.Invoke();
        });
    }

    public void Prefetch(int era, IEnumerable<byte> unitTypes)
    {
        foreach (var type in unitTypes.Distinct())
        {
            if (TryGet(era, type) is null) Ensure(era, type);
        }
    }

    private static string Key(int era, byte unitType) =>
        $"{EraNames[Math.Clamp(era, 0, EraNames.Length - 1)]}/{unitType:D4}";

    private static string LocalPath(int era, byte unitType) =>
        Path.Combine(CacheRoot, EraNames[Math.Clamp(era, 0, EraNames.Length - 1)], $"{unitType:D4}.png");

    private static string RemoteUrl(int era, byte unitType) =>
        $"{BaseUrl}/{EraNames[Math.Clamp(era, 0, EraNames.Length - 1)]}/{unitType:D4}.png";

    private static BitmapSource LoadBitmap(string path)
    {
        var bmp = new BitmapImage();
        bmp.BeginInit();
        bmp.CacheOption = BitmapCacheOption.OnLoad;
        bmp.UriSource = new Uri(path, UriKind.Absolute);
        bmp.EndInit();
        bmp.Freeze();
        return bmp;
    }
}
