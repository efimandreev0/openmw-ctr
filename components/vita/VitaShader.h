#ifndef OPENMW_COMPONENTS_VITA_SHADER_H
#define OPENMW_COMPONENTS_VITA_SHADER_H

#ifdef __vita__

#include <osg/Material>
#include <osg/Program>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/ref_ptr>

namespace Vita
{
    /// Get the shared VitaLit program (objects, actors, sky, batched statics).
    osg::ref_ptr<osg::Program> createVitaLitProgram();

    /// Get a 1x1 white fallback texture (for untextured geometry).
    osg::ref_ptr<osg::Texture2D> getWhiteFallbackTexture();

    /// Get the shared VitaTerrain program (terrain chunks with blend maps, per-pass).
    osg::ref_ptr<osg::Program> createVitaTerrainProgram();

    /// Get the shared VitaTerrainMulti program (up to 4 layers in single pass).
    osg::ref_ptr<osg::Program> createVitaTerrainMultiProgram();

    /// Apply VitaLit shader + material uniforms to a node.
    /// Called from ShaderVisitor for each geometry/drawable node.
    void applyVitaShader(osg::Node& node, int colorMode, float alphaRef, const osg::Material* mat);

    /// Add lighting/fog/camera uniforms to the scene root StateSet.
    /// These are updated per-frame by RenderingManager.
    void setupSceneUniforms(osg::StateSet* sceneRootSS);

    /// Update per-frame uniforms on the scene root StateSet.
    /// sunDirView is the sun direction in view (eye) space.
    void updateSceneUniforms(osg::StateSet* ss, const osg::Vec3f& sunDirView, const osg::Vec3f& sunColor,
        const osg::Vec3f& ambient, float fogStart, float fogEnd, const osg::Vec4f& fogColor);
}

#elif defined(__3DS__)

// 3DS: NovaGL is a fixed-function GLES1.1 layer — there are no runtime GLSL
// programs to install. Rendering goes through the stock OSG FFP path
// (lighting via LightManager FFP method, terrain via multi-pass TexEnvCombine).
// These inline fallbacks keep the shared Vita call sites compiling; they
// intentionally do nothing except the white fallback texture, which is real.

#include <osg/Image>
#include <osg/Material>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>

namespace osg
{
    class Node;
}

namespace Vita
{
    inline osg::ref_ptr<osg::Texture2D> getWhiteFallbackTexture()
    {
        static osg::ref_ptr<osg::Texture2D> texture = []() {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            unsigned char* data = image->data();
            data[0] = data[1] = data[2] = data[3] = 255;
            osg::ref_ptr<osg::Texture2D> tex = new osg::Texture2D(image);
            tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
            tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
            return tex;
        }();
        return texture;
    }

    inline void applyVitaShader(osg::Node&, int, float, const osg::Material*) {}

    inline void setupSceneUniforms(osg::StateSet*) {}

    inline void updateSceneUniforms(osg::StateSet*, const osg::Vec3f&, const osg::Vec3f&, const osg::Vec3f&, float,
        float, const osg::Vec4f&)
    {
    }
}

#endif // __vita__ / __3DS__
#endif // OPENMW_COMPONENTS_VITA_SHADER_H
