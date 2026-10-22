using App.Display;

namespace App.MainApp
{
    public class MainApp
    {
        public Display display;
        private U8g2 oled;
        private int current_screen_index = 0;
        private int current_mainmenu_choose = 0;
        private const int BUTTON_ENTER_PIN = 8;
        private const int ROTARY_ENCODER_PIN = 4;
        private const string FILES_PATH = "files/text/";
        private List<string> FoundedFiles = new List<string>();
        private int current_file_choosed = 0;
        public void Start()
        {
            var enter_button = new Pin(BUTTON_ENTER_PIN, GPIO.InputPullUp);
            enter_button.OnChange(GPIO.Falling, OnEnterButtonPressed);
            Timer.Start(0, 1000000, ReadEncoderRotary);
            display = new Display();
            display.Init();
            oled = display.oled;
            display.DrawCentered("Welcome!");
            oled.SendBuffer();
            Thread.Sleep(1500);
            ShowScreen(1);
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
            oled.DrawStr(6, display.Center_Height(), "<");
            oled.DrawStr(oled.Width - 6, display.Center_Height(), ">");
            oled.SendBuffer();
        }
        private void ShowSettingScreen() { }
        private void ShowMenuScreen()
        {
            oled.ClearBuffer();
            if (current_mainmenu_choose == 0)
            {
                oled.DrawFrame(10, display.Center_Height() - 11, 35, 14);
                oled.DrawStr(15, display.Center_Height(), "Load");
                string SettingString = "Setting";
                oled.DrawStr(oled.Width - oled.GetStrWidth(SettingString) - 15, display.Center_Height(), SettingString);
            }
            else
            {
                oled.DrawStr(15, display.Center_Height(), "Load");
                string SettingString = "Setting";
                oled.DrawFrame(oled.Width - oled.GetStrWidth(SettingString) - 20, display.Center_Height() - 11, 48, 14);
                oled.DrawStr(oled.Width - oled.GetStrWidth(SettingString) - 15, display.Center_Height(), SettingString);
            }
            oled.SendBuffer();
        }
        private void ReadEncoderRotary()
        {
            var adc = ADC.Read(4);
            Console.WriteLine(adc);
        }
        private void OnEnterButtonPressed(bool level)
        {
            if (current_screen_index == 0) ShowScreen(current_mainmenu_choose + 1);
            else if (current_screen_index == 1) ShowScreen(3);
        }
        private void OnRotaryEncoderRotate(int v)
        {
            if (current_screen_index == 0)
            {
                if (v > 0) current_mainmenu_choose++;
                if (v < 0) current_mainmenu_choose--;
                current_mainmenu_choose = Math.Clamp(current_mainmenu_choose, 0, 1);
                ShowScreen(0);
            }
            else if (current_screen_index == 1)
            {
                if (v > 0) current_file_choosed++;
                if (v < 0) current_file_choosed--;
                current_file_choosed = Math.Clamp(current_file_choosed, 0, FoundedFiles.Count);
                ShowScreen(1);
            }
        }
    }
}
var app = new MainApp();
app.Start();
