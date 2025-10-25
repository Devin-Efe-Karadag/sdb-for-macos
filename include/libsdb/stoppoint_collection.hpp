#ifndef SDB_STOPPOINT_COLLECTION_HPP
#define SDB_STOPPOINT_COLLECTION_HPP

#include <vector>
#include <memory>
#include <algorithm>
#include <libsdb/types.hpp>
#include <libsdb/error.hpp>
#include <type_traits>

namespace sdb {
    template <class Stoppoint, bool Owning = true>

    class stoppoint_collection {
    public:
        using pointer_type = std::conditional_t<Owning,
            std::unique_ptr<Stoppoint>,
            Stoppoint*>;
        Stoppoint& push(pointer_type bs);

        bool contains_id(typename Stoppoint::id_type id) const;

        bool contains_address(virt_addr address) const;

        bool enabled_stoppoint_at_address(virt_addr address) const;

        Stoppoint& get_by_id(typename Stoppoint::id_type id);

        const Stoppoint& get_by_id(typename Stoppoint::id_type id) const;
        Stoppoint& get_by_address(virt_addr address);

        const Stoppoint& get_by_address(virt_addr address) const;

        std::vector<Stoppoint*> get_in_region(
            virt_addr low, virt_addr high) const;

        void remove_by_id(typename Stoppoint::id_type id);
        void remove_by_address(virt_addr address);

        template <class F>
        void for_each(F f);
        template <class F>
        void for_each(F f) const;

        std::size_t size() const { return stoppoints_.size(); }

        bool empty() const { return stoppoints_.empty(); }

    private:
        using points_t = std::vector<pointer_type>;

        typename points_t::iterator find_by_id(typename Stoppoint::id_type id);
        typename points_t::const_iterator find_by_id(typename Stoppoint::id_type id) const;
        typename points_t::iterator find_by_address(virt_addr address);
        typename points_t::const_iterator find_by_address(virt_addr address) const;

        points_t stoppoints_;
    };

    template <class Stoppoint, bool Owning>
    Stoppoint& stoppoint_collection<Stoppoint,Owning>::push(
        pointer_type bs) {
        stoppoints_.push_back(std::move(bs));

        return *stoppoints_.back();
    }

    template <class Stoppoint, bool Owning>

    auto stoppoint_collection<Stoppoint,Owning>::find_by_id(typename Stoppoint::id_type id)
        -> typename points_t::iterator {
        return std::find_if(begin(stoppoints_), end(stoppoints_),
            [=](auto& point) { return point->id() == id; });
    }

    template <class Stoppoint, bool Owning>

    auto stoppoint_collection<Stoppoint,Owning>::find_by_id(typename Stoppoint::id_type id) const
        -> typename points_t::const_iterator {
        return const_cast<stoppoint_collection*>(this)->find_by_id(id);
    }

    template <class Stoppoint, bool Owning>

    auto stoppoint_collection<Stoppoint,Owning>::find_by_address(virt_addr address)
        -> typename points_t::iterator
    {
        return std::find_if(begin(stoppoints_), end(stoppoints_),
            [=](auto& point) { return point->at_address(address); });
    }

    template <class Stoppoint, bool Owning>

    auto stoppoint_collection<Stoppoint,Owning>::find_by_address(virt_addr address) const
        -> typename points_t::const_iterator {
        return const_cast<stoppoint_collection*>(this)->find_by_address(address);
    }
