#pragma once

#include "rhi/vulkan/vulkan_common.h"

namespace Renderer {

	struct LightData {
		alignas(16) glm::vec3 position;
		float range;
		alignas(16) glm::vec3 direction;
		float intensity;
		alignas(16) glm::vec3 color;
		int type;
		alignas(16) glm::vec2 coneAngles;
		float padding1;
		float padding2;
	};

	struct SceneUBO {
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 projection;
		alignas(16) glm::vec3 cameraPos;
		float time;
		alignas(16) LightData lights[8];
		int lightCount;
		alignas(16) glm::vec3 ambientColor;
		float padding;
	};
}
