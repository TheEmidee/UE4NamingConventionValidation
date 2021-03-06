#include "NamingConventionValidationCommandlet.h"

#include "NamingConventionValidationLog.h"
#include "EditorNamingValidatorSubsystem.h"

#include <Editor.h>
#include <AssetRegistryHelpers.h>
#include <AssetRegistryModule.h>
#include <IAssetRegistry.h>

UNamingConventionValidationCommandlet::UNamingConventionValidationCommandlet()
{
    LogToConsole = false;
}

int32 UNamingConventionValidationCommandlet::Main( const FString & params )
{
    UE_LOG( LogNamingConventionValidation, Log, TEXT( "--------------------------------------------------------------------------------------------" ) );
    UE_LOG( LogNamingConventionValidation, Log, TEXT( "Running NamingConventionValidation Commandlet" ) );
    TArray< FString > tokens;
    TArray< FString > switches;
    TMap< FString, FString > params_map;
    ParseCommandLine( *params, tokens, switches, params_map );

    // validate data
    if ( !ValidateData() )
    {
        UE_LOG( LogNamingConventionValidation, Warning, TEXT( "Errors occurred while validating naming convention" ) );
        return 2; // return something other than 1 for error since the engine will return 1 if any other system (possibly unrelated) logged errors during execution.
    }

    UE_LOG( LogNamingConventionValidation, Log, TEXT( "Successfully finished running NamingConventionValidation Commandlet" ) );
    UE_LOG( LogNamingConventionValidation, Log, TEXT( "--------------------------------------------------------------------------------------------" ) );
    return 0;
}

//static
bool UNamingConventionValidationCommandlet::ValidateData()
{
    auto & asset_registry_module = FModuleManager::LoadModuleChecked< FAssetRegistryModule >( AssetRegistryConstants::ModuleName );

    TArray< FAssetData > asset_data_list;

    FARFilter filter;
    filter.bRecursivePaths = true;
    filter.PackagePaths.Add( "/Game" );
    asset_registry_module.Get().GetAssets( filter, asset_data_list );

    auto * editor_validator_subsystem = GEditor->GetEditorSubsystem< UEditorNamingValidatorSubsystem >();
    check( editor_validator_subsystem );

    // ReSharper disable once CppExpressionWithoutSideEffects
    editor_validator_subsystem->ValidateAssets( asset_data_list );

    return true;
}
