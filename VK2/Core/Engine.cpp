#include "Engine.h"

#include "VkBootStrap/VkBootstrap.h"


#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <shaderc/shaderc.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <Windows.h>

namespace core {

namespace {

////////////////////////////////////////////////////////////////////////////////

// Create a command pool for commands submitted to the graphics queue
VkCommandPoolCreateInfo CommandPoolCreateInfo(
	uint32_t queueFamilyIdx
	, VkCommandPoolCreateFlags flags = 0)
{
	VkCommandPoolCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.pNext = nullptr;

	info.queueFamilyIndex = queueFamilyIdx;
	info.flags = flags;
	return info;
}

////////////////////////////////////////////////////////////////////////////////

// Allocate a command buffer that we will use for rendering
VkCommandBufferAllocateInfo CommandBufferAllocateInfo(
	VkCommandPool pool
	, uint32_t count = 1
	, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
{
	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.pNext = nullptr;

	info.commandPool = pool;
	info.commandBufferCount = count;
	info.level = level;

	return info;
}

////////////////////////////////////////////////////////////////////////////////

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

Engine::Engine(const std::string & appName) 
	: AppName(appName)
{

}

////////////////////////////////////////////////////////////////////////////////

Engine::~Engine() {

}

////////////////////////////////////////////////////////////////////////////////

int Engine::Exec() {
	try {
		Initialize();
	} catch (const std::exception & e) {
		std::cout << e.what() << std::endl;
		return 1;
	}

	Run();

	try {
		Cleanup();
	} catch (const std::exception & e) {
		std::cout << e.what() << std::endl;
		return 1;
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////

void Engine::Initialize() {
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		throw std::runtime_error("Could not initialize SDL.");

	// Cast to silence compiler warning
	SDL_WindowFlags windowFlags = (SDL_WindowFlags) SDL_WINDOW_VULKAN;

	Window = SDL_CreateWindow(
		AppName.c_str()
		, SDL_WINDOWPOS_CENTERED
		, SDL_WINDOWPOS_CENTERED
		, WindowExtents.width
		, WindowExtents.height
		, windowFlags
	);

	if (!Window)
		throw std::runtime_error("Failed to create SDL Window");

	InitVulkan();
	InitSwapchain();
	InitCommands();
	InitDefaultRenderpass();
	InitFramebuffers();
	InitSyncStructures();
	InitPipelines();

	IsInitialized = true;
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitVulkan() {
	// Built the Vulkan Instance with basic debug features if building in Debug config
	vkb::InstanceBuilder builder;
	auto instRet = builder.set_app_name(AppName.c_str())
#if defined(_DEBUG)
		.request_validation_layers(true)
#endif
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkbInst = instRet.value();

	Instance = vkbInst.instance;
	DebugMessenger = vkbInst.debug_messenger;

	// Get the surface of the window opened with SDL
	SDL_Vulkan_CreateSurface(Window, Instance, &Surface);

	// use VkBootstrap to select a GPU
	vkb::PhysicalDeviceSelector selector(vkbInst);
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 1)
		.set_surface(Surface)
		.select()
		.value();

	ChosenGPU = physicalDevice.physical_device;

	// Use VkBootstrap to build the driver from the physical GPU
	vkb::DeviceBuilder deviceBuilder(physicalDevice);
	vkb::Device vkbDevice = deviceBuilder.build().value();

	Device = vkbDevice.device;

	// Use VkBootstrap to get a Graphics queue
	GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	GraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitSwapchain() {
	vkb::SwapchainBuilder swapchainBuilder(ChosenGPU, Device, Surface);

	vkb::Swapchain vkbSwapchain = swapchainBuilder.use_default_format_selection()
		// This is where to swap V-Sync setting
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(WindowExtents.width, WindowExtents.height)
		.build()
		.value();

	// Store swapchain & images in engine
	Swapchain = vkbSwapchain.swapchain;
	SwapchainImages = vkbSwapchain.get_images().value();
	SwapchainImageViews = vkbSwapchain.get_image_views().value();
	SwapchainFormat = vkbSwapchain.image_format;
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitCommands() {
	auto commandPoolInfo = CommandPoolCreateInfo(
		GraphicsQueueFamily
		, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	);
	
	if (vkCreateCommandPool(Device, &commandPoolInfo, nullptr, &CommandPool))
		throw std::runtime_error("Failed to create command pool.");

	VkCommandBufferAllocateInfo cmdAllocInfo = CommandBufferAllocateInfo(CommandPool);

	if (vkAllocateCommandBuffers(Device, &cmdAllocInfo, &MainCommandBuffer))
		throw std::runtime_error("Failed to allocate a command buffer.");
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitDefaultRenderpass() {
	// The renderpass will use this color attachment. 
	// This is a description of the image we will write into with render commands.
	VkAttachmentDescription colorAttachment = {};
	// We use the format from the swapchain so we can pass it through the swapchain
	colorAttachment.format = SwapchainFormat;
	// Change this if implementing MSAA
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	// We Clear when this attacment is loaded
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	// We keep the attacment stored when the renderpass ends
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// We don't care about stencil
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// We don't know or care about the starting layout of the attachment
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// After the renderpass ends, the image has to be on a layout ready for display
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Add a subpass that will render into colorAttachment
	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Create 1 subpass (minimum)
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	if (vkCreateRenderPass(Device, &renderPassInfo, nullptr, &RenderPass))
		throw std::runtime_error("Failed to create render pass");
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitFramebuffers() {
	// Create the framebuffers for the swapchain images.
	// This connects the renderpass to the images for rendering.
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;

	fbInfo.renderPass = RenderPass;
	fbInfo.attachmentCount = 1;
	fbInfo.width = WindowExtents.width;
	fbInfo.height = WindowExtents.height;
	fbInfo.layers = 1;

	// Grab the number of images in the swapchain
	const size_t swapchainSize = SwapchainImages.size();
	Framebuffers.resize(swapchainSize);

	for (size_t i = 0; i < swapchainSize; i++) {
		fbInfo.pAttachments = &SwapchainImageViews[i];
		if (vkCreateFramebuffer(Device, &fbInfo, nullptr, &Framebuffers[i]))
			throw std::runtime_error("Failed to create a framebuffer");
	}
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitSyncStructures() {
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	// Use signaled flag so we can wait on this fence before using it on a GPU command
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	if (vkCreateFence(Device, &fenceCreateInfo, nullptr, &RenderFence))
		throw std::runtime_error("Failed to create fence");

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	if (vkCreateSemaphore(Device, &semaphoreCreateInfo, nullptr, &PresentSemaphore))
		throw std::runtime_error("Failed to create present semaphore");

	if (vkCreateSemaphore(Device, &semaphoreCreateInfo, nullptr, &RenderSemaphore))
		throw std::runtime_error("Failed to create render semaphore");
}

////////////////////////////////////////////////////////////////////////////////

void Engine::InitPipelines() {
	VkShaderModule triangleVertShader, triangleFragShader;
	LoadShaderModule("./Shaders/triangle.vert", triangleVertShader);
	LoadShaderModule("./Shaders/triangle.frag", triangleFragShader);
}

////////////////////////////////////////////////////////////////////////////////

void Engine::Cleanup() {
	if (!IsInitialized)
		return;
	
	// Vulkan objects need to be destroyed in reverse order of creation

	// Destroying command pool will destroy all command buffers that have been allocated from it
	vkDestroyCommandPool(Device, CommandPool, nullptr);

	vkDestroySwapchainKHR(Device, Swapchain, nullptr);

	vkDestroyRenderPass(Device, RenderPass, nullptr);
	
	for (size_t i = 0; i < SwapchainImageViews.size(); i++) {
		vkDestroyFramebuffer(Device, Framebuffers[i], nullptr);
		vkDestroyImageView(Device, SwapchainImageViews[i], nullptr);
	}

	vkDestroyDevice(Device, nullptr);
	vkDestroySurfaceKHR(Instance, Surface, nullptr);
	vkb::destroy_debug_utils_messenger(Instance, DebugMessenger);
	vkDestroyInstance(Instance, nullptr);

	SDL_DestroyWindow(Window);
}

////////////////////////////////////////////////////////////////////////////////

void Engine::Draw() {
	// Wait until GPU has finished rendering the previous frame, timeout after 1 second
	vkWaitForFences(Device, 1, &RenderFence, true, 1000000000);
	vkResetFences(Device, 1, &RenderFence);

	// Request image from swapchain, timeout after 1 second
	uint32_t swapchainImageIdx;
	vkAcquireNextImageKHR(Device, Swapchain, 1000000000, PresentSemaphore, nullptr, &swapchainImageIdx);

	// Empty command buffer, since we know that all commands have been executed (fence is cleared)
	vkResetCommandBuffer(MainCommandBuffer, 0);
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;

	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(MainCommandBuffer, &cmdBeginInfo);

	// Compute a clear color from the frame number
	VkClearValue clearValue;
	float flash = abs(sin(FrameNumber / 120.0f));
	clearValue.color = { 0.f, 0.f, flash, 1.f };

	// Start the main renderpass
	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;

	rpInfo.renderPass = RenderPass;
	rpInfo.renderArea.offset = { 0, 0 };
	rpInfo.renderArea.extent = WindowExtents;
	rpInfo.framebuffer = Framebuffers[swapchainImageIdx];

	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(MainCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdEndRenderPass(MainCommandBuffer);
	vkEndCommandBuffer(MainCommandBuffer);

	// Prepare submission to the queue

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &PresentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &RenderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &MainCommandBuffer;

	// RenderFence will block draw until the graphics queue is cleared of all commands
	vkQueueSubmit(GraphicsQueue, 1, &submit, RenderFence);

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &Swapchain;

	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &RenderSemaphore;

	presentInfo.pImageIndices = &swapchainImageIdx;

	vkQueuePresentKHR(GraphicsQueue, &presentInfo);

	FrameNumber++;
}

////////////////////////////////////////////////////////////////////////////////

void Engine::Run() {
	bool stillRunning = true;

	while (stillRunning) {

		SDL_Event e;
		while (SDL_PollEvent(&e)) {

			switch (e.type) {
			case SDL_QUIT:
				stillRunning = false;
				break;

			default:
				break;
			}
		}

		Draw();
	}
}

////////////////////////////////////////////////////////////////////////////////

void Engine::LoadShaderModule(const std::string & glslPath, VkShaderModule & outShaderModule) {
	const std::string spvPath = glslPath + ".spv";
	
	// Check if we have compiled this glsl into spir-v already.
	// Rerun compilation if the glsl has been dirtied since the previous compilation
	if (!std::filesystem::exists(spvPath) || 
		std::filesystem::last_write_time(glslPath) > std::filesystem::last_write_time(spvPath)) {
		if (!CompileGlslToSpv(glslPath, spvPath))
			throw std::runtime_error("Failed to compile " + glslPath + " to spv");
	}

	// Open the spv file here
	std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("Failed to open shader file!");

	// Allocate buffer to size of file (in bytes)
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	// Reset the cursor of the file to the top
	file.seekg(0);
	// Read spv into buffer
	file.read(buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size();
	createInfo.pCode = reinterpret_cast<const uint32_t *>(buffer.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(Device, &createInfo, nullptr, &shaderModule)) {
		throw std::runtime_error("Failed to create shader module from " + spvPath);
	}

	outShaderModule = shaderModule;
}

////////////////////////////////////////////////////////////////////////////////

bool Engine::CompileGlslToSpv(const std::string & glslPath, std::optional<std::string> inOutSpvPath) {
	if (!inOutSpvPath.has_value())
		inOutSpvPath = glslPath + ".spv";

	if (!std::filesystem::exists(glslPath))
		return false;

	std::ifstream file(glslPath, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		return false;
	
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	// Ensure null terminated
	buffer.push_back(0);

	// Create shader compiler
	shaderc::Compiler compiler;
	auto result = compiler.CompileGlslToSpv(
		buffer.data()
		, shaderc_shader_kind::shaderc_glsl_infer_from_source
		, glslPath.c_str()
	);

	if (result.GetNumErrors() > 0) {
		auto errorString = result.GetErrorMessage();
		OutputDebugString(std::wstring(errorString.begin(), errorString.end()).c_str());
		OutputDebugString(L"\n");
		return false;
	}

	std::ofstream outFile(*inOutSpvPath, std::ios::binary);
	if (!outFile.is_open())
		return false;

	// Write compiled spir-v to file
	outFile.write((char *)result.cbegin(), (result.cend() - result.cbegin()) * sizeof(uint32_t));
	outFile.close();

	return true;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace core
