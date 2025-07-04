// Copyright Epic Games, Inc. All Rights Reserved.

#include "WoWLandscapeImporter.h"
#include "Style/WoWLandscapeImporterStyle.h"
#include "Commands/WoWLandscapeImporterCommands.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Slate/SlateGameResources.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Engine/Engine.h"
#include "Styling/AppStyle.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "WoWTileHelper.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "Engine/World.h"

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
	IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	TArray<FString> LandscapeFiles;

	// Use a visitor to find .r16 files
	class FDirectoryVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		TArray<FString> &LandscapeFilesRef;
		FString BasePath;

		FDirectoryVisitor(const FString &InBasePath, TArray<FString> &InLandscapeFiles)
			: LandscapeFilesRef(InLandscapeFiles), BasePath(InBasePath) {}

		virtual bool Visit(const TCHAR *FilenameOrDirectory, bool bIsDirectory) override
		{
			if (!bIsDirectory)
			{
				FString FileName = FPaths::GetCleanFilename(FString(FilenameOrDirectory));

				// Only collect .r16 files
				if (FileName.EndsWith(TEXT(".r16")))
				{
					LandscapeFilesRef.Add(FileName);
				}
			}
			return true;
		}
	};

	FDirectoryVisitor Visitor(DirectoryPath, LandscapeFiles);
	PlatformFile.IterateDirectory(*DirectoryPath, Visitor);

	// Check if we found any .r16 files and update status message
	if (LandscapeFiles.IsEmpty())
	{
		UpdateStatusMessage(TEXT("No landscape files (.r16) found in selected directory"), true);
	}
	else
	{
		double Zscale = 0.0;

		// Read heightmap metadata JSON and extract heightmap range
		FString MetadataPath = FPaths::Combine(DirectoryPath, TEXT("heightmap_metadata.json"));
		FString JsonString;
		if (FFileHelper::LoadFileToString(JsonString, *MetadataPath))
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				const TSharedPtr<FJsonObject> *HeightRangeObject;
				if (JsonObject->TryGetObjectField(TEXT("height_range"), HeightRangeObject) && HeightRangeObject->IsValid())
				{
					double RangeValue;
					if ((*HeightRangeObject)->TryGetNumberField(TEXT("range"), RangeValue))
					{
						Zscale = RangeValue * 91.44;	 // RangeValue is in yards, convert to centimeters(1 yard = 91.44 cm)
						Zscale = (Zscale / 51200) * 100; // Convert to percentage scale
					}
				}
			}
		}

		TArray<Tile> Tiles1D;
		uint8 ColumnOrigin = 255;
		uint8 RowOrigin = 255;
		uint8 ColumnMax = 0;
		uint8 RowMax = 0;

		// First pass: collect tiles and find bounds
		for (const FString &FileName : LandscapeFiles)
		{
			TArray<FString> NameParts;
			FPaths::GetBaseFilename(FileName).ParseIntoArray(NameParts, TEXT("_"), true);

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

			TArray<uint8> FileData;
			FString FullPath = FPaths::Combine(DirectoryPath, FileName);
			if (FFileHelper::LoadFileToArray(FileData, *FullPath))
			{
				const int32 DataCount = FileData.Num() / sizeof(uint16);
				NewTile.HeightmapData.AddUninitialized(DataCount);
				FMemory::Memcpy(NewTile.HeightmapData.GetData(), FileData.GetData(), FileData.Num());

				Tiles1D.Add(NewTile);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Could not read file: %s"), *FullPath);
			}
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

			TArray<uint16> TopTileData;
			TArray<uint16> LeftTileData;
			TArray<uint16> TopLeftTileData;

			if (TopTile)
				TopTileData = WoWTileHelper::GetRows(TopTile->HeightmapData, 257, (ArrayRow * 2));

			if (LeftTile)
				LeftTileData = WoWTileHelper::GetColumns(LeftTile->HeightmapData, 257, (ArrayColumn * 2));

			if (TopLeftTile)
				TopLeftTileData = WoWTileHelper::GetCorner(TopLeftTile->HeightmapData, 257, (ArrayColumn * 2), (ArrayRow * 2));

			TArray<uint16> HeightmapToImport = WoWTileHelper::ExpandTile(CurrentTile.HeightmapData, TopTileData, LeftTileData, TopLeftTileData, 257, (ArrayColumn * 2), (ArrayRow * 2));
			HeightmapToImport = WoWTileHelper::CropTile(HeightmapToImport, (257 + (ArrayColumn * 2)), 255);

			// Spawn the Landscape actor
			ALandscape *Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>();
			Landscape->SetActorLabel("Landscape_" + FString::Printf(TEXT("%d_%d"), CurrentTile.Column, CurrentTile.Row));
			Landscape->SetActorScale3D(FVector(190.5f, 190.5f, Zscale));

			float_t tileSize = 254 * 190.5f;
			Landscape->SetActorLocation(FVector(ArrayColumn * tileSize, ArrayRow * tileSize, 170000.f));

			// Prepare data for the Import function
			TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
			HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightmapToImport));
			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
			MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

			// Import the heightmap data into the landscape
			Landscape->Import(FGuid::NewGuid(), 0, 0, 254, 254, 2, 127, HeightDataPerLayer, nullptr, MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive);

			// Finalize the landscape actor
			ULandscapeInfo *LandscapeInfo = Landscape->GetLandscapeInfo();
			LandscapeInfo->UpdateLayerInfoMap(Landscape);
			Landscape->RegisterAllComponents();
		}
		FString Message = FString::Printf(TEXT("Imported %d landscape file(s)"), LandscapeFiles.Num());
		UpdateStatusMessage(Message, false);
	}
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