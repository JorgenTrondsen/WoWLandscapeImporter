// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Math/Color.h"
#include "LandscapeProxy.h"

class FToolBarBuilder;
class FMenuBuilder;
class ULandscapeLayerInfoObject;
class ULandscapeGrassType;

struct Layer
{
	FName LayerName;
	int32 ImageIndex;
	int32 ChannelIndex;
};

/** Struct to hold layer data within a chunk*/
struct Chunk
{
	TArray<Layer> Layers;
};

/** Struct to represent a tile in the landscape grid */
struct Tile
{
	TArray<uint16> HeightmapData;
	TArray<TArray<FColor>> AlphamapPNGs;
	TArray<Chunk> Chunks;

	uint8 Column, Row;

	Tile()
	{
		Chunks.SetNum(256);
		AlphamapPNGs.SetNum(2);
	}
};

struct LayerMetadata
{
	TObjectPtr<ULandscapeLayerInfoObject> LayerInfo;
	TObjectPtr<UTexture2D> LayerTexture;
	TObjectPtr<ULandscapeGrassType> FoliageAsset;
};

struct ActorData
{
	FString ModelPath;
	FString Tile;
	FString ParentWMO;
	FVector Position;
	FRotator Rotation;
	double Scale;

	// Equality operator for TArray::Contains
	bool operator==(const ActorData &Other) const
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
	void ImportLandscape();

private:
	void RegisterMenus();

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

	/** Update the status message in the UI */
	void UpdateStatusMessage(const FString &Message, bool bIsError = false);

	/** Function to import and create landscape layers */
	void ImportLayers(TMap<int, TPair<FString, int>> &TexturePaths, TArray<FString> &FoliageFiles, TArray<FString> &FoliageJSONs);

	TArray<UStaticMesh *> ImportModels(TArray<FString> &ModelPaths, UMaterial *ModelMaterial);

	/** Function to create proxy data for landscape import */
	TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> CreateProxyData(const int Row, const int Column);

	/** Helper functions*/
	TSharedPtr<FJsonObject> LoadJsonObject(const FString& FilePath);
	template <typename ArrayType>
	bool LoadImageData(const FString& FilePath, ERGBFormat RGBFormat, int32 BitDepth, TArray<ArrayType>& OutRawData)
	{
		TArray<uint8> FileData;
		if (FFileHelper::LoadFileToArray(FileData, *FilePath))
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
			{
				TArray<uint8> RawData;
				if (ImageWrapper->GetRaw(RGBFormat, BitDepth, RawData))
				{
					OutRawData.SetNumUninitialized(RawData.Num() / sizeof(ArrayType));
					FMemory::Memcpy(OutRawData.GetData(), RawData.GetData(), RawData.Num());
					return true;
				}
			}
		}
		return false;
	}

	UMaterial *CreateModelMaterial(const FString MaterialName, bool isFoliage = false);

	void CreateLandscapeMaterial(ALandscape *Landscape);

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

	/** Components per proxy setting */
	int WPGridSize = 1;

	TArray<TArray<Tile>> TileGrid;

	FString DirectoryPath;

	/** Key-value store for data and metadata of landscape layers */
	TMap<FName, LayerMetadata> LayerMetadataMap;
};
