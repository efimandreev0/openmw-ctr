#ifndef OPENMW_APPS_OPENMW_CTR_CTRFRAME_H
#define OPENMW_APPS_OPENMW_CTR_CTRFRAME_H

#include <functional>

namespace CtrFrame
{
    using Presenter = std::function<void(bool drawScene)>;

    void setPresenter(Presenter presenter);

    void present(bool drawScene);
}

#endif
