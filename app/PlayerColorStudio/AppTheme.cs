using System.Windows;
using System.Windows.Media;
using MediaColor = System.Windows.Media.Color;

namespace PlayerColorStudio;

public static class AppTheme
{
    // Border thickness stays fixed so toggling On/Off does not shift layout.
    public static readonly Thickness CardBorderThickness = new(2);
    public static readonly Thickness SwatchBorderThickness = new(2);
    public static readonly Thickness AccessibleSwatchBorderThickness = new(4);
    public static readonly Thickness ControlBorderThickness = new(2);

    private static readonly MediaColor WindowColor = MediaColor.FromRgb(0x1A, 0x27, 0x40);
    private static readonly MediaColor PanelColor = MediaColor.FromRgb(0x22, 0x33, 0x52);
    private static readonly MediaColor CardColor = MediaColor.FromRgb(0x2C, 0x3D, 0x5C);
    private static readonly MediaColor TextColor = MediaColor.FromRgb(0xFF, 0xFF, 0xFF);
    private static readonly MediaColor MutedTextColor = MediaColor.FromRgb(0xB8, 0xC4, 0xD8);
    private static readonly MediaColor CardBorderColor = MediaColor.FromRgb(0x4A, 0x63, 0x88);
    private static readonly MediaColor SwatchBorderColor = MediaColor.FromRgb(0x7A, 0x8E, 0xAD);
    private static readonly MediaColor ControlBorderColor = MediaColor.FromRgb(0x5A, 0x73, 0x98);
    private static readonly MediaColor InputBackgroundColor = MediaColor.FromRgb(0x16, 0x23, 0x38);
    private static readonly MediaColor TabIdleBackgroundColor = MediaColor.FromRgb(0x33, 0x48, 0x68);
    private static readonly MediaColor ButtonBackgroundColor = MediaColor.FromRgb(0x33, 0x48, 0x68);

    // Soft high-contrast palette: easier on the eyes than pure black on white.
    private static readonly MediaColor AccessibleSurfaceColor = MediaColor.FromRgb(0xF7, 0xF7, 0xF7);
    private static readonly MediaColor AccessibleCardColor = MediaColor.FromRgb(0xFF, 0xFF, 0xFF);
    private static readonly MediaColor AccessibleTextColor = MediaColor.FromRgb(0x1A, 0x1A, 0x1A);
    private static readonly MediaColor AccessibleMutedTextColor = MediaColor.FromRgb(0x52, 0x52, 0x52);
    private static readonly MediaColor AccessibleBorderColor = MediaColor.FromRgb(0x33, 0x33, 0x33);
    private static readonly MediaColor CheckBoxCheckedFillColor = MediaColor.FromRgb(0x4C, 0xC3, 0x7A);

    public static void ApplyColorBlindMode(bool enabled)
    {
        var resources = System.Windows.Application.Current.Resources;
        if (enabled)
        {
            SetBrush(resources, "WindowBrush", AccessibleSurfaceColor);
            SetBrush(resources, "PanelBrush", AccessibleSurfaceColor);
            SetBrush(resources, "CardBrush", AccessibleCardColor);
            SetBrush(resources, "TextBrush", AccessibleTextColor);
            SetBrush(resources, "MutedTextBrush", AccessibleMutedTextColor);
            SetBrush(resources, "CardBorderBrush", AccessibleBorderColor);
            SetBrush(resources, "SwatchBorderBrush", AccessibleBorderColor);
            SetBrush(resources, "ControlBorderBrush", AccessibleBorderColor);
            SetBrush(resources, "InputBackgroundBrush", AccessibleCardColor);
            SetBrush(resources, "TabIdleBackgroundBrush", AccessibleSurfaceColor);
            SetBrush(resources, "ButtonBackgroundBrush", AccessibleCardColor);
            SetBrush(resources, "CheckBoxCheckedFillBrush", CheckBoxCheckedFillColor);
            resources["SwatchBorderThickness"] = AccessibleSwatchBorderThickness;
        }
        else
        {
            SetBrush(resources, "WindowBrush", WindowColor);
            SetBrush(resources, "PanelBrush", PanelColor);
            SetBrush(resources, "CardBrush", CardColor);
            SetBrush(resources, "TextBrush", TextColor);
            SetBrush(resources, "MutedTextBrush", MutedTextColor);
            SetBrush(resources, "CardBorderBrush", CardBorderColor);
            SetBrush(resources, "SwatchBorderBrush", SwatchBorderColor);
            SetBrush(resources, "ControlBorderBrush", ControlBorderColor);
            SetBrush(resources, "InputBackgroundBrush", InputBackgroundColor);
            SetBrush(resources, "TabIdleBackgroundBrush", TabIdleBackgroundColor);
            SetBrush(resources, "ButtonBackgroundBrush", ButtonBackgroundColor);
            SetBrush(resources, "CheckBoxCheckedFillBrush", Colors.Transparent);
            resources["SwatchBorderThickness"] = SwatchBorderThickness;
        }
    }

    private static void SetBrush(ResourceDictionary resources, string key, MediaColor color)
    {
        if (resources[key] is SolidColorBrush existing && existing.IsFrozen)
        {
            resources[key] = Freeze(color);
            return;
        }

        if (resources[key] is SolidColorBrush brush)
        {
            brush.Color = color;
            return;
        }

        resources[key] = Freeze(color);
    }

    private static SolidColorBrush Freeze(MediaColor color)
    {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        return brush;
    }
}
