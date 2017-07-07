/*
 * microsoft surface 2.0 open source driver 0.0.1
 *
 * Copyright (c) 2012 by Florian Echtler <floe@butterbrot.org>
 * Licensed under GNU General Public License (GPL) v2 or later
 *
 * this is so experimental that the warranty shot itself.
 * so don't expect any.
 *
 */

#include "surface.h"
#ifndef WIN32
#include <unistd.h>
#else
#include <windows.h>

void usleep(__int64 usec) 
{ 
    HANDLE timer; 
    LARGE_INTEGER ft; 

    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer); 
}
#endif

int timeout = 1000;

#define ENDPOINT_VIDEO 0x82
#define ENDPOINT_BLOBS 0x86

#define ENDPOINT_DDR_READ  0x84
#define ENDPOINT_DDR_WRITE 0x08

#define VIDEO_HEADER_MAGIC 0x46425553
#define VIDEO_PACKET_SIZE  16384


/************************* HELPER FUNCTIONS *************************/

bool kernelDriverDetached = false;

libusb_device_handle* sur40_get_device_handle() {

	int result;

	result = libusb_init(NULL);
	if(result!=0) return NULL;

	libusb_device_handle* handle = libusb_open_device_with_vid_pid(NULL,ID_MICROSOFT,ID_SURFACE);
	if (!handle) return NULL;

	if (libusb_kernel_driver_active(handle, 0)) {
    		result = libusb_detach_kernel_driver(handle, 0);
    		if (result == 0) kernelDriverDetached = true;
		else return NULL;
	}

	if (libusb_claim_interface( handle, 0 ) < 0) return NULL;
	return handle;
}

void sur40_close_device(libusb_device_handle* handle) {

	libusb_release_interface(handle, 0);

	if (kernelDriverDetached) libusb_attach_kernel_driver(handle, 0);

	libusb_exit(NULL);
}


/************************** CONTROL STUFF ***************************/

#define SURFACE_GET_VERSION 0xb0 // 12 bytes string

#define SURFACE_ACCEL_CAPS  0xb3 //  5 bytes - sent only once during setup
#define SURFACE_SENSOR_CAPS 0xc1 // 24 bytes - sent only once during setup

#define SURFACE_GET_STATUS  0xc5 //  4 bytes state (?) - sent once per second, response usually 0x00000000
#define SURFACE_GET_SENSORS 0xb1 //  8 bytes sensors   - sent once per second, response probably 0xZZXXYYTT

// get version info
void surface_get_version( libusb_device_handle* handle, uint16_t index , bool verbose = false) {
	uint8_t buf[13]; buf[12] = 0;
	libusb_control_transfer( handle, 0xC0, SURFACE_GET_VERSION, 0x00, index, buf, 12, timeout );
	if (verbose) printf("version string 0x%02x: %s\n", index, buf);
}

// get device status word
int surface_get_status( libusb_device_handle* handle ) {
	uint8_t buf[4];
	libusb_control_transfer( handle, 0xC0, SURFACE_GET_STATUS, 0x00, 0x00, buf, 4, timeout );
	return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

// get sensor status
void surface_get_sensors( libusb_device_handle* handle, bool verbose = false ) {
	surface_sensors sensors;
	libusb_control_transfer( handle, 0xC0, SURFACE_GET_SENSORS, 0x00, 0x00, (unsigned char*)(&sensors), 8, timeout );
	if (verbose) printf("temp: %d x: %d y: %d z: %d\n",sensors.temp,sensors.acc_x,sensors.acc_y,sensors.acc_z);
}

// other commands
void surface_command( libusb_device_handle* handle, uint16_t cmd, uint16_t index, uint16_t len, bool verbose = false ) {
	uint8_t buf[24];
	libusb_control_transfer( handle, 0xC0, cmd, 0x00, index, buf, len, timeout );
	if (verbose) {
		printf("command 0x%02x,0x%02x: ", cmd, index );
		for (int i = 0; i < len; i++) printf("0x%02x ", buf[i]);
		printf("\n");
	}
}

// mindless repetition of the microsoft driver's init sequence.
// quite probably unnecessary, but leave it like this for now.
void surface_init( libusb_device_handle* handle, bool verbose ) {

	if (verbose) printf("microsoft surface 2.0 open source driver 0.0.1\n");

 	surface_get_version(handle, 0x00);
	surface_get_version(handle, 0x01);
	surface_get_version(handle, 0x02);

	surface_command(handle, SURFACE_SENSOR_CAPS, 0x00, 24 );
	surface_command(handle, SURFACE_ACCEL_CAPS,  0x00,  5 );

	surface_get_version(handle, 0x03);
}

/**************************** CALIBRATION ***************************/

#define SURFACE_PEEK     0xc4 // read 48 bytes internal state - perhaps controller memory?
#define SURFACE_POKE     0xc5 // used for calibration setup - seems to access memory like c4, or maybe I2C in general

	#define SP_INIT1 0x05 // host interface module
		// value 0x00 = default mode, corrected frame
		// value 0x04 = interlaced full frame (double height), used for calibration
		// value 0x08 = top half of full frame
		// value 0x10 = bottom half of full frame

	#define SP_INIT2 0x07 // image corrector module
		// value 0x00 = default mode
		// value 0x01 = disable image correction, used for calibration
		// value 0x02 = accumulate black (wait 4.5 seconds)
		// value 0x04 = accumulate white (wait 5.5 seconds)

	// TODO: actually, this _may_ mean parameter 0x01 for module 0x07, i.e. also the image corrector
	#define SP_INIT3 0x17 // ... unknown ...
		// value 0x80 = default mode
		// value 0x85 = raw mode (?), similar to disabling image corrector, used for calibration
		// value 0x83 = "FOM" mode (?), similar to 0x85, used for capture file generation

	// this sequence apparently accesses I2C: index 0x96 == panel, 0xae == panel eeprom
	#define SP_NVW1  0x32 // always with index 0x96 or 0xae, indicates memory write (0xae == permanent write?)
	#define SP_NVW2  0x72 // index == offset into memory
	#define SP_NVW3  0xb2 // index == value to write into memory

#define SURFACE_DDR_READ_ENABLE  0x40,0xb1 // enable/disable read from FPGA memory
#define SURFACE_DDR_READ_REQUEST 0x40,0xc4 // read buffer from FPGA memory

#define SURFACE_GET_PARAMS 0xc0,0xb4 // read key-value store: read 64 bytes, get 30
#define SURFACE_SET_PARAMS 0x40,0xb6 // write key-value store: write 42 bytes

#define SURFACE_BUS_STATUS   0xc0,0xb5 // read 64 bytes, used for SPI completion polling
#define SURFACE_QUERY_SPI    0xc0,0xc3 // query SPI flash size

#define SURFACE_SPI_TRANSFER_REQUEST 0x40,0xc3 // transfer data between SPI flash and DDR
#define SURFACE_SPI_TRANSFER_ENABLE  0x40,0xb1 // enable SPI flash access

#define SURFACE_SPI_PAGE_SIZE 4096

#define SURFACE_I2C_READ     0xc0,0xb6 // read USB firmware I2C eeprom
#define SURFACE_I2C_WRITE    0x40,0xb0 // write ...

/* rough calibration sequence:
	- column correction (with white board)
	- accumulate white (with white board)
	- accumulate black (with black board)
*/

void surface_peek( libusb_device_handle* handle ) {
	uint8_t buf[48];
	libusb_control_transfer( handle, 0xC0, SURFACE_PEEK, 0x00, 0x00, buf, 48, timeout );
	for (int i = 0; i < 48; i++) printf("0x%02x ", buf[i]);
	printf("\n");
}

void surface_calib_accumulate_white( libusb_device_handle* handle ) {
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x04, NULL, 0, timeout ); // AccumulateWhite
	usleep(5500000);
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x00, NULL, 0, timeout ); // "normal mode"
	// ... followed by AdjustWhite
}

void surface_calib_accumulate_black( libusb_device_handle* handle ) {
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x02, NULL, 0, timeout ); // AccumulateBlack
	usleep(4500000);
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x00, NULL, 0, timeout ); // "normal mode"
}

void surface_calib_setup( libusb_device_handle* handle ) {
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT1, 0x04, NULL, 0, timeout ); // CaptureMode = RawFullFrame
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x01, NULL, 0, timeout );
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT3, 0x85, NULL, 0, timeout );
}

void surface_calib_finish( libusb_device_handle* handle ) {
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT1, 0x00, NULL, 0, timeout ); // CaptureMode = Corrected
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x00, NULL, 0, timeout );
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_INIT3, 0x80, NULL, 0, timeout );
}

void surface_poke( libusb_device_handle* handle, uint8_t offset, uint8_t value ) {
	// writes i2c register, device 0x96 = panel, device 0xae = eeprom
	uint8_t index = 0x96; // 0xae for permanent write
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_NVW1,  index, NULL, 0, timeout ); usleep(20000);
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_NVW2, offset, NULL, 0, timeout ); usleep(20000);
	libusb_control_transfer( handle, 0x40, SURFACE_POKE, SP_NVW3,  value, NULL, 0, timeout ); usleep(20000);
}

// ( ... 0x01, 0x80, 0x00 ) == poll until FPGA is no longer in GPIO mode, used for SPI transfer completion
// ( ... 0x01, 0x02, 0x02 ) == poll until FPGA is no longer in reset mode, used after firmware upgrade
int surface_poll_completion( libusb_device_handle* handle, int tries, int offset, int mask, int value ) {
	uint8_t buffer[64];
	for ( int i = 0; i < tries; i++ ) {
		usleep(5000);
		int result = libusb_control_transfer( handle, SURFACE_BUS_STATUS, 0, 0, buffer, sizeof(buffer), timeout );
		if (result < 0) { printf("error in BUS_STATUS\n"); return result; }
		printf("completion polling: offset %d == %02x\n",offset,buffer[offset]);
		if ((buffer[offset] & mask) == value) return i;
	}
	return -1;
}

int surface_read_ddr( libusb_device_handle* handle, uint8_t* buffer, uint32_t bufsize, uint32_t target_offset, uint32_t blocksize );

// likely 0x190 pages of fpga bitstream (?), calibration starts at page 0x190
int surface_read_spi_flash( libusb_device_handle* handle, uint16_t page, uint8_t buffer[4096] ) {

	uint32_t request[2] = { 0x04ff2000, 4096 };
	int result, direction = 0; // SPI to DDR

	result = libusb_control_transfer( handle, SURFACE_SPI_TRANSFER_ENABLE, 0, true, NULL, 0, timeout); // enable read from SPI flash?
	if (result < 0) { printf("error in FPGA_READS\n"); return result; }

	result = libusb_control_transfer( handle, SURFACE_SPI_TRANSFER_REQUEST, page, direction, (unsigned char*)request, 4, timeout );
	if (result < 0) { printf("error in SPI_TRANSFER\n"); return result; }

	result = surface_poll_completion( handle, 20, 0x01, 0x80, 0x00 );
	if (result < 0) { printf("error in poll_completion\n"); return result; }

	return surface_read_ddr( handle, buffer, request[1], request[0], request[1] );
}

// query SPI flash size in MB (stores FPGA bitstream and calibration)
// seems to be broken, always returns timeout (and is also never used in original code)
int surface_spi_flash_size_mb( libusb_device_handle* handle ) {
	uint8_t buffer[64];
	uint32_t request[1] = { 0 };
	int result = libusb_control_transfer( handle, SURFACE_QUERY_SPI, 0, 0, buffer, sizeof(buffer), timeout );
	if ((result < 0) || (buffer[0] == 0)) {
		libusb_control_transfer( handle, SURFACE_SPI_TRANSFER_REQUEST, 0, 0, (unsigned char*)request, sizeof(request), timeout );
		usleep(100000);
		result = libusb_control_transfer( handle, SURFACE_QUERY_SPI, 0, 0, buffer, sizeof(buffer), timeout );
	}
	return (result < 0 ? result : buffer[0] );
}

// retrieves 8k of Cypress FX2 firmware, stored in 64k I2C EEPROM
int surface_read_usb_flash( libusb_device_handle* handle, uint8_t buffer[8192] ) {
	int offset = 0;
	while (offset < 8192) {
		int result = libusb_control_transfer( handle, SURFACE_I2C_READ, offset,	0, buffer+offset, 64, timeout );
		if (result < 64) return result;
		offset += result;
	}
	return offset;
}

#ifdef DANGER_WILL_ROBINSON
// write Cypress FX2 USB firmware to I2C EEPROM
int surface_write_usb_flash( libusb_device_handle* handle, uint8_t buffer[8192] ) {
	int offset = 0;
	while (offset < 8192) {
		int result = libusb_control_transfer( handle, SURFACE_I2C_WRITE, offset, 0, buffer+offset, 32, timeout );
		if (result < 32) return result;
		offset += result;
	}
	return offset;
}
#endif

int surface_read_ddr( libusb_device_handle* handle, uint8_t* buffer, uint32_t bufsize, uint32_t target_offset, uint32_t blocksize ) {

	uint32_t request[2] = { target_offset, bufsize };
	uint32_t bufpos = 0;
	int result = 0;

	libusb_control_transfer( handle, SURFACE_DDR_READ_ENABLE,  0, true, NULL, 0, timeout );
	libusb_control_transfer( handle, SURFACE_DDR_READ_REQUEST, 0, 0, (unsigned char*)request, sizeof(request), timeout );

	while (bufpos < bufsize) {
		int rest = bufsize-bufpos; if (rest > blocksize) rest = blocksize;
		libusb_bulk_transfer( handle, ENDPOINT_DDR_READ, buffer+bufpos, rest, &result, timeout );
		if (result < 0) { printf("error in libusb_bulk_transfer\n"); return result; }
		bufpos += result;
	}

	return bufpos;
}

int surface_write_ddr( libusb_device_handle* handle, uint8_t* buffer, uint32_t bufsize, uint32_t target_offset, uint32_t blocksize ) {

	uint32_t request[2] = { target_offset, blocksize };
	uint32_t bufpos = 0;
	int result = 0;

	while (bufpos < bufsize) {
		int rest = bufsize-bufpos; if (rest > blocksize) rest = blocksize;
		libusb_bulk_transfer( handle, ENDPOINT_DDR_WRITE, (unsigned char*)request, sizeof(request), &result, timeout );
		if (result < 0) { printf("error in libusb_bulk_transfer\n"); return result; }
		libusb_bulk_transfer( handle, ENDPOINT_DDR_WRITE, buffer+bufpos, rest, &result, timeout );
		if (result < 0) { printf("error in libusb_bulk_transfer\n"); return result; }
		bufpos += result;
		request[0] += result;
	}

	return bufpos;
}

int surface_write_calib( libusb_device_handle* handle, surface_calib* calib ) {
	return surface_write_ddr( handle, (uint8_t*)calib, sizeof(surface_calib), 0x05000000, 2048 );
}

int surface_read_calib( libusb_device_handle* handle, surface_calib* calib ) {
	return surface_read_ddr( handle, (uint8_t*)calib, sizeof(surface_calib), 0x05000000, 2048 );
}

// value was: 0xc7, 0xb7, 0xa7, 0x97, 0x98, 0x99
// value & 0xF0 = VideoVoltage (0-15), value & 0x0F = VSBias (0-9)
// default = 0xc7
// part 1 of calibration (VideoVoltage): adjust avg. brightness to between 5 and 16
// - start with value = 0x0c
// - if avg. brightness < 5: value++
// - else if avg. brightness < 16: break
// - else value--
// part 2 of calibration (VSBias):
// - calculate avg. brightness, count all pixels > 235
// - if avg. < 155: value++
// - else if avg. < 210 && % of bright pixels < 4.1: break
// - else value--
void surface_set_vsvideo( libusb_device_handle* handle, uint8_t value ) {
	for (int i = 0; i < 4; i++)
		surface_poke( handle, 0x1c+i, value );
}

// value was: 0x20, 0xff, 0x80, 0xff
void surface_set_irlevel( libusb_device_handle* handle, uint8_t value ) {
	for (int i = 0; i < 8; i++)
		surface_poke( handle, 0x08+(2*i), value );
}

void surface_set_preprocessor( libusb_device_handle* handle, uint8_t value )
{
	uint8_t setting_07[2] = { 0x01, 0x00 };
	uint8_t setting_17[2] = { 0x85, 0x80 };

	if (value > 1) return;

	surface_poke(handle, 0x07, setting_07[value]);
	surface_poke(handle, 0x17, setting_17[value]);
}

void surface_calib_start( libusb_device_handle* handle ) {
	surface_calib_setup( handle );
	surface_poke( handle, 0x17, 0x00 ); // WledPwmClkHz = 0
	surface_peek( handle );
}

void surface_calib_end( libusb_device_handle* handle ) {
	surface_calib_finish( handle );
	surface_poke( handle, 0x17, 0x04 ); // WledPwmClkHz = 4
	surface_peek( handle );
}



/************************** IMAGE FUNCTIONS *************************/

int surface_get_image( libusb_device_handle* handle, uint8_t* image, unsigned int bufsize ) {

	uint8_t buffer[512];
	uint32_t bufpos = 0;
	int result = 0;

	libusb_bulk_transfer( handle, ENDPOINT_VIDEO, buffer, sizeof(buffer), &result, timeout );
	if (result <  sizeof(surface_image)) { printf("transfer size mismatch\n"); return -1; }

	surface_image* header = (surface_image*)buffer;

	// printf("header: magic %08x, id %d, size %d, timestamp %d\n", header->magic, header->packet_id, header->size, header->timestamp);

	if (header->magic != VIDEO_HEADER_MAGIC) { printf("image magic mismatch\n"); return -1; }
	if (header->size  != bufsize           ) { printf("image size  mismatch\n"); return -1; }

	if (result > sizeof(surface_image)) {
		bufpos = result-sizeof(surface_image);
		memmove(buffer,buffer+sizeof(surface_image),bufpos);
	}

	while (bufpos < bufsize) {
		int rest = bufsize-bufpos; if (rest > VIDEO_PACKET_SIZE) rest = VIDEO_PACKET_SIZE;
		libusb_bulk_transfer( handle, ENDPOINT_VIDEO, image+bufpos, rest, &result, timeout );
		if (result < 0) { printf("error in libusb_bulk_transfer\n"); return result; }
		bufpos += result;
	}

	return header->timestamp;
}


/************************** BLOB FUNCTIONS **************************/

int surface_get_blobs( libusb_device_handle* handle, surface_blob* outblob ) {

	uint8_t buffer[512];
	//uint32_t packet_id;
	int result;

	int need_blobs = -1;
	int current = 0;

	surface_header* header = (surface_header*)buffer;
	surface_blob*   inblob = (surface_blob*)(buffer+sizeof(surface_header));

	do {

		libusb_bulk_transfer( handle, ENDPOINT_BLOBS, buffer, sizeof(buffer), &result, timeout );
		result-= sizeof(surface_header);
		if (result < 0) { printf("error in libusb_bulk_transfer\n"); return result; }
		if (result % sizeof(surface_blob) != 0) { printf("transfer size mismatch\n"); return -1; }
		//printf("id: %x count: %d\n",header->packet_id,header->count);

		// first packet
		if (need_blobs == -1) {
			need_blobs = header->count;
			//packet_id = header->packet_id;
		}

		// sanity check. when video data is also being retrieved, or the number of
		// blobs is >= 9, the packet ID will usually increase in the middle of a
		// series instead of at the end. however, the data is still consistent, so
		// the packet ID is probably just valid for the first packet in a series.
		// if (packet_id != header->packet_id) { printf("packet ID mismatch\n"); }

		int packet_blobs = result / sizeof(surface_blob);

		for (int i = 0; i < packet_blobs; i++) outblob[current++] = inblob[i];

	}	while (current < need_blobs);

	return need_blobs;
}
