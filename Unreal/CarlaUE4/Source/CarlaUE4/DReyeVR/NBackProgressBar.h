// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NBackProgressBar.generated.h"

/**
 * 
 */
UCLASS()
class CARLAUE4_API UNBackProgressBar : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Progress")
		void SetProgress(float InProgress);

protected:
	UPROPERTY(meta = (BindWidget))
	class UProgressBar* ProgressBar;
};