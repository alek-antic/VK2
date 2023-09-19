#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

struct SDL_Window;

namespace core {

////////////////////////////////////////////////////////////////////////////////
// Class to run the application. 
class Engine {
public:
	explicit Engine(const std::string & appName);
	~Engine();

	int Exec();
private:
	// Initialize the SDL window
	void Initialize();

	// Initialize Vulkan constructs
	void InitVulkan();

	// Initialize swapchain
	void InitSwapchain();

	// Initialize Vulkan Commands
	void InitCommands();

	// Initialize renderpass
	void InitDefaultRenderpass();

	// Initialize framebuffers
	void InitFramebuffers();

	// Initialize synchrnoization constructs
	void InitSyncStructures();

	void InitPipelines();

	// Destroy the SDL window and Vulkan constructs
	void Cleanup();

	// Draw loop
	void Draw();

	// Run the main event loop
	void Run();

	// Throws if fails
	void LoadShaderModule(const std::string & glslPath, VkShaderModule & outShaderModule);
	// Compile a glsl shader to spv, and cache it to inOutSpvPath.
	// If it is not provided, glslPath.spv will be used as a default.
	bool CompileGlslToSpv(const std::string & glslPath, std::optional<std::string> inOutSpvPath);

private:
	// Window members
	SDL_Window * Window = nullptr;
	VkExtent2D WindowExtents = { 1280, 700 };

	// State members
	bool IsInitialized = false;
	std::string AppName;
	uint64_t FrameNumber = 0;

	// Vulkan members
	VkInstance Instance;
	VkDebugUtilsMessengerEXT DebugMessenger;
	VkPhysicalDevice ChosenGPU;
	VkDevice Device;
	VkSurfaceKHR Surface;

	// Swapchain members
	VkSwapchainKHR Swapchain;
	VkFormat SwapchainFormat;
	std::vector<VkImage> SwapchainImages;
	std::vector<VkImageView> SwapchainImageViews;

	// Commands members
	VkQueue GraphicsQueue;
	uint32_t GraphicsQueueFamily;
	VkCommandPool CommandPool;
	VkCommandBuffer MainCommandBuffer;

	// Renderpass members
	VkRenderPass RenderPass;
	std::vector<VkFramebuffer> Framebuffers;

	// Synchronization members
	VkSemaphore PresentSemaphore;
	VkSemaphore RenderSemaphore;
	VkFence RenderFence;
};

} // namespace core
