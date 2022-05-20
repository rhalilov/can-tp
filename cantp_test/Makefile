CC = gcc
MV = mv
CFLAGS = -I -Wall -g -O2
INCLUDES = 	-I. \
			-I.. \
			
OUTPUT_PATH = ./build

SRCS =	cantp_test.c \
		cbtimer_lin.c \
		../can-tp.c \

LIBS = -lrt

OBJS = $(SRCS:.c=.o)

MAIN =	cantp_test

.PHONY: depend clean

all:    $(MAIN)
	@echo $(MAIN) has been compiled

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OUTPUT_PATH)/*.o $(LFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@
	$(MV) $@ $(OUTPUT_PATH)/

clean:
	$(RM) $(OUTPUT_PATH)/*.o *~ $(MAIN)

depend: $(SRCS)
	makedepend $(INCLUDES) $^
