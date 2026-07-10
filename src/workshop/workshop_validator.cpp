#include "workshop_validator.h"
#include "mmu/log.h"

#include "src/common.h"

#include <filesystem.h>
#include <KeyValues.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

static std::string WorkshopRootAbs()
{
	std::error_code ec;
	fs::path abs = fs::absolute(fs::path("steamapps") / "workshop", ec);
	if (ec)
		return std::string("steamapps/workshop");
	return abs.string();
}

// True if `steamapps/workshop/content/730/<id>/` exists AND contains at least one .vpk file.
static bool WorkshopFolderHasVPK(const std::string &workshopId)
{
	if (workshopId.empty())
		return false;

	fs::path folder = fs::path(WorkshopRootAbs()) / "content" / "730" / workshopId;

	std::error_code ec;
	if (!fs::is_directory(folder, ec))
		return false;

	fs::directory_iterator it(folder, ec);
	if (ec)
		return false;

	for (const auto &entry : it)
	{
		std::error_code ec2;
		if (!entry.is_regular_file(ec2))
			continue;
		const auto &p = entry.path();
		if (p.has_extension() && p.extension() == ".vpk")
			return true;
	}
	return false;
}

// Remove the entry for `workshopId` from a single ACF section, if present.
// Returns true if anything was removed.
static bool PruneIdFromSection(KeyValues *pACF, const char *sectionName, const char *workshopId)
{
	KeyValues *pSection = pACF->FindKey(sectionName);
	if (!pSection)
		return false;
	if (!pSection->FindKey(workshopId))
		return false;
	return pSection->FindAndDeleteSubKey(workshopId);
}

// Prune the ACF entry for the given workshop ID from both WorkshopItemsInstalled and WorkshopItemDetails,
// save the file, then ask SteamUGC to re-read it so the engine sees the addon as not-installed.
static bool PruneACFEntryForId(const std::string &workshopId)
{
	if (!g_pFullFileSystem)
		return false;

	const std::string workshopRoot = WorkshopRootAbs();
	const std::string acfPath = workshopRoot + "/appworkshop_730.acf";

	KeyValues *pACF = new KeyValues("AppWorkshop");
	KeyValues::AutoDelete autoDelete(pACF);
	if (!pACF->LoadFromFile(g_pFullFileSystem, acfPath.c_str(), "GAME"))
	{
		// No ACF means Steam doesn't believe the addon is installed; nothing to do.
		return false;
	}

	bool removedInstalled = PruneIdFromSection(pACF, "WorkshopItemsInstalled", workshopId.c_str());
	bool removedDetails = PruneIdFromSection(pACF, "WorkshopItemDetails", workshopId.c_str());
	if (!removedInstalled && !removedDetails)
		return false;

	pACF->SaveToFile(g_pFullFileSystem, acfPath.c_str(), "GAME");
	if (g_RTVSteamAPI.SteamUGC())
	{
		g_RTVSteamAPI.SteamUGC()->BInitWorkshopForGameServer(730, const_cast<char *>(workshopRoot.c_str()));
	}
	return true;
}

bool RTV_EnsureWorkshopMapReady(const std::string &workshopId)
{
	if (workshopId.empty())
		return false;

	if (WorkshopFolderHasVPK(workshopId))
		return false; // already good

	MMU_LOG_INFO("Workshop addon %s has no .vpk on disk; pruning stale ACF entry so Steam will re-download.\n",
				   workshopId.c_str());
	return PruneACFEntryForId(workshopId);
}
