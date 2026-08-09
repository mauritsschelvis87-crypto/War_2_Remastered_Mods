using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace PlayerColorStudio;

public partial class App : System.Windows.Application
{
    public App()
    {
        Resources.Add("BooleanToOpacityConverter", new BooleanToOpacityConverter());
    }
}

public sealed class BooleanToOpacityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        value is true ? 1.0 : 0.55;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        System.Windows.Data.Binding.DoNothing;
}
