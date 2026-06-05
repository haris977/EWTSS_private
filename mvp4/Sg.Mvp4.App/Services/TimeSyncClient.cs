using System.Net.Http;
using System.Net.Http.Json;
using System.Threading;
using System.Threading.Tasks;
using Sg.Mvp4.Domain.Contracts;

namespace Sg.Mvp4.App.Services;

public interface ITimeSyncClient
{
    Task<TimeSyncStatusDto> GetStatusAsync(CancellationToken ct = default);
}

public sealed class TimeSyncClient : ITimeSyncClient
{
    private readonly HttpClient _http;

    public TimeSyncClient(HttpClient http)
    {
        _http = http;
    }

    public async Task<TimeSyncStatusDto> GetStatusAsync(CancellationToken ct = default)
    {
        var dto = await _http.GetFromJsonAsync<TimeSyncStatusDto>("/time/status", ct);
        if (dto is null)
            throw new System.InvalidOperationException("drs-server /time/status returned null body.");
        return dto;
    }
}
