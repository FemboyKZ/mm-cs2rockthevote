#ifndef _INCLUDE_RTV_GAMEDATA_H_
#define _INCLUDE_RTV_GAMEDATA_H_

#include <cstddef>
#include <cstdint>

namespace gamedata
{
	// Loads CBaseGameSystemFactory::sm_pFirst (RIP-relative mov, displacement at +3).
	// Signature from CS2Fixes cs2fixes.jsonc "IGameSystem_InitAllSystems_pFirst".
#ifdef _WIN32
	// 48 8B 1D ? ? ? ? 48 85 DB 0F 84 ? ? ? ? BD
	static constexpr uint8_t kGameSystemFactorySig[] = {0x48, 0x8B, 0x1D, 0x2A, 0x2A, 0x2A, 0x2A, 0x48, 0x85,
														0xDB, 0x0F, 0x84, 0x2A, 0x2A, 0x2A, 0x2A, 0xBD};
#else
	// 4C 8B 35 ? ? ? ? 4D 85 F6 75 ? E9
	static constexpr uint8_t kGameSystemFactorySig[] = {0x4C, 0x8B, 0x35, 0x2A, 0x2A, 0x2A, 0x2A, 0x4D, 0x85, 0xF6, 0x75, 0x2A, 0xE9};
#endif
	static constexpr size_t kGameSystemFactorySigLen = sizeof(kGameSystemFactorySig);
} // namespace gamedata

#endif // _INCLUDE_RTV_GAMEDATA_H_
