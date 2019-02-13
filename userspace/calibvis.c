#include <stdio.h>
#include <stdint.h>

#define COUNT 1024*540

uint16_t buffer[COUNT];

uint8_t buf1[COUNT];
uint8_t buf2[COUNT];

uint8_t* header = "P5 1024 540 255 ";

int main(int argc, char* argv[]) {

	FILE* foo = fopen(argv[1],"r");
	fread(buffer, 0x10e000, 1, foo);
  fclose(foo);

	for (int i = 0; i < COUNT; i++) {
		buf1[i] = buffer[i] >> 8;
		buf2[i] = buffer[i] & 0xFF;
	}

	FILE* out1 = fopen("out1.pgm","w");
	fwrite(header,strlen(header),1,out1);
	fwrite(buf1,sizeof(buf1),1,out1);
	fclose(out1);

	FILE* out2 = fopen("out2.pgm","w");
	fwrite(header,strlen(header),1,out2);
	fwrite(buf2,sizeof(buf2),1,out2);
	fclose(out2);

}
