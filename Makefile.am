SUBDIRS =			\
	mate-panel		\
	libmate-panel-applet		\
	applets			\
	doc			\
	help			\
	icons			\
	man			\
	po

if HAVE_MATECOMPONENT
SUBDIRS += matecomponent
endif

ACLOCAL_AMFLAGS = -I m4

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --enable-introspection --disable-scrollkeeper

EXTRA_DIST = \
	COPYING-DOCS		\
	HACKING			\
	MAINTAINERS		\
	ChangeLog.pre-git	\
	mate-doc-utils.make

DISTCLEANFILES =		\
	mate-doc-utils.make

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	$(srcdir)/configure \
	$(srcdir)/gtk-doc.make \
	$(srcdir)/m4/mate-doc-utils.m4 \
	$(srcdir)/m4/gtk-doc.m4 \
	$(srcdir)/m4/intltool.m4 \
	$(srcdir)/m4/libtool.m4 \
	$(srcdir)/m4/ltoptions.m4 \
	$(srcdir)/m4/ltsugar.m4 \
	$(srcdir)/m4/ltversion.m4 \
	$(srcdir)/m4/lt~obsolete.m4

-include $(top_srcdir)/git.mk
