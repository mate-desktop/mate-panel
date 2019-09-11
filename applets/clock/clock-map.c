#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <cairo.h>
#include <sys/time.h>
#include <gtk/gtk.h>
#include <math.h>

#include "clock.h"
#include "clock-map.h"
#include "clock-sunpos.h"
#include "clock-marshallers.h"

enum {
	NEED_LOCATIONS,
	LAST_SIGNAL
};

enum {
        MARKER_NORMAL = 0,
        MARKER_HILIGHT,
        MARKER_CURRENT,
        MARKER_NB
};

static char *marker_files[MARKER_NB] = {
        "clock-map-location-marker.png",
        "clock-map-location-hilight.png",
        "clock-map-location-current.png"
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
        time_t last_refresh;

        gint width;
        gint height;

	guint highlight_timeout_id;

        GdkPixbuf *stock_map_pixbuf;
        GdkPixbuf *location_marker_pixbuf[MARKER_NB];

        GdkPixbuf *location_map_pixbuf;

        /* The shadow itself */
        GdkPixbuf *shadow_pixbuf;

        /* The map with the shadow composited onto it */
        GdkPixbuf *shadow_map_pixbuf;
} ClockMapPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClockMap, clock_map, GTK_TYPE_WIDGET)

static void clock_map_finalize (GObject *);
static void clock_map_size_allocate (GtkWidget *this,
					 GtkAllocation *allocation);
static gboolean clock_map_draw (GtkWidget *this,
				      cairo_t *cr);
static void clock_map_get_preferred_width (GtkWidget *widget,
					   gint *minimum_width,
					   gint *natural_width);
static void clock_map_get_preferred_height (GtkWidget *widget,
					    gint *minimum_height,
					    gint *natural_height);
static void clock_map_place_locations (ClockMap *this);
static void clock_map_render_shadow (ClockMap *this);
static void clock_map_display (ClockMap *this);

ClockMap *
clock_map_new (void)
{
        ClockMap *this;

        this = g_object_new (CLOCK_MAP_TYPE, NULL);

        return this;
}

static void
clock_map_class_init (ClockMapClass *this_class)
{
        GObjectClass *g_obj_class = G_OBJECT_CLASS (this_class);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (g_obj_class);

        g_obj_class->finalize = clock_map_finalize;

        /* GtkWidget signals */

        widget_class->size_allocate = clock_map_size_allocate;
        widget_class->draw = clock_map_draw;
	widget_class->get_preferred_width = clock_map_get_preferred_width;
	widget_class->get_preferred_height = clock_map_get_preferred_height;

	/**
	 * ClockMap::need-locations
	 *
	 * The map widget emits this signal when it needs to know which
	 * locations to display.
	 *
	 * Returns: the handler should return a (GList *) of (ClockLocation *).
	 * The map widget will not modify this list, so the caller should keep
	 * it alive.
	 */
	signals[NEED_LOCATIONS] = g_signal_new ("need-locations",
						G_TYPE_FROM_CLASS (g_obj_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (ClockMapClass, need_locations),
						NULL,
						NULL,
						_clock_marshal_POINTER__VOID,
						G_TYPE_POINTER, 0);
}

static void
clock_map_init (ClockMap *this)
{
        int i;
        ClockMapPrivate *priv = clock_map_get_instance_private (this);

        gtk_widget_set_has_window (GTK_WIDGET (this), FALSE);

	priv->last_refresh = 0;
	priv->width = 0;
	priv->height = 0;
	priv->highlight_timeout_id = 0;
        priv->stock_map_pixbuf = NULL;

        g_assert (sizeof (marker_files)/sizeof (char *) == MARKER_NB);

        for (i = 0; i < MARKER_NB; i++) {
                char *resource;

                resource = g_strconcat (CLOCK_RESOURCE_PATH "icons/", marker_files[i], NULL);
                priv->location_marker_pixbuf[i] = gdk_pixbuf_new_from_resource (resource, NULL);
                g_free (resource);
        }
}

static void
clock_map_finalize (GObject *g_obj)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (CLOCK_MAP(g_obj));
	int i;

	if (priv->highlight_timeout_id) {
		g_source_remove (priv->highlight_timeout_id);
		priv->highlight_timeout_id = 0;
	}

        if (priv->stock_map_pixbuf) {
                g_object_unref (priv->stock_map_pixbuf);
                priv->stock_map_pixbuf = NULL;
        }

	for (i = 0; i < MARKER_NB; i++) {
        	if (priv->location_marker_pixbuf[i]) {
                	g_object_unref (priv->location_marker_pixbuf[i]);
                	priv->location_marker_pixbuf[i] = NULL;
        	}
	}

        if (priv->location_map_pixbuf) {
                g_object_unref (priv->location_map_pixbuf);
                priv->location_map_pixbuf = NULL;
        }

        if (priv->shadow_pixbuf) {
                g_object_unref (priv->shadow_pixbuf);
                priv->shadow_pixbuf = NULL;
        }

        if (priv->shadow_map_pixbuf) {
                g_object_unref (priv->shadow_map_pixbuf);
                priv->shadow_map_pixbuf = NULL;
        }

        G_OBJECT_CLASS (clock_map_parent_class)->finalize (g_obj);
}

void
clock_map_refresh (ClockMap *this)
{
    ClockMapPrivate *priv = clock_map_get_instance_private (this);
	GtkWidget *widget = GTK_WIDGET (this);
	GtkAllocation allocation;

	gtk_widget_get_allocation (widget, &allocation);

        /* Only do something if we have some space allocated.
         * Note that 1x1 is not really some space... */
        if (allocation.width <= 1 || allocation.height <= 1)
                return;

        /* Allocation changed => we reload the map */
	if (priv->width != allocation.width ||
	    priv->height != allocation.height) {
                if (priv->stock_map_pixbuf) {
                        g_object_unref (priv->stock_map_pixbuf);
                        priv->stock_map_pixbuf = NULL;
                }

                priv->width = allocation.width;
                priv->height = allocation.height;
        }

        if (!priv->stock_map_pixbuf) {
                priv->stock_map_pixbuf = gdk_pixbuf_new_from_resource_at_scale (CLOCK_RESOURCE_PATH "icons/clock-map.png",
                                                                                priv->width, priv->height,
                                                                                FALSE,
                                                                                NULL);
        }

        clock_map_place_locations (this);

        clock_map_display (this);
}

static gboolean
clock_map_draw (GtkWidget *this, cairo_t *cr)
{
    ClockMapPrivate *priv = clock_map_get_instance_private (CLOCK_MAP(this));
	int width, height;
	GtkStyleContext *context;
	GdkRGBA color;

	context = gtk_widget_get_style_context (this);
	gtk_style_context_get_color (context, GTK_STATE_FLAG_ACTIVE, &color);

	if (!priv->shadow_map_pixbuf) {
                g_warning ("Needed to refresh the map in expose event.");
		clock_map_refresh (CLOCK_MAP (this));
        }

	width = gdk_pixbuf_get_width (priv->shadow_map_pixbuf);
	height = gdk_pixbuf_get_height (priv->shadow_map_pixbuf);

	gdk_cairo_set_source_pixbuf (cr, priv->shadow_map_pixbuf, 0, 0);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_paint (cr);

        /* draw a simple outline */
	cairo_rectangle (cr, 0.5, 0.5, width - 1, height - 1);
	gdk_cairo_set_source_rgba (cr, &color);

        cairo_set_line_width (cr, 1.0);
        cairo_stroke (cr);

	return FALSE;
}

static void
clock_map_get_preferred_width (GtkWidget *this,
                                gint *minimum_width,
                                gint *natural_width)
{
        *minimum_width = *natural_width = 250;
}

static void
clock_map_get_preferred_height (GtkWidget *this,
                                 gint *minimum_height,
                                 gint *natural_height)
{
        *minimum_height = *natural_height = 125;
}

static void
clock_map_size_allocate (GtkWidget *this, GtkAllocation *allocation)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (CLOCK_MAP(this));

	if (GTK_WIDGET_CLASS (clock_map_parent_class)->size_allocate)
		GTK_WIDGET_CLASS (clock_map_parent_class)->size_allocate (this, allocation);

	if (priv->width != allocation->width ||
	    priv->height != allocation->height)
                clock_map_refresh (CLOCK_MAP (this));
}

static void
clock_map_mark (ClockMap *this, gfloat latitude, gfloat longitude, gint mark)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (this);
        GdkPixbuf *marker = priv->location_marker_pixbuf[mark];
        GdkPixbuf *partial = NULL;

        int x, y;
        int width, height;
        int marker_width, marker_height;
        int dest_x, dest_y, dest_width, dest_height;

        width = gdk_pixbuf_get_width (priv->location_map_pixbuf);
        height = gdk_pixbuf_get_height (priv->location_map_pixbuf);

        x = (width / 2.0 + (width / 2.0) * longitude / 180.0);
        y = (height / 2.0 - (height / 2.0) * latitude / 90.0);

        marker_width = gdk_pixbuf_get_width (marker);
        marker_height = gdk_pixbuf_get_height (marker);

        dest_x = x - marker_width / 2;
        dest_y = y - marker_height / 2;
        dest_width = marker_width;
        dest_height = marker_height;

        /* create a small partial pixbuf if the mark is too close to
           the north or south pole */
        if (dest_y < 0) {
                partial = gdk_pixbuf_new_subpixbuf
                        (marker, 0, dest_y + marker_height,
                         marker_width, -dest_y);

                dest_y = 0.0;
                marker_height = gdk_pixbuf_get_height (partial);
        } else if (dest_y + dest_height > height) {
                partial = gdk_pixbuf_new_subpixbuf
                        (marker, 0, 0, marker_width, height - dest_y);
                marker_height = gdk_pixbuf_get_height (partial);
        }

        if (partial) {
                marker = partial;
        }

        /* handle the cases where the marker needs to be placed across
           the 180 degree longitude line */
        if (dest_x < 0) {
                /* split into our two pixbufs for the left and right edges */
                GdkPixbuf *lhs = NULL;
                GdkPixbuf *rhs = NULL;

                lhs = gdk_pixbuf_new_subpixbuf
                        (marker, -dest_x, 0, marker_width + dest_x, marker_height);

                gdk_pixbuf_composite (lhs, priv->location_map_pixbuf,
                                      0, dest_y,
                                      gdk_pixbuf_get_width (lhs),
                                      gdk_pixbuf_get_height (lhs),
                                      0, dest_y,
                                      1.0, 1.0, GDK_INTERP_NEAREST, 0xFF);

                rhs = gdk_pixbuf_new_subpixbuf
                        (marker, 0, 0, -dest_x, marker_height);

                gdk_pixbuf_composite (rhs, priv->location_map_pixbuf,
                                      width - gdk_pixbuf_get_width (rhs) - 1,
                                      dest_y,
                                      gdk_pixbuf_get_width (rhs),
                                      gdk_pixbuf_get_height (rhs),
                                      width - gdk_pixbuf_get_width (rhs) - 1,
                                      dest_y,
                                      1.0, 1.0, GDK_INTERP_NEAREST, 0xFF);

                g_object_unref (lhs);
                g_object_unref (rhs);
        } else if (dest_x + dest_width > width) {
                /* split into our two pixbufs for the left and right edges */
                GdkPixbuf *lhs = NULL;
                GdkPixbuf *rhs = NULL;

                lhs = gdk_pixbuf_new_subpixbuf
                        (marker, width - dest_x, 0, marker_width - width + dest_x, marker_height);

                gdk_pixbuf_composite (lhs, priv->location_map_pixbuf,
                                      0, dest_y,
                                      gdk_pixbuf_get_width (lhs),
                                      gdk_pixbuf_get_height (lhs),
                                      0, dest_y,
                                      1.0, 1.0, GDK_INTERP_NEAREST, 0xFF);

                rhs = gdk_pixbuf_new_subpixbuf
                        (marker, 0, 0, width - dest_x, marker_height);

                gdk_pixbuf_composite (rhs, priv->location_map_pixbuf,
                                      width - gdk_pixbuf_get_width (rhs) - 1,
                                      dest_y,
                                      gdk_pixbuf_get_width (rhs),
                                      gdk_pixbuf_get_height (rhs),
                                      width - gdk_pixbuf_get_width (rhs) - 1,
                                      dest_y,
                                      1.0, 1.0, GDK_INTERP_NEAREST, 0xFF);

                g_object_unref (lhs);
                g_object_unref (rhs);
        } else {
                gdk_pixbuf_composite (marker, priv->location_map_pixbuf,
                                      dest_x, dest_y,
                                      gdk_pixbuf_get_width (marker),
                                      gdk_pixbuf_get_height (marker),
                                      dest_x, dest_y,
                                      1.0, 1.0, GDK_INTERP_NEAREST, 0xFF);
        }

        if (partial != NULL) {
                g_object_unref (partial);
        }
}

/**
 * Return value: %TRUE if @loc can be placed on the map, %FALSE otherwise.
 **/
static gboolean
clock_map_place_location (ClockMap *this, ClockLocation *loc, gboolean hilight)
{
        gfloat latitude, longitude;
	gint marker;

        clock_location_get_coords (loc, &latitude, &longitude);
        /* 0/0 means unset, basically */
        if (latitude == 0 && longitude == 0)
                return FALSE;

	if (hilight)
		marker = MARKER_HILIGHT;
	else if (clock_location_is_current (loc))
		marker = MARKER_CURRENT;
	else
		marker = MARKER_NORMAL;

        clock_map_mark (this, latitude, longitude, marker);

        return TRUE;
}

static void
clock_map_place_locations (ClockMap *this)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (this);
        GList *locs;
        ClockLocation *loc;

        if (priv->location_map_pixbuf) {
                g_object_unref (priv->location_map_pixbuf);
                priv->location_map_pixbuf = NULL;
        }

        priv->location_map_pixbuf = gdk_pixbuf_copy (priv->stock_map_pixbuf);

	locs = NULL;
	g_signal_emit (this, signals[NEED_LOCATIONS], 0, &locs);

        while (locs) {
                loc = CLOCK_LOCATION (locs->data);

                clock_map_place_location (this, loc, FALSE);

                locs = locs->next;
        }

#if 0
        /* map_mark test suite for the edge cases */

        /* points around longitude 180 */
        clock_map_mark (this, 0.0, 180.0);
        clock_map_mark (this, -15.0, -178.0);
        clock_map_mark (this, -30.0, -176.0);
        clock_map_mark (this, 15.0, 178.0);
        clock_map_mark (this, 30.0, 176.0);

        clock_map_mark (this, 90.0, 180.0);
        clock_map_mark (this, -90.0, 180.0);

        /* north pole & friends */
        clock_map_mark (this, 90.0, 0.0);
        clock_map_mark (this, 88.0, -15.0);
        clock_map_mark (this, 92.0, 15.0);

        /* south pole & friends */
        clock_map_mark (this, -90.0, 0.0);
        clock_map_mark (this, -88.0, -15.0);
        clock_map_mark (this, -92.0, 15.0);
#endif
}

static void
clock_map_compute_vector (gdouble lat, gdouble lon, gdouble *vec)
{
        gdouble lat_rad, lon_rad;
        lat_rad = lat * (M_PI/180.0);
        lon_rad = lon * (M_PI/180.0);

        vec[0] = sin(lon_rad) * cos(lat_rad);
        vec[1] = sin(lat_rad);
        vec[2] = cos(lon_rad) * cos(lat_rad);
}

static guchar
clock_map_is_sunlit (gdouble pos_lat, gdouble pos_long,
                         gdouble sun_lat, gdouble sun_long)
{
        gdouble pos_vec[3];
        gdouble sun_vec[3];
        gdouble dot;

        /* twilight */
        gdouble epsilon = 0.01;

        clock_map_compute_vector (pos_lat, pos_long, pos_vec);
        clock_map_compute_vector (sun_lat, sun_long, sun_vec);

        /* compute the dot product of the two */
        dot = pos_vec[0]*sun_vec[0] + pos_vec[1]*sun_vec[1]
                + pos_vec[2]*sun_vec[2];

        if (dot > epsilon) {
                return 0x00;
        }

        if (dot < -epsilon) {
                return 0xFF;
        }

        return (guchar)(-128 * ((dot / epsilon) - 1));
}

static void
clock_map_render_shadow_pixbuf (GdkPixbuf *pixbuf)
{
        int x, y;
        int height, width;
        int n_channels, rowstride;
        guchar *pixels, *p;
        gdouble sun_lat, sun_lon;
        time_t now = time (NULL);

        n_channels = gdk_pixbuf_get_n_channels (pixbuf);
        rowstride = gdk_pixbuf_get_rowstride (pixbuf);
        pixels = gdk_pixbuf_get_pixels (pixbuf);

        width = gdk_pixbuf_get_width (pixbuf);
        height = gdk_pixbuf_get_height (pixbuf);

        sun_position (now, &sun_lat, &sun_lon);

        for (y = 0; y < height; y++) {
                gdouble lat = (height / 2.0 - y) / (height / 2.0) * 90.0;

                for (x = 0; x < width; x++) {
                        guchar shade;
                        gdouble lon =
                                (x - width / 2.0) / (width / 2.0) * 180.0;

                        shade = clock_map_is_sunlit (lat, lon,
                                                         sun_lat, sun_lon);

                        p = pixels + y * rowstride + x * n_channels;
                        p[3] = shade;
                }
        }
}

static void
clock_map_render_shadow (ClockMap *this)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (this);

        if (priv->shadow_pixbuf) {
                g_object_unref (priv->shadow_pixbuf);
        }

        priv->shadow_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
                                              priv->width, priv->height);

        /* Initialize to all shadow */
        gdk_pixbuf_fill (priv->shadow_pixbuf, 0x6d9ccdff);

        clock_map_render_shadow_pixbuf (priv->shadow_pixbuf);

        if (priv->shadow_map_pixbuf) {
                g_object_unref (priv->shadow_map_pixbuf);
        }

        priv->shadow_map_pixbuf = gdk_pixbuf_copy (priv->location_map_pixbuf);

        gdk_pixbuf_composite (priv->shadow_pixbuf, priv->shadow_map_pixbuf,
                              0, 0, priv->width, priv->height,
                              0, 0, 1, 1, GDK_INTERP_NEAREST, 0x66);
}

static void
clock_map_display (ClockMap *this)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (this);

        if (priv->width > 0 || priv->height > 0)
                clock_map_render_shadow (this);
	gtk_widget_queue_draw (GTK_WIDGET (this));

        time (&priv->last_refresh);
}

typedef struct {
       ClockMap *map;
       ClockLocation *location;
       int count;
} BlinkData;

static gboolean
highlight (gpointer user_data)
{
       BlinkData *data = user_data;

       if (data->count == 6)
               return FALSE;

       if (data->count % 2 == 0) {
                 if (!clock_map_place_location (data->map,
                                                data->location, TRUE))
                         return FALSE;
       } else
                 clock_map_place_locations (data->map);
       clock_map_display (data->map);

       data->count++;

       return TRUE;
}

static void
highlight_destroy (gpointer user_data)
{
       BlinkData *data = user_data;
       ClockMapPrivate *priv;

       priv = clock_map_get_instance_private (data->map);
       priv->highlight_timeout_id = 0;

       g_object_unref (data->location);
       g_free (data);
}

void
clock_map_blink_location (ClockMap *this, ClockLocation *loc)
{
       BlinkData *data;
       ClockMapPrivate *priv;

       priv = clock_map_get_instance_private (this);

       g_return_if_fail (IS_CLOCK_MAP (this));
       g_return_if_fail (IS_CLOCK_LOCATION (loc));

       data = g_new0 (BlinkData, 1);
       data->map = this;
       data->location = g_object_ref (loc);

       if (priv->highlight_timeout_id) {
	       g_source_remove (priv->highlight_timeout_id);
	       clock_map_place_locations (this);
       }

       highlight (data);

       priv->highlight_timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
							300, highlight, data,
							highlight_destroy);
}

static gboolean
clock_map_needs_refresh (ClockMap *this)
{
        ClockMapPrivate *priv = clock_map_get_instance_private (this);
        time_t now_t;

        time (&now_t);

	/* refresh once per minute */
	return (ABS (now_t - priv->last_refresh) >= 60);
}

void
clock_map_update_time (ClockMap *this)
{

	g_return_if_fail (IS_CLOCK_MAP (this));

        if (!clock_map_needs_refresh (this)) {
                return;
        }

        clock_map_display (this);
}
