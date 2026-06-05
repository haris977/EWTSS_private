using System.IO;
using System.Windows;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Serilog;
using Sg.App.Models;
using Sg.App.Services;
using Sg.App.ViewModels;
using Application = System.Windows.Application;

namespace Sg.App;

public partial class App : Application
{
    private IHost? _host;
    private CancellationTokenSource _pollerCts = new();

    public IServiceProvider Services => _host?.Services
        ?? throw new InvalidOperationException("Host not started.");

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var logDir = @"C:\EWTSS\sg-app\logs";
        Directory.CreateDirectory(logDir);

        _host = Host.CreateDefaultBuilder()
            .ConfigureAppConfiguration(cfg =>
            {
                cfg.SetBasePath(AppContext.BaseDirectory);
                cfg.AddJsonFile("appsettings.json", optional: false, reloadOnChange: true);
                cfg.AddJsonFile("appsettings.Development.json", optional: true, reloadOnChange: true);
                cfg.AddEnvironmentVariables(prefix: "SGAPP_");
            })
            .ConfigureServices((ctx, services) => ConfigureServices(ctx.Configuration, services))
            .UseSerilog((ctx, _, lc) => lc
                .ReadFrom.Configuration(ctx.Configuration)
                .WriteTo.File(Path.Combine(logDir, "sg-app-.log"), rollingInterval: RollingInterval.Day)
                .WriteTo.Debug())
            .Build();

        _host.Start();
        Log.Information("Sg.App starting up.");

        var mainWindow = Services.GetRequiredService<MainWindow>();
        mainWindow.Show();

        var vm = Services.GetRequiredService<MainWindowViewModel>();
        var pollSeconds = _host!.Services.GetRequiredService<IConfiguration>()
            .GetValue<int?>("DrsServer:PollSeconds") ?? 10;
        // Wrap the polling task so synchronous pre-await exceptions surface to
        // Serilog instead of vanishing into the fire-and-forget void. The poll
        // loop itself already routes per-iteration exceptions to "sync_lost"
        // banner state; this guards only the construction path.
        _ = Task.Run(async () =>
        {
            try
            {
                await vm.StartPolling(TimeSpan.FromSeconds(pollSeconds), _pollerCts.Token);
            }
            catch (OperationCanceledException) when (_pollerCts.IsCancellationRequested)
            {
                // Expected during shutdown.
            }
            catch (Exception ex)
            {
                Log.Error(ex, "Time-sync poll loop terminated unexpectedly");
            }
        });
    }

    public static void ConfigureServices(IConfiguration config, IServiceCollection services)
    {
        services.AddSingleton(config);
        services.AddSingleton<IExerciseStateService, ExerciseStateService>();
        services.AddSingleton<SyncBannerService>();
        services.AddHttpClient<ITimeSyncClient, TimeSyncClient>((sp, http) =>
        {
            var cfg = sp.GetRequiredService<IConfiguration>();
            var url = cfg.GetValue<string>("DrsServer:Url")
                      ?? throw new InvalidOperationException("DrsServer:Url not configured.");
            http.BaseAddress = new Uri(url);
        });
        services.AddSingleton<MainWindowViewModel>();
        services.AddTransient<TimeSyncViewModel>();
        services.AddTransient<Views.Admin.TimeSyncView>();
        services.AddSingleton<MainWindow>();
    }

    protected override async void OnExit(ExitEventArgs e)
    {
        Log.Information("Sg.App shutting down.");
        _pollerCts.Cancel();
        if (_host is not null)
        {
            await _host.StopAsync(TimeSpan.FromSeconds(5));
            _host.Dispose();
        }
        _pollerCts.Dispose();
        Log.CloseAndFlush();
        base.OnExit(e);
    }
}
