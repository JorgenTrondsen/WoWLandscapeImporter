// Copyright Epic Games, Inc. All Rights Reserved.

#include "WoWLandscapeImporterCommands.h"

#define LOCTEXT_NAMESPACE "FWoWLandscapeImporterModule"

void FWoWLandscapeImporterCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "WoWLandscapeImporter", "Bring up WoWLandscapeImporter window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
