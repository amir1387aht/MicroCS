/* MicroCS Studio - completion snippets: type the label and pick it from the completion list.
 * text: unique ${1:default} fields are visited numerically; $0 is the final caret.
 * Legacy $0-only snippets remain supported. Labels must be unique
 * (tests/studio/test_templates.js checks). The templates live in tools/studio/templates/. */
const SNIPPETS = [
  { label: "wifi", detail: "Wifi.Connect + print the IP", text: "if (Wifi.Connect(\"$0\", \"password\"))\n    Console.WriteLine(\"ip \" + Wifi.Ip);\nelse\n    Console.WriteLine(\"Wi-Fi: \" + Wifi.StatusName);" },
  { label: "wifievents", detail: "Wifi.OnEvent((ev, value) => ...)", text: "Wifi.OnEvent((ev, value) =>\n{\n    Console.WriteLine(Wifi.EventName(ev));\n    $0\n});" },
  { label: "httpget", detail: "Http.Get with error handling", text: "try\n{\n    var r = Http.Get(\"$0\");\n    Console.WriteLine(r.StatusCode + \" \" + r.Body);\n}\ncatch (IOException e) { Console.WriteLine(\"network error: \" + e.Message); }" },
  { label: "httppost", detail: "Http.Post a JSON body", text: "var r = Http.Post(\"$0\", Json.Stringify(data), \"application/json\");" },
  { label: "json", detail: "Json.Parse a reply", text: "var doc = Json.Parse($0);\nConsole.WriteLine(doc[\"key\"]);" },
  { label: "tcpclient", detail: "TcpClient: connect, write a line, read a line", text: "var tcp = new TcpClient(\"$0\", 80, 5000);\ntcp.WriteLine(\"hello\");\nConsole.WriteLine(tcp.ReadLine());\ntcp.Close();" },
  { label: "tcpserver", detail: "TcpListener accept loop", text: "var server = new TcpListener($0);\nserver.Start();\nwhile (true)\n{\n    var c = server.AcceptTcpClient(500);\n    if (c == null) continue;\n    c.WriteLine(c.ReadLine());\n    c.Close();\n}" },
  { label: "udp", detail: "UdpClient send + receive", text: "var udp = new UdpClient($0);\nudp.Send(\"hello\", \"192.168.1.255\", 5000);\nvar reply = udp.ReceiveString(1000);" },
  { label: "httpserver", detail: "HttpServer with a page and a JSON route", text: "var web = new HttpServer(80);\nweb.Get(\"/\", req => \"<h1>$0</h1>\");\nweb.Get(\"/api/status\", req => new Dictionary<string, object> { [\"uptime\"] = Environment.TickCount });\nweb.Start();\nConsole.WriteLine($\"open http://{Wifi.Ip}:{web.Port}/\");\nweb.Run();" },
  { label: "route", detail: "HttpServer route with a path parameter", text: "web.Post(\"/$0/{id}\", req =>\n{\n    string id = req.Param(\"id\");\n    return \"ok\";\n});" },
  { label: "wsserver", detail: "WebSocket endpoint on an HttpServer", text: "web.WebSocket(\"/ws\",\n    (ws, msg) => ws.Send(\"echo: \" + msg),\n    ws => Console.WriteLine(\"connected \" + ws.RemoteAddress),\n    ws => Console.WriteLine(\"closed\"));$0" },
  { label: "wsclient", detail: "WebSocket client: connect, send, receive", text: "var ws = new WebSocket();\nws.Connect(\"ws://$0\", 5000);\nws.OnMessage(m => Console.WriteLine(m));\nws.Send(\"hello\");\nwhile (ws.IsOpen) ws.Poll(100);" },
  { label: "captive", detail: "hotspot + DnsServer captive portal", text: "Wifi.StartAp(\"$0MicroCS-Setup\", \"\");\nvar dns = new DnsServer(Wifi.ApIp);\ndns.Start();\nvar web = new HttpServer(80);\nweb.NotFound(req => req.Redirect(\"http://\" + Wifi.ApIp + \"/\"));\nweb.Start();" },
  { label: "wps", detail: "Wifi.Wps / SmartConfig provisioning", text: "if (Net.Has(\"Wifi.Wps\") && Wifi.Wps(120000))\n    Console.WriteLine(\"joined \" + Wifi.Ssid);$0" },
  { label: "espnow", detail: "EspNow start, peer, send", text: "EspNow.Start();\nEspNow.AddPeer(\"$0\");\nEspNow.OnReceive((mac, data) => Console.WriteLine(mac + \": \" + Encoding.UTF8.GetString(data)));" },
  { label: "ntp", detail: "Ntp.Sync + local time", text: "Ntp.Timezone = \"$0UTC0\";\nNtp.Sync();\nConsole.WriteLine(Ntp.LocalTime);" },
  { label: "u8g2", detail: "U8g2 OLED on I2C + first frame", text: "var oled = new U8g2(\"ssd1306_i2c_128x64_noname\");\noled.Begin();\noled.SetFont(\"6x10_tf\");\noled.ClearBuffer();\noled.DrawStr(0, 12, \"$0\");\noled.SendBuffer();" },
  { label: "u8frame", detail: "U8g2 frame: ClearBuffer ... SendBuffer", text: "oled.ClearBuffer();\n$0\noled.SendBuffer();" },
  { label: "u8page", detail: "U8g2 page mode: Draw(g => ...)", text: "oled.Draw(g =>\n{\n    $0\n});" },
  { label: "u8x8", detail: "U8x8 text display (no frame buffer)", text: "var lcd = new U8x8(\"ssd1306_i2c_128x64_noname\");\nlcd.Begin();\nlcd.SetFont(\"chroma48medium8_r\");\nlcd.DrawString(0, 0, \"$0\");" },
  { label: "gpiocfg", detail: "Pin with GpioConfig (pull, initial level)", text: "var pin = new Pin($0, new GpioConfig(GPIO.Output) { Initial = 0 });" },
  { label: "i2ccfg", detail: "I2C.Open with I2cConfig (speed, SDA, SCL)", text: "I2C.Open(0, new I2cConfig(400_000, $0, ) { PullUps = true });" },
  { label: "spicfg", detail: "SPI.Open with SpiConfig (speed, mode, CS)", text: "SPI.Open(0, new SpiConfig(8_000_000, 0) { Cs = $0 });" },
  { label: "uartcfg", detail: "UART.Open with UartConfig (baud, TX, RX)", text: "UART.Open(1, new UartConfig(115200, $0, ) { RxBuffer = 1024 });" },
  { label: "dmabuf", detail: "DmaBuffer", text: "var buf = new DmaBuffer($0);" },
  { label: "spiasync", detail: "SPI.WriteAsync + Wait (DMA)", text: "var t = SPI.WriteAsync(0, $0);\n// other work while the transfer runs\nt.Wait();" },
  { label: "cw", detail: "Console.WriteLine", text: "Console.WriteLine($0);" },
  { label: "cwi", detail: "Console.WriteLine($\"...\")", text: "Console.WriteLine($\"$0\");" },
  { label: "for", detail: "for loop", text: "for (int i = 0; i < ${1:3}; i++)\n{\n    ${2:Console.WriteLine(i);}\n}\n$0" },
  { label: "forr", detail: "reverse for loop", text: "for (int i = ${1:3} - 1; i >= 0; i--)\n{\n    ${2:Console.WriteLine(i);}\n}\n$0" },
  { label: "foreach", detail: "foreach loop", text: "foreach (var item in ${1:new int[] {1, 2, 3\\}})\n{\n    ${2:Console.WriteLine(item);}\n}\n$0" },
  { label: "while", detail: "while loop", text: "while (${1:false})\n{\n    ${2:Console.WriteLine(\"loop\");}\n}\n$0" },
  { label: "loop", detail: "main loop with Thread.Sleep", text: "while (true)\n{\n    $0\n    Thread.Sleep(100);\n}" },
  { label: "do", detail: "do / while", text: "do\n{\n    $0\n} while ();" },
  { label: "if", detail: "if statement", text: "if (${1:true})\n{\n    ${2:Console.WriteLine(\"yes\");}\n}\n$0" },
  { label: "ife", detail: "if / else", text: "if (${1:true})\n{\n    ${2:Console.WriteLine(\"yes\");}\n}\nelse\n{\n    ${3:Console.WriteLine(\"no\");}\n}\n$0" },
  { label: "switch", detail: "switch statement", text: "switch ($0)\n{\n    case 0:\n        break;\n    default:\n        break;\n}" },
  { label: "try", detail: "try / catch", text: "try\n{\n    ${1:Console.WriteLine(\"try\");}\n}\ncatch (Exception e)\n{\n    ${2:Console.WriteLine(e.Message);}\n}\n$0" },
  { label: "tryf", detail: "try / catch / finally", text: "try\n{\n    ${1:Console.WriteLine(\"try\");}\n}\ncatch (Exception e)\n{\n    ${2:Console.WriteLine(e.Message);}\n}\nfinally\n{\n    ${3:Console.WriteLine(\"finally\");}\n}\n$0" },
  { label: "tryio", detail: "catch hardware errors", text: "try\n{\n    $0\n}\ncatch (IOException e) { Console.WriteLine($\"I/O error: {e.Message}\"); }\ncatch (TimeoutException) { Console.WriteLine(\"timeout\"); }" },
  { label: "class", detail: "class", text: "class ${1:Example}\n{\n    ${2:public int Value { get; set; \\}}\n}\n$0" },
  { label: "ctor", detail: "constructor", text: "public $0()\n{\n    \n}" },
  { label: "prop", detail: "auto property", text: "public int $0 { get; set; }" },
  { label: "propg", detail: "read-only property", text: "public int $0 => 0;" },
  { label: "interface", detail: "interface", text: "interface I$0\n{\n    \n}" },
  { label: "enum", detail: "enum", text: "enum $0 { A, B, C }" },
  { label: "func", detail: "local function", text: "int ${1:Compute}(int x)\n{\n    return ${2:x + 1};\n}\n$0" },
  { label: "lambda", detail: "Action lambda", text: "Action $0 = () =>\n{\n    \n};" },
  { label: "pin", detail: "new Pin output", text: "var led = new Pin($0, GPIO.Output);" },
  { label: "pinin", detail: "new Pin input with pull-up", text: "var button = new Pin($0, GPIO.InputPullUp);" },
  { label: "onchange", detail: "pin interrupt", text: "button.OnChange(GPIO.Falling, (bool level) =>\n{\n    $0\n});" },
  { label: "gpioirq", detail: "GPIO.OnChange(pin, edge, fn)", text: "GPIO.OnChange($0, GPIO.Both, (int pin, bool level) =>\n{\n    \n});" },
  { label: "every", detail: "Scheduler.Every", text: "Scheduler.Every(1000, () =>\n{\n    $0\n});" },
  { label: "after", detail: "Scheduler.After", text: "Scheduler.After(1000, () =>\n{\n    $0\n});" },
  { label: "timer", detail: "Timer.Start (hardware)", text: "Timer.Start(0, 1000, () =>\n{\n    $0\n});" },
  { label: "i2cdev", detail: "new I2cDevice", text: "I2C.Open(0, 400000);\nvar dev = new I2cDevice(0, 0x$0);" },
  { label: "i2cscan", detail: "scan the I2C bus", text: "I2C.Open(0);\nforeach (int a in I2C.Scan(0)) Console.WriteLine($\"0x{a:X2}\");$0" },
  { label: "spidev", detail: "new SpiDevice", text: "var dev = new SpiDevice(0, $0, 1000000, 0);" },
  { label: "uart", detail: "open a UART", text: "UART.Open(1, 115200);$0" },
  { label: "uartrx", detail: "UART.OnReceive", text: "UART.OnReceive(1, (int available) =>\n{\n    string s = UART.ReadString(1, available);\n    $0\n});" },
  { label: "adc", detail: "read millivolts", text: "int mv = ADC.ReadMillivolts($0);" },
  { label: "pwm", detail: "PWM.Set", text: "PWM.Set($0, 1000, 0.5);" },
  { label: "servo", detail: "new Servo (hobby servo)", text: "var servo = new Servo($0);\nservo.Angle = 90;" },
  { label: "sw", detail: "Stopwatch", text: "var sw = Stopwatch.StartNew();\n$0\nConsole.WriteLine($\"{sw.ElapsedMilliseconds} ms\");" },
  { label: "wdt", detail: "watchdog", text: "Watchdog.Start(3000);\n// call Watchdog.Feed() regularly$0" },
  { label: "readfile", detail: "read a text file", text: "string text = File.Exists(\"$0\") ? File.ReadAllText(\"\") : \"\";" },
  { label: "append", detail: "append a line to a file", text: "File.AppendAllText(\"/log.txt\", $\"{Environment.TickCount}: $0\\n\");" },
  { label: "ls", detail: "list files", text: "foreach (var f in Directory.GetFiles(\"/$0\")) Console.WriteLine(f);" },
  { label: "has", detail: "if the board has a peripheral", text: "if (Hal.Has(\"$0\"))\n{\n    \n}" },
  { label: "plot", detail: "print values for the serial plotter", text: "Console.WriteLine($\"a:{$0} b:{0}\");" },
  { label: "pulse", detail: "GPIO.PulseIn (pulse length in µs)", text: "int us = GPIO.PulseIn($0, true, 30000);" },
  { label: "regs", detail: "read I2C registers", text: "byte[] r = I2C.ReadRegisters(0, 0x$0, 0x00, 2);\nint value = (r[0] << 8) | r[1];" },
  { label: "bcd", detail: "BCD helpers (RTC chips)", text: "int FromBcd(int b) => (b >> 4) * 10 + (b & 0x0F);\nint ToBcd(int v) => ((v / 10) << 4) | (v % 10);$0" },
  { label: "clamp", detail: "Math.Clamp", text: "Math.Clamp($0, 0, 100)" },
  { label: "map", detail: "map a value from one range to another", text: "double Map(double x, double inMin, double inMax, double outMin, double outMax) => (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;$0" },
  { label: "usingb", detail: "using block (calls Dispose)", text: "using (var $0 = )\n{\n    \n}" },
  { label: "cmd", detail: "read commands from the console", text: "for (;;)\n{\n    string line = Console.ReadLine();\n    if (line == null) break;\n    $0\n}" },
];

/* The small snippet grammar is deliberately not an LSP/mirror language. The
 * existing offline script owns both its catalog and its pure UTF-16 model. */
const SnippetText = (() => {
  const MAX_TEMPLATE = 16 * 1024,
    MAX_OUTPUT = 32 * 1024;
  const MAX_FIELDS = 32,
    MAX_DOCUMENT = 1024 * 1024;
  function parse(template, indent = "") {
    if (
      typeof template !== "string" ||
      template.length > MAX_TEMPLATE ||
      indent.length > 256 ||
      !/^[ \t]*$/.test(indent)
    )
      throw new Error("Snippet template or indentation exceeds its bound.");
    const source = template.replace(/\r\n|\r/g, "\n"),
      fields = [],
      ids = new Set();
    let text = "",
      final = null,
      finalOrder = -1,
      order = 0;
    const append = (value) => {
      text += value.replace(/\n/g, "\n" + indent);
      if (text.length > MAX_OUTPUT)
        throw new Error("Expanded snippet exceeds its bound.");
    };
    for (let i = 0; i < source.length;) {
      if (source.startsWith("$0", i)) {
        if (final !== null || /\d/.test(source[i + 2] || ""))
          throw new Error("Snippet needs a unique final $0 marker.");
        final = text.length;
        finalOrder = order++;
        i += 2;
      } else if (source.startsWith("${", i)) {
        const header = /^\$\{([1-9]\d*):/.exec(source.slice(i));
        if (!header)
          throw new Error("Invalid snippet field; use ${1:default}.");
        const id = Number(header[1]);
        if (id > MAX_FIELDS || ids.has(id) || fields.length >= MAX_FIELDS)
          throw new Error(
            "Snippet fields must be unique numbers from 1 to 32.",
          );
        ids.add(id);
        i += header[0].length;
        let value = "",
          closed = false;
        while (i < source.length) {
          const ch = source[i++];
          if (ch === "}") {
            closed = true;
            break;
          }
          if (ch === "\\" && /[}\\]/.test(source[i] || ""))
            value += source[i++];
          else {
            if (ch === "$" && (source[i] === "{" || /\d/.test(source[i] || "")))
              throw new Error(
                "Nested or mirrored snippet fields are not supported.",
              );
            value += ch;
          }
        }
        if (!closed) throw new Error("Unclosed snippet field.");
        const a = text.length;
        append(value);
        fields.push({ id, a, b: text.length, order: order++ });
      } else {
        if (source[i] === "$" && /\d/.test(source[i + 1] || ""))
          throw new Error("Numbered fields need a default: ${1:default}.");
        append(source[i++]);
      }
    }
    if (fields.length && final === null)
      throw new Error("Numbered snippets require a final $0 marker.");
    return {
      text,
      fields: fields.sort((a, b) => a.id - b.id),
      final: final === null ? text.length : final,
      finalOrder,
    };
  }
  class Session {
    constructor(parsed, start, view) {
      if (
        !Number.isInteger(start) ||
        start < 0 ||
        !parsed.fields.length ||
        view.length > MAX_DOCUMENT ||
        view.slice(start, start + parsed.text.length) !== parsed.text
      )
        throw new Error(
          "Snippet navigation is unavailable for this document or insertion.",
        );
      this.fields = parsed.fields.map((field) => ({
        ...field,
        a: field.a + start,
        b: field.b + start,
      }));
      this.final = parsed.final + start;
      this.finalOrder = parsed.finalOrder;
      this.view = view;
      this.active = 0;
    }
    field() {
      return this.fields[this.active];
    }
    contains(a, b) {
      const field = this.field();
      return a >= field.a && b >= a && b <= field.b;
    }
    update(next) {
      if (next === this.view) return true;
      if (next.length > MAX_DOCUMENT) return false;
      const field = this.field(),
        delta = next.length - this.view.length,
        end = field.b + delta;
      // Accept only a change contained in the active field. Repeated characters
      // cannot make a guessed minimal diff drift into surrounding source.
      if (
        end < field.a ||
        next.slice(0, field.a) !== this.view.slice(0, field.a) ||
        next.slice(end) !== this.view.slice(field.b)
      )
        return false;
      for (const other of this.fields)
        if (other.order > field.order) {
          other.a += delta;
          other.b += delta;
        }
      if (
        this.final > field.b ||
        (this.final === field.b && this.finalOrder > field.order)
      )
        this.final += delta;
      field.b = end;
      this.view = next;
      return true;
    }
    move(backward = false) {
      if (backward) this.active = Math.max(0, this.active - 1);
      else if (++this.active === this.fields.length) {
        this.active--;
        return { done: true, a: this.final, b: this.final };
      }
      const field = this.field();
      return { done: false, a: field.a, b: field.b };
    }
  }
  return Object.freeze({
    parse,
    Session,
    MAX_TEMPLATE,
    MAX_OUTPUT,
    MAX_FIELDS,
    MAX_DOCUMENT,
  });
})();
