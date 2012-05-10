#include "surface.h"

#include <stdio.h>

/******************************* MAIN *******************************/

int main( int argc, char* argv[] ) {

	uint8_t buffer[VIDEO_BUFFER_SIZE];
	surface_blob blob[256];
	int result,frame = 0;

	usb_dev_handle* s40 = usb_get_device_handle( ID_MICROSOFT, ID_SURFACE );

	surface_init( s40 );

	while (1) {
		result = surface_get_blobs( s40, blob );
		if (result <= 0) continue;
		printf("%d blobs\n",result);
		for (int i = 0; i < result; i++)
			printf("    x: %d y: %d size: %d\n",blob[i].pos_x,blob[i].pos_y,blob[i].area);
		if ((frame++ % 60) == 0) printf("status 0x%08x\n",surface_get_status(s40));
	}

	surface_get_image( s40, buffer );
	FILE* foo = fopen("surface.raw","w+");
	fwrite(buffer,VIDEO_BUFFER_SIZE,1,foo);
	fclose(foo);
}

