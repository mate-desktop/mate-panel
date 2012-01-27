/*
 * panel-menu-bar.c: panel Applications/Places/Desktop menu bar
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2004 Vincent Untz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Mark McLoughlin <mark@skynet.ie>
 *	Vincent Untz <vincent@vuntz.net>
 */

#include <config.h>

#include "panel-menu-bar.h"

#include <string.h>
#include <glib/gi18n.h>

#include <libpanel-util/panel-error.h>
#include <libpanel-util/panel-launch.h>
#include <libpanel-util/panel-show.h>

#include "panel-util.h"
#include "panel-background.h"
#include "panel-action-button.h"
#include "applet.h"
#include "menu.h"
#include "panel-menu-items.h"
#include "panel-globals.h"
#include "panel-profile.h"
#include "panel-lockdown.h"
#include "panel-stock-icons.h"
#include "panel-typebuiltins.h"
#include "panel-icon-names.h"

G_DEFINE_TYPE (PanelMenuBar, panel_menu_bar, GTK_TYPE_MENU_BAR)

#define PANEL_MENU_BAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PANEL_TYPE_MENU_BAR, PanelMenuBarPrivate))

struct _PanelMenuBarPrivate {
	AppletInfo* info;
	PanelWidget* panel;

	GtkWidget* applications_menu;
	GtkWidget* applications_item;
	GtkWidget* places_item;
	GtkWidget* desktop_item;

	PanelOrientation orientation;
};

enum {
	PROP_0,
	PROP_ORIENTATION,
};

static void panel_menu_bar_update_text_gravity(PanelMenuBar* menubar);

static gboolean panel_menu_bar_reinit_tooltip(GtkWidget* widget, PanelMenuBar* menubar)
{
	g_object_set(menubar->priv->applications_item, "has-tooltip", TRUE, NULL);
	if (!panel_lockdown_get_disable_places_menu())
		g_object_set(menubar->priv->places_item, "has-tooltip", TRUE, NULL);
	g_object_set(menubar->priv->desktop_item, "has-tooltip", TRUE, NULL);

	return FALSE;
}

static gboolean panel_menu_bar_hide_tooltip_and_focus(GtkWidget* widget, PanelMenuBar* menubar)
{
	/* remove focus that would be drawn on the currently focused child of
	 * the toplevel. See bug#308632. */
	gtk_window_set_focus(GTK_WINDOW(menubar->priv->panel->toplevel), NULL);

	g_object_set(widget, "has-tooltip", FALSE, NULL);

	return FALSE;
}

static void panel_menu_bar_setup_tooltip(PanelMenuBar* menubar)
{
	panel_util_set_tooltip_text(menubar->priv->applications_item, _("Browse and run installed applications"));
	if (!panel_lockdown_get_disable_places_menu())
		panel_util_set_tooltip_text(menubar->priv->places_item, _("Access documents, folders and network places"));
	panel_util_set_tooltip_text(menubar->priv->desktop_item, _("Change desktop appearance and behavior, get help, or log out"));

	//FIXME: this doesn't handle the right-click case. Sigh.
	/* Hide tooltip if a menu is activated */
	g_signal_connect(menubar->priv->applications_item, "activate", G_CALLBACK (panel_menu_bar_hide_tooltip_and_focus), menubar);
	if (!panel_lockdown_get_disable_places_menu())
		g_signal_connect(menubar->priv->places_item, "activate", G_CALLBACK (panel_menu_bar_hide_tooltip_and_focus), menubar);
	g_signal_connect(menubar->priv->desktop_item, "activate", G_CALLBACK (panel_menu_bar_hide_tooltip_and_focus), menubar);

	/* Reset tooltip when the menu bar is not used */
	g_signal_connect(GTK_MENU_SHELL (menubar), "deactivate", G_CALLBACK (panel_menu_bar_reinit_tooltip), menubar);
}

static void panel_menu_bar_init(PanelMenuBar* menubar)
{
	GtkWidget* image;

	menubar->priv = PANEL_MENU_BAR_GET_PRIVATE(menubar);

	menubar->priv->info = NULL;


	menubar->priv->applications_menu = create_applications_menu("mate-applications.menu", NULL, TRUE);

	menubar->priv->applications_item = panel_image_menu_item_new();
	gtk_menu_item_set_label(GTK_MENU_ITEM(menubar->priv->applications_item), _("Applications"));
	image = gtk_image_new_from_icon_name(PANEL_ICON_MAIN_MENU, panel_menu_bar_icon_get_size());
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menubar->priv->applications_item), image);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menubar->priv->applications_item), menubar->priv->applications_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menubar->priv->applications_item);
	if (!panel_lockdown_get_disable_places_menu())
		menubar->priv->places_item = panel_place_menu_item_new(FALSE);
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menubar->priv->places_item);

	menubar->priv->desktop_item = panel_desktop_menu_item_new(FALSE, TRUE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menubar->priv->desktop_item);

	panel_menu_bar_setup_tooltip(menubar);

	panel_menu_bar_update_text_gravity(menubar);
	g_signal_connect(menubar, "screen-changed", G_CALLBACK(panel_menu_bar_update_text_gravity), NULL);
}

static void panel_menu_bar_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
	PanelMenuBar* menubar;

	g_return_if_fail(PANEL_IS_MENU_BAR(object));

	menubar = PANEL_MENU_BAR (object);

	switch (prop_id)
	{
		case PROP_ORIENTATION:
			g_value_set_enum(value, menubar->priv->orientation);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void panel_menu_bar_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	PanelMenuBar* menubar;

	g_return_if_fail (PANEL_IS_MENU_BAR (object));

	menubar = PANEL_MENU_BAR(object);

	switch (prop_id)
	{
		case PROP_ORIENTATION:
			panel_menu_bar_set_orientation(menubar, g_value_get_enum(value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void panel_menu_bar_parent_set(GtkWidget* widget, GtkWidget* previous_parent)
{
	PanelMenuBar* menubar = PANEL_MENU_BAR(widget);
	GtkWidget* parent;

	parent = gtk_widget_get_parent(widget);

	g_assert(!parent || PANEL_IS_WIDGET(parent));

	menubar->priv->panel = (PanelWidget*) parent;

	if (menubar->priv->applications_menu)
	{
		mate_panel_applet_menu_set_recurse(GTK_MENU(menubar->priv->applications_menu), "menu_panel", menubar->priv->panel);
	}

	if (menubar->priv->places_item)
	{
		panel_place_menu_item_set_panel(menubar->priv->places_item, menubar->priv->panel);
	}

	if (menubar->priv->desktop_item)
	{
		panel_desktop_menu_item_set_panel(menubar->priv->desktop_item, menubar->priv->panel);
	}
}

static void panel_menu_bar_size_allocate(GtkWidget* widget, GtkAllocation* allocation)
{
	GtkAllocation old_allocation;
	GtkAllocation widget_allocation;
	PanelBackground* background;

	gtk_widget_get_allocation(widget, &widget_allocation);

	old_allocation.x = widget_allocation.x;
	old_allocation.y = widget_allocation.y;
	old_allocation.width = widget_allocation.width;
	old_allocation.height = widget_allocation.height;

	GTK_WIDGET_CLASS(panel_menu_bar_parent_class)->size_allocate (widget, allocation);

	if (old_allocation.x == allocation->x && old_allocation.y == allocation->y && old_allocation.width == allocation->width && old_allocation.height == allocation->height)
	{
		return;
	}

	background = &PANEL_MENU_BAR(widget)->priv->panel->background;

	if (background->type == PANEL_BACK_NONE || (background->type == PANEL_BACK_COLOR && !background->has_alpha))
	{
		return;
	}

	panel_menu_bar_change_background(PANEL_MENU_BAR(widget));
}

static void panel_menu_bar_class_init(PanelMenuBarClass* klass)
{
	GObjectClass* gobject_class = (GObjectClass*) klass;
	GtkWidgetClass* widget_class  = (GtkWidgetClass*) klass;

	gobject_class->get_property = panel_menu_bar_get_property;
	gobject_class->set_property = panel_menu_bar_set_property;

	widget_class->parent_set = panel_menu_bar_parent_set;
	widget_class->size_allocate = panel_menu_bar_size_allocate;

	g_type_class_add_private(klass, sizeof(PanelMenuBarPrivate));

	g_object_class_install_property(gobject_class, PROP_ORIENTATION, g_param_spec_enum("orientation", "Orientation", "The PanelMenuBar orientation", PANEL_TYPE_ORIENTATION, PANEL_ORIENTATION_TOP, G_PARAM_READWRITE));

	gtk_rc_parse_string (
		"style \"panel-menubar-style\"\n"
		"{\n"
		"  GtkMenuBar::shadow-type = none\n"
		"  GtkMenuBar::internal-padding = 0\n"
		"}\n"
		"class \"PanelMenuBar\" style \"panel-menubar-style\"");
}

static gboolean panel_menu_bar_on_expose(GtkWidget* widget, GdkEventExpose* event, gpointer data)
{
	PanelMenuBar* menubar = data;

	if (gtk_widget_has_focus(GTK_WIDGET(menubar)))
	{
		gtk_paint_focus(gtk_widget_get_style(widget), gtk_widget_get_window(widget), gtk_widget_get_state(GTK_WIDGET(menubar)), NULL, widget, "menubar-applet", 0, 0, -1, -1);
	}

	return FALSE;
}

static void panel_menu_bar_load(PanelWidget* panel, gboolean locked, int position, gboolean exactpos, const char* id)
{
	PanelMenuBar* menubar;

	g_return_if_fail (panel != NULL);

	menubar = g_object_new(PANEL_TYPE_MENU_BAR, NULL);

	menubar->priv->info = mate_panel_applet_register(GTK_WIDGET(menubar), NULL, NULL, panel, locked, position, exactpos, PANEL_OBJECT_MENU_BAR, id);

	if (!menubar->priv->info)
	{
		gtk_widget_destroy(GTK_WIDGET(menubar));
		return;
	}

	mate_panel_applet_add_callback(menubar->priv->info, "help", GTK_STOCK_HELP, _("_Help"), NULL);

	/* Menu editors */
	if (panel_is_program_in_path("mozo") || panel_is_program_in_path("matemenu-simple-editor"))
	{
		mate_panel_applet_add_callback (menubar->priv->info, "edit", NULL, _("_Edit Menus"), NULL);
	}

	g_signal_connect_after(menubar, "focus-in-event", G_CALLBACK(gtk_widget_queue_draw), menubar);
	g_signal_connect_after(menubar, "focus-out-event", G_CALLBACK(gtk_widget_queue_draw), menubar);
	g_signal_connect_after(menubar, "expose-event", G_CALLBACK(panel_menu_bar_on_expose), menubar);

	gtk_widget_set_can_focus(GTK_WIDGET(menubar), TRUE);

	panel_widget_set_applet_expandable(panel, GTK_WIDGET(menubar), FALSE, TRUE);
}

void panel_menu_bar_load_from_mateconf(PanelWidget* panel, gboolean locked, int position, gboolean exactpos, const char* id)
{
	panel_menu_bar_load(panel, locked, position, exactpos, id);
}

void panel_menu_bar_create(PanelToplevel* toplevel, int position)
{
	char* id;

	id = panel_profile_prepare_object(PANEL_OBJECT_MENU_BAR, toplevel, position, FALSE);
	panel_profile_add_to_list(PANEL_MATECONF_OBJECTS, id);
	g_free(id);
}

void panel_menu_bar_invoke_menu(PanelMenuBar* menubar, const char* callback_name)
{
	GdkScreen* screen;

	g_return_if_fail(PANEL_IS_MENU_BAR(menubar));
	g_return_if_fail(callback_name != NULL);

	screen = gtk_widget_get_screen(GTK_WIDGET(menubar));

	if (!strcmp(callback_name, "help"))
	{
		panel_show_help(screen, "user-guide", "menubar", NULL);

	}
	else if (!strcmp(callback_name, "edit"))
	{
		GError* error = NULL;

		panel_launch_desktop_file_with_fallback("mozo.desktop", "mozo", screen, &error);

		if (error)
		{
			g_error_free(error);
			panel_launch_desktop_file_with_fallback("matemenu-simple-editor.desktop", "matemenu-simple-editor", screen, NULL);
		}
	}
}

void panel_menu_bar_popup_menu(PanelMenuBar* menubar, guint32 activate_time)
{
	GtkMenu* menu;
	GtkMenuShell* menu_shell;

	g_return_if_fail(PANEL_IS_MENU_BAR(menubar));

	menu = GTK_MENU(menubar->priv->applications_menu);

	/*
	 * We need to call _gtk_menu_shell_activate() here as is done in
	 * window_key_press_handler in gtkmenubar.c which pops up menu
	 * when F10 is pressed.
	 *
	 * As that function is private its code is replicated here.
	 */
	menu_shell = GTK_MENU_SHELL(menubar);

	if (!menu_shell->active)
	{
		gtk_grab_add(GTK_WIDGET(menu_shell));

		menu_shell->have_grab = TRUE;
		menu_shell->active = TRUE;
	}

	gtk_menu_shell_select_item(menu_shell, gtk_menu_get_attach_widget(menu));
}

void panel_menu_bar_change_background(PanelMenuBar* menubar)
{
	panel_background_change_background_on_widget(&menubar->priv->panel->background, GTK_WIDGET(menubar));
}

static void set_item_text_gravity(GtkWidget* item)
{
	GtkWidget* label;
	PangoLayout* layout;
	PangoContext* context;

	label = gtk_bin_get_child(GTK_BIN(item));
	layout = gtk_label_get_layout(GTK_LABEL(label));
	context = pango_layout_get_context(layout);
	pango_context_set_base_gravity(context, PANGO_GRAVITY_AUTO);
}

static void panel_menu_bar_update_text_gravity(PanelMenuBar* menubar)
{
	set_item_text_gravity(menubar->priv->applications_item);
	if (!panel_lockdown_get_disable_places_menu())
		set_item_text_gravity(menubar->priv->places_item);
	set_item_text_gravity(menubar->priv->desktop_item);
}

static void set_item_text_angle_and_alignment(GtkWidget* item, double text_angle, float xalign, float yalign)
{
	GtkWidget *label;

	label = gtk_bin_get_child (GTK_BIN (item));

	gtk_label_set_angle (GTK_LABEL (label), text_angle);

	gtk_misc_set_alignment (GTK_MISC (label), xalign, yalign);
}

static void panel_menu_bar_update_orientation(PanelMenuBar* menubar)
{
	GtkPackDirection pack_direction;
	double text_angle;
	float text_xalign;
	float text_yalign;

	pack_direction = GTK_PACK_DIRECTION_LTR;
	text_angle = 0.0;
	text_xalign = 0.0;
	text_yalign = 0.5;

	switch (menubar->priv->orientation)
	{
		case PANEL_ORIENTATION_TOP:
		case PANEL_ORIENTATION_BOTTOM:
			break;
		case PANEL_ORIENTATION_LEFT:
			pack_direction = GTK_PACK_DIRECTION_BTT;
			text_angle = 90.0;
			text_xalign = 0.5;
			text_yalign = 0.0;
			break;
		case PANEL_ORIENTATION_RIGHT:
			pack_direction = GTK_PACK_DIRECTION_TTB;
			text_angle = 270.0;
			text_xalign = 0.5;
			text_yalign = 0.0;
			break;
		default:
			g_assert_not_reached();
			break;
	}

	gtk_menu_bar_set_pack_direction(GTK_MENU_BAR(menubar), pack_direction);
	gtk_menu_bar_set_child_pack_direction(GTK_MENU_BAR(menubar), pack_direction);

	set_item_text_angle_and_alignment(menubar->priv->applications_item, text_angle, text_xalign, text_yalign);
	if (!panel_lockdown_get_disable_places_menu())
		set_item_text_angle_and_alignment(menubar->priv->places_item, text_angle, text_xalign, text_yalign);
	set_item_text_angle_and_alignment(menubar->priv->desktop_item, text_angle, text_xalign, text_yalign);
}

void panel_menu_bar_set_orientation(PanelMenuBar* menubar, PanelOrientation orientation)
{
	g_return_if_fail(PANEL_IS_MENU_BAR(menubar));

	if (menubar->priv->orientation == orientation)
	{
		return;
	}

	menubar->priv->orientation = orientation;

	panel_menu_bar_update_orientation(menubar);

	g_object_notify(G_OBJECT(menubar), "orientation");
}

PanelOrientation panel_menu_bar_get_orientation(PanelMenuBar* menubar)
{
	g_return_val_if_fail(PANEL_IS_MENU_BAR(menubar), 0);

	return menubar->priv->orientation;
}
