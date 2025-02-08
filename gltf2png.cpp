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
#include <gltfio/MaterialProvider.h>

#include <utils/EntityManager.h>
#include <math/norm.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <stb_image_write.h>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <cmath>

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

struct AppConfig {
    std::string modelPath;
    std::string outputFile = "output.png";
    uint32_t width = 800;
    uint32_t height = 600;
};

bool parseArguments(int argc, char** argv, AppConfig& config) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model.gltf> [widthxheight] [output.png]\n";
        return false;
    }

    config.modelPath = argv[1];

    if (argc >= 3) {
        std::string resolution = argv[2];
        size_t xpos = resolution.find('x');
        if (xpos != std::string::npos) {
            try {
                config.width = std::stoi(resolution.substr(0, xpos));
                config.height = std::stoi(resolution.substr(xpos+1));
            } catch (...) {
                std::cerr << "Invalid resolution format. Using default 800x600\n";
            }
        }
    }

    if (argc >= 4) {
        config.outputFile = argv[3];
    }

    return true;
}

/** void logStep(const char* message) {
    std::cout << "STEP: " << message << std::endl;
    std::cout.flush();
}*/

void logStep(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::cout << "STEP: " << buffer << std::endl;
    std::cout.flush();
}

void setupLighting(Engine* engine, Scene* scene) {
    EntityManager& em = EntityManager::get();
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

    // Add fill light
    Entity fillLight = em.create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color(Color::toLinear<ACCURATE>({0.8f, 0.8f, 1.0f})) // Cool fill
        .intensity(50000.0f)
        .direction(normalize(float3{-0.5f, -0.5f, -0.5f}))
        .build(*engine, fillLight);
    scene->addEntity(fillLight);
}

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
}

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

int main(int argc, char** argv) {
    try {
        logStep("Program started");
        AppConfig config;
        if (!parseArguments(argc, argv, config)) return EXIT_FAILURE;

        logStep("Creating Filament engine");
        Engine* engine = Engine::create(Engine::Backend::OPENGL); 
        
        logStep("Creating renderer components");
        Renderer* renderer = engine->createRenderer();
        Scene* scene = engine->createScene();
        View* view = engine->createView();
	
        //view->setAntiAliasing(View::AntiAliasing::FXAA);
        view->setAntiAliasing(View::AntiAliasing::NONE);
        view->setSampleCount(8);  // Enable 4x MSAA
	view->setToneMapping(View::ToneMapping::ACES);  // Better contrast

        renderer->setClearOptions({
            .clearColor = {0.101f, 0.101f, 0.101f, 1.0f}, // #222222 background
            .clear = true
        });

        // Add ambient occlusion (after creating renderer)
        view->setAmbientOcclusionOptions({
            .radius = 0.5f,
            .power = 2.0f,
            .bias = 0.01f,
            .resolution = 0.25f
        });

        // Enable better shadows
        view->setShadowType(View::ShadowType::PCF);
	
        logStep("Creating swap chain");
        SwapChain* swapChain = engine->createSwapChain(config.width, config.height, 0);

        logStep("Creating render target");
        Texture* colorTexture = Texture::Builder()
            .width(config.width)
            .height(config.height)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*engine);

        RenderTarget* renderTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR0, colorTexture)
            .build(*engine);

        view->setViewport({0, 0, config.width, config.height});
        view->setRenderTarget(renderTarget);

        logStep("Setting up lighting");
        setupLighting(engine, scene);

        logStep("Loading 3D model");
        MaterialProvider* materials = createUbershaderLoader(engine);
        AssetLoader* loader = AssetLoader::create({engine, materials, nullptr});
        
        std::ifstream file(config.modelPath, std::ios::ate | std::ios::binary);
        if (!file) {
            throw std::runtime_error("Could not open model file: " + config.modelPath);
        }

        size_t size = file.tellg();
        file.seekg(0);
        std::vector<uint8_t> buffer(size);
        file.read((char*)buffer.data(), size);
        file.close();

        logStep("Creating Filament asset");
        FilamentAsset* asset = loader->createAssetFromBinary(buffer.data(), size);
        if (!asset) {
            throw std::runtime_error("Failed to load model: " + config.modelPath);
        }

       
	
        if (asset->getMaterialInstanceCount() == 0) {
            throw std::runtime_error("No materials loaded in asset");
        }
        std::cout << "Loaded " << asset->getMaterialInstanceCount() << " materials\n";

        logStep("Adding entities to scene");
        scene->addEntities(asset->getEntities(), asset->getEntityCount());

        logStep("Positioning camera");
        filament::Aabb bbox = asset->getBoundingBox();
        std::cout << "Model Bounding Box: " << bbox << std::endl;
        Entity cameraEntity = setupCamera(engine, view, config.width, config.height, bbox, 30.0f, 60.0f);

        logStep("Rendering scene");
        view->setScene(scene);
        
        logStep("Begin frame");
        if (!renderer->beginFrame(swapChain)) {
            throw std::runtime_error("Failed to begin frame");
        }

        logStep("Rendering view");
        renderer->render(view);

        logStep("Ending frame");
        renderer->endFrame();

        // Add additional synchronization
        logStep("Waiting for GPU completion");
        engine->flushAndWait();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        logStep("Capturing pixels");
        std::vector<uint8_t> pixels(config.width * config.height * 4);
        bool readSuccess = false;
	
        // Create descriptor first to ensure scope
        {
            Texture::PixelBufferDescriptor descriptor(
                pixels.data(),
                pixels.size(),
                Texture::Format::RGBA,
                Texture::Type::UBYTE,
                1  // Alignment
            );
        
            logStep("Reading pixels from render target");
            renderer->readPixels(
                renderTarget,
                0, 0,
                config.width, config.height,
                std::move(descriptor)
            );
        }

	logStep("Applying post-processing");
        const float sharpenAmount = 0.8f;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            // Simple sharpen kernel
            if (i > config.width * 4 + 4) {  // Avoid edges
                for (int c = 0; c < 3; c++) {
                    int current = pixels[i + c];
                    int left = pixels[i - 4 + c];
                    int right = pixels[i + 4 + c];
                    int up = pixels[i - config.width * 4 + c];
                    int down = pixels[i + config.width * 4 + c];
                    
                    int sharpened = current * (1 + 4 * sharpenAmount) 
                                  - (left + right + up + down) * sharpenAmount;
                    pixels[i + c] = static_cast<uint8_t>(std::clamp(sharpened, 0, 255));
                }
            }
        }

        engine->flushAndWait();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

	

        saveImage(config.outputFile, pixels, config.width, config.height);

        logStep("Cleaning up resources");
        loader->destroyAsset(asset);
        AssetLoader::destroy(&loader);
        materials->destroyMaterials();
        delete materials;
        
        engine->destroy(cameraEntity);
	    engine->destroy(renderTarget);
        engine->destroy(colorTexture);
        engine->destroy(view);
        engine->destroy(scene);
        engine->destroy(renderer);
        engine->destroy(swapChain);
        
        logStep("Destroying engine");
        Engine::destroy(&engine);

        logStep("Program completed successfully");
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
        return EXIT_FAILURE;
    }
}
