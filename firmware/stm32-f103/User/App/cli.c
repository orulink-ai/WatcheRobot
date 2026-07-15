#include "cli.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_config.h"
#include "bmi160.h"
#include "ip5306_irq.h"
#include "ip5306_key.h"
#include "platform_uart.h"
#include "qmc6309.h"
#include "sensors.h"
#include "touch_sensor.h"

static uint8_t s_cmdBuffer[APP_CLI_CMD_BUFFER_SIZE];
static uint8_t s_cmdIndex;

static const char *Cli_SkipLeadingSpaces(const char *text);
static void Cli_CopyTrimmedCommand(const char *src, char *dst, size_t dstSize);
static uint8_t Cli_TryHandleServoOffCommand(const char *cmd);
static uint8_t Cli_TryHandleServoRecipCommand(const char *cmd);
static uint8_t Cli_TryHandleServoTimeCommand(const char *cmd);
static uint8_t Cli_TryHandleServoStopCommand(const char *cmd);
static uint8_t Cli_TryHandleServoLimitCommand(const char *cmd);
static uint8_t Cli_TryHandleServoStatusCommand(const char *cmd);
static uint8_t Cli_TryHandleServoCommand(const char *cmd);
static uint8_t Cli_TryHandleServoFeedbackCommand(const char *cmd);
static uint8_t Cli_TryHandleServoCalCommand(const char *cmd);
static uint8_t Cli_TryHandleServoAngleCommand(const char *cmd);
static uint8_t Cli_TryHandleWs2812Command(const char *cmd);
static uint8_t Cli_TryHandleBottomIrTestCommand(const char *cmd);
static void Cli_PrintQmc6309Diagnostics(void);
static void Cli_PrintServoTimeUsage(void);
static uint8_t Cli_ParseServoProfile(const char *text, Servo_MotionProfileTypeDef *profile);
static const char *Cli_ServoProfileToString(Servo_MotionProfileTypeDef profile);

void Cli_ProcessInput(void)
{
    uint8_t byte;

    /*
     * 一次调用内尽量把串口接收缓冲里的字节全部读完。
     * 否则主循环若带固定延时，整行命令会因为读取过慢而发生丢字节。
     */
    while (Platform_Uart_TryReadByte(&byte) != 0U) {
        if ((byte == '\b') || (byte == 127U)) {
            if (s_cmdIndex > 0U) {
                s_cmdIndex--;
                Platform_Uart_Print("\b \b");
            }
            continue;
        }

        if ((byte == '\r') || (byte == '\n')) {
            if (s_cmdIndex > 0U) {
                s_cmdBuffer[s_cmdIndex] = '\0';
                Platform_Uart_Print("\r\n");
                Cli_ExecuteCommand((const char *)s_cmdBuffer);
                s_cmdIndex = 0U;
            }
            continue;
        }

        if ((s_cmdIndex < (APP_CLI_CMD_BUFFER_SIZE - 1U)) && (byte >= 32U) && (byte < 127U)) {
            char echo[2] = {(char)byte, '\0'};

            s_cmdBuffer[s_cmdIndex++] = byte;
            Platform_Uart_Print(echo);
        }
    }
}

void Cli_ExecuteCommand(const char *cmd)
{
    char normalizedCmd[APP_CLI_CMD_BUFFER_SIZE];
    const char *trimmedCmd;
#if (APP_BMI160_ENABLE != 0U)
    BMI160_DataTypeDef bmi160;
#endif
    QMC6309_DataTypeDef qmc6309 = {0};

    Cli_CopyTrimmedCommand(cmd, normalizedCmd, sizeof(normalizedCmd));
    trimmedCmd = normalizedCmd;

    if ((strcmp(trimmedCmd, "help") == 0) || (strcmp(trimmedCmd, "?") == 0)) {
        Platform_Uart_Print("========== Available Commands ==========\r\n");
        Platform_Uart_Print("  help, ?     - Show this help\r\n");
        Platform_Uart_Print("  servo <angle> - Set servo #1 angle (0~180)\r\n");
        Platform_Uart_Print("  servo <id> <angle> - Set servo #<id> angle (id=1/2)\r\n");
        Platform_Uart_Print("  servo_move_time <id> <angle> <ms> [steps] [linear|ease|anti_drop] [ease_strength] - Timed us interpolation\r\n");
        Platform_Uart_Print("  servo_time <id> <angle> <ms> [steps] [linear|ease|anti_drop] [ease_strength] - Alias of servo_move_time\r\n");
        Platform_Uart_Print("  servo_stop [id] - Stop motion and hold current PWM\r\n");
        Platform_Uart_Print("  servo_limit <id> <min> <max> - Set runtime angle limit\r\n");
        Platform_Uart_Print("  servo_limits - Show runtime angle limits\r\n");
        Platform_Uart_Print("  servo_status [id] - Show command/motion state\r\n");
        Platform_Uart_Print("  servo_off [id] - Release servo PWM output\r\n");
        Platform_Uart_Print("  servo_recip [id] - Start servo reciprocating motion\r\n");
        Platform_Uart_Print("  servo_fb    - Read servo feedback ADC raw/mV\r\n");
        Platform_Uart_Print("  servo_cal [id] [samples] - Sample servo feedback raw min/max/avg\r\n");
        Platform_Uart_Print("  servo_angle [id] - Read estimated servo angle from feedback\r\n");
        Platform_Uart_Print("  ws red|green|blue|white|off - Control side WS2812\r\n");
        Platform_Uart_Print("  ws rgb <r> <g> <b> - Set side WS2812 RGB (0~255)\r\n");
        Platform_Uart_Print("  ws count <n> - Set active side WS2812 LED count\r\n");
        Platform_Uart_Print("  ws breathe red|green|blue|white|rgb <r> <g> <b>\r\n");
        Platform_Uart_Print("  ws rainbow|cyber - Start side cyber scan effect\r\n");
        Platform_Uart_Print("  ws stop     - Stop side WS2812 dynamic effect\r\n");
        Platform_Uart_Print("  ws_bottom red|green|blue|white|off - Control bottom WS2812 (PB10)\r\n");
        Platform_Uart_Print("  ws_bottom rgb <r> <g> <b> - Set bottom WS2812 RGB (0~255)\r\n");
        Platform_Uart_Print("  ws_bottom count <n> - Set active bottom WS2812 LED count\r\n");
        Platform_Uart_Print("  ws_bottom breathe red|green|blue|white|rgb <r> <g> <b>\r\n");
        Platform_Uart_Print("  ws_bottom rainbow|cyber - Start bottom cyber scan effect\r\n");
        Platform_Uart_Print("  ws_bottom stop - Stop bottom WS2812 dynamic effect\r\n");
        Platform_Uart_Print("  ws_bottom test - Run bottom WS2812 color test (PB10)\r\n");
        Platform_Uart_Print("  bottom_light_test - Alias of ws_bottom test\r\n");
        Platform_Uart_Print("  scan        - Scan I2C bus\r\n");
        Platform_Uart_Print("  scan_all    - Scan all I2C addresses\r\n");
        Platform_Uart_Print("  i2c_diag    - Print I2C bus diagnostics\r\n");
        Platform_Uart_Print("  i2c_recover - Recover stuck I2C bus\r\n");
        Platform_Uart_Print("  i2c_pin_test - Test PB6/PB7 GPIO levels\r\n");
        Platform_Uart_Print("  bmi160      - Read BMI160 (Gyro+Acc)\r\n");
        Platform_Uart_Print("  qmc6309, mag - Read QMC6309 (Mag)\r\n");
        Platform_Uart_Print("  qmc6309_status - Read QMC6309 status/config\r\n");
        Platform_Uart_Print("  bottom_ir_test - Read bottom IR ADC raw/threshold (PB0)\r\n");
        Platform_Uart_Print("  bottom_ir   - Alias of bottom_ir_test\r\n");
        Platform_Uart_Print("  touch       - Read touch sensor (PA4)\r\n");
        Platform_Uart_Print("  ip5306_irq  - Read IP5306 IRQ status (PA1)\r\n");
        Platform_Uart_Print("  ip5306_irq_clear - Clear pending IRQ flag\r\n");
        Platform_Uart_Print("  ip5306_on   - Short press IP5306 KEY\r\n");
        Platform_Uart_Print("  ip5306_off  - Alias of ip5306_double\r\n");
        Platform_Uart_Print("  ip5306_long - Long press IP5306 KEY\r\n");
        Platform_Uart_Print("  ip5306_double - Double short press IP5306 KEY\r\n");
        Platform_Uart_Print("  ip5306_hold - Hold NMOS gate HIGH for 5s test\r\n");
        Platform_Uart_Print("  all         - Read all sensors\r\n");
        Platform_Uart_Print("======================================\r\n");
    } else if (Cli_TryHandleServoOffCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoRecipCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoTimeCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoStopCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoLimitCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoStatusCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoFeedbackCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoCalCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoAngleCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleServoCommand(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleWs2812Command(trimmedCmd) != 0U) {
        return;
    } else if (Cli_TryHandleBottomIrTestCommand(trimmedCmd) != 0U) {
        return;
    } else if (strcmp(trimmedCmd, "bottom_light_test") == 0) {
        if (App_RunBottomLightTest() == 0U) {
            Platform_Uart_Print("Bottom light test failed\r\n");
            return;
        }
        Platform_Uart_Print("========== Bottom Light ==========\r\n");
        Platform_Uart_Print("  Command: bottom_light_test\r\n");
        Platform_Uart_Print("  Pin    : PB10 / TIM2_CH3\r\n");
        Platform_Uart_Print("  Status : OK\r\n");
        Platform_Uart_Print("==================================\r\n");
    } else if (strcmp(trimmedCmd, "scan") == 0) {
        Sensors_Scan();
    } else if (strcmp(trimmedCmd, "scan_all") == 0) {
        Sensors_ScanAllI2cAddresses();
    } else if (strcmp(trimmedCmd, "i2c_diag") == 0) {
        Sensors_PrintI2cDiagnostics();
    } else if (strcmp(trimmedCmd, "i2c_recover") == 0) {
        Sensors_RecoverI2cBus();
    } else if (strcmp(trimmedCmd, "i2c_pin_test") == 0) {
        Sensors_TestI2cPins();
    } else if (strcmp(trimmedCmd, "bmi160") == 0) {
#if (APP_BMI160_ENABLE != 0U)
        BMI160_ReadData(&bmi160);
        Platform_Uart_Print("========== BMI160 Data ==========\r\n");
        Platform_Uart_Printf("  Gyro X: %d\r\n", bmi160.gyroX);
        Platform_Uart_Printf("  Gyro Y: %d\r\n", bmi160.gyroY);
        Platform_Uart_Printf("  Gyro Z: %d\r\n", bmi160.gyroZ);
        Platform_Uart_Printf("  Acc  X: %d\r\n", bmi160.accX);
        Platform_Uart_Printf("  Acc  Y: %d\r\n", bmi160.accY);
        Platform_Uart_Printf("  Acc  Z: %d\r\n", bmi160.accZ);
        Platform_Uart_Print("=================================\r\n");
#else
        Platform_Uart_Print("BMI160 disabled in app_config.h\r\n");
#endif
    } else if (strcmp(trimmedCmd, "qmc6309_status") == 0) {
        Cli_PrintQmc6309Diagnostics();
    } else if ((strcmp(trimmedCmd, "qmc6309") == 0) || (strcmp(trimmedCmd, "mag") == 0) ||
               (strcmp(trimmedCmd, "bmm150") == 0)) {
        Platform_Uart_Print("========== QMC6309 Data ==========\r\n");
        if (QMC6309_ReadData(&qmc6309) != 0U) {
            Platform_Uart_Printf("  Mag X: %d\r\n", qmc6309.magX);
            Platform_Uart_Printf("  Mag Y: %d\r\n", qmc6309.magY);
            Platform_Uart_Printf("  Mag Z: %d\r\n", qmc6309.magZ);
        } else {
            Platform_Uart_Print("  Read failed or data not ready\r\n");
            Cli_PrintQmc6309Diagnostics();
        }
        Platform_Uart_Print("=================================\r\n");
    } else if (strcmp(trimmedCmd, "all") == 0) {
#if (APP_BMI160_ENABLE != 0U)
        BMI160_ReadData(&bmi160);
#endif
        Platform_Uart_Print("========== All Sensors Data ==========\r\n");
#if (APP_BMI160_ENABLE != 0U)
        Platform_Uart_Print("[BMI160 - Gyroscope]\r\n");
        Platform_Uart_Printf("  X: %d  Y: %d  Z: %d\r\n", bmi160.gyroX, bmi160.gyroY, bmi160.gyroZ);
        Platform_Uart_Print("[BMI160 - Accelerometer]\r\n");
        Platform_Uart_Printf("  X: %d  Y: %d  Z: %d\r\n", bmi160.accX, bmi160.accY, bmi160.accZ);
#else
        Platform_Uart_Print("[BMI160]\r\n");
        Platform_Uart_Print("  Disabled\r\n");
#endif
        Platform_Uart_Print("[QMC6309 - Magnetometer]\r\n");
        if (QMC6309_ReadData(&qmc6309) != 0U) {
            Platform_Uart_Printf("  X: %d  Y: %d  Z: %d\r\n", qmc6309.magX, qmc6309.magY, qmc6309.magZ);
        } else {
            Platform_Uart_Print("  Read failed or data not ready\r\n");
            Cli_PrintQmc6309Diagnostics();
        }
        Platform_Uart_Print("======================================\r\n");
    } else if (strcmp(trimmedCmd, "touch") == 0) {
        uint8_t state = TouchSensor_Read();

        Platform_Uart_Print("========== Touch Sensor ==========\r\n");
        Platform_Uart_Printf("  PA4 State: %s\r\n", state ? "HIGH (Not Touched)" : "LOW (Touched)");
        Platform_Uart_Printf("  Raw Value: %d\r\n", state);
        Platform_Uart_Print("=================================\r\n");
    } else if ((strcmp(trimmedCmd, "ip5306_irq") == 0) || (strcmp(trimmedCmd, "ip5306irq") == 0) ||
               (strcmp(trimmedCmd, "ip530_irq") == 0) || (strcmp(trimmedCmd, "ip530irq") == 0)) {
        uint8_t state = IP5306_Irq_Read();
        uint8_t hasPendingEvent = IP5306_Irq_HasPendingEvent();
        uint32_t eventCount = IP5306_Irq_GetEventCount();
        uint32_t lastEventTickMs = IP5306_Irq_GetLastEventTickMs();

        Platform_Uart_Print("========== IP5306 IRQ ==========\r\n");
        Platform_Uart_Printf("  PA1 State: %s\r\n", state ? "HIGH" : "LOW");
        Platform_Uart_Printf("  Raw Value: %d\r\n", state);
        Platform_Uart_Printf("  Pending Event: %s\r\n", hasPendingEvent ? "YES" : "NO");
        Platform_Uart_Printf("  Event Count: %lu\r\n", (unsigned long)eventCount);
        Platform_Uart_Printf("  Last Tick: %lu ms\r\n", (unsigned long)lastEventTickMs);
        Platform_Uart_Print("================================\r\n");
    } else if ((strcmp(trimmedCmd, "ip5306_irq_clear") == 0) || (strcmp(trimmedCmd, "ip5306irqclear") == 0) ||
               (strcmp(trimmedCmd, "ip530_irq_clear") == 0) || (strcmp(trimmedCmd, "ip530irqclear") == 0)) {
        IP5306_Irq_ClearPendingEvent();

        Platform_Uart_Print("========== IP5306 IRQ ==========\r\n");
        Platform_Uart_Print("  Action: Pending IRQ flag cleared\r\n");
        Platform_Uart_Print("================================\r\n");
    } else if ((strcmp(trimmedCmd, "ip5306_on") == 0) || (strcmp(trimmedCmd, "ip5306on") == 0) ||
               (strcmp(trimmedCmd, "ip530_on") == 0) || (strcmp(trimmedCmd, "ip530on") == 0) ||
               (strcmp(trimmedCmd, "power on") == 0)) {
        Platform_Uart_Print("========== IP5306 KEY ==========\r\n");
        Platform_Uart_Print("  Action: Drive NMOS gate HIGH for 100ms\r\n");
        Platform_Uart_Print("  Expect: Short-press behavior defined by your board's IP5306 variant\r\n");
        Platform_Uart_Print("================================\r\n");
        App_SetPower5V(1U, 0U);
    } else if ((strcmp(trimmedCmd, "ip5306_long") == 0) || (strcmp(trimmedCmd, "ip5306long") == 0) ||
               (strcmp(trimmedCmd, "ip530_long") == 0) || (strcmp(trimmedCmd, "ip530long") == 0)) {
        Platform_Uart_Print("========== IP5306 KEY ==========\r\n");
        Platform_Uart_Print("  Action: Drive NMOS gate HIGH for 2500ms\r\n");
        Platform_Uart_Print("  Expect: Long-press behavior defined by your board's IP5306 variant\r\n");
        Platform_Uart_Print("================================\r\n");
        IP5306_Key_LongPress();
    } else if ((strcmp(trimmedCmd, "ip5306_double") == 0) || (strcmp(trimmedCmd, "ip5306double") == 0) ||
               (strcmp(trimmedCmd, "ip530_double") == 0) || (strcmp(trimmedCmd, "ip530double") == 0) ||
               (strcmp(trimmedCmd, "ip5306_off") == 0) || (strcmp(trimmedCmd, "ip5306off") == 0) ||
               (strcmp(trimmedCmd, "ip530_off") == 0) || (strcmp(trimmedCmd, "ip530off") == 0) ||
               (strcmp(trimmedCmd, "power off") == 0)) {
        Platform_Uart_Print("========== IP5306 KEY ==========\r\n");
        Platform_Uart_Print("  Action: Drive NMOS gate HIGH twice for 100ms\r\n");
        Platform_Uart_Print("  Gap   : 200ms between the two short presses\r\n");
        Platform_Uart_Print("  Expect: Double-short-press behavior defined by your board's IP5306 variant\r\n");
        Platform_Uart_Print("================================\r\n");
        App_SetPower5V(0U, 0U);
    } else if ((strcmp(trimmedCmd, "ip5306_hold") == 0) || (strcmp(trimmedCmd, "ip5306hold") == 0) ||
               (strcmp(trimmedCmd, "ip530_hold") == 0) || (strcmp(trimmedCmd, "ip530hold") == 0)) {
        Platform_Uart_Print("========== IP5306 KEY ==========\r\n");
        Platform_Uart_Print("  Action: Drive NMOS gate HIGH for 5000ms\r\n");
        Platform_Uart_Print("  Use: Measure PB12 gate or KEY net with multimeter\r\n");
        Platform_Uart_Print("================================\r\n");
        IP5306_Key_TestHold();
    } else if (strlen(trimmedCmd) > 0U) {
        Platform_Uart_Printf("Unknown command: %s\r\n", trimmedCmd);
        Platform_Uart_Print("Type 'help' for available commands\r\n");
    }
}

static void Cli_PrintQmc6309Diagnostics(void)
{
    QMC6309_DiagnosticsTypeDef diagnostics;

    Platform_Uart_Print("========== QMC6309 Status ==========\r\n");
    if (QMC6309_ReadDiagnostics(&diagnostics) == 0U) {
        Platform_Uart_Print("  Diagnostic read failed\r\n");
        Platform_Uart_Print("====================================\r\n");
        return;
    }

    Platform_Uart_Printf("  CHIP_ID : 0x%02X\r\n", diagnostics.chipId);
    Platform_Uart_Printf("  STATUS  : 0x%02X\r\n", diagnostics.status);
    Platform_Uart_Printf("  CTRL1   : 0x%02X\r\n", diagnostics.ctrl1);
    Platform_Uart_Printf("  CTRL2   : 0x%02X\r\n", diagnostics.ctrl2);
    Platform_Uart_Printf("  DRDY    : %s\r\n", ((diagnostics.status & 0x01U) != 0U) ? "YES" : "NO");
    Platform_Uart_Printf("  OVFL    : %s\r\n", ((diagnostics.status & 0x02U) != 0U) ? "YES" : "NO");
    Platform_Uart_Print("====================================\r\n");
}

static const char *Cli_SkipLeadingSpaces(const char *text)
{
    while ((*text == ' ') || (*text == '\t')) {
        text++;
    }

    return text;
}

static void Cli_CopyTrimmedCommand(const char *src, char *dst, size_t dstSize)
{
    const char *start = Cli_SkipLeadingSpaces(src);
    size_t length = strlen(start);

    while ((length > 0U) && ((start[length - 1U] == ' ') || (start[length - 1U] == '\t'))) {
        length--;
    }

    if (length >= (dstSize - 1U)) {
        length = dstSize - 1U;
    }

    memcpy(dst, start, length);
    dst[length] = '\0';
}

static uint8_t Cli_ParseServoProfile(const char *text, Servo_MotionProfileTypeDef *profile)
{
    if ((text == NULL) || (profile == NULL)) {
        return 0U;
    }

    if ((strcmp(text, "linear") == 0) || (strcmp(text, "lin") == 0)) {
        *profile = SERVO_MOTION_PROFILE_LINEAR;
        return 1U;
    }

    if ((strcmp(text, "ease") == 0) || (strcmp(text, "ease_in_out") == 0)) {
        *profile = SERVO_MOTION_PROFILE_EASE_IN_OUT;
        return 1U;
    }

    if ((strcmp(text, "anti_drop") == 0) || (strcmp(text, "antidrop") == 0) || (strcmp(text, "soft_down") == 0)) {
        *profile = SERVO_MOTION_PROFILE_ANTI_DROP;
        return 1U;
    }

    return 0U;
}

static void Cli_PrintServoTimeUsage(void)
{
    Platform_Uart_Print("Use: servo_move_time <id> <angle> <ms> [steps] [linear|ease|anti_drop] [ease_strength]\r\n");
    Platform_Uart_Print("     servo_time is accepted as an alias\r\n");
}

static const char *Cli_ServoProfileToString(Servo_MotionProfileTypeDef profile)
{
    if (profile == SERVO_MOTION_PROFILE_ANTI_DROP) {
        return "anti_drop";
    }
    return (profile == SERVO_MOTION_PROFILE_EASE_IN_OUT) ? "ease" : "linear";
}

static uint8_t Cli_CommandTokenMatches(const char *cmd, const char *token)
{
    size_t tokenLength;

    if ((cmd == NULL) || (token == NULL)) {
        return 0U;
    }

    tokenLength = strlen(token);
    if (strncmp(cmd, token, tokenLength) != 0) {
        return 0U;
    }

    return ((cmd[tokenLength] == '\0') || (cmd[tokenLength] == ' ') || (cmd[tokenLength] == '\t')) ? 1U : 0U;
}

static uint8_t Cli_TryHandleServoCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    unsigned long angle;
    uint16_t pulseUs;

    if (Cli_CommandTokenMatches(cursor, "servo") == 0U) {
        return 0U;
    }

    cursor += 5;
    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor == '\0') {
        Platform_Uart_Print("Use: servo <angle> or servo <id> <angle>\r\n");
        return 1U;
    }

    angle = strtoul(cursor, &endPtr, 10);
    if (endPtr == cursor) {
        Platform_Uart_Print("Use: servo <angle> or servo <id> <angle>\r\n");
        return 1U;
    }

    while (*endPtr == ' ') {
        endPtr++;
    }

    if (*endPtr != '\0') {
        servoIndex = angle;
        cursor = endPtr;
        angle = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo <angle> or servo <id> <angle>\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL) || (angle > 180UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2, angle must be 0~180\r\n");
        return 1U;
    }

    if (App_SetServoAngle((uint8_t)servoIndex, (uint8_t)angle, &pulseUs) == 0U) {
        Platform_Uart_Print("Failed to set servo angle\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Printf("  Angle : %lu deg\r\n", angle);
    Platform_Uart_Printf("  Pulse : %u us\r\n", pulseUs);
    Platform_Uart_Print("  Signal: ACTIVE (manual release required)\r\n");
    Platform_Uart_Print("================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleServoTimeCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    const char *commandName;
    size_t commandLength;
    unsigned long servoIndex;
    unsigned long angle;
    unsigned long durationMs;
    unsigned long requestedSteps = 0UL;
    unsigned long easeStrengthPercent = SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT;
    uint8_t stepsProvided = 0U;
    uint16_t targetPulseUs;
    uint16_t updatePeriodMs;
    uint32_t effectiveSteps;
    Servo_StateTypeDef state;
    Servo_MotionProfileTypeDef profile = SERVO_MOTION_PROFILE_EASE_IN_OUT;
    char profileText[16];
    size_t profileLength;

    if (Cli_CommandTokenMatches(cursor, "servo_move_time") != 0U) {
        commandName = "servo_move_time";
    } else if (Cli_CommandTokenMatches(cursor, "servo_time") != 0U) {
        commandName = "servo_time";
    } else {
        return 0U;
    }

    commandLength = strlen(commandName);
    cursor += commandLength;
    while (*cursor == ' ') {
        cursor++;
    }

    servoIndex = strtoul(cursor, &endPtr, 10);
    if (endPtr == cursor) {
        Cli_PrintServoTimeUsage();
        return 1U;
    }

    cursor = endPtr;
    while (*cursor == ' ') {
        cursor++;
    }
    angle = strtoul(cursor, &endPtr, 10);
    if (endPtr == cursor) {
        Cli_PrintServoTimeUsage();
        return 1U;
    }

    cursor = endPtr;
    while (*cursor == ' ') {
        cursor++;
    }
    durationMs = strtoul(cursor, &endPtr, 10);
    if (endPtr == cursor) {
        Cli_PrintServoTimeUsage();
        return 1U;
    }

    cursor = endPtr;
    while (*cursor == ' ') {
        cursor++;
    }
    if ((*cursor >= '0') && (*cursor <= '9')) {
        requestedSteps = strtoul(cursor, &endPtr, 10);
        if (endPtr == cursor) {
            Cli_PrintServoTimeUsage();
            return 1U;
        }
        stepsProvided = 1U;
        cursor = endPtr;
        while (*cursor == ' ') {
            cursor++;
        }
    }
    if (*cursor != '\0') {
        profileLength = 0U;
        while ((cursor[profileLength] != '\0') && (cursor[profileLength] != ' ') && (cursor[profileLength] != '\t')) {
            profileLength++;
        }
        if ((profileLength == 0U) || (profileLength >= sizeof(profileText))) {
            Platform_Uart_Print("Profile must be linear, ease or anti_drop\r\n");
            return 1U;
        }
        memcpy(profileText, cursor, profileLength);
        profileText[profileLength] = '\0';
        if (Cli_ParseServoProfile(profileText, &profile) == 0U) {
            Platform_Uart_Print("Profile must be linear, ease or anti_drop\r\n");
            return 1U;
        }
        cursor += profileLength;
        while (*cursor == ' ') {
            cursor++;
        }
    }

    if (*cursor != '\0') {
        easeStrengthPercent = strtoul(cursor, &endPtr, 10);
        if (endPtr == cursor) {
            Cli_PrintServoTimeUsage();
            return 1U;
        }
        cursor = endPtr;
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor != '\0') {
            Cli_PrintServoTimeUsage();
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL) || (angle > 180UL) ||
        (durationMs == 0UL) || (durationMs > 60000UL) ||
        ((stepsProvided != 0U) && ((requestedSteps == 0UL) || (requestedSteps > 3000UL))) ||
        (easeStrengthPercent > SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT)) {
        Platform_Uart_Print("Servo id must be 1 or 2, angle 0~180, duration 1~60000ms, steps 1~3000, ease_strength 0~100\r\n");
        return 1U;
    }

    if (App_MoveServoToAngleOverTimeWithStepsAndEaseStrength((uint8_t)servoIndex,
                                                             (uint8_t)angle,
                                                             (uint16_t)durationMs,
                                                             (uint16_t)requestedSteps,
                                                             profile,
                                                             (uint8_t)easeStrengthPercent,
                                                             &targetPulseUs,
                                                             &updatePeriodMs) == 0U) {
        Platform_Uart_Print("Failed to start timed servo motion\r\n");
        return 1U;
    }

    if (App_GetServoState((uint8_t)servoIndex, &state) == 0U) {
        Platform_Uart_Print("Failed to read timed servo motion state\r\n");
        return 1U;
    }
    effectiveSteps = (updatePeriodMs == 0U) ? 0UL : (((uint32_t)durationMs + (uint32_t)updatePeriodMs - 1UL) /
                                                     (uint32_t)updatePeriodMs);

    Platform_Uart_Print("========== Servo Motion ==========\r\n");
    Platform_Uart_Printf("  Servo   : %lu\r\n", servoIndex);
    Platform_Uart_Printf("  Command : %s\r\n", commandName);
    Platform_Uart_Printf("  Target  : %lu deg\r\n", angle);
    Platform_Uart_Printf("  Duration: %lu ms\r\n", durationMs);
    Platform_Uart_Printf("  Steps req: %lu\r\n", requestedSteps);
    Platform_Uart_Printf("  Update  : %u ms (~%lu effective steps)\r\n",
                         state.motionUpdatePeriodMs,
                         (unsigned long)effectiveSteps);
    Platform_Uart_Printf("  Profile : %s\r\n", Cli_ServoProfileToString(profile));
    Platform_Uart_Printf("  Ease    : %lu%%\r\n", easeStrengthPercent);
    Platform_Uart_Printf("  Start pulse : %u us\r\n", state.motionStartPulse);
    Platform_Uart_Printf("  Target pulse: %u us\r\n", targetPulseUs);
    Platform_Uart_Print("  Source  : command pulse, us interpolation (no feedback)\r\n");
    Platform_Uart_Print("  Status  : MOVING\r\n");
    Platform_Uart_Print("==================================\r\n");
    return 1U;
}

static uint8_t Cli_TryHandleServoStopCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    Servo_StateTypeDef state;

    if (strncmp(cursor, "servo_stop", 10U) != 0) {
        return 0U;
    }

    cursor += 10;
    while (*cursor == ' ') {
        cursor++;
    }
    if (*cursor != '\0') {
        servoIndex = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo_stop [id]\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2\r\n");
        return 1U;
    }

    if ((App_StopServoMotion((uint8_t)servoIndex) == 0U) ||
        (App_GetServoState((uint8_t)servoIndex, &state) == 0U)) {
        Platform_Uart_Print("Failed to stop servo motion\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo Motion ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Print("  Action: Stop and hold\r\n");
    Platform_Uart_Printf("  Pulse : %u us\r\n", state.currentPulse);
    Platform_Uart_Printf("  Signal: %s\r\n", state.isSignalActive ? "ACTIVE" : "RELEASED");
    Platform_Uart_Print("==================================\r\n");
    return 1U;
}

static uint8_t Cli_TryHandleServoLimitCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex;
    unsigned long minAngle;
    unsigned long maxAngle;
    uint8_t servo1Min;
    uint8_t servo1Max;
    uint8_t servo2Min;
    uint8_t servo2Max;

    if (strcmp(cursor, "servo_limits") == 0) {
        if ((App_GetServoLimit(1U, &servo1Min, &servo1Max) == 0U) ||
            (App_GetServoLimit(2U, &servo2Min, &servo2Max) == 0U)) {
            Platform_Uart_Print("Failed to read servo limits\r\n");
            return 1U;
        }
        Platform_Uart_Print("========== Servo Limits ==========\r\n");
        Platform_Uart_Printf("  Servo1: %u~%u deg\r\n", servo1Min, servo1Max);
        Platform_Uart_Printf("  Servo2: %u~%u deg\r\n", servo2Min, servo2Max);
        Platform_Uart_Print("==================================\r\n");
        return 1U;
    }

    if (strncmp(cursor, "servo_limit", 11U) != 0) {
        return 0U;
    }

    cursor += 11;
    while (*cursor == ' ') {
        cursor++;
    }
    servoIndex = strtoul(cursor, &endPtr, 10);
    if (endPtr == cursor) {
        Platform_Uart_Print("Use: servo_limit <id> <min> <max>\r\n");
        return 1U;
    }

    cursor = endPtr;
    while (*cursor == ' ') {
        cursor++;
    }
    minAngle = strtoul(cursor, &endPtr, 10);
    if (endPtr == cursor) {
        Platform_Uart_Print("Use: servo_limit <id> <min> <max>\r\n");
        return 1U;
    }

    cursor = endPtr;
    while (*cursor == ' ') {
        cursor++;
    }
    maxAngle = strtoul(cursor, &endPtr, 10);
    if ((endPtr == cursor) || (*endPtr != '\0')) {
        Platform_Uart_Print("Use: servo_limit <id> <min> <max>\r\n");
        return 1U;
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL) || (minAngle > maxAngle) || (maxAngle > 180UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2, limits must be 0~180 and min<=max\r\n");
        return 1U;
    }

    if (App_SetServoLimit((uint8_t)servoIndex, (uint8_t)minAngle, (uint8_t)maxAngle) == 0U) {
        Platform_Uart_Print("Failed to set servo limit\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo Limits ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Printf("  Limit : %lu~%lu deg\r\n", minAngle, maxAngle);
    Platform_Uart_Print("  Status: OK\r\n");
    Platform_Uart_Print("==================================\r\n");
    return 1U;
}

static uint8_t Cli_TryHandleServoStatusCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    Servo_StateTypeDef state;
    App_ServoFeedbackSnapshotTypeDef snapshot;
    uint8_t minAngle;
    uint8_t maxAngle;
    uint8_t hasFeedback;

    if (strncmp(cursor, "servo_status", 12U) != 0) {
        return 0U;
    }

    cursor += 12;
    while (*cursor == ' ') {
        cursor++;
    }
    if (*cursor != '\0') {
        servoIndex = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo_status [id]\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL) ||
        (App_GetServoState((uint8_t)servoIndex, &state) == 0U) ||
        (App_GetServoLimit((uint8_t)servoIndex, &minAngle, &maxAngle) == 0U)) {
        Platform_Uart_Print("Servo id must be 1 or 2\r\n");
        return 1U;
    }
    hasFeedback = App_GetServoFeedbackSnapshot((uint8_t)servoIndex, &snapshot);

    Platform_Uart_Print("========== Servo Status ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Printf("  Pulse : %u us\r\n", state.currentPulse);
    Platform_Uart_Printf("  Target: %u us\r\n", state.targetPulse);
    Platform_Uart_Printf("  Motion: %s\r\n", state.isMotionActive ? "MOVING" : "IDLE");
    Platform_Uart_Printf("  Profile: %s\r\n", Cli_ServoProfileToString(state.motionProfile));
    Platform_Uart_Printf("  Ease  : %u%%\r\n", state.motionEaseStrengthPercent);
    Platform_Uart_Printf("  Update : %u ms\r\n", state.motionUpdatePeriodMs);
    Platform_Uart_Printf("  Limit : %u~%u deg\r\n", minAngle, maxAngle);
    Platform_Uart_Printf("  Signal: %s\r\n", state.isSignalActive ? "ACTIVE" : "RELEASED");
    if ((hasFeedback != 0U) && (snapshot.feedbackValid != 0U)) {
        Platform_Uart_Print("  Feedback: VALID\r\n");
        Platform_Uart_Printf("  Command Angle : %u deg\r\n", snapshot.commandAngle);
        Platform_Uart_Printf("  Feedback Raw  : %u\r\n", snapshot.feedbackRaw);
        Platform_Uart_Printf("  Feedback V    : %u mV\r\n", snapshot.feedbackMv);
        Platform_Uart_Printf("  Feedback Angle: %u deg\r\n", snapshot.feedbackAngle);
    } else {
        Platform_Uart_Print("  Feedback: INVALID\r\n");
    }
    Platform_Uart_Print("==================================\r\n");
    return 1U;
}

static uint8_t Cli_TryHandleServoOffCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    Servo_StateTypeDef state;

    if (strncmp(cursor, "servo_off", 9U) != 0) {
        return 0U;
    }

    cursor += 9;
    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor != '\0') {
        servoIndex = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo_off [id]\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2\r\n");
        return 1U;
    }

    if (App_ReleaseServo((uint8_t)servoIndex) == 0U) {
        Platform_Uart_Print("Failed to release servo PWM\r\n");
        return 1U;
    }

    if (App_GetServoState((uint8_t)servoIndex, &state) == 0U) {
        Platform_Uart_Print("Failed to read servo state\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Print("  Action: Release PWM output\r\n");
    Platform_Uart_Printf("  Pulse : %u us\r\n", state.currentPulse);
    Platform_Uart_Printf("  Signal: %s\r\n", state.isSignalActive ? "ACTIVE" : "RELEASED");
    Platform_Uart_Print("================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleServoRecipCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    Servo_StateTypeDef state;

    if (strncmp(cursor, "servo_recip", 11U) != 0) {
        return 0U;
    }

    cursor += 11;
    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor != '\0') {
        servoIndex = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo_recip [id]\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2\r\n");
        return 1U;
    }

    if (App_StartServoReciprocating((uint8_t)servoIndex) == 0U) {
        Platform_Uart_Print("Failed to start servo reciprocating\r\n");
        return 1U;
    }

    if (App_GetServoState((uint8_t)servoIndex, &state) == 0U) {
        Platform_Uart_Print("Failed to read servo state\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Print("  Action: Start reciprocating motion\r\n");
    Platform_Uart_Printf("  Pulse : %u us\r\n", state.currentPulse);
    Platform_Uart_Printf("  Signal: %s\r\n", state.isSignalActive ? "ACTIVE" : "RELEASED");
    Platform_Uart_Print("================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleWs2812Command(const char *cmd)
{
    const char *cursor = cmd;
    const char *commandPrefix;
    const char *targetName;
    char *endPtr;
    unsigned long ledCount;
    unsigned long red;
    unsigned long green;
    unsigned long blue;
    uint8_t isBottom = 0U;
    uint8_t result = 0U;

    if ((strncmp(cursor, "ws_bottom", 9U) == 0) && ((cursor[9] == ' ') || (cursor[9] == '\0'))) {
        cursor += 9;
        commandPrefix = "ws_bottom";
        targetName = "Bottom WS2812";
        isBottom = 1U;
    } else if ((strncmp(cursor, "ws", 2U) == 0) && ((cursor[2] == ' ') || (cursor[2] == '\0'))) {
        cursor += 2;
        commandPrefix = "ws";
        targetName = "Side WS2812";
    } else {
        return 0U;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor == '\0') {
        Platform_Uart_Printf("Use: %s off | %s red | %s green | %s blue | %s white | %s rainbow|cyber | %s stop | %s count <n> | %s rgb <r> <g> <b> | %s breathe ...\r\n",
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix);
        return 1U;
    }

    if (strcmp(cursor, "off") == 0) {
        result = (isBottom != 0U) ? App_SetBottomWs2812Rgb(0U, 0U, 0U) : App_SetWs2812Rgb(0U, 0U, 0U);
    } else if (strcmp(cursor, "red") == 0) {
        result = (isBottom != 0U) ? App_SetBottomWs2812Rgb(255U, 0U, 0U) : App_SetWs2812Rgb(255U, 0U, 0U);
    } else if (strcmp(cursor, "green") == 0) {
        result = (isBottom != 0U) ? App_SetBottomWs2812Rgb(0U, 255U, 0U) : App_SetWs2812Rgb(0U, 255U, 0U);
    } else if (strcmp(cursor, "blue") == 0) {
        result = (isBottom != 0U) ? App_SetBottomWs2812Rgb(0U, 0U, 255U) : App_SetWs2812Rgb(0U, 0U, 255U);
    } else if (strcmp(cursor, "white") == 0) {
        result = (isBottom != 0U) ? App_SetBottomWs2812Rgb(255U, 255U, 255U) : App_SetWs2812Rgb(255U, 255U, 255U);
    } else if (strcmp(cursor, "stop") == 0) {
        if (isBottom != 0U) {
            App_StopBottomWs2812Effect();
        } else {
            App_StopWs2812Effect();
        }
        result = 1U;
    } else if ((strcmp(cursor, "rainbow") == 0) || (strcmp(cursor, "cyber") == 0)) {
        result = (isBottom != 0U) ? App_StartBottomWs2812Rainbow() : App_StartWs2812Rainbow();
    } else if ((isBottom != 0U) && (strcmp(cursor, "test") == 0)) {
        if (App_RunBottomLightTest() == 0U) {
            Platform_Uart_Print("Bottom light test failed\r\n");
            return 1U;
        }
        Platform_Uart_Print("========== Bottom Light ==========\r\n");
        Platform_Uart_Print("  Command: ws_bottom test\r\n");
        Platform_Uart_Print("  Pin    : PB10 / TIM2_CH3\r\n");
        Platform_Uart_Print("  Status : OK\r\n");
        Platform_Uart_Print("==================================\r\n");
        return 1U;
    } else if (strncmp(cursor, "count ", 6U) == 0) {
        ledCount = strtoul(cursor + 6, &endPtr, 10);
        if ((endPtr == (cursor + 6)) || (*endPtr != '\0')) {
            Platform_Uart_Printf("Use: %s count <n>\r\n", commandPrefix);
            return 1U;
        }
        if ((ledCount == 0UL) ||
            (ledCount > (unsigned long)((isBottom != 0U) ? App_GetBottomWs2812MaxLedCount() : App_GetWs2812MaxLedCount()))) {
            Platform_Uart_Printf("LED count must be 1~%u\r\n",
                                 (isBottom != 0U) ? App_GetBottomWs2812MaxLedCount() : App_GetWs2812MaxLedCount());
            return 1U;
        }
        if (isBottom != 0U) {
            App_StopBottomWs2812Effect();
            result = App_SetBottomWs2812LedCount((uint16_t)ledCount);
        } else {
            App_StopWs2812Effect();
            result = App_SetWs2812LedCount((uint16_t)ledCount);
        }
    } else if (strncmp(cursor, "rgb ", 4U) == 0) {
        cursor += 4;

        red = strtoul(cursor, &endPtr, 10);
        if (endPtr == cursor) {
            Platform_Uart_Printf("Use: %s rgb <r> <g> <b>\r\n", commandPrefix);
            return 1U;
        }

        cursor = endPtr;
        while (*cursor == ' ') {
            cursor++;
        }

        green = strtoul(cursor, &endPtr, 10);
        if (endPtr == cursor) {
            Platform_Uart_Printf("Use: %s rgb <r> <g> <b>\r\n", commandPrefix);
            return 1U;
        }

        cursor = endPtr;
        while (*cursor == ' ') {
            cursor++;
        }

        blue = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0') || (red > 255UL) || (green > 255UL) || (blue > 255UL)) {
            Platform_Uart_Print("RGB must be 0~255\r\n");
            return 1U;
        }

        result = (isBottom != 0U) ? App_SetBottomWs2812Rgb((uint8_t)red, (uint8_t)green, (uint8_t)blue)
                                  : App_SetWs2812Rgb((uint8_t)red, (uint8_t)green, (uint8_t)blue);
    } else if (strncmp(cursor, "breathe ", 8U) == 0) {
        cursor += 8;

        if (strcmp(cursor, "red") == 0) {
            result = (isBottom != 0U) ? App_StartBottomWs2812Breathe(255U, 0U, 0U)
                                      : App_StartWs2812Breathe(255U, 0U, 0U);
        } else if (strcmp(cursor, "green") == 0) {
            result = (isBottom != 0U) ? App_StartBottomWs2812Breathe(0U, 255U, 0U)
                                      : App_StartWs2812Breathe(0U, 255U, 0U);
        } else if (strcmp(cursor, "blue") == 0) {
            result = (isBottom != 0U) ? App_StartBottomWs2812Breathe(0U, 0U, 255U)
                                      : App_StartWs2812Breathe(0U, 0U, 255U);
        } else if (strcmp(cursor, "white") == 0) {
            result = (isBottom != 0U) ? App_StartBottomWs2812Breathe(255U, 255U, 255U)
                                      : App_StartWs2812Breathe(255U, 255U, 255U);
        } else if (strncmp(cursor, "rgb ", 4U) == 0) {
            cursor += 4;

            red = strtoul(cursor, &endPtr, 10);
            if (endPtr == cursor) {
                Platform_Uart_Printf("Use: %s breathe rgb <r> <g> <b>\r\n", commandPrefix);
                return 1U;
            }

            cursor = endPtr;
            while (*cursor == ' ') {
                cursor++;
            }

            green = strtoul(cursor, &endPtr, 10);
            if (endPtr == cursor) {
                Platform_Uart_Printf("Use: %s breathe rgb <r> <g> <b>\r\n", commandPrefix);
                return 1U;
            }

            cursor = endPtr;
            while (*cursor == ' ') {
                cursor++;
            }

            blue = strtoul(cursor, &endPtr, 10);
            if ((endPtr == cursor) || (*endPtr != '\0') || (red > 255UL) || (green > 255UL) || (blue > 255UL)) {
                Platform_Uart_Print("RGB must be 0~255\r\n");
                return 1U;
            }

            result = (isBottom != 0U) ? App_StartBottomWs2812Breathe((uint8_t)red, (uint8_t)green, (uint8_t)blue)
                                      : App_StartWs2812Breathe((uint8_t)red, (uint8_t)green, (uint8_t)blue);
        } else {
            Platform_Uart_Printf("Use: %s breathe red|green|blue|white|rgb <r> <g> <b>\r\n", commandPrefix);
            return 1U;
        }
    } else {
        Platform_Uart_Printf("Use: %s off | %s red | %s green | %s blue | %s white | %s rainbow|cyber | %s stop | %s count <n> | %s rgb <r> <g> <b> | %s breathe ...\r\n",
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix,
                             commandPrefix);
        return 1U;
    }

    if (result == 0U) {
        Platform_Uart_Printf("%s update failed\r\n", targetName);
        return 1U;
    }

    Platform_Uart_Printf("========== %s ==========\r\n", targetName);
    Platform_Uart_Printf("  Command: %s\r\n", cmd);
    Platform_Uart_Print("  Status : OK\r\n");
    Platform_Uart_Print("================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleServoFeedbackCommand(const char *cmd)
{
    uint16_t servo1Raw;
    uint16_t servo2Raw;
    uint8_t hasServo1;
    uint8_t hasServo2;

    if (strcmp(cmd, "servo_fb") != 0) {
        return 0U;
    }

    hasServo1 = App_GetServoFeedbackRaw(1U, &servo1Raw);
    hasServo2 = App_GetServoFeedbackRaw(2U, &servo2Raw);

    if ((hasServo1 == 0U) && (hasServo2 == 0U)) {
        Platform_Uart_Print("Failed to read servo feedback ADC\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo Feedback ==========\r\n");
    if (hasServo1 != 0U) {
        Platform_Uart_Printf("  Servo1 Raw : %u\r\n", servo1Raw);
        Platform_Uart_Printf("  Servo1 V   : %u mV\r\n", App_ConvertServoFeedbackToMv(servo1Raw));
    } else {
        Platform_Uart_Print("  Servo1     : feedback not configured\r\n");
    }

    if (hasServo2 != 0U) {
        Platform_Uart_Printf("  Servo2 Raw : %u\r\n", servo2Raw);
        Platform_Uart_Printf("  Servo2 V   : %u mV\r\n", App_ConvertServoFeedbackToMv(servo2Raw));
    } else {
        Platform_Uart_Print("  Servo2     : feedback not configured\r\n");
    }
    Platform_Uart_Print("====================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleServoCalCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    unsigned long sampleCount = 64UL;
    uint16_t raw;
    uint16_t currentRaw = 0U;
    uint16_t minRaw = APP_SERVO_FEEDBACK_ADC_MAX;
    uint16_t maxRaw = 0U;
    uint32_t sumRaw = 0U;
    unsigned long index;

    if (strncmp(cursor, "servo_cal", 9U) != 0) {
        return 0U;
    }

    cursor += 9;
    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor != '\0') {
        servoIndex = strtoul(cursor, &endPtr, 10);
        if (endPtr == cursor) {
            Platform_Uart_Print("Use: servo_cal [id] [samples]\r\n");
            return 1U;
        }
        cursor = endPtr;
        while (*cursor == ' ') {
            cursor++;
        }
    }

    if (*cursor != '\0') {
        sampleCount = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo_cal [id] [samples]\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2\r\n");
        return 1U;
    }
    if (sampleCount == 0UL) {
        sampleCount = 1UL;
    }
    if (sampleCount > 1000UL) {
        sampleCount = 1000UL;
    }

    for (index = 0UL; index < sampleCount; index++) {
        if (App_GetServoFeedbackRaw((uint8_t)servoIndex, &raw) == 0U) {
            Platform_Uart_Print("Servo feedback not configured or read failed\r\n");
            return 1U;
        }
        currentRaw = raw;
        if (raw < minRaw) {
            minRaw = raw;
        }
        if (raw > maxRaw) {
            maxRaw = raw;
        }
        sumRaw += raw;
    }

    Platform_Uart_Print("========== Servo Calibration ==========\r\n");
    Platform_Uart_Printf("  Servo   : %lu\r\n", servoIndex);
    Platform_Uart_Printf("  Samples : %lu\r\n", sampleCount);
    Platform_Uart_Printf("  Current : %u\r\n", currentRaw);
    Platform_Uart_Printf("  Min     : %u\r\n", minRaw);
    Platform_Uart_Printf("  Max     : %u\r\n", maxRaw);
    Platform_Uart_Printf("  Avg     : %lu\r\n", sumRaw / sampleCount);
    Platform_Uart_Print("=======================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleServoAngleCommand(const char *cmd)
{
    const char *cursor = cmd;
    char *endPtr;
    unsigned long servoIndex = 1UL;
    uint16_t adcRaw;
    uint8_t angle;

    if (strncmp(cursor, "servo_angle", 11U) != 0) {
        return 0U;
    }

    cursor += 11;
    while (*cursor == ' ') {
        cursor++;
    }

    if (*cursor != '\0') {
        servoIndex = strtoul(cursor, &endPtr, 10);
        if ((endPtr == cursor) || (*endPtr != '\0')) {
            Platform_Uart_Print("Use: servo_angle [id]\r\n");
            return 1U;
        }
    }

    if ((servoIndex < 1UL) || (servoIndex > 2UL)) {
        Platform_Uart_Print("Servo id must be 1 or 2\r\n");
        return 1U;
    }

    if ((App_GetServoFeedbackRaw((uint8_t)servoIndex, &adcRaw) == 0U) ||
        (App_GetServoFeedbackAngle((uint8_t)servoIndex, &angle) == 0U)) {
        Platform_Uart_Print("Servo feedback not configured or read failed\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Servo Angle ==========\r\n");
    Platform_Uart_Printf("  Servo : %lu\r\n", servoIndex);
    Platform_Uart_Printf("  Raw   : %u\r\n", adcRaw);
    Platform_Uart_Printf("  V     : %u mV\r\n", App_ConvertServoFeedbackToMv(adcRaw));
    Platform_Uart_Printf("  Angle : %u deg\r\n", angle);
    Platform_Uart_Print("=================================\r\n");

    return 1U;
}

static uint8_t Cli_TryHandleBottomIrTestCommand(const char *cmd)
{
    App_BottomIrTestSnapshotTypeDef snapshot;

    if ((strcmp(cmd, "bottom_ir_test") != 0) && (strcmp(cmd, "bottom_ir") != 0) && (strcmp(cmd, "ir_test") != 0)) {
        return 0U;
    }

    if (App_ReadBottomIrTest(&snapshot) == 0U) {
        Platform_Uart_Print("Bottom IR ADC read failed\r\n");
        return 1U;
    }

    Platform_Uart_Print("========== Bottom IR ==========\r\n");
    Platform_Uart_Printf("  Command   : %s\r\n", cmd);
    Platform_Uart_Print("  Pin       : PB0 / ADC1_IN8\r\n");
    Platform_Uart_Printf("  Raw       : %u\r\n", snapshot.raw);
    Platform_Uart_Printf("  V         : %u mV\r\n", snapshot.millivolts);
    Platform_Uart_Printf("  Threshold : %u\r\n", snapshot.thresholdRaw);
    Platform_Uart_Printf("  Compare   : %s\r\n", (snapshot.aboveThreshold != 0U) ? "ABOVE" : "BELOW");
    Platform_Uart_Printf("  Active    : %s\r\n", (snapshot.active != 0U) ? "YES" : "NO");
    Platform_Uart_Print("===============================\r\n");

    return 1U;
}
