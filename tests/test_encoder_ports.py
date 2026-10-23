"""Compile the actual GPIO producers with small SDK doubles (not full SDK builds)."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
def function(path, signature):
    text = (root/path).read_text()
    begin = text.index(signature)
    brace = text.index('{', begin)
    depth = 1; end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    assert 'hal->gpio_capture = true;' in text
    return text[begin:end]
common = '''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define MCS_HAL_EV_GPIO 1
static int g_cfg, calls, posts, consume;
static bool mcs_hal_gpio_edge(void* ctx,int pin) { assert(ctx == &g_cfg); assert(pin == 3); calls++; return consume != 0; }
static bool mcs_hal_post(int type,int pin,int level) { assert(type==1 && pin==3 && level==1); posts++; return true; }
'''
ports = [
('esp32','ports/esp32/mcs_port_esp32.c','static void IRAM_ATTR gpio_isr(','''
#define IRAM_ATTR
#define E_INV(p) 0
typedef int gpio_num_t;
static int gpio_get_level(gpio_num_t p) { assert(p==3); return 1; }
#define post_isr mcs_hal_post
''','gpio_isr((void*)(intptr_t)3);'),
('rp2','ports/rp2/mcs_port_rp2.c','static void gpio_cb(','''
typedef unsigned uint;
#define GPIO_IRQ_EDGE_RISE 1
#define GPIO_IRQ_EDGE_FALL 2
#define R_INV(p) 0
static int gpio_get(uint p) { assert(p==3); return 1; }
''','gpio_cb(3,GPIO_IRQ_EDGE_RISE);'),
('arduino','ports/arduino/mcs_port_arduino.cpp','template <int N> static void irq_tramp(','''
#define HIGH 1
static volatile int g_irq_pin[1] = {3};
static int digitalRead(int p) { assert(p==3); return 1; }
''','irq_tramp<0>();'),
('stm32','ports/stm32/mcs_port_stm32.c','void mcs_stm32_exti(','''
typedef struct { int exti_pin[16]; } mcs_stm32_board_t;
#define g_cfg board_storage
static mcs_stm32_board_t board_storage, *g_board=&board_storage;
typedef int GPIO_TypeDef;
#define GPIO_PIN_SET 1
static GPIO_TypeDef* port_fast(int p) { assert(p==3); static int port; return &port; }
static int HAL_GPIO_ReadPin(GPIO_TypeDef* p,uint16_t mask) { assert(p && mask==8); return 1; }
''','g_board->exti_pin[3]=4; mcs_stm32_exti(8);'),
('zephyr','ports/zephyr/mcs_port_zephyr.c','static void gpio_isr(','''
#define BIT(n) (1u<<(n))
struct device { int unused; };
struct gpio_callback { int unused; };
typedef unsigned gpio_port_pins_t;
typedef unsigned gpio_pin_t;
static struct gpio_callback g_gcb[1];
static const struct device dev = {0};
static int gpio_pin_get_raw(const struct device* d,gpio_pin_t p) { assert(d==&dev && p==3); return 1; }
''','gpio_isr(&dev,g_gcb,8);')]
with tempfile.TemporaryDirectory() as tmp:
    for name,path,signature,prelude,invoke in ports:
        code = common
        if name == 'stm32':
            # The producer passes the board, not the other ports' config object.
            code = code.replace('static int g_cfg, calls, posts, consume;', 'static int calls, posts, consume;\nstatic void* expected_ctx;')
            code = code.replace('ctx == &g_cfg','ctx == expected_ctx')
        code += prelude + function(path,signature)
        code += '\nint main(void) {\n'
        if name == 'stm32': code += 'expected_ctx = g_board;\n'
        code += 'consume=1; '+invoke+' assert(calls==1 && posts==0);\n'
        code += 'consume=0; '+invoke+' assert(calls==2 && posts==1); return 0; }\n'
        src=Path(tmp)/(name+'.cpp'); src.write_text(code)
        exe=Path(tmp)/name
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        print('PASS',name,'actual GPIO producer: capture before queue, ordinary GPIO fallback')
