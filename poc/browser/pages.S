/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   The app's own pages and scripts inside it, as C strings with a 0 after
   each (main.cpp): the bar (chrome.html), the keyboard, the menu, the
   dialog, the radial menus, the start page and the settings, each with theme.css and
   icons.svg put in (include.awk: the .built.html files); the D-pad's way
   through a page (nav.js) and the built-in extensions (ext/). */

	.macro page name, file
	.section .rodata
	.global \name
	.type \name, @object
\name:
	.incbin "\file"
	.byte 0
	.size \name, . - \name
	.endm

	page chrome_html, "chrome.built.html"
	page keyboard_html, "keyboard.built.html"
	page menu_html, "menu.built.html"
	page dialog_html, "dialog.built.html"
	page radial_html, "radial.built.html"
	page tabs_html, "tabs.built.html"
	page start_html, "start.built.html"
	page settings_html, "settings.built.html"
	page phone_html, "phone.built.html"
	page nav_js, "nav.js"
	page ext_gm_js, "ext/gm.js"
	page ext_adblock_js, "ext/adblock.js"
	page ext_dark_js, "ext/dark.js"
	page ext_fullscreen_js, "ext/fullscreen.js"
	page ext_adhosts_txt, "ext/adhosts.txt"
	page ext_gamepad_js, "ext/gamepad.js"
	page ext_devtools_js, "ext/devtools.js"
	page ext_manager_js, "ext/manager.js"
	page ext_logins_js, "ext/logins.js"
	page ext_reader_js, "ext/reader.js"
	page ext_linkmenu_js, "ext/linkmenu.js"

	.section .note.GNU-stack, "", @progbits
