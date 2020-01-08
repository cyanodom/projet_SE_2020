clientDir = src/client
daemonDir = src/daemon
toolsDir = src/tools

CC = gcc
LDFLAGS = -Wl,-z,relro,-z,now -pie
CFLAGS = -std=c11 -Wall -Wconversion -Werror -Wextra -Wpedantic -O2						 \
	-D_POSIX_SOURCE -D_XOPEN_SOURCE -D_FORTIFY_SOURCE -fstack-protector-all -fpie\
	-I$(toolsDir)
VPATH = $(clientDir):$(daemonDir):$(toolsDir)
.PHONY = all clean

OBJECTS_CLIENT = $(clientDir)/client.o
OBJECTS_DAEMON = $(daemonDir)/daemon.o $(daemonDir)/load_conf.o 							 \
#	$(daemonDir)/pool_thread.o

EXEC_CLIENT = client
EXEC_DAEMON = daemon

all: $(EXEC_DAEMON) $(EXEC_CLIENT)

clean:
	$(RM) $(OBJECTS_CLIENT) $(EXEC_CLIENT) $(OBJECTS_DAEMON) $(EXEC_DAEMON)

#CLIENT
$(EXEC_CLIENT): $(OBJECTS_CLIENT)
	$(CC) $(LDFLAGS) $(OBJECTS_CLIENT) -o $(EXEC_CLIENT)

$(clientDir)/client.o: client.c

#SERVER
$(EXEC_DAEMON): $(OBJECTS_DAEMON)
	$(CC) $(LDFLAGS) -pthread $(OBJECTS_DAEMON) -o $(EXEC_DAEMON)

$(daemonDir)/daemon.o: daemon.c load_conf.h
$(daemonDir)/load_conf.o: load_conf.c load_conf.h
$(daemonDir)/pool_thread.o: pool_thread.c pool_thread.h
	$(COMPILE.c) -pthread $(OUTPUT_OPTION) $<
