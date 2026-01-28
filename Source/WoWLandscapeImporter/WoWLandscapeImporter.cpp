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
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "ImageUtils.h"
#include "Engine/StaticMeshActor.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "LandscapeLayerInfoObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionViewProperty.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "MaterialEditorUtilities.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialEditingLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "AssetImportTask.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxTextureImportData.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "PhysicsEngine/BodySetup.h"

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
			DirectoryPath);

		if (bFolderSelected && !DirectoryPath.IsEmpty())
		{
			ImportLandscape();
		}
		else if (!bFolderSelected)
		{
			UpdateStatusMessage(TEXT("Directory selection cancelled"), false);
		}
	}

	return FReply::Handled();
}

void FWoWLandscapeImporterModule::ImportLandscape()
{
	TArray<FString> HeightmapFiles;
	TArray<FString> AlphamapPNGs;
	TArray<FString> AlphamapJSONs;
	TArray<FString> CSVFiles;

	// Find all required files in the directory
	FString SearchPattern = FPaths::Combine(DirectoryPath, TEXT("heightmaps/*.png"));
	IFileManager::Get().FindFiles(HeightmapFiles, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("alphamaps/*.png"));
	IFileManager::Get().FindFiles(AlphamapPNGs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("alphamaps/*.json"));
	IFileManager::Get().FindFiles(AlphamapJSONs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.csv"));
	IFileManager::Get().FindFiles(CSVFiles, *SearchPattern, true, false);

	// Check if we found all required files
	if (HeightmapFiles.IsEmpty() || AlphamapPNGs.IsEmpty() || AlphamapJSONs.IsEmpty() || CSVFiles.IsEmpty())
	{
		UpdateStatusMessage(TEXT("Missing either: Heightmap files, Alphamap PNGs, Alphamap JSONs, or CSV files"), true);
	}
	else
	{
		double Zscale = 0.0, SeaLevelOffset = 0.0;
		int TileColumns = 0, TileRows = 0;

		// Read heightmap metadata JSON and extract heightmap range
		FString MetadataPath = FPaths::Combine(DirectoryPath, TEXT("heightmaps/heightmap_metadata.json"));
		FString JsonString;
		if (FFileHelper::LoadFileToString(JsonString, *MetadataPath))
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				TSharedPtr<FJsonObject> HeightDataObject = JsonObject->GetObjectField(TEXT("height_data"));
				double Range = HeightDataObject->GetNumberField(TEXT("range"));
				Zscale = Range * 91.44;			 // RangeValue is in yards, convert to centimeters (1 yard = 91.44 cm)
				Zscale = (Zscale / 51200) * 100; // Convert to percentage scale(100% = 51200 cm)

				double NormalizedSealevel = HeightDataObject->GetNumberField(TEXT("normalized_sealevel"));
				double CalculatedSeaLevel = (NormalizedSealevel - 0.5) * 51200;
				SeaLevelOffset = CalculatedSeaLevel * (Zscale / 100);

				TSharedPtr<FJsonObject> TileDataObject = JsonObject->GetObjectField(TEXT("tile_data"));
				TileColumns = TileDataObject->GetNumberField(TEXT("columns"));
				TileRows = TileDataObject->GetNumberField(TEXT("rows"));
			}
		}

		TileGrid.SetNum(TileRows);
		for (int Row = 0; Row < TileRows; Row++)
			TileGrid[Row].SetNum(TileColumns);

		TArray<FString> TexturePaths;
		// First pass: collect filedata and metadata
		for (int i = 0; i < HeightmapFiles.Num(); i++)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(HeightmapFiles[i]).ParseIntoArray(NameParts, TEXT("_"), true);
			Tile NewTile;

			NewTile.Column = FCString::Atoi(*NameParts[1]);
			NewTile.Row = FCString::Atoi(*NameParts[2]);

			// Collect heightmap data
			TArray<uint8> FileData;
			FString HeightmapPath = FPaths::Combine(DirectoryPath, TEXT("heightmaps/"), HeightmapFiles[i]);
			if (FFileHelper::LoadFileToArray(FileData, *HeightmapPath))
			{
				IImageWrapperModule &ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
				{
					TArray<uint8> RawData;
					if (ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData))
					{
						NewTile.HeightmapData.AddUninitialized(RawData.Num() / sizeof(uint16));
						FMemory::Memcpy(NewTile.HeightmapData.GetData(), RawData.GetData(), RawData.Num());
					}
				}
			}

			// Collect alphamap PNG data
			FString AlphamapPath = FPaths::Combine(DirectoryPath, TEXT("alphamaps/"), AlphamapPNGs[i]);
			if (FFileHelper::LoadFileToArray(FileData, *AlphamapPath))
			{
				IImageWrapperModule &ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
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
			FString JsonPath = FPaths::Combine(DirectoryPath, TEXT("alphamaps/"), AlphamapJSONs[i]);
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
						FString TexturePath = LayerObject->GetStringField(TEXT("file")).Replace(TEXT("\\"), TEXT("/"));
						TexturePaths.Add(TexturePath);

						int ChunkIndex = LayerObject->GetNumberField(TEXT("chunkIndex"));
						NewTile.Chunks[ChunkIndex].Layers.Add(FName(FPaths::GetBaseFilename(TexturePath)));
					}
				}
			}
			TileGrid[NewTile.Row][NewTile.Column] = NewTile;
		}

		// Import the collected texture layers and create layer info
		ImportLayers(TexturePaths);

		// Create the main landscape actor first
		ALandscape *Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>();
		Landscape->SetActorLabel(*FPaths::GetCleanFilename(DirectoryPath));
		Landscape->SetActorScale3D(FVector(192, 192, Zscale)); // X/Y scale is 192 = 48,768 cm ÷ 254 quads. Standard WoW ADT (map tile) is 533.333 yards (48,768 cm) wide.

		// Initialize the landscape with a valid GUID and landscape parameters
		FGuid LandscapeGuid = FGuid::NewGuid();
		Landscape->SetLandscapeGuid(LandscapeGuid);

		// Set landscape configuration parameters to match what we'll import
		Landscape->ComponentSizeQuads = 254;
		Landscape->SubsectionSizeQuads = 127;
		Landscape->NumSubsections = 2;

		ULandscapeInfo *LandscapeInfo = Landscape->CreateLandscapeInfo();
		{
			int CompPerProxy = 2;
			FScopedSlowTask SlowTask(TileRows * TileColumns, LOCTEXT("ImportingWoWLandscape", "Importing WoW Landscape..."));
			SlowTask.MakeDialog();

			for (int Row = 0; Row < TileRows; Row += CompPerProxy)
			{
				for (int Column = 0; Column < TileColumns; Column += CompPerProxy)
				{
					SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingProxy", "Importing Proxy at (Row {1}), (Column {0})"), Column, Row));

					int RowOffset = FMath::Min(Row + (CompPerProxy - 1), TileRows - 1) - Row;
					int ColumnOffset = FMath::Min(Column + (CompPerProxy - 1), TileColumns - 1) - Column;
					if (TileGrid[Row][Column].HeightmapData.Num() == 0)
					{
						if (TileGrid[Row + RowOffset][Column + ColumnOffset].HeightmapData.Num() == 0 || CompPerProxy == 1)
							continue;
					}

					TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> ProxyData = CreateProxyData(Row, Column, RowOffset, ColumnOffset);

					// Create a LandscapeStreamingProxy actor for the current tiles
					ALandscapeStreamingProxy *StreamingProxy = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscapeStreamingProxy>();
					StreamingProxy->SetActorLabel(FString::Printf(TEXT("%d_%d_Proxy"), Column, Row));
					StreamingProxy->SetActorScale3D(Landscape->GetActorScale3D());
					StreamingProxy->SetActorLocation(Landscape->GetActorLocation());

					// Prepare data for the Import function on the streaming proxy
					TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
					HeightDataPerLayer.Add(FGuid(), MoveTemp(ProxyData.Get<0>()));
					TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
					MaterialLayerDataPerLayer.Add(FGuid(), MoveTemp(ProxyData.Get<1>()));

					uint32 MinX = Column * 254;
					uint32 MinY = Row * 254;
					uint32 MaxX = MinX + (254 * (ColumnOffset + 1));
					uint32 MaxY = MinY + (254 * (RowOffset + 1));
					StreamingProxy->Import(FGuid::NewGuid(), MinX, MinY, MaxX, MaxY, 2, 127, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive);

					StreamingProxy->SetLandscapeGuid(LandscapeGuid);
					LandscapeInfo->RegisterActor(StreamingProxy);
				}
			}
		}

		// Create landscape and model material
		CreateLandscapeMaterial(Landscape);
		UMaterial *ModelMaterial = CreateModelMaterial();

		TArray<ActorData> ActorsArray;
		// First pass: parse CSV files and collect actor data
		for (const FString &CSVFile : CSVFiles)
		{
			FString CSVPath = FPaths::Combine(DirectoryPath, CSVFile);
			FString CSVContent;
			if (FFileHelper::LoadFileToString(CSVContent, *CSVPath))
			{
				TArray<FString> CSVLines;
				CSVContent.ParseIntoArrayLines(CSVLines);
				CSVLines.RemoveAt(0);

				for (const FString &Line : CSVLines)
				{
					TArray<FString> CSVFields;
					Line.ParseIntoArray(CSVFields, TEXT(";"), false);

					ActorData Actor;
					Actor.ModelPath = FPaths::ConvertRelativePathToFull(DirectoryPath, CSVFields[0]);

					if (CSVFields[10] == TEXT("gobj"))
					{
						// Game object has data relative to the center of the map, so we need to offset by 17066.66
						Actor.Position = FVector(
							(17066.66f - FCString::Atod(*CSVFields[2])) * 91.44f,
							(17066.66f - FCString::Atod(*CSVFields[1])) * 91.44f,
							FCString::Atod(*CSVFields[3]) * 91.44f + SeaLevelOffset);
						// Create quaternion from game object rotation data
						FQuat GOBJQuat(
							-FCString::Atod(*CSVFields[7]), // X
							FCString::Atod(*CSVFields[6]),	// Y
							-FCString::Atod(*CSVFields[5]), // Z
							FCString::Atod(*CSVFields[4])	// W
						);
						Actor.Rotation = GOBJQuat.Rotator();
						Actor.Rotation.Yaw = Actor.Rotation.Yaw - 90;
						Actor.Rotation.Roll = Actor.Rotation.Roll - 180;
					}
					else
					{
						// Extract position data and convert to centimeters(from yards)
						Actor.Position = FVector(
							FCString::Atod(*CSVFields[1]) * 91.44f,
							FCString::Atod(*CSVFields[3]) * 91.44f,
							FCString::Atod(*CSVFields[2]) * 91.44f + SeaLevelOffset);
						// Extract rotation data and convert to Unreal's coordinate system
						Actor.Rotation = FRotator(
							-FCString::Atod(*CSVFields[4]),
							90 - FCString::Atod(*CSVFields[5]),
							FCString::Atod(*CSVFields[6]));
					}

					Actor.Scale = FCString::Atod(*CSVFields[8]);

					if (IFileManager::Get().FileSize(*Actor.ModelPath) < 1000 || (ActorsArray.Contains(Actor)))
						continue; // Skip empty or duplicate obj files

					if (Actor.ModelPath.Contains(TEXT("/wmo/")))
					{
						FString WMOCSV = FPaths::GetBaseFilename(Actor.ModelPath) + TEXT("_ModelPlacementInformation.csv");
						FString WMOCSVPath = FPaths::GetPath(Actor.ModelPath);
						FString WMOCSVContent;
						if (FFileHelper::LoadFileToString(WMOCSVContent, *FPaths::Combine(WMOCSVPath, WMOCSV)))
						{
							TArray<FString> WMOCSVLines;
							WMOCSVContent.ParseIntoArrayLines(WMOCSVLines);
							WMOCSVLines.RemoveAt(0);

							for (const FString &WMOCSVLine : WMOCSVLines)
							{
								TArray<FString> WMOCSVFields;
								WMOCSVLine.ParseIntoArray(WMOCSVFields, TEXT(";"), false);

								ActorData WMOActor;
								WMOActor.ModelPath = FPaths::ConvertRelativePathToFull(WMOCSVPath, WMOCSVFields[0]);

								if (IFileManager::Get().FileSize(*WMOActor.ModelPath) < 1000)
									continue; // Skip empty or invalid obj files

								WMOActor.Position = FVector(
									FCString::Atod(*WMOCSVFields[1]) * 91.44f,
									-FCString::Atod(*WMOCSVFields[2]) * 91.44f,
									FCString::Atod(*WMOCSVFields[3]) * 91.44f);
								WMOActor.Position = Actor.Rotation.RotateVector(WMOActor.Position);
								WMOActor.Position += Actor.Position;

								// Create quaternion from WMO actors rotation data
								FQuat WMOQuat(
									-FCString::Atod(*WMOCSVFields[5]), // X
									FCString::Atod(*WMOCSVFields[6]),  // Y
									-FCString::Atod(*WMOCSVFields[7]), // Z
									FCString::Atod(*WMOCSVFields[4])   // W
								);

								// Combine WMO rotation with WMO actor's rotation and make euler angles
								WMOQuat = Actor.Rotation.Quaternion() * WMOQuat;
								WMOActor.Rotation = WMOQuat.Rotator();

								WMOActor.Scale = FCString::Atod(*WMOCSVFields[8]);
								ActorsArray.Add(WMOActor);
							}
						}
					}
					ActorsArray.Add(Actor);
				}
			}
		}

		// Sort by filename
		ActorsArray.Sort([](const ActorData &A, const ActorData &B)
						 { return A.ModelPath < B.ModelPath; });

		// Extract model paths from ActorsArray
		TArray<FString> ModelPaths;
		for (const ActorData &Actor : ActorsArray)
			ModelPaths.Add(Actor.ModelPath);

		TArray<UStaticMesh *> ImportedModels = ImportModels(ModelPaths, ModelMaterial);

		int Model = 0;
		// Second pass: spawn static mesh actors for each model and set their properties
		for (int Actor = 0; Actor < ActorsArray.Num(); Actor++)
		{
			if (Actor != 0 && ActorsArray[Actor].ModelPath != ActorsArray[Actor - 1].ModelPath)
				Model++;

			// Spawn static mesh actor
			AStaticMeshActor *ModelActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AStaticMeshActor>();
			ModelActor->SetActorLabel(FPaths::GetBaseFilename(ActorsArray[Actor].ModelPath));

			ModelActor->GetStaticMeshComponent()->SetStaticMesh(ImportedModels[Model]);

			// We need to calculate the correct positions, as they are stored as yards in csv.
			ModelActor->SetActorLocation(ActorsArray[Actor].Position);
			ModelActor->SetActorRotation(ActorsArray[Actor].Rotation);
			ModelActor->SetActorScale3D(FVector(ActorsArray[Actor].Scale * 91.44f));
		}
	}
}

void FWoWLandscapeImporterModule::ImportLayers(TArray<FString> &TexturePaths)
{
	TSet<FString> UniqueTexturePaths(TexturePaths);
	TexturePaths = UniqueTexturePaths.Array();
	UInterchangeManager &InterchangeManager = UInterchangeManager::GetInterchangeManager();

	// Setup import parameters
	FImportAssetParameters ImportParams;
	ImportParams.bIsAutomated = true;
	ImportParams.bReplaceExisting = true;

	TArray<UE::Interchange::FAssetImportResultRef> ImportResults;
	for (const FString &TexturePath : TexturePaths)
	{
		const FString DestinationDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/%s"), *FPaths::GetPath(TexturePath).Replace(TEXT("../"), TEXT("")));

		// Create source data for this file and start async import
		UInterchangeSourceData *SourceData = UInterchangeManager::CreateSourceData(FPaths::ConvertRelativePathToFull(DirectoryPath, TexturePath.RightChop(3)));
		UE::Interchange::FAssetImportResultRef ImportResult = InterchangeManager.ImportAssetAsync(DestinationDirectory, SourceData, ImportParams);
		ImportResults.Add(ImportResult);
	}

	{
		FScopedSlowTask SlowTask(ImportResults.Num(), LOCTEXT("ImportingWoWLayers", "Importing WoW Layers..."));
		SlowTask.MakeDialog();

		for (int i = 0; i < ImportResults.Num(); i++)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingLayer", "Importing Layer: {0}"), i));
			const UE::Interchange::FAssetImportResultRef &ImportResult = ImportResults[i];

			// Wait for the import to complete
			ImportResult->WaitUntilDone();

			UObject *ImportedObject = ImportResult->GetImportedObjects()[0];

			const FString DestinationDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/%s"), *FPaths::GetPath(TexturePaths[i]).Replace(TEXT("../"), TEXT("")));
			FString TextureFileName = FPaths::GetBaseFilename(TexturePaths[i]);
			FString LayerInfoName = FString::Printf(TEXT("LI_%s"), *TextureFileName);

			// Create the layer info object
			UPackage *LayerInfoPackage = CreatePackage(*(DestinationDirectory + TEXT("/") + LayerInfoName));
			ULandscapeLayerInfoObject *LayerInfo = NewObject<ULandscapeLayerInfoObject>(LayerInfoPackage, *LayerInfoName, RF_Public | RF_Standalone);
			LayerInfo->LayerName = FName(*TextureFileName);
			LayerInfo->PhysMaterial = nullptr;
			LayerInfo->LayerUsageDebugColor = FLinearColor::White;
			LayerInfo->MarkPackageDirty();

			// Add to layer metadata map
			LayerMetadataMap.Add(LayerInfo->LayerName, LayerMetadata{LayerInfo, static_cast<UTexture2D *>(ImportedObject)});
		}
	}
}

TArray<UStaticMesh *> FWoWLandscapeImporterModule::ImportModels(TArray<FString> &ModelPaths, UMaterial *ModelMaterial)
{
	// Remove duplicates from the asset paths
	TSet<FString> UniqueAssetPaths(ModelPaths);
	ModelPaths = UniqueAssetPaths.Array();

	double StartTime = FPlatformTime::Seconds();

	// Get the Interchange Manager
	UInterchangeManager &InterchangeManager = UInterchangeManager::GetInterchangeManager();

	// Create a custom pipeline for OBJ imports
	UInterchangeGenericAssetsPipeline *Pipeline = NewObject<UInterchangeGenericAssetsPipeline>();
	Pipeline->bUseSourceNameForAsset = true;
	Pipeline->ReimportStrategy = EReimportStrategyFlags::ApplyNoProperties;
	Pipeline->ImportOffsetRotation = FRotator(0, 0, 90); // 90 degree rotation around Z-axis

	// Configure mesh pipeline for optimal static mesh import
	Pipeline->MeshPipeline->bImportStaticMeshes = true;
	Pipeline->MeshPipeline->bImportSkeletalMeshes = false;
	Pipeline->MeshPipeline->bCombineStaticMeshes = true;
	Pipeline->MeshPipeline->bImportCollision = false;
	Pipeline->MeshPipeline->bBuildReversedIndexBuffer = false;
	Pipeline->MeshPipeline->bBuildNanite = false;

	// Configure material pipeline
	Pipeline->MaterialPipeline->bImportMaterials = true;
	Pipeline->MaterialPipeline->SearchLocation = EInterchangeMaterialSearchLocation::DoNotSearch;
	Pipeline->MaterialPipeline->MaterialImport = EInterchangeMaterialImportOption::ImportAsMaterialInstances;
	Pipeline->MaterialPipeline->ParentMaterial = ModelMaterial;

	// Setup import parameters
	FImportAssetParameters ImportParams;
	ImportParams.bIsAutomated = true;
	ImportParams.bReplaceExisting = true;
	ImportParams.OverridePipelines.Add(FSoftObjectPath(Pipeline));

	TArray<UE::Interchange::FAssetImportResultRef> ImportResults;
	for (const FString &ModelPath : ModelPaths)
	{
		// Create source data for this file
		UInterchangeSourceData *SourceData = UInterchangeManager::CreateSourceData(ModelPath);

		// Start async import
		UE::Interchange::FAssetImportResultRef ImportResult = InterchangeManager.ImportAssetAsync(TEXT("/Game/Assets/WoWExport/Meshes/"), SourceData, ImportParams);
		ImportResults.Add(ImportResult);
	}

	TArray<UStaticMesh *> ImportedModels;
	TArray<UTexture2D *> ImportedTextures;
	TArray<UMaterialInstance *> ImportedMaterials;
	{
		FScopedSlowTask SlowTask(ImportResults.Num(), LOCTEXT("ImportingWoWModels", "Importing WoW Models..."));
		SlowTask.MakeDialog();

		for (int32 i = 0; i < ImportResults.Num(); i++)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingModel", "Importing Model: {0}"), i));
			const UE::Interchange::FAssetImportResultRef &ImportResult = ImportResults[i];

			// Wait for this import to complete
			ImportResult->WaitUntilDone();

			// Get the imported objects
			const TArray<UObject *> &ImportedObjects = ImportResult->GetImportedObjects();

			for (UObject *ImportedObject : ImportedObjects)
			{
				if (ImportedObject->GetName().Contains(TEXT("_lod")))
					continue;

				if (UStaticMesh *StaticMesh = Cast<UStaticMesh>(ImportedObject))
				{
					StaticMesh->SetLODGroup(FName("LevelArchitecture"), false);
					StaticMesh->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
					StaticMesh->LODForCollision = 2;

					// Mark the asset as modified and save changes
					StaticMesh->MarkPackageDirty();
					StaticMesh->PostEditChange();

					ImportedModels.Add(StaticMesh);
				}

				if (UTexture2D *Texture = Cast<UTexture2D>(ImportedObject))
					ImportedTextures.Add(Texture);

				if (UMaterialInstance *Material = Cast<UMaterialInstance>(ImportedObject))
					ImportedMaterials.Add(Material);
			}
		}
	}

	// Remove duplicates from ImportedTextures and ImportedMaterials
	TSet<UTexture2D *> UniqueTextures(ImportedTextures);
	ImportedTextures = UniqueTextures.Array();
	TSet<UMaterialInstance *> UniqueMaterials(ImportedMaterials);
	ImportedMaterials = UniqueMaterials.Array();

	for (UMaterialInstance *Material : ImportedMaterials)
	{
		// find the texture that correspond to this material by name
		FString TextureName = FString("TEX") + Material->GetName().Mid(3);

		UTexture2D **Texture = ImportedTextures.FindByPredicate([&TextureName](const UTexture2D *Tex)
																{ return Tex->GetName() == TextureName; });

		// Cast to material instance constant since the imported material should be an instance of ModelMaterial
		UMaterialInstanceConstant *MaterialInstance = Cast<UMaterialInstanceConstant>(Material);
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MaterialInstance, FName("ModelTexture"), *Texture);

		// Mark the material instance as dirty and update it
		MaterialInstance->MarkPackageDirty();
		MaterialInstance->PostEditChange();
	}

	double EndTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("Interchange model import took %.2f seconds"), EndTime - StartTime);
	return ImportedModels;
}

TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> FWoWLandscapeImporterModule::CreateProxyData(const int StartRow, const int StartColumn, const int RowOffset, const int ColumnOffset)
{
	// Height and width of proxy in vertices(pixels)
	const int ProxyHeight = 254 * (RowOffset + 1) + 1;
	const int ProxyWidth = 254 * (ColumnOffset + 1) + 1;
	TArray<uint16> Heightmap;
	Heightmap.SetNumZeroed(ProxyWidth * ProxyHeight);
	TMap<FName, FLandscapeImportLayerInfo> LayerInfoMap;

	int CurrentRow = StartRow;
	int TileY = 0;
	for (int ProxyY = 0; ProxyY < ProxyHeight; ProxyY++)
	{
		if (TileY == 255)
		{
			CurrentRow++;
			TileY = 1;													// We skip first row/column of subsequent tiles to avoid overlapping vertices(heightmaps share borders)
		}
		int ChunkY = TileY >= 127 ? (TileY + 1) / 16 : TileY / 16; 		// Downsampled alphamap has overlapping chunks in the middle, so we need to adjust the tile index accordingly

		int CurrentColumn = StartColumn;
		int TileX = 0;
		for (int ProxyX = 0; ProxyX < ProxyWidth; ProxyX++)
		{
			if (TileX == 255)
			{
				CurrentColumn++;
				TileX = 1;
			}
			int ChunkX = TileX >= 127 ? (TileX + 1) / 16 : TileX / 16;

			Tile &CurrentTile = TileGrid[CurrentRow][CurrentColumn];
			
			int ProxyIndex = ProxyY * ProxyWidth + ProxyX;
			int TileIndex = TileY * 255 + TileX;

			if (CurrentTile.HeightmapData.Num() == 0)
			{	// No heightmap data for this tile, set height to 0 and continue
				Heightmap[ProxyIndex] = 0;
				continue;
			}
			
			Heightmap[ProxyIndex] = CurrentTile.HeightmapData[TileIndex];
			FColor PixelValue = TileGrid[CurrentRow][CurrentColumn].AlphamapData[TileIndex];
			
			int ChunkIndex = ChunkY * 16 + ChunkX;
			// Calculate the weight for each layer based on the pixel data
			for (int LayerIndex = 0; LayerIndex < CurrentTile.Chunks[ChunkIndex].Layers.Num(); LayerIndex++)
			{
				LayerMetadata *LayerMetadata = LayerMetadataMap.Find(CurrentTile.Chunks[ChunkIndex].Layers[LayerIndex]);
				FLandscapeImportLayerInfo &ImportLayerInfo = LayerInfoMap.FindOrAdd(CurrentTile.Chunks[ChunkIndex].Layers[LayerIndex]);
				if (ImportLayerInfo.LayerData.Num() == 0)
				{
					ImportLayerInfo.LayerData.SetNumZeroed(ProxyWidth * ProxyHeight);
					ImportLayerInfo.LayerInfo = LayerMetadata->LayerInfo;
					ImportLayerInfo.LayerName = LayerMetadata->LayerInfo->LayerName;
				}

				switch (LayerIndex)
				{
				case 0:
					ImportLayerInfo.LayerData[ProxyIndex] = PixelValue.A - PixelValue.R - PixelValue.G - PixelValue.B;
					break;

				case 1:
					ImportLayerInfo.LayerData[ProxyIndex] = PixelValue.R;
					break;

				case 2:
					ImportLayerInfo.LayerData[ProxyIndex] = PixelValue.G;
					break;

				case 3:
					ImportLayerInfo.LayerData[ProxyIndex] = PixelValue.B;
					break;
				}
			}
			TileX++;
		}
		TileY++;
	}

	TArray<FLandscapeImportLayerInfo> LayerInfoArray;
	LayerInfoMap.GenerateValueArray(LayerInfoArray);

	return MakeTuple(MoveTemp(Heightmap), MoveTemp(LayerInfoArray));
}

UMaterial *FWoWLandscapeImporterModule::CreateModelMaterial()
{
	const FString MaterialDirectory = TEXT("/Game/Assets/WoWExport/Materials/");
	const FString MaterialName = TEXT("M_Model");

	// Create the material asset
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.CreateAsset(MaterialName, MaterialDirectory, UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>());

	// Load the asset and perform all modifications
	const FString MaterialPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialName);
	UMaterial *ModelMaterial = static_cast<UMaterial *>(UEditorAssetLibrary::LoadAsset(MaterialPackagePath));
	ModelMaterial->BlendMode = BLEND_Masked;
	ModelMaterial->OpacityMaskClipValue = 0.5f;

	// Create texture sample parameter node and connect to base color
	UMaterialExpressionTextureSampleParameter2D *TextureSample = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -800, 0, ModelMaterial);
	TextureSample->ParameterName = FName("ModelTexture");
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_BaseColor)->Expression = TextureSample;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_OpacityMask)->Expression = TextureSample;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_OpacityMask)->OutputIndex = 4;

	// Create metallic switch parameter and connect to metallic attribute
	UMaterialExpressionConstant *ZeroConstant = CreateNode(NewObject<UMaterialExpressionConstant>(ModelMaterial), -800, 250, ModelMaterial);
	ZeroConstant->R = 0.0f;
	UMaterialExpressionStaticSwitchParameter *MetallicSwitch = CreateNode(NewObject<UMaterialExpressionStaticSwitchParameter>(ModelMaterial), -400, 50, ModelMaterial);
	MetallicSwitch->ParameterName = FName("MetallicSwitch");
	MetallicSwitch->DefaultValue = false;
	UMaterialExpressionOneMinus *OneMinus = CreateNode(NewObject<UMaterialExpressionOneMinus>(ModelMaterial), -480, 65, ModelMaterial);
	OneMinus->Input.Expression = TextureSample;
	OneMinus->Input.OutputIndex = 4;
	MetallicSwitch->A.Expression = OneMinus;
	MetallicSwitch->A.OutputIndex = 4;
	MetallicSwitch->B.Expression = ZeroConstant;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Metallic)->Expression = MetallicSwitch;

	// Create scalar parameters and connect to corresponding attributes
	UMaterialExpressionScalarParameter *SpecularParameter = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -800, 350, ModelMaterial);
	SpecularParameter->ParameterName = FName("Specular");
	SpecularParameter->DefaultValue = 0.0f;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Specular)->Expression = SpecularParameter;
	UMaterialExpressionScalarParameter *RoughnessParameter = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -800, 450, ModelMaterial);
	RoughnessParameter->ParameterName = FName("Roughness");
	RoughnessParameter->DefaultValue = 0.0f;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Roughness)->Expression = RoughnessParameter;

	// Mark the material as dirty and update it
	ModelMaterial->MarkPackageDirty();
	ModelMaterial->PostEditChange();

	return ModelMaterial;
}

void FWoWLandscapeImporterModule::CreateLandscapeMaterial(ALandscape *Landscape)
{
	const FString BaseName = FPaths::GetCleanFilename(DirectoryPath);
	const FString MaterialDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/Materials/%s"), *BaseName);
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Create the RVT asset
	const FString RVTName = FString::Printf(TEXT("RVT_%s"), *BaseName);
	const FString RVTPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *RVTName);
	AssetTools.CreateAsset(RVTName, MaterialDirectory, URuntimeVirtualTexture::StaticClass(), nullptr);

	// Load the RVT asset
	URuntimeVirtualTexture *RVTAsset = static_cast<URuntimeVirtualTexture *>(UEditorAssetLibrary::LoadAsset(RVTPackagePath));
	Landscape->RuntimeVirtualTextures.Add(RVTAsset);

	// Create and load the material asset
	const FString MaterialName = FString::Printf(TEXT("M_%s"), *BaseName);
	const FString MaterialPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialName);
	AssetTools.CreateAsset(MaterialName, MaterialDirectory, UMaterial::StaticClass(), nullptr);
	UMaterial *LandscapeMaterial = static_cast<UMaterial *>(UEditorAssetLibrary::LoadAsset(MaterialPackagePath));
	LandscapeMaterial->bUseMaterialAttributes = true;

	// Create and load 256x256 texture2darray asset
	FString TextureArrayName = FString::Printf(TEXT("TEX_%s_Array_256"), *BaseName);
	FString TextureArrayPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *TextureArrayName);
	AssetTools.CreateAsset(TextureArrayName, MaterialDirectory, UTexture2DArray::StaticClass(), nullptr);
	UTexture2DArray *TextureArray256Asset = static_cast<UTexture2DArray *>(UEditorAssetLibrary::LoadAsset(TextureArrayPackagePath));

	// Create and load 512x512 texture2darray asset
	TextureArrayName = FString::Printf(TEXT("TEX_%s_Array_512"), *BaseName);
	TextureArrayPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *TextureArrayName);
	AssetTools.CreateAsset(TextureArrayName, MaterialDirectory, UTexture2DArray::StaticClass(), nullptr);
	UTexture2DArray *TextureArray512Asset = static_cast<UTexture2DArray *>(UEditorAssetLibrary::LoadAsset(TextureArrayPackagePath));

	// Create and load 1024x1024 texture2darray asset
	TextureArrayName = FString::Printf(TEXT("TEX_%s_Array_1024"), *BaseName);
	TextureArrayPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *TextureArrayName);
	AssetTools.CreateAsset(TextureArrayName, MaterialDirectory, UTexture2DArray::StaticClass(), nullptr);
	UTexture2DArray *TextureArray1024Asset = static_cast<UTexture2DArray *>(UEditorAssetLibrary::LoadAsset(TextureArrayPackagePath));

	// -- Calculate the UV mapping for the landscape --
	int32 Section0 = -4300;
	UMaterialExpressionLandscapeLayerCoords *CoordsNode = CreateNode(NewObject<UMaterialExpressionLandscapeLayerCoords>(LandscapeMaterial), Section0, 0, LandscapeMaterial);

	// Create Near and Far tiling size parameters
	UMaterialExpressionScalarParameter *NearTilingSize = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section0, 150, LandscapeMaterial);
	NearTilingSize->ParameterName = FName("NearTilingSize");
	NearTilingSize->Group = FName("DistanceBlend");
	NearTilingSize->DefaultValue = 2.0f;
	UMaterialExpressionScalarParameter *FarTilingSize = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section0, 250, LandscapeMaterial);
	FarTilingSize->ParameterName = FName("FarTilingSize");
	FarTilingSize->Group = FName("DistanceBlend");
	FarTilingSize->DefaultValue = 30.0f;

	// Create divide nodes to calculate UVs based on tiling sizes
	UMaterialExpressionDivide *DivideNodeNear = CreateNode(NewObject<UMaterialExpressionDivide>(LandscapeMaterial), Section0 + 300, 0, LandscapeMaterial);
	DivideNodeNear->A.Expression = CoordsNode;
	DivideNodeNear->B.Expression = NearTilingSize;
	UMaterialExpressionDivide *DivideNodeFar = CreateNode(NewObject<UMaterialExpressionDivide>(LandscapeMaterial), Section0 + 300, 100, LandscapeMaterial);
	DivideNodeFar->A.Expression = CoordsNode;
	DivideNodeFar->B.Expression = FarTilingSize;

	// Create reroute nodes for Near and Far UVs
	UMaterialExpressionNamedRerouteDeclaration *NearReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section0 + 450, 0, LandscapeMaterial);
	NearReroute->Name = FName("NearUV");
	NearReroute->NodeColor = FLinearColor::Blue;
	NearReroute->Input.Expression = DivideNodeNear;
	UMaterialExpressionNamedRerouteDeclaration *FarReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section0 + 450, 100, LandscapeMaterial);
	FarReroute->Name = FName("FarUV");
	FarReroute->NodeColor = FLinearColor::Red;
	FarReroute->Input.Expression = DivideNodeFar;

	// -- RVT MIP based depth fading --
	UMaterialExpressionConstant *ConstantNode = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section0, 500, LandscapeMaterial);
	ConstantNode->R = 2.0f;

	UMaterialExpressionViewProperty *ViewProperty = CreateNode(NewObject<UMaterialExpressionViewProperty>(LandscapeMaterial), Section0, 600, LandscapeMaterial);
	ViewProperty->Property = MEVP_RuntimeVirtualTextureOutputLevel;

	UMaterialExpressionPower *PowerNode = CreateNode(NewObject<UMaterialExpressionPower>(LandscapeMaterial), Section0 + 300, 600, LandscapeMaterial);
	PowerNode->Base.Expression = ConstantNode;
	PowerNode->Exponent.Expression = ViewProperty;

	UMaterialExpressionMultiply *MultiplyNode = CreateNode(NewObject<UMaterialExpressionMultiply>(LandscapeMaterial), Section0 + 450, 600, LandscapeMaterial);
	MultiplyNode->A.Expression = PowerNode;
	MultiplyNode->ConstB = 1000.0f;

	UMaterialExpressionScalarParameter *BlendDistanceStart = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section0 + 450, 500, LandscapeMaterial);
	BlendDistanceStart->ParameterName = FName("BlendDistanceStart");
	BlendDistanceStart->Group = FName("DistanceBlend");
	BlendDistanceStart->DefaultValue = 16000.0f;

	UMaterialExpressionSubtract *SubtractNode = CreateNode(NewObject<UMaterialExpressionSubtract>(LandscapeMaterial), Section0 + 650, 600, LandscapeMaterial);
	SubtractNode->A.Expression = MultiplyNode;
	SubtractNode->B.Expression = BlendDistanceStart;

	UMaterialExpressionSaturate *SaturateNode = CreateNode(NewObject<UMaterialExpressionSaturate>(LandscapeMaterial), Section0 + 850, 600, LandscapeMaterial);
	SaturateNode->Input.Expression = SubtractNode;

	UMaterialExpressionNamedRerouteDeclaration *DepthFadeReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section0 + 1000, 600, LandscapeMaterial);
	DepthFadeReroute->Name = FName("DepthFade");
	DepthFadeReroute->NodeColor = FLinearColor::Green;
	DepthFadeReroute->Input.Expression = SaturateNode;

	int32 Section1 = -2800;
	UMaterialExpressionLandscapeLayerBlend *LayerBlendNode = CreateNode(NewObject<UMaterialExpressionLandscapeLayerBlend>(LandscapeMaterial), Section1 + 1400, 0, LandscapeMaterial);

	// Find the 3PointLevels Material Function
	UMaterialFunction *ThreePointLevelsFunc = LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions02/3PointLevels.3PointLevels"));

	// Create black value, gray value, and white value parameters
	UMaterialExpressionScalarParameter *BlackValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section1 + 1000, -250, LandscapeMaterial);
	BlackValueNode->ParameterName = FName("BlackValue");
	BlackValueNode->Group = FName("3PointLevels");
	BlackValueNode->DefaultValue = 0.5f;
	UMaterialExpressionScalarParameter *GrayValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section1 + 1000, -175, LandscapeMaterial);
	GrayValueNode->ParameterName = FName("GrayValue");
	GrayValueNode->Group = FName("3PointLevels");
	GrayValueNode->DefaultValue = 0.6f;
	UMaterialExpressionScalarParameter *WhiteValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section1 + 1000, -100, LandscapeMaterial);
	WhiteValueNode->ParameterName = FName("WhiteValue");
	WhiteValueNode->Group = FName("3PointLevels");
	WhiteValueNode->DefaultValue = 1.0f;
	UMaterialExpressionNamedRerouteDeclaration *BlackValueReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section1 + 1200, -250, LandscapeMaterial);
	BlackValueReroute->Name = FName("BlackValue");
	BlackValueReroute->NodeColor = FLinearColor::Black;
	BlackValueReroute->Input.Expression = BlackValueNode;
	UMaterialExpressionNamedRerouteDeclaration *GrayValueReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section1 + 1200, -175, LandscapeMaterial);
	GrayValueReroute->Name = FName("GrayValue");
	GrayValueReroute->NodeColor = FLinearColor::Gray;
	GrayValueReroute->Input.Expression = GrayValueNode;
	UMaterialExpressionNamedRerouteDeclaration *WhiteValueReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section1 + 1200, -100, LandscapeMaterial);
	WhiteValueReroute->Name = FName("WhiteValue");
	WhiteValueReroute->NodeColor = FLinearColor::White;
	WhiteValueReroute->Input.Expression = WhiteValueNode;

	// Loop through our stored layer data to create and connect texture samplers
	int32 NodeOffsetY = 0;
	int32 TextureArrayIndex256 = 0, TextureArrayIndex512 = 0, TextureArrayIndex1024 = 0;
	for (auto const &[LayerName, LayerMetadata] : LayerMetadataMap)
	{

		UMaterialExpressionNamedRerouteUsage *NearRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY, LandscapeMaterial);
		NearRerouteUsage->Declaration = NearReroute;
		UMaterialExpressionNamedRerouteUsage *FarRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY + 75, LandscapeMaterial);
		FarRerouteUsage->Declaration = FarReroute;
		UMaterialExpressionNamedRerouteUsage *DepthFadeRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY + 150, LandscapeMaterial);
		DepthFadeRerouteUsage->Declaration = DepthFadeReroute;

		// Interpolate UV coordinates first, then sample texture once
		UMaterialExpressionLinearInterpolate *LerpUV = CreateNode(NewObject<UMaterialExpressionLinearInterpolate>(LandscapeMaterial), Section1 + 150, NodeOffsetY, LandscapeMaterial);
		LerpUV->A.Expression = NearRerouteUsage;
		LerpUV->B.Expression = FarRerouteUsage;
		LerpUV->Alpha.Expression = DepthFadeRerouteUsage;

		UTexture2D *LayerTexture = LayerMetadata.LayerTexture.Get();
		UMaterialExpressionTextureSampleParameter2DArray *TextureSampleArray = nullptr;
		UMaterialExpressionTextureSample *TextureSample = nullptr;
		bool bUseTextureArray = true;

		if (LayerTexture->GetSizeX() == 256 || LayerTexture->GetSizeX() == 512 || LayerTexture->GetSizeX() == 1024)
		{
			// Create constant for texture array index
			UMaterialExpressionConstant *ArrayIndexConstant = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section1 + 200, NodeOffsetY + 100, LandscapeMaterial);

			// Create append vector to combine UV coordinates with array index
			UMaterialExpressionAppendVector *AppendUVIndex = CreateNode(NewObject<UMaterialExpressionAppendVector>(LandscapeMaterial), Section1 + 300, NodeOffsetY + 50, LandscapeMaterial);
			AppendUVIndex->A.Expression = LerpUV;
			AppendUVIndex->B.Expression = ArrayIndexConstant;

			TextureSampleArray = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2DArray>(LandscapeMaterial), Section1 + 350, NodeOffsetY, LandscapeMaterial);
			TextureSampleArray->SamplerSource = ESamplerSourceMode::SSM_Wrap_WorldGroupSettings;

			// Connect the UV+index coordinates to the texture array sampler
			TextureSampleArray->Coordinates.Expression = AppendUVIndex;

			if (LayerTexture->CompressionSettings == TC_Normalmap)
				TextureSampleArray->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;

			if (LayerTexture->GetSizeX() == 256)
			{
				TextureArray256Asset->SourceTextures.Add(LayerTexture);
				TextureSampleArray->Texture = TextureArray256Asset;
				TextureSampleArray->ParameterName = FName("TextureArray256");
				ArrayIndexConstant->R = TextureArrayIndex256;
				TextureArrayIndex256++;
			}
			else if (LayerTexture->GetSizeX() == 512)
			{
				TextureArray512Asset->SourceTextures.Add(LayerTexture);
				TextureSampleArray->Texture = TextureArray512Asset;
				TextureSampleArray->ParameterName = FName("TextureArray512");
				ArrayIndexConstant->R = TextureArrayIndex512;
				TextureArrayIndex512++;
			}
			else if (LayerTexture->GetSizeX() == 1024)
			{
				TextureArray1024Asset->SourceTextures.Add(LayerTexture);
				TextureSampleArray->Texture = TextureArray1024Asset;
				TextureSampleArray->ParameterName = FName("TextureArray1024");
				ArrayIndexConstant->R = TextureArrayIndex1024;
				TextureArrayIndex1024++;
			}
		}
		else
		{
			bUseTextureArray = false;
			TextureSample = CreateNode(NewObject<UMaterialExpressionTextureSample>(LandscapeMaterial), Section1 + 350, NodeOffsetY, LandscapeMaterial);
			TextureSample->Texture = LayerTexture;
			TextureSample->Coordinates.Expression = LerpUV;
			TextureSample->SamplerSource = ESamplerSourceMode::SSM_Wrap_WorldGroupSettings;

			if (LayerTexture->CompressionSettings == TC_Normalmap)
				TextureSample->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
		}

		// Connect the output 0 of the texture sample to the MakeMaterialAttributes node
		UMaterialExpressionMakeMaterialAttributes *MakeMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionMakeMaterialAttributes>(LandscapeMaterial), Section1 + 650, NodeOffsetY, LandscapeMaterial);
		MakeMaterialAttributesNode->BaseColor.Expression = bUseTextureArray ? TextureSampleArray : TextureSample;

		// Create a constant node for Specular and connect it
		UMaterialExpressionConstant *SpecularConstantNode = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section1 + 400, NodeOffsetY + 250, LandscapeMaterial);
		SpecularConstantNode->R = 0.0f;
		MakeMaterialAttributesNode->Specular.Expression = SpecularConstantNode;

		UMaterialExpressionMaterialFunctionCall *LevelsNode = CreateNode(NewObject<UMaterialExpressionMaterialFunctionCall>(LandscapeMaterial), Section1 + 1050, NodeOffsetY + 150, LandscapeMaterial);
		LevelsNode->MaterialFunction = ThreePointLevelsFunc;
		LevelsNode->UpdateFromFunctionResource();

		// Connect the alpha channel of the texture to the 'Input' of the 3PointLevels function
		LevelsNode->GetInput(0)->Expression = bUseTextureArray ? TextureSampleArray : TextureSample;
		LevelsNode->GetInput(0)->OutputIndex = 4;

		// Connect the black, gray, and white values to the 3PointLevels function
		UMaterialExpressionNamedRerouteUsage *BlackValueUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 900, NodeOffsetY + 250, LandscapeMaterial);
		BlackValueUsage->Declaration = BlackValueReroute;
		LevelsNode->GetInput(2)->Expression = BlackValueUsage;
		UMaterialExpressionNamedRerouteUsage *GrayValueUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 900, NodeOffsetY + 325, LandscapeMaterial);
		GrayValueUsage->Declaration = GrayValueReroute;
		LevelsNode->GetInput(3)->Expression = GrayValueUsage;
		UMaterialExpressionNamedRerouteUsage *WhiteValueUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 900, NodeOffsetY + 400, LandscapeMaterial);
		WhiteValueUsage->Declaration = WhiteValueReroute;
		LevelsNode->GetInput(4)->Expression = WhiteValueUsage;

		FLayerBlendInput LayerInput;
		LayerInput.LayerName = LayerMetadata.LayerInfo->LayerName;
		LayerInput.BlendType = LB_HeightBlend;
		LayerInput.LayerInput.Expression = MakeMaterialAttributesNode;
		LayerInput.HeightInput.Expression = LevelsNode;

		LayerBlendNode->Layers.Add(LayerInput);

		NodeOffsetY += 600;
	}

	// Update the texture array asset after adding all source textures
	TextureArray256Asset->UpdateSourceFromSourceTextures();
	TextureArray256Asset->MipGenSettings = TMGS_FromTextureGroup;
	TextureArray256Asset->MarkPackageDirty();
	TextureArray256Asset->PostEditChange();
	TextureArray512Asset->UpdateSourceFromSourceTextures();
	TextureArray512Asset->MipGenSettings = TMGS_FromTextureGroup;
	TextureArray512Asset->MarkPackageDirty();
	TextureArray512Asset->PostEditChange();
	TextureArray1024Asset->UpdateSourceFromSourceTextures();
	TextureArray1024Asset->MipGenSettings = TMGS_FromTextureGroup;
	TextureArray1024Asset->MarkPackageDirty();
	TextureArray1024Asset->PostEditChange();

	UMaterialExpressionGetMaterialAttributes *GetMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionGetMaterialAttributes>(LandscapeMaterial), Section1 + 1800, 300, LandscapeMaterial);
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_BaseColor));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Specular));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Roughness));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Normal));
	GetMaterialAttributesNode->Outputs.SetNum(GetMaterialAttributesNode->AttributeGetTypes.Num() + 1);
	GetMaterialAttributesNode->MaterialAttributes.Expression = LayerBlendNode;

	// RVT output class does not have Minimal API, so we use FindObject to get the class. (This is a workaround)
	UClass *RVTOutputClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionRuntimeVirtualTextureOutput"));
	UMaterialExpression *RVTOutputExpression = static_cast<UMaterialExpression *>(NewObject<UObject>(LandscapeMaterial, RVTOutputClass));
	UMaterialExpressionCustomOutput *RVTOutputNode = static_cast<UMaterialExpressionCustomOutput *>(CreateNode(RVTOutputExpression, Section1 + 2200, 300, LandscapeMaterial));
	// Connect the output of the GetMaterialAttributes node to the RVT output node
	for (int i = 0; i < GetMaterialAttributesNode->AttributeGetTypes.Num(); ++i)
	{
		RVTOutputNode->GetInput(i)->Expression = GetMaterialAttributesNode;
		RVTOutputNode->GetInput(i)->OutputIndex = i + 1;
	}

	UMaterialExpressionRuntimeVirtualTextureSample *RVTSampleNode = CreateNode(NewObject<UMaterialExpressionRuntimeVirtualTextureSample>(LandscapeMaterial), Section1 + 1800, 0, LandscapeMaterial);
	RVTSampleNode->VirtualTexture = RVTAsset;
	RVTSampleNode->MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;

	UMaterialExpressionMakeMaterialAttributes *MakeMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionMakeMaterialAttributes>(LandscapeMaterial), Section1 + 2500, 0, LandscapeMaterial);
	MakeMaterialAttributesNode->BaseColor.Expression = RVTSampleNode;
	MakeMaterialAttributesNode->Specular.Expression = RVTSampleNode;
	MakeMaterialAttributesNode->Specular.OutputIndex = 1;
	MakeMaterialAttributesNode->Roughness.Expression = RVTSampleNode;
	MakeMaterialAttributesNode->Roughness.OutputIndex = 2;
	MakeMaterialAttributesNode->Normal.Expression = RVTSampleNode;
	MakeMaterialAttributesNode->Normal.OutputIndex = 3;

	// Create landscape visibility mask node and connect it to opacity mask on material attributes node
	UMaterialExpressionLandscapeVisibilityMask *VisibilityMaskNode = CreateNode(NewObject<UMaterialExpressionLandscapeVisibilityMask>(LandscapeMaterial), Section1 + 2200, 150, LandscapeMaterial);
	MakeMaterialAttributesNode->OpacityMask.Expression = VisibilityMaskNode;

	// Connect the final material attributes to the material's output
	LandscapeMaterial->GetExpressionInputForProperty(MP_MaterialAttributes)->Expression = MakeMaterialAttributesNode;

	// Mark the package as needing to be saved and update the material
	LandscapeMaterial->MarkPackageDirty();
	LandscapeMaterial->PostEditChange();

	// Create a landscape material instance and return it
	const FString MaterialInstanceName = FString::Printf(TEXT("MI_%s"), *BaseName);
	const FString MaterialInstancePackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialInstanceName);

	// Create the material instance asset
	UMaterialInstanceConstantFactoryNew *MaterialInstanceFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
	AssetTools.CreateAsset(MaterialInstanceName, MaterialDirectory, UMaterialInstanceConstant::StaticClass(), MaterialInstanceFactory);

	// Load and configure the material instance
	UMaterialInstanceConstant *MaterialInstance = static_cast<UMaterialInstanceConstant *>(UEditorAssetLibrary::LoadAsset(MaterialInstancePackagePath));
	MaterialInstance->SetParentEditorOnly(LandscapeMaterial);

	// Update and mark as dirty
	MaterialInstance->MarkPackageDirty();
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