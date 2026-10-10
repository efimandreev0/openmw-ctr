#include "shaders.hpp"

#include <components/debug/debuglog.hpp>

namespace CtrGpu
{
    bool ShaderProgram::load(const unsigned char* data, std::size_t size)
    {
        mDvlb = DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<unsigned char*>(data)), static_cast<u32>(size));
        if (mDvlb == nullptr)
        {
            Log(Debug::Error) << "Failed to parse a PICA200 shader binary";
            return false;
        }
        shaderProgramInit(&mProgram);
        shaderProgramSetVsh(&mProgram, &mDvlb->DVLE[0]);
        mValid = true;
        return true;
    }

    void ShaderProgram::unload()
    {
        if (!mValid)
            return;
        shaderProgramFree(&mProgram);
        DVLB_Free(mDvlb);
        mDvlb = nullptr;
        mValid = false;
    }

    int ShaderProgram::uniform(const char* name) const
    {
        return mValid ? shaderInstanceGetUniformLocation(mProgram.vertexShader, name) : -1;
    }
}
