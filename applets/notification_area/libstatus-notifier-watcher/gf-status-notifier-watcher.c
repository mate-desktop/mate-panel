/*
 * Copyright (C) 2016 Alberts Muktupāvels
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gf-sn-watcher-v0.h"
#include "gf-status-notifier-watcher.h"

struct _GfStatusNotifierWatcher
{
  GObject        parent;

  GfSnWatcherV0 *v0;
};

G_DEFINE_TYPE (GfStatusNotifierWatcher, gf_status_notifier_watcher, G_TYPE_OBJECT)

static void
gf_status_notifier_watcher_dispose (GObject *object)
{
  GfStatusNotifierWatcher *watcher;

  watcher = GF_STATUS_NOTIFIER_WATCHER (object);

  g_clear_object (&watcher->v0);

  G_OBJECT_CLASS (gf_status_notifier_watcher_parent_class)->dispose (object);
}

static void
gf_status_notifier_watcher_class_init (GfStatusNotifierWatcherClass *watcher_class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (watcher_class);

  object_class->dispose = gf_status_notifier_watcher_dispose;
}

static void
gf_status_notifier_watcher_init (GfStatusNotifierWatcher *watcher)
{
  watcher->v0 = gf_sn_watcher_v0_new ();
}

GfStatusNotifierWatcher *
gf_status_notifier_watcher_new (void)
{
  return g_object_new (GF_TYPE_STATUS_NOTIFIER_WATCHER, NULL);
}
