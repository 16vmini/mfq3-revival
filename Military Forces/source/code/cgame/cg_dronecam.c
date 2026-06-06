/*
 * MFQ3 drone camera ("DRONE CAM") - a picture-in-picture drone feed rendered as
 * a second viewport.
 *
 * MVP scope (per design): a SINGLE drone, a fixed straight-down (nadir) view
 * regardless of the drone's heading. For now the camera is anchored high above
 * the player so we can see the feed working without a spawned drone entity;
 * re-anchoring to a real friendly drone later is a one-line change (vieworg).
 *
 * Modeled on CG_HUD_Camera (cg_drawnewhud.c) - the engine's proven
 * second-RenderScene path.
 */

#include "cg_local.h"

#define DRONECAM_ALT	1800.0f		// how high above the anchor the camera sits
#define DRONECAM_FOV	55.0f

static bool	s_droneCamOn = false;

void CG_DroneCam_Clear( void )
{
	s_droneCamOn = false;
}

// console command "dronecam" - toggle the feed on/off
void CG_DroneCam_Toggle( void )
{
	s_droneCamOn = !s_droneCamOn;
	CG_Printf( s_droneCamOn ? "Drone cam: ON\n" : "Drone cam: OFF\n" );
}

// Drawn each frame from CG_Draw2D. Renders a nadir view from high above the
// anchor into a corner box (the "drone feed"), then overlays feed chrome.
void CG_DroneCam_Draw( void )
{
	refdef_t	cam;
	vec3_t		camAngles;
	float		x, y, w, h;		// 640x480 virtual coords for the feed image
	float		rx, ry, rw, rh;	// real pixels for the sub-render
	vec4_t		green = { 0.10f, 0.90f, 0.30f, 0.85f };	// drone-feed green
	vec4_t		black = { 0.00f, 0.00f, 0.00f, 1.00f };

	if( !s_droneCamOn )
		return;
	if( !cg.snap )					// no live view yet
		return;

	// feed image rectangle (top-right corner)
	x = 470; y = 26; w = 150; h = 150;

	// panel: black backing + green border, with an 18px label strip below
	CG_FillRect( x - 3, y - 3, w + 6, h + 21, black );
	CG_FillRect( x - 3, y - 3,        w + 6, 2, green );
	CG_FillRect( x - 3, y + h + 16,   w + 6, 2, green );
	CG_FillRect( x - 3, y - 3,        2, h + 21, green );
	CG_FillRect( x + w + 1, y - 3,    2, h + 21, green );

	// build the sub-viewport refdef (mirror CG_HUD_Camera)
	memcpy( &cam, &cg.refdef, sizeof( cam ) );
	rx = x; ry = y; rw = w; rh = h;
	CG_AdjustFrom640( &rx, &ry, &rw, &rh );
	cam.x = (int)rx; cam.y = (int)ry; cam.width = (int)rw; cam.height = (int)rh;
	cam.fov_x = DRONECAM_FOV;
	cam.fov_y = DRONECAM_FOV;

	// straight down, regardless of drone heading (MVP)
	VectorSet( camAngles, 90, 0, 0 );
	AnglesToAxis( camAngles, cam.viewaxis );

	// MVP anchor: high above the player. (Later: the friendly drone's origin.)
	VectorCopy( cg.predictedPlayerEntity.lerpOrigin, cam.vieworg );
	cam.vieworg[2] += DRONECAM_ALT;

	// populate + render the second view (same sequence the MFD camera uses)
	cg.drawingMFD = true;
	CG_AddPacketEntities();
	CG_AddMarks();
	CG_AddLocalEntities();
	refExport.RenderScene( &cam );
	cg.drawingMFD = false;

	// feed chrome (2D overlay)
	CG_DrawBigStringColor( (int)(x + 2), (int)(y + h + 1), "DRONE 01", green );
	// centre reticle
	CG_FillRect( x + w/2 - 6, y + h/2, 12, 1, green );
	CG_FillRect( x + w/2, y + h/2 - 6, 1, 12, green );
}
