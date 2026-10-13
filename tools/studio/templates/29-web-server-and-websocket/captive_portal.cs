// Captive portal: phones show "Sign in to network" and open the form by themselves
if (!Hal.Has("StringBuilder") || !Hal.Has("Wifi") || !Hal.Has("Wifi.Scan") || !Hal.Has("Wifi.AP") || !Hal.Has("Wifi.Save") || !Hal.Has("HttpServer") || !Hal.Has("DnsServer"))
{
    Console.WriteLine("This firmware lacks: StringBuilder, Wifi, Wifi.Scan, Wifi.AP, Wifi.Save, HttpServer, DnsServer. Enable the needed modules or use a supported board.");
    return;
}
if (Wifi.SavedSsid != "" && Wifi.Connect()) { Console.WriteLine("online as " + Wifi.Ip); return; }

Wifi.StartAp("MicroCS-Setup", "");              // open hotspot, the board is 192.168.4.1
using var dns = new DnsServer(Wifi.ApIp);             // every name -> the board
dns.Start();
using var web = new HttpServer(80);

string Options()
{
    var sb = new StringBuilder();
    foreach (var n in Wifi.Scan()) if (n.Ssid != "") sb.Append($"<option>{n.Ssid}</option>");
    return sb.ToString();
}
web.Get("/", req => $@"<!doctype html><meta name=viewport content='width=device-width'>
<h2>Wi-Fi setup</h2><form method=post action=/save>
<select name=ssid>{Options()}</select><br><input name=pass type=password placeholder=password><br>
<button>Connect</button></form>");
bool done = false;
web.Post("/save", req =>
{
    Wifi.Save(req.Param("ssid"), req.Param("pass", ""));
    done = true;
    return "<h2>Saved - the board joins your network now.</h2>";
});
web.NotFound(req => req.Redirect("http://" + Wifi.ApIp + "/"));   // phones probe /generate_204 ...
web.Start();
Console.WriteLine($"join 'MicroCS-Setup' and open http://{Wifi.ApIp}/");

// Wait for setup to complete or Studio Stop / Ctrl-C.
while (!done) web.Poll(100);   // also answers the DnsServer
web.Stop(); dns.Stop();
if (done) { Wifi.StopAp(); Console.WriteLine(Wifi.Connect() ? "online as " + Wifi.Ip : "could not join"); }
