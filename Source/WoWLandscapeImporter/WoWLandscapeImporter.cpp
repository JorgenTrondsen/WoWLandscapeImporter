#include "WoWLandscapeImporter.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Async/Async.h"
#include "Commands/WoWLandscapeImporterCommands.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFilemanager.h"
#include "IAssetTools.h"
#include "IDesktopPlatform.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Landscape.h"
#include "LandscapeGrassType.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeStreamingProxy.h"
#include "MaterialDomain.h"
#include "MaterialEditingLibrary.h"
#include "MaterialEditorUtilities.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionViewProperty.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Style/WoWLandscapeImporterStyle.h"
#include "ToolMenus.h"
#include "UObject/ConstructorHelpers.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

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
					 [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 2)[SNew(STextBlock).Text(LOCTEXT("WoWLandscapeImporterTitle", "WoW Importer")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)).Justification(ETextJustify::Center)] + SVerticalBox::Slot().AutoHeight().Padding(0, 3)[SNew(STextBlock).Text(LOCTEXT("ImportDescription", "Select directory:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).Justification(ETextJustify::Center)] + SVerticalBox::Slot().AutoHeight().Padding(0, 5)[SNew(SButton).Text(LOCTEXT("ImportButtonText", "Import")).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked_Raw(this, &FWoWLandscapeImporterModule::OnImportButtonClicked).ContentPadding(FMargin(12, 6))] + SVerticalBox::Slot().AutoHeight().Padding(0, 10)[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SNew(STextBlock).Text(LOCTEXT("WPGridSizeLabel", "World Partition Grid Size:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))] + SHorizontalBox::Slot().AutoWidth()[SNew(SSpinBox<int>).MinValue(1).MaxValue(10).Value_Lambda([this]()
																																																																																																																																																																																																																																																																																				 { return WPGridSize; })
																																																																																																																																																																																																																																																																						   .OnValueChanged_Lambda([this](int NewValue)
																																																																																																																																																																																																																																																																												  { WPGridSize = NewValue; })
																																																																																																																																																																																																																																																																						   .MinDesiredWidth(60.0f)]] +
					  SVerticalBox::Slot()
						  .AutoHeight()
						  .Padding(0, 10)
							  [SAssignNew(StatusMessageWidget, STextBlock)
								   .Text(FText::GetEmpty())
								   .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
								   .Justification(ETextJustify::Center)
								   .ColorAndOpacity(FSlateColor(FLinearColor::White))]]];
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

		// Temporarily commented out ImportLandscape as we want to focus on implementing new logic specifically for CreateModelMaterial and ImportModels.
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

		// TArray<FString> SelectedFiles;
		// const bool bOBJSelected = DesktopPlatform->OpenFileDialog(
		// 	FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		// 	TEXT("Select Foliage OBJ File"),
		// 	DefaultPath,
		// 	TEXT(""),
		// 	TEXT("OBJ Files (*.obj)|*.obj"),
		// 	EFileDialogFlags::Multiple,
		// 	SelectedFiles);

		// if (bOBJSelected && SelectedFiles.Num() > 0)
		// {
		// 	OBJFilePath = FPaths::GetPath(SelectedFiles[0]);
		// 	UMaterial *ModelMaterial = CreateModelMaterial(TEXT("M_Model"));
		// 	ImportModels(SelectedFiles, ModelMaterial);
		// }
	}

	return FReply::Handled();
}

void FWoWLandscapeImporterModule::ImportLandscape()
{
	TArray<FString> HeightmapFiles;
	TArray<FString> AlphamapPNGs;
	TArray<FString> AlphamapJSONs;
	TArray<FString> CSVFiles;
	TArray<FString> FoliageFiles;
	TArray<FString> FoliageJSONs;

	// Find all required files in the directory
	FString SearchPattern = FPaths::Combine(DirectoryPath, TEXT("heightmaps/*.png"));
	IFileManager::Get().FindFiles(HeightmapFiles, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("alphamaps/*.png"));
	IFileManager::Get().FindFiles(AlphamapPNGs, *SearchPattern, true, false);
	AlphamapPNGs.RemoveAll([](const FString &File)
						   { return File.Contains(TEXT("_1.png")); }); // Remove secondary alphamap pngs from the list, we'll handle them in pairs later
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("alphamaps/*.json"));
	IFileManager::Get().FindFiles(AlphamapJSONs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.csv"));
	IFileManager::Get().FindFiles(CSVFiles, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("foliage/*.obj"));
	IFileManager::Get().FindFiles(FoliageFiles, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("foliage/layerinfo*.json"));
	IFileManager::Get().FindFiles(FoliageJSONs, *SearchPattern, true, false);

	for (FString &File : FoliageFiles)
		File = FPaths::Combine(DirectoryPath, TEXT("foliage/"), File);

	if (HeightmapFiles.IsEmpty() || AlphamapPNGs.IsEmpty() || AlphamapJSONs.IsEmpty() || CSVFiles.IsEmpty() || FoliageJSONs.IsEmpty())
	{
		UpdateStatusMessage(TEXT("Missing either: Heightmap files, Alphamap PNGs, Alphamap JSONs, CSV files, or Foliage JSONs"), true);
	}
	else
	{
		double Zscale = 0.0, SeaLevelOffset = 0.0;
		int TileColumns = 0, TileRows = 0;

		// Read heightmap metadata JSON and extract heightmap range
		FString MetadataPath = FPaths::Combine(DirectoryPath, TEXT("heightmaps/heightmap.json"));
		TSharedPtr<FJsonObject> JsonObject = LoadJsonObject(MetadataPath);
		TSharedPtr<FJsonObject> HeightDataObject = JsonObject->GetObjectField(TEXT("height_data"));

		double Range = HeightDataObject->GetNumberField(TEXT("range"));
		Zscale = Range * 91.44;			 // RangeValue is in yards, convert to centimeters (1 yard = 91.44 cm)
		Zscale = (Zscale / 51200) * 100; // Convert to percentage scale (100% = 51200 cm)

		double NormalizedSealevel = HeightDataObject->GetNumberField(TEXT("normalized_sealevel"));
		double CalculatedSeaLevel = (NormalizedSealevel - 0.5) * 51200;
		SeaLevelOffset = CalculatedSeaLevel * (Zscale / 100);

		TSharedPtr<FJsonObject> TileDataObject = JsonObject->GetObjectField(TEXT("tile_data"));
		TileColumns = TileDataObject->GetNumberField(TEXT("columns"));
		TileRows = TileDataObject->GetNumberField(TEXT("rows"));

		TileGrid.SetNum(TileRows);
		for (int Row = 0; Row < TileRows; Row++)
			TileGrid[Row].SetNum(TileColumns);

		TMap<int, TTuple<FString, FString, int>> TexturePaths;
		// Collect filedata and metadata
		for (int i = 0; i < HeightmapFiles.Num(); i++)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(HeightmapFiles[i]).ParseIntoArray(NameParts, TEXT("_"), true);
			Tile NewTile;

			NewTile.Column = FCString::Atoi(*NameParts[1]);
			NewTile.Row = FCString::Atoi(*NameParts[2]);

			// Collect heightmap PNG data
			FString HeightmapPath = FPaths::Combine(DirectoryPath, TEXT("heightmaps/"), HeightmapFiles[i]);
			LoadImageData(HeightmapPath, ERGBFormat::Gray, 16, NewTile.HeightmapData);

			// Collect alphamaps and their PNG data
			for (int j = 0; j < 2; j++)
			{
				FString FileName = (j == 0) ? AlphamapPNGs[i] : AlphamapPNGs[i].LeftChop(4) + TEXT("_1.png");
				FString AlphamapPath = FPaths::Combine(DirectoryPath, TEXT("alphamaps/"), FileName);
				LoadImageData(AlphamapPath, ERGBFormat::BGRA, 8, NewTile.AlphamapPNGs[j]);
			}

			// Collect alphamap JSON data
			FString JsonPath = FPaths::Combine(DirectoryPath, TEXT("alphamaps/"), AlphamapJSONs[i]);
			TSharedPtr<FJsonObject> AlphamapJsonObject = LoadJsonObject(JsonPath);
			TArray<TSharedPtr<FJsonValue>> Layers = AlphamapJsonObject->GetArrayField(TEXT("layers"));

			for (const TSharedPtr<FJsonValue> &LayerValue : Layers)
			{
				TSharedPtr<FJsonObject> LayerObject = LayerValue->AsObject();
				FString TexPathBase = LayerObject->GetStringField(TEXT("file")).Replace(TEXT("\\"), TEXT("/"));
				FString TexPathHeight = LayerObject->GetStringField(TEXT("heightFile")).Replace(TEXT("\\"), TEXT("/"));
				TexturePaths.FindOrAdd(LayerObject->GetNumberField(TEXT("effectID")), TTuple<FString, FString, int>(TexPathBase, TexPathHeight, 0)).Get<2>()++;

				int ChunkIndex = LayerObject->GetNumberField(TEXT("chunkIndex"));
				Layer NewLayer;
				NewLayer.LayerName = FName(FPaths::GetBaseFilename(TexPathBase));
				NewLayer.ImageIndex = LayerObject->GetIntegerField(TEXT("imageIndex"));
				NewLayer.ChannelIndex = LayerObject->GetIntegerField(TEXT("channelIndex"));
				NewTile.Chunks[ChunkIndex].Layers.Add(NewLayer);
			}
			TileGrid[NewTile.Row][NewTile.Column] = NewTile;
		}

		UMaterial *ModelMaterial = CreateModelMaterial(TEXT("M_Model"));
		ImportLayers(TexturePaths, FoliageFiles, FoliageJSONs, ModelMaterial);

		ALandscape *Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>();
		Landscape->SetActorLabel(*FPaths::GetCleanFilename(DirectoryPath));
		Landscape->SetActorScale3D(FVector(48768.f / 255.f, 48768.f / 255.f, Zscale)); // X/Y scale is 48,768 cm ÷ 255 quads. Standard WoW ADT (map tile) is 533.333 yards (48,768 cm) wide.

		FGuid LandscapeGuid = FGuid::NewGuid();
		Landscape->SetLandscapeGuid(LandscapeGuid);

		// Heightmaps are 256x256, but each landscape component should be 510x510
		Landscape->ComponentSizeQuads = 510;
		Landscape->SubsectionSizeQuads = 255;
		Landscape->NumSubsections = 2;

		ULandscapeInfo *LandscapeInfo = Landscape->CreateLandscapeInfo();
		{
			FScopedSlowTask SlowTask(TileRows * TileColumns, LOCTEXT("ImportingWoWLandscape", "Importing WoW Landscape..."));
			SlowTask.MakeDialog();

			for (int Row = 0; Row < TileRows; Row++)
			{
				bool RowHasData = false;
				for (int Column = 0; Column < TileColumns; Column++)
				{
					SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingProxy", "Importing Proxy at (Row {1}), (Column {0})"), Column, Row));
					if (TileGrid[Row][Column].HeightmapData.Num() == 0)
						continue;
					RowHasData = true;
					TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> ProxyData = CreateProxyData(Row, Column);

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

					uint32 MinY = Row * 255;
					uint32 MinX = Column * 255;
					uint32 MaxY = MinY + 510;
					uint32 MaxX = MinX + 510;
					StreamingProxy->Import(FGuid::NewGuid(), MinX, MinY, MaxX, MaxY, 2, 255, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Layered);

					StreamingProxy->SetLandscapeGuid(LandscapeGuid);
					LandscapeInfo->RegisterActor(StreamingProxy);

					Column++;
				}
				if (RowHasData)
					Row++;
			}
		}
		CreateLandscapeMaterial(Landscape);

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
					Actor.Tile = CSVFile.Replace(TEXT("_ModelPlacementInformation.csv"), TEXT("")).Replace(TEXT("adt_"), TEXT(""));

					if (CSVFields[10] == TEXT("gobj"))
					{
						// Game object has data relative to the center of the map, so we need to offset by 17066.66656f
						Actor.Position = FVector(
							(17066.66656f - FCString::Atod(*CSVFields[2])) * 91.44f,
							(17066.66656f - FCString::Atod(*CSVFields[1])) * 91.44f,
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
								WMOActor.Tile = Actor.Tile;
								WMOActor.ParentWMO = FPaths::GetBaseFilename(Actor.ModelPath);

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

			// Set folder path based on tile and parent WMO (if applicable)
			FString FolderPath = ActorsArray[Actor].ParentWMO.IsEmpty() ? ActorsArray[Actor].Tile : FString::Printf(TEXT("%s/%s"), *ActorsArray[Actor].Tile, *ActorsArray[Actor].ParentWMO);
			ModelActor->SetFolderPath(FName(*FolderPath));

			ModelActor->GetStaticMeshComponent()->SetStaticMesh(ImportedModels[Model]);

			// We need to calculate the correct positions, as they are stored as yards in csv.
			ModelActor->SetActorLocation(ActorsArray[Actor].Position);
			ModelActor->SetActorRotation(ActorsArray[Actor].Rotation);
			ModelActor->SetActorScale3D(FVector(ActorsArray[Actor].Scale * 91.44f));
		}
	}
}

void FWoWLandscapeImporterModule::ImportLayers(TMap<int, TTuple<FString, FString, int>> &TexturePaths, TArray<FString> &FoliageFiles, TArray<FString> &FoliageJSONs, UMaterial *ModelMaterial)
{
	// Find the Map Key with the highest count for each Texture Path
	TMap<FString, int> BestKeys;
	for (auto &Elem : TexturePaths)
	{
		int *BestKey = BestKeys.Find(Elem.Value.Get<0>());
		if (!BestKey || Elem.Value.Get<2>() > TexturePaths[*BestKey].Get<2>())
			BestKeys.Add(Elem.Value.Get<0>(), Elem.Key); // Add or overwrite with the new best
	}

	// Remove any entries that are not the chosen "best" key
	for (auto It = TexturePaths.CreateIterator(); It; ++It)
	{
		if (BestKeys[It->Value.Get<0>()] != It->Key)
		{
			// Remove the foliage json file that corresponds to the removed effectID key.
			FoliageJSONs.Remove(FString::Printf(TEXT("layerinfo%d.json"), It->Key));
			It.RemoveCurrent();
		}
	}

	// Import textures
	UInterchangeManager &InterchangeManager = UInterchangeManager::GetInterchangeManager();
	FImportAssetParameters ImportParams;
	ImportParams.bIsAutomated = true;
	ImportParams.bReplaceExisting = true;

	TArray<TTuple<UE::Interchange::FAssetImportResultRef, UE::Interchange::FAssetImportResultRef>> ImportResults;
	for (auto &TextureTuple : TexturePaths)
	{
		FString DestinationDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/%s"), *FPaths::GetPath(TextureTuple.Value.Get<0>()).Replace(TEXT("../"), TEXT("")));
		UInterchangeSourceData *SourceData = UInterchangeManager::CreateSourceData(FPaths::ConvertRelativePathToFull(DirectoryPath, TextureTuple.Value.Get<0>().RightChop(3)));
		UE::Interchange::FAssetImportResultRef ImportResult = InterchangeManager.ImportAssetAsync(DestinationDirectory, SourceData, ImportParams);

		DestinationDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/%s"), *FPaths::GetPath(TextureTuple.Value.Get<1>()).Replace(TEXT("../"), TEXT("")));
		SourceData = UInterchangeManager::CreateSourceData(FPaths::ConvertRelativePathToFull(DirectoryPath, TextureTuple.Value.Get<1>().RightChop(3)));
		UE::Interchange::FAssetImportResultRef ImportResultHeight = InterchangeManager.ImportAssetAsync(DestinationDirectory, SourceData, ImportParams);

		ImportResults.Add(MakeTuple(ImportResult, ImportResultHeight));
	}

	{
		FScopedSlowTask SlowTask(ImportResults.Num(), LOCTEXT("ImportingWoWLayers", "Importing WoW Layers..."));
		SlowTask.MakeDialog();

		int Index = 0;
		for (auto &TextureTuple : TexturePaths)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingLayer", "Importing Layer: {0}"), Index));
			const UE::Interchange::FAssetImportResultRef &ImportResult = ImportResults[Index].Get<0>();
			const UE::Interchange::FAssetImportResultRef &ImportResultHeight = ImportResults[Index].Get<1>();

			const FString DestinationDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/%s"), *FPaths::GetPath(TextureTuple.Value.Get<0>()).Replace(TEXT("../"), TEXT("")));
			FString TextureFileName = FPaths::GetBaseFilename(TextureTuple.Value.Get<0>());
			FString LayerInfoName = FString::Printf(TEXT("LI_%s"), *TextureFileName);

			UPackage *LayerInfoPackage = CreatePackage(*(DestinationDirectory + TEXT("/") + LayerInfoName));
			ULandscapeLayerInfoObject *LayerInfo = NewObject<ULandscapeLayerInfoObject>(LayerInfoPackage, *LayerInfoName, RF_Public | RF_Standalone);
			LayerInfo->LayerName = FName(*TextureFileName);
			LayerInfo->PhysMaterial = nullptr;
			LayerInfo->LayerUsageDebugColor = FLinearColor::White;
			LayerInfo->MarkPackageDirty();

			LayerMetadata Metadata;
			Metadata.LayerInfo = LayerInfo;
			ImportResult->WaitUntilDone();
			ImportResultHeight->WaitUntilDone();
			if (ImportResult->GetImportedObjects().Num() > 0)
				Metadata.LayerTexture = Cast<UTexture2D>(ImportResult->GetImportedObjects()[0]);
			if (ImportResultHeight->GetImportedObjects().Num() > 0)
				Metadata.LayerTextureHeight = Cast<UTexture2D>(ImportResultHeight->GetImportedObjects()[0]);
			Metadata.FoliageAsset = nullptr;
			LayerMetadataMap.Add(LayerInfo->LayerName, Metadata);

			Index++;
		}
	}

	TArray<UStaticMesh *> ImportedFoliage = ImportModels(FoliageFiles, ModelMaterial, true);

	// Map foliage mesh to corresponding layers in LayerMetadataMap
	for (FString &FoliageJSON : FoliageJSONs)
	{
		FString JsonPath = FPaths::Combine(DirectoryPath, TEXT("foliage/"), FoliageJSON);
		TSharedPtr<FJsonObject> JsonObject = LoadJsonObject(JsonPath);
		int EffectID = JsonObject->GetIntegerField(TEXT("ID"));
		TArray<FString> FoliageNames;
		const TSharedPtr<FJsonObject> DoodadModelIDsObject = JsonObject->GetObjectField(TEXT("DoodadModelIDs"));

		for (const TTuple<FString, TSharedPtr<FJsonValue>> &Pair : DoodadModelIDsObject->Values)
			FoliageNames.Add(Pair.Value->AsObject()->GetStringField(TEXT("fileName")).Replace(TEXT(".obj"), TEXT("")));

		// Find which imported foliage meshes correspond to these names
		TArray<UStaticMesh *> FoliageMeshes;
		for (const FString &FoliageName : FoliageNames)
		{
			for (UStaticMesh *ImportedMesh : ImportedFoliage)
			{
				if (ImportedMesh->GetName() == FoliageName)
				{
					FoliageMeshes.Add(ImportedMesh);
					break;
				}
			}
		}

		if (FoliageMeshes.Num() > 0)
		{
			LayerMetadata *LayerMetaData = LayerMetadataMap.Find(FName(*FPaths::GetBaseFilename(TexturePaths[EffectID].Get<0>())));

			FString PackagePath = FPackageName::GetLongPackagePath(LayerMetaData->LayerInfo->GetOutermost()->GetName());
			FString AssetName = "GT_" + FPaths::GetBaseFilename(TexturePaths[EffectID].Get<0>());

			UPackage *GrassPackage = CreatePackage(*(PackagePath + TEXT("/") + AssetName));
			ULandscapeGrassType *FoliageAsset = NewObject<ULandscapeGrassType>(GrassPackage, *AssetName, RF_Public | RF_Standalone);

			FoliageAsset->GrassVarieties.Empty();
			for (UStaticMesh *Mesh : FoliageMeshes)
			{
				FGrassVariety Variety;
				Variety.GrassMesh = Mesh;
				Variety.GrassDensity = FPerPlatformFloat(20.0f);
				Variety.ScaleX = FFloatInterval(160.0f, 160.0f);
				Variety.StartCullDistance = FPerPlatformInt(34500);
				Variety.EndCullDistance = FPerPlatformInt(35000);
				Variety.bCastDynamicShadow = false;
				Variety.InstanceWorldPositionOffsetDisableDistance = 10000.0f;
				FoliageAsset->GrassVarieties.Add(Variety);
			}

			FoliageAsset->MarkPackageDirty();
			LayerMetaData->FoliageAsset = FoliageAsset;
		}
	}
}

TArray<UStaticMesh *> FWoWLandscapeImporterModule::ImportModels(TArray<FString> &ModelPaths, UMaterial *ModelMaterial, bool isFoliage)
{
	// Remove duplicates from the asset paths
	ModelPaths = TSet<FString>(MoveTemp(ModelPaths)).Array();

	UInterchangeGenericAssetsPipeline *Pipeline = NewObject<UInterchangeGenericAssetsPipeline>();
	Pipeline->bUseSourceNameForAsset = true;
	Pipeline->ReimportStrategy = EReimportStrategyFlags::ApplyNoProperties;
	Pipeline->ImportOffsetRotation = FRotator(0, 0, 90); // 90 degree rotation around Z-axis
	Pipeline->MeshPipeline->bBuildReversedIndexBuffer = false;
	Pipeline->MeshPipeline->bImportStaticMeshes = true;
	Pipeline->MeshPipeline->bCombineStaticMeshes = true;
	Pipeline->MeshPipeline->bImportCollision = false;
	Pipeline->MaterialPipeline->bImportMaterials = false;

	FImportAssetParameters ImportParams;
	ImportParams.bIsAutomated = true;
	ImportParams.bReplaceExisting = true;
	ImportParams.OverridePipelines.Add(FSoftObjectPath(Pipeline));

	UInterchangeManager &InterchangeManager = UInterchangeManager::GetInterchangeManager();
	TArray<TTuple<FString, UE::Interchange::FAssetImportResultRef, UE::Interchange::FAssetImportResultRef>> ImportResults;
	for (const FString &ModelPath : ModelPaths)
	{
		// Import the source model
		UInterchangeSourceData *SourceData = UInterchangeManager::CreateSourceData(ModelPath);
		UE::Interchange::FAssetImportResultRef ImportResult = InterchangeManager.ImportAssetAsync(TEXT("/Game/Assets/WoWExport/Meshes/"), SourceData, ImportParams);

		// Import the corresponding collision model(if it exists)
		UInterchangeSourceData *SourceDataCollision = UInterchangeManager::CreateSourceData(ModelPath.Replace(TEXT(".obj"), TEXT(".phys.obj")));
		UE::Interchange::FAssetImportResultRef ImportResultCollision = InterchangeManager.ImportAssetAsync(TEXT("/Game/Assets/WoWExport/Meshes/"), SourceDataCollision, ImportParams);

		ImportResults.Add(MakeTuple(ModelPath, ImportResult, ImportResultCollision));
	}

	TArray<UStaticMesh *> ImportedModels;
	TMap<FString, MtlData> NewMtls;
	TMap<FString, UTexture2D *> ImportedTextures;
	{
		FScopedSlowTask SlowTask(ImportResults.Num(), LOCTEXT("ImportingModels", "Importing Models..."));
		SlowTask.MakeDialog();

		int ModelIndex = 0;
		for (const TTuple<FString, UE::Interchange::FAssetImportResultRef, UE::Interchange::FAssetImportResultRef> &ImportTuple : ImportResults)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingModel", "Importing Model: {0}"), ModelIndex++));
			const FString &ModelPath = ImportTuple.Get<0>();
			const UE::Interchange::FAssetImportResultRef &ImportResult = ImportTuple.Get<1>();
			const UE::Interchange::FAssetImportResultRef &ImportResultPhys = ImportTuple.Get<2>();

			// Extract data from corresponding json file
			const FString JsonPath = ModelPath.Replace(TEXT(".obj"), TEXT(".json"));
			const TSharedPtr<FJsonObject> JsonObject = LoadJsonObject(JsonPath);
			const JsonData Json = ParseModelJson(JsonObject);

			ImportResult->WaitUntilDone();
			const TArray<UObject *> &ImportedObjects = ImportResult->GetImportedObjects();

			for (UObject *ImportedObject : ImportedObjects)
			{
				if (UStaticMesh *Mesh = Cast<UStaticMesh>(ImportedObject))
				{
					// Set Mesh properties and assign collision mesh (if it exists)
					Mesh = Cast<UStaticMesh>(ImportedObject);
					Mesh->SetLODGroup(FName("LevelArchitecture"), false);
					Mesh->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;

					ImportResultPhys->WaitUntilDone();
					if (ImportResultPhys->GetImportedObjects().Num() > 0)
						Mesh->ComplexCollisionMesh = Cast<UStaticMesh>(ImportResultPhys->GetImportedObjects()[0]);

					bool isInjected = false;

					// Create the material instance(s) for this model and set the instance parameters based on the json data
					for (int i = 0; i < Mesh->GetStaticMaterials().Num(); i++)
					{
						FStaticMaterial &StaticMtl = Mesh->GetStaticMaterials()[i];
						FString MtlName = StaticMtl.MaterialSlotName.ToString();
						TSharedPtr<FJsonObject> MtlObject = Json.MtlNameToMtlObject[MtlName];

						// If the material instance already exists, then we skip this
						if (!NewMtls.Contains(MtlName))
						{
							IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
							UObject *NewAsset = AssetTools.CreateAsset(MtlName, TEXT("/Game/Assets/WoWExport/Meshes/Materials/"), UMaterialInstanceConstant::StaticClass(), NewObject<UMaterialInstanceConstantFactoryNew>());
							MtlData Mtl;
							Mtl.Instance = Cast<UMaterialInstanceConstant>(NewAsset);
							Mtl.Instance->SetParentEditorOnly(ModelMaterial);

							if (Json.FileType == TEXT("wmo"))
							{
								const int Shader = MtlObject->GetIntegerField(TEXT("shader"));
								int BlendMode = MtlObject->GetIntegerField(TEXT("blendMode"));
								bool isEmissive = Shader == 9 || Shader == 12 || Shader == 15;
								Mtl.Instance->BasePropertyOverrides.bOverride_BlendMode = true;
								Mtl.Instance->BasePropertyOverrides.BlendMode = EGxBlendToUE5(BlendMode);
								Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("IsEmissive"), isEmissive);
								Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("IsReflective"), true);

								Mtl.BlendMode = BlendMode;
								Mtl.isM2 = false;

								if (Shader == 23 || Shader == 6 || Shader == 7 || Shader == 13 || Shader == 15 || Shader == 20) // The following shaders have multiple blended textures that require vertex colors
								{
									if (!isInjected)
									{
										InjectVertexColors(Mesh, JsonObject);
										isInjected = true;
									}

									Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("isMultiLayer"), true);
									if (Shader == 23) // MapObjUnkShader
									{
										if (Json.FDIDToTexName.Contains(MtlObject->GetIntegerField(TEXT("texture2"))))
											Mtl.ParamToTexName.Add(TEXT("Texture2"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("texture2"))]);
										if (Json.FDIDToTexName.Contains(MtlObject->GetIntegerField(TEXT("texture3"))))
											Mtl.ParamToTexName.Add(TEXT("Texture3"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("texture3"))]);
										if (Json.FDIDToTexName.Contains(MtlObject->GetIntegerField(TEXT("color3"))))
											Mtl.ParamToTexName.Add(TEXT("Color3"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("color3"))]);
										if (Json.FDIDToTexName.Contains(MtlObject->GetIntegerField(TEXT("flags3"))))
											Mtl.ParamToTexName.Add(TEXT("Flags3"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("flags3"))]);

										TArray<int> HeightFDIDs;
										const TArray<TSharedPtr<FJsonValue>> &RuntimeDataArray = MtlObject->GetArrayField(TEXT("runtimeData"));
										for (const TSharedPtr<FJsonValue> &Val : RuntimeDataArray)
											HeightFDIDs.Add((int)Val->AsNumber());

										for (int j = 0; j < 4; j++)
											if (Json.FDIDToTexName.Contains(HeightFDIDs[j]))
												Mtl.ParamToTexName.Add(FName(*FString::Printf(TEXT("Height%d"), j)), Json.FDIDToTexName[HeightFDIDs[j]]);
									}
									else // TwoLayer Shading
									{
										Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("isTwoLayer"), true);
										Mtl.ParamToTexName.Add(TEXT("Texture1"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("texture1"))]);
										Mtl.ParamToTexName.Add(TEXT("Texture2"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("texture2"))]);
									}
								}
								else
									Mtl.ParamToTexName.Add(TEXT("Texture1"), Json.FDIDToTexName[MtlObject->GetIntegerField(TEXT("texture1"))]);
							}
							else
							{
								TSharedPtr<FJsonObject> TexUnit = Json.MtlNameToTexUnit[MtlName];
								int BlendMode = M2ToEGxBlend(MtlObject->GetIntegerField(TEXT("blendingMode")));
								int MtlFlags = MtlObject->GetIntegerField(TEXT("flags"));
								int TexFlags = TexUnit->GetIntegerField(TEXT("flags"));
								bool isEmissive = (MtlFlags & 0x01) != 0;
								bool isTwoSided = (MtlFlags & 0x04) != 0;
								bool isReflective = (TexFlags & 0x80) != 0;

								Mtl.ParamToTexName.Add(TEXT("Texture1"), TEXT("TEX") + MtlName.Mid(3));
								Mtl.BlendMode = BlendMode;
								Mtl.isM2 = true;

								Mtl.Instance->BasePropertyOverrides.bOverride_BlendMode = true;
								Mtl.Instance->BasePropertyOverrides.BlendMode = EGxBlendToUE5(BlendMode);
								Mtl.Instance->BasePropertyOverrides.bOverride_TwoSided = true;
								Mtl.Instance->BasePropertyOverrides.TwoSided = isTwoSided || isEmissive;
								Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("IsEmissive"), isEmissive);
								Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("IsReflective"), isReflective);

								if (isReflective)
									Mtl.Instance->SetScalarParameterValueEditorOnly(FName("Metallic"), 1.0f);

								if (isTwoSided && BlendMode == 1 && isFoliage)
								{
									Mtl.Instance->BasePropertyOverrides.bOverride_ShadingModel = true;
									Mtl.Instance->BasePropertyOverrides.ShadingModel = EMaterialShadingModel::MSM_TwoSidedFoliage;
									Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("EnableWind"), true);
								}
							}
							NewMtls.Add(MtlName, Mtl);
						}
						StaticMtl.MaterialInterface = NewMtls[MtlName].Instance;
					}
					Mesh->PostEditChange();
					ImportedModels.Add(Mesh);
				}

				if (UTexture2D *Texture = Cast<UTexture2D>(ImportedObject))
					ImportedTextures.Add(Texture->GetName(), Texture);
			}
		}
	}

	{ // We assign textures to material instances when all textures have been imported
		FScopedSlowTask SlowTask(NewMtls.Num(), LOCTEXT("AssigningTextures", "Assigning Textures..."));
		SlowTask.MakeDialog();

		for (auto &MtlPair : NewMtls)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("AssigningTexture", "Assigning Textures for Material: {0}"), FText::FromString(MtlPair.Key)));
			MtlData &Mtl = MtlPair.Value;
			for (auto &ParamPair : Mtl.ParamToTexName)
			{
				UTexture2D *Texture = ImportedTextures[ParamPair.Value];
				Mtl.Instance->SetTextureParameterValueEditorOnly(ParamPair.Key, Texture);

				if (Texture->HasAlphaChannel() && (Mtl.BlendMode != 1 && Mtl.BlendMode != 2 && Mtl.BlendMode != 11))
				{
					Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("isReflective"), true);
					Mtl.Instance->SetScalarParameterValueEditorOnly(FName("Metallic"), 1.0f);
					if (Mtl.isM2)
						Mtl.Instance->SetStaticSwitchParameterValueEditorOnly(FName("InvertAlpha"), true);
				}
				Mtl.Instance->PostEditChange();
			}
		}
	}
	return ImportedModels;
}

JsonData FWoWLandscapeImporterModule::ParseModelJson(const TSharedPtr<FJsonObject> &JsonObject)
{
	JsonData Json;
	Json.FileType = JsonObject->GetStringField(TEXT("fileType"));
	TArray<TSharedPtr<FJsonValue>> MtlArray = JsonObject->GetArrayField(TEXT("materials"));
	TArray<TSharedPtr<FJsonValue>> TexArray = JsonObject->GetArrayField(TEXT("textures"));
	TArray<TSharedPtr<FJsonValue>> TexComboArray = JsonObject->GetArrayField(TEXT("textureCombos"));

	// Build FDID -> Texture Name Lookup
	for (int TexIdx = 0; TexIdx < TexArray.Num(); ++TexIdx)
	{
		TSharedPtr<FJsonObject> TexObject = TexArray[TexIdx]->AsObject();
		FString TexName = TEXT("TEX") + TexObject->GetStringField(TEXT("mtlName")).Mid(3);
		int FDID = TexObject->GetIntegerField(TEXT("fileDataID"));
		Json.FDIDToTexName.Add(FDID, TexName);
	}

	// Build Mtl Name -> Mtl Object Lookup (and Mtl Name -> Tex Unit Object for M2)
	if (Json.FileType == TEXT("m2"))
	{
		TArray<TSharedPtr<FJsonValue>> TexUnitArray = JsonObject->GetObjectField(TEXT("skin"))->GetArrayField(TEXT("textureUnits"));
		for (int UnitIdx = 0; UnitIdx < TexUnitArray.Num(); ++UnitIdx)
		{
			TSharedPtr<FJsonObject> UnitObject = TexUnitArray[UnitIdx]->AsObject();
			int MtlIndex = UnitObject->GetNumberField(TEXT("materialIndex"));
			int Flags = UnitObject->GetNumberField(TEXT("flags"));
			int TextureIndex = TexComboArray[UnitObject->GetNumberField(TEXT("textureComboIndex"))]->AsNumber();
			FString MtlName = TexArray[TextureIndex]->AsObject()->GetStringField(TEXT("mtlName"));

			if (!Json.MtlNameToMtlObject.Contains(MtlName) || (Flags & 0x80) != 0) // First come, first served with exception of 0x80 flag
			{
				Json.MtlNameToMtlObject.Add(MtlName, MtlArray[MtlIndex]->AsObject());
				Json.MtlNameToTexUnit.Add(MtlName, UnitObject);
			}
		}
	}
	else
	{
		for (int MtlIndex = 0; MtlIndex < MtlArray.Num(); ++MtlIndex)
		{
			TSharedPtr<FJsonObject> MtlObject = MtlArray[MtlIndex]->AsObject();
			int Shader = MtlObject->GetIntegerField(TEXT("shader"));

			// shader 23 (pixelShader 20) uses texture2 as the MTL material reference in blender script
			int FDID = 0;
			if (Shader == 23)
				FDID = MtlObject->GetIntegerField(TEXT("texture2"));
			else
				FDID = MtlObject->GetIntegerField(TEXT("texture1"));

			for (int TexIdx = 0; TexIdx < TexArray.Num(); ++TexIdx)
			{
				TSharedPtr<FJsonObject> TextureObject = TexArray[TexIdx]->AsObject();
				if (TextureObject->GetIntegerField(TEXT("fileDataID")) == FDID)
				{
					FString MtlName = TextureObject->GetStringField(TEXT("mtlName"));
					if (!Json.MtlNameToMtlObject.Contains(MtlName) || Shader == 23 || Shader == 13) // First come, first served with exception of shader 23
						Json.MtlNameToMtlObject.Add(MtlName, MtlObject);
					break;
				}
			}
		}
	}
	return Json;
}

void FWoWLandscapeImporterModule::InjectVertexColors(UStaticMesh *Mesh, const TSharedPtr<FJsonObject> &JsonObject)
{
	TArray<FVector4f> AllVertexColors;
	const TArray<TSharedPtr<FJsonValue>> &GroupsArray = JsonObject->GetArrayField(TEXT("groups"));

	for (const TSharedPtr<FJsonValue> &GroupVal : GroupsArray)
	{
		TSharedPtr<FJsonObject> GroupObj = GroupVal->AsObject();

		const TArray<TSharedPtr<FJsonValue>> *ColorArray = nullptr;
		if (!GroupObj->TryGetArrayField(TEXT("colors2"), ColorArray))
		{
			const TArray<TSharedPtr<FJsonValue>> *OuterArray = nullptr;
			if (GroupObj->TryGetArrayField(TEXT("vertexColours"), OuterArray) && OuterArray->Num() > 0)
				ColorArray = &((*OuterArray).Last()->AsArray()); // Get the last vertex color array as this is used for TwoLayerShader
		}
		if (!ColorArray)
			continue;

		for (int c = 0; c < ColorArray->Num(); c += 4)
		{
			float B = (*ColorArray)[c]->AsNumber() / 255.0f;
			float G = (*ColorArray)[c + 1]->AsNumber() / 255.0f;
			float R = (*ColorArray)[c + 2]->AsNumber() / 255.0f;
			float A = (*ColorArray)[c + 3]->AsNumber() / 255.0f;
			AllVertexColors.Add(FVector4f(R, G, B, A));
		}
	}

	FMeshDescription *MeshDescription = Mesh->GetMeshDescription(0);
	FStaticMeshAttributes Attributes(*MeshDescription);

	Attributes.Register();
	TVertexInstanceAttributesRef<FVector4f> InstanceColors = Attributes.GetVertexInstanceColors();
	MeshDescription->VertexInstanceAttributes().RegisterAttribute<FVector4f>(MeshAttribute::VertexInstance::Color, 1, FVector4f(1.0f, 1.0f, 1.0f, 1.0f));
	InstanceColors = Attributes.GetVertexInstanceColors();

	for (const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
	{
		FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);
		if (VertexID.GetValue() < AllVertexColors.Num())
			InstanceColors.Set(VertexInstanceID, AllVertexColors[VertexID.GetValue()]);
	}
	Mesh->CommitMeshDescription(0);
}

int FWoWLandscapeImporterModule::M2ToEGxBlend(const int BlendingMode)
{
	switch (BlendingMode)
	{
	case 0: return 0;  // Opaque
	case 1: return 1;  // Alpha key
	case 2: return 2;  // Alpha
	case 3: return 10; // No Alpha Additive
	case 4: return 3;  // Additive
	case 5: return 4;  // Modulate
	case 6: return 5;  // Modulate2x
	case 7: return 13; // BlendAdd
	default: return 0;
	}
}

EBlendMode FWoWLandscapeImporterModule::EGxBlendToUE5(int BlendMode)
{
	switch (BlendMode)
	{
	case 0: return EBlendMode::BLEND_Opaque;
	case 1: return EBlendMode::BLEND_Masked;
	case 2: return EBlendMode::BLEND_Translucent;
	case 3: return EBlendMode::BLEND_Additive;
	case 4: return EBlendMode::BLEND_Modulate;
	case 5: return EBlendMode::BLEND_Modulate;
	case 6: return EBlendMode::BLEND_Modulate;
	case 7: return EBlendMode::BLEND_Additive;
	case 8: return EBlendMode::BLEND_Opaque;
	case 9: return EBlendMode::BLEND_Opaque;
	case 10: return EBlendMode::BLEND_Additive;
	case 11: return EBlendMode::BLEND_Translucent;
	case 13: return EBlendMode::BLEND_AlphaComposite;
	default: return EBlendMode::BLEND_Opaque;
	}
}

TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> FWoWLandscapeImporterModule::CreateProxyData(const int StartRow, const int StartColumn)
{
	// Height and width of proxy in vertices(pixels)
	const int ProxyHeight = 511;
	const int ProxyWidth = 511;
	TArray<uint16> Heightmap;
	Heightmap.SetNumZeroed(ProxyWidth * ProxyHeight);
	TMap<FName, FLandscapeImportLayerInfo> LayerInfoMap;

	int CurrentRow = StartRow;
	int TileY = 0;
	for (int ProxyY = 0; ProxyY < ProxyHeight; ProxyY++)
	{
		if (TileY == 256)
		{
			CurrentRow++;
			TileY = 1; // We skip first row/column of subsequent tiles to avoid overlapping vertices(heightmaps share borders)
		}

		int ChunkY = TileY / 16; // All heightmaps/alphamaps are now 256x256, so we dont need to worry about the special case for the first 127 pixels and can just do an integer division to get the chunk index.
		int CurrentColumn = StartColumn;
		int TileX = 0;
		for (int ProxyX = 0; ProxyX < ProxyWidth; ProxyX++)
		{
			if (TileX == 256)
			{
				CurrentColumn++;
				TileX = 1;
			}

			int ProxyIndex = ProxyY * ProxyWidth + ProxyX;
			if (CurrentRow >= TileGrid.Num() || CurrentColumn >= TileGrid[0].Num() || TileGrid[CurrentRow][CurrentColumn].HeightmapData.Num() == 0)
			{
				TileX++;
				continue; // No heightmap data for this tile, so we can just leave it as 0
			}

			Tile &CurrentTile = TileGrid[CurrentRow][CurrentColumn];
			int TileIndex = TileY * 256 + TileX;
			Heightmap[ProxyIndex] = CurrentTile.HeightmapData[TileIndex];

			int ChunkX = TileX / 16;
			int ChunkIndex = ChunkY * 16 + ChunkX;
			// Calculate the weight for each layer based on the pixel data
			for (int LayerIndex = 0; LayerIndex < CurrentTile.Chunks[ChunkIndex].Layers.Num(); LayerIndex++)
			{
				const Layer &CurrentLayer = CurrentTile.Chunks[ChunkIndex].Layers[LayerIndex];
				FColor Pixel = TileGrid[CurrentRow][CurrentColumn].AlphamapPNGs[CurrentLayer.ImageIndex][TileIndex];

				LayerMetadata *LayerMetadata = LayerMetadataMap.Find(CurrentLayer.LayerName);
				FLandscapeImportLayerInfo &ImportLayerInfo = LayerInfoMap.FindOrAdd(CurrentLayer.LayerName);
				if (ImportLayerInfo.LayerData.Num() == 0)
				{
					ImportLayerInfo.LayerData.SetNumZeroed(ProxyWidth * ProxyHeight);
					ImportLayerInfo.LayerInfo = LayerMetadata->LayerInfo;
					ImportLayerInfo.LayerName = LayerMetadata->LayerInfo->LayerName;
				}

				if (CurrentLayer.ChannelIndex == -1) // Base Layer
				{
					int TotalWeight = 0;
					for (int OtherLayerIndex = 0; OtherLayerIndex < CurrentTile.Chunks[ChunkIndex].Layers.Num(); OtherLayerIndex++)
					{
						const Layer &OtherLayer = CurrentTile.Chunks[ChunkIndex].Layers[OtherLayerIndex];
						if (OtherLayer.ChannelIndex != -1)
						{
							int OtherPNG = OtherLayer.ImageIndex;
							FColor OtherPixel = TileGrid[CurrentRow][CurrentColumn].AlphamapPNGs[OtherPNG][TileIndex];
							switch (OtherLayer.ChannelIndex)
							{
							case 0: TotalWeight += OtherPixel.R; break;
							case 1: TotalWeight += OtherPixel.G; break;
							case 2: TotalWeight += OtherPixel.B; break;
							case 3: TotalWeight += OtherPixel.A; break;
							}
						}
					}
					ImportLayerInfo.LayerData[ProxyIndex] = 255 - TotalWeight;
				}
				else
				{
					switch (CurrentLayer.ChannelIndex)
					{
					case 0: ImportLayerInfo.LayerData[ProxyIndex] = Pixel.R; break;
					case 1: ImportLayerInfo.LayerData[ProxyIndex] = Pixel.G; break;
					case 2: ImportLayerInfo.LayerData[ProxyIndex] = Pixel.B; break;
					case 3: ImportLayerInfo.LayerData[ProxyIndex] = Pixel.A; break;
					}
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

UMaterial *FWoWLandscapeImporterModule::CreateModelMaterial(const FString MaterialName)
{
	const FString MaterialDirectory = TEXT("/Game/Assets/WoWExport/Materials/");
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.CreateAsset(MaterialName, MaterialDirectory, UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>());

	const FString MaterialPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialName);
	UMaterial *ModelMaterial = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPackagePath));
	ModelMaterial->OpacityMaskClipValue = 0.5f;

	int Section0 = 700;
	int Section1 = 1200;
	UMaterialExpressionVertexColor *VertexColorNode = CreateNode(NewObject<UMaterialExpressionVertexColor>(ModelMaterial), -1200 - Section1, -200, ModelMaterial);
	UMaterialExpressionTextureSampleParameter2D *TextureSample0 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 0, ModelMaterial);
	TextureSample0->ParameterName = FName("Texture1");
	UMaterialExpressionTextureSampleParameter2D *TextureSample1 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 250, ModelMaterial);
	TextureSample1->ParameterName = FName("Texture2");
	UMaterialExpressionTextureSampleParameter2D *TextureSample2 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 500, ModelMaterial);
	TextureSample2->ParameterName = FName("Texture3");
	UMaterialExpressionTextureSampleParameter2D *TextureSample3 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 750, ModelMaterial);
	TextureSample3->ParameterName = FName("Color3");
	UMaterialExpressionTextureSampleParameter2D *TextureSample4 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 1000, ModelMaterial);
	TextureSample4->ParameterName = FName("Flags3");
	UMaterialExpressionTextureSampleParameter2D *HeightSample0 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 1250, ModelMaterial);
	HeightSample0->ParameterName = FName("Height0");
	UMaterialExpressionTextureSampleParameter2D *HeightSample1 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 1500, ModelMaterial);
	HeightSample1->ParameterName = FName("Height1");
	UMaterialExpressionTextureSampleParameter2D *HeightSample2 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 1750, ModelMaterial);
	HeightSample2->ParameterName = FName("Height2");
	UMaterialExpressionTextureSampleParameter2D *HeightSample3 = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2D>(ModelMaterial), -1200 - Section1, 2000, ModelMaterial);
	HeightSample3->ParameterName = FName("Height3");

	UMaterialExpressionConstant *ZeroConstant = CreateNode(NewObject<UMaterialExpressionConstant>(ModelMaterial), -900, 800, ModelMaterial);
	ZeroConstant->R = 0.0f;

	auto AddInput = [&](UMaterialExpressionCustom *CustomNode, const char *Name, UMaterialExpression *Expression, int OutputIndex = 0)
	{
		FCustomInput &Input = CustomNode->Inputs.AddDefaulted_GetRef();
		Input.InputName = FName(Name);
		Input.Input.Expression = Expression;
		Input.Input.OutputIndex = OutputIndex;
	};
	auto CreateStaticSwitch = [&](const FName &ParamName, UMaterialExpression *TrueExp, int TrueIdx, UMaterialExpression *FalseExp, int FalseIdx, int X, int Y, bool bDefault) -> UMaterialExpressionStaticSwitchParameter *
	{
		UMaterialExpressionStaticSwitchParameter *Switch = CreateNode(NewObject<UMaterialExpressionStaticSwitchParameter>(ModelMaterial), X, Y, ModelMaterial);
		Switch->ParameterName = ParamName;
		Switch->DefaultValue = bDefault;
		if (TrueExp)
		{
			Switch->A.Expression = TrueExp;
			Switch->A.OutputIndex = TrueIdx;
		}
		if (FalseExp)
		{
			Switch->B.Expression = FalseExp;
			Switch->B.OutputIndex = FalseIdx;
		}
		return Switch;
	};

	// -- Foliage --
	UClass *PerInstanceFadeAmountClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionPerInstanceFadeAmount"));
	UMaterialExpression *PerInstanceFadeAmount = CreateNode(Cast<UMaterialExpression>(NewObject<UObject>(ModelMaterial, PerInstanceFadeAmountClass)), -1100, Section0 + 370, ModelMaterial);

	UMaterialExpressionMaterialFunctionCall *DitherTemporalAA = CreateNode(NewObject<UMaterialExpressionMaterialFunctionCall>(ModelMaterial), -1100, Section0 + 450, ModelMaterial);
	UMaterialFunction *DitherFunction = LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"));
	DitherTemporalAA->MaterialFunction = DitherFunction;
	DitherTemporalAA->UpdateFromFunctionResource();
	DitherTemporalAA->GetInput(0)->Expression = PerInstanceFadeAmount;

	UMaterialExpressionMultiply *OpacityMultiply = CreateNode(NewObject<UMaterialExpressionMultiply>(ModelMaterial), -400, Section0 + 400, ModelMaterial);
	OpacityMultiply->B.Expression = DitherTemporalAA;

	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_OpacityMask)->Expression = OpacityMultiply;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_OpacityMask)->OutputIndex = 0;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Opacity)->Expression = OpacityMultiply;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Opacity)->OutputIndex = 0;

	UMaterialExpressionMaterialFunctionCall *GrassWindNode = CreateNode(NewObject<UMaterialExpressionMaterialFunctionCall>(ModelMaterial), -800, Section0 + 650, ModelMaterial);
	GrassWindNode->MaterialFunction = LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions01/WorldPositionOffset/SimpleGrassWind.SimpleGrassWind"));
	GrassWindNode->UpdateFromFunctionResource();

	UMaterialExpressionScalarParameter *WindIntensityParam = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -1100, Section0 + 550, ModelMaterial);
	WindIntensityParam->ParameterName = FName("WindIntensity");
	WindIntensityParam->DefaultValue = 0.8f;
	UMaterialExpressionScalarParameter *WindWeightParam = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -1100, Section0 + 650, ModelMaterial);
	WindWeightParam->ParameterName = FName("WindWeight");
	WindWeightParam->DefaultValue = 0.3f;
	UMaterialExpressionScalarParameter *WindSpeedParam = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -1100, Section0 + 750, ModelMaterial);
	WindSpeedParam->ParameterName = FName("WindSpeed");
	WindSpeedParam->DefaultValue = 0.2f;

	GrassWindNode->GetInput(0)->Expression = WindIntensityParam;
	GrassWindNode->GetInput(1)->Expression = WindWeightParam;
	GrassWindNode->GetInput(2)->Expression = WindSpeedParam;

	// Rolling wind logic (Compact Custom Node)
	UMaterialExpressionCustom *CustomWind = CreateNode(NewObject<UMaterialExpressionCustom>(ModelMaterial), -800, Section0 + 950, ModelMaterial);
	CustomWind->Description = TEXT("Rolling Waves");
	CustomWind->Inputs.Empty();
	GrassWindNode->GetInput(3)->Expression = CustomWind;
	GrassWindNode->GetInput(3)->OutputIndex = 0;
	AddInput(CustomWind, "WorldPos", CreateNode(NewObject<UMaterialExpressionWorldPosition>(ModelMaterial), -1100, Section0 + 850, ModelMaterial));
	UClass *ObjectPosClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionObjectPositionWS"));
	AddInput(CustomWind, "ObjectPos", CreateNode(Cast<UMaterialExpression>(NewObject<UObject>(ModelMaterial, ObjectPosClass)), -1100, Section0 + 1000, ModelMaterial));
	auto *Time = CreateNode(NewObject<UMaterialExpressionTime>(ModelMaterial), -1100, Section0 + 1100, ModelMaterial);
	AddInput(CustomWind, "Time", Time);
	auto *Speed = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -1100, Section0 + 1200, ModelMaterial);
	Speed->ParameterName = "WindWaveSpeed";
	Speed->DefaultValue = 1.5f;
	AddInput(CustomWind, "Speed", Speed);
	auto *Freq = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -1100, Section0 + 1300, ModelMaterial);
	Freq->ParameterName = "WindWaveFrequency";
	Freq->DefaultValue = 0.001f;
	AddInput(CustomWind, "Frequency", Freq);
	auto *Amount = CreateNode(NewObject<UMaterialExpressionVectorParameter>(ModelMaterial), -1100, Section0 + 1400, ModelMaterial);
	Amount->ParameterName = "WindWaveAmount";
	Amount->DefaultValue = FLinearColor(10.0f, 10.0f, 0.0f, 0.0f);
	AddInput(CustomWind, "Amount", Amount);
	auto *Scale = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -1100, Section0 + 1600, ModelMaterial);
	Scale->ParameterName = "WindHeightScale";
	Scale->DefaultValue = 0.005f;
	AddInput(CustomWind, "HeightScale", Scale);

	CustomWind->Code = TEXT("float Phase = (WorldPos.x + WorldPos.y) * Frequency + Time * Speed;\n"
							"return float3(sin(Phase) * Amount.x, cos(Phase * 0.85) * Amount.y, 0) * (WorldPos.z - ObjectPos.z) * HeightScale;");

	UMaterialExpressionStaticSwitchParameter *WindSwitch = CreateNode(NewObject<UMaterialExpressionStaticSwitchParameter>(ModelMaterial), -450, Section0 + 650, ModelMaterial);
	WindSwitch->ParameterName = FName("EnableWind");
	WindSwitch->DefaultValue = false;
	WindSwitch->A.Expression = GrassWindNode;
	WindSwitch->B.Expression = ZeroConstant;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_WorldPositionOffset)->Expression = WindSwitch;

	// -- WMO Shader 20 --
	UMaterialExpressionCustom *Shader20Blend = CreateNode(NewObject<UMaterialExpressionCustom>(ModelMaterial), -400 - Section1, 400, ModelMaterial);
	Shader20Blend->Description = TEXT("WMOShader20_Blend");
	Shader20Blend->OutputType = CMOT_Float4;
	Shader20Blend->Inputs.Empty();
	AddInput(Shader20Blend, "Weights", VertexColorNode);
	AddInput(Shader20Blend, "D0", TextureSample1, 5);
	AddInput(Shader20Blend, "D1", TextureSample2, 5);
	AddInput(Shader20Blend, "D2", TextureSample3, 5);
	AddInput(Shader20Blend, "D3", TextureSample4, 5);
	AddInput(Shader20Blend, "H0", HeightSample0, 4);
	AddInput(Shader20Blend, "H1", HeightSample1, 4);
	AddInput(Shader20Blend, "H2", HeightSample2, 4);
	AddInput(Shader20Blend, "H3", HeightSample3, 4);
	Shader20Blend->PostEditChange();

	// Implementation of height-suppression blend
	Shader20Blend->Code = TEXT(
		"float weights[4];\n"
		"weights[0] = Weights.r; // Texture2 = Red channel\n"
		"weights[1] = Weights.g; // Texture3 = Green channel\n"
		"weights[2] = Weights.b; // Color3 = Blue channel\n"
		"weights[3] = saturate(1.0 - (weights[0] + weights[1] + weights[2])); // Flags3 = Base\n"
		"\n"
		"float heights[4] = {H0, H1, H2, H3};\n"
		"float4 diff[4] = {D0, D1, D2, D3};\n"
		"\n"
		"float alphas[4];\n"
		"float maxAlpha = 0.0;\n"
		"for(int i = 0; i < 4; i++) {\n"
		"    alphas[i] = weights[i] * max(heights[i], 0.004);\n"
		"    maxAlpha = max(maxAlpha, alphas[i]);\n"
		"}\n"
		"\n"
		"float4 result = float4(0,0,0,0);\n"
		"float totalWeight = 0.0;\n"
		"for(int i = 0; i < 4; i++) {\n"
		"    float supp = (1.0 - saturate(maxAlpha - alphas[i])) * alphas[i];\n"
		"    result += diff[i] * supp;\n"
		"    totalWeight += supp;\n"
		"}\n"
		"\n"
		"return result / max(totalWeight, 0.0001);");

	// -- WMO TwoLayer --
	UMaterialExpressionLinearInterpolate *TwoLayerLerp = CreateNode(NewObject<UMaterialExpressionLinearInterpolate>(ModelMaterial), -400 - Section1, 200, ModelMaterial);
	TwoLayerLerp->A.Expression = TextureSample1;
	TwoLayerLerp->A.OutputIndex = 5;
	TwoLayerLerp->B.Expression = TextureSample0;
	TwoLayerLerp->B.OutputIndex = 5;
	TwoLayerLerp->Alpha.Expression = VertexColorNode;
	TwoLayerLerp->Alpha.OutputIndex = 4;

	// Shader switches for controlling output of WMO/M2 materials
	UMaterialExpressionStaticSwitchParameter *isTwoLayerSwitch = CreateStaticSwitch(FName("isTwoLayer"), TwoLayerLerp, 0, Shader20Blend, 0, -1150, 200, false);
	UMaterialExpressionStaticSwitchParameter *isMultiLayerSwitch = CreateStaticSwitch(FName("isMultiLayer"), isTwoLayerSwitch, 0, TextureSample0, 5, -1150, 400, false);
	UMaterialExpressionComponentMask *AlphaMask = CreateNode(NewObject<UMaterialExpressionComponentMask>(ModelMaterial), -900, 325, ModelMaterial);
	AlphaMask->Input.Expression = isMultiLayerSwitch;
	AlphaMask->R = false;
	AlphaMask->G = false;
	AlphaMask->B = false;
	AlphaMask->A = true;
	OpacityMultiply->A.Expression = AlphaMask;
	UMaterialExpressionComponentMask *BaseMask = CreateNode(NewObject<UMaterialExpressionComponentMask>(ModelMaterial), -900, 475, ModelMaterial);
	BaseMask->Input.Expression = isMultiLayerSwitch;
	BaseMask->R = true;
	BaseMask->G = true;
	BaseMask->B = true;
	BaseMask->A = false;
	UMaterialExpressionStaticSwitchParameter *isEmissive = CreateStaticSwitch(FName("isEmissive"), BaseMask, 0, ZeroConstant, 0, -450, 475, false);
	UMaterialExpressionOneMinus *MinusTexAlpha = CreateNode(NewObject<UMaterialExpressionOneMinus>(ModelMaterial), -775, 325, ModelMaterial);
	MinusTexAlpha->Input.Expression = AlphaMask;
	UMaterialExpressionStaticSwitchParameter *InvTexAlpha = CreateStaticSwitch(FName("InvertAlpha"), MinusTexAlpha, 0, AlphaMask, 0, -675, 325, false);
	UMaterialExpressionStaticSwitchParameter *isReflective = CreateStaticSwitch(FName("isReflective"), InvTexAlpha, 0, ZeroConstant, 0, -450, 325, false);

	// Parameters for Metallic/Specular control
	UMaterialExpressionScalarParameter *MetallicParameter = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -450, 0, ModelMaterial);
	MetallicParameter->ParameterName = FName("Metallic");
	MetallicParameter->DefaultValue = 0.0f;
	UMaterialExpressionScalarParameter *SpecularParameter = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -450, 100, ModelMaterial);
	SpecularParameter->ParameterName = FName("Specular");
	SpecularParameter->DefaultValue = 0.0f;
	UMaterialExpressionScalarParameter *RoughnessParameter = CreateNode(NewObject<UMaterialExpressionScalarParameter>(ModelMaterial), -450, 200, ModelMaterial);
	RoughnessParameter->ParameterName = FName("Roughness");
	RoughnessParameter->DefaultValue = 0.2f;
	UMaterialExpressionMultiply *MetallicMultiply = CreateNode(NewObject<UMaterialExpressionMultiply>(ModelMaterial), -200, 0, ModelMaterial);
	MetallicMultiply->A.Expression = isReflective;
	MetallicMultiply->B.Expression = MetallicParameter;
	UMaterialExpressionMultiply *SpecularMultiply = CreateNode(NewObject<UMaterialExpressionMultiply>(ModelMaterial), -200, 100, ModelMaterial);
	SpecularMultiply->A.Expression = isReflective;
	SpecularMultiply->B.Expression = SpecularParameter;

	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_BaseColor)->Expression = BaseMask;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Metallic)->Expression = MetallicMultiply;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Specular)->Expression = SpecularMultiply;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_Roughness)->Expression = RoughnessParameter;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_EmissiveColor)->Expression = isEmissive;
	ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_SubsurfaceColor)->Expression = BaseMask;

	ModelMaterial->MarkPackageDirty();
	ModelMaterial->PostEditChange();

	return ModelMaterial;
}

void FWoWLandscapeImporterModule::CreateLandscapeMaterial(ALandscape *Landscape)
{
	const FString BaseName = FPaths::GetCleanFilename(DirectoryPath);
	const FString MaterialDirectory = FString::Printf(TEXT("/Game/Assets/WoWExport/Materials/%s"), *BaseName);
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const FString RVTName = FString::Printf(TEXT("RVT_%s"), *BaseName);
	const FString RVTPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *RVTName);
	AssetTools.CreateAsset(RVTName, MaterialDirectory, URuntimeVirtualTexture::StaticClass(), nullptr);

	URuntimeVirtualTexture *RVTAsset = Cast<URuntimeVirtualTexture>(UEditorAssetLibrary::LoadAsset(RVTPackagePath));
	Landscape->RuntimeVirtualTextures.Add(RVTAsset);

	ARuntimeVirtualTextureVolume *RVTVolume = Landscape->GetWorld()->SpawnActor<ARuntimeVirtualTextureVolume>();
	RVTVolume->SetActorLabel(FString::Printf(TEXT("RVT_Volume_%s"), *BaseName));
	URuntimeVirtualTextureComponent *RVTComp = RVTVolume->GetComponentByClass<URuntimeVirtualTextureComponent>();
	RVTComp->SetVirtualTexture(RVTAsset);

	// Calculate the bounding box of the landscape by iterating through its proxies and set the RVT volume location and scale accordingly
	FBox LandscapeBox(ForceInit);
	ULandscapeInfo *LandscapeInfo = Landscape->GetLandscapeInfo();
	LandscapeInfo->ForEachLandscapeProxy([&LandscapeBox](ALandscapeProxy *Proxy)
										 {
		LandscapeBox += Proxy->GetComponentsBoundingBox(true);
		return true; });
	RVTVolume->SetActorLocation(FVector(LandscapeBox.Min.X, LandscapeBox.Min.Y, LandscapeBox.Min.Z));
	RVTVolume->SetActorScale3D(LandscapeBox.GetSize());

	const FString MaterialName = FString::Printf(TEXT("M_%s"), *BaseName);
	const FString MaterialPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialName);
	AssetTools.CreateAsset(MaterialName, MaterialDirectory, UMaterial::StaticClass(), nullptr);
	UMaterial *LandscapeMaterial = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(MaterialPackagePath));
	LandscapeMaterial->bUseMaterialAttributes = true;

	FString TexArrayName = FString::Printf(TEXT("TEX_%s_Array_256"), *BaseName);
	FString TexArrayPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *TexArrayName);
	AssetTools.CreateAsset(TexArrayName, MaterialDirectory, UTexture2DArray::StaticClass(), nullptr);
	UTexture2DArray *TexArray256Asset = Cast<UTexture2DArray>(UEditorAssetLibrary::LoadAsset(TexArrayPackagePath));

	TexArrayName = FString::Printf(TEXT("TEX_%s_Array_512"), *BaseName);
	TexArrayPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *TexArrayName);
	AssetTools.CreateAsset(TexArrayName, MaterialDirectory, UTexture2DArray::StaticClass(), nullptr);
	UTexture2DArray *TexArray512Asset = Cast<UTexture2DArray>(UEditorAssetLibrary::LoadAsset(TexArrayPackagePath));

	TexArrayName = FString::Printf(TEXT("TEX_%s_Array_1024"), *BaseName);
	TexArrayPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *TexArrayName);
	AssetTools.CreateAsset(TexArrayName, MaterialDirectory, UTexture2DArray::StaticClass(), nullptr);
	UTexture2DArray *TexArray1024Asset = Cast<UTexture2DArray>(UEditorAssetLibrary::LoadAsset(TexArrayPackagePath));

	int Section0 = -4300;
	UMaterialExpressionLandscapeLayerCoords *CoordsNode = CreateNode(NewObject<UMaterialExpressionLandscapeLayerCoords>(LandscapeMaterial), Section0, 0, LandscapeMaterial);

	UMaterialExpressionScalarParameter *NearTilingSize = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section0, 150, LandscapeMaterial);
	NearTilingSize->ParameterName = FName("NearTilingSize");
	NearTilingSize->Group = FName("DistanceBlend");
	NearTilingSize->DefaultValue = 5.0f;
	UMaterialExpressionScalarParameter *FarTilingSize = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section0, 250, LandscapeMaterial);
	FarTilingSize->ParameterName = FName("FarTilingSize");
	FarTilingSize->Group = FName("DistanceBlend");
	FarTilingSize->DefaultValue = 30.0f;

	UMaterialExpressionDivide *DivideNodeNear = CreateNode(NewObject<UMaterialExpressionDivide>(LandscapeMaterial), Section0 + 300, 0, LandscapeMaterial);
	DivideNodeNear->A.Expression = CoordsNode;
	DivideNodeNear->B.Expression = NearTilingSize;
	UMaterialExpressionDivide *DivideNodeFar = CreateNode(NewObject<UMaterialExpressionDivide>(LandscapeMaterial), Section0 + 300, 100, LandscapeMaterial);
	DivideNodeFar->A.Expression = CoordsNode;
	DivideNodeFar->B.Expression = FarTilingSize;

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

	int Section1 = -2800;
	UMaterialExpressionLandscapeLayerBlend *LayerBlendNode = CreateNode(NewObject<UMaterialExpressionLandscapeLayerBlend>(LandscapeMaterial), Section1 + 1400, 0, LandscapeMaterial);

	UMaterialExpressionScalarParameter *BlackValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section1 + 1000, -250, LandscapeMaterial);
	BlackValueNode->ParameterName = FName("BlackValue");
	BlackValueNode->Group = FName("3PointLevels");
	BlackValueNode->DefaultValue = 0.0f;
	UMaterialExpressionScalarParameter *GrayValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial), Section1 + 1000, -175, LandscapeMaterial);
	GrayValueNode->ParameterName = FName("GrayValue");
	GrayValueNode->Group = FName("3PointLevels");
	GrayValueNode->DefaultValue = 0.5f;
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

	UMaterialExpressionLandscapeGrassOutput *GrassOutputNode = CreateNode(NewObject<UMaterialExpressionLandscapeGrassOutput>(LandscapeMaterial), Section0 + 500, 900, LandscapeMaterial);
	GrassOutputNode->GrassTypes.RemoveAt(0);

	// Loop through our stored layer data to create and connect texture samplers
	int NodeOffsetY = 0;
	int TexArrayIndex256 = 0, TexArrayIndex512 = 0, TexArrayIndex1024 = 0;
	for (auto const &[LayerName, LayerMetadata] : LayerMetadataMap)
	{
		if (LayerMetadata.FoliageAsset)
		{
			UMaterialExpressionLandscapeLayerSample *LayerSample = CreateNode(NewObject<UMaterialExpressionLandscapeLayerSample>(LandscapeMaterial), Section0, 900 + (GrassOutputNode->GrassTypes.Num() * 130), LandscapeMaterial);
			LayerSample->ParameterName = LayerName;

			UMaterialExpressionSmoothStep *SmoothStepNode = CreateNode(NewObject<UMaterialExpressionSmoothStep>(LandscapeMaterial), Section0 + 250, 900 + (GrassOutputNode->GrassTypes.Num() * 130), LandscapeMaterial);
			SmoothStepNode->Value.Expression = LayerSample;
			SmoothStepNode->ConstMin = 0.4f;
			SmoothStepNode->ConstMax = 1.0f;

			FGrassInput GrassInput;
			GrassInput.Name = LayerName;
			GrassInput.GrassType = LayerMetadata.FoliageAsset;
			GrassInput.Input.Expression = SmoothStepNode;
			GrassOutputNode->GrassTypes.Add(GrassInput);
		}

		UTexture2D *LayerTex = LayerMetadata.LayerTexture.Get();
		UTexture2D *LayerTexHeight = LayerMetadata.LayerTextureHeight.Get();
		if (!(LayerTex->GetSizeX() == 256 || LayerTex->GetSizeX() == 512 || LayerTex->GetSizeX() == 1024)) continue;

		UMaterialExpressionNamedRerouteUsage *NearRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY, LandscapeMaterial);
		NearRerouteUsage->Declaration = NearReroute;
		UMaterialExpressionNamedRerouteUsage *FarRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY + 75, LandscapeMaterial);
		FarRerouteUsage->Declaration = FarReroute;
		UMaterialExpressionNamedRerouteUsage *DepthFadeRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY + 150, LandscapeMaterial);
		DepthFadeRerouteUsage->Declaration = DepthFadeReroute;

		UMaterialExpressionLinearInterpolate *LerpUV = CreateNode(NewObject<UMaterialExpressionLinearInterpolate>(LandscapeMaterial), Section1 + 150, NodeOffsetY, LandscapeMaterial);
		LerpUV->A.Expression = NearRerouteUsage;
		LerpUV->B.Expression = FarRerouteUsage;
		LerpUV->Alpha.Expression = DepthFadeRerouteUsage;

		UMaterialExpressionConstant *ArrayIndexConstant = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section1 + 180, NodeOffsetY + 150, LandscapeMaterial);
		UMaterialExpressionConstant *ArrayIndexConstantHeight = nullptr;
		UMaterialExpressionAppendVector *AppendUVIndex = CreateNode(NewObject<UMaterialExpressionAppendVector>(LandscapeMaterial), Section1 + 290, NodeOffsetY, LandscapeMaterial);
		AppendUVIndex->A.Expression = LerpUV;
		AppendUVIndex->B.Expression = ArrayIndexConstant;

		UMaterialExpressionTextureSampleParameter2DArray *TexSampleArray = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2DArray>(LandscapeMaterial), Section1 + 410, NodeOffsetY, LandscapeMaterial);
		TexSampleArray->SamplerSource = ESamplerSourceMode::SSM_Wrap_WorldGroupSettings;
		TexSampleArray->Coordinates.Expression = AppendUVIndex;

		UMaterialExpressionTextureSampleParameter2DArray *TexSampleHeightArray = nullptr;
		UMaterialExpressionMaterialFunctionCall *LevelsNode = nullptr;
		if (LayerTexHeight)
		{
			ArrayIndexConstantHeight = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section1 + 180, NodeOffsetY + 350, LandscapeMaterial);
			UMaterialExpressionAppendVector *AppendUVIndexHeight = CreateNode(NewObject<UMaterialExpressionAppendVector>(LandscapeMaterial), Section1 + 290, NodeOffsetY + 250, LandscapeMaterial);
			AppendUVIndexHeight->A.Expression = LerpUV;
			AppendUVIndexHeight->B.Expression = ArrayIndexConstantHeight;

			TexSampleHeightArray = CreateNode(NewObject<UMaterialExpressionTextureSampleParameter2DArray>(LandscapeMaterial), Section1 + 410, NodeOffsetY + 250, LandscapeMaterial);
			TexSampleHeightArray->SamplerSource = ESamplerSourceMode::SSM_Wrap_WorldGroupSettings;
			TexSampleHeightArray->Coordinates.Expression = AppendUVIndexHeight;

			UMaterialFunction *ThreePointLevelsFunc = LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions02/3PointLevels.3PointLevels"));
			LevelsNode = CreateNode(NewObject<UMaterialExpressionMaterialFunctionCall>(LandscapeMaterial), Section1 + 1050, NodeOffsetY, LandscapeMaterial);
			LevelsNode->MaterialFunction = ThreePointLevelsFunc;
			LevelsNode->UpdateFromFunctionResource();
			LevelsNode->GetInput(0)->Expression = TexSampleHeightArray;
			LevelsNode->GetInput(0)->OutputIndex = 4;
			UMaterialExpressionNamedRerouteUsage *BlackValueUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 900, NodeOffsetY + 50, LandscapeMaterial);
			BlackValueUsage->Declaration = BlackValueReroute;
			LevelsNode->GetInput(2)->Expression = BlackValueUsage;
			UMaterialExpressionNamedRerouteUsage *GrayValueUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 900, NodeOffsetY + 125, LandscapeMaterial);
			GrayValueUsage->Declaration = GrayValueReroute;
			LevelsNode->GetInput(3)->Expression = GrayValueUsage;
			UMaterialExpressionNamedRerouteUsage *WhiteValueUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 900, NodeOffsetY + 200, LandscapeMaterial);
			WhiteValueUsage->Declaration = WhiteValueReroute;
			LevelsNode->GetInput(4)->Expression = WhiteValueUsage;
		}

		if (LayerTex->CompressionSettings == TC_Normalmap)
			TexSampleArray->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;

		if (LayerTex->GetSizeX() == 256)
		{
			TexArray256Asset->SourceTextures.Add(LayerTex);
			TexSampleArray->Texture = TexArray256Asset;
			TexSampleArray->ParameterName = FName("TextureArray256");
			ArrayIndexConstant->R = TexArrayIndex256;
			TexArrayIndex256++;
			if (LayerTexHeight)
			{
				TexArray256Asset->SourceTextures.Add(LayerTexHeight);
				TexSampleHeightArray->Texture = TexArray256Asset;
				TexSampleHeightArray->ParameterName = FName("TextureHeightArray256");
				ArrayIndexConstantHeight->R = TexArrayIndex256;
				TexArrayIndex256++;
			}
		}
		else if (LayerTex->GetSizeX() == 512)
		{
			TexArray512Asset->SourceTextures.Add(LayerTex);
			TexSampleArray->Texture = TexArray512Asset;
			TexSampleArray->ParameterName = FName("TextureArray512");
			ArrayIndexConstant->R = TexArrayIndex512;
			TexArrayIndex512++;
			if (LayerTexHeight)
			{
				TexArray512Asset->SourceTextures.Add(LayerTexHeight);
				TexSampleHeightArray->Texture = TexArray512Asset;
				TexSampleHeightArray->ParameterName = FName("TextureHeightArray512");
				ArrayIndexConstantHeight->R = TexArrayIndex512;
				TexArrayIndex512++;
			}
		}
		else if (LayerTex->GetSizeX() == 1024)
		{
			TexArray1024Asset->SourceTextures.Add(LayerTex);
			TexSampleArray->Texture = TexArray1024Asset;
			TexSampleArray->ParameterName = FName("TextureArray1024");
			ArrayIndexConstant->R = TexArrayIndex1024;
			TexArrayIndex1024++;
			if (LayerTexHeight)
			{
				TexArray1024Asset->SourceTextures.Add(LayerTexHeight);
				TexSampleHeightArray->Texture = TexArray1024Asset;
				TexSampleHeightArray->ParameterName = FName("TextureHeightArray1024");
				ArrayIndexConstantHeight->R = TexArrayIndex1024;
				TexArrayIndex1024++;
			}
		}

		UMaterialExpressionMakeMaterialAttributes *MakeMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionMakeMaterialAttributes>(LandscapeMaterial), Section1 + 650, NodeOffsetY, LandscapeMaterial);
		MakeMaterialAttributesNode->BaseColor.Expression = TexSampleArray;

		UMaterialExpressionConstant *SpecularConstantNode = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section1, NodeOffsetY + 225, LandscapeMaterial);
		SpecularConstantNode->R = 0.0f;
		MakeMaterialAttributesNode->Specular.Expression = SpecularConstantNode;
		UMaterialExpressionConstant *RoughnessConstantNode = CreateNode(NewObject<UMaterialExpressionConstant>(LandscapeMaterial), Section1, NodeOffsetY + 300, LandscapeMaterial);
		RoughnessConstantNode->R = 0.4f;
		MakeMaterialAttributesNode->Roughness.Expression = RoughnessConstantNode;

		FLayerBlendInput LayerInput;
		LayerInput.LayerName = LayerMetadata.LayerInfo->LayerName;
		LayerInput.BlendType = LayerTexHeight ? LB_HeightBlend : LB_WeightBlend;
		LayerInput.LayerInput.Expression = MakeMaterialAttributesNode;
		LayerInput.HeightInput.Expression = LevelsNode;

		LayerBlendNode->Layers.Add(LayerInput);
		NodeOffsetY += 600;
	}

	TexArray256Asset->UpdateSourceFromSourceTextures();
	TexArray256Asset->MipGenSettings = TMGS_FromTextureGroup;
	TexArray256Asset->MarkPackageDirty();
	TexArray256Asset->PostEditChange();
	TexArray512Asset->UpdateSourceFromSourceTextures();
	TexArray512Asset->MipGenSettings = TMGS_FromTextureGroup;
	TexArray512Asset->MarkPackageDirty();
	TexArray512Asset->PostEditChange();
	TexArray1024Asset->UpdateSourceFromSourceTextures();
	TexArray1024Asset->MipGenSettings = TMGS_FromTextureGroup;
	TexArray1024Asset->MarkPackageDirty();
	TexArray1024Asset->PostEditChange();

	UMaterialExpressionGetMaterialAttributes *GetMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionGetMaterialAttributes>(LandscapeMaterial), Section1 + 1800, 300, LandscapeMaterial);
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_BaseColor));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Specular));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Roughness));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Normal));
	GetMaterialAttributesNode->Outputs.SetNum(GetMaterialAttributesNode->AttributeGetTypes.Num() + 1);
	GetMaterialAttributesNode->MaterialAttributes.Expression = LayerBlendNode;

	// RVT output class does not have Minimal API, so we use FindObject to get the class. (This is a workaround)
	UClass *RVTOutputClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionRuntimeVirtualTextureOutput"));
	UMaterialExpression *RVTOutputExpression = Cast<UMaterialExpression>(NewObject<UObject>(LandscapeMaterial, RVTOutputClass));
	UMaterialExpressionCustomOutput *RVTOutputNode = Cast<UMaterialExpressionCustomOutput>(CreateNode(RVTOutputExpression, Section1 + 2200, 300, LandscapeMaterial));

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

	UMaterialExpressionLandscapeVisibilityMask *VisibilityMaskNode = CreateNode(NewObject<UMaterialExpressionLandscapeVisibilityMask>(LandscapeMaterial), Section1 + 2200, 150, LandscapeMaterial);
	MakeMaterialAttributesNode->OpacityMask.Expression = VisibilityMaskNode;

	LandscapeMaterial->GetExpressionInputForProperty(MP_MaterialAttributes)->Expression = MakeMaterialAttributesNode;

	LandscapeMaterial->MarkPackageDirty();
	LandscapeMaterial->PostEditChange();

	const FString MaterialInstanceName = FString::Printf(TEXT("MI_%s"), *BaseName);
	const FString MaterialInstancePackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialInstanceName);

	UMaterialInstanceConstantFactoryNew *MaterialInstanceFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
	AssetTools.CreateAsset(MaterialInstanceName, MaterialDirectory, UMaterialInstanceConstant::StaticClass(), MaterialInstanceFactory);

	UMaterialInstanceConstant *MaterialInstance = Cast<UMaterialInstanceConstant>(UEditorAssetLibrary::LoadAsset(MaterialInstancePackagePath));
	MaterialInstance->SetParentEditorOnly(LandscapeMaterial);
	Landscape->LandscapeMaterial = MaterialInstance;

	FProperty *MaterialProperty = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), FName("LandscapeMaterial"));
	FPropertyChangedEvent MaterialPropertyChangedEvent(MaterialProperty);
	Landscape->PostEditChangeProperty(MaterialPropertyChangedEvent);
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

TSharedPtr<FJsonObject> FWoWLandscapeImporterModule::LoadJsonObject(const FString &FilePath)
{
	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			return JsonObject;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWoWLandscapeImporterModule, WoWLandscapeImporter)