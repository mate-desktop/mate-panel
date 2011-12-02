size = scalable

iconsdir = $(datadir)/icons/hicolor/$(size)/apps

icons_DATA =					\
	mate-panel-clock.svg			\
	mate-panel-drawer.svg			\
	mate-panel-fish.svg			\
	mate-panel-force-quit.svg		\
	mate-panel-launcher.svg		\
	mate-panel-notification-area.svg	\
	mate-panel-separator.svg		\
	mate-panel.svg				\
	mate-panel-window-list.svg		\
	mate-panel-window-menu.svg		\
	mate-panel-workspace-switcher.svg

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated. After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

EXTRA_DIST =		\
	$(icons_DATA)

-include $(top_srcdir)/git.mk
