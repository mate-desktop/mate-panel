/*
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include "panel-monitor-lookup.h"

static const PanelMonitorEntry laptop_and_hdmi[] = {
	{ "Chimei-14", 0 },
	{ "Dell-U2412M", 1 },
};

static void
test_found_by_id (void)
{
	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 0, "Dell-U2412M", 0), ==, 1);
	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 0, "Chimei-14", 1), ==, 0);
}

static void
test_id_wins_over_index (void)
{
	static const PanelMonitorEntry swapped[] = {
		{ "Dell-U2412M", 0 },
		{ "Chimei-14", 1 },
	};

	g_assert_cmpint (panel_monitor_lookup (swapped, 2, 2, 1, "Dell-U2412M", 1), ==, 0);
}

static void
test_missing_id_gives_primary (void)
{
	static const PanelMonitorEntry laptop_only[] = {
		{ "Chimei-14", 0 },
	};
	static const PanelMonitorEntry three[] = {
		{ "A-1", 0 },
		{ "B-2", 1 },
		{ "C-3", 2 },
	};

	g_assert_cmpint (panel_monitor_lookup (laptop_only, 1, 1, 0, "Dell-U2412M", 1), ==, 0);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, 2, "Dell-U2412M", 1), ==, 2);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, -1, "Dell-U2412M", 1), ==, 0);
}

static void
test_index_when_id_empty (void)
{
	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 0, "", 1), ==, 1);
	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 0, NULL, 1), ==, 1);
	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 1, "", 5), ==, 1);
}

static void
test_index_counts_from_the_primary (void)
{
	static const PanelMonitorEntry three[] = {
		{ "A-1", 0 },
		{ "B-2", 1 },
		{ "C-3", 2 },
	};

	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 1, "", 0), ==, 1);
	g_assert_cmpint (panel_monitor_lookup (laptop_and_hdmi, 2, 2, 1, NULL, 1), ==, 0);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, 2, "", 0), ==, 2);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, 2, "", 1), ==, 0);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, 2, "", 2), ==, 1);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, 1, "", 2), ==, 2);
	g_assert_cmpint (panel_monitor_lookup (three, 3, 3, -1, "", 1), ==, 1);
}

static void
test_mirrors_share_a_monitor (void)
{
	static const PanelMonitorEntry mirrored[] = {
		{ "Chimei-14", 0 },
		{ "Dell-U2412M", 0 },
	};

	g_assert_cmpint (panel_monitor_lookup (mirrored, 2, 1, 0, "Dell-U2412M", 1), ==, 0);
	g_assert_cmpstr (panel_monitor_id_for_index (mirrored, 2, 0), ==, "Chimei-14");
}

static void
test_no_monitors (void)
{
	g_assert_cmpint (panel_monitor_lookup (NULL, 0, 0, 0, "Chimei-14", 0), ==, -1);
}

static void
test_id_to_save (void)
{
	static const PanelMonitorEntry unknown[] = {
		{ NULL, 0 },
		{ "", 1 },
	};

	g_assert_cmpstr (panel_monitor_id_for_index (laptop_and_hdmi, 2, 1), ==, "Dell-U2412M");
	g_assert_null (panel_monitor_id_for_index (laptop_and_hdmi, 2, 2));
	g_assert_null (panel_monitor_id_for_index (unknown, 2, 0));
	g_assert_null (panel_monitor_id_for_index (unknown, 2, 1));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/monitor-lookup/found-by-id", test_found_by_id);
	g_test_add_func ("/monitor-lookup/id-wins-over-index", test_id_wins_over_index);
	g_test_add_func ("/monitor-lookup/missing-id-gives-primary", test_missing_id_gives_primary);
	g_test_add_func ("/monitor-lookup/index-when-id-empty", test_index_when_id_empty);
	g_test_add_func ("/monitor-lookup/index-counts-from-the-primary", test_index_counts_from_the_primary);
	g_test_add_func ("/monitor-lookup/mirrors-share-a-monitor", test_mirrors_share_a_monitor);
	g_test_add_func ("/monitor-lookup/no-monitors", test_no_monitors);
	g_test_add_func ("/monitor-lookup/id-to-save", test_id_to_save);
	return g_test_run ();
}
