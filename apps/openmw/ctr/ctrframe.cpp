#include "ctrframe.hpp"

namespace CtrFrame
{
    namespace
    {
        Presenter& presenter()
        {
            static Presenter sPresenter;
            return sPresenter;
        }
    }

    void setPresenter(Presenter value)
    {
        presenter() = std::move(value);
    }

    void present(bool drawScene)
    {
        if (presenter())
            presenter()(drawScene);
    }
}
