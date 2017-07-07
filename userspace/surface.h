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

#ifndef _SURFACE_H_
#define _SURFACE_H_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <libusb-1.0/libusb.h>

#define ID_MICROSOFT 0x045e
#define ID_SURFACE   0x0775

#define VIDEO_RES_X 960
#define VIDEO_RES_Y 540

#define VIDEO_BUFFER_SIZE  VIDEO_RES_X * VIDEO_RES_Y


// read 512 bytes from endpoint 0x86 -> get header + blobs
struct surface_header {

	uint16_t type;  // always 0x0001
	uint16_t count; // count of blobs (if == 0: continue prev. packet ID)

	uint32_t packet_id;

	uint32_t timestamp; // milliseconds (increases by 16 or 17 each frame)
	uint32_t unknown;   // "epoch?" always 02/03 00 00 00 

};


struct surface_blob {

	uint16_t blob_id;

	uint8_t action;     // 0x02 = enter/exit, 0x03 = update (?) 
	uint8_t type;       // 0x01 blob, 0x02 finger, 0x04 tag (bitmask)

	uint16_t bb_pos_x;  // upper left corner of bounding box
	uint16_t bb_pos_y;

	uint16_t bb_size_x; // size of bounding box
	uint16_t bb_size_y;

	uint16_t pos_x;     // finger tip position
	uint16_t pos_y;

	uint16_t ctr_x;     // centroid position
	uint16_t ctr_y;

	uint16_t axis_x;    // somehow related to major/minor axis, mostly:
	uint16_t axis_y;    // axis_x == bb_size_y && axis_y == bb_size_x 

	float    angle;     // orientation in radians relative to x axis
	uint32_t area;      // size in pixels/pressure (?)

	uint8_t padding[24];

	uint32_t tag_id;
	uint32_t unknown;
};


// read 512 bytes from endpoint 0x82 -> get header below
// continue reading 16k blocks until header.size bytes read
struct surface_image {
	uint32_t magic;     // "SUBF"
	uint32_t packet_id;
	uint32_t size;      // always 0x0007e900 = 960x540
	uint32_t timestamp; // milliseconds (increases by 16 or 17 each frame)
	uint32_t unknown;   // "epoch?" always 02/03 00 00 00 
};


// read 8 bytes using control message 0xc0,0xb1,0x00,0x00
struct surface_sensors {
	uint16_t temp;
	uint16_t acc_x;
	uint16_t acc_y;
	uint16_t acc_z;
};


// internal calibration for one row
#ifdef WIN32
struct surface_row_calib {
#else
struct __attribute__((__packed__)) surface_row_calib {
#endif
	uint16_t calib[VIDEO_RES_X]; // MSB = black level, LSB = white level?

	// fields below are only valid in first row, 0xFF otherwise
	uint8_t  padding0[20];

	uint8_t  version[16];   // version string ("IFC2.0.0.0")
	uint8_t  padding1[4];

	uint64_t time_local;    // calibration time (local) as 64-bit integer
	uint8_t  padding2[2];

	uint64_t time_utc;      // calibration time (UTC) as 64-bit integer
	uint8_t  padding3[2];

	uint8_t  temperature;   // panel temperature at calibration time
	uint8_t  padding4[67];
};

// in first row:
// calib[0] = 0xCACA
// calib[1] = b1.......1.......
// paddingN[..] = 0x00

// internal calibration block
struct surface_calib {
	surface_row_calib row[VIDEO_RES_Y];
};

// helper to find a device by vendor and product
libusb_device_handle* sur40_get_device_handle();
void sur40_close_device(libusb_device_handle* handle);

// get device status word
int surface_get_status( libusb_device_handle* handle );

// get sensor status
void surface_get_sensors( libusb_device_handle* handle );

// initialization sequence
void surface_init( libusb_device_handle* handle, bool verbose = false );

// retrieve raw data from surface
int surface_get_image( libusb_device_handle* handle, uint8_t* image, unsigned int bufsize = VIDEO_BUFFER_SIZE );
int surface_get_blobs( libusb_device_handle* handle, surface_blob* blob );

// calibration: start/finish
void surface_calib_start( libusb_device_handle* handle );
void surface_calib_end( libusb_device_handle* handle );

void surface_set_vsvideo( libusb_device_handle* handle, uint8_t value = 0xA8 );
void surface_set_irlevel( libusb_device_handle* handle, uint8_t value = 0xFF );
void surface_set_preprocessor( libusb_device_handle* handle, uint8_t value = 0x01);
void surface_peek( libusb_device_handle* handle );

int surface_read_calib( libusb_device_handle* handle, surface_calib* calib );
int surface_write_calib( libusb_device_handle* handle, surface_calib* calib );

int surface_read_usb_flash( libusb_device_handle* handle, uint8_t buffer[8192] );
int surface_read_spi_flash( libusb_device_handle* handle, uint16_t page, uint8_t buffer[4096] );


#endif // _SURFACE_H_
