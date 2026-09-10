#include "print_utils.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/lang/translations.h"
#include "src/player/player_manager.h"

#include "mmu/log.h"
#include "mmu/print.h"

#include <cstdarg>
#include <cstdio>

static bool SlotIsHuman(int slot)
{
	PlayerInfo *p = g_RTVPlayerManager.GetPlayer(slot);
	return p && p->connected && !p->fakePlayer;
}

static mmu::ChatPrinter &Printer()
{
	static mmu::ChatPrinter printer = []
	{
		mmu::ChatPrinter p;
		mmu::ChatPrinter::Setup s;
		s.translations = &g_RTVTranslations;
		s.slotLanguage = &RTV_SlotLanguage;
		s.chatPrefix = &g_RTVConfig.general.chatPrefix;
		s.conTag = "RTV";
		s.slotIsHuman = &SlotIsHuman;
		p.Configure(s);
		return p;
	}();
	return printer;
}

MMU_PRINT_SLOT_FN(RTV_PrintToChat, Printer().ChatToSlotV(slot, fmt, args))
MMU_PRINT_GLOBAL_FN(RTV_ChatToAll, Printer().ChatToAllV(fmt, args))
MMU_PRINT_SLOT_FN(RTV_PrintToChatT, Printer().ChatToSlotTV(slot, fmt, args))
MMU_PRINT_GLOBAL_FN(RTV_ChatToAllT, Printer().ChatToAllTV(fmt, args))
MMU_PRINT_SLOT_FN(RTV_PrintToClient, Printer().ClientConsoleV(slot, fmt, args))

void RTV_ConPrint(const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	MMU_LOG_INFO("%s\n", buf);
}
