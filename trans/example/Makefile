CC = gcc
MIG = mig
CFLAGS = -Wall -g  -D_GNU_SOURCE
LDFLAGS = -lthreads -lports -ltrivfs -lfshelp -lshouldbeinlibc
INCLUDES = -I.
LCHDRS = 
MIGCOMSFLAGS = -prefix S_
OBJS = $(SRCS:.c=.o)

all: data-server data-user

stubs: data.defs
	    $(MIG) $(MIGCOMSFLAGS) -server dataServer.c -user dataUser.c $^
data-server: data-server.c dataServer.c 
	    $(CC) $^ $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@
data-user: data-user.c dataUser.c
	    $(CC) $^ $(CFLAGS) $(INCLUDES) -o $@
clean:
	    rm -f *.o data-server data-user

start: data-server data-user
	    settrans -ac ./trans data-server
		    ps -x | grep data-server
end:
	    settrans -fg ./trans
