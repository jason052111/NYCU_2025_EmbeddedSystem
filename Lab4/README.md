# LAB4 Report: Audio System Cross-Compilation and Deployment

---

## 1. Commands Entered (Step-by-step)

### 1.1 Toolchain and staging rootfs setup
```bash
export TOOLCHAIN=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/bin
export PATH=$TOOLCHAIN:$PATH

export STAGING=$HOME/lab4_rootfs
mkdir -p "$STAGING/usr/local"

export CC=arm-linux-gnueabihf-gcc
export AR=arm-linux-gnueabihf-ar
export RANLIB=arm-linux-gnueabihf-ranlib
export STRIP=arm-linux-gnueabihf-strip

# compile-time include/lib search paths
export CPPFLAGS="-I$STAGING/usr/local/include"
export LDFLAGS="-L$STAGING/usr/local/lib"
export PKG_CONFIG_PATH="$STAGING/usr/local/lib/pkgconfig"
```
Common cleanup before rebuilding:
```bash
make distclean 2>/dev/null || true
```

### 1.2 zlib (build shared library)
```bash
cd zlib-1.2.3
make distclean 2>/dev/null || true

CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="-O2 -fPIC" ./configure -s
make -j$(nproc)
make install prefix=$STAGING/usr/local
```

### 1.3 alsa-lib (libasound.so)
```bash
cd alsa-lib-1.0.26
make distclean 2>/dev/null || true

CFLAGS="-O2 -fPIC" ./configure --host=arm-linux-gnueabihf --prefix=/usr/local
make -j$(nproc)
make DESTDIR=$STAGING install
```

### 1.4 alsa-utils (aplay/arecord)
To avoid extra dependencies (e.g., `ncurses` UI), disable `alsamixer`.
```bash
cd alsa-utils-1.0.26
make distclean 2>/dev/null || true

./configure --host=arm-linux-gnueabihf --prefix=/usr/local \
  --disable-nls --disable-xmlto --disable-alsamixer

make -j$(nproc)
make DESTDIR=$STAGING install
```

### 1.5 libid3tag (shared)
```bash
cd libid3tag-0.15.1b
make distclean 2>/dev/null || true

CFLAGS="-O2 -fPIC" ./configure --host=arm-linux-gnueabihf --prefix=/usr/local \
  --enable-shared --disable-static \
  CPPFLAGS="$CPPFLAGS" LDFLAGS="$LDFLAGS"

make -j$(nproc)
make DESTDIR=$STAGING install
```

### 1.6 libmad (shared) + ARM mode + disable dependency tracking
```bash
cd libmad-0.15.1b
make distclean 2>/dev/null || true

CFLAGS="-O2 -fPIC -marm" ./configure --host=arm-linux-gnueabihf --prefix=/usr/local \
  --enable-fpm=arm --enable-shared --disable-static --disable-dependency-tracking \
  CPPFLAGS="$CPPFLAGS" LDFLAGS="$LDFLAGS"

# single job build + override CFLAGS to avoid unsupported old GCC flags
make -j1 CFLAGS="-O2 -fPIC -marm"
make DESTDIR=$STAGING install
```

### 1.7 madplay (ARM executable)
```bash
cd madplay-0.15.2b
make distclean 2>/dev/null || true

CFLAGS="-O2 -marm" \
CPPFLAGS="$CPPFLAGS" \
LDFLAGS="$LDFLAGS" \
./configure --host=arm-linux-gnueabihf --prefix=/usr/local --disable-dependency-tracking

make -j$(nproc) || make -j1 CFLAGS="-O2 -marm"
make DESTDIR=$STAGING install
```

### 1.8 Verification commands
```bash
file $STAGING/usr/local/bin/aplay
file $STAGING/usr/local/bin/madplay
file $STAGING/usr/local/lib/libasound.so*
file $STAGING/usr/local/lib/libmad.so*
file $STAGING/usr/local/lib/libid3tag.so*
file $STAGING/usr/local/lib/libz.so*

arm-linux-gnueabihf-readelf -d $STAGING/usr/local/bin/aplay | grep NEEDED
arm-linux-gnueabihf-readelf -d $STAGING/usr/local/bin/madplay | grep NEEDED
```

---

## 2. Problems Encountered and Resolutions

| Problem | Resolution |
| :--- | :--- |
| **2.1 Copy to USB failed: `cannot create symbolic link ... Operation not permitted`**<br>When copying staging files to a USB drive, the filesystem (e.g., FAT/exFAT/NTFS) may not support symlinks like `libmad.so.0 -> libmad.so.0.2.1` or `arecord -> aplay`. Therefore `cp -rf` fails. | **Fix A (recommended):** Pack with tar (preserves symlinks), then extract on board.<br>`cd ~/lab4_rootfs`<br>`tar czf /tmp/lab4_usr_local.tgz usr/local`<br>`cp /tmp/lab4_usr_local.tgz /media/<user>/<usb>/Lab4/`<br>`sync`<br><br>*(On board: `tar xzf /path/to/lab4_usr_local.tgz -C /`)*<br><br>**Fix B:** Dereference symlinks during copy (works on USB but duplicates files).<br>`cd ~/lab4_rootfs`<br>`cp -rLf usr /media/<user>/<usb>/Lab4/` |
| **2.2 libmad build error: `-fforce-mem` is unrecognized**<br>The libmad Makefile contains old GCC optimization flags not supported by this toolchain. | **Fix:** Override CFLAGS at build time.<br>`make -j1 CFLAGS="-O2 -fPIC -marm"` |
| **2.3 libmad assembler error related to Thumb mode**<br>Errors such as "processor does not support Thumb mode" indicate the build used Thumb while the source expects ARM. | **Fix:** Force ARM mode using `-marm`.<br>`CFLAGS="-O2 -fPIC -marm" ./configure ...`<br>`make -j1 CFLAGS="-O2 -fPIC -marm"` |
| **2.4 Recording and Playback Connection Problem** | **Fix:** When performing audio recording, connect the input source to the **pink** port first and record the sound. After recording is finished, connect the speaker/headphone to the **green** port to play back the recorded audio. |

---

## 3. Deploy to Board and Run

### 3.1 Deploy
Copy `~/lab4_rootfs/usr/local` to the board and place it under `/usr/local`. Then ensure the runtime loader can find shared libraries:
```bash
export LAB4=/home/embedsky/Lab4/usr/local
export PATH=$LAB4/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

export ALSA_CONFIG_DIR=$LAB4/share/alsa
export ALSA_PLUGIN_DIR=$LAB4/lib/alsa-lib
```

### 3.2 Demo 1: Play MP3 (pipe WAV stream to aplay)
```bash
$LAB4/bin/madplay -o wav:- /home/embedsky/Lab4/music.mp3 | $LAB4/bin/aplay
```

### 3.3 Demo 2: Record and playback WAV
List audio devices and decide the correct `hw:X,Y`:
```bash
arecord -l
aplay -l
```

Record 5 seconds in CD format WAV:
```bash
$LAB4/bin/arecord -D hw:0,0 -f S16_LE -c 2 -r 44100 -t wav -d 5 test.wav
```

Playback:
```bash
aplay test.wav
```

---

## 4. Signal Processing Procedure for Playing an MP3 File

The playback pipeline is:
**MP3 (compressed) ➔ decode to PCM ➔ send PCM to ALSA ➔ sound output**

1. **MP3 input**: MP3 is a lossy compressed bitstream organized in frames (MPEG Layer III).
2. **Decoding (madplay + libmad)**:
   * Parse frames and side information
   * Huffman/entropy decoding reconstructs spectral coefficients
   * Dequantization + inverse transforms (IMDCT) convert frequency domain blocks to time domain
   * Synthesis filterbank outputs final **PCM samples** (e.g., 16-bit, 44.1kHz, stereo)
3. **WAV stream output**: `madplay -o wav:-` wraps PCM into a WAV-formatted stream and writes to stdout.
4. **Playback (aplay + alsa-lib + driver)**:
   * `aplay` reads WAV header for sample rate / channels / bit depth
   * `alsa-lib (libasound)` configures the PCM device
   * ALSA driver sends PCM to sound hardware (codec/I2S/USB), producing audible output

---

## 5. References
* [ALSA Project](https://www.alsa-project.org/wiki/Main_Page)
* [zlib](https://zlib.net/)
* [libmad (MAD)](https://www.underbit.com/products/mad/)
* [madplay](https://www.underbit.com/products/mad/)
* [libid3tag](https://www.underbit.com/products/mad/)
