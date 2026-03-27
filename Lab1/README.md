# LAB1 Report

**Group:** 9  
**Student ID:** 314551147  

---

## 1. Commands Entered (Step-by-step)

### 1.1 Toolchain and Environment Setup
According to the manual, most commands should be run with `sudo`. First, we set up the cross-compiler path:

```bash
# Replace with your actual path to the arm-gcc toolchain
export ARM_GCC_IMX6_V3=/opt/EmbedSky/gcc-linaro-arm-linux-gnueabihf-4.9
export PATH="$ARM_GCC_IMX6_V3/bin:$PATH"

# Verify the cross-compiler installation
arm-linux-gnueabihf-gcc -v
```

### 1.2 Target Board OS Installation (SD Card Preparation)
Following instructions in "TQSDMaker User Manual":

```bash
# Ensure the SD card disk is NOT mounted before starting 
umount /dev/sdb1 2>/dev/null || true

# Extract the tool packed in image_new_v11.tar.bz2
tar -xjf image_new_v11.tar.bz2
cd image_new_v11
sudo ./TQSDMaker /dev/sdb
```

### 1.3 Image Replacement for Linux 4.1
Replace specific files in the linux 4.1 folder from `E9V3_Linux4.1_image.tar.bz2`:

```bash
tar -xjf E9V3_Linux4.1_image.tar.bz2
cd linux_4.1_folder

# Rename and replace the four required files as per manual
cp zImage_header_4.1.15 zImage
cp dtb_header_4.1.15 imx6q-sabresd.dtb
```

### 1.4 Compiling the Hello World Program
Prepare the source code `hello_world.c` and compile using the cross-compiler:

```c
// Source Code Content:
#include <stdio.h>
int main() { 
    printf("314551147 helloworld\n"); 
    return 0; 
}
```

```bash
arm-linux-gnueabihf-gcc hello_world.c -o hello_world
```

---

## 2. Problems Encountered and Resolutions

| Problem | Resolution |
| :--- | :--- |
| **2.1 SD Card Boot Failure: Disk Mounted Error**<br>While doing step 3.1.2 in the TQSDMaker manual, the process failed because the SD card was automatically mounted by Ubuntu. | **Fix:** Manually unmount all partitions of the SD card using `umount` before running the maker tool. |
| **2.2 Compiler Not Found in Path**<br>The command `arm-linux-gnueabihf-gcc` was initially not recognized. | **Fix:** Ensure the environment parameters modification on page 3 of the setup manual is correctly applied to the current shell session. |
| **2.3 Advance Task: Offline File Transfer via RS-232**<br>The board is not allowed to connect to the Internet during any progress. | **Fix:** Use the RS-232 interface on the host computer with a baud rate of 115200 to transfer files. I utilized `rx/sx` (Zmodem) over the serial terminal to move the executable. |

---

## 3. Verification and Results

### 3.1 Standard Output Verification
Connect the board via RS-232 to the host. Set the baud rate to **115200**.

```bash
# On the Target Board (Logged in via Serial)
chmod +x hello_world
./hello_world
```

**Output:** `314551147 helloworld`.

---

## 4. Questions and Conceptual Answers

| Question | Answer |
| :--- | :--- |
| **4.1 What is arm-linux-gnueabihf-gcc? Why not use gcc?** | It is a cross-compiler that targets the ARM architecture with a hardware floating-point (hf) unit. We cannot use standard `gcc` because it generates x86 instructions for the host PC, whereas the development board uses the ARM instruction set. |
| **4.2 Can executable hello_world run on the host computer?** | No. The binary format is compiled for ARM processors. The host computer (x86_64) cannot execute these instructions as the Instruction Set Architecture (ISA) is fundamentally different. |

---

## 5. Advance: Performance Testing

Tested system performance using the `time` command to measure execution speed:

```bash
time ./hello_world
```
This allows us to analyze the user, system, and real-time latency of the embedded environment.

---

## 6. References

* 114 Embedded System Design LAB 1 Manual
* TQIMX6Q(V3) QT5.5 Environment Setup Manual
* TQSDMaker User Manual
