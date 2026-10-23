// Fill in your endpoint and connect Wi-Fi first (or use an existing connection).
// Empty endpoint keeps this template safe to run without contacting a service.
if (!Hal.Has("Http.Json")) {
    Console.WriteLine("This firmware lacks the JSON HTTP helpers.");
    return;
}
string endpoint = "";
if (string.IsNullOrEmpty(endpoint)) {
    Console.WriteLine("Set endpoint to your JSON API URL and connect your network first.");
    return;
}
Http.Timeout = 15000;
var headers = new Dictionary<string, string> { ["Accept"] = "application/json" };
try {
    var current = Http.GetJson(endpoint, headers);
    Console.WriteLine(Json.Serialize(current, true));
    var update = new Dictionary<string, object> { ["enabled"] = true };
    var response = Http.PostJson(endpoint, update, headers);
    response.EnsureSuccess();
    if (response.ContentLength > 0) Console.WriteLine(Json.Serialize(response.Json));
}
catch (FormatException e) { Console.WriteLine("Not JSON: " + e.Message); }
catch (IOException e) { Console.WriteLine("HTTP/network error: " + e.Message); }
catch (TimeoutException) { Console.WriteLine("Timed out."); }
