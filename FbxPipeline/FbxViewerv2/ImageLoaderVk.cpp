#include <BufferPools.Vulkan.h>
#include <ImageLoaderVk.h>
#include <QueuePools.Vulkan.h>

#ifndef LODEPNG_COMPILE_ALLOCATORS
#define LODEPNG_COMPILE_ALLOCATORS
#endif

void* lodepng_malloc( size_t size ) {
    return malloc( size );
}

void* lodepng_realloc( void* ptr, size_t new_size ) {
    return realloc( ptr, new_size );
}

void lodepng_free( void* ptr ) {
    free( ptr );
}

#include <lodepng.h>
#include <lodepng_util.h>
#include <gli/gli.hpp>

/**
 * Exactly the same, no need for explicit mapping.
 * But still decided to leave it as a function in case GLI stops maintain such compatibility.
 */
VkFormat ToImgFormat( gli::format textureFormat ) {
    return static_cast< VkFormat >( textureFormat );
}

VkImageType ToImgType( gli::target textureTarget ) {
    switch ( textureTarget ) {
        case gli::TARGET_1D:
            return VK_IMAGE_TYPE_1D;
        case gli::TARGET_1D_ARRAY:
            return VK_IMAGE_TYPE_1D;
        case gli::TARGET_2D:
            return VK_IMAGE_TYPE_2D;
        case gli::TARGET_2D_ARRAY:
            return VK_IMAGE_TYPE_2D;
        case gli::TARGET_3D:
            return VK_IMAGE_TYPE_3D;
        case gli::TARGET_RECT:
            return VK_IMAGE_TYPE_2D;
        case gli::TARGET_RECT_ARRAY:
            return VK_IMAGE_TYPE_2D;
        case gli::TARGET_CUBE:
            return VK_IMAGE_TYPE_2D;
        case gli::TARGET_CUBE_ARRAY:
            return VK_IMAGE_TYPE_2D;
        default:
            return VK_IMAGE_TYPE_MAX_ENUM;
    }
}

VkImageViewType ToImgViewType( gli::target textureTarget ) {
    switch ( textureTarget ) {
        case gli::TARGET_1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case gli::TARGET_1D_ARRAY:
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case gli::TARGET_2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case gli::TARGET_2D_ARRAY:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case gli::TARGET_3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case gli::TARGET_RECT:
            return VK_IMAGE_VIEW_TYPE_2D;
        case gli::TARGET_RECT_ARRAY:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case gli::TARGET_CUBE:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case gli::TARGET_CUBE_ARRAY:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:
            return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }
}

bool apemodevk::ImageLoader::Recreate( GraphicsDevice* pInNode, HostBufferPool* pInHostBufferPool ) {
    pNode = pInNode;

    if ( nullptr == pInHostBufferPool ) {
        pHostBufferPool = new HostBufferPool( );
        pHostBufferPool->Recreate( *pNode, *pNode, &pNode->AdapterProps.limits, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false );
    } else {
        pHostBufferPool = pInHostBufferPool;
    }

    return true;
}

std::unique_ptr< apemodevk::LoadedImage > apemodevk::ImageLoader::LoadImageFromData(
    const std::vector< uint8_t >& InFileContent, EImageFileFormat eFileFormat, bool bImgView, bool bAwaitLoading ) {

    auto loadedImage = std::make_unique< LoadedImage >( );

    VkBufferImageCopy              bufferImageCopy;
    VkImageMemoryBarrier           imageMemoryBarrierWrite;
    VkImageMemoryBarrier           imageMemoryBarrierRead;
    HostBufferPool::SuballocResult imageBufferSuballocResult;

    InitializeStruct( loadedImage->imageCreateInfo );
    InitializeStruct( loadedImage->imageViewCreateInfo );
    InitializeStruct( bufferImageCopy );
    InitializeStruct( imageMemoryBarrierWrite );
    InitializeStruct( imageMemoryBarrierRead );

    /**
     * @note All the data will
     **/
    switch ( eFileFormat ) {
        case apemodevk::ImageLoader::eImageFileFormat_DDS:
        case apemodevk::ImageLoader::eImageFileFormat_KTX: {
            auto texture = gli::load( (const char*) InFileContent.data( ), InFileContent.size( ) );

            if ( false == texture.empty( ) ) {
                loadedImage->imageCreateInfo.format        = ToImgFormat( texture.format( ) );
                loadedImage->imageCreateInfo.imageType     = ToImgType( texture.target( ) );
                loadedImage->imageCreateInfo.extent.width  = (uint32_t) texture.extent( ).x;
                loadedImage->imageCreateInfo.extent.height = (uint32_t) texture.extent( ).y;
                loadedImage->imageCreateInfo.extent.depth  = (uint32_t) texture.extent( ).z;
                loadedImage->imageCreateInfo.mipLevels     = (uint32_t) texture.levels( );
                loadedImage->imageCreateInfo.arrayLayers   = (uint32_t) texture.faces( ) * texture.layers( );
                loadedImage->imageCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
                loadedImage->imageCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
                loadedImage->imageCreateInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                loadedImage->imageCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
                loadedImage->imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                loadedImage->imageViewCreateInfo.flags                       = 0;
                loadedImage->imageViewCreateInfo.format                      = ToImgFormat( texture.format( ) );
                loadedImage->imageViewCreateInfo.viewType                    = ToImgViewType( texture.target( ) );
                loadedImage->imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                loadedImage->imageViewCreateInfo.subresourceRange.levelCount = (uint32_t) texture.levels( );
                loadedImage->imageViewCreateInfo.subresourceRange.layerCount = (uint32_t) texture.faces( ) * texture.layers( );
                loadedImage->imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
                loadedImage->imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;

                pHostBufferPool->Reset( );
                imageBufferSuballocResult = pHostBufferPool->Suballocate( texture.data( ), (uint32_t) texture.size( ) );
                pHostBufferPool->Flush( ); /* Unmap buffers and flush all memory ranges */

                bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bufferImageCopy.imageSubresource.layerCount = (uint32_t) texture.faces( ) * texture.layers( );
                bufferImageCopy.imageExtent.width           = (uint32_t) texture.extent( ).x;
                bufferImageCopy.imageExtent.height          = (uint32_t) texture.extent( ).y;
                bufferImageCopy.imageExtent.depth           = (uint32_t) texture.extent( ).z;
                bufferImageCopy.bufferOffset                = imageBufferSuballocResult.dynamicOffset;
                bufferImageCopy.bufferImageHeight           = 0; /* Tightly packed according to the imageExtent */
                bufferImageCopy.bufferRowLength             = 0; /* Tightly packed according to the imageExtent */

                imageMemoryBarrierWrite.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrierWrite.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
                imageMemoryBarrierWrite.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrierWrite.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageMemoryBarrierWrite.subresourceRange.levelCount = (uint32_t) texture.levels( );
                imageMemoryBarrierWrite.subresourceRange.layerCount = (uint32_t) texture.faces( ) * texture.layers( );

                imageMemoryBarrierRead.srcAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrierRead.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
                imageMemoryBarrierRead.oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrierRead.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageMemoryBarrierRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageMemoryBarrierRead.subresourceRange.levelCount = (uint32_t)texture.levels();
                imageMemoryBarrierRead.subresourceRange.layerCount = (uint32_t)texture.faces() * texture.layers();
            }
        } break;
        case apemodevk::ImageLoader::eImageFileFormat_PNG: {

            /* Ensure no leaks */
            struct LodePNGStateWrapper {
                LodePNGState state;
                LodePNGStateWrapper( ) {
                    lodepng_state_init( &state );
                }
                ~LodePNGStateWrapper( ) {
                    lodepng_state_cleanup( &state );
                }
            } stateWrapper;

            /* Load png file here from memory buffer */
            uint8_t* pImageBytes = nullptr;
            uint32_t imageHeight = 0;
            uint32_t imageWidth  = 0;

            if ( 0 == lodepng_decode( &pImageBytes,
                                      &imageWidth,
                                      &imageHeight,
                                      &stateWrapper.state,
                                      InFileContent.data( ),
                                      InFileContent.size( ) ) ) {

                loadedImage->imageCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
                loadedImage->imageCreateInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
                loadedImage->imageCreateInfo.extent.width  = imageWidth;
                loadedImage->imageCreateInfo.extent.height = imageHeight;
                loadedImage->imageCreateInfo.extent.depth  = 1;
                loadedImage->imageCreateInfo.mipLevels     = 1;
                loadedImage->imageCreateInfo.arrayLayers   = 1;
                loadedImage->imageCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
                loadedImage->imageCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
                loadedImage->imageCreateInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                loadedImage->imageCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
                loadedImage->imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                loadedImage->imageViewCreateInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
                loadedImage->imageViewCreateInfo.format                      = VK_FORMAT_R8G8B8A8_UNORM;
                loadedImage->imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                loadedImage->imageViewCreateInfo.subresourceRange.levelCount = 1;
                loadedImage->imageViewCreateInfo.subresourceRange.layerCount = 1;

                pHostBufferPool->Reset( );
                imageBufferSuballocResult = pHostBufferPool->Suballocate( pImageBytes, imageWidth * imageHeight * 4 );
                pHostBufferPool->Flush( );   /* Unmap buffers and flush all memory ranges */

                lodepng_free( pImageBytes ); /* Free decoded PNG since it is no longer needed */

                bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bufferImageCopy.imageSubresource.layerCount = 1;
                bufferImageCopy.imageExtent.width           = imageWidth;
                bufferImageCopy.imageExtent.height          = imageHeight;
                bufferImageCopy.imageExtent.depth           = 1;
                bufferImageCopy.bufferOffset                = imageBufferSuballocResult.dynamicOffset;
                bufferImageCopy.bufferImageHeight           = 0; /* Tightly packed according to the imageExtent */
                bufferImageCopy.bufferRowLength             = 0; /* Tightly packed according to the imageExtent */

                imageMemoryBarrierWrite.srcAccessMask               = 0;
                imageMemoryBarrierWrite.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrierWrite.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
                imageMemoryBarrierWrite.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrierWrite.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageMemoryBarrierWrite.subresourceRange.levelCount = 1;
                imageMemoryBarrierWrite.subresourceRange.layerCount = 1;

                imageMemoryBarrierRead.srcAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrierRead.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
                imageMemoryBarrierRead.oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrierRead.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageMemoryBarrierRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageMemoryBarrierRead.subresourceRange.levelCount = 1;
                imageMemoryBarrierRead.subresourceRange.layerCount = 1;
            }
        } break;
    }

    if ( false == loadedImage->hImg.Recreate( *pNode, *pNode, loadedImage->imageCreateInfo ) ) {
        DebugBreak( );
        return nullptr;
    }

    auto fontImgAllocInfo = loadedImage->hImg.GetMemoryAllocateInfo( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    if ( false == loadedImage->hImgMemory.Recreate( *pNode, fontImgAllocInfo ) ) {
        DebugBreak( );
        return nullptr;
    }

    if ( false == loadedImage->hImg.BindMemory( loadedImage->hImgMemory, 0 ) ) {
        DebugBreak( );
        return nullptr;
    }

    loadedImage->imageViewCreateInfo.image = loadedImage->hImg;
    if ( false == loadedImage->hImgView.Recreate( *pNode, loadedImage->imageViewCreateInfo ) ) {
        DebugBreak( );
        return nullptr;
    }

    auto acquiredQueue = pNode->GetQueuePool( )->Acquire( false, VK_QUEUE_TRANSFER_BIT, true );
    if ( nullptr == acquiredQueue.pQueue ) {
        while ( nullptr == acquiredQueue.pQueue ) {
            acquiredQueue = pNode->GetQueuePool( )->Acquire( false, VK_QUEUE_TRANSFER_BIT, false );
        }
    }

    auto acquiredCmdBuffer = pNode->GetCommandBufferPool( )->Acquire( false, acquiredQueue.QueueFamilyId );
    assert( acquiredCmdBuffer.pCmdBuffer != nullptr );

    if ( VK_SUCCESS != CheckedCall( vkResetCommandPool( *pNode, acquiredCmdBuffer.pCmdPool, 0 ) ) ) {
        return nullptr;
    }

    VkCommandBufferBeginInfo cmdBufferBeginInfo;
    InitializeStruct( cmdBufferBeginInfo );
    cmdBufferBeginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if ( VK_SUCCESS != CheckedCall( vkBeginCommandBuffer( acquiredCmdBuffer.pCmdBuffer, &cmdBufferBeginInfo ) ) ) {
        return nullptr;
    }

    imageMemoryBarrierWrite.image               = loadedImage->hImg;
    imageMemoryBarrierWrite.srcQueueFamilyIndex = acquiredQueue.QueueFamilyId;
    imageMemoryBarrierWrite.dstQueueFamilyIndex = acquiredQueue.QueueFamilyId;

    vkCmdPipelineBarrier( acquiredCmdBuffer.pCmdBuffer,
                          VK_PIPELINE_STAGE_HOST_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0,
                          0,
                          NULL,
                          0,
                          NULL,
                          1,
                          &imageMemoryBarrierWrite);

    vkCmdCopyBufferToImage( acquiredCmdBuffer.pCmdBuffer,
                            imageBufferSuballocResult.descBufferInfo.buffer,
                            loadedImage->hImg,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &bufferImageCopy );

    imageMemoryBarrierRead.image = loadedImage->hImg;
    imageMemoryBarrierRead.srcQueueFamilyIndex = acquiredQueue.QueueFamilyId;
    imageMemoryBarrierRead.dstQueueFamilyIndex = acquiredQueue.QueueFamilyId;

    vkCmdPipelineBarrier( acquiredCmdBuffer.pCmdBuffer,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0,
                          0,
                          NULL,
                          0,
                          NULL,
                          1,
                          &imageMemoryBarrierRead);

    vkEndCommandBuffer( acquiredCmdBuffer.pCmdBuffer );

    VkSubmitInfo submitInfo;
    InitializeStruct( submitInfo );
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &acquiredCmdBuffer.pCmdBuffer;

    /* Reset signaled fence */
    if ( VK_SUCCESS == vkGetFenceStatus( *pNode, acquiredQueue.pFence ) )
        if ( VK_SUCCESS != CheckedCall( vkResetFences( *pNode, 1, &acquiredQueue.pFence ) ) ) {
            return nullptr;
        }

    if ( VK_SUCCESS != CheckedCall( vkQueueSubmit( acquiredQueue.pQueue, 1, &submitInfo, acquiredQueue.pFence ) ) ) {
        return nullptr;
    }

    if ( bAwaitLoading ) {
        /* No need to pass fence to command buffer pool */
        /* Ensure the image can be used right away */
        CheckedCall( vkWaitForFences( *pNode, 1, &acquiredQueue.pFence, true, UINT64_MAX ) );
    } else {
        /* Ensure the image memory transfer can be synchronized */
        /* Ensure the command buffer is synchronized */
        loadedImage->pLoadedFence = acquiredQueue.pFence;
        acquiredCmdBuffer.pFence  = acquiredQueue.pFence;
    }

    pNode->GetCommandBufferPool( )->Release( acquiredCmdBuffer );
    pNode->GetQueuePool( )->Release( acquiredQueue );

    return std::move( loadedImage );
}