#pragma once
void fan_gpio_init(void);                  // 初始化LED/继电器GPIO
void change_fan_state(bool on);            // 控制风扇并闪灯
void change_fan_state(bool on, int level); // 控制风扇并闪灯 level 1 2 3
int getFanLevel(void);                     // 获取上次风扇挡位 1 2 3
bool get_fan_isON(void);                   // 获取风扇状态
void fan_gpio_led_on(void);                // LED常亮
void fan_gpio_led_off(void);               // LED熄灭
void blink_led(int times);
void set_fan_timer(int seconds);
bool fan_timer_running();
int fan_timer_left();
void cancel_fan_timer();
