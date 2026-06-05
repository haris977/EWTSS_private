using System.Windows.Controls;
using WpfUserControl = System.Windows.Controls.UserControl;
using Sg.Mvp4.Domain.ViewModels;

namespace Sg.Mvp4.App.Views.Entities;

/// <summary>
/// Property panel for <see cref="CoverageDefinitionViewModel"/>.
/// No "Edit on Map" button — coverage bounds are configured via fields only.
/// <para>
/// Asset paths are shown in two ways:
/// <list type="number">
///   <item>A read-only ListBox of all available scenario assets (Aircraft, Facility, Sensor)
///         so users can see what is available.</item>
///   <item>A multi-line TextBox where the user types or pastes the exact paths to include.
///         On every text change the code-behind syncs <c>AssetPaths</c> on the VM.</item>
/// </list>
/// </para>
/// </summary>
public partial class CoveragePanel : WpfUserControl
{
    private bool _syncingAssets;

    public CoveragePanel()
    {
        InitializeComponent();

        // When DataContext is set (or changes), populate the text box from VM.
        DataContextChanged += (_, _) => SyncTextBoxFromVm();
    }

    private CoverageDefinitionViewModel? Vm => DataContext as CoverageDefinitionViewModel;

    // -----------------------------------------------------------------------
    // Sync AssetPaths TextBox ↔ VM.AssetPaths
    // -----------------------------------------------------------------------

    private void SyncTextBoxFromVm()
    {
        if (Vm is null) return;

        _syncingAssets = true;
        AssetPathsBox.Text = string.Join(Environment.NewLine, Vm.AssetPaths);
        _syncingAssets = false;

        // Keep the text box in sync when the collection changes from outside (Reset).
        Vm.AssetPaths.CollectionChanged += (_, _) =>
        {
            if (_syncingAssets) return;
            _syncingAssets = true;
            AssetPathsBox.Text = string.Join(Environment.NewLine, Vm.AssetPaths);
            _syncingAssets = false;
        };
    }

    private void AssetPathsBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_syncingAssets || Vm is null) return;

        _syncingAssets = true;
        var paths = AssetPathsBox.Text
            .Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Select(p => p.TrimEnd('\r').Trim())
            .Where(p => p.Length > 0)
            .ToList();

        Vm.AssetPaths.Clear();
        foreach (var p in paths)
            Vm.AssetPaths.Add(p);
        _syncingAssets = false;
    }
}
