/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// sv_bot.c

#include "server.h"
#include "sv_server.h"
#include "sv_client.h"
#include "../game/game.h"
//#include "../botlib/botlib.h"


//extern botlib_export_t	*botlib_export;
int	bot_enable;




/*
==================
BotImport_GetMemory
==================
*/
void *BotImport_GetMemory(int size) 
{
	void *ptr;

	ptr = Z_TagMalloc( size, TAG_BOTLIB );
	return ptr;
}

/*
==================
BotImport_FreeMemory
==================
*/
void BotImport_FreeMemory(void *ptr) 
{
	Z_Free(ptr);
}

/*
=================
BotImport_HunkAlloc
=================
*/
void *BotImport_HunkAlloc( int size )
{
	if( Hunk_CheckMark() ) {
		Com_Error( ERR_DROP, "SV_Bot_HunkAlloc: Alloc with marks already set\n" );
	}
	return Hunk_Alloc( size, h_high );
}


/*
==================
SV_AddBot

Allocate a real client slot for an AI bot and bring it into the world as a
vehicle. Mirrors the accept path of SV_DirectConnect, but with an NA_BOT
netchan and no network handshake (bots never get snapshots sent).

Added for the bot-AI integration (PR #1): MFQ3's engine bot support was
stubbed out, so the PR's AI had no real client/playerState to drive. This
provides one. Returns the client number, or -1 on failure.

vehicleIndex: index into availableVehicles[] (read back via the
              "cg_nextVehicle" userinfo key in MF_ClientSpawn)
team:         1 = red, 2 = blue
==================
*/
int SV_AddBot( int vehicleIndex, int team, const char *name )
{
	SV_Client	*newcl = 0;
	int			clientNum;
	char		userinfo[MAX_INFO_STRING];
	netadr_t	botaddr;
	usercmd_t	cmd;
	char		*denied;

	// find a free client slot (clients are 1-indexed in this fork)
	for( int i = 1; i <= sv_maxclients->integer; i++ )
	{
		SV_Client* cl = theSVS.svClients_.at(i);
		if( !cl )
		{
			newcl = new SV_Client;
			newcl->clientNum_ = i;
			break;
		}
		if( cl->state_ == SV_Client::CS_FREE )
		{
			newcl = cl;
			newcl->clientNum_ = i;
			break;
		}
	}
	if( !newcl )
	{
		Com_Printf( "SV_AddBot: no free client slots (max %d)\n", sv_maxclients->integer );
		return -1;
	}

	clientNum = newcl->clientNum_;
	newcl->clear();
	newcl->clientNum_ = clientNum;
	theSVS.svClients_.at( clientNum ) = newcl;

	// set up a bot netchan (no real network address)
	memset( &botaddr, 0, sizeof(botaddr) );
	botaddr.type = NA_BOT;
	Netchan_Setup( NS_SERVER, &newcl->netchan_, botaddr, clientNum );
	newcl->netchan_end_queue_ = &newcl->netchan_start_queue_;

	// build the bot userinfo (vehicle + team are what MF_ClientSpawn reads)
	userinfo[0] = 0;
	Info_SetValueForKey( userinfo, "name", (name && name[0]) ? name : "Drone" );
	Info_SetValueForKey( userinfo, "cg_nextVehicle", va( "%d", vehicleIndex ) );
	Info_SetValueForKey( userinfo, "cg_vehicle", va( "%d", vehicleIndex ) );
	Info_SetValueForKey( userinfo, "team", (team == 2) ? "blue" : "red" );
	Info_SetValueForKey( userinfo, "ip", "localhost" );
	Info_SetValueForKey( userinfo, "handicap", "100" );
	Info_SetValueForKey( userinfo, "rate", "25000" );
	Info_SetValueForKey( userinfo, "snaps", "20" );
	Q_strncpyz( newcl->userinfo_, userinfo, sizeof(newcl->userinfo_) );

	// let the game accept the connection (isBot = true)
	denied = theSG.clientConnect( clientNum, true, true );
	if( denied )
	{
		Com_Printf( "SV_AddBot: game rejected bot: %s\n", denied );
		newcl->state_ = SV_Client::CS_FREE;
		return -1;
	}

	SV_UserinfoChanged( newcl );

	newcl->state_ = SV_Client::CS_CONNECTED;
	newcl->nextSnapshotTime_ = theSVS.time_;
	newcl->lastPacketTime_ = theSVS.time_;
	newcl->lastConnectTime_ = theSVS.time_;

	// MF_ClientConnect clears cg_nextVehicle/cg_vehicle to -1 and writes it back.
	// A bot has no cgame to re-pick a vehicle from the menu, so re-assert the
	// chosen one here, after connect but before begin/spawn -- otherwise
	// MF_ClientSpawn reads -1 and spawns an invisible vehicle-less spectator.
	SV_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );
	Info_SetValueForKey( userinfo, "cg_nextVehicle", va( "%d", vehicleIndex ) );
	Info_SetValueForKey( userinfo, "cg_vehicle", va( "%d", vehicleIndex ) );
	SV_SetUserinfo( clientNum, userinfo );

	// bring it into the world now (sets CS_ACTIVE + calls clientBegin -> spawns the vehicle)
	memset( &cmd, 0, sizeof(cmd) );
	SV_ClientEnterWorld( newcl, &cmd );

	Com_Printf( "SV_AddBot: bot client %d spawned (vehicle %d, team %d)\n", clientNum, vehicleIndex, team );
	return clientNum;
}


