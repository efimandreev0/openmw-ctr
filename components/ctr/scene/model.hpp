#ifndef OPENMW_COMPONENTS_CTR_SCENE_MODEL_H
#define OPENMW_COMPONENTS_CTR_SCENE_MODEL_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Matrixf>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/sceneutil/textkeymap.hpp>
#include <components/vfs/pathutil.hpp>

namespace Resource
{
    class NifFileManager;
}

namespace CtrScene
{
    class TextureCache;

    constexpr int sMaxBonesPerDraw = 20;

    enum MaterialFlags : std::uint16_t
    {
        Mat_AlphaBlend = 1 << 0,
        Mat_AlphaTest = 1 << 1,
        Mat_TwoSided = 1 << 2,
        Mat_CullFront = 1 << 3,
        Mat_NoDepthWrite = 1 << 4,
        Mat_NoDepthTest = 1 << 5,
        Mat_VertexAmbDiff = 1 << 6,
        Mat_VertexEmissive = 1 << 7,
        Mat_Unlit = 1 << 8,
    };

    struct Material
    {
        CtrGpu::TexturePtr mTexture;
        osg::Vec4f mDiffuse{ 1.f, 1.f, 1.f, 1.f }; ///< alpha in w
        osg::Vec3f mAmbient{ 1.f, 1.f, 1.f };
        osg::Vec3f mEmissive{ 0.f, 0.f, 0.f };
        std::uint16_t mFlags = 0;
        std::uint8_t mSrcBlend = 6;
        std::uint8_t mDstBlend = 7;
        std::uint8_t mAlphaFunc = 4;
        std::uint8_t mAlphaRef = 0;
    };

    enum VertexFormatFlags : std::uint8_t
    {
        VF_Color = 1 << 0,
        VF_Skin = 1 << 1,
    };

    inline std::uint16_t vertexStride(std::uint8_t format)
    {
        std::uint16_t stride = 8 + 4 + 4;
        if (format & VF_Color)
            stride += 4;
        if (format & VF_Skin)
            stride += 8;
        return stride;
    }

    struct GpuGeometry
    {
        void* mVertices = nullptr;
        std::uint16_t* mIndices = nullptr;
        std::uint32_t mVertexCount = 0;
        std::uint32_t mIndexCount = 0;

        GpuGeometry() = default;
        GpuGeometry(GpuGeometry&& other) noexcept;
        GpuGeometry& operator=(GpuGeometry&& other) noexcept;
        GpuGeometry(const GpuGeometry&) = delete;
        GpuGeometry& operator=(const GpuGeometry&) = delete;
        ~GpuGeometry();

        std::size_t byteSize(std::uint16_t stride) const
        {
            return static_cast<std::size_t>(mVertexCount) * stride + mIndexCount * sizeof(std::uint16_t);
        }
    };

    struct SkinPart
    {
        GpuGeometry mGeometry;
        std::vector<std::uint8_t> mBones;
    };

    enum MeshFlags : std::uint8_t
    {
        Mesh_TriBip = 1 << 0, ///< "tri bip*" helper geometry, removed from creatures
        Mesh_Billboard = 1 << 1, ///< under a NiBillboardNode
    };

    struct Mesh
    {
        std::string mName;
        std::int16_t mNode = -1; ///< node the (rigid) geometry hangs from
        std::int16_t mSwitch = -1; ///< switch whose child mSwitchChild must be active for the mesh to show
        std::int8_t mSwitchChild = -1;
        std::uint8_t mFlags = 0;
        std::uint8_t mVertexFormat = 0;
        Material mMaterial;

        osg::Vec3f mPosScale;
        osg::Vec3f mPosOffset;
        float mUvScale = 1.f;

        GpuGeometry mGeometry;
        osg::BoundingSpheref mBound;

        std::vector<std::string> mBoneNames;
        std::vector<osg::Matrixf> mBoneBind;
        std::vector<SkinPart> mSkinParts;

        bool isSkinned() const { return !mBoneNames.empty(); }
    };

    enum NodeFlags : std::uint16_t
    {
        Node_Hidden = 1 << 0, ///< hidden or collision-only
        Node_Billboard = 1 << 1,
        Node_Switch = 1 << 2,
    };

    struct Node
    {
        std::string mName;
        osg::Matrixf mLocal;
        std::int16_t mParent = -1;
        std::uint16_t mFlags = 0;
        std::int32_t mRecordIndex = -1;
    };

    struct Switch
    {
        std::string mName;
        std::int16_t mNode = -1;
        std::uint8_t mInitialChild = 0;
        std::uint8_t mNumChildren = 0;
    };

    struct ParticleColorKey
    {
        float mTime;
        osg::Vec4f mColor;
    };

    struct ParticleGravity
    {
        float mForce = 0.f;
        float mDecay = 0.f;
        bool mPoint = false; ///< point force (towards mPosition), else wind (along mDirection)
        osg::Vec3f mPosition;
        osg::Vec3f mDirection;
    };

    struct ParticleSystem
    {
        std::int16_t mNode = -1; ///< node of the NiParticles; particle space when mWorldSpace is false
        std::int16_t mEmitterNode = -1;
        std::int32_t mEmitterRecord = -1;
        bool mWorldSpace = true;
        bool mAutoPlay = false;
        std::uint16_t mMaxParticles = 0;
        float mBirthRate = 0.f;
        float mSpeedMin = 0.f;
        float mSpeedMax = 0.f;
        float mPlanarAngle = 0.f;
        float mPlanarAngleVariation = 0.f;
        float mDeclination = 0.f;
        float mDeclinationVariation = 0.f;
        float mLifetime = 0.f;
        float mLifetimeVariation = 0.f;
        osg::Vec3f mEmitterDimensions;
        float mInitialSize = 1.f;
        osg::Vec4f mInitialColor{ 1.f, 1.f, 1.f, 1.f };
        float mEmitStart = 0.f;
        float mEmitStop = 0.f;

        float mFrequency = 1.f;
        float mPhase = 0.f;
        float mStartTime = 0.f;
        float mStopTime = 0.f;
        std::uint8_t mExtrapolation = 0;

        float mGrowTime = 0.f;
        float mFadeTime = 0.f;
        std::vector<ParticleColorKey> mColorKeys;
        std::vector<ParticleGravity> mGravity;

        Material mMaterial;
        float mBoundRadius = 0.f;

        float controllerTime(float input) const;
    };

    struct Model
    {
        VFS::Path::Normalized mPath;
        std::vector<Node> mNodes;
        std::vector<Mesh> mMeshes;
        std::vector<Switch> mSwitches;
        std::vector<ParticleSystem> mParticleSystems;
        osg::BoundingBoxf mBounds;
        std::size_t mGpuBytes = 0;
        float mMaxControllerTime = 0.f;
        bool mHasSkin = false;
        SceneUtil::TextKeyMap mTextKeys;

        int findNode(std::string_view name) const;
        int findNodeByRecordIndex(int recordIndex) const;
        int findSwitch(std::string_view name) const;

        void computeBindPose(std::vector<osg::Matrixf>& out) const;
    };

    using ModelPtr = std::shared_ptr<const Model>;

    class ModelCache
    {
    public:
        ModelCache(Resource::NifFileManager* nifFileManager, TextureCache* textures);
        ~ModelCache();

        ModelPtr get(VFS::Path::NormalizedView path);

        ModelPtr find(VFS::Path::NormalizedView path);

        void purgeUnused();
        void clear();

        std::size_t size() const { return mModels.size(); }
        std::size_t getGpuBytes() const;

        TextureCache* getTextureCache() const { return mTextures; }

    private:
        Resource::NifFileManager* mNifFileManager;
        TextureCache* mTextures;
        std::unordered_map<std::string, ModelPtr> mModels;
    };
}

#endif
