/*
 * g_bot.h — Bot AI header for MFQ3
 *
 * All structures, enums, and declarations for the drone/bot AI system.
 * This is the single include needed by any file using the bot system.
 *
 * Written as new code (not a modification of existing MFQ3 source).
 * References original commented-out code where logic was adapted from.
 */

#ifndef __G_BOT_H__
#define __G_BOT_H__

#include "g_local.h"
#include "g_level.h"   /* full GameEntity / LevelLocals defs (not just fwd-decls) */

/* Compat: this MFQ3 fork dropped Quake's classic qboolean in favour of C++ bool.
   The bot AI (PR #1) was written against the stock Quake type, so shim it here. */
#ifndef MFQ3_QBOOLEAN_COMPAT
#define MFQ3_QBOOLEAN_COMPAT
typedef bool qboolean;
#define qtrue  true
#define qfalse false
#endif

/*
=============================================================================
  CONSTANTS
=============================================================================
*/

/* Maximum bots active at once */
#define MAX_BOTS				32

/* Maximum waypoints in the global list */
#define MAX_BOT_WAYPOINTS		2048

/* Maximum script tasks for waypoint events */
#define MAX_BOT_SCRIPT_TASKS	1024

/* How often (ms) each bot think function runs — matches FRAMETIME */
#define BOT_THINK_INTERVAL		100

/* Scan radius for target acquisition (world units) */
#define BOT_SCAN_RADIUS			4000

/* Distance at which a bot considers itself "at" a waypoint */
#define BOT_WAYPOINT_REACH		128

/* Health threshold (fraction of max) below which bot flees */
#define BOT_FLEE_HEALTH_FRAC	0.25f

/* Angle tolerance (degrees) for firing weapons */
#define BOT_FIRE_ANGLE_TOL		5.0f

/* Obstacle avoidance trace distance */
#define BOT_AVOID_TRACE_DIST	256

/* Combat timer: how long to stay in ATTACK before re-evaluating */
#define BOT_COMBAT_TIMEOUT		5000

/* Flee timer: how long to stay in FLEE before returning to patrol */
#define BOT_FLEE_TIMEOUT		8000

/* Maximum path filename length for waypoint save/load */
#define BOT_MAX_FILEPATH		144


/*
=============================================================================
  WAYPOINT SYSTEM
=============================================================================
*/

/* Waypoint structure — adapted from original waypoint_t in g_local.h */
typedef struct bot_waypoint_s {
	char		name[MAX_NAME_LENGTH];	/* targetname from map entity */
	vec3_t		pos;					/* world position */
	int			index;					/* index in the waypoint list */
	int			nextWaypointIndex;		/* index of the next waypoint in chain (-1 = end) */
} bot_waypoint_t;

/* Global waypoint list — adapted from original waypointList_t */
typedef struct {
	bot_waypoint_t	waypoints[MAX_BOT_WAYPOINTS];
	int				usedWPs;			/* number of waypoints actually used */
} bot_waypointList_t;


/*
=============================================================================
  SCRIPT TASK SYSTEM (for waypoint-triggered actions)
=============================================================================
*/

/* Task types — adapted from original taskTypes_t */
typedef enum {
	BOT_TASK_ON_WAYPOINT
} bot_taskTypes_t;

/* A single script task — adapted from original scripttask_t */
typedef struct bot_scripttask_s {
	bot_taskTypes_t	type;
	int				ownerEntityNum;		/* which bot entity owns this task */
	char			name[MAX_NAME_LENGTH]; /* waypoint name this task triggers on */
	int				nextWaypointIndex;	/* set bot's nextWaypoint to this (-1 = none) */
} bot_scripttask_t;

/* Global script task list — adapted from original scripttaskList_t */
typedef struct {
	bot_scripttask_t	scripts[MAX_BOT_SCRIPT_TASKS];
	int					usedSTs;		/* number of script tasks actually used */
} bot_scripttaskList_t;


/*
=============================================================================
  BOT STATE MACHINE
=============================================================================
*/

/* Bot AI states */
typedef enum {
	BOT_STATE_IDLE,			/* Sitting still, no orders */
	BOT_STATE_PATROL,		/* Following waypoints */
	BOT_STATE_CHASE,		/* Moving toward an enemy target */
	BOT_STATE_ATTACK,		/* Actively firing on a target */
	BOT_STATE_FLEE,			/* Running away (low health) */
	BOT_STATE_RETURN_TO_BASE, /* Head back to start/spawn point */
	BOT_STATE_TAKEOFF,		/* Air vehicles: taking off */
	BOT_STATE_LANDING,		/* Air vehicles: landing */

	BOT_STATE_MAX
} botAIState_t;


/*
=============================================================================
  BOT STATE STRUCTURE
=============================================================================
*/

/* Per-bot runtime state — this is the core of the AI */
typedef struct {
	/* Core state */
	botAIState_t		state;				/* current AI state */
	int				entityNum;			/* entity number in g_entities / theLevel */
	int				team;				/* team: MF_TEAM_1 or MF_TEAM_2 */
	int				vehicleCat;			/* CAT_PLANE, CAT_GROUND, CAT_HELO, CAT_LQM, CAT_BOAT */
	int				vehicleIndex;		/* index into availableVehicles[] */
	qboolean		active;				/* is this bot slot in use? */

	/* Navigation */
	int				currentWaypoint;	/* index into bot_waypoints[], -1 = none */
	int				patrolStartWaypoint;/* first waypoint for patrol loop */
	vec3_t			homeOrigin;			/* spawn/home position */
	vec3_t			homeAngles;			/* spawn/home angles */

	/* Targeting */
	int				targetEntityNum;	/* entity we're targeting, -1 = none */
	float			targetDist;			/* distance to current target */
	int				lastScanTime;		/* level.time of last target scan */
	int				scanInterval;		/* ms between target scans */

	/* Combat */
	int				combatTimer;		/* level.time when combat started */
	int				lastFireTime;		/* level.time of last weapon fire */
	int				fireInterval;		/* ms between shots (weapon-dependent) */
	float			aimSkill;			/* 0.0 - 1.0, affects aim accuracy */

	/* Flee */
	int				fleeTimer;			/* level.time when flee started */

	/* Script */
	int				idxScriptBegin;		/* first script task index for this bot */
	int				idxScriptEnd;		/* last script task index (exclusive) */

	/* Air vehicle specific */
	qboolean		wantsTakeoff;		/* bot wants to take off */
	qboolean		wantsLanding;		/* bot wants to land */
	float			cruiseAltitude;		/* desired flight altitude */

	/* Ground vehicle specific */
	qboolean		wantsStop;			/* bot wants to halt */
	float			stuckTimer;			/* time spent stuck (for unstuck logic) */
	vec3_t			lastPos;			/* position last frame (for stuck detection) */

	/* Infantry specific */
	qboolean		wantsCover;			/* seeking cover */
	vec3_t			coverOrigin;		/* cover position */
} botState_t;


/*
=============================================================================
  GLOBAL BOT STATE
=============================================================================
*/

/* Global list of active bots */
typedef struct {
	botState_t			bots[MAX_BOTS];
	int					numBots;			/* count of active bots */
	bot_waypointList_t	waypointList;		/* global waypoint store */
	bot_scripttaskList_t scriptList;			/* global script task store */
	qboolean			initialized;		/* has the system been initialized? */
} botGlobals_t;

extern botGlobals_t botGlobals;


/*
=============================================================================
  WAYPOINT FUNCTIONS (g_bot_waypoint.c)
=============================================================================
*/

/* Find a waypoint by name. Returns index or -1 if not found. */
int		Bot_FindWaypoint( const char *name );

/* Add a waypoint from a map entity (targetname + origin). */
void	Bot_AddWaypoint( const char *name, vec3_t origin );

/* Link waypoints: resolve target chains so nextWaypointIndex is populated. */
void	Bot_LinkWaypoints( void );

/* Get direction and distance from entity to its current waypoint.
   Stores direction in dir, returns distance. (Adapted from getTargetDirAndDist) */
float	Bot_GetWaypointDirAndDist( botState_t *bs, vec3_t dir );

/* Called when bot reaches a waypoint. Processes script tasks.
   (Adapted from onWaypointEvent) */
void	Bot_OnWaypointEvent( botState_t *bs );

/* Save waypoints to a file */
void	Bot_SaveWaypoints( const char *filename );

/* Load waypoints from a file */
void	Bot_LoadWaypoints( const char *filename );

/* Initialize the waypoint system (call once at map start) */
void	Bot_InitWaypoints( void );


/*
=============================================================================
  CORE AI (g_bot_ai.c)
=============================================================================
*/

/* Initialize the bot system (call once at game init) */
void	Bot_Init( void );

/* Main think dispatcher — routes to vehicle-specific think. */
void	Bot_Think( botState_t *bs );

/* State machine: evaluate and transition states. */
void	Bot_UpdateState( botState_t *bs );

/* Target acquisition: scan for enemies within range. */
int		Bot_AcquireTarget( botState_t *bs );

/* Check if we should flee (low health). */
qboolean Bot_ShouldFlee( botState_t *bs );

/* Combat: decide whether to fire weapons. */
qboolean Bot_ShouldFire( botState_t *bs );

/* Set a bot's state with logging. */
void	Bot_SetState( botState_t *bs, botAIState_t newState );


/*
=============================================================================
  GROUND VEHICLE AI (g_bot_ground.c)
=============================================================================
*/

void	Bot_Ground_Think( botState_t *bs );		/* adapted from Drone_Ground_Think */
void	Bot_Ground_Navigate( botState_t *bs );
void	Bot_Ground_Combat( botState_t *bs );
void	Bot_Ground_AvoidObstacles( botState_t *bs );


/*
=============================================================================
  PLANE AI (g_bot_air.c)
=============================================================================
*/

void	Bot_Plane_Think( botState_t *bs );		/* adapted from Drone_Plane_Think */
void	Bot_Plane_Navigate( botState_t *bs );
void	Bot_Plane_Combat( botState_t *bs );
void	Bot_Plane_Dogfight( botState_t *bs );
void	Bot_Plane_Takeoff( botState_t *bs );
void	Bot_Plane_Landing( botState_t *bs );
void	Bot_Plane_AltitudeManagement( botState_t *bs );


/*
=============================================================================
  HELICOPTER AI (g_bot_helo.c)
=============================================================================
*/

void	Bot_Helo_Think( botState_t *bs );
void	Bot_Helo_Navigate( botState_t *bs );
void	Bot_Helo_Combat( botState_t *bs );
void	Bot_Helo_Takeoff( botState_t *bs );
void	Bot_Helo_Landing( botState_t *bs );


/*
=============================================================================
  BOAT AI (g_bot_boat.c)
=============================================================================
*/

void	Bot_Boat_Think( botState_t *bs );
void	Bot_Boat_Navigate( botState_t *bs );
void	Bot_Boat_Combat( botState_t *bs );


/*
=============================================================================
  INFANTRY / LQM AI (g_bot_infantry.c)
=============================================================================
*/

void	Bot_Infantry_Think( botState_t *bs );
void	Bot_Infantry_Navigate( botState_t *bs );
void	Bot_Infantry_Combat( botState_t *bs );
void	Bot_Infantry_SeekCover( botState_t *bs );


/*
=============================================================================
  CONSOLE COMMANDS (g_bot_cmds.c)
=============================================================================
*/

/* Spawn a bot: bot_add [team] [vehicle_type]
   team: 1 or 2
   vehicle_type: index into availableVehicles[] */
void	Bot_Add_f( void );

/* Remove a bot: bot_remove [entitynum] */
void	Bot_Remove_f( void );

/* Add a waypoint at a specified position: bot_waypoint_add [name] [x] [y] [z] */
void	Bot_WaypointAdd_f( void );

/* Save waypoints: bot_waypoint_save [filename] */
void	Bot_WaypointSave_f( void );

/* Load waypoints: bot_waypoint_load [filename] */
void	Bot_WaypointLoad_f( void );

/* Spawn a circling plane: bot_add_circling [team] [vehicle_type]
   Creates a plane bot that circles around the first connected player. */
void	Bot_AddCircling_f( void );

/* Spawn a stationary ground tank below the player: bot_target */
void	Bot_AddTank_f( void );

/* Per-frame tick: call from G_RunFrame to process all active bots */
void	Bot_Frame( void );

/* Register all bot console commands (call from G_InitGame or similar) */
void	Bot_RegisterCommands( void );


/*
=============================================================================
  SPAWNING HELPERS
=============================================================================
*/

/* Create and spawn a bot entity of the given vehicle type.
   Returns the bot's slot index in botGlobals.bots[], or -1 on failure. */
int		Bot_Spawn( int team, int vehicleIndex, vec3_t origin, vec3_t angles );

/* Remove a bot by slot index */
void	Bot_Remove( int slotIndex );


#endif /* __G_BOT_H__ */