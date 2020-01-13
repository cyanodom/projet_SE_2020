clientDir = src/client
daemonDir = src/daemon
toolsDir = src/tools

CC = gcc
LDFLAGS = -Wl,-z,relro,-z,now -pie -lrt
CFLAGS = -std=c11 -Wall -Wconversion -Wextra -Wpedantic -O2 --static -g									 \
	-D_POSIX_SOURCE -D_XOPEN_SOURCE -D_FORTIFY_SOURCE -fstack-protector-all -fpie\
	-I$(toolsDir) -DDEBUG_LEVEL=4
VPATH = $(clientDir):$(daemonDir):$(toolsDir)
.PHONY = all clean

OBJECTS_CLIENT = $(clientDir)/client.o
OBJECTS_DAEMON = $(daemonDir)/load_conf.o $(daemonDir)/pool_thread.o					 \
	$(daemonDir)/pipe.o $(daemonDir)/daemon.o

EXEC_CLIENT = client
EXEC_DAEMON = daemon

all: $(EXEC_DAEMON) $(EXEC_CLIENT)

clean:
	$(RM) $(OBJECTS_CLIENT) $(EXEC_CLIENT) $(OBJECTS_DAEMON) $(EXEC_DAEMON)

#CLIENT

$(clientDir)/client.o: client.c

$(EXEC_CLIENT): $(OBJECTS_CLIENT)
	$(CC) $(LDFLAGS) $(OBJECTS_CLIENT) -o $(EXEC_CLIENT)

#SERVER

$(daemonDir)/daemon.o: daemon.c load_conf.h
$(daemonDir)/load_conf.o: load_conf.c load_conf.h
$(daemonDir)/pool_thread.o: pool_thread.c pool_thread.h
	$(COMPILE.c) -pthread $(OUTPUT_OPTION) $<
$(daemonDir)/pipe.o: pipe.c pipe.h

$(EXEC_DAEMON): $(OBJECTS_DAEMON)
	$(CC) $(LDFLAGS) -pthread $(OBJECTS_DAEMON) -o $(EXEC_DAEMON)
