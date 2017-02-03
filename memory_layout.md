### Panel register layout:
```
0x00: 22    // EvenFrameFieldOrder
0x01: 22    // OddFrameFieldOrder
0x02: 60    // IrPwmClkHz
0x03: ff    // EvenFrameIrPwmDuty
0x04: ff    // OddFrameIrPwmDuty
0x05: 00    // EvenFrameIrPwmDelay
0x06: 00    // OddFrameIrPwmDelay

0x07: 00 ff // Pwm1Start / Pwm1Width
0x09: 00 ff |
0x0b: 00 ff |
0x0d: 00 ff |
0x0f: 00 ff  _ second byte: IR intensity for horizontal stripe of ~ 68 pixels, first byte: always zero
0x11: 00 ff |
0x13: 00 ff |
0x15: 00 ff // Pwm8Start / Pwm8Width

0x17: 04    // WledPwmClkHz <- 0x00 for calibration, 0x04 for normal operation
0x18: 75    // WledPwmDuty <- 0xFF = full brightness, 0x00 = dark
0x19: 01 6f
0x1b: 38    <- rarely 0x68 or 0x78
0x1c: 99    // VsVideo01  global sensor parameters, only seems to work properly if all values equal
0x1d: 99    // VsVideo02  value & 0xF0 = VideoVoltage (0x01-0x0F), value & 0x0F = VS Bias (0x01-0x09)
0x1e: 99    // VsVideo03  VideoVoltage ~ black level (?)
0x1f: 99    // VsVideo04  VS Bias      ~ sensor gain (?)
0x20: 21    // CodeVersion
0x21: 2f    // RoDataCtrl1: & 0x03 = Visible Gain, & 0x0c = IR Gain, & 0x10 = Interlace Enable
0x22: 00
0x23: 00 
0x24: 00
0x25: 02    // SyncSel 
0x26: 01    // HighZSel
0x27: 00    // RollingFields 
0x28: 00    <- registers above this are not saved
0x29: 00 
0x2a: 75    <- or sometimes ff
0x2b: 80 50 <- rarely f0 50
0x2d: 00 00 <- rarely 10 00 or 50 00
0x2f: 27    // PanelTemp
```

### DDR memory layout (probably 128 MB):
```
0x4ff0000 - scratch buffer for transfers to SPI flash (4k)
0x4ff2000 - scratch buffer for transfers from SPI flash (4k)
0x5000000 - calibration data (0x10e000)
```

### I2C flash layout (8kB/64kBit):
```
0x0000: Cypress FX2 firmware (0x1E40)
0x1E40: 0xFF 0xFF 0xFF 0xFF ...
0x1F9B: 0x10 (?)
0x1F9C: "persistent key-value store" (64b)
0x1FDC: version string 1 (Cypress, 12b)
0x1FE8: version string 2 (FPGA, 12b)
0x1FF4: 0xFF 0xFF 0xFF 0xFF ...
```

### SPI flash layout (probably 4MB/32MBit):
```
0x000000 - FPGA bitstream (?)
0x190000 - calibration data (0x10e000)
```
