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

// ****************************************************************************
// This snippet illustrates simple use of PxVehicleNoDrive.
//
// It creates a vehicle on a plane and then controls the vehicle so that it performs a 
// number of choreographed manoeuvres such as accelerate, reverse, brake, handbrake, and turn.

// It is a good idea to record and playback with pvd (PhysX Visual Debugger).
// ****************************************************************************

#include <ctype.h>

#include "PxPhysicsAPI.h"

#include "vehicle/PxVehicleUtil.h"
#include "../SnippetVehicleCommon/SnippetVehicleRaycast.h"
#include "../SnippetVehicleCommon/SnippetVehicleFilterShader.h"
#include "../SnippetVehicleCommon/SnippetVehicleTireFriction.h"
#include "../SnippetVehicleCommon/SnippetVehicleCreate.h"

#include "../SnippetCommon/SnippetPrint.h"
#include "../SnippetCommon/SnippetPVD.h"
#include "../SnippetUtils/SnippetUtils.h"


using namespace physx;

PxDefaultAllocator		gAllocator;
PxDefaultErrorCallback	gErrorCallback;

PxFoundation*			gFoundation = NULL;
PxPhysics*				gPhysics	= NULL;

PxDefaultCpuDispatcher*	gDispatcher = NULL;
PxScene*				gScene		= NULL;

PxCooking*				gCooking	= NULL;

PxMaterial*				gMaterial	= NULL;

PxVisualDebuggerConnection*		 
						gConnection	= NULL;

VehicleSceneQueryData*	gVehicleSceneQueryData = NULL;
PxBatchQuery*			gBatchQuery = NULL;

PxVehicleDrivableSurfaceToTireFrictionPairs* gFrictionPairs = NULL;

PxRigidStatic*			gGroundPlane = NULL;
PxVehicleNoDrive*		gVehicleNoDrive	= NULL;

enum DriveMode
{
	eDRIVE_MODE_ACCEL_FORWARDS=0,
	eDRIVE_MODE_ACCEL_REVERSE,
	eDRIVE_MODE_HARD_TURN_LEFT,
	eDRIVE_MODE_HANDBRAKE_TURN_LEFT,
	eDRIVE_MODE_HARD_TURN_RIGHT,
	eDRIVE_MODE_HANDBRAKE_TURN_RIGHT,
	eDRIVE_MODE_BRAKE,
	eDRIVE_MODE_NONE
};

DriveMode gDriveModeOrder[] =
{
	eDRIVE_MODE_BRAKE,
	eDRIVE_MODE_ACCEL_FORWARDS,
	eDRIVE_MODE_BRAKE,
	eDRIVE_MODE_ACCEL_REVERSE,
	eDRIVE_MODE_BRAKE,
	eDRIVE_MODE_HARD_TURN_LEFT, 
	eDRIVE_MODE_BRAKE,
	eDRIVE_MODE_HARD_TURN_RIGHT,
	eDRIVE_MODE_ACCEL_FORWARDS,
	eDRIVE_MODE_HANDBRAKE_TURN_LEFT,
	eDRIVE_MODE_ACCEL_FORWARDS,
	eDRIVE_MODE_HANDBRAKE_TURN_RIGHT,
	eDRIVE_MODE_NONE
};

PxF32					gVehicleModeLifetime = 4.0f;
PxF32					gVehicleModeTimer = 0.0f;
bool					gVehicleOrderComplete = false;
PxU32					gVehicleOrderProgress = 0;

VehicleDesc initVehicleDesc()
{
	//Set up the chassis mass, dimensions, moment of inertia, and center of mass offset.
	//The moment of inertia is just the moment of inertia of a cuboid but modified for easier steering.
	//Center of mass offset is 0.65m above the base of the chassis and 0.25m towards the front.
	const PxF32 chassisMass = 1500.0f;
	const PxVec3 chassisDims(2.5f,2.0f,5.0f);
	const PxVec3 chassisMOI
		((chassisDims.y*chassisDims.y + chassisDims.z*chassisDims.z)*chassisMass/12.0f,
		 (chassisDims.x*chassisDims.x + chassisDims.z*chassisDims.z)*0.8f*chassisMass/12.0f,
		 (chassisDims.x*chassisDims.x + chassisDims.y*chassisDims.y)*chassisMass/12.0f);
	const PxVec3 chassisCMOffset(0.0f, -chassisDims.y*0.5f + 0.65f, 0.25f);

	//Set up the wheel mass, radius, width, moment of inertia, and number of wheels.
	//Moment of inertia is just the moment of inertia of a cylinder.
	const PxF32 wheelMass = 20.0f;
	const PxF32 wheelRadius = 0.5f;
	const PxF32 wheelWidth = 0.4f;
	const PxF32 wheelMOI = 0.5f*wheelMass*wheelRadius*wheelRadius;
	const PxU32 nbWheels = 4;

	VehicleDesc vehicleDesc;
	vehicleDesc.chassisMass = chassisMass;
	vehicleDesc.chassisDims = chassisDims;
	vehicleDesc.chassisMOI = chassisMOI;
	vehicleDesc.chassisCMOffset = chassisCMOffset;
	vehicleDesc.chassisMaterial = gMaterial;
	vehicleDesc.wheelMass = wheelMass;
	vehicleDesc.wheelRadius = wheelRadius;
	vehicleDesc.wheelWidth = wheelWidth;
	vehicleDesc.wheelMOI = wheelMOI;
	vehicleDesc.numWheels = nbWheels;
	vehicleDesc.wheelMaterial = gMaterial;
	return vehicleDesc;
}

void startAccelerateForwardsMode()
{
	gVehicleNoDrive->setDriveTorque(0, 1000.0f);
	gVehicleNoDrive->setDriveTorque(1, 1000.0f);
}

void startAccelerateReverseMode()
{
	gVehicleNoDrive->setDriveTorque(0, -1000.0f);
	gVehicleNoDrive->setDriveTorque(1, -1000.0f);
}

void startBrakeMode()
{
	gVehicleNoDrive->setBrakeTorque(0, 1000.0f);
	gVehicleNoDrive->setBrakeTorque(1, 1000.0f);
	gVehicleNoDrive->setBrakeTorque(2, 1000.0f);
	gVehicleNoDrive->setBrakeTorque(3, 1000.0f);
}

void startTurnHardLeftMode()
{
	gVehicleNoDrive->setDriveTorque(0, 1000.0f);
	gVehicleNoDrive->setDriveTorque(1, 1000.0f);
	gVehicleNoDrive->setSteerAngle(0, 1.0f); 
	gVehicleNoDrive->setSteerAngle(1, 1.0f); 
}

void startTurnHardRightMode()
{
	gVehicleNoDrive->setDriveTorque(0, 1000.0f);
	gVehicleNoDrive->setDriveTorque(1, 1000.0f);
	gVehicleNoDrive->setSteerAngle(0, -1.0f); 
	gVehicleNoDrive->setSteerAngle(1, -1.0f); 
}

void startHandbrakeTurnLeftMode()
{
	gVehicleNoDrive->setBrakeTorque(2, 1000.0f);
	gVehicleNoDrive->setBrakeTorque(3, 1000.0f);
	gVehicleNoDrive->setDriveTorque(0, 1000.0f);
	gVehicleNoDrive->setDriveTorque(1, 1000.0f);
	gVehicleNoDrive->setSteerAngle(0, 1.0f); 
	gVehicleNoDrive->setSteerAngle(1, 1.0f); 
}

void startHandbrakeTurnRightMode()
{
	gVehicleNoDrive->setBrakeTorque(2, 1000.0f);
	gVehicleNoDrive->setBrakeTorque(3, 1000.0f);
	gVehicleNoDrive->setDriveTorque(0, 1000.0f);
	gVehicleNoDrive->setDriveTorque(1, 1000.0f);
	gVehicleNoDrive->setSteerAngle(0, -1.0f); 
	gVehicleNoDrive->setSteerAngle(1, -1.0f); 
}

void releaseAllControls()
{
	gVehicleNoDrive->setDriveTorque(0, 0.0f);
	gVehicleNoDrive->setDriveTorque(1, 0.0f);
	gVehicleNoDrive->setDriveTorque(2, 0.0f);
	gVehicleNoDrive->setDriveTorque(3, 0.0f);

	gVehicleNoDrive->setBrakeTorque(0, 0.0f);
	gVehicleNoDrive->setBrakeTorque(1, 0.0f);
	gVehicleNoDrive->setBrakeTorque(2, 0.0f);
	gVehicleNoDrive->setBrakeTorque(3, 0.0f);

	gVehicleNoDrive->setSteerAngle(0, 0.0f);
	gVehicleNoDrive->setSteerAngle(1, 0.0f);
	gVehicleNoDrive->setSteerAngle(2, 0.0f);
	gVehicleNoDrive->setSteerAngle(3, 0.0f);
}

void initPhysics()
{
	gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
	PxProfileZoneManager* profileZoneManager = &PxProfileZoneManager::createProfileZoneManager(gFoundation);
	gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(),true, profileZoneManager);

	if(gPhysics->getPvdConnectionManager())
	{
		gPhysics->getVisualDebugger()->setVisualizeConstraints(true);
		gPhysics->getVisualDebugger()->setVisualDebuggerFlag(PxVisualDebuggerFlag::eTRANSMIT_CONTACTS, true);
		gPhysics->getVisualDebugger()->setVisualDebuggerFlag(PxVisualDebuggerFlag::eTRANSMIT_SCENEQUERIES, true);	
		gConnection = PxVisualDebuggerExt::createConnection(gPhysics->getPvdConnectionManager(), PVD_HOST, 5425, 10);
	}

	PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
	
	PxU32 numWorkers = 1;
	gDispatcher = PxDefaultCpuDispatcherCreate(numWorkers);
	sceneDesc.cpuDispatcher	= gDispatcher;
	sceneDesc.filterShader	= VehicleFilterShader;

	gScene = gPhysics->createScene(sceneDesc);

	gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.6f);

	gCooking = 	PxCreateCooking(PX_PHYSICS_VERSION, *gFoundation, PxCookingParams(PxTolerancesScale()));	

	/////////////////////////////////////////////

	PxInitVehicleSDK(*gPhysics);
	PxVehicleSetBasisVectors(PxVec3(0,1,0), PxVec3(0,0,1));
	PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);

	//Create the batched scene queries for the suspension raycasts.
	gVehicleSceneQueryData = VehicleSceneQueryData::allocate(1, PX_MAX_NB_WHEELS, 1, gAllocator);
	gBatchQuery = VehicleSceneQueryData::setUpBatchedSceneQuery(0, *gVehicleSceneQueryData, gScene);

	//Create the friction table for each combination of tire and surface type.
	gFrictionPairs = createFrictionPairs(gMaterial);
	
	//Create a plane to drive on.
	gGroundPlane = createDrivablePlane(gMaterial, gPhysics);
	gScene->addActor(*gGroundPlane);

	//Create a vehicle that will drive on the plane.
	VehicleDesc vehicleDesc = initVehicleDesc();
	gVehicleNoDrive = createVehicleNoDrive(vehicleDesc, gPhysics, gCooking);
	PxTransform startTransform(PxVec3(0, (vehicleDesc.chassisDims.y*0.5f + vehicleDesc.wheelRadius + 1.0f), 0), PxQuat(PxIdentity));
	gVehicleNoDrive->getRigidDynamicActor()->setGlobalPose(startTransform);
	gScene->addActor(*gVehicleNoDrive->getRigidDynamicActor());

	//Set the vehicle to rest in first gear.
	//Set the vehicle to use auto-gears.
	gVehicleNoDrive->setToRestState();

	gVehicleModeTimer = 0.0f;
	gVehicleOrderProgress = 0;
	startBrakeMode();
}

void incrementDrivingMode(const PxF32 timestep)
{
	gVehicleModeTimer += timestep;
	if(gVehicleModeTimer > gVehicleModeLifetime)
	{
		//Increment to next driving mode.
		gVehicleModeTimer = 0.0f;
		gVehicleOrderProgress++;
		releaseAllControls();

		//If we are at the end of the list of driving modes then start again.
		if(eDRIVE_MODE_NONE == gDriveModeOrder[gVehicleOrderProgress])
		{
			gVehicleOrderProgress = 0;
			gVehicleOrderComplete = true;
		}

		//Start driving in the selected mode.
		DriveMode eDriveMode = gDriveModeOrder[gVehicleOrderProgress];
		switch(eDriveMode)
		{
		case eDRIVE_MODE_ACCEL_FORWARDS:
			startAccelerateForwardsMode();
			break;
		case eDRIVE_MODE_ACCEL_REVERSE:
			startAccelerateReverseMode();
			break;
		case eDRIVE_MODE_HARD_TURN_LEFT:
			startTurnHardLeftMode();
			break;
		case eDRIVE_MODE_HANDBRAKE_TURN_LEFT:
			startHandbrakeTurnLeftMode();
			break;
		case eDRIVE_MODE_HARD_TURN_RIGHT:
			startTurnHardRightMode();
			break;
		case eDRIVE_MODE_HANDBRAKE_TURN_RIGHT:
			startHandbrakeTurnRightMode();
			break;
		case eDRIVE_MODE_BRAKE:
			startBrakeMode();
			break;
		case eDRIVE_MODE_NONE:
			break;
		};
	}
}

void stepPhysics()
{
	const PxF32 timestep = 1.0f/60.0f;

	//Cycle through the driving modes to demonstrate how to accelerate/reverse/brake/turn etc.
	incrementDrivingMode(timestep);

	//Raycasts.
	PxVehicleWheels* vehicles[1] = {gVehicleNoDrive};
	PxRaycastQueryResult* raycastResults = gVehicleSceneQueryData->getRaycastQueryResultBuffer(0);
	const PxU32 raycastResultsSize = gVehicleSceneQueryData->getRaycastQueryResultBufferSize();
	PxVehicleSuspensionRaycasts(gBatchQuery, 1, vehicles, raycastResultsSize, raycastResults);

	//Vehicle update.
	const PxVec3 grav = gScene->getGravity();
	PxWheelQueryResult wheelQueryResults[PX_MAX_NB_WHEELS];
	PxVehicleWheelQueryResult vehicleQueryResults[1] = {{wheelQueryResults, gVehicleNoDrive->mWheelsSimData.getNbWheels()}};
	PxVehicleUpdates(timestep, grav, *gFrictionPairs, 1, vehicles, vehicleQueryResults);

	//Scene update.
	gScene->simulate(timestep);
	gScene->fetchResults(true);
}
	
void cleanupPhysics()
{
	gVehicleNoDrive->getRigidDynamicActor()->release();
	gVehicleNoDrive->free();
	gGroundPlane->release();
	gBatchQuery->release();
	gVehicleSceneQueryData->free(gAllocator);
	gFrictionPairs->release();
	PxCloseVehicleSDK();

	gMaterial->release();
	gCooking->release();
	gScene->release();
	gDispatcher->release();
	PxProfileZoneManager* profileZoneManager = gPhysics->getProfileZoneManager();
	if(gConnection != NULL)
		gConnection->release();
	gPhysics->release();	
	profileZoneManager->release();
	gFoundation->release();

	printf("SnippetVehicleNoDrive done.\n");
}

void keyPress(const char key, const PxTransform& camera)
{
	PX_UNUSED(camera);
	PX_UNUSED(key);
}

int snippetMain(int, const char*const*)
{
#ifdef RENDER_SNIPPET
	extern void renderLoop();
	renderLoop();
#else
	initPhysics();
	while(!gVehicleOrderComplete)
	{
		stepPhysics();
	}
	cleanupPhysics();
#endif

	return 0;
}
