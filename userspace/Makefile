CFLAGS=-ggdb -Wall

all: test view calib

clean:
	rm -f *.o test view

%.o: %.cpp surface.h
	g++ $(CFLAGS) -c -o $@ $<

test: test.o surface.o
	g++ $(CFLAGS) -o $@ $^ -lusb

view: view.o surface.o
	g++ $(CFLAGS) -o $@ $^ -lusb -lGL -lGLU -lglut

calib: calib.o surface.o
	g++ $(CFLAGS) -o $@ $^ -lusb -lGL -lGLU -lglut

