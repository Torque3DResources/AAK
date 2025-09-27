//-----------------------------------------------------------------------------
// 3D Action Adventure Kit for T3D
// Copyright (C) 2008-2013 Ubiq Visuals, Inc. (http://www.ubiqvisuals.com/)
//
// This file also incorporates work covered by the following copyright and  
// permission notice:
//
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// Arcane-FX for MIT Licensed Open Source version of Torque 3D from GarageGames
// Copyright (C) 2015 Faust Logic, Inc.
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
#include "platform/platform.h"
#include "./AAKplayer.h"

#include "platform/profiler.h"
#include "math/mMath.h"
#include "math/mathIO.h"
#include "math/mathUtils.h"
#include "core/resourceManager.h"
#include "core/stringTable.h"
#include "core/volume.h"
#include "core/stream/bitStream.h"
#include "console/consoleTypes.h"
#include "console/engineAPI.h"
#include "collision/extrudedPolyList.h"
#include "collision/clippedPolyList.h"
#include "collision/concretePolyList.h"
#include "collision/earlyOutPolyList.h"
#include "ts/tsShapeInstance.h"
#include "sfx/sfxSystem.h"
#include "sfx/sfxTrack.h"
#include "sfx/sfxSource.h"
#include "sfx/sfxTypes.h"
#include "scene/sceneManager.h"
#include "scene/sceneRenderState.h"
#include "T3D/gameBase/gameConnection.h"
#include "T3D/trigger.h"
#include "T3D/physicalZone.h"
#include "T3D/item.h"
#include "T3D/missionArea.h"
#include "T3D/fx/particleEmitter.h"
#include "T3D/fx/cameraFXMgr.h"
#include "T3D/fx/splash.h"
#include "T3D/tsStatic.h"
#include "T3D/physics/physicsPlugin.h"
#include "T3D/physics/physicsPlayer.h"
#include "T3D/decal/decalManager.h"
#include "T3D/decal/decalData.h"
#include "materials/baseMatInstance.h"
#include "terrain/terrData.h"
#include "gfx/sim/debugDraw.h"
#include "AAKUtils.h"

#ifdef TORQUE_EXTENDED_MOVE
   #include "T3D/gameBase/extended/extendedMove.h"
#endif

#ifdef TORQUE_OPENVR
#include "platform/input/openVR/openVRProvider.h"
#include "platform/input/openVR/openVRTrackedObject.h"
#endif

// Amount we try to stay out of walls by...
static F32 sWeaponPushBack = 0.03f;

// Amount of time if takes to transition to a new action sequence.
static F32 sAnimationTransitionTime = 0.25f;
static bool sUseAnimationTransitions = true;
static F32 sLandReverseScale = 0.25f;
static F32 sStandingJumpSpeed = 2.0f;
static F32 sJumpingThreshold = 4.0f;
static F32 sSlowStandThreshSquared = 1.69f;
static S32 sRenderMyPlayer = true;
static S32 sRenderMyItems = true;
static bool sRenderPlayerCollision = false;
static bool sRenderHelpers = false;

// Chooses new action animations every n ticks.
static const F32 sNewAnimationTickTime = 1.0f;
static const F32 sMountPendingTickWait = 13.0f * F32(TickMs);

// Number of ms before we pick non-contact animations
static const S32 sContactTickTime = TickMs*2.0f;	//64 ms

// Player is kept out of walls by this much during climb/wall/ledge 
static const F32 sSurfaceDistance = 0.05f;

// Movement constants
static F32 sVerticalStepDot = 0.173f;   // 80
static F32 sMinFaceDistance = 0.01f;
static F32 sTractionDistance = 0.03f;
static F32 sNormalElasticity = 0.01f;
static F32 sAirElasticity = 0.35f;
static U32 sMoveRetryCount = 5;
static F32 sMaxImpulseVelocity = 200.0f;

static F32 sMaxVelocity = 10.0f;

// Move triggers
static S32 sJumpTrigger = 2;
static S32 sCrouchTrigger = 3;
static S32 sProneTrigger = 4;
static S32 sSprintTrigger = 5;
static S32 sImageTrigger0 = 0;
static S32 sImageTrigger1 = 1;
static S32 sJumpJetTrigger = 1;
static S32 sVehicleDismountTrigger = 2;

// Client prediction
static F32 sMinWarpTicks = 0.5f;       // Fraction of tick at which instant warp occurs
static S32 sMaxWarpTicks = 3;          // Max warp duration in ticks
static S32 sMaxPredictionTicks = 30;   // Number of ticks to predict

// Anchor point compression
const F32 sAnchorMaxDistance = 32.0f;

//
static U32 sCollisionMoveMask =  TerrainObjectType       |
                                 WaterObjectType         | 
                                 PlayerObjectType        |
                                 StaticShapeObjectType   | 
                                 VehicleObjectType       |
                                 PhysicalZoneObjectType  |
                                 PathShapeObjectType;

static U32 sServerCollisionContactMask = sCollisionMoveMask |
                                         ItemObjectType     |
                                         TriggerObjectType  |
                                         CorpseObjectType;

static U32 sClientCollisionContactMask = sCollisionMoveMask |
                                         TriggerObjectType |
                                         PhysicalZoneObjectType;

enum PlayerConstants {
   JumpSkipContactsMax = 8
};

//----------------------------------------------------------------------------
// Player shape animation sequences:

// Action Animations:
AAKPlayerData::ActionAnimationDef AAKPlayerData::ActionAnimationList[NumTableActionAnims] =
{
   // *** WARNING ***
   // This array is indexed using the enum values defined in player.h

   // Root is the default animation
   { "root" },       // RootAnim,

   // These are selected in the move state based on velocity
   { "run",          { 0.0f, 1.0f, 0.0f } },       // RunForwardAnim,
   { "back",         { 0.0f,-1.0f, 0.0f } },       // BackBackwardAnim
   { "side",         {-1.0f, 0.0f, 0.0f } },       // SideLeftAnim,
   { "side_right",   { 1.0f, 0.0f, 0.0f } },       // SideRightAnim,
   { "walk",         { 0.0f, 1.0f, 0.0f } },       // WalkForwardAnim,

   { "sprint_root" },
   { "sprint_forward",  { 0.0f, 1.0f, 0.0f } },
   { "sprint_backward", { 0.0f,-1.0f, 0.0f } },
   { "sprint_side",     {-1.0f, 0.0f, 0.0f } },
   { "sprint_right",    { 1.0f, 0.0f, 0.0f } },

   { "crouch_root" },
   { "crouch_forward",  { 0.0f, 1.0f, 0.0f } },
   { "crouch_backward", { 0.0f,-1.0f, 0.0f } },
   { "crouch_side",     {-1.0f, 0.0f, 0.0f } },
   { "crouch_right",    { 1.0f, 0.0f, 0.0f } },

   { "prone_root" },
   { "prone_forward",   { 0.0f, 1.0f, 0.0f } },
   { "prone_backward",  { 0.0f,-1.0f, 0.0f } },

   { "swim_root" },
   { "swim_forward",    { 0.0f, 1.0f, 0.0f } },
   { "swim_backward",   { 0.0f,-1.0f, 0.0f }  },
   { "swim_left",       {-1.0f, 0.0f, 0.0f }  },
   { "swim_right",      { 1.0f, 0.0f, 0.0f }  },

   // These are set explicitly based on player actions
   { "fall" },       // FallAnim
   { "jump" },       // JumpAnim
   { "standjump" },  // StandJumpAnim
   { "land" },       // StandingLandAnim
   { "jumpland" },   // RunningLandAnim
   { "jet" },        // JetAnim

	//Ubiq:
	{"death1"},			//Death1Anim

	{"stop"},		//StopAnim

	{"wallidle"},		//WallIdleAnim
	{"wallleft"},		//WallLeftAnim
	{"wallright"},		//WallRightAnim

	{ "ledgeidle" },	// LedgeIdleAnim
	{ "ledgeleft" },	// LedgeLeftAnim
	{ "ledgeright" },	// LedgeRightAnim
	{ "ledgeup" },		// LedgeUpAnim

	{ "climbidle" },	// ClimbIdleAnim
	{ "climbup" },		// ClimbUpAnim
	{ "climbdown" },	// ClimbDownAnim
	{ "climbleft" },	// ClimbLeftAnim
	{ "climbright" },	// ClimbRightAnim

	{ "slidefront" },   // SlideFrontAnim
	{ "slideback" },	// SlideBackAnim
};

typedef AAKPlayerData::Sounds aakPlayerSoundsEnum;
DefineEnumType(aakPlayerSoundsEnum);

ImplementEnumType(aakPlayerSoundsEnum, "enum types.\n"
   "@ingroup PlayerData\n\n")
   { aakPlayerSoundsEnum::stop, "stop", "..." },
   { aakPlayerSoundsEnum::jumpCrouch, "jumpCrouch", "..." },
   { aakPlayerSoundsEnum::land, "land", "..." },
   { aakPlayerSoundsEnum::climbIdle, "climbIdle", "..." },
   { aakPlayerSoundsEnum::climbUp, "climbUp", "..." },
   { aakPlayerSoundsEnum::climbDown, "climbDown", "..." },
   { aakPlayerSoundsEnum::climbLeftRight, "climbLeftRight", "..." },
   { aakPlayerSoundsEnum::ledgeIdle, "ledgeIdle", "..." },
   { aakPlayerSoundsEnum::ledgeUp, "ledgeUp", "..." },
   { aakPlayerSoundsEnum::ledgeLeftRight, "ledgeLeftRight", "..." },
   { aakPlayerSoundsEnum::slide, "slide", "..." },
   { aakPlayerSoundsEnum::FootSoft, "FootSoft", "..." },
   { aakPlayerSoundsEnum::FootHard,            "FootHard","..." },
   { aakPlayerSoundsEnum::FootMetal,           "FootMetal","..." },
   { aakPlayerSoundsEnum::FootSnow,            "FootSnow","..." },
   { aakPlayerSoundsEnum::FootShallowSplash,   "FootShallowSplash","..." },
   { aakPlayerSoundsEnum::FootWading,          "FootWading","..." },
   { aakPlayerSoundsEnum::FootUnderWater,      "FootUnderWater","..." },
   { aakPlayerSoundsEnum::FootBubbles,         "FootBubbles","..." },
   { aakPlayerSoundsEnum::MoveBubbles,         "MoveBubbles","..." },
   { aakPlayerSoundsEnum::WaterBreath,         "WaterBreath","..." },
   { aakPlayerSoundsEnum::ImpactSoft,          "ImpactSoft","..." },
   { aakPlayerSoundsEnum::ImpactHard,          "ImpactHard","..." },
   { aakPlayerSoundsEnum::ImpactMetal,         "ImpactMetal","..." },
   { aakPlayerSoundsEnum::ImpactSnow,          "ImpactSnow","..." },
   { aakPlayerSoundsEnum::ImpactWaterEasy,     "ImpactWaterEasy","..." },
   { aakPlayerSoundsEnum::ImpactWaterMedium,   "ImpactWaterMedium","..." },
   { aakPlayerSoundsEnum::ImpactWaterHard,     "ImpactWaterHard","..." },
   { aakPlayerSoundsEnum::ExitWater,           "ExitWater","..." },
      EndImplementEnumType;

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

IMPLEMENT_CO_DATABLOCK_V1(AAKPlayerData);

ConsoleDocClass( AAKPlayerData,
   "@brief Defines properties for a AAKPlayer object.\n\n"
   "@see Player\n"
   "@ingroup gameObjects\n"
);

AAKPlayerData::AAKPlayerData() : PlayerData()
{
   density = 10.0f;      // from ShapeBase

   vertDrag = 0.01f;
   vertDragFalling = 1.0f;

   // Jumping
   jumpForceClimb = 75.0f;

	//Ubiq: 
	cameraOffset = Point3F(0.0f, 0.0f, 0.0f);	

	walkRunAnimVelocity = 2.0f;

	groundTurnRate = 30.0f;
	airTurnRate = 10.0f;
	//maxAirTurn = 0.1f;

	//Ubiq: ground friction
	groundFriction = 0.01f;

	//Ubiq: jet time
	jetTime = 0.5f;

	//Ubiq: climbing
	climbHeightMin = 0.0f;
	climbHeightMax = 1.0f;
	climbSpeedUp = 1.0f;
	climbSpeedDown = 1.0f;
	climbSpeedSide = 1.0f;
	climbScrapeSpeed = -1.0f;
	climbScrapeFriction = 0.05f;

	//Ubiq: grabbing
	grabHeightMin = 1.4f;
	grabHeightMax = 1.6f;
	grabHeight = 1.5f;
	grabSpeedSide = 1.0f;
	grabSpeedUp = 1.0f;
	grabUpForwardOffset = 0.5f;
	grabUpUpwardOffset = 0.1f;
	grabUpTestBox = Point3F(0.5f, 0.5f, 1.5f);

	//Ubiq: Wall hug
	wallHugSpeed = 1.0f;
	wallHugHeightMin = 1.4f;
	wallHugHeightMax = 1.6f;

	//Ubiq: Jump delays
	runJumpCrouchDelay = 150.0f;
	standJumpCrouchDelay = 400.0f;

	//Ubiq: Ground Snap
	groundSnapSpeed = 0.05f;
	groundSnapRayLength = 0.5f;
	groundSnapRayOffset = 0.05f;
   orientToGround = false;

	//Ubiq: Land state
	landDuration = 100.0f;
	landSpeedFactor = 0.5f;

   dMemset(actionList, 0, sizeof(actionList));
}

bool AAKPlayerData::preload(bool server, String& errorStr)
{
   if (!Parent::preload(server, errorStr))
      return false;

   //We'll override what PlayerData normally preloads for action animation lists with our own version
   if (getShape())
   {
      // Go ahead a pre-load the player shape
      TSShapeInstance* si = new TSShapeInstance(getShape(), false);
      TSThread* thread = si->addThread();

      // Extract ground transform velocity from animations
      // Get the named ones first so they can be indexed directly.
      ActionAnimation* dp = &actionList[0];
      for (int i = 0; i < NumTableActionAnims; i++, dp++)
      {
         ActionAnimationDef* sp = &ActionAnimationList[i];
         dp->name = sp->name;
         dp->dir.set(sp->dir.x, sp->dir.y, sp->dir.z);
         dp->sequence = getShape()->findSequence(sp->name);

         // If this is a sprint action and is missing a sequence, attempt to use
         // the standard run ones.
         if (dp->sequence == -1 && i >= SprintRootAnim && i <= SprintRightAnim)
         {
            S32 offset = i - SprintRootAnim;
            ActionAnimationDef* standDef = &ActionAnimationList[RootAnim + offset];
            dp->sequence = getShape()->findSequence(standDef->name);
         }

         dp->velocityScale = true;
         dp->death = false;
         if (dp->sequence != -1)
            getGroundInfo(si, thread, dp);

         // No real reason to spam the console about a missing jet animation
         if (dStricmp(sp->name, "jet") != 0)
            AssertWarn(dp->sequence != -1, avar("PlayerData::preload - Unable to find named animation sequence '%s'!", sp->name));
      }
      for (int b = 0; b < getShape()->sequences.size(); b++)
      {
         if (!isTableSequence(b))
         {
            dp->sequence = b;
            dp->name = getShape()->getName(getShape()->sequences[b].nameIndex);
            dp->velocityScale = false;
            getGroundInfo(si, thread, dp++);
         }
      }
      actionCount = dp - actionList;
      AssertFatal(actionCount <= NumActionAnims, "Too many action animations!");
      delete si;
   }

   return true;
}

//Ubiq:
bool AAKPlayerData::isLedgeAction(U32 action)
{
	return (action == LedgeIdleAnim
		|| action == LedgeLeftAnim
		|| action == LedgeRightAnim
		|| action == LedgeUpAnim
		);
}

bool AAKPlayerData::isClimbAction(U32 action)
{
	return (action == ClimbIdleAnim
		|| action == ClimbLeftAnim
		|| action == ClimbRightAnim
		|| action == ClimbUpAnim
		|| action == ClimbDownAnim
		);
}

bool AAKPlayerData::isLandAction(U32 action)
{
	return (action == StandingLandAnim
		|| action == RunningLandAnim
		);
}


void AAKPlayerData::initPersistFields()
{
   Parent::initPersistFields();

   addGroup( "Interaction: Sounds" );
   INITPERSISTFIELD_SOUNDASSET_ENUMED(PlayerSound, aakPlayerSoundsEnum, AAKPlayerData::Sounds::MaxSounds, AAKPlayerData, "Sounds related to player interaction.");
   endGroup( "Interaction: Sounds" );

   //Ubiq: TODO: put these into groups that make more sense & add documentation strings
   addGroup("AAK General");

      addField("cameraOffset", TypePoint3F, Offset(cameraOffset, AAKPlayerData), "");

      addField("walkRunAnimVelocity", TypeF32, Offset(walkRunAnimVelocity, AAKPlayerData), "");
   endGroup("AAK General");

   addGroup("AAK Movement");
      addField("groundTurnRate", TypeF32, Offset(groundTurnRate, AAKPlayerData), "");
      addField("airTurnRate", TypeF32, Offset(airTurnRate, AAKPlayerData), "");
      //addField("maxAirTurn", TypeF32, Offset(maxAirTurn, AAKPlayerData), "");

      addField("groundFriction", TypeF32, Offset(groundFriction, AAKPlayerData), "");

      addField("jetTime", TypeF32, Offset(jetTime, AAKPlayerData), "");
   endGroup("AAK Movement");

   //Ubiq: climbing
   addGroup("AAK Climbing");
      addField("climbHeightMin", TypeF32, Offset(climbHeightMin, AAKPlayerData), "");
      addField("climbHeightMax", TypeF32, Offset(climbHeightMax, AAKPlayerData), "");
      addField("climbSpeedUp", TypeF32, Offset(climbSpeedUp, AAKPlayerData), "");
      addField("climbSpeedDown", TypeF32, Offset(climbSpeedDown, AAKPlayerData), "");
      addField("climbSpeedSide", TypeF32, Offset(climbSpeedSide, AAKPlayerData), "");
      addField("climbScrapeSpeed", TypeF32, Offset(climbScrapeSpeed, AAKPlayerData), "");
      addField("climbScrapeFriction", TypeF32, Offset(climbScrapeFriction, AAKPlayerData), "");
   endGroup("AAK Climbing");

   //Ubiq: grabbing
   addGroup("AAK Grabbing");
      addField("grabHeightMin", TypeF32, Offset(grabHeightMin, AAKPlayerData), "");
      addField("grabHeightMax", TypeF32, Offset(grabHeightMax, AAKPlayerData), "");
      addField("grabHeight", TypeF32, Offset(grabHeight, AAKPlayerData), "");
      addField("grabSpeedSide", TypeF32, Offset(grabSpeedSide, AAKPlayerData), "");
      addField("grabSpeedUp", TypeF32, Offset(grabSpeedUp, AAKPlayerData), "");
      addField("grabUpForwardOffset", TypeF32, Offset(grabUpForwardOffset, AAKPlayerData), "");
      addField("grabUpUpwardOffset", TypeF32, Offset(grabUpUpwardOffset, AAKPlayerData), "");
      addField("grabUpTestBox", TypePoint3F, Offset(grabUpTestBox, AAKPlayerData), "");
   endGroup("AAK Grabbing");

   //Ubiq: Wall hug
   addGroup("AAK Wall hug");
      addField("wallHugSpeed", TypeF32, Offset(wallHugSpeed, AAKPlayerData), "");
      addField("wallHugHeightMin", TypeF32, Offset(wallHugHeightMin, AAKPlayerData), "");
      addField("wallHugHeightMax", TypeF32, Offset(wallHugHeightMax, AAKPlayerData), "");
   endGroup("AAK Wall hug");

   //Ubiq: Stand jump
   addGroup("AAK Stand jump");
      addField("runJumpCrouchDelay", TypeF32, Offset(runJumpCrouchDelay, AAKPlayerData), "");
      addField("standJumpCrouchDelay", TypeF32, Offset(standJumpCrouchDelay, AAKPlayerData), "");
   endGroup("AAK Stand jump");

   //Ubiq: Ground Snap
   addGroup("AAK Ground Snap");
      addField("groundSnapSpeed", TypeF32, Offset(groundSnapSpeed, AAKPlayerData), "");
      addField("groundSnapRayLength", TypeF32, Offset(groundSnapRayLength, AAKPlayerData), "");
      addField("groundSnapRayOffset", TypeF32, Offset(groundSnapRayOffset, AAKPlayerData), "");
      addField("orientToGround", TypeBool, Offset(orientToGround, AAKPlayerData), "");
   endGroup("AAK Ground Snap");

   //Ubiq: Land state
   addGroup("AAK Land State");
      addField("landDuration", TypeF32, Offset(landDuration, AAKPlayerData), "");
      addField("landSpeedFactor", TypeF32, Offset(landSpeedFactor, AAKPlayerData), "");
   endGroup("AAK Land State");

   Parent::initPersistFields();
}

void AAKPlayerData::packData(BitStream* stream)
{
   Parent::packData(stream);

   stream->write(vertDrag);
   stream->write(vertDragFalling);

   // Jumping
   stream->write(jumpForceClimb);
   
	//Ubiq:
	stream->write(cameraOffset.x);
	stream->write(cameraOffset.y);
	stream->write(cameraOffset.z);

	stream->write(walkRunAnimVelocity);

	stream->write(groundTurnRate);
	stream->write(airTurnRate);
	//stream->write(maxAirTurn);

	stream->write(groundFriction);

	stream->write(jetTime);

	//Ubiq: climb
	stream->write(climbHeightMin);
	stream->write(climbHeightMax);
	stream->write(climbSpeedUp);
	stream->write(climbSpeedDown);
	stream->write(climbSpeedSide);
	stream->write(climbScrapeSpeed);
	stream->write(climbScrapeFriction);

	//Ubiq: grab
	stream->write(grabHeightMin);
	stream->write(grabHeightMax);
	stream->write(grabHeight);
	stream->write(grabSpeedSide);
	stream->write(grabSpeedUp);
	stream->write(grabUpForwardOffset);
	stream->write(grabUpUpwardOffset);
	stream->write(grabUpTestBox.x);
	stream->write(grabUpTestBox.y);
	stream->write(grabUpTestBox.z);

	//Ubiq: Wall hug
	stream->write(wallHugSpeed);
	stream->write(wallHugHeightMin);
	stream->write(wallHugHeightMax);

	//Ubiq: stand jump
	stream->write(runJumpCrouchDelay);
	stream->write(standJumpCrouchDelay);

	//Ubiq: ground snap
	stream->write(groundSnapSpeed);
	stream->write(groundSnapRayLength);
	stream->write(groundSnapRayOffset);
   stream->writeFlag(orientToGround);
	
	//Ubiq: Land state
	stream->write(landDuration);
	stream->write(landSpeedFactor);
}

void AAKPlayerData::unpackData(BitStream* stream)
{
   Parent::unpackData(stream);

   stream->read(&vertDrag);
   stream->read(&vertDragFalling);

   // Jumping
   stream->read(&jumpForceClimb);
   
	//Ubiq:
	stream->read(&cameraOffset.x);
	stream->read(&cameraOffset.y);
	stream->read(&cameraOffset.z);

	stream->read(&walkRunAnimVelocity);

	stream->read(&groundTurnRate);
	stream->read(&airTurnRate);
	//stream->read(&maxAirTurn);

	stream->read(&groundFriction);

	stream->read(&jetTime);

	//Ubiq: climb
	stream->read(&climbHeightMin);
	stream->read(&climbHeightMax);
	stream->read(&climbSpeedUp);
	stream->read(&climbSpeedDown);
	stream->read(&climbSpeedSide);
	stream->read(&climbScrapeSpeed);
	stream->read(&climbScrapeFriction);

	//Ubiq: grab
	stream->read(&grabHeightMin);
	stream->read(&grabHeightMax);
	stream->read(&grabHeight);
	stream->read(&grabSpeedSide);
	stream->read(&grabSpeedUp);
	stream->read(&grabUpForwardOffset);
	stream->read(&grabUpUpwardOffset);	
	stream->read(&grabUpTestBox.x);
	stream->read(&grabUpTestBox.y);
	stream->read(&grabUpTestBox.z);

	//Ubiq: Wall hug
	stream->read(&wallHugSpeed);
	stream->read(&wallHugHeightMin);
	stream->read(&wallHugHeightMax);

	//Ubiq: stand jump
	stream->read(&runJumpCrouchDelay);
	stream->read(&standJumpCrouchDelay);

	//Ubiq: ground snap
	stream->read(&groundSnapSpeed);
	stream->read(&groundSnapRayLength);
	stream->read(&groundSnapRayOffset);
   orientToGround = stream->readFlag();
	
	//Ubiq: Land state
	stream->read(&landDuration);
	stream->read(&landSpeedFactor);
}


//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

IMPLEMENT_CO_NETOBJECT_V1(AAKPlayer);

ConsoleDocClass(AAKPlayer,
   "@ingroup gameObjects\n"
);

//----------------------------------------------------------------------------

AAKPlayer::AAKPlayer() : Player()
{
   mDataBlock = nullptr;

   mSlideSound = 0;	//Ubiq

   mJumping = false;

   mActionAnimation.action = PlayerData::NullAnimation;
   mActionAnimation.thread = 0;
   mActionAnimation.delayTicks = 0;
   mActionAnimation.forward = true;
   mActionAnimation.firstPerson = false;
   //mActionAnimation.time = 1.0f; //ActionAnimation::Scale;
   mActionAnimation.waitForEnd = false;
   mActionAnimation.holdAtEnd = false;
   mActionAnimation.animateOnServer = false;
   mActionAnimation.atEnd = false;
   mActionAnimation.useSynchedPos = true;
   mActionAnimation.callbackTripped = false;

	//----------------------------------------------------------------------------
	// Ubiq custom
	//----------------------------------------------------------------------------

	mDieOnNextCollision = false;

	mRunSurface = false;
	mJumpSurface = false;
	mSlideSurface = false;

	//Ubiq: Snap to ground
	mGroundSnap = 0.0f;

	//Ubiq: Slide state
	mSlideState.active = false;
	mSlideState.surfaceNormal.zero();

	//Ubiq: Jump state
	mJumpState.active = false;
	mJumpState.isCrouching = false;
	mJumpState.crouchDelay = 0;
	mJumpState.jumpType = JumpType_Run;

	//Ubiq: Climb state
	mClimbTriggerCount = 0;
	mClimbState.active = false;
	mClimbState.direction = MOVE_DIR_NONE;
	mClimbState.surfaceNormal.set(0,0,0);

	//Ubiq: Wall Hug state
	mWallHugState.active = false;
	mWallHugState.direction = MOVE_DIR_NONE;
	mWallHugState.surfaceNormal.set(0,0,0);

	//Ubiq: Ledge Grab state
	mLedgeState.active = false;
	mLedgeState.ledgeNormal.zero();
	mLedgeState.ledgePoint.zero();
	mLedgeState.direction = MOVE_DIR_NONE;
	mLedgeState.climbingUp = false;
	mLedgeState.animPos = 0.0f;
	mLedgeState.deltaAnimPos = 0.0f;
	mLedgeState.deltaAnimPosVec = 0.0f;

	//Ubiq: Land state
	mLandState.active = false;
	mLandState.timer = 0;

	//Ubiq: Stop state
	mStoppingTimer = 0;
}


//----------------------------------------------------------------------------

void AAKPlayer::onRemove()
{
   if ( isGhost() )
   {
      SFX_DELETE( mSlideSound );
   }

   Parent::onRemove();
}

//----------------------------------------------------------------------------

bool AAKPlayer::onNewDataBlock( GameBaseData *dptr, bool reload )
{
   AAKPlayerData* prevData = mDataBlock;
   mDataBlock = dynamic_cast<AAKPlayerData*>(dptr);
   if ( !mDataBlock || !Parent::onNewDataBlock( dptr, reload ) )
      return false;

   if ( isGhost() )
   {
      SFX_DELETE( mSlideSound );

      if (mDataBlock->getPlayerSound(AAKPlayerData::slide))
         mSlideSound = SFX->createSource(mDataBlock->getPlayerSoundProfile(AAKPlayerData::slide));
   }

   return true;
}

//----------------------------------------------------------------------------

void AAKPlayer::processTick(const Move* move)
{
   PROFILE_SCOPE(AAKPlayer_ProcessTick);

   bool prevMoveMotion = mMoveMotion;
   Pose prevPose = getPose();

   //Ubiq:
   if(mShapeInstance)
      mShapeInstance->animate();

   // If we're not being controlled by a client, let the
   // AI sub-module get a chance at producing a move.
   Move aiMove;
   if (!move && isServerObject() && getAIMove(&aiMove))
      move = &aiMove;

   // Manage the control object and filter moves for the player
   Move pMove,cMove;
   if (mControlObject) {
      if (!move)
         mControlObject->processTick(0);
      else {
         //Ubiq: if mounted, only vehicle will get the move
         if (isMounted())
         {
            pMove = NullMove;
            cMove = *move;
            //if (isMounted()) {
               // Filter Jump trigger if mounted
               //pMove.trigger[sJumpTrigger] = move->trigger[sJumpTrigger];
               //cMove.trigger[sJumpTrigger] = false;
            //}
            if (move->freeLook) {
               // Filter yaw/picth/roll when freelooking.
               pMove.yaw = move->yaw;
               pMove.pitch = move->pitch;
               pMove.roll = move->roll;
               pMove.freeLook = true;
               cMove.freeLook = false;
               cMove.yaw = cMove.pitch = cMove.roll = 0.0f;
            }
         }
         
         //Ubiq: otherwise, both objects will get the move
         else
         {
            pMove = *move;
            cMove = *move;
         }
         mControlObject->processTick((mDamageState == Enabled)? &cMove: &NullMove);
         move = &pMove;
      }
   }

   Parent::Parent::processTick(move);
   // Check for state changes in the standard move triggers and
   // set bits for any triggers that switched on this tick in
   // the fx_s_triggers mask. Flag any changes to be packed to
   // clients.
   /*if (isServerObject())
   {
      fx_s_triggers = 0;
      if (move)
      {
         U8 on_bits = 0;
         for (S32 i = 0; i < MaxTriggerKeys; i++)
            if (move->trigger[i])
               on_bits |= BIT(i);

         if (on_bits != move_trigger_states)
         {
            U8 switched_on_bits = (on_bits & ~move_trigger_states);
            if (switched_on_bits)
            {
               fx_s_triggers |= (U32)switched_on_bits;
               setMaskBits(TriggerMask);
            }
            move_trigger_states = on_bits;
         }
      }
   }*/
   // Warp to catch up to server
   if (mDelta.warpTicks > 0) {
	   mDelta.warpTicks--;

      // Set new pos
      getTransform().getColumn(3, &mDelta.pos);
	  mDelta.pos += mDelta.warpOffset;
	  mDelta.rot += mDelta.rotOffset;

      // Wrap yaw to +/-PI
      if (mDelta.rot.z < - M_PI_F)
		  mDelta.rot.z += M_2PI_F;
      else if (mDelta.rot.z > M_PI_F)
		  mDelta.rot.z -= M_2PI_F;

      if (!ignore_updates)
      {
         setPosition(mDelta.pos, mDelta.rot);
      }

      Point3F dir = mDelta.rot;
      if (!mDelta.posVec.isZero() && (mFabs(mDelta.posVec.z) < 0.01))
      {
         dir = -mDelta.posVec;
         dir.normalize();
         dir = Point3F(0, 0, mAtan2(dir.x, dir.y));
      }

      setRenderPosition(mDelta.pos, mDelta.rot);
      updateDeathOffsets();
      updateLookAnimation();

      //Ubiq:
      updateLedgeUpAnimation();

      // Backstepping
	  mDelta.posVec = -mDelta.warpOffset;
	  mDelta.rotVec = -mDelta.rotOffset;
     if (mIsNaN(mDelta.posVec)) mDelta.posVec.zero();
     if (mIsNaN(mDelta.rotVec)) mDelta.posVec.zero();
   }
   else {
      // If there is no move, the player is either an
      // unattached player on the server, or a player's
      // client ghost.
      if (!move) {
         if (isGhost()) {
            // If we haven't run out of prediction time,
            // predict using the last known move.
            if (mPredictionCount-- <= 0)
               return;

            move = &mDelta.move;
         }
         else
            move = &NullMove;
      }
      if (!isGhost())
         updateAnimation(TickSec);

      PROFILE_START(AAKPlayer_PhysicsSection);
      if ( isServerObject() || didRenderLastRender() || getControllingClient() )
      {
         if ( !mPhysicsRep )
         {
            if ( isMounted() )
            {
               // If we're mounted then do not perform any collision checks
               // and clear our previous working list.
               mConvex.clearWorkingList();
            }
            else
            {
               updateWorkingCollisionSet();
            }
         }

         updateState();
         updateMove(move);
         updateLookAnimation();
         updateDeathOffsets();
         updatePos();
      }
      PROFILE_END();

      //Ubiq:
      updateLedgeUpAnimation();

      if (!isGhost())
      {
         // Animations are advanced based on frame rate on the
         // client and must be ticked on the server.
         updateActionThread();
         updateAnimationTree(true);

         // Check for sprinting motion changes
         Pose currentPose = getPose();
         // Player has just switched into Sprint pose and is moving
         if (currentPose == SprintPose && prevPose != SprintPose && mMoveMotion)
         {
            mDataBlock->onStartSprintMotion_callback( this );
         }
         // Player has just switched out of Sprint pose and is moving, or was just moving
         else if (currentPose != SprintPose && prevPose == SprintPose && (mMoveMotion || prevMoveMotion))
         {
            mDataBlock->onStopSprintMotion_callback( this );
         }
         // Player is in Sprint pose and has modified their motion
         else if (currentPose == SprintPose && prevMoveMotion != mMoveMotion)
         {
            if (mMoveMotion)
            {
               mDataBlock->onStartSprintMotion_callback( this );
            }
            else
            {
               mDataBlock->onStopSprintMotion_callback( this );
            }
         }
      }
   }

   if (!isGhost())
      updateAttachment();
}

void AAKPlayer::interpolateTick(F32 dt)
{
   if (mControlObject)
      mControlObject->interpolateTick(dt);

   // Client side interpolation
   Parent::Parent::interpolateTick(dt);

   Point3F pos = mDelta.pos + mDelta.posVec * dt;
   Point3F rot = mDelta.rot + mDelta.rotVec * dt;

   //Ubiq: ledge up animation
   if(mLedgeState.climbingUp)
   {
      F32 animPos = mLedgeState.animPos + mLedgeState.deltaAnimPosVec * dt;

      if (mActionAnimation.thread)
      {
         //ideally we wouldn't clearTransition, but it gets "stuck" otherwise
         mShapeInstance->clearTransition(mActionAnimation.thread);
         mShapeInstance->setPos(mActionAnimation.thread, animPos);
      }
   }

   if (!ignore_updates)
   {
      Point3F dir = mDelta.rot;
      if (!mDelta.posVec.isZero() && (mFabs(mDelta.posVec.z) < 0.01))
      {
         dir = -mDelta.posVec;
         dir.normalize();
         dir = Point3F(0, 0, mAtan2(dir.x, dir.y));
      }
      setRenderPosition(pos, rot, dt);
   }

/*
   // apply camera effects - is this the best place? - bramage
   GameConnection* connection = GameConnection::getConnectionToServer();
   if( connection->isFirstPerson() )
   {
      ShapeBase *obj = dynamic_cast<ShapeBase*>(connection->getControlObject());
      if( obj == this )
      {
         MatrixF curTrans = getRenderTransform();
         curTrans.mul( gCamFXMgr.getTrans() );
         Parent::setRenderTransform( curTrans );
      }
   }
*/

   updateLookAnimation(dt);
   mDelta.dt = dt;

   updateRenderChangesByParent();
}

void AAKPlayer::advanceTime(F32 dt)
{
   // Client side animations
   Parent::Parent::advanceTime(dt);
   // Increment timer for triggering idle events.
   if (idle_timer >= 0.0f)
      idle_timer += dt;
   updateActionThread();
   updateAnimation(dt);
   updateSplash();
   updateFroth(dt);
   updateSounds(dt);
   //updateWaterSounds(dt);	//Ubiq: functionality moved to updateSounds()

   mLastPos = getPosition();

   if (mImpactSound)
      playImpactSound();

   // update camera effects.  Definitely need to find better place for this - bramage
   if( isControlObject() )
   {
      if( mDamageState == Disabled || mDamageState == Destroyed )
      {
         // clear out all camera effects being applied to player if dead
         gCamFXMgr.clear();
      }
   }
}

void AAKPlayer::setState(ActionState state, U32 recoverTicks)
{
   if (state != mState) {
      // Skip initialization if there is no manager, the state
      // will get reset when the object is added to a manager.
      if (isProperlyAdded()) {
         switch (state) {
            case RecoverState: {
               //Ubiq: removing these - we use our own landing system
               /*if (mDataBlock->landSequenceTime > 0.0f)
               {
                  // Use the land sequence as the basis for the recovery
                  setActionThread(AAKPlayerData::LandAnim, true, false, true, true);
                  F32 timeScale = mShapeInstance->getDuration(mActionAnimation.thread) / mDataBlock->landSequenceTime;
                  mShapeInstance->setTimeScale(mActionAnimation.thread,timeScale);
                  mRecoverDelay =  mDataBlock->landSequenceTime;
               }
               else
               {
                  // Legacy recover system
                  mRecoverTicks = recoverTicks;
                  mReversePending = U32(F32(mRecoverTicks) / sLandReverseScale);
                  setActionThread(AAKPlayerData::LandAnim, true, false, true, true);
               }*/
               break;
            }
            
            default:
               break;
         }
      }

      mState = state;
   }
}

void AAKPlayer::updateState()
{
   switch (mState)
   {
      case RecoverState:
         //Ubiq: removing these - we use our own landing system
         /*if (mDataBlock->landSequenceTime > 0.0f)
         {
            // Count down the land time
            mRecoverDelay -= TickSec;
            if (mRecoverDelay <= 0.0f)
            {
               setState(MoveState);
            }
         }
         else
         {
            // Legacy recover system
            if (mRecoverTicks-- <= 0)
            {
               if (mReversePending && mActionAnimation.action != AAKPlayerData::NullAnimation)
               {
                  // this serves and counter, and direction state
                  mRecoverTicks = mReversePending;
                  mActionAnimation.forward = false;

                  S32 seq = mDataBlock->actionList[mActionAnimation.action].sequence;
                  S32 imageBasedSeq = convertActionToImagePrefix(mActionAnimation.action);
                  if (imageBasedSeq != -1)
                     seq = imageBasedSeq;

                  F32 pos = mShapeInstance->getPos(mActionAnimation.thread);

                  mShapeInstance->setTimeScale(mActionAnimation.thread, -sLandReverseScale);
                  mShapeInstance->transitionToSequence(mActionAnimation.thread,
                                                       seq, pos, sAnimationTransitionTime, true);
                  mReversePending = 0;
               }
               else
               {
                  setState(MoveState);
               }
            }        // Stand back up slowly only if not moving much-
            else if (!mReversePending && mVelocity.lenSquared() > sSlowStandThreshSquared)
            {
               mActionAnimation.waitForEnd = false;
               setState(MoveState);
            }
         }*/
         break;
      
      default:
         break;
   }
}

void AAKPlayer::updateAttachment()
{
   Point3F rot, pos;
   RayInfo rInfo;
   MatrixF mat = getTransform();
   mat.getColumn(3, &pos);
   F32 height = mObjBox.getExtents().z + 0.1;
   disableCollision();
   if (gServerContainer.castRay(Point3F(pos.x, pos.y, pos.z + height / 2),
      Point3F(pos.x, pos.y, pos.z - height / 2),
      sCollisionMoveMask, &rInfo))
   {
      if (rInfo.object->getTypeMask() & PathShapeObjectType) //Ramen
      {
         if (!mJumpState.active && !mClimbState.active && !mLedgeState.active && !mSlideState.active && !mSwimming)
            setPosition(rInfo.point, getRotation());

         if (getParent() == NULL)
         { // ONLY do this if we are not parented
            //Con::printf("I'm on a pathshape object. Going to attempt attachment.");
            ShapeBase* col = static_cast<ShapeBase*>(rInfo.object);
            if (!isGhost())
            {
               this->attachToParent(col);
            }
         }
      }
      else
      {
         //Con::printf("object %i",rInfo.object->getId());
      }
   }
   else
   {
      if (getParent() != NULL)
      {
         clearProcessAfter();
         attachToParent(NULL);
      }
   }
   enableCollision();
}

void AAKPlayer::updateMove(const Move* move)
{
   /*struct Move my_move;
   if (override_movement && movement_op < 3)
   {
      my_move = *move;
      switch (movement_op)
      {
      case 0: // add
         my_move.x += movement_data.x;
         my_move.y += movement_data.y;
         my_move.z += movement_data.z;
         break;
      case 1: // mult
         my_move.x *= movement_data.x;
         my_move.y *= movement_data.y;
         my_move.z *= movement_data.z;
         break;
      case 2: // replace
         my_move.x = movement_data.x;
         my_move.y = movement_data.y;
         my_move.z = movement_data.z;
         break;
      }
      move = &my_move;
   }*/
   mDelta.move = *move;

#ifdef TORQUE_OPENVR
   if (mControllers[0])
   {
      mControllers[0]->processTick(move);
   }

   if (mControllers[1])
   {
      mControllers[1]->processTick(move);
   }

#endif

   // Is waterCoverage high enough to be 'swimming'?
   {
      bool swimming = mWaterCoverage > 0.65f && canSwim();      

      if ( swimming != mSwimming )
      {
         if ( !isGhost() )
         {
            if ( swimming )
               mDataBlock->onStartSwim_callback( this );
            else
               mDataBlock->onStopSwim_callback( this );
         }

         mSwimming = swimming;
      }
   }

	VectorF moveVec(move->x,move->y,move->z);
	GameConnection* con = getControllingClient();

	//Ubiq: running diagonally shouldn't be faster
	if(moveVec.len() > 1)
		moveVec.normalizeSafe();

	//draw the move vector for debugging
	#ifdef ENABLE_DEBUGDRAW
   if (sRenderHelpers)
   {
      DebugDrawer::get()->drawLine(getPosition(), getPosition() + moveVec, ColorI::RED);
      DebugDrawer::get()->setLastTTL(TickMs);
   }
	#endif

	//Ubiq: use this normalized moveVec for testing in dot products
	VectorF moveVecNormalized(moveVec);
	moveVecNormalized.normalizeSafe();

   // Trigger images
   if (mDamageState == Enabled) 
   {
      setImageTriggerState( 0, move->trigger[sImageTrigger0] );

      // If you have a secondary mounted image then
      // send the second trigger to it.  Else give it
      // to the first image as an alt fire.
      if ( getMountedImage( 1 ) )
         setImageTriggerState( 1, move->trigger[sImageTrigger1] );
      else
         setImageAltTriggerState( 0, move->trigger[sImageTrigger1] );
   }

   // Update current orientation
   if (mDamageState == Enabled)
   {
      F32 prevZRot = mRot.z;
	  mDelta.headVec = mHead;

      bool doStandardMove = true;
      bool absoluteDelta = false;

      if(doStandardMove)
      {
      if (!(mClimbState.active || mLedgeState.active || mWallHugState.active))
         mRot.x = mRot.y = 0;

      F32 p = move->pitch * (mPose == SprintPose ? mDataBlock->sprintPitchScale : 1.0f);
      if (p > M_PI_F) 
         p -= M_2PI_F;
      mHead.x = mClampF(mHead.x + p,mDataBlock->minLookAngle,
                        mDataBlock->maxLookAngle);

      F32 y = move->yaw * (mPose == SprintPose ? mDataBlock->sprintYawScale : 1.0f);
      if (y > M_PI_F)
         y -= M_2PI_F;

      
      if (!isAnimationLocked())
      {
         if (move->freeLook && ((isMounted() && getMountNode() == 0) || (con && !con->isFirstPerson())))
         {
            mHead.z = mClampF(mHead.z + y,
               -mDataBlock->maxFreelookAngle,
               mDataBlock->maxFreelookAngle);
         }
         else if (!con || (con && con->isFirstPerson()))
         {
            mRot.z += y;
            // Rotate the head back to the front, center horizontal
            // as well if we're controlling another object.
            //mHead.z *= 0.5f;
            //if (mControlObject)
               //mHead.x *= 0.5f;
         }
      }

		//Ubiq: Lorne: if ledge/climb/wall, just face the surface
		if(mClimbState.active || mLedgeState.active || mWallHugState.active)
		{
			VectorF normal;
			if(mLedgeState.active)
				normal.set(-mLedgeState.ledgeNormal);
			else if(mClimbState.active)
				normal.set(-mClimbState.surfaceNormal);
			else
				normal.set(-mWallHugState.surfaceNormal);

			//face the surface
			Point3F goalRot = MathUtils::createOrientFromDir(normal).toEuler();
			Point3F delta = goalRot - mRot;
			
			//fix it -PI to +PI
			if (delta.z < -M_PI_F)
				delta.z += M_2PI_F;
			if (delta.z > M_PI_F)
				delta.z -= M_2PI_F;

			mRot += delta * 0.1f;
		}

		//Ubiq: Lorne: third-person behavior, rotate to face movement direction
		else if(!isAnimationLocked() && (!mJumpState.isCrouching && moveVec.len() > 0) && (con && !con->isFirstPerson()))
		{
			F32 yaw, pitch;
            AAKUtils::getAnglesFromVector(moveVec, yaw, pitch);

			F32 deltaYaw = yaw - mRot.z;
			//fix it -PI to +PI
			while (deltaYaw < -M_PI_F)
				deltaYaw += M_2PI_F;
			while (deltaYaw > M_PI_F)
				deltaYaw -= M_2PI_F;

			//choose our turn speed...
			F32 speed;
			if (mContactTimer == 0)
			{
				if(deltaYaw > 2.2 || deltaYaw < -2.2)
					//on the ground, fast 180
					speed = mDataBlock->groundTurnRate;

				else
					//on the ground, regular
					speed = mDataBlock->groundTurnRate;
			}
			else
				//in the air
				speed = mDataBlock->airTurnRate;

			speed *= TickSec;

			F32 turn = 0;
			if(mFabs(deltaYaw) < speed)
				turn = deltaYaw;
			else if(deltaYaw > 0)
				turn = speed;
			else if(deltaYaw < 0)
				turn = -speed;
			mRot.z += turn;

			if(mContactTimer == 0 && !mJumpState.active)
			{
				//Ubiq: Lorne: we just rotated while on the ground
				//let's rotate mVelocity by the same amount. This causes the player to 
				//maintain his speed through sharp turns (instead of losing it by applying
				//force in an opposing direction and then having to regain it)
				AngAxisF rotation(Point3F(0,0,1), turn);
				MatrixF xform;
				rotation.setMatrix(&xform);
				VectorF velocityTrans;
				xform.mulV(mVelocity, &velocityTrans);
				mVelocity.set(velocityTrans);
			}

         // Rotate the head back to the front, center horizontal
         // as well if we're controlling another object.
         //Ubiq: swimming seems to work better without these - removing
         //mHead.z *= 0.5f;
         //if (mControlObject)
            //mHead.x *= 0.5f;
      }

      // constrain the range of mRot.z
      while (mRot.z < 0.0f)
         mRot.z += M_2PI_F;
      while (mRot.z > M_2PI_F)
         mRot.z -= M_2PI_F;
     }

	  mDelta.rot = mRot;
	  mDelta.rotVec.x = mDelta.rotVec.y = 0.0f;
	  mDelta.rotVec.z = prevZRot - mRot.z;
      if (mDelta.rotVec.z > M_PI_F)
		  mDelta.rotVec.z -= M_2PI_F;
      else if (mDelta.rotVec.z < -M_PI_F)
		  mDelta.rotVec.z += M_2PI_F;

	  mDelta.head = mHead;
	  mDelta.headVec -= mHead;

      if (absoluteDelta)
      {
         mDelta.headVec = Point3F(0, 0, 0);
		 mDelta.rotVec = Point3F(0, 0, 0);
      }

      for(U32 i=0; i<3; ++i)
      {
         if (mDelta.headVec[i] > M_PI_F)
            mDelta.headVec[i] -= M_2PI_F;
         else if (mDelta.headVec[i] < -M_PI_F)
            mDelta.headVec[i] += M_2PI_F;
      }
   }
   MatrixF zRot;
   zRot.set(EulerF(0.0f, 0.0f, mRot.z));

   // Desired move direction & speed
   //VectorF moveVec;
   F32 moveSpeed;
   // If BLOCK_USER_CONTROL is set in anim_clip_flags, the user won't be able to
   // resume control over the player character. This generally happens for
   // short periods of time synchronized with script driven animation at places
   // where it makes sense that user motion is prohibited, such as when the 
   // player is lifted off the ground or knocked down.
   if ((mState == MoveState || (mState == RecoverState)) && mDamageState == Enabled && !isAnimationLocked())
   {
      /*zRot.getColumn(0,&moveVec);
      moveVec *= (move->x * (mPose == SprintPose ? mDataBlock->sprintStrafeScale : 1.0f));
      VectorF tv;
      zRot.getColumn(1,&tv);
      moveVec += tv * move->y;*/
      	if ( mSwimming )
		   moveSpeed = mDataBlock->maxUnderwaterForwardSpeed * moveVec.len();
		else if ( mPose == PronePose )
		   moveSpeed = mDataBlock->maxProneForwardSpeed * moveVec.len();
		else if ( mPose == CrouchPose )
		   moveSpeed = mDataBlock->maxCrouchForwardSpeed * moveVec.len();
		else if ( mPose == SprintPose )
		   moveSpeed = mDataBlock->maxSprintForwardSpeed * moveVec.len();
		else // StandPose
		   moveSpeed = mDataBlock->maxForwardSpeed * moveVec.len();


      // Cancel any script driven animations if we are going to move.
      if (!moveVec.isZero() &&
         (mActionAnimation.action >= AAKPlayerData::NumTableActionAnims))
         mActionAnimation.action = AAKPlayerData::NullAnimation;
   }
   else
   {
      moveVec.set(0.0f, 0.0f, 0.0f);
      moveSpeed = 0.0f;
   }

   // apply speed bias here.
   //speed_bias = speed_bias + (speed_bias_goal - speed_bias)*0.1f;
   //moveSpeed *= speed_bias;
   // Acceleration due to gravity
   //VectorF acc(0.0f, 0.0f, mGravity * mGravityMod * TickSec);
   VectorF acc(0,0,0);

   // Determine ground contact normal. Only look for contacts if
   // we can move and aren't mounted.
   VectorF contactNormal(0,0,0);
   mJumpSurface = mRunSurface = mSlideSurface = false;
   if ( !isMounted() && !mSwimming )	//Ubiq: don't check for surfaces when swimming
      findContact(&mRunSurface,&mJumpSurface,&mSlideSurface,&contactNormal);
   if (mJumpSurface)
      mJumpSurfaceNormal = contactNormal;
   if (!mClimbState.active && !mLedgeState.active)
      acc = VectorF(0.0f, 0.0f, mNetGravity / (1.0 - mBuoyancy) * TickSec);
   if (getParent() != NULL)
      acc = VectorF::Zero;

	//Ubiq: Slide state
   mSlideState.active = mSlideSurface && mVelocity.z < mDataBlock->fallingSpeedThreshold;
	if(mSlideState.active)
	{
		mContactTimer = 0;
		mJumping = false;
		mFalling = false;
		mSlideState.surfaceNormal = contactNormal;
	}

	//if we haven't been in contact for a while, but we just detected a runSurface above
	if(mContactTimer >= sContactTickTime && mRunSurface && mDamageState == Enabled && !isAnimationLocked())
	{
		//we know we've *just* landed this tick
		//play the land animation
		if(mJumpState.active && mJumpState.jumpType == JumpType_Run)
		{	
			setActionThread(AAKPlayerData::RunningLandAnim,true,false,true);
			mLandState.active = true;
			mLandState.timer = mDataBlock->landDuration;
		}
		else
		{
			setActionThread(AAKPlayerData::StandingLandAnim,true,false,true);
			mLandState.active = true;
			mLandState.timer = mDataBlock->landDuration;
		}

		//play sound effects on client
      if (isGhost()) SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::land), &getTransform());
	}

	//Ubiq: Not wall hugging?
	if(!mWallHugState.active)
	{
		//can we enter the wall hug state?
		if(canStartWallHug())
		{
			//do we have a wall?
			PlaneF wallPlane;
			bool wall = false;
			findWallContact(&wall, &wallPlane);
			if(wall)
			{
				//is the player moving into the surface?
				if(mDot(moveVecNormalized, wallPlane) < -0.95f)
				{
					Point3F snapPos = snapToPlane(wallPlane);

					//apply some fudge factor to make sure we're out of the wall
					//and to help deal with 2+ surfaces at once
					Point3F fudge(wallPlane);
					fudge.normalize(sSurfaceDistance);
					snapPos += fudge;

					//is the space clear? nothing in the way?
					if(worldBoxIsClear(mObjBox, snapPos))
					{
						//enter wallHug state
						mWallHugState.active = true;
						mWallHugState.surfaceNormal = wallPlane.getNormal();
						mStoppingTimer = 0;
					}
				}
			}
		}
	}

	//Ubiq: wall hugging?
	if(mWallHugState.active)
	{
		//do we have a wall?
		PlaneF wallPlane;
		bool wall = false;
		findWallContact(&wall, &wallPlane);

		//Check to make sure we should still be in the wall hug state
		if(!canWallHug() || !wall || mDot(wallPlane, moveVecNormalized) > 0.95f)
		{
			mWallHugState.active = false;
			mWallHugState.surfaceNormal.set(0,0,0);
		}
		else
		{
			//We're wall hugging...
			mWallHugState.active = true;
			mWallHugState.surfaceNormal = wallPlane.getNormal();
			mWallHugState.direction = MOVE_DIR_NONE;

			//see if we can snap to the wall
			Point3F snapPos = snapToPlane(wallPlane);

			//apply some fudge factor to make sure we're out of the wall
			//and to help deal with 2+ surfaces at once
			Point3F fudge(wallPlane);
			fudge.normalize(sSurfaceDistance);
			snapPos += fudge;

			//is the space clear? nothing in the way?
			if(worldBoxIsClear(mObjBox, snapPos))
			{
				//cool, snap to the wall
				mObjToWorld.setColumn(3, snapPos);
			}

			//is player is giving directional input?
			if (moveVec.len())
			{
				VectorF pv;
				pv = moveVec;

				VectorF rotAxis;
				mCross(mWallHugState.surfaceNormal, Point3F(0,0,1), &rotAxis);
				rotAxis.normalize();

				//snap direction and choose appropriate speed
				F32 pvDotSurface = mDot(pv, mWallHugState.surfaceNormal);
				if(pvDotSurface >= -0.5)
				{
					if(mDot(pv, rotAxis) > 0)
					{
						//left
						mWallHugState.direction = MOVE_DIR_LEFT;
						pv = rotAxis;
						pv.normalize(mDataBlock->wallHugSpeed);
					}
					else
					{
						//right
						mWallHugState.direction = MOVE_DIR_RIGHT;
						pv = -rotAxis;
						pv.normalize(mDataBlock->wallHugSpeed);
					}
				}
				else
				{
					pv.zero();
				}

				mVelocity.x = pv.x;
				mVelocity.y = pv.y;
			}
			else
			{
				//no player input, stop moving
				mVelocity.x = 0;
				mVelocity.y = 0;
			}
		}
	}

	//Ubiq: jump was released, we can climb
	if(!move->trigger[sJumpJetTrigger])
		mClimbState.ignoreClimb = false;

	//Ubiq: Not climbing?
	if(!mClimbState.active)
	{
		//can we enter the climb state?
		if(canStartClimb())
		{
			//do we have a climb surface?
			PlaneF climbPlane;
			bool climbSurface = false;
			findClimbContact(&climbSurface, &climbPlane);
			if(climbSurface)
			{
				//is the player moving into the surface, or in the air?
				if(mDot(moveVecNormalized, climbPlane) < -0.5f
					|| !mRunSurface)
				{
					Point3F snapPos = snapToPlane(climbPlane);

					//apply some fudge factor to make sure we're out of the surface
					//and to help deal with 2+ surfaces at once
					Point3F fudge(climbPlane);
					fudge.normalize(sSurfaceDistance);
					snapPos += fudge;

					//is the space clear? nothing in the way?
					if(worldBoxIsClear(mObjBox, snapPos))
					{
						//enter climb state
						mClimbState.active = true;
						mClimbState.surfaceNormal = climbPlane.getNormal();
						mClimbState.ignoreClimb = false;
						mJumpState.active = false;
						mJumping = false;
						mStoppingTimer = 0;
					}
				}
			}
		}
	}

	//Ubiq: Climbing?
	if(mClimbState.active)
	{
		//do we have a climb surface?
		PlaneF climbPlane;
		bool climbSurface = false;
		findClimbContact(&climbSurface, &climbPlane);

		//Check to make sure we should still be in the climb state
		if(!canClimb() || !climbSurface)
		{
			mClimbState.active = false;
			mClimbState.surfaceNormal.set(0,0,0);
			mClimbState.ignoreClimb = true;
		}
      else if (move->trigger[sJumpTrigger])
      {
         mClimbState.active = false;
         mClimbState.ignoreClimb = true;
         mClimbState.surfaceNormal = climbPlane.getNormal();
         acc.set(mClimbState.surfaceNormal * (mDataBlock->jumpForceClimb) / getMass());
      }
		else
		{
			//We're climbing...
			mClimbState.surfaceNormal = climbPlane.getNormal();
			mClimbState.direction = MOVE_DIR_NONE;

			//force states
			mJumpSurface = false;
			mJumpState.active = false;
			mJumpState.isCrouching = false;
			mJumpState.crouchDelay = 0;
			//mRunSurface = false;
			mContactTimer = 0;

			acc.zero();

			if(mVelocity.z < mDataBlock->climbScrapeSpeed)
			{
				//falling too fast to actually start climbing
				//but we can scrape at the wall and deccelerate
				mVelocity -= mVelocity * mDataBlock->climbScrapeFriction * TickSec;

				//slight gravity into surface
				acc.set(-mClimbState.surfaceNormal * 0.5f);
			}
			else
			{
				//see if we can snap to the surface
				Point3F snapPos = snapToPlane(climbPlane);

				//apply some fudge factor to make sure we're out of the surface
				//and to help deal with 2+ surfaces at once
				Point3F fudge(climbPlane);
				fudge.normalize(sSurfaceDistance);
				snapPos += fudge;

				//is the space clear? nothing in the way?
				if(worldBoxIsClear(mObjBox, snapPos))
				{
					//cool, snap to the wall
					mObjToWorld.setColumn(3, snapPos);
				}

				//regular climb
				if (moveVec.len())
				{
					//player is giving directional input

					VectorF pv;
					pv = moveVec;
					//rotate pv around an axis perpendicular to: surface normal and up
					//rotate by: PI/2 - surfacePitch

					//find the axis to rotate around
					VectorF rotAxis;
					mCross(mClimbState.surfaceNormal, Point3F(0,0,1), &rotAxis);
					rotAxis.normalize();

					//snap direction and choose appropriate speed
					F32 pvDotSurface = mDot(pv, mClimbState.surfaceNormal);
					if(pvDotSurface < -0.5)
					{
						//up
						mClimbState.direction = MOVE_DIR_UP;
						pv = -mClimbState.surfaceNormal;
						pv.normalize(mDataBlock->climbSpeedUp);
					}
					else if(pvDotSurface > 0.5)
					{
						if(mRunSurface)
						{
							//we're at the bottom, leave climb state
							mClimbState.active = false;
							mClimbState.ignoreClimb = true;
						}
						else
						{
							//down
							mClimbState.direction = MOVE_DIR_DOWN;
							pv = mClimbState.surfaceNormal;
							pv.normalize(mDataBlock->climbSpeedDown);
						}
					}
					else
					{
						if(mDot(pv, rotAxis) > 0)
						{
							//left
							mClimbState.direction = MOVE_DIR_LEFT;
							pv = rotAxis;
							pv.normalize(mDataBlock->climbSpeedSide);
						}
						else
						{
							//right
							mClimbState.direction = MOVE_DIR_RIGHT;
							pv = -rotAxis;
							pv.normalize(mDataBlock->climbSpeedSide);
						}
					}

					//are we ledge-grabbing up (or trying to)?
					if(mLedgeState.climbingUp || mLedgeState.direction == MOVE_DIR_UP)
					{
						//we are, can't move
						pv.zero();
					}

					//get the pitch of the surface
					F32 surfaceYaw, surfacePitch;
               AAKUtils::getAnglesFromVector(mClimbState.surfaceNormal, surfaceYaw, surfacePitch);

					F32 desiredPitch = M_PI_F / 2.0f - surfacePitch;

					//rotate pv
					AngAxisF test(rotAxis, desiredPitch);
					MatrixF xform;
					test.setMatrix(&xform);
					VectorF pvTrans;
					xform.mulV(pv, &pvTrans);
					pv = pvTrans;
					mVelocity = pv;
				}
				else
				{
					//no player input, stop moving
					mVelocity.zero();
				}
			}
		}
	}

	//Ubiq: jump was released, we can ledge grab
	if(!move->trigger[sJumpJetTrigger] && !move->trigger[sJumpTrigger])
		mLedgeState.ignoreLedge = false;

	if(!mLedgeState.active)
	{
		//can we enter the grab state?
		if(canStartLedgeGrab())
		{
			//do we have a ledge?
			bool ledge = false;
			VectorF ledgeNormal(0,0,0);
			Point3F ledgePoint(0,0,0);
			bool canMoveLeft = false;
			bool canMoveRight = false;

			findLedgeContact(&ledge, &ledgeNormal, &ledgePoint, &canMoveLeft, &canMoveRight);
			if(ledge && canMoveLeft && canMoveRight
				//on ground: don't grab unless specifically pushing into ledge
				//in air: grab unless specifically pulling away
				&& mDot(ledgeNormal, moveVecNormalized) <= (mContactTimer < sContactTickTime ? -0.5f : 0.75f))
			{
				//okay, we found a suitable ledge but we still need to determine the point the
				//player should snap to and test that point to ensure there is nothing in the way
				PlaneF ledgePlane (ledgePoint, ledgeNormal);
				Point3F snapPos = snapToPlane(ledgePlane);

				//apply some fudge factor to make sure we're out of the wall
				//and to help deal with 2+ edges at once
				Point3F fudge(ledgeNormal);
				fudge.normalize(sSurfaceDistance);
				snapPos += fudge;

				//set height to the ledge height
				snapPos.z = ledgePoint.z - mDataBlock->grabHeight;

				//is the space clear? nothing in the way?
				if(worldBoxIsClear(mObjBox, snapPos))
				{
					//ok, enter ledge state
					mLedgeState.active = true;
					mLedgeState.ledgeNormal = ledgeNormal;
					mLedgeState.ledgePoint = ledgePoint;
					mLedgeState.ignoreLedge = false;
					mJumpState.active = false;
					mJumping = false;
					mStoppingTimer = 0;
				}
			}
		}
	}

	if(mLedgeState.climbingUp)
	{
		//exit from bottom?
		if(mLedgeState.animPos == 0.0f)
		{
			//might still be grabbing, just clear climbingUp state
			mLedgeState.climbingUp = false; 
			mLedgeState.animPos = 0.0f;
			mLedgeState.deltaAnimPos = 0.0f;
			mLedgeState.deltaAnimPosVec = 0.0f;
			setMaskBits(LedgeUpMask);
		}

		//exit from top?
		if(mLedgeState.animPos == 1.0f)
		{
			//snap up to top
			Point3F pos = getLedgeUpPosition();
			mObjToWorld.setColumn(3, pos);

			setActionThread(AAKPlayerData::RootAnim,true,false,false);
			mShapeInstance->clearTransition(mActionAnimation.thread);
			mShapeInstance->animate(); //why is this necessary?

			//clear ledge grabbing
			mLedgeState.active = false;
			mLedgeState.ledgeNormal.zero();
			mLedgeState.ledgePoint.zero();
			mLedgeState.direction = MOVE_DIR_NONE;
			mLedgeState.climbingUp = false;
			mLedgeState.animPos = 0.0f;
			mLedgeState.deltaAnimPos = 0.0f;
			mLedgeState.deltaAnimPosVec = 0.0f;
			setMaskBits(LedgeUpMask);

			//clear climbing
			mClimbState.active = false;
			mClimbState.direction = MOVE_DIR_NONE;
			mClimbState.surfaceNormal.zero();
		}
	}

	if(mLedgeState.active)
	{
		//do we have a ledge?
		bool ledge = false;
		VectorF ledgeNormal(0,0,0);
		Point3F ledgePoint(0,0,0);
		bool canMoveLeft = false;
		bool canMoveRight = false;

		findLedgeContact(&ledge, &ledgeNormal, &ledgePoint, &canMoveLeft, &canMoveRight);

		//Check to make sure we should still be in the grab state
		if((!canLedgeGrab() || !ledge || mDot(ledgeNormal, moveVecNormalized) > 0.75f || move->trigger[sJumpTrigger])
			&& !mLedgeState.climbingUp)	//can't leave if you're climbing up
		{
			mLedgeState.active = false;
			mLedgeState.ledgeNormal.zero();
			mLedgeState.ledgePoint.zero();
			mLedgeState.direction = MOVE_DIR_NONE;
			mLedgeState.ignoreLedge = true;

			mLedgeState.climbingUp = false;
			mLedgeState.animPos = 0.0f;
			mLedgeState.deltaAnimPos = 0.0f;
			mLedgeState.deltaAnimPosVec = 0.0f;
			setMaskBits(LedgeUpMask);
		}
		else if(ledge)
		{
			AssertFatal(!ledgeNormal.isZero(), "ledgeNormal is 0!");

			//okay, we found a suitable ledge but we still need to determine the point the
			//player should snap to and test that point to ensure there is nothing in the way
			PlaneF ledgePlane (ledgePoint, ledgeNormal);
			Point3F snapPos = snapToPlane(ledgePlane);

			//apply some fudge factor to make sure we're out of the wall
			//and to help deal with 2+ edges at once
			Point3F fudge(ledgeNormal);
			fudge.normalize(sSurfaceDistance);
			snapPos += fudge;

			//set height to the ledge height
			snapPos.z = ledgePoint.z - mDataBlock->grabHeight;

			//is the space clear? nothing in the way?
			if(worldBoxIsClear(mObjBox, snapPos))
			{
				//cool, snap to the ledge
				mObjToWorld.setColumn(3, snapPos);
			}

			//We're grabbing / climbing up...
			mLedgeState.active = true;
			mLedgeState.ledgeNormal = ledgeNormal;
			mLedgeState.ledgePoint = ledgePoint;

			//force states
			mJumpSurface = false;
			mJumpState.active = false;
			mJumpState.isCrouching = false;
			mJumpState.crouchDelay = 0;
			mRunSurface = false;
			mContactTimer = 0;

			//dead-stop
			acc.zero();
			mVelocity.zero();

			mLedgeState.direction = MOVE_DIR_NONE;			


			//if player is giving directional input
			if (moveVec.len())
			{
				VectorF pv;
				pv = moveVec;

				//snap direction and choose appropriate speed
				F32 pvDotSurface = mDot(pv, mLedgeState.ledgeNormal);
				if(pvDotSurface < -0.5)
				{
					//up
					pv.zero();

					mLedgeState.direction = MOVE_DIR_UP;

					if(canStartLedgeUp())
					{
						mLedgeState.climbingUp = true;
						mLedgeState.animPos = F32_MIN;	//skip over 0 so we don't exit immediately
					}
				}
				else if(pvDotSurface > 0.5)
				{
					//down, nothing to do!
					pv.zero();
				}
				else
				{
					//now we find a vector perpendicular to: ledge normal and up
					VectorF rotAxis;
					mCross(mLedgeState.ledgeNormal, Point3F(0,0,1), &rotAxis);
					rotAxis.normalize();

					if(mDot(pv, rotAxis) > 0)
					{
						//left
						if(canMoveLeft)
						{
							mLedgeState.direction = MOVE_DIR_LEFT;
							pv = rotAxis;
							pv.normalize(mDataBlock->grabSpeedSide);
						}
						else
						{
							//can't go left
							pv.zero();
						}
					}
					else
					{
						//right
						if(canMoveRight)
						{
							mLedgeState.direction = MOVE_DIR_RIGHT;
							pv = -rotAxis;
							pv.normalize(mDataBlock->grabSpeedSide);
						}
						else
						{
							//can't go right
							pv.zero();
						}
					}
				}

				//are we ledge-grabbing up (or trying to)?
				if(mLedgeState.climbingUp || mLedgeState.direction == MOVE_DIR_UP)
				{
					//we are, can't move
					pv.zero();
				}

				mVelocity = pv;
			}
		}
	}

	// Acceleration on run surface
	if (mRunSurface
			&& !mLedgeState.active	//these states handle
			&& !mClimbState.active	//their own movement
			&& !mWallHugState.active)
	{
		mContactTimer = 0;
		mJumping = false;
		mLedgeState.active = false;

      // Force a 0 move if there is no energy, and only drain
      // move energy if we're moving.
      VectorF pv;
      if (mPose == SprintPose && mEnergy >= mDataBlock->minSprintEnergy) {
         if (moveSpeed)
            mEnergy -= mDataBlock->sprintEnergyDrain;
         pv = moveVec;
      }
      else if (mEnergy >= mDataBlock->minRunEnergy) {
         if (moveSpeed)
            mEnergy -= mDataBlock->runEnergyDrain;

			//Ubiq: at low speeds (or in first-person) we want to move in the direction of input
			//regardless of which direction the player is actually facing
			if(mVelocity.len() < 1.0f || ((con && con->isFirstPerson()) || !con))
				pv = moveVec;

			//Ubiq: at higher speeds in third-person we only want to run forwards (no strafing)
			else
			{
				getTransform().getColumn(1, &pv);
				pv.normalize(moveVec.len());
			}
		}
      else
         pv.set(0.0f, 0.0f, 0.0f);

		//Ubiq: Land state
		if(mLandState.active)
		{
			if(!mDataBlock->isLandAction(mActionAnimation.action))
			{
				//exit land state
				mLandState.active = false;
			}
			else if(mLandState.timer > 0)
			{
				//reduces the speed of the player upon landing
				moveSpeed *= mDataBlock->landSpeedFactor;
				mLandState.timer -= TickMs;
			}
			else if (!moveVec.isZero() || move->trigger[sJumpTrigger])
			{
				//exit land state
				mLandState.active = false;

				//cancel land animation
				mActionAnimation.action = AAKPlayerData::NullAnimation;
			}
		}

      // Adjust the player's requested dir. to be parallel
      // to the contact surface.
      F32 pvl = pv.len();
      if(mJetting)
      {
         pvl = moveVec.len();
         if (pvl)
         {
            VectorF nn;
            mCross(pv,VectorF(0.0f, 0.0f, 0.0f),&nn);
            nn *= 1 / pvl;
            VectorF cv(0.0f, 0.0f, 0.0f);
            cv -= nn * mDot(nn,cv);
            pv -= cv * mDot(pv,cv);
            pvl = pv.len();
         }
      }
      else if (!mPhysicsRep)
      {
         // We only do this if we're not using a physics library.  The
         // library will take care of itself.
         if (pvl)
         {
            VectorF nn;
            mCross(pv,VectorF(0.0f, 0.0f, 1.0f),&nn);
            nn *= 1.0f / pvl;
            VectorF cv = contactNormal;
            cv -= nn * mDot(nn,cv);
            pv -= cv * mDot(pv,cv);
            pvl = pv.len();
         }
      }

      // Convert to acceleration
      if ( pvl )
         pv *= moveSpeed / pvl;
      VectorF runAcc = pv - (mVelocity + acc);
      F32 runSpeed = runAcc.len();

      // Clamp acceleration
      F32 maxAcc;
      if (mPose == SprintPose)
      {
         maxAcc = (mDataBlock->sprintForce / getMass()) * TickSec;
      }
      else
      {
         maxAcc = (mDataBlock->runForce / getMass()) * TickSec;
      }
      if (runSpeed > maxAcc)
         runAcc *= maxAcc / runSpeed;
      acc += runAcc;

		//Ubiq: Stopping Timer
		if(mVelocity.len() > 0.01f && mDot(mVelocity, runAcc) < -0.01f)
			mStoppingTimer += TickMs; //deaccelerating, increment timer
		else
			mStoppingTimer = 0; //accelerating, reset timer

		//if we're no-longer giving input, but still running, we're going to stop
		if(mStoppingTimer > 64
			&& mActionAnimation.action == AAKPlayerData::RunForwardAnim
			&& mVelocity.len() > mDataBlock->walkRunAnimVelocity)
		{
			//play stop animation
			setActionThread(AAKPlayerData::StopAnim,true,false,true);

			//play sound effects on client
         if (isGhost()) SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::stop), &getTransform());
		}

		if(mActionAnimation.action == AAKPlayerData::StopAnim
			&& mStoppingTimer == 0)
		{
			//cancel stop animation, we're moving again
			mActionAnimation.action = AAKPlayerData::NullAnimation;	
		}

		if(!mJumpState.isCrouching)
		{
			mJumpState.active = false;
		}
	}
   else if (!mSwimming && mDataBlock->airControl > 0.0f
      && !mClimbState.active && !mLedgeState.active && !mWallHugState.active)
   {
      VectorF pv;
      pv = moveVec;
      F32 pvl = pv.len();

      if (pvl)
         pv *= moveSpeed / pvl;

      VectorF runAcc = pv - (mVelocity + acc); //Ubiq: Lorne: TODO: Is this okay?
      runAcc.z = 0;
      runAcc.x = runAcc.x * mDataBlock->airControl;
      runAcc.y = runAcc.y * mDataBlock->airControl;
      F32 runSpeed = runAcc.len();
      // We don't test for sprinting when performing air control
      F32 maxAcc = (mDataBlock->runForce / getMass()) * TickSec * 0.3f;

      if (runSpeed > maxAcc)
         runAcc *= maxAcc / runSpeed;

      acc += runAcc;

      // There are no special air control animations 
      // so... increment this unless you really want to 
      // play the run anims in the air.
      mContactTimer += TickMs;	//Ubiq: mContactTimer is now in ms (not ticks)
   }
   else if (mSwimming)
   {
      // Remove acc into contact surface (should only be gravity)
      // Clear out floating point acc errors, this will allow
      // the player to "rest" on the ground.
      F32 vd = -mDot(acc,contactNormal);
      if (vd > 0.0f) {
         VectorF dv = contactNormal * (vd + 0.002f);
         acc += dv;
         if (acc.len() < 0.0001f)
            acc.set(0.0f, 0.0f, 0.0f);
      }

      // get the head pitch and add it to the moveVec
      // This more accurate swim vector calc comes from Matt Fairfax
      MatrixF xRot;
      xRot.set(EulerF(mHead.x, 0, 0));
      zRot.set(EulerF(0, 0, mRot.z));
      MatrixF rot;
      rot.mul(zRot, xRot);
      rot.getColumn(1,&moveVec);

      /*moveVec *= move->x;
      VectorF tv;
      rot.getColumn(1,&tv);
      moveVec += tv * move->y;
      rot.getColumn(2,&tv);
      moveVec += tv * move->z;*/

      // Force a 0 move if there is no energy, and only drain
      // move energy if we're moving.
      VectorF swimVec;
      if (mEnergy >= mDataBlock->minRunEnergy) {
         if (moveSpeed)         
            mEnergy -= mDataBlock->runEnergyDrain;
         swimVec = moveVec;
      }
      else
         swimVec.set(0.0f, 0.0f, 0.0f);

      // If we are swimming but close enough to the shore/ground
      // we can still have a surface-normal. In this case align the
      // velocity to the normal to make getting out of water easier.
      
      moveVec.normalize();
      F32 isSwimUp = mDot( moveVec, contactNormal );

      if ( !contactNormal.isZero() && isSwimUp < 0.1f )
      {
         F32 pvl = swimVec.len();

         if ( true && pvl )
         {
            VectorF nn;
            mCross(swimVec,VectorF(0.0f, 0.0f, 1.0f),&nn);
            nn *= 1.0f / pvl;
            VectorF cv = contactNormal;
            cv -= nn * mDot(nn,cv);
            swimVec -= cv * mDot(swimVec,cv);
         }
      }

      F32 swimVecLen = swimVec.len();

      // Convert to acceleration.
      if ( swimVecLen )
         swimVec *= moveSpeed / swimVecLen;
      VectorF swimAcc = swimVec - (mVelocity + acc);
      F32 swimSpeed = swimAcc.len();

      // Clamp acceleration.
      F32 maxAcc = (mDataBlock->swimForce / getMass()) * TickSec;
      if ( false && swimSpeed > maxAcc )
         swimAcc *= maxAcc / swimSpeed;      

      acc += swimAcc;

     mContactTimer += TickMs;	//Ubiq: mContactTimer is now in ms (not ticks)
   }
	
   //Ubiq: Lorne: Enter the jump state?
   if (move->trigger[sJumpTrigger] && !isMounted() && !isAnimationLocked())
   {
      if (canJump() && !mJumpState.active)
      {
         //If we have directional input or if we're running, this will be a run-jump
         if (moveVec.len() > 0 || mVelocity.len() > 3.0f)
         {
            //enter the jump state
            mJumpState.active = true;
            mJumpState.isCrouching = true;
            mJumpState.crouchDelay = mDataBlock->runJumpCrouchDelay;
            mJumpState.jumpType = JumpType_Run;
         }

         //If no directional input and we're at walking speed (or stopped), this will be a stand-jump
         else
         {
            //enter the jump state
            mJumpState.active = true;
            mJumpState.isCrouching = true;
            mJumpState.crouchDelay = mDataBlock->standJumpCrouchDelay;
            mJumpState.jumpType = JumpType_Stand;
         }

         //play crouch sound for jump
         if (isGhost())
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::jumpCrouch), &getTransform());

         //cancel any playing animations
         mActionAnimation.action = AAKPlayerData::NullAnimation;
      }
      else
      {
         if (mJumpSurface)
         {
            if (mJumpDelay > 0)
               mJumpDelay--;
            mJumpSurfaceLastContact = 0;
         }
         else
            mJumpSurfaceLastContact++;
      }
   }


   // Acceleration from Jumping
   // While BLOCK_USER_CONTROL is set in anim_clip_flags, the user won't be able to
   // make the player character jump.
   //if (move->trigger[sJumpTrigger] && canJump() && !isAnimationLocked() && mJumpState.active && mJumpState.isCrouching)
   if(mJumpState.active && mJumpState.isCrouching && !isAnimationLocked())
   {      
      mJumpState.crouchDelay -= TickMs;

      if(mJumpState.jumpType == JumpType_Stand)
      {
         //freeze the player
         mVelocity.zero();
         acc.zero();
      }

      //jump if the crouch timer expires or if we lose our
      //jump-surface (meaning we've just gone off the edge!)
      if(mJumpState.crouchDelay <= 0 || !mJumpSurface)
      {
         mJumpState.isCrouching = false;
         mJumping = true;      // Scale the jump impulse base on maxJumpSpeed
         F32 zSpeedScale = mVelocity.z;
         if (zSpeedScale <= mDataBlock->maxJumpSpeed)
         {
            zSpeedScale = (zSpeedScale <= mDataBlock->minJumpSpeed)? 1:
            1 - (zSpeedScale - mDataBlock->minJumpSpeed) /
            (mDataBlock->maxJumpSpeed - mDataBlock->minJumpSpeed);

            // We want to scale the jump size by the player size, somewhat
            // in reduced ratio so a smaller player can jump higher in
            // proportion to his size, than a larger player.
            F32 scaleZ = (getScale().z * 0.25) + 0.75;

            // Calculate our jump impulse
            F32 impulse = mDataBlock->jumpForce / getMass();
            acc.z += scaleZ * impulse * zSpeedScale;
         }

         //if it's a run-jump...
         if(mJumpState.jumpType == JumpType_Run)
         {
            VectorF jumpVec;
            if(moveVec.len() > 0)
            {
               jumpVec.set(moveVec);

               //also, in third-person snap rotation to the direction of the jump
               //(in case the player changed their mind during the crouch)
               if(con && !con->isFirstPerson())
               {
                  F32 yaw, pitch;
                  MathUtils::getAnglesFromVector(moveVec, yaw, pitch);
                  mRot.z = yaw;
               }
            }
            else
               getTransform().getColumn(1, &jumpVec);
				
            //Force a specific velocity in the direction we're trying to move
            //This gives the jump some serious "oomph" even if we're barely moving
            if (mVelocity.len() < mDataBlock->maxForwardSpeed)
               mVelocity.normalize(mDataBlock->maxForwardSpeed);

            jumpVec.normalize(mVelocity.len());

            mVelocity.x = 0;
            mVelocity.y = 0;
            acc.x = jumpVec.x;
            acc.y = jumpVec.y;

            //so each jump is more similar
            //(specifically prevents weak jumps when running downhill)
            if(mVelocity.z < 0)
               mVelocity.z = 0;
         }

         mJumpDelay = mDataBlock->jumpDelay;
         mEnergy -= mDataBlock->jumpEnergyDrain;
         mJumpSurfaceLastContact = JumpSkipContactsMax;

         //play jump sound
         if (isGhost())
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::jump), &getTransform());

         // Flag the jump event trigger.
         fx_s_triggers |= PLAYER_JUMP_S_TRIGGER;
         setMaskBits(TriggerMask);
      }
   }
   else
   {
      if (mJumpSurface) 
      {
         if (mJumpDelay > 0)
            mJumpDelay--;
         mJumpSurfaceLastContact = 0;
      }
      else
         mJumpSurfaceLastContact++;
   }

   if (move->trigger[sJumpJetTrigger] && !isMounted() && canJetJump() && !isAnimationLocked())
	{
      mJetting = true;

		//only apply jetting if our upward velocity is within range
		if ((mVelocity.z <= mDataBlock->jetMaxJumpSpeed) && (mVelocity.z >= mDataBlock->jetMinJumpSpeed))
		{
			F32 impulse = mDataBlock->jetJumpForce / mMass;
			acc.z += impulse * TickSec;
         mEnergy -= mDataBlock->jetJumpEnergyDrain;
		}
	}
	else
	{
      mJetting = false;
	}

	if (mJetting)
	{
		F32 newEnergy = mEnergy - mDataBlock->minJumpEnergy;

		if (newEnergy < 0)
		{
			newEnergy = 0;
			mJetting = false;
		}

		mEnergy = newEnergy;
	}

	//Ubiq: wall hug gives you immunity from physical zones
	if(!mWallHugState.active)
	{
		// Add in force from physical zones...
		acc += (mAppliedForce / getMass()) * TickSec;
	}

   // Adjust velocity with all the move & gravity acceleration
   // TG: I forgot why doesn't the TickSec multiply happen here...
   mVelocity += acc;

   // apply horizontal air resistance

   F32 hvel = mSqrt(mVelocity.x * mVelocity.x + mVelocity.y * mVelocity.y);

   if(hvel > mDataBlock->horizResistSpeed)
   {
      F32 speedCap = hvel;
      if(speedCap > mDataBlock->horizMaxSpeed)
         speedCap = mDataBlock->horizMaxSpeed;
      speedCap -= mDataBlock->horizResistFactor * TickSec * (speedCap - mDataBlock->horizResistSpeed);
      F32 scale = speedCap / hvel;
      mVelocity.x *= scale;
      mVelocity.y *= scale;
   }
   if(mVelocity.z > mDataBlock->upResistSpeed)
   {
      if(mVelocity.z > mDataBlock->upMaxSpeed)
         mVelocity.z = mDataBlock->upMaxSpeed;
      mVelocity.z -= mDataBlock->upResistFactor * TickSec * (mVelocity.z - mDataBlock->upResistSpeed);
   }

	//apply ground friction
   if (!contactNormal.isZero())
   {
      F32 curFriction = mDataBlock->groundFriction;
      if (mSlideSurface)
         curFriction *= mFabs(contactNormal.z);
      mVelocity -= mVelocity * curFriction * TickSec;
   }

   F32 vertDrag;
   if ((mFalling) && ((move->trigger[sJumpTrigger]) || (this->mIsAiControlled)))
      vertDrag = mDataBlock->vertDragFalling;
   else
      vertDrag = mDataBlock->vertDrag;

   VectorF angledDrag = VectorF(mDrag, mDrag, vertDrag);
   // Container buoyancy & drag
   if (mBuoyancy != 0)
   {     
      // Applying buoyancy when standing still causing some jitters-
      if (mBuoyancy > 1.0 || !mVelocity.isZero() || !mRunSurface)
      {
         // A little hackery to prevent oscillation
         // based on http://reinot.blogspot.com/2005/11/oh-yes-they-float-georgie-they-all.html

         F32 buoyancyForce = mBuoyancy * mNetGravity * TickSec;
         F32 currHeight = getPosition().z;
         const F32 C = 2.0f;
         const F32 M = 0.1f;
         
         if ( currHeight + mVelocity.z * TickSec * C > mLiquidHeight )
            buoyancyForce *= M;
                  
         //mVelocity.z -= buoyancyForce;
      }
   }

   // Apply drag
   if (mSwimming)
      mVelocity -= mVelocity * angledDrag * TickSec * (mVelocity.len() / mDataBlock->maxUnderwaterForwardSpeed);
   else
      mVelocity -= mVelocity * angledDrag * TickSec;

   // Clamp to our sanity maximum velocity
   mVelocity.x = mClampF(mVelocity.x, -sMaxVelocity, sMaxVelocity);
   mVelocity.y = mClampF(mVelocity.y, -sMaxVelocity, sMaxVelocity);
   mVelocity.z = mClampF(mVelocity.z, -sMaxVelocity, sMaxVelocity);

   // Clamp very small velocity to zero
   if ( mVelocity.isZero() )
      mVelocity = Point3F::Zero;

   // If we are not touching anything and have sufficient -z vel,
   // we are falling.
   if (mContactTimer < sContactTickTime)	//Ubiq: Lorne: check the contactTimer instead of mRunSurface
   {
      mFalling = false;
   }
   else
   {
      VectorF vel;
      mWorldToObj.mulV(mVelocity,&vel);
      mFalling = vel.z < mDataBlock->fallingSpeedThreshold;
   }
   
   // Vehicle Dismount   
   if ( !isGhost() && move->trigger[sVehicleDismountTrigger] && canJump())
      mDataBlock->doDismount_callback( this );

   // Enter/Leave Liquid
   if ( !mInWater && mWaterCoverage > 0.0f ) 
   {      
      mInWater = true;

      if ( !isGhost() )
         mDataBlock->onEnterLiquid_callback( this, mWaterCoverage, mLiquidType.c_str() );
   }
   else if ( mInWater && mWaterCoverage <= 0.0f ) 
   {      
      mInWater = false;

      if ( !isGhost() )
         mDataBlock->onLeaveLiquid_callback( this, mLiquidType.c_str() );
      else
      {
         // exit-water splash sound happens for client only
         if ( getSpeed() >= mDataBlock->exitSplashSoundVel && !isMounted() )         
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ExitWater), &getTransform());
      }
   }

   // Update the PlayerPose
   Pose desiredPose = mPose;

   if ( mSwimming )
      desiredPose = SwimPose; 
   else if ( mRunSurface && move->trigger[sCrouchTrigger] && canCrouch() )     
      desiredPose = CrouchPose;
   else if ( mRunSurface && move->trigger[sProneTrigger] && canProne() )
      desiredPose = PronePose;
   else if ( move->trigger[sSprintTrigger] && canSprint() )
      desiredPose = SprintPose;
   else if ( canStand() )
      desiredPose = StandPose;

   setPose( desiredPose );
}

//----------------------------------------------------------------------------

bool AAKPlayer::canJump()
{
   return mAllowJumping && mState == MoveState && mDamageState == Enabled && !isMounted() && !mJumpDelay && mEnergy >= mDataBlock->minJumpEnergy && mJumpSurfaceLastContact < JumpSkipContactsMax && !mSwimming && (mPose != SprintPose || mDataBlock->sprintCanJump) && !mJumpState.active;
}

bool AAKPlayer::canJetJump()
{
   return mAllowJetJumping && mState == MoveState && mDamageState == Enabled && !isMounted() && mEnergy >= mDataBlock->jetMinJumpEnergy && mDataBlock->jetJumpForce != 0.0f && mJumping && mContactTimer < mDataBlock->jetTime;
}

bool AAKPlayer::canCrouch()
{
   if (!mAllowCrouching)
      return false;

   if ( mState != MoveState || 
        mDamageState != Enabled || 
        isMounted() || 
        mSwimming ||
        mFalling )
      return false;

   // Can't crouch if no crouch animation!
   if ( mDataBlock->actionList[AAKPlayerData::CrouchRootAnim].sequence == -1 )
      return false;       

	// We are already in this pose, so don't test it again...
	if ( mPose == CrouchPose )
		return true;

   // Do standard Torque physics test here!
   if ( !mPhysicsRep )
   {
      F32 radius;

      if ( mPose == PronePose )
         radius = mDataBlock->proneBoxSize.z;
      else
         return true;

      // use our X and Y dimentions on our boxsize as the radii for our search, and the difference between a standing position
      // and the position we currently are in.
      Point3F extent( mDataBlock->crouchBoxSize.x / 2, mDataBlock->crouchBoxSize.y / 2, mDataBlock->crouchBoxSize.z - radius );

      Point3F position = getPosition();
      position.z += radius;

      // Use these radii to create a box that represents the difference between a standing position and the position
      // we want to move into.
      Box3F B(position - extent, position + extent, true);

      EarlyOutPolyList polyList;
      polyList.mPlaneList.clear();
      polyList.mNormal.set( 0,0,0 );
      polyList.mPlaneList.setSize( 6 );
      polyList.mPlaneList[0].set( B.minExtents, VectorF( -1,0,0 ) );
      polyList.mPlaneList[1].set( B.maxExtents, VectorF( 0,1,0 ) );
      polyList.mPlaneList[2].set( B.maxExtents, VectorF( 1,0,0 ) );
      polyList.mPlaneList[3].set( B.minExtents, VectorF( 0,-1,0 ) );
      polyList.mPlaneList[4].set( B.minExtents, VectorF( 0,0,-1 ) );
      polyList.mPlaneList[5].set( B.maxExtents, VectorF( 0,0,1 ) );

      // If an object exists in this space, we must stay prone. Otherwise we are free to crouch.
      return !getContainer()->buildPolyList( PLC_Collision, B, StaticShapeObjectType, &polyList );
   }

   return mPhysicsRep->testSpacials( getPosition(), mDataBlock->crouchBoxSize );
}

//----------------------------------------------------------------------------

bool AAKPlayer::setActionThread(const char* sequence, bool forward, bool hold, bool wait, bool fsp, bool forceSet, bool useSynchedPos)
{
   if (anim_clip_flags & ANIM_OVERRIDDEN)
      return false;
   for (U32 i = 1; i < mDataBlock->actionCount; i++)
   {
      AAKPlayerData::ActionAnimation &anim = mDataBlock->actionList[i];
      if (!dStricmp(anim.name,sequence))
      {
         setActionThread(i,forward,hold,wait,fsp,forceSet,useSynchedPos);
         setMaskBits(ActionMask);
         return true;
      }
   }
   return false;
}

void AAKPlayer::setActionThread(U32 action, bool forward, bool hold, bool wait, bool fsp, bool forceSet, bool useSynchedPos)
{
   //are we already playing this animation (in the same direction)?
   if (mActionAnimation.action == action && mActionAnimation.forward == forward && !forceSet)
      return;

   if (isAnimationLocked())
      return;

   //is it a valid animation?
   if (action >= AAKPlayerData::NumActionAnims)
   {
      Con::errorf("AAKPlayer::setActionThread(%d): Player action out of range", action);
      return;
   }

   //are we just reversing the timescale of the same animation?
   bool reversingSameAnimation = mActionAnimation.action == action && mActionAnimation.forward != forward;

   //do both animations (the one playing and the one we are switching to) use synched pos?
   bool bothUseSynchedPos = mActionAnimation.useSynchedPos && useSynchedPos;	

   bool isClient = isClientObject();
   if (isClientObject())
   {
      mark_idle = (action == AAKPlayerData::RootAnim);
      idle_timer = (mark_idle) ? 0.0f : -1.0f;
   }
   AAKPlayerData::ActionAnimation &anim = mDataBlock->actionList[action];
   if (anim.sequence != -1)
   {
      mActionAnimation.action          = action;
      mActionAnimation.forward         = forward;
      mActionAnimation.firstPerson     = fsp;
      mActionAnimation.holdAtEnd       = hold;
      mActionAnimation.waitForEnd      = hold? true: wait;
      mActionAnimation.animateOnServer = fsp;
      mActionAnimation.atEnd           = false;
      mActionAnimation.delayTicks      = (S32)sNewAnimationTickTime;
      mActionAnimation.useSynchedPos   = useSynchedPos;
      mActionAnimation.callbackTripped = false;

      if (sUseAnimationTransitions && (isGhost() || mActionAnimation.animateOnServer))
      {
         // The transition code needs the timeScale to be set in the
         // right direction to know which way to go.
         F32   transTime = sAnimationTransitionTime;

			//Stop animation needs a fast blend to look right
			if(action == AAKPlayerData::StopAnim)
				transTime = 0.15f;

			//Jump, ledge and climb actions need a faster blend to look right
			if(mDataBlock && (mDataBlock->isJumpAction(action)
            || mDataBlock->isLedgeAction(action)
            || mDataBlock->isClimbAction(action)))
				transTime = 0.1f;

			//Land animations need a super-fast blend to look right
			if(action == AAKPlayerData::StandingLandAnim
				|| action == AAKPlayerData::RunningLandAnim)
				transTime = 0.05f;

			F32 pos;
			if(reversingSameAnimation || bothUseSynchedPos)
				pos = mShapeInstance->getPos(mActionAnimation.thread);
			else
				pos = mActionAnimation.forward ? 0.0f : 1.0f;

         mShapeInstance->setTimeScale(mActionAnimation.thread, mActionAnimation.forward ? 1.0f : -1.0f);
         mShapeInstance->transitionToSequence(mActionAnimation.thread, anim.sequence, pos, transTime, true);
      }
      else
      {
         S32 seq = anim.sequence;
         S32 imageBasedSeq = convertActionToImagePrefix(mActionAnimation.action);
         if (imageBasedSeq != -1)
            seq = imageBasedSeq;

         mShapeInstance->setSequence(mActionAnimation.thread,seq,
            mActionAnimation.forward ? 0.0f : 1.0f);
      }
   }
}

void AAKPlayer::updateActionThread()
{
   PROFILE_START(AAKPlayer_UpdateActionThread);

   // Select an action animation sequence, this assumes that
   // this function is called once per tick.
   if(mActionAnimation.action != AAKPlayerData::NullAnimation)
      if (mActionAnimation.forward)
         mActionAnimation.atEnd = mShapeInstance->getPos(mActionAnimation.thread) == 1;
      else
         mActionAnimation.atEnd = mShapeInstance->getPos(mActionAnimation.thread) == 0;

   // Only need to deal with triggers on the client
   if( isGhost() )
   {
      bool triggeredLeft = false;
      bool triggeredRight = false;
      bool triggeredSound = false;

      F32 offset = 0.0f;
      if( mShapeInstance->getTriggerState( 1 ) )
      {
         triggeredLeft = true;
         offset = -mDataBlock->decalOffset * getScale().x;
      }
      else if(mShapeInstance->getTriggerState( 2 ) )
      {
         triggeredRight = true;
         offset = mDataBlock->decalOffset * getScale().x;
      }
      if(mShapeInstance->getTriggerState(3))
      {
         triggeredSound = true;
      }

      process_client_triggers(triggeredLeft, triggeredRight);
      if ((triggeredLeft || triggeredRight) && !noFootfallFX)
      {
         Point3F rot, pos;
         RayInfo rInfo;
         MatrixF mat = getRenderTransform();
         mat.getColumn( 1, &rot );
         mat.mulP( Point3F( offset, 0.0f, 0.0f), &pos );

         if( gClientContainer.castRay( Point3F( pos.x, pos.y, pos.z + 0.07f ),
               Point3F( pos.x, pos.y, pos.z - 2.0f ),
               STATIC_COLLISION_TYPEMASK | VehicleObjectType, &rInfo ) )
         {
            Material* material = ( rInfo.material ? dynamic_cast< Material* >( rInfo.material->getMaterial() ) : 0 );

            // Put footprints on surface, if appropriate for material.

            if( material && material->mShowFootprints
                && mDataBlock->decalData && !footfallDecalOverride )
            {
               Point3F normal;
               Point3F tangent;
               mObjToWorld.getColumn( 0, &tangent );
               mObjToWorld.getColumn( 2, &normal );
               gDecalManager->addDecal( rInfo.point, normal, tangent, mDataBlock->decalData, getScale().y );
            }
            
            // Emit footpuffs.

            if (!footfallDustOverride && rInfo.t <= 0.5f && mWaterCoverage == 0.0f
                                         && material && material->mShowDust
                                         && mDataBlock->footPuffEmitter != nullptr)
            {
               // New emitter every time for visibility reasons
               ParticleEmitter * emitter = new ParticleEmitter;
               emitter->onNewDataBlock( mDataBlock->footPuffEmitter, false );

               LinearColorF colorList[ ParticleData::PDC_NUM_KEYS];

               for( U32 x = 0; x < getMin( Material::NUM_EFFECT_COLOR_STAGES, ParticleData::PDC_NUM_KEYS ); ++ x )
                  colorList[ x ].set( material->mEffectColor[ x ].red,
                                      material->mEffectColor[ x ].green,
                                      material->mEffectColor[ x ].blue,
                                      material->mEffectColor[ x ].alpha );
               for( U32 x = Material::NUM_EFFECT_COLOR_STAGES; x < ParticleData::PDC_NUM_KEYS; ++ x )
                  colorList[ x ].set( 1.0, 1.0, 1.0, 0.0 );

               emitter->setColors( colorList );
               if( !emitter->registerObject() )
               {
                  Con::warnf( ConsoleLogEntry::General, "Could not register emitter for particle of class: %s", mDataBlock->getName() );
                  delete emitter;
                  emitter = NULL;
               }
               else
               {
                  emitter->emitParticles( pos, Point3F( 0.0, 0.0, 1.0 ), mDataBlock->footPuffRadius,
                     Point3F( 0, 0, 0 ), mDataBlock->footPuffNumParts );
                  emitter->deleteWhenEmpty();
               }
            }

            // Play footstep sound.
            
            if (footfallSoundOverride <= 0)
               playFootstepSound( triggeredLeft, material, rInfo.object );
         }
      }

		//Ubiq: deal with "special" triggered sounds
		//TODO: this could be handled in a more elegant way
		if(triggeredSound)
		{
			//climb idle
			if(mActionAnimation.action == AAKPlayerData::ClimbIdleAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::climbIdle), &getTransform());
			}

			//climb up
			if(mActionAnimation.action == AAKPlayerData::ClimbUpAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::climbUp), &getTransform());
			}

			//climb left or right
			if(mActionAnimation.action == AAKPlayerData::ClimbLeftAnim || mActionAnimation.action == AAKPlayerData::ClimbRightAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::climbLeftRight), &getTransform());
			}

			//climb down
			if(mActionAnimation.action == AAKPlayerData::ClimbDownAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::climbDown), &getTransform());
			}

			//ledge idle
			if(mActionAnimation.action == AAKPlayerData::LedgeIdleAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ledgeIdle), &getTransform());
			}

			//ledge left or right
			if(mActionAnimation.action == AAKPlayerData::LedgeLeftAnim || mActionAnimation.action == AAKPlayerData::LedgeRightAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ledgeLeftRight), &getTransform());
			}

			//ledge up
			if(mActionAnimation.action == AAKPlayerData::LedgeUpAnim)
			{
            SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ledgeUp), &getTransform());
			}
		}
	}

   // Mount pending variable puts a hold on the delayTicks below so players don't
   // inadvertently stand up because their mount has not come over yet.
   if (mMountPending)
      mMountPending = (isMounted() ? 0 : (mMountPending - 1));

   if (mActionAnimation.action == AAKPlayerData::NullAnimation || !mActionAnimation.waitForEnd || //either no animation or not waiting till the end
      ((mActionAnimation.atEnd && !mActionAnimation.holdAtEnd) && //or not holding that state and
         (mActionAnimation.delayTicks -= mMountPending) <= 0)) //not waiting to mount
   {
      //The scripting language will get a call back when a script animation has finished...
      //  example: When the chat menu animations are done playing...
      if (isServerObject() && mActionAnimation.action >= AAKPlayerData::NumTableActionAnims)
         mDataBlock->animationDone_callback(this, mActionAnimation.thread->getSequenceName().c_str());
      pickActionAnimation();
   }

   if ( (mActionAnimation.action != AAKPlayerData::StandingLandAnim) &&
      (mActionAnimation.action != AAKPlayerData::RunningLandAnim) &&
      (mActionAnimation.action != AAKPlayerData::NullAnimation)  && !(anim_clip_flags & ANIM_OVERRIDDEN))   {
      // Update action animation time scale to match ground velocity
      AAKPlayerData::ActionAnimation &anim =
         mDataBlock->actionList[mActionAnimation.action];
      F32 scale = 1;
      if (anim.velocityScale && anim.speed) {
         VectorF vel;
         mWorldToObj.mulV(mVelocity,&vel);
         scale = mFabs(mDot(vel, anim.dir) / anim.speed);

         if (scale > mDataBlock->maxTimeScale)
            scale = mDataBlock->maxTimeScale;
      }

      mShapeInstance->setTimeScale(mActionAnimation.thread,
                                   mActionAnimation.forward? scale: -scale);
   }
   PROFILE_END();
}

void AAKPlayer::pickActionAnimation()
{
   // Only select animations in our normal move state.
   //if (mState != MoveState || mDamageState != Enabled)	//Ubiq: we set the death animation in here, so don't return
   if (mState != MoveState)
      return;

   if (isMounted() || mMountPending)
   {
      // Go into root position unless something was set explicitly
      // from a script.
      if (mActionAnimation.action != AAKPlayerData::RootAnim &&
          mActionAnimation.action < AAKPlayerData::NumTableActionAnims)
         setActionThread(AAKPlayerData::RootAnim,true,false,false);
      return;
   }

   bool forward = true;
   bool useSynchedPos = false;

   U32 action = AAKPlayerData::RootAnim;

	//Ubiq: death overrides everything else
	if(mDamageState != Enabled)
	{
		action = AAKPlayerData::Death1Anim;
	}

	//Ubiq: climb down anim overrides ledge anims if animPos == 0
	else if (mLedgeState.active && mLedgeState.animPos == 0.0f
		&& mClimbState.active && mClimbState.direction == MOVE_DIR_DOWN)
	{
		action = AAKPlayerData::ClimbDownAnim;
	}

	//Ubiq: ledge grabbing, climbing up
	else if (mLedgeState.active && mLedgeState.climbingUp)
	{
		action = AAKPlayerData::LedgeUpAnim;
	}

	//Ubiq: ledge grabbing, regular 
	else if(mLedgeState.active)
	{
		//idle by default
		action = AAKPlayerData::LedgeIdleAnim;

		//are we moving fast enough to warrant a move animation?
		if(mVelocity.len() >= 0.1f)
		{
			//choose the appropriate animation
			if(mLedgeState.direction == MOVE_DIR_LEFT)
				action = AAKPlayerData::LedgeLeftAnim;
			if(mLedgeState.direction == MOVE_DIR_RIGHT)
				action = AAKPlayerData::LedgeRightAnim;
		}
	}

	//Ubiq: climbing
	else if(mClimbState.active)
	{
		//idle by default
		action = AAKPlayerData::ClimbIdleAnim;

		//are we moving fast enough to warrant a move animation?
		if(mVelocity.len() >= 0.1f)
		{
			//choose the appropriate animation
			switch(mClimbState.direction)
			{
			case MOVE_DIR_UP:
				action = AAKPlayerData::ClimbUpAnim;
				break;

			case MOVE_DIR_DOWN:
				action = AAKPlayerData::ClimbDownAnim;
				break;

			case MOVE_DIR_LEFT:
				action = AAKPlayerData::ClimbLeftAnim;
				break;

			case MOVE_DIR_RIGHT:
				action = AAKPlayerData::ClimbRightAnim;
				break;

			default:
				action = AAKPlayerData::ClimbIdleAnim;
			}
		}
	}

	else if(mWallHugState.active)
	{
		//idle by default
		action = AAKPlayerData::WallIdleAnim;

		//are we moving fast enough to warrant a move animation?
		if(mVelocity.len() >= 0.1f)
		{
			//choose the appropriate animation
			switch(mWallHugState.direction)
			{
			case MOVE_DIR_LEFT:
				action = AAKPlayerData::WallLeftAnim;
				break;

			case MOVE_DIR_RIGHT:
				action = AAKPlayerData::WallRightAnim;
				break;

			default:
				action = AAKPlayerData::WallIdleAnim;
			}
		}
	}

	// Jetting overrides the fall animation condition
	/*else if (mJetting)
	{
		// Play the jetting animation
		action = AAKPlayerData::JetAnim;
	}*/

	//Ubiq: Sliding
	else if (mSlideState.active)
	{
		Point3F forwardVec;
		getTransform().getColumn(1, &forwardVec);

		if(mDot(mSlideState.surfaceNormal, forwardVec) < 0)
			action = AAKPlayerData::SlideBackAnim;
		else
			action = AAKPlayerData::SlideFrontAnim;
	}

   else if (mFalling)
   {
      // Not in contact with any surface and falling
      action = AAKPlayerData::FallAnim;
   }

	//Ubiq: jump animations
	else if(mJumpState.active)
	{
		switch(mJumpState.jumpType)
		{
		case JumpType_Run:
			action = AAKPlayerData::JumpAnim;
			break;

		case JumpType_Stand:
			action = AAKPlayerData::StandJumpAnim;
			break;
		}
	}

	else
   {
      if (mContactTimer >= sContactTickTime)
      {
         // Nothing under our feet
         action = AAKPlayerData::RootAnim;
      }
      else
      {
			//Ubiq: Lorne:

			//default
			action = AAKPlayerData::RootAnim;

			//get velocity relative to player
			VectorF vel;
			mWorldToObj.mulV(mVelocity,&vel);

			//are we moving?
			if(vel.len() > 0.01f)
			{
				//forward
				if(mDot(vel, Point3F(0,1,0)) > 0.5)
				{
					//run
					if(mVelocity.len() > mDataBlock->walkRunAnimVelocity)
					{
						action = AAKPlayerData::RunForwardAnim;
						useSynchedPos = true;
					}

					//walk
					else
					{
						action = AAKPlayerData::WalkForwardAnim;
						useSynchedPos = true;
					}
				}

				//backward
				//else if(mDot(vel, Point3F(0,-1,0)) > 0.5)
				//	action = AAKPlayerData::BackBackwardAnim;

				//left
				//else if(mDot(vel, Point3F(-1,0,0)) > 0.5)
					//action = AAKPlayerData::WallLeftAnim;

				//right
				//else if(mDot(vel, Point3F(1,0,0)) > 0.5)
					//action = AAKPlayerData::WallRightAnim;
			}
		}
	}
	setActionThread(action,forward,false,false,true,false,useSynchedPos);
}

//----------------------------------------------------------------------------

//Ubiq: Lorne: modified to use ConcretePolyList instead of ClippedPolyList
//the latter was allowing players to "step" up steep slopes even when no suitable
//ground existed above. The new algorithm looks for suitable edges to step up,
//similar to the way findLedgeContact works
bool AAKPlayer::step(Point3F *pos,F32 *maxStep,F32 time)
{
   const Point3F& scale = getScale();

	//Ubiq: Lorne: new way to calculate offset. The old way
	//(mVelocity * time) caused issues when velocity was near 0
	Point3F forward;
	getTransform().getColumn(1, &forward);
	Point3F offset(forward);
	offset.normalize(0.2f);
   Box3F box;
   box.minExtents = mObjBox.minExtents + offset + *pos;
   box.maxExtents = mObjBox.maxExtents + offset + *pos;
   box.maxExtents.z += mDataBlock->maxStepHeight * scale.z + sMinFaceDistance;

	ConcretePolyList polyList;
	CollisionWorkingList& rList = mConvex.getWorkingList();
	CollisionWorkingList* pList = rList.wLink.mNext;
	while (pList != &rList)
	{
		Convex* pConvex = pList->mConvex;

		// Alright, here's the deal... a polysoup mesh really needs to be 
		// designed with stepping in mind.  If there are too many smallish polygons
		// the stepping system here gets confused and allows you to run up walls 
		// or on the edges/seams of meshes.

		TSStatic *st = dynamic_cast<TSStatic *> (pConvex->getObject());
		bool skip = false;
		if (st && !st->allowPlayerStep())
			skip = true;

		if ((pConvex->getObject()->getTypeMask() & StaticObjectType) != 0 && !skip)
		{
			Box3F convexBox = pConvex->getBoundingBox();
			if (box.isOverlapped(convexBox))
				pConvex->getPolyList(&polyList);
		}
		pList = pList->wLink.mNext;
	}

	ConcretePolyList::Poly* poly = polyList.mPolyList.begin();
	ConcretePolyList::Poly* end = polyList.mPolyList.end();

	for (; poly != end; poly++)
	{
		//upward-facing surface?
		//if it's upward facing we could potentially stand on it
		if(poly->plane.z > 0.5) 
		{
			//loop through all the verticies of this polygon
			for (S32 i=poly->vertexStart; i < poly->vertexStart + poly->vertexCount; i++)
			{
				S32 vertex1Index = i;
				S32 vertex2Index = i + 1;

				//the last vertex is connected to the first
				if(i == poly->vertexStart + poly->vertexCount - 1) 
					vertex2Index = poly->vertexStart;

				//get the verticies
				Point3F vertex1 = polyList.mVertexList[polyList.mIndexList[vertex1Index]];
				Point3F vertex2 = polyList.mVertexList[polyList.mIndexList[vertex2Index]];

				//Now we're going to collide the edge with our box. We'll do
				//this from both directions and use the higher collision point.
				//This is neccessary when one vertex is higher than the other. We want
				//to make sure we step up enough to actually fit our collision box over the highest end
				F32 t1; Point3F n1;
				bool collided1 = box.collideLine(vertex1, vertex2, &t1, &n1);
				Point3F collisionPoint1;
				if(collided1)
				{
					//find the point of collision
					collisionPoint1.interpolate(vertex1, vertex2, t1);
				}

				F32 t2; Point3F n2;
				bool collided2 = box.collideLine(vertex2, vertex1, &t2, &n2);
				Point3F collisionPoint2;
				if(collided2)
				{
					//find the point of collision
					collisionPoint2.interpolate(vertex2, vertex1, t2);
				}

				//does this edge pass through our box?
				if(collided1 && collided2)
				{
					Point3F collisionPoint;

					//choose the higher collision point to use
					if(collisionPoint1.z > collisionPoint2.z)
						collisionPoint = collisionPoint1;
					else
						collisionPoint = collisionPoint2;

					F32 step = collisionPoint.z - pos->z;
					if(collisionPoint.z > pos->z && step < *maxStep)
					{
#ifdef ENABLE_DEBUGDRAW
                  if (sRenderHelpers)
                  {
                     //draw the edge
                     DebugDrawer::get()->drawLine(vertex1, vertex2, LinearColorF(0.5f, 0, 0));
                     DebugDrawer::get()->setLastTTL(2000);

                     //calculate the "normal" of this edge
                     Point3F normal = mCross(Point3F(0, 0, 1), vertex2 - vertex1);
                     normal.normalizeSafe();

                     //draw the normal at the collisionPoint we used
                     DebugDrawer::get()->drawLine(collisionPoint, collisionPoint + normal, LinearColorF(0, 0.5f, 0));
                     DebugDrawer::get()->setLastTTL(2000);
                  }
#endif

						// Go ahead and step
						pos->z = collisionPoint.z + sMinFaceDistance;
						return true;
					}
				}
			}
		}
	}

   return false;
}

//----------------------------------------------------------------------------
Point3F AAKPlayer::_move( const F32 travelTime, Collision *outCol )
{
   // Try and move to new pos
   F32 totalMotion  = 0.0f;
   
   // TODO: not used?
   //F32 initialSpeed = mVelocity.len();

   Point3F start;
   Point3F initialPosition;
   getTransform().getColumn(3,&start);
   initialPosition = start;

   static CollisionList collisionList;
   static CollisionList physZoneCollisionList;

   collisionList.clear();
   physZoneCollisionList.clear();

   MatrixF collisionMatrix(true);
   collisionMatrix.setColumn(3, start);

   VectorF firstNormal(0.0f, 0.0f, 0.0f);
   F32 maxStep = mDataBlock->maxStepHeight;
   F32 time = travelTime;
   U32 count = 0;

   const Point3F& scale = getScale();

   static Polyhedron sBoxPolyhedron;
   static ExtrudedPolyList sExtrudedPolyList;
   static ExtrudedPolyList sPhysZonePolyList;

   for (; count < sMoveRetryCount; count++) {
      F32 speed = mVelocity.len();
      if (!speed && !mDeath.haveVelocity())
         break;

      Point3F end = start + mVelocity * time;
      if (mDeath.haveVelocity()) {
         // Add in death movement-
         VectorF  deathVel = mDeath.getPosAdd();
         VectorF  resVel;
         getTransform().mulV(deathVel, & resVel);
         end += resVel;
      }
      Point3F distance = end - start;

      if (mFabs(distance.x) < mObjBox.len_x() &&
          mFabs(distance.y) < mObjBox.len_y() &&
          mFabs(distance.z) < mObjBox.len_z())
      {
         // We can potentially early out of this.  If there are no polys in the clipped polylist at our
         //  end position, then we can bail, and just set start = end;
         Box3F wBox = mScaledBox;
         wBox.minExtents += end;
         wBox.maxExtents += end;

         static EarlyOutPolyList eaPolyList;
         eaPolyList.clear();
         eaPolyList.mNormal.set(0.0f, 0.0f, 0.0f);
         eaPolyList.mPlaneList.clear();
         eaPolyList.mPlaneList.setSize(6);
         eaPolyList.mPlaneList[0].set(wBox.minExtents,VectorF(-1.0f, 0.0f, 0.0f));
         eaPolyList.mPlaneList[1].set(wBox.maxExtents,VectorF(0.0f, 1.0f, 0.0f));
         eaPolyList.mPlaneList[2].set(wBox.maxExtents,VectorF(1.0f, 0.0f, 0.0f));
         eaPolyList.mPlaneList[3].set(wBox.minExtents,VectorF(0.0f, -1.0f, 0.0f));
         eaPolyList.mPlaneList[4].set(wBox.minExtents,VectorF(0.0f, 0.0f, -1.0f));
         eaPolyList.mPlaneList[5].set(wBox.maxExtents,VectorF(0.0f, 0.0f, 1.0f));

         // Build list from convex states here...
         CollisionWorkingList& rList = mConvex.getWorkingList();
         CollisionWorkingList* pList = rList.wLink.mNext;
         while (pList != &rList) {
            Convex* pConvex = pList->mConvex;
            if (pConvex->getObject()->getTypeMask() & sCollisionMoveMask) {
               Box3F convexBox = pConvex->getBoundingBox();
               if (wBox.isOverlapped(convexBox))
               {
                  // No need to separate out the physical zones here, we want those
                  //  to cause a fallthrough as well...
                  pConvex->getPolyList(&eaPolyList);
               }
            }
            pList = pList->wLink.mNext;
         }

         if (eaPolyList.isEmpty())
         {
            totalMotion += (end - start).len();
            start = end;
            break;
         }
      }

      collisionMatrix.setColumn(3, start);
      sBoxPolyhedron.buildBox(collisionMatrix, mScaledBox, true);

      // Setup the bounding box for the extrudedPolyList
      Box3F plistBox = mScaledBox;
      collisionMatrix.mul(plistBox);
      Point3F oldMin = plistBox.minExtents;
      Point3F oldMax = plistBox.maxExtents;
      plistBox.minExtents.setMin(oldMin + (mVelocity * time) - Point3F(0.1f, 0.1f, 0.1f));
      plistBox.maxExtents.setMax(oldMax + (mVelocity * time) + Point3F(0.1f, 0.1f, 0.1f));

      // Build extruded polyList...
      VectorF vector = end - start;
      sExtrudedPolyList.extrude(sBoxPolyhedron,vector);
      sExtrudedPolyList.setVelocity(mVelocity);
      sExtrudedPolyList.setCollisionList(&collisionList);

      sPhysZonePolyList.extrude(sBoxPolyhedron,vector);
      sPhysZonePolyList.setVelocity(mVelocity);
      sPhysZonePolyList.setCollisionList(&physZoneCollisionList);

      // Build list from convex states here...
      CollisionWorkingList& rList = mConvex.getWorkingList();
      CollisionWorkingList* pList = rList.wLink.mNext;
      while (pList != &rList) {
         Convex* pConvex = pList->mConvex;
         if (pConvex->getObject()->getTypeMask() & sCollisionMoveMask) {
            Box3F convexBox = pConvex->getBoundingBox();
            if (plistBox.isOverlapped(convexBox))
            {
               if (pConvex->getObject()->getTypeMask() & PhysicalZoneObjectType)
                  pConvex->getPolyList(&sPhysZonePolyList);
               else
                  pConvex->getPolyList(&sExtrudedPolyList);
            }
         }
         pList = pList->wLink.mNext;
      }

      // Take into account any physical zones...
      for (U32 j = 0; j < physZoneCollisionList.getCount(); j++) 
      {
         AssertFatal(dynamic_cast<PhysicalZone*>(physZoneCollisionList[j].object), "Bad phys zone!");
         const PhysicalZone* pZone = (PhysicalZone*)physZoneCollisionList[j].object;
         if (pZone->isActive())
            mVelocity *= pZone->getVelocityMod();
      }

      if (collisionList.getCount() != 0 && collisionList.getTime() < 1.0f) 
      {
         // Set to collision point
         F32 velLen = mVelocity.len();

         F32 dt = time * getMin(collisionList.getTime(), 1.0f);
         start += mVelocity * dt;
         time -= dt;

         totalMotion += velLen * dt;

         bool wasFalling = mFalling;
         mFalling = false;

         // Back off...
         if ( velLen > 0.f ) {
            F32 newT = getMin(0.01f / velLen, dt);
            start -= mVelocity * newT;
            totalMotion -= velLen * newT;
         }

         // Try stepping if there is a vertical surface
         if (collisionList.getMaxHeight() < start.z + mDataBlock->maxStepHeight * scale.z) 
         {
            bool stepped = false;
            for (U32 c = 0; c < collisionList.getCount(); c++) 
            {
               const Collision& cp = collisionList[c];
               // if (mFabs(mDot(cp.normal,VectorF(0,0,1))) < sVerticalStepDot)
               //    Dot with (0,0,1) just extracts Z component [lh]-
               if (mFabs(cp.normal.z) < sVerticalStepDot)
               {
                  stepped = step(&start,&maxStep,time);
                  break;
               }
            }
            if (stepped)
            {
               continue;
            }
         }

         // Pick the surface most parallel to the face that was hit.
         const Collision *collision = &collisionList[0];
         const Collision *cp = collision + 1;
         const Collision *ep = collision + collisionList.getCount();
         for (; cp != ep; cp++)
         {
            if (cp->faceDot > collision->faceDot)
               collision = cp;
         }

         F32 bd = _doCollisionImpact( collision, wasFalling );

         // Copy this collision out so
         // we can use it to do impacts
         // and query collision.
         *outCol = *collision;
         if (isServerObject() && bd > 6.8f && collision->normal.z > 0.7f)
         {
            fx_s_triggers |= PLAYER_LANDING_S_TRIGGER;
            setMaskBits(TriggerMask);
         }

         // Subtract out velocity
		 //Ubiq: Lorne: use lower elasticity on ground than in air
		 F32 elasticity = mContactTimer == 0 ? sNormalElasticity : sAirElasticity; 
         VectorF dv = collision->normal * (bd + elasticity);
         mVelocity += dv;
         if (count == 0)
         {
            firstNormal = collision->normal;
         }
         else
         {
            if (count == 1)
            {
               // Re-orient velocity along the crease.
               if (mDot(dv,firstNormal) < 0.0f &&
                   mDot(collision->normal,firstNormal) < 0.0f)
               {
                  VectorF nv;
                  mCross(collision->normal,firstNormal,&nv);
                  F32 nvl = nv.len();
                  if (nvl)
                  {
                     if (mDot(nv,mVelocity) < 0.0f)
                        nvl = -nvl;
                     nv *= mVelocity.len() / nvl;
                     mVelocity = nv;
                  }
               }
            }
         }
      }
      else
      {
         totalMotion += (end - start).len();
         start = end;
         break;
      }
   }

   if (count == sMoveRetryCount)
   {
      // Failed to move
      start = initialPosition;
      mVelocity.set(0.0f, 0.0f, 0.0f);
   }

   return start;
}

F32 AAKPlayer::_doCollisionImpact( const Collision *collision, bool fallingCollision)
{
   F32 bd = -mDot( mVelocity, collision->normal);

   // shake camera on ground impact
   if( bd > mDataBlock->groundImpactMinSpeed && isControlObject() )
   {
      F32 ampScale = (bd - mDataBlock->groundImpactMinSpeed) / mDataBlock->minImpactSpeed;

      CameraShake *groundImpactShake = new CameraShake;
      groundImpactShake->setDuration( mDataBlock->groundImpactShakeDuration );
      groundImpactShake->setFrequency( mDataBlock->groundImpactShakeFreq );

      VectorF shakeAmp = mDataBlock->groundImpactShakeAmp * ampScale;
      groundImpactShake->setAmplitude( shakeAmp );
      groundImpactShake->setFalloff( mDataBlock->groundImpactShakeFalloff );
      groundImpactShake->init();
      gCamFXMgr.addFX( groundImpactShake );
   }

   if ( ((bd > mDataBlock->minImpactSpeed && fallingCollision) || bd > mDataBlock->minLateralImpactSpeed) 
      && !mMountPending )
   {
      if (!isGhost())
      {
         onImpact(collision->object, collision->normal * bd);
         mImpactSound = AAKPlayerData::ImpactNormal;
         setMaskBits(ImpactMask);
      }

      if (mDamageState == Enabled && mState != RecoverState) 
      {
         // Scale how long we're down for
         //Ubiq: removing these - we use our own landing system
         /*if (mDataBlock->landSequenceTime > 0.0f)
         {
            // Recover time is based on the land sequence
            setState(RecoverState);
         }
         else
         {
            // Legacy recover system
            F32   value = (bd - mDataBlock->minImpactSpeed);
            F32   range = (mDataBlock->minImpactSpeed * 0.9f);
            U32   recover = mDataBlock->recoverDelay;
            if (value < range)
               recover = 1 + S32(mFloor( F32(recover) * value / range) );
            //Con::printf("Used %d recover ticks", recover);
            //Con::printf("  minImpact = %g, this one = %g", mDataBlock->minImpactSpeed, bd);
            setState(RecoverState, recover);
         }*/
      }
   }

   return bd;
}

//----------------------------------------------------------------------------
 
void AAKPlayer::_findContact( SceneObject **contactObject, 
                           VectorF *contactNormal, 
                           Vector<SceneObject*> *outOverlapObjects )
{
   Parent::_findContact(contactObject, contactNormal, outOverlapObjects);

   Point3F pos;
   getTransform().getColumn(3,&pos);

   Box3F wBox;
   Point3F exp(0,0,sTractionDistance);
   wBox.minExtents = pos + mScaledBox.minExtents - exp;
   wBox.maxExtents.x = pos.x + mScaledBox.maxExtents.x;
   wBox.maxExtents.y = pos.y + mScaledBox.maxExtents.y;
   wBox.maxExtents.z = pos.z + mScaledBox.minExtents.z + sTractionDistance;
#ifdef ENABLE_DEBUGDRAW
   if (sRenderHelpers)
   {
      DebugDrawer::get()->drawBox(wBox.minExtents, wBox.maxExtents);
      DebugDrawer::get()->setLastTTL(TickMs);
   }
#endif
}

void AAKPlayer::findContact( bool *run, bool *jump, bool *slide, VectorF *contactNormal )
{
   SceneObject *contactObject = NULL;

   Vector<SceneObject*> overlapObjects;
   if ( mPhysicsRep )
      mPhysicsRep->findContact( &contactObject, contactNormal, &overlapObjects );
   else
      _findContact( &contactObject, contactNormal, &overlapObjects );

   // Check for triggers, corpses and items.
   const U32 filterMask = isGhost() ? sClientCollisionContactMask : sServerCollisionContactMask;
   for ( U32 i=0; i < overlapObjects.size(); i++ )
   {
      SceneObject *obj = overlapObjects[i];
      U32 objectMask = obj->getTypeMask();

      if ( !( objectMask & filterMask ) )
         continue;

      // Check: triggers, corpses and items...
      //
      if (objectMask & TriggerObjectType)
      {
         Trigger* pTrigger = static_cast<Trigger*>( obj );
         pTrigger->potentialEnterObject(this);
      }
      else if (objectMask & CorpseObjectType)
      {
         // If we've overlapped the worldbounding boxes, then that's it...
         if ( getWorldBox().isOverlapped( obj->getWorldBox() ) )
         {
            ShapeBase* col = static_cast<ShapeBase*>( obj );
            queueCollision(col,getVelocity() - col->getVelocity());
         }
      }
      else if (objectMask & ItemObjectType)
      {
         // If we've overlapped the worldbounding boxes, then that's it...
         Item* item = static_cast<Item*>( obj );
         if (  getWorldBox().isOverlapped(item->getWorldBox()) &&
               item->getCollisionObject() != this && 
               !item->isHidden() )
            queueCollision(item,getVelocity() - item->getVelocity());
      }
   }

   if(contactObject != NULL) {		//Ubiq: this test is necessary or slide is always set true
   F32 vd = (*contactNormal).z;
   *run = vd > mDataBlock->runSurfaceCos;
   *jump = vd > mDataBlock->jumpSurfaceCos;
   *slide = vd <= mDataBlock->runSurfaceCos;
   }

   mContactInfo.clear();
  
   mContactInfo.contacted = contactObject != NULL;
   mContactInfo.contactObject = contactObject;

   if ( mContactInfo.contacted )
      mContactInfo.contactNormal = *contactNormal;

   mContactInfo.run = *run;
   mContactInfo.jump = *jump;
   mContactInfo.slide = *slide;
}

//----------------------------------------------------------------------------

void AAKPlayer::setPosition(const Point3F& pos,const Point3F& rot)
{
   MatrixF mat;
   if (isMounted()) {
      // Use transform from mounted object
      MatrixF nmat,zrot;
      mMount.object->getMountTransform( mMount.node, mMount.xfm, &nmat );
      zrot.set(EulerF(0.0f, 0.0f, rot.z));
      mat.mul(nmat,zrot);
   }
   else {
      mat.set(EulerF(rot.x, rot.y, rot.z));
      mat.setColumn(3,pos);
   }
   Parent::Parent::setTransform(mat);
   mRot = rot;

   if ( mPhysicsRep )
      mPhysicsRep->setTransform( mat );
}


void AAKPlayer::setRenderPosition(const Point3F& pos, const Point3F& rot, F32 dt)
{
   disableCollision();
   
   MatrixF mat;
   
   //default
   mat.set(EulerF(rot.x, rot.y, rot.z));
   
   if (isMounted()) {
      // Use transform from mounted object
      MatrixF nmat,zrot;
      mMount.object->getRenderMountTransform( dt, mMount.node, mMount.xfm, &nmat );
      zrot.set(EulerF(0.0f, 0.0f, rot.z));
      mat.mul(nmat,zrot);
   }

	//-------------------------------------------------------------------
	// Orient to ground
	//-------------------------------------------------------------------
   else if (inDeathAnim() || (mDataBlock->orientToGround && mContactTimer < sContactTickTime))
	{
		VectorF normal;
		Point3F corner[3], hit[3]; S32 c;
		corner[0] = Point3F(mObjBox.maxExtents.x - 0.01f, mObjBox.maxExtents.y - 0.01f, mObjBox.minExtents.z) + pos;
		corner[1] = Point3F(mObjBox.minExtents.x + 0.01f, mObjBox.maxExtents.y - 0.01f, mObjBox.minExtents.z) + pos;
		corner[2] = Point3F(mObjBox.maxExtents.x - 0.01f, mObjBox.minExtents.y + 0.01f, mObjBox.minExtents.z) + pos;

		//do three casts
		RayInfo rInfo;
		for (c = 0; c < 3; c++)
		{
			Point3F rayStart = corner[c] + Point3F(0,0,0.3f);
			Point3F rayEnd = corner[c] - Point3F(0,0,2.0f);
			if (gClientContainer.castRay(rayStart, rayEnd, sCollisionMoveMask, &rInfo))
			{
				hit[c] = rInfo.point;

				#ifdef ENABLE_DEBUGDRAW
            if (sRenderHelpers)
            {
               DebugDrawer::get()->drawLine(rayStart, rInfo.point, ColorI::RED);
               DebugDrawer::get()->setLastTTL(TickMs);
               DebugDrawer::get()->drawLine(rInfo.point, rayEnd, ColorI::BLUE);
               DebugDrawer::get()->setLastTTL(TickMs);
            }
				#endif
			}
			else
				break;
		}

		//if all three hit
		if(c == 3)
		{
			mCross(hit[1] - hit[0], hit[2] - hit[1], &normal);
			normal.normalize();

			#ifdef ENABLE_DEBUGDRAW
         {
            if (sRenderHelpers)
            {
               DebugDrawer::get()->drawLine(pos, pos + normal, ColorI::BLUE);
               DebugDrawer::get()->setLastTTL(TickMs);
            }
         }
			#endif

         //validate it's a valid normal for us to orient on
         F32 vd = normal.z;
         if (vd > mDataBlock->runSurfaceCos)
         {
            VectorF  upY(0.0f, 1.0f, 0.0f), ahead;
            VectorF  sideVec;
            mat.set(EulerF(0.0f, 0.0f, rot.z));
            mat.mulV(upY, &ahead);
            mCross(ahead, normal, &sideVec);
            sideVec.normalize();
            mCross(normal, sideVec, &ahead);

            mat.setColumn(0, sideVec);
            mat.setColumn(1, ahead);
            mat.setColumn(2, normal);
         }
		}
	}


	//-------------------------------------------------------------------
	// Ubiq: Snap to ground
	//-------------------------------------------------------------------
	if(mDataBlock->groundSnapSpeed > 0 && mDataBlock->groundSnapRayLength > 0)
	{
		F32 goal = 0.0f;

		//if not jumping/climbing/grabbing
		if(!mJumping && !mClimbState.active && !mLedgeState.active && !mSwimming)
		{
			//see if we should set our ground snap goal
			Point3F forward;
			getTransform().getColumn(1, &forward);
			forward.normalize(mDataBlock->groundSnapRayOffset);

			RayInfo rInfo;
			Point3F rayStart = pos + forward;
			Point3F rayEnd = rayStart + Point3F(0,0,-mDataBlock->groundSnapRayLength);
			
			if(getContainer()->castRay(rayStart, rayEnd, sCollisionMoveMask, &rInfo))
			{
				//if it's sloped, we need an offset
				//(this won't help on stairs - we don't want it since the popping looks bad)
				if(rInfo.normal.z != 1.0f)
				{
					//set the goal
					goal = rInfo.point.z - rayStart.z;

					#ifdef ENABLE_DEBUGDRAW
               if (sRenderHelpers)
               {
                  DebugDrawer::get()->drawLine(rayStart, rInfo.point, ColorI::RED);
                  DebugDrawer::get()->setLastTTL(TickMs);
                  DebugDrawer::get()->drawLine(rInfo.point, rayEnd, ColorI::BLUE);
                  DebugDrawer::get()->setLastTTL(TickMs);
               }
					#endif
				}
			}
		}

		//always move toward goal
		if(dt > 0)
		{
			F32 diff = goal - mGroundSnap;
			if(mFabs(diff) < mDataBlock->groundSnapSpeed * dt)
				mGroundSnap = goal;		//as close as we're going to get, snap to it
			else if(diff > 0)
				mGroundSnap = goal;	//move player up instantly (we don't ever want him in the ground)
			else if (diff < 0)
				mGroundSnap -= mDataBlock->groundSnapSpeed * dt;	//move player down
		}
	}
	//-------------------------------------------------------------------

	//apply ground snap
	Point3F tmp(pos);
	tmp.z += mGroundSnap;
	mat.setColumn(3, tmp);

	Parent::Parent::setRenderTransform(mat);

	enableCollision();
}

//----------------------------------------------------------------------------

void AAKPlayer::getCameraParameters(F32 *min,F32* max,Point3F* off,MatrixF* rot)
{
   if (!mControlObject.isNull() && mControlObject == getObjectMount()) {
      mControlObject->getCameraParameters(min,max,off,rot);
      return;
   }
   const Point3F& scale = getScale();
   *min = mDataBlock->cameraMinDist * scale.y;
   *max = mDataBlock->cameraMaxDist * scale.y;
   *off = mDataBlock->cameraOffset * scale;  //Ubiq
   rot->identity();
}

//----------------------------------------------------------------------------

void AAKPlayer::writePacketData(GameConnection *connection, BitStream *stream)
{
   Parent::writePacketData(connection, stream);

   stream->write(mRot.x);
   stream->write(mRot.y);
   
	//----------------------------------------------------------------------------
	// Ubiq custom
	//----------------------------------------------------------------------------

	//Ubiq: Slide state
	stream->write(mSlideState.active);
	stream->write(mSlideState.surfaceNormal.x);
	stream->write(mSlideState.surfaceNormal.y);
	stream->write(mSlideState.surfaceNormal.z);

	//Ubiq: Jump state
	stream->write(mJumpState.active);
	stream->write(mJumpState.isCrouching);
	stream->write(mJumpState.crouchDelay);
	stream->write((U16)mJumpState.jumpType);

	//Ubiq: Climb state
	stream->write(mClimbState.active);
	stream->write((U16)mClimbState.direction);
	stream->write(mClimbState.surfaceNormal.x);
	stream->write(mClimbState.surfaceNormal.y);
	stream->write(mClimbState.surfaceNormal.z);
	stream->write(mClimbTriggerCount);

	//Ubiq: Wallhug state
	stream->write(mWallHugState.active);
	stream->write(mWallHugState.surfaceNormal.x);
	stream->write(mWallHugState.surfaceNormal.y);
	stream->write(mWallHugState.surfaceNormal.z);
	stream->write((U16)mWallHugState.direction);

	//Ubiq: Ledge state
	stream->write(mLedgeState.active);
	stream->write(mLedgeState.ledgeNormal.x);
	stream->write(mLedgeState.ledgeNormal.y);
	stream->write(mLedgeState.ledgeNormal.z);
	stream->write(mLedgeState.ledgePoint.x);
	stream->write(mLedgeState.ledgePoint.y);
	stream->write(mLedgeState.ledgePoint.z);
	stream->write((U16)mLedgeState.direction);
	stream->write(mLedgeState.climbingUp);
	stream->write(mLedgeState.animPos);	

	//Ubiq: Land state
	stream->write(mLandState.active);
	stream->write(mLandState.timer);

	//Ubiq: Stop state
	stream->write(mStoppingTimer);
}

void AAKPlayer::readPacketData(GameConnection *connection, BitStream *stream)
{
   Parent::readPacketData(connection, stream);

   Point3F rot;
   
   stream->read(&rot.x);
   stream->read(&rot.y);

   if (!ignore_updates)
      setPosition(getPosition(), Point3F(rot.x, rot.y, mRot.z));
   
	//----------------------------------------------------------------------------
	// Ubiq custom
	//----------------------------------------------------------------------------

	//Ubiq: Slide state
	stream->read(&mSlideState.active);
	stream->read(&mSlideState.surfaceNormal.x);
	stream->read(&mSlideState.surfaceNormal.y);
	stream->read(&mSlideState.surfaceNormal.z);

	//Ubiq: Jump state
	stream->read(&mJumpState.active);
	stream->read(&mJumpState.isCrouching);
	stream->read(&mJumpState.crouchDelay);
	U16 jumpType;
	stream->read(&jumpType);
	mJumpState.jumpType = (JumpType)jumpType;

	//Ubiq: Climb state
	stream->read(&mClimbState.active);
	U16 climbDirTemp;
	stream->read(&climbDirTemp);
	mClimbState.direction = (MoveDir)climbDirTemp;
	stream->read(&mClimbState.surfaceNormal.x);
	stream->read(&mClimbState.surfaceNormal.y);
	stream->read(&mClimbState.surfaceNormal.z);
	stream->read(&mClimbTriggerCount);

	//Ubiq: Wallhug state
	stream->read(&mWallHugState.active);
	stream->read(&mWallHugState.surfaceNormal.x);
	stream->read(&mWallHugState.surfaceNormal.y);
	stream->read(&mWallHugState.surfaceNormal.z);
	U16 wallDirTemp;
	stream->read(&wallDirTemp);
	mWallHugState.direction = (MoveDir)wallDirTemp;

	//Ubiq: Ledge state
	stream->read(&mLedgeState.active);
	stream->read(&mLedgeState.ledgeNormal.x);
	stream->read(&mLedgeState.ledgeNormal.y);
	stream->read(&mLedgeState.ledgeNormal.z);
	stream->read(&mLedgeState.ledgePoint.x);
	stream->read(&mLedgeState.ledgePoint.y);
	stream->read(&mLedgeState.ledgePoint.z);
	U16 grabDirTemp;
	stream->read(&grabDirTemp);
	mLedgeState.direction = (MoveDir)grabDirTemp;
	stream->read(&mLedgeState.climbingUp);
	stream->read(&mLedgeState.animPos);

	//Ubiq: Land state
	stream->read(&mLandState.active);
	stream->read(&mLandState.timer);

	//Ubiq: Stop state
	stream->read(&mStoppingTimer);
}

U32 AAKPlayer::packUpdate(NetConnection *con, U32 mask, BitStream *stream)
{
   U32 retMask = Parent::packUpdate(con, mask, stream);

   //LedgeUpMask
   if (stream->writeFlag(mask & LedgeUpMask))
   {
      stream->write(mLedgeState.animPos);
   }

   return retMask;
}

void AAKPlayer::unpackUpdate(NetConnection *con, BitStream *stream)
{
   Parent::unpackUpdate(con,stream);

   //LedgeUpMask
   if (stream->readFlag())
   {
      stream->read(&mLedgeState.animPos);

      // New delta for client-side interpolation
      mLedgeState.deltaAnimPos = mLedgeState.animPos;
      mLedgeState.deltaAnimPosVec = 0.0f;
   }
}

DefineEngineMethod( AAKPlayer, setActionThread, bool, ( const char* name, bool hold, bool fsp ), ( false, true ),
   "@brief Set the main action sequence to play for this player.\n\n"
   "@param name Name of the action sequence to set\n"
   "@param hold Set to false to get a callback on the datablock when the sequence ends (AAKPlayerData::animationDone()).  "
   "When set to true no callback is made.\n"
   "@param fsp True if first person and none of the spine nodes in the shape should animate.  False will allow the shape's "
   "spine nodes to animate.\n"
   "@return True if succesful, false if failed\n"
   
   "@note The spine nodes for the Player's shape are named as follows:\n\n<ul>"
   "<li>Bip01 Pelvis</li>"
   "<li>Bip01 Spine</li>"
   "<li>Bip01 Spine1</li>"
   "<li>Bip01 Spine2</li>"
   "<li>Bip01 Neck</li>"
   "<li>Bip01 Head</li></ul>\n\n"
   
   "You cannot use setActionThread() to have the Player play one of the motion "
   "determined action animation sequences.  These sequences are chosen based on how "
   "the Player moves and the Player's current pose.  The names of these sequences are:\n\n<ul>"
   "<li>root</li>"
   "<li>run</li>"
   "<li>side</li>"
   "<li>side_right</li>"
   "<li>crouch_root</li>"
   "<li>crouch_forward</li>"
   "<li>crouch_backward</li>"
   "<li>crouch_side</li>"
   "<li>crouch_right</li>"
   "<li>prone_root</li>"
   "<li>prone_forward</li>"
   "<li>prone_backward</li>"
   "<li>swim_root</li>"
   "<li>swim_forward</li>"
   "<li>swim_backward</li>"
   "<li>swim_left</li>"
   "<li>swim_right</li>"
   "<li>fall</li>"
   "<li>jump</li>"
   "<li>standjump</li>"
   "<li>land</li>"
   "<li>jet</li></ul>\n\n"
   
   "If the player moves in any direction then the animation sequence set using this "
   "method will be cancelled and the chosen mation-based sequence will take over.  This makes "
   "great for times when the Player cannot move, such as when mounted, or when it doesn't matter "
   "if the action sequence changes, such as waving and saluting.\n"
   
   "@tsexample\n"
      "// Place the player in a sitting position after being mounted\n"
      "%player.setActionThread( \"sitting\", true, true );\n"
	"@endtsexample\n")
{
   return object->setActionThread( name, true, hold, true, fsp, false, false);
}

//----------------------------------------------------------------------------
void AAKPlayer::consoleInit()
{
   Con::addVariable("$AAKPlayer::renderMyPlayer",TypeBool, &sRenderMyPlayer,
      "@brief Determines if the player is rendered or not.\n\n"
      "Used on the client side to disable the rendering of all AAKPlayer objects.  This is "
      "mainly for the tools or debugging.\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::renderMyItems",TypeBool, &sRenderMyItems,
      "@brief Determines if mounted shapes are rendered or not.\n\n"
      "Used on the client side to disable the rendering of all AAKPlayer mounted objects.  This is "
      "mainly used for the tools or debugging.\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::renderCollision", TypeBool, &sRenderPlayerCollision, 
      "@brief Determines if the player's collision mesh should be rendered.\n\n"
      "This is mainly used for the tools and debugging.\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::renderHelpers", TypeBool, &sRenderHelpers,
      "@brief Determines if the player's nodes, mantle-able geometry et al should be rendered.\n\n"
      "This is mainly used for the tools and debugging.\n"
      "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::minWarpTicks",TypeF32,&sMinWarpTicks, 
      "@brief Fraction of tick at which instant warp occures on the client.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::maxWarpTicks",TypeS32,&sMaxWarpTicks, 
      "@brief When a warp needs to occur due to the client being too far off from the server, this is the "
      "maximum number of ticks we'll allow the client to warp to catch up.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::maxPredictionTicks",TypeS32,&sMaxPredictionTicks, 
      "@brief Maximum number of ticks to predict on the client from the last known move obtained from the server.\n\n"
	   "@ingroup GameObjects\n");

   Con::addVariable("$AAKPlayer::maxImpulseVelocity", TypeF32, &sMaxImpulseVelocity, 
      "@brief The maximum velocity allowed due to a single impulse.\n\n"
	   "@ingroup GameObjects\n");

   // Move triggers
   Con::addVariable("$AAKPlayer::jumpTrigger", TypeS32, &sJumpTrigger, 
      "@brief The move trigger index used for player jumping.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::crouchTrigger", TypeS32, &sCrouchTrigger, 
      "@brief The move trigger index used for player crouching.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::proneTrigger", TypeS32, &sProneTrigger, 
      "@brief The move trigger index used for player prone pose.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::sprintTrigger", TypeS32, &sSprintTrigger, 
      "@brief The move trigger index used for player sprinting.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::imageTrigger0", TypeS32, &sImageTrigger0, 
      "@brief The move trigger index used to trigger mounted image 0.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::imageTrigger1", TypeS32, &sImageTrigger1, 
      "@brief The move trigger index used to trigger mounted image 1 or alternate fire "
      "on mounted image 0.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::jumpJetTrigger", TypeS32, &sJumpJetTrigger, 
      "@brief The move trigger index used for player jump jetting.\n\n"
	   "@ingroup GameObjects\n");
   Con::addVariable("$AAKPlayer::vehicleDismountTrigger", TypeS32, &sVehicleDismountTrigger, 
      "@brief The move trigger index used to dismount player.\n\n"
	   "@ingroup GameObjects\n");

   //Ubiq: TODO: add documentation strings
   addField("climbTriggerCount", TypeS32, Offset(mClimbTriggerCount, AAKPlayer), "");
   addField("dieOnNextCollision", TypeBool, Offset(mDieOnNextCollision, AAKPlayer), "");
   afx_consoleInit();
}

void AAKPlayer::initPersistFields()
{
   addField("inWater", TypeBool, Offset(mInWater, AAKPlayer),
      "@brief script exposure of inWater state.");
   addField("isFalling", TypeBool, Offset(mFalling, AAKPlayer),
      "@brief script exposure of falling state.");
   addField("isRunSurface", TypeBool, Offset(mRunSurface, AAKPlayer),
      "@brief script exposure of runSurface state.");

   Parent::initPersistFields();
}

//-----------------------------------------------------------------------------

void AAKPlayer::playFootstepSound( bool triggeredLeft, Material* contactMaterial, SceneObject* contactObject )
{
   if (footfallSoundOverride > 0)
      return;
   MatrixF footMat = getTransform();
   if( mWaterCoverage > 0.0 && mWaterCoverage < 1.0 )
   {
      if ( mWaterCoverage < mDataBlock->footSplashHeight )
         SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootShallowSplash), &getTransform());
      else
         SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootWading), &getTransform());
   }

   if (mWaterCoverage >= 1.0)
   {
	   if ( triggeredLeft )
	   {
         SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootUnderWater), &getTransform());
         SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootBubbles), &getTransform());
	   }
   }

   else if( contactMaterial && contactMaterial->isCustomFootstepSoundValid())
   {
      // Footstep sound defined on material.
      //Ubiq: set volume and pitch based on player velocity (landing is always max volume)
      F32 speedFactor = mLandState.active ? 1.0f : mVelocity.len()/mDataBlock->maxForwardSpeed;
      speedFactor = mClampF(speedFactor, 0.3f, 1.0f);
      
      F32 volume = speedFactor;
      F32 pitch = (0.85f * (1.0f - speedFactor)) + (1.15f * speedFactor); //map (0 - 1) to (0.85 - 1.15)
      
      SFXSource* source = SFX->createSource(contactMaterial->getCustomFootstepSoundProfile(), &footMat);
      source->setVolume(volume);
      source->setPitch(pitch);
      source->play();
      SFX->deleteWhenStopped(source);
   }
   else
   {
      S32 sound = -1;
      if( contactMaterial && contactMaterial->mFootstepSoundId != -1 )
         sound = contactMaterial->mFootstepSoundId;
      else if( contactObject && contactObject->getTypeMask() & VehicleObjectType )
         sound = 2;

      SFXSource* source = NULL;
      switch (sound)
      {
      case 0: // Soft
         source = SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootSoft), &getTransform());
         break;
      case 1: // Hard
         source = SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootHard), &getTransform());
         break;
      case 2: // Metal
         source = SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootMetal), &getTransform());
         break;
      case 3: // Snow
         source = SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::FootSnow), &getTransform());
         break;
      }

      if (source)
      {
         F32 pitch = mRandF(0.5f, 1.5f);
         source->setPitch(pitch);
      }
   }
}

void AAKPlayer::playImpactSound()
{
   if( mWaterCoverage == 0.0f )
   {
      Point3F pos;
      RayInfo rInfo;
      MatrixF mat = getTransform();
      mat.mulP(Point3F(mDataBlock->decalOffset,0.0f,0.0f), &pos);

      if( gClientContainer.castRay( Point3F( pos.x, pos.y, pos.z + 0.01f ),
                                    Point3F( pos.x, pos.y, pos.z - 2.0f ),
                                    STATIC_COLLISION_TYPEMASK | VehicleObjectType,
                                    &rInfo ) )
      {
         Material* material = ( rInfo.material ? dynamic_cast< Material* >( rInfo.material->getMaterial() ) : 0 );

         if( material && material->isCustomImpactSoundValid())
            SFX->playOnce( material->getCustomImpactSoundProfile(), &getTransform() );
         else
         {
            S32 sound = -1;
            if( material && material->mImpactSoundId )
               sound = material->mImpactSoundId;
            else if( rInfo.object->getTypeMask() & VehicleObjectType )
               sound = 2; // Play metal;

            switch( sound )
            {
            case 0:
               //Soft
               SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ImpactSoft), &getTransform());
               break;
            case 1:
               //Hard
               SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ImpactHard), &getTransform());
               break;
            case 2:
               //Metal
               SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ImpactMetal), &getTransform());
               break;
            case 3:
               //Snow
               SFX->playOnce(mDataBlock->getPlayerSoundProfile(AAKPlayerData::ImpactSnow), &getTransform());
               break;
               /*
            default:
               //Hard
               alxPlay(mDataBlock->sound[AAKPlayerData::ImpactHard], &getTransform());
               break;
               */
            }
         }
      }
   }

   mImpactSound = 0;
}

//--------------------------------------------------------------------------

void AAKPlayer::updateFroth( F32 dt )
{
   // update bubbles
   Point3F moveDir = getVelocity();
   mBubbleEmitterTime += dt;

   if (mBubbleEmitterTime < mDataBlock->bubbleEmitTime) {
      if (mSplashEmitter[AAKPlayerData::BUBBLE_EMITTER]) {
         Point3F emissionPoint = getRenderPosition();
         U32 emitNum = AAKPlayerData::BUBBLE_EMITTER;
         mSplashEmitter[emitNum]->emitParticles(mLastPos, emissionPoint,
            Point3F( 0.0, 0.0, 1.0 ), moveDir, (U32)(dt * 1000.0));
      }
   }

   Point3F contactPoint;
   if (!collidingWithWater(contactPoint)) {
      mLastWaterPos = mLastPos;
      return;
   }

   F32 speed = moveDir.len();
   if ( speed < mDataBlock->splashVelEpsilon ) 
      speed = 0.0;

   U32 emitRate = (U32) (speed * mDataBlock->splashFreqMod * dt);

   // If we're in the water, swimming, but not
   // moving, then lets emit some particles because
   // we're treading water.  
   //Ubiq: actually it looks kinda silly - removing
   /*if ( mSwimming && speed == 0.0 )
   {
      emitRate = (U32) (2.0f * mDataBlock->splashFreqMod * dt);
   }*/

   U32 i;
   for ( i=0; i<AAKPlayerData::BUBBLE_EMITTER; i++ ) {
      if (mSplashEmitter[i] )
         mSplashEmitter[i]->emitParticles( mLastWaterPos,
            contactPoint, Point3F( 0.0, 0.0, 1.0 ),
            moveDir, emitRate );
   }
   mLastWaterPos = contactPoint;
}

//----------------------------------------------------------------------------
// Ubiq custom
//----------------------------------------------------------------------------
//-------------------------------------------------------------------
// AAKPlayer::getNodePosition
//
// Returns the current world coordinates of the named node
//-------------------------------------------------------------------
Point3F AAKPlayer::getNodePosition(const char *nodeName)
{
	const MatrixF& mat = this->getTransform();
	Point3F nodePoint;
	MatrixF nodeMat;
	S32 ni = mDataBlock->getShape()->findNode(nodeName);
	AssertFatal(ni >= 0, "ShapeBase::getNodePosition() - couldn't find node!");
	nodeMat = mShapeInstance->mNodeTransforms[ni];
	nodePoint = nodeMat.getPosition();
	mat.mulP(nodePoint);
	return nodePoint;
}

//-------------------------------------------------------------------
// AAKPlayer::setObjectBox
//
// Sets the player object box / convex box from the one provided
//-------------------------------------------------------------------
void AAKPlayer::setObjectBox(Point3F size)
{
   mObjBox = createObjectBox(size);

   // Setup the box for our convex object...
   mObjBox.getCenter(&mConvex.mCenter);
   mConvex.mSize.x = mObjBox.len_x() / 2.0f;
   mConvex.mSize.y = mObjBox.len_y() / 2.0f;
   mConvex.mSize.z = mObjBox.len_z() / 2.0f;

   // Initialize our scaled attributes as well
   onScaleChanged();
}

//-------------------------------------------------------------------
// AAKPlayer::createObjectBox
//
// Given a point (containing length, width & height) makes an object box
//-------------------------------------------------------------------
Box3F AAKPlayer::createObjectBox(Point3F size)
{
   Box3F returnBox;
   returnBox.maxExtents.x = size.x * 0.5f;
   returnBox.maxExtents.y = size.y * 0.5f;
   returnBox.maxExtents.z = size.z;
   returnBox.minExtents.x = -returnBox.maxExtents.x;
   returnBox.minExtents.y = -returnBox.maxExtents.y;
   returnBox.minExtents.z = 0.0f;
   return returnBox;
}

//-------------------------------------------------------------------
// AAKPlayer::getSurfaceType
//
// Returns the type mask of the object the player is standing on
//-------------------------------------------------------------------
U32 AAKPlayer::getSurfaceType()
{
   Point3F pos = getPosition();
   RayInfo rInfo;
   disableCollision();
   if (getContainer()->castRay(Point3F(pos.x, pos.y, pos.z + 0.2f), Point3F(pos.x, pos.y, pos.z - 0.2f), 0xffffffff, &rInfo))
   {
      enableCollision();
      return rInfo.object->getTypeMask();;
   }
   else
   {
      enableCollision();
      return DefaultObjectType;
   }
}

//-------------------------------------------------------------------
// AAKPlayer::worldBoxIsClear
//
// Returns true if there are no obstructions inside the given world space box
//-------------------------------------------------------------------
bool AAKPlayer::worldBoxIsClear(Box3F objSpaceBox, Point3F worldOffset)
{
   //this overload takes a box in object space and a world space offset
   //convert objSpaceBox to a world space box by adding the offset
   objSpaceBox.minExtents += worldOffset;
   objSpaceBox.maxExtents += worldOffset;

   return worldBoxIsClear(objSpaceBox);
}

//-------------------------------------------------------------------
// AAKPlayer::worldBoxIsClear
//
// Returns true if there are no obstructions inside the given world space box
//-------------------------------------------------------------------
bool AAKPlayer::worldBoxIsClear(Box3F worldSpaceBox)
{
   EarlyOutPolyList polyList;
   polyList.mNormal.set(0.0f, 0.0f, 0.0f);
   polyList.mPlaneList.clear();
   polyList.mPlaneList.setSize(6);
   polyList.mPlaneList[0].set(worldSpaceBox.minExtents, VectorF(-1.0f, 0.0f, 0.0f));
   polyList.mPlaneList[1].set(worldSpaceBox.maxExtents, VectorF(0.0f, 1.0f, 0.0f));
   polyList.mPlaneList[2].set(worldSpaceBox.maxExtents, VectorF(1.0f, 0.0f, 0.0f));
   polyList.mPlaneList[3].set(worldSpaceBox.minExtents, VectorF(0.0f, -1.0f, 0.0f));
   polyList.mPlaneList[4].set(worldSpaceBox.minExtents, VectorF(0.0f, 0.0f, -1.0f));
   polyList.mPlaneList[5].set(worldSpaceBox.maxExtents, VectorF(0.0f, 0.0f, 1.0f));

   //is there geometry in the way?
   disableCollision();
   bool isClear = !getContainer()->buildPolyList(PLC_Collision, worldSpaceBox, sCollisionMoveMask, &polyList);
   enableCollision();

#ifdef ENABLE_DEBUGDRAW
   if (sRenderHelpers)
   {
      LinearColorF color;
      if (isClear)
         color = LinearColorF(0, 1, 0);
      else
         color = LinearColorF(1, 0, 0);

      DebugDrawer::get()->drawBox(worldSpaceBox.minExtents, worldSpaceBox.maxExtents, color);
      DebugDrawer::get()->setLastTTL(TickMs);
   }
#endif

   return isClear;
}

//-------------------------------------------------------------------
// AAKPlayer::updateSounds
//
// Update all our player sound effects
//-------------------------------------------------------------------
void AAKPlayer::updateSounds(F32 dt)
{
   //turn on/off slide sounds
   if (mSlideSound)
   {
      if ((mSlideState.active || mStoppingTimer > 16)
         && (mRunSurface || mSlideSurface)
         && mVelocity.len() > 0.01f)
      {
         if (!mSlideSound->isPlaying())
            mSlideSound->play();

         //set volume and pitch based on speed
         F32 speedFactor = mVelocity.len() / 16.0f;
         speedFactor = mClampF(speedFactor, 0, 1);

         F32 volume = speedFactor;
         F32 pitch = (0.75f * (1.0f - speedFactor)) + (1.25f * speedFactor); //map (0 - 1) to (0.75 - 1.25)

         mSlideSound->setVolume(volume);
         mSlideSound->setPitch(pitch);
         mSlideSound->setTransform(getRenderTransform());
         mSlideSound->setVelocity(mVelocity);
      }
      else if (mSlideSound->isPlaying())
         mSlideSound->pause();
   }

   //Ubiq: moved here from updateWaterSounds()
   if (mWaterCoverage < 1.0f || mDamageState != Enabled)
   {
      // Stop everything
      if (mMoveBubbleSound)
         mMoveBubbleSound->stop();
      if (mWaterBreathSound)
         mWaterBreathSound->stop();
      return;
   }

   if (mMoveBubbleSound)
   {
      // We're under water and still alive, so let's play something
      if (mVelocity.len() > 1.0f)
      {
         if (!mMoveBubbleSound->isPlaying())
            mMoveBubbleSound->play();

         mMoveBubbleSound->setTransform(getTransform());
      }
      else
         mMoveBubbleSound->stop();
   }

   if (mWaterBreathSound)
   {
      if (!mWaterBreathSound->isPlaying())
         mWaterBreathSound->play();

      mWaterBreathSound->setTransform(getTransform());
   }
}

//-------------------------------------------------------------------
// AAKPlayer::findClimbContact
//
// Check to see if we have a suitable climb surface in front of us
//-------------------------------------------------------------------
void AAKPlayer::findClimbContact(bool* climb, PlaneF* climbPlane)
{
	*climb = false;

	Point3F pos;
	getTransform().getColumn(3, &pos);

	Point3F forward;
	getTransform().getColumn(1, &forward);

	Box3F wBox = mObjBox;
	Point3F offset(forward);
	offset.normalize(0.2f);
	wBox.minExtents.z = mDataBlock->climbHeightMin;
	wBox.maxExtents.z = mDataBlock->climbHeightMax;
	wBox.minExtents += offset + pos;
	wBox.maxExtents += offset + pos;


#ifdef ENABLE_DEBUGDRAW
   if (sRenderHelpers)
   {
      DebugDrawer::get()->drawBox(wBox.minExtents, wBox.maxExtents, LinearColorF::RED);
      DebugDrawer::get()->setLastTTL(100);
   }
#endif

	static ClippedPolyList polyList;
	polyList.clear();
	polyList.doConstruct();
	polyList.mNormal.set(0.0f, 0.0f, 0.0f);

	polyList.mPlaneList.setSize(6);
	polyList.mPlaneList[0].setYZ(wBox.minExtents, -1.0f);
	polyList.mPlaneList[1].setXZ(wBox.maxExtents, 1.0f);
	polyList.mPlaneList[2].setYZ(wBox.maxExtents, 1.0f);
	polyList.mPlaneList[3].setXZ(wBox.minExtents, -1.0f);
	polyList.mPlaneList[4].setXY(wBox.minExtents, -1.0f);
	polyList.mPlaneList[5].setXY(wBox.maxExtents, 1.0f);
	Box3F plistBox = wBox;

	// Build list from convex states here...
	CollisionWorkingList& rList = mConvex.getWorkingList();
	CollisionWorkingList* pList = rList.wLink.mNext;
	while (pList != &rList)
	{
		Convex* pConvex = pList->mConvex;

		if ((pConvex->getObject()->getTypeMask() & StaticObjectType) != 0)
		{
			bool skip = true;

			TSStatic *st = dynamic_cast<TSStatic *> (pConvex->getObject());
			if (st && st->allowPlayerClimb())
				skip = false;

			TerrainBlock *terrain = dynamic_cast<TerrainBlock *> (pConvex->getObject());
			if (terrain && terrain->allowPlayerClimb())
				skip = false;

			if(!skip)
			{
				Box3F convexBox = pConvex->getBoundingBox();
				if (plistBox.isOverlapped(convexBox))
					pConvex->getPolyList(&polyList);
			}
		}
		pList = pList->wLink.mNext;
	}

	if (!polyList.isEmpty())
	{
		// Average the normals of all vertical-ish polygons together
		// This allows the player to climb around beveled corners
		*climbPlane = PlaneF(0,0,0,0); F32 totalWeight = 0.0f;
		for (U32 p = 0; p < polyList.mPolyList.size(); p++)
		{
			//nearly vertical surface?
			if(mFabs(polyList.mPolyList[p].plane.z) < 0.2)
			{
				//facing the player?
				if(mDot(polyList.mPolyList[p].plane, forward) < -0.5f)
				{
					//calculate area of polygon
					//-------------------------------------
					Point3F areaNorm(0,0,0);
					for (S32 i=polyList.mPolyList[p].vertexStart; i < polyList.mPolyList[p].vertexStart + polyList.mPolyList[p].vertexCount; i++)
					{
						S32 vertex1Index = i;
						S32 vertex2Index = i + 1;

						//the last vertex is connected to the first
						if(i == polyList.mPolyList[p].vertexStart + polyList.mPolyList[p].vertexCount - 1) 
							vertex2Index = polyList.mPolyList[p].vertexStart;

						//get the verticies
						Point3F vertex1 = polyList.mVertexList[polyList.mIndexList[vertex1Index]].point;
						Point3F vertex2 = polyList.mVertexList[polyList.mIndexList[vertex2Index]].point;

						Point3F tmp;
						mCross(vertex1, vertex2, &tmp);
						areaNorm += tmp;
					}
					F32 area = mDot(polyList.mPolyList[p].plane, areaNorm);
					area *= (area < 0 ? -0.5f : 0.5f);
					//-------------------------------------

					F32 weight = area;
					totalWeight += weight;
					*climbPlane += polyList.mPolyList[p].plane * weight;
					climbPlane->d += polyList.mPolyList[p].plane.d * weight;
				}
			}
		}
		if(totalWeight > 0)
		{
			*climb = true;

			//divide to finish the weighted averages
			*climbPlane /= totalWeight;
			climbPlane->d /= totalWeight;
		}
		else
		{
			//we failed to find a suitable climb surface
			*climb = false;
		}
	}
}

//-------------------------------------------------------------------
// AAKPlayer::canStartClimb
//
// Returns true if the player is currently allowed to enter the climb state
//-------------------------------------------------------------------
bool AAKPlayer::canStartClimb()
{
	return mState == MoveState && mDamageState == Enabled && !isMounted()
		&& (mPose == StandPose || mPose == SprintPose)
		&& !mDieOnNextCollision
		&& mClimbTriggerCount > 0
		&& mVelocity.z <= 0.1f
		&& !mClimbState.ignoreClimb;
}

//-------------------------------------------------------------------
// AAKPlayer::canClimb
//
// Returns true if the player should continue in the climb state
//-------------------------------------------------------------------
bool AAKPlayer::canClimb()
{
	return mState == MoveState && mDamageState == Enabled && !isMounted()
		&& (mPose == StandPose || mPose == SprintPose)
		&& !mDieOnNextCollision
		&& mClimbTriggerCount > 0
		&& !mClimbState.ignoreClimb;
}

//-------------------------------------------------------------------
// AAKPlayer::findWallContact
//
// Check to see if we have a suitable wall hug surface in front of us
//-------------------------------------------------------------------
void AAKPlayer::findWallContact(bool* wall, PlaneF* wallPlane)
{
	*wall = false;

	Point3F pos;
	getTransform().getColumn(3, &pos);

	Point3F forward;
	getTransform().getColumn(1, &forward);

	Box3F wBox = mObjBox;
	Point3F offset(forward);
	offset.normalize(0.2f);
	wBox.minExtents.z = mDataBlock->wallHugHeightMin;
	wBox.maxExtents.z = mDataBlock->wallHugHeightMax;
	wBox.minExtents += offset + pos;
	wBox.maxExtents += offset + pos;


#ifdef ENABLE_DEBUGDRAW
   if (sRenderHelpers)
   {
      DebugDrawer::get()->drawBox(wBox.minExtents, wBox.maxExtents, LinearColorF::GREEN);
      DebugDrawer::get()->setLastTTL(TickMs);
   }
#endif

	static ClippedPolyList polyList;
	polyList.clear();
	polyList.doConstruct();
	polyList.mNormal.set(0.0f, 0.0f, 0.0f);

	polyList.mPlaneList.setSize(6);
	polyList.mPlaneList[0].setYZ(wBox.minExtents, -1.0f);
	polyList.mPlaneList[1].setXZ(wBox.maxExtents, 1.0f);
	polyList.mPlaneList[2].setYZ(wBox.maxExtents, 1.0f);
	polyList.mPlaneList[3].setXZ(wBox.minExtents, -1.0f);
	polyList.mPlaneList[4].setXY(wBox.minExtents, -1.0f);
	polyList.mPlaneList[5].setXY(wBox.maxExtents, 1.0f);
	Box3F plistBox = wBox;

	// Build list from convex states here...
	CollisionWorkingList& rList = mConvex.getWorkingList();
	CollisionWorkingList* pList = rList.wLink.mNext;
	while (pList != &rList)
	{
		Convex* pConvex = pList->mConvex;

		if ((pConvex->getObject()->getTypeMask() & StaticObjectType) != 0)
		{
			bool skip = true;

			TSStatic *st = dynamic_cast<TSStatic *> (pConvex->getObject());
			if (st && st->allowPlayerWallHug())
				skip = false;

			TerrainBlock *terrain = dynamic_cast<TerrainBlock *> (pConvex->getObject());
			if (terrain && terrain->allowPlayerClimb())
				skip = false;

			if(!skip)
			{
				Box3F convexBox = pConvex->getBoundingBox();
				if (plistBox.isOverlapped(convexBox))
					pConvex->getPolyList(&polyList);
			}
		}
		pList = pList->wLink.mNext;
	}

	if (!polyList.isEmpty())
	{
		// Average the normals of all vertical polygons together
		// This allows the player to wall hug around beveled corners
		*wallPlane = PlaneF(0,0,0,0); F32 totalWeight = 0.0f;
		for (U32 p = 0; p < polyList.mPolyList.size(); p++)
		{
			//nearly vertical surface?
			if(mFabs(polyList.mPolyList[p].plane.z) < 0.2)
			{
				//facing the player?
				if(mDot(polyList.mPolyList[p].plane, forward) < -0.5f)
				{
					//calculate area of polygon
					//-------------------------------------
					Point3F areaNorm(0,0,0);
					for (S32 i=polyList.mPolyList[p].vertexStart; i < polyList.mPolyList[p].vertexStart + polyList.mPolyList[p].vertexCount; i++)
					{
						S32 vertex1Index = i;
						S32 vertex2Index = i + 1;

						//the last vertex is connected to the first
						if(i == polyList.mPolyList[p].vertexStart + polyList.mPolyList[p].vertexCount - 1) 
							vertex2Index = polyList.mPolyList[p].vertexStart;

						//get the verticies
						Point3F vertex1 = polyList.mVertexList[polyList.mIndexList[vertex1Index]].point;
						Point3F vertex2 = polyList.mVertexList[polyList.mIndexList[vertex2Index]].point;

						Point3F tmp;
						mCross(vertex1, vertex2, &tmp);
						areaNorm += tmp;
					}
					F32 area = mDot(polyList.mPolyList[p].plane, areaNorm);
					area *= (area < 0 ? -0.5f : 0.5f);
					//-------------------------------------

					F32 weight = area;
					totalWeight += weight;
					*wallPlane += polyList.mPolyList[p].plane * weight;
					wallPlane->d += polyList.mPolyList[p].plane.d * weight;
				}
			}
		}
		if(totalWeight > 0)
		{
			*wall = true;

			//divide to finish the weighted averages
			*wallPlane /= totalWeight;
			wallPlane->d /= totalWeight;
		}
		else
		{
			//we failed to find a suitable wall hug surface
			*wall = false;
		}
	}
}

//-------------------------------------------------------------------
// AAKPlayer::canStartWallHug
//
// Returns true if the player is currently allowed to enter the wall hug state
//-------------------------------------------------------------------
bool AAKPlayer::canStartWallHug()
{
	return mState == MoveState && mDamageState == Enabled && !isMounted()
		&& (mPose == StandPose || mPose == SprintPose)
		&& !mDieOnNextCollision
		&& mRunSurface
		&& !mSlideState.active
		&& !mJumpState.active
		&& !mLandState.active
		&& !mClimbState.active
		&& !mLedgeState.active;
}

//-------------------------------------------------------------------
// AAKPlayer::canWallHug
//
// Returns true if the player should continue in the wall hug state
//-------------------------------------------------------------------
bool AAKPlayer::canWallHug()
{
	return mState == MoveState && mDamageState == Enabled && !isMounted()
		&& (mPose == StandPose || mPose == SprintPose)
		&& !mDieOnNextCollision
		&& mContactTimer < sContactTickTime	//similar to (but more tolerant than) checking mRunSurface
		&& !mSlideState.active
		&& !mJumpState.active
		&& !mLandState.active
		&& !mClimbState.active
		&& !mLedgeState.active;
}

//-------------------------------------------------------------------
// AAKPlayer::findLedgeContact
//
// Check to see if we have a suitable ledge to grab in front of us
//-------------------------------------------------------------------
void AAKPlayer::findLedgeContact(bool* ledge, VectorF* ledgeNormal, Point3F* ledgePoint, bool* canMoveLeft, bool* canMoveRight)
{
	*ledge = false;
	ledgeNormal->zero();
	ledgePoint->zero();
	*canMoveLeft = false;
	*canMoveRight = false;

	F32 totalWeight = 0.0f;

	Point3F pos;
	getTransform().getColumn(3, &pos);

	Point3F forward;
	getTransform().getColumn(1, &forward);

	Box3F wBox = mObjBox;
	Point3F offset(forward);
	offset.normalize(wBox.len_y());
	wBox.minExtents.z = mDataBlock->grabHeightMin;
	wBox.maxExtents.z = mDataBlock->grabHeightMax;

	//if player is falling quickly he may miss a ledge between ticks
	//thus we extend the box downward to ensure ledges are always found
	wBox.minExtents.z += (mVelocity.z * TickSec) - wBox.len_z();

	wBox.minExtents += offset + pos;
	wBox.maxExtents += offset + pos;

#ifdef ENABLE_DEBUGDRAW
   if (sRenderHelpers)
   {
      DebugDrawer::get()->drawBox(wBox.minExtents, wBox.maxExtents, LinearColorF::BLUE);
      DebugDrawer::get()->setLastTTL(TickMs);
   }
#endif

	static ConcretePolyList polyList;
	polyList.clear();
	polyList.doConstruct();
	Box3F plistBox = wBox;

	// Build list from convex states here...
	CollisionWorkingList& rList = mConvex.getWorkingList();
	CollisionWorkingList* pList = rList.wLink.mNext;
	while (pList != &rList)
	{
		Convex* pConvex = pList->mConvex;

		if ((pConvex->getObject()->getTypeMask() & StaticObjectType) != 0)
		{
			bool skip = true;

			TSStatic *st = dynamic_cast<TSStatic *> (pConvex->getObject());
			if (st && st->allowPlayerLedgeGrab())
 				skip = false;

			TerrainBlock *terrain = dynamic_cast<TerrainBlock *> (pConvex->getObject());
			if (terrain && terrain->allowPlayerClimb())
				skip = false;

			if(!skip)
			{
				Box3F convexBox = pConvex->getBoundingBox();
				if (plistBox.isOverlapped(convexBox))
					pConvex->getObject()->buildPolyList(PLC_Collision,&polyList,plistBox,SphereF());
			}
		}
		pList = pList->wLink.mNext;
	}

	if (!polyList.isEmpty())
	{
		for (U32 p = 0; p < polyList.mPolyList.size(); p++)
		{
			//upward-facing surface?
			if(polyList.mPolyList[p].plane.z > 0.9f) 
			{
				//loop through all the verticies of this polygon
				for (S32 i=polyList.mPolyList[p].vertexStart; i < polyList.mPolyList[p].vertexStart + polyList.mPolyList[p].vertexCount; i++)
				{
					S32 vertex1Index = i;
					S32 vertex2Index = i + 1;

					//the last vertex is connected to the first
					if(i == polyList.mPolyList[p].vertexStart + polyList.mPolyList[p].vertexCount - 1) 
						vertex2Index = polyList.mPolyList[p].vertexStart;

					//get the verticies
					Point3F vertex1 = polyList.mVertexList[polyList.mIndexList[vertex1Index]];
					Point3F vertex2 = polyList.mVertexList[polyList.mIndexList[vertex2Index]];

					//calculate the "normal" (only X&Y) of this edge
					Point3F normal = mCross(Point3F(0,0,1), vertex2 - vertex1);
					normal.normalizeSafe();

					//quick test: is player facing this edge?
					//we'll test the *real* normal more thoroughly later
					if(mDot(forward, normal) <= 0)
					{
						//Now we're going to collide the edge with our box. We'll do
						//this from both directions and use the average collision point.
						//This is neccessary when one vertex is higher than the other.
						F32 t1; Point3F n1;
						bool collided1 = wBox.collideLine(vertex1, vertex2, &t1, &n1);
						
						F32 t2; Point3F n2;
						bool collided2 = wBox.collideLine(vertex2, vertex1, &t2, &n2);

						//does this edge pass through our box?
						if(collided1 && collided2)
						{
							//Now we need to make sure that:
							//1) this edge is "significant" (ie. large difference in normals of polygons on either side) and
							//2) the edge normal actually faces the player (prevents grabbing the floor on the other side of a wall etc.)
							//    __                         | |
							//      \   0                  --+ |   0
							//      |  /|\                     |  /|\
							//      |  / \                     |  / \
							//BAD, neither edge of         BAD, edge normal does
							//bevel is significant         not face the player

							//so let's go through the other polygons to find an adjacent polygon (a polygon that shares this edge)

							U32 adjPolyIndex;
							if(findAdjacentPoly(&polyList, vertex1, vertex2, p, &adjPolyIndex))
							{
								Point3F polyNormal = polyList.mPolyList[p].plane;
								Point3F adjPolyNormal = polyList.mPolyList[adjPolyIndex].plane;

								//find the *real* edge normal
								Point3F edgeNormal = (polyNormal + adjPolyNormal) / 2.0f;

								//does the edge normal face the player?
								F32 edgeNormalDotForward = mDot(edgeNormal, forward);
								if(edgeNormalDotForward <= -0.2f)
								{
									//is this a "significant edge"?
									F32 polyNormalDotadjPolyNormal = mDot(polyNormal, adjPolyNormal);
									if(polyNormalDotadjPolyNormal <= 0.1f)
									{
										//find the points of collision
										Point3F collisionPoint1, collisionPoint2;
										collisionPoint1.interpolate(vertex1, vertex2, t1);
										collisionPoint2.interpolate(vertex2, vertex1, t2);

										//calculate this edge's weight using length inside box
										F32 lengthInBox = (collisionPoint1 - collisionPoint2).len();
  										F32 weight = lengthInBox;

										//we'll take the average of the two collision points
										Point3F collisionPoint = (collisionPoint1 + collisionPoint2) / 2.0f;

										totalWeight += weight;
										*ledgeNormal += normal * weight;
										*ledgePoint += collisionPoint * weight;
										*canMoveLeft = *canMoveLeft || !wBox.isContained(vertex2);
										*canMoveRight = *canMoveRight || !wBox.isContained(vertex1);

										#ifdef ENABLE_DEBUGDRAW
                              if (sRenderHelpers)
                              {
                                 //draw the edge
                                 DebugDrawer::get()->drawLine(vertex1, vertex2, LinearColorF(1.0f, 0.0f, 0.5f));
                                 DebugDrawer::get()->setLastTTL(TickMs);

                                 //draw the edgeNormal at the midPoint
                                 Point3F midPoint = (vertex1 + vertex2) / 2.0f;
                                 DebugDrawer::get()->drawLine(midPoint, midPoint + edgeNormal, LinearColorF(0.0f, 0.5f, 0.5f));
                                 DebugDrawer::get()->setLastTTL(TickMs);
                              }
										#endif
									}
								}
							}
						}
					}
				}
			}
		}

		if(totalWeight > 0)
		{
			*ledge = true;

			//divide to finish the weighted averages
			*ledgePoint /= totalWeight;
			*ledgeNormal /= totalWeight;

			#ifdef ENABLE_DEBUGDRAW
         if (sRenderHelpers)
         {
            //draw the ledge point
            DebugDrawer::get()->drawLine(*ledgePoint, *ledgePoint + *ledgeNormal, ColorI::BLACK);
            DebugDrawer::get()->setLastTTL(TickMs);
         }
			#endif
		}
		else
		{
			//we failed to find a suitable ledge
			*ledge = false;
		}
	}
}

//-------------------------------------------------------------------
// AAKPlayer::findAdjacentPoly
//
// Given a polyList, an edge defined by 2 verticies, and a polygon index,
// this method finds another polygon in the polylist that shares the edge.
// Returns: true if polygon found (and sets adjPolyIndex), false otherwise.
//-------------------------------------------------------------------
bool AAKPlayer::findAdjacentPoly(ConcretePolyList* polyList, Point3F vertex1, Point3F vertex2, U32 polyIndex, U32* adjPolyIndex)
{
	Point3F edgeUnitVec = vertex1 - vertex2;
	edgeUnitVec.normalizeSafe();

	for (U32 i = 0; i < polyList->mPolyList.size(); i++)
	{
		//don't find this polygon
		if(i == polyIndex)
			continue;

		//loop through all the verticies of this polygon
		for (S32 j = polyList->mPolyList[i].vertexStart; j < polyList->mPolyList[i].vertexStart + polyList->mPolyList[i].vertexCount; j++)
		{
			S32 vertex1Index = j;
			S32 vertex2Index = j + 1;

			//the last vertex is connected to the first
			if(j == polyList->mPolyList[i].vertexStart + polyList->mPolyList[i].vertexCount - 1) 
				vertex2Index = polyList->mPolyList[i].vertexStart;

			//get the verticies
			Point3F vertex1B = polyList->mVertexList[polyList->mIndexList[vertex1Index]];
			Point3F vertex2B = polyList->mVertexList[polyList->mIndexList[vertex2Index]];

			//Now we need to compare the edge defined by vertex1 and vertex2 to the edge defined by vertex1B
			//and vertex2B. If they form the same line (and at least partially overlap), we've found a match.

			//as a quick check, the vectors should be opposite
			Point3F edgeBUnitVec = vertex1B - vertex2B;
			edgeBUnitVec.normalizeSafe();
			F32 dot = mDot(edgeUnitVec, edgeBUnitVec);
			if(dot >= -0.99f)	//use a little fudge
				continue;

			//find the shortest distance from vertex1B (and then vertex2B) to the infinite line defined
			//by vertex1 and vertex2, if the distances are both zero, we've found an identical edge
			F32 dist1 = (mCross(edgeUnitVec, vertex1 - vertex1B)).len();
			F32 dist2 = (mCross(edgeUnitVec, vertex1 - vertex2B)).len();

			//we'll use a little fudge here since geometry is rarely perfect
			F32 threshold = 0.001f;
			if(dist1 <= threshold && dist2 <= threshold)
			{
				//The edges must at least partially overlap. If both vertex1B and
				//vertex2B are "outside" and on the same side, they don't overlap
				PlaneF plane1(vertex1, edgeUnitVec);
				PlaneF plane2(vertex2, edgeUnitVec);
				F32 p1v1B = plane1.distToPlane(vertex1B);
				F32 p1v2B = plane1.distToPlane(vertex2B);
				F32 p2v1B = plane2.distToPlane(vertex1B);
				F32 p2v2B = plane2.distToPlane(vertex2B);

				bool overlap = !(
					(p1v1B < 0 && p1v2B < 0
					&& p2v1B < 0 && p2v2B < 0)
					||
					(p1v1B > 0 && p1v2B > 0
					&& p2v1B > 0 && p2v2B > 0)
					);

				if(overlap)
				{
					*adjPolyIndex = i;
					return true;
				}
			}
		}
	}
	return false;
}


//-------------------------------------------------------------------
// AAKPlayer::canStartLedgeGrab
//
// Returns true if the player is currently allowed to enter the grab state
//-------------------------------------------------------------------
bool AAKPlayer::canStartLedgeGrab()
{
	return mState == MoveState && mDamageState == Enabled && !isMounted()
		&& (mPose == StandPose || mPose == SprintPose)
		&& !mDieOnNextCollision
		&& mVelocity.z <= mDataBlock->climbSpeedUp
		&& !mLedgeState.ignoreLedge;
}

//-------------------------------------------------------------------
// AAKPlayer::canLedgeGrab
//
// Returns true if the player is currently allowed to be in the grab state
//-------------------------------------------------------------------
bool AAKPlayer::canLedgeGrab()
{
	return mState == MoveState && mDamageState == Enabled && !isMounted()
		&& (mPose == StandPose || mPose == SprintPose)
		&& !mDieOnNextCollision
  		&& !(mClimbState.active && mClimbState.direction == MOVE_DIR_DOWN)
		&& !mLedgeState.ignoreLedge;
}

//-------------------------------------------------------------------
// AAKPlayer::getLedgeUpPosition
//
// Returns the point the player will teleport to at the end of the ledge up anim
//-------------------------------------------------------------------
Point3F AAKPlayer::getLedgeUpPosition()
{
	Point3F pos = getPosition();
	Point3F offset = mLedgeState.ledgeNormal;
	offset.normalize(mDataBlock->grabUpForwardOffset);
	pos -= offset;
	pos.z = mLedgeState.ledgePoint.z + mDataBlock->grabUpUpwardOffset;
	return pos;
}


//-------------------------------------------------------------------
// AAKPlayer::canStartLedgeUp
//
// Returns true if there is room above for the player to pull himself up
//-------------------------------------------------------------------
bool AAKPlayer::canStartLedgeUp() 
{
	//can't start if we're already doing it
	if(mLedgeState.climbingUp)
		return false;

	//make sure we're in ledge idle (and no blend in progress)
	if(mActionAnimation.action != AAKPlayerData::LedgeIdleAnim
		|| mShapeInstance->inTransition())
		return false;

	//check that we have space above to stand
	Point3F pos = getLedgeUpPosition();	
	return worldBoxIsClear(createObjectBox(mDataBlock->grabUpTestBox), pos);
}

//-------------------------------------------------------------------
// AAKPlayer::updateLedgeUpAnimation
//
// If necessary, moves the ledge up animation forward / backward
//-------------------------------------------------------------------
void AAKPlayer::updateLedgeUpAnimation()
{
	//Ubiq: integrate ledge climb up animation
	if(mLedgeState.climbingUp && mActionAnimation.action == AAKPlayerData::LedgeUpAnim)
	{
		//update delta
		mLedgeState.deltaAnimPosVec = mLedgeState.animPos;

		F32 vel = mDataBlock->grabSpeedUp * TickSec;

		//play in reverse?
		if(mLedgeState.direction != MOVE_DIR_UP && mLedgeState.animPos < 0.2)
			vel *= -1;

		//update the animation position
		F32 pos = mLedgeState.animPos + vel;
		mLedgeState.animPos = mClampF(pos, 0.0f, 1.0f);
		if (mActionAnimation.thread)
		{
			mShapeInstance->clearTransition(mActionAnimation.thread);
			mShapeInstance->setPos(mActionAnimation.thread, mLedgeState.animPos);
		}

		//tell the client it's changed
		setMaskBits(LedgeUpMask);

		if(isClientObject())
		{
			//calc delta for backstepping
			mLedgeState.deltaAnimPos = mLedgeState.animPos;
			mLedgeState.deltaAnimPosVec = mLedgeState.deltaAnimPosVec - mLedgeState.deltaAnimPos;
		}
	}
} 

//-------------------------------------------------------------------
// AAKPlayer::snapToPlane
//
// Collide player with given plane and return the new position
// Note: the player is NOT actually moved here!
//-------------------------------------------------------------------
Point3F AAKPlayer::snapToPlane(PlaneF plane)
{
	//get our collision box
	Box3F boundingBox(mObjBox);
	Point3F pos = getPosition();
	boundingBox.minExtents += pos;
	boundingBox.maxExtents += pos;
	
	//we'll test all 8 verticies of our collision box
	Vector<Point3F> testPoints;
	testPoints.push_back(Point3F(boundingBox.maxExtents.x, boundingBox.maxExtents.y, boundingBox.maxExtents.z));
	testPoints.push_back(Point3F(boundingBox.minExtents.x, boundingBox.maxExtents.y, boundingBox.maxExtents.z));
	testPoints.push_back(Point3F(boundingBox.maxExtents.x, boundingBox.minExtents.y, boundingBox.maxExtents.z));
	testPoints.push_back(Point3F(boundingBox.minExtents.x, boundingBox.minExtents.y, boundingBox.maxExtents.z));
	testPoints.push_back(Point3F(boundingBox.maxExtents.x, boundingBox.maxExtents.y, boundingBox.minExtents.z));
	testPoints.push_back(Point3F(boundingBox.minExtents.x, boundingBox.maxExtents.y, boundingBox.minExtents.z));
	testPoints.push_back(Point3F(boundingBox.maxExtents.x, boundingBox.minExtents.y, boundingBox.minExtents.z));
	testPoints.push_back(Point3F(boundingBox.minExtents.x, boundingBox.minExtents.y, boundingBox.minExtents.z));

	//find the point with the least distance to plane
	//negative best distance = pull player out of the plane
	//positive best distance = push player onto plane
	F32 bestDist = F32_MAX;
	Point3F bestPoint(0,0,0);
	for(int i = 0; i < testPoints.size(); i++)
	{
		F32 dist = plane.distToPlane(testPoints[i]);
		if(dist < bestDist)
		{
			bestDist = dist;
			bestPoint = testPoints[i];
		}
	}
	
	//project the selected point onto the plane
	Point3F projected = plane.project(bestPoint);

	//find how much it moved
	Point3F offset = projected - bestPoint;

	return pos + offset;
}
