/*
 * MFQ3 missions: end-of-mission banner ("MISSION COMPLETE" / "MISSION FAILED").
 *
 * Drawn over the end-of-level intermission. The server reports the outcome via
 * the "mission_end" server command (handled in cg_servercmds.c):
 *   mission_end <success> <primaryDone> <primaryTotal> <bonusDone> <bonusTotal>
 * State is module-local and reset on CG_Init.
 */

#include "cg_local.h"

static bool	s_meActive		= false;
static bool	s_meSuccess		= false;
static int	s_mePrimaryDone	= 0;
static int	s_mePrimaryTotal= 0;
static int	s_meBonusDone	= 0;
static int	s_meBonusTotal	= 0;

void CG_MissionEnd_Clear( void )
{
	s_meActive		= false;
	s_meSuccess		= false;
	s_mePrimaryDone	= 0;
	s_mePrimaryTotal= 0;
	s_meBonusDone	= 0;
	s_meBonusTotal	= 0;
}

void CG_MissionEnd_Set( bool success, int primaryDone, int primaryTotal, int bonusDone, int bonusTotal )
{
	s_meSuccess		= success;
	s_mePrimaryDone	= primaryDone;
	s_mePrimaryTotal= primaryTotal;
	s_meBonusDone	= bonusDone;
	s_meBonusTotal	= bonusTotal;
	s_meActive		= true;
}

void CG_DrawMissionEndBanner( void )
{
	vec4_t		backdrop = { 0.0f, 0.0f, 0.0f, 0.55f };
	vec4_t		green    = { 0.2f, 1.0f, 0.3f, 1.0f };
	vec4_t		red      = { 1.0f, 0.25f, 0.2f, 1.0f };
	char		line[80];
	const char*	title;
	int			y = 56;

	if( !s_meActive )
		return;

	title = s_meSuccess ? "MISSION COMPLETE" : "MISSION FAILED";

	// dark band behind the banner
	CG_FillRect( 0, (float)(y - 12), SCREEN_WIDTH, 110, backdrop );

	// centered title, green for success / red for failure
	CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(title) * BIGCHAR_WIDTH) / 2, y,
		title, s_meSuccess ? green : red );
	y += BIGCHAR_HEIGHT + 16;

	// primary objectives
	Com_sprintf( line, sizeof(line), "Primary objectives  %d / %d", s_mePrimaryDone, s_mePrimaryTotal );
	CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(line) * BIGCHAR_WIDTH) / 2, y, line, colorWhite );
	y += BIGCHAR_HEIGHT + 6;

	// bonus targets
	Com_sprintf( line, sizeof(line), "Bonus targets  %d / %d", s_meBonusDone, s_meBonusTotal );
	CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(line) * BIGCHAR_WIDTH) / 2, y, line, colorWhite );
}
