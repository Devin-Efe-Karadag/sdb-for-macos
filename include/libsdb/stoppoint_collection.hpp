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
