# 命名规范符合性检查报告

> 对照 `coding rule.md`（Siemens Healthcare CV SW 团队 C/C++ 命名规范，匈牙利前缀变体），检查 `application/src/` 下自有代码。
> 检查日期：2026-08-05 · 本报告**只检查不改代码**。

---

## 一、规范要点摘要

**标识符配方**：
```
Identifier ::= [Special] Scope [Additionals] [Type] [ _ ] Name [ _ Unit]
EnumLiteral ::= E_ Prefix _ Name [ _ Unit]
```

| 元素 | 说明 | 示例 |
|------|------|------|
| `[Special]` | `hw_`(硬件) / `t_`(类型) | `hw_ui_ADC1_kV`、`t_en_Weekday` |
| `[Scope]` | `g_`(global) / `f_`(file) / `l_`(local) / `p_`(parameter) / `m_`(member) | `g_si_TubeVoltage_V` |
| `[Additionals]` | `c_`(const) / `p`(指针) / `a`(数组) / `v`(volatile) / `h`(handle) | `l_pch_String` |
| `[Type]` | `si` / `ui` / `en` / `st` / `bv` / `fl` / `ch` / `fc` / `vd` | `t_st_MyStruct` |
| `Name` | 名词(对象)/动词(方法) | `TubeVoltage` |
| `[ _ Unit]` | 物理单位后缀 | `_V`、`_mV`、`_kHz` |

**枚举字面量**：`E_` 开头 + 全大写前缀 + 名称（+ 可选单位），如 `E_WD_MONDAY`。

---

## 二、检查范围

**已检查**（自有代码）：`src/bsp/`、`src/external/`、`src/ntc/`、`src/scheduler/`、`src/state_machine/`、`src/main/main.c`

**已排除**（第三方/生成/库回调）：
- `src/canopen/`、`src/candriver/`、`src/objdic/`（port GmbH 协议栈 / 生成代码）
- `src/main/nmtslave.c`、`src/main/usr_301.c`（port GmbH CANopen 库模板回调）

---

## 三、核心结论

现有代码命名体系与西门子匈牙利规则**基本不吻合**，但工程已形成自有一套约定俗成风格。**全部 6 类标识符均未采用规范要求的前缀体系。**

| 类别 | 现有风格 | 规范要求 | 吻合度 |
|------|---------|---------|--------|
| 函数名 | camelCase + 模块前缀（`bspAinInit`）| 需 `fc` 类型 + `g_`/`f_` 前缀 | ❌ |
| 全局变量 | camelCase（`dinMax`）| 需 `g_ui_` 等 | ❌ |
| static 变量 | camel/snake 混用，仅 state_machine 用 `g_` | 需 `f_` 前缀 | ❌ |
| 类型名 | camelCase + `_t` 后缀（`stateMachineState_t`）| 需 `t_en_`/`t_st_` 前缀 | ❌（前后缀相反）|
| 枚举成员 | SCREAMING_SNAKE + 域前缀（`FAN_DUTY_MAX`）| 需 `E_` 前缀 | ❌（无 `E_`）|
| 单位后缀 | 部分枚举有 `_MV`/`_MS`/`_OHM` | 需 `_Unit` 后缀 | ⚠️ 部分符合 |

---

## 四、逐类详细对照

### 1. 函数名（91 个，含 static）

**现状**：统一 camelCase、无下划线，模块前缀（`bsp`/`board`/`led`/`wtdg`/`max6703a`/`ntc`/`scheduler`/`stateMachine`）。

**代表**：
```
bspAinInit  bspAoutWrite  bspDoutSetMask  stateMachineTick  ntcReadTemp
max6703aFeed  schedulerStart  ledSwitchOn  wtdgFeed  boardInit
```

**规范要求**：函数属 `fc` 类型，前缀应为 `g_fc_`（全局）或 `f_fc_`（文件）。

**不一致点**：
- ❌ 无 `g_`/`f_`/`fc` 前缀
- ⚠️ 模块前缀不统一：`bsp_wtdg.c` 用 `wtdgXxx`、`bsp_led.c` 用 `ledXxx`、`bsp_board.c` 用 `boardXxx`，而 `bsp_dio.c`/`bsp_ain.c` 用 `bspXxx`。同目录风格不一。

### 2. 全局变量（top-level，非 static）

**现状**（仅 3 个真全局量，均无前缀）：
| 名字 | 位置 | 说明 |
|------|------|------|
| `dinMax` | bsp_dio.c:57 | `const uint8_t` |
| `doutMax` | bsp_dio.c:46 | `const uint8_t` |
| `wdt` | bsp_wtdg.c:26/29 | `const struct device *const` |

**规范要求**：`g_ui_` + 名称。**全不符合**。

### 3. 文件级 static 变量

**现状**：混合 camelCase / snake_case，无前缀，仅 state_machine 用 `g_`。

| 模块 | 名字 | 问题 |
|------|------|------|
| bsp_ain | `ain_specs`/`ainData`/`ainName` | camel/snake 混用 |
| bsp_dio | `din_state`/`dinSettings` | 同文件混用 |
| bsp_wtdg | `wdt_channel_id`/`err` | **`err` 裸名**，全局冲突风险 |
| state_machine | `g_state`/`g_config` 等 | **唯一用 `g_` 前缀的模块** |
| scheduler | `ain_work`/`din_work` 等 | snake_case |

**规范要求**：`f_` + 类型缩写。**全不符合**（仅 state_machine 用 `g_` 接近但作用域应为 `f_`）。

### 4. 类型名（16 个）

**现状**：主流 camelCase + `_t` 后缀（10/16），部分不一致：
- ✅ `stateMachineState_t`、`fanDuty_t`、`ntcHwParam_t`（camelCase + `_t`）
- ❌ `boardSpiXfer`、`bspDinSettings`（无 `_t` 后缀）
- ❌ `bsp_gpio_id_t`、`ntc_point_t`（snake_case）
- ❌ `struct bsp_pwm_opt`（未 typedef 的裸 tag）

**规范要求**：`t_st_`/`t_en_` 前缀 + 类型缩写。**全不符合**（前后缀相反）。

### 5. 枚举成员

**现状**：SCREAMING_SNAKE_CASE + 语义域前缀，19 组前缀（`AIN_ADC_`/`DOUT_`/`FAN_DUTY_`/`STATEMACHINE_STATE_` 等）。

**规范要求**：`E_` + 前缀 + 名称。**全不符合**（无 `E_` 前缀）。

**有趣发现**：第三方 CANopen 库内部**反而符合** `E_` 前缀规范（`E_SDO_A_*`、`E_PDO_MAPPING`）。

### 6. 局部变量 / 参数

**现状**：最不统一的一类。
- bsp 层：camelCase / 单字母（`channel`/`idx`/`i`/`v`/`s`）
- ntc / state_machine：snake_case + 单位后缀（`r_ohm`/`v_mv`/`duty_percent`/`temp_max`）

**规范要求**：`p_`/`l_` + 类型缩写。**全不符合**。

---

## 五、规范内自洽（符合或接近的部分）

以下部分**接近或符合**规范的某一方面：

1. **枚举成员带单位后缀**（`PWR_ON_OFF_PERIOD_MS`、`NTC_HW_VREF_MV`、`NTC_HW_RFIXED_OHM`）——接近规范的 `_Unit` 要求
2. **ntc 局部变量带单位后缀**（`r_ohm`/`v_mv`/`v_drop_mv`）——符合 `_Unit` 精神
3. **函数名统一 camelCase 无下划线**——最一致的一类，规范未禁止（只是要求加前缀）
4. **枚举成员统一 SCREAMING_SNAKE + 域前缀**——一致性好，仅缺 `E_` 开头

---

## 六、6 个主要不一致点（改造优先项）

| # | 问题 | 位置 | 说明 |
|---|------|------|------|
| 1 | `g_` 前缀仅 state_machine 用 | state_machine.c | 其余模块 static 无 `f_` 前缀 |
| 2 | 函数缺 `bsp` 前缀 | bsp_wtdg/led/board | `wtdgInit`/`ledInit`/`boardInit` vs `bspDioInit` |
| 3 | 类型 `_t` 后缀不齐 | bsp_board.h / bsp_dio.h / bsp_pwm.c | `boardSpiXfer`/`bspDinSettings` 缺后缀；`struct bsp_pwm_opt` 未 typedef |
| 4 | static 变量 camel/snake 混用 | bsp_dio.c | `din_state` vs `dinSettings` 同文件并存 |
| 5 | `AIN_NUMBER` extern 与宏不符 | bsp_ain.h:91 vs bsp_ain.c:16 | 头文件 `extern uint8_t` 但 .c 是 `#define`（死 extern）|
| 6 | `err` 裸名冲突风险 | bsp_wtdg.c:34 | 全局/static 命名太泛 |

---

## 七、建议（未实施）

若未来需向西门子规范靠拢，建议按**渐进式**推进（非一次性全改）：

1. **从"单位后缀"开始**（最接近规范，改动小）：为带物理量的枚举/变量统一加 `_V`/`_mV`/`_ms` 后缀
2. **统一模块前缀**：`bsp_wtdg`/`bsp_led`/`bsp_board` 函数改为 `bspXxx` 前缀，消除同目录不一致
3. **类型名统一 `_t` 后缀**：补 `boardSpiXfer_t`/`bspDinSettings_t`，typedef `struct bsp_pwm_opt`
4. **是否全面引入匈牙利前缀**（`g_`/`f_`/`t_en_`/`E_`）需团队决策——这是**全工程大改**，涉及上百处，且与现有 Zephyr/CANopen 生态风格冲突

> ⚠️ 注意：规范里 `g_`/`f_` 等匈牙利前缀与 Zephyr 保留前缀（`k_`/`sys_`/`atomic_` 等）是两个独立体系，引入时不冲突，但会显著改变代码可读性习惯。

---

*报告结束。本报告仅检查与说明，未修改任何代码。*
