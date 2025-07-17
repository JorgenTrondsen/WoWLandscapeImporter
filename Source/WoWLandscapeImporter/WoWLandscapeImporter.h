// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Math/Color.h"

class FToolBarBuilder;
class FMenuBuilder;
class ULandscapeLayerInfoObject;

/** Struct to hold layer data within a chunk*/
struct Chunk
{
	TArray<FName> Layers;
};

/** Struct to represent a tile in the landscape grid */
struct Tile
{
	TArray<FColor> AlphamapData;
	TArray<Chunk> Chunks;

	uint8 Column, Row;

	Tile()
	{
		Chunks.SetNum(256);
	}
};

struct LayerStruct
{
	TArray<uint8> LayerData;
	TObjectPtr<ULandscapeLayerInfoObject> LayerInfo;
	TObjectPtr<UTexture2D> LayerTexture;
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

	/** Function to import landscape */
	void ImportLandscape(const FString &DirectoryPath);

private:
	void RegisterMenus();

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

	/** Update the status message in the UI */
	void UpdateStatusMessage(const FString &Message, bool bIsError = false);

	/** Function to import texture and create layer info*/
	FName ImportTexture_CreateLayerInfo(const FString &RelativeTexturePath, const FString &BaseDirectoryPath);

	TArray<uint16> CropLandscape(const TArray<uint16> &Heightmap, const uint16 HeightmapWidth, const uint16 CropWidth, const uint16 CropHeight);

	UMaterial *CreateLandscapeMaterial(const FString &BaseDirectoryPath);

	TSharedPtr<class FUICommandList> PluginCommands;

	/** Status message widget reference */
	TSharedPtr<class STextBlock> StatusMessageWidget;

	/** Key-value store for data arrays of landscape layers */
	TMap<FString, LayerStruct> LayerStructMap;
};
