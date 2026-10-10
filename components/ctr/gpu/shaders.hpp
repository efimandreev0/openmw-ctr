#ifndef OPENMW_COMPONENTS_CTR_GPU_SHADERS_H
#define OPENMW_COMPONENTS_CTR_GPU_SHADERS_H

#include <cstddef>

#include <citro3d.h>

extern "C"
{
    extern const unsigned char ctr_shader_ui[];
    extern const std::size_t ctr_shader_ui_size;
    extern const unsigned char ctr_shader_world[];
    extern const std::size_t ctr_shader_world_size;
    extern const unsigned char ctr_shader_skin[];
    extern const std::size_t ctr_shader_skin_size;
}

namespace CtrGpu
{
    struct ShaderProgram
    {
        DVLB_s* mDvlb = nullptr;
        shaderProgram_s mProgram{};
        bool mValid = false;

        bool load(const unsigned char* data, std::size_t size);
        void unload();
        int uniform(const char* name) const;
    };
}

#endif
