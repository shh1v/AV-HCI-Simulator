// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic


#include "DReyeVR/TaskProgressBar.h"

#include "ProgressBar.h"

void UTaskProgressBar::SetProgress(float InProgress)
{
    if (TaskProgressBar)
    {
        TaskProgressBar->SetPercent(InProgress);
    }
}