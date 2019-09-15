/**
 * clock.c
 *
 * A GTK+ widget that implements a clock face
 *
 * (c) 2007, Peter Teichman
 * (c) 2005-2006, Davyd Madeley
 *
 * Authors:
 *   Davyd Madeley  <davyd@madeley.id.au>
 *   Peter Teichman <peter@novell.com>
 */

#include <gtk/gtk.h>
#include <math.h>
#include <time.h>

#include "clock.h"
#include "clock-face.h"
#include "clock-location.h"
#include "clock-utils.h"

static GHashTable *pixbuf_cache = NULL;

static void     clock_face_finalize             (GObject *);
static gboolean clock_face_draw                 (GtkWidget      *clock,
                                                 cairo_t        *cr);
static void     clock_face_get_preferred_width  (GtkWidget      *this,
                                                 gint           *minimal_width,
                                                 gint           *natural_width);
static void     clock_face_get_preferred_height (GtkWidget      *this,
                                                 gint           *minimal_height,
                                                 gint           *natural_height);
static void     clock_face_size_allocate        (GtkWidget      *clock,
                                                 GtkAllocation  *allocation);

static void update_time_and_face  (ClockFace      *this,
                                   gboolean        force_face_loading);
static void clock_face_load_face  (ClockFace      *this,
                                   gint width, gint height);

typedef struct _ClockFacePrivate ClockFacePrivate;

typedef enum {
    CLOCK_FACE_MORNING,
    CLOCK_FACE_DAY,
    CLOCK_FACE_EVENING,
    CLOCK_FACE_NIGHT,
    CLOCK_FACE_INVALID
} ClockFaceTimeOfDay;

struct _ClockFacePrivate
{
    struct tm time; /* the time on the clock face */
    int minute_offset; /* the offset of the minutes hand */

    ClockFaceSize size;
    ClockFaceTimeOfDay timeofday;
    ClockLocation *location;
    GdkPixbuf *face_pixbuf;
    GtkWidget *size_widget;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClockFace, clock_face, GTK_TYPE_WIDGET)

static void
clock_face_class_init (ClockFaceClass *class)
{
    GObjectClass *obj_class;
    GtkWidgetClass *widget_class;

    obj_class = G_OBJECT_CLASS (class);
    widget_class = GTK_WIDGET_CLASS (class);

    /* GtkWidget signals */
    widget_class->draw = clock_face_draw;
    widget_class->get_preferred_width  = clock_face_get_preferred_width;
    widget_class->get_preferred_height = clock_face_get_preferred_height;
    widget_class->size_allocate = clock_face_size_allocate;

    /* GObject signals */
    obj_class->finalize = clock_face_finalize;
}

static void
clock_face_init (ClockFace *this)
{
    ClockFacePrivate *priv = clock_face_get_instance_private (this);

    priv->size = CLOCK_FACE_SMALL;
    priv->timeofday = CLOCK_FACE_INVALID;
    priv->location = NULL;
    priv->size_widget = NULL;

    gtk_widget_set_has_window (GTK_WIDGET (this), FALSE);
}

static gboolean
clock_face_draw (GtkWidget *this, cairo_t *cr)
{
    ClockFacePrivate *priv;
    int width, height;
    double x, y;
    double radius;
    int hours, minutes, seconds;

    /* Hand lengths as a multiple of the clock radius */
    double hour_length, min_length, sec_length;

    priv = clock_face_get_instance_private (CLOCK_FACE(this));

    if (GTK_WIDGET_CLASS (clock_face_parent_class)->draw)
        GTK_WIDGET_CLASS (clock_face_parent_class)->draw (this, cr);

    if (priv->size == CLOCK_FACE_LARGE) {
            hour_length = 0.45;
            min_length = 0.6;
            sec_length = 0.65;
    } else {
            hour_length = 0.5;
            min_length = 0.7;
            sec_length = 0.8;   /* not drawn currently */
    }

    width = gtk_widget_get_allocated_width (this);
    height = gtk_widget_get_allocated_width (this);
    x = width / 2;
    y = height / 2;
    radius = MIN (width / 2, height / 2) - 5;

    /* clock back */
    if (priv->face_pixbuf) {
            cairo_save (cr);
            gdk_cairo_set_source_pixbuf (cr, priv->face_pixbuf, 0, 0);
            cairo_paint (cr);
            cairo_restore (cr);
    }

    /* clock hands */
    hours = priv->time.tm_hour;
    minutes = priv->time.tm_min + priv->minute_offset;
    seconds = priv->time.tm_sec;

    cairo_set_line_width (cr, 1);

    /* hour hand:
     * the hour hand is rotated 30 degrees (pi/6 r) per hour +
     * 1/2 a degree (pi/360 r) per minute
     */
    cairo_save (cr);
    cairo_move_to (cr, x, y);
    cairo_line_to (cr, x + radius * hour_length * sin (M_PI / 6 * hours +
                                                       M_PI / 360 * minutes),
                       y + radius * hour_length * -cos (M_PI / 6 * hours +
                                                        M_PI / 360 * minutes));
    cairo_stroke (cr);
    cairo_restore (cr);
    /* minute hand:
     * the minute hand is rotated 6 degrees (pi/30 r) per minute
     */
    cairo_move_to (cr, x, y);
    cairo_line_to (cr, x + radius * min_length * sin (M_PI / 30 * minutes),
                       y + radius * min_length * -cos (M_PI / 30 * minutes));
    cairo_stroke (cr);

    /* seconds hand:
     * operates identically to the minute hand
     */
    if (priv->size == CLOCK_FACE_LARGE) {
            cairo_save (cr);
            cairo_set_source_rgb (cr, 0.937, 0.161, 0.161); /* tango red */
            cairo_move_to (cr, x, y);
            cairo_line_to (cr, x + radius * sec_length * sin (M_PI / 30 * seconds),
                           y + radius * sec_length * -cos (M_PI / 30 * seconds));
            cairo_stroke (cr);
            cairo_restore (cr);
    }

    return FALSE;
}

static void
clock_face_redraw_canvas (ClockFace *this)
{
    gtk_widget_queue_draw (GTK_WIDGET (this));
}

static void
clock_face_get_preferred_width (GtkWidget *this,
                                gint      *minimal_width,
                                gint      *natural_width)
{
    ClockFacePrivate *priv = clock_face_get_instance_private (CLOCK_FACE(this));

    if (priv->size_widget != NULL) {
           int child_minimal_height;
           int child_natural_height;

            /* Tie our size to the height of the size_widget */
            gtk_widget_get_preferred_height (GTK_WIDGET (priv->size_widget),
                                             &child_minimal_height,
                                             &child_natural_height);

            /* Pad out our height by a little bit - this improves
               the balance */
            *minimal_width = child_minimal_height + child_minimal_height / 8;
            *natural_width = child_natural_height + child_natural_height / 8;
    } else if (priv->face_pixbuf != NULL) {
            /* Use the size of the current pixbuf */
            *minimal_width = *natural_width = gdk_pixbuf_get_width (GDK_PIXBUF (priv->face_pixbuf));
    } else {
            /* we don't know anything, so use known dimensions for the svg
             * files */
            if (priv->size == CLOCK_FACE_LARGE)
                    *minimal_width = *natural_width = 50;
            else
                    *minimal_width = *natural_width = 36;
    }
}

static void
clock_face_get_preferred_height (GtkWidget *this,
                                 gint      *minimal_height,
                                 gint      *natural_height)
{
    ClockFacePrivate *priv = clock_face_get_instance_private (CLOCK_FACE(this));

    if (priv->size_widget != NULL) {
           int child_minimal_height;
           int child_natural_height;

            /* Tie our size to the height of the size_widget */
            gtk_widget_get_preferred_height (GTK_WIDGET (priv->size_widget),
                                             &child_minimal_height,
                                             &child_natural_height);

            /* Pad out our height by a little bit - this improves
               the balance */
            *minimal_height = child_minimal_height + child_minimal_height / 8;
            *natural_height = child_natural_height + child_natural_height / 8;
    } else if (priv->face_pixbuf != NULL) {
            /* Use the size of the current pixbuf */
            *minimal_height = *natural_height = gdk_pixbuf_get_height (GDK_PIXBUF (priv->face_pixbuf));
    } else {
            /* we don't know anything, so use known dimensions for the svg
             * files */
            if (priv->size == CLOCK_FACE_LARGE)
                    *minimal_height = *natural_height = 50;
            else
                    *minimal_height = *natural_height = 36;
    }
}

static void
clock_face_size_allocate (GtkWidget     *this,
                          GtkAllocation *allocation)
{
    GtkAllocation this_allocation;
    GtkAllocation old_allocation;

    gtk_widget_get_allocation (this, &this_allocation);

    old_allocation.width = this_allocation.width;
    old_allocation.height = this_allocation.height;

    if (GTK_WIDGET_CLASS (clock_face_parent_class)->size_allocate)
            GTK_WIDGET_CLASS (clock_face_parent_class)->size_allocate (this, allocation);

    if (old_allocation.width  == allocation->width &&
        old_allocation.height == allocation->height)
            return;

    /* Reload the face for the new size */
    update_time_and_face (CLOCK_FACE (this), TRUE);
}

static void
update_time_and_face (ClockFace *this,
                      gboolean   force_face_loading)
{
    ClockFacePrivate *priv;
    ClockFaceTimeOfDay timeofday;

    priv = clock_face_get_instance_private (this);

    /* update the time */
    if (priv->location) {
            clock_location_localtime (priv->location, &priv->time);
    } else {
            time_t timet;
            time (&timet);
            localtime_r (&timet, &priv->time);
    }

    /* FIXME  this should be a pref in gsetting
     * Or we could use some code from clock-sun.c?
     * currently we hardcode
     * morning 7-9
     * day 9-17
     * evening 17-22
     * night 22-7
     */
    if (priv->time.tm_hour < 7)
        timeofday = CLOCK_FACE_NIGHT;
    else if (priv->time.tm_hour < 9)
        timeofday = CLOCK_FACE_MORNING;
    else if (priv->time.tm_hour < 17)
        timeofday = CLOCK_FACE_DAY;
    else if (priv->time.tm_hour < 22)
        timeofday = CLOCK_FACE_EVENING;
    else
        timeofday = CLOCK_FACE_NIGHT;

    if (priv->timeofday != timeofday || force_face_loading) {
            GtkAllocation allocation;
            gint width, height;

            priv->timeofday = timeofday;

            gtk_widget_get_allocation (GTK_WIDGET (this), &allocation);

            width = allocation.width;
            height = allocation.height;

            /* Only load the pixbuf if we have some space allocated.
             * Note that 1x1 is not really some space... */
            if (width > 1 && height > 1)
                    clock_face_load_face (this, width, height);
    }
}

gboolean
clock_face_refresh (ClockFace *this)
{
    update_time_and_face (this, FALSE);
    clock_face_redraw_canvas (this);

    return TRUE; /* keep running this event */
}

GtkWidget *
clock_face_new (ClockFaceSize size)
{
    GObject *obj = g_object_new (INTL_TYPE_CLOCK_FACE, NULL);
    ClockFacePrivate *priv = clock_face_get_instance_private (CLOCK_FACE(obj));

    priv->size = size;

    return GTK_WIDGET (obj);
}

GtkWidget *
clock_face_new_with_location (ClockFaceSize size,
                              ClockLocation *loc,
                              GtkWidget *size_widget)
{
    GObject *obj = g_object_new (INTL_TYPE_CLOCK_FACE, NULL);
    ClockFacePrivate *priv = clock_face_get_instance_private (CLOCK_FACE(obj));

    priv->size = size;
    priv->location = g_object_ref (loc);
    priv->size_widget = g_object_ref (size_widget);

    return GTK_WIDGET (obj);
}

static void
clock_face_finalize (GObject *obj)
{
    ClockFacePrivate *priv = clock_face_get_instance_private (CLOCK_FACE(obj));

    if (priv->location) {
            g_object_unref (priv->location);
            priv->location = NULL;
    }

    if (priv->face_pixbuf) {
            g_object_unref (priv->face_pixbuf);
            priv->face_pixbuf = NULL;
    }

    if (priv->size_widget) {
            g_object_unref (priv->size_widget);
            priv->size_widget = NULL;
    }

    G_OBJECT_CLASS (clock_face_parent_class)->finalize (obj);

    if (pixbuf_cache && g_hash_table_size (pixbuf_cache) == 0) {
            g_hash_table_destroy (pixbuf_cache);
            pixbuf_cache = NULL;
    }
}

/* The pixbuf is being disposed, so remove it from the cache */
static void
remove_pixbuf_from_cache (const char *key,
                          GObject    *pixbuf)
{
    g_hash_table_remove (pixbuf_cache, key);
}

static void
clock_face_load_face (ClockFace *this, gint width, gint height)
{
    ClockFacePrivate *priv = clock_face_get_instance_private (this);
    const gchar *size_string[2] = { "small", "large" };
    const gchar *daytime_string[4] = { "morning", "day", "evening", "night" };
    gchar *cache_name;
    gchar *name;

    if (!pixbuf_cache)
            pixbuf_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);

    if (priv->face_pixbuf != NULL) {
            /* This might empty the cache, but it's useless to destroy
             * it since this object is still alive and might add another
             * pixbuf in the cache later (eg, a few lines below) */
            g_object_unref (priv->face_pixbuf);
            priv->face_pixbuf = NULL;
    }

    /* Look for the pixbuf in the process-wide cache first */
    cache_name = g_strdup_printf ("%d-%d-%d-%d",
                                  priv->size, priv->timeofday,
                                  width, height);

    priv->face_pixbuf = g_hash_table_lookup (pixbuf_cache, cache_name);
    if (priv->face_pixbuf) {
            g_object_ref (priv->face_pixbuf);
            g_free (cache_name);
            return;
    }

    /* The pixbuf is not cached, let's load it */
    name = g_strconcat (CLOCK_RESOURCE_PATH "icons/",
                        "clock-face-", size_string[priv->size],
                        "-", daytime_string[priv->timeofday], ".svg",
                        NULL);
    priv->face_pixbuf = clock_utils_pixbuf_from_svg_resource_at_size (name,
                                                                      width, height);
    g_free (name);

    if (!priv->face_pixbuf) {
            name = g_strconcat (CLOCK_RESOURCE_PATH "icons/",
                                "clock-face-", size_string[priv->size], ".svg",
                                NULL);
            priv->face_pixbuf = clock_utils_pixbuf_from_svg_resource_at_size (name,
                                                                              width, height);
            g_free (name);
    }

    /* Save the found pixbuf in the cache */
    if (priv->face_pixbuf) {
            g_hash_table_replace (pixbuf_cache,
                                  cache_name, priv->face_pixbuf);
            /* This will handle automatic removal from the cache when
             * the pixbuf isn't needed anymore */
            g_object_weak_ref (G_OBJECT (priv->face_pixbuf),
                               (GWeakNotify) remove_pixbuf_from_cache,
                               cache_name);
    } else
            g_free (cache_name);
}
