# Open re-implementation of the Quansheng UV-K5/K6/5R v2.1.27 firmware

This repository is a merge of [OneOfEleven custom firmware](https://github.com/OneOfEleven/uv-k5-firmware-custom) with [fagci spectrum analizer](https://github.com/fagci/uv-k5-firmware-fagci-mod/tree/refactor) plus my few changes.<br>
All is a cloned and customized version of DualTachyon's open firmware found [here](https://github.com/DualTachyon/uv-k5-firmware) ... a cool achievement !

> [!TIP]
> There is a work done by others on forks of this repository. I encourage you to take a look at those too. [SEE HERE](https://github.com/egzumer/uv-k5-firmware-custom/discussions/485)

> [!WARNING]  
> Use this firmware at your own risk (entirely). There is absolutely no guarantee that it will work in any way shape or form on your radio(s), it may even brick your radio(s), in which case, you'd need to buy another radio.
Anyway, have fun.

## Table of Contents

* [Main Features](#main-features)
* [Manual](#manual)
* [Radio Performance](#radio-performance)
* [User Customization](#user-customization)
* [Compiler](#compiler)
* [Building](#building)
* [Credits](#credits)
* [Other sources of information](#other-sources-of-information)
* [License](#license)
* [Example changes/updates](#example-changesupdates)

## Main features:
* many of OneOfEleven mods:
   * AM fix, huge improvement in reception quality
   * long press buttons functions replicating F+ action
   * fast scanning
   * channel name editing in the menu
   * channel name + frequency display option
   * shortcut for scan-list assignment (long press `5 NOAA`)
   * scan-list toggle (long press `* Scan` while scanning)
   * configurable button function selectable from menu
   * battery percentage/voltage on status bar, selectable from menu
   * longer backlight times
   * mic bar
   * RSSI s-meter
   * more frequency steps
   * squelch more sensitive
* fagci spectrum analyzer (**F+5** to turn on)
* some other mods introduced by me:
   * SSB demodulation (adopted from fagci)
   * backlight dimming
   * battery voltage calibration from menu
   * better battery percentage calculation, selectable for 1600mAh or 2200mAh
   * more configurable button functions
   * long press MENU as another configurable button
   * better DCS/CTCSS scanning in the menu (`* SCAN` while in RX DCS/CTCSS menu item)
   * Piotr022 style s-meter
   * restore initial freq/channel when scanning stopped with EXIT, remember last found transmission with MENU button
   * reordered and renamed menu entries
   * LCD interference crash fix
   * many others...

 ## Manual

Up to date manual is available in the [Wiki section](https://github.com/egzumer/uv-k5-firmware-custom/wiki)

## Radio performance

Please note that the Quansheng UV-Kx radios are not professional quality transceivers, their
performance is strictly limited. The RX front end has no track-tuned band pass filtering
at all, and so are wide band/wide open to any and all signals over a large frequency range.

Using the radio in high intensity RF environments will most likely make reception anything but
easy (AM mode will suffer far more than FM ever will), the receiver simply doesn't have a
great dynamic range, which results in distorted AM audio with stronger RX'ed signals.
There is nothing more anyone can do in firmware/software to improve that, once the RX gain
adjustment I do (AM fix) reaches the hardwares limit, your AM RX audio will be all but
non-existent (just like Quansheng's firmware).
On the other hand, FM RX audio will/should be fine.

But, they are nice toys for the price, fun to play with.

## SI4732 SSB (LSB / USB / CW)

When **ENABLE_FMRADIO** and **ENABLE_FM_SI4732** are enabled, you can switch AM band modulation with the **3** key: AM → LSB → USB → CW → AM. For **LSB/USB/CW to actually decode** (not just show on screen), the Si4732 needs a **firmware patch** loaded from EEPROM.

**You need to do one of the following:**

1. **256 KB (2 Mbit) EEPROM**  
   The patch is read from EEPROM at address **0x3C228**, length **15832** bytes. The original K5 often has 8 KB EEPROM, so that region does not exist unless the radio has been upgraded to a larger EEPROM (e.g. 256 KB).

2. **Write the SSB patch into EEPROM**  
   The firmware does **not** contain or write the patch; it only **reads** it from the address above. You must put the patch there using an external method, for example:
   - Use the **reference firmware’s “写频” / K5Web** flow (e.g. [K5Web](https://k5.vicicode.com/)) if it supports writing the SI4732 patch.
   - Use the **PU2CLR SI4735** Arduino tool **SI47XX_09_SAVE_SSB_PATCH_EEPROM** to save the patch to a file, then write that file to EEPROM at offset **0x3C228** with a programmer or custom tool.
   - Restore an EEPROM backup from a device that already has the patch (e.g. one that ran the “HS” build of the reference firmware with the patch written).

If the patch is missing or the EEPROM is too small, the UI will still switch to LSB/USB/CW, but the receiver will not demodulate SSB correctly.

## 单边带（SSB）补丁怎么写入

要让 LSB/USB/CW 真正能解调，必须把 **Si4732 SSB 补丁**（约 15.8KB）写到 EEPROM 的 **0x3C228** 位置。机器需已换 **2Mbit (256KB) EEPROM**，否则没有这片区域。

**推荐方式（任选其一）：**

### 方式一：K5Web 写频（最简单）

1. 刷好本固件，并确保 **ENABLE_UART=1** 能编译通过、电脑能连上对讲机（见上文「电脑连接」）。
2. 打开浏览器访问 **[K5Web](https://k5.vicicode.com/)**，注册/登录。
3. 在网站里找到 **SI4732 SSB 补丁** 或 **写频 / 固件升级** 相关入口（参考 uv-k5-firmware-custom 的「写频方式」说明）。
4. 按页面提示：连接对讲机、选择 2Mbit、上传或写入补丁。补丁会被写到 EEPROM 0x3C228，长度 15832 字节。

### 方式二：从已有补丁的机器备份 EEPROM

1. 找一台**已经写过 SSB 补丁**的 K5（例如刷过 uv-k5-firmware-custom 的 **LOSEHUxxxHS** 并用 K5Web 写过补丁）。
2. 用支持 2Mbit 的 CPS 或工具，**完整读出该机的 EEPROM**，保存为文件。
3. 在你的机器上（也需 2Mbit EEPROM + 本固件），用同一工具把该 EEPROM 文件**写回**，或只截取 **0x3C228 起 15832 字节** 写入你机 EEPROM 的 0x3C228。

### 方式三：Arduino 导出补丁 + 自己写 EEPROM

1. 用 **PU2CLR 的 [SI4735 库](https://github.com/pu2clr/SI4735)**，打开示例 **SI47XX_09_SAVE_SSB_PATCH_EEPROM**，在 Arduino + Si4732 板子上运行，把补丁**保存到文件**（或先读到 PC）。
2. 得到约 **15832 字节** 的补丁 bin 文件。
3. 用能按**地址**写 EEPROM 的工具（例如支持 2Mbit 的 K5 写频软件、或通用 EEPROM 编程器），把该文件**写入对讲机 EEPROM 的起始偏移 0x3C228**（即地址 246312）。

**校验：** 写入后重启对讲机，进 AM 波段按 **3** 切到 LSB/USB，若声音解调正常，说明补丁已生效。

### AM/单边带使用 FMI 天线（实验性）

Si4732 有两个天线输入：**AMI**（AM/LW）、**FMI**（FM/SW）。官方仅文档化了 **PROP_FM_ANTENNA_INPUT (0x1107)** 用于 FM 时选择 FMI/AMI，**未明确** AM/SSB 是否可由软件切到 FMI。若你希望 AM 或单边带从 **FMI** 取信号（例如 FMI 接了更好的短波天线），可编译时打开实验选项：

- **Makefile** 中设 **`ENABLE_SI4732_AM_USE_FMI=1`**，重新编译、烧录。
- 固件会在进入 AM 和 SSB 时设置 `PROP_FM_ANTENNA_INPUT = 0`（通常对应 FMI）。若无效或更差，可改 `si473x.c` 里该值为 `1` 再试，或关闭本选项。

**注意：** 若芯片内部 AM/SW 只接 AMI，此选项可能无效果；若有效，则 AM/SSB 会从 FMI 取信号。

## 2Mbit (256KB) EEPROM 识别

若你已把机器换成 **2Mbit EEPROM**，希望电脑 CPS 显示为 2Mbit 而不是 64Kbit：

1. **Makefile 里打开 2Mbit 上报**  
   **`ENABLE_2MBIT_EEPROM`** 设为 **`1`**（默认已是 1）。固件会：
   - 版本字符串以 **"HS"** 结尾（与 LOSEHUxxxHS 一致），
   - 在 0x0515 版本回复的 **Padding** 里填 256KB 提示，供部分 CPS 识别。

2. **固件已支持 32 位 EEPROM 读写**  
   串口命令 **0x052B**（32 位读）、**0x0538**（32 位写），可访问整片 256KB。

3. **仍显示 64Kbit 时**  
   - 确认刷入的固件是 **ENABLE_2MBIT_EEPROM=1** 编译的，且 CPS 已重新连接对讲机（重新读版本）。
   - 换用**明确支持 2Mbit 的写频软件**：如 [K5Web](https://k5.vicicode.com/) 或 uv-k5-firmware-custom 的 LOSEHUxxxHS 配套 CPS。不同软件识别方式不同（有的看版本后缀 "H"/"HS"，有的看协议里的其他字段）。
   - 若必须用某款只认“特定版本号”的 CPS，可在 **`version.c`** 里把 `Version` 改成该 CPS 认识的 2Mbit 固件版本串（例如某 LOSEHUxxxHS 的完整版本字符串），再重新编译、刷机。

## 电脑连接（写频 / CPS）

若刷入本固件后**电脑连接失败**，而别的固件可以连接，是因为本仓库默认**关闭了 UART**（`ENABLE_UART=0`），以节省 Flash。  
要恢复电脑连接，请：

1. 打开 **Makefile**，把 **`ENABLE_UART`** 改为 **`1`**。
2. 因当前固件（含 Si4732 等）已接近写满 Flash，开启 UART 后会超约 1.9KB，链接会报错。需要**同时关闭一项其它功能**再编译，例如：
   - 把 **`ENABLE_SPECTRUM`** 改为 **`0`**（关闭频谱），或  
   - 把 **`ENABLE_DTMF_CALLING`** 改为 **`0`**（关闭 DTMF 呼叫）。
3. 保存后执行 **`make clean && make`**，刷入新生成的 `firmware.bin`。

示例（需要电脑连接且保留频谱时，可关 DTMF）：`ENABLE_UART=1`，`ENABLE_DTMF_CALLING=0`。

## User customization

You can customize the firmware by enabling/disabling various compile options, this allows
us to remove certain firmware features in order to make room in the flash for others.
You'll find the options at the top of "Makefile" ('0' = disable, '1' = enable) ..


|Build option | Description |
| --- | ---- |
|🧰 **STOCK QUANSHENG FEATURES**||
| ENABLE_UART | 电脑写频/连接 (CPS、编程线)。关闭则电脑无法连接。**开启后固件会超 Flash**，需同时关闭一项其它功能（如 ENABLE_SPECTRUM 或 ENABLE_DTMF_CALLING）再编译。 |
| ENABLE_AIRCOPY | easier to just enter frequency with butts |
| ENABLE_FMRADIO | WBFM VHF broadcast band receiver |
| ENABLE_NOAA | everything NOAA (only of any use in the USA) |
| ENABLE_VOICE | want to hear voices ? |
| ENABLE_VOX | |
| ENABLE_ALARM | TX alarms |
| ENABLE_TX1750 | side key 1750Hz TX tone (older style repeater access)|
| ENABLE_PWRON_PASSWORD | power-on password stuff |
| ENABLE_DTMF_CALLING | DTMF calling fuctionality, sending calls, receiving calls, group calls, contacts list etc. |
| ENABLE_FLASHLIGHT | enable top flashlight LED (on, blink, SOS) |
|🧰 **CUSTOM MODS**||
| ENABLE_BIG_FREQ | big font frequencies (like original QS firmware) |
| ENABLE_SMALL_BOLD | bold channel name/no. (when name + freq channel display mode) |
| ENABLE_CUSTOM_MENU_LAYOUT | changes how the menu looks like |
| ENABLE_KEEP_MEM_NAME | maintain channel name when (re)saving memory channel|
| ENABLE_WIDE_RX | full 18MHz to 1300MHz RX (though front-end/PA not designed for full range)|
| ENABLE_TX_WHEN_AM | allow TX (always FM) when RX is set to AM|
| ENABLE_F_CAL_MENU | enable the radios hidden frequency calibration menu |
| ENABLE_CTCSS_TAIL_PHASE_SHIFT | standard CTCSS tail phase shift rather than QS's own 55Hz tone method|
| ENABLE_BOOT_BEEPS | gives user audio feedback on volume knob position at boot-up |
| ENABLE_SHOW_CHARGE_LEVEL | show the charge level when the radio is on charge |
| ENABLE_REVERSE_BAT_SYMBOL | mirror the battery symbol on the status bar (+ pole on the right) |
| ENABLE_NO_CODE_SCAN_TIMEOUT | disable 32-sec CTCSS/DCS scan timeout (press exit butt instead of time-out to end scan) |
| ENABLE_AM_FIX | dynamically adjust the front end gains when in AM mode to help prevent AM demodulator saturation, ignore the on-screen RSSI level (for now) |
| ENABLE_AM_FIX_SHOW_DATA | show debug data for the AM fix |
| ENABLE_SQUELCH_MORE_SENSITIVE | make squelch levels a little bit more sensitive - I plan to let user adjust the values themselves |
| ENABLE_FASTER_CHANNEL_SCAN | increases the channel scan speed, but the squelch is also made more twitchy |
| ENABLE_RSSI_BAR | enable a dBm/Sn RSSI bar graph level in place of the little antenna symbols |
| ENABLE_AUDIO_BAR | experimental, display an audio bar level when TX'ing |
| ENABLE_COPY_CHAN_TO_VFO | copy current channel settings into frequency mode. Long press `1 BAND` when in channel mode |
| ENABLE_SPECTRUM | fagci spectrum analyzer, activated with `F` + `5 NOAA`|
| ENABLE_REDUCE_LOW_MID_TX_POWER | makes medium and low power settings even lower |
| ENABLE_BYP_RAW_DEMODULATORS | additional BYP (bypass?) and RAW demodulation options, proved not to be very useful, but it is there if you want to experiment |
| ENABLE_BLMIN_TMP_OFF | additional function for configurable buttons that toggles `BLMin` on and off wihout saving it to the EEPROM |
| ENABLE_SCAN_RANGES | scan range mode for frequency scanning, see wiki for instructions (radio operation -> frequency scanning) |
|🧰 **DEBUGGING** ||
| ENABLE_AM_FIX_SHOW_DATA| displays settings used by  AM-fix when AM transmission is received |
| ENABLE_AGC_SHOW_DATA | displays AGC settings |
| ENABLE_UART_RW_BK_REGS | adds 2 extra commands that allow to read and write BK4819 registers |
|🧰 **COMPILER/LINKER OPTIONS**||
| ENABLE_CLANG | **experimental, builds with clang instead of gcc (LTO will be disabled if you enable this) |
| ENABLE_SWD | only needed if using CPU's SWD port (debugging/programming) |
| ENABLE_OVERLAY | cpu FLASH stuff, not needed |
| ENABLE_LTO | reduces size of compiled firmware but might break EEPROM reads (OVERLAY will be disabled if you enable this) |

## Compiler

arm-none-eabi GCC version 10.3.1 is recommended, which is the current version on Ubuntu 22.04.03 LTS.
Other versions may generate a flash file that is too big.
You can get an appropriate version from: https://developer.arm.com/downloads/-/gnu-rm

clang may be used but isn't fully supported. Resulting binaries may also be bigger.
You can get it from: https://releases.llvm.org/download.html

## Building

### Github Codespace build method

This is the least demanding option as you don't have to install enything on your computer. All you need is Github account.

1. Go to https://github.com/egzumer/uv-k5-firmware-custom
1. Click green `Code` button
1. Change tab from `Local` to `Codespace`
1. Click green `Create codespace on main` button

<img src="images/codespace1.png" width=700 />

5. Open `Makefile`
1. Edit build options, save `Makefile` changes
1. Run `./compile-with-docker.sh` in terminal window
1. Open folder `compiled-firmware`
1. Right click `firmware.packed.bin`
1. Click `Download`, now you should have a firmware on your computer that you can proceed to flash on your radio. You can use [online flasher](https://egzumer.github.io/uvtools)

<img src="images/codespace2.png" width=700 />

### Docker build method

If you have docker installed you can use [compile-with-docker.bat](./compile-with-docker.bat) (Windows) or [compile-with-docker.sh](./compile-with-docker.sh) (Linux/Mac), the output files are created in `compiled-firmware` folder. This method gives significantly smaller binaries, I've seen differences up to 1kb, so it can fit more functionalities this way. The challenge can be (or not) installing docker itself.

### Windows environment build method

1. Open windows command line and run:
    ```
    winget install -e -h git.git Python.Python.3.8 GnuWin32.Make
    winget install -e -h Arm.GnuArmEmbeddedToolchain -v "10 2021.10"
    ```
2. Close command line, open a new one and run:
    ```
    pip install --user --upgrade pip
    pip install crcmod
    mkdir c:\projects & cd /D c:/projects
    git clone https://github.com/egzumer/uv-k5-firmware-custom.git
    ```
3. From now on you can build the firmware by going to `c:\projects\uv-k5-firmware-custom` and running `win_make.bat` or by running a command line:
    ```
    cd /D c:\projects\uv-k5-firmware-custom
    win_make.bat
    ```
4. To reset the repository and pull new changes run (!!! it will delete all your changes !!!):
    ```
    cd /D c:\projects\uv-k5-firmware-custom
    git reset --hard & git clean -fd & git pull
    ```

I've left some notes in the win_make.bat file to maybe help with stuff.

## Credits

Many thanks to various people on Telegram for putting up with me during this effort and helping:

* [OneOfEleven](https://github.com/OneOfEleven)
* [DualTachyon](https://github.com/DualTachyon)
* [Mikhail](https://github.com/fagci)
* [Andrej](https://github.com/Tunas1337)
* [Manuel](https://github.com/manujedi)
* @wagner
* @Lohtse Shar
* [@Matoz](https://github.com/spm81)
* @Davide
* @Ismo OH2FTG
* [OneOfEleven](https://github.com/OneOfEleven)
* @d1ced95
* and others I forget

## Other sources of information

[ludwich66 - Quansheng UV-K5 Wiki](https://github.com/ludwich66/Quansheng_UV-K5_Wiki/wiki)<br>
[amnemonic - tools and sources of information](https://github.com/amnemonic/Quansheng_UV-K5_Firmware)

## License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

## Example changes/updates

<p float="left">
  <img src="/images/image1.png" width=300 />
  <img src="/images/image2.png" width=300 />
  <img src="/images/image3.png" width=300 />
</p>

Video showing the AM fix working ..

<video src="/images/AM_fix.mp4"></video>

<video src="https://github.com/OneOfEleven/uv-k5-firmware-custom/assets/51590168/2a3a9cdc-97da-4966-bf0d-1ce6ad09779c"></video>
