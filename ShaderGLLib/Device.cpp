#include "Device.h"
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include "Frame.h"
#include "Render.h"

namespace sgl {

	Device::Device(
		void* gl_context, 
		const std::pair<std::uint32_t, std::uint32_t> size) : 
		gl_context_(gl_context),
		size_(size)
	{
		// Initialize GLEW.
		if (GLEW_OK != glewInit())
		{
			throw std::runtime_error("couldn't initialize GLEW");
		}

		// Enable blending to 1 - source alpha.
		glEnable(GL_BLEND);
		error_.Display(__FILE__, __LINE__ - 1);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		error_.Display(__FILE__, __LINE__ - 1);
		// Enable Z buffer.
		glEnable(GL_DEPTH_TEST);
		error_.Display(__FILE__, __LINE__ - 1);
		glDepthFunc(GL_LEQUAL);
		error_.Display(__FILE__, __LINE__ - 1);
		// Enable seamless cube map.
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
		error_.Display(__FILE__, __LINE__ - 1);
	}

	void Device::Startup(const float fov /*= 65.0f*/)
	{
		fov_ = fov;
		SetupCamera();
	}

	void Device::Draw(const double dt)
	{
		Display(DrawTexture(dt));
	}

	void Device::Display(const std::shared_ptr<Texture>& texture)
	{
		auto prog = CreateProgram("Display");
		auto quad = CreateQuadMesh(prog);
		
		TextureManager texture_manager{};
		texture_manager.AddTexture(
			"Display",
			texture);

		quad->SetTextures({ "Display" });
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		error_.Display(__FILE__, __LINE__ - 1);
		quad->Draw(texture_manager);
	}

	std::shared_ptr<Texture> Device::DrawTexture(const double dt)
	{
		SetupCamera();

		glViewport(0, 0, size_.first, size_.second);

		glClearColor(.2f, 0.f, .2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		auto texture = std::make_shared<sgl::Texture>(size_);
		Frame frame{};
		Render render{};

		frame.BindTexture(*texture);
		frame.BindAttach(render);
		render.BindStorage(size_);

		texture->Bind();

		for (const std::shared_ptr<sgl::Scene>& scene : scene_tree_)
		{
			const std::shared_ptr<sgl::Mesh>& mesh = scene->GetLocalMesh();
			if (!mesh)
			{
				continue;
			}

			// Draw the mesh.
			mesh->Draw(
				texture_manager_,
				perspective_,
				view_,
				scene->GetLocalModel(dt));
		}

		return texture;
	}

	void Device::SetupCamera()
	{
		// Set the perspective matrix.
		const float aspect =
			static_cast<float>(size_.first) / static_cast<float>(size_.second);
		perspective_ = glm::perspective(
			glm::radians(fov_),
			aspect,
			0.1f,
			100.0f);

		// Set the camera.
		view_ = camera_.GetLookAt();
	}

} // End namespace sgl.
