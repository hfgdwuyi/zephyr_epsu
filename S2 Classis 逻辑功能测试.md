# S2 Classis 逻辑功能测试

## 说明

原始工作表是横向状态矩阵。为便于阅读，下面按状态拆分整理，每个状态单独列出“输出信号”和“继电器/efuse状态”。

## 1. 待机 T0

| 类别 | 内容 |
|---|---|
| 输出信号 | 1. LED_GRID_PWR_IN<br>2. LED_S2_SYS_ON<br>4. LED_PWR_24_ON<br>5. LED_CP_24V_ON<br>6. LED_PAC230V_ON<br>7. MAINS_CONNECTED_MCU<br>8. MAINS_CONNECTED_ISPC<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>LED_TROLLEY_CONNECTED<br>TRL_MU_CONNECTED_MCU<br>TRL_MU_CONNECTED_IS_PC<br>TROLLEY_ENABLE_DRV |
| 继电器/efuse状态 | K3 ON PAC6<br>K10 ON PDC5 |

## 2. 开机 T2

| 类别 | 内容 |
|---|---|
| 输出信号 | 1. LED_GRID_PWR_IN<br>2. LED_S2_SYS_ON<br>4. LED_PWR_24_ON<br>5. LED_CP_24V_ON<br>6. LED_PAC230V_ON<br>7. MAINS_CONNECTED_MCU<br>8. MAINS_CONNECTED_ISPC<br>9. DRV_IS_PC_SITE_ON<br>10. DRV_APP_HOST_SITE_ON<br>11. LED_SYSTEM_ON<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>LED_TROLLEY_CONNECTED<br>TRL_MU_CONNECTED_MCU<br>TRL_MU_CONNECTED_IS_PC<br>TROLLEY_ENABLE_DRV |
| 继电器/efuse状态 | K3 ON PAC6<br>K4 ON PAC1<br>K7 ON PAC3<br>K8_1 ON PDC1<br>K8_2 ON PDC2<br>K9 ON PDC3<br>K10 ON PDC5<br>K11 ON PDC6<br>K12 ON PDC7<br>K13 ON PDC4<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>K5 ON PAC2 |

## 3. Standby（UPS 模式）市电掉电开机后

| 类别 | 内容 |
|---|---|
| 输出信号 | 1. LED_GRID_PWR_IN<br>2. LED_S2_SYS_ON<br>3. LED_PWR_24_ON<br>4. LED_CP_24V_ON<br>5. LED_PAC230V_ON<br>6. MAINS_CONNECTED_MCU<br>7. MAINS_CONNECTED_ISPC |
| 继电器/efuse状态 | K8_1 ON PDC1<br>K8_2 ON PDC2<br>K9 ON PDC3<br>K10 ON PDC5<br>K11 ON PDC6<br>K12 ON PDC7<br>K13 ON PDC4<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>K5 ON PAC2 |

## 4. 关机 T4

| 类别 | 内容 |
|---|---|
| 输出信号 | 1. LED_GRID_PWR_IN<br>2. LED_S2_SYS_ON<br>3. LED_PWR_24_ON<br>4. LED_CP_24V_ON<br>5. LED_PAC230V_ON<br>6. MAINS_CONNECTED_MCU<br>7. MAINS_CONNECTED_ISPC<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>LED_TROLLEY_CONNECTED<br>TRL_MU_CONNECTED_MCU<br>TRL_MU_CONNECTED_IS_PC<br>TROLLEY_ENABLE_DRV |
| 继电器/efuse状态 | K3 ON PAC6<br>K10 ON PDC5<br><br>分别判断 IS_PC_ON、APP_HOST_ON 状态：<br>1. IS_PC_ON 高电平：K9、K11 ON<br>APP_HOST_ON 高电平：K5 ON<br>2. IS_PC_ON 低电平：K9、K11 OFF<br>APP_HOST_ON 低电平：K5 OFF |

## 5. UPS 模式下市电恢复，恢复到开机状态

| 类别 | 内容 |
|---|---|
| 输出信号 | 1. LED_GRID_PWR_IN<br>2. LED_S2_SYS_ON<br>3. LED_PWR_24_ON<br>4. LED_CP_24V_ON<br>5. LED_PAC230V_ON<br>6. MAINS_CONNECTED_MCU<br>7. MAINS_CONNECTED_ISPC<br>8. DRV_IS_PC_SITE_ON<br>9. DRV_APP_HOST_SITE_ON<br>10. LED_SYSTEM_ON<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>LED_TROLLEY_CONNECTED<br>TRL_MU_CONNECTED_MCU<br>TRL_MU_CONNECTED_IS_PC<br>TROLLEY_ENABLE_DRV |
| 继电器/efuse状态 | K3 ON PAC6<br>K4 ON PAC1<br>K7 ON PAC3<br>K8_1 ON PDC1<br>K8_2 ON PDC2<br>K9 ON PDC3<br>K10 ON PDC5<br>K11 ON PDC6<br>K12 ON PDC7<br>K13 ON PDC4<br><br>判断 TROLLEY_CONNECTED 信号 2s 延时，高电平使能：<br>K5 ON PAC2 |

## 6. UPS 模式下关机，关机后系统掉电

| 类别 | 内容 |
|---|---|
| 输出信号 | 检测到关机按键有效，关闭所有继电器、信号、efuse 使能，等待系统自行掉电。 |
| 继电器/efuse状态 | 原表未单独列出。 |

## 软件复位

超过 5s 执行软件复位，必须检测到信号上升沿和下降沿才触发；复位之后恢复到待机状态。

## 开关机逻辑

系统开关机根据 SYSTEM_ON_OFF 判断：

1. 使能时间小于 0.5s 无效。
2. 保持时间 0.5s 到 5s 之间，视为有效开关机信号。
3. 必须检测到信号上升沿和下降沿才触发。
4. 超过 5s 执行软件复位。
5. 软件复位后恢复到待机状态。