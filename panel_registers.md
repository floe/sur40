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
0x18: 75    <- changes dynamically, status byte?
0x19: 01 6f
0x1b: 38    <- rarely 0x68 or 0x78
0x1c: 99 99 // VsVideo01 / VsVideo02 \_ some sort of global gain setting? probably needs to be > 0x80, and only seems to work properly if all values equal
0x1e: 99 99 // VsVideo03 / VsVideo04 /  & 0xF0 = VideoVoltage, & 0x0F = VS Bias
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

The following sequence (first offset - value) is executed during calibration. Looks somehow like a search
algorithm: first select the rough overall gain in 0x1c-0x1f, then adjust the fine gain in 0x08-0x16.
```
1c c7
08 20
1c b7
1c a7
1c 97
08 ff
1c 98
1c 99
1c 99 <- switch to dark side of calibration board happens here?
08 80
08 ff
```
