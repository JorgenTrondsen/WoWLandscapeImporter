// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Math/Color.h"
#include "LandscapeProxy.h"

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
	TArray<uint16> HeightmapData;
	TArray<FColor> AlphamapData;
	TArray<Chunk> Chunks;

	uint8 Column, Row;

	Tile()
	{
		Chunks.SetNum(256);
	}
};

struct LayerMetadata
{
	TObjectPtr<ULandscapeLayerInfoObject> LayerInfo;
	TObjectPtr<UTexture2D> LayerTexture;
};

struct ActorData
{
	FString ModelPath;
	FVector Position;
	FRotator Rotation;
	double Scale;

	// Equality operator for TArray::Contains
	bool operator==(const ActorData& Other) const
	{
		return ModelPath == Other.ModelPath &&
			   Position.Equals(Other.Position, 0.1f) &&
			   Rotation.Equals(Other.Rotation, 0.1f) &&
			   FMath::IsNearlyEqual(Scale, Other.Scale, 0.1f);
	}
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

	/** Function to create layer info corresponding to a texture */
	FName CreateLayerInfo(const FString &TextureFileName, const FString &DestinationDirectory, UObject *ImportedTexture);

	/** Function to import/find asset, returns the imported/found asset */
	UTexture2D *ImportTexture(const FString &TexturePath, const FString &DestinationDirectory);

	/** Test function for asset import */
	TArray<UStaticMesh*> ImportModels(TArray<FString> &ModelPaths);

	/** Function to create proxy data for landscape import */
	TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> CreateProxyData(const int TileRow, const int TileCol, const uint8 CompPerProxy);

	void CreateLandscapeMaterial(const FString &BaseDirectoryPath, ALandscape *Landscape);

	template <typename NodeType>
	NodeType *CreateNode(NodeType *NewObject, int32 EditorX, int32 EditorY, UMaterial *LandscapeMaterial)
	{
		NodeType *Node = NewObject;
		Node->MaterialExpressionEditorX = EditorX;
		Node->MaterialExpressionEditorY = EditorY;
		LandscapeMaterial->GetExpressionCollection().AddExpression(Node);
		return Node;
	}

	TSharedPtr<class FUICommandList> PluginCommands;

	/** Status message widget reference */
	TSharedPtr<class STextBlock> StatusMessageWidget;

	TArray<TArray<Tile>> TileGrid;

	/** Key-value store for data and metadata of landscape layers */
	TMap<FName, LayerMetadata> LayerMetadataMap;
};
