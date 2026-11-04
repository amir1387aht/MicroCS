// args: --lvgl=100x60
// only: v8
// LVGL 8 messages: lv_msg_subscribe with a C# callback (token result), lv_msg_send, lv_msg_unsubscribe.
int got = 0;
var tok = lv_msg_subscribe(7, (t, m) => {
    got++;
    Console.WriteLine("msg id=" + lv_msg_get_id(m) + " ud=" + lv_msg_get_user_data(m));
}, "u");
lv_msg_send(7, null);
lv_msg_send(8, null);
lv_msg_send(7, null);
Console.WriteLine("got " + got);
Console.WriteLine("unsub " + lv_msg_unsubscribe(tok));
lv_msg_send(7, null);
Console.WriteLine("got " + got);
Console.WriteLine("unsub again " + lv_msg_unsubscribe(tok));
