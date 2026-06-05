using System.Windows;
using System.Windows.Input;
using Sg.App.Views.Shell;
using Sg.Domain.Interaction;
using Sg.Domain.Models;
using Sg.Domain.ViewModels;

namespace Sg.App;

public partial class MainWindow : Window
{
    private readonly IInteractionController _controller;

    public MainWindow(MainWindowViewModel vm,
                      StkDisplayHost stkHost,
                      ObjectTreeView tree,
                      PropertyPanelHostView panel,
                      IInteractionController controller)
    {
        InitializeComponent();
        _controller = controller;
        DataContext = vm;
        StkHostSlot.Content = stkHost;
        TreeSlot.Content    = tree;
        PanelSlot.Content   = panel;
    }

    private void ExitMenu_Click(object sender, RoutedEventArgs e) => Close();

    // Window-level PreviewKeyDown so the keys reach us even when focus is in
    // the WindowsFormsHost (STK ActiveX). STK doesn't forward Esc/Enter to us.
    protected override void OnPreviewKeyDown(System.Windows.Input.KeyEventArgs e)
    {
        base.OnPreviewKeyDown(e);
        if (_controller.Mode == InteractionMode.Idle) return;

        if (e.Key == Key.Escape)
        {
            // Esc: cancel placement OR cancel an in-progress drag-edit.
            _controller.Cancel();
            e.Handled = true;
        }
        else if (e.Key == Key.Enter || e.Key == Key.Return)
        {
            // Enter: finalise placement (aircraft/area-target) OR commit edit.
            // Right-click is unreliable here: STK consumes right-click on the
            // 3D globe for camera operations and never raises MouseDownEvent
            // with btn=2, so a button-2 finalize path was unreachable.
            switch (_controller.Mode)
            {
                case InteractionMode.PlacingAircraft:
                case InteractionMode.PlacingAreaTarget:
                    _controller.FinalizePlacement();
                    e.Handled = true;
                    break;
                case InteractionMode.EditingEntity:
                    _controller.ApplyEdit();
                    e.Handled = true;
                    break;
            }
        }
    }
}
