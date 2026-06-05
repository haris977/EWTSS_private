using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using Sg.App.Services;

namespace Sg.App.Converters;

public sealed class BannerLevelToVisibilityConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is BannerLevel.None or null
            ? Visibility.Collapsed
            : Visibility.Visible;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotSupportedException();
}
