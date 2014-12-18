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

#include <stdint.h>
#include <usb.h>


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
	uint8_t unknown;    // always 0x01 or 0x02 (no idea what this is?)

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

	uint8_t padding[32];
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


// helper to find a device by vendor and product
usb_dev_handle* usb_get_device_handle( int vendor, int product );

// get device status word
int surface_get_status( usb_dev_handle* handle );

// get sensor status
void surface_get_sensors( usb_dev_handle* handle );

// initialization sequence
void surface_init( usb_dev_handle* handle );

// retrieve raw data from surface
int surface_get_image( usb_dev_handle* handle, uint8_t* image );
int surface_get_blobs( usb_dev_handle* handle, surface_blob* blob );

#endif // _SURFACE_H_

