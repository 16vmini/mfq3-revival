/*
 * MFQ3 missions: "Mission Complete" banner.
 *
 * Drawn over the end-of-level intermission when the server reports that all
 * primary objectives were destroyed (the "mission_complete" server command,
 * handled in cg_servercmds.c). State is module-local and reset on CG_Init.
 */

#include "cg_local.h"

// client-side mission result (set by the server's "mission_complete" command)
static bool	s_mcActive		= false;
static int	s_mcPrimary		= 0;	// primary objectives (all destroyed when shown)
static int	s_mcBonusDone	= 0;
static int	s_mcBonusTotal	= 0;

void CG_MissionComplete_Clear( void )
{
	s_mcActive		= false;
	s_mcPrimary		= 0;
	s_mcBonusDone	= 0;
	s_mcBonusTotal	= 0;
}

void CG_MissionComplete_Set( int primary, int bonusDone, int bonusTotal )
{
	s_mcPrimary		= primary;
	s_mcBonusDone	= bonusDone;
	s_mcBonusTotal	= bonusTotal;
	s_mcActive		= true;
}

void CG_DrawMissionCompleteBanner( void )
{
	vec4_t		backdrop = { 0.0f, 0.0f, 0.0f, 0.55f };
	char		line[80];
	const char*	title = "MISSION COMPLETE";
	int			y = 56;

	if( !s_mcActive )
		return;

	// dark band behind the banner
	CG_FillRect( 0, (float)(y - 12), SCREEN_WIDTH, 110, backdrop );

	// centered gold title
	CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(title) * BIGCHAR_WIDTH) / 2, y, title, colorYellow );
	y += BIGCHAR_HEIGHT + 16;

	// primary objectives (all destroyed by definition when this shows)
	Com_sprintf( line, sizeof(line), "Primary objectives  %d / %d", s_mcPrimary, s_mcPrimary );
	CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(line) * BIGCHAR_WIDTH) / 2, y, line, colorWhite );
	y += BIGCHAR_HEIGHT + 6;

	// bonus targets
	Com_sprintf( line, sizeof(line), "Bonus targets  %d / %d", s_mcBonusDone, s_mcBonusTotal );
	CG_DrawBigStringColor( (SCREEN_WIDTH - (int)strlen(line) * BIGCHAR_WIDTH) / 2, y, line, colorWhite );
}
