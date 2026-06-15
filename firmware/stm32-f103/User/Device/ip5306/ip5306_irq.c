#include "ip5306_irq.h"

#include "board_pins.h"
#include "platform_time.h"

static volatile uint8_t s_hasPendingEvent;
static volatile uint32_t s_eventCount;
static volatile uint32_t s_lastEventTickMs;

void IP5306_Irq_Init(void)
{
    s_hasPendingEvent = 0U;
    s_eventCount = 0U;
    s_lastEventTickMs = 0U;
}

uint8_t IP5306_Irq_Read(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(BOARD_IP5306_IRQ_PORT, BOARD_IP5306_IRQ_PIN);
}

void IP5306_Irq_OnRisingEdge(void)
{
    s_hasPendingEvent = 1U;
    s_eventCount++;
    s_lastEventTickMs = Platform_Time_GetTickMs();
}

uint8_t IP5306_Irq_HasPendingEvent(void)
{
    return s_hasPendingEvent;
}

uint32_t IP5306_Irq_GetEventCount(void)
{
    return s_eventCount;
}

uint32_t IP5306_Irq_GetLastEventTickMs(void)
{
    return s_lastEventTickMs;
}

void IP5306_Irq_ClearPendingEvent(void)
{
    s_hasPendingEvent = 0U;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BOARD_IP5306_IRQ_PIN) {
        IP5306_Irq_OnRisingEdge();
    }
}
