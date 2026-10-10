#include "actoranimation.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include "../../mwbase/environment.hpp"

#include "../../mwmechanics/actorutil.hpp"
#include "../../mwmechanics/creaturestats.hpp"
#include "../../mwmechanics/drawstate.hpp"
#include "../../mwmechanics/weapontype.hpp"

#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../../mwworld/inventorystore.hpp"

namespace MWRender
{
    ActorAnimation::ActorAnimation(const MWWorld::Ptr& ptr, SceneUtil::PositionAttitudeTransform* parentNode,
        Resource::ResourceSystem* resourceSystem)
        : Animation(ptr, parentNode, resourceSystem)
    {
        MWWorld::ContainerStore& store = mPtr.getClass().getContainerStore(mPtr);

        for (MWWorld::ConstContainerStoreIterator iter = store.cbegin(MWWorld::ContainerStore::Type_Light);
             iter != store.cend(); ++iter)
        {
            const ESM::Light* light = iter->get<ESM::Light>()->mBase;
            if (!(light->mData.mFlags & ESM::Light::Carry))
                addHiddenItemLight(*iter, light);
        }

        removeEffects();
    }

    ActorAnimation::~ActorAnimation() = default;

    PartHolderPtr ActorAnimation::attachMesh(VFS::Path::NormalizedView model, std::string_view bonename)
    {
        if (!hasBone(bonename))
            return nullptr;
        const int id = addPart(model, bonename, bonename);
        if (id < 0)
            return nullptr;
        return std::make_unique<PartHolder>(this, id, model);
    }

    PartHolderPtr ActorAnimation::attach(
        VFS::Path::NormalizedView model, std::string_view bonename, std::string_view bonefilter, bool isLight)
    {
        int id;
        if (isLight)
        {
            const osg::Quat rotation(osg::DegreesToRadians(-90.f), osg::Vec3f(1, 0, 0));
            id = addPart(model, bonename, bonefilter, &rotation);
        }
        else
            id = addPart(model, bonename, bonefilter);
        if (id < 0)
            throw std::runtime_error("Can't attach " + std::string(model.value()));
        return std::make_unique<PartHolder>(this, id, model);
    }

    VFS::Path::Normalized ActorAnimation::getShieldMesh(const MWWorld::ConstPtr& shield, bool female) const
    {
        const ESM::Armor* armor = shield.get<ESM::Armor>()->mBase;
        const std::vector<ESM::PartReference>& bodyparts = armor->mParts.mParts;
        if (!bodyparts.empty())
        {
            const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
            const MWWorld::Store<ESM::BodyPart>& partStore = store.get<ESM::BodyPart>();
            for (const auto& part : bodyparts)
            {
                if (part.mPart != ESM::PRT_Shield)
                    continue;

                const ESM::RefId* bodypartName = nullptr;
                if (female && !part.mFemale.empty())
                    bodypartName = &part.mFemale;
                else if (!part.mMale.empty())
                    bodypartName = &part.mMale;

                if (bodypartName && !bodypartName->empty())
                {
                    const ESM::BodyPart* bodypart = partStore.search(*bodypartName);
                    if (bodypart == nullptr || bodypart->mData.mType != ESM::BodyPart::MT_Armor)
                        return VFS::Path::Normalized();
                    if (!bodypart->mModel.getNormalized().empty())
                        return Misc::ResourceHelpers::correctMeshPath(bodypart->mModel.getNormalized());
                }
            }
        }

        return shield.getClass().getCorrectedModel(shield);
    }

    std::string ActorAnimation::getSheathedShieldMesh(const MWWorld::ConstPtr& /*shield*/) const
    {
        return std::string();
    }

    bool ActorAnimation::updateCarriedLeftVisible(ESM::RefId weaptype) const
    {
        return !(MWMechanics::getWeaponType(weaptype)->mFlags & ESM::WeaponType::TwoHanded);
    }

    bool ActorAnimation::useShieldAnimations() const
    {
        return false;
    }

    std::string_view ActorAnimation::getHolsteredWeaponBoneName(const MWWorld::ConstPtr& weapon)
    {
        if (weapon.isEmpty())
            return {};

        auto type = weapon.getClass().getType();
        if (type == ESM::Weapon::sRecordId)
        {
            const MWWorld::LiveCellRef<ESM::Weapon>* ref = weapon.get<ESM::Weapon>();
            const ESM::RefId weaponType = ref->mBase->mData.mType;
            return MWMechanics::getWeaponType(weaponType)->mSheathingBone;
        }

        return {};
    }

    void ActorAnimation::itemAdded(const MWWorld::ConstPtr& item, int /*count*/)
    {
        if (item.getType() == ESM::Light::sRecordId)
        {
            const ESM::Light* light = item.get<ESM::Light>()->mBase;
            if (!(light->mData.mFlags & ESM::Light::Carry))
                addHiddenItemLight(item, light);
        }
    }

    void ActorAnimation::itemRemoved(const MWWorld::ConstPtr& item, int count)
    {
        if (item.getType() == ESM::Light::sRecordId)
        {
            ItemLightMap::iterator iter = mItemLights.find(item);
            if (iter != mItemLights.end())
            {
                if (!item.getCellRef().getCount())
                    removeHiddenItemLight(item);
            }
        }
    }

    void ActorAnimation::addHiddenItemLight(const MWWorld::ConstPtr& item, const ESM::Light* esmLight)
    {
        if (mItemLights.find(item) != mItemLights.end())
            return;
        const SceneUtil::LightCommon common(*esmLight);
        if (common.mNegative || common.mRadius <= 0.f)
            return;
        CtrScene::PointLight light;
        light.mColor = osg::Vec3f(common.mColor.r(), common.mColor.g(), common.mColor.b());
        light.mRadius = common.mRadius;
        mItemLights.emplace(item, light);
    }

    void ActorAnimation::removeHiddenItemLight(const MWWorld::ConstPtr& item)
    {
        mItemLights.erase(item);
    }

    void ActorAnimation::collectLights(std::vector<const CtrScene::PointLight*>& out, float time)
    {
        Animation::collectLights(out, time);
        if (mItemLights.empty() || !isRenderable() || mPtr == MWMechanics::getPlayer())
            return;
        const osg::Vec3f position = mInsert->getPosition() + osg::Vec3f(0.f, 0.f, 64.f);
        for (auto& [item, light] : mItemLights)
        {
            light.mPosition = position;
            out.push_back(&light);
        }
    }
}
