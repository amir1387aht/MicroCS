using App.Display;

namespace App.MainApp
{
    public class MainApp
    {
        public Display display;
        private U8g2 oled;
        private int current_screen_index = 0;
        private int current_mainmenu_choose = 0;
        private int current_setting_choose = 0;
        private int is_in_settting_edit_mode = 1;
        private const int BUTTON_ENTER_PIN = 8;
        private const int ROTARY_ENCODER_PIN = 4;
        private const string PROJECT_VER = "V0.1";
        private const string FILES_PATH = "files/text/";
        private const string SETTING_PATH = "setting.json";
        private List<string> FoundedFiles = new List<string>();
        private List<SettingData> AppSettingData = new List<SettingData>()
        {
            new SettingData()
            {
                name = "Contrast",
                m_value = 4,
                options = new List<string>() { "10", "30", "50", "70", "100" }
            },
            new SettingData()
            {
                name = "Reset Board", m_value = 0, options = new List<string>()
            },
            new SettingData()
            {
                name = "Shutdown", m_value = 0, options = new List<string>()
            },
            new SettingData()
            {
                name = $"{PROJECT_VER}\nBy Another.",
                m_value = 0, options = new List<string>()
            }
        };
        private int current_file_choosed = 0;
        private int ButtonLastClick = 0;
        private int ButtonClickDuration = 300;
        private int RotaryLastRotate = 0;
        private int RotaryDuration = 300;
        private string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n");
        private string Json(Dictionary<string, object> d)
        {
            var parts = d.Select(kv => $"\"{Esc(kv.Key)}\":" + kv.Value switch
            {
                null => "null",
                string s => $"\"{Esc(s)}\"",
                bool b => b ? "true" : "false",
                double x => x.ToString("F3"),
                _ => kv.Value.ToString(),
            });
            return "{" + string.Join(",", parts) + "}";
        }
        private RotaryEncoder encoder;
        public void Start()
        {
            if (!File.Exists(SETTING_PATH))
                File.WriteAllText(SETTING_PATH, "{}");
            string[] settingData = File.ReadAllLines(SETTING_PATH);
            // var enter_button = new Pin(BUTTON_ENTER_PIN, GPIO.InputPullUp);
            // enter_button.OnChange(GPIO.Falling, OnEnterButtonPressed);
            // GPIO.Mode(4, GPIO.InputPullDown);
            Timer.Start(0, 30_000, ReadEncoderRotary);
            encoder = new RotaryEncoder(18, 7, 2, BUTTON_ENTER_PIN);
            display = new Display();
            display.Init();
            oled = display.oled;
            display.DrawCentered("Welcome!");
            oled.SendBuffer();
            Thread.Sleep(1500);
            ShowScreen(2);
        }
        private void ShowScreen(int index)
        {
            current_screen_index = index;
            switch (index)
            {
                case 0: ShowMenuScreen(); break;
                case 1: ShowLoadScreen(); break;
                case 2: ShowSettingScreen(); break;
                case 3: ShowTextPage();
                default: break;
            }
        }
        private void LoadTextFiles()
        {
            FoundedFiles.Clear();
            var files = Directory.GetFiles(FILES_PATH);
            foreach (var file in files)
            {
                if (File.GetLength(file) > 0)
                    FoundedFiles.Add($"{Path.GetFileName(file), 12}");
            }
        }
        private void ShowTextPage() { }
        private void ShowLoadScreen()
        {
            oled.ClearBuffer();
            LoadTextFiles();
            if (FoundedFiles.Count == 0)
            {
                display.DrawCentered("No Files Found!");
                oled.SendBuffer();
                return;
            }
            string filename = FoundedFiles[current_file_choosed];
            if (string.IsNullOrEmpty(filename)) return;
            display.DrawCentered(filename);
            oled.DrawStr(6, display.CenterHeight(), "<");
            oled.DrawStr(oled.Width - 6, display.CenterHeight(), ">");
            oled.SendBuffer();
        }
        private void ShowSettingScreen()
        {
            oled.ClearBuffer();
            var currentSetting = AppSettingData[current_setting_choose];
            if (string.IsNullOrEmpty(currentSetting.name)) return;
            if (currentSetting.options.Count != 0)
            {
                oled.DrawStr(15, display.CenterHeight(), currentSetting.name);
                string setting_value = currentSetting.options[currentSetting.m_value];
                oled.DrawStr(oled.Width - oled.GetStrWidth(setting_value) - 15, display.CenterHeight(), setting_value);
                if (is_in_settting_edit_mode == 1)
                    oled.DrawFrame(oled.Width - oled.GetStrWidth(setting_value) - 20, display.CenterHeight() - 11, oled.GetStrWidth(setting_value) + 5, display.CenterHeight());
            }
            else display.DrawCentered(currentSetting.name);
            oled.SendBuffer();
        }
        private void ShowMenuScreen()
        {
            oled.ClearBuffer();
            if (current_mainmenu_choose == 0)
            {
                oled.DrawFrame(10, display.CenterHeight() - 11, 35, 14);
                oled.DrawStr(15, display.CenterHeight(), "Load");
                string SettingString = "Setting";
                oled.DrawStr(oled.Width - oled.GetStrWidth(SettingString) - 15, display.CenterHeight(), SettingString);
            }
            else
            {
                oled.DrawStr(15, display.CenterHeight(), "Load");
                string SettingString = "Setting";
                oled.DrawFrame(oled.Width - oled.GetStrWidth(SettingString) - 20, display.CenterHeight() - 11, 48, 14);
                oled.DrawStr(oled.Width - oled.GetStrWidth(SettingString) - 15, display.CenterHeight(), SettingString);
            }
            oled.SendBuffer();
        }
        int fuck = 0;
        int last = 0;
        bool wasPressed = false;
        int stableButton = 0;
        private void ReadEncoderRotary()
        {
            if (encoder.ButtonPressed)
            {
                if (stableButton < 3) stableButton++;
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
            }
            int position = encoder.Position;
            if (position != last)
            {
                OnRotaryEncoderRotate(position - last);
                encoder.Reset();
                last = encoder.Position;
            }
            // var adc = ADC.Read(4);
            // if (adc > 2000)
            // {
            //     if (RotaryLastRotate + RotaryDuration > Environment.TickCount) return;
            //     RotaryLastRotate = Environment.TickCount;
            //     fuck++;
            //     Console.WriteLine($"Right {fuck}\n-----------------");
            //     OnRotaryEncoderRotate(1);
            // }
        }
        private void OnEnterButtonPressed(bool level)
        {
            if (ButtonLastClick + ButtonClickDuration > Environment.TickCount) return;
            ButtonLastClick = Environment.TickCount;
            if (current_screen_index == 0) ShowScreen(current_mainmenu_choose + 1);
            else if (current_screen_index == 1) ShowScreen(3);
        }
        private void OnRotaryEncoderRotate(int v)
        {
            if (current_screen_index == 0)
            {
                if (v > 0) current_mainmenu_choose++;
                if (v < 0) current_mainmenu_choose--;
                if (current_mainmenu_choose < 0) current_mainmenu_choose = 1;
                if (current_mainmenu_choose > 1) current_mainmenu_choose = 0;
                ShowScreen(0);
            }
            else if (current_screen_index == 1)
            {
                if (v > 0) current_file_choosed++;
                if (v < 0) current_file_choosed--;
                if (current_file_choosed < 0) current_file_choosed = FoundedFiles.Count - 1;
                if (current_file_choosed > FoundedFiles.Count - 1) current_file_choosed = 0;
                ShowScreen(1);
            }
            else if (current_screen_index == 2)
            {
                if (v > 0) current_setting_choose++;
                if (v < 0) current_setting_choose--;
                if (current_setting_choose < 0 || current_setting_choose >= AppSettingData.Count)
                {
                    current_setting_choose = 0;
                    ShowScreen(0);
                }
                else ShowScreen(2);
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
