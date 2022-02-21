
# Your C compilier and flags
SHELL = /bin/sh
installroot=
srcdir = .
top_srcdir = ..
INSTALL = /usr/bin/install -c

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
libdir = ${exec_prefix}/lib
mandir = ${datarootdir}/man
manext = .1 
datadir = ${datarootdir}
datarootdir = ${prefix}/share
%.txt: %.1
	nroff -mandoc $< |col -bx >$@
%.ps: %.1
	groff -Tps -mandoc $< >$@	

all:
	touch build

dosdoc: catdoc.txt catdoc.ps xls2csv.txt xls2csv.ps catppt.txt catppt.ps
clean:
	$(RM) catppt.txt catppt.ps  \
            catdoc.txt catdoc.ps \
            xls2csv.txt xls2csv.ps \
            wordview.txt wordview.ps build
install: install-catdoc
install-catdoc: catdoc.1 xls2csv.1 catppt.1
	../mkinstalldirs $(installroot)$(mandir)
	$(INSTALL) -m 644 catdoc.1 $(installroot)$(mandir)/man1/catdoc.1
	$(INSTALL) -m 644 xls2csv.1 $(installroot)$(mandir)/man1/xls2csv.1
	$(INSTALL) -m 644 catppt.1 $(installroot)$(mandir)/man1/catppt.1
install-wordview:	
	../mkinstalldirs $(installroot)$(mandir)
	$(INSTALL) -m 644 wordview.1 $(installroot)$(mandir)/man1/wordview.1
distclean: clean
	$(RM)  Makefile catdoc.1 xls2csv.1 catppt.1 wordview.1

