// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_PLUGIN_API_H
#define EGBE_PLUGIN_API_H

// Note: This file should only be included when creating a plugin to be
//       exported as a dynamically loaded library.

#include "egbe.h"
#include "common.h"

long egbe_plugin_export_api = EGBE_PLUGIN_API;

#define PLUGIN_NAME(val) char *egbe_plugin_export_name = val

#define PLUGIN_DESCRIPTION(val) char *egbe_plugin_export_description = val

#define PLUGIN_WEBSITE(val) char *egbe_plugin_export_website = val

#define PLUGIN_AUTHOR(val) char *egbe_plugin_export_author = val

// TODO: VERSION_{MAJOR,MINOR,PATCH}?
#define PLUGIN_VERSION(val) char *egbe_plugin_export_version = val

#define PLUGIN_INIT(val) EGBE_PLUGIN_INIT egbe_plugin_export_init = val
#define PLUGIN_EXIT(val) EGBE_PLUGIN_EXIT egbe_plugin_export_exit = val

// Note: This hook should only be used when it is necessary for the main EGBE
//       function to exist ON THE SAME STACK FRAME as the plugin itself.
//       Otherwise, it's much simpler to just use `init`/`exit` as necessary.
//
//       If you do need to use the call hook, it is NECESSARY to include
//       `call_next_plugin(context)` somewhere in your function.
//       See the `ruby` plugin for a practical example.
#define PLUGIN_CALL(val) EGBE_PLUGIN_CALL egbe_plugin_export_call = val

#define PLUGIN_START_DEBUGGER(val) \
	EGBE_PLUGIN_START_DEBUGGER egbe_plugin_export_start_debugger = val

#define PLUGIN_START_LINK_CLIENT(val) \
	EGBE_PLUGIN_START_LINK_CLIENT egbe_plugin_export_start_link_client = val

#endif
