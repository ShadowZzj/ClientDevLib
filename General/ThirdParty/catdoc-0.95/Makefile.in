
# Your C compilier and flags
SHELL = /bin/sh
PACKAGE_VERSION = @PACKAGE_VERSION@
PACKAGE_TARNAME = @PACKAGE_TARNAME@
tardir=${PACKAGE_TARNAME}-${PACKAGE_VERSION}

all:
	for i in src doc charsets; do\
	   (cd $$i; $(MAKE) all);\
	done

install:
	for i in src doc charsets; do\
	   (cd $$i; $(MAKE) install);\
	done
clean:	
	for i in src doc charsets; do\
	   (cd $$i; $(MAKE) clean);\
	done
distclean:	
	for i in src doc charsets; do\
	   (cd $$i; $(MAKE) distclean);\
	done
	rm -f Makefile config.*
dist:
	$(MAKE) clean
	mkdir $(tardir)
	cp -rp src doc compat charsets $(tardir)
	cp -rp install-sh missing mkinstalldirs CODING.STD COPYING $(tardir)
	cp -rp INSTALL INSTALL.dos Makefile Makefile.in NEWS README TODO $(tardir)
	cp -rp acconfig.h configure configure.in $(tardir)
	$(MAKE) -C $(tardir) distclean
	$(MAKE) -C doc dosdoc
	cp -rp doc/*.txt doc/*.ps $(tardir)/doc
	tar cf $(tardir).tar $(tardir)
	gzip -c $(tardir).tar > $(tardir).tar.gz
	rm -rf $(tardir)

	
