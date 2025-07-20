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
#include "LandscapeStreamingProxy.h"
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
		double Zscale = 0.0;
		uint8 ColumnOrigin = 0, RowOrigin = 0, TileColumns = 0, TileRows = 0;

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

				TSharedPtr<FJsonObject> TotalVerticesObject = JsonObject->GetObjectField(TEXT("tile_range"));
				ColumnOrigin = TotalVerticesObject->GetNumberField(TEXT("column_min"));
				RowOrigin = TotalVerticesObject->GetNumberField(TEXT("row_min"));
				TileColumns = TotalVerticesObject->GetNumberField(TEXT("column_count"));
				TileRows = TotalVerticesObject->GetNumberField(TEXT("row_count"));
			}
		}

		TileGrid.SetNum(TileRows);
		for (uint8 Row = 0; Row < TileRows; Row++)
			TileGrid[Row].SetNum(TileColumns);

		// First pass: collect filedata, metadata and find bounds
		for (int i = 0; i < HeightmapFiles.Num(); i++)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(HeightmapFiles[i]).ParseIntoArray(NameParts, TEXT("_"), true);
			Tile NewTile;

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

			TileGrid[FCString::Atoi(*NameParts[2]) - RowOrigin][FCString::Atoi(*NameParts[1]) - ColumnOrigin] = NewTile;
		}

		// Use a Scoped Slow Task to provide feedback to the user and prevent the editor from freezing.
		FScopedSlowTask SlowTask(TileRows * TileColumns, LOCTEXT("ImportingWoWLandscape", "Importing WoW Landscape..."));
		SlowTask.MakeDialog();

		// Create the main landscape actor first
		ALandscape *Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>();
		Landscape->SetActorLabel(*FPaths::GetCleanFilename(DirectoryPath));
		Landscape->SetActorScale3D(FVector(190.5f, 190.5f, Zscale)); // There is 190.5 centimeters between each vertex in the heightmap

		// Initialize the landscape with a valid GUID and landscape parameters
		FGuid LandscapeGuid = FGuid::NewGuid();
		Landscape->SetLandscapeGuid(LandscapeGuid);

		// Set landscape configuration parameters to match what we'll import
		Landscape->ComponentSizeQuads = 254;
		Landscape->SubsectionSizeQuads = 127;
		Landscape->NumSubsections = 2;

		uint8 CompPerProxy = 2;
		for (int TileRow = 0; TileRow < TileRows; TileRow += CompPerProxy)
		{
			for (int TileCol = 0; TileCol < TileColumns; TileCol += CompPerProxy)
			{
				// Update the slow task dialog
				SlowTask.EnterProgressFrame(CompPerProxy * CompPerProxy, FText::Format(LOCTEXT("ImportingProxy", "Importing Proxy at (Column {0}), (Row {1})"), TileCol, TileRow));

				TArray<uint16> ProxyHeightmap = CreateProxyHeightmap(TileRow, TileCol, CompPerProxy);

				// Create a LandscapeStreamingProxy for the heightmap data
				ALandscapeStreamingProxy *StreamingProxy = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscapeStreamingProxy>();
				StreamingProxy->SetActorLabel(FString::Printf(TEXT("%d_%d_Proxy"), TileCol, TileRow));
				StreamingProxy->SetActorScale3D(Landscape->GetActorScale3D());

				// Set the landscape guid for the proxy
				StreamingProxy->SetLandscapeGuid(LandscapeGuid);

				// Prepare data for the Import function on the streaming proxy
				TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
				HeightDataPerLayer.Add(FGuid(), MoveTemp(ProxyHeightmap));

				// Prepare material layer data
				TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
				MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

				// Import the heightmap data into the streaming proxy using the same GUID
				uint32 MinX = TileCol * 254;
				uint32 MinY = TileRow * 254;
				uint32 MaxX = MinX + (254 * CompPerProxy);
				uint32 MaxY = MinY + (254 * CompPerProxy);
				StreamingProxy->Import(LandscapeGuid, MinX, MinY, MaxX, MaxY, 2, 127, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive); // For every proxy added, then this function uses more time and the memory usage increases.
			}
		}
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

TArray<uint16> FWoWLandscapeImporterModule::CreateProxyHeightmap(const int TileRow, const int TileCol, const uint8 CompPerProxy)
{
	const uint16 HeightmapWidth = 254 * CompPerProxy + 1;
	const uint16 HeightmapHeight = 254 * CompPerProxy + 1;

	const uint8 ShiftedCols = ((256 * CompPerProxy + 1) - HeightmapWidth) * (TileCol / CompPerProxy);
	const uint8 ShiftedRows = ((256 * CompPerProxy + 1) - HeightmapHeight) * (TileRow / CompPerProxy);

	TArray<uint16> ProxyHeightmap;
	ProxyHeightmap.SetNumZeroed(HeightmapWidth * HeightmapHeight);

	int8 GridOffsetRow = TileRow > 0 ? -1 : 0;
	uint32 TileIndexRow = GridOffsetRow < 0 ? 256 - ShiftedRows : 0;
	for (int ProxyRow = 0; ProxyRow < HeightmapHeight; ProxyRow++)
	{
		if (TileIndexRow % 256 == 0 && TileIndexRow != 0)
		{
			TileIndexRow = 0;
			GridOffsetRow++;
		}
		uint8 TileGridRow = TileRow + GridOffsetRow;

		int8 GridOffsetCol = TileCol > 0 ? -1 : 0;
		uint32 TileIndexCol = GridOffsetCol < 0 ? 256 - ShiftedCols : 0;
		for (int ProxyCol = 0; ProxyCol < HeightmapWidth; ProxyCol++)
		{
			if (TileIndexCol % 256 == 0 && TileIndexCol != 0)
			{
				TileIndexCol = 0;
				GridOffsetCol++;
			}
			uint8 TileGridCol = TileCol + GridOffsetCol;

			uint32 TileIndex = (TileIndexRow * 257) + TileIndexCol;
			uint32 ProxyIndex = (ProxyRow * HeightmapWidth) + ProxyCol;

			if (!TileGrid[TileGridRow].IsValidIndex(TileGridCol) || TileGrid[TileGridRow][TileGridCol].HeightmapData.IsEmpty())
				ProxyHeightmap[ProxyIndex] = 0; // If no heightmap data is found, set to 0
			else
				ProxyHeightmap[ProxyIndex] = TileGrid[TileGridRow][TileGridCol].HeightmapData[TileIndex];

			TileIndexCol++;
		}
		TileIndexRow++;
	}

	return ProxyHeightmap;
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