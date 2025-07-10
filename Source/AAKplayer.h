//-----------------------------------------------------------------------------
// 3D Action Adventure Kit for T3D
// Copyright (C) 2008-2013 Ubiq Visuals, Inc. (http://www.ubiqvisuals.com/)
//
// This file also incorporates work covered by the following copyright and  
// permission notice:
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

#ifndef _AAK_PLAYER_H_
#define _AAK_PLAYER_H_

#ifndef _PLAYER_H_
#include "T3D/player.h"
#endif

#ifndef _CONCRETEPOLYLIST_H_
#include "collision/concretePolyList.h"
#endif


//----------------------------------------------------------------------------

struct AAKPlayerData: public PlayerData {
   typedef PlayerData Parent;

   /// Zounds!
   enum Sounds {
      //Ubiq sounds:
      stop,
      jumpCrouch,
      jump,
      land,
      climbIdle,
      climbUp,
      climbDown,
      climbLeftRight,
      ledgeIdle,
      ledgeUp,
      ledgeLeftRight,
      slide,//

      FootSoft,
      FootHard,
      FootMetal,
      FootSnow,
      FootShallowSplash,
      FootWading,
      FootUnderWater,
      FootBubbles,
      MoveBubbles,
      WaterBreath,
      ImpactSoft,
      ImpactHard,
      ImpactMetal,
      ImpactSnow,
      ImpactWaterEasy,
      ImpactWaterMedium,
      ImpactWaterHard,
      ExitWater,
      MaxSounds
   };
   DECLARE_SOUNDASSET_ARRAY(AAKPlayerData, PlayerSound, Sounds::MaxSounds);

   enum {
      // *** WARNING ***
      // These enum values are used to index the ActionAnimationList
      // array instantiated in player.cc
      // The first several are selected in the move state based on velocity
      RootAnim,
      RunForwardAnim,
      BackBackwardAnim,
      SideLeftAnim,
      SideRightAnim,
      WalkForwardAnim,

      SprintRootAnim,
      SprintForwardAnim,
      SprintBackwardAnim,
      SprintLeftAnim,
      SprintRightAnim,

      CrouchRootAnim,
      CrouchForwardAnim,
      CrouchBackwardAnim,
      CrouchLeftAnim,
      CrouchRightAnim,

      ProneRootAnim,
      ProneForwardAnim,
      ProneBackwardAnim,

      SwimRootAnim,
      SwimForwardAnim,
      SwimBackwardAnim,
      SwimLeftAnim,
      SwimRightAnim,

      // These are set explicitly based on player actions
      FallAnim,
      JumpAnim,
      StandJumpAnim,
      StandingLandAnim,
	   RunningLandAnim,
      JetAnim,

		//Ubiq:
		//==========================================
		Death1Anim,

		StopAnim,

		WallIdleAnim,
		WallLeftAnim,
		WallRightAnim,

		LedgeIdleAnim,
		LedgeLeftAnim,
		LedgeRightAnim,
		LedgeUpAnim,

		ClimbIdleAnim,
		ClimbUpAnim,
		ClimbDownAnim,
		ClimbLeftAnim,
		ClimbRightAnim,

		SlideFrontAnim,
		SlideBackAnim,

      //
      NumMoveActionAnims = WalkForwardAnim + 1,
      NumTableActionAnims = SlideBackAnim + 1,
      NumExtraActionAnims = 512 - NumTableActionAnims,
      NumActionAnims = NumTableActionAnims + NumExtraActionAnims,
      ActionAnimBits = 9,
      NullAnimation = (1 << ActionAnimBits) - 1
   };

   static ActionAnimationDef ActionAnimationList[NumTableActionAnims];
   ActionAnimation actionList[NumActionAnims];

   DECLARE_CONOBJECT(AAKPlayerData);
   AAKPlayerData();

   bool preload(bool server, String& errorStr) override;
  
   bool isLedgeAction(U32 action);
   bool isClimbAction(U32 action);
   bool isLandAction(U32 action);

   static void initPersistFields();
   virtual void packData(BitStream* stream);
   virtual void unpackData(BitStream* stream);

   //Ubiq:
	Point3F cameraOffset;			///< Offset from player origin camera looks at

	F32 walkRunAnimVelocity;		///< Velocity at which player switches between walk and run animations

   F32 vertDrag;
   F32 vertDragFalling;

	//Ubiq: Turn Rates
	F32 airTurnRate;				///<
	F32 groundTurnRate;				///<

	F32 groundFriction;				///<

	F32 jetTime;					///<

   F32 jumpForceClimb;             ///< Force exerted per jump

	//Ubiq: climbing
	F32 climbHeightMin;				///< Minimum height of climb detection range (relative to players feet)
	F32 climbHeightMax;				///< Maximum height of climb detection range (relative to players feet)
	F32 climbSpeedUp;				///<
	F32 climbSpeedDown;				///<
	F32 climbSpeedSide;				///<
	F32 climbScrapeSpeed;			///<
	F32 climbScrapeFriction;		///<

	//Ubiq: ledge grabbing
	F32 grabHeightMin;				///<
	F32 grabHeightMax;				///<
	F32 grabHeight;					///<
	F32 grabSpeedSide;				///<
	F32 grabSpeedUp;				///<
	F32 grabUpForwardOffset;		///<
	F32 grabUpUpwardOffset;			///<
	Point3F grabUpTestBox;			///< Width, depth and height of box used to test if player has room above to pull himself up

	//Ubiq: Wall hug
	F32 wallHugSpeed;				///< How fast player moves left/right when wall hugging
	F32 wallHugHeightMin;			///< Minimum height of wall detection range (relative to players feet)
	F32 wallHugHeightMax;			///< Maximum height of wall detection range (relative to players feet)

	//Ubiq: Jump delays
	F32 runJumpCrouchDelay;			///<
	F32 standJumpCrouchDelay;		///<

	//Ubiq: ground snap
	F32 groundSnapSpeed;			///<
	F32 groundSnapRayOffset;		///<
	F32 groundSnapRayLength;		///<
   bool orientToGround;       ///< Indicates if the player should rotate to match the ground normal
	
	//Ubiq: Land state
   F32 landDuration;				///< the duration of the land in ms
	F32 landSpeedFactor;			///< the speed reduction factor upon landing

};


//----------------------------------------------------------------------------

class AAKPlayer: public Player
{
   typedef Player Parent;

protected:

   /// Bit masks for different types of events
   enum MaskBits {
      LedgeUpMask = Parent::NextFreeMask << 0,
      NextFreeMask = Parent::NextFreeMask << 1   };

   SFXSource* mSlideSound;        ///< Ubiq: Sound for sliding down or scraping across surfaces

   AAKPlayerData* mDataBlock;    ///< MMmmmmm...datablock...

   struct ContactInfo 
   {
      bool contacted, jump, run, slide;
      SceneObject *contactObject;
      VectorF  contactNormal;

      void clear()
      {
         contacted=jump=run=false; 
         contactObject = NULL; 
         contactNormal.set(1,1,1);
      }

      ContactInfo() { clear(); }

   } mContactInfo;

   //

   virtual void setActionThread(U32 action, bool forward = true, bool hold = false, bool wait = false, bool fsp = true, bool forceSet = false, bool useSynchedPos = false);
   void findContact(bool* run, bool* jump, bool* slide, VectorF* contactNormal);

   bool step(Point3F* pos, F32* maxStep, F32 time);

   Point3F _move(const F32 travelTime, Collision* outCol);

   F32 _doCollisionImpact(const Collision* collision, bool fallingCollision);

   void _findContact(SceneObject** contactObject,
      VectorF* contactNormal,
      Vector<SceneObject*>* outOverlapObjects);

public:
   DECLARE_CONOBJECT(AAKPlayer);
   DECLARE_CATEGORY("Actor \t Controllable");

   AAKPlayer();
   static void consoleInit();
   static void initPersistFields();

   void onRemove() override;
   bool onNewDataBlock(GameBaseData* dptr, bool reload) override;

   void processTick(const Move* move) override;
   void interpolateTick(F32 dt) override;
   void advanceTime(F32 dt) override;

   void setState(ActionState state, U32 recoverTicks);
   void updateState();

   void updateMove(const Move* move) override;

   bool canJump();
   bool canJetJump();
   bool canCrouch();

   void updateAttachment() override;

   // Animation
   bool setActionThread(const char* sequence, bool forward, bool hold, bool wait, bool fsp, bool forceSet, bool useSynchedPos);

   void updateActionThread() override;
   void pickActionAnimation() override;

   void setPosition(const Point3F& pos, const Point3F& rot);
   void setRenderPosition(const Point3F& pos, const Point3F& rot, F32 dt = -1);

   void getCameraParameters(F32* min, F32* max, Point3F* off, MatrixF* rot) override;

   void writePacketData(GameConnection* connection, BitStream* stream) override;
   void readPacketData(GameConnection* connection, BitStream* stream) override;

   U32 packUpdate(NetConnection* con, U32 mask, BitStream* stream) override;
   void unpackUpdate(NetConnection* con, BitStream* stream) override;

   void playFootstepSound(bool triggeredLeft, Material* contactMaterial, SceneObject* contactObject);
   void playImpactSound();
   void updateFroth(F32 dt);

   //----------------------------------------------------------------------------
   // Ubiq custom
   //----------------------------------------------------------------------------
   bool mDieOnNextCollision;

   bool mRunSurface, mJumpSurface, mSlideSurface;

   Point3F getNodePosition(const char *nodeName);
   void setObjectBox(Point3F size);
   Box3F createObjectBox(Point3F size);
   bool worldBoxIsClear(Box3F worldSpaceBox);
   bool worldBoxIsClear(Box3F objSpaceBox, Point3F worldPosition);
   Point3F snapToPlane(PlaneF plane);	//collides player with given plane and returns the new position
   U32 getSurfaceType();
   void updateSounds( F32 dt );                ///< Update sounds
   

   enum MoveDir
   {
	   MOVE_DIR_NONE,
	   MOVE_DIR_UP,
	   MOVE_DIR_DOWN,
	   MOVE_DIR_LEFT,
	   MOVE_DIR_RIGHT
   };

   //-------------------------------------------------------------------
   // Snap to ground
   //-------------------------------------------------------------------
   F32 mGroundSnap;		//offset for render position (client only) on Z axis


   //-------------------------------------------------------------------
   // Slide state
   //-------------------------------------------------------------------
   struct SlideState
   {
	   bool active;
	   Point3F surfaceNormal;
   }
   mSlideState;


   //-------------------------------------------------------------------
   // Jump state
   //-------------------------------------------------------------------
   bool mJumping;			//in the air from a jump?

   enum JumpType
   {
	   JumpType_Run,
	   JumpType_Stand
   };

   struct JumpState
   {
	   bool active;			//are we currently in the standJump state?
	   bool isCrouching;	//are we currently in the "crouch" phase of the jump? (active will also be true)
	   F32 crouchDelay;		//how long we're frozen in the "crouch" phase
	   JumpType jumpType;	//run jump or stand jump?
   } mJumpState;


   //-------------------------------------------------------------------
   // Climb state
   //-------------------------------------------------------------------
   struct ClimbState
   {
      bool active = false;
      Point3F surfaceNormal;		//normal of surface we're currently on (which direction player should face)
      MoveDir direction = MOVE_DIR_NONE;
      bool ignoreClimb = false;
   } mClimbState;
   S32 mClimbTriggerCount;

   void findClimbContact(bool* climb, PlaneF* climbPlane);
   bool canStartClimb();
   bool canClimb();


   //-------------------------------------------------------------------
   // Wall Hug state
   //-------------------------------------------------------------------
   struct WallHugState
   {
      bool active = false;
      Point3F surfaceNormal;		//normal of surface we're currently on (which direction player should face)
      MoveDir direction = MOVE_DIR_NONE;
   } mWallHugState;

   void findWallContact(bool* wall, PlaneF* wallPlane);
   bool canStartWallHug();
   bool canWallHug();


   //-------------------------------------------------------------------
   // Ledge Grab state
   //-------------------------------------------------------------------
   struct LedgeState
   {
      bool active = false;
      Point3F ledgeNormal;		//normal of ledge we're currently on (which direction player should face)
      Point3F ledgePoint;		//point on the ledge where player is grabbing
      MoveDir direction = MOVE_DIR_NONE;
      bool ignoreLedge = false;

      bool climbingUp = false;		//are we pulling ourselves up?
      F32 animPos = 0.0f;			//what pos are we at in the climb up animation? (0 - 1)
      F32 deltaAnimPos = 1.0f;		//for interpolation, the last pos in the climb up animation (0 - 1)
      F32 deltaAnimPosVec = 1.0f;	//for interpolation, how fast are we playing climb up animation?
   }
   mLedgeState;
   void findLedgeContact(bool* ledge, VectorF* ledgeNormal, Point3F* ledgePoint, bool* canMoveLeft, bool* canMoveRight);
   bool findAdjacentPoly(ConcretePolyList* polyList, Point3F vertex1, Point3F vertex2, U32 polyIndex, U32* adjPolyIndex);
   bool canStartLedgeGrab();
   bool canLedgeGrab();
   Point3F getLedgeUpPosition();
   bool canStartLedgeUp();
   void updateLedgeUpAnimation();


   //-------------------------------------------------------------------
   // Land state
   //-------------------------------------------------------------------
   struct LandState
   {
	   bool active;			//is the player landing from a fall or jump?
	   S32 timer;			//timer to track how long before we leave LandState
   }
   mLandState;


   //-------------------------------------------------------------------
   // Stop state
   //-------------------------------------------------------------------
   S32 mStoppingTimer;		//how long we've been slowing down for (ms)
};

#endif
