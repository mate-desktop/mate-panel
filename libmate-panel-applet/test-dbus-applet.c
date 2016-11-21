#include <config.h>
#include <string.h>

#include "mate-panel-applet.h"

static void
test_applet_on_do (GtkAction *action,
		   gpointer   user_data)
{
        g_message ("%s called\n", gtk_action_get_name (action));
}

static const GtkActionEntry test_applet_menu_actions[] = {
	{ "TestAppletDo1", NULL, "TestAppletDo1",
	  NULL, NULL,
	  G_CALLBACK (test_applet_on_do) },
	{ "TestAppletDo2", NULL, "TestAppletDo2",
	  NULL, NULL,
	  G_CALLBACK (test_applet_on_do) },
	{ "TestAppletDo3", NULL, "TestAppletDo3",
	  NULL, NULL,
	  G_CALLBACK (test_applet_on_do) }
};

static const char test_applet_menu_xml[] =
	"<menuitem name=\"Test Item 1\" action=\"TestAppletDo1\" />\n"
	"<menuitem name=\"Test Item 2\" action=\"TestAppletDo2\" />\n"
	"<menuitem name=\"Test Item 3\" action=\"TestAppletDo3\" />\n";

typedef struct _TestApplet      TestApplet;
typedef struct _TestAppletClass TestAppletClass;

struct _TestApplet {
	MatePanelApplet   base;
	GtkWidget    *label;
};

struct _TestAppletClass {
	MatePanelAppletClass   base_class;
};

static GType test_applet_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (TestApplet, test_applet, PANEL_TYPE_APPLET);

static void
test_applet_init (TestApplet *applet)
{
}

static void
test_applet_class_init (TestAppletClass *klass)
{
}

static void
test_applet_handle_orient_change (TestApplet       *applet,
				  MatePanelAppletOrient orient,
				  gpointer          dummy)
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
	case 12:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"xx-small\">Hello</span>");
		break;
	case 24:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"x-small\">Hello</span>");
		break;
	case 36:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"small\">Hello</span>");
		break;
	case 48:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"medium\">Hello</span>");
		break;
	case 64:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"large\">Hello</span>");
		break;
	case 80:
		gtk_label_set_markup (
			GTK_LABEL (applet->label), "<span size=\"x-large\">Hello</span>");
		break;
	case 128:
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
				      cairo_pattern_t           *pattern,
				      gpointer                   dummy)
{
	GdkWindow *window = gtk_widget_get_window (applet->label);

	switch (type) {
	case PANEL_NO_BACKGROUND:
		g_message ("Setting background to default");
		gdk_window_set_background_pattern (window, NULL);
		break;
	case PANEL_COLOR_BACKGROUND:
		g_message ("Setting background to #%2x%2x%2x",
			   color->red, color->green, color->blue);
		gdk_window_set_background_pattern (window, NULL);
		break;
	case PANEL_PIXMAP_BACKGROUND:
		g_message ("Setting background to '%p'", pattern);
		gdk_window_set_background_pattern (window, pattern);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static gboolean
test_applet_fill (TestApplet *applet)
{
	GtkActionGroup *action_group;

	applet->label = gtk_label_new (NULL);

	gtk_container_add (GTK_CONTAINER (applet), applet->label);

	gtk_widget_show_all (GTK_WIDGET (applet));

	test_applet_handle_size_change (applet,
					mate_panel_applet_get_size (MATE_PANEL_APPLET (applet)),
					NULL);
	test_applet_handle_orient_change (applet,
					  mate_panel_applet_get_orient (MATE_PANEL_APPLET (applet)),
					  NULL);

	action_group = gtk_action_group_new ("TestAppletActions");
	gtk_action_group_add_actions (action_group,
				      test_applet_menu_actions,
				      G_N_ELEMENTS (test_applet_menu_actions),
				      applet);

	mate_panel_applet_setup_menu (MATE_PANEL_APPLET (applet),
				 test_applet_menu_xml,
				 action_group);
	g_object_unref (action_group);

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

	if (!strcmp (iid, "TestApplet"))
		retval = test_applet_fill (applet);

	return retval;
}


MATE_PANEL_APPLET_OUT_PROCESS_FACTORY ("TestAppletFactory",
				  test_applet_get_type (),
				  "A Test Applet for the MATE-3.0 Panel",
				  (MatePanelAppletFactoryCallback) test_applet_factory,
				  NULL)

