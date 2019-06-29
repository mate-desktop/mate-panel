#include "panel-color.h"

/**
 * rgb_to_hls:
 * @r: on input, red; on output, hue
 * @g: on input, green; on output, lightness
 * @b: on input, blue; on output, saturation
 *
 * Converts a red/green/blue triplet to a hue/lightness/saturation triplet.
 */
static void
rgb_to_hls (gdouble *r, gdouble *g, gdouble *b)
{
	gdouble min;
	gdouble max;
	gdouble red;
	gdouble green;
	gdouble blue;
	gdouble h, l, s;
	gdouble delta;

	red = *r;
	green = *g;
	blue = *b;

	if (red > green) {
		if (red > blue)
			max = red;
		else
			max = blue;

		if (green < blue)
			min = green;
		else
			min = blue;
	} else {
		if (green > blue)
			max = green;
		else
			max = blue;

		if (red < blue)
			min = red;
		else
			min = blue;
	}

	l = (max + min) / 2;
	s = 0;
	h = 0;

	if (max != min) {
		if (l <= 0.5)
			s = (max - min) / (max + min);
		else
			s = (max - min) / (2 - max - min);

		delta = max -min;
		if (red == max)
			h = (green - blue) / delta;
		else if (green == max)
			h = 2 + (blue - red) / delta;
		else if (blue == max)
			h = 4 + (red - green) / delta;

		h *= 60;
		if (h < 0.0)
			h += 360;
	}

	*r = h;
	*g = l;
	*b = s;
}

/**
 * hls_to_rgb:
 * @h: on input, hue; on output, red
 * @l: on input, lightness; on output, green
 * @s: on input, saturation; on output, blue
 *
 * Converts a hue/lightness/saturation triplet to a red/green/blue triplet.
 */
static void
hls_to_rgb (gdouble *h, gdouble *l, gdouble *s)
{
	gdouble hue;
	gdouble lightness;
	gdouble saturation;
	gdouble m1, m2;
	gdouble r, g, b;

	lightness = *l;
	saturation = *s;

	if (lightness <= 0.5)
		m2 = lightness * (1 + saturation);
	else
		m2 = lightness + saturation - lightness * saturation;
	m1 = 2 * lightness - m2;

	if (saturation == 0) {
		*h = lightness;
		*l = lightness;
		*s = lightness;
	} else {
		hue = *h + 120;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			r = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			r = m2;
		else if (hue < 240)
			r = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			r = m1;

		hue = *h;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			g = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			g = m2;
		else if (hue < 240)
			g = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			g = m1;

		hue = *h - 120;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			b = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			b = m2;
		else if (hue < 240)
			b = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			b = m1;

		*h = r;
		*l = g;
		*s = b;
	}
}

/**
 * gtk_style_shade:
 * @a: the starting colour
 * @b: (out): the resulting colour
 * @k: amount to scale lightness and saturation by
 *
 * Takes a colour "a", scales the lightness and saturation by a certain amount,
 * and sets "b" to the resulting colour.
 * gtkstyle.c cut-and-pastage.
 */
void
gtk_style_shade (GdkRGBA *a, GdkRGBA *b, gdouble k)
{
	gdouble red;
	gdouble green;
	gdouble blue;

	red = a->red;
	green = a->green;
	blue = a->blue;

	rgb_to_hls (&red, &green, &blue);

	green *= k;
	if (green > 1.0)
		green = 1.0;
	else if (green < 0.0)
		green = 0.0;

  	blue *= k;
  	if (blue > 1.0)
  		blue = 1.0;
  	else if (blue < 0.0)
  		blue = 0.0;

	hls_to_rgb (&red, &green, &blue);

	b->red = red;
	b->green = green;
	b->blue = blue;
}
