#ifndef USER_DEVICE_IP5306_KEY_H
#define USER_DEVICE_IP5306_KEY_H

#include <stdint.h>

/*
 * IP5306 KEY 控制接口。
 * 当前通过 GPIO 推挽输出控制外部 NMOS：
 * GPIO 高电平 -> NMOS 导通 -> KEY 被拉到 GND -> 等价按下
 * GPIO 低电平 -> NMOS 截止 -> KEY 断开 -> 等价释放
 */

/* 初始化 KEY 引脚的默认状态，确保上电后处于“未按下”。 */
void IP5306_Key_Init(void);

/* 以毫秒为单位模拟一次按键按下，适合做时序验证。 */
void IP5306_Key_PressMs(uint32_t durationMs);

/* 模拟一次短按，通常用于开机、唤醒或点亮输出。 */
void IP5306_Key_ShortPress(void);

/* 模拟一次长按。 */
void IP5306_Key_LongPress(void);

/* 模拟 1 秒内连续两次短按。 */
void IP5306_Key_DoubleShortPress(void);

/* 拉低更长时间，方便用万用表验证 GPIO 是否真的能把 KEY 拉到地。 */
void IP5306_Key_TestHold(void);

#endif /* USER_DEVICE_IP5306_KEY_H */
