using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
// The project also references WinForms — pin the WPF types explicitly.
using Color = System.Windows.Media.Color;
using KeyEventArgs = System.Windows.Input.KeyEventArgs;
using MouseEventArgs = System.Windows.Input.MouseEventArgs;
using Point = System.Windows.Point;

namespace PlayerColorStudio;

/// <summary>
/// App-styled color picker (replaces the WinForms ColorDialog): saturation /
/// brightness surface, hue bar and a hex field, all using the studio theme.
/// </summary>
public partial class ColorPickerDialog : Window
{
    private double _hue;        // 0..360
    private double _saturation; // 0..1
    private double _value;      // 0..1
    private bool _draggingSv;
    private bool _draggingHue;

    public string SelectedHex { get; private set; }

    public ColorPickerDialog(string initialHex, string? title = null)
    {
        InitializeComponent();
        if (!string.IsNullOrWhiteSpace(title)) TitleText.Text = title;

        var color = ParseHex(initialHex) ?? Colors.White;
        SelectedHex = FormatHex(color);
        OldSwatch.Background = new SolidColorBrush(color);
        (_hue, _saturation, _value) = RgbToHsv(color);
        Loaded += (_, _) => UpdateFromHsv();
    }

    private void UpdateFromHsv()
    {
        var color = HsvToRgb(_hue, _saturation, _value);
        SelectedHex = FormatHex(color);

        HueStop.Color = HsvToRgb(_hue, 1, 1);
        NewSwatch.Background = new SolidColorBrush(color);
        HexBox.Text = SelectedHex;

        Canvas.SetLeft(SvThumb, _saturation * SvSurface.ActualWidth - SvThumb.Width / 2);
        Canvas.SetTop(SvThumb, (1 - _value) * SvSurface.ActualHeight - SvThumb.Height / 2);
        Canvas.SetLeft(HueThumb, (HueSurface.ActualWidth - HueThumb.Width) / 2);
        Canvas.SetTop(HueThumb, _hue / 360.0 * HueSurface.ActualHeight - HueThumb.Height / 2);
    }

    private void ApplySvFromPoint(Point p)
    {
        _saturation = Math.Clamp(p.X / SvSurface.ActualWidth, 0, 1);
        _value = Math.Clamp(1 - p.Y / SvSurface.ActualHeight, 0, 1);
        UpdateFromHsv();
    }

    private void ApplyHueFromPoint(Point p)
    {
        _hue = Math.Clamp(p.Y / HueSurface.ActualHeight, 0, 1) * 360.0;
        UpdateFromHsv();
    }

    private void SvSurface_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _draggingSv = true;
        SvSurface.CaptureMouse();
        ApplySvFromPoint(e.GetPosition(SvSurface));
    }

    private void SvSurface_MouseMove(object sender, MouseEventArgs e)
    {
        if (_draggingSv) ApplySvFromPoint(e.GetPosition(SvSurface));
    }

    private void HueSurface_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _draggingHue = true;
        HueSurface.CaptureMouse();
        ApplyHueFromPoint(e.GetPosition(HueSurface));
    }

    private void HueSurface_MouseMove(object sender, MouseEventArgs e)
    {
        if (_draggingHue) ApplyHueFromPoint(e.GetPosition(HueSurface));
    }

    private void Surface_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        _draggingSv = false;
        _draggingHue = false;
        SvSurface.ReleaseMouseCapture();
        HueSurface.ReleaseMouseCapture();
    }

    private void HexBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) CommitHexBox();
    }

    private void HexBox_LostFocus(object sender, RoutedEventArgs e) => CommitHexBox();

    private void CommitHexBox()
    {
        var color = ParseHex(HexBox.Text);
        if (color is null)
        {
            HexBox.Text = SelectedHex; // revert invalid input
            return;
        }
        (_hue, _saturation, _value) = RgbToHsv(color.Value);
        UpdateFromHsv();
    }

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e) => DragMove();

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        CommitHexBox();
        DialogResult = true;
    }

    private void Cancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
    }

    private static string FormatHex(Color c) => $"#{c.R:X2}{c.G:X2}{c.B:X2}";

    private static Color? ParseHex(string? text)
    {
        var t = (text ?? string.Empty).Trim().TrimStart('#');
        if (t.Length != 6 || !t.All(Uri.IsHexDigit)) return null;
        return Color.FromRgb(
            Convert.ToByte(t[..2], 16),
            Convert.ToByte(t[2..4], 16),
            Convert.ToByte(t[4..6], 16));
    }

    private static (double H, double S, double V) RgbToHsv(Color c)
    {
        double r = c.R / 255.0, g = c.G / 255.0, b = c.B / 255.0;
        double max = Math.Max(r, Math.Max(g, b));
        double min = Math.Min(r, Math.Min(g, b));
        double delta = max - min;

        double h = 0;
        if (delta > 0)
        {
            if (max == r) h = 60 * (((g - b) / delta) % 6);
            else if (max == g) h = 60 * ((b - r) / delta + 2);
            else h = 60 * ((r - g) / delta + 4);
        }
        if (h < 0) h += 360;

        double s = max <= 0 ? 0 : delta / max;
        return (h, s, max);
    }

    private static Color HsvToRgb(double h, double s, double v)
    {
        double c = v * s;
        double x = c * (1 - Math.Abs(h / 60.0 % 2 - 1));
        double m = v - c;
        var (r, g, b) = ((int)(h / 60.0) % 6) switch
        {
            0 => (c, x, 0.0),
            1 => (x, c, 0.0),
            2 => (0.0, c, x),
            3 => (0.0, x, c),
            4 => (x, 0.0, c),
            _ => (c, 0.0, x),
        };
        return Color.FromRgb(
            (byte)Math.Round((r + m) * 255),
            (byte)Math.Round((g + m) * 255),
            (byte)Math.Round((b + m) * 255));
    }
}
