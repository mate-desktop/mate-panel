panelmodule_LTLIBRARIES = libmate-panel-applets-matecomponent.la
panelmoduledir = $(modulesdir)

libmate_panel_applets_matecomponent_la_SOURCES =	\
	$(CORBA_SRCLIST)		\
	mate-panel-applets-matecomponent-module.c	\
	mate-panel-applets-manager-matecomponent.c	\
	mate-panel-applets-manager-matecomponent.h	\
	mate-panel-applet-frame-matecomponent.c	\
	mate-panel-applet-frame-matecomponent.h

libmate_panel_applets_matecomponent_la_CPPFLAGS =	\
	$(MATECOMPONENT_CFLAGS)		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/mate-panel	\
	-I$(top_builddir)		\
	-DDATADIR=\""$(datadir)"\"

libmate_panel_applets_matecomponent_la_LDFLAGS =				\
	-export_dynamic -avoid-version -module -no-undefined	\
	-export-symbols-regex '^g_io_module_(load|unload|query)'

libmate_panel_applets_matecomponent_la_LIBADD =	\
	$(MATECOMPONENT_LIBS)

CORBA_SRCLIST =			\
	MATE_Panel-stubs.c	\
	MATE_Panel-skels.c	\
	MATE_Panel-common.c	\
	MATE_Panel.h

$(CORBA_SRCLIST): $(top_srcdir)/matecomponent/idl/MATE_Panel.idl $(MATECORBA_IDL)
	$(AM_V_GEN)$(MATECORBA_IDL) -I $(MATECOMPONENT_IDLDIR) -I $(MATECOMPONENT_ACT_IDLDIR) $(top_srcdir)/matecomponent/idl/MATE_Panel.idl

xmluidir   = $(datadir)/mate-2.0/ui
xmlui_DATA = MATE_Panel_Popup.xml

install-data-hook:
	if test -z "$(DESTDIR)" -a "$(GIO_QUERYMODULES)" != "no" ; then	\
		$(GIO_QUERYMODULES) $(modulesdir) ;			\
	fi
uninstall-local:
	if test "$(GIO_QUERYMODULES)" != "no" ; then	\
		$(GIO_QUERYMODULES) $(modulesdir) ;	\
	fi

EXTRA_DIST = $(xmlui_DATA)

CLEANFILES = $(CORBA_SRCLIST)

-include $(top_srcdir)/git.mk
