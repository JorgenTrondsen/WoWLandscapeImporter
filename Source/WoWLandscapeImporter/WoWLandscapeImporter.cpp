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
#include "WoWTileHelper.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Modules/ModuleManager.h"
#include "LandscapeLayerInfoObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Engine.h"
#include "UObject/Package.h"
#include "LandscapeEdit.h"

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
	TArray<FString> HeightmapFiles;
	TArray<FString> AlphamapPNGs;
	TArray<FString> AlphamapJSONs;

	// Find all required files in the directory
	FString SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.r16"));
	IFileManager::Get().FindFiles(HeightmapFiles, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("tex_*.png"));
	IFileManager::Get().FindFiles(AlphamapPNGs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("tex_*.json"));
	IFileManager::Get().FindFiles(AlphamapJSONs, *SearchPattern, true, false);

	// Check if we found all required files
	if (HeightmapFiles.IsEmpty() || AlphamapPNGs.IsEmpty() || AlphamapJSONs.IsEmpty())
	{
		UpdateStatusMessage(TEXT("Missing either: R16 files, Alphamap PNGs, Alphamap JSONs"), true);
	}
	else
	{
		double_t Zscale = 0.0;

		// Read heightmap metadata JSON and extract heightmap range
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
			}
		}

		TArray<Tile> Tiles1D;
		uint8 ColumnOrigin = 255;
		uint8 RowOrigin = 255;
		uint8 ColumnMax = 0;
		uint8 RowMax = 0;

		// First pass: collect filedata, metadata and find bounds
		for (int i = 0; i < HeightmapFiles.Num(); i++)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(HeightmapFiles[i]).ParseIntoArray(NameParts, TEXT("_"), true);

			Tile NewTile;
			NewTile.Column = FCString::Atoi(*NameParts[1]);
			NewTile.Row = FCString::Atoi(*NameParts[2]);

			if (NewTile.Column < ColumnOrigin)
				ColumnOrigin = NewTile.Column;
			if (NewTile.Column > ColumnMax)
				ColumnMax = NewTile.Column;
			if (NewTile.Row < RowOrigin)
				RowOrigin = NewTile.Row;
			if (NewTile.Row > RowMax)
				RowMax = NewTile.Row;

			// Collect heightmap data
			TArray<uint8> HeightmapData;
			FString HeightmapPath = FPaths::Combine(DirectoryPath, HeightmapFiles[i]);
			if (FFileHelper::LoadFileToArray(HeightmapData, *HeightmapPath))
			{
				const int32 DataCount = HeightmapData.Num() / sizeof(uint16);
				NewTile.HeightmapData.AddUninitialized(DataCount);
				FMemory::Memcpy(NewTile.HeightmapData.GetData(), HeightmapData.GetData(), HeightmapData.Num());
			}

			//  Collect alphamap PNG data
			TArray<uint8> PngData;
			FString PngPath = FPaths::Combine(DirectoryPath, AlphamapPNGs[i]);
			// 1. Load the compressed PNG file from disk into a byte array
			if (FFileHelper::LoadFileToArray(PngData, *PngPath))
			{
				// 2. Get the ImageWrapper module
				IImageWrapperModule &ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				// 3. Decompress the PNG data into a raw byte array in BGRA format (which matches FColor)
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
						ULandscapeLayerInfoObject *Layer = ImportTexture_CreateLayerInfo(TextureFile, DirectoryPath);

						uint16 LayerIndex = LayerObject->GetNumberField(TEXT("index"));
						uint16 ChunkIndex = LayerObject->GetNumberField(TEXT("chunkIndex"));

						NewTile.Chunks[ChunkIndex].Layers[LayerIndex] = Layer;
					}
				}
			}

			Tiles1D.Add(NewTile);
		}

		// Create 2D tile array with proper dimensions
		const uint8 ColumnCount = ColumnMax - ColumnOrigin + 1;
		const uint8 RowCount = RowMax - RowOrigin + 1;
		TArray<TArray<Tile *>> Tiles2D;
		Tiles2D.SetNum(ColumnCount);

		// Initialize all pointers to nullptr
		for (int32 i = 0; i < ColumnCount; i++)
		{
			Tiles2D[i].SetNum(RowCount);
			for (int32 j = 0; j < RowCount; j++)
			{
				Tiles2D[i][j] = nullptr;
			}
		}

		// Second pass: Place tiles in the 2D array & and import the tile as landscape
		for (Tile &CurrentTile : Tiles1D)
		{
			const uint8 ArrayColumn = CurrentTile.Column - ColumnOrigin;
			const uint8 ArrayRow = CurrentTile.Row - RowOrigin;

			Tiles2D[ArrayColumn][ArrayRow] = &CurrentTile;

			Tile *TopTile = (ArrayRow > 0) ? Tiles2D[ArrayColumn][ArrayRow - 1] : nullptr;
			Tile *LeftTile = (ArrayColumn > 0) ? Tiles2D[ArrayColumn - 1][ArrayRow] : nullptr;
			Tile *TopLeftTile = (ArrayRow > 0 && ArrayColumn > 0) ? Tiles2D[ArrayColumn - 1][ArrayRow - 1] : nullptr;

			TArray<uint16> TopTileHM, LeftTileHM, TopLeftTileHM;
			TArray<FColor> TopTileAM, LeftTileAM, TopLeftTileAM;
			TArray<Chunk> TopTileChunks, LeftTileChunks, TopLeftTileChunks;

			if (TopTile)
			{
				TopTileHM = WoWTileHelper::GetRows(TopTile->HeightmapData, 257, (ArrayRow * 2), true);
				TopTileAM = WoWTileHelper::GetRows(TopTile->AlphamapData, 1024, (ArrayRow * 8), false);
				TopTileChunks = TopTile->Chunks;
			}

			if (LeftTile)
			{
				LeftTileHM = WoWTileHelper::GetColumns(LeftTile->HeightmapData, 257, (ArrayColumn * 2), true);
				LeftTileAM = WoWTileHelper::GetColumns(LeftTile->AlphamapData, 1024, (ArrayColumn * 8), false);
				LeftTileChunks = LeftTile->Chunks;
			}

			if (TopLeftTile)
			{
				TopLeftTileHM = WoWTileHelper::GetCorner(TopLeftTile->HeightmapData, 257, (ArrayColumn * 2), (ArrayRow * 2), true);
				TopLeftTileAM = WoWTileHelper::GetCorner(TopLeftTile->AlphamapData, 1024, (ArrayColumn * 8), (ArrayRow * 8), false);
				TopLeftTileChunks = TopLeftTile->Chunks;
			}

			TArray<uint16> HeightmapToImport = WoWTileHelper::ExpandTile(CurrentTile.HeightmapData, TopTileHM, LeftTileHM, TopLeftTileHM, 257, (ArrayColumn * 2), (ArrayRow * 2));
			TArray<FColor> AlphamapToImport = WoWTileHelper::ExpandTile(CurrentTile.AlphamapData, TopTileAM, LeftTileAM, TopLeftTileAM, 1024, (ArrayColumn * 8), (ArrayRow * 8));

			HeightmapToImport = WoWTileHelper::CropTile(HeightmapToImport, (257 + (ArrayColumn * 2)), 0, 0, 255, 255);
			AlphamapToImport = WoWTileHelper::CropTile(AlphamapToImport, (1024 + (ArrayColumn * 8)), 0, 0, 1020, 1020);

			// Spawn the Landscape actor
			ALandscape *Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>();
			Landscape->SetActorLabel("Landscape_" + FString::Printf(TEXT("%d_%d"), CurrentTile.Column, CurrentTile.Row));
			Landscape->SetActorScale3D(FVector(190.5f, 190.5f, Zscale));

			float_t tileSize = 254 * 190.5f;
			Landscape->SetActorLocation(FVector(ArrayColumn * tileSize, ArrayRow * tileSize, 170000.f));

			// Prepare data for the Import function
			TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
			HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightmapToImport));

			// Is this what we use to paint the landscape with?
			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
			MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

			// Import the heightmap data into the landscape
			Landscape->Import(FGuid::NewGuid(), 0, 0, 254, 254, 2, 127, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive);

			// Finalize the landscape actor
			ULandscapeInfo *LandscapeInfo = Landscape->GetLandscapeInfo();
			LandscapeInfo->UpdateLayerInfoMap(Landscape);
			Landscape->RegisterAllComponents();

			// // TODO: Paint the imported landscape with alphamap data and layer textures from each chunk.
			// TArray<Chunk> AlphamapChunks = WoWTileHelper::ExtractTileChunks(CurrentTile.Chunks, TopTileChunks, LeftTileChunks, TopLeftTileChunks, (ArrayColumn * 8), (ArrayRow * 8));

			// // Determine the grid of the current AlphamapChunks array
			// const uint8 IncludeChunkX = (ArrayColumn % 8 == 0) ? 0 : 1;
			// const uint8 ChunkGridX = 16 + IncludeChunkX;
			// const uint8 IncludeChunkY = (ArrayRow % 8 == 0) ? 0 : 1;
			// const uint8 ChunkGridY = 16 + IncludeChunkY;

			// for (uint16 Row = 0; Row < ChunkGridY; Row++)
			// {
			// 	for (uint16 Col = 0; Col < ChunkGridX; Col++)
			// 	{
			// 		TArray<FColor> AlphamapChunkData;
			// 		uint16 CropStartX = 0, CropStartY = 0, CropEndX = 0, CropEndY = 0;

			// 		// Extract the part of the alphamap that correspond to the current chunk in the AlphamapChunks array
			// 		if ((Row == 0 && Col == 0) && (ChunkGridX == 17 && ChunkGridY == 17))
			// 		{
			// 			CropEndX = FMath::Min((ArrayColumn * 8), 1016);
			// 			CropEndY = FMath::Min((ArrayRow * 8), 1016);

			// 			AlphamapChunkData = WoWTileHelper::CropTile(AlphamapToImport, 1016, 0, 0, CropEndX, CropEndY);
			// 		}
			// 		else if (Row == 0 && ChunkGridY == 17)
			// 		{
			// 			CropStartX = (ArrayColumn * 8) + ((Col - IncludeChunkX) * 64);
			// 			CropEndX = FMath::Min((CropStartX + 64), 1016);
			// 			CropEndY = FMath::Min((ArrayRow * 8), 1016);

			// 			AlphamapChunkData = WoWTileHelper::CropTile(AlphamapToImport, 1016, CropStartX, 0, CropEndX, CropEndY);
			// 		}
			// 		else if (Col == 0 && ChunkGridX == 17)
			// 		{
			// 			CropStartY = (ArrayRow * 8) + ((Row - IncludeChunkY) * 64);
			// 			CropEndX = FMath::Min((ArrayColumn * 8), 1016);
			// 			CropEndY = FMath::Min((CropStartY + 64), 1016);

			// 			AlphamapChunkData = WoWTileHelper::CropTile(AlphamapToImport, 1016, 0, CropStartY, CropEndX, CropEndY);
			// 		}
			// 		else
			// 		{
			// 			CropStartX = (ArrayColumn * 8) + ((Col - IncludeChunkX) * 64);
			// 			CropStartY = (ArrayRow * 8) + ((Row - IncludeChunkY) * 64);
			// 			CropEndX = FMath::Min((CropStartX + 64), 1016);
			// 			CropEndY = FMath::Min((CropStartY + 64), 1016);

			// 			AlphamapChunkData = WoWTileHelper::CropTile(AlphamapToImport, 1016, CropStartX, CropStartY, CropEndX, CropEndY);
			// 		}

			// 		// Extract the layers from the current chunk in the AlphamapChunks array
			// 		TArray<UTexture2D *> LayerTextures = AlphamapChunks[Row * ChunkGridX + Col].Layers;

			// 		// Paint the landscape with the extracted alphamap chunk data and layer textures
			// 	}
			// }
		}
		FString Message = FString::Printf(TEXT("Imported %d heightmap file(s)"), HeightmapFiles.Num());
		UpdateStatusMessage(Message, false);
	}
}

ULandscapeLayerInfoObject *FWoWLandscapeImporterModule::ImportTexture_CreateLayerInfo(const FString &RelativeTexturePath, const FString &BaseDirectoryPath)
{
	FString CleanPath = RelativeTexturePath.Replace(TEXT("..\\..\\"), TEXT("")).Replace(TEXT("\\"), TEXT("/"));

	FString TextureDirectory = FPaths::GetPath(CleanPath);
	FString TextureFileName = FPaths::GetBaseFilename(CleanPath);
	FString PackagePath = TextureDirectory.IsEmpty() ? FString::Printf(TEXT("/Game/Assets/%s"), *TextureFileName) : FString::Printf(TEXT("/Game/Assets/%s/%s"), *TextureDirectory, *TextureFileName);

	// Return existing layer info if it already exists
	if (UObject *ExistingAsset = UEditorAssetLibrary::LoadAsset(PackagePath))
		return Cast<ULandscapeLayerInfoObject>(ExistingAsset);

	FString FullTexturePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(BaseDirectoryPath, RelativeTexturePath));

	FString AssetDirectory = TextureDirectory.IsEmpty() ? TEXT("/Game/Assets") : FString::Printf(TEXT("/Game/Assets/%s"), *TextureDirectory);
	if (!UEditorAssetLibrary::DoesDirectoryExist(AssetDirectory))
		UEditorAssetLibrary::MakeDirectory(AssetDirectory);

	// Import the texture
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UObject *> ImportedAssets = AssetTools.ImportAssets({FullTexturePath}, AssetDirectory);

	if (ImportedAssets.Num() > 0)
	{
		// Create the layer info object
		FString LayerInfoName = FString::Printf(TEXT("LI_%s"), *TextureFileName);
		FString LayerInfoPackagePath = FString::Printf(TEXT("%s/%s"), *AssetDirectory, *LayerInfoName);

		UPackage *LayerInfoPackage = CreatePackage(*LayerInfoPackagePath);
		ULandscapeLayerInfoObject *LayerInfo = NewObject<ULandscapeLayerInfoObject>(LayerInfoPackage, *LayerInfoName, RF_Public | RF_Standalone);

		// TODO: Where do we set the texture for the layer info?
		LayerInfo->LayerName = FName(*TextureFileName);
		LayerInfo->PhysMaterial = nullptr; // Set default physical material if needed
		LayerInfo->LayerUsageDebugColor = FLinearColor::White;

		// Notify the asset registry
		FAssetRegistryModule &AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().AssetCreated(LayerInfo);
		LayerInfo->MarkPackageDirty();

		return LayerInfo;
	}

	return nullptr;
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