// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic


#include "RetrieveDataRunnable.h"
#include "EgoVehicle.h"


RetrieveDataRunnable::RetrieveDataRunnable(AEgoVehicle* InEgoVehicleInstance)
    : EgoVehicle(InEgoVehicleInstance)
{
    // Now create the thread with the static ThreadStarter
    Thread = FRunnableThread::Create(this, TEXT("RetrieveDataRunnable"), 0, TPri_TimeCritical);
}

RetrieveDataRunnable::~RetrieveDataRunnable()
{
    if (Thread != nullptr)
    {
        // Tell the thread to stop
        Stop();

        // Wait for the thread to finish
        Thread->WaitForCompletion();

        // Delete the Thread
        delete Thread;
        Thread = nullptr;
    }
}

uint32 RetrieveDataRunnable::Run()
{
    // Initial wait before starting
    FPlatformProcess::Sleep(0.03);

    // Establish connections with the required ports
        
    EgoVehicle->EstablishEyeTrackerConnection();    // Establish connection with the eye-tracker
    EgoVehicle->EstablishVehicleStatusConnection(); // Establish vehicle status connection

    FPlatformProcess::Sleep(0.1); // Wait for sometime to ensure connection has been established

    EgoVehicle->UpdateVehicleStatus(AEgoVehicle::VehicleStatus::ManualDrive); // Update the vehicle status to manual mode


    // While not told to stop this thread and not yet finished processing
    while (StopTaskCounter.GetValue() == 0)
    {
        if (EgoVehicle != nullptr)
        {
			// Retrieve all the data from the pupil eye tracker
            EgoVehicle->RetrieveOnSurf();

            // Send the current locally stored vehicle status
            EgoVehicle->SendCurrVehicleStatus();

            // Retrieve vehicle status from the client
            EgoVehicle->RetrieveVehicleStatus();
        }
    }

    // Terminate all the port connections to use it later for next game launch
    EgoVehicle->TerminateEyeTrackerConnection();
    EgoVehicle->TerminateVehicleStatusConnection();

    // Sleep for some time
    FPlatformProcess::Sleep(0.01);

    return 0;
}

void RetrieveDataRunnable::Stop()
{
    StopTaskCounter.Increment();
}
