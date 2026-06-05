using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using NUnit.Framework;
using Sg.Mvp4.Domain.Contracts;
using Sg.Mvp4.Domain.Stk;
using AppType = Sg.Mvp4.App.App;

namespace Sg.Mvp4.Tests.Contracts;

[TestFixture]
public class BackendModeTests
{
    [Test, Category(TestCategories.Unit)]
    public void InProcess_mode_registers_StkScenarioBackend()
    {
        var services = new ServiceCollection();
        AppType.RegisterBackend(services, "InProcess");

        // Inspect the registration metadata instead of resolving — resolving
        // would construct StkScenarioBackend and start STK Engine, which is
        // (a) not what this DI-wiring test is supposed to verify, and (b) costs
        // the integration suite a second AgSTKXApplication lifecycle in-process
        // that STK 12.9 cannot tolerate.
        services.Should().ContainSingle(d =>
            d.ServiceType == typeof(StkScenarioBackend) &&
            d.ImplementationType == typeof(StkScenarioBackend) &&
            d.Lifetime == ServiceLifetime.Singleton);

        services.Should().ContainSingle(d =>
            d.ServiceType == typeof(IScenarioBackend) &&
            d.Lifetime == ServiceLifetime.Singleton);
    }

    [Test, Category(TestCategories.Unit)]
    public void Remote_mode_throws()
    {
        var services = new ServiceCollection();
        var act = () => AppType.RegisterBackend(services, "Remote");
        act.Should().Throw<NotImplementedException>().WithMessage("*Remote*");
    }

    [Test, Category(TestCategories.Unit)]
    public void Unknown_mode_throws()
    {
        var services = new ServiceCollection();
        var act = () => AppType.RegisterBackend(services, "Bogus");
        act.Should().Throw<InvalidOperationException>().WithMessage("*backend mode*");
    }
}
