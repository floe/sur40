```
// put down white side of calibration board
RunColumnCorrection {

	SetCaptureMode(RawFullFrame)
	WledPwmClkHz = 0

	ComputeVideo0 {
		SetIrStrength(0x20)
		bulk read 2 (large) images from endpoint 0x82 
		(size == 1036800 = 960x540x2, takes ~ 0.5s / frame)
	}

	ComputeVsBias {
		SetIrStrength(0xFF)
		bulk read 2 (large) images from endpoint 0x82
		bulk read 2 (large) images from endpoint 0x82
		bulk read 2 (large) images from endpoint 0x82
	}

	SaveVideo0Bias

	// "acquiring gray reference image"
	SetIrStrength(0x80)
	bulk read 3 (large) images from endpoint 0x82

	// "acquiring white reference image"
	SetIrStrength(0xFF)
	bulk read 3 (large) images from endpoint 0x82

	// "resetting panel state"
	SetCaptureMode(Corrected)
	WriteToDDR(0x5000000,2048)
}

AccumulateWhite {

	ResetAllSettings {
		EP0Helper.GetParameters
		EP0Helper.SetParameters
		SetCaptureMode(Corrected)
	}

	// 40 c5 07 00 04 00 00 00
	[pause ~ 5.5 sec: "Accumulating White"]
	// 40 c5 07 00 00 00 00 00

	AdjustWhite {
		SetFpgaReadsEnable(true)
		StartFpgaDdrMemoryRead(0x05000000,0x0010e000)
		// 960 x 2 bytes + 128 bytes padding per line, 540 lines
		// maybe lower & upper threshold per pixel?

		CreateRegisterHelper
		WriteToDDR [bulk write 540 x 2048 bytes (with headers) to endpoint 0x08]
	}
}

// switch to black side of calibration board
AccumulateBlack {

	ResetAllSettings {
		EP0Helper.GetParameters (?)
		EP0Helper.SetParameters (?)
		SetCaptureMode(Corrected)
	}

	// 40 c5 07 00 02 00 00 00
	[pause ~ 4.5 sec: "Accumulating Black"]
	// 40 c5 07 00 00 00 00 00
}

SaveCalibrationData
```
