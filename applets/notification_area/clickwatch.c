/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* clickwatch.c
 * Copyright (C) 2026 kosmik2001
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* notification-area-clickwatch: a separate process that sees clicks.

   Why a separate process rather than a thread or code inside the applet:
   XRecord works perfectly well standalone (measured: every press, including
   the wheel), but keeping it inside the GTK process breaks the panel. GDK
   drains the shared socket first, so XRecordProcessReplies() is left with
   nothing, and trying to reconcile the two in one process hung the applet.
   Splitting them removes the conflict by construction: this process has no
   GDK, no GTK, and no Xlib connection in common with the applet.

   Protocol: one line per event on stdout, flushed immediately:
       PRESS <button> <root_x> <root_y>
       RELEASE <button> <root_x> <root_y>
   The parent (the applet) reads the lines and decides whether to dismiss the
   popup. Nothing is grabbed: RECORD only duplicates events, so each click
   still reaches the window it was aimed at.

   Exit: SIGTERM/SIGINT, or stdout closing (the parent went away).
*/
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static volatile sig_atomic_t stop = 0;
static int last_err = -1;

static void
on_signal (int sig)
{
  (void) sig;
  stop = 1;
}

static int
trap (Display *d, XErrorEvent *e)
{
  last_err = e->error_code;
  (void) d;
  return 0;
}

static void
intercept (XPointer closure, XRecordInterceptData *data)
{
  (void) closure;

  if (data->category == XRecordFromServer && data->data != NULL)
    {
      xEvent *ev = (xEvent *) data->data;

      if (ev->u.u.type == ButtonPress || ev->u.u.type == ButtonRelease)
        {
          printf ("%s %d %d %d\n",
                  ev->u.u.type == ButtonPress ? "PRESS" : "RELEASE",
                  ev->u.u.detail,
                  ev->u.keyButtonPointer.rootX,
                  ev->u.keyButtonPointer.rootY);
          fflush (stdout);
        }
    }

  XRecordFreeData (data);
}

int main (void)
{
  Display *dpy;
  XRecordRange *range;
  XRecordClientSpec clients = XRecordAllClients;
  XRecordContext ctx;
  int op = 0, ev = 0, er = 0, maj = 0, min = 0;
  int fd;
  struct sigaction sa;

  setvbuf (stdout, NULL, _IOFBF, 0);

  memset (&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGINT, &sa, NULL);
  /* SIGPIPE: the parent closed the pipe, which is a normal exit, not a fault */
  signal (SIGPIPE, SIG_IGN);

  XSetErrorHandler (trap);

  dpy = XOpenDisplay (NULL);
  if (dpy == NULL)
    {
      fprintf (stderr, "clickwatch: cannot open display\n");
      return 2;
    }

  if (!XQueryExtension (dpy, "RECORD", &op, &ev, &er) ||
      !XRecordQueryVersion (dpy, &maj, &min))
    {
      fprintf (stderr, "clickwatch: RECORD extension unavailable\n");
      return 3;
    }

  range = XRecordAllocRange ();
  if (range == NULL)
    {
      fprintf (stderr, "clickwatch: XRecordAllocRange failed\n");
      return 4;
    }
  range->device_events.first = ButtonPress;
  range->device_events.last  = ButtonRelease;

  ctx = XRecordCreateContext (dpy, 0, &clients, 1, &range, 1);
  XFree (range);
  if (ctx == 0)
    {
      fprintf (stderr, "clickwatch: XRecordCreateContext failed (error %d)\n", last_err);
      return 5;
    }

  if (!XRecordEnableContextAsync (dpy, ctx, intercept, NULL))
    {
      fprintf (stderr, "clickwatch: XRecordEnableContextAsync failed (error %d)\n", last_err);
      XRecordFreeContext (dpy, ctx);
      return 6;
    }

  fprintf (stderr, "clickwatch: recording, RECORD %d.%d\n", maj, min);
  fflush (stderr);

  fd = ConnectionNumber (dpy);
  while (!stop)
    {
      fd_set rfd;
      struct timeval tv = { 0, 200000 };

      FD_ZERO (&rfd);
      FD_SET (fd, &rfd);

      if (select (fd + 1, &rfd, NULL, NULL, &tv) > 0 && FD_ISSET (fd, &rfd))
        XRecordProcessReplies (dpy);

      /* stdout closed: the parent is gone */
      if (errno == 0 && write (STDOUT_FILENO, "", 0) < 0 && errno == EPIPE)
        break;
    }

  XRecordDisableContext (dpy, ctx);
  XRecordFreeContext (dpy, ctx);
  XSync (dpy, False);
  XCloseDisplay (dpy);
  return 0;
}
