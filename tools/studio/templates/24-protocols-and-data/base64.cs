// Base64 and hex helpers (no Convert.ToBase64String in MicroCS - here is one)
const string B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string ToBase64(byte[] d)
{
    var sb = new StringBuilder();
    for (int i = 0; i < d.Length; i += 3)
    {
        int n = d[i] << 16 | (i + 1 < d.Length ? d[i + 1] << 8 : 0) | (i + 2 < d.Length ? d[i + 2] : 0);
        sb.Append(B64[(n >> 18) & 63]).Append(B64[(n >> 12) & 63]);
        sb.Append(i + 1 < d.Length ? B64[(n >> 6) & 63] : '=');
        sb.Append(i + 2 < d.Length ? B64[n & 63] : '=');
    }
    return sb.ToString();
}
byte[] FromBase64(string s)
{
    var outp = new List<byte>(); int buf = 0, bits = 0;
    foreach (char c in s)
    {
        if (c == '=') break;
        buf = (buf << 6) | B64.IndexOf(c); bits += 6;
        if (bits >= 8) { bits -= 8; outp.Add((byte)((buf >> bits) & 0xFF)); }
    }
    return outp.ToArray();
}
string ToHex(byte[] d) => string.Concat(d.Select(b => b.ToString("x2")));

byte[] data = Encoding.UTF8.GetBytes("MicroCS!");
string b64 = ToBase64(data);
Console.WriteLine($"{b64}  ->  {Encoding.UTF8.GetString(FromBase64(b64))}");
Console.WriteLine("hex: " + ToHex(data) + "   BitConverter: " + BitConverter.ToString(data));
