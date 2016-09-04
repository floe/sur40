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

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "surface.h"

int timeout = 1000;


#define ENDPOINT_VIDEO 0x82
#define ENDPOINT_BLOBS 0x86

#define ENDPOINT_CALIB_READ 0x84
#define ENDPOINT_CALIB_SEND 0x08

#define VIDEO_HEADER_MAGIC 0x46425553
#define VIDEO_PACKET_SIZE  16384


/************************* HELPER FUNCTIONS *************************/

usb_dev_handle* usb_get_device_handle( int vendor, int product ) {

	usb_init();
	usb_find_busses();
	usb_find_devices();

	struct usb_bus* busses = usb_get_busses();
		
	for (struct usb_bus* bus = busses; bus; bus = bus->next) {
		for (struct usb_device* dev = bus->devices; dev; dev = dev->next) {
			if ((dev->descriptor.idVendor == vendor) && (dev->descriptor.idProduct == product)) {
				usb_dev_handle* handle = usb_open(dev);
				if (!handle) return 0;
				if (usb_claim_interface( handle, 0 ) < 0) return 0;
				return handle;
			}
		}
	}
	return 0;
}


/************************** CONTROL STUFF ***************************/

#define SURFACE_GET_VERSION 0xb0 // 12 bytes string

#define SURFACE_UNKNOWN1    0xb3 //  5 bytes - sent only once during setup
#define SURFACE_UNKNOWN2    0xc1 // 24 bytes - sent only once during setup

#define SURFACE_GET_STATUS  0xc5 //  4 bytes state (?) - sent once per second, response usually 0x00000000
#define SURFACE_GET_SENSORS 0xb1 //  8 bytes sensors   - sent once per second, response probably 0xZZXXYYTT

// get version info
void surface_get_version( usb_dev_handle* handle, uint16_t index ) {
	uint8_t buf[13]; buf[12] = 0;
	usb_control_msg( handle, 0xC0, SURFACE_GET_VERSION, 0x00, index, (char*)buf, 12, timeout );
	printf("version string 0x%02x: %s\n", index, buf);
}

// get device status word
int surface_get_status( usb_dev_handle* handle ) {
	uint8_t buf[4];
	usb_control_msg( handle, 0xC0, SURFACE_GET_STATUS, 0x00, 0x00, (char*)buf, 4, timeout );
	return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

// get sensor status
void surface_get_sensors( usb_dev_handle* handle ) {
	surface_sensors sensors;
	usb_control_msg( handle, 0xC0, SURFACE_GET_SENSORS, 0x00, 0x00, (char*)(&sensors), 8, timeout );
	printf("temp: %d x: %d y: %d z: %d\n",sensors.temp,sensors.acc_x,sensors.acc_y,sensors.acc_z);
}

// other commands
void surface_command( usb_dev_handle* handle, uint16_t cmd, uint16_t index, uint16_t len ) {
	uint8_t buf[24];
	usb_control_msg( handle, 0xC0, cmd, 0x00, index, (char*)buf, len, timeout );
	printf("command 0x%02x,0x%02x: ", cmd, index ); 
	for (int i = 0; i < len; i++) printf("0x%02x ", buf[i]);
	printf("\n");
}

// mindless repetition of the microsoft driver's init sequence.
// quite probably unnecessary, but leave it like this for now.
void surface_init( usb_dev_handle* handle ) {

	printf("microsoft surface 2.0 open source driver 0.0.1\n");

 	surface_get_version(handle, 0x00);
	surface_get_version(handle, 0x01);
	surface_get_version(handle, 0x02);

	surface_command(handle, SURFACE_UNKNOWN2, 0x00, 24 );
	surface_command(handle, SURFACE_UNKNOWN1, 0x00,  5 );

	surface_get_version(handle, 0x03);
}

/**************************** CALIBRATION ***************************/

#define SURFACE_PEEK        0xc4 // read 48 bytes internal state - perhaps NV memory?
#define SURFACE_POKE        0xc5 // used for calibration setup - seems to access NV memory like c4, or maybe I2C in general
	#define SP_INIT1 0x05
	#define SP_INIT2 0x07
	#define SP_INIT3 0x17
	#define SP_NVW1  0x32 // always with index 0x96 or 0xae, indicates nv write
	#define SP_NVW2  0x72 // index == offset into nv memory
	#define SP_NVW3  0xb2 // index == value to write into nv memory

#define SURFACE_UNKNOWN3 0xb4 // read 64 bytes, get 30
#define SURFACE_UNKNOWN4 0xb6 // write 42 bytes

#define SURFACE_COMMIT 0xc3 // maybe trigger calibration save? send 4 bytes
#define SURFACE_STATUS 0xb5 // read 64 bytes, get 41, used for completion polling

void surface_peek( usb_dev_handle* handle ) {
	uint8_t buf[48];
	usb_control_msg( handle, 0xC0, SURFACE_PEEK, 0x00, 0x00, (char*)buf, 48, timeout );
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 16; i++) printf("0x%02x ", buf[j*16+i]);
		printf("\n");
	}
}

void surface_calib_setup( usb_dev_handle* handle ) {
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_INIT1, 0x04, NULL, 0, timeout );
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x01, NULL, 0, timeout );
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_INIT3, 0x85, NULL, 0, timeout );
}

void surface_calib_finish( usb_dev_handle* handle ) {
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_INIT1, 0x00, NULL, 0, timeout );
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_INIT2, 0x00, NULL, 0, timeout );
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_INIT3, 0x80, NULL, 0, timeout );
}

void surface_poke( usb_dev_handle* handle, uint8_t offset, uint8_t value ) {
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_NVW1,   0x96, NULL, 0, timeout );
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_NVW2, offset, NULL, 0, timeout );
	usb_control_msg( handle, 0x40, SURFACE_POKE, SP_NVW3,  value, NULL, 0, timeout );
}

// value was: 0xc7, 0xb7, 0xa7, 0x97, 0x98, 0x99
void surface_poke_sequence1( usb_dev_handle* handle, uint8_t value ) {
	for (int i = 0; i < 4; i++)
		surface_poke( handle, 0x1c+i, value );
}

// value was: 0x20, 0xff, 0x80, 0xff
void surface_poke_sequence2( usb_dev_handle* handle, uint8_t value ) {
	for (int i = 0; i < 8; i++)
		surface_poke( handle, 0x08+(2*i), value );
}

void surface_calib_start( usb_dev_handle* handle ) {

	surface_peek( handle );

	surface_calib_setup( handle );
	surface_poke( handle, 0x17, 0x00 );

	surface_poke_sequence1( handle, 0xc7 );

	surface_peek( handle );

	surface_poke_sequence2( handle, 0x20 );
}

void surface_calib_end( usb_dev_handle* handle ) {
	surface_calib_finish( handle );
	surface_poke( handle, 0x17, 0x04 );
}



/************************** IMAGE FUNCTIONS *************************/

int surface_get_image( usb_dev_handle* handle, uint8_t* image ) {

	uint8_t buffer[512];
	int result, bufpos = 0;

	result = usb_bulk_read( handle, ENDPOINT_VIDEO, (char*)buffer, sizeof(buffer), timeout );
	if (result != sizeof(surface_image)) { printf("transfer size mismatch\n"); return -1; }

	surface_image* header = (surface_image*)buffer;
	if (header->magic != VIDEO_HEADER_MAGIC) { printf("image magic mismatch\n"); return -1; }
	if (header->size  != VIDEO_BUFFER_SIZE ) { printf("image size  mismatch\n"); return -1; }

	while (bufpos < VIDEO_BUFFER_SIZE) {
		result = usb_bulk_read( handle, ENDPOINT_VIDEO, (char*)(image+bufpos), VIDEO_PACKET_SIZE, timeout );
		if (result < 0) { printf("error in usb_bulk_read\n"); return result; }
		bufpos += result;
	}

	return header->timestamp;
}


/************************** BLOB FUNCTIONS **************************/

int surface_get_blobs( usb_dev_handle* handle, surface_blob* outblob ) {

	uint8_t buffer[512];
	uint32_t packet_id;
	int result;

	int need_blobs = -1;
	int current = 0;

	surface_header* header = (surface_header*)buffer;
	surface_blob*   inblob = (surface_blob*)(buffer+sizeof(surface_header));

	do {

		result = usb_bulk_read( handle, ENDPOINT_BLOBS, (char*)(buffer), sizeof(buffer), timeout ) - sizeof(surface_header);
		if (result < 0) { printf("error in usb_bulk_read\n"); return result; }
		if (result % sizeof(surface_blob) != 0) { printf("transfer size mismatch\n"); return -1; }
		//printf("id: %x count: %d\n",header->packet_id,header->count);

		// first packet
		if (need_blobs == -1) {
			need_blobs = header->count;
			packet_id = header->packet_id;
		}

		// sanity check. when video data is also being retrieved, the packet ID
		// will usually increase in the middle of a series instead of at the end.
		if (packet_id != header->packet_id) { printf("packet ID mismatch\n"); }

		int packet_blobs = result / sizeof(surface_blob);

		for (int i = 0; i < packet_blobs; i++) outblob[current++] = inblob[i];

	}	while (current < need_blobs);

	return need_blobs;
}

