/*
 * Copyright (C) 2007 Red Hat, Inc.
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
 *     Jonathan Blandford <jrb@redhat.com>
 *     Matthias Clasen <mclasen@redhat.com>
 */

#include <time.h>
#include <gtk/gtk.h>
#include <math.h>
#include "clock-sunpos.h"

/* Calculated with the methods and figures from "Practical Astronomy With Your
 * Calculator, version 3" by Peter Duffet-Smith.
 */
/* Table 6.  Details of the Sun's apparent orbit at epoch 1990.0 */

#define EPOCH          2447891.5  /* days */    /* epoch 1990 */
#define UNIX_EPOCH     2440586.5  /* days */    /* epoch 1970 */
#define EPSILON_G      279.403303 /* degrees */ /* ecliptic longitude at epoch 1990.0 */
#define MU_G           282.768422 /* degrees */ /* ecliptic longitude at perigee */
#define ECCENTRICITY   0.016713                 /* eccentricity of orbit */
#define R_0            149598500  /* km */      /* semi-major axis */
#define THETA_0        0.533128   /* degrees */ /* angular diameter at r = r_0 */
#define MEAN_OBLIQUITY 23.440592  /* degrees */ /* mean obliquity of earth's axis at epoch 1990.0 */

#define NORMALIZE(x) \
  while (x>360) x-=360; while (x<0) x+= 360;

#define DEG_TO_RADS(x) \
  (x * G_PI/180.0)

#define RADS_TO_DEG(x) \
  (x * 180.0/G_PI)

/* Calculate number of days since 4713BC.
 */
static gdouble
unix_time_to_julian_date (gint unix_time)
{
  return UNIX_EPOCH + (double) unix_time / (60 * 60 * 24);
}

/* Finds an iterative solution for [ E - e sin (E) = M ] for values of e less
   than 0.1.  Page 90  */

#define ERROR_ACCURACY 1e-6 /* radians */

static gdouble
solve_keplers_equation (gdouble e,
			gdouble M)
{
  gdouble d, E;

  /* start with an initial estimate */
  E = M;

  d = E - e * sin (E) - M;

  while (ABS (d) > ERROR_ACCURACY)
    {
      E = E - (d / (1 - e * cos (E)));
      d = E - e * sin (E) - M;
    }

  return E;
}

  /* convert the ecliptic longitude to right ascension and declination.  Section 27.  */
static void
ecliptic_to_equatorial (gdouble  lambda,
			gdouble  beta,
			gdouble *ra,
			gdouble *dec)
{
  gdouble cos_mo;
  gdouble sin_mo;

  g_assert (ra != NULL);
  g_assert (dec != NULL);

  sin_mo = sin (DEG_TO_RADS (MEAN_OBLIQUITY));
  cos_mo = cos (DEG_TO_RADS (MEAN_OBLIQUITY));

  *ra = atan2 (sin (lambda) * cos_mo - tan (beta) * sin_mo, cos (lambda));
  *dec = asin (sin (beta) * cos_mo + cos (beta) * sin_mo * sin (lambda));
}

/* calculate GST.  Section 12  */
static gdouble
greenwich_sidereal_time (gdouble unix_time)
{
  gdouble u, JD, T, T0, UT;

  u = fmod (unix_time, 24 * 60 * 60);
  JD = unix_time_to_julian_date (unix_time - u);
  T = (JD - 2451545) / 36525;
  T0 = 6.697374558 + (2400.051336 * T) + (0.000025862 * T * T);
  T0 = fmod (T0, 24);
  UT = u / (60 * 60);
  T0 = T0 + UT * 1.002737909;
  T0 = fmod (T0, 24);

  return T0;
}

/* Calculate the position of the sun at a given time.  pages 89-91 */
void
sun_position (time_t unix_time, gdouble *lat, gdouble *lon)
{
  gdouble jd, D, N, M, E, x, v, lambda;
  gdouble ra, dec;
  jd = unix_time_to_julian_date (unix_time);

  /* Calculate number of days since the epoch */
  D = jd - EPOCH;

  N = D*360/365.242191;

  /* normalize to 0 - 360 degrees */
  NORMALIZE (N);

  /* Step 4: */
  M = N + EPSILON_G - MU_G;
  NORMALIZE (M);

  /* Step 5: convert to radians */
  M = DEG_TO_RADS (M);

  /* Step 6: */
  E = solve_keplers_equation (ECCENTRICITY, M);

  /* Step 7: */
  x = sqrt ((1 + ECCENTRICITY)/(1 - ECCENTRICITY)) * tan (E/2);

  /* Step 8, 9 */
  v = 2 * RADS_TO_DEG (atan (x));
  NORMALIZE (v);

  /* Step 10 */
  lambda = v + MU_G;
  NORMALIZE (lambda);

  /* convert the ecliptic longitude to right ascension and declination */
  ecliptic_to_equatorial (DEG_TO_RADS (lambda), 0.0, &ra, &dec);

  ra = ra - (G_PI/12) * greenwich_sidereal_time (unix_time);
  ra = RADS_TO_DEG (ra);
  dec = RADS_TO_DEG (dec);
  NORMALIZE (ra);
  NORMALIZE (dec);

  *lat = dec;
  *lon = ra;
}


#if 0
int
main (int argc, char *argv[])
{
  gint i;
  gint now;
  GTimeVal timeval;
  gdouble lat, lon;

  gtk_init (&argc, &argv);

  g_get_current_time (&timeval);
  now = timeval.tv_sec;

  for (i = 0; i < now; i += 15 * 60)
    {
      sun_position (i, &lat, &lon);
      g_print ("%d: %f %f\n", lat, lon);
    }

  return 0;
}

#endif
