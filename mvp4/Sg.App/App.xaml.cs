using System.IO;
using System.Windows;
using Microsoft.Extensions.DependencyInjection;
using Serilog;
using Sg.App.Services;
using Sg.App.Views.Shell;
using Sg.Domain.Contracts;
using Sg.Domain.Interaction;
using Sg.Domain.Services;
using Sg.Domain.Stk;
using Sg.Domain.ViewModels;
using Application = System.Windows.Application;

namespace Sg.App;

public partial class App : Application
{
    public IServiceProvider Services { get; private set; } = null!;
    private SplashWindow? _splash;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // Required for WinForms-hosted ActiveX controls — every STK sample's Main() does this.
        System.Windows.Forms.Application.EnableVisualStyles();
        System.Windows.Forms.Application.SetCompatibleTextRenderingDefault(false);

        // Serilog: rolling daily file at C:\EWTSS\mvp4\logs + Debug sink.
        var logDir = @"C:\EWTSS\mvp4\logs";
        Directory.CreateDirectory(logDir);
        Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .WriteTo.File(Path.Combine(logDir, "mvp4-.log"), rollingInterval: RollingInterval.Day)
            .WriteTo.Debug()
            .CreateLogger();
        Log.Information("MVP4 starting up.");

        _splash = new SplashWindow();
        _splash.Show();

        try
        {
            _splash.SetStatus("Constructing STK COM root...");

            _splash.SetStatus("Building services...");
            var services = new ServiceCollection();
            // Backend mode (InProcess today; "Remote" reserved for future server impl).
            // Set MVP4_BACKEND=Remote to force the future remote-backend path; defaults to InProcess.
            // RegisterBackend also registers the concrete StkScenarioBackend so StkDisplayHost
            // can reach AgStkObjectRoot for SceneManager access.
            var backendMode = Environment.GetEnvironmentVariable("MVP4_BACKEND") ?? "InProcess";
            RegisterBackend(services, backendMode);
            services.AddSingleton<IInteractionController, InteractionController>();
            services.AddSingleton<IFileDialogService, FileDialogService>();
            services.AddSingleton<MainWindowViewModel>();
            services.AddSingleton<StkDisplayHost>();
            services.AddSingleton<ObjectTreeViewModel>();
            services.AddSingleton<ObjectTreeView>();
            services.AddSingleton<PropertyPanelHostViewModel>();
            services.AddSingleton<PropertyPanelHostView>();
            services.AddSingleton<MainWindow>();
            Services = services.BuildServiceProvider();

            _splash.SetStatus("Creating initial scenario...");
            var stk = Services.GetRequiredService<IScenarioBackend>();
            stk.NewScenario("Untitled", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));

            _splash.SetStatus("Opening main window...");
            Services.GetRequiredService<MainWindow>().Show();

            Log.Information("MVP4 main window shown.");
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Startup failed");
            System.Windows.MessageBox.Show(
                $"MVP4 failed to start:\n\n{ex.Message}\n\n" +
                "Check that STK 12 is installed and COM-registered. See the log at " +
                @"C:\EWTSS\mvp4\logs\ for details.",
                "Startup error",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(1);
            return;
        }
        finally
        {
            _splash?.Close();
            _splash = null;
        }
    }

    public static void RegisterBackend(IServiceCollection services, string mode)
    {
        if (mode == "InProcess")
        {
            services.AddSingleton<StkScenarioBackend>();
            services.AddSingleton<IScenarioBackend>(sp => sp.GetRequiredService<StkScenarioBackend>());
        }
        else if (mode == "Remote")
            throw new NotImplementedException("Remote backend lands when the ASP.NET server is added (deferred).");
        else
            throw new InvalidOperationException($"Unknown backend mode: '{mode}'.");
    }

    protected override void OnExit(ExitEventArgs e)
    {
        Log.Information("MVP4 shutting down.");

        // Best-effort COM teardown.
        try
        {
            Services?.GetService<StkScenarioBackend>()?.Dispose();
        }
        catch (Exception ex)
        {
            Log.Warning(ex, "STK dispose failed during shutdown.");
        }

        // 5-second watchdog: STK COM teardown can hang waiting on the engine
        // process. If we exceed the timeout, force-exit so the user isn't left
        // with a zombie window.
        var watchdog = new System.Threading.Thread(() =>
        {
            System.Threading.Thread.Sleep(5000);
            Log.Warning("Graceful shutdown exceeded 5s — forcing Environment.Exit.");
            Environment.Exit(0);
        }) { IsBackground = true };
        watchdog.Start();

        Log.CloseAndFlush();
        base.OnExit(e);
    }
}
