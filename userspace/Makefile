#CFLAGS=-g -Wall
CFLAGS=-O3 -w

%.o: %.cpp surface.h
	@echo [CXX] $@
	@ $(CXX) $(CXXFLAGS) -o $@ -c $<

all: test view calib

clean:
	rm -f *.o test view calib

test: test.o surface.o
	@echo [LD] $@
	@ $(CXX) $(CFLAGS) -o $@ $^ -lusb-1.0

view: view.o surface.o
	@echo [LD] $@
	@ $(CXX) $(CFLAGS) -o $@ $^ -lusb-1.0 -lGL -lGLU -lglut

calib: calib.o surface.o
	@echo [LD] $@
	@ $(CXX) $(CFLAGS) -o $@ $^ -lusb-1.0 -lGL -lGLU -lglut

