// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Style/WoWLandscapeImporterStyle.h"

class FWoWLandscapeImporterCommands : public TCommands<FWoWLandscapeImporterCommands>
{
public:
	FWoWLandscapeImporterCommands()
		: TCommands<FWoWLandscapeImporterCommands>(TEXT("WoWLandscapeImporter"), NSLOCTEXT("Contexts", "WoWLandscapeImporter", "WoWLandscapeImporter Plugin"), NAME_None, FWoWLandscapeImporterStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};