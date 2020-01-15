clientDir = src/client
daemonDir = src/daemon
toolsDir = src/tools

CC = gcc
LDFLAGS = -Wl,-z,relro,-z,now -pie -lrt -pthread
CFLAGS = -std=c11 -Wall -Wconversion -Wextra -Wpedantic -O2 --static -g -Werror\
	-D_POSIX_SOURCE -D_XOPEN_SOURCE=500 -D_FORTIFY_SOURCE -fstack-protector-all  \
	-fpie -I$(toolsDir) -I$(clientDir) -I$(daemonDir) -DDEBUG_LEVEL=4
VPATH = $(clientDir):$(daemonDir):$(toolsDir)
.PHONY = all clean

OBJECTS_CLIENT = $(clientDir)/client.o $(daemonDir)/load_conf.o                \
		$(daemonDir)/shm.h
OBJECTS_DAEMON = $(daemonDir)/load_conf.o $(daemonDir)/pool_thread.o					 \
		$(daemonDir)/pipe.o $(daemonDir)/daemon.o $(daemonDir)/shm.h

EXEC_CLIENT = client
EXEC_DAEMON = daemon

all: $(EXEC_DAEMON) $(EXEC_CLIENT)

clean:
	$(RM) $(OBJECTS_CLIENT) $(EXEC_CLIENT) $(OBJECTS_DAEMON) $(EXEC_DAEMON)

#CLIENT

$(clientDir)/client.o: client.c load_conf.h
$(daemonDir)/load_conf.o : load_conf.c load_conf.h
$(EXEC_CLIENT): $(OBJECTS_CLIENT)
	$(CC) $(LDFLAGS) $(OBJECTS_CLIENT) -o $(EXEC_CLIENT)

#SERVER

$(daemonDir)/daemon.o: daemon.c load_conf.h
$(daemonDir)/load_conf.o: load_conf.c load_conf.h
$(daemonDir)/pool_thread.o: pool_thread.c pool_thread.h
	$(COMPILE.c) -pthread $(OUTPUT_OPTION) $<
$(daemonDir)/pipe.o: pipe.c pipe.h

$(EXEC_DAEMON): $(OBJECTS_DAEMON)
	$(CC) $(LDFLAGS) $(OBJECTS_DAEMON) -o $(EXEC_DAEMON)
