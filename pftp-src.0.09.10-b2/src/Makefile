SSLDIR          = /usr
CPPFLAGS        = -Wall -D_REENTRANT -I../include -g -DDEBUG -I$(SSLDIR)/include -DTLS
LINKFLAGS       = -lpanel -lncurses -lpthread -g -L$(SSLDIR)/lib -lssl -lcrypto -DTLS
DO              = 
OBJS            = tlsutil.o
CPP= g++
.cc.o	:	
	        $(CPP) -c $(CPPFLAGS) $<

all	:	main.o switchserver.o tcp.o displayupdate.o server.o bookmark.o opensitedialog.o displayhandler.o keyhandler.o $(OBJS)
		$(CPP) -o pftp main.o switchserver.o tcp.o displayupdate.o server.o bookmark.o opensitedialog.o displayhandler.o keyhandler.o $(OBJS) $(LINKFLAGS)
		$(DO)
		cp -f pftp ..

dynamic	:	main.o switchserver.o tcp.o displayupdate.o server.o bookmark.o opensitedialog.o displayhandler.o keyhandler.o $(OBJS)
		$(CPP) -o pftp main.o switchserver.o tcp.o displayupdate.o server.o bookmark.o opensitedialog.o displayhandler.o keyhandler.o $(OBJS) $(LINKFLAGS)
		$(DO)
		cp -f pftp ..

static	:	main.o switchserver.o tcp.o displayupdate.o server.o bookmark.o opensitedialog.o displayhandler.o keyhandler.o $(OBJS)
		$(CPP) -o pftp main.o switchserver.o tcp.o displayupdate.o server.o bookmark.o opensitedialog.o displayhandler.o keyhandler.o $(OBJS) $(LINKFLAGS) -ldl -static
		$(DO)
		cp -f pftp ..

clean	:	
		rm -f *.o; rm -f ../pftp;rm -f pftp


