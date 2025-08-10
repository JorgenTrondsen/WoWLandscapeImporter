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
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h" 
#include "MaterialEditorUtilities.h"
#include "MaterialGraph/MaterialGraph.h" 
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "UObject/ConstructorHelpers.h"
#include "AssetImportTask.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxTextureImportData.h"

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
	TArray<FString> CSVFiles;

	// Find all required files in the directory
	FString SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.r16"));
	IFileManager::Get().FindFiles(HeightmapFiles, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("tex_*.png"));
	IFileManager::Get().FindFiles(AlphamapPNGs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("tex_*.json"));
	IFileManager::Get().FindFiles(AlphamapJSONs, *SearchPattern, true, false);
	SearchPattern = FPaths::Combine(DirectoryPath, TEXT("*.csv"));
	IFileManager::Get().FindFiles(CSVFiles, *SearchPattern, true, false);

	// Check if we found all required files
	if (HeightmapFiles.IsEmpty() || AlphamapPNGs.IsEmpty() || AlphamapJSONs.IsEmpty() || CSVFiles.IsEmpty())
	{
		UpdateStatusMessage(TEXT("Missing either: R16 files, Alphamap PNGs, Alphamap JSONs, or CSV files"), true);
	}
	else
	{
		double Zscale = 0.0, SeaLevelOffset = 0.0;
		uint8 TileColumns = 0, TileRows = 0;

		// Read heightmap metadata JSON and extract heightmap range
		FString MetadataPath = FPaths::Combine(DirectoryPath, TEXT("heightmap_metadata.json"));
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
		for (uint8 Row = 0; Row < TileRows; Row++)
			TileGrid[Row].SetNum(TileColumns);

		// First pass: collect filedata and metadata
		for (int i = 0; i < HeightmapFiles.Num(); i++)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(HeightmapFiles[i]).ParseIntoArray(NameParts, TEXT("_"), true);
			Tile NewTile;

			NewTile.Column = FCString::Atoi(*NameParts[1]);
			NewTile.Row = FCString::Atoi(*NameParts[2]);

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
						FString TexturePath = LayerObject->GetStringField(TEXT("file")).Replace(TEXT("\\"), TEXT("/"));
						FString DestinationDirectory = TEXT("/Game/Assets/WoWExport/Textures/") + FPaths::GetPath(TexturePath).Replace(TEXT("../"), TEXT(""));

						UTexture2D *ImportedTexture = ImportTexture(FPaths::Combine(DirectoryPath, TexturePath), DestinationDirectory);
						FName LayerName = CreateLayerInfo(FPaths::GetBaseFilename(TexturePath), DestinationDirectory, ImportedTexture);

						uint16 ChunkIndex = LayerObject->GetNumberField(TEXT("chunkIndex"));
						NewTile.Chunks[ChunkIndex].Layers.Add(LayerName);
					}
				}
			}
			TileGrid[NewTile.Row][NewTile.Column] = NewTile;
		}
		
		uint8 CompPerProxy = 1;
		uint8 NumProxiesX = FMath::DivideAndRoundUp(TileRows, CompPerProxy);
		uint8 NumProxiesY = FMath::DivideAndRoundUp(TileColumns, CompPerProxy);
		// Use a Scoped Slow Task to provide feedback to the user and prevent the editor from freezing.
		FScopedSlowTask SlowTask(NumProxiesX * NumProxiesY, LOCTEXT("ImportingWoWLandscape", "Importing WoW Landscape..."));
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
		
		ULandscapeInfo *LandscapeInfo = Landscape->CreateLandscapeInfo();
		
		for (int TileRow = 0; TileRow < TileRows; TileRow += CompPerProxy)
		{
			for (int TileCol = 0; TileCol < TileColumns; TileCol += CompPerProxy)
			{
				if (TileGrid[TileRow][TileCol].HeightmapData.Num() == 0)
					continue;

				// Update the slow task dialog
				SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ImportingProxy", "Importing Proxy at (Row {1}), (Column {0})"), TileRow, TileCol));
				
				TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> ProxyData = this->CreateProxyData(TileRow, TileCol, CompPerProxy);
				
				// Create a LandscapeStreamingProxy actor for the current tiles
				ALandscapeStreamingProxy *StreamingProxy = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscapeStreamingProxy>();
				StreamingProxy->SetActorLabel(FString::Printf(TEXT("%d_%d_Proxy"), TileCol, TileRow));
				StreamingProxy->SetActorScale3D(Landscape->GetActorScale3D());
				StreamingProxy->SetActorLocation(Landscape->GetActorLocation());
				
				// Prepare data for the Import function on the streaming proxy
				TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
				HeightDataPerLayer.Add(FGuid(), MoveTemp(ProxyData.Get<0>()));

				// Prepare material layer data
				TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
				MaterialLayerDataPerLayer.Add(FGuid(), MoveTemp(ProxyData.Get<1>()));

				uint32 MinX = TileGrid[TileRow][TileCol].Column * (254 * CompPerProxy);
				uint32 MinY = TileGrid[TileRow][TileCol].Row * (254 * CompPerProxy);
				uint32 MaxX = MinX + (254 * CompPerProxy);
				uint32 MaxY = MinY + (254 * CompPerProxy);
				StreamingProxy->Import(FGuid::NewGuid(), MinX, MinY, MaxX, MaxY, 2, 127, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive);
				
				StreamingProxy->SetLandscapeGuid(LandscapeGuid);
				LandscapeInfo->RegisterActor(StreamingProxy);
			}
		}
		// Create and assign the landscape material
		CreateLandscapeMaterial(DirectoryPath, Landscape);

		TArray<ActorData> ActorsArray;
		// First pass: parse CSV files and collect actor data
		for (const FString &CSVFile : CSVFiles)
		{
			// Load csv file
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
					
					// Extract position data and convert to centimeters(from yards)
					Actor.Position = FVector(
						FCString::Atod(*CSVFields[1]) * 91.44f, 					
						FCString::Atod(*CSVFields[3]) * 91.44f, 					
						FCString::Atod(*CSVFields[2]) * 91.44f + SeaLevelOffset		
					);
					// Extract rotation data and convert to Unreal's coordinate system
					Actor.Rotation = FRotator(
						-FCString::Atod(*CSVFields[4]), 		 			
						90 - FCString::Atod(*CSVFields[5]), 				
						FCString::Atod(*CSVFields[6])					
					);		
					
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
									FCString::Atod(*WMOCSVFields[3]) * 91.44f
								);
								WMOActor.Position = Actor.Rotation.RotateVector(WMOActor.Position); 
								WMOActor.Position += Actor.Position; 

								// Create quaternion from WMO actors rotation data
                                FQuat WMOQuat(
									-FCString::Atod(*WMOCSVFields[5]),  // X
                                    FCString::Atod(*WMOCSVFields[6]),  	// Y
                                    -FCString::Atod(*WMOCSVFields[7]),  // Z
                                    FCString::Atod(*WMOCSVFields[4])   	// W
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
		ActorsArray.Sort([](const ActorData& A, const ActorData& B)
		{
			return A.ModelPath < B.ModelPath;
		});

		// Extract model paths from ActorsArray
		TArray<FString> ModelPaths;
		for (const ActorData& Actor : ActorsArray)
			ModelPaths.Add(Actor.ModelPath);

		TArray<UStaticMesh*> ImportedModels = ImportModels(ModelPaths);

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
			ModelActor->SetActorScale3D(FVector(ActorsArray[Actor].Scale));
		}
	}
}


FName FWoWLandscapeImporterModule::CreateLayerInfo(const FString &TextureFileName, const FString &DestinationDirectory, UObject *ImportedTexture)
{
	FString LayerInfoName = FString::Printf(TEXT("LI_%s"), *TextureFileName);
	FString LayerInfoPackagePath = DestinationDirectory + TEXT("/") + LayerInfoName;

	if (UEditorAssetLibrary::DoesAssetExist(LayerInfoPackagePath))
		return FName(*TextureFileName);
		
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

	LayerMetadataMap.Add(LayerInfo->LayerName, LayerMetadata{LayerInfo, static_cast<UTexture2D*>(ImportedTexture)});

	return LayerInfo->LayerName;
}

TArray<UStaticMesh*> FWoWLandscapeImporterModule::ImportModels(TArray<FString> &ModelPaths)
{
	// Remove duplicates from the asset paths
	TSet<FString> UniqueAssetPaths(ModelPaths);
	ModelPaths = UniqueAssetPaths.Array();
	
	double StartTime = FPlatformTime::Seconds();
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TArray<FString> CreatedMaterials;
	for (const FString &ModelPath : ModelPaths)
	{
		// Find corresponding MTL file
		FString MTLPath = FPaths::ChangeExtension(ModelPath, TEXT("mtl"));
		
		// Parse MTL file and extract texture paths
		FString MTLContent;
		if (FFileHelper::LoadFileToString(MTLContent, *MTLPath))
		{
			TArray<FString> MTLLines;
			MTLContent.ParseIntoArrayLines(MTLLines);
			
			FString MaterialName;
			for (const FString &Line : MTLLines)
			{		
				if (Line.StartsWith(TEXT("newmtl ")))
					// Extract material name from mtl file
					MaterialName = Line.Mid(7).Replace(TEXT("&"), TEXT("_"));	 
				else if (Line.StartsWith(TEXT("map_Kd ")))
				{
					if (CreatedMaterials.Contains(MaterialName))
						continue;

					// Extract texture paths from the MTL file
					FString TexturePath = Line.Mid(7).Replace(TEXT("\\"), TEXT("/")); 
					FString MTLDirectory = FPaths::GetPath(MTLPath);
					FString RelativeTexturePath = FPaths::Combine(MTLDirectory, TexturePath);

					// Import the texture
					UTexture2D *ImportedTexture = ImportTexture(RelativeTexturePath, TEXT("/Game/Assets/WoWExport/Textures/"));

					// Create the material
					UMaterial *ModelMaterial = static_cast<UMaterial*>(AssetTools.CreateAsset(MaterialName, TEXT("/Game/Assets/WoWExport/Meshes/Materials/"), UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>()));

					UMaterialExpressionTextureSample *TextureSampleNear = CreateNode(NewObject<UMaterialExpressionTextureSample>(ModelMaterial), -100, 0, ModelMaterial);
					TextureSampleNear->Texture = ImportedTexture;
					ModelMaterial->GetExpressionInputForProperty(EMaterialProperty::MP_BaseColor)->Expression = TextureSampleNear;
					ModelMaterial->MarkPackageDirty();
					ModelMaterial->PostEditChange();

					CreatedMaterials.Add(MaterialName);
				}
			}
		}
	}

	double EndTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("Material creation took %.2f seconds"), EndTime - StartTime);	

	StartTime = FPlatformTime::Seconds();

	FString DestinationDirectory = TEXT("/Game/Assets/WoWExport/Meshes/");
	if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationDirectory))
		UEditorAssetLibrary::MakeDirectory(DestinationDirectory);
	
	TArray<UAssetImportTask*> ImportTasks;

	UFbxImportUI *ImportOptions = NewObject<UFbxImportUI>();
	ImportOptions->bIsObjImport = true;
	ImportOptions->bImportMaterials = false; 
	ImportOptions->bImportTextures = false;
	ImportOptions->StaticMeshImportData->ImportRotation = FRotator(0, 0, 90);
	ImportOptions->StaticMeshImportData->ImportUniformScale = 91.44f;
	ImportOptions->StaticMeshImportData->bCombineMeshes = true;
	ImportOptions->TextureImportData->MaterialSearchLocation = EMaterialSearchLocation::UnderParent;
	
	for (const FString &ModelPath : ModelPaths)
	{
		// Create an import task for automated import
		UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
		ImportTask->Filename = ModelPath;
		ImportTask->DestinationPath = DestinationDirectory;
		ImportTask->bSave = false;
		ImportTask->bAutomated = true;
		ImportTask->bReplaceExisting = true;
		ImportTask->bAsync = true;
		ImportTask->Options = ImportOptions;
		ImportTasks.Add(ImportTask);
	}
	
	// Import the asset using the task
	AssetTools.ImportAssetTasks(ImportTasks);

	// return the imported models
	TArray<UStaticMesh*> ImportedModels;
	for (UAssetImportTask* Task : ImportTasks)
		ImportedModels.Add(static_cast<UStaticMesh*>(Task->GetObjects()[0]));

	EndTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("Model import took %.2f seconds"), EndTime - StartTime);
	return ImportedModels;
}

UTexture2D *FWoWLandscapeImporterModule::ImportTexture(const FString &TexturePath, const FString &DestinationDirectory)
{
	// Try to load existing texture first
	if (UObject *ExistingAsset = UEditorAssetLibrary::LoadAsset(FPaths::Combine(DestinationDirectory, FPaths::GetBaseFilename(TexturePath))))
		return static_cast<UTexture2D*>(ExistingAsset);

	if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationDirectory))
		UEditorAssetLibrary::MakeDirectory(DestinationDirectory);

			
	UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
	ImportTask->Filename = TexturePath;
	ImportTask->DestinationPath = DestinationDirectory;
	ImportTask->bSave = false;
	ImportTask->bAutomated = true;
		
	// Import the texture
	IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks({ImportTask});
	
	return static_cast<UTexture2D*>(ImportTask->GetObjects()[0]);
}

TTuple<TArray<uint16>, TArray<FLandscapeImportLayerInfo>> FWoWLandscapeImporterModule::CreateProxyData(const int TileRow, const int TileCol, const uint8 CompPerProxy)
{
	const uint16 MapWidth = 254 * CompPerProxy + 1;
	const uint16 MapHeight = 254 * CompPerProxy + 1;

	const uint8 ShiftedCols = ((256 * CompPerProxy + 1) - MapWidth) * (TileCol / CompPerProxy);
	const uint8 ShiftedRows = ((256 * CompPerProxy + 1) - MapHeight) * (TileRow / CompPerProxy);

	TArray<uint16> ProxyHeightmap;
	ProxyHeightmap.SetNumZeroed(MapWidth * MapHeight);
	TMap<FName, FLandscapeImportLayerInfo> LayerInfoMap;

	int8 GridOffsetRow = TileRow > 0 ? -1 : 0;
	uint32 TileIndexRow = GridOffsetRow < 0 ? 256 - ShiftedRows : 0;
	for (int ProxyRow = 0; ProxyRow < MapHeight; ProxyRow++)
	{
		if (TileIndexRow % 256 == 0 && TileIndexRow != 0)
		{
			TileIndexRow = 0;
			GridOffsetRow++;
		}
		uint8 TileGridRow = TileRow + GridOffsetRow;

		int8 GridOffsetCol = TileCol > 0 ? -1 : 0;
		uint32 TileIndexCol = GridOffsetCol < 0 ? 256 - ShiftedCols : 0;
		for (int ProxyCol = 0; ProxyCol < MapWidth; ProxyCol++)
		{
			if (TileIndexCol % 256 == 0 && TileIndexCol != 0)
			{
				TileIndexCol = 0;
				GridOffsetCol++;
			}
			uint8 TileGridCol = TileCol + GridOffsetCol;

			uint32 TileIndexHM = (TileIndexRow * 257) + TileIndexCol;
			uint32 TileIndexAM = (TileIndexRow * 256) + TileIndexCol;
			uint32 ProxyIndex = (ProxyRow * MapWidth) + ProxyCol;

			if (TileGrid.IsValidIndex(TileGridRow) && TileGrid[TileGridRow].IsValidIndex(TileGridCol) && !TileGrid[TileGridRow][TileGridCol].HeightmapData.IsEmpty())
			{
				ProxyHeightmap[ProxyIndex] = TileGrid[TileGridRow][TileGridCol].HeightmapData[TileIndexHM];

				uint8 ChunkIndex = (TileIndexRow / 16) * 16 + (TileIndexCol / 16);
				FColor PixelData = TileGrid[TileGridRow][TileGridCol].AlphamapData[TileIndexAM];
				// Calculate the weight for each layer based on the pixel data
				for (uint16 LayerIndex = 0; LayerIndex < TileGrid[TileGridRow][TileGridCol].Chunks[ChunkIndex].Layers.Num(); ++LayerIndex)
				{
					LayerMetadata *LayerMetadata = LayerMetadataMap.Find(TileGrid[TileGridRow][TileGridCol].Chunks[ChunkIndex].Layers[LayerIndex]);

					FLandscapeImportLayerInfo &ImportLayerInfo = LayerInfoMap.FindOrAdd(TileGrid[TileGridRow][TileGridCol].Chunks[ChunkIndex].Layers[LayerIndex]);
					if (ImportLayerInfo.LayerData.Num() == 0)
					{
						ImportLayerInfo.LayerData.SetNumZeroed(MapWidth * MapHeight);
						ImportLayerInfo.LayerInfo = LayerMetadata->LayerInfo;
						ImportLayerInfo.LayerName = LayerMetadata->LayerInfo->LayerName;
					}

					switch (LayerIndex)
					{
					case 0:
						ImportLayerInfo.LayerData[ProxyIndex] = PixelData.A - PixelData.R - PixelData.G - PixelData.B;
						break;

					case 1:
						ImportLayerInfo.LayerData[ProxyIndex] = PixelData.R;
						break;

					case 2:
						ImportLayerInfo.LayerData[ProxyIndex] = PixelData.G;
						break;

					case 3:
						ImportLayerInfo.LayerData[ProxyIndex] = PixelData.B;
						break;
					}
				}
			}
			TileIndexCol++;
		}
		TileIndexRow++;
	}

	TArray<FLandscapeImportLayerInfo> LayerInfoArray;
	LayerInfoMap.GenerateValueArray(LayerInfoArray);

	return MakeTuple(MoveTemp(ProxyHeightmap), MoveTemp(LayerInfoArray));
}

void FWoWLandscapeImporterModule::CreateLandscapeMaterial(const FString &BaseDirectoryPath, ALandscape *Landscape)
{
    const FString MaterialDirectory = TEXT("/Game/Assets_Configuration/Materials");
    const FString BaseName = FPaths::GetCleanFilename(BaseDirectoryPath);
    
    if (!UEditorAssetLibrary::DoesDirectoryExist(MaterialDirectory))
    UEditorAssetLibrary::MakeDirectory(MaterialDirectory);
    
    IAssetTools &AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    
    // Create the RVT asset
    const FString RVTName = FString::Printf(TEXT("RVT_%s"), *BaseName);
    const FString RVTPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *RVTName);
    AssetTools.CreateAsset(RVTName, MaterialDirectory, URuntimeVirtualTexture::StaticClass(), nullptr);

    // Load the RVT asset
    URuntimeVirtualTexture *RVTAsset = static_cast<URuntimeVirtualTexture*>(UEditorAssetLibrary::LoadAsset(RVTPackagePath));
	Landscape->RuntimeVirtualTextures.Add(RVTAsset);

    // Create the material asset
    const FString MaterialName = FString::Printf(TEXT("M_Landscape_%s"), *BaseName);
    const FString MaterialPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialName);
    UMaterialFactoryNew *MaterialFactory = NewObject<UMaterialFactoryNew>();
    AssetTools.CreateAsset(MaterialName, MaterialDirectory, UMaterial::StaticClass(), MaterialFactory);
    
    // Load the asset and perform all modifications
    UMaterial *LandscapeMaterial = static_cast<UMaterial*>(UEditorAssetLibrary::LoadAsset(MaterialPackagePath));
	LandscapeMaterial->bUseMaterialAttributes = true;
	
	// -- Calculate the UV mapping for the landscape --
	int32 Section0 = -3800;
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
	UMaterialExpressionDivide* DivideNodeNear = CreateNode(NewObject<UMaterialExpressionDivide>(LandscapeMaterial), Section0 + 300, 0, LandscapeMaterial);
	DivideNodeNear->A.Expression = CoordsNode;
	DivideNodeNear->B.Expression = NearTilingSize;
	UMaterialExpressionDivide* DivideNodeFar = CreateNode(NewObject<UMaterialExpressionDivide>(LandscapeMaterial), Section0 + 300, 100, LandscapeMaterial);
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

	UMaterialExpressionSaturate *SaturateNode = CreateNode(NewObject<UMaterialExpressionSaturate>(LandscapeMaterial), Section0 + 975, 600, LandscapeMaterial);
	SaturateNode->Input.Expression = SubtractNode;

	UMaterialExpressionNamedRerouteDeclaration *DepthFadeReroute = CreateNode(NewObject<UMaterialExpressionNamedRerouteDeclaration>(LandscapeMaterial), Section0 + 1100, 600, LandscapeMaterial);
	DepthFadeReroute->Name = FName("DepthFade");
	DepthFadeReroute->NodeColor = FLinearColor::Green;
	DepthFadeReroute->Input.Expression = SaturateNode;

	int32 Section1 = -2500;
    UMaterialExpressionLandscapeLayerBlend *LayerBlendNode = CreateNode(NewObject<UMaterialExpressionLandscapeLayerBlend>(LandscapeMaterial), Section1 + 1400, 0, LandscapeMaterial);

    // Find the 3PointLevels Material Function
	UMaterialFunction* ThreePointLevelsFunc = LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions02/3PointLevels.3PointLevels"));

	// Create black value, gray value, and white value parameters
	UMaterialExpressionScalarParameter *BlackValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial),Section1 + 1000, -250, LandscapeMaterial);
	BlackValueNode->ParameterName = FName("BlackValue");
	BlackValueNode->Group = FName("3PointLevels");
	BlackValueNode->DefaultValue = 0.5f;
	UMaterialExpressionScalarParameter *GrayValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial),Section1 + 1000, -175, LandscapeMaterial);
	GrayValueNode->ParameterName = FName("GrayValue");
	GrayValueNode->Group = FName("3PointLevels");
	GrayValueNode->DefaultValue = 0.6f;
	UMaterialExpressionScalarParameter *WhiteValueNode = CreateNode(NewObject<UMaterialExpressionScalarParameter>(LandscapeMaterial),Section1 + 1000, -100, LandscapeMaterial);
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
    for (auto const &[LayerName, LayerMetadata] : LayerMetadataMap)
    {
        UMaterialExpressionNamedRerouteUsage *NearRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY, LandscapeMaterial);
        NearRerouteUsage->Declaration = NearReroute;
		UMaterialExpressionNamedRerouteUsage *FarRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1, NodeOffsetY + 300, LandscapeMaterial);
        FarRerouteUsage->Declaration = FarReroute;
		
        UMaterialExpressionTextureSample *TextureSampleNear = CreateNode(NewObject<UMaterialExpressionTextureSample>(LandscapeMaterial), Section1 + 100, NodeOffsetY, LandscapeMaterial);
        TextureSampleNear->Texture = LayerMetadata.LayerTexture.Get();
        TextureSampleNear->Coordinates.Expression = NearRerouteUsage;
        TextureSampleNear->SamplerSource = ESamplerSourceMode::SSM_Wrap_WorldGroupSettings;
        UMaterialExpressionTextureSample *TextureSampleFar = CreateNode(NewObject<UMaterialExpressionTextureSample>(LandscapeMaterial), Section1 + 100, NodeOffsetY + 300, LandscapeMaterial);
		TextureSampleFar->Texture = LayerMetadata.LayerTexture.Get();
		TextureSampleFar->Coordinates.Expression = FarRerouteUsage;
		TextureSampleFar->SamplerSource = ESamplerSourceMode::SSM_Wrap_WorldGroupSettings;

        if (LayerMetadata.LayerTexture->CompressionSettings == TC_Normalmap)
		{
            TextureSampleNear->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
			TextureSampleFar->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
		}
		
		UMaterialExpressionNamedRerouteUsage *DepthFadeRerouteUsage = CreateNode(NewObject<UMaterialExpressionNamedRerouteUsage>(LandscapeMaterial), Section1 + 500, NodeOffsetY + 200, LandscapeMaterial);
		DepthFadeRerouteUsage->Declaration = DepthFadeReroute;

		UMaterialExpressionLinearInterpolate *LerpBaseColor = CreateNode(NewObject<UMaterialExpressionLinearInterpolate>(LandscapeMaterial), Section1 + 500, NodeOffsetY, LandscapeMaterial);
		LerpBaseColor->A.Expression = TextureSampleNear;
		LerpBaseColor->B.Expression = TextureSampleFar;
		LerpBaseColor->Alpha.Expression = DepthFadeRerouteUsage;
		UMaterialExpressionLinearInterpolate *LerpAlpha = CreateNode(NewObject<UMaterialExpressionLinearInterpolate>(LandscapeMaterial), Section1 + 500, NodeOffsetY + 300, LandscapeMaterial);
		LerpAlpha->A.Expression = TextureSampleNear;
		LerpAlpha->B.Expression = TextureSampleFar;
		LerpAlpha->Alpha.Expression = DepthFadeRerouteUsage;
		LerpAlpha->A.OutputIndex = 4; 
		LerpAlpha->B.OutputIndex = 4; 

        // Connect the output 0 of the texture sample to the MakeMaterialAttributes node
        UMaterialExpressionMakeMaterialAttributes *MakeMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionMakeMaterialAttributes>(LandscapeMaterial), Section1 + 700, NodeOffsetY, LandscapeMaterial);
        MakeMaterialAttributesNode->BaseColor.Expression = LerpBaseColor;
		MakeMaterialAttributesNode->Specular.Expression = LerpAlpha;
		
		UMaterialExpressionMaterialFunctionCall *LevelsNode = CreateNode(NewObject<UMaterialExpressionMaterialFunctionCall>(LandscapeMaterial),Section1 + 1050, NodeOffsetY + 150, LandscapeMaterial);
		LevelsNode->MaterialFunction = ThreePointLevelsFunc;
		LevelsNode->UpdateFromFunctionResource();

		// Connect the alpha channel of the texture to the 'Input' of the 3PointLevels function
		LevelsNode->GetInput(0)->Expression = LerpAlpha;

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
	UMaterialExpressionGetMaterialAttributes *GetMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionGetMaterialAttributes>(LandscapeMaterial), Section1 + 1800, 300, LandscapeMaterial);
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_BaseColor));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Specular));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Roughness));
	GetMaterialAttributesNode->AttributeGetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Normal));
	GetMaterialAttributesNode->Outputs.SetNum(GetMaterialAttributesNode->AttributeGetTypes.Num() + 1);
	GetMaterialAttributesNode->MaterialAttributes.Expression = LayerBlendNode;

	// RVT output class does not have Minimal API, so we use FindObject to get the class. (This is a workaround)
	UClass *RVTOutputClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionRuntimeVirtualTextureOutput"));
	UMaterialExpression *RVTOutputExpression = static_cast<UMaterialExpression*>(NewObject<UObject>(LandscapeMaterial, RVTOutputClass));
	UMaterialExpressionCustomOutput *RVTOutputNode = static_cast<UMaterialExpressionCustomOutput*>(CreateNode(RVTOutputExpression, Section1 + 2200, 300, LandscapeMaterial));
	// Connect the output of the GetMaterialAttributes node to the RVT output node
	for (int i = 0; i < GetMaterialAttributesNode->AttributeGetTypes.Num(); ++i)
	{
		RVTOutputNode->GetInput(i)->Expression = GetMaterialAttributesNode;
		RVTOutputNode->GetInput(i)->OutputIndex = i + 1;
	}

	UMaterialExpressionRuntimeVirtualTextureSample *RVTSampleNode = CreateNode(NewObject<UMaterialExpressionRuntimeVirtualTextureSample>(LandscapeMaterial), Section1 + 1800, 0, LandscapeMaterial);
	RVTSampleNode->VirtualTexture = RVTAsset;
	RVTSampleNode->MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;

	UMaterialExpressionSetMaterialAttributes *SetMaterialAttributesNode = CreateNode(NewObject<UMaterialExpressionSetMaterialAttributes>(LandscapeMaterial), Section1 + 2200, 0, LandscapeMaterial);
	SetMaterialAttributesNode->AttributeSetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_BaseColor));
	SetMaterialAttributesNode->AttributeSetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Specular));
	SetMaterialAttributesNode->AttributeSetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Roughness));
	SetMaterialAttributesNode->AttributeSetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Normal));
	SetMaterialAttributesNode->Inputs.SetNum(SetMaterialAttributesNode->AttributeSetTypes.Num() + 1);
	for (int i = 0; i < SetMaterialAttributesNode->AttributeSetTypes.Num(); ++i)
	{
		SetMaterialAttributesNode->GetInput(i + 1)->Expression = RVTSampleNode;
		SetMaterialAttributesNode->GetInput(i + 1)->OutputIndex = i;
	}

	// Connect the final material attributes to the material's output
	LandscapeMaterial->GetExpressionInputForProperty(MP_MaterialAttributes)->Expression = SetMaterialAttributesNode;

	// Mark the package as needing to be saved and update the material
	LandscapeMaterial->MarkPackageDirty();
	LandscapeMaterial->PostEditChange();

	// Create a landscape material instance and return it
	const FString MaterialInstanceName = FString::Printf(TEXT("MI_Landscape_%s"), *BaseName);
	const FString MaterialInstancePackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDirectory, *MaterialInstanceName);

	// Create the material instance asset
	UMaterialInstanceConstantFactoryNew *MaterialInstanceFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
	AssetTools.CreateAsset(MaterialInstanceName, MaterialDirectory, UMaterialInstanceConstant::StaticClass(), MaterialInstanceFactory);

	// Load and configure the material instance
	UMaterialInstanceConstant *MaterialInstance = static_cast<UMaterialInstanceConstant*>(UEditorAssetLibrary::LoadAsset(MaterialInstancePackagePath));
	MaterialInstance->SetParentEditorOnly(LandscapeMaterial);

	// Update and mark as dirty
	MaterialInstance->MarkPackageDirty();

	Landscape->LandscapeMaterial = MaterialInstance;
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