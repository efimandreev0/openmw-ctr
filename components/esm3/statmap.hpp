#ifndef OPENMW_COMPONENTS_ESM3_STATMAP_H
#define OPENMW_COMPONENTS_ESM3_STATMAP_H

#include <array>
#include <bitset>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

#include <components/esm/refid.hpp>

namespace ESM
{
    template <class Value, std::size_t N, RefId (*IndexToRefId)(int), int (*RefIdToIndex)(RefId)>
    class StatMap
    {
    public:
        using value_type = std::pair<RefId, Value>;

        class const_iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::pair<RefId, Value>;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = value_type;

            const_iterator(const StatMap* map, std::size_t position)
                : mMap(map)
                , mPosition(position)
            {
                skip();
            }

            value_type operator*() const
            {
                if (mPosition < N)
                    return { IndexToRefId(static_cast<int>(mPosition)), mMap->mValues[mPosition] };
                return mMap->mExtra[mPosition - N];
            }

            const_iterator& operator++()
            {
                ++mPosition;
                skip();
                return *this;
            }

            bool operator==(const const_iterator& other) const { return mPosition == other.mPosition; }
            bool operator!=(const const_iterator& other) const { return mPosition != other.mPosition; }

        private:
            void skip()
            {
                while (mPosition < N && !mMap->mPresent[mPosition])
                    ++mPosition;
            }

            const StatMap* mMap;
            std::size_t mPosition;
        };

        const_iterator begin() const { return const_iterator(this, 0); }
        const_iterator end() const { return const_iterator(this, N + mExtra.size()); }

        bool empty() const { return mPresent.none() && mExtra.empty(); }
        std::size_t size() const { return mPresent.count() + mExtra.size(); }

        void clear()
        {
            mValues = {};
            mPresent.reset();
            mExtra.clear();
        }

        bool emplace(const RefId& id, Value value)
        {
            const int index = RefIdToIndex(id);
            if (index >= 0 && static_cast<std::size_t>(index) < N)
            {
                if (mPresent[index])
                    return false;
                mPresent[index] = true;
                mValues[index] = value;
                return true;
            }
            for (const auto& entry : mExtra)
                if (entry.first == id)
                    return false;
            mExtra.emplace_back(id, value);
            return true;
        }

        Value get(const RefId& id) const
        {
            const int index = RefIdToIndex(id);
            if (index >= 0 && static_cast<std::size_t>(index) < N)
                return mPresent[index] ? mValues[index] : Value{};
            for (const auto& entry : mExtra)
                if (entry.first == id)
                    return entry.second;
            return Value{};
        }

        Value& operator[](const RefId& id)
        {
            const int index = RefIdToIndex(id);
            if (index >= 0 && static_cast<std::size_t>(index) < N)
            {
                mPresent[index] = true;
                return mValues[index];
            }
            for (auto& entry : mExtra)
                if (entry.first == id)
                    return entry.second;
            return mExtra.emplace_back(id, Value{}).second;
        }

        bool operator==(const StatMap& other) const
        {
            return mValues == other.mValues && mPresent == other.mPresent && mExtra == other.mExtra;
        }

    private:
        std::array<Value, N> mValues{};
        std::bitset<N> mPresent;
        std::vector<std::pair<RefId, Value>> mExtra;
    };

    template <class Map>
    auto getStatValue(const Map& map, const RefId& id) -> typename Map::value_type::second_type
    {
        if constexpr (requires { map.get(id); })
            return map.get(id);
        else
        {
            const auto it = map.find(id);
            return it == map.end() ? typename Map::value_type::second_type{} : it->second;
        }
    }
}

#endif
