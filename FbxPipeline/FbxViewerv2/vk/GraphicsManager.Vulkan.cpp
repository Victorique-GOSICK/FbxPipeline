//#include <GameEngine.GraphicsEcosystem.Precompiled.h>
#include <GraphicsDevice.Vulkan.h>
#include <GraphicsManager.Vulkan.h>

#include <NativeDispatchableHandles.Vulkan.h>
#include <SystemAllocationCallbacks.Vulkan.h>
#include <TInfoStruct.Vulkan.h>

#include <GraphicsManager.KnownExtensions.Vulkan.h>
#include <GraphicsManager.KnownLayers.Vulkan.h>

/// -------------------------------------------------------------------------------------------------------------------
/// GraphicsEcosystem PrivateContent
/// -------------------------------------------------------------------------------------------------------------------

apemodevk::GraphicsManager::APIVersion::APIVersion( bool bDump )
    : Major( VK_API_VERSION_1_0 >> 22 )
    , Minor( ( VK_API_VERSION_1_0 >> 12 ) & 0x3ff )
    , Patch( VK_API_VERSION_1_0 & 0xfff ) {
    //_Aux_DebugTraceF("Vulkan %u.%u.%u", Major, Minor, Patch);
}

#define _Game_app_filter "GameApp/"
#define _Game_app_filter_length sizeof( _Game_app_filter )
#define _Game_app_UUID_length 64

bool apemodevk::GraphicsManager::ScanInstanceLayerProperties( ) {
    uint32_t                LayerPropertyCount = 0;
    ResultHandle            ErrorHandle;
    VkLayerPropertiesVector GlobalLayers;

    do {
        ErrorHandle = vkEnumerateInstanceLayerProperties( &LayerPropertyCount, NULL );
        _Game_engine_Assert( ErrorHandle, "vkEnumerateInstanceLayerProperties failed." );

        if ( ErrorHandle.Succeeded( ) && LayerPropertyCount ) {
            GlobalLayers.resize( LayerPropertyCount );
            ErrorHandle = vkEnumerateInstanceLayerProperties( &LayerPropertyCount, GlobalLayers.data( ) );
        }

    } while ( ErrorHandle == ResultHandle::Incomplete );

    _Game_engine_Assert( ErrorHandle, "vkEnumerateInstanceLayerProperties failed." );
    LayerWrappers.reserve( LayerPropertyCount + 1 );

    auto ResolveLayerExtensions = [this]( VkLayerProperties const *Layer ) {
        ResultHandle ErrorHandle;
        uint32_t     LayerExtensionCount = 0;

        const bool  bIsUnnamed   = !Layer;
        auto &      LayerWrapper = apemodevk::PushBackAndGet( LayerWrappers );
        char const *LayerName    = bIsUnnamed ? LayerWrapper.Layer->layerName : nullptr;

        LayerWrapper.Layer      = !bIsUnnamed ? *Layer : VkLayerProperties( );
        LayerWrapper.bIsUnnamed = bIsUnnamed;

        do {
            ErrorHandle = vkEnumerateInstanceExtensionProperties( LayerName, &LayerExtensionCount, NULL );
            _Game_engine_Assert( ErrorHandle, "vkEnumerateInstanceExtensionProperties failed." );

            if ( ErrorHandle.Succeeded( ) && LayerExtensionCount ) {
                LayerWrapper.Extensions.resize( LayerExtensionCount );
                ErrorHandle =
                    vkEnumerateInstanceExtensionProperties( LayerName, &LayerExtensionCount, LayerWrapper.Extensions.data( ) );
            }

        } while ( ErrorHandle == ResultHandle::Incomplete );

        _Game_engine_Assert( ErrorHandle, "vkEnumerateInstanceExtensionProperties failed." );
        _Game_engine_Assert( LayerWrapper.IsValidInstanceLayer( ), "Layer is invalid" );

        LayerWrapper.DumpExtensions( );
    };

    auto ResolveLayerExtensionsRef = [this, &ResolveLayerExtensions]( VkLayerProperties const &Layer ) {
        ResolveLayerExtensions( &Layer );
    };

    ResolveLayerExtensions( nullptr );
    std::for_each( GlobalLayers.begin( ), GlobalLayers.end( ), ResolveLayerExtensionsRef );

    PresentLayers.reserve( GlobalLayers.size( ) );

    auto       LayerWrapperIt    = LayerWrappers.begin( );
    auto const LayerWrapperEndIt = LayerWrappers.end( );
    std::advance( LayerWrapperIt, 1 );

    auto ResolveLayerName = [&]( NativeLayerWrapper const &LayerWrapper ) {
        if ( strcmp( LayerWrapper.Layer->layerName, "VK_LAYER_LUNARG_vktrace" ) &&
             strcmp( LayerWrapper.Layer->layerName, "VK_LAYER_RENDERDOC_Capture" ) ) {
            //_Aux_DebugTraceF(" + %s", LayerWrapper.Layer->layerName);
            PresentLayers.push_back( LayerWrapper.Layer->layerName );
        } else {
            //_Aux_DebugTraceF(" - %s ", LayerWrapper.Layer->layerName);
        }
    };

    std::for_each( LayerWrapperIt, LayerWrapperEndIt, ResolveLayerName );

    PresentExtensions.reserve( GetUnnamedLayer( ).Extensions.size( ) );

    auto const ExtensionIt    = GetUnnamedLayer( ).Extensions.begin( );
    auto const ExtensionEndIt = GetUnnamedLayer( ).Extensions.end( );
    auto ResolveExtensionName = [&]( VkExtensionProperties const &Extension ) {
        //_Aux_DebugTraceF(" > %s", Extension.extensionName);
        return Extension.extensionName;
    };
    std::transform( ExtensionIt, ExtensionEndIt, std::back_inserter( PresentExtensions ), ResolveExtensionName );

    return ErrorHandle.Succeeded( );
}

bool apemodevk::GraphicsManager::ScanAdapters( ) {
    uint32_t               AdaptersFound = 0;
    VkPhysicalDeviceVector Adapters;

    ResultHandle ErrorHandle;
    ErrorHandle = vkEnumeratePhysicalDevices( InstanceHandle, &AdaptersFound, NULL );
    _Game_engine_Assert( ErrorHandle, "vkEnumeratePhysicalDevices failed." );

    Adapters.resize( AdaptersFound );
    ErrorHandle = vkEnumeratePhysicalDevices( InstanceHandle, &AdaptersFound, Adapters.data( ) );
    _Game_engine_Assert( ErrorHandle, "vkEnumeratePhysicalDevices failed." );

    // TODO:
    //      Choose the best 2 nodes here.
    //      Ensure the integrate GPU is always the secondary one.

    std::for_each( Adapters.begin( ), Adapters.end( ), [&]( VkPhysicalDevice const &Adapter ) {
        GraphicsDevice *CurrentNode = nullptr;

        if ( GetPrimaryGraphicsNode( ) == nullptr ) {
            PrimaryNode.swap( GraphicsDevice::MakeNewUnique( ) );
            CurrentNode = GetPrimaryGraphicsNode( );
            //_Aux_DebugTraceF("Creating primary node...");
        } else if ( GetSecondaryGraphicsNode( ) == nullptr ) {
            SecondaryNode.swap( GraphicsDevice::MakeNewUnique( ) );
            CurrentNode = GetSecondaryGraphicsNode( );
            //_Aux_DebugTraceF("Creating secondary node...");
        }

        if ( CurrentNode != nullptr ) {
            CurrentNode->RecreateResourcesFor( Adapter, *this );
        } else {
            //_Game_engine_Warn_once_if(true, "We can handle at most 2 devices with this ecosystem.");
        }
    } );

    return AdaptersFound != 0;
}

apemodevk::GraphicsManager::NativeLayerWrapper &apemodevk::GraphicsManager::GetUnnamedLayer( ) {
    _Game_engine_Assert( LayerWrappers.front( ).IsUnnamedLayer( ), "vkEnumerateInstanceExtensionProperties failed." );
    return LayerWrappers.front( );
}

bool apemodevk::GraphicsManager::InitializeInstance( ) {
    if ( !ScanInstanceLayerProperties( ) )
        return false;

    TInfoStruct< VkApplicationInfo > AppDesc;
    AppDesc->apiVersion         = VK_API_VERSION_1_0;
    AppDesc->pApplicationName   = AppName.c_str( );
    AppDesc->pEngineName        = EngineName.c_str( );
    AppDesc->applicationVersion = 1;
    AppDesc->engineVersion      = 1;
    AppDesc->pNext              = VK_NULL_HANDLE;

    // clang-format off
    auto DebugFlags 
        = VK_DEBUG_REPORT_ERROR_BIT_EXT
        | VK_DEBUG_REPORT_DEBUG_BIT_EXT
        | VK_DEBUG_REPORT_WARNING_BIT_EXT
        | VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
    // clang-format on

    TInfoStruct< VkDebugReportCallbackCreateInfoEXT > DebugDesc;
    DebugDesc->pfnCallback = DebugCallback;
    DebugDesc->pUserData   = this;
    DebugDesc->sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    DebugDesc->flags       = DebugFlags;

    TInfoStruct< VkInstanceCreateInfo > InstanceDesc;
    InstanceDesc->pNext                   = DebugDesc;
    InstanceDesc->pApplicationInfo        = AppDesc;
    InstanceDesc->enabledLayerCount       = _Get_collection_length_u( PresentLayers );
    InstanceDesc->ppEnabledLayerNames     = PresentLayers.data( );
    InstanceDesc->enabledExtensionCount   = _Get_collection_length_u( PresentExtensions );
    InstanceDesc->ppEnabledExtensionNames = PresentExtensions.data( );

    /*
    Those layers cannot be used as standalone layers (please, see vktrace, renderdoc docs)
    -----------------------------------------------------------------
        Layer VK_LAYER_LUNARG_vktrace (impl 0x00000001, spec 0x00400003):
                Extension VK_KHR_surface (spec 0x00000019)
                Extension VK_KHR_win32_surface (spec 0x00000005)
                Extension VK_EXT_debug_report (spec 0x00000001)
    -----------------------------------------------------------------
        Layer VK_LAYER_RENDERDOC_Capture (impl 0x0000001a, spec 0x000d2001):
                Extension VK_KHR_surface (spec 0x00000019)
                Extension VK_KHR_win32_surface (spec 0x00000005)
                Extension VK_EXT_debug_report (spec 0x00000001)
    -----------------------------------------------------------------
    */

    bool bIsOk = InstanceHandle.Recreate( InstanceDesc );
    _Game_engine_Assert( bIsOk, "vkCreateInstance failed." );

    if ( !ScanAdapters( ) )
        return false;

    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL apemodevk::GraphicsManager::BreakCallback( VkFlags                    msgFlags,
                                                                        VkDebugReportObjectTypeEXT objType,
                                                                        uint64_t                   srcObject,
                                                                        size_t                     location,
                                                                        int32_t                    msgCode,
                                                                        const char *               pLayerPrefix,
                                                                        const char *               pMsg,
                                                                        void *                     pUserData ) {
    if ( msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT ) {
        DebugBreak();
    }

    /*
    * false indicates that layer should not bail-out of an
    * API call that had validation failures. This may mean that the
    * app dies inside the driver due to invalid parameter(s).
    * That's what would happen without validation layers, so we'll
    * keep that behavior here.
    */

    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL apemodevk::GraphicsManager::DebugCallback( VkFlags                    msgFlags,
                                                                        VkDebugReportObjectTypeEXT objType,
                                                                        uint64_t                   srcObject,
                                                                        size_t                     location,
                                                                        int32_t                    msgCode,
                                                                        const char *               pLayerPrefix,
                                                                        const char *               pMsg,
                                                                        void *                     pUserData ) {
    if ( msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT ) {
        OutputDebugStringA("\n E R R O R:");
        OutputDebugStringA(pLayerPrefix);
        OutputDebugStringA("\n");
        OutputDebugStringA(pMsg);
        OutputDebugStringA("\n");
        /*apemodevk::DebugTrace<true, apemodevk::Error> (
            "\tDebugCallback: [%s] Code %d: %s", pLayerPrefix, msgCode, pMsg);
        apemodevk::Platform::DebugBreak ();*/
    } else if ( msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT ) {
        OutputDebugStringA("\n W A R N I N G:");
        OutputDebugStringA(pLayerPrefix);
        OutputDebugStringA("\n");
        OutputDebugStringA(pMsg);
        OutputDebugStringA("\n");
        /*apemodevk::DebugTrace<true, apemodevk::Warning> (
            "\tDebugCallback: [%s] Code %d: %s", pLayerPrefix, msgCode, pMsg);*/
    } else if ( msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ) {
        /*apemodevk::DebugTrace<true, apemodevk::Info> (
            "\tDebugCallback: [%s] Code %d: %s", pLayerPrefix, msgCode, pMsg);*/
    } else if ( msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT ) {
        /*apemodevk::DebugTrace<true, apemodevk::Debug> (
            "\tDebugCallback: [%s] Code %d: %s", pLayerPrefix, msgCode, pMsg);*/
    }

    /*
    * false indicates that layer should not bail-out of an
    * API call that had validation failures. This may mean that the
    * app dies inside the driver due to invalid parameter(s).
    * That's what would happen without validation layers, so we'll
    * keep that behavior here.
    */

    return false;
}

bool apemodevk::GraphicsManager::NativeLayerWrapper::IsUnnamedLayer( ) const {
    return bIsUnnamed;
}

bool apemodevk::GraphicsManager::NativeLayerWrapper::IsValidInstanceLayer( ) const {
    if ( IsUnnamedLayer( ) ) {
        auto const KnownExtensionBeginIt = Vulkan::KnownInstanceExtensions;
        auto const KnownExtensionEndIt   = Vulkan::KnownInstanceExtensions + Vulkan::KnownInstanceExtensionCount;
        size_t     KnownExtensionsFound =
            std::count_if( KnownExtensionBeginIt, KnownExtensionEndIt, [this]( char const *KnownExtension ) {
                auto const ExtensionBeginIt = Extensions.begin( );
                auto const ExtensionEndIt   = Extensions.end( );
                auto const FoundExtensionIt =
                    std::find_if( ExtensionBeginIt, ExtensionEndIt, [&]( VkExtensionProperties const &Extension ) {
                        static const errno_t eStrCmp_EqualStrings = 0;
                        return strcmp( KnownExtension, Extension.extensionName ) == eStrCmp_EqualStrings;
                    } );

                auto const bIsExtensionFound = FoundExtensionIt != ExtensionEndIt;
                _Game_engine_Assert( bIsExtensionFound, "Extension '%s' was not found.", KnownExtension );

                return bIsExtensionFound;
            } );

        return KnownExtensionsFound == Vulkan::KnownInstanceExtensionCount;
    }

    return true;
}

void apemodevk::GraphicsManager::NativeLayerWrapper::DumpExtensions( ) const {
    /*_Aux_DebugTrace_separator_header;
    _Aux_DebugTraceFunc;

    apemodevk::DebugTrace("\t Layer %s (impl 0x%08x, spec 0x%08x):"
        , bIsUnnamed ? "<Global>" : Layer->layerName
        , Layer->implementationVersion
        , Layer->specVersion
        );

    std::for_each(Extensions.begin(), Extensions.end(), [&](VkExtensionProperties const & Extension)
    {
        apemodevk::DebugTrace("\t\t Extension %s (spec 0x%08x)"
            , Extension.extensionName
            , Extension.specVersion
            );
    });

    _Aux_DebugTrace_separator_footer;*/
}

/// -------------------------------------------------------------------------------------------------------------------
/// GraphicsEcosystem
/// -------------------------------------------------------------------------------------------------------------------

apemodevk::GraphicsManager::GraphicsManager( ) {
    _Aux_DebugTraceFunc;
}

apemodevk::GraphicsManager::~GraphicsManager( ) {
    _Aux_DebugTraceFunc;
}

 apemodevk::GraphicsDevice * apemodevk::GraphicsManager::GetPrimaryGraphicsNode() {
    return PrimaryNode.get();
}

apemodevk::GraphicsDevice * apemodevk::GraphicsManager::GetSecondaryGraphicsNode() {
    return SecondaryNode.get();
}

bool apemodevk::GraphicsManager::RecreateGraphicsNodes( ) {
    auto const bIsInstInitialized = InitializeInstance( );
    _Game_engine_Assert( bIsInstInitialized, "Vulkan Instance initialization failed." );
    return bIsInstInitialized;
}