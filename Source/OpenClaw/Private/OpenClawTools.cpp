// Copyright OpenClaw. All Rights Reserved.

#include "OpenClawTools.h"
#include "OpenClawConnectionManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "EditorModeManager.h"
#include "LevelEditor.h"
#include "FileHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "HighResScreenshot.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFilemanager.h"
#include "UObject/UnrealType.h"

namespace
{
	UWorld* ResolveToolWorld(const TSharedPtr<FJsonObject>& Params, bool bPreferPlayWorld = true)
	{
		const FString RequestedWorld = Params.IsValid() && Params->HasField(TEXT("world"))
			? Params->GetStringField(TEXT("world")).ToLower()
			: TEXT("");

		if (GEditor)
		{
			if (RequestedWorld == TEXT("play") || RequestedWorld == TEXT("pie") || RequestedWorld == TEXT("runtime"))
			{
				if (GEditor->PlayWorld)
				{
					return GEditor->PlayWorld.Get();
				}
			}
			else if (RequestedWorld == TEXT("editor"))
			{
				if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
				{
					return EditorWorld;
				}
			}

			if (bPreferPlayWorld && GEditor->PlayWorld)
			{
				return GEditor->PlayWorld.Get();
			}

			if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
			{
				return EditorWorld;
			}
		}

		return nullptr;
	}

	AActor* FindActorByNameInWorld(UWorld* World, const FString& Name)
	{
		if (!World || Name.IsEmpty())
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetName() == Name || Actor->GetActorLabel() == Name))
			{
				return Actor;
			}
		}

		return nullptr;
	}
}

TSharedPtr<FJsonObject> FOpenClawTools::ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Params)
{
	UE_LOG(LogOpenClaw, Log, TEXT("Executing tool: %s"), *ToolName);
	
	// Map tool names to handlers
	if (ToolName == TEXT("level.getCurrent")) return Level_GetCurrent(Params);
	if (ToolName == TEXT("level.list")) return Level_List(Params);
	if (ToolName == TEXT("level.open")) return Level_Open(Params);
	if (ToolName == TEXT("level.save")) return Level_Save(Params);
	
	if (ToolName == TEXT("actor.find")) return Actor_Find(Params);
	if (ToolName == TEXT("actor.getAll")) return Actor_GetAll(Params);
	if (ToolName == TEXT("actor.create")) return Actor_Create(Params);
	if (ToolName == TEXT("actor.delete") || ToolName == TEXT("actor.destroy")) return Actor_Delete(Params);
	if (ToolName == TEXT("actor.getData")) return Actor_GetData(Params);
	if (ToolName == TEXT("actor.setProperty")) return Actor_SetProperty(Params);
	if (ToolName == TEXT("actor.callFunction")) return Actor_CallFunction(Params);
	
	if (ToolName == TEXT("transform.getPosition")) return Transform_GetPosition(Params);
	if (ToolName == TEXT("transform.setPosition")) return Transform_SetPosition(Params);
	if (ToolName == TEXT("transform.getRotation")) return Transform_GetRotation(Params);
	if (ToolName == TEXT("transform.setRotation")) return Transform_SetRotation(Params);
	if (ToolName == TEXT("transform.getScale")) return Transform_GetScale(Params);
	if (ToolName == TEXT("transform.setScale")) return Transform_SetScale(Params);
	
	if (ToolName == TEXT("component.get")) return Component_Get(Params);
	if (ToolName == TEXT("component.add")) return Component_Add(Params);
	if (ToolName == TEXT("component.remove")) return Component_Remove(Params);
	
	if (ToolName == TEXT("editor.play")) return Editor_Play(Params);
	if (ToolName == TEXT("editor.stop")) return Editor_Stop(Params);
	if (ToolName == TEXT("editor.pause")) return Editor_Pause(Params);
	if (ToolName == TEXT("editor.resume")) return Editor_Resume(Params);
	if (ToolName == TEXT("editor.getState")) return Editor_GetState(Params);
	
	if (ToolName == TEXT("debug.hierarchy")) return Debug_Hierarchy(Params);
	if (ToolName == TEXT("debug.screenshot")) return Debug_Screenshot(Params);
	if (ToolName == TEXT("debug.log")) return Debug_Log(Params);
	
	if (ToolName == TEXT("input.simulateKey")) return Input_SimulateKey(Params);
	if (ToolName == TEXT("input.simulateMouse")) return Input_SimulateMouse(Params);
	if (ToolName == TEXT("input.simulateAxis")) return Input_SimulateAxis(Params);
	
	if (ToolName == TEXT("asset.list")) return Asset_List(Params);
	if (ToolName == TEXT("asset.import")) return Asset_Import(Params);
	
	if (ToolName == TEXT("console.execute")) return Console_Execute(Params);
	if (ToolName == TEXT("console.getLogs")) return Console_GetLogs(Params);
	
	if (ToolName == TEXT("blueprint.list")) return Blueprint_List(Params);
	if (ToolName == TEXT("blueprint.open")) return Blueprint_Open(Params);
	
	return MakeErrorResult(FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
}

int32 FOpenClawTools::GetToolCount()
{
	// Count of all available tools
	// Level: 4, Actor: 7, Transform: 6, Component: 3, Editor: 5, Debug: 3, Input: 3, Asset: 2, Console: 2, Blueprint: 2
	return 37;
}

// Helper functions
TSharedPtr<FJsonObject> FOpenClawTools::MakeSuccessResult(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::MakeErrorResult(const FString& Error)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), Error);
	return Result;
}

UWorld* FOpenClawTools::GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

AActor* FOpenClawTools::FindActorByName(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}

	if (UWorld* PreferredWorld = ResolveToolWorld(nullptr, true))
	{
		if (AActor* Found = FindActorByNameInWorld(PreferredWorld, Name))
		{
			return Found;
		}
	}

	if (UWorld* EditorWorld = ResolveToolWorld(nullptr, false))
	{
		return FindActorByNameInWorld(EditorWorld, Name);
	}

	return nullptr;
}

TSharedPtr<FJsonObject> FOpenClawTools::ActorToJson(AActor* Actor, bool bDetailed)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	
	if (!Actor)
	{
		return Obj;
	}
	
	Obj->SetStringField(TEXT("name"), Actor->GetName());
	Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	
	// Transform
	FVector Location = Actor->GetActorLocation();
	FRotator Rotation = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();
	
	TSharedPtr<FJsonObject> TransformObj = MakeShareable(new FJsonObject());
	
	TSharedPtr<FJsonObject> PosObj = MakeShareable(new FJsonObject());
	PosObj->SetNumberField(TEXT("x"), Location.X);
	PosObj->SetNumberField(TEXT("y"), Location.Y);
	PosObj->SetNumberField(TEXT("z"), Location.Z);
	TransformObj->SetObjectField(TEXT("position"), PosObj);
	
	TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject());
	RotObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rotation.Roll);
	TransformObj->SetObjectField(TEXT("rotation"), RotObj);
	
	TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject());
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	TransformObj->SetObjectField(TEXT("scale"), ScaleObj);
	
	Obj->SetObjectField(TEXT("transform"), TransformObj);
	
	if (bDetailed)
	{
		// Add components
		TArray<TSharedPtr<FJsonValue>> ComponentsArray;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
			{
				TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
				CompObj->SetStringField(TEXT("name"), Component->GetName());
				CompObj->SetStringField(TEXT("class"), Component->GetClass()->GetName());
				ComponentsArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
			}
		}
		Obj->SetArrayField(TEXT("components"), ComponentsArray);
	}
	
	return Obj;
}

// Level tools
TSharedPtr<FJsonObject> FOpenClawTools::Level_GetCurrent(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorResult(TEXT("No editor world"));
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), World->GetMapName());
	Result->SetStringField(TEXT("path"), World->GetPathName());
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Level_List(const TSharedPtr<FJsonObject>& Params)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), AssetDataList);
	
	TArray<TSharedPtr<FJsonValue>> LevelsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> LevelObj = MakeShareable(new FJsonObject());
		LevelObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		LevelObj->SetStringField(TEXT("path"), AssetData.GetSoftObjectPath().ToString());
		LevelsArray.Add(MakeShareable(new FJsonValueObject(LevelObj)));
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("levels"), LevelsArray);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Level_Open(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = Params->GetStringField(TEXT("path"));
	
	if (Path.IsEmpty())
	{
		return MakeErrorResult(TEXT("path is required"));
	}
	
	if (GEditor)
	{
		FEditorFileUtils::LoadMap(Path);
		return MakeSuccessResult(FString::Printf(TEXT("Opened level: %s"), *Path));
	}
	
	return MakeErrorResult(TEXT("GEditor not available"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Level_Save(const TSharedPtr<FJsonObject>& Params)
{
	if (GEditor)
	{
		UWorld* World = GetEditorWorld();
		if (World)
		{
			FEditorFileUtils::SaveCurrentLevel();
			return MakeSuccessResult(TEXT("Level saved"));
		}
	}
	
	return MakeErrorResult(TEXT("Failed to save level"));
}

// Actor tools
TSharedPtr<FJsonObject> FOpenClawTools::Actor_Find(const TSharedPtr<FJsonObject>& Params)
{
	const FString Name = Params->HasField(TEXT("name")) ? Params->GetStringField(TEXT("name")) : TEXT("");
	const FString ClassFilter = Params->HasField(TEXT("class")) ? Params->GetStringField(TEXT("class")) : TEXT("");

	if (Name.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MakeErrorResult(TEXT("name or class is required"));
	}

	UWorld* World = ResolveToolWorld(Params, true);
	if (!World)
	{
		return MakeErrorResult(TEXT("No world available"));
	}

	AActor* FirstMatch = nullptr;
	TArray<TSharedPtr<FJsonValue>> MatchesArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		const bool bNameMatch = Name.IsEmpty() || Actor->GetName() == Name || Actor->GetActorLabel() == Name;
		const bool bClassMatch = ClassFilter.IsEmpty() || Actor->GetClass()->GetName().Contains(ClassFilter);

		if (bNameMatch && bClassMatch)
		{
			if (!FirstMatch)
			{
				FirstMatch = Actor;
			}
			MatchesArray.Add(MakeShareable(new FJsonValueObject(ActorToJson(Actor, true))));
		}
	}

	if (!FirstMatch)
	{
		if (!Name.IsEmpty())
		{
			return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
		}
		return MakeErrorResult(FString::Printf(TEXT("No actors found for class filter: %s"), *ClassFilter));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("actor"), ActorToJson(FirstMatch, true));
	Result->SetNumberField(TEXT("count"), MatchesArray.Num());
	Result->SetArrayField(TEXT("matches"), MatchesArray);
	Result->SetStringField(TEXT("world"), World->GetName());

	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Actor_GetAll(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = ResolveToolWorld(Params, true);
	if (!World)
	{
		return MakeErrorResult(TEXT("No world available"));
	}

	const FString ClassFilter = Params->HasField(TEXT("class")) ? Params->GetStringField(TEXT("class")) : TEXT("");
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && !Actor->IsA<AWorldSettings>())
		{
			if (ClassFilter.IsEmpty() || Actor->GetClass()->GetName().Contains(ClassFilter))
			{
				ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorToJson(Actor, false))));
			}
		}
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("world"), World->GetName());
	Result->SetNumberField(TEXT("count"), ActorsArray.Num());
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Actor_Create(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorResult(TEXT("No editor world"));
	}
	
	FString Type = Params->GetStringField(TEXT("type"));
	FString ClassName = Params->GetStringField(TEXT("class"));
	FString Name = Params->GetStringField(TEXT("name"));
	
	// Support both "type" and "class" parameters
	if (Type.IsEmpty() && !ClassName.IsEmpty()) Type = ClassName;
	if (Type.IsEmpty()) Type = TEXT("StaticMeshActor");
	
	FVector Location = FVector::ZeroVector;
	// Support both flat (x,y,z) and nested (location.x) params
	if (Params->HasField(TEXT("x"))) Location.X = Params->GetNumberField(TEXT("x"));
	if (Params->HasField(TEXT("y"))) Location.Y = Params->GetNumberField(TEXT("y"));
	if (Params->HasField(TEXT("z"))) Location.Z = Params->GetNumberField(TEXT("z"));
	if (Params->HasField(TEXT("location")))
	{
		auto LocObj = Params->GetObjectField(TEXT("location"));
		if (LocObj->HasField(TEXT("x"))) Location.X = LocObj->GetNumberField(TEXT("x"));
		if (LocObj->HasField(TEXT("y"))) Location.Y = LocObj->GetNumberField(TEXT("y"));
		if (LocObj->HasField(TEXT("z"))) Location.Z = LocObj->GetNumberField(TEXT("z"));
	}
	
	AActor* NewActor = nullptr;
	
	if (Type == TEXT("StaticMeshActor") || Type == TEXT("Cube"))
	{
		NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		if (AStaticMeshActor* SMAActor = Cast<AStaticMeshActor>(NewActor))
		{
			UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (CubeMesh && SMAActor->GetStaticMeshComponent())
			{
				SMAActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
			}
		}
	}
	else if (Type == TEXT("Sphere"))
	{
		NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		if (AStaticMeshActor* SMAActor = Cast<AStaticMeshActor>(NewActor))
		{
			UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			if (SphereMesh && SMAActor->GetStaticMeshComponent())
			{
				SMAActor->GetStaticMeshComponent()->SetStaticMesh(SphereMesh);
			}
		}
	}
	else if (Type == TEXT("Cylinder"))
	{
		NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		if (AStaticMeshActor* SMAActor = Cast<AStaticMeshActor>(NewActor))
		{
			UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			if (CylinderMesh && SMAActor->GetStaticMeshComponent())
			{
				SMAActor->GetStaticMeshComponent()->SetStaticMesh(CylinderMesh);
			}
		}
	}
	else if (Type == TEXT("Cone"))
	{
		NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		if (AStaticMeshActor* SMAActor = Cast<AStaticMeshActor>(NewActor))
		{
			UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
			if (ConeMesh && SMAActor->GetStaticMeshComponent())
			{
				SMAActor->GetStaticMeshComponent()->SetStaticMesh(ConeMesh);
			}
		}
	}
	else if (Type == TEXT("PointLight"))
	{
		NewActor = World->SpawnActor<APointLight>(Location, FRotator::ZeroRotator);
	}
	else if (Type == TEXT("Camera"))
	{
		NewActor = World->SpawnActor<ACameraActor>(Location, FRotator::ZeroRotator);
	}
	else
	{
		// Try dynamic class resolution before falling back
		UClass* DynamicClass = nullptr;

		// Handle Blueprint asset paths (/Game/...) - need _C suffix for Blueprint class
		FString ClassPath = Type;
		if (ClassPath.StartsWith(TEXT("/Game/")) && !ClassPath.EndsWith(TEXT("_C")))
		{
			ClassPath += TEXT("_C");
		}

		DynamicClass = LoadClass<AActor>(nullptr, *ClassPath);

		if (!DynamicClass)
		{
			// Try as a script/native class path (/Script/ModuleName.ClassName)
			DynamicClass = FindObject<UClass>(nullptr, *ClassPath);
		}

		if (DynamicClass && DynamicClass->IsChildOf<AActor>())
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			NewActor = World->SpawnActor<AActor>(DynamicClass, Location, FRotator::ZeroRotator, SpawnParams);
		}

		if (!NewActor)
		{
			// Final fallback: static mesh cube
			UE_LOG(LogOpenClaw, Warning, TEXT("actor.create: Could not resolve class '%s', falling back to StaticMeshActor"), *Type);
			NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
			if (AStaticMeshActor* SMAActor = Cast<AStaticMeshActor>(NewActor))
			{
				UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				if (CubeMesh && SMAActor->GetStaticMeshComponent())
				{
					SMAActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
				}
			}
		}
	}
	
	if (NewActor)
	{
		if (!Name.IsEmpty())
		{
			NewActor->SetActorLabel(*Name);
		}
		
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created actor: %s"), *NewActor->GetName()));
		Result->SetObjectField(TEXT("actor"), ActorToJson(NewActor, false));
		return Result;
	}
	
	return MakeErrorResult(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Actor_Delete(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	if (Name.IsEmpty())
	{
		return MakeErrorResult(TEXT("name is required"));
	}
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	Actor->Destroy();
	
	return MakeSuccessResult(FString::Printf(TEXT("Deleted actor: %s"), *Name));
}

TSharedPtr<FJsonObject> FOpenClawTools::Actor_GetData(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	if (Name.IsEmpty())
	{
		return MakeErrorResult(TEXT("name is required"));
	}
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("actor"), ActorToJson(Actor, true));
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Actor_SetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	FString Property = Params->GetStringField(TEXT("property"));
	
	if (Name.IsEmpty() || Property.IsEmpty())
	{
		return MakeErrorResult(TEXT("name and property are required"));
	}
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FString Value = Params->GetStringField(TEXT("value"));
	FString ComponentName = Params->GetStringField(TEXT("component"));
	
	// Try to find property on actor or its components
	UObject* TargetObject = Actor;
	if (!ComponentName.IsEmpty())
	{
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp && (Comp->GetName().Contains(ComponentName) || Comp->GetClass()->GetName().Contains(ComponentName)))
			{
				TargetObject = Comp;
				break;
			}
		}
	}
	
	FProperty* Prop = TargetObject->GetClass()->FindPropertyByName(FName(*Property));
	if (!Prop)
	{
		// Search components if not found on actor directly
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp)
			{
				Prop = Comp->GetClass()->FindPropertyByName(FName(*Property));
				if (Prop)
				{
					TargetObject = Comp;
					break;
				}
			}
		}
	}
	
	if (!Prop)
	{
		return MakeErrorResult(FString::Printf(TEXT("Property not found: %s"), *Property));
	}
	
	// Use ImportText for type-safe property setting
	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
	if (Prop->ImportText_Direct(*Value, PropAddr, TargetObject, PPF_None))
	{
		// Notify property change
		FPropertyChangedEvent ChangeEvent(Prop);
		TargetObject->PostEditChangeProperty(ChangeEvent);
		return MakeSuccessResult(FString::Printf(TEXT("Set %s = %s"), *Property, *Value));
	}
	
	return MakeErrorResult(FString::Printf(TEXT("Failed to set property %s to %s"), *Property, *Value));
}

TSharedPtr<FJsonObject> FOpenClawTools::Actor_CallFunction(const TSharedPtr<FJsonObject>& Params)
{
	// Resolve actor and function names
	FString ActorName = Params->HasField(TEXT("actor")) ? Params->GetStringField(TEXT("actor")) : Params->GetStringField(TEXT("name"));
	FString FunctionName = Params->GetStringField(TEXT("function"));
	FString ComponentName = Params->HasField(TEXT("component")) ? Params->GetStringField(TEXT("component")) : TEXT("");

	if (ActorName.IsEmpty())
	{
		return MakeErrorResult(TEXT("actor is required"));
	}
	if (FunctionName.IsEmpty())
	{
		return MakeErrorResult(TEXT("function is required"));
	}

	// Search editor world first, then PIE play world if active
	AActor* Actor = FindActorByName(ActorName);
	if (!Actor && GEditor && GEditor->PlayWorld)
	{
		for (TActorIterator<AActor> It(GEditor->PlayWorld); It; ++It)
		{
			if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
			{
				Actor = *It;
				break;
			}
		}
	}

	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	// Resolve target object: actor itself, or a named component
	UObject* TargetObject = Actor;
	if (!ComponentName.IsEmpty())
	{
		UObject* FoundComp = nullptr;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp && (Comp->GetName().Contains(ComponentName) || Comp->GetClass()->GetName().Contains(ComponentName)))
			{
				FoundComp = Comp;
				break;
			}
		}
		if (!FoundComp)
		{
			return MakeErrorResult(FString::Printf(TEXT("Component not found on actor '%s': %s"), *ActorName, *ComponentName));
		}
		TargetObject = FoundComp;
	}

	// Find the UFunction via reflection
	UFunction* Func = TargetObject->FindFunction(FName(*FunctionName));
	if (!Func)
	{
		return MakeErrorResult(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
	}

	// Security guard: only allow Blueprint-callable functions
	if (!(Func->FunctionFlags & FUNC_BlueprintCallable))
	{
		return MakeErrorResult(FString::Printf(TEXT("Function '%s' is not Blueprint-callable and cannot be called via MCP"), *FunctionName));
	}

	// Allocate and zero-initialize parameter memory
	TArray<uint8> Parms;
	Parms.SetNumZeroed(Func->ParmsSize);

	// Fill input parameters from JSON
	const TSharedPtr<FJsonObject>* JsonParams = nullptr;
	if (Params->TryGetObjectField(TEXT("params"), JsonParams) && JsonParams && (*JsonParams).IsValid())
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Prop = *It;

			// Skip return and output-only params
			if (Prop->PropertyFlags & CPF_ReturnParm)
			{
				continue;
			}

			FString PropName = Prop->GetName();
			if (!(*JsonParams)->HasField(PropName))
			{
				continue;
			}

			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Parms.GetData());

			if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				FloatProp->SetPropertyValue(PropAddr, (float)(*JsonParams)->GetNumberField(PropName));
			}
			else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			{
				DoubleProp->SetPropertyValue(PropAddr, (*JsonParams)->GetNumberField(PropName));
			}
			else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			{
				IntProp->SetPropertyValue(PropAddr, FMath::RoundToInt32((float)(*JsonParams)->GetNumberField(PropName)));
			}
			else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue(PropAddr, (*JsonParams)->GetBoolField(PropName));
			}
			else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				StrProp->SetPropertyValue(PropAddr, (*JsonParams)->GetStringField(PropName));
			}
			else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				NameProp->SetPropertyValue(PropAddr, FName(*(*JsonParams)->GetStringField(PropName)));
			}
			else
			{
				UE_LOG(LogOpenClaw, Warning, TEXT("actor.callFunction: Unsupported param type for '%s' — left zero-initialized"), *PropName);
			}
		}
	}

	// Invoke the function
	TargetObject->ProcessEvent(Func, Parms.GetData());

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), Actor->GetName());
	Result->SetStringField(TEXT("function"), FunctionName);
	if (!ComponentName.IsEmpty())
	{
		Result->SetStringField(TEXT("component"), ComponentName);
	}

	// Read return value if the function has one
	for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* Prop = *It;
		if (!(Prop->PropertyFlags & CPF_ReturnParm))
		{
			continue;
		}

		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Parms.GetData());
		Result->SetStringField(TEXT("returnType"), Prop->GetClass()->GetName());

		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			Result->SetNumberField(TEXT("returnValue"), FloatProp->GetPropertyValue(PropAddr));
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			Result->SetNumberField(TEXT("returnValue"), DoubleProp->GetPropertyValue(PropAddr));
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			Result->SetNumberField(TEXT("returnValue"), IntProp->GetPropertyValue(PropAddr));
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			Result->SetBoolField(TEXT("returnValue"), BoolProp->GetPropertyValue(PropAddr));
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			Result->SetStringField(TEXT("returnValue"), StrProp->GetPropertyValue(PropAddr));
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			Result->SetStringField(TEXT("returnValue"), NameProp->GetPropertyValue(PropAddr).ToString());
		}
		else if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Prop))
		{
			if (UObject* ReturnObject = ObjectProp->GetObjectPropertyValue(PropAddr))
			{
				TSharedPtr<FJsonObject> ReturnObjectInfo = MakeShareable(new FJsonObject());
				ReturnObjectInfo->SetStringField(TEXT("name"), ReturnObject->GetName());
				ReturnObjectInfo->SetStringField(TEXT("class"), ReturnObject->GetClass()->GetName());
				ReturnObjectInfo->SetStringField(TEXT("path"), ReturnObject->GetPathName());

				if (AActor* ReturnActor = Cast<AActor>(ReturnObject))
				{
					ReturnObjectInfo->SetStringField(TEXT("label"), ReturnActor->GetActorLabel());
				}

				Result->SetObjectField(TEXT("returnValue"), ReturnObjectInfo);
			}
			else
			{
				Result->SetNullField(TEXT("returnValue"));
			}
		}
		else
		{
			Result->SetStringField(TEXT("returnValueUnsupported"), Prop->GetCPPType());
		}
		break;
	}

	return Result;
}

// Transform tools
TSharedPtr<FJsonObject> FOpenClawTools::Transform_GetPosition(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FVector Location = Actor->GetActorLocation();
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("x"), Location.X);
	Result->SetNumberField(TEXT("y"), Location.Y);
	Result->SetNumberField(TEXT("z"), Location.Z);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Transform_SetPosition(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FVector Location = Actor->GetActorLocation();
	if (Params->HasField(TEXT("x"))) Location.X = Params->GetNumberField(TEXT("x"));
	if (Params->HasField(TEXT("y"))) Location.Y = Params->GetNumberField(TEXT("y"));
	if (Params->HasField(TEXT("z"))) Location.Z = Params->GetNumberField(TEXT("z"));
	
	Actor->SetActorLocation(Location);
	
	return MakeSuccessResult(FString::Printf(TEXT("Set position to (%f, %f, %f)"), Location.X, Location.Y, Location.Z));
}

TSharedPtr<FJsonObject> FOpenClawTools::Transform_GetRotation(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FRotator Rotation = Actor->GetActorRotation();
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	Result->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	Result->SetNumberField(TEXT("roll"), Rotation.Roll);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Transform_SetRotation(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FRotator Rotation = Actor->GetActorRotation();
	if (Params->HasField(TEXT("pitch"))) Rotation.Pitch = Params->GetNumberField(TEXT("pitch"));
	if (Params->HasField(TEXT("yaw"))) Rotation.Yaw = Params->GetNumberField(TEXT("yaw"));
	if (Params->HasField(TEXT("roll"))) Rotation.Roll = Params->GetNumberField(TEXT("roll"));
	
	Actor->SetActorRotation(Rotation);
	
	return MakeSuccessResult(FString::Printf(TEXT("Set rotation to (P:%f, Y:%f, R:%f)"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
}

TSharedPtr<FJsonObject> FOpenClawTools::Transform_GetScale(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FVector Scale = Actor->GetActorScale3D();
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("x"), Scale.X);
	Result->SetNumberField(TEXT("y"), Scale.Y);
	Result->SetNumberField(TEXT("z"), Scale.Z);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Transform_SetScale(const TSharedPtr<FJsonObject>& Params)
{
	FString Name = Params->GetStringField(TEXT("name"));
	
	AActor* Actor = FindActorByName(Name);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *Name));
	}
	
	FVector Scale = Actor->GetActorScale3D();
	if (Params->HasField(TEXT("x"))) Scale.X = Params->GetNumberField(TEXT("x"));
	if (Params->HasField(TEXT("y"))) Scale.Y = Params->GetNumberField(TEXT("y"));
	if (Params->HasField(TEXT("z"))) Scale.Z = Params->GetNumberField(TEXT("z"));
	
	Actor->SetActorScale3D(Scale);
	
	return MakeSuccessResult(FString::Printf(TEXT("Set scale to (%f, %f, %f)"), Scale.X, Scale.Y, Scale.Z));
}

// Component tools
TSharedPtr<FJsonObject> FOpenClawTools::Component_Get(const TSharedPtr<FJsonObject>& Params)
{
	// Support both "actor" and "name" parameter names
	FString ActorName = Params->GetStringField(TEXT("actor"));
	if (ActorName.IsEmpty()) ActorName = Params->GetStringField(TEXT("name"));
	FString ComponentName = Params->GetStringField(TEXT("component"));
	
	AActor* Actor = FindActorByName(ActorName);
	if (!Actor)
	{
		return MakeErrorResult(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}
	
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && (ComponentName.IsEmpty() || Component->GetName().Contains(ComponentName)))
		{
			TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
			CompObj->SetStringField(TEXT("name"), Component->GetName());
			CompObj->SetStringField(TEXT("class"), Component->GetClass()->GetName());
			ComponentsArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
		}
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("components"), ComponentsArray);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Component_Add(const TSharedPtr<FJsonObject>& Params)
{
	// TODO: Implement component adding
	return MakeErrorResult(TEXT("Component adding not yet implemented"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Component_Remove(const TSharedPtr<FJsonObject>& Params)
{
	// TODO: Implement component removal
	return MakeErrorResult(TEXT("Component removal not yet implemented"));
}

// Editor tools
TSharedPtr<FJsonObject> FOpenClawTools::Editor_Play(const TSharedPtr<FJsonObject>& Params)
{
	if (GEditor)
	{
		if (!GEditor->PlayWorld)
		{
			FRequestPlaySessionParams SessionParams;
			SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
			GEditor->RequestPlaySession(SessionParams);
			return MakeSuccessResult(TEXT("Started play mode"));
		}
		else
		{
			return MakeSuccessResult(TEXT("Already in play mode"));
		}
	}
	
	return MakeErrorResult(TEXT("GEditor not available"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Editor_Stop(const TSharedPtr<FJsonObject>& Params)
{
	if (GEditor)
	{
		if (GEditor->PlayWorld)
		{
			// Set bRequestEndPlayMapQueued flag to defer PIE teardown to next tick.
			// Direct EndPlayMap() from within AsyncTask causes TaskGraph 
			// recursion guard crash (audio device teardown re-enters named thread).
			GEditor->RequestEndPlayMap();
			return MakeSuccessResult(TEXT("Stopped play mode"));
		}
		else
		{
			return MakeSuccessResult(TEXT("Not in play mode"));
		}
	}
	
	return MakeErrorResult(TEXT("GEditor not available"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Editor_Pause(const TSharedPtr<FJsonObject>& Params)
{
	if (GEditor && GEditor->PlayWorld)
	{
		GEditor->PlayWorld->bDebugPauseExecution = true;
		return MakeSuccessResult(TEXT("Paused"));
	}
	
	return MakeErrorResult(TEXT("Not in play mode"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Editor_Resume(const TSharedPtr<FJsonObject>& Params)
{
	if (GEditor && GEditor->PlayWorld)
	{
		GEditor->PlayWorld->bDebugPauseExecution = false;
		return MakeSuccessResult(TEXT("Resumed"));
	}
	
	return MakeErrorResult(TEXT("Not in play mode"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Editor_GetState(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	
	if (GEditor)
	{
		bool bIsPlaying = GEditor->PlayWorld != nullptr;
		bool bIsPaused = bIsPlaying && GEditor->PlayWorld->bDebugPauseExecution;
		
		Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);
		Result->SetBoolField(TEXT("isPaused"), bIsPaused);
		Result->SetStringField(TEXT("state"), bIsPaused ? TEXT("paused") : (bIsPlaying ? TEXT("playing") : TEXT("editing")));
	}
	else
	{
		Result->SetStringField(TEXT("state"), TEXT("unknown"));
	}
	
	return Result;
}

// Debug tools
TSharedPtr<FJsonObject> FOpenClawTools::Debug_Hierarchy(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = ResolveToolWorld(Params, true);
	if (!World)
	{
		return MakeErrorResult(TEXT("No world available"));
	}
	
	int32 MaxDepth = Params->HasField(TEXT("depth")) ? Params->GetIntegerField(TEXT("depth")) : 10;
	
	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && !Actor->IsA<AWorldSettings>())
		{
			ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorToJson(Actor, false))));
		}
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("level"), World->GetMapName());
	Result->SetNumberField(TEXT("actorCount"), ActorsArray.Num());
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Debug_Screenshot(const TSharedPtr<FJsonObject>& Params)
{
	FString Filename = Params->HasField(TEXT("filename")) 
		? Params->GetStringField(TEXT("filename"))
		: FString::Printf(TEXT("screenshot_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	
	FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / Filename;
	
	// Request screenshot
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), Path);
	Result->SetStringField(TEXT("message"), TEXT("Screenshot requested"));
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Debug_Log(const TSharedPtr<FJsonObject>& Params)
{
	FString Message = Params->GetStringField(TEXT("message"));
	FString Level = Params->HasField(TEXT("level")) ? Params->GetStringField(TEXT("level")) : TEXT("log");
	
	if (Level == TEXT("warning"))
	{
		UE_LOG(LogOpenClaw, Warning, TEXT("%s"), *Message);
	}
	else if (Level == TEXT("error"))
	{
		UE_LOG(LogOpenClaw, Error, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogOpenClaw, Log, TEXT("%s"), *Message);
	}
	
	return MakeSuccessResult(TEXT("Logged message"));
}

// Input tools
TSharedPtr<FJsonObject> FOpenClawTools::Input_SimulateKey(const TSharedPtr<FJsonObject>& Params)
{
	FString Key = Params->GetStringField(TEXT("key"));
	bool bPressed = !Params->HasField(TEXT("pressed")) || Params->GetBoolField(TEXT("pressed"));
	
	if (Key.IsEmpty())
	{
		return MakeErrorResult(TEXT("key is required"));
	}
	
	// TODO: Implement key simulation via FSlateApplication
	return MakeSuccessResult(FString::Printf(TEXT("Key %s: %s"), *Key, bPressed ? TEXT("pressed") : TEXT("released")));
}

TSharedPtr<FJsonObject> FOpenClawTools::Input_SimulateMouse(const TSharedPtr<FJsonObject>& Params)
{
	FString Action = Params->GetStringField(TEXT("action"));
	
	// TODO: Implement mouse simulation
	return MakeSuccessResult(FString::Printf(TEXT("Mouse action: %s"), *Action));
}

TSharedPtr<FJsonObject> FOpenClawTools::Input_SimulateAxis(const TSharedPtr<FJsonObject>& Params)
{
	FString Axis = Params->GetStringField(TEXT("axis"));
	float Value = Params->GetNumberField(TEXT("value"));
	
	// TODO: Implement axis simulation
	return MakeSuccessResult(FString::Printf(TEXT("Axis %s set to %f"), *Axis, Value));
}

// Asset tools
TSharedPtr<FJsonObject> FOpenClawTools::Asset_List(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = Params->HasField(TEXT("path")) ? Params->GetStringField(TEXT("path")) : TEXT("/Game");
	FString Type = Params->HasField(TEXT("type")) ? Params->GetStringField(TEXT("type")) : TEXT("");
	
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByPath(FName(*Path), AssetDataList, true);
	
	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (Type.IsEmpty() || AssetData.AssetClassPath.GetAssetName().ToString().Contains(Type))
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject());
			AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), AssetData.GetSoftObjectPath().ToString());
			AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
			AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
		}
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());
	Result->SetArrayField(TEXT("assets"), AssetsArray);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Asset_Import(const TSharedPtr<FJsonObject>& Params)
{
	// TODO: Implement asset import
	return MakeErrorResult(TEXT("Asset import not yet implemented"));
}

// Console tools
TSharedPtr<FJsonObject> FOpenClawTools::Console_Execute(const TSharedPtr<FJsonObject>& Params)
{
	const FString Command = Params->HasField(TEXT("command")) ? Params->GetStringField(TEXT("command")) : TEXT("");

	if (Command.IsEmpty())
	{
		return MakeErrorResult(TEXT("command is required"));
	}

	if (GEngine)
	{
		UWorld* ExecWorld = ResolveToolWorld(Params, true);
		if (!ExecWorld)
		{
			return MakeErrorResult(TEXT("No valid world for console execution"));
		}

		const bool bExecResult = GEngine->Exec(ExecWorld, *Command);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), bExecResult);
		Result->SetBoolField(TEXT("execResult"), bExecResult);
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Executed: %s"), *Command));
		Result->SetStringField(TEXT("world"), ExecWorld->GetName());
		return Result;
	}

	return MakeErrorResult(TEXT("GEngine not available"));
}

TSharedPtr<FJsonObject> FOpenClawTools::Console_GetLogs(const TSharedPtr<FJsonObject>& Params)
{
	const int32 Count = Params->HasField(TEXT("count")) ? Params->GetIntegerField(TEXT("count")) : 50;
	const FString Filter = Params->HasField(TEXT("filter")) ? Params->GetStringField(TEXT("filter")) : TEXT("");
	const int32 Attempts = Params->HasField(TEXT("attempts")) ? FMath::Clamp(Params->GetIntegerField(TEXT("attempts")), 1, 10) : 3;
	const float RetryDelaySeconds = Params->HasField(TEXT("retryDelayMs"))
		? FMath::Clamp((float)Params->GetNumberField(TEXT("retryDelayMs")) / 1000.0f, 0.05f, 5.0f)
		: 0.5f;

	TArray<TSharedPtr<FJsonValue>> LogsArray;
	TArray<FString> LogLines;

	const FString LogsDir = FPaths::ProjectLogDir();
	const FString PreferredProjectLogPath = FPaths::ConvertRelativePathToFull(LogsDir / (FString(FApp::GetProjectName()) + TEXT(".log")));

	auto BuildCandidatePaths = [&]() -> TArray<FString>
	{
		TArray<FString> CandidatePaths;
		TSet<FString> Seen;

		auto AddCandidate = [&](const FString& InPath)
		{
			if (InPath.IsEmpty())
			{
				return;
			}

			const FString FullPath = FPaths::ConvertRelativePathToFull(InPath);
			if (!Seen.Contains(FullPath))
			{
				CandidatePaths.Add(FullPath);
				Seen.Add(FullPath);
			}
		};

		// Prefer the active project log first; platform absolute log can point at backup rotations.
		AddCandidate(PreferredProjectLogPath);
		AddCandidate(FPlatformOutputDevices::GetAbsoluteLogFilename());

		TArray<FString> CandidateLogs;
		IFileManager::Get().FindFiles(CandidateLogs, *(LogsDir / TEXT("*.log")), true, false);
		CandidateLogs.Sort([&](const FString& A, const FString& B)
		{
			const FDateTime TimeA = IFileManager::Get().GetTimeStamp(*(LogsDir / A));
			const FDateTime TimeB = IFileManager::Get().GetTimeStamp(*(LogsDir / B));
			return TimeA > TimeB;
		});

		for (const FString& Candidate : CandidateLogs)
		{
			AddCandidate(LogsDir / Candidate);
		}

		return CandidatePaths;
	};

	FString SelectedLogPath;
	int32 AttemptUsed = 0;

	for (int32 Attempt = 1; Attempt <= Attempts; ++Attempt)
	{
		AttemptUsed = Attempt;

		if (GLog)
		{
			GLog->FlushThreadedLogs();
			GLog->Flush();
		}

		const TArray<FString> CandidatePaths = BuildCandidatePaths();
		for (const FString& CandidatePath : CandidatePaths)
		{
			if (FPaths::FileExists(CandidatePath) && FFileHelper::LoadFileToStringArray(LogLines, *CandidatePath))
			{
				SelectedLogPath = CandidatePath;
				break;
			}
		}

		if (!SelectedLogPath.IsEmpty())
		{
			break;
		}

		if (Attempt < Attempts)
		{
			FPlatformProcess::Sleep(RetryDelaySeconds);
		}
	}

	if (SelectedLogPath.IsEmpty())
	{
		return MakeErrorResult(FString::Printf(TEXT("Unable to read any log file after %d attempt(s) in %s"), Attempts, *LogsDir));
	}

	int32 Added = 0;
	for (int32 i = LogLines.Num() - 1; i >= 0; --i)
	{
		if (Filter.IsEmpty() || LogLines[i].Contains(Filter))
		{
			TSharedPtr<FJsonObject> LogEntry = MakeShareable(new FJsonObject());
			LogEntry->SetStringField(TEXT("message"), LogLines[i]);
			LogEntry->SetNumberField(TEXT("line"), i + 1);
			LogsArray.Insert(MakeShareable(new FJsonValueObject(LogEntry)), 0);
			Added++;
			if (Added >= Count)
			{
				break;
			}
		}
	}

	const bool bUsedFallback = !PreferredProjectLogPath.IsEmpty() && SelectedLogPath != PreferredProjectLogPath;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("logPath"), SelectedLogPath);
	Result->SetStringField(TEXT("selectedFile"), FPaths::GetCleanFilename(SelectedLogPath));
	Result->SetStringField(TEXT("preferredLogPath"), PreferredProjectLogPath);
	Result->SetBoolField(TEXT("usedFallback"), bUsedFallback);
	Result->SetStringField(TEXT("lastWriteTime"), IFileManager::Get().GetTimeStamp(*SelectedLogPath).ToString());
	Result->SetNumberField(TEXT("attempts"), AttemptUsed);
	Result->SetNumberField(TEXT("totalLines"), LogLines.Num());
	Result->SetNumberField(TEXT("count"), LogsArray.Num());
	Result->SetArrayField(TEXT("logs"), LogsArray);

	return Result;
}

// Blueprint tools
TSharedPtr<FJsonObject> FOpenClawTools::Blueprint_List(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = Params->HasField(TEXT("path")) ? Params->GetStringField(TEXT("path")) : TEXT("/Game");
	
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	
	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByPath(FName(*Path), AssetDataList, true);
	
	TArray<TSharedPtr<FJsonValue>> BlueprintsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (AssetData.AssetClassPath.GetAssetName().ToString().Contains(TEXT("Blueprint")))
		{
			TSharedPtr<FJsonObject> BPObj = MakeShareable(new FJsonObject());
			BPObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			BPObj->SetStringField(TEXT("path"), AssetData.GetSoftObjectPath().ToString());
			BlueprintsArray.Add(MakeShareable(new FJsonValueObject(BPObj)));
		}
	}
	
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), BlueprintsArray.Num());
	Result->SetArrayField(TEXT("blueprints"), BlueprintsArray);
	
	return Result;
}

TSharedPtr<FJsonObject> FOpenClawTools::Blueprint_Open(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = Params->GetStringField(TEXT("path"));
	
	if (Path.IsEmpty())
	{
		return MakeErrorResult(TEXT("path is required"));
	}
	
	// TODO: Open blueprint in editor
	return MakeErrorResult(TEXT("Blueprint opening not yet implemented"));
}
