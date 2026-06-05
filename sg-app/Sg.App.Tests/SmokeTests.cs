using FluentAssertions;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Sg.App;
using Sg.App.Models;
using Sg.App.Services;
using Sg.App.ViewModels;

namespace Sg.App.Tests;

[TestFixture]
public class SmokeTests
{
    [Test, Category(TestCategories.Unit)]
    public void Composition_root_resolves_all_singletons()
    {
        var config = new ConfigurationBuilder()
            .AddInMemoryCollection(new Dictionary<string, string?>
            {
                ["DrsServer:Url"] = "http://localhost:8000/",
            })
            .Build();

        var services = new ServiceCollection();
        App.ConfigureServices(config, services);
        var provider = services.BuildServiceProvider();

        provider.GetRequiredService<IExerciseStateService>().Should().NotBeNull();
        provider.GetRequiredService<SyncBannerService>().Should().NotBeNull();
        provider.GetRequiredService<ITimeSyncClient>().Should().NotBeNull();
        provider.GetRequiredService<MainWindowViewModel>().Should().NotBeNull();
    }

    [Test, Category(TestCategories.Unit)]
    public void ExerciseStateService_initial_state_is_stopped()
    {
        var svc = new ExerciseStateService();
        svc.CurrentState.Should().Be(ExerciseState.Stopped);
    }

    [Test, Category(TestCategories.Unit)]
    public void ExerciseStateService_raises_StateChanged_on_transition()
    {
        var svc = new ExerciseStateService();
        ExerciseState? observed = null;
        svc.StateChanged += (_, s) => observed = s;

        svc.Transition(ExerciseState.Armed);

        observed.Should().Be(ExerciseState.Armed);
        svc.CurrentState.Should().Be(ExerciseState.Armed);
    }

    [Test, Category(TestCategories.Unit)]
    public void ExerciseStateService_does_not_raise_for_same_state()
    {
        var svc = new ExerciseStateService();
        var raisedCount = 0;
        svc.StateChanged += (_, _) => raisedCount++;

        svc.Transition(ExerciseState.Stopped);  // already Stopped — no-op

        raisedCount.Should().Be(0);
    }
}
