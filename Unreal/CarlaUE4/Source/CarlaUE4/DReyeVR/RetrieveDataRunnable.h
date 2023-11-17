// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic

#pragma once

#include "CoreMinimal.h"
#include "EgoVehicle.h"

/**
 * 
 */
class CARLAUE4_API RetrieveDataRunnable : public FRunnable
{
private:
    AEgoVehicle* EgoVehicle;    // Pointer required for calling data retrieve methods
    FRunnableThread* Thread;    // Thread to run the worker FRunnable on
    FThreadSafeCounter StopTaskCounter; // Stop this thread? Uses Thread Safe Counter

public:
    RetrieveDataRunnable(AEgoVehicle* InEgoVehicleInstance);
    virtual ~RetrieveDataRunnable();

    virtual uint32 Run() override;
    void Stop();
};
