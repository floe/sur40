CFLAGS=-ggdb -Wall

all: test view

clean:
	rm *.o test view

%.o: %.c surface.h
	g++ $(CFLAGS) -c -o $@ $<

test: test.o surface.o
	g++ $(CFLAGS) -o $@ $^ -lusb
	
view: view.o surface.o
	g++ $(CFLAGS) -o $@ $^ -lusb -lGL -lGLU -lglut

