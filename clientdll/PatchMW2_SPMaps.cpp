// ==========================================================
// IW4M project
// 
// Component: clientdll
// Sub-component: steam_api
// Purpose: singleplayer level loading code (reallocation, 
//          address changes, other patches, ...)
//
// Initial author: NTAuthority
// Started: 2011-05-25
// ==========================================================

#include "StdInc.h"
#include <vector>
#include <string>
#include <unordered_map>

// entity dumping, needs to be split?
struct MapEnts
{
	const char* name;
	const char* entitystring;
};

bool justIgnore = false;
StompHook _dbAddXAssetHook;
DWORD _dbAddXAssetHookLoc = 0x5BB650;
DWORD _dbAddXAssetHookRet = 0x5BB657;

void _DumpMapEntities(MapEnts* entities)
{
	char filename[255];

	CreateDirectoryA("raw/maps", NULL);
	CreateDirectoryA("raw/maps/mp", NULL);

	_snprintf(filename, sizeof(filename), "raw/%s.ents", entities->name);

	FILE* file = fopen(filename, "w");
	if (file)
	{
		fwrite(entities->entitystring, 1, strlen(entities->entitystring), file);
		fclose(file);
	}
}

static char mapEntities[1024 * 1024];

void _LoadMapEntities(MapEnts* entry)
{
	char* buffer;
	char filename[255];
	_snprintf(filename, sizeof(filename), "%s.ents", entry->name);

	// why the weird casts?
	if (FS_ReadFile((const char*)(&filename), (char**)&buffer) >= 0)
	{
		strcpy(mapEntities, buffer);
		entry->entitystring = mapEntities;

		FS_FreeFile(buffer);
	}
}

// more code
static DWORD gameWorldSP;
static DWORD gameWorldMP;

void* ReallocateAssetPool(int type, unsigned int newSize);

// TODO: load these dynamically
struct LevelDependency
{
	const char* level;
	const char* dependency;
};

LevelDependency _dependencies[] = 
{
	{ "oilrig", "mp_subbase" },
	{ "invasion", "mp_invasion" },
	{ "gulag", "mp_subbase" },
	{ "contingency", "mp_subbase" },
	{ "so_ghillies", "mp_brecourt" },
	{ "roadkill", "mp_afghan" }, // actually Rust is vs. TF 141 and not vs. Army Rangers, but meh.
	{ "favela", "mp_favela" }, // originally said 'takedown', don't know who the fsck came up with this
	{ "so_bridge", "mp_storm" }, // other map dependency requests, PM me on fourdeltaone.net :)
	{ "iw4_credits", "mp_rust" },
	{ "trainer", "mp_rust" },
	{ "dc_whitehouse", "mp_highrise" },
	{ "airport", "mp_terminal" },
	{ "af_caves", "mp_rust" },
	{ "af_chase", "mp_rust" },
	{ "arcadia", "mp_invasion" },
	{ "boneyard", "mp_boneyard" },
	{ "cliffhanger", "mp_derail" },
	{ "co_hunted", "mp_underpass" },
	{ "so_hunted", "mp_underpass" }, // HAHA, lol 
	{ "dcemp", "mp_highrise" },
	{ "dcburning", "mp_highrise" },
	{ "ending", "mp_rust" },
	{ "estate", "mp_estate" },
	{ "favela_escape", "mp_favela" },
//	{ "mp_nuked", "mp_terminal" },
//	{ "mp_nuked", "mp_nuked_shaders" },
	{ "mp_nuked", "team_us_army" },
	{ "mp_nuked", "team_opforce_airborne" },
//	{ "mp_bog_sh", "mp_nuked_shaders" },
	{ "mp_bog_sh", "w2_shaders" },	// Probably unnecessary?
	{ "mp_bog_sh", "w2_xmodels" },
	{ "mp_bog_sh", "team_us_army" },
	{ "mp_bog_sh", "team_opforce_composite" },
	//{ "mp_cross_fire", "iw4_wc_shaders" },
	{ "mp_cross_fire", "team_us_army" },
	{ "mp_cross_fire", "team_opforce_composite" },
//	{ "mp_bloc", "iw4_wc_shaders" },
	{ "mp_bloc", "team_us_army" },
	{ "mp_bloc", "team_opforce_airborne" },
	{ "mp_bloc", "environment_forest" },
//	{ "mp_fav_tropical", "iw4_wc_shaders" },
	{ "mp_fav_tropical", "team_us_army" },
	{ "mp_fav_tropical", "team_opforce_airborne" },
	{ "mp_fav_tropical", "mp_bog_sh_deps" },
//	{ "mp_nuked", "weap_oilrig" },
	{ "ui_viewer_mp", "ui_viewer_mp_shaders" },
	{ "ui_viewer_mp", "team_us_army" },
//	{ "mp_rust", "iw4_wc_shaders" },
//	{ "mp_rust", "weap_trey" },
//	{ "mp_nuked", "common_tc_mp" },
//	{ "mp_nuked", "iw4_wc_shaders" },
//	{ "mp_cross_fire", "common_tc_mp" },
//	{ "mp_cross_fire", "iw4_wc_shaders" },
//	{ "mp_cross_fire", "common_gametype_mp" },
//	{ "mp_nuked", "mp_nightshift" },
	{ "mp_cargoship", "team_tf141" },
	{ "mp_cargoship", "team_us_army" },
	{ "mp_cargoship_sh", "team_tf141" },
	{ "mp_cargoship_sh", "team_us_army" },
	{ "mp_cargoship_sh", "mp_cargoship" },
	{ "mp_killhouse", "team_us_army" },
	{ "mp_killhouse", "team_opforce_composite" },
//	{ "mp_rust", 0 },
	{ 0, 0 }
};

// called during Com_LoadLevelZone, replaces the DB_LoadXAssets call
CallHook mapZoneLoadHook;
DWORD mapZoneLoadHookLoc = 0x42C2AF;

// Com_Sprintf call in Com_GetBspFilename
CallHook getBSPNameHook;
DWORD _getBSPNameHookLoc = 0x4C5979;

static char levelDependencyName[64];
static char levelAssetName[64];

static bool _hideIW4CCommon = false;

std::unordered_map<std::string, bool> _allowedAssetMap;
bool* ignoreThisFx;

bool AssetRestrict_RestrictFromMaps(assetType_t type, const char* name, const char* zone)
{
	if (!_stricmp(zone, "ui_viewer_mp"))
	{
		if (type == ASSET_TYPE_MATERIAL)
		{
			if (!strcmp(name, "gfx_glow_hs"))
			{
				_hideIW4CCommon = false;
			}

			if (!strcmp(name, "mc/mtl_sra_tracker_ssncm_blk"))
			{
				_hideIW4CCommon = false;
			}

			if (_hideIW4CCommon)
			{
				//OutputDebugString(va("hidden material %s\n", name));
				return true;
			}

			if (!strcmp(name, "mc/mtl_heg_zmcowboy_zm_m_00"))
			{
				_hideIW4CCommon = true;
			}

			if (strstr(name, "_mag_") || strstr(name, "_iro_"), strstr(name, "_sto_") || strstr(name, "_gri_") || strstr(name, "_muz_") || strstr(name, "_red_"))
			{
				return true;
			}
		}
		else if (type == ASSET_TYPE_STRINGTABLE)
		{
			return true;
		}

		if (type == ASSET_TYPE_MATERIAL || type == ASSET_TYPE_IMAGE || type == ASSET_TYPE_XMODEL || type == ASSET_TYPE_XMODELSURFS)
		{
			if (_allowedAssetMap.find(name) != _allowedAssetMap.end())
			{
				return true;
			}
		}
	}

	if (!_stricmp(zone, levelDependencyName))
	{
		// don't load other maps
		if (type == ASSET_TYPE_GAME_MAP_MP || type == ASSET_TYPE_COL_MAP_MP || type == ASSET_TYPE_GFX_MAP || type == ASSET_TYPE_MAP_ENTS || type == ASSET_TYPE_COM_MAP || type == ASSET_TYPE_FX_MAP)
		{
			return true;
		}

		// also don't load localize/fx
		if (type == ASSET_TYPE_LOCALIZE/* || type == ASSET_TYPE_FX*/) // we need to link 'fx' assets as otherwise we
																      // crash at Mark_FxEffectDefAsset...
																	  // guess rule #1 needs to be expanded:
																	  // so rule #2 becomes 'don't touch fastfiles through code
																	  // if you do not understand the code'.
																	  // rule #1 still stands: 'don't touch fastfiles'
		{
			return true;
		}

		// and don't load * images (GfxWorld internal)
		if (type == ASSET_TYPE_IMAGE && name[0] == '*')
		{
			return true;
		}

		// don't load external bad images
		/*if (type != ASSET_TYPE_VERTEXSHADER && type != ASSET_TYPE_PIXELSHADER && type != ASSET_TYPE_VERTEXDECL && type != ASSET_TYPE_TECHSET &&
			type != ASSET_TYPE_XMODEL && type != ASSET_TYPE_XMODELSURFS && type != ASSET_TYPE_XANIM)
		{
			if (_allowedAssetMap.find(name) == _allowedAssetMap.end())
			{
				return true;
			}
		}*/
	}

	if (type == ASSET_TYPE_FX)
	{
		/*if (!strcmp(zone, "mp_nuked"))
		{
			if (!strncmp(name, "destructibles/", 14) || !strncmp(name, "props/", 6) || !strcmp(name, "maps/mp_maps/fx_mp_nuked_glass_break") || !strcmp(name, "maps/mp_maps/fx_mp_nuked_nuclear_explosion") || !strncmp(name, "gfx_", 4))
			{
				return true;
			}
		}*/

		if (*ignoreThisFx)
		{
			*ignoreThisFx = false;
			return true;
		}

		if (!strcmp(zone, "mp_nuked")/* || !strcmp(zone, "mp_bog_sh")*/)
		{
			// glass is sadly a model, therefore broken for now due to extdll issues :(
			//if (!strstr(name, "double_rainbow") && !strstr(name, "smoke/car_damage_") && !strstr(name, "butterfly") && !strstr(name, "sprinkler") && !strstr(name, "hose_spray")/* && !strstr(name, "car_glass")*/)
			{
				//OutputDebugString(va("blocked fx %s\n", name));
				//return true;
			}
		}
	}

	if (type == ASSET_TYPE_WEAPON)
	{
		//if (!stricmp(zone, levelAssetName))
		//{
			//OutputDebugString(name);
			//OutputDebugString("\n");

			if (strstr(name, "_mp") == 0 && strstr(name, "m40a3") == 0 && strstr(name, "winchester1200") == 0)
			{
				OutputDebugString(va("blocked weapon %s\n", name));
				return true;
			}
			else if (!strcmp("ui_viewer_mp", zone))
			{
				return true;
			}
		//}
	}

	if (type == ASSET_TYPE_ADDON_MAP_ENTS)
	{
		return true;
	}
	
	if (type == ASSET_TYPE_MATERIAL || type == ASSET_TYPE_MENU || type == ASSET_TYPE_MENUFILE || type == ASSET_TYPE_WEAPON || type == ASSET_TYPE_RAWFILE)
	{
		if (!strcmp(zone, "common_tc_mp") || !strcmp(zone, "common_gametype_mp"))
		{
			return true;
		}
	}

	if (type == ASSET_TYPE_STRINGTABLE)
	{
		if (!strcmp(zone, "mp_cross_fire"))
		{
			return true;
		}
	}

	return false;
}

char* GetZonePath(const char* fileName);

void MapZoneLoadHookFunc(XZoneInfo* data, int count, int unknown)
{
	// check DLC map existence
	if (true)
	{
		if (GetFileAttributes(va("%s/%s.ff", GetZonePath(data[0].name), data[0].name)) == INVALID_FILE_ATTRIBUTES)
		{
			const char* dlcname = "Unknown";

			if (!_stricmp(data[0].name, "mp_nuked"))
			{
				dlcname = "Nuketown";
			}

			if (!_stricmp(data[0].name, "mp_cross_fire") || !_stricmp(data[0].name, "mp_cargoship") || !_stricmp(data[0].name, "mp_bloc"))
			{
				dlcname = "Classics #1";
			}

			if (!_stricmp(data[0].name, "mp_complex") || !_stricmp(data[0].name, "mp_compact") || !_stricmp(data[0].name, "mp_storm") || !_stricmp(data[0].name, "mp_overgrown") || !_stricmp(data[0].name, "mp_crash"))
			{
				dlcname = "Stimulus";
			}

			if (!_stricmp(data[0].name, "mp_abandon") || !_stricmp(data[0].name, "mp_vacant") || !_stricmp(data[0].name, "mp_trailerpark") || !_stricmp(data[0].name, "mp_strike") || !_stricmp(data[0].name, "mp_fuel2"))
			{
				dlcname = "Resurgence";
			}

			Com_Error(1, "You do not seem to have the map '%s'.\nDownload the %s DLC from the Store in the Main Menu.", data[0].name, dlcname);

			return;
		}
	}

	// and the usual
	XZoneInfo newData[10];

	// flag us as loading level assets so we don't load weapons
	strcpy(levelAssetName, data[0].name);
	levelDependencyName[0] = '\0';

	// load the base XAsset
	//DB_LoadXAssets(data, count, unknown);
	
	// add level dependencies
	int oldCount = count;
	count = 0;

	for (LevelDependency* dependency = _dependencies; dependency->level; dependency++)
	{
		if (!_stricmp(dependency->level, data[0].name))
		{
			newData[count].name = dependency->dependency;
			newData[count].type1 = data[0].type1;
			newData[count].type2 = data[0].type2;

			if (dependency->dependency)
			{
				if (!strcmp(dependency->dependency, "common_tc_mp"))
				//if (!strcmp(dependency->dependency, "iw4_wc_shaders"))
				{
					DB_LoadXAssets(&newData[count], 1, 1);
				}
				else
				{
					count++;
				}
			}
			//break;
		}
	}

	// load level dependencies
	if (count > 0)
	{
		if (newData[0].name)
		{
			strcpy(levelDependencyName, newData[0].name);
			DB_LoadXAssets(newData, count, unknown);
		}
	}
#if PRE_RELEASE_DEMO
	/*else
	{
		Com_Error(2, "Unsupported level: %s", data[0].name);
	}*/
#endif

	DB_LoadXAssets(data, oldCount, unknown);
}

void _GetBSPNameHookFunc(char* buffer, size_t size, const char* format, const char* mapname)
{
	// the usual stuff
	if (_strnicmp("mp_", mapname, 3))
	{
		format = "maps/%s.d3dbsp";
	}
	
	if (!_stricmp(mapname, "ui_viewer_mp"))
	{
		format = "maps/mp/mp_ui_viewer.d3dbsp";
	}

	if (!_stricmp(mapname, "mp_cargoship_sh"))
	{
		format = "maps/mp/mp_cargoship.d3dbsp";
	}

	_snprintf(buffer, size, format, mapname);

	// check for being MP/SP, and change data accordingly
	if (_strnicmp("mp_", mapname, 3) || !_stricmp(mapname, "mp_nuked") || !_stricmp(mapname, "mp_cross_fire") || !_stricmp(mapname, "mp_cargoship") || !_stricmp(mapname, "mp_cargoship_sh") || !_stricmp(mapname, "ui_viewer_mp") || !_stricmp(mapname, "mp_bog_sh") || !_stricmp(mapname, "mp_bloc") || !_stricmp(mapname, "mp_fav_tropical") || !_stricmp(mapname, "mp_killhouse")) // TODO: fix the hardcoded nuked reference
	{
		// SP
		*(DWORD*)0x4D90B7 = gameWorldSP + 52;		// some game data structure
	}
	else
	{
		// MP
		*(DWORD*)0x4D90B7 = gameWorldMP + 4;		// some game data structure
	}
}

void _AssetRestrict_PreLoadFromMaps(assetType_t type, void* entry, const char* zone)
{
	if (type == ASSET_TYPE_MAP_ENTS)
	{
		if (true || GAME_FLAG(GAME_FLAG_DUMPDATA))
		{
			_DumpMapEntities((MapEnts*)entry);
		}

		_LoadMapEntities((MapEnts*)entry);
	}
}

CallHook ignoreEntityHook;
DWORD ignoreEntityHookLoc = 0x5FBD6E;

typedef struct  
{
	char unknown[16];
} xAssetEntry_t;

static xAssetEntry_t xEntries[789312];

void _ReallocXAssetEntries()
{
	int newsize = 516 * 2048;
	//newEnts = malloc(newsize);

	// get (obfuscated) memory locations
	unsigned int origMin = 0x134CAD8;
	unsigned int origMax = 0x134CAE8;

	unsigned int difference = (unsigned int)xEntries - origMin;

	// scan the .text memory
	char* scanMin = (char*)0x401000;
	char* scanMax = (char*)0x6D7000;
	char* current = scanMin;

	for (; current < scanMax; current += 1) {
		unsigned int* intCur = (unsigned int*)current;

		// if the address points to something within our range of interest
		if (*intCur == origMin || *intCur == origMax) {
			// patch it
			*intCur += difference;
		}
	}

	*(DWORD*)0x5BAEB0 = 789312;
}

bool IgnoreEntityHookFunc(const char* entity)
{
	return (!strncmp(entity, "dyn_", 4) || !strncmp(entity, "node_", 5) || !strncmp(entity, "actor_", 6)/* || !strncmp(entity, "weapon_", 7)*/);
}

typedef const char* (__cdecl * DB_GetXAssetNameHandler_t)(void* asset);
DB_GetXAssetNameHandler_t* DB_GetXAssetNameHandlers = (DB_GetXAssetNameHandler_t*)0x799328;

char CanWeLoadAsset(assetType_t type, void* entry)
{
	const char* name = DB_GetXAssetNameHandlers[type](entry);

	if (!name)
	{
		return 2;
	}

	if (type == ASSET_TYPE_WEAPON)
	{
		// somewhat-workaround for issue 'could not load weapon "destructible_car"' and cars not doing any damage
		if (strcmp(CURRENT_ZONE_NAME, "common_tc_mp") && (!strcmp(name, "none") || !strcmp(name, "destructible_car"))) // common_tc_mp also has these
		{
			return 1;
		}

		if (!strcmp(CURRENT_ZONE_NAME, "patch_mp") && strcmp(name, "airdrop_marker_mp"))
		{
			return 0;
		}
	}

	if (AssetRestrict_RestrictFromMaps(type, name, CURRENT_ZONE_NAME))
	{
		//deadAssets[*(DWORD*)entry] = true;
		return 2;
	}

	return 1;
}

void _DoBeforeLoadAsset(assetType_t type, void** entry)
{
	if (entry)
	{
		_AssetRestrict_PreLoadFromMaps(type, *entry, CURRENT_ZONE_NAME);
	}
}

void* LoadDefaultAsset(assetType_t atype)
{
	void* defaultAsset;

	__asm
	{
		push edi
			mov edi, atype
			mov eax, 5BB210h
			call eax
			pop edi

			mov defaultAsset, eax
	}

	static void* retStuff[2];
	retStuff[0] = 0;
	retStuff[1] = defaultAsset;

	return retStuff;
}

void __declspec(naked) _DB_AddXAssetHookStub()
{
	__asm
	{
		mov eax, [esp + 4]
		mov ecx, [esp + 8]

		push ecx
		push eax
		call CanWeLoadAsset
		add esp, 08h

		cmp al, 2h
		je doDefault

		test al, al
		jz doNotLoad

		mov eax, [esp + 4]
		mov ecx, [esp + 8]
		push ecx
		push eax
		call _DoBeforeLoadAsset
		add esp, 08h

		mov eax, [esp + 8]
		sub esp, 14h
		jmp _dbAddXAssetHookRet

doNotLoad:
		mov eax, [esp + 8]
		retn

doDefault:
		mov eax, [esp + 4]

		push eax
		call LoadDefaultAsset
		add esp, 4h

		retn
	}
}

void PatchMW2_SPMaps()
{
	ignoreThisFx = &justIgnore;
	// set addon_map_ents asset size/pool 'linking' handler sizes (needs to be done before realloc!)
	// NOTE: DON'T ENABLE THIS - IT MESSES UP DOBJS AND SUCH
	//*(DWORD*)0x5B9501 = 12;
	//*(DWORD*)0x799740 = 0x5BC480; // this function has a '12' size.

	// reallocate asset pools
	ReallocateAssetPool(ASSET_TYPE_IMAGE, 7168);
	ReallocateAssetPool(ASSET_TYPE_LOADED_SOUND, 2700);
	ReallocateAssetPool(ASSET_TYPE_FX, 1200);
	ReallocateAssetPool(ASSET_TYPE_LOCALIZE, 14000);
	ReallocateAssetPool(ASSET_TYPE_XANIM, 8192);
	//ReallocateAssetPool(ASSET_TYPE_XMODEL, 3072);
	ReallocateAssetPool(ASSET_TYPE_XMODEL, 5125);
	ReallocateAssetPool(ASSET_TYPE_PHYSPRESET, 128);
	ReallocateAssetPool(ASSET_TYPE_PIXELSHADER, 10000);
	//ReallocateAssetPool(ASSET_TYPE_ADDON_MAP_ENTS, 128);
	ReallocateAssetPool(ASSET_TYPE_VERTEXSHADER, 3072);
	//ReallocateAssetPool(ASSET_TYPE_TECHSET, 1024);
	//ReallocateAssetPool(ASSET_TYPE_MATERIAL, 8192);
	ReallocateAssetPool(ASSET_TYPE_VERTEXDECL, 196);
	ReallocateAssetPool(ASSET_TYPE_WEAPON, 2400);

	// get and store GameWorld*p data
	gameWorldSP = (DWORD)ReallocateAssetPool(ASSET_TYPE_GAME_MAP_SP, 1);
	gameWorldMP = (*(DWORD*)0x4D90B7) - 4;

	// allow loading of IWffu (unsigned) files
	*(BYTE*)0x4158D9 = 0xEB; // main function
	*(WORD*)0x4A1D97 = 0x9090; // DB_AuthLoad_InflateInit

	// ignore 'node_' entities
	ignoreEntityHook.initialize(ignoreEntityHookLoc, IgnoreEntityHookFunc);
	ignoreEntityHook.installHook();

	// asset zone loading
	mapZoneLoadHook.initialize(mapZoneLoadHookLoc, MapZoneLoadHookFunc);
	mapZoneLoadHook.installHook();

	// BSP name
	getBSPNameHook.initialize(_getBSPNameHookLoc, _GetBSPNameHookFunc);
	getBSPNameHook.installHook();

	// hunk size (was 300 MiB)
	*(DWORD*)0x64A029 = 0x1C200000; // 450 MiB
	*(DWORD*)0x64A057 = 0x1C200000;

	_ReallocXAssetEntries();

	_dbAddXAssetHook.initialize(_dbAddXAssetHookLoc, _DB_AddXAssetHookStub, 7);
	_dbAddXAssetHook.installHook();
}