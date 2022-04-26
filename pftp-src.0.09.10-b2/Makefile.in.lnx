MAKE= make
all	:	src/Makefile
		cd src;$(MAKE);cd ..

dynamic	:	src/Makefile
		cd src;$(MAKE) dynamic;cd ..

indent	:	src/Makefile
		cd src;indent -nut -kr *.cc; rm *~; cd ..

static	:	src/Makefile
		cd src;$(MAKE) static;cd ..

clean	:	pftp	
		cd src;$(MAKE) clean;cd ..

pkg     :	
		rm -f Makefile;cd src;$(MAKE) clean; rm -f Makefile; rm -f pftp;rm -f ../pftp;cd ..; cd .. && tar -czf pftpfxp-src.tgz pftpfxp --exclude .pftpbookmrks --exclude .pftpbookmrks.bak

rls     :
	        ./configure; $(MAKE) static;
		mkdir ../pftp
		cp -R pftp README.* HOWTO CHANGELOG .pftpconf old ../pftp
		rm -f Makefile pftp
		cd src;$(MAKE) clean; rm -f Makefile; rm -f pftp;cd ..
		cd ..;tar -czf pftpfxp-src.tgz pftpfxp --exclude .pftpbookmrks --exclude .pftpbookmrks.bak
		cd ..;tar -czf pftp.tgz pftp
		cd ..;rm -rf pftp

drls:
	        ./configure --debug; $(MAKE) static;
		mkdir ../pftp
		cp -R pftp README.* HOWTO CHANGELOG .pftpconf old ../pftp
		rm -f Makefile pftp
		cd src;$(MAKE) clean; rm -f Makefile; rm -f pftp;cd ..
		cd ..;tar -czf pftpfxp-src.tgz pftpfxp --exclude .pftpbookmrks --exclude .pftpbookmrks.bak
		cd ..;tar -czf pftp-debug.tgz pftp
		cd ..;rm -rf pftp

