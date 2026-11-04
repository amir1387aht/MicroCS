# LVGL examples

These scripts also appear under **LVGL graphics** in Studio. They require MicroCS built with the LVGL binding and the corresponding LVGL widgets enabled. On hardware the firmware must initialise LVGL, the display, input and tick first; see [the LVGL guide](../../docs/LVGL.md).

- `keyboard.cs`: text area and on-screen keyboard.
- `calendar.cs`: a calendar with a highlighted date.
- `canvas.cs`: a colour-filled canvas buffer.
- `arc_dashboard.cs`: a timer updating an arc, bar and label.
- `event_data.cs`: event user data and object-tree traversal.

On the host, build `make mcs-lvgl LVGL_DIR=/path/to/lvgl` and run, for example, `build/mcs_lvgl --lvgl=320x240 --sim examples/lvgl/arc_dashboard.cs`. These interactive examples keep running until stopped. Widget availability depends on `lv_conf.h`.
