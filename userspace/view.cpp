/*
 * microsoft surface 2.0 open source driver 0.9
 *
 * Copyright (c) 2012 by Florian Echtler <floe@butterbrot.org>
 * Licensed under GNU General Public License (GPL) v2 or later
 */

#include "surface.h"
#include <GL/glut.h>

libusb_device_handle* s40;
GLuint texture;

int curframe = 0;
int lasttime = 0;
int lastframe = 0;
double fps = 0.0;

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

uint8_t image[VIDEO_BUFFER_SIZE];
surface_blob blobs[256];
char buffer[128];

void display() {

	int curtime = glutGet( GLUT_ELAPSED_TIME );
	curframe++;

	if ((curtime - lasttime) >= 1000) {
		fps = (1000.0*(curframe-lastframe))/((double)(curtime-lasttime));
		lasttime  = curtime;
		lastframe = curframe;
		snprintf(buffer,sizeof(buffer),"FPS: %f",fps);
		printf("%s\n",buffer);
	}

	// clear buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// move to origin
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0,VIDEO_RES_Y,0);
	glScalef(1.0f, -1.0f, 1.0f);

	surface_get_image( s40, image );
	int bc = surface_get_blobs( s40, blobs );

	glEnable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, 1, VIDEO_RES_X, VIDEO_RES_Y, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, image);

	glBegin(GL_TRIANGLE_FAN);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0,0,0);
	glTexCoord2f(1, 0); glVertex3f(VIDEO_RES_X,0,0);
	glTexCoord2f(1, 1); glVertex3f(VIDEO_RES_X,VIDEO_RES_Y,0);
	glTexCoord2f(0, 1); glVertex3f(0,VIDEO_RES_Y,0);
	glEnd();

	glDisable(GL_TEXTURE_2D);

	// green: tip
	glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
	for (int i = 0; i < bc; i++) cross( blobs[i].pos_x/2, blobs[i].pos_y/2 );

	// red: centroid(?)
	glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
	for (int i = 0; i < bc; i++) cross( blobs[i].ctr_x/2, blobs[i].ctr_y/2 );

	// yellow: axis(?)
	/*glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
	for (int i = 0; i < bc; i++) cross( (blobs[i].pos_x+blobs[i].axis_x)/2, (blobs[i].pos_y+blobs[i].axis_y)/2 );*/

	// blue: bbox
	glColor4f(0.0f, 0.0f, 1.0f, 1.0f);
	for (int i = 0; i < bc; i++) box( blobs[i].bb_pos_x/2, blobs[i].bb_pos_y/2, (blobs[i].bb_pos_x+blobs[i].bb_size_x)/2, (blobs[i].bb_pos_y+blobs[i].bb_size_y)/2 );

	// white: id
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	char id_buf[64];
	for (int i = 0; i < bc; i++) {
		snprintf(id_buf,64,"%d",blobs[i].blob_id);
		output(blobs[i].ctr_x/2, blobs[i].ctr_y/2,id_buf);
	}

	output(20,20,buffer);
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
			sur40_close_device( s40 );
			exit(0);
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

	s40 = sur40_get_device_handle();
	if (s40==NULL) return 0;
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

	sur40_close_device(s40);
	return 0;
}


