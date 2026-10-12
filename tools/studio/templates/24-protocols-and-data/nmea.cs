// NMEA 0183: "$...*hh" - XOR of the characters between $ and *
bool Valid(string s)
{
    int star = s.IndexOf('*');
    if (!s.StartsWith("$") || star < 0) return false;
    int x = 0;
    for (int i = 1; i < star; i++) x ^= s[i];
    return x == Convert.ToInt32(s.Substring(star + 1, 2), 16);
}
double Degrees(string v, string hemi)                 // ddmm.mmmm -> decimal degrees
{
    double raw = double.Parse(v);
    double deg = Math.Floor(raw / 100) + (raw % 100) / 60;
    return hemi == "S" || hemi == "W" ? -deg : deg;
}

string line = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
Console.WriteLine("checksum ok: " + Valid(line));
string[] f = line.Split('*')[0].Split(',');
Console.WriteLine($"time {f[1]}  fix {(f[2] == "A" ? "valid" : "none")}  lat {Degrees(f[3], f[4]):F5}  lon {Degrees(f[5], f[6]):F5}  speed {double.Parse(f[7]) * 1.852:F1} km/h");
