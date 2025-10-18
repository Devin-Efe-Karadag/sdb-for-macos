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
