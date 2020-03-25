size = 64x64

iconsdir = $(datadir)/icons/hicolor/$(size)/apps

icons_DATA =					\
	mate-panel-clock.png			\
	mate-panel-drawer.png			\
	mate-panel-fish.png			\
	mate-panel-force-quit.png		\
	mate-panel-launcher.png			\
	mate-panel-notification-area.png	\
	mate-panel.png				\
	mate-panel-separator.png		\
	mate-panel-window-list.png		\
	mate-panel-window-menu.png		\
	mate-panel-workspace-switcher.png

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
