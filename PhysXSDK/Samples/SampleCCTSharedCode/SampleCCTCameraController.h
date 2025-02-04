/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  

#include "PhysXSampleApplication.h"
#include "SampleCameraController.h"
#include "characterkinematic/PxController.h"
#include "characterkinematic/PxControllerObstacles.h"

class ControlledActor;

class SampleCCTCameraController : public CameraController
{
	public:
													SampleCCTCameraController(PhysXSample& base);

						void						setControlled(ControlledActor** controlled, PxU32 controlledIndex, PxU32 nbCCTs);

		virtual			void				onDigitalInputEvent(const SampleFramework::InputEvent& , bool val);
		virtual			void				onAnalogInputEvent(const SampleFramework::InputEvent& , float val);
		virtual			void				onPointerInputEvent(const SampleFramework::InputEvent&, physx::PxU32, physx::PxU32, physx::PxReal, physx::PxReal, bool val);
		virtual			void				collectInputEvents(std::vector<const SampleFramework::InputEvent*>& inputEvents);

		virtual			void				update(Camera& camera, PxReal dtime);
		virtual			PxReal				getCameraSpeed()
		{ 
			return mKeyShiftDown ? mRunningSpeed : mWalkingSpeed ; 
		}
						void						setCameraMaxSpeed(PxReal speed)			{ mCameraMaxSpeed = speed; }
						void						setView(PxReal pitch, PxReal yaw);
						void						startJump();

		PX_FORCE_INLINE ControlledActor*			getControlledActor()					{ return mCCTs[mControlledIndex];	}
		PX_FORCE_INLINE void						setJumpForce(PxReal force)				{ mJumpForce = force;				}
		PX_FORCE_INLINE void						setGravity(PxReal g)					{ mGravity = g;						}
		PX_FORCE_INLINE void						enableCCT(bool bState)					{ mCCTEnabled = bState;			    }
		PX_FORCE_INLINE bool						getCCTState()							{ return mCCTEnabled;				}

		// The CCT's physics & rendering positions don't always match if the CCT
		// is updated with variable timesteps and the physics with fixed-timesteps.
		// true to link the camera to the CCT's physics position.
		// false to link the camera to the CCT's rendering position.
		PX_FORCE_INLINE void						setCameraLinkToPhysics(bool b)			{ mLinkCameraToPhysics = b;			}
		PX_FORCE_INLINE bool						getCameraLinkToPhysics()		const	{ return mLinkCameraToPhysics;		}

		PX_FORCE_INLINE void						setObstacleContext(PxObstacleContext* context)		{ mObstacleContext = context;	}
		PX_FORCE_INLINE void						setFilterData(const PxFilterData* filterData)		{ mFilterData = filterData;		}
		PX_FORCE_INLINE void						setFilterCallback(PxSceneQueryFilterCallback* cb)	{ mFilterCallback = cb;			}
		PX_FORCE_INLINE void						setCCTFilterCallback(PxControllerFilterCallback* cb){ mCCTFilterCallback = cb;		}

	private:
						SampleCCTCameraController& operator=(const SampleCCTCameraController&);
						PhysXSample&				mBase;	// PT: TODO: find a way to decouple us from PhysXSampleApplication. Only needed for "recenterCursor". Maybe the app could inherit from the cam...

						PxObstacleContext*			mObstacleContext;	// User-defined additional obstacles
						const PxFilterData*			mFilterData;		// User-defined filter data for 'move' function
						PxSceneQueryFilterCallback*	mFilterCallback;	// User-defined filter data for 'move' function
						PxControllerFilterCallback*	mCCTFilterCallback;	// User-defined filter data for 'move' function

						PxU32						mControlledIndex;
						PxU32						mNbCCTs;
						ControlledActor**			mCCTs;

						PxReal						mTargetYaw, mTargetPitch;
						PxReal						mPitchMin,	mPitchMax;

						PxReal						mGamepadPitchInc, mGamepadYawInc;
						PxReal						mGamepadForwardInc, mGamepadLateralInc;
						PxReal						mSensibility;

						bool						mFwd,mBwd,mLeft,mRight,mKeyShiftDown;
						bool						mCCTEnabled;

						PxReal						mRunningSpeed;
						PxReal						mWalkingSpeed;
						PxReal						mGamepadWalkingSpeed;
						PxReal						mCameraMaxSpeed;
						PxReal						mJumpForce;
						PxReal						mGravity;

						bool						mLinkCameraToPhysics;
};
