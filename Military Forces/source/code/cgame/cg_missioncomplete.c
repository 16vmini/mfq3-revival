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

// returns true if the banner is active (so the caller can skip the normal
// intermission scoreboard / center string and show only this)
bool CG_DrawMissionEndBanner( void )
{
	vec4_t		backdrop = { 0.0f, 0.0f, 0.0f, 0.65f };
	vec4_t		green    = { 0.2f, 1.0f, 0.3f, 1.0f };
	vec4_t		red      = { 1.0f, 0.25f, 0.2f, 1.0f };
	char		line[80];
	const char*	title;
	int			y = 56;

	if( !s_meActive )
		return false;

	title = s_meSuccess ? "MISSION COMPLETE" : "MISSION FAILED";

	// full-screen dim so the FFA scoreboard behind it doesn't bleed through
	CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, backdrop );

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
	y += BIGCHAR_HEIGHT + 24;

	// restart prompt
	{
		const char* hint = "Press FIRE to restart mission";
		CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(hint) * BIGCHAR_WIDTH) / 2, y, hint, colorYellow );
	}

	return true;
}
