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


usb_dev_handle* s40;
GLuint texture;
int mode = GL_UNSIGNED_BYTE;


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
	surface_blob blobs[256];

  // clear buffers
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // move to origin
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
	glTranslatef(0,VIDEO_RES_Y,0);
	glScalef(1.0f, -1.0f, 1.0f);

    // always 960 pixels wide, in calibration mode double height
    // apparently 4 subfields (QRST), interlaced row-wise as QQRRSSTT 
	surface_get_image( s40, image, ((mode == GL_UNSIGNED_BYTE) ? VIDEO_BUFFER_SIZE : VIDEO_BUFFER_SIZE*2) );
	//int bc = surface_get_blobs( s40, blobs );

	glEnable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, 1, VIDEO_RES_X, VIDEO_RES_Y, 0, GL_LUMINANCE, mode, image);

	glBegin(GL_TRIANGLE_FAN);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0,0,0);
	glTexCoord2f(1, 0); glVertex3f(VIDEO_RES_X,0,0);
	glTexCoord2f(1, 1); glVertex3f(VIDEO_RES_X,VIDEO_RES_Y,0);
	glTexCoord2f(0, 1); glVertex3f(0,VIDEO_RES_Y,0);
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


void keyboard(unsigned char key, int x, int y) {
  switch (key) {
    case 'q': 
			//usb_reset( s40 ); sleep(1);
			usb_close( s40 );
			exit(0); 
			break;
		case 'c':
			surface_calib_start( s40 );
			mode = GL_UNSIGNED_SHORT;
			break;
		case 'e':
			surface_calib_end( s40 );
			mode = GL_UNSIGNED_BYTE;
			break;
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

 	glutInitWindowSize(VIDEO_RES_X,VIDEO_RES_Y);
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
