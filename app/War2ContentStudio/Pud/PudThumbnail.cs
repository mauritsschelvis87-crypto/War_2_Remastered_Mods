using System.Windows.Media;
using System.Windows.Media.Imaging;
using War2ContentStudio.Editor;

namespace War2ContentStudio.Pud;

public static class PudThumbnail
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

    public static WriteableBitmap Render(PudDocument doc, int scale = 4)
    {
        scale = Math.Max(1, scale);
        var size = doc.MapSize;
        var outSize = size * scale;
        var pixels = new byte[outSize * outSize * 4];
        var tiles = doc.GetTiles();

        for (var y = 0; y < size; y++)
        {
            for (var x = 0; x < size; x++)
            {
                FillRect(pixels, outSize, x * scale, y * scale, scale, scale, PudCatalog.ColorForTile(tiles[y * size + x]));
            }
        }

        foreach (var unit in doc.GetUnits())
        {
            var (tw, th) = UnitFootprints.Get(unit.Type);
            var color = ColorForUnit(unit);
            var px = Math.Clamp(unit.X, 0, size - 1);
            var py = Math.Clamp(unit.Y, 0, size - 1);
            FillRect(pixels, outSize, px * scale, py * scale, tw * scale, th * scale, color);
        }

        var bmp = new WriteableBitmap(outSize, outSize, 96, 96, PixelFormats.Bgra32, null);
        bmp.WritePixels(new System.Windows.Int32Rect(0, 0, outSize, outSize), pixels, outSize * 4, 0);
        bmp.Freeze();
        return bmp;
    }

    private static Color ColorForUnit(PudUnit unit) => unit.Type switch
    {
        PudUnit.TypeGoldMine => Color.FromRgb(0xFF, 0xD7, 0x00),
        PudUnit.TypeOilPatch => Color.FromRgb(0x20, 0x20, 0x20),
        PudUnit.TypeHumanStart => PlayerColors[unit.Owner % 8],
        PudUnit.TypeOrcStart => PlayerColors[unit.Owner % 8],
        _ when UnitFootprints.IsBuilding(unit.Type) => Color.FromRgb(0xBB, 0x88, 0x55),
        _ => PlayerColors[unit.Owner % 8],
    };

    private static void FillRect(byte[] pixels, int stride, int x, int y, int w, int h, Color color)
    {
        for (var dy = 0; dy < h; dy++)
        {
            for (var dx = 0; dx < w; dx++)
            {
                var px = x + dx;
                var py = y + dy;
                if (px < 0 || py < 0 || px >= stride || py >= stride) continue;
                var i = (py * stride + px) * 4;
                pixels[i] = color.B;
                pixels[i + 1] = color.G;
                pixels[i + 2] = color.R;
                pixels[i + 3] = 255;
            }
        }
    }
}
