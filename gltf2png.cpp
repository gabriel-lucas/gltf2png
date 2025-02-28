#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Color.h>
#include <filament/Camera.h>
#include <filament/LightManager.h>
#include <filament/TransformManager.h>
#include <filament/Viewport.h>
#include <filament/Texture.h>
#include <filament/RenderTarget.h>
#include <filament/SwapChain.h>
#include <backend/DriverEnums.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/Animator.h>
#include <gltfio/materials/uberarchive.h>
#include <utils/EntityManager.h>
#include <gltfio/FilamentInstance.h>

#include <math/norm.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <cmath>
#include <cstdarg>
#include <future>
#include <chrono>

using namespace filament::backend;
using namespace filament;
using namespace filament::math;
using namespace gltfio;
using namespace utils;

// Add output operators for Filament math types
std::ostream& operator<<(std::ostream& os, const float3& vec) {
    os << "[" << vec.x << ", " << vec.y << ", " << vec.z << "]";
    return os;
}

std::ostream& operator<<(std::ostream& os, const filament::Aabb& box) {
    os << "center: " << box.center() << " extent: " << box.extent();
    return os;
}

struct AppContext {
    Engine* engine = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    SwapChain* swapChain = nullptr;
    RenderTarget* renderTarget = nullptr;
    Texture* colorTexture = nullptr;
    FilamentAsset* asset = nullptr;
    ResourceLoader* resourceLoader = nullptr;
    MaterialProvider* materials = nullptr;
    AssetLoader* assetLoader = nullptr;
    std::vector<Entity> lightEntities;
    std::vector<uint8_t> pixels;
    Entity cameraEntity;
    TextureProvider* stbDecoder = nullptr;
    TextureProvider* ktxDecoder = nullptr;
    struct Config {
        std::string modelPath;
        std::string outputFile = "output.png";
        uint32_t width = 800;
        uint32_t height = 600;
        float cameraDistanceMultiplier = 2.0f;  // Adjusted for better framing
        float fovDegrees = 55.0f;               // Slightly wider FOV
    } config;
};

// Forward declarations
//void initializeFilament(AppContext& ctx);
//void loadModel(AppContext& ctx);
//void setupCamera(AppContext& ctx);
//void renderFrame(AppContext& ctx);
//void cleanupFilament(AppContext& ctx);

bool parseArguments(int argc, char** argv, AppContext& ctx) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model.gltf> [widthxheight] [output.png]\n";
        return false;
    }

    ctx.config.modelPath = argv[1];

    if (argc >= 3) {
        std::string resolution = argv[2];
        size_t xpos = resolution.find('x');
        if (xpos != std::string::npos) {
            try {
                ctx.config.width = std::stoi(resolution.substr(0, xpos));
                ctx.config.height = std::stoi(resolution.substr(xpos+1));
            } catch (...) {
                std::cerr << "Invalid resolution format. Using default 800x600\n";
            }
        }
    }

    if (argc >= 4) {
        ctx.config.outputFile = argv[3];
    }

    return true;
}

void logStep(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::cout << "STEP: " << buffer << std::endl;
    std::cout.flush();
}

std::vector<Entity> setupLighting(Engine* engine, Scene* scene) {
    std::vector<Entity> lights;
    EntityManager& em = EntityManager::get();

    // Create sun light
    Entity sun = em.create();
    LightManager::ShadowOptions shadowOptions {
        .mapSize = 1024,
        .shadowCascades = 4,
        .constantBias = 0.1f,
        .normalBias = 0.5f
    };
    LightManager::Builder(LightManager::Type::SUN)
        .color(Color::toLinear<ACCURATE>({0.9f, 0.9f, 0.8f}))
        .intensity(150000.0f)
        .shadowOptions(shadowOptions)
        .direction(normalize(float3{0.6f, -1.0f, -0.4f}))
        .build(*engine, sun);
    scene->addEntity(sun);
    lights.push_back(sun);

    // Create fill light
    Entity fillLight = em.create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color(Color::toLinear<ACCURATE>({0.8f, 0.8f, 1.0f}))
        .intensity(50000.0f)
        .direction(normalize(float3{-0.5f, -0.5f, -0.5f}))
        .build(*engine, fillLight);
    scene->addEntity(fillLight);
    lights.push_back(fillLight);

    return lights;
}


//setupCamera( 30.0f, 60.0f);

void setupCamera(AppContext& ctx) {
    logStep("Positioning camera");
    //filament::Aabb bbox = ctx.asset->getBoundingBox();
    //std::cout << "Model Bounding Box: " << bbox << std::endl;
    const auto& bbox = ctx.asset->getBoundingBox();
    const float3 center = bbox.center();
    const float3 extent = bbox.extent();

    // Calculate radius based on maximum extent dimension
    const float maxExtent = std::max({extent.x, extent.y, extent.z});
    const float radius = maxExtent * ctx.config.cameraDistanceMultiplier;

    EntityManager& em = EntityManager::get();
    ctx.cameraEntity = em.create();

    Camera* camera = ctx.engine->createCamera(ctx.cameraEntity);
    camera->setProjection(ctx.config.fovDegrees,
                         static_cast<float>(ctx.config.width)/ctx.config.height,
                         0.1f,  // Improved near plane
                         radius * 10);  // Dynamic far plane

    // Position camera looking at center with upward Y-axis
    auto& tm = ctx.engine->getTransformManager();
    const float3 eye = center + float3{0, 0, radius};
    tm.setTransform(tm.getInstance(ctx.cameraEntity),
                   mat4f::lookAt(eye, center, float3{0, 1, 0}));

    ctx.view->setCamera(camera);
}


/**
    Entity setupCamera(Engine* engine, View* view, uint32_t width, uint32_t height, const filament::Aabb& bbox, float thetaDegrees = 45.0f, float phiDegrees = 20.0f) {
    EntityManager& em = EntityManager::get();
    Entity cameraEntity = em.create();
    Camera* camera = engine->createCamera(cameraEntity);
    camera->setProjection(60.0f,                    // Vertical FOV (degrees)
                         float(width)/height, 
                         0.001f,                    // Near plane
                         500000.0f);                // Far plane
    
    view->setCamera(camera);

    auto& tm = engine->getTransformManager();
    float3 center = bbox.center();
    float extent = norm(bbox.extent());
    if (extent < 0.001f) {
        extent = 1.0f;  // Handle zero-sized models
        std::cerr << "Warning: Small bounding box extent detected, using default camera distance\n";
    }
   
    // Convert degrees to radians
    float theta = thetaDegrees * M_PI / 180.0f;
    float phi = phiDegrees * M_PI / 180.0f;
    
    // Calculate camera position using spherical coordinates
    float radius = extent * 2.5f;  // Increased distance for better framing
    float3 eye = {
        radius * sinf(phi) * cosf(theta),
        radius * cosf(phi),
        radius * sinf(phi) * sinf(theta)
    };
    eye += center;  // Center the camera around the model

    tm.setTransform(tm.getInstance(cameraEntity), 
           mat4f::lookAt(eye, center, float3{0, 1, 0}));
    
    return cameraEntity;
} */



void saveImage(const std::string& outputFile, std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) {
    logStep("Saving image %s", outputFile.c_str());

    // Convert from linear to sRGB and set alpha to 255
    for (size_t i = 0; i < pixels.size(); i += 4) {
        // Gamma correction (linear to sRGB)
        for (int j = 0; j < 3; ++j) { // R, G, B channels
            float linear = pixels[i + j] / 255.0f;
            float srgb = linear <= 0.0031308f ?
                linear * 12.92f :
                1.055f * std::pow(linear, 1.0f/2.4f) - 0.055f;
            pixels[i + j] = static_cast<uint8_t>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }
        // Force alpha to 255
        pixels[i + 3] = 255;
    }

    stbi_write_png(outputFile.c_str(), width, height, 4, pixels.data(), width * 4);
}

void initializeFilament(AppContext& ctx) {
    

    ctx.engine = Engine::create(Engine::Backend::OPENGL);
    
    // Configure quality settings


    logStep("Creating renderer components");
    ctx.renderer = ctx.engine->createRenderer();
    ctx.renderer->setClearOptions({
        .clearColor = {0.101f, 0.101f, 0.101f, 1.0f},
        .clear = true
    });

    logStep("Creating View");
    ctx.view = ctx.engine->createView();
    ctx.view->setAntiAliasing(View::AntiAliasing::FXAA);  // Enable AA
    //ctx.view->setAntiAliasing(View::AntiAliasing::NONE);
    //ctx.view->setSampleCount(4);  // MSAA samples
    
    // Configure AO and shadows
    ctx.view->setAmbientOcclusionOptions({
        .radius = 0.5f, .power = 2.0f, .bias = 0.01f, .resolution = 0.5f
    });
    ctx.view->setShadowType(View::ShadowType::PCF);
    ctx.view->setViewport({0, 0, ctx.config.width, ctx.config.height});
    ctx.scene = ctx.engine->createScene();

    logStep("Creating swap chain");
    ctx.swapChain = ctx.engine->createSwapChain(ctx.config.width, ctx.config.height);

    logStep("Creating render target");
    ctx.colorTexture = Texture::Builder()
        .width(ctx.config.width)
        .height(ctx.config.height)
        .levels(1)
        .format(Texture::InternalFormat::RGBA8)
        .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE | Texture::Usage::BLIT_SRC)
        .build(*ctx.engine);

    ctx.renderTarget = RenderTarget::Builder()
        .texture(RenderTarget::AttachmentPoint::COLOR0, ctx.colorTexture)
        .build(*ctx.engine);

    ctx.view->setViewport({0, 0, ctx.config.width, ctx.config.height});
    ctx.view->setRenderTarget(ctx.renderTarget);
    
    logStep("Setting up lighting");
    ctx.lightEntities = setupLighting(ctx.engine, ctx.scene);
}

void loadModel(AppContext& ctx){
    logStep("Loading 3D model");
    ctx.materials = createUbershaderProvider(ctx.engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
    ctx.assetLoader = AssetLoader::create({ctx.engine, ctx.materials, nullptr});
    
    std::ifstream file(ctx.config.modelPath, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open model file: " + ctx.config.modelPath);
    }
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> buffer(size);
    file.read((char*)buffer.data(), size);
    file.close();

    logStep("Creating Filament asset");
    //FilamentAsset* asset = loader->createAssetFromBinary(buffer.data(), size);
    ctx.asset = ctx.assetLoader->createAsset(buffer.data(), size);
    if (!ctx.asset || !ctx.asset->getRoot()) {
        throw std::runtime_error("Failed to load model: " + ctx.config.modelPath);
    }

    logStep("Loading resources");
    ResourceConfiguration resConfig;
    resConfig.engine = ctx.engine;
    resConfig.normalizeSkinningWeights = true;
    resConfig.gltfPath = ctx.config.modelPath.c_str();

    logStep("Path Model: %s",ctx.config.modelPath.c_str());
    ctx.resourceLoader = new ResourceLoader(resConfig);
    ctx.stbDecoder = createStbProvider(ctx.engine);
    //ctx.ktxDecoder = createKtx2Provider(engine);
    ctx.resourceLoader->addTextureProvider("image/png",  ctx.stbDecoder);
    ctx.resourceLoader->addTextureProvider("image/jpeg", ctx.stbDecoder);
    //resourceLoader->addTextureProvider("image/ktx2", ktxDecoder);
    
    bool asyncStarted = ctx.resourceLoader->asyncBeginLoad(ctx.asset);
    logStep("AsyncBeginLoad returned: %s", asyncStarted ? "true" : "false");
    if (!asyncStarted) {
        throw std::runtime_error("Failed to start asynchronous resource loading.");
    }

    logStep("Waiting for resources to load");
    while (ctx.resourceLoader->asyncGetLoadProgress() < 1.0f) {
        // This call is essential—it processes pending tasks
        ctx.resourceLoader->asyncUpdateLoad();
        
	    float progress = ctx.resourceLoader->asyncGetLoadProgress();
        std::cout << "Loading progress: " << progress << std::endl;
        if (progress <= 0.0f) {  // Add timeout for stalled loading
            static int attempts = 0;
            if (++attempts > 100) {  // 10 seconds timeout
                throw std::runtime_error("Resource loading timeout");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    
	// Cancel any remaining asynchronous tasks.
    ctx.resourceLoader->asyncCancelLoad();
    
    // Optionally flush one more time.
    ctx.engine->flushAndWait();

    std::cout << "Materials loaded via UbershaderProvider\n";
    logStep("Adding entities to scene");
    logStep("Number of entities: %zu", ctx.asset->getEntityCount());
    ctx.scene->addEntities(ctx.asset->getEntities(), ctx.asset->getEntityCount());
}

void renderFrame(AppContext& ctx) {
    logStep("Rendering scene");
    ctx.view->setScene(ctx.scene);
    
    // Render the main frame.
    logStep("Begin frame");
    if (!ctx.renderer->beginFrame(ctx.swapChain)) {
        throw std::runtime_error("Failed to begin frame");
    }
    
    logStep("Rendering view");
    ctx.renderer->render(ctx.view);
    
    logStep("Ending frame");
    ctx.renderer->endFrame();

    // Flush the engine to submit all pending GPU commands.
    logStep("Flushing engine");
    ctx.engine->flushAndWait();

    // Prepare the pixel buffer.
    logStep("Capturing pixels");
    ctx.pixels.resize(ctx.config.width * ctx.config.height * 4);

    // Set up a promise and future for the asynchronous readPixels callback.
    std::promise<void> promise;
    auto future = promise.get_future();

    // Define a lambda callback that logs when it's invoked and fulfills the promise.
    auto readCallback = [](void* /*buffer*/, size_t /*size*/, void* user) {
        std::cout << "readPixels callback invoked" << std::endl;
        static_cast<std::promise<void>*>(user)->set_value();
    };

    // Create the PixelBufferDescriptor.
    Texture::PixelBufferDescriptor descriptor(
        ctx.pixels.data(),
        ctx.pixels.size(),
        Texture::Format::RGBA,
        Texture::Type::UBYTE,
        readCallback,
        &promise
    );

    logStep("Reading pixels from render target");
    ctx.renderer->readPixels(
        ctx.renderTarget,
        0, 0,
        ctx.config.width, ctx.config.height,
        std::move(descriptor)
    );

    // Submit a dummy frame using the same swapChain to force processing of pending commands.
    logStep("Submitting dummy frame to process readPixels callback");
    if (ctx.renderer->beginFrame(ctx.swapChain)) {
        ctx.renderer->endFrame();
    } else {
        logStep("Dummy frame beginFrame() failed");
    }

    logStep("Waiting for readPixels callback to complete");
    future.wait();  // Blocks until the callback calls promise.set_value().
    logStep("readPixels callback completed");
}


void renderFrame2(AppContext& ctx){
    
        logStep("Rendering scene");
        ctx.view->setScene(ctx.scene);
        
        logStep("Begin frame");
        if (!ctx.renderer->beginFrame(ctx.swapChain)) {
            throw std::runtime_error("Failed to begin frame");
        }

        logStep("Rendering view");
        ctx.renderer->render(ctx.view);

        logStep("Ending frame");
        ctx.renderer->endFrame();

        // Add additional synchronization
        logStep("Waiting for GPU completion");
        ctx.engine->flushAndWait();
        //std::this_thread::sleep_for(std::chrono::milliseconds(200));

        logStep("Capturing pixels");
        ctx.pixels.resize(ctx.config.width * ctx.config.height * 4);

        std::promise<void> promise;
        auto future = promise.get_future();
        Texture::PixelBufferDescriptor descriptor(
            ctx.pixels.data(),
            ctx.pixels.size(),
            Texture::Format::RGBA,
            Texture::Type::UBYTE,
            [](void* buffer, size_t size, void* user) {
                static_cast<std::promise<void>*>(user)->set_value();
            },
            &promise
        );


        logStep("Reading pixels from render target");
        ctx.renderer->readPixels(
            ctx.renderTarget,
            0, 0,
            ctx.config.width, ctx.config.height,
            std::move(descriptor)
        );

        // Submit a dummy frame to force the GPU to process pending commands and trigger the readPixels callback.
        logStep("Submitting dummy frame to process readPixels callback");
        ctx.renderer->renderStandaloneView(ctx.view);
        
        future.wait();  // Blocks until pixels are ready


        /** Create descriptor first to ensure scope
        {
            Texture::PixelBufferDescriptor descriptor(
                ctx.pixels.data(),
                ctx.pixels.size(),
                Texture::Format::RGBA,
                Texture::Type::UBYTE,
                1  // Alignment
            );
        
        
           
            
        } */

        //ctx.engine->flushAndWait();
        //std::this_thread::sleep_for(std::chrono::milliseconds(100));


}

void cleanupFilament(AppContext& ctx){
    logStep("Cleaning up resources");
    ctx.view->setScene(nullptr);
        
    // Delete ResourceLoader
    delete ctx.resourceLoader;
	delete ctx.stbDecoder;	
    //delete ktxDecoder;
 	
	ctx.assetLoader->destroyAsset(ctx.asset);
    AssetLoader::destroy(&ctx.assetLoader);
    if (ctx.materials) {
        ctx.materials->destroyMaterials();
        //gltfio::MaterialProvider::destroy(ctx.materials);
    }

    // Destroy the light entities
    for (Entity light : ctx.lightEntities) {
        ctx.engine->destroy(light);
    }

    //engine->destroy(colorGrading);
    ctx.engine->destroy(ctx.cameraEntity);
	ctx.engine->destroy(ctx.renderTarget);
    ctx.engine->destroy(ctx.colorTexture);
    ctx.engine->destroy(ctx.view);
    ctx.engine->destroy(ctx.scene);
    ctx.engine->destroy(ctx.renderer);
    
    
    logStep("Destroying engine");
    Engine::destroy(&ctx.engine);
}

int main(int argc, char** argv) {
    AppContext ctx;
    try {
        if (!parseArguments(argc, argv, ctx)) return EXIT_FAILURE;
        
    	logStep("Creating Filament engine");
        initializeFilament(ctx);
        loadModel(ctx);
        setupCamera(ctx);
        renderFrame(ctx);
        saveImage(ctx.config.outputFile, ctx.pixels, ctx.config.width, ctx.config.height);
        cleanupFilament(ctx);
         logStep("Program completed successfully");
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        cleanupFilament(ctx);
        return EXIT_FAILURE;
    }
}