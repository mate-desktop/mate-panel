SUBDIRS =			\
	mate-panel		\
	libmate-panel-applet		\
	applets			\
	doc			\
	help			\
	icons			\
	data			\
	man			\
	po

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--disable-introspection

EXTRA_DIST = \
	autogen.sh		\
	COPYING-DOCS		\
	HACKING

MPCLEANFILES = \
	INSTALL \
	aclocal.m4 \
	compile \
	config.* \
	configure \
	depcomp \
	install-sh \
	ltmain.sh \
	missing \
	omf.make \
	xmldocs.make \
	mkinstalldirs \
	gtk-doc.make

mp-clean: maintainer-clean
	-test -z "$(MPCLEANFILES)" || rm -f $(MPCLEANFILES)
	-rm -rf m4;
	find . -name "Makefile.in" -exec rm \{\} \;

# Build ChangeLog from GIT  history
ChangeLog:
	$(AM_V_GEN) if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog

-include $(top_srcdir)/git.mk
