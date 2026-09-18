using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Color = System.Windows.Media.Color;

namespace PlayerColorStudio;

/// <summary>
/// Renders a simple Warcraft II .pud terrain minimap to a JPEG
/// named after the pud basename (no .QoL suffix).
/// </summary>
internal static class PudMapImageGenerator
{
    private static readonly Color[] PlayerColors =
    [
        Color.FromRgb(0x20, 0x60, 0xE0),
        Color.FromRgb(0xE0, 0x60, 0x20),
        Color.FromRgb(0x20, 0xE0, 0x40),
        Color.FromRgb(0xE0, 0xE0, 0x40),
        Color.FromRgb(0xA0, 0x40, 0xE0),
        Color.FromRgb(0x40, 0xE0, 0xE0),
        Color.FromRgb(0xE0, 0xE0, 0xE0),
        Color.FromRgb(0xE0, 0x40, 0x40),
    ];

    public static bool TryGenerate(string pudPath, string jpgPath, out string? error)
    {
        error = null;
        try
        {
            var bytes = File.ReadAllBytes(pudPath);
            var sections = ReadSections(bytes);
            if (!sections.TryGetValue("DIM ", out var dim) || dim.Length < 4)
            {
                error = "missing DIM";
                return false;
            }

            var width = BitConverter.ToUInt16(dim, 0);
            var height = BitConverter.ToUInt16(dim, 2);
            if (width == 0 || height == 0 || width > 256 || height > 256)
            {
                error = $"bad size {width}x{height}";
                return false;
            }

            var era = 0;
            if (sections.TryGetValue("ERA ", out var eraData) && eraData.Length >= 2)
                era = BitConverter.ToUInt16(eraData, 0);
            else if (sections.TryGetValue("ERAX", out eraData) && eraData.Length >= 2)
                era = BitConverter.ToUInt16(eraData, 0);

            byte[]? tileData = null;
            if (sections.TryGetValue("MTXM", out var mtxm) && mtxm.Length >= width * height * 2)
                tileData = mtxm;
            else if (sections.TryGetValue("SQM ", out var sqm) && sqm.Length >= width * height * 2)
                tileData = sqm;

            if (tileData is null)
            {
                error = "missing MTXM/SQM";
                return false;
            }

            // Keep the preview readable without magnifying the simplified tile art.
            var scale = Math.Max(1, Math.Min(32, 1024 / Math.Max(width, height)));
            var outW = width * scale;
            var outH = height * scale;
            var pixels = new byte[outW * outH * 4];

            for (var y = 0; y < height; y++)
            {
                for (var x = 0; x < width; x++)
                {
                    var tile = BitConverter.ToUInt16(tileData, (y * width + x) * 2);
                    var color = ColorForTile(tile, era);
                    DrawTile(pixels, outW, outH, x * scale, y * scale, scale, color, tile);
                }
            }

            if (sections.TryGetValue("UNIT", out var unitData))
            {
                var count = unitData.Length / 8;
                for (var i = 0; i < count; i++)
                {
                    var off = i * 8;
                    var ux = BitConverter.ToUInt16(unitData, off);
                    var uy = BitConverter.ToUInt16(unitData, off + 2);
                    var type = unitData[off + 4];
                    var owner = unitData[off + 5];
                    var ai = BitConverter.ToUInt16(unitData, off + 6);
                    if (ux >= width || uy >= height) continue;

                    switch (type)
                    {
                        case 0x5C: // gold mine
                            DrawMine(pixels, outW, outH, ux, uy, ai, scale, Color.FromRgb(0xFF, 0xD7, 0x00));
                            break;
                        case 0x5D: // oil
                            DrawMine(pixels, outW, outH, ux, uy, ai, scale, Color.FromRgb(0x20, 0x20, 0x20));
                            break;
                        case 0x5E: // human start
                        case 0x5F: // orc start
                            DrawStart(pixels, outW, outH, ux, uy, scale, PlayerColors[owner % 8]);
                            break;
                    }
                }
            }

            var bmp = BitmapSource.Create(outW, outH, 96, 96, PixelFormats.Bgra32, null, pixels, outW * 4);
            bmp.Freeze();

            var dir = Path.GetDirectoryName(jpgPath);
            if (!string.IsNullOrWhiteSpace(dir))
                Directory.CreateDirectory(dir);

            var tmp = jpgPath + ".part";
            using (var fs = File.Create(tmp))
            {
                var encoder = new JpegBitmapEncoder { QualityLevel = 96 };
                encoder.Frames.Add(BitmapFrame.Create(bmp));
                encoder.Save(fs);
            }

            File.Move(tmp, jpgPath, overwrite: true);
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            try
            {
                var tmp = jpgPath + ".part";
                if (File.Exists(tmp)) File.Delete(tmp);
            }
            catch { /* ignore */ }
            return false;
        }
    }

    private static Dictionary<string, byte[]> ReadSections(byte[] bytes)
    {
        var sections = new Dictionary<string, byte[]>(StringComparer.Ordinal);
        var offset = 0;
        while (offset + 8 <= bytes.Length)
        {
            var name = System.Text.Encoding.ASCII.GetString(bytes, offset, 4);
            var size = BitConverter.ToInt32(bytes, offset + 4);
            offset += 8;
            if (size < 0 || offset + size > bytes.Length)
                break;
            var data = new byte[size];
            if (size > 0)
                Buffer.BlockCopy(bytes, offset, data, 0, size);
            sections[name] = data;
            offset += size;
        }
        return sections;
    }

    private static Color ColorForTile(ushort tile, int era)
    {
        var baseColor = TileBaseColor(tile);
        return TintForEra(baseColor, era);
    }

    private static Color TileBaseColor(ushort tile)
    {
        var prefix = tile & 0xFF00;
        var nibble = (tile >> 4) & 0xF;
        if (tile < 0x0100)
        {
            return nibble switch
            {
                1 => Color.FromRgb(0x44, 0x88, 0xCC),
                2 => Color.FromRgb(0x33, 0x66, 0xAA),
                3 => Color.FromRgb(0xC8, 0xA0, 0x70),
                4 => Color.FromRgb(0xA0, 0x78, 0x50),
                5 => Color.FromRgb(0x44, 0xAA, 0x44),
                6 => Color.FromRgb(0x33, 0x88, 0x33),
                7 => Color.FromRgb(0x22, 0x66, 0x22),
                8 => Color.FromRgb(0x88, 0x88, 0x88),
                9 => Color.FromRgb(0x99, 0x99, 0xAA),
                0xA => Color.FromRgb(0x66, 0x44, 0x33),
                0xB => Color.FromRgb(0x88, 0x88, 0x99),
                0xC => Color.FromRgb(0x55, 0x33, 0x22),
                _ => Color.FromRgb(0x55, 0x55, 0x55),
            };
        }

        return prefix switch
        {
            0x0100 => Color.FromRgb(0x3A, 0x78, 0xBB),
            0x0200 => Color.FromRgb(0x55, 0x99, 0xBB),
            0x0300 => Color.FromRgb(0x88, 0x70, 0x50),
            0x0400 => Color.FromRgb(0x77, 0x77, 0x66),
            0x0500 => Color.FromRgb(0x66, 0x99, 0x55),
            0x0600 => Color.FromRgb(0x3A, 0x99, 0x3A),
            0x0700 => Color.FromRgb(0x2A, 0x77, 0x2A),
            0x0800 => Color.FromRgb(0x77, 0x88, 0x77),
            0x0900 => Color.FromRgb(0x66, 0x55, 0x44),
            _ => Color.FromRgb(0x55, 0x55, 0x55),
        };
    }

    private static Color TintForEra(Color c, int era) => era switch
    {
        // Winter — cooler / snowier
        1 => Color.FromRgb(
            (byte)Math.Min(255, c.R + 40),
            (byte)Math.Min(255, c.G + 45),
            (byte)Math.Min(255, c.B + 55)),
        // Wasteland — dusty brown
        2 => Color.FromRgb(
            (byte)Math.Min(255, (c.R * 5 + 180) / 6),
            (byte)Math.Min(255, (c.G * 4 + 120) / 5),
            (byte)Math.Min(255, (c.B * 3 + 60) / 4)),
        // Swamp — darker / murkier green
        3 => Color.FromRgb(
            (byte)Math.Max(0, c.R * 3 / 4),
            (byte)Math.Max(0, (c.G * 5 + 40) / 6),
            (byte)Math.Max(0, c.B * 2 / 3)),
        // Forest (default)
        _ => c,
    };

    private static void DrawTile(byte[] pixels, int outW, int outH, int x, int y, int scale, Color color, ushort tile)
    {
        FillRect(pixels, outW, outH, x, y, scale, scale, color);
        if (scale < 4) return;

        var light = AdjustColor(color, 18);
        var shade = AdjustColor(color, -22);
        var edge = Math.Max(1, scale / 32);
        FillRect(pixels, outW, outH, x, y, scale, edge, light);
        FillRect(pixels, outW, outH, x, y + scale - edge, scale, edge, shade);

        if (scale < 8) return;

        var hash = unchecked((uint)(tile * 2654435761u + (uint)x * 17u + (uint)y * 31u));
        var accent = IsWater(color) ? light : shade;
        var accent2 = IsWater(color) ? AdjustColor(color, 30) : AdjustColor(color, 14);
        var inset = Math.Max(2, scale / 4);
        var mark = Math.Max(1, scale / 18);
        var px = x + inset + (int)(hash % (uint)Math.Max(1, scale - inset * 2));
        var py = y + inset + (int)((hash >> 8) % (uint)Math.Max(1, scale - inset * 2));

        if (IsWater(color))
        {
            FillRect(pixels, outW, outH, x + inset, py, scale - inset * 2, mark, accent2);
            FillRect(pixels, outW, outH, x + inset + scale / 5, py + scale / 3, scale / 2, mark, accent);
        }
        else if (IsGreen(color))
        {
            FillRect(pixels, outW, outH, px, py, mark * 2, mark, accent);
            FillRect(pixels, outW, outH, px + scale / 3, py + scale / 2, mark, mark * 2, accent2);
            FillRect(pixels, outW, outH, x + inset, y + scale - inset - mark, scale / 3, mark, accent);
        }
        else
        {
            FillRect(pixels, outW, outH, px, py, scale / 3, mark, accent);
            FillRect(pixels, outW, outH, px + scale / 4, py + scale / 3, scale / 4, mark, accent2);
        }
    }

    private static bool IsWater(Color color) => color.B > color.R + 18 && color.B > color.G + 8;

    private static bool IsGreen(Color color) => color.G > color.R + 12 && color.G > color.B + 6;

    private static Color AdjustColor(Color color, int delta)
    {
        static byte Clamp(int value) => (byte)Math.Clamp(value, 0, 255);
        return Color.FromRgb(
            Clamp(color.R + delta),
            Clamp(color.G + delta),
            Clamp(color.B + delta));
    }

    private static void DrawMine(byte[] pixels, int outW, int outH, int tx, int ty, ushort ai, int scale, Color color)
    {
        var d = 1 + (ai >= 20 ? 1 : 0); // ai*2500 approx; treat large as bigger blob
        var cx = tx * scale + scale / 2;
        var cy = ty * scale + scale / 2;
        var rad = Math.Max(scale, d * scale);
        FillRect(pixels, outW, outH, cx - rad, cy - rad, rad * 2 + 1, rad * 2 + 1, color);
    }

    private static void DrawStart(byte[] pixels, int outW, int outH, int tx, int ty, int scale, Color color)
    {
        var cx = tx * scale + scale / 2;
        var cy = ty * scale + scale / 2;
        var r = Math.Max(2, scale);
        FillRect(pixels, outW, outH, cx - r, cy - r, r * 2 + 1, r * 2 + 1, color);
        // dark corners for X marker readability
        var black = Color.FromRgb(0, 0, 0);
        SetPixel(pixels, outW, outH, cx - r, cy - r, black);
        SetPixel(pixels, outW, outH, cx + r, cy - r, black);
        SetPixel(pixels, outW, outH, cx - r, cy + r, black);
        SetPixel(pixels, outW, outH, cx + r, cy + r, black);
    }

    private static void FillRect(byte[] pixels, int outW, int outH, int x, int y, int w, int h, Color color)
    {
        for (var dy = 0; dy < h; dy++)
        {
            var py = y + dy;
            if (py < 0 || py >= outH) continue;
            for (var dx = 0; dx < w; dx++)
            {
                var px = x + dx;
                if (px < 0 || px >= outW) continue;
                var i = (py * outW + px) * 4;
                pixels[i] = color.B;
                pixels[i + 1] = color.G;
                pixels[i + 2] = color.R;
                pixels[i + 3] = 255;
            }
        }
    }

    private static void SetPixel(byte[] pixels, int outW, int outH, int x, int y, Color color)
    {
        if (x < 0 || y < 0 || x >= outW || y >= outH) return;
        var i = (y * outW + x) * 4;
        pixels[i] = color.B;
        pixels[i + 1] = color.G;
        pixels[i + 2] = color.R;
        pixels[i + 3] = 255;
    }
}
