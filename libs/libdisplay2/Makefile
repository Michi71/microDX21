#
# Makefile
#

CIRCLEHOME = ../circle-stdlib/libs/circle

# Include-Pfade für Circle-Display-Header
CPPFLAGS += -I$(CIRCLEHOME)/addon/display
CPPFLAGS += -I$(CIRCLEHOME)/include
CPPFLAGS += -I.

# Dein Objektfile
OBJS = sh1106display.o sh1106spidisplay.o ssd1305display.o ssd1305spidisplay.o ssd1306spidisplay.o epd2in13v4display.o gt1151touch.o cst816dtouch.o

# Name der Library
TARGET = libdisplay2.a

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)


