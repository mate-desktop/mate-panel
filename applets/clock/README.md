# Clock Applet

The clock applet displays the time and date in the MATE panel. When clicked,
it opens a calendar window that can also show upcoming appointments and tasks
from calendar data sources.

## Calendar Data Sources

The applet supports two calendar backends, which can be active simultaneously.

### Evolution Data Server (EDS)

When built with `--enable-eds` and the `libecal-2.0` / `libedataserver-1.2`
libraries are present, the applet reads appointments and tasks directly from
Evolution Data Server. This gives access to any calendar or task list
configured in GNOME Online Accounts or Evolution.

The "Show calendar events" and "Show tasks" checkboxes in the applet
preferences control whether each type of data is displayed.

### vdir / vdirsyncer

When built with `--enable-libical` and `libical-glib >= 3.0` is present, the
applet can read calendars stored in **vdir format** — a directory of `.ics`
files as produced by [vdirsyncer](https://vdirsyncer.readthedocs.io/).

#### Auto-discovery

The applet automatically scans `$XDG_DATA_HOME/vdirsyncer/` (typically
`~/.local/share/vdirsyncer/`) for vdir collections. This matches the default
storage path used by vdirsyncer. No configuration is needed if vdirsyncer is
set up with its default paths.

#### Additional paths

Extra collection directories can be added via GSettings:

```
gsettings set org.mate.panel.applet.clock vdir-calendar-paths \
  "['/path/to/collection1', '/path/to/collection2']"
```

Each path should point directly to a directory containing `.ics` files
(a single vdir collection), not to a parent directory.

#### Setting up vdirsyncer

Install vdirsyncer and create `~/.vdirsyncer/config`:

```ini
[general]
status_path = "~/.vdirsyncer/status/"

[pair my_calendar]
a = "my_local"
b = "my_remote"
collections = ["from b"]
conflict_resolution = "b wins"

[storage my_local]
type = "filesystem"
path = "~/.local/share/vdirsyncer/my_calendar/"
fileext = ".ics"

[storage my_remote]
type = "caldav"
url = "https://your-caldav-server/path/"
username = "user@example.com"
password = "yourpassword"
```

Then run:

```
vdirsyncer discover my_calendar
vdirsyncer sync
```

The applet will pick up the synced events automatically on next start. Live
updates are detected via `GFileMonitor` — new or changed `.ics` files are
reflected in the calendar window without restarting the applet.

#### Recurring events

Recurring events (RRULE, RDATE, EXDATE) are fully expanded using libical-glib,
so repeating events appear correctly on each occurrence date.

#### Collection metadata

If a vdir collection directory contains a `displayname` file, its contents
are used as the backend name shown in event tooltips. A `color` file
(containing an `#RRGGBB` hex color) is used to color-code events from that
collection in the calendar window.
