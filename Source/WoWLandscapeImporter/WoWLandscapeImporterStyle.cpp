// Copyright Epic Games, Inc. All Rights Reserved.

#include "WoWLandscapeImporterStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FWoWLandscapeImporterStyle::StyleInstance = nullptr;

void FWoWLandscapeImporterStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FWoWLandscapeImporterStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FWoWLandscapeImporterStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("WoWLandscapeImporterStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FWoWLandscapeImporterStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("WoWLandscapeImporterStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("WoWLandscapeImporter")->GetBaseDir() / TEXT("Resources"));

	Style->Set("WoWLandscapeImporter.OpenPluginWindow", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));

	return Style;
}

void FWoWLandscapeImporterStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FWoWLandscapeImporterStyle::Get()
{
	return *StyleInstance;
}
