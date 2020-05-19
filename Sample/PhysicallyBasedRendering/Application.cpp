#include "Application.h"
#include <glm/gtc/matrix_transform.hpp>
#include "Frame.h"
#include "Render.h"

Application::Application(
	const std::shared_ptr<sgl::Window>& window, 
	const draw_model_enum draw_model /*= draw_model_enum::SPHERE*/,
	const texture_model_enum texture_model /*= texture_model_enum::METAL*/) :
	window_(window),
	draw_model_(draw_model),
	texture_model_(texture_model) {}

bool Application::Startup()
{
	auto device = window_->GetUniqueDevice();
	device->Startup();
	// Comment out if you want to see the errors.
	// window_->Startup();

	// Create Environment cube map texture.
	auto texture = std::make_shared<sgl::TextureCubeMap>(
		"../Asset/CubeMap/Hamarikyu.hdr",
		std::make_pair<std::uint32_t, std::uint32_t>(512, 512),
		sgl::PixelElementSize::FLOAT,
		sgl::PixelStructure::RGB);

	auto mesh = CreatePhysicallyBasedRenderedMesh(device, texture);

	// Create the back cube map.
	auto cube_map_mesh = CreateCubeMapMesh(device, texture);

	// Pack it into a Scene object.
	sgl::SceneTree scene_tree{};
	{
		auto scene_root = std::make_shared<sgl::SceneMatrix>(glm::mat4(1.0));
		scene_tree.AddNode(scene_root);
		scene_tree.AddNode(
			std::make_shared<sgl::SceneMesh>(cube_map_mesh),
			scene_root);
		auto scene_matrix = std::make_shared<sgl::SceneMatrix>(glm::mat4(1.0));
		scene_tree.AddNode(scene_matrix, scene_root);
		scene_tree.AddNode(
			std::make_shared<sgl::SceneMesh>(mesh),
			scene_matrix);
	}

	device->SetSceneTree(scene_tree);
	return true;
}

void Application::Run()
{
	window_->SetDraw([this](
		const double dt, 
		std::shared_ptr<sgl::Texture>& texture)
	{
		// Update the camera.
		float dtf = static_cast<float>(dt);
		auto device = window_->GetUniqueDevice();
		glm::vec4 position = { 0.f, 0.f, 2.f, 1.f };
		glm::mat4 rot_y(1.0f);
		rot_y = glm::rotate(rot_y, dtf * -.1f, glm::vec3(0.f, 1.f, 0.f));
		sgl::Camera cam(glm::vec3(position * rot_y), { 0.f, 0.f, 0.f });
		device->SetCamera(cam);
		if (pbr_program_)
		{
			// Don't forget to use before setting any uniform.
			pbr_program_->Use();
			pbr_program_->UniformVector3(
				"camera_position",
				device->GetCamera().GetPosition());
		}
		texture = AddBloom(texture);
	});
	window_->Run();
}

std::vector<std::string> Application::CreateTextures(
	sgl::TextureManager& texture_manager,
	const std::shared_ptr<sgl::TextureCubeMap>& texture) const
{
	// Get the name of the texture.
	std::string name = texture_model_texture_map_.at(texture_model_);

	// Add the default texture to the texture manager.
	texture_manager.AddTexture("Environment", texture);

	// Create the Monte-Carlo prefilter.
	auto monte_carlo_prefilter = std::make_shared<sgl::TextureCubeMap>(
		std::make_pair<std::uint32_t, std::uint32_t>(128, 128), 
		sgl::PixelElementSize::FLOAT, 
		sgl::PixelStructure::RGB);
	sgl::FillProgramMultiTextureCubeMapMipmap(
		std::vector<std::shared_ptr<sgl::Texture>>{ monte_carlo_prefilter },
		texture_manager,
		{ "Environment" },
		sgl::CreateProgram("MonteCarloPrefilter"),
		5,
		[](const int mipmap, const std::shared_ptr<sgl::Program>& program)
		{
			float roughness = static_cast<float>(mipmap) / 4.0f;
			program->UniformFloat("roughness", roughness);
		});
	texture_manager.AddTexture("MonteCarloPrefilter", monte_carlo_prefilter);

	// Create the Irradiance cube map texture.
	auto irradiance = std::make_shared<sgl::TextureCubeMap>(
		std::make_pair<std::uint32_t, std::uint32_t>(32, 32),
		sgl::PixelElementSize::FLOAT,
		sgl::PixelStructure::RGB);
	sgl::FillProgramMultiTextureCubeMap(
		std::vector<std::shared_ptr<sgl::Texture>>{ irradiance },
		texture_manager,
		{ "Environment" },
		sgl::CreateProgram("IrradianceCubeMap"));
	texture_manager.AddTexture("Irradiance", irradiance);

	// Create the LUT BRDF.
	auto integrate_brdf = std::make_shared<sgl::Texture>(
		std::make_pair<std::uint32_t, std::uint32_t>(512, 512),
		sgl::PixelElementSize::FLOAT,
		sgl::PixelStructure::RGB);
	sgl::FillProgramMultiTexture(
		std::vector<std::shared_ptr<sgl::Texture>>{ integrate_brdf },
		texture_manager,
		{},
		sgl::CreateProgram("IntegrateBRDF"));
	texture_manager.AddTexture("IntegrateBRDF", integrate_brdf);

	// Create the texture and bind it to the mesh.
	texture_manager.AddTexture(
		"Color",
		std::make_shared<sgl::Texture>("../Asset/" + name + "/Color.jpg"));
	texture_manager.AddTexture(
		"Normal",
		std::make_shared<sgl::Texture>("../Asset/" + name + "/Normal.jpg"));
	texture_manager.AddTexture(
		"Metallic",
		std::make_shared<sgl::Texture>("../Asset/" + name + "/Metalness.jpg"));
	texture_manager.AddTexture(
		"Roughness",
		std::make_shared<sgl::Texture>("../Asset/" + name + "/Roughness.jpg"));
	texture_manager.AddTexture(
		"AmbientOcclusion",
		std::make_shared<sgl::Texture>(
			"../Asset/" + name + "/AmbientOcclusion.jpg"));

	return 
	{ 
		"Color",
		"Normal",
		"Metallic",
		"Roughness",
		"AmbientOcclusion",
		"MonteCarloPrefilter",
		"Irradiance",
		"IntegrateBRDF" 
	};
}

std::shared_ptr<sgl::Texture> Application::AddBloom(
	const std::shared_ptr<sgl::Texture>& texture) const
{
	auto brightness = CreateBrightness(texture);
	auto gaussian_blur = CreateGaussianBlur(brightness);
	return gaussian_blur;
	auto merge = MergeDisplayAndGaussianBlur(texture, gaussian_blur);
	return merge;
}

std::shared_ptr<sgl::Texture> Application::CreateBrightness(
	const std::shared_ptr<sgl::Texture>& texture) const
{
	//You can get the size from the texture.
	auto size = texture->GetSize();
	//Initialize the frame and the render
	sgl::Frame frame = sgl::Frame();
	sgl::Render render = sgl::Render();
	//Create a new texture
	auto tex = std::make_shared<sgl::Texture>(size, sgl::PixelElementSize::FLOAT);
	//Bind it
	frame.BindTexture(*tex);
	//A texture manager
	sgl::TextureManager texture_manager{};
	//Add the texture
	texture_manager.AddTexture("Brightness", texture);
	//Create the program
	auto program = sgl::CreateProgram("Brightness");
	//Create the quad
	auto quad = CreateQuadMesh(program);
	//Add the texture to the quad
	quad->SetTextures({ "Brightness" });
	//Draw
	quad->Draw(texture_manager);
	//Return the new texture
	return tex;
}

std::shared_ptr<sgl::Texture> Application::CreateGaussianBlur(
	const std::shared_ptr<sgl::Texture>& texture) const
{
	//You can get the size from the texture.
	auto size = texture->GetSize();

	sgl::Render render;

	// You will need 2 frame And you will need 2 textures -> Organize them in an array
	std::shared_ptr<sgl::Texture> textures[2];
	textures[0] = std::make_shared<sgl::Texture>(size, sgl::PixelElementSize::FLOAT);
	textures[1] = std::make_shared<sgl::Texture>(size, sgl::PixelElementSize::FLOAT);

	sgl::Frame frames[2];
	frames[0].BindAttach(render);
	frames[1].BindAttach(render);

	render.BindStorage(size);

	glViewport(0, 0, size.first, size.second);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	frames[0].BindTexture(*textures[0]);
	frames[1].BindTexture(*textures[1]);

	//Create the program
	auto program = sgl::CreateProgram("GaussianBlur");
	//Create the quad
	auto quad = CreateQuadMesh(program);

	bool horizontal = true;
	bool beginning = true;
	
	for (int it = 0; it < 10; it++) {
		sgl::TextureManager texture_manager;

		program->UniformInt("horizontal", horizontal);

		frames[horizontal].Bind();

		texture_manager.AddTexture(
			"Image",
			(beginning) ? texture : textures[!horizontal]);

		//Set textures
		quad->SetTextures({ "Image" });
		//Draw quad
		quad->Draw(texture_manager);

		//Switch horizontal bool
		if (beginning)
		{
			beginning = !beginning;
		}
		horizontal = !horizontal;
	}
	return textures[0];
}

std::shared_ptr<sgl::Texture> Application::MergeDisplayAndGaussianBlur(
	const std::shared_ptr<sgl::Texture>& display,
	const std::shared_ptr<sgl::Texture>& gaussian_blur,
	const float exposure /*= 1.0f*/) const
{
#pragma message ("You have to complete this code!")
	return gaussian_blur;
}

std::shared_ptr<sgl::Mesh> Application::CreatePhysicallyBasedRenderedMesh(
	const std::shared_ptr<sgl::Device>& device,
	const std::shared_ptr<sgl::TextureCubeMap>& texture)
{
	// Create the physically based rendering program.
	pbr_program_ = sgl::CreateProgram("PhysicallyBasedRendering");
	pbr_program_->UniformMatrix("projection", device->GetProjection());
	pbr_program_->UniformMatrix("view", device->GetView());
	pbr_program_->UniformMatrix("model", device->GetModel());

	// Create lights.
	sgl::LightManager light_manager{};
	const float light_value = 300.f;
	const glm::vec3 light_vec(light_value, light_value, light_value);
	light_manager.AddLight(sgl::Light({ 10.f, 10.f, 10.f }, light_vec));
	light_manager.AddLight(sgl::Light({ 10.f, -10.f, 10.f }, light_vec));
	light_manager.AddLight(sgl::Light({ -10.f, 10.f, 10.f }, light_vec));
	light_manager.AddLight(sgl::Light({ -10.f, -10.f, 10.f }, light_vec));
	light_manager.RegisterToProgram(pbr_program_);
	device->SetLightManager(light_manager);

	// Mesh creation.
	auto mesh = std::make_shared<sgl::Mesh>(
		"../Asset/Model/" + draw_model_shape_map_.at(draw_model_) + ".obj",
		pbr_program_);

	// Get the texture manager.
	auto texture_manager = device->GetTextureManager();
	
	// Set the texture to be used in the shader.
	mesh->SetTextures(CreateTextures(texture_manager, texture));
	device->SetTextureManager(texture_manager);

	return mesh;
}

std::shared_ptr<sgl::Mesh> Application::CreateCubeMapMesh(
	const std::shared_ptr<sgl::Device>& device,
	const std::shared_ptr<sgl::TextureCubeMap>& texture) const
{
	// Create the cube map program.
	auto cubemap_program = sgl::CreateProgram("CubeMapHighDynamicRange");
	cubemap_program->UniformMatrix("projection", device->GetProjection());

	// Create the mesh for the cube.
	auto cube_mesh = sgl::CreateCubeMesh(cubemap_program);

	// Get the texture manager.
	auto texture_manager = device->GetTextureManager();
	texture_manager.AddTexture("Skybox", texture);
	cube_mesh->SetTextures({ "Skybox" });
	device->SetTextureManager(texture_manager);

	// Enable the cleaning of the depth.
	cube_mesh->ClearDepthBuffer(true);
	return cube_mesh;
}
