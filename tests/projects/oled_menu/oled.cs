// Representative two-file OLED/menu regression, not an exact hardware capture.
namespace App.Display {
    class Display {
        public U8g2 oled;
        public enum Font { Title, Text, Icon }
        public void Init() {
            oled = new U8g2("ssd1306_i2c_128x32_univision");
            oled.Begin(); oled.ClearBuffer(); SwitchFont(Font.Text);
        }
        public int Center_Width(string text) { return (oled.Width - oled.GetStrWidth(text)) / 2; }
        public int Center_Height() { return (oled.Height + oled.GetAscent()) / 2; }
        public void DrawCentered(string text) { oled.DrawStr(Center_Width(text), Center_Height(), text); }
        public void SwitchFont(Font font) {
            if (font == Font.Title) oled.SetFont("ncenB08_te");
            else if (font == Font.Icon) oled.SetFont("streamline_all_t");
            else oled.SetFont("helvR08_tf");
        }
    }
}
// Dependency loading must install the declarations above without executing this.
int oledDependencyTopLevelMustNotRun = 1;
