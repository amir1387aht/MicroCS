using App.Display;
namespace App.MainApp {
    class MainApp {
        Display display;
        U8g2 oled;
        int menuIndex = 0;
        int screen = 0;
        List<string> FoundedFiles = new List<string>();
        string[] items = { "Files", "Settings", "Back" };
        Pin enter;
        public void Start() {
            enter = new Pin(8, GPIO.InputPullUp);
            enter.OnChange(GPIO.Falling, OnEnterButtonPressed);
            display = new Display(); display.Init(); oled = display.oled;
            display.DrawCentered("Welcome"); oled.SendBuffer();
            Thread.Sleep(1500); DrawMenu();
        }
        public void DrawMenu() {
            oled.ClearBuffer(); display.SwitchFont(Display.Font.Title);
            display.DrawCentered(screen == 0 ? "Menu" : "Files");
            display.SwitchFont(Display.Font.Text);
            for (int i = 0; i < items.Length; i++) {
                if (i == menuIndex) oled.DrawBox(i * 42, 23, 40, 9);
                oled.DrawStr(i * 42, 30, items[i]);
            }
            oled.SendBuffer();
        }
        public void OnEnterButtonPressed(bool level) {
            menuIndex = (menuIndex + 1) % 3; screen = 1 - screen;
            FoundedFiles.Clear();
            foreach (string path in Directory.GetFiles("/")) FoundedFiles.Add(path);
            DrawMenu();
        }
    }
}
var app = new MainApp(); app.Start();
