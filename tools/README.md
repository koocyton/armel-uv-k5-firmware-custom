# K5 工具

## SSB 补丁写入 (ssb-patch-writer.html)

通过浏览器将对讲机 SSB 补丁写入 EEPROM 地址 **0x3C228**。

### 使用条件

- 对讲机已刷本仓库固件，且编译时 **ENABLE_UART=1**、**ENABLE_2MBIT_EEPROM=1**。
- 机器已换 **2Mbit (256KB) EEPROM**。
- 使用 **Chrome** 或 **Edge**（支持 Web Serial API）。
- 补丁文件：约 15832 字节的 bin 文件（可从 [K5Web](https://k5.vicicode.com/)、PU2CLR SI4735 示例等获取）。

### 步骤

1. 用 USB 连接 K5；对讲机**正常开机**即可，**无需**进入固件升级/刷机模式。若 CPS、写频软件已占用串口，请先关闭。
2. 用浏览器打开 **ssb-patch-writer.html**（本地打开或通过 http 服务）。
3. 点击「连接串口」，在弹窗中选择 K5 对应的串口（如 COM3、/dev/ttyUSB0）。
4. 选择补丁 bin 文件。
5. 点击「写入 0x3C228」，等待日志出现「写入完成」。
6. 重启对讲机。进入 Radio（F+0）后，在 FM/AM 下**长按 F** 进入单边带，单边带下**短按 F** 切换 USB/LSB/CW，**长按 F** 退回 AM。

### 说明

- 页面通过 0x0514 建立会话，再按块发送 0x0538 将数据写入 0x3C228；写入时会**持续读取**串口应答，避免 USB 缓冲塞满导致进度停在约 14xxx/15832。
- 块间有短延时以便 EEPROM 写入完成。
- 若连接或写入失败，请确认串口未被其他程序占用、固件支持 UART 与 32 位写。
- 使用较新本仓库固件时，对高地址 EEPROM 写入不再每次全量 `SETTINGS_InitEEPROM()`，整段补丁写入会快很多。
