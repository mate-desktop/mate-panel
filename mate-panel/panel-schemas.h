#ifndef __PANEL_SCHEMAS_H__
#define __PANEL_SCHEMAS_H__

#define PANEL_RESOURCE_PATH "/org/mate/panel/"
#define PANEL_GENERAL_PATH "/org/mate/panel/general/"

#define PANEL_TOPLEVEL_PATH           "/org/mate/panel/toplevels/"
#define PANEL_TOPLEVEL_DEFAULT_PREFIX "toplevel"
#define PANEL_OBJECT_PATH             "/org/mate/panel/objects/"
#define PANEL_OBJECT_DEFAULT_PREFIX   "object"

#define PANEL_SCHEMA                  "org.mate.panel"
#define PANEL_DEFAULT_LAYOUT          "default-layout"
#define PANEL_TOPLEVEL_ID_LIST_KEY    "toplevel-id-list"
#define PANEL_OBJECT_ID_LIST_KEY      "object-id-list"
#define PANEL_LOCKED_DOWN_KEY         "locked-down"
#define PANEL_DISABLE_FORCE_QUIT_KEY  "disable-force-quit"
#define PANEL_DISABLED_APPLETS_KEY    "disabled-applets"

#define PANEL_TOPLEVEL_SCHEMA                "org.mate.panel.toplevel"
#define PANEL_TOPLEVEL_NAME_KEY              "name"
#define PANEL_TOPLEVEL_SCREEN_KEY            "screen"
#define PANEL_TOPLEVEL_MONITOR_KEY           "monitor"
#define PANEL_TOPLEVEL_EXPAND_KEY            "expand"
#define PANEL_TOPLEVEL_ORIENTATION_KEY       "orientation"
#define PANEL_TOPLEVEL_SIZE_KEY              "size"
#define PANEL_TOPLEVEL_X_KEY                 "x"
#define PANEL_TOPLEVEL_Y_KEY                 "y"
#define PANEL_TOPLEVEL_X_RIGHT_KEY           "x-right"
#define PANEL_TOPLEVEL_Y_BOTTOM_KEY          "y-bottom"
#define PANEL_TOPLEVEL_X_CENTERED_KEY        "x-centered"
#define PANEL_TOPLEVEL_Y_CENTERED_KEY        "y-centered"
#define PANEL_TOPLEVEL_AUTO_HIDE_KEY         "auto-hide"
#define PANEL_TOPLEVEL_ENABLE_ANIMATIONS_KEY "enable-animations"
#define PANEL_TOPLEVEL_ENABLE_BUTTONS_KEY    "enable-buttons"
#define PANEL_TOPLEVEL_ENABLE_ARROWS_KEY     "enable-arrows"
#define PANEL_TOPLEVEL_HIDE_DELAY_KEY        "hide-delay"
#define PANEL_TOPLEVEL_UNHIDE_DELAY_KEY      "unhide-delay"
#define PANEL_TOPLEVEL_AUTO_HIDE_SIZE_KEY    "auto-hide-size"
#define PANEL_TOPLEVEL_ANIMATION_SPEED_KEY   "animation-speed"

#define PANEL_TOPLEVEL_BACKGROUND_SCHEMA       "org.mate.panel.toplevel.background"
#define PANEL_TOPLEVEL_BACKGROUND_SCHEMA_CHILD "background"

#define PANEL_OBJECT_SCHEMA                  "org.mate.panel.object"
#define PANEL_OBJECT_TYPE_KEY                "object-type"
#define PANEL_OBJECT_TOPLEVEL_ID_KEY         "toplevel-id"
#define PANEL_OBJECT_POSITION_KEY            "position"
#define PANEL_OBJECT_PANEL_RIGHT_STICK_KEY   "panel-right-stick"
#define PANEL_OBJECT_LOCKED_KEY              "locked"
#define PANEL_OBJECT_APPLET_IID_KEY          "applet-iid"
#define PANEL_OBJECT_ATTACHED_TOPLEVEL_ID_KEY "attached-toplevel-id"
#define PANEL_OBJECT_TOOLTIP_KEY              "tooltip"
#define PANEL_OBJECT_USE_CUSTOM_ICON_KEY      "use-custom-icon"
#define PANEL_OBJECT_CUSTOM_ICON_KEY          "custom-icon"
#define PANEL_OBJECT_USE_MENU_PATH_KEY        "use-menu-path"
#define PANEL_OBJECT_MENU_PATH_KEY            "menu-path"
#define PANEL_OBJECT_HAS_ARROW_KEY            "has-arrow"
#define PANEL_OBJECT_LAUNCHER_LOCATION_KEY    "launcher-location"
#define PANEL_OBJECT_ACTION_TYPE_KEY          "action-type"

#define PANEL_MENU_BAR_SCHEMA                 "org.mate.panel.menubar"
#define PANEL_MENU_BAR_SHOW_APPLICATIONS_KEY  "show-applications"
#define PANEL_MENU_BAR_SHOW_PLACES_KEY        "show-places"
#define PANEL_MENU_BAR_SHOW_DESKTOP_KEY       "show-desktop"
#define PANEL_MENU_BAR_SHOW_ICON_KEY          "show-icon"
#define PANEL_MENU_BAR_ICON_NAME_KEY          "icon-name"
#define PANEL_MENU_BAR_MAX_ITEMS_OR_SUBMENU   "max-items-or-submenu"

/* external schemas */

#define CAJA_DESKTOP_SCHEMA                   "org.mate.caja.desktop"
#define CAJA_DESKTOP_HOME_ICON_NAME_KEY       "home-icon-name"
#define CAJA_DESKTOP_COMPUTER_ICON_NAME_KEY   "computer-icon-name"

#define CAJA_PREFS_SCHEMA                     "org.mate.caja.preferences"
#define CAJA_PREFS_DESKTOP_IS_HOME_DIR_KEY    "desktop-is-home-dir"

#define MATE_SESSION_SCHEMA                   "org.mate.session"
#define MATE_SESSION_LOGOUT_PROMPT_KEY        "logout-prompt"

#define LOCKDOWN_SCHEMA                       "org.mate.lockdown"
#define LOCKDOWN_DISABLE_COMMAND_LINE_KEY     "disable-command-line"
#define LOCKDOWN_DISABLE_LOCK_SCREEN_KEY      "disable-lock-screen"
#define LOCKDOWN_DISABLE_LOG_OUT_KEY          "disable-log-out"

#define MARCO_SCHEMA                          "org.mate.Marco.general"
#define MARCO_MOUSE_BUTTON_MODIFIER_KEY       "mouse-button-modifier"

#define MARCO_KEYBINDINGS_SCHEMA             "org.mate.Marco.window-keybindings"
#define MARCO_ACTIVATE_WINDOW_MENU_KEY       "activate-window-menu"
#define MARCO_TOGGLE_MAXIMIZED_KEY           "toggle-maximized"
#define MARCO_MAXIMIZE_KEY                   "maximize"
#define MARCO_UNMAXIMIZE_KEY                 "unmaximize"
#define MARCO_TOGGLE_SHADED_KEY              "toggle-shaded"
#define MARCO_BEGIN_MOVE_KEY                 "begin-move"
#define MARCO_BEGIN_RESIZE_KEY               "begin-resize"

#endif /* __PANEL_SCHEMAS_H__ */
