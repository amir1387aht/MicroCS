namespace App.Display
{
    public class Display
    {
        public U8g2 oled;
        public void Init()
        {
            oled = new U8g2("u8g2_Setup_ssd1306_i2c_128x32_univision_f");
            oled.Begin();
            oled.ClearBuffer();
            SwitchFont(Font.Text);
            oled.SendBuffer();
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
        public void SwitchFont(Font font = Font.Text)
        {
            switch (font)
            {
                case Font.Title:
                    oled.SetFont("u8g2_font_helvR08_tf");
                    break;
                case Font.Text:
                default:
                    oled.SetFont("u8g2_font_ncenB08_te");
                    break;
                case Font.Icon:
                    oled.SetFont("u8g2_font_streamline_all_t");
                    break;
            }
        }
        public enum Font { Title, Text, Icon }
    }
}
