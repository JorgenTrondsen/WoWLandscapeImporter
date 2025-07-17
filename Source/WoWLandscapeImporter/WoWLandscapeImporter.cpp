#include "WoWLandscapeImporter.h"
#include "Style/WoWLandscapeImporterStyle.h"
#include "Commands/WoWLandscapeImporterCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "LandscapeLayerInfoObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h" // Required for landscape UVs
#include "MaterialEditorUtilities.h"
#include "MaterialGraph/MaterialGraph.h" // **FIX**: Provides the full definition of UMaterialGraph

static const FName WoWLandscapeImporterTabName("WoWLandscapeImporter");

#define LOCTEXT_NAMESPACE "FWoWLandscapeImporterModule"

void FWoWLandscapeImporterModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FWoWLandscapeImporterStyle::Initialize();
	FWoWLandscapeImporterStyle::ReloadTextures();

	FWoWLandscapeImporterCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FWoWLandscapeImporterCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FWoWLandscapeImporterModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FWoWLandscapeImporterModule::RegisterMenus));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(WoWLandscapeImporterTabName, FOnSpawnTab::CreateRaw(this, &FWoWLandscapeImporterModule::OnSpawnPluginTab)).SetDisplayName(LOCTEXT("FWoWLandscapeImporterTabTitle", "WoWLandscapeImporter")).SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FWoWLandscapeImporterModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FWoWLandscapeImporterStyle::Shutdown();

	FWoWLandscapeImporterCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WoWLandscapeImporterTabName);
}

TSharedRef<SDockTab> FWoWLandscapeImporterModule::OnSpawnPluginTab(const FSpawnTabArgs &SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
			[SNew(SBox)
				 .Padding(FMargin(10.0f))
					 [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 2)[SNew(STextBlock).Text(LOCTEXT("WoWLandscapeImporterTitle", "WoW Importer")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)).Justification(ETextJustify::Center)] + SVerticalBox::Slot().AutoHeight().Padding(0, 3)[SNew(STextBlock).Text(LOCTEXT("ImportDescription", "Select directory:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).Justification(ETextJustify::Center)] + SVerticalBox::Slot().AutoHeight().Padding(0, 5)[SNew(SButton).Text(LOCTEXT("ImportButtonText", "Import")).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked_Raw(this, &FWoWLandscapeImporterModule::OnImportButtonClicked).ContentPadding(FMargin(12, 6))] + SVerticalBox::Slot().AutoHeight().Padding(0, 10)[SAssignNew(StatusMessageWidget, STextBlock).Text(FText::GetEmpty()).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).Justification(ETextJustify::Center).ColorAndOpacity(FSlateColor(FLinearColor::White))]]];
}

FReply FWoWLandscapeImporterModule::OnImportButtonClicked()
{
	// Clear any previous status message
	UpdateStatusMessage(TEXT(""), false);

	IDesktopPlatform *DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		FString SelectedDirectory;
		const FString Title = TEXT("Select WoW Landscape Directory");
		const FString DefaultPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE")), TEXT("wow.export\\maps"));

		const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			Title,
			DefaultPath,
			SelectedDirectory);

		if (bFolderSelected && !SelectedDirectory.IsEmpty())
		{
			ImportLandscape(SelectedDirectory);
		}
		else if (!bFolderSelected)
		{
			UpdateStatusMessage(TEXT("Directory selection cancelled"), false);
		}
	}

	return FReply::Handled();
}

void FWoWLandscapeImporterModule::ImportLandscape(const FString &DirectoryPath)
{
	TArray<FString> HeightmapFile;
	TArray<FString> AlphamapPNGs;
	TArray<FString> AlphamapJSONs;

	// Find all required files in the directory
	FString SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.r16"));
	IFileManager::Get().FindFiles(HeightmapFile, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("tex_*.png"));
	IFileManager::Get().FindFiles(AlphamapPNGs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("tex_*.json"));
	IFileManager::Get().FindFiles(AlphamapJSONs, *SearchPattern, true, false);

	// Check if we found all required files
	if (HeightmapFile.IsEmpty() || AlphamapPNGs.IsEmpty() || AlphamapJSONs.IsEmpty())
	{
		UpdateStatusMessage(TEXT("Missing either: R16 files, Alphamap PNGs, Alphamap JSONs"), true);
	}
	else
	{
		double_t Zscale = 0.0;
		uint16_t VerticesX = 0;
		uint16_t VerticesY = 0;

		// Read heightmap metadata JSON and extract heightmap range and resolution
		FString MetadataPath = FPaths::Combine(DirectoryPath, TEXT("heightmap_metadata.json"));
		FString JsonString;
		if (FFileHelper::LoadFileToString(JsonString, *MetadataPath))
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				TSharedPtr<FJsonObject> HeightRangeObject = JsonObject->GetObjectField(TEXT("height_range"));
				double_t Range = HeightRangeObject->GetNumberField(TEXT("range"));
				Zscale = Range * 91.44;			 // RangeValue is in yards, convert to centimeters (1 yard = 91.44 cm)
				Zscale = (Zscale / 51200) * 100; // Convert to percentage scale

				TSharedPtr<FJsonObject> TotalVerticesObject = JsonObject->GetObjectField(TEXT("total_vertices"));
				VerticesX = TotalVerticesObject->GetNumberField(TEXT("x"));
				VerticesY = TotalVerticesObject->GetNumberField(TEXT("y"));
			}
		}

		TArray<uint16> HeightmapToImport;
		TArray<Tile> Tiles;
		uint8 ColumnOrigin = 255;
		uint8 RowOrigin = 255;

		// Collect heightmap data
		TArray<uint8> HeightmapData;
		FString HeightmapPath = FPaths::Combine(DirectoryPath, HeightmapFile[0]);
		if (FFileHelper::LoadFileToArray(HeightmapData, *HeightmapPath))
		{
			const int32 DataCount = HeightmapData.Num() / sizeof(uint16);
			HeightmapToImport.AddUninitialized(DataCount);
			FMemory::Memcpy(HeightmapToImport.GetData(), HeightmapData.GetData(), HeightmapData.Num());
		}

		// Collect alphamap PNG and JSON data
		for (int i = 0; i < AlphamapPNGs.Num(); i++)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(AlphamapPNGs[i]).ParseIntoArray(NameParts, TEXT("_"), true);

			Tile NewTile;
			NewTile.Column = FCString::Atoi(*NameParts[1]);
			NewTile.Row = FCString::Atoi(*NameParts[2]);

			if (NewTile.Column < ColumnOrigin)
				ColumnOrigin = NewTile.Column;
			if (NewTile.Row < RowOrigin)
				RowOrigin = NewTile.Row;

			//  Collect alphamap PNG data
			TArray<uint8> PngData;
			FString PngPath = FPaths::Combine(DirectoryPath, AlphamapPNGs[i]);
			if (FFileHelper::LoadFileToArray(PngData, *PngPath))
			{
				IImageWrapperModule &ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(PngData.GetData(), PngData.Num()))
				{
					TArray<uint8> RawData;
					if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData) && RawData.Num() > 0)
					{
						NewTile.AlphamapData.AddUninitialized(RawData.Num() / sizeof(FColor));
						FMemory::Memcpy(NewTile.AlphamapData.GetData(), RawData.GetData(), RawData.Num());
					}
				}
			}

			// Collect alphamap JSON data
			FString JsonPath = FPaths::Combine(DirectoryPath, AlphamapJSONs[i]);
			if (FFileHelper::LoadFileToString(JsonString, *JsonPath))
			{
				TSharedPtr<FJsonObject> JsonObject;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

				if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
				{
					TArray<TSharedPtr<FJsonValue>> Layers = JsonObject->GetArrayField(TEXT("layers"));
					for (const TSharedPtr<FJsonValue> &LayerValue : Layers)
					{
						TSharedPtr<FJsonObject> LayerObject = LayerValue->AsObject();
						FString TextureFile = LayerObject->GetStringField(TEXT("file"));
						FName LayerName = ImportTexture_CreateLayerInfo(TextureFile, DirectoryPath);

						uint16 ChunkIndex = LayerObject->GetNumberField(TEXT("chunkIndex"));
						NewTile.Chunks[ChunkIndex].Layers.Add(LayerName);
					}
				}
			}

			Tiles.Add(NewTile);
		}

		// Adjust the landscape size to fit UE5 landscape import parameters(127x2 = 254x254 quads per component)
		uint16 CropWidth = VerticesX - (VerticesX % 254) + 1;
		uint16 CropHeight = VerticesY - (VerticesY % 254) + 1;
		HeightmapToImport = CropLandscape(HeightmapToImport, VerticesX, CropWidth, CropHeight);

		// Fill the layers data array with weights from the alphamap
		for (const Tile &Tile : Tiles)
		{
			uint32 TileOffsetX = (Tile.Column - ColumnOrigin) * 256;
			uint32 TileOffsetY = (Tile.Row - RowOrigin) * 256;

			// Iterate over the alphamap data
			for (uint16 Row = 0; Row < 256; ++Row)
			{
				for (uint16 Col = 0; Col < 256; ++Col)
				{
					uint32 GlobalIndexX = (Col + TileOffsetX);
					uint32 GlobalIndexY = (Row + TileOffsetY) * CropWidth;
					uint32 GlobalIndex = GlobalIndexY + GlobalIndexX;

					if (GlobalIndexX >= CropWidth || GlobalIndexY >= (uint32)(CropWidth * CropHeight))
						continue; // Skip if out of bounds

					uint16 AlphamapIndex = (Row * 256) + Col;
					uint8 ChunkIndex = (Row / 16) * 16 + (Col / 16);

					FColor PixelData = Tile.AlphamapData[AlphamapIndex];

					// Calculate the weight for each layer based on the pixel data
					for (uint16 LayerIndex = 0; LayerIndex < Tile.Chunks[ChunkIndex].Layers.Num(); ++LayerIndex)
					{
						LayerStruct *LayerStruct = LayerStructMap.Find(Tile.Chunks[ChunkIndex].Layers[LayerIndex].ToString());

						if (LayerStruct && LayerStruct->LayerData.Num() == 0)
							LayerStruct->LayerData.Init(0, CropWidth * CropHeight);

						switch (LayerIndex)
						{
						case 0:
							LayerStruct->LayerData[GlobalIndex] = PixelData.A - PixelData.R - PixelData.G - PixelData.B;
							break;

						case 1:
							LayerStruct->LayerData[GlobalIndex] = PixelData.R;
							break;

						case 2:
							LayerStruct->LayerData[GlobalIndex] = PixelData.G;
							break;

						case 3:
							LayerStruct->LayerData[GlobalIndex] = PixelData.B;
							break;
						}
					}
				}
			}
		}

		// Spawn the Landscape actor
		ALandscape *Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>();
		Landscape->SetActorLabel(*FPaths::GetCleanFilename(DirectoryPath));
		Landscape->SetActorScale3D(FVector(190.5f, 190.5f, Zscale)); // There is 190.5 centimeters between each vertex in the heightmap

		// Prepare data for the Import function
		TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
		HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightmapToImport));

		// Prepare material layer data
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
		TArray<FLandscapeImportLayerInfo> FinalLayerInfos;
		for (auto &LayerStruct : LayerStructMap)
		{
			FLandscapeImportLayerInfo ImportLayerInfo;
			ImportLayerInfo.LayerName = LayerStruct.Value.LayerInfo->LayerName;
			ImportLayerInfo.LayerInfo = LayerStruct.Value.LayerInfo;
			ImportLayerInfo.LayerData = MoveTemp(LayerStruct.Value.LayerData);
			FinalLayerInfos.Add(ImportLayerInfo);
		}
		// Add the final layer infos to the material layer data map
		MaterialLayerDataPerLayer.Add(FGuid(), MoveTemp(FinalLayerInfos));

		// Import the heightmap data into the landscape
		Landscape->Import(FGuid::NewGuid(), 0, 0, (CropWidth - 1), (CropHeight - 1), 2, 127, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive);

		// Create and assign the landscape material
		UMaterial *LandscapeMaterial = CreateLandscapeMaterial(DirectoryPath);
		Landscape->LandscapeMaterial = LandscapeMaterial;

		// Finalize the landscape actor
		ULandscapeInfo *LandscapeInfo = Landscape->GetLandscapeInfo();
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
		Landscape->RegisterAllComponents();
	}
}

FName FWoWLandscapeImporterModule::ImportTexture_CreateLayerInfo(const FString &RelativeTexturePath, const FString &BaseDirectoryPath)
{
	FString CleanPath = RelativeTexturePath.Replace(TEXT("..\\..\\"), TEXT("")).Replace(TEXT("\\"), TEXT("/"));

	FString TextureDirectory = FPaths::GetPath(CleanPath);
	FString TextureFileName = FPaths::GetBaseFilename(CleanPath);
	FString AssetDirectory = TextureDirectory.IsEmpty() ? TEXT("/Game/Assets") : FString::Printf(TEXT("/Game/Assets/%s"), *TextureDirectory);
	FString LayerInfoName = FString::Printf(TEXT("LI_%s"), *TextureFileName);
	FString LayerInfoPackagePath = FString::Printf(TEXT("%s/%s"), *AssetDirectory, *LayerInfoName);

	// Return existing layer info if it already exists
	if (UObject *ExistingAsset = UEditorAssetLibrary::LoadAsset(LayerInfoPackagePath))
		return FName(*TextureFileName);

	if (!UEditorAssetLibrary::DoesDirectoryExist(AssetDirectory))
		UEditorAssetLibrary::MakeDirectory(AssetDirectory);

	// Import the texture
	FString FullTexturePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(BaseDirectoryPath, RelativeTexturePath));
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UObject *> ImportedAssets = AssetTools.ImportAssets({FullTexturePath}, AssetDirectory);

	if (ImportedAssets.Num() > 0)
	{
		// Create the layer info object
		UPackage *LayerInfoPackage = CreatePackage(*LayerInfoPackagePath);
		ULandscapeLayerInfoObject *LayerInfo = NewObject<ULandscapeLayerInfoObject>(LayerInfoPackage, *LayerInfoName, RF_Public | RF_Standalone);

		LayerInfo->LayerName = FName(*TextureFileName);
		LayerInfo->PhysMaterial = nullptr;
		LayerInfo->LayerUsageDebugColor = FLinearColor::White;

		// Notify the asset registry
		FAssetRegistryModule &AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().AssetCreated(LayerInfo);
		LayerInfo->MarkPackageDirty();

		LayerStructMap.Add(TextureFileName, {TArray<uint8>(), LayerInfo, Cast<UTexture2D>(ImportedAssets[0])});

		return LayerInfo->LayerName;
	}

	return NAME_None;
}

TArray<uint16> FWoWLandscapeImporterModule::CropLandscape(const TArray<uint16> &Heightmap, const uint16 HeightmapWidth, const uint16 CropWidth, const uint16 CropHeight)
{
	TArray<uint16> OutData;
	OutData.SetNumUninitialized(CropWidth * CropHeight);

	for (uint16 y = 0; y < CropHeight; ++y)
	{
		// Copy the row of data from the heightmap to the cropped array
		FMemory::Memcpy(&OutData[y * CropWidth], &Heightmap[y * HeightmapWidth], CropWidth * sizeof(uint16));
	}

	return OutData;
}

UMaterial *FWoWLandscapeImporterModule::CreateLandscapeMaterial(const FString &BaseDirectoryPath)
{
	const FString MaterialDirectory = TEXT("/Game/Assets_Configuration/Materials");
	const FString BaseName = FPaths::GetCleanFilename(BaseDirectoryPath);
	const FString MaterialName = FString::Printf(TEXT("M_Landscape_%s"), *BaseName);
	const FString MaterialPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialName);

	if (!UEditorAssetLibrary::DoesDirectoryExist(MaterialDirectory))
		UEditorAssetLibrary::MakeDirectory(MaterialDirectory);

	// --- STEP 1: Create the material asset ---

	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew *MaterialFactory = NewObject<UMaterialFactoryNew>();
	AssetTools.CreateAsset(MaterialName, MaterialDirectory, UMaterial::StaticClass(), MaterialFactory);

	// --- STEP 2: Load the asset and perform all modifications ---

	UMaterial *LandscapeMaterial = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPackagePath));

	// Set the material to be used on Landscapes
	LandscapeMaterial->MaterialDomain = EMaterialDomain::MD_Surface;

	// Create material expressions directly and add them to the material
	UMaterialExpressionLandscapeLayerCoords *CoordsNode = NewObject<UMaterialExpressionLandscapeLayerCoords>(LandscapeMaterial);
	CoordsNode->MaterialExpressionEditorX = -550;
	CoordsNode->MaterialExpressionEditorY = 0;
	LandscapeMaterial->GetExpressionCollection().AddExpression(CoordsNode);

	UMaterialExpressionLandscapeLayerBlend *LayerBlendNode = NewObject<UMaterialExpressionLandscapeLayerBlend>(LandscapeMaterial);
	LayerBlendNode->MaterialExpressionEditorX = 0;
	LayerBlendNode->MaterialExpressionEditorY = 150;
	LandscapeMaterial->GetExpressionCollection().AddExpression(LayerBlendNode);

	// Loop through our stored layer data to create and connect texture samplers
	int32 NodeOffsetY = 0;
	for (auto const &[LayerName, LayerStruct] : LayerStructMap)
	{
		if (LayerStruct.LayerTexture && LayerStruct.LayerInfo)
		{
			UMaterialExpressionTextureSample *TextureSampleNode = NewObject<UMaterialExpressionTextureSample>(LandscapeMaterial);
			TextureSampleNode->MaterialExpressionEditorX = -350;
			TextureSampleNode->MaterialExpressionEditorY = NodeOffsetY;
			LandscapeMaterial->GetExpressionCollection().AddExpression(TextureSampleNode);

			if (TextureSampleNode)
			{
				TextureSampleNode->Texture = LayerStruct.LayerTexture.Get();
				TextureSampleNode->Coordinates.Expression = CoordsNode;

				FLayerBlendInput LayerInput;
				LayerInput.LayerName = LayerStruct.LayerInfo->LayerName;
				LayerInput.BlendType = LB_WeightBlend;
				LayerInput.LayerInput.Expression = TextureSampleNode;
				LayerInput.LayerInput.OutputIndex = 0;
				LayerInput.PreviewWeight = (LayerBlendNode->Layers.Num() == 0) ? 1.0f : 0.0f;
				LayerBlendNode->Layers.Add(LayerInput);

				NodeOffsetY += 150;
			}
		}
	}

	// Connect the final blend node to the material's Base Color output
	if (LayerBlendNode && LayerBlendNode->Layers.Num() > 0)
	{
		FExpressionInput *BaseColorInput = LandscapeMaterial->GetExpressionInputForProperty(MP_BaseColor);
		if (BaseColorInput)
		{
			BaseColorInput->Expression = LayerBlendNode;
		}
	}

	// Update and recompile the material
	FPropertyChangedEvent PropertyChangedEvent(nullptr);
	LandscapeMaterial->PostEditChangeProperty(PropertyChangedEvent);

	// Mark the package as needing to be saved.
	LandscapeMaterial->MarkPackageDirty();

	return LandscapeMaterial;
}

void FWoWLandscapeImporterModule::UpdateStatusMessage(const FString &Message, bool bIsError)
{
	if (StatusMessageWidget.IsValid())
	{
		FText StatusText = FText::FromString(Message);
		FSlateColor TextColor = bIsError ? FSlateColor(FLinearColor::Red) : FSlateColor(FLinearColor::Green);

		StatusMessageWidget->SetText(StatusText);
		StatusMessageWidget->SetColorAndOpacity(TextColor);
	}
}

void FWoWLandscapeImporterModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(WoWLandscapeImporterTabName);
}

void FWoWLandscapeImporterModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu *Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection &Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FWoWLandscapeImporterCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu *ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection &Section = ToolbarMenu->FindOrAddSection("Settings");
			{
				FToolMenuEntry &Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FWoWLandscapeImporterCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWoWLandscapeImporterModule, WoWLandscapeImporter)