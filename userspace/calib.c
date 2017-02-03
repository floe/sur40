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

#include <stdlib.h> // random() etc.
#include <string.h> // strlen() etc.
#include <stdio.h>  // printf() etc.
#include <time.h>   // time()
#include <math.h>   // fabsf()

#include <unistd.h> // fcntl()
#include <fcntl.h>

#include <sys/socket.h> // inet_addr()
#include <netinet/in.h>
#include <arpa/inet.h>

#include <GL/glut.h>

void deinterlace( uint8_t* input, uint8_t* output ) {

	int in_off = 0, out_off = 0;

	for (int y = 0; y < VIDEO_RES_Y*2; y+=8) {

		in_off = y * VIDEO_RES_X;
		out_off = y/2 * VIDEO_RES_X;

		memcpy( output+out_off, input+in_off, VIDEO_RES_X*4 ); in_off += VIDEO_RES_X * 4; out_off += VIDEO_RES_X * VIDEO_RES_Y;
		memcpy( output+out_off, input+in_off, VIDEO_RES_X*4 ); in_off += VIDEO_RES_X * 4; out_off += VIDEO_RES_X * VIDEO_RES_Y;
		//memcpy( output+out_off, input+in_off, VIDEO_RES_X*2 ); in_off += VIDEO_RES_X * 2; out_off += VIDEO_RES_X * VIDEO_RES_Y / 2;
		//memcpy( output+out_off, input+in_off, VIDEO_RES_X*2 ); in_off += VIDEO_RES_X * 2; out_off += VIDEO_RES_X * VIDEO_RES_Y / 2;
	}
}


usb_dev_handle* s40;
GLuint texture;
int mode = 1;

int voltage = 0x0c;
int bias    = 0x07;
bool set = false;


void output( int x, int y, char *string ) {
  glRasterPos2f(x, y);
  int len, i;
  len = (int)strlen(string);
  for (i = 0; i < len; i++) {
    glutBitmapCharacter(GLUT_BITMAP_9_BY_15, string[i]);
  }
}


void cross( int x, int y ) {

	glBegin(GL_LINES);
	glVertex3f(x+10,y,0);
	glVertex3f(x-10,y,0);
	glVertex3f(x,y+10,0);
	glVertex3f(x,y-10,0);
	glEnd();
}

void box( int x1, int y1, int x2, int y2 ) {

	glBegin(GL_LINES);
	glVertex3f(x1,y1,0); glVertex3f(x1,y2,0);
	glVertex3f(x2,y1,0); glVertex3f(x2,y2,0);
	glVertex3f(x1,y1,0); glVertex3f(x2,y1,0);
	glVertex3f(x1,y2,0); glVertex3f(x2,y2,0);
	glEnd();
}



void idle() { glutPostRedisplay(); }

void display() {

	uint8_t image[VIDEO_BUFFER_SIZE*2];
	uint8_t image2[VIDEO_BUFFER_SIZE*2];
	surface_blob blobs[256];

  // clear buffers
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // move to origin
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
	glTranslatef(0,VIDEO_RES_Y*mode,0);
	glScalef(1.0f, -1.0f, 1.0f);

    // always 960 pixels wide, in calibration mode double height
    // apparently 4 subfields (QRST), interlaced row-wise as QQRRSSTT 
	surface_get_image( s40, image, VIDEO_BUFFER_SIZE*mode );
	//int bc = surface_get_blobs( s40, blobs );

	if (mode == 2) deinterlace(image,image2);

	glEnable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, 1, VIDEO_RES_X, VIDEO_RES_Y*mode, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (mode == 2 ? image2 : image));

	glBegin(GL_TRIANGLE_FAN);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0,0,0);
	glTexCoord2f(1, 0); glVertex3f(VIDEO_RES_X,0,0);
	glTexCoord2f(1, 1); glVertex3f(VIDEO_RES_X,VIDEO_RES_Y*mode,0);
	glTexCoord2f(0, 1); glVertex3f(0,VIDEO_RES_Y*mode,0);
	glEnd();

	glDisable(GL_TEXTURE_2D);

  // redraw
  glutSwapBuffers();
}


void resize(int w, int h) {

  // set a whole-window viewport
  glViewport(0,0,w,h);

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0.0, w, 0.0, h, -1000, 1000 );
	
  // invalidate display
  glutPostRedisplay();
}

surface_calib calibbuf;

void read_calib() {
	int res = surface_read_calib(s40,&calibbuf);
	printf("read_calib result: %x\n",res);
	FILE* foo = fopen("calib.raw","w");
	fwrite(&calibbuf, 0x10e000, 1, foo);
	fclose(foo);
}

void write_calib() {
	calibbuf.row[0].version[4] = 'f';
	calibbuf.row[0].version[5] = 'o';
	calibbuf.row[0].version[6] = 'o';
	calibbuf.row[539].padding4[66] = 0xFE;
	int res = surface_write_calib(s40,&calibbuf);
	printf("write_calib result: %x\n",res);
}

void read_usb_fw() {
	uint8_t usb_fw[8192];
	int res = surface_read_usb_flash(s40,usb_fw);
	printf("read_fw result: %x\n",res);
	FILE* foo = fopen("usb_fw.raw","w");
	fwrite(usb_fw, 8192, 1, foo);
	fclose(foo);
}

void read_fpga_fw() {
	uint8_t page[4096];
	memset(page,0,sizeof(page));
	int res = surface_read_spi_flash(s40,0x190,page);
	printf("read_fw result: %d\n",res);
	FILE* foo = fopen("fpga.raw","w");
	fwrite(page, sizeof(page), 1, foo);
	fclose(foo);

}

void keyboard(unsigned char key, int x, int y) {
  switch (key) {
		case 'q':
			//usb_reset( s40 ); sleep(1);
			usb_close( s40 );
			exit(0); 
			break;
		case 'c':
			surface_calib_start( s40 );
			mode = 2;
			break;
		case 'e':
			surface_calib_end( s40 );
			mode = 1;
			break;

		case 'r':
			read_calib();
			break;

		case 'w':
			write_calib();
			break;

		case 'R':
			read_usb_fw();
			break;

		case 'f':
			read_fpga_fw();
			break;

		case '<': set = true; voltage++; if (voltage > 15) voltage = 15; break;
		case '>': set = true; voltage--; if (voltage <  1) voltage =  1; break;

		case '+': set = true; bias++; if (bias > 9) bias = 9; break;
		case '-': set = true; bias--; if (bias < 1) bias = 1; break;
  }
  if (set) {
	surface_set_vsvideo( s40, (voltage << 4) | bias );
	printf("setting vsvideo: %02x ",(voltage << 4) | bias );
	set = false;
	surface_peek(s40);
  }
}


// initialize the GL library
void initGL() {

	// enable and set colors
	glEnable(GL_COLOR_MATERIAL);
	glClearColor(0.0,0.0,0.0,1.0);

	// misc stuff
	glDisable(GL_LIGHTING);
	glDisable(GL_CULL_FACE);
	glShadeModel( GL_FLAT );

	glDepthFunc(GL_LESS);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &texture );
	glBindTexture(GL_TEXTURE_2D, texture );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	resize(VIDEO_RES_X,VIDEO_RES_Y);
}


/******************************* MAIN *******************************/

int main(int argc, char* argv[]) {

	s40 = usb_get_device_handle( ID_MICROSOFT, ID_SURFACE );
	surface_init( s40 );

	glutInitWindowSize(VIDEO_RES_X,VIDEO_RES_Y*2);
	glutInit(&argc,argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH );
	glutCreateWindow("surfaceview");

	initGL();

	// make functions known to GLUT
	glutKeyboardFunc(keyboard);
	glutDisplayFunc(display);
	glutReshapeFunc(resize);
	glutIdleFunc(idle);

	// start the action
	glutMainLoop();

	return 0;
}
