#include "print_utils.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/lang/translations.h"
#include "src/player/player_manager.h"

#include "mmu/recipient_filter.h"

#include <engine/igameeventsystem.h>
#include <irecipientfilter.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/inetworkserializer.h>
#include <networksystem/netmessage.h>
#include <usermessages.pb.h>

#include <cstdarg>
#include <cstdio>

#define HUD_PRINTTALK 3

class CAllPlayersFilter : public IRecipientFilter
{
public:
	CAllPlayersFilter() {}

	~CAllPlayersFilter() override {}

	void AddRecipient(int slot)
	{
		m_recipients.Set(slot);
	}

	NetChannelBufType_t GetNetworkBufType() const override
	{
		return BUF_RELIABLE;
	}

	bool IsInitMessage() const override
	{
		return false;
	}

	const CPlayerBitVec &GetRecipients() const override
	{
		return m_recipients;
	}

	CPlayerSlot GetPredictedPlayerSlot() const override
	{
		return CPlayerSlot(-1);
	}

private:
	CPlayerBitVec m_recipients;
};

static INetworkMessageInternal *GetTextMsgMsg()
{
	static INetworkMessageInternal *s_pMsg = nullptr;
	if (!s_pMsg && g_pNetworkMessages)
	{
		s_pMsg = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
	}
	return s_pMsg;
}

static void SendChatToFilter(IRecipientFilter *pFilter, const char *text)
{
	INetworkMessageInternal *pNetMsg = GetTextMsgMsg();
	if (!pNetMsg || !g_pGameEventSystem)
	{
		return;
	}

	CNetMessage *pData = pNetMsg->AllocateMessage();
	if (!pData)
	{
		return;
	}

	auto *pTextMsg = pData->ToPB<CUserMessageTextMsg>();
	pTextMsg->set_dest(HUD_PRINTTALK);
	pTextMsg->add_param(text);

	g_pGameEventSystem->PostEventAbstract(-1, false, pFilter, pNetMsg, pData, 0);
	g_pNetworkMessages->DeallocateNetMessageAbstract(pNetMsg, pData);
}

void RTV_PrintToChat(int slot, const char *fmt, ...)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	char chatBuf[600];
	snprintf(chatBuf, sizeof(chatBuf), " %s%s", g_RTVConfig.general.chatPrefix.c_str(), buf);

	CSingleRecipientFilter filter(slot);
	SendChatToFilter(&filter, chatBuf);
}

void RTV_ChatToAll(const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	char chatBuf[600];
	snprintf(chatBuf, sizeof(chatBuf), " %s%s", g_RTVConfig.general.chatPrefix.c_str(), buf);

	CAllPlayersFilter filter;
	CGlobalVars *globals = GetGameGlobals();
	if (globals)
	{
		for (int i = 0; i < globals->maxClients; i++)
		{
			PlayerInfo *p = g_RTVPlayerManager.GetPlayer(i);
			if (p && p->connected && !p->fakePlayer)
			{
				filter.AddRecipient(i);
			}
		}
	}

	SendChatToFilter(&filter, chatBuf);

	// Strip Source chat color codes (0x01–0x10) before console print to avoid
	// Windows BEL (\x07) triggering a system beep.
	char conBuf[512];
	char *dst = conBuf;
	for (const char *src = buf; *src && dst < conBuf + sizeof(conBuf) - 1; src++)
	{
		unsigned char c = (unsigned char)*src;
		if (c < 0x01 || c > 0x10)
		{
			*dst++ = *src;
		}
	}
	*dst = '\0';
	META_CONPRINTF("[RTV] %s\n", conBuf);
}

void RTV_PrintToChatT(int slot, const char *phrase, ...)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	std::string tmpl = g_RTVTranslations.Translate(RTV_SlotLanguage(slot), phrase);

	char buf[512];
	va_list args;
	va_start(args, phrase);
	vsnprintf(buf, sizeof(buf), tmpl.c_str(), args);
	va_end(args);

	char chatBuf[600];
	snprintf(chatBuf, sizeof(chatBuf), " %s%s", g_RTVConfig.general.chatPrefix.c_str(), buf);

	CSingleRecipientFilter filter(slot);
	SendChatToFilter(&filter, chatBuf);
}

void RTV_ChatToAllT(const char *phrase, ...)
{
	CGlobalVars *globals = GetGameGlobals();
	int maxClients = globals ? globals->maxClients : 0;

	for (int i = 0; i < maxClients; i++)
	{
		PlayerInfo *p = g_RTVPlayerManager.GetPlayer(i);
		if (!p || !p->connected || p->fakePlayer)
		{
			continue;
		}

		std::string tmpl = g_RTVTranslations.Translate(RTV_SlotLanguage(i), phrase);

		char buf[512];
		va_list args;
		va_start(args, phrase);
		vsnprintf(buf, sizeof(buf), tmpl.c_str(), args);
		va_end(args);

		char chatBuf[600];
		snprintf(chatBuf, sizeof(chatBuf), " %s%s", g_RTVConfig.general.chatPrefix.c_str(), buf);

		CSingleRecipientFilter filter(i);
		SendChatToFilter(&filter, chatBuf);
	}

	// Console mirror, rendered once in the server default language.
	std::string conTmpl = g_RTVTranslations.Translate("", phrase);
	char conFmt[512];
	va_list conArgs;
	va_start(conArgs, phrase);
	vsnprintf(conFmt, sizeof(conFmt), conTmpl.c_str(), conArgs);
	va_end(conArgs);

	// Strip Source chat color codes (0x01-0x10) before console print
	char conBuf[512];
	char *dst = conBuf;
	for (const char *src = conFmt; *src && dst < conBuf + sizeof(conBuf) - 1; src++)
	{
		unsigned char c = (unsigned char)*src;
		if (c < 0x01 || c > 0x10)
		{
			*dst++ = *src;
		}
	}
	*dst = '\0';
	META_CONPRINTF("[RTV] %s\n", conBuf);
}

void RTV_PrintToClient(int slot, const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (slot < 0)
	{
		META_CONPRINTF("%s", buf);
		return;
	}

	if (slot > MAXPLAYERS || !g_pEngine)
	{
		return;
	}

	g_pEngine->ClientPrintf(CPlayerSlot(slot), buf);
}

void RTV_ConPrint(const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	META_CONPRINTF("[RTV] %s\n", buf);
}
