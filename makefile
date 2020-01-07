clientDir = src/client/
daemonDir = src/daemon/

CC = gcc
LDFLAGS =
CFLAGS = -std=c11 -Wall -Wconversion -Werror -Wextra -Wpedantic -O2						 \
	-D_POSIX_SOURCE -D_XOPEN_SOURCE=500
VPATH = $(clientDir):$(daemonDir)
$OBJECTS_CLIENT = $(clientDir)/client.o
$OBJECTS_DAEMON = $(daemonDir)/daemon.o
.PHONY = all clean

EXEC_CLIENT = client
EXEC_DAEMON = daemon

all: $(EXEC_DAEMON) $(EXEC_CLIENT)

clean:
	$(RM) $(OBJECTS_CLIENT) $(OBJECTS_DAEMON)

#CLIENT
$(EXEC_CLIENT): $(OBJECTS_CLIENT)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(EXEC_CLIENT)

$(clientDir)/client.o: client.c

#SERVER
$(EXEC_DAEMON): $(OBJECTS_DAEMON)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(EXEC_DAEMON)

$(daemonDir)/daemon.o: daemon.c load_conf.c
