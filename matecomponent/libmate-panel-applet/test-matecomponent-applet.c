/*
 * test-matecomponent-applet.c:
 *
 * Authors:
 *    Mark McLoughlin <mark@skynet.ie>
 *
 * Copyright 2001 Sun Microsystems, Inc.
 */

#include <config.h>
#include <string.h>

#include <libmatecomponentui.h>

#include "mate-panel-applet.h"

static void
test_applet_on_do (MateComponentUIComponent *uic,
		   gpointer           user_data,
		   const gchar       *verbname)
{
        g_message ("%s called\n", verbname);
}

static const MateComponentUIVerb test_applet_menu_verbs [] = {
        MATECOMPONENT_UI_VERB ("TestAppletDo1", test_applet_on_do),
        MATECOMPONENT_UI_VERB ("TestAppletDo2", test_applet_on_do),
        MATECOMPONENT_UI_VERB ("TestAppletDo3", test_applet_on_do),

        MATECOMPONENT_UI_VERB_END
};

static const char test_applet_menu_xml [] =
	"<popup name=\"button3\">\n"
	"   <menuitem name=\"Test Item 1\" verb=\"TestAppletDo1\" _label=\"Test This One\"/>\n"
	"   <menuitem name=\"Test Item 2\" verb=\"TestAppletDo2\" _label=\"Test This Two\"/>\n"
	"   <menuitem name=\"Test Item 3\" verb=\"TestAppletDo3\" _label=\"Test This Three\"/>\n"
	"</popup>\n";

typedef struct {
	MatePanelApplet   base;
	GtkWidget    *label;
} TestApplet;

static GType
test_applet_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (MatePanelAppletClass),
			NULL, NULL, NULL, NULL, NULL,
			sizeof (TestApplet),
			0, NULL, NULL
		};

		type = g_type_register_static (
				PANEL_TYPE_APPLET, "TestApplet", &info, 0);
	}

	return type;
}

static void
test_applet_handle_orient_change (TestApplet        *applet,
				  MatePanelAppletOrient  orient,
				  gpointer           dummy)
{
        gchar *text;

        text = g_strdup (gtk_label_get_text (GTK_LABEL (applet->label)));

        g_strreverse (text);

        gtk_label_set_text (GTK_LABEL (applet->label), text);

        g_free (text);
}

static void
test_applet_handle_size_change (TestApplet *applet,
				gint        size,
				gpointer    dummy)
{
	switch (size) {
	case MATE_Vertigo_PANEL_XX_SMALL:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"xx-small\">Hello</span>");
		break;
	case MATE_Vertigo_PANEL_X_SMALL:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"x-small\">Hello</span>");
		break;
	case MATE_Vertigo_PANEL_SMALL:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"small\">Hello</span>");
		break;
	case MATE_Vertigo_PANEL_MEDIUM:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"medium\">Hello</span>");
		break;
	case MATE_Vertigo_PANEL_LARGE:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"large\">Hello</span>");
		break;
	case MATE_Vertigo_PANEL_X_LARGE:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"x-large\">Hello</span>");
		break;
	case MATE_Vertigo_PANEL_XX_LARGE:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"xx-large\">Hello</span>");
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
test_applet_handle_background_change (TestApplet                *applet,
				      MatePanelAppletBackgroundType  type,
				      GdkColor                  *color,
				      GdkPixmap                 *pixmap,
				      gpointer                   dummy)
{
	GdkWindow *window = gtk_widget_get_window (applet->label);

	switch (type) {
	case PANEL_NO_BACKGROUND:
		g_message ("Setting background to default");
		gdk_window_set_back_pixmap (window, NULL, FALSE);
		break;
	case PANEL_COLOR_BACKGROUND:
		g_message ("Setting background to #%2x%2x%2x",
			    color->red, color->green, color->blue);
		gdk_window_set_back_pixmap (window, NULL, FALSE);
		break;
	case PANEL_PIXMAP_BACKGROUND:
		g_message ("Setting background to '%p'", pixmap);
		gdk_window_set_back_pixmap (window, pixmap, FALSE);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static gboolean
test_applet_fill (TestApplet *applet)
{
	applet->label = gtk_label_new (NULL);

	gtk_container_add (GTK_CONTAINER (applet), applet->label);

	gtk_widget_show_all (GTK_WIDGET (applet));

	test_applet_handle_size_change (applet, MATE_Vertigo_PANEL_MEDIUM, NULL);

	mate_panel_applet_setup_menu (
		MATE_PANEL_APPLET (applet), test_applet_menu_xml, test_applet_menu_verbs, NULL);

	gtk_widget_set_tooltip_text (GTK_WIDGET (applet), "Hello Tip");

	mate_panel_applet_set_flags (MATE_PANEL_APPLET (applet), MATE_PANEL_APPLET_HAS_HANDLE);

	g_signal_connect (G_OBJECT (applet),
			  "change_orient",
			  G_CALLBACK (test_applet_handle_orient_change),
			  NULL);

	g_signal_connect (G_OBJECT (applet),
			  "change_size",
			  G_CALLBACK (test_applet_handle_size_change),
			  NULL);

	g_signal_connect (G_OBJECT (applet),
			  "change_background",
			  G_CALLBACK (test_applet_handle_background_change),
			  NULL);

	return TRUE;
}

static gboolean
test_applet_factory (TestApplet  *applet,
		     const gchar *iid,
		     gpointer     data)
{
	gboolean retval = FALSE;

	if (!strcmp (iid, "OAFIID:MATE_Panel_TestMateComponentApplet"))
		retval = test_applet_fill (applet);

	return retval;
}

MATE_PANEL_APPLET_MATECOMPONENT_FACTORY ("OAFIID:MATE_Panel_TestMateComponentApplet_Factory",
			     test_applet_get_type (),
			     "A Test Applet for the MATE-2.0 Panel",
			     "0",
			     (MatePanelAppletFactoryCallback) test_applet_factory,
			     NULL)
