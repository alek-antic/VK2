#version 450
#pragma shader_stage(fragment)

layout (location = 0) out vec4 outFragColor;

void main() {
	// red for our triangle
	outFragColor = vec4(1.f, 0.f, 0.f, 1.0f);
}