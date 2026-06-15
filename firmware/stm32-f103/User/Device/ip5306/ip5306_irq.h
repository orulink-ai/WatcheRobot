#ifndef USER_DEVICE_IP5306_IRQ_H
#define USER_DEVICE_IP5306_IRQ_H

#include <stdint.h>

/*
 * IP5306 IRQ 读取接口。
 * 当前提供引脚电平读取、上升沿事件记录和状态查询接口。
 */

/* 初始化 IRQ 事件状态，避免上电残留旧标志。 */
void IP5306_Irq_Init(void);

uint8_t IP5306_Irq_Read(void);

/* 由 EXTI 回调在检测到 IRQ 上升沿时调用。 */
void IP5306_Irq_OnRisingEdge(void);

/* 是否存在尚未清除的 IRQ 事件标志。 */
uint8_t IP5306_Irq_HasPendingEvent(void);

/* 获取 IRQ 上升沿累计次数。 */
uint32_t IP5306_Irq_GetEventCount(void);

/* 获取最近一次 IRQ 上升沿发生时的系统毫秒计数。 */
uint32_t IP5306_Irq_GetLastEventTickMs(void);

/* 清除 IRQ 待处理标志。 */
void IP5306_Irq_ClearPendingEvent(void);

#endif /* USER_DEVICE_IP5306_IRQ_H */
