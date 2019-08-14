/*
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 * Copyright (C) 2013 Fredrik Hultin.
 * Copyright (C) 2013 Jakob Bornecrantz.
 * Distributed under the Boost 1.0 licence, see LICENSE for full text.
 */

/* OpenGL Test - Main Implementation */

#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include "gl.h"
#define MATH_3D_IMPLEMENTATION
#include "math_3d.h"

#define degreesToRadians(angleDegrees) ((angleDegrees) * M_PI / 180.0)
#define radiansToDegrees(angleRadians) ((angleRadians) * 180.0 / M_PI)

#include <openvr/openvr.h>
#include <vector>
#include <iostream>

void check_error(int line, vr::EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, vr::VR_GetVRInitErrorAsSymbol(error)); }

void GLAPIENTRY
gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
				  GLsizei length, const GLchar* message, const void* userParam)
{
	fprintf(stderr, "GL DEBUG CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
			(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
			type, severity, message);
}

void draw_floor(GLuint shader, GLuint floor_buffer)
{
	int modelLoc = glGetUniformLocation(shader, "model");
	int colorLoc = glGetUniformLocation(shader, "uniformColor");
  /*
	
	for(int i = 0; i < 18; i ++) {
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*) cube_modelmatrix[i].m);
		glUniform4f(colorLoc, cube_colors[i].x, cube_colors[i].y, cube_colors[i].z, cube_alpha[i]);
		glDrawArrays(GL_TRIANGLES, 0, 36);
	}
  */
	// floor is 10x10m, 0.1m thick
	mat4_t floor = m4_identity();
	floor = m4_mul(floor, m4_scaling(vec3(10, 0.1, 10)));
	// we could move the floor to -1.8m height if the HMD tracker sits at zero
	 floor = m4_mul(floor, m4_translation(vec3(0, 2, 0)));
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*) floor.m);
    glUniform4f(colorLoc, 1., 1., 1.f, .4f);

    int aPosLoc = glGetAttribLocation(shader, "aPos");
    int inNormalLoc = glGetAttribLocation(shader, "in_Normal");

    glDisableVertexAttribArray(inNormalLoc);

    glBindBuffer(GL_ARRAY_BUFFER, floor_buffer);
    glVertexAttribPointer(aPosLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*) 0);

    glDrawArrays(GL_TRIANGLES, 0, 36);
    glEnableVertexAttribArray(inNormalLoc);

}
mat4_t matrix34_to_mat4 (const vr::HmdMatrix34_t *mat34)
{
	return mat4(
		mat34->m[0][0], mat34->m[0][1], mat34->m[0][2], mat34->m[0][3],
		mat34->m[1][0], mat34->m[1][1], mat34->m[1][2], mat34->m[1][3],
		mat34->m[2][0], mat34->m[2][1], mat34->m[2][2], mat34->m[2][3],
		0, 0, 0, 1
	);
}


// clamp pitch to [-89, 89]
float clampPitch(float p)
{
    return p > 89.0f ? 89.0f : (p < -89.0f ? -89.0f : p);   
}

// clamp yaw to [-180, 180] to reduce floating point inaccuracy
float clampYaw(float y)
{
    float temp = (y + 180.0f) / 360.0f;
    return y - ((int)temp - (temp < 0.0f ? 1 : 0)) * 360.0f;
}

struct OnScreenObject {
    bool loadingRenderModel = false, loadingTexture = false;
    vr::RenderModel_t *renderModel = nullptr;
    vr::RenderModel_TextureMap_t* textureMap = nullptr;
    vr::IVRSystem * ctx = nullptr;
    int idx = -1;
    GLuint mTexture = -1;
    GLuint mBuffer = -1;
    GLuint elementbuffer = -1;

    void LoadRenderModel() {
        if(loadingRenderModel)
            return;

        char name[1024] = {};

        vr::ETrackedPropertyError error;
        ctx->GetStringTrackedDeviceProperty(idx, vr::Prop_RenderModelName_String, name, 1024, &error);

        auto rError = vr::VRRenderModels()->LoadRenderModel_Async(name, &renderModel);
        if(rError == vr::VRRenderModelError_None) {
            std::cout << "Loading " << name << std::endl;
            loadingRenderModel = true;
        }
    }

    void LoadTexture() {
        if(loadingTexture)
            return;

        auto error = vr::VRRenderModels()->LoadTexture_Async( renderModel->diffuseTextureId, &textureMap );
        if(error == vr::VRRenderModelError_None) {
            loadingTexture = true;
        }
    }

    void Draw(GLuint appshader, const vr::TrackedDevicePose_t& openvr_hmd_pose) {
        if(renderModel == 0) {
            LoadRenderModel();
            return;
        }
        if(textureMap == 0 && renderModel) {
            LoadTexture();
            return;
        } else if(mBuffer == (GLuint)-1 && renderModel) {
            glGenBuffers(1, &elementbuffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, renderModel->unTriangleCount * 3 * sizeof(renderModel->rIndexData[0]),
                    renderModel->rIndexData, GL_STATIC_DRAW);

            glGenBuffers(1, &mBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, mBuffer);
            glBufferData(GL_ARRAY_BUFFER, renderModel->unVertexCount * sizeof(renderModel->rVertexData[0]),
                    renderModel->rVertexData, GL_DYNAMIC_DRAW);

            // Texture loading
            glGenTextures(1, &mTexture);
            glBindTexture( GL_TEXTURE_2D, mTexture);

            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, textureMap->unWidth, textureMap->unHeight,
                          0, GL_RGBA, GL_UNSIGNED_BYTE, textureMap->rubTextureMapData );
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );

            GLfloat fLargest;
            glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest );
            glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest );

            glBindTexture( GL_TEXTURE_2D, 0 );
        }


        mat4_t hmd_modelmatrix = matrix34_to_mat4(&openvr_hmd_pose.mDeviceToAbsoluteTracking);

        int modelLoc = glGetUniformLocation(appshader, "model");
        int colorLoc = glGetUniformLocation(appshader, "uniformColor");

        mat4_t scaled = m4_mul(hmd_modelmatrix, m4_scaling(vec3(1, 1, 1)));

        vec3_t hmd_color = vec3(1, 1, 1);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*) scaled.m);
        glUniform4f(colorLoc, hmd_color.x, hmd_color.y, hmd_color.z, 1.0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, mBuffer);

        int aNormLoc = glGetAttribLocation(appshader, "in_Normal");
        int aPosLoc = glGetAttribLocation(appshader, "aPos");
        int texLoc  = glGetAttribLocation(appshader, "in_TexCoord");

        int vertex_stride = sizeof(renderModel->rVertexData[0]);
        glEnableVertexAttribArray(texLoc);

        glVertexAttribPointer(aPosLoc, 3, GL_FLOAT, false, vertex_stride, (GLvoid*)offsetof(vr::RenderModel_Vertex_t, vPosition));
        if(aNormLoc != -1)
            glVertexAttribPointer(aNormLoc, 3, GL_FLOAT, false, vertex_stride, (GLvoid*)offsetof(vr::RenderModel_Vertex_t, vNormal));
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, false, vertex_stride, (GLvoid*)offsetof(vr::RenderModel_Vertex_t, rfTextureCoord));

        auto textureLoc = glGetUniformLocation(appshader, "mytexture");
        glUniform1i(textureLoc, 0);

        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, mTexture );

        glDrawElements(GL_TRIANGLES, renderModel->unTriangleCount * 3, GL_UNSIGNED_SHORT, (void*)0);

        //glUniform4f(colorLoc, 1.0, 0, 0, 1.0);
        //glDrawArrays(GL_LINE_STRIP, 0, 36);

    }
};

int main(int argc, char** argv)
{
	int hmd_w = 2560;
	int hmd_h = 1440;

    vec3_t look_at = {};

    vr::EVRInitError error;
    auto ctx = vr::VR_Init(&error, vr::VRApplication_Background);

    assert(ctx);

	check_error(__LINE__, error);

	char fn_table_name[128];
	sprintf (fn_table_name, "FnTable:%s", vr::IVRSystem_Version);
	check_error(__LINE__, error);

	gl_ctx gl;
	GLuint VAOs[2] = {};
	GLuint appshader;
	init_gl(&gl, hmd_w, hmd_h, VAOs, &appshader);

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(gl_debug_callback, nullptr);

	GLuint texture;
	GLuint framebuffer;
	GLuint depthbuffer;
	for (int i = 0; i < 2; i++)
		create_fbo(hmd_w, hmd_h, &framebuffer, &texture, &depthbuffer);

	SDL_ShowCursor(SDL_DISABLE);

    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];

    OnScreenObject objs[vr::k_unMaxTrackedDeviceCount];
    for(size_t i = 0;i < vr::k_unMaxTrackedDeviceCount;i++) {
        objs[i].ctx = ctx;
        objs[i].idx = i;
    }

	    const float sensitivity = 0.001f; 

#define CTR_X (hmd_w / 2)
#define CTR_Y (hmd_h / 2)
#define RESET_MOUSE SDL_WarpMouseInWindow(gl.window, CTR_X, CTR_Y)
	float yaw = 0, pitch = 45;
	
	bool done = false;

	float dist = 3.0;

	while(!done){
		SDL_Event event;
		while(SDL_PollEvent(&event)){
			if(event.type == SDL_KEYDOWN){
				switch(event.key.keysym.sym){
					case SDLK_ESCAPE:
						done = true;
						break;
				    case SDLK_EQUALS:
				        dist -= .1;
				        break;
				    case SDLK_MINUS:
				        dist += .1;
				        break;
					default:
						break;
				}
			}

			if (event.type == SDL_MOUSEMOTION)
			  {
			    float deltaX = (float)event.motion.x - CTR_X;
			    float deltaY = (float)event.motion.y - CTR_Y;

			    yaw = clampYaw(yaw + sensitivity * deltaX);
			    pitch = clampPitch(pitch - sensitivity * deltaY);

			    // reset *every time*
			    RESET_MOUSE;
			  }
		}

		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		glViewport(0, 0, hmd_w, hmd_h);

		mat4_t projectionmatrix = m4_perspective(45, (float)hmd_w / (float)hmd_h, 0.001, 100);

		glUseProgram(appshader);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthbuffer, 0);

		glClearColor(0.0, 0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glBindVertexArray(VAOs[0]);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_SCISSOR_TEST);

		glUniformMatrix4fv(glGetUniformLocation(appshader, "proj"), 1, GL_FALSE, (GLfloat*) projectionmatrix.m);

		vec3_t from = vec3(cos(yaw)* cos(pitch) * dist, sin(pitch) * dist, sin(yaw) * cos(pitch) * dist);
        from = v3_add(from, look_at);

		vec3_t to = look_at; // can't look at 0,0,0
		vec3_t up = vec3(0, 1, 0);
		//up = v3_add(to, up);
		mat4_t viewmatrix = m4_look_at(from, to, up);

		glUniformMatrix4fv(glGetUniformLocation(appshader, "view"), 1, GL_FALSE, (GLfloat*)viewmatrix.m);

        //draw_floor(appshader, floor_buffer);

        ctx->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding , 0, poses, vr::k_unMaxTrackedDeviceCount);

        for(size_t i = 0;i < vr::k_unMaxTrackedDeviceCount;i++) {
            if(!poses[i].bPoseIsValid)
                continue;

            objs[i].Draw(appshader, poses[i]);

            auto clss = ctx->GetTrackedDeviceClass(i);
            if(clss != vr::TrackedDeviceClass_TrackingReference && look_at.x == 0.0 && look_at.y == 0.0 && look_at.y == 0.0) {
                look_at.x = poses[i].mDeviceToAbsoluteTracking.m[0][3];
                look_at.y = poses[i].mDeviceToAbsoluteTracking.m[1][3];
                look_at.z = poses[i].mDeviceToAbsoluteTracking.m[2][3];
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glBlitNamedFramebuffer(
			(GLuint)framebuffer, // readFramebuffer
			(GLuint)0,    // backbuffer     // drawFramebuffer
			(GLint)0,     // srcX0
			(GLint)0,     // srcY0
			(GLint)hmd_w,     // srcX1
			(GLint)hmd_h,     // srcY1
			(GLint)0,     // dstX0
			(GLint)0,     // dstY0
			(GLint)hmd_w, // dstX1
			(GLint)hmd_h, // dstY1
			(GLbitfield)GL_COLOR_BUFFER_BIT, // mask
			(GLenum)GL_LINEAR);              // filter

		// Da swap-dawup!
		SDL_GL_SwapWindow(gl.window);
		SDL_Delay(10);
	}


	return 0;
}
