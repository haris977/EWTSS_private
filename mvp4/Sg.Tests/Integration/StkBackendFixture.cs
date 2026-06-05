using NUnit.Framework;
using Sg.Domain.Stk;
using System.Threading;

namespace Sg.Tests.Integration;

/// <summary>
/// STK Engine 12.9 only tolerates one AgSTKXApplication lifecycle per process;
/// init → dispose → init crashes the native COM server. This fixture creates a
/// single shared StkScenarioBackend for the whole integration suite. Tests in
/// this namespace use <see cref="Shared"/> and call CloseScenario() in their
/// own [SetUp] for isolation instead of constructing fresh backends.
/// </summary>
[SetUpFixture]
[Apartment(ApartmentState.STA)]
public class StkBackendFixture
{
    private static StkScenarioBackend? _shared;

    public static StkScenarioBackend Shared => _shared
        ?? throw new InvalidOperationException(
            "StkBackendFixture.Shared accessed before [OneTimeSetUp]. " +
            "Tests using it must live in the Sg.Tests.Integration namespace.");

    [OneTimeSetUp]
    public void Init()
    {
        // Mirror App.OnStartup's STK-X-control prelude (every STK 12 sample's
        // Main() does the same).
        System.Windows.Forms.Application.EnableVisualStyles();
        System.Windows.Forms.Application.SetCompatibleTextRenderingDefault(false);

        _shared = new StkScenarioBackend();
    }

    [OneTimeTearDown]
    public void Cleanup()
    {
        _shared?.Dispose();
        _shared = null;
    }
}
