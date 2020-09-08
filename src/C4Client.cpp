/*
 * LegacyClonk
 *
 * Copyright (c) RedWolf Design
 * Copyright (c) 2017-2019, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

#include <C4Include.h>
#include <C4Client.h>

#include <C4Config.h>
#include <C4Network2Client.h>
#include <C4Game.h>
#include <C4Log.h>

#ifndef HAVE_WINSOCK
#include <netdb.h>
#include <sys/socket.h> /* for AF_INET */
#endif

// C4ClientCore

C4ClientCore::C4ClientCore()
	: iID(-1),
	fActivated(false),
	fObserver(false),
	fLobbyReady(false)
{
	Name.Ref(""); Nick.Ref("");
}

C4ClientCore::~C4ClientCore() {}

void C4ClientCore::SetLocal(int32_t inID, bool fnActivated, bool fnObserver)
{
	// status
	iID = inID;
	fActivated = fnActivated;
	fObserver = fnObserver;
	// misc
	Name.CopyValidated(Config.Network.LocalName);

	ValidatedStdStrBuf<C4InVal::VAL_NameNoEmpty> NickBuf;
	NickBuf.Copy(Config.Network.Nick);
	if (!NickBuf.getLength()) NickBuf.CopyValidated(Name);
	Nick.CopyValidated(NickBuf);
}

int32_t C4ClientCore::getDiffLevel(const C4ClientCore &CCore2) const
{
	// nick won't ever be changed
	if (Nick != CCore2.getNick())
		return C4ClientCoreDL_Different;
	// identification changed?
	if (iID != CCore2.getID() || Name != CCore2.getName())
		return C4ClientCoreDL_IDChange;
	// status change?
	if (fActivated != CCore2.isActivated() || fObserver != CCore2.isObserver() || fLobbyReady != CCore2.isLobbyReady())
		return C4ClientCoreDL_IDMatch;
	// otherwise: identical
	return C4ClientCoreDL_None;
}

// C4PacketBase virtuals

void C4ClientCore::CompileFunc(StdCompiler *pComp)
{
	pComp->Value(mkNamingAdapt(iID,         "ID",        C4ClientIDUnknown));
	pComp->Value(mkNamingAdapt(fActivated,  "Activated", false));
	pComp->Value(mkNamingAdapt(fObserver,   "Observer",  false));
	pComp->Value(mkNamingAdapt(Name,        "Name", ""));
	pComp->Value(mkNamingAdapt(Nick,        "Nick", ""));
	pComp->Value(mkNamingAdapt(fLobbyReady, "LobbyReady", false));
}

// *** C4Client

C4Client::C4Client()
	: C4Client(C4ClientCore()) {}

C4Client::C4Client(const C4ClientCore &Core)
	: Core(Core), fLocal(false), pNetClient(nullptr), pNext(nullptr), last_lobby_ready_change(0), muted(Config.Sound.MuteSoundChatCommand), lastSound{} {}

C4Client::~C4Client()
{
	// network client bind must be removed before
	assert(!pNetClient);
}

bool C4Client::canSound() const
{
	return std::chrono::steady_clock::now() - lastSound > Config.Sound.SoundCommandCooldown;
}

void C4Client::SetActivated(bool fnActivated)
{
	Core.SetActivated(fnActivated);
	// activity
	if (fnActivated && pNetClient)
		pNetClient->SetLastActivity(Game.FrameCounter);
}

void C4Client::SetLobbyReady(bool fnLobbyReady, time_t *time_since_last_change)
{
	// Change state
	Core.SetLobbyReady(fnLobbyReady);
	// Keep track of times
	if (time_since_last_change)
	{
		time_t now = time(nullptr);
		*time_since_last_change = now - last_lobby_ready_change;
		last_lobby_ready_change = now;
	}
}

void C4Client::SetLocal()
{
	// set flag
	fLocal = true;
}

void C4Client::Remove()
{
	// remove players for this client
	Game.Players.RemoveAtClient(getID(), true);
}

void C4Client::CompileFunc(StdCompiler *pComp)
{
	pComp->Value(Core);
	// Assume this is a non-local client
	if (pComp->isCompiler())
		fLocal = false;
}

// C4ClientList

C4ClientList::C4ClientList()
	: pFirst(nullptr), pLocal(nullptr), pNetClients(nullptr) {}

C4ClientList::~C4ClientList()
{
	Clear();
}

void C4ClientList::Clear()
{
	ClearNetwork();
	while (pFirst)
	{
		C4Client *pClient = pFirst;
		Remove(pClient, true);
		delete pClient;
	}
}

void C4ClientList::Add(C4Client *pClient)
{
	// local client?
	if (pClient->isLocal()) pLocal = pClient;
	// already in list?
	assert(!getClientByID(pClient->getID()));
	// find insert position
	C4Client *pPos = pFirst, *pPrev = nullptr;
	for (; pPos; pPrev = pPos, pPos = pPos->pNext)
		if (pPos->getID() > pClient->getID())
			break;
	// add to list
	pClient->pNext = pPos;
	(pPrev ? pPrev->pNext : pFirst) = pClient;
	// register to network
	if (pNetClients)
		pClient->pNetClient = pNetClients->RegClient(pClient);
}

C4Client *C4ClientList::getClientByID(int32_t iID) const
{
	for (C4Client *pClient = pFirst; pClient; pClient = pClient->pNext)
		if (pClient->getID() == iID)
			return pClient;
	return nullptr;
}

C4Client *C4ClientList::getClientByName(const char *szName) const
{
	for (C4Client *pClient = pFirst; pClient; pClient = pClient->pNext)
		if (SEqual(pClient->getName(), szName))
			return pClient;
	return nullptr;
}

int32_t C4ClientList::getClientCnt() const
{
	int32_t iCnt = 0;
	for (C4Client *pClient = pFirst; pClient; pClient = pClient->pNext)
		iCnt++;
	return iCnt;
}

bool C4ClientList::Init(int32_t iLocalClientID)
{
	Clear();
	// Add local client (activated, not observing)
	AddLocal(iLocalClientID, true, false);
	return true;
}

void C4ClientList::InitNetwork(C4Network2ClientList *pnNetClients)
{
	// clear list before
	pnNetClients->Clear();
	// set
	pNetClients = pnNetClients;
	// copy client list, create links
	for (C4Client *pClient = pFirst; pClient; pClient = pClient->pNext)
		pClient->pNetClient = pNetClients->RegClient(pClient);
}

void C4ClientList::ClearNetwork()
{
	// clear the list (should remove links)
	C4Network2ClientList *pOldNetClients = pNetClients;
	pNetClients = nullptr;
	if (pOldNetClients) pOldNetClients->Clear();
}

bool C4ClientList::Remove(C4Client *pClient, bool fTemporary)
{
	// first client in list?
	if (pClient == pFirst)
		pFirst = pClient->pNext;
	else
	{
		// find previous
		C4Client *pPrev;
		for (pPrev = pFirst; pPrev && pPrev->pNext != pClient; pPrev = pPrev->pNext);
		// not found?
		if (!pPrev) return false;
		// unlink
		pPrev->pNext = pClient->pNext;
	}
	if (pClient == pLocal) pLocal = nullptr;
	if (!fTemporary)
	{
		pClient->Remove();
		// remove network client
		if (pNetClients && pClient->getNetClient())
			pNetClients->DeleteClient(pClient->getNetClient());
	}
	// done
	return true;
}

C4Client *C4ClientList::Add(const C4ClientCore &Core)
{
	// client with same ID in list?
	if (getClientByID(Core.getID()))
	{
		Log("ClientList: Duplicated client ID!"); return nullptr;
	}
	// create, add
	C4Client *pClient = new C4Client(Core);
	Add(pClient);
	return pClient;
}

C4Client *C4ClientList::AddLocal(int32_t iID, bool fActivated, bool fObserver)
{
	// only one local client allowed
	assert(!pLocal);
	// create core
	C4ClientCore LocalCore;
	LocalCore.SetLocal(iID, fActivated, fObserver);
	// create client
	C4Client *pClient = new C4Client(LocalCore);
	pClient->SetLocal();
	// add
	Add(pClient);
	assert(pLocal);
	return pClient;
}

void C4ClientList::SetLocalID(int32_t iID)
{
	if (!pLocal) return;
	pLocal->SetID(iID);
	// resort local client
	C4Client *pSavedLocal = pLocal;
	Remove(pLocal, true); Add(pSavedLocal);
}

void C4ClientList::CtrlRemove(const C4Client *pClient, const char *szReason)
{
	// host only
	if (!pLocal || !pLocal->isHost()) return;
	// Net client? flag
	if (pClient->getNetClient())
		pClient->getNetClient()->SetStatus(NCS_Remove);
	// add control
	Game.Control.DoInput(CID_ClientRemove,
		new C4ControlClientRemove(pClient->getID(), szReason),
		CDT_Sync);
}

void C4ClientList::RemoveRemote()
{
	// remove all remote clients
	for (C4Client *pClient = pFirst, *pNext; pClient; pClient = pNext)
	{
		pNext = pClient->pNext;
		// remove all clients except the local client
		if (!pClient->isLocal())
		{
			Remove(pClient);
			delete pClient;
		}
	}
}

C4ClientList &C4ClientList::operator=(const C4ClientList &List)
{
	// remove clients that are not in the list
	C4Client *pClient, *pNext;
	for (pClient = pFirst; pClient; pClient = pNext)
	{
		pNext = pClient->pNext;
		if (!List.getClientByID(pClient->getID()))
		{
			Remove(pClient);
			delete pClient;
		}
	}
	// add all clients
	for (pClient = List.pFirst; pClient; pClient = pClient->pNext)
	{
		// already in list?
		C4Client *pNewClient = getClientByID(pClient->getID());
		if (pNewClient)
		{
			// core change
			pNewClient->SetCore(pClient->getCore());
		}
		else
		{
			// add
			pNewClient = Add(pClient->getCore());
		}
	}
	return *this;
}

void C4ClientList::CompileFunc(StdCompiler *pComp)
{
	// Clear existing data
	bool fCompiler = pComp->isCompiler();
	if (fCompiler) Clear();
	// Client count
	uint32_t iClientCnt = getClientCnt();
	pComp->Value(mkNamingCountAdapt(iClientCnt, "Client"));
	// Compile all clients
	if (pComp->isCompiler())
		for (uint32_t i = 0; i < iClientCnt; i++)
		{
			C4Client *pClient = new C4Client();
			pComp->Value(mkNamingAdapt(*pClient, "Client"));
			Add(pClient);
		}
	else
		for (C4Client *pClient = pFirst; pClient; pClient = pClient->pNext)
			pComp->Value(mkNamingAdapt(*pClient, "Client"));
}
