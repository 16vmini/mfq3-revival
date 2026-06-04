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
static char	s_meMessage[128]= "";	// optional .mis flavour line

// fly-through gate marker state (waypoint missions)
static bool			s_gateActive	= false;
static vec3_t		s_gateOrigin;
static float		s_gateRadius	= 300.0f;
static int			s_gateHit		= 0;
static int			s_gateTotal		= 0;
static qhandle_t	s_gateModel		= 0;

void CG_MissionEnd_Clear( void )
{
	s_meActive		= false;
	s_meSuccess		= false;
	s_mePrimaryDone	= 0;
	s_mePrimaryTotal= 0;
	s_meBonusDone	= 0;
	s_meBonusTotal	= 0;
	s_meMessage[0]	= 0;
	s_gateActive	= false;
	s_gateHit		= 0;
	s_gateTotal		= 0;
}

// the .mis "success"/"failure" line; arrives just before mission_end
void CG_MissionEnd_SetText( const char* msg )
{
	Q_strncpyz( s_meMessage, msg ? msg : "", sizeof(s_meMessage) );
}


// --- fly-through gate marker (waypoint missions) --------------------
// The server broadcasts the current target gate via the "mission_gate" command.
// We draw a big spinning marker at it (a placeholder ball for now) so the player
// has something to fly through; passing it is detected server-side.
void CG_MissionGate_Set( float x, float y, float z, float radius, int hit, int total )
{
	s_gateOrigin[0]	= x;
	s_gateOrigin[1]	= y;
	s_gateOrigin[2]	= z;
	s_gateRadius	= radius;
	s_gateHit		= hit;
	s_gateTotal		= total;
	s_gateActive	= ( total > 0 );
}

// HUD target box for the current gate: project its world position to the screen
// and draw a box + distance, so the player can see where to fly even though the
// world model wouldn't render. Pairs with the radar blip.
void CG_DrawMissionGate2D( void )
{
	vec3_t	gate, d;
	int		gx, gy;
	vec4_t	yellow = { 1.0f, 0.9f, 0.0f, 1.0f };

	if( !CG_MissionGate_Get( gate ) )
		return;

	if( CG_WorldToScreenCoords( gate, &gx, &gy, true ) )
	{
		char	lbl[32];
		float	dist, half;
		vec3_t	edge;
		int		ex, ey;

		VectorSubtract( gate, cg.refdef.vieworg, d );
		dist = VectorLength( d );

		// Fade the box out as we close in, so it dissolves instead of plastering
		// over the plane (a 2D overlay can't be occluded by the 3D model). Full
		// from far, gone by ~1.5x the gate radius.
		{
			float near = s_gateRadius * 1.5f;
			float far  = s_gateRadius * 4.0f;
			if( dist <= near ) return;
			if( dist < far )
				yellow[3] = ( dist - near ) / ( far - near );
		}

		// size the box to the gate's REAL radius: project a point one radius to
		// the screen-right of the centre; the pixel gap is the projected radius,
		// so the box grows naturally as you close in.
		VectorMA( gate, s_gateRadius, cg.refdef.viewaxis[1], edge );
		if( CG_WorldToScreenCoords( edge, &ex, &ey, true ) )
			half = (float)abs( ex - gx );
		else
			half = 26.0f;
		if( half < 10.0f )  half = 10.0f;
		if( half > 240.0f ) half = 240.0f;

		CG_DrawRect( (float)gx - half, (float)gy - half, half * 2, half * 2, 2, yellow );
		Com_sprintf( lbl, sizeof(lbl), "GATE  %d", (int)dist );
		CG_DrawSmallStringColor( gx - 26, (int)( gy + half + 4 ), lbl, yellow );
	}
}

// Registered at load time (CG_RegisterGraphics) - mid-frame registration returns
// a handle but doesn't actually load the model, so it drew nothing.
void CG_MissionGate_RegisterMedia( void )
{
	s_gateModel = refExport.RegisterModel( "models/mapobjects/GR_trees/tree1.md3" );
}

// expose the current target gate position (for the radar blip). Returns false if
// there's no active course or all gates are cleared.
bool CG_MissionGate_Get( vec3_t out )
{
	if( !s_gateActive || s_gateTotal <= 0 || s_gateHit >= s_gateTotal )
		return false;
	VectorCopy( s_gateOrigin, out );
	return true;
}

// (Reserved) world-space gate model. Adding a refEntity here did not render in
// this fork's pipeline, so gate guidance is done with the 2D HUD box + radar
// blip instead. Kept as a stub for when a custom ring model is wired up.
void CG_AddMissionGate( void )
{
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

	// optional .mis flavour line ("Well done, you took off!")
	if( s_meMessage[0] )
	{
		CG_DrawSmallStringColor( (SCREEN_WIDTH - (int)strlen(s_meMessage) * SMALLCHAR_WIDTH) / 2, y,
			s_meMessage, colorYellow );
		y += SMALLCHAR_HEIGHT + 16;
	}

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
