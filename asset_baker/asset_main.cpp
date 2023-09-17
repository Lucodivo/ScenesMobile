#include <iostream>
#include <unordered_set>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

#include "lz4/lz4.h"
#include <chrono>
#include "nlohmann/json.hpp"
#include "compressonator.h"

#include "stb/stb_image.h"
#include "tinyobjloader/tiny_obj_loader.h"
#include "tinygltf/tiny_gltf.h"

#include "noop_types.h"

#include "util.cpp"

#include "asset_loader.h"
#include "texture_asset.h"
#include "cubemap_asset.h"
#include "model_asset.h"
using namespace assets;

#include "noop_math.h"
using namespace noop;

struct {
  const char* texture = ".tx";
  const char* cubeMap = ".cbtx";
  const char* model = ".modl";
} bakedExtensions;

const char* assetBakerCacheFileName = "Asset-Baker-Cache.asb";
struct {
  const char* cacheFiles = "cacheFiles";
  const char* originalFileName = "originalFileName";
  const char* originalFileLastModified = "originalFileLastModified";
  const char* bakedFiles = "bakedFiles";
  const char* fileName = "fileName";
  const char* filePath = "filePath";
} cacheJsonStrings;

struct ConverterState {
  fs::path assetsDir;
  fs::path bakedAssetDir;
  fs::path outputFileDir;
  std::vector<fs::path> bakedFilePaths;
};

// TODO: Caching should note the version of the AssetLib used when asset was baked
struct AssetBakeCachedItem {
  struct BakedFile {
    std::string path;
    std::string ext;
    std::string name;
  };

  std::string originalFileName;
  f64 originalFileLastModified;
  std::vector<BakedFile> bakedFiles;
};

bool convertTexture(const fs::path& inputPath, const char* outputFilename);
bool convertCubeMapTexture(const fs::path& inputDir, const char* outputFilename);
bool convertModel(const fs::path& inputPath, const char* outputFileName);

void saveCache(const std::unordered_map<std::string, AssetBakeCachedItem>& oldCache, const std::vector<AssetBakeCachedItem>& newBakedItems);
void loadCache(std::unordered_map<std::string, AssetBakeCachedItem>& assetBakeCache);

void writeOutputData(const std::unordered_map<std::string, AssetBakeCachedItem>& oldCache, const ConverterState& converterState);
void replace(std::string& str, const char oldToken, const char newToken);
void replaceBackSlashes(std::string& str);
std::size_t fileCountInDir(const fs::path& dirPath);
std::size_t dirCountInDir(const fs::path& dirPath);

f64 lastModifiedTimeStamp(const fs::path& file);
bool fileUpToDate(const std::unordered_map<std::string, AssetBakeCachedItem>& cache, const fs::path& file);
void replace(std::string& str, const char* oldTokens, u32 oldTokensCount, char newToken);

bool CompressionCallback(float fProgress, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2);

int main(int argc, char* argv[]) {

  // NOTE: Count is often at least 1, as argv[0] is full path of the program being run
  if(argc < 4) {
    char* arg1 = {argv[1]};
    if(strcmp(arg1, "--clean") == 0) {
      fs::path cacheFile{assetBakerCacheFileName};
      if(fs::remove(cacheFile)) {
        printf("Successfully deleted cache.");
      } else {
        printf("Attempted to clean but cache file was not found.");
      }
      return 0;
    }

    printf("Incorrect number of arguments.\n");
    printf("Use ex: .\\assetbaker {raw assets dir} {baked asset output dir} {baked asset metadata output dir}\n");
    return -1;
  }

  std::unordered_map<std::string, AssetBakeCachedItem> oldAssetBakeCache;
  loadCache(oldAssetBakeCache);
  std::vector<AssetBakeCachedItem> newlyCachedItems;
  newlyCachedItems.reserve(50);

  ConverterState converterState;
  converterState.assetsDir = {argv[1]};
  converterState.bakedAssetDir = { argv[2] };
  converterState.outputFileDir = { argv[3] };

  if(!fs::is_directory(converterState.assetsDir)) {
    std::cout << "Could not find assets directory: " << argv[1];
    return -1;
  }

  // Create export folder if needed
  if(!fs::is_directory(converterState.bakedAssetDir)) {
    fs::create_directory(converterState.bakedAssetDir);
  }

  std::cout << "loaded asset directory at " << converterState.assetsDir << std::endl;

  fs::path asset_models_dir = converterState.assetsDir / "models";
  fs::path asset_skyboxes_dir = converterState.assetsDir / "skyboxes";
  fs::path asset_textures_dir = converterState.assetsDir / "textures";
  fs::create_directory(converterState.bakedAssetDir / "models");
  fs::create_directory(converterState.bakedAssetDir / "skyboxes");
  fs::create_directory(converterState.bakedAssetDir / "textures");

  // TODO: Bring back with a cache that respects file formats
  size_t skyboxDirCount = dirCountInDir(asset_skyboxes_dir);
  printf("skybox directories found: %d\n", (int)skyboxDirCount);
  for(auto const& skyboxDir: std::filesystem::directory_iterator(asset_skyboxes_dir)) {
    if(fileUpToDate(oldAssetBakeCache, skyboxDir)) {
      continue;
    } else if(fs::is_directory(skyboxDir)) {
      fs::path exportPath = converterState.bakedAssetDir / "skyboxes" / skyboxDir.path().filename().replace_extension(bakedExtensions.cubeMap);
      printf("Beginning bake of skybox asset: %s\n", skyboxDir.path().string().c_str());
      if(convertCubeMapTexture(skyboxDir, exportPath.string().c_str())) {
        converterState.bakedFilePaths.push_back(skyboxDir);
      } else {
        printf("Failed to bake skybox asset: %s\n", skyboxDir.path().string().c_str());
      }
    }
  }

  if(exists(asset_textures_dir)) {
    for(auto const& textureFileEntry: std::filesystem::directory_iterator(asset_textures_dir)) {
      if(fileUpToDate(oldAssetBakeCache, textureFileEntry)) {
        continue;
      } else if(fs::is_regular_file(textureFileEntry)) {
        fs::path exportPath = converterState.bakedAssetDir / "textures" / textureFileEntry.path().filename().replace_extension(bakedExtensions.texture);
        printf("Beginning bake of texture asset: %s\n", textureFileEntry.path().string().c_str());
        if(convertTexture(textureFileEntry, exportPath.string().c_str())) {
          converterState.bakedFilePaths.push_back(textureFileEntry);
        } else {
          printf("Failed to bake texture asset: %s\n", textureFileEntry.path().string().c_str());
        }
      }
    }
  } else {
    printf("Could not find textures asset directory at: %s", asset_textures_dir.string().c_str());
  }

  if(exists(asset_models_dir)) {
    for(auto const& modelFileEntry: std::filesystem::directory_iterator(asset_models_dir)) {
      if(fileUpToDate(oldAssetBakeCache, modelFileEntry)) {
        continue;
      } else if(fs::is_regular_file(modelFileEntry)) {
        fs::path exportPath = converterState.bakedAssetDir / "models" / modelFileEntry.path().filename().replace_extension(bakedExtensions.model);
        printf("%s\n", modelFileEntry.path().string().c_str());
        convertModel(modelFileEntry, exportPath.string().c_str());
      }
    }
  } else {
    printf("Could not find models asset directory at: %s", asset_models_dir.string().c_str());
  }

  // TODO: Cache baked models

  // remember baked item
  u32 convertedFilesCount = (u32)converterState.bakedFilePaths.size();
  for(u32 i = 0; i < convertedFilesCount; i++) {
    const fs::path& recentlyBakedFile = converterState.bakedFilePaths[i];
    AssetBakeCachedItem newlyBakedItem;
    newlyBakedItem.originalFileName = recentlyBakedFile.string();
    newlyBakedItem.originalFileLastModified = lastModifiedTimeStamp(recentlyBakedFile);
    AssetBakeCachedItem::BakedFile bakedFile;
    bakedFile.path = recentlyBakedFile.string();
    bakedFile.name = recentlyBakedFile.filename().string();
    bakedFile.ext = recentlyBakedFile.extension().string();
    newlyBakedItem.bakedFiles.push_back(bakedFile);
    newlyCachedItems.push_back(newlyBakedItem);
  }

  writeOutputData(oldAssetBakeCache, converterState);
  saveCache(oldAssetBakeCache, newlyCachedItems);

  return 0;
}

bool convertModel(const fs::path& inputPath, const char* outputFileName) {
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;
  tinygltf::Model tinyGLTFModel;

  ModelInfo modelInfo = {};
  modelInfo.originalFileName = inputPath.string();

  std::vector<char> modelBytes;
  readFile(inputPath.string().c_str(), modelBytes);

  bool ret = loader.LoadBinaryFromMemory(&tinyGLTFModel, &err, &warn, (const unsigned char*)modelBytes.data(), (const u32)modelBytes.size());

  if (!warn.empty()) {
    printf("Warning: %s\n", warn.c_str());
    return false;
  }
  if (!err.empty()) {
    printf("Error: %s\n", err.c_str());
    return false;
  }
  if (!ret) {
    printf("Failed to parse glTF\n");
    return false;
  }

  struct gltfAttributeMetadata {
    u32 accessorIndex;
    u32 numComponents;
    u32 bufferViewIndex;
    u32 bufferIndex;
    u64 bufferByteOffset;
    u64 bufferByteLength;
  };

  const char* positionIndexKeyString = "POSITION";
  const char* normalIndexKeyString = "NORMAL";
  const char* texture0IndexKeyString = "TEXCOORD_0";

  u32 meshCount = (u32)tinyGLTFModel.meshes.size();
  assert(meshCount != 0);
  std::vector<tinygltf::Accessor>* gltfAccessors = &tinyGLTFModel.accessors;
  std::vector<tinygltf::BufferView>* gltfBufferViews = &tinyGLTFModel.bufferViews;

  auto populateAttributeMetadata = [gltfAccessors, gltfBufferViews](const char* keyString, const tinygltf::Primitive& gltfPrimitive) -> gltfAttributeMetadata {
    gltfAttributeMetadata result{};
    result.accessorIndex = gltfPrimitive.attributes.at(keyString);
    result.numComponents = tinygltf::GetNumComponentsInType(gltfAccessors->at(result.accessorIndex).type);
    result.bufferViewIndex = gltfAccessors->at(result.accessorIndex).bufferView;
    result.bufferIndex = gltfBufferViews->at(result.bufferViewIndex).buffer;
    result.bufferByteOffset = gltfBufferViews->at(result.bufferViewIndex).byteOffset;
    result.bufferByteLength = gltfBufferViews->at(result.bufferViewIndex).byteLength;
    return result;
  };

  // TODO: Handle models with more than one mesh
  tinygltf::Mesh gltfMesh = tinyGLTFModel.meshes[0];

  assert(!gltfMesh.primitives.empty());
  // TODO: handle meshes that have more than one primitive
  tinygltf::Primitive gltfPrimitive = gltfMesh.primitives[0];
  assert(gltfPrimitive.indices > -1); // TODO: Should we deal with models that don't have indices?

  // TODO: Allow variability in attributes beyond POSITION, NORMAL, TEXCOORD_0?
  assert(gltfPrimitive.attributes.find(positionIndexKeyString) != gltfPrimitive.attributes.end());
  gltfAttributeMetadata positionAttribute = populateAttributeMetadata(positionIndexKeyString, gltfPrimitive);

  f64* minValues = tinyGLTFModel.accessors[positionAttribute.accessorIndex].minValues.data();
  f64* maxValues = tinyGLTFModel.accessors[positionAttribute.accessorIndex].maxValues.data();

  modelInfo.boundingBoxMin[0] = (f32)minValues[0];
  modelInfo.boundingBoxMin[1] = (f32)minValues[1];
  modelInfo.boundingBoxMin[2] = (f32)minValues[2];
  modelInfo.boundingBoxDiagonal[0] = (f32)(maxValues[0] - minValues[0]);
  modelInfo.boundingBoxDiagonal[1] = (f32)(maxValues[1] - minValues[1]);
  modelInfo.boundingBoxDiagonal[2] = (f32)(maxValues[2] - minValues[2]);

  b32 normalAttributesAvailable = gltfPrimitive.attributes.find(normalIndexKeyString) != gltfPrimitive.attributes.end();
  gltfAttributeMetadata normalAttribute = {0};
  if(normalAttributesAvailable) { // normal attribute data
    normalAttribute = populateAttributeMetadata(normalIndexKeyString, gltfPrimitive);
    assert(positionAttribute.bufferIndex == normalAttribute.bufferIndex);
  }

  b32 texture0AttributesAvailable = gltfPrimitive.attributes.find(texture0IndexKeyString) != gltfPrimitive.attributes.end();
  gltfAttributeMetadata texture0Attribute = {0};
  if(texture0AttributesAvailable) { // texture 0 uv coord attribute data
    texture0Attribute = populateAttributeMetadata(texture0IndexKeyString, gltfPrimitive);
    assert(positionAttribute.bufferIndex == texture0Attribute.bufferIndex);
  }

  // TODO: Handle vertex attributes that don't share the same buffer?
  u32 vertexAttBufferIndex = positionAttribute.bufferIndex;
  assert(tinyGLTFModel.buffers.size() > vertexAttBufferIndex);

  u32 indicesAccessorIndex = gltfPrimitive.indices;
  tinygltf::BufferView indicesGLTFBufferView = gltfBufferViews->at(gltfAccessors->at(indicesAccessorIndex).bufferView);
  u32 indicesGLTFBufferIndex = indicesGLTFBufferView.buffer;
  u64 indicesGLTFBufferByteOffset = indicesGLTFBufferView.byteOffset;
  u64 indicesGLTFBufferByteLength = indicesGLTFBufferView.byteLength;

  u64 minOffset = Min(positionAttribute.bufferByteOffset, Min(texture0Attribute.bufferByteOffset, normalAttribute.bufferByteOffset));
  u8* vertexAttributeDataOffset = tinyGLTFModel.buffers[indicesGLTFBufferIndex].data.data() + minOffset;
  u8* indicesDataOffset = tinyGLTFModel.buffers[indicesGLTFBufferIndex].data.data() + indicesGLTFBufferByteOffset;

  modelInfo.indexCount = u32(gltfAccessors->at(indicesAccessorIndex).count);
  modelInfo.indexTypeSize = tinygltf::GetComponentSizeInBytes(gltfAccessors->at(indicesAccessorIndex).componentType);

  // TODO: Handle the possibility of the three attributes not being side-by-side in the buffer
  u64 sizeOfAttributeData = positionAttribute.bufferByteLength + normalAttribute.bufferByteLength + texture0Attribute.bufferByteLength;
  assert(tinyGLTFModel.buffers[vertexAttBufferIndex].data.size() >= sizeOfAttributeData);
  const u32 positionAttributeIndex = 0;
  const u32 normalAttributeIndex = 1;
  const u32 texture0AttributeIndex = 2;

  void* positionAttributeData = (void*)(vertexAttributeDataOffset + positionAttribute.bufferByteOffset);
  void* normalAttributeData = (void*)(vertexAttributeDataOffset + normalAttribute.bufferByteOffset );
  void* uvAttributeData = (void*)(vertexAttributeDataOffset + texture0Attribute.bufferByteOffset);
  void* indicesData = (void*)indicesDataOffset;

  s32 normalImageIndex = -1;
  s32 albedoImageIndex = -1;

  s32 gltfMaterialIndex = gltfPrimitive.material;
  f64* baseColor = nullptr;
  if(gltfMaterialIndex >= 0) {
    tinygltf::Material gltfMaterial = tinyGLTFModel.materials[gltfMaterialIndex];
    // TODO: Handle more then just TEXCOORD_0 vertex attribute?
    assert(gltfMaterial.normalTexture.texCoord == 0 && gltfMaterial.pbrMetallicRoughness.baseColorTexture.texCoord == 0);

    baseColor = gltfMaterial.pbrMetallicRoughness.baseColorFactor.data();

    // NOTE: gltf.textures.samplers gives info about how to magnify/minify textures and how texture wrapping should work
    s32 normalTextureIndex = gltfMaterial.normalTexture.index;
    if(normalTextureIndex >= 0) {
      normalImageIndex = tinyGLTFModel.textures[normalTextureIndex].source;
    }

    s32 baseColorTextureIndex = gltfMaterial.pbrMetallicRoughness.baseColorTexture.index;
    if(baseColorTextureIndex >= 0) {
      albedoImageIndex = tinyGLTFModel.textures[baseColorTextureIndex].source;
    }
  }

  modelInfo.positionAttributeSize = positionAttribute.bufferByteLength;
  modelInfo.normalAttributeSize = normalAttribute.bufferByteLength;
  modelInfo.uvAttributeSize = texture0Attribute.bufferByteLength;
  modelInfo.indicesSize = indicesGLTFBufferByteLength;

  f64 defaultBaseColor[] = {0.0, 0.0, 0.0, 0.0};
  baseColor = baseColor == nullptr ? defaultBaseColor : baseColor;
  modelInfo.baseColor[0] = (f32)baseColor[0];
  modelInfo.baseColor[1] = (f32)baseColor[1];
  modelInfo.baseColor[2] = (f32)baseColor[2];
  modelInfo.baseColor[3] = (f32)baseColor[3];

  CMP_BYTE* compressedAlbedo = nullptr;
  if(albedoImageIndex != -1) {
    tinygltf::Image albedoImage = tinyGLTFModel.images[albedoImageIndex];
    void* albedoImageData = albedoImage.image.data();
    u64 albedoImageSize = albedoImage.image.size();
    u64 albedoImageWidth = albedoImage.width;
    u64 albedoImageHeight = albedoImage.height;
    u64 albedoImageChannels = albedoImage.component;

    modelInfo.albedoTexWidth = albedoImageWidth;
    modelInfo.albedoTexHeight = albedoImageHeight;

    assert(albedoImageChannels == 3 || albedoImageChannels == 4);
    CMP_FORMAT albedoStartFormat = albedoImageChannels == 3 ? CMP_FORMAT_RGB_888 : CMP_FORMAT_RGBA_8888;
    CMP_FORMAT albedoDesiredFormat = albedoImageChannels == 3 ? CMP_FORMAT_ETC2_RGB : CMP_FORMAT_ETC2_RGBA;

    CMP_Texture srcTexture = {0};
    srcTexture.dwSize = sizeof(srcTexture);
    srcTexture.dwWidth = (CMP_DWORD)albedoImageWidth;
    srcTexture.dwHeight = (CMP_DWORD)albedoImageHeight;
    srcTexture.dwPitch = (CMP_DWORD)(albedoImageWidth * albedoImageChannels);
    srcTexture.format = albedoStartFormat;
    srcTexture.dwDataSize = (CMP_DWORD)albedoImageSize;
    srcTexture.pData = (CMP_BYTE *)albedoImageData;
    srcTexture.pMipSet = nullptr;

    CMP_Texture destTexture = {0};
    destTexture.dwSize = sizeof(destTexture);
    destTexture.dwWidth = srcTexture.dwWidth;
    destTexture.dwHeight = srcTexture.dwHeight;
    destTexture.format = albedoDesiredFormat;
    destTexture.nBlockHeight = 4;
    destTexture.nBlockWidth = 4;
    destTexture.nBlockDepth = 1;
    destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);
    compressedAlbedo = (CMP_BYTE *) malloc(destTexture.dwDataSize);
    destTexture.pData = compressedAlbedo;

    CMP_CompressOptions options = {0};
    options.dwSize = sizeof(options);
    options.fquality = 1.0f;            // Quality
    options.dwnumThreads = 0;               // Number of threads to use per texture set to auto

    auto compressionStart = std::chrono::high_resolution_clock::now();
    try {
      CMP_ERROR cmp_status = CMP_ConvertTexture(&srcTexture, &destTexture, &options, &CompressionCallback);
      if (cmp_status != CMP_OK) {
        std::printf("Error %d: Something went wrong with compressing albedo texture for %s\n", cmp_status,
                    inputPath.string().c_str());
      }
    } catch (const std::exception &ex) {
      std::printf("Error: %s\n", ex.what());
    }
    auto compressionEnd = std::chrono::high_resolution_clock::now();
    auto diff = compressionEnd - compressionStart;
    std::cout << "compression took " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0
              << "ms" << std::endl;

    modelInfo.albedoTexSize = destTexture.dwDataSize;
    modelInfo.albedoTexFormat = destTexture.format == CMP_FORMAT_ETC2_RGB ? TextureFormat_ETC2_RGB : TextureFormat_ETC2_RGBA;
  }

  u8* compressedNormal = nullptr;
  if(normalImageIndex != -1) {
    CMP_FORMAT normalStartFormat = CMP_FORMAT_RGB_888;
    // TODO: We should *NOT* be using ETC2 format for normals. It is just not the right encoding for such a thing.
    // TODO: GLES 3.0 only guarantees ETC1, ETC2, EAC, ASTC.
    // TODO: My personal device also supports a few ATC formats.
    CMP_FORMAT normalDesiredFormat = CMP_FORMAT_ETC2_RGB;

    tinygltf::Image normalImage = tinyGLTFModel.images[normalImageIndex];
    void* normalImageData = normalImage.image.data();
    u64 normalImageSize = normalImage.image.size();
    u64 normalImageWidth = normalImage.width;
    u64 normalImageHeight = normalImage.height;

    modelInfo.normalTexWidth = normalImageWidth;
    modelInfo.normalTexHeight = normalImageHeight;

    CMP_Texture srcTexture = {0};
    srcTexture.dwSize = sizeof(srcTexture);
    srcTexture.dwWidth = (CMP_DWORD)normalImageWidth;
    srcTexture.dwHeight = (CMP_DWORD)normalImageHeight;
    srcTexture.dwPitch = (CMP_DWORD)(normalImageWidth * 3);
    srcTexture.format = normalStartFormat;
    srcTexture.dwDataSize = (CMP_DWORD)normalImageSize;
    srcTexture.pData = (CMP_BYTE *) normalImageData;
    srcTexture.pMipSet = nullptr;

    CMP_Texture destTexture = {0};
    destTexture.dwSize = sizeof(destTexture);
    destTexture.dwWidth = srcTexture.dwWidth;
    destTexture.dwHeight = srcTexture.dwHeight;
    destTexture.dwPitch = 0;
    destTexture.format = normalDesiredFormat;
    destTexture.nBlockHeight = 4;
    destTexture.nBlockWidth = 4;
    destTexture.nBlockDepth = 1;
    destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);
    compressedNormal = (CMP_BYTE *) malloc(destTexture.dwDataSize);
    destTexture.pData = compressedNormal;

    CMP_CompressOptions options = {0};
    options.dwSize = sizeof(options);
    options.fquality = 1.0f;            // Quality
    options.dwnumThreads = 0;               // Number of threads to use per texture set to auto

    auto compressionStart = std::chrono::high_resolution_clock::now();
    try {
      CMP_ERROR cmp_status = CMP_ConvertTexture(&srcTexture, &destTexture, &options, &CompressionCallback);
      if (cmp_status != CMP_OK) {
        std::printf("Error %d: Something went wrong with compressing normal texture for %s\n", cmp_status,
                    inputPath.string().c_str());
      }
    } catch (const std::exception &ex) {
      std::printf("Error: %s\n", ex.what());
    }
    auto compressionEnd = std::chrono::high_resolution_clock::now();
    auto diff = compressionEnd - compressionStart;
    std::cout << "compression took " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0
              << "ms" << std::endl;

    modelInfo.normalTexSize = destTexture.dwDataSize;
    modelInfo.normalTexFormat = TextureFormat_ETC2_RGB;
  }

  AssetFile modelAsset = packModel(&modelInfo,
                                   positionAttributeData,
                                   normalAttributeData,
                                   uvAttributeData,
                                   indicesData,
                                   compressedNormal,
                                   compressedAlbedo);

  saveAssetFile(outputFileName, modelAsset);

  free(compressedAlbedo);
  free(compressedNormal);

  return false;
}

bool convertCubeMapTexture(const fs::path& inputDir, const char* outputFilename) {

  int frontWidth, frontHeight, frontChannels,
      backWidth, backHeight, backChannels,
      topWidth, topHeight, topChannels,
      bottomWidth, bottomHeight, bottomChannels,
      leftWidth, leftHeight, leftChannels,
      rightWidth, rightHeight, rightChannels;

  fs::path ext;
  for(auto const& skyboxFaceImage: std::filesystem::directory_iterator(inputDir)) {
    if(fs::is_regular_file(skyboxFaceImage)) {
      ext = skyboxFaceImage.path().extension(); // pull extension from first file we find
      printf("%s\n", ext.string().c_str());
      break;
    }
  }

  if(ext.empty()) {
    printf("Skybox directory (%s) was found empty.", inputDir.string().c_str());
  }

  fs::path frontPath = (inputDir / "front").replace_extension(ext);
  fs::path backPath = (inputDir / "back").replace_extension(ext);
  fs::path topPath = (inputDir / "top").replace_extension(ext);
  fs::path bottomPath = (inputDir / "bottom").replace_extension(ext);
  fs::path leftPath = (inputDir / "left").replace_extension(ext);
  fs::path rightPath = (inputDir / "right").replace_extension(ext);

  auto imageLoadStart = std::chrono::high_resolution_clock::now();
  stbi_uc* frontPixels = stbi_load(frontPath.u8string().c_str(), &frontWidth, &frontHeight, &frontChannels, STBI_rgb);
  stbi_uc* backPixels = stbi_load(backPath.u8string().c_str(), &backWidth, &backHeight, &backChannels, STBI_rgb);
  stbi_uc* topPixels = stbi_load(topPath.u8string().c_str(), &topWidth, &topHeight, &topChannels, STBI_rgb);
  stbi_uc* bottomPixels = stbi_load(bottomPath.u8string().c_str(), &bottomWidth, &bottomHeight, &bottomChannels, STBI_rgb);
  stbi_uc* rightPixels = stbi_load(rightPath.u8string().c_str(), &rightWidth, &rightHeight, &rightChannels, STBI_rgb);
  stbi_uc* leftPixels = stbi_load(leftPath.u8string().c_str(), &leftWidth, &leftHeight, &leftChannels, STBI_rgb);
  auto imageLoadEnd = std::chrono::high_resolution_clock::now();

  auto diff = imageLoadEnd - imageLoadStart;
  std::cout << "srcTexture took " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0 << "ms to load" << std::endl;

  if(!frontPixels || !backPixels || !topPixels || !bottomPixels || !leftPixels || !rightPixels) {
    std::cout << "Failed to load CubeMap face file for directory " << inputDir << std::endl;
    return false;
  }

  if(frontWidth != backWidth || frontWidth != topWidth || frontWidth != bottomWidth || frontWidth != leftWidth || frontWidth != rightWidth ||
      frontHeight != backHeight || frontHeight != topHeight || frontHeight != bottomHeight || frontHeight != leftHeight || frontHeight != rightHeight ||
      frontChannels != backChannels || frontChannels != topChannels || frontChannels != bottomChannels || frontChannels != leftChannels || frontChannels != rightChannels) {
    std::cout << "One or more CubeMap faces do not match in either width, height, or number of channels for directory " << inputDir << std::endl;
    return false;
  }

  if((frontWidth % 4) != 0 || (frontHeight % 4) != 0) {
    std::cout << "CubeMap face widths and heights must be evenly divisible by 4." << inputDir << std::endl;
    return false;
  }

  assert(topChannels == 3 && "Number of channels for cube map asset does not match desired");

  u32 facePixelsSize = frontWidth * frontHeight * frontChannels;
  unsigned char* cubeMapPixels_fbtbrl = (unsigned char*)malloc(facePixelsSize * 6);
  memcpy(cubeMapPixels_fbtbrl, frontPixels, facePixelsSize);
  memcpy(cubeMapPixels_fbtbrl + (1 * facePixelsSize), backPixels, facePixelsSize);
  memcpy(cubeMapPixels_fbtbrl + (2 * facePixelsSize), topPixels, facePixelsSize);
  memcpy(cubeMapPixels_fbtbrl + (3 * facePixelsSize), bottomPixels, facePixelsSize);
  memcpy(cubeMapPixels_fbtbrl + (4 * facePixelsSize), rightPixels, facePixelsSize);
  memcpy(cubeMapPixels_fbtbrl + (5 * facePixelsSize), leftPixels, facePixelsSize);

  stbi_image_free(frontPixels);
  stbi_image_free(backPixels);
  stbi_image_free(topPixels);
  stbi_image_free(bottomPixels);
  stbi_image_free(leftPixels);
  stbi_image_free(rightPixels);

  CMP_Texture srcTexture = {0};
  srcTexture.dwSize = sizeof(srcTexture);
  srcTexture.dwWidth = topWidth;
  srcTexture.dwHeight = topHeight * 6;
  srcTexture.dwPitch = topWidth * topChannels;
  srcTexture.format = CMP_FORMAT_RGB_888;
  srcTexture.dwDataSize = topWidth * topHeight * topChannels * topChannels * 6;
  srcTexture.pData = cubeMapPixels_fbtbrl;
  srcTexture.pMipSet = nullptr;

  CMP_Texture destTexture = {0};
  destTexture.dwSize     = sizeof(destTexture);
  destTexture.dwWidth    = srcTexture.dwWidth;
  destTexture.dwHeight   = srcTexture.dwHeight;
  destTexture.dwPitch    = 0;
  destTexture.format     = CMP_FORMAT_ETC2_RGB;
  destTexture.nBlockHeight = 4;
  destTexture.nBlockWidth = 4;
  destTexture.nBlockDepth = 1;
  destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);
  destTexture.pData      = (CMP_BYTE*)malloc(destTexture.dwDataSize);

  CMP_CompressOptions options = {0};
  options.dwSize       = sizeof(options);
  options.fquality     = 1.0f;            // Quality
  options.dwnumThreads = 0;               // Number of threads to use per texture set to auto

  auto compressionStart = std::chrono::high_resolution_clock::now();
  try {
    CMP_ERROR cmp_status = CMP_ConvertTexture(&srcTexture, &destTexture, &options, &CompressionCallback);
    if(cmp_status != CMP_OK) {
      std::printf("Error %d: Something went wrong with compressing %s\n", cmp_status, inputDir.string().c_str());
    }
  } catch (const std::exception& ex) {
    std::printf("Error: %s\n",ex.what());
  }
  auto compressionEnd = std::chrono::high_resolution_clock::now();
  diff = compressionEnd - compressionStart;
  std::cout << "compression took " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0 << "ms" << std::endl;

  CubeMapInfo info;
  info.format = TextureFormat_ETC2_RGB;
  info.faceWidth = topWidth;
  info.faceHeight = topHeight;
  info.originalFolder = inputDir.string();
  info.faceSize = (u32)(ceil(info.faceWidth / 4.f) * ceil(info.faceHeight / 4.f)) * 8;
  assets::AssetFile cubeMapAssetFile = assets::packCubeMap(&info, destTexture.pData);

  free(cubeMapPixels_fbtbrl);
  free(destTexture.pData);

  saveAssetFile(outputFilename, cubeMapAssetFile);

  return true;
}

bool convertTexture(const fs::path& inputPath, const char* outputFilename) {
  int texWidth, texHeight, texChannels;

  auto imageLoadStart = std::chrono::high_resolution_clock::now();
  stbi_uc* pixels = stbi_load(inputPath.u8string().c_str(), &texWidth, &texHeight, &texChannels, 0);
  auto imageLoadEnd = std::chrono::high_resolution_clock::now();
  auto diff = imageLoadEnd - imageLoadStart;
  std::cout << "texture took " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0 << "ms to load" << std::endl;

  if(!pixels) {
    std::cout << "Failed to load texture file " << inputPath << std::endl;
    return false;
  }

  assert(texChannels == 3 || texChannels == 1 && "Texture has an unsupported amount of channels.");

  TextureInfo texInfo;
  texInfo.size = texWidth * texHeight * texChannels;
  texInfo.originalFileName = inputPath.string();
  texInfo.width = texWidth;
  texInfo.height = texHeight;

  if(texChannels == 1) {
    texInfo.format = TextureFormat_R8;
    assets::AssetFile newImage = assets::packTexture(&texInfo, pixels);
    saveAssetFile(outputFilename, newImage);
  } else if(texChannels == 3) {
    CMP_Texture srcTexture = {0};
    srcTexture.dwSize = sizeof(srcTexture);
    srcTexture.dwWidth = texWidth;
    srcTexture.dwHeight = texHeight;
    srcTexture.dwPitch = texWidth * texChannels;
    srcTexture.format = CMP_FORMAT_RGB_888;
    srcTexture.dwDataSize = texWidth * texHeight * texChannels;
    srcTexture.pData = pixels;
    srcTexture.pMipSet = nullptr;

    CMP_Texture destTexture = {0};
    destTexture.dwSize     = sizeof(destTexture);
    destTexture.dwWidth    = srcTexture.dwWidth;
    destTexture.dwHeight   = srcTexture.dwHeight;
    destTexture.dwPitch    = 0;
    destTexture.format     = CMP_FORMAT_ETC2_RGB;
    destTexture.nBlockHeight = 4;
    destTexture.nBlockWidth = 4;
    destTexture.nBlockDepth = 1;
    destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);
    destTexture.pData      = (CMP_BYTE*)malloc(destTexture.dwDataSize);

    CMP_CompressOptions options = {0};
    options.dwSize       = sizeof(options);
    options.fquality     = 1.0f;            // Quality
    options.dwnumThreads = 0;               // Number of threads to use per texture set to auto

    auto compressionStart = std::chrono::high_resolution_clock::now();
    try {
      CMP_ERROR cmp_status = CMP_ConvertTexture(&srcTexture, &destTexture, &options, &CompressionCallback);
      if(cmp_status != CMP_OK) {
        std::printf("Error %d: Something went wrong with compressing %s\n", cmp_status, inputPath.string().c_str());
      }
    } catch (const std::exception& ex) {
      std::printf("Error: %s\n",ex.what());
    }
    auto compressionEnd = std::chrono::high_resolution_clock::now();
    diff = compressionEnd - compressionStart;
    std::cout << "compression took " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0 << "ms" << std::endl;

    texInfo.format = TextureFormat_ETC2_RGB;
    assets::AssetFile newImage = assets::packTexture(&texInfo, destTexture.pData);
    saveAssetFile(outputFilename, newImage);
    free(destTexture.pData);
  }

  stbi_image_free(pixels);

  return true;
}

f64 lastModifiedTimeStamp(const fs::path &file) {
  auto lastModifiedTimePoint = fs::last_write_time(file);
  f64 lastModified = (f64)(lastModifiedTimePoint.time_since_epoch().count());
  return lastModified;
}

bool CompressionCallback(float fProgress, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2) {
  std::printf("\rCompression progress = %3.0f  ", fProgress);
  bool abortCompression = false;
  return abortCompression;
}

bool fileUpToDate(const std::unordered_map<std::string, AssetBakeCachedItem> &cache, const fs::path& file) {
  std::string fileName = file.string();
  auto cachedItem = cache.find(fileName);
  if(cachedItem == cache.end()) {
    return false;
  }

  f64 lastModified = lastModifiedTimeStamp(file);

  bool upToDate = epsilonComparison(lastModified, cachedItem->second.originalFileLastModified);

  if(upToDate) {
    printf("Asset file \"%s\" is up-to-date\n", fileName.c_str());
  }

  return upToDate;
}

std::size_t fileCountInDir(const fs::path& dirPath) {
  std::size_t fileCount = 0u;
  for(auto const& file: std::filesystem::directory_iterator(dirPath)) {
    if(fs::is_regular_file(file)) {
      ++fileCount;
    }
  }
  return fileCount;
}

std::size_t dirCountInDir(const fs::path& dirPath) {
  std::size_t dirCount = 0u;
  for(auto const& file: std::filesystem::directory_iterator(dirPath)) {
    if(fs::is_directory(file)) {
      ++dirCount;
    }
  }
  return dirCount;
}

void replace(std::string &str, const char *oldTokens, u32 oldTokensCount, char newToken) {
  for(char& c: str) {
    for(u32 i = 0; i < oldTokensCount; i++) {
      if(c == oldTokens[i]) {
        c = newToken;
        break;
      }
    }
  }
}

void replaceBackSlashes(std::string &str) {
  for(char& c: str) {
    if(c == '\\') {
      c = '/';
    }
  }
}

void writeOutputData(const std::unordered_map<std::string, AssetBakeCachedItem> &oldCache,
                     const ConverterState &converterState) {
  if(!fs::is_directory(converterState.outputFileDir)) {
    fs::create_directory(converterState.outputFileDir);
  }

  std::ofstream outTexturesFile, outMeshFile, outMaterialFile, outPrefabFile;
  outTexturesFile.open((converterState.outputFileDir / "baked_textures.incl").string(), std::ios::out);
  outMeshFile.open((converterState.outputFileDir / "baked_meshes.incl").string(), std::ios::out);
  outMaterialFile.open((converterState.outputFileDir / "baked_materials.incl").string(), std::ios::out);
  outPrefabFile.open((converterState.outputFileDir / "baked_prefabs.incl").string(), std::ios::out);

  auto write = [&](std::string bakedPath, std::string originalFileName, const char* fileExt) {
    const char tokensToReplace[] = {'.', '-'};
    replace(originalFileName, tokensToReplace, ArrayCount(tokensToReplace), '_');
    replaceBackSlashes(bakedPath);
    if(strcmp(fileExt, bakedExtensions.texture) == 0) {
      outTexturesFile << "BakedTexture(" << originalFileName << ",\"" << bakedPath << "\")\n";
    }
  };

  for(const fs::path& path: converterState.bakedFilePaths) {
    std::string extensionStr = path.extension().string();
    std::string fileName = path.filename().replace_extension("").string();
    std::string filePath = path.string();
    write(filePath, fileName, extensionStr.c_str());
  }

  for(auto [originalFileName, cachedItem] : oldCache) {
    u32 cachedBakedFileCount = (u32)cachedItem.bakedFiles.size();
    for(u32 i = 0; i < cachedBakedFileCount; i++) {
      const AssetBakeCachedItem::BakedFile& bakedFile = cachedItem.bakedFiles[i];
      std::string extensionStr = bakedFile.ext;
      std::string fileName = std::string(bakedFile.name.begin(), bakedFile.name.end() - bakedFile.ext.size());
      std::string filePath = bakedFile.path;
      write(filePath, fileName, extensionStr.c_str());
    }
  }

  outTexturesFile.close();
  outMeshFile.close();
  outMaterialFile.close();
  outPrefabFile.close();
}

void saveCache(const std::unordered_map<std::string, AssetBakeCachedItem> &oldCache,
               const std::vector<AssetBakeCachedItem> &newBakedItems) {
  nlohmann::json cacheJson;

  nlohmann::json bakedFiles;
  for(auto& [fileName, oldCacheItem] : oldCache) {
    nlohmann::json newCacheItemJson;
    newCacheItemJson[cacheJsonStrings.originalFileName] = oldCacheItem.originalFileName;
    newCacheItemJson[cacheJsonStrings.originalFileLastModified] = oldCacheItem.originalFileLastModified;
    nlohmann::json newCacheBakedFiles;
    u32 bakedFileCount = (u32)oldCacheItem.bakedFiles.size();
    for(u32 i = 0; i < bakedFileCount; i++) {
      nlohmann::json newCacheBakedFile;
      const AssetBakeCachedItem::BakedFile& bakedFile = oldCacheItem.bakedFiles[i];
      newCacheBakedFile[cacheJsonStrings.filePath] = bakedFile.path;
      newCacheBakedFile[cacheJsonStrings.fileName] = bakedFile.name;
      newCacheBakedFiles.push_back(newCacheBakedFile);
    }
    newCacheItemJson[cacheJsonStrings.bakedFiles] = newCacheBakedFiles;
    bakedFiles.push_back(newCacheItemJson);
  }

  for(auto& newCacheItem : newBakedItems) {
    nlohmann::json newCacheItemJson;
    newCacheItemJson[cacheJsonStrings.originalFileName] = newCacheItem.originalFileName;
    newCacheItemJson[cacheJsonStrings.originalFileLastModified] = newCacheItem.originalFileLastModified;
    nlohmann::json newCacheBakedFiles;
    u32 bakedFileCount = (u32)newCacheItem.bakedFiles.size();
    for(u32 i = 0; i < bakedFileCount; i++) {
      nlohmann::json newCacheBakedFile;
      const AssetBakeCachedItem::BakedFile& bakedFile = newCacheItem.bakedFiles[i];
      newCacheBakedFile[cacheJsonStrings.filePath] = bakedFile.path;
      newCacheBakedFile[cacheJsonStrings.fileName] = bakedFile.name;
      newCacheBakedFiles.push_back(newCacheBakedFile);
    }
    newCacheItemJson[cacheJsonStrings.bakedFiles] = newCacheBakedFiles;
    bakedFiles.push_back(newCacheItemJson);
  }

  cacheJson[cacheJsonStrings.cacheFiles] = bakedFiles;
  std::string jsonString = cacheJson.dump(1);
  writeFile(assetBakerCacheFileName, jsonString);
}

void loadCache(std::unordered_map<std::string, AssetBakeCachedItem> &assetBakeCache) {
  std::vector<char> fileBytes;

  if(!readFile(assetBakerCacheFileName, fileBytes)) {
    return;
  }

  std::string fileString(fileBytes.begin(), fileBytes.end());
  nlohmann::json cache = nlohmann::json::parse(fileString);

  nlohmann::json cachedFiles = cache[cacheJsonStrings.cacheFiles];

  for (auto& element : cachedFiles) {
    AssetBakeCachedItem cachedItem;
    cachedItem.originalFileName = element[cacheJsonStrings.originalFileName];
    cachedItem.originalFileLastModified = element[cacheJsonStrings.originalFileLastModified];
    u32 bakedFileCount = (u32)element[cacheJsonStrings.bakedFiles].size();
    for(u32 i = 0; i < bakedFileCount; i++) {
      nlohmann::json bakedFileJson = element[cacheJsonStrings.bakedFiles][i];
      AssetBakeCachedItem::BakedFile bakedFile;
      bakedFile.path = bakedFileJson[cacheJsonStrings.filePath];
      bakedFile.name = bakedFileJson[cacheJsonStrings.fileName];
      cachedItem.bakedFiles.push_back(bakedFile);
    }
    assetBakeCache[cachedItem.originalFileName] = cachedItem;
  }
}