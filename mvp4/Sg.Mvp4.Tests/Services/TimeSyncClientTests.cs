using NUnit.Framework;
using FluentAssertions;
using System.Net;
using System.Net.Http;
using System.Threading.Tasks;

namespace Sg.Mvp4.Tests.Services;

[TestFixture]
public class TimeSyncClientTests
{
    private class StubHandler : HttpMessageHandler
    {
        public string ResponseJson { get; set; } = "";
        public HttpStatusCode StatusCode { get; set; } = HttpStatusCode.OK;
        protected override Task<HttpResponseMessage> SendAsync(
            HttpRequestMessage request, System.Threading.CancellationToken ct)
            => Task.FromResult(new HttpResponseMessage(StatusCode)
            {
                Content = new StringContent(ResponseJson, System.Text.Encoding.UTF8, "application/json")
            });
    }

    [Test, Category(TestCategories.Unit)]
    public async Task GetStatus_returns_parsed_dto_on_healthy_response()
    {
        var stub = new StubHandler
        {
            ResponseJson = """
                {
                  "current_time": "2026-05-14T12:34:56.789Z",
                  "ntp_offset_ms": 0.4,
                  "ntp_jitter_ms": 0.2,
                  "ntp_peer": "WS1-SG.local",
                  "last_sync": "2026-05-14T12:34:50.000Z",
                  "status": "healthy"
                }
                """
        };
        var http = new HttpClient(stub) { BaseAddress = new System.Uri("http://ws2.local:8000/") };
        var client = new Sg.Mvp4.App.Services.TimeSyncClient(http);

        var dto = await client.GetStatusAsync();

        dto.Status.Should().Be("healthy");
        dto.NtpOffsetMs.Should().BeApproximately(0.4, 0.001);
        dto.NtpJitterMs.Should().BeApproximately(0.2, 0.001);
        dto.NtpPeer.Should().Be("WS1-SG.local");
    }
}
