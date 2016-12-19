```
0x00: 22 22 
0x02: 60 ff 
0x04: ff 00 
0x06: 00 00 
0x08: ff 00 \
0x0a: ff 00 |
0x0c: ff 00 |
0x0e: ff 00  _ first byte: gain for horizontal stripe of ~ 68 pixels, second byte: always zero
0x10: ff 00 |
0x12: ff 00 |
0x14: ff 00 |
0x16: ff    /
0x17: 04    <- 0x00 for calibration, 0x04 for normal operation
0x18: 75    <- changes dynamically, status byte?
0x19: 01 6f
0x1b: 38    <- rarely 0x68 or 0x78
0x1c: 99 99 \_ somehow related to gain 
0x1e: 99 99 / 
0x20: 21 2f 
0x22: 00 00 
0x24: 00 02 
0x26: 01 00 
0x28: 00 00 
0x2a: 75    <- or sometimes ff
0x2b: 80 50 <- rarely f0 50
0x2d: 00 00 <- rarely 10 00 or 50 00
0x2f: 27    <- maybe some sort of counter 
```
