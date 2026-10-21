/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gui-config.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include "feature/gui/gui-runner.h"
#include "feature/gui/remap.h"
#include <mgba/internal/gba/gba.h>
#ifdef M_CORE_GB
#include <mgba/internal/gb/gb.h>
#include <mgba/gb/interface.h>
#endif
#include <mgba-util/gui/file-select.h>
#include <mgba-util/gui/menu.h>
#include <mgba-util/vfs.h>

#ifndef GUI_MAX_INPUTS
#define GUI_MAX_INPUTS 7
#endif

enum {
	CONFIG_REMAP,
	CONFIG_SAVE,
};

enum {
	CATEGORY_DISPLAY,
	CATEGORY_AUDIO,
	CATEGORY_AUTOSAVE,
	CATEGORY_CONTROLS,
	CATEGORY_BIOS,
	CATEGORY_GB,
	CATEGORY_MAX
};

static const char* const _categoryNames[CATEGORY_MAX] = {
	[CATEGORY_DISPLAY] = "Display",
	[CATEGORY_AUDIO] = "Audio",
	[CATEGORY_AUTOSAVE] = "Autosave",
	[CATEGORY_CONTROLS] = "Controls",
	[CATEGORY_BIOS] = "BIOS",
	[CATEGORY_GB] = "Game Boy",
};

// Platform-specific settings go with the display settings unless listed here
static const struct {
	const char* key;
	int category;
} _extraCategories[] = {
	{ "camera", CATEGORY_GB },
	{ "gyroSensitivity", CATEGORY_CONTROLS },
	{ "useLightSensor", CATEGORY_CONTROLS },
};

static bool _biosNamed(const char* name) {
	char ext[PATH_MAX + 1] = {};
	separatePath(name, NULL, NULL, ext);

	if (strcasestr(name, "bios")) {
		return true;
	}
	if (!strncmp(ext, "bin", PATH_MAX)) {
		return true;
	}
	return false;
}

static int _extraCategory(const struct GUIMenuItem* item) {
	if (GUIVariantIsString(item->data)) {
		size_t i;
		for (i = 0; i < sizeof(_extraCategories) / sizeof(*_extraCategories); ++i) {
			if (strcmp(item->data.v.s, _extraCategories[i].key) == 0) {
				return _extraCategories[i].category;
			}
		}
	}
	return CATEGORY_DISPLAY;
}

static void _loadValues(struct mGUIRunner* runner, struct GUIMenuItemList* items) {
	size_t i;
	for (i = 0; i < GUIMenuItemListSize(items); ++i) {
		struct GUIMenuItem* item = GUIMenuItemListGetPointer(items, i);
		if (!item->validStates || GUIVariantIsVoid(item->data)) {
			continue;
		}
		if (GUIVariantIsString(item->data)) {
			if (item->stateMappings) {
				size_t j;
				for (j = 0; j < item->nStates; ++j) {
					const struct GUIVariant* v = &item->stateMappings[j];
					struct GUIVariant test;
					switch (v->type) {
					case GUI_VARIANT_VOID:
						if (!mCoreConfigGetValue(&runner->config, item->data.v.s)) {
							item->state = j;
							break;
						}
						break;
					case GUI_VARIANT_UNSIGNED:
						if (mCoreConfigGetUIntValue(&runner->config, item->data.v.s, &test.v.u) && test.v.u == v->v.u) {
							item->state = j;
							break;
						}
						break;
					case GUI_VARIANT_INT:
						if (mCoreConfigGetIntValue(&runner->config, item->data.v.s, &test.v.i) && test.v.i == v->v.i) {
							item->state = j;
							break;
						}
						break;
					case GUI_VARIANT_FLOAT:
						if (mCoreConfigGetFloatValue(&runner->config, item->data.v.s, &test.v.f) && fabsf(test.v.f - v->v.f) <= 1e-3f) {
							item->state = j;
							break;
						}
						break;
					case GUI_VARIANT_STRING:
						test.v.s = mCoreConfigGetValue(&runner->config, item->data.v.s);
						if (test.v.s && strcmp(test.v.s, v->v.s) == 0) {
							item->state = j;
							break;
						}
						break;
					case GUI_VARIANT_POINTER:
						break;
					}
				}
			} else {
				mCoreConfigGetUIntValue(&runner->config, item->data.v.s, &item->state);
			}
		}
	}
}

static void _saveValues(struct mGUIRunner* runner, struct GUIMenuItemList* items) {
	size_t i;
	for (i = 0; i < GUIMenuItemListSize(items); ++i) {
		struct GUIMenuItem* item = GUIMenuItemListGetPointer(items, i);
		if (!item->validStates || !GUIVariantIsString(item->data)) {
			continue;
		}
		if (item->stateMappings) {
			const struct GUIVariant* v = &item->stateMappings[item->state];
			switch (v->type) {
			case GUI_VARIANT_VOID:
				mCoreConfigSetValue(&runner->config, item->data.v.s, NULL);
				break;
			case GUI_VARIANT_UNSIGNED:
				mCoreConfigSetUIntValue(&runner->config, item->data.v.s, v->v.u);
				break;
			case GUI_VARIANT_INT:
				mCoreConfigSetIntValue(&runner->config, item->data.v.s, v->v.i);
				break;
			case GUI_VARIANT_FLOAT:
				mCoreConfigSetFloatValue(&runner->config, item->data.v.s, v->v.f);
				break;
			case GUI_VARIANT_STRING:
				mCoreConfigSetValue(&runner->config, item->data.v.s, v->v.s);
				break;
			case GUI_VARIANT_POINTER:
				break;
			}
#ifdef M_CORE_GB
		} else if (GUIVariantCompareString(item->data, "gb.pal")) {
			const struct GBColorPreset* colorPresets;
			GBColorPresetList(&colorPresets);
			const struct GBColorPreset* preset = &colorPresets[item->state];
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[0]", preset->colors[0] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[1]", preset->colors[1] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[2]", preset->colors[2] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[3]", preset->colors[3] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[4]", preset->colors[4] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[5]", preset->colors[5] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[6]", preset->colors[6] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[7]", preset->colors[7] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[8]", preset->colors[8] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[9]", preset->colors[9] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[10]", preset->colors[10] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal[11]", preset->colors[11] & 0xFFFFFF);
			mCoreConfigSetUIntValue(&runner->config, "gb.pal", item->state);
#endif
		} else {
			mCoreConfigSetUIntValue(&runner->config, item->data.v.s, item->state);
		}
	}
}

void mGUIShowConfig(struct mGUIRunner* runner, struct GUIMenuItem* extra, size_t nExtra) {
	struct GUIMenu menu = {
		.title = "Settings",
		.index = 0,
		.background = &runner->background.d
	};
	struct GUIMenu categories[CATEGORY_MAX];
	size_t i;
	GUIMenuItemListInit(&menu.items, 0);
	for (i = 0; i < CATEGORY_MAX; ++i) {
		categories[i] = (struct GUIMenu) {
			.title = _categoryNames[i],
			.index = 0,
			.background = &runner->background.d
		};
		GUIMenuItemListInit(&categories[i].items, 0);
	}

	struct GUIMenuItemList* items = &categories[CATEGORY_DISPLAY].items;
	for (i = 0; i < nExtra; ++i) {
		if (_extraCategory(&extra[i]) == CATEGORY_DISPLAY) {
			*GUIMenuItemListAppend(items) = extra[i];
		}
	}
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Interframe blending",
		.data = GUI_V_S("interframeBlending"),
		.submenu = 0,
		.state = false,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Frameskip",
		.data = GUI_V_S("frameskip"),
		.submenu = 0,
		.state = 0,
		.validStates = (const char*[]) {
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
		},
		.nStates = 10
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Show framerate",
		.data = GUI_V_S("fpsCounter"),
		.submenu = 0,
		.state = false,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Show status OSD",
		.data = GUI_V_S("showOSD"),
		.submenu = 0,
		.state = true,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};

	items = &categories[CATEGORY_AUDIO].items;
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Mute",
		.data = GUI_V_S("mute"),
		.submenu = 0,
		.state = false,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Mute while fast forwarding",
		.data = GUI_V_S("fastForwardMute"),
		.submenu = 0,
		.state = false,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};

	items = &categories[CATEGORY_AUTOSAVE].items;
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Autosave state",
		.data = GUI_V_S("autosave"),
		.submenu = 0,
		.state = true,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Autoload state",
		.data = GUI_V_S("autoload"),
		.submenu = 0,
		.state = true,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};

	items = &categories[CATEGORY_CONTROLS].items;
	const char* mapNames[GUI_MAX_INPUTS + 1];
	if (runner->keySources) {
		for (i = 0; runner->keySources[i].id && i < GUI_MAX_INPUTS; ++i) {
			mapNames[i] = runner->keySources[i].name;
		}
		if (i == 1) {
			// Don't display a name if there's only one input source
			i = 0;
		}
		*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
			.title = "Remap controls",
			.data = GUI_V_U(CONFIG_REMAP),
			.state = 0,
			.validStates = i ? mapNames : 0,
			.nStates = i
		};
	}
	if (runner->setSpeedRatio) {
		*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
			.title = "Fast forward speed",
			.data = GUI_V_S("fastForwardRatio"),
			.submenu = 0,
			.state = 0,
			.validStates = (const char*[]) {
				"1.25x", "1.5x", "2x", "3x", "4x"
			},
			.stateMappings = (const struct GUIVariant[]) {
				GUI_V_F(1.25f),
				GUI_V_F(1.5f),
				GUI_V_F(2.0f),
				GUI_V_F(3.0f),
				GUI_V_F(4.0f),
			},
			.nStates = 5
		};
	}

	items = &categories[CATEGORY_BIOS].items;
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Use BIOS if found",
		.data = GUI_V_S("useBios"),
		.submenu = 0,
		.state = true,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
#ifdef M_CORE_GBA
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Select GBA BIOS path",
		.data = GUI_V_S("gba.bios"),
	};
#endif
#ifdef M_CORE_GB
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Select GB BIOS path",
		.data = GUI_V_S("gb.bios"),
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Select GBC BIOS path",
		.data = GUI_V_S("gbc.bios"),
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Select SGB BIOS path",
		.data = GUI_V_S("sgb.bios"),
	};
#endif

	items = &categories[CATEGORY_GB].items;
#ifdef M_CORE_GB
	struct GUIMenuItem* palette = GUIMenuItemListAppend(items);
	*palette = (struct GUIMenuItem) {
		.title = "GB palette",
		.data = GUI_V_S("gb.pal"),
	};
	const struct GBColorPreset* colorPresets;
	palette->nStates = GBColorPresetList(&colorPresets);
	const char** paletteStates = calloc(palette->nStates, sizeof(char*));
	for (i = 0; i < palette->nStates; ++i) {
		paletteStates[i] = colorPresets[i].name;
	}
	palette->validStates = paletteStates;
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Enable SGB features",
		.data = GUI_V_S("sgb.model"),
		.submenu = 0,
		.state = true,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.stateMappings = (const struct GUIVariant[]) {
			GUI_V_S("DMG"),
			GUI_V_S("SGB"),
		},
		.nStates = 2
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Enable SGB borders",
		.data = GUI_V_S("sgb.borders"),
		.submenu = 0,
		.state = true,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Crop SGB borders",
		.data = GUI_V_S("sgb.borderCrop"),
		.submenu = 0,
		.state = false,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
#endif
#if defined(M_CORE_GBA) && (defined(GEKKO) || defined(__SWITCH__) || defined(PSP2))
	*GUIMenuItemListAppend(items) = (struct GUIMenuItem) {
		.title = "Enable GBP features",
		.data = GUI_V_S("gba.forceGbp"),
		.submenu = 0,
		.state = false,
		.validStates = (const char*[]) {
			"Off", "On"
		},
		.nStates = 2
	};
#endif

	for (i = 0; i < nExtra; ++i) {
		int category = _extraCategory(&extra[i]);
		if (category != CATEGORY_DISPLAY) {
			*GUIMenuItemListAppend(&categories[category].items) = extra[i];
		}
	}

	for (i = 0; i < CATEGORY_MAX; ++i) {
		size_t nItems = GUIMenuItemListSize(&categories[i].items);
		if (!nItems) {
			// Hide categories that are empty, e.g. when a core is not built
			continue;
		}
		struct GUIMenuItem* first = GUIMenuItemListGetPointer(&categories[i].items, 0);
		if (nItems == 1 && !first->validStates && !first->submenu) {
			// A category with only one action, e.g. remapping the only input source, runs it directly
			struct GUIMenuItem* item = GUIMenuItemListAppend(&menu.items);
			*item = *first;
			item->title = categories[i].title;
			continue;
		}
		*GUIMenuItemListAppend(&menu.items) = (struct GUIMenuItem) {
			.title = categories[i].title,
			.submenu = &categories[i],
		};
	}
	*GUIMenuItemListAppend(&menu.items) = (struct GUIMenuItem) {
		.title = "Save",
		.data = GUI_V_U(CONFIG_SAVE),
	};
	*GUIMenuItemListAppend(&menu.items) = (struct GUIMenuItem) {
		.title = "Cancel",
		.data = GUI_V_V,
	};
	enum GUIMenuExitReason reason;
	char gbaBiosPath[256] = "";
#ifdef M_CORE_GB
	char gbBiosPath[256] = "";
	char gbcBiosPath[256] = "";
	char sgbBiosPath[256] = "";
#endif

	for (i = 0; i < CATEGORY_MAX; ++i) {
		_loadValues(runner, &categories[i].items);
	}

	struct GUIMenu* current = &menu;
	struct GUIMenuItem* item;
	while (true) {
		reason = GUIShowMenu(&runner->params, current, &item);
		if (reason == GUI_MENU_EXIT_BACK && current != &menu) {
			current = &menu;
			continue;
		}
		if (reason != GUI_MENU_EXIT_ACCEPT || GUIVariantIsVoid(item->data)) {
			break;
		}
		// Accepting an item leaves the menu, so come back to its category afterwards
		current = &menu;
		for (i = 0; i < CATEGORY_MAX; ++i) {
			struct GUIMenu* category = &categories[i];
			if (GUIMenuItemListSize(&category->items) && item == GUIMenuItemListGetPointer(&category->items, category->index)) {
				current = category;
			}
		}
		if (GUIVariantCompareUInt(item->data, CONFIG_SAVE)) {
			if (gbaBiosPath[0]) {
				mCoreConfigSetValue(&runner->config, "gba.bios", gbaBiosPath);
			}
			if (gbBiosPath[0]) {
				mCoreConfigSetValue(&runner->config, "gb.bios", gbBiosPath);
			}
			if (gbcBiosPath[0]) {
				mCoreConfigSetValue(&runner->config, "gbc.bios", gbcBiosPath);
			}
			if (sgbBiosPath[0]) {
				mCoreConfigSetValue(&runner->config, "sgb.bios", sgbBiosPath);
			}
			for (i = 0; i < CATEGORY_MAX; ++i) {
				_saveValues(runner, &categories[i].items);
			}
			if (runner->keySources) {
				size_t i;
				for (i = 0; runner->keySources[i].id; ++i) {
					mInputMapSave(&runner->core->inputMap, runner->keySources[i].id, mCoreConfigGetInput(&runner->config));
					mInputMapSave(&runner->params.keyMap, runner->keySources[i].id, mCoreConfigGetInput(&runner->config));
				}
			}
			mCoreConfigSave(&runner->config);
			mCoreLoadForeignConfig(runner->core, &runner->config);
			break;
		}
		if (GUIVariantCompareUInt(item->data, CONFIG_REMAP)) {
			mGUIRemapKeys(&runner->params, &runner->core->inputMap, &runner->keySources[item->state]);
			continue;
		}
		if (GUIVariantCompareString(item->data, "gba.bios")) {
			// TODO: show box if failed
			if (!GUISelectFile(&runner->params, gbaBiosPath, sizeof(gbaBiosPath), _biosNamed, GBAIsBIOS, NULL)) {
				gbaBiosPath[0] = '\0';
			}
			continue;
		}
#ifdef M_CORE_GB
		if (GUIVariantCompareString(item->data, "gb.bios")) {
			// TODO: show box if failed
			if (!GUISelectFile(&runner->params, gbBiosPath, sizeof(gbBiosPath), _biosNamed, GBIsBIOS, NULL)) {
				gbBiosPath[0] = '\0';
			}
			continue;
		}
		if (GUIVariantCompareString(item->data, "gbc.bios")) {
			// TODO: show box if failed
			if (!GUISelectFile(&runner->params, gbcBiosPath, sizeof(gbcBiosPath), _biosNamed, GBIsBIOS, NULL)) {
				gbcBiosPath[0] = '\0';
			}
			continue;
		}
		if (GUIVariantCompareString(item->data, "sgb.bios")) {
			// TODO: show box if failed
			if (!GUISelectFile(&runner->params, sgbBiosPath, sizeof(sgbBiosPath), _biosNamed, GBIsBIOS, NULL)) {
				sgbBiosPath[0] = '\0';
			}
			continue;
		}
#endif
		if (item->validStates) {
			if (item->state < item->nStates - 1) {
				do {
					++item->state;
				} while (!item->validStates[item->state] && item->state < item->nStates - 1);
				if (!item->validStates[item->state]) {
					item->state = 0;
				}
			} else {
				item->state = 0;
			}
		}
	}
#ifdef M_CORE_GB
	free(paletteStates);
#endif
	for (i = 0; i < CATEGORY_MAX; ++i) {
		GUIMenuItemListDeinit(&categories[i].items);
	}
	GUIMenuItemListDeinit(&menu.items);
}
