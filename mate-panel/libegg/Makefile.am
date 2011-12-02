platform_defines = -DEGG_SM_CLIENT_BACKEND_XSMP
platform_sources = eggsmclient-xsmp.c

noinst_LTLIBRARIES = libegg.la

AM_CPPFLAGS =				\
	$(EGG_SMCLIENT_CFLAGS)		\
	-DG_LOG_DOMAIN=\""EggSMClient"\"\
	$(platform_defines)		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

libegg_la_SOURCES =			\
	eggdesktopfile.c		\
	eggdesktopfile.h		\
	eggsmclient.c			\
	eggsmclient.h			\
	eggsmclient-private.h		\
	$(platform_sources)

libegg_la_LIBADD =			\
	$(EGG_SMCLIENT_LIBS)

-include $(top_srcdir)/git.mk
