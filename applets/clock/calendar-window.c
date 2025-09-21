/*
 * calendar-window.c: toplevel window containing a calendar and
 * tasks/appointments
 *
 * Copyright (C) 2007 Vincent Untz <vuntz@gnome.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors:
 *      Vincent Untz <vuntz@gnome.org>
 *
 * Most of the original code comes from clock.c
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "calendar-window.h"

#include "clock.h"
#include "clock-utils.h"
#include "clock-typebuiltins.h"
#ifdef HAVE_EDS
#include "calendar-client.h"
#endif

#define KEY_LOCATIONS_EXPANDED      "expand-locations"

#ifdef HAVE_EDS
#define KEY_SHOW_CALENDAR_EVENTS "show-calendar-events"
#define KEY_SHOW_TASKS           "show-tasks"
#define SCHEMA_CALENDAR_APP      "org.mate.desktop.default-applications.office.calendar"
#define SCHEMA_TASKS_APP         "org.mate.desktop.default-applications.office.tasks"
#endif

enum {
	EDIT_LOCATIONS,
#ifdef HAVE_EDS
	PERMISSION_READY,
#endif
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _CalendarWindowPrivate {
	GtkWidget  *calendar;

	char       *prefs_path;

	gboolean     invert_order;
	gboolean     show_weeks;
	time_t      *current_time;

	GtkWidget *locations_list;

	GSettings  *settings;

        /* Signal handler IDs for proper cleanup */
        gulong calendar_month_changed_id;
        gulong calendar_day_selected_id;

#ifdef HAVE_EDS
	ClockFormat  time_format;

        CalendarClient *client;

        GtkWidget *appointment_list;

        GtkListStore *appointments_model;
        GtkListStore *tasks_model;

        GtkTreeSelection *previous_selection;

        GtkTreeModelFilter *appointments_filter;
        GtkTreeModelFilter *tasks_filter;

        GtkWidget *task_list;
        GtkWidget *task_entry;

        /* EDS-specific signal handler IDs */
        gulong client_appointments_changed_id;
        gulong client_tasks_changed_id;
#endif /* HAVE_EDS */
};

G_DEFINE_TYPE_WITH_PRIVATE (CalendarWindow, calendar_window, GTK_TYPE_WINDOW)

enum {
	PROP_0,
	PROP_INVERTORDER,
	PROP_SHOWWEEKS,
	PROP_CURRENTTIMEP,
	PROP_PREFSPATH
};

static time_t *calendar_window_get_current_time_p (CalendarWindow *calwin);
static void    calendar_window_set_current_time_p (CalendarWindow *calwin,
						   time_t         *current_time);
static const char *calendar_window_get_prefs_path (CalendarWindow *calwin);
static void    calendar_window_set_prefs_path     (CalendarWindow *calwin,
						   const char           *prefs_path);
static GtkWidget * create_hig_frame 		  (CalendarWindow *calwin,
		  				   const char *title,
                  				   const char *button_label,
		  				   const char *key,
                  				   GCallback   callback);

#ifdef HAVE_EDS
enum {
        APPOINTMENT_COLUMN_UID,
        APPOINTMENT_COLUMN_TYPE,
        APPOINTMENT_COLUMN_SUMMARY,
        APPOINTMENT_COLUMN_DESCRIPTION,
        APPOINTMENT_COLUMN_START_TIME,
        APPOINTMENT_COLUMN_START_TEXT,
        APPOINTMENT_COLUMN_END_TIME,
        APPOINTMENT_COLUMN_ALL_DAY,
        APPOINTMENT_COLUMN_COLOR,
        N_APPOINTMENT_COLUMNS
};

enum {
        TASK_COLUMN_UID,
        TASK_COLUMN_TYPE,
        TASK_COLUMN_SUMMARY,
        TASK_COLUMN_DESCRIPTION,
        TASK_COLUMN_START_TIME,
        TASK_COLUMN_START_TEXT,
        TASK_COLUMN_DUE_TIME,
        TASK_COLUMN_DUE_TEXT,
        TASK_COLUMN_PERCENT_COMPLETE,
        TASK_COLUMN_PERCENT_COMPLETE_TEXT,
        TASK_COLUMN_COMPLETED,
        TASK_COLUMN_COMPLETED_TIME,
        TASK_COLUMN_PRIORITY,
        TASK_COLUMN_COLOR,
        N_TASK_COLUMNS
};

enum {
	APPOINTMENT_TYPE_APPOINTMENT,
	TASK_TYPE_TASK
};

static void calendar_window_pack_pim (CalendarWindow *calwin, GtkWidget *vbox);
static char *format_time (ClockFormat format, time_t t, gint year, gint month, gint day);
static void update_frame_visibility (GtkWidget *frame, GtkTreeModel *model);
static GtkWidget *create_appointment_list (CalendarWindow *calwin, GtkWidget **tree_view, GtkWidget **scrolled_window);
static GtkWidget *create_task_list (CalendarWindow *calwin, GtkWidget **tree_view, GtkWidget **scrolled_window);
static void calendar_window_create_appointments_model (CalendarWindow *calwin);
static void calendar_window_create_tasks_model (CalendarWindow *calwin);
static void handle_appointments_changed (CalendarWindow *calwin);
static void handle_tasks_changed (CalendarWindow *calwin);
static void mark_day_on_calendar (CalendarClient *client, guint day, CalendarWindow *calwin);
static gboolean is_for_filter (GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static gboolean appointment_tooltip_query_cb (GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data);
static gboolean task_tooltip_query_cb (GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data);
static void appointment_row_activated_cb (GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data);
static void task_row_activated_cb (GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data);
static void task_completion_toggled_cb (GtkCellRendererToggle *cell, gchar *path_str, CalendarWindow *calwin);
static gboolean task_entry_key_press_cb (GtkWidget *widget, GdkEventKey *event, CalendarWindow *calwin);
static void task_entry_activate_cb (GtkEntry *entry, CalendarWindow *calwin);
#endif /* HAVE_EDS */

static void calendar_mark_today(GtkCalendar *calendar)
{
	time_t now;
	struct tm tm1;
	guint year, month, day;

	gtk_calendar_get_date(calendar, &year, &month, &day);
	time(&now);
	localtime_r (&now, &tm1);
	if ((tm1.tm_mon == (int) month) && (tm1.tm_year + 1900 == (int) year)) {
		gtk_calendar_mark_day (GTK_CALENDAR (calendar), (guint) tm1.tm_mday);
	} else {
		gtk_calendar_unmark_day (GTK_CALENDAR (calendar), (guint) tm1.tm_mday);
	}
}

static gboolean calendar_update(gpointer user_data)
{
	GtkCalendar *calendar = user_data;
	calendar_mark_today(calendar);
	return G_SOURCE_REMOVE;
}

static void calendar_month_changed_cb(GtkCalendar *calendar, gpointer user_data)
{
	gtk_calendar_clear_marks(calendar);
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, calendar_update, calendar, NULL);

#ifdef HAVE_EDS
	/* Update calendar client when date changes */
	CalendarWindow *calwin = CALENDAR_WINDOW (user_data);
	if (calwin->priv->client) {
		guint year, month, day;
		gtk_calendar_get_date (calendar, &year, &month, &day);
		calendar_client_select_month (calwin->priv->client, month, year);
		calendar_client_select_day (calwin->priv->client, day);

		/* Refresh appointments and tasks for the new date */
		handle_appointments_changed (calwin);
		handle_tasks_changed (calwin);
	}
#endif
}

static GtkWidget *
calendar_window_create_calendar (CalendarWindow *calwin)
{
	GtkWidget                 *calendar;
	GtkCalendarDisplayOptions  options;
	struct tm                  tm1;

	calendar = gtk_calendar_new ();
	gtk_widget_set_size_request(GTK_WIDGET(calendar), 330, 100);
	options = gtk_calendar_get_display_options (GTK_CALENDAR (calendar));
	if (calwin->priv->show_weeks)
		options |= GTK_CALENDAR_SHOW_WEEK_NUMBERS;
	else
		options &= ~(GTK_CALENDAR_SHOW_WEEK_NUMBERS);
	gtk_calendar_set_display_options (GTK_CALENDAR (calendar), options);

	localtime_r (calwin->priv->current_time, &tm1);
	gtk_calendar_select_month (GTK_CALENDAR (calendar),
				   (guint) tm1.tm_mon, (guint) (tm1.tm_year + 1900));
	gtk_calendar_select_day (GTK_CALENDAR (calendar), (guint) tm1.tm_mday);
	calendar_mark_today (GTK_CALENDAR(calendar));

	calwin->priv->calendar_month_changed_id = g_signal_connect(calendar, "month-changed",
			 G_CALLBACK(calendar_month_changed_cb), calwin);
	calwin->priv->calendar_day_selected_id = g_signal_connect(calendar, "day-selected",
			 G_CALLBACK(calendar_month_changed_cb), calwin);

	return calendar;
}

static void
expand_collapse_child (GtkWidget *child,
		       gpointer   data)
{
	gboolean expanded;

	if (data == child || gtk_widget_is_ancestor (data, child))
		return;

	expanded = gtk_expander_get_expanded (GTK_EXPANDER (data));
	g_object_set (child, "visible", expanded, NULL);
}

static void
expand_collapse (GtkWidget  *expander,
		 GParamSpec *pspec,
                 gpointer    data)
{
	GtkWidget *box = data;

	gtk_container_foreach (GTK_CONTAINER (box),
			       (GtkCallback)expand_collapse_child,
			       expander);
}

static void add_child (GtkContainer *container,
                       GtkWidget    *child,
                       GtkExpander  *expander)
{
	expand_collapse_child (child, expander);
}

static GtkWidget *
create_hig_frame (CalendarWindow *calwin,
		  const char *title,
                  const char *button_label,
		  const char *key,
                  GCallback   callback)
{
        GtkWidget *vbox;
        GtkWidget *hbox;
        char      *bold_title;
        GtkWidget *expander;

        vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

        bold_title = g_strdup_printf ("<b>%s</b>", title);
	expander = gtk_expander_new (bold_title);
        g_free (bold_title);
	gtk_expander_set_use_markup (GTK_EXPANDER (expander), TRUE);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), expander, FALSE, FALSE, 0);
	gtk_widget_show_all (vbox);

	g_signal_connect (expander, "notify::expanded",
			  G_CALLBACK (expand_collapse), hbox);
	g_signal_connect (expander, "notify::expanded",
			  G_CALLBACK (expand_collapse), vbox);

	/* FIXME: this doesn't really work, since "add" does not
	 * get emitted for e.g. gtk_box_pack_start
	 */
	g_signal_connect (vbox, "add", G_CALLBACK (add_child), expander);
	g_signal_connect (hbox, "add", G_CALLBACK (add_child), expander);

        if (button_label) {
                GtkWidget *label;
                GtkWidget *button_box;
                GtkWidget *button;
                gchar     *text;

                button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
                gtk_widget_show (button_box);

                button = gtk_button_new ();
                gtk_container_add (GTK_CONTAINER (button_box), button);

                text = g_markup_printf_escaped ("<small>%s</small>", button_label);
                label = gtk_label_new (text);
                g_free (text);
                gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
                gtk_container_add (GTK_CONTAINER (button), label);

                gtk_widget_show_all (button);

                gtk_box_pack_end (GTK_BOX (hbox), button_box, FALSE, FALSE, 0);

                g_signal_connect_swapped (button, "clicked", callback, calwin);

                g_object_bind_property (expander, "expanded",
                                        button_box, "visible",
                                        G_BINDING_DEFAULT|G_BINDING_SYNC_CREATE);
        }

	g_settings_bind (calwin->priv->settings, key, expander, "expanded",
			 G_SETTINGS_BIND_DEFAULT);

        return vbox;
}

static void
edit_locations (CalendarWindow *calwin)
{
	g_signal_emit (calwin, signals[EDIT_LOCATIONS], 0);
}

#ifdef HAVE_EDS
static gboolean
hide_task_entry_idle (gpointer user_data)
{
	CalendarWindow *calwin = CALENDAR_WINDOW (user_data);
	if (calwin->priv->task_entry) {
		gtk_widget_hide (calwin->priv->task_entry);
	}
	return FALSE; /* Remove the idle source */
}

static gboolean
focus_task_entry_idle (gpointer user_data)
{
	CalendarWindow *calwin = CALENDAR_WINDOW (user_data);
	if (calwin->priv->task_entry && gtk_widget_get_visible (calwin->priv->task_entry)) {
		gtk_widget_grab_focus (calwin->priv->task_entry);
	}
	return FALSE; /* Remove the idle source */
}

static void
add_task (CalendarWindow *calwin)
{
	if (calwin->priv->task_entry) {
		gtk_widget_show (calwin->priv->task_entry);
		gtk_widget_set_can_focus (calwin->priv->task_entry, TRUE);
		gtk_widget_set_sensitive (calwin->priv->task_entry, TRUE);

		/* Make sure parent window is active */
		gtk_window_present (GTK_WINDOW (calwin));

		/* Ensure widget is realized */
		if (!gtk_widget_get_realized (calwin->priv->task_entry)) {
			gtk_widget_realize (calwin->priv->task_entry);
		}

		/* Try to grab focus immediately */
		gtk_widget_grab_focus (calwin->priv->task_entry);

		/* Also try to grab focus in idle callback in case immediate focus fails */
		g_idle_add (focus_task_entry_idle, calwin);
	}
}
#endif

static void
calendar_window_pack_locations (CalendarWindow *calwin, GtkWidget *vbox)
{
	calwin->priv->locations_list = create_hig_frame (calwin,
							 _("Locations"), _("Edit"),
							 KEY_LOCATIONS_EXPANDED,
							 G_CALLBACK (edit_locations));

	/* we show the widget before adding to the container, since adding to
	 * the container changes the visibility depending on the state of the
	 * expander */
	gtk_widget_show (calwin->priv->locations_list);
	gtk_container_add (GTK_CONTAINER (vbox), calwin->priv->locations_list);

	/* gtk_box_pack_start (GTK_BOX (vbox), calwin->priv->locations_list, TRUE, FALSE, 0); */
}

static void
calendar_window_fill (CalendarWindow *calwin)
{
        GtkWidget *frame;
        GtkWidget *vbox;

        frame = gtk_frame_new (NULL);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
        gtk_container_add (GTK_CONTAINER (calwin), frame);
        gtk_widget_show (frame);

        vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

        gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
        gtk_container_add (GTK_CONTAINER (frame), vbox);
        gtk_widget_show (vbox);

	calwin->priv->calendar = calendar_window_create_calendar (calwin);
        gtk_widget_show (calwin->priv->calendar);

#ifdef HAVE_EDS
        /* Calendar client will be initialized later in calendar_window_pack_pim */
#endif

	if (!calwin->priv->invert_order) {
                gtk_box_pack_start (GTK_BOX (vbox),
				    calwin->priv->calendar, TRUE, FALSE, 0);
#ifdef HAVE_EDS
                calendar_window_pack_pim (calwin, vbox);
#endif
		calendar_window_pack_locations (calwin, vbox);
	} else {
		calendar_window_pack_locations (calwin, vbox);
#ifdef HAVE_EDS
                calendar_window_pack_pim (calwin, vbox);
#endif
                gtk_box_pack_start (GTK_BOX (vbox),
				    calwin->priv->calendar, TRUE, FALSE, 0);
	}
}

GtkWidget *
calendar_window_get_locations_box (CalendarWindow *calwin)
{
	return calwin->priv->locations_list;
}

static GObject *
calendar_window_constructor (GType                  type,
			     guint                  n_construct_properties,
			     GObjectConstructParam *construct_properties)
{
	GObject        *obj;
	CalendarWindow *calwin;

	obj = G_OBJECT_CLASS (calendar_window_parent_class)->constructor (type,
									  n_construct_properties,
									  construct_properties);

	calwin = CALENDAR_WINDOW (obj);

	g_assert (calwin->priv->current_time != NULL);
	g_assert (calwin->priv->prefs_path != NULL);

	calendar_window_fill (calwin);

	return obj;
}

static void
calendar_window_get_property (GObject    *object,
			      guint       prop_id,
			      GValue     *value,
			      GParamSpec *pspec)
{
	CalendarWindow *calwin;

	g_return_if_fail (CALENDAR_IS_WINDOW (object));

	calwin = CALENDAR_WINDOW (object);

	switch (prop_id) {
	case PROP_INVERTORDER:
		g_value_set_boolean (value,
				     calendar_window_get_invert_order (calwin));
		break;
	case PROP_SHOWWEEKS:
		g_value_set_boolean (value,
				     calendar_window_get_show_weeks (calwin));
		break;
	case PROP_CURRENTTIMEP:
		g_value_set_pointer (value,
				     calendar_window_get_current_time_p (calwin));
		break;
	case PROP_PREFSPATH:
		g_value_set_string (value,
				    calendar_window_get_prefs_path (calwin));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
calendar_window_set_property (GObject       *object,
			      guint          prop_id,
			      const GValue  *value,
			      GParamSpec    *pspec)
{
	CalendarWindow *calwin;

	g_return_if_fail (CALENDAR_IS_WINDOW (object));

	calwin = CALENDAR_WINDOW (object);

	switch (prop_id) {
	case PROP_INVERTORDER:
		calendar_window_set_invert_order (calwin,
						  g_value_get_boolean (value));
		break;
	case PROP_SHOWWEEKS:
		calendar_window_set_show_weeks (calwin,
						g_value_get_boolean (value));
		break;
	case PROP_CURRENTTIMEP:
		calendar_window_set_current_time_p (calwin,
						    g_value_get_pointer (value));
		break;
	case PROP_PREFSPATH:
		calendar_window_set_prefs_path (calwin,
					        g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
calendar_window_dispose (GObject *object)
{
	CalendarWindow *calwin;

	calwin = CALENDAR_WINDOW (object);

	g_clear_pointer (&calwin->priv->prefs_path, g_free);

	/* Disconnect calendar signals */
	if (calwin->priv->calendar && calwin->priv->calendar_month_changed_id > 0) {
		g_signal_handler_disconnect (calwin->priv->calendar, calwin->priv->calendar_month_changed_id);
		calwin->priv->calendar_month_changed_id = 0;
	}
	if (calwin->priv->calendar && calwin->priv->calendar_day_selected_id > 0) {
		g_signal_handler_disconnect (calwin->priv->calendar, calwin->priv->calendar_day_selected_id);
		calwin->priv->calendar_day_selected_id = 0;
	}

#ifdef HAVE_EDS
	/* Disconnect client signals */
	if (calwin->priv->client) {
		if (calwin->priv->client_appointments_changed_id > 0) {
			g_signal_handler_disconnect (calwin->priv->client, calwin->priv->client_appointments_changed_id);
			calwin->priv->client_appointments_changed_id = 0;
		}
		if (calwin->priv->client_tasks_changed_id > 0) {
			g_signal_handler_disconnect (calwin->priv->client, calwin->priv->client_tasks_changed_id);
			calwin->priv->client_tasks_changed_id = 0;
		}
		g_signal_handlers_disconnect_by_data (calwin->priv->client, calwin);

		/* Client is owned by ClockData, don't unref it */
		calwin->priv->client = NULL;
	}
#endif

	if (calwin->priv->settings)
		g_object_unref (calwin->priv->settings);
	calwin->priv->settings = NULL;

	G_OBJECT_CLASS (calendar_window_parent_class)->dispose (object);
}

static void
calendar_window_class_init (CalendarWindowClass *klass)
{
	GObjectClass   *gobject_class   = G_OBJECT_CLASS (klass);

	gobject_class->constructor = calendar_window_constructor;
	gobject_class->get_property = calendar_window_get_property;
	gobject_class->set_property = calendar_window_set_property;

	gobject_class->dispose = calendar_window_dispose;

	signals[EDIT_LOCATIONS] = g_signal_new ("edit-locations",
						G_TYPE_FROM_CLASS (gobject_class),
						G_SIGNAL_RUN_FIRST,
						G_STRUCT_OFFSET (CalendarWindowClass, edit_locations),
						NULL,
						NULL,
						g_cclosure_marshal_VOID__VOID,
						G_TYPE_NONE, 0);

	g_object_class_install_property (
		gobject_class,
		PROP_INVERTORDER,
		g_param_spec_boolean ("invert-order",
				      "Invert Order",
				      "Invert order of the calendar and tree views",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		gobject_class,
		PROP_SHOWWEEKS,
		g_param_spec_boolean ("show-weeks",
				      "Show Weeks",
				      "Show weeks in the calendar",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (
		gobject_class,
		PROP_CURRENTTIMEP,
		g_param_spec_pointer ("current-time",
				      "Current Time",
				      "Pointer to a variable containing the current time",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		gobject_class,
		PROP_PREFSPATH,
		g_param_spec_string ("prefs-path",
				     "Preferences Path",
				     "Preferences path in GSettings",
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
calendar_window_init (CalendarWindow *calwin)
{
	GtkWindow *window;

	calwin->priv = calendar_window_get_instance_private (calwin);

#ifdef HAVE_EDS
	/* Initialize signal handler IDs */
	calwin->priv->calendar_month_changed_id = 0;
	calwin->priv->calendar_day_selected_id = 0;
	calwin->priv->client_appointments_changed_id = 0;
#endif

	window = GTK_WINDOW (calwin);
	gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_DOCK);
	gtk_window_set_decorated (window, FALSE);
	gtk_window_set_resizable (window, FALSE);
	gtk_window_set_default_size (window, 337, -1);
	gtk_window_stick (window);
	gtk_window_set_title (window, _("Calendar"));
	gtk_window_set_icon_name (window, CLOCK_ICON);
}

GtkWidget *
calendar_window_new (time_t     *static_current_time,
		     const char *prefs_path,
		     gboolean    invert_order,
		     GSettings  *settings)
{
	CalendarWindow *calwin;

	calwin = g_object_new (CALENDAR_TYPE_WINDOW,
			       "type", GTK_WINDOW_TOPLEVEL,
			       "current-time", static_current_time,
			       "invert-order", invert_order,
			       "prefs-path", prefs_path,
			       NULL);

#ifdef HAVE_EDS
	/* Store settings for calendar client initialization in init */
	if (settings) {
		calwin->priv->settings = g_object_ref (settings);
	}
#endif

	return GTK_WIDGET (calwin);
}

void
calendar_window_refresh (CalendarWindow *calwin)
{
	g_return_if_fail (CALENDAR_IS_WINDOW (calwin));

#ifdef HAVE_EDS
	if (calwin->priv->appointments_filter && calwin->priv->appointment_list)
		gtk_tree_model_filter_refilter (calwin->priv->appointments_filter);

	/* Update frame visibility based on model content */
	if (calwin->priv->appointment_list && calwin->priv->appointments_filter)
		update_frame_visibility (calwin->priv->appointment_list,
					 GTK_TREE_MODEL (calwin->priv->appointments_filter));
#endif
}

gboolean
calendar_window_get_invert_order (CalendarWindow *calwin)
{
	g_return_val_if_fail (CALENDAR_IS_WINDOW (calwin), FALSE);

	return calwin->priv->invert_order;
}

void
calendar_window_set_invert_order (CalendarWindow *calwin,
				  gboolean        invert_order)
{
	g_return_if_fail (CALENDAR_IS_WINDOW (calwin));

	if (invert_order == calwin->priv->invert_order)
		return;

	calwin->priv->invert_order = invert_order;
	/* FIXME: update the order of the content of the window */

	g_object_notify (G_OBJECT (calwin), "invert-order");
}

gboolean
calendar_window_get_show_weeks (CalendarWindow *calwin)
{
	g_return_val_if_fail (CALENDAR_IS_WINDOW (calwin), FALSE);

	return calwin->priv->show_weeks;
}

void
calendar_window_set_show_weeks (CalendarWindow *calwin,
				gboolean        show_weeks)
{
	g_return_if_fail (CALENDAR_IS_WINDOW (calwin));

	if (show_weeks == calwin->priv->show_weeks)
		return;

	calwin->priv->show_weeks = show_weeks;

	if (calwin->priv->calendar) {
		GtkCalendarDisplayOptions options;

		options = gtk_calendar_get_display_options (GTK_CALENDAR (calwin->priv->calendar));

		if (show_weeks)
			options |= GTK_CALENDAR_SHOW_WEEK_NUMBERS;
		else
			options &= ~(GTK_CALENDAR_SHOW_WEEK_NUMBERS);

		gtk_calendar_set_display_options (GTK_CALENDAR (calwin->priv->calendar),
						  options);
	}

	g_object_notify (G_OBJECT (calwin), "show-weeks");
}

ClockFormat
calendar_window_get_time_format (CalendarWindow *calwin)
{
	g_return_val_if_fail (CALENDAR_IS_WINDOW (calwin),
			      CLOCK_FORMAT_INVALID);

#ifdef HAVE_EDS
	return calwin->priv->time_format;
#else
	return CLOCK_FORMAT_INVALID;
#endif
}

static time_t *
calendar_window_get_current_time_p (CalendarWindow *calwin)
{
	g_return_val_if_fail (CALENDAR_IS_WINDOW (calwin), NULL);

	return calwin->priv->current_time;
}

static void
calendar_window_set_current_time_p (CalendarWindow *calwin,
				    time_t         *current_time)
{
	g_return_if_fail (CALENDAR_IS_WINDOW (calwin));

	if (current_time == calwin->priv->current_time)
		return;

	calwin->priv->current_time = current_time;

	g_object_notify (G_OBJECT (calwin), "current-time");
}

static const char *
calendar_window_get_prefs_path (CalendarWindow *calwin)
{
	g_return_val_if_fail (CALENDAR_IS_WINDOW (calwin), NULL);

	return calwin->priv->prefs_path;
}

static void
calendar_window_set_prefs_path (CalendarWindow *calwin,
				const char     *prefs_path)
{
	g_return_if_fail (CALENDAR_IS_WINDOW (calwin));

	if (!calwin->priv->prefs_path && (!prefs_path || !prefs_path [0]))
		return;

	if (calwin->priv->prefs_path && prefs_path && prefs_path [0] &&
	    !strcmp (calwin->priv->prefs_path, prefs_path))
		return;

	g_free (calwin->priv->prefs_path);
	if (prefs_path && prefs_path [0])
		calwin->priv->prefs_path = g_strdup (prefs_path);
	else
		calwin->priv->prefs_path = NULL;

	g_object_notify (G_OBJECT (calwin), "prefs-path");

	/* Only create new settings if we don't already have shared settings */
	if (!calwin->priv->settings) {
		calwin->priv->settings = g_settings_new_with_path (CLOCK_SCHEMA, calwin->priv->prefs_path);
	}
}

#ifdef HAVE_EDS

static char *
format_time (ClockFormat format,
             time_t      t,
             gint        year,
             gint        month,
             gint        day)
{
        GDateTime *dt;
        gchar *time;

        if (!t)
                return NULL;

        /* Evolution timestamps are in UTC but represent local appointment times
         * Since TZID lookup failed, treat UTC timestamp as local time directly */
        dt = g_date_time_new_from_unix_utc (t);
        time = NULL;

        if (!dt)
                return NULL;

        /* Always show time since we're filtering by selected date */
        if (format == CLOCK_FORMAT_12) {
                /* Translators: This is a strftime format string.
                 * It is used to display the time in 12-hours format
                 * (eg, like in the US: 8:10 am). The %p expands to
                 * am/pm.
                 */
                time = g_date_time_format (dt, _("%l:%M %p"));
        } else {
                /* Translators: This is a strftime format string.
                 * It is used to display the time in 24-hours format
                 * (eg, like in France: 20:10).
                 */
                time = g_date_time_format (dt, _("%H:%M"));
        }

        g_date_time_unref (dt);
        return time;
}

static void
update_frame_visibility (GtkWidget    *frame,
                         GtkTreeModel *model)
{
        GtkTreeIter iter;
        gboolean    model_empty;

        if (!frame)
                return;

        model_empty = !gtk_tree_model_get_iter_first (model, &iter);

        if (model_empty)
                gtk_widget_hide (frame);
        else
                gtk_widget_show (frame);
}


static void
calendar_window_create_appointments_model (CalendarWindow *calwin)
{
        calwin->priv->appointments_model = gtk_list_store_new (N_APPOINTMENT_COLUMNS,
                                                               G_TYPE_STRING,  /* APPOINTMENT_COLUMN_UID */
                                                               G_TYPE_INT,     /* APPOINTMENT_COLUMN_TYPE */
                                                               G_TYPE_STRING,  /* APPOINTMENT_COLUMN_SUMMARY */
                                                               G_TYPE_STRING,  /* APPOINTMENT_COLUMN_DESCRIPTION */
                                                               G_TYPE_ULONG,   /* APPOINTMENT_COLUMN_START_TIME */
                                                               G_TYPE_STRING,  /* APPOINTMENT_COLUMN_START_TEXT */
                                                               G_TYPE_ULONG,   /* APPOINTMENT_COLUMN_END_TIME */
                                                               G_TYPE_BOOLEAN, /* APPOINTMENT_COLUMN_ALL_DAY */
                                                               G_TYPE_STRING); /* APPOINTMENT_COLUMN_COLOR */

        calwin->priv->appointments_filter = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (GTK_TREE_MODEL (calwin->priv->appointments_model), NULL));
        gtk_tree_model_filter_set_visible_func (calwin->priv->appointments_filter,
                                                (GtkTreeModelFilterVisibleFunc) is_for_filter,
                                                GINT_TO_POINTER (APPOINTMENT_TYPE_APPOINTMENT),
                                                NULL);
}

static void
calendar_window_create_tasks_model (CalendarWindow *calwin)
{
        calwin->priv->tasks_model = gtk_list_store_new (N_TASK_COLUMNS,
                                                        G_TYPE_STRING,  /* TASK_COLUMN_UID */
                                                        G_TYPE_INT,     /* TASK_COLUMN_TYPE */
                                                        G_TYPE_STRING,  /* TASK_COLUMN_SUMMARY */
                                                        G_TYPE_STRING,  /* TASK_COLUMN_DESCRIPTION */
                                                        G_TYPE_ULONG,   /* TASK_COLUMN_START_TIME */
                                                        G_TYPE_STRING,  /* TASK_COLUMN_START_TEXT */
                                                        G_TYPE_ULONG,   /* TASK_COLUMN_DUE_TIME */
                                                        G_TYPE_STRING,  /* TASK_COLUMN_DUE_TEXT */
                                                        G_TYPE_INT,     /* TASK_COLUMN_PERCENT_COMPLETE */
                                                        G_TYPE_STRING,  /* TASK_COLUMN_PERCENT_COMPLETE_TEXT */
                                                        G_TYPE_BOOLEAN, /* TASK_COLUMN_COMPLETED */
                                                        G_TYPE_ULONG,   /* TASK_COLUMN_COMPLETED_TIME */
                                                        G_TYPE_INT,     /* TASK_COLUMN_PRIORITY */
                                                        G_TYPE_STRING); /* TASK_COLUMN_COLOR */

        calwin->priv->tasks_filter = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (GTK_TREE_MODEL (calwin->priv->tasks_model), NULL));
        gtk_tree_model_filter_set_visible_func (calwin->priv->tasks_filter,
                                                (GtkTreeModelFilterVisibleFunc) is_for_filter,
                                                GINT_TO_POINTER (TASK_TYPE_TASK),
                                                NULL);
}

static GtkWidget *
create_hig_calendar_frame (CalendarWindow *calwin,
                           const char     *title,
                           const char     *button_label,
                           const char     *key,
                           GCallback       callback)
{
        return create_hig_frame (calwin, title, button_label, key, callback);
}


static GtkWidget *
create_appointment_list (CalendarWindow *calwin,
                        GtkWidget     **tree_view,
                        GtkWidget     **scrolled_window)
{
        GtkWidget         *frame;
        GtkWidget         *list;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *cell;

        frame = create_hig_calendar_frame (calwin, _("Appointments"), NULL,
					   KEY_SHOW_CALENDAR_EVENTS, NULL);

        list = gtk_tree_view_new ();
        gtk_tree_view_set_model (GTK_TREE_VIEW (list),
                                GTK_TREE_MODEL (calwin->priv->appointments_filter));

        column = gtk_tree_view_column_new ();
        cell = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, cell, FALSE);
        gtk_tree_view_column_set_attributes (column, cell,
                                            "text", APPOINTMENT_COLUMN_START_TEXT,
                                            NULL);

        cell = gtk_cell_renderer_text_new ();
        g_object_set (cell,
                     "wrap-mode", PANGO_WRAP_WORD_CHAR,
                     "wrap-width", 200,
                     "ellipsize", PANGO_ELLIPSIZE_END,
                     NULL);
        gtk_tree_view_column_pack_start (column, cell, TRUE);
        gtk_tree_view_column_set_attributes (column, cell,
                                            "text", APPOINTMENT_COLUMN_SUMMARY,
                                            NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);

        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
        gtk_widget_set_has_tooltip (list, TRUE);
        g_signal_connect (list, "query-tooltip", G_CALLBACK (appointment_tooltip_query_cb), calwin);
        g_signal_connect (list, "row-activated", G_CALLBACK (appointment_row_activated_cb), calwin);

        *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (*scrolled_window),
                                       GTK_POLICY_NEVER,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (*scrolled_window),
                                            GTK_SHADOW_IN);
        gtk_widget_set_size_request (*scrolled_window, -1, 150);
        gtk_container_add (GTK_CONTAINER (*scrolled_window), list);

        gtk_container_add (GTK_CONTAINER (frame), *scrolled_window);

        /* Ensure the scrolled window and tree view are visible */
        gtk_widget_show (*scrolled_window);
        gtk_widget_show (list);

        /* Appointment list widgets created */

        *tree_view = list;
        return frame;
}

static GtkWidget *
create_task_list (CalendarWindow *calwin,
		  GtkWidget     **tree_view,
		  GtkWidget     **scrolled_window)
{
	GtkWidget         *frame;
	GtkWidget         *list;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *cell;

	frame = create_hig_calendar_frame (calwin, _("Tasks"), _("Add"),
					   KEY_SHOW_TASKS, G_CALLBACK (add_task));

	list = gtk_tree_view_new ();
	gtk_tree_view_set_model (GTK_TREE_VIEW (list),
				 GTK_TREE_MODEL (calwin->priv->tasks_filter));

	column = gtk_tree_view_column_new ();

	/* Completion checkbox */
	cell = gtk_cell_renderer_toggle_new ();
	gtk_tree_view_column_pack_start (column, cell, FALSE);
	gtk_tree_view_column_set_attributes (column, cell,
					     "active", TASK_COLUMN_COMPLETED,
					     NULL);
	g_signal_connect (cell, "toggled", G_CALLBACK (task_completion_toggled_cb), calwin);


	/* Task summary */
	cell = gtk_cell_renderer_text_new ();
	g_object_set (cell,
		      "wrap-mode", PANGO_WRAP_WORD_CHAR,
		      "wrap-width", 200,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      NULL);
	gtk_tree_view_column_pack_start (column, cell, TRUE);
	gtk_tree_view_column_set_attributes (column, cell,
					     "text", TASK_COLUMN_SUMMARY,
					     "strikethrough", TASK_COLUMN_COMPLETED,
					     NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_widget_set_has_tooltip (list, TRUE);
	g_signal_connect (list, "query-tooltip", G_CALLBACK (task_tooltip_query_cb), calwin);
	g_signal_connect (list, "row-activated", G_CALLBACK (task_row_activated_cb), calwin);

	*scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (*scrolled_window),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (*scrolled_window),
					     GTK_SHADOW_IN);
	gtk_widget_set_size_request (*scrolled_window, -1, 150);
	gtk_container_add (GTK_CONTAINER (*scrolled_window), list);

	gtk_container_add (GTK_CONTAINER (frame), *scrolled_window);

	/* Create task entry field */
	calwin->priv->task_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (calwin->priv->task_entry), _("Enter task description..."));
	gtk_widget_set_can_focus (calwin->priv->task_entry, TRUE);
	gtk_widget_set_sensitive (calwin->priv->task_entry, TRUE);
	g_signal_connect (calwin->priv->task_entry, "key-press-event", G_CALLBACK (task_entry_key_press_cb), calwin);
	g_signal_connect (calwin->priv->task_entry, "activate", G_CALLBACK (task_entry_activate_cb), calwin);
	gtk_container_add (GTK_CONTAINER (frame), calwin->priv->task_entry);

	/* Ensure the scrolled window and tree view are visible */
	gtk_widget_show (*scrolled_window);
	gtk_widget_show (list);

	/* Hide task entry after all show operations are complete */
	g_idle_add (hide_task_entry_idle, calwin);

	*tree_view = list;
	return frame;
}

static void
calendar_window_pack_pim (CalendarWindow *calwin,
                          GtkWidget      *vbox)
{
	GtkWidget *list;
	GtkWidget *tree_view;
	GtkWidget *scrolled_window;
	gboolean show_calendar_events;
	gboolean show_tasks;

	/* Check if calendar events should be shown */
	show_calendar_events = g_settings_get_boolean (calwin->priv->settings, KEY_SHOW_CALENDAR_EVENTS);
	show_tasks = g_settings_get_boolean (calwin->priv->settings, KEY_SHOW_TASKS);

	if (!show_calendar_events && !show_tasks) {
		return;
	}

	/* Connect signals if client is available */
	if (calwin->priv->client) {
		if (show_calendar_events && calwin->priv->client_appointments_changed_id == 0) {
			calwin->priv->client_appointments_changed_id = g_signal_connect_swapped (calwin->priv->client,
											 "appointments-changed",
											 G_CALLBACK (handle_appointments_changed),
											 calwin);
		}
		if (show_tasks && calwin->priv->client_tasks_changed_id == 0) {
			calwin->priv->client_tasks_changed_id = g_signal_connect_swapped (calwin->priv->client,
										  "tasks-changed",
										  G_CALLBACK (handle_tasks_changed),
										  calwin);
		}
	}


	/* Create and pack appointments list if enabled */
	if (show_calendar_events) {
		calendar_window_create_appointments_model (calwin);
		list = create_appointment_list (calwin, &tree_view, &scrolled_window);
		update_frame_visibility (list,
					 GTK_TREE_MODEL (calwin->priv->appointments_filter));
		calwin->priv->appointment_list = list;

		gtk_box_pack_start (GTK_BOX (vbox),
				    calwin->priv->appointment_list,
				    TRUE, TRUE, 0);
	}

	/* Create and pack tasks list if enabled */
	if (show_tasks) {
		calendar_window_create_tasks_model (calwin);
		list = create_task_list (calwin, &tree_view, &scrolled_window);
		update_frame_visibility (list,
					 GTK_TREE_MODEL (calwin->priv->tasks_filter));
		calwin->priv->task_list = list;

		gtk_box_pack_start (GTK_BOX (vbox),
				    calwin->priv->task_list,
				    TRUE, TRUE, 0);
	}
}

static gboolean
is_for_filter (GtkTreeModel *model,
               GtkTreeIter  *iter,
               gpointer      data)
{
        gint type;
        gint expected_type = GPOINTER_TO_INT (data);

        /* Check if this is a task model or appointment model */
        if (expected_type == TASK_TYPE_TASK) {
                gtk_tree_model_get (model, iter, TASK_COLUMN_TYPE, &type, -1);
        } else {
                gtk_tree_model_get (model, iter, APPOINTMENT_COLUMN_TYPE, &type, -1);
        }
        return type == expected_type;
}

static void
mark_day_on_calendar (CalendarClient *client,
                      guint           day,
                      CalendarWindow *calwin)
{
        gtk_calendar_mark_day (GTK_CALENDAR (calwin->priv->calendar), day);
}



static gint
compare_appointments_by_time (const CalendarAppointment *a, const CalendarAppointment *b)
{
        /* Sort by start time - earlier appointments first */
        if (a->start_time < b->start_time)
                return -1;
        else if (a->start_time > b->start_time)
                return 1;
        else
                return 0;
}

static gint
compare_tasks_by_due_time (const CalendarTask *a, const CalendarTask *b)
{
        /* Sort by due time - earlier due dates first, then by priority */
        if (a->due_time && b->due_time) {
                if (a->due_time < b->due_time)
                        return -1;
                else if (a->due_time > b->due_time)
                        return 1;
        } else if (a->due_time && !b->due_time) {
                return -1; /* Tasks with due dates come first */
        } else if (!a->due_time && b->due_time) {
                return 1;
        }

        /* If due times are equal or both missing, sort by priority (higher priority first) */
        if (a->priority > b->priority)
                return -1;
        else if (a->priority < b->priority)
                return 1;
        else
                return 0;
}

static void
handle_appointments_changed (CalendarWindow *calwin)
{
        GSList *events, *l;
        guint   year, month, day;

        /* Skip redundant calls during initialization */
        if (g_object_get_data (G_OBJECT (calwin), "initializing")) {
                return;
        }

        if (calwin->priv->calendar) {
                gtk_calendar_clear_marks (GTK_CALENDAR (calwin->priv->calendar));

                calendar_client_foreach_appointment_day (calwin->priv->client,
                                                         (CalendarDayIter) mark_day_on_calendar,
                                                         calwin);
        }

        gtk_list_store_clear (calwin->priv->appointments_model);

        calendar_client_get_date (calwin->priv->client, &year, &month, &day);

        events = calendar_client_get_events (calwin->priv->client,
                                             CALENDAR_EVENT_APPOINTMENT);

        /* Sort appointments by start time for better display order */
        events = g_slist_sort (events, (GCompareFunc) compare_appointments_by_time);

        /* Found appointments for current date */
        for (l = events; l; l = l->next) {
                CalendarAppointment *appointment = l->data;
                GtkTreeIter          iter;
                char                *start_text;

                g_assert (CALENDAR_EVENT (appointment)->type == CALENDAR_EVENT_APPOINTMENT);

                if (appointment->is_all_day)
                        start_text = g_strdup (_("All Day"));
                else
                        start_text = format_time (calendar_window_get_time_format (calwin),
                                                  appointment->start_time,
                                                  year, month, day);

                gtk_list_store_append (calwin->priv->appointments_model, &iter);
                /* Appointment added to model */
                gtk_list_store_set (calwin->priv->appointments_model, &iter,
                                    APPOINTMENT_COLUMN_UID,         appointment->uid,
                                    APPOINTMENT_COLUMN_TYPE,        APPOINTMENT_TYPE_APPOINTMENT,
                                    APPOINTMENT_COLUMN_SUMMARY,     appointment->summary,
                                    APPOINTMENT_COLUMN_DESCRIPTION, appointment->description,
                                    APPOINTMENT_COLUMN_START_TIME,  (gint64)appointment->start_time,
                                    APPOINTMENT_COLUMN_START_TEXT,  start_text,
                                    APPOINTMENT_COLUMN_END_TIME,    (gint64)appointment->end_time,
                                    APPOINTMENT_COLUMN_ALL_DAY,     appointment->is_all_day,
                                    APPOINTMENT_COLUMN_COLOR,       appointment->color_string,
                                    -1);

                g_free (start_text);
        }

        /* Refresh filter before checking visibility */
        if (calwin->priv->appointments_filter)
                gtk_tree_model_filter_refilter (calwin->priv->appointments_filter);

        update_frame_visibility (calwin->priv->appointment_list,
                                 GTK_TREE_MODEL (calwin->priv->appointments_filter));
}

static void
handle_tasks_changed (CalendarWindow *calwin)
{
        GSList *events, *l;
        guint   year, month, day;

        /* Skip redundant calls during initialization */
        if (g_object_get_data (G_OBJECT (calwin), "initializing")) {
                return;
        }

        if (!calwin->priv->tasks_model) {
                return;
        }

        gtk_list_store_clear (calwin->priv->tasks_model);

        calendar_client_get_date (calwin->priv->client, &year, &month, &day);

        events = calendar_client_get_events (calwin->priv->client,
                                             CALENDAR_EVENT_TASK);

        /* Sort tasks by due time for better display order */
        events = g_slist_sort (events, (GCompareFunc) compare_tasks_by_due_time);

        /* Found tasks for current date */
        for (l = events; l; l = l->next) {
                CalendarTask *task = (CalendarTask *) l->data;
                GtkTreeIter   iter;
                char         *start_text = NULL;
                char         *due_text = NULL;
                char         *percent_complete_text = NULL;
                gboolean      completed;

                g_assert (CALENDAR_EVENT (task)->type == CALENDAR_EVENT_TASK);

                if (task->start_time) {
                        start_text = format_time (calendar_window_get_time_format (calwin),
                                                  task->start_time,
                                                  year, month, day);
                } else {
                        start_text = g_strdup ("");
                }

                if (task->due_time) {
                        due_text = format_time (calendar_window_get_time_format (calwin),
                                                task->due_time,
                                                year, month, day);
                } else {
                        due_text = g_strdup ("");
                }

                /* Format percent complete as text */
                if (task->percent_complete > 0) {
                        percent_complete_text = g_strdup_printf ("%d%%", task->percent_complete);
                } else {
                        percent_complete_text = g_strdup ("");
                }

                completed = (task->percent_complete == 100);

                gtk_list_store_append (calwin->priv->tasks_model, &iter);
                gtk_list_store_set (calwin->priv->tasks_model, &iter,
                                    TASK_COLUMN_UID,                   task->uid,
                                    TASK_COLUMN_TYPE,                  TASK_TYPE_TASK,
                                    TASK_COLUMN_SUMMARY,               task->summary,
                                    TASK_COLUMN_DESCRIPTION,           task->description,
                                    TASK_COLUMN_START_TIME,            (gint64)task->start_time,
                                    TASK_COLUMN_START_TEXT,            start_text,
                                    TASK_COLUMN_DUE_TIME,              (gint64)task->due_time,
                                    TASK_COLUMN_DUE_TEXT,              due_text,
                                    TASK_COLUMN_PERCENT_COMPLETE,      task->percent_complete,
                                    TASK_COLUMN_PERCENT_COMPLETE_TEXT, percent_complete_text,
                                    TASK_COLUMN_COMPLETED,             completed,
                                    TASK_COLUMN_COMPLETED_TIME,        (gint64)task->completed_time,
                                    TASK_COLUMN_PRIORITY,              task->priority,
                                    TASK_COLUMN_COLOR,                 task->color_string,
                                    -1);

                g_free (start_text);
                g_free (due_text);
                g_free (percent_complete_text);
        }

        /* Refresh filter before checking visibility */
        if (calwin->priv->tasks_filter)
                gtk_tree_model_filter_refilter (calwin->priv->tasks_filter);

        update_frame_visibility (calwin->priv->task_list,
                                 GTK_TREE_MODEL (calwin->priv->tasks_filter));
}

static void
appointment_row_activated_cb (GtkTreeView       *tree_view,
                             GtkTreePath       *path,
                             GtkTreeViewColumn *column,
                             gpointer           user_data)
{
        GAppInfo *app_info;
        GError *error = NULL;

        /* Launch Evolution calendar */
        app_info = g_app_info_get_default_for_type ("text/calendar", FALSE);
        if (!app_info) {
                /* Try launching evolution directly if no calendar app is set */
                app_info = g_app_info_create_from_commandline ("evolution -c calendar",
                                                               "Evolution Calendar",
                                                               G_APP_INFO_CREATE_NONE,
                                                               &error);
        }

        if (app_info) {
                if (!g_app_info_launch (app_info, NULL, NULL, &error)) {
                        g_warning ("Failed to launch calendar application: %s", error->message);
                        g_error_free (error);
                }
                g_object_unref (app_info);
        } else {
                g_warning ("No calendar application found");
                if (error) {
                        g_warning ("Error: %s", error->message);
                        g_error_free (error);
                }
        }
}

static gboolean
appointment_tooltip_query_cb (GtkWidget   *widget,
                             gint         x,
                             gint         y,
                             gboolean     keyboard_mode,
                             GtkTooltip  *tooltip,
                             gpointer     user_data)
{
        GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
        GtkTreeModel *model;
        GtkTreePath *path;
        GtkTreeIter iter;
        gchar *summary, *description, *start_text;
        gchar *tooltip_text, *end_text;
        gboolean all_day;
        gulong start_time, end_time;

        if (!gtk_tree_view_get_tooltip_context (tree_view, &x, &y, keyboard_mode,
                                                &model, &path, &iter)) {
                return FALSE;
        }

        gtk_tree_model_get (model, &iter,
                            APPOINTMENT_COLUMN_SUMMARY, &summary,
                            APPOINTMENT_COLUMN_DESCRIPTION, &description,
                            APPOINTMENT_COLUMN_START_TEXT, &start_text,
                            APPOINTMENT_COLUMN_START_TIME, &start_time,
                            APPOINTMENT_COLUMN_END_TIME, &end_time,
                            APPOINTMENT_COLUMN_ALL_DAY, &all_day,
                            -1);

        if (!summary) {
                gtk_tree_path_free (path);
                return FALSE;
        }

        /* Format end time */
        if (!all_day && end_time > 0) {
                GDateTime *end_dt = g_date_time_new_from_unix_utc (end_time);
                if (end_dt) {
                        end_text = g_date_time_format (end_dt, "%H:%M");
                        g_date_time_unref (end_dt);
                } else {
                        end_text = NULL;
                }
        } else {
                end_text = NULL;
        }

        if (description && strlen (description) > 0) {
                if (all_day) {
                        tooltip_text = g_markup_printf_escaped ("<b>%s</b>\n%s\nAll Day", summary, description);
                } else if (end_text) {
                        tooltip_text = g_markup_printf_escaped ("<b>%s</b>\n%s\n%s - %s", summary, description, start_text ? start_text : "", end_text);
                } else {
                        tooltip_text = g_markup_printf_escaped ("<b>%s</b>\n%s\n%s", summary, description, start_text ? start_text : "");
                }
        } else {
                if (all_day) {
                        tooltip_text = g_markup_printf_escaped ("<b>%s</b>\nAll Day", summary);
                } else if (end_text) {
                        tooltip_text = g_markup_printf_escaped ("<b>%s</b>\n%s - %s", summary, start_text ? start_text : "", end_text);
                } else {
                        tooltip_text = g_markup_printf_escaped ("<b>%s</b>\n%s", summary, start_text ? start_text : "");
                }
        }

        gtk_tooltip_set_markup (tooltip, tooltip_text);
        gtk_tree_view_set_tooltip_row (tree_view, tooltip, path);

        g_free (summary);
        g_free (description);
        g_free (start_text);
        g_free (end_text);
        g_free (tooltip_text);
        gtk_tree_path_free (path);

        return TRUE;
}

static gboolean
task_tooltip_query_cb (GtkWidget   *widget,
                       gint         x,
                       gint         y,
                       gboolean     keyboard_mode,
                       GtkTooltip  *tooltip,
                       gpointer     user_data)
{
        GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
        GtkTreeModel *model;
        GtkTreePath *path;
        GtkTreeIter iter;
        gchar *summary, *description, *start_text, *due_text;
        gchar *tooltip_text;
        gint percent_complete, priority;

        if (!gtk_tree_view_get_tooltip_context (tree_view, &x, &y, keyboard_mode,
                                                &model, &path, &iter)) {
                return FALSE;
        }

        gtk_tree_model_get (model, &iter,
                            TASK_COLUMN_SUMMARY, &summary,
                            TASK_COLUMN_DESCRIPTION, &description,
                            TASK_COLUMN_START_TEXT, &start_text,
                            TASK_COLUMN_DUE_TEXT, &due_text,
                            TASK_COLUMN_PERCENT_COMPLETE, &percent_complete,
                            TASK_COLUMN_PRIORITY, &priority,
                            -1);

        if (!summary) {
                gtk_tree_path_free (path);
                return FALSE;
        }

        /* Build tooltip with task information */
        if (description && strlen (description) > 0) {
                tooltip_text = g_markup_printf_escaped ("<b>%s</b>\n%s\nProgress: %d%%",
                                                       summary, description, percent_complete);
        } else {
                tooltip_text = g_markup_printf_escaped ("<b>%s</b>\nProgress: %d%%",
                                                       summary, percent_complete);
        }

        /* Add due date if available */
        if (due_text && strlen (due_text) > 0) {
                gchar *temp = tooltip_text;
                tooltip_text = g_markup_printf_escaped ("%s\nDue: %s", temp, due_text);
                g_free (temp);
        }

        gtk_tooltip_set_markup (tooltip, tooltip_text);
        gtk_tree_view_set_tooltip_row (tree_view, tooltip, path);

        g_free (summary);
        g_free (description);
        g_free (start_text);
        g_free (due_text);
        g_free (tooltip_text);
        gtk_tree_path_free (path);

        return TRUE;
}

static void
task_row_activated_cb (GtkTreeView       *tree_view,
                      GtkTreePath       *path,
                      GtkTreeViewColumn *column,
                      gpointer           user_data)
{
        GAppInfo *app_info;
        GError *error = NULL;

        /* Launch Evolution tasks */
        app_info = g_app_info_get_default_for_uri_scheme ("task");
        if (!app_info) {
                /* Fallback to Evolution tasks directly */
                app_info = g_app_info_create_from_commandline ("evolution --component=tasks",
                                                               "Evolution Tasks",
                                                               G_APP_INFO_CREATE_NONE,
                                                               &error);
        }

        if (app_info) {
                g_app_info_launch (app_info, NULL, NULL, &error);
                g_object_unref (app_info);
        }

        if (error) {
                g_warning ("Failed to launch Evolution tasks: %s", error->message);
                g_error_free (error);
        }
}

static void
task_completion_toggled_cb (GtkCellRendererToggle *cell,
                           gchar                 *path_str,
                           CalendarWindow        *calwin)
{
        GtkTreePath *path;
        GtkTreeIter iter;
        gchar *task_uid;
        gboolean completed;
        gint percent_complete;

        path = gtk_tree_path_new_from_string (path_str);

        if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (calwin->priv->tasks_filter), &iter, path)) {
                gtk_tree_path_free (path);
                return;
        }

        gtk_tree_model_get (GTK_TREE_MODEL (calwin->priv->tasks_filter), &iter,
                            TASK_COLUMN_UID, &task_uid,
                            TASK_COLUMN_COMPLETED, &completed,
                            TASK_COLUMN_PERCENT_COMPLETE, &percent_complete,
                            -1);

        /* Toggle completion state */
        completed = !completed;
        percent_complete = completed ? 100 : 0;

        /* Update the Evolution task */
        if (calwin->priv->client && task_uid) {
                calendar_client_set_task_completed (calwin->priv->client,
                                                    task_uid,
                                                    completed,
                                                    percent_complete);
        }

        g_free (task_uid);
        gtk_tree_path_free (path);
}

static gboolean
task_entry_key_press_cb (GtkWidget      *widget,
                        GdkEventKey    *event,
                        CalendarWindow *calwin)
{
        const gchar *text;

        if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
                /* Get the text from the entry */
                text = gtk_entry_get_text (GTK_ENTRY (widget));

                /* Create task if text is not empty */
                if (text && *text != '\0') {
                        if (calwin->priv->client) {
                                gboolean success = calendar_client_create_task (calwin->priv->client, text);
                                if (success) {
                                        /* Clear the entry and hide it */
                                        gtk_entry_set_text (GTK_ENTRY (widget), "");
                                        gtk_widget_hide (widget);
                                } else {
                                        g_warning ("Failed to create task");
                                }
                        }
                }
                return TRUE; /* Event handled */
        } else if (event->keyval == GDK_KEY_Escape) {
                /* Clear the entry and hide it */
                gtk_entry_set_text (GTK_ENTRY (widget), "");
                gtk_widget_hide (widget);
                return TRUE; /* Event handled */
        }

        return FALSE; /* Let other handlers process the event */
}

static void
task_entry_activate_cb (GtkEntry       *entry,
                       CalendarWindow *calwin)
{
        const gchar *text;

        /* Get the text from the entry */
        text = gtk_entry_get_text (entry);

        /* Create task if text is not empty */
        if (text && *text != '\0') {
                if (calwin->priv->client) {
                        gboolean success = calendar_client_create_task (calwin->priv->client, text);
                        if (success) {
                                /* Clear the entry and hide it */
                                gtk_entry_set_text (entry, "");
                                gtk_widget_hide (GTK_WIDGET (entry));
                        } else {
                                g_warning ("Failed to create task");
                        }
                }
        }
}

void
calendar_window_set_client (CalendarWindow *calwin, CalendarClient *client)
{
	g_return_if_fail (CALENDAR_IS_WINDOW (calwin));
	calwin->priv->client = client;

	/* If we have a client, initialize the calendar data */
	if (client && calwin->priv->calendar) {
		guint year, month, day;
		gtk_calendar_get_date (GTK_CALENDAR (calwin->priv->calendar), &year, &month, &day);

		calendar_client_select_month (calwin->priv->client, month, year);
		calendar_client_select_day (calwin->priv->client, day);

		/* Trigger initial data load if widgets exist */
		if (calwin->priv->appointment_list) {
			handle_appointments_changed (calwin);
		}
		if (calwin->priv->task_list) {
			handle_tasks_changed (calwin);
		}
	}
}

#endif /* HAVE_EDS */
