# LAB2 Report: Video Output and Input via Framebuffer

**Group:** 9  
**Student ID:** 314551147  

---

## 1. Introduction & Objectives
[cite_start]本實驗旨在於嵌入式系統（E9V3）上實作影像輸出與輸入功能，全程不依賴 X Window 或 GUI 系統，而是直接操作 Linux Framebuffer (`/dev/fb0`) 進行渲染 [cite: 206, 220]。實驗分為三個部分：
1. [cite_start]靜態影像顯示（BMP 輸出至 Framebuffer）[cite: 208]。
2. [cite_start]攝影機即時串流與無阻塞截圖（Camera Stream & Non-blocking Screenshot）[cite: 357, 369]。
3. [cite_start]HDMI 1080p 輸出與電子跑馬燈實作（Electronic Scroll Board）[cite: 381, 398]。

---

## 2. System Implementation

### 2.1 Part 1: Static Image Display (`lab_2-1.cpp`)
* **目標：** 將給定的 `NYCU_logo.bmp` 顯示於開發板螢幕上。
* **實作細節：** * 透過 `open("/dev/fb0", O_RDWR)` 開啟 Framebuffer 裝置。
  * 利用 `ioctl` 與 `FBIOGET_VSCREENINFO` 取得螢幕的解析度（xres_virtual）與色彩深度（bits_per_pixel）。
  * 使用 OpenCV 讀取圖片後，將色彩空間由原本的 BGR 轉換為 LCD 支援的 16-bit **BGR565** 格式。
  * 將轉換後的像素資料逐行寫入 Framebuffer 對應的記憶體位置中完成顯示。

### 2.2 Part 2: Real-time Camera Stream & Screenshots (`lab_2-2.cpp`)
* [cite_start]**目標：** 顯示攝影機畫面，維持長寬比（不變形），並實作按鍵 `c` 截圖功能（無延遲、無阻塞）[cite: 365, 369, 374]。
* **畫面置中與留白（Letterboxing）：** 取得攝影機畫面後，計算來源畫面與目標螢幕的長寬比。依據比例縮放畫面，並使用 `cv::copyMakeBorder` 在影像的上下或左右補上黑色邊界，確保畫面嚴格置中且不變形。
* **無阻塞鍵盤讀取：** 實作 `getch_noblock()`，利用 `termios` 關閉終端機的行緩衝（Line buffering）與回顯（Echo），並使用 `select()` 監聽 `STDIN_FILENO`，達成不卡死主迴圈的按鍵偵測。
* **截圖儲存：** 程式啟動時會自動在 SD 卡建立遞增的 `screenshot_{id}` 資料夾，按下 `c` 鍵時，直接透過 OpenCV 將原始 frame 儲存為 BMP 檔案，達到無延遲的截圖要求。

### 2.3 Part 3: HDMI Output & Electronic Scroll Board (`lab_2-3_advance.cpp`)
* [cite_start]**目標：** 在 HDMI (1080p) 畫面上顯示 `advance.png`，並實作可鍵盤控制方向的自動跑馬燈 [cite: 387, 395, 398, 402]。
* **輕量化讀圖：** 為了在進階要求中避免龐大的依賴，引入了 `stb_image.h` 取代部分 OpenCV 功能來載入 PNG 圖片。
* **滾動邏輯：** * 將載入的圖片等比例縮放至螢幕高度（1080），此時圖片寬度會大於螢幕寬度。
  * 設定 `offset_x` 變數作為可視區域（ROI）的起點，在主迴圈中不斷擷取 `offset_x` 到 `offset_x + screen_width` 的影像區塊寫入 Framebuffer。
  * 每次迴圈自動將 `offset_x` 加上方向位移量，並透過 `getch_noblock()` 監聽 `j` (向右) 與 `l` (向左) 鍵來即時改變滾動方向。

---

## 3. Compilation Commands
```bash
# Cross-compiling the application with OpenCV and pthread
arm-linux-gnueabihf-g++ lab_2-3_advance.cpp -o demo \
  -I/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/include/ \
  -I/usr/local/arm-opencv/install/include/ \
  -L/usr/local/arm-opencv/install/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/libc/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/qt5.5_env/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/usr/lib/ \
  -lpthread -lopencv_world
```
*執行前需指定動態函式庫路徑：* `LD_LIBRARY_PATH=./ /path/to/demo`

---

## 4. Questions and Conceptual Answers

| Question | Answer |
| :--- | :--- |
| **Q1: What are cmake and make for? Relationship?** | [cite_start]`cmake` 是一個跨平台的建置系統生成器，它會讀取 `CMakeLists.txt` 並針對目標平台生成對應的 Makefile [cite: 341][cite_start]。`make` 則是負責讀取生成的 Makefile，並實際呼叫編譯器（如 gcc/g++）來編譯原始碼與連結函式庫 [cite: 341]。 |
| **Q2: Why are there so many arguments in compilation?** | [cite_start]這些參數指定了交叉編譯器（Cross-compiler）尋找標頭檔（`-I`）與靜態/動態函式庫（`-L`）的路徑 [cite: 342][cite_start]。`-Wl,-rpath-link` 則是告訴連結器（Linker）在編譯時期去哪裡解析動態函式庫的依賴關係，以確保 ARM 架構的程式能在 x86 主機上順利連結 [cite: 342]。 |
| **Q3: What is libopencv_world.so? Why LD_LIBRARY_PATH?** | [cite_start]`libopencv_world.so` 是一個將所有 OpenCV 模組打包在一起的動態函式庫 [cite: 343][cite_start]。使用 `LD_LIBRARY_PATH` 是為了在執行時告訴 Linux Loader (ld.so) 去哪裡尋找這個動態函式庫；如果不加，系統預設只會找 `/lib` 和 `/usr/lib`，會導致找不到函式庫而無法執行 [cite: 343, 344]。 |
| **Q4: Why not just use cv::imshow()?** | [cite_start]`cv::imshow()` 底層極度依賴視窗系統（如 X Window System 或 Wayland）及 GUI 框架（如 GTK/Qt）[cite: 346][cite_start]。在精簡的嵌入式 Linux 環境中，通常沒有運作這些龐大的視窗系統，因此必須直接將像素資料寫入 Framebuffer 來顯示畫面 [cite: 345, 346]。 |
| **Q5: What is a framebuffer?** | [cite_start]Framebuffer 是 Linux 核心提供的一個子系統，它將圖形硬體抽象化為一塊記憶體空間（Video Memory）[cite: 347][cite_start]。開發者只需將像素顏色資料寫入這塊記憶體，顯示控制器就會自動將其輸出到螢幕上 [cite: 347]。 |
| **Q6: `cat /dev/fb0 > fb0` then `cat fb0 > /dev/fb0`?** | [cite_start]第一行指令會將目前螢幕上的像素資料備份到一個名為 `fb0` 的檔案中（相當於全螢幕截圖）。第二行指令則是將該檔案的資料寫回 Framebuffer，這會使得螢幕恢復成剛才截圖時的畫面 [cite: 348, 349, 350]。 |
| **Q7: Difference between `/dev/fb0` and `/dev/fb1`?** | [cite_start]在許多開發板（如 i.MX6）上，SOC 支援多個獨立的顯示介面（例如 LCD 螢幕與 HDMI 輸出）。`/dev/fb0` 通常對應預設的主要顯示器（如 LCD），而 `/dev/fb1` 則對應第二個顯示介面（如 HDMI）[cite: 351, 352]。 |

---

## 5. References
* 114 Embedded System Design LAB 2 Manual
* OpenCV 3.4.7 Documentation
* Linux Framebuffer API (`<linux/fb.h>`)
* stb_image: Single-file image reading library
