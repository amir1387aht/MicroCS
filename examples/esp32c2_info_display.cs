// MicroCS ESP32-C2 info display app.
// Setting edits update the display immediately and persist when edit mode ends.
// Compile to app.mcsb on the host for a small VM pool; source compilation needs more RAM.
namespace App.MainApp
{
public class MainApp
{
private U8g2 oled;
private RotaryEncoder encoder;
private int current_screen_index = 0;
private int current_mainmenu_choose = 0;
private int current_setting_choose = 0;
private int current_file_choosed = 0;
private bool is_in_settting_edit_mode = false;
private bool isShuttingDown = false;
private const int BUTTON_ENTER_PIN = 8;
private const int WAKEUP_HOLD_MS = 3000;
private const int ButtonClickDuration = 300;
private const string PROJECT_VER = "V1.0";
private const string FILES_PATH = "files/text/";
private const string SETTING_PATH = "setting.json";
private int ButtonLastClick = -300;
private int last = 0;
private bool wasPressed = false;
private int stableButton = 0;
private List<string> FoundedFiles = new List<string>();
private List<SettingData> AppSettingData = new List<SettingData>()
{
new SettingData()
{
name = "Contrast",
m_value = 4,
options = new List<string>()
{
"10", "30", "50", "70", "100"
}
},
new SettingData()
{
name = "Reset Board",
m_value = 0,
options = new List<string>()
},
new SettingData()
{
name = "Shutdown",
m_value = 0,
options = new List<string>()
},
new SettingData()
{
name = $"{PROJECT_VER} - By Another.",
m_value = 0,
options = new List<string>()
}
};
public void Start()
{
Power.ClearWakePin();
LoadSettings();
oled = new U8g2("u8g2_Setup_ssd1306_i2c_96x16_er_f");
oled.Begin();
oled.SetFont("u8g2_font_ncenB08_te");
oled.ClearBuffer();
oled.SendBuffer();
// Apply values only after the display exists. Never run action entries.
foreach (SettingData data in AppSettingData)
{
if (data.options.Count != 0)
OnSettingDataChanged(data);
}
StartInput();
DrawCentered("Welcome!");
oled.SendBuffer();
// BLE startup remains commented out, as in the supplied app.
Thread.Sleep(1500);
ShowScreen(0);
}
private void StartInput()
{
encoder = new RotaryEncoder(18, 7, 2, BUTTON_ENTER_PIN, true);
last = encoder.Position;
wasPressed = encoder.ButtonPressed;
stableButton = 0;
ButtonLastClick = Environment.TickCount - ButtonClickDuration;
Timer.Start(0, 30_000, ReadEncoderRotary);
}
public int CenterWidth(string text)
{
return (oled.Width - oled.GetStrWidth(text)) / 2;
}
public int CenterHeight()
{
int ascent = oled.Ascent;
int descent = oled.Descent;
return (oled.Height - ascent - descent) / 2 + ascent;
}
public void DrawCentered(string text)
{
oled.DrawStr(CenterWidth(text), CenterHeight(), text);
}
private void LoadSettings()
{
if (!File.Exists(SETTING_PATH))
{
SaveSetting();
return;
}
try
{
object savedData;
if (!Json.TryParse(File.ReadAllText(SETTING_PATH), out savedData)
|| !(savedData is List<object>))
{
Console.WriteLine("Invalid setting.json: using defaults; existing file left unchanged.");
return;
}
foreach (var saved in savedData)
{
if (!(saved is Dictionary<string, int>))
continue;
if (!saved.ContainsKey("name") || !saved.ContainsKey("m_value"))
continue;
if (!(saved["name"] is string))
continue;
for (int i = 0; i < AppSettingData.Count; i++)
{
var setting = AppSettingData[i];
if (setting.name != (string)saved["name"] || setting.options.Count == 0)
continue;
// Restore just the value. Keep typed models and built-in options.
try
{
int value = Convert.ToInt32(saved["m_value"]);
setting.m_value = Math.Clamp(value, 0, setting.options.Count - 1);
AppSettingData[i] = setting;
}
catch (Exception e)
{
Console.WriteLine("Ignored invalid setting value: " + setting.name);
}
break;
}
}
}
catch (Exception e)
{
Console.WriteLine("Settings load failed: " + e.Message);
}
}
private void SaveSetting()
{
// Preserve the existing array format; do not persist action entries.
var saved = new List<Dictionary<string, int>>();
foreach (SettingData data in AppSettingData)
{
if (data.options.Count != 0)
saved.Add(new Dictionary<string, int>
{
["name"] = data.name, ["m_value"] = data.m_value
});
}
File.WriteAllText(SETTING_PATH, JsonSerializer.Serialize(saved));
}
private void ShowScreen(int index)
{
current_screen_index = index;
if (index != 2)
is_in_settting_edit_mode = false;
switch (index)
{
case 0: ShowMenuScreen();
break;
case 1: ShowLoadScreen();
break;
case 2: ShowSettingScreen();
break;
case 3: ShowTextPage();
break;
default: break;
}
}
private void LoadTextFiles()
{
FoundedFiles.Clear();
if (!Directory.Exists(FILES_PATH))
return;
foreach (var file in Directory.GetFiles(FILES_PATH))
{
if (File.GetLength(file) > 0)
FoundedFiles.Add(Path.GetFileName(file));
}
}
private void ShowTextPage()
{
oled.ClearBuffer();
oled.SendBuffer();
}
private void ShowLoadScreen()
{
oled.ClearBuffer();
LoadTextFiles();
if (FoundedFiles.Count == 0)
{
current_file_choosed = 0;
DrawCentered("No Files Found!");
oled.SendBuffer();
return;
}
current_file_choosed = Math.Clamp(current_file_choosed, 0, FoundedFiles.Count - 1);
string filename = FoundedFiles[current_file_choosed];
if (!string.IsNullOrEmpty(filename))
{
DrawCentered(filename);
oled.DrawStr(0, CenterHeight(), "<");
oled.DrawStr(oled.Width - 6, CenterHeight(), ">");
}
oled.SendBuffer();
}
private void ShowSettingScreen()
{
oled.ClearBuffer();
var currentSetting = AppSettingData[current_setting_choose];
if (string.IsNullOrEmpty(currentSetting.name))
return;
if (currentSetting.options.Count != 0)
{
oled.DrawStr(0, CenterHeight(), currentSetting.name);
string setting_value = currentSetting.options[currentSetting.m_value];
int valueWidth = oled.GetStrWidth(setting_value);
oled.DrawStr(oled.Width - valueWidth - 5, CenterHeight(), setting_value);
if (is_in_settting_edit_mode)
oled.DrawFrame(oled.Width - valueWidth - 8,
CenterHeight() - 10, valueWidth + 6, CenterHeight() - 1);
}
else
DrawCentered(currentSetting.name);
oled.SendBuffer();
}
private void ShowMenuScreen()
{
oled.ClearBuffer();
string settingString = "Setting";
int settingX = oled.Width - oled.GetStrWidth(settingString) - 5;
if (current_mainmenu_choose == 0)
oled.DrawFrame(0, CenterHeight() - 11, 35, 14);
else
oled.DrawFrame(settingX - 5, CenterHeight() - 11, 48, 14);
oled.DrawStr(5, CenterHeight(), "Load");
oled.DrawStr(settingX, CenterHeight(), settingString);
oled.SendBuffer();
}
private void ReadEncoderRotary()
{
if (isShuttingDown || encoder == null)
return;
if (encoder.ButtonPressed)
{
if (stableButton < 3)
stableButton++;
}
else
{
stableButton = 0;
wasPressed = false;
}
if (stableButton == 3 && !wasPressed)
{
wasPressed = true;
OnEnterButtonPressed(true);
if (isShuttingDown || encoder == null)
return;
}
int position = encoder.Position;
if (position != last)
{
int movement = position - last;
last = position;
OnRotaryEncoderRotate(movement);
}
}
private void OnEnterButtonPressed(bool level)
{
int now = Environment.TickCount;
if (now - ButtonLastClick < ButtonClickDuration)
return;
ButtonLastClick = now;
if (current_screen_index == 0)
ShowScreen(current_mainmenu_choose + 1);
else if (current_screen_index == 1)
{
if (FoundedFiles.Count != 0)
ShowScreen(3);
}
else if (current_screen_index == 2)
{
var setting = AppSettingData[current_setting_choose];
if (setting.options.Count != 0)
{
is_in_settting_edit_mode = !is_in_settting_edit_mode;
// Commit once when leaving edit mode.
if (!is_in_settting_edit_mode)
SaveSetting();
OnSettingDataChanged(setting);
}
else
OnSettingDataChanged(setting);
if (!isShuttingDown)
ShowScreen(2);
}
}
private void OnRotaryEncoderRotate(int v)
{
if (v == 0) return;
if (current_screen_index == 0)
{
current_mainmenu_choose += v > 0 ? 1 : -1;
if (current_mainmenu_choose < 0) current_mainmenu_choose = 1;
if (current_mainmenu_choose > 1) current_mainmenu_choose = 0;
ShowScreen(0);
}
else if (current_screen_index == 1)
{
if (FoundedFiles.Count == 0) return;
current_file_choosed += v > 0 ? 1 : -1;
if (current_file_choosed < 0)
{
current_file_choosed = 0;
ShowScreen(0);
}
else if (current_file_choosed >= FoundedFiles.Count)
{
current_file_choosed = 0;
ShowScreen(1);
}
else ShowScreen(1);
}
else if (current_screen_index == 2)
{
if (!is_in_settting_edit_mode)
{
current_setting_choose += v > 0 ? 1 : -1;
if (current_setting_choose < 0)
{
current_setting_choose = 0;
ShowScreen(0);
}
else if (current_setting_choose >= AppSettingData.Count)
{
current_setting_choose = 0;
ShowScreen(2);
}
else
ShowScreen(2);
}
else
{
var setting = AppSettingData[current_setting_choose];
if (setting.options.Count == 0) return;
int nextValue = Math.Clamp(setting.m_value + (v > 0 ? 1 : -1),
0, setting.options.Count - 1);
if (nextValue == setting.m_value) return;
setting.m_value = nextValue;
AppSettingData[current_setting_choose] = setting;
OnSettingDataChanged(setting);
// Keep edits in RAM. The button handler persists them on edit commit.
ShowScreen(2);
}
}
}
private void OnSettingDataChanged(SettingData data)
{
switch (data.name)
{
case "Contrast":
oled.SetContrast(int.Parse(data.options[data.m_value]));
break;
case "Reset Board":
SaveSetting();
oled.ClearBuffer();
oled.SendBuffer();
Hal.Reset();
break;
case "Shutdown":
Shutdown();
break;
default:
break;
}
}
private void Shutdown()
{
SaveSetting();
isShuttingDown = true;
Timer.Stop(0);
encoder.Dispose();
encoder = null;
try
{
oled.ClearBuffer();
oled.SendBuffer();
oled.Sleep();
if (Hal.Has("Wifi")) Wifi.Off();
if (Hal.Has("Ble")) Ble.Reset();
WaitForWakeHold();
Hal.Reset();
return;
}
catch (Exception e)
{
Console.WriteLine("Sleep failed: " + e.Message);
}
Power.ClearWakePin();
oled.Wake();
isShuttingDown = false;
ShowScreen(current_screen_index);
StartInput();
}
private void WaitForButtonRelease()
{
while (!GPIO.Read(BUTTON_ENTER_PIN))
Thread.Sleep(20);
}
private void WaitForWakeHold()
{
GPIO.Mode(BUTTON_ENTER_PIN, GPIO.InputPullUp);
WaitForButtonRelease();
Thread.Sleep(50);
Power.SetWakePin(BUTTON_ENTER_PIN, false);
while (true)
{
Power.LightSleep();
int pressedAt = Environment.TickCount;
while (!GPIO.Read(BUTTON_ENTER_PIN))
{
if (Environment.TickCount - pressedAt >= WAKEUP_HOLD_MS)
{
WaitForButtonRelease();
return;
}
Thread.Sleep(20);
}
}
}
private struct SettingData
{
public string name;
public int m_value;
public List<string> options = new List<string>();
}
}
}
var app = new MainApp();
app.Start();
