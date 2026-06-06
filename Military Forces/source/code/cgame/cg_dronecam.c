/*
 * MFQ3 drone camera ("DRONE CAM") - a picture-in-picture drone feed rendered as
 * a second viewport (straight-down / nadir view).
 *
 * Commands:
 *   dronecam            toggle the feed on/off (keeps current target)
 *   dronecam <n>        show drone #n (from "listdrones"); turns the feed on
 *   dronecam off|0      turn the feed off
 *   listdrones          list friendly ("your team") drones you can view
 *
 * If no valid drone is selected it falls back to an overhead view above the
 * player, so the feed is testable before real drone entities exist.
 *
 * RENDER NOTE: the 3D sub-render (CG_DroneCam_Render) runs in the 3D pass right
 * after the main RenderScene, reusing the scene CG_DrawActiveFrame already
 * built. It must NOT re-call CG_AddPacketEntities (that rewrites the predicted
 * player entity / frame-interp globals and stalls movement). The 2D chrome
 * (CG_DroneCam_Draw) only frames the box.
 */

#include "cg_local.h"

#define DRONECAM_X		470.0f		// feed box, 640x480 virtual coords (top-right)
#define DRONECAM_Y		26.0f
#define DRONECAM_W		150.0f
#define DRONECAM_H		150.0f
#define DRONECAM_ALT	1800.0f			// overhead-fallback height (when no real drone)
#define DRONECAM_FOV_DEFAULT	18.0f	// default lens (deg): low = telephoto/zoom, high = wide
#define DRONECAM_MAXLIST	16

static bool	s_droneCamOn   = false;
static int	s_droneTarget  = -1;	// entity number to view, -1 = overhead-above-player
static int	s_droneIndex   = 1;		// 1-based list number (for the label)
static float	s_droneFov = DRONECAM_FOV_DEFAULT;	// lens FOV (deg); set via "dronezoom"

void CG_DroneCam_Clear( void )
{
	s_droneCamOn  = false;
	s_droneTarget = -1;
}

// Is this entity a "drone" we may view (a friendly aircraft/vehicle, not us)?
static bool CG_IsViewableDrone( int num )
{
	centity_t*		c;
	entityState_t*	s;

	if( num < 0 || num >= MAX_GENTITIES )		return false;
	if( !cg.snap || num == cg.snap->ps.clientNum )	return false;

	c = &cg_entities[num];
	if( !c->currentValid )						return false;
	s = &c->currentState;

	if( s->eType != ET_VEHICLE && s->eType != ET_MISC_VEHICLE )
		return false;

	// team filter only matters in team games; in FFA show all non-self flyers
	if( cgs.gametype >= GT_TEAM &&
		s->generic1 != cg.snap->ps.persistant[PERS_TEAM] )
		return false;

	return true;
}

// Collect viewable drones into list[]; returns the count.
static int CG_GatherDrones( int* list, int maxList )
{
	int n = 0, num;
	for( num = 0; num < MAX_GENTITIES && n < maxList; num++ )
	{
		if( CG_IsViewableDrone( num ) )
			list[n++] = num;
	}
	return n;
}

// console: list friendly drones
void CG_ListDrones( void )
{
	int	list[DRONECAM_MAXLIST];
	int	count, i;

	if( !cg.snap ) { CG_Printf( "Not in game.\n" ); return; }

	count = CG_GatherDrones( list, DRONECAM_MAXLIST );
	if( !count ) { CG_Printf( "No friendly drones in view.\n" ); return; }

	CG_Printf( "Friendly drones (use \\dronecam <n>):\n" );
	for( i = 0; i < count; i++ )
	{
		entityState_t*	s = &cg_entities[ list[i] ].currentState;
		const char*		name = "vehicle";
		if( s->modelindex >= 0 && s->modelindex < bg_numberOfVehicles )
			name = availableVehicles[ s->modelindex ].descriptiveName;
		CG_Printf( "  %d: %s (ent %d)\n", i + 1, name, list[i] );
	}
}

// console: "dronecam", "dronecam <n>", "dronecam off"
void CG_DroneCam_Cmd( void )
{
	if( Cmd_Argc() < 2 )
	{
		s_droneCamOn = !s_droneCamOn;			// bare toggle
	}
	else
	{
		const char* a = CG_Argv( 1 );
		if( !Q_stricmp( a, "off" ) || !Q_stricmp( a, "0" ) )
		{
			s_droneCamOn = false;
		}
		else
		{
			int	list[DRONECAM_MAXLIST];
			int	count = CG_GatherDrones( list, DRONECAM_MAXLIST );
			int	n = atoi( a );
			if( n >= 1 && n <= count )
			{
				s_droneTarget = list[n - 1];
				s_droneIndex  = n;
				CG_Printf( "Drone cam -> #%d\n", n );
			}
			else
			{
				s_droneTarget = -1;				// overhead fallback
				CG_Printf( "No drone #%d (try \\listdrones) - showing overhead.\n", n );
			}
			s_droneCamOn = true;
		}
	}
	CG_Printf( s_droneCamOn ? "Drone cam: ON\n" : "Drone cam: OFF\n" );
}

// console: dronezoom <degrees> - set the drone cam lens (low = telephoto/zoom in, high = wide)
void CG_DroneZoom_Cmd( void )
{
	if( Cmd_Argc() < 2 )
	{
		CG_Printf( "Drone cam lens: %.0f deg  (usage: dronezoom <5-120>; low=zoom in, high=wide)\n", s_droneFov );
		return;
	}
	s_droneFov = atof( CG_Argv( 1 ) );
	if( s_droneFov < 5.0f )   s_droneFov = 5.0f;
	if( s_droneFov > 120.0f ) s_droneFov = 120.0f;
	CG_Printf( "Drone cam lens -> %.0f deg\n", s_droneFov );
}

// On + the player is alive and flying/driving (not dead, selecting, spectating).
// Auto-switches OFF on death so the feed doesn't linger through respawn.
static bool CG_DroneCam_ShouldShow( void )
{
	if( !s_droneCamOn )		return false;
	if( !cg.snap )			return false;

	if( cg.snap->ps.pm_type == PM_DEAD ||
		cg.snap->ps.stats[STAT_HEALTH] <= 0 ||
		( cg.snap->ps.pm_flags & PMF_VEHICLESELECT ) ||
		cg.snap->ps.persistant[PERS_TEAM] == ClientBase::TEAM_SPECTATOR )
	{
		s_droneCamOn = false;
		return false;
	}
	return true;
}

// where the camera looks down from: the selected drone, else above the player
static void CG_DroneCam_Anchor( vec3_t out )
{
	if( s_droneTarget >= 0 && CG_IsViewableDrone( s_droneTarget ) )
	{
		// look DOWN from the drone itself, so the feed shows the GROUND beneath
		// it (real ISR view) - not the drone seen from far above
		VectorCopy( cg_entities[ s_droneTarget ].lerpOrigin, out );
		out[2] -= 64.0f;	// just under the belly so the drone's own model doesn't fill the lens
	}
	else
	{
		// no real drone selected: float a camera high above the player
		VectorCopy( cg.predictedPlayerEntity.lerpOrigin, out );
		out[2] += DRONECAM_ALT;
	}
}

// 3D pass: render the drone's nadir view into the corner box, reusing the
// already-built scene (called right after the main RenderScene).
void CG_DroneCam_Render( void )
{
	refdef_t	cam;
	vec3_t		camAngles;
	float		rx, ry, rw, rh;

	if( !CG_DroneCam_ShouldShow() )
		return;

	memcpy( &cam, &cg.refdef, sizeof( cam ) );

	rx = DRONECAM_X; ry = DRONECAM_Y; rw = DRONECAM_W; rh = DRONECAM_H;
	CG_AdjustFrom640( &rx, &ry, &rw, &rh );
	cam.x = (int)rx; cam.y = (int)ry; cam.width = (int)rw; cam.height = (int)rh;
	cam.fov_x = s_droneFov;
	cam.fov_y = s_droneFov;

	// straight down, regardless of drone heading (MVP)
	VectorSet( camAngles, 90, 0, 0 );
	AnglesToAxis( camAngles, cam.viewaxis );

	CG_DroneCam_Anchor( cam.vieworg );

	// re-populate the scene for this viewpoint (the main RenderScene consumed it)
	// - same sequence the MFD camera uses, so entities appear in the feed
	cg.drawingMFD = true;
	cg.drawingDroneCam = true;		// show the player's own vehicle in this view (unlike MFDs)
	CG_AddPacketEntities();
	CG_AddMarks();
	CG_AddLocalEntities();
	refExport.RenderScene( &cam );
	cg.drawingDroneCam = false;
	cg.drawingMFD = false;
}

// 2D pass: frame the feed box (green border + label + reticle). No 3D render here.
void CG_DroneCam_Draw( void )
{
	float	x = DRONECAM_X, y = DRONECAM_Y, w = DRONECAM_W, h = DRONECAM_H;
	vec4_t	green = { 0.10f, 0.90f, 0.30f, 0.85f };
	char	label[24];

	if( !CG_DroneCam_ShouldShow() )
		return;

	CG_FillRect( x - 2, y - 2, w + 4, 2, green );
	CG_FillRect( x - 2, y + h, w + 4, 2, green );
	CG_FillRect( x - 2, y - 2, 2, h + 4, green );
	CG_FillRect( x + w, y - 2, 2, h + 4, green );

	if( s_droneTarget >= 0 && CG_IsViewableDrone( s_droneTarget ) )
		Com_sprintf( label, sizeof(label), "DRONE %d", s_droneTarget );
	else
		Q_strncpyz( label, "DRONE 01", sizeof(label) );
	CG_DrawBigStringColor( (int)(x + 3), (int)(y + 3), label, green );

	CG_FillRect( x + w/2 - 6, y + h/2, 12, 1, green );
	CG_FillRect( x + w/2, y + h/2 - 6, 1, 12, green );
}
