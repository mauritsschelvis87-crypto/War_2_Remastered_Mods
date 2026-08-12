using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
// The project also references WinForms — pin the WPF types explicitly.
using Color = System.Windows.Media.Color;
using ColorConverter = System.Windows.Media.ColorConverter;

namespace PlayerColorStudio;

public enum StudioDialogKind
{
    Info,
    Warning,
    Error,
    Question,
}

/// <summary>
/// App-styled replacement for MessageBox so every popup shares the same look.
/// </summary>
public partial class StudioDialog : Window
{
    private StudioDialog(string message, string title, StudioDialogKind kind, bool confirm)
    {
        InitializeComponent();
        TitleText.Text = title;
        Title = title;
        MessageText.Text = message;
        ApplyKind(kind);
        if (confirm)
        {
            OkButton.Content = "Yes";
            NoButton.Visibility = Visibility.Visible;
        }
    }

    private void ApplyKind(StudioDialogKind kind)
    {
        var (glyph, hex) = kind switch
        {
            StudioDialogKind.Warning => ("!", "#FFE8B34B"),
            StudioDialogKind.Error => ("✕", "#FFE05B5B"),
            StudioDialogKind.Question => ("?", "#FF4E9DFF"),
            _ => ("i", "#FF4E9DFF"),
        };
        var color = (Color)ColorConverter.ConvertFromString(hex);
        IconText.Text = glyph;
        IconText.Foreground = new SolidColorBrush(color);
        IconBadge.Background = new SolidColorBrush(Color.FromArgb(0x33, color.R, color.G, color.B));
    }

    private static StudioDialog Create(Window? owner, string message, string title,
        StudioDialogKind kind, bool confirm)
    {
        var dialog = new StudioDialog(message, title, kind, confirm);
        // Owner may not be shown yet (startup errors) — fall back to screen center.
        if (owner is { IsLoaded: true, IsVisible: true })
        {
            dialog.Owner = owner;
        }
        else
        {
            dialog.WindowStartupLocation = WindowStartupLocation.CenterScreen;
        }
        return dialog;
    }

    public static void Show(Window? owner, string message, string title,
        StudioDialogKind kind = StudioDialogKind.Info)
    {
        Create(owner, message, title, kind, confirm: false).ShowDialog();
    }

    public static bool Confirm(Window? owner, string message, string title)
    {
        return Create(owner, message, title, StudioDialogKind.Question, confirm: true)
            .ShowDialog() == true;
    }

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e) => DragMove();

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = true;
    }

    private void No_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
    }
}
