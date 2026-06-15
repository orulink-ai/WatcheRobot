#ifndef USER_APP_CLI_H
#define USER_APP_CLI_H

/*
 * 串口命令处理入口。
 * 当前保持轮询读取一字节、按行组包、收到回车后执行命令的行为不变。
 */

void Cli_ProcessInput(void);
void Cli_ExecuteCommand(const char *cmd);

#endif /* USER_APP_CLI_H */
