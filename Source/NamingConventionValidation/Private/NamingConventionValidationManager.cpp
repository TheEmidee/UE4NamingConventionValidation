#include "NamingConventionValidationManager.h"

#include <AssetRegistryModule.h>
#include <AssetToolsModule.h>
#include <CoreGlobals.h>
#include <Developer/MessageLog/Public/MessageLogModule.h>
#include <Editor.h>
#include <IAssetTools.h>
#include <Logging/MessageLog.h>
#include <Misc/ScopedSlowTask.h>
#include <Modules/ModuleManager.h>

#define LOCTEXT_NAMESPACE "NamingConventionValidationManager"

UNamingConventionValidationManager * GNamingConventionValidationManager = nullptr;

UNamingConventionValidationManager::UNamingConventionValidationManager( const FObjectInitializer & object_initializer ) :
    Super( object_initializer )
{
    NamingConventionValidationManagerClassName = FSoftClassPath( TEXT( "/Script/NamingConventionValidation.NamingConventionValidationManager" ) );
    bValidateOnSave = true;
    BlueprintsPrefix = "BP_";
}

UNamingConventionValidationManager * UNamingConventionValidationManager::Get()
{
    if ( GNamingConventionValidationManager == nullptr )
    {
        FSoftClassPath naming_convention_validation_manager_class_name = ( UNamingConventionValidationManager::StaticClass()->GetDefaultObject< UNamingConventionValidationManager >() )->NamingConventionValidationManagerClassName;

        UClass * singleton_class = naming_convention_validation_manager_class_name.TryLoadClass< UObject >();
        checkf( singleton_class != nullptr, TEXT( "Naming Convention Validation config value NamingConventionValidationManagerClassName is not a valid class name." ) );

        GNamingConventionValidationManager = NewObject< UNamingConventionValidationManager >( GetTransientPackage(), singleton_class, NAME_None );
        checkf( GNamingConventionValidationManager != nullptr, TEXT( "Naming Convention validation config value NamingConventionValidationManagerClassName is not a subclass of UNamingConventionValidationManager." ) );

        GNamingConventionValidationManager->AddToRoot();
        GNamingConventionValidationManager->Initialize();
    }

    return GNamingConventionValidationManager;
}

void UNamingConventionValidationManager::Initialize()
{
    FMessageLogInitializationOptions init_options;
    init_options.bShowFilters = true;

    FMessageLogModule & message_log_module = FModuleManager::LoadModuleChecked< FMessageLogModule >( "MessageLog" );
    message_log_module.RegisterLogListing( "NamingConventionValidation", LOCTEXT( "NamingConventionValidation", "Naming Convention Validation" ), init_options );

    for ( auto & class_description : ClassDescriptions )
    {
        class_description.Class = class_description.ClassPath.TryLoadClass< UObject >();

        ensureAlwaysMsgf( class_description.Class != nullptr, TEXT( "Impossible to get a valid UClass for the classpath %s" ), *class_description.ClassPath.ToString() );
    }

    ClassDescriptions.Sort();

    for ( auto & class_path : ExcludedClassPaths )
    {
        auto * excluded_class = class_path.TryLoadClass< UObject >();
        ensureAlwaysMsgf( excluded_class != nullptr, TEXT( "Impossible to get a valid UClass for the excluded classpath %s" ), *class_path.ToString() );

        if ( excluded_class != nullptr )
        {
            ExcludedClasses.Add( excluded_class );
        }
    }

    static const FDirectoryPath 
        engine_directory_path( { TEXT( "/Engine/" ) } );
    
    ExcludedDirectories.Add( engine_directory_path );
}

UNamingConventionValidationManager::~UNamingConventionValidationManager()
{
}

ENamingConventionValidationResult UNamingConventionValidationManager::IsAssetNamedCorrectly( const FAssetData & asset_data, FText & error_message ) const
{
    if ( IsPathExcludedFromValidation( asset_data.PackageName.ToString() ) )
    {
        return ENamingConventionValidationResult::Excluded;
    }

    FName asset_class;
    if ( !TryGetAssetDataRealClass( asset_class, asset_data ) )
    {
        return ENamingConventionValidationResult::Unknown;
    }

    return DoesAssetMatchNameConvention( asset_data, asset_class, error_message );
}

int32 UNamingConventionValidationManager::ValidateAssets( const TArray< FAssetData > & asset_data_list, bool skip_excluded_directories /* = true */, bool show_if_no_failures /* = true */ ) const
{
    FScopedSlowTask slow_task( 1.0f, LOCTEXT( "NamingConventionValidatingDataTask", "Validating Naming Convention..." ) );
    slow_task.Visibility = show_if_no_failures ? ESlowTaskVisibility::ForceVisible : ESlowTaskVisibility::Invisible;

    if ( show_if_no_failures )
    {
        slow_task.MakeDialogDelayed( 0.1f );
    }

    FMessageLog data_validation_log( "NamingConventionValidation" );

    int32 num_added = 0;
    int32 num_files_checked = 0;
    int32 num_valid_files = 0;
    int32 num_invalid_files = 0;
    int32 num_files_skipped = 0;
    int32 num_files_unable_to_validate = 0;

    const auto num_files_to_validate = asset_data_list.Num();

    for ( const FAssetData & asset_data : asset_data_list )
    {
        slow_task.EnterProgressFrame( 1.0f / num_files_to_validate, FText::Format( LOCTEXT( "ValidatingNamingConventionFilename", "Validating Naming Convention {0}" ), FText::FromString( asset_data.GetFullName() ) ) );

        FText error_message;
        const auto result = IsAssetNamedCorrectly( asset_data, error_message );

        switch ( result )
        {
            case ENamingConventionValidationResult::Excluded:
            {
                data_validation_log.Info()
                    ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                    ->AddToken( FTextToken::Create( LOCTEXT( "ExcludedNamingConventionResult", "has not been tested based on the configuration." ) ) );

                ++num_files_skipped;
            }
            break;
            case ENamingConventionValidationResult::Valid:
            {
                ++num_valid_files;
                ++num_files_checked;
            }
            break;
            case ENamingConventionValidationResult::Invalid:
            {
                data_validation_log.Error()
                    ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                    ->AddToken( FTextToken::Create( LOCTEXT( "InvalidNamingConventionResult", "does not match naming convention." ) ) )
                    ->AddToken( FTextToken::Create( error_message ) );

                ++num_invalid_files;
                ++num_files_checked;
            }
            break;
            case ENamingConventionValidationResult::Unknown:
            {
                if ( show_if_no_failures )
                {
                    FFormatNamedArguments arguments;
                    arguments.Add( TEXT( "ClassName" ), FText::FromString( asset_data.AssetClass.ToString() ) );

                    data_validation_log.Warning()
                        ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                        ->AddToken( FTextToken::Create( LOCTEXT( "UnknownNamingConventionResult", "has no known naming convention." ) ) )
                        ->AddToken( FTextToken::Create( FText::Format( LOCTEXT( "UnknownClass", " Class = {ClassName}" ), arguments ) ) );
                }
                ++num_files_checked;
                ++num_files_unable_to_validate;
            }
            break;
        }
    }

    const auto has_failed = ( num_invalid_files > 0 );

    if ( has_failed || show_if_no_failures )
    {
        FFormatNamedArguments arguments;
        arguments.Add( TEXT( "Result" ), has_failed ? LOCTEXT( "Failed", "FAILED" ) : LOCTEXT( "Succeeded", "SUCCEEDED" ) );
        arguments.Add( TEXT( "NumChecked" ), num_files_checked );
        arguments.Add( TEXT( "NumValid" ), num_valid_files );
        arguments.Add( TEXT( "NumInvalid" ), num_invalid_files );
        arguments.Add( TEXT( "NumSkipped" ), num_files_skipped );
        arguments.Add( TEXT( "NumUnableToValidate" ), num_files_unable_to_validate );

        TSharedRef< FTokenizedMessage > validation_log = has_failed ? data_validation_log.Error() : data_validation_log.Info();
        validation_log->AddToken( FTextToken::Create( FText::Format( LOCTEXT( "SuccessOrFailure", "NamingConvention Validation {Result}." ), arguments ) ) );
        validation_log->AddToken( FTextToken::Create( FText::Format( LOCTEXT( "ResultsSummary", "Files Checked: {NumChecked}, Passed: {NumValid}, Failed: {NumInvalid}, Skipped: {NumSkipped}, Unable to validate: {NumUnableToValidate}" ), arguments ) ) );

        data_validation_log.Open( EMessageSeverity::Info, true );
    }

    return num_invalid_files;
}

void UNamingConventionValidationManager::ValidateOnSave( const TArray< FAssetData > & asset_data_list ) const
{
    if ( !bValidateOnSave || GEditor->IsAutosaving() )
    {
        return;
    }

    FMessageLog data_validation_log( "NamingConventionValidation" );

    if ( ValidateAssets( asset_data_list, true, false ) > 0 )
    {
        const FText error_message_notification = FText::Format(
            LOCTEXT( "ValidationFailureNotification", "Naming Convention Validation failed when saving {0}, check Naming Convention Validation log" ),
            asset_data_list.Num() == 1 ? FText::FromName( asset_data_list[ 0 ].AssetName ) : LOCTEXT( "MultipleErrors", "multiple assets" ) );
        data_validation_log.Notify( error_message_notification, EMessageSeverity::Warning, /*bForce=*/true );
    }
}

void UNamingConventionValidationManager::ValidateSavedPackage( FName package_name )
{
    if ( !bValidateOnSave || GEditor->IsAutosaving() )
    {
        return;
    }

    SavedPackagesToValidate.AddUnique( package_name );

    GEditor->GetTimerManager()->SetTimerForNextTick( this, &UNamingConventionValidationManager::ValidateAllSavedPackages );
}

int32 UNamingConventionValidationManager::RenameAssets( const TArray< FAssetData > & asset_data_list, bool skip_excluded_directories, bool show_if_no_failures ) const
{
    FScopedSlowTask slow_task( 1.0f, LOCTEXT( "NamingConventionValidatingDataTask", "Renaming following Naming Convention..." ) );
    slow_task.Visibility = show_if_no_failures ? ESlowTaskVisibility::ForceVisible : ESlowTaskVisibility::Invisible;

    if ( show_if_no_failures )
    {
        slow_task.MakeDialogDelayed( 0.1f );
    }

    FMessageLog data_validation_log( "NamingConventionValidation" );

    int32 num_added = 0;
    int32 num_files_checked = 0;
    int32 num_files_renamed = 0;
    int32 num_files_skipped = 0;
    int32 num_files_failed = 0;

    const auto num_files_to_validate = asset_data_list.Num();

    for ( const FAssetData & asset_data : asset_data_list )
    {
        slow_task.EnterProgressFrame( 1.0f / num_files_to_validate, FText::Format( LOCTEXT( "ValidatingNamingConventionFilename", "Renaming following Naming Convention {0}" ), FText::FromString( asset_data.GetFullName() ) ) );

        FText error_message;
        const auto result = IsAssetNamedCorrectly( asset_data, error_message );

        switch ( result )
        {
            case ENamingConventionValidationResult::Excluded:
            {
                data_validation_log.Info()
                    ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                    ->AddToken( FTextToken::Create( LOCTEXT( "ExcludedNamingConventionResult", "has not been excluded based on the configuration." ) ) );

                ++num_files_skipped;
            }
            break;
            case ENamingConventionValidationResult::Valid:
            {
                ++num_files_checked;
            }
            break;
            case ENamingConventionValidationResult::Invalid:
            {
                TArray< FAssetRenameData > assets_to_rename;
                const auto old_object_path = asset_data.ToSoftObjectPath();

                FSoftObjectPath new_object_path;
                GetRenamedAssetSoftObjectPath( new_object_path, asset_data );

                FAssetRenameData asset_rename_data( old_object_path, new_object_path );
                assets_to_rename.Emplace( asset_rename_data );

                FAssetToolsModule & module = FModuleManager::GetModuleChecked< FAssetToolsModule >( "AssetTools" );
                if ( !module.Get().RenameAssets( assets_to_rename ) )
                {
                    ++num_files_failed;

                    data_validation_log.Error()
                        ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                        ->AddToken( FTextToken::Create( LOCTEXT( "FailedRenameFollowingNamingConvention", "could not be renamed." ) ) );
                }
                else
                {
                    ++num_files_renamed;

                    data_validation_log.Info()
                        ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                        ->AddToken( FTextToken::Create( LOCTEXT( "SucceededRenameFollowingNamingConvention", "has been renamed to" ) ) )
                        ->AddToken( FAssetNameToken::Create( new_object_path.GetLongPackageName() ) );
                }

                ++num_files_checked;
            }
            break;
            case ENamingConventionValidationResult::Unknown:
            {
                if ( show_if_no_failures )
                {
                    FFormatNamedArguments arguments;
                    arguments.Add( TEXT( "ClassName" ), FText::FromString( asset_data.AssetClass.ToString() ) );

                    data_validation_log.Warning()
                        ->AddToken( FAssetNameToken::Create( asset_data.PackageName.ToString() ) )
                        ->AddToken( FTextToken::Create( LOCTEXT( "UnknownNamingConventionResult", "has no known naming convention." ) ) )
                        ->AddToken( FTextToken::Create( FText::Format( LOCTEXT( "UnknownClass", " Class = {ClassName}" ), arguments ) ) );
                }
                ++num_files_checked;
            }
            break;
        }
    }

    const auto has_failed = ( num_files_failed > 0 );

    if ( has_failed || show_if_no_failures )
    {
        FFormatNamedArguments arguments;
        arguments.Add( TEXT( "Result" ), has_failed ? LOCTEXT( "Failed", "FAILED" ) : LOCTEXT( "Succeeded", "SUCCEEDED" ) );
        arguments.Add( TEXT( "NumChecked" ), num_files_checked );
        arguments.Add( TEXT( "NumRenamed" ), num_files_renamed );
        arguments.Add( TEXT( "NumSkipped" ), num_files_skipped );
        arguments.Add( TEXT( "NumFailed" ), num_files_failed );

        TSharedRef< FTokenizedMessage > validation_log = has_failed ? data_validation_log.Error() : data_validation_log.Info();
        validation_log->AddToken( FTextToken::Create( FText::Format( LOCTEXT( "SuccessOrFailure", "Renaming following NamingConvention {Result}." ), arguments ) ) );
        validation_log->AddToken( FTextToken::Create( FText::Format( LOCTEXT( "ResultsSummary", "Files Checked: {NumChecked}, Renamed: {NumRenamed}, Failed: {NumFailed}, Skipped: {NumSkipped}" ), arguments ) ) );

        data_validation_log.Open( EMessageSeverity::Info, true );
    }

    return num_files_failed;
}

// -- PROTECTED

bool UNamingConventionValidationManager::IsPathExcludedFromValidation( const FString & path ) const
{
    for ( const FDirectoryPath & ExcludedPath : ExcludedDirectories )
    {
        if ( path.Contains( ExcludedPath.Path ) )
        {
            return true;
        }
    }

    return false;
}

void UNamingConventionValidationManager::ValidateAllSavedPackages()
{
    FAssetRegistryModule & asset_registry_module = FModuleManager::LoadModuleChecked< FAssetRegistryModule >( "AssetRegistry" );
    TArray< FAssetData > assets;

    for ( FName package_name : SavedPackagesToValidate )
    {
        // We need to query the in-memory data as the disk cache may not be accurate
        asset_registry_module.Get().GetAssetsByPackageName( package_name, assets );
    }

    ValidateOnSave( assets );

    SavedPackagesToValidate.Empty();
}

// -- PRIVATE

ENamingConventionValidationResult UNamingConventionValidationManager::DoesAssetMatchNameConvention( const FAssetData & asset_data, const FName asset_class, FText & error_message ) const
{
    const auto asset_name = asset_data.AssetName.ToString();
    FSoftClassPath asset_class_path( asset_class.ToString() );

    if ( UClass * asset_real_class = asset_class_path.TryLoadClass< UObject >() )
    {
        for ( auto * excluded_class : ExcludedClasses )
        {
            if ( asset_real_class->IsChildOf( excluded_class ) )
            {
                return ENamingConventionValidationResult::Excluded;
            }
        }

        for ( const auto & class_description : ClassDescriptions )
        {
            if ( asset_real_class->IsChildOf( class_description.Class ) )
            {
                if ( !class_description.Prefix.IsEmpty() )
                {
                    if ( !asset_name.StartsWith( class_description.Prefix ) )
                    {
                        error_message = FText::Format( LOCTEXT( "WrongPrefix", "Assets of class '{0}' must have a name which starts with {1}" ), FText::FromString( class_description.ClassPath.ToString() ), FText::FromString( class_description.Prefix ) );
                        return ENamingConventionValidationResult::Invalid;
                    }
                }

                if ( !class_description.Suffix.IsEmpty() )
                {
                    if ( !asset_name.EndsWith( class_description.Suffix ) )
                    {
                        error_message = FText::Format( LOCTEXT( "WrongSuffix", "Assets of class '{0}' must have a name which ends with {1}" ), FText::FromString( class_description.ClassPath.ToString() ), FText::FromString( class_description.Prefix ) );
                        return ENamingConventionValidationResult::Invalid;
                    }
                }

                return ENamingConventionValidationResult::Valid;
            }
        }
    }

    static const FName blueprint_class_name( "Blueprint" );
    if ( asset_data.AssetClass == blueprint_class_name )
    {
        if ( !asset_name.StartsWith( BlueprintsPrefix ) )
        {
            error_message = FText::FromString( TEXT( "Generic blueprint assets must start with BP_" ) );
            return ENamingConventionValidationResult::Invalid;
        }

        return ENamingConventionValidationResult::Valid;
    }

    return ENamingConventionValidationResult::Unknown;
}

void UNamingConventionValidationManager::GetRenamedAssetSoftObjectPath( FSoftObjectPath & renamed_soft_object_path, const FAssetData & asset_data ) const
{
    FSoftObjectPath path = asset_data.ToSoftObjectPath();
    FName asset_class;

    // /Game/Levels/Props/Meshes/1M_Cube.1M_Cube
    TryGetAssetDataRealClass( asset_class, asset_data );

    FString renamed_path = FPaths::GetPath( path.GetLongPackageName() );
    FString renamed_name = path.GetAssetName();

    const auto asset_name = asset_data.AssetName.ToString();
    FSoftClassPath asset_class_path( asset_class.ToString() );

    if ( UClass * asset_real_class = asset_class_path.TryLoadClass< UObject >() )
    {
        for ( const auto & class_description : ClassDescriptions )
        {
            if ( asset_real_class->IsChildOf( class_description.Class ) )
            {
                if ( !class_description.Prefix.IsEmpty() )
                {
                    renamed_name.InsertAt( 0, class_description.Prefix );
                }

                if ( !class_description.Suffix.IsEmpty() )
                {
                    renamed_name.Append( class_description.Suffix );
                }

                break;
            }
        }
    }

    if ( renamed_name == path.GetAssetName() )
    {
        static const FName blueprint_class_name( "Blueprint" );
        if ( asset_data.AssetClass == blueprint_class_name )
        {
            renamed_name.InsertAt( 0, BlueprintsPrefix );
        }
    }

    renamed_path.Append( "/" );
    renamed_path.Append( renamed_name );
    renamed_path.Append( "." );
    renamed_path.Append( renamed_name );

    renamed_soft_object_path.SetPath( renamed_path );
}

bool UNamingConventionValidationManager::TryGetAssetDataRealClass( FName & asset_class, const FAssetData & asset_data ) const
{
    static const FName
        native_parent_class_key( "NativeParentClass" ),
        native_class_key( "NativeClass" );

    if ( !asset_data.GetTagValue( native_parent_class_key, asset_class ) )
    {
        if ( !asset_data.GetTagValue( native_class_key, asset_class ) )
        {
            if ( auto * asset = asset_data.GetAsset() )
            {
                FSoftClassPath class_path( asset->GetClass() );
                asset_class = *class_path.ToString();
            }
            else
            {
                return false;
            }
        }
    }

    return true;
}

#undef LOCTEXT_NAMESPACE
