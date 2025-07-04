// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

/** Struct to represent a tile in the landscape grid */
struct Tile
{
	TArray<uint16> HeightmapData;

	uint8 Column, Row;
};

class FWoWLandscapeImporterModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

	/** Function to handle import button click */
	FReply OnImportButtonClicked();

	/** Function to import heightmaps */
	void ImportLandscape(const FString &DirectoryPath);

private:
	void RegisterMenus();

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

	/** Update the status message in the UI */
	void UpdateStatusMessage(const FString &Message, bool bIsError = false);

private:
	TSharedPtr<class FUICommandList> PluginCommands;

	/** Status message widget reference */
	TSharedPtr<class STextBlock> StatusMessageWidget;
};
