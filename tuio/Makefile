PLATFORM=$(shell uname)
ENDIANESS=OSC_HOST_LITTLE_ENDIAN

SUR40_TUIO = Sur40_TUIO

INCLUDES = -I./TUIO -I./oscpack -I../userspace
#CFLAGS  = -g -Wall -O3 -fPIC
CFLAGS  = -w -O3 -fPIC
CXXFLAGS = $(CFLAGS) $(INCLUDES) -D$(ENDIANESS)

SUR40_SOURCES = Sur40_TUIO.cpp ../userspace/surface.cpp
COMMON_TUIO_SOURCES = ./TUIO/TuioTime.cpp ./TUIO/TuioPoint.cpp ./TUIO/TuioContainer.cpp ./TUIO/TuioObject.cpp ./TUIO/TuioCursor.cpp ./TUIO/TuioBlob.cpp ./TUIO/TuioDispatcher.cpp ./TUIO/TuioManager.cpp  ./TUIO/OneEuroFilter.cpp
SERVER_TUIO_SOURCES = ./TUIO/TuioServer.cpp ./TUIO/UdpSender.cpp ./TUIO/TcpSender.cpp ./TUIO/WebSockSender.cpp ./TUIO/FlashSender.cpp
OSC_SOURCES = ./oscpack/osc/OscTypes.cpp ./oscpack/osc/OscOutboundPacketStream.cpp  ./oscpack/ip/posix/NetworkingUtils.cpp ./oscpack/ip/posix/UdpSocket.cpp

COMMON_TUIO_OBJECTS = $(COMMON_TUIO_SOURCES:.cpp=.o)
SERVER_TUIO_OBJECTS = $(SERVER_TUIO_SOURCES:.cpp=.o)
OSC_OBJECTS = $(OSC_SOURCES:.cpp=.o)
SUR40_OBJECTS = $(SUR40_SOURCES:.cpp=.o)

%.o: %.cpp
	@echo [CXX] $@
	@ $(CXX) $(CXXFLAGS) -o $@ -c $<

%.o: %.c
	@echo [CC] $@
	@ $(CC) $(CFLAGS) -o $@ -c $<

all: $(SUR40_TUIO)

$(SUR40_TUIO): $(COMMON_TUIO_OBJECTS) $(SERVER_TUIO_OBJECTS) $(OSC_OBJECTS) $(SUR40_OBJECTS) $(COBJECTS)
	@echo [LD] $(SUR40_TUIO)
	@ $(CXX) -o $@ $+ -lpthread -lusb $(LD_FLAGS)

clean:
	@echo [CLEAN] $(SUR40_TUIO)
	@ rm -f $(SUR40_TUIO)
	@ rm -f $(COMMON_TUIO_OBJECTS) $(SERVER_TUIO_OBJECTS) $(OSC_OBJECTS) $(SUR40_OBJECTS) $(COBJECTS)
