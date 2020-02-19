#define _USE_MATH_DEFINES
#include <ogl.h>
#include <application.h>
#include <mesh.h>
#include <camera.h>
#include <material.h>
#include <memory>
#include <iostream>
#include <stack>
#include <random>
#include <chrono>
#include <random>

#undef min
#define CAMERA_FAR_PLANE 300.0f
#define DEBUG_CAMERA_FAR_PLANE 10000.0f
#define SHADOW_MAP_SIZE 1024
#define LIGHT_FAR_PLANE 650.0f
#define SHADOW_MAP_EXTENTS 75.0f

struct GlobalUniforms
{
    DW_ALIGNED(16)
    glm::mat4 view_proj;
    DW_ALIGNED(16)
    glm::mat4 light_view_proj;
    DW_ALIGNED(16)
    glm::vec4 cam_pos;
};

class AreaLightShadows : public dw::Application
{
protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    bool init(int argc, const char* argv[]) override
    {
        m_light_target              = glm::vec3(0.0f);
        glm::vec3 default_light_dir = glm::normalize(glm::vec3(-0.5000f, 0.9770f, 0.5000f));
        m_light_direction           = -default_light_dir;
        m_light_color               = glm::vec3(10000.0f);

        // Create GPU resources.
        if (!create_shaders())
            return false;

        // Load scene.
        if (!load_scene())
            return false;

        create_textures();

        if (!create_uniform_buffer())
            return false;

        // Create camera.
        create_camera();

        m_transform = glm::mat4(1.0f);
        m_transform = glm::scale(m_transform, glm::vec3(0.1f));
        m_transform = glm::rotate(m_transform, glm::radians(45.0f), glm::vec3(0.0, 1.0f, 0.0f));

        m_plane_transform = glm::mat4(1.0f);
        m_plane_transform = glm::scale(m_plane_transform, glm::vec3(0.4f));

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update(double delta) override
    {
        // Update camera.
        update_camera();

        update_uniforms();

        render_shadow_map();
        render_lit_scene();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void shutdown() override
    {
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void window_resized(int width, int height) override
    {
        // Override window resized method to update camera projection.
        m_main_camera->update_projection(60.0f, 0.1f, CAMERA_FAR_PLANE, float(m_width) / float(m_height));

        create_textures();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void key_pressed(int code) override
    {
        // Handle forward movement.
        if (code == GLFW_KEY_W)
            m_heading_speed = m_camera_speed;
        else if (code == GLFW_KEY_S)
            m_heading_speed = -m_camera_speed;

        // Handle sideways movement.
        if (code == GLFW_KEY_A)
            m_sideways_speed = -m_camera_speed;
        else if (code == GLFW_KEY_D)
            m_sideways_speed = m_camera_speed;

        if (code == GLFW_KEY_SPACE)
            m_mouse_look = true;

        if (code == GLFW_KEY_G)
            m_debug_gui = !m_debug_gui;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void key_released(int code) override
    {
        // Handle forward movement.
        if (code == GLFW_KEY_W || code == GLFW_KEY_S)
            m_heading_speed = 0.0f;

        // Handle sideways movement.
        if (code == GLFW_KEY_A || code == GLFW_KEY_D)
            m_sideways_speed = 0.0f;

        if (code == GLFW_KEY_SPACE)
            m_mouse_look = false;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void mouse_pressed(int code) override
    {
        // Enable mouse look.
        if (code == GLFW_MOUSE_BUTTON_RIGHT)
            m_mouse_look = true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void mouse_released(int code) override
    {
        // Disable mouse look.
        if (code == GLFW_MOUSE_BUTTON_RIGHT)
            m_mouse_look = false;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    dw::AppSettings intial_app_settings() override
    {
        dw::AppSettings settings;

        settings.resizable    = true;
        settings.maximized    = false;
        settings.refresh_rate = 60;
        settings.major_ver    = 4;
        settings.width        = 1920;
        settings.height       = 1080;
        settings.title        = "Area Light Shadows (c) 2019 Dihara Wijetunga";

        return settings;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_lit_scene()
    {
        render_scene(nullptr, m_mesh_program, 0, 0, m_width, m_height, GL_NONE);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_shadow_map()
    {
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        m_shadow_map_fbo->bind();

        glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        // Bind shader program.
        m_shadow_map_program->use();

        // Bind uniform buffers.
        m_global_ubo->bind_base(0);

        // Draw scene.
        render_mesh(m_mesh, m_transform, m_shadow_map_program, glm::vec3(0.5f));
        render_mesh(m_plane, m_plane_transform, m_shadow_map_program, glm::vec3(0.5f));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_shaders()
    {
        {
            // Create general shaders
            m_mesh_vs       = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_VERTEX_SHADER, "shader/mesh_vs.glsl"));
            m_shadow_map_vs = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_VERTEX_SHADER, "shader/shadow_map_vs.glsl"));
            m_mesh_fs       = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/mesh_fs.glsl"));
            m_depth_fs      = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/depth_fs.glsl"));

            {
                if (!m_shadow_map_vs || !m_depth_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_shadow_map_vs.get(), m_depth_fs.get() };
                m_shadow_map_program      = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_shadow_map_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }

                m_shadow_map_program->uniform_block_binding("GlobalUniforms", 0);
            }

            {
                if (!m_mesh_vs || !m_mesh_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_mesh_vs.get(), m_mesh_fs.get() };
                m_mesh_program            = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_mesh_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }

                m_mesh_program->uniform_block_binding("GlobalUniforms", 0);
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_textures()
    {
        m_shadow_map = std::make_unique<dw::gl::Texture2D>(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1, 1, 1, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
        m_shadow_map->set_wrapping(GL_CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER);
        m_shadow_map->set_border_color(1.0f, 1.0f, 1.0f, 1.0f);

        m_shadow_map_fbo = std::make_unique<dw::gl::Framebuffer>();
        m_shadow_map_fbo->attach_depth_stencil_target(m_shadow_map.get(), 0, 0);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_uniform_buffer()
    {
        // Create uniform buffer for global data
        m_global_ubo = std::make_unique<dw::gl::UniformBuffer>(GL_DYNAMIC_DRAW, sizeof(GlobalUniforms));

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool load_scene()
    {
        m_mesh  = dw::Mesh::load("mesh/lucy.obj");
        m_plane = dw::Mesh::load("mesh/plane.obj");

        if (!m_mesh || !m_plane)
        {
            DW_LOG_FATAL("Failed to load mesh!");
            return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_camera()
    {
        m_main_camera = std::make_unique<dw::Camera>(60.0f, 0.1f, CAMERA_FAR_PLANE, float(m_width) / float(m_height), glm::vec3(50.0f, 20.0f, 0.0f), glm::vec3(-1.0f, 0.0, 0.0f));
        m_main_camera->set_rotatation_delta(glm::vec3(0.0f, -90.0f, 0.0f));
        m_main_camera->update();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_mesh(dw::Mesh::Ptr mesh, glm::mat4 model, std::unique_ptr<dw::gl::Program>& program, glm::vec3 color)
    {
        program->set_uniform("u_Model", model);

        // Bind vertex array.
        mesh->mesh_vertex_array()->bind();

        for (uint32_t i = 0; i < mesh->sub_mesh_count(); i++)
        {
            dw::SubMesh& submesh = mesh->sub_meshes()[i];

            program->set_uniform("u_Color", color);
            program->set_uniform("u_Direction", m_light_direction);
            program->set_uniform("u_LightColor", m_light_color);

            // Issue draw call.
            glDrawElementsBaseVertex(GL_TRIANGLES, submesh.index_count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned int) * submesh.base_index), submesh.base_vertex);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_scene(dw::gl::Framebuffer* fbo, std::unique_ptr<dw::gl::Program>& program, int x, int y, int w, int h, GLenum cull_face, bool clear = true)
    {
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        if (cull_face == GL_NONE)
            glDisable(GL_CULL_FACE);
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cull_face);
        }

        if (fbo)
            fbo->bind();
        else
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glViewport(x, y, w, h);

        if (clear)
        {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        // Bind shader program.
        program->use();

        program->set_uniform("u_LightBias", m_shadow_bias);

        if (program->set_uniform("s_ShadowMap", 1))
            m_shadow_map->bind(1);

        // Bind uniform buffers.
        m_global_ubo->bind_base(0);

        // Draw scene.
        render_mesh(m_mesh, m_transform, program, glm::vec3(0.5f));
        render_mesh(m_plane, m_plane_transform, program, glm::vec3(0.5f));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_uniforms()
    {
        glm::vec3 light_camera_pos = m_light_target - m_light_direction * 200.0f;
        glm::mat4 view             = glm::lookAt(light_camera_pos, m_light_target, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj             = glm::ortho(-SHADOW_MAP_EXTENTS, SHADOW_MAP_EXTENTS, -SHADOW_MAP_EXTENTS, SHADOW_MAP_EXTENTS, 1.0f, LIGHT_FAR_PLANE);

        m_global_uniforms.light_view_proj = proj * view;

        void* ptr = m_global_ubo->map(GL_WRITE_ONLY);
        memcpy(ptr, &m_global_uniforms, sizeof(GlobalUniforms));
        m_global_ubo->unmap();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_transforms(dw::Camera* camera)
    {
        // Update camera matrices.
        m_global_uniforms.view_proj = camera->m_projection * camera->m_view;
        m_global_uniforms.cam_pos   = glm::vec4(camera->m_position, 0.0f);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_camera()
    {
        dw::Camera* current = m_main_camera.get();

        float forward_delta = m_heading_speed * m_delta;
        float right_delta   = m_sideways_speed * m_delta;

        current->set_translation_delta(current->m_forward, forward_delta);
        current->set_translation_delta(current->m_right, right_delta);

        m_camera_x = m_mouse_delta_x * m_camera_sensitivity;
        m_camera_y = m_mouse_delta_y * m_camera_sensitivity;

        if (m_mouse_look)
        {
            // Activate Mouse Look
            current->set_rotatation_delta(glm::vec3((float)(m_camera_y),
                                                    (float)(m_camera_x),
                                                    (float)(0.0f)));
        }
        else
        {
            current->set_rotatation_delta(glm::vec3((float)(0),
                                                    (float)(0),
                                                    (float)(0)));
        }

        current->update();
        update_transforms(current);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // General GPU resources.
    std::unique_ptr<dw::gl::Shader> m_mesh_fs;
    std::unique_ptr<dw::gl::Shader> m_depth_fs;

    std::unique_ptr<dw::gl::Shader> m_mesh_vs;
    std::unique_ptr<dw::gl::Shader> m_shadow_map_vs;

    std::unique_ptr<dw::gl::Program> m_mesh_program;
    std::unique_ptr<dw::gl::Program> m_shadow_map_program;

    std::unique_ptr<dw::gl::Framebuffer> m_shadow_map_fbo;
    std::unique_ptr<dw::gl::Texture2D>   m_shadow_map;

    std::unique_ptr<dw::gl::UniformBuffer> m_global_ubo;

    dw::Mesh::Ptr               m_mesh;
    dw::Mesh::Ptr               m_plane;
    std::unique_ptr<dw::Camera> m_main_camera;

    GlobalUniforms m_global_uniforms;

    // Scene
    glm::mat4 m_transform;
    glm::mat4 m_plane_transform;

    // Camera controls.
    bool  m_mouse_look         = false;
    float m_heading_speed      = 0.0f;
    float m_sideways_speed     = 0.0f;
    float m_camera_sensitivity = 0.05f;
    float m_camera_speed       = 0.05f;
    float m_offset             = 0.1f;
    bool  m_debug_gui          = true;

    glm::vec3 m_light_target;
    glm::vec3 m_light_direction;
    glm::vec3 m_light_color;

    // Shadow Mapping.
    float     m_shadow_bias = 0.001f;
    glm::mat4 m_light_view_proj;

    // Camera orientation.
    float m_camera_x;
    float m_camera_y;
};

DW_DECLARE_MAIN(AreaLightShadows)