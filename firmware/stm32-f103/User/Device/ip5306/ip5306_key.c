#include "ip5306_key.h"

#include "app_config.h"
#include "board_pins.h"
#include "platform_time.h"

/*
 * 当前 GPIO 不再直接连接 KEY，而是驱动外部 NMOS 的栅极。
 * GPIO 高电平时 NMOS 导通，KEY 被拉到地，等价“按下”。
 * GPIO 低电平时 NMOS 截止，KEY 断开，等价“释放”。
 */
static void IP5306_Key_SetReleased(void)
{
    HAL_GPIO_WritePin(BOARD_IP5306_KEY_PORT, BOARD_IP5306_KEY_PIN, GPIO_PIN_RESET);
}

static void IP5306_Key_SetPressed(void)
{
    HAL_GPIO_WritePin(BOARD_IP5306_KEY_PORT, BOARD_IP5306_KEY_PIN, GPIO_PIN_SET);
}

void IP5306_Key_Init(void)
{
    IP5306_Key_SetReleased();
}

void IP5306_Key_PressMs(uint32_t durationMs)
{
    IP5306_Key_SetPressed();
    Platform_Time_DelayMs(durationMs);
    IP5306_Key_SetReleased();
}

void IP5306_Key_ShortPress(void)
{
    IP5306_Key_PressMs(APP_IP5306_KEY_SHORT_PRESS_MS);
}

void IP5306_Key_LongPress(void)
{
    IP5306_Key_PressMs(APP_IP5306_KEY_LONG_PRESS_MS);
}

void IP5306_Key_DoubleShortPress(void)
{
    IP5306_Key_ShortPress();
    Platform_Time_DelayMs(APP_IP5306_KEY_DOUBLE_PRESS_INTERVAL_MS);
    IP5306_Key_ShortPress();
}

void IP5306_Key_TestHold(void)
{
    IP5306_Key_PressMs(APP_IP5306_KEY_TEST_HOLD_MS);
}
