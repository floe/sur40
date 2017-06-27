CFLAGS=-ggdb -Wall

%.o: %.cpp surface.h
	@echo [CXX] $@
	@ $(CXX) $(CXXFLAGS) -o $@ -c $<

all: test view calib

clean:
	rm -f *.o test view

test: test.o surface.o
	@echo [LD] $@
	@ $(CXX) $(CFLAGS) -o $@ $^ -lusb

view: view.o surface.o
	@echo [LD] $@
	@ $(CXX) $(CFLAGS) -o $@ $^ -lusb -lGL -lGLU -lglut

calib: calib.o surface.o
	@echo [LD] $@
	@ $(CXX) $(CFLAGS) -o $@ $^ -lusb -lGL -lGLU -lglut

