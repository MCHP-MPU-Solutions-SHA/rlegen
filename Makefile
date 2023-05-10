TARGET    = rlegen
OBJECTS   = $(patsubst %.c,%.o,$(wildcard *.c))

ARCH ?= x86
ifeq (${ARCH},x86)
	CROSS_COMPILE =
endif
ifeq (${ARCH},arm)
	CROSS_COMPILE ?= arm-linux-gnueabi-
endif

ifndef V
QUIET_CC    = @echo '  CC       '$@;
QUIET_LINK  = @echo '  LINK     '$@;
QUIET_STRIP = @echo '  STRIP    '$@;
endif

CC      = $(QUIET_CC)$(CROSS_COMPILE)gcc
LD      = $(QUIET_LINK)$(CROSS_COMPILE)gcc
STRIP   = $(QUIET_STRIP)$(CROSS_COMPILE)strip
CFLAGS  = -Wall -Werror -O3
LDFLAGS = -ldl

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) $(CFLAGS) ${LDFLAGS} $(OBJECTS) -o $@
	$(STRIP) $@

clean:
	-rm -f $(TARGET) *.o $(LINUX_DIR)/*.o *.map
