using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using Sg.App.Services;

namespace Sg.App.Converters;

public sealed class BannerLevelToBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value switch
        {
            BannerLevel.Warn  => Brushes.Goldenrod,
            BannerLevel.Alert => Brushes.OrangeRed,
            BannerLevel.Lost  => Brushes.DarkRed,
            _                 => Brushes.Transparent,
        };

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotSupportedException();
}
