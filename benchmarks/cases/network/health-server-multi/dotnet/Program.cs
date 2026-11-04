using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using System;
using System.Threading.Tasks;

var builder = WebApplication.CreateBuilder(args);
var port = 8080;
if (args.Length > 0 && int.TryParse(args[0], out var p)) port = p;
builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
var app = builder.Build();
app.Use((Func<HttpContext, Func<Task>, Task>)((context, next) => Task.Run(async () => {
    if (!HttpMethods.IsGet(context.Request.Method)) { context.Response.StatusCode = 404; await context.Response.WriteAsync("not found"); return; }
    switch (context.Request.Path.Value) {
        case "/healthz": context.Response.ContentType = "text/plain"; await context.Response.WriteAsync("ok"); break;
        case "/": context.Response.ContentType = "text/plain"; await context.Response.WriteAsync("health-server ok"); break;
        default: context.Response.StatusCode = 404; context.Response.ContentType = "text/plain"; await context.Response.WriteAsync("not found"); break;
    }
})));
Console.WriteLine($"listening on http://127.0.0.1:{port}");
app.Run();
