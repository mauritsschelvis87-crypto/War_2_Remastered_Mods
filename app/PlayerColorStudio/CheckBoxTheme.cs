using System.Windows;

namespace PlayerColorStudio;

public static class CheckBoxTheme
{
    public static readonly DependencyProperty IsColorBlindModeProperty =
        DependencyProperty.RegisterAttached(
            "IsColorBlindMode",
            typeof(bool),
            typeof(CheckBoxTheme),
            new FrameworkPropertyMetadata(false, FrameworkPropertyMetadataOptions.Inherits));

    public static void SetIsColorBlindMode(DependencyObject element, bool value) =>
        element.SetValue(IsColorBlindModeProperty, value);

    public static bool GetIsColorBlindMode(DependencyObject element) =>
        (bool)element.GetValue(IsColorBlindModeProperty);
}
