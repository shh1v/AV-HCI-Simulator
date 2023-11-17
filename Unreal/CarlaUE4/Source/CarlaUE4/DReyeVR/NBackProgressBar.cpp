// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic


#include "DReyeVR/NBackProgressBar.h"

#include "ProgressBar.h"

void UNBackProgressBar::SetProgress(float InProgress)
{
    if (ProgressBar)
    {
        ProgressBar->SetPercent(InProgress);
    }
}
