using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace PlayerColorStudio;

public static class Loc
{
    public static readonly DependencyProperty TextKeyProperty =
        DependencyProperty.RegisterAttached(
            "TextKey",
            typeof(string),
            typeof(Loc),
            new PropertyMetadata(null, OnKeyChanged));

    public static readonly DependencyProperty HeaderKeyProperty =
        DependencyProperty.RegisterAttached(
            "HeaderKey",
            typeof(string),
            typeof(Loc),
            new PropertyMetadata(null, OnKeyChanged));

    public static string GetTextKey(DependencyObject d) => (string)d.GetValue(TextKeyProperty);
    public static void SetTextKey(DependencyObject d, string value) => d.SetValue(TextKeyProperty, value);

    public static string GetHeaderKey(DependencyObject d) => (string)d.GetValue(HeaderKeyProperty);
    public static void SetHeaderKey(DependencyObject d, string value) => d.SetValue(HeaderKeyProperty, value);

    private static void OnKeyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e) =>
        Apply(d);

    public static void Refresh(DependencyObject root)
    {
        if (root is null) return;
        Apply(root);
        if (root is Visual or System.Windows.Media.Media3D.Visual3D)
            WalkVisualTree(root);
    }

    private static void WalkVisualTree(DependencyObject parent)
    {
        var count = VisualTreeHelper.GetChildrenCount(parent);
        for (var i = 0; i < count; ++i)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            Apply(child);
            WalkVisualTree(child);
        }
    }

    private static void Apply(DependencyObject d)
    {
        var textKey = GetTextKey(d);
        if (!string.IsNullOrWhiteSpace(textKey))
        {
            var text = Localization.Get(textKey);
            switch (d)
            {
                case TextBlock tb:
                    tb.Text = text;
                    break;
                case System.Windows.Controls.Button btn:
                    btn.Content = text;
                    break;
                case System.Windows.Controls.CheckBox cb:
                    cb.Content = text;
                    break;
            }
        }

        var headerKey = GetHeaderKey(d);
        if (!string.IsNullOrWhiteSpace(headerKey))
        {
            var header = Localization.Get(headerKey);
            switch (d)
            {
                case TabItem tab:
                    tab.Header = header;
                    break;
                case GridViewColumn col:
                    col.Header = header;
                    break;
            }
        }
    }
}
