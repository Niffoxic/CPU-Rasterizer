#ifndef ECSTEMPLATE_CORE_H
#define ECSTEMPLATE_CORE_H

#include <cstdint>
#include <limits>
#include <vector>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <cassert>
#include <algorithm>
#include <utility>
#include <cstddef>
#include <new>
#include <span>
#include <functional>
#include <type_traits>
#include <cstring>

namespace fecs
{
    //~ Types
    using fecs_id      = std::uint32_t;
    using storage_id   = std::uint64_t;
    using component_id = std::uint32_t;
    using table_id     = std::uint32_t;

    static constexpr fecs_id    INVALID_ID     = std::numeric_limits<std::uint32_t>::max();
    static constexpr table_id   INVALID_TABLE  = std::numeric_limits<std::uint32_t>::max();
    static constexpr storage_id INVALID_ENTITY = std::numeric_limits<storage_id>::max();

    [[nodiscard]] static constexpr std::uint32_t next_pow2_u32(std::uint32_t v) noexcept
    {
        if (v <= 1u) return 1u;
        --v;
        v |= v >> 1u;
        v |= v >> 2u;
        v |= v >> 4u;
        v |= v >> 8u;
        v |= v >> 16u;
        return v + 1u;
    }

#pragma region ENTITY_CORE

    struct entity
    {
        storage_id value{ INVALID_ENTITY };
        constexpr entity() noexcept = default;
        constexpr explicit entity(const storage_id& value_) noexcept : value(value_) {}

        static constexpr entity make(const fecs_id index, const fecs_id generation) noexcept
        {
            return entity((static_cast<storage_id>(index) << 32u) | static_cast<storage_id>(generation));
        }

        [[nodiscard]] constexpr fecs_id index     () const noexcept { return static_cast<fecs_id>(value >> 32); }
        [[nodiscard]] constexpr fecs_id generation() const noexcept { return static_cast<fecs_id>(value);       }
        [[nodiscard]] constexpr bool    valid     () const noexcept { return value != INVALID_ENTITY;           }

        friend constexpr bool operator==(const entity a, const entity b) noexcept { return a.value == b.value; }
        friend constexpr bool operator!=(const entity a, const entity b) noexcept { return a.value != b.value; }
    };

    class entity_manager
    {
    public:
        entity_manager() noexcept = default;

        [[nodiscard]] entity create_entity()
        {
            fecs_id index{ INVALID_ID };

            if (not fragment_.empty()) [[unlikely]]
            {
                index = fragment_.back();
                fragment_.pop_back();
            }
            else [[likely]]
            {
                index = static_cast<fecs_id>(generations_.size());
                generations_.emplace_back(0u);
            }

            const auto gen = generations_[index];
            ++alive_count_;
            return entity::make(index, gen);
        }

        void destroy_entity(const entity e) noexcept
        {
            if (not alive(e)) return;

            const auto index = e.index();
            ++generations_[index];
            fragment_.emplace_back(index);
            --alive_count_;
        }

        [[nodiscard]] bool alive(const entity e) const noexcept
        {
            if (not e.valid()) return false;
            const auto index = e.index();
            return index < generations_.size() && generations_[index] == e.generation();
        }

        [[nodiscard]] std::uint32_t alive_count() const noexcept { return alive_count_; }
        [[nodiscard]] std::size_t capacity()    const noexcept { return generations_.size(); }

    private:
        std::vector<fecs_id> generations_{};
        std::vector<fecs_id> fragment_{};
        std::uint32_t alive_count_{ 0u };
    };

#pragma endregion

#pragma region COMPONENT_CORE

    struct component_info
    {
        component_id    id          { 0u };
        std::type_index type        { typeid(void) };
        std::uint32_t   size        { 0u };
        std::uint32_t   alignment   { 0u };

        void (*default_ctor)(void* destination)               = nullptr;
        void (*dtor)        (void* obj)                       = nullptr;
        void (*move_ctor)   (void* destination, void* source) = nullptr;
    };

    class component_registry
    {
    public:
        component_registry() noexcept = default;

        template<typename T>
        component_id add() noexcept
        {
            const std::type_index type_id{ typeid(T) };

            if (const auto it = type_ids_.find(type_id); it != type_ids_.end())
                return it->second;

            const auto id = static_cast<component_id>(type_infos_.size());
            type_ids_.emplace(type_id, id);

            component_info info{};
            info.id        = id;
            info.type      = type_id;
            info.size      = static_cast<std::uint32_t>(sizeof(T));
            info.alignment = static_cast<std::uint32_t>(alignof(T));

            info.default_ctor = +[](void* destination) { ::new (destination) T(); };
            info.dtor         = +[](void* obj)         { static_cast<T*>(obj)->~T(); };
            info.move_ctor    = +[](void* destination, void* source)
            {
                ::new (destination) T(std::move(*static_cast<T*>(source)));
            };

            type_infos_.emplace_back(info);
            return id;
        }

        template<typename ComponentType>
        [[nodiscard]] component_id get_id() const noexcept
        {
            const std::type_index type_id{ typeid(ComponentType) };
            const auto it = type_ids_.find(type_id);
            assert(it != type_ids_.end() && "Component type is not registered. (call register_component<T>() first)");
            return it->second;
        }

        [[nodiscard]] component_id get_id(const std::type_index type_id) const noexcept
        {
            const auto it = type_ids_.find(type_id);
            assert(it != type_ids_.end() && "Component type is not registered.");
            return it->second;
        }

        [[nodiscard]] const component_info& get_info(const component_id id) const noexcept
        {
            assert(id < type_infos_.size() && "Component Id invalid or not registered.");
            return type_infos_.at(id);
        }

        std::size_t count() const noexcept { return type_infos_.size(); }

    private:
        std::unordered_map<std::type_index, component_id> type_ids_{};
        std::vector<component_info>                       type_infos_{};
    };

#pragma endregion

#pragma region ARCHETYPE_SIGNATURE

    struct archetype_key
    {
        std::vector<component_id> component_ids;

        archetype_key() noexcept = default;

        explicit archetype_key(std::vector<component_id> ids) noexcept
            : component_ids(std::move(ids))
        {
            canonicalise();
        }

        archetype_key(const std::initializer_list<component_id> init) noexcept
            : component_ids(init)
        {
            canonicalise();
        }

        void canonicalise()
        {
            std::ranges::sort(component_ids);
            component_ids.erase(std::ranges::unique(component_ids).begin(), component_ids.end());
        }

        [[nodiscard]] bool contains(const component_id id) const noexcept
        {
            return std::ranges::binary_search(component_ids, id);
        }

        friend bool operator==(const archetype_key& left, const archetype_key& right) noexcept
        {
            return left.component_ids == right.component_ids;
        }

        friend bool operator!=(const archetype_key& left, const archetype_key& right) noexcept
        {
            return left.component_ids != right.component_ids;
        }
    };

    struct archetype_hash
    {
        std::size_t operator()(const archetype_key& key) const noexcept
        {
            std::uint64_t hash{ 1469598103934665603ull };
            for (const component_id id : key.component_ids)
            {
                hash ^= static_cast<std::uint64_t>(id) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
                hash *= 1099511628211ull;
            }
            hash ^= static_cast<std::uint64_t>(key.component_ids.size()) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            return static_cast<std::size_t>(hash);
        }
    };

    inline archetype_key make_archetype_key(const std::initializer_list<component_id> init) noexcept { return { init }; }
    inline archetype_key make_archetype_key(std::vector<component_id> ids)                  noexcept { return archetype_key(std::move(ids)); }

#pragma endregion

#pragma region TABLE_STORAGE_SOA

    struct column
    {
        component_info info{};
        std::byte* data{ nullptr };
        std::size_t size{ 0 };
        std::size_t capacity{ 0 };

        column() noexcept = default;
        ~column() noexcept { clear_and_free(); }

        explicit column(const component_info& ci) noexcept : info(ci) {}

        column(const column&) noexcept = delete;
        column& operator=(const column&) noexcept = delete;
        column(column&& other) noexcept { *this = std::move(other); }

        column& operator=(column&& other) noexcept
        {
            if (this == &other) return *this;
            clear_and_free();

            info     = other.info;
            data     = other.data;
            size     = other.size;
            capacity = other.capacity;

            other.data     = nullptr;
            other.size     = 0;
            other.capacity = 0u;
            return *this;
        }

        [[nodiscard]] std::size_t stride() const noexcept { return info.size; }
        [[nodiscard]] std::size_t align () const noexcept { return info.alignment; }

        [[nodiscard]] void* ptr_at(const std::size_t index) noexcept
        {
            assert(index < size && "Index out of bounds");
            return data + index * info.size;
        }

        [[nodiscard]] const void* ptr_at(const std::size_t index) const noexcept
        {
            assert(index < size && "Index out of bounds");
            return data + index * info.size;
        }

        [[nodiscard]] void* ptr_at_unsafe(const std::size_t index) const noexcept
        {
            return data + index * info.size;
        }

        void clear() noexcept
        {
            if (not data)
            {
                size = 0u;
                return;
            }

            assert(info.dtor && "Column Info does not have valid destructor");
            for (std::size_t i = 0; i < size; ++i)
                info.dtor(ptr_at_unsafe(i));

            size = 0;
        }

        void remove_swap_nodestroy(const std::size_t row) noexcept
        {
            assert(row < size && "Index out of bounds");
            if (const std::size_t last_index = size - 1u; row != last_index)
            {
                std::memcpy(ptr_at_unsafe(row), ptr_at_unsafe(last_index), stride());
            }
            --size;
        }

        void reserve(const std::size_t new_capacity) noexcept
        {
            if (new_capacity <= capacity) return;
            grow_to(new_capacity);
        }

        void push_defaults() noexcept
        {
            if (size == capacity)
            {
                const std::size_t new_capacity = capacity == 0u ? 8u : capacity * 2u;
                grow_to(new_capacity);
            }

            void* dst = ptr_at_unsafe(size);
            assert(info.default_ctor && "Component default constructor is null");
            info.default_ctor(dst);
            ++size;
        }

        void remove_swap(const std::size_t row) noexcept
        {
            assert(row < size && "Index out of bounds");

            const std::size_t last_index = size - 1u;
            void* dst = ptr_at_unsafe(row);
            void* src = ptr_at_unsafe(last_index);

            assert(info.dtor && "Column Info does not have valid destructor");
            assert(info.move_ctor && "Column Info does not have valid move constructor");

            info.dtor(dst);
            if (row != last_index) info.move_ctor(dst, src);
            info.dtor(src);

            --size;
        }

    private:
        void clear_and_free() noexcept
        {
            clear();
            capacity = 0u;

            if (not data) return;

            operator delete(data, std::align_val_t{ static_cast<std::size_t>(info.alignment) });
            data = nullptr;
        }

        void grow_to(const std::size_t new_capacity)
        {
            const std::size_t bytes = new_capacity * stride();
            auto* new_data = static_cast<std::byte*>(
                operator new(bytes, std::align_val_t{ static_cast<std::size_t>(info.alignment) })
            );

            if (data)
            {
                assert(info.move_ctor && info.dtor);
                for (std::size_t i = 0; i < size; ++i)
                {
                    void* dst = new_data + i * stride();
                    void* src = data     + i * stride();
                    info.move_ctor(dst, src);
                    info.dtor(src);
                }

                operator delete(data, std::align_val_t{ static_cast<std::size_t>(info.alignment) });
            }

            data     = new_data;
            capacity = new_capacity;
        }
    };

#pragma endregion

#pragma region WORLD_STORAGE

    struct table
    {
        table_id id{ INVALID_TABLE };
        archetype_key key{};

        std::vector<entity> entities{};
        std::vector<column> columns{};

        std::vector<component_id>   lookup_keys_{};
        std::vector<std::uint32_t>  lookup_vals_{};
        std::uint32_t               lookup_mask_{ 0u };

        [[nodiscard]] std::size_t size() const noexcept { return entities.size(); }
        [[nodiscard]] bool has(const component_id cid) const noexcept { return key.contains(cid); }
        [[nodiscard]] std::size_t column_count() const noexcept { return columns.size(); }

        [[nodiscard]] entity remove_row_swap_nodestroy(const std::size_t row) noexcept
        {
            assert(row < size() && "Row index out of bounds!");
            const std::size_t last_index = size() - 1u;

            entity swapped_in_entity{ INVALID_ENTITY };

            if (row != last_index)
            {
                swapped_in_entity = entities[last_index];
                entities[row] = swapped_in_entity;
            }

            entities.pop_back();
            for (auto& c : columns) c.remove_swap_nodestroy(row);

            return swapped_in_entity;
        }

        [[nodiscard]] size_t column_index(const component_id cid) const noexcept
        {
            const auto it = std::ranges::lower_bound(key.component_ids, cid);
            assert(it != key.component_ids.end() && "Component Id not found in archetype key!");
            return static_cast<std::size_t>(it - key.component_ids.begin());
        }

        [[nodiscard]] column& get_column(const component_id cid) noexcept { return columns[column_index(cid)]; }
        [[nodiscard]] const column& get_column(const component_id cid) const noexcept { return columns[column_index(cid)]; }

        [[nodiscard]] void* get_component_ptr(const std::size_t row, const component_id cid) noexcept
        {
            assert(row < size() && "Row index out of bounds!");
            return get_column(cid).ptr_at_unsafe(row);
        }

        [[nodiscard]] const void* get_component_ptr(const std::size_t row, const component_id cid) const noexcept
        {
            assert(row < size() && "Row index out of bounds!");
            return get_column(cid).ptr_at_unsafe(row);
        }

        void reserve(const std::size_t val) noexcept
        {
            entities.reserve(val);
            for (auto& c : columns) c.reserve(val);
        }

        [[nodiscard]] std::size_t add_row(const entity e)
        {
            const std::size_t row = entities.size();
            entities.emplace_back(e);
            for (auto& c : columns) c.push_defaults();
            return row;
        }

        [[nodiscard]] entity remove_row_swap(const std::size_t row)
        {
            assert(row < size() && "Row index out of bounds!");
            const std::size_t last_index = size() - 1u;

            auto swapped_in_entity = entity(INVALID_ENTITY);

            if (row != last_index)
            {
                swapped_in_entity = entities[last_index];
                entities[row] = swapped_in_entity;
            }

            entities.pop_back();
            for (auto& c : columns) c.remove_swap(row);

            return swapped_in_entity;
        }

        [[nodiscard]] std::size_t add_row_uninitialized(const entity e) noexcept
        {
            const std::size_t row = entities.size();
            entities.emplace_back(e);

            for (auto& c : columns)
            {
                if (c.size == c.capacity)
                {
                    const std::size_t new_capacity = c.capacity == 0u ? 8u : c.capacity * 2u;
                    c.reserve(new_capacity);
                }
                ++c.size;
            }

            return row;
        }

        void default_construct_at(const std::size_t row, const component_id cid) noexcept
        {
            column& col = get_column(cid);
            void* dst = col.ptr_at_unsafe(row);
            assert(col.info.default_ctor && "Column Info does not have valid default constructor");
            col.info.default_ctor(dst);
        }

        void destroy_at(const std::size_t row, const component_id cid) noexcept
        {
            column& col = get_column(cid);
            void* obj = col.ptr_at_unsafe(row);
            assert(col.info.dtor && "Column Info does not have valid destructor");
            col.info.dtor(obj);
        }

        template<typename T>
        void copy_construct_at(const std::size_t row, const component_id cid, const T& src) noexcept
        {
            static_assert(not std::is_void_v<T>);
            column& col = get_column(cid);
            void* dst = col.ptr_at_unsafe(row);
            new (dst) T(src);
        }

        void finalize_schema() noexcept { build_lookup(); }

        [[nodiscard]] std::uint32_t column_index_fast(const component_id cid) const noexcept
        {
            return column_index_fast_impl(cid);
        }

        [[nodiscard]] column& get_column_fast(const component_id cid) noexcept
        {
            return columns[static_cast<std::size_t>(column_index_fast(cid))];
        }

        [[nodiscard]] const column& get_column_fast(const component_id cid) const noexcept
        {
            return columns[static_cast<std::size_t>(column_index_fast(cid))];
        }

        [[nodiscard]] void* column_ptr(const component_id cid) noexcept
        {
            return get_column_fast(cid).data;
        }

        [[nodiscard]] const void* column_ptr(const component_id cid) const noexcept
        {
            return get_column_fast(cid).data;
        }

        template<typename T>
        [[nodiscard]] T* get_array(const component_registry& reg) noexcept
        {
            const component_id cid = reg.get_id<T>();
            assert(has(cid));
            return static_cast<T*>(column_ptr(cid));
        }

        template<typename T>
        [[nodiscard]] const T* get_array(const component_registry& reg) const noexcept
        {
            const component_id cid = reg.get_id<T>();
            assert(has(cid));
            return static_cast<const T*>(column_ptr(cid));
        }

        template<typename T>
        [[nodiscard]] std::span<T> get_span(const component_registry& reg) noexcept
        {
            return { get_array<T>(reg), size() };
        }

        template<typename T>
        [[nodiscard]] std::span<const T> get_span(const component_registry& reg) const noexcept
        {
            return { get_array<T>(reg), size() };
        }

    private:
        [[nodiscard]] static std::uint32_t hash_cid(const component_id cid) noexcept
        {
            std::uint32_t x = cid;
            x ^= x >> 16u;
            x *= 0x7feb352du;
            x ^= x >> 15u;
            x *= 0x846ca68bu;
            x ^= x >> 16u;
            return x;
        }

        void build_lookup() noexcept
        {
            const auto n = static_cast<std::uint32_t>(key.component_ids.size());
            if (n == 0u)
            {
                lookup_keys_.clear();
                lookup_vals_.clear();
                lookup_mask_ = 0u;
                return;
            }

            const std::uint32_t cap = next_pow2_u32(n * 2u);
            lookup_keys_.assign(cap, INVALID_ID);
            lookup_vals_.assign(cap, 0u);
            lookup_mask_ = cap - 1u;

            for (std::uint32_t i = 0; i < n; ++i)
            {
                const component_id cid = key.component_ids[i];

                std::uint32_t slot = hash_cid(cid) & lookup_mask_;
                while (lookup_keys_[slot] != INVALID_ID) slot = (slot + 1u) & lookup_mask_;

                lookup_keys_[slot] = cid;
                lookup_vals_[slot] = i;
            }
        }

        [[nodiscard]] std::uint32_t column_index_fast_impl(const component_id cid) const noexcept
        {
            if (lookup_mask_ == 0u)
            {
                const auto it = std::ranges::lower_bound(key.component_ids, cid);
                assert(it != key.component_ids.end() && *it == cid);
                return static_cast<std::uint32_t>(it - key.component_ids.begin());
            }

            std::uint32_t slot = hash_cid(cid) & lookup_mask_;
            while (true)
            {
                const component_id k = lookup_keys_[slot];
                if (k == cid) return lookup_vals_[slot];
                if (k == INVALID_ID) break;
                slot = (slot + 1u) & lookup_mask_;
            }

            assert(false && "Component Id not found in table (column_index_fast)");
            return 0u;
        }
    };

    class world_storage
    {
    public:
        explicit world_storage(component_registry& registry) noexcept
            : registry_(registry)
        {
            (void)get_or_create_table(make_archetype_key({}));
        }

        [[nodiscard]] table_id get_or_create_table(const archetype_key& key) noexcept
        {
            if (const auto it = key_tables_.find(key); it != key_tables_.end()) [[likely]]
                return it->second;

            return create_table(key);
        }

        [[nodiscard]] table_id get_or_create_table(const std::initializer_list<component_id> init) noexcept
        {
            return get_or_create_table(make_archetype_key(init));
        }

        [[nodiscard]] table_id get_or_create_table(std::vector<component_id> ids) noexcept
        {
            return get_or_create_table(make_archetype_key(std::move(ids)));
        }

        [[nodiscard]] table_id find_table(const archetype_key& key) const noexcept
        {
            const auto it = key_tables_.find(key);
            return it != key_tables_.end() ? it->second : INVALID_TABLE;
        }

        [[nodiscard]] table& get_table(const table_id id) noexcept
        {
            assert(id < tables_.size() && "Invalid table id");
            return tables_.at(id);
        }

        [[nodiscard]] const table& get_table(const table_id id) const noexcept
        {
            assert(id < tables_.size() && "Invalid table id");
            return tables_.at(id);
        }

        [[nodiscard]] std::size_t table_count() const noexcept { return tables_.size(); }
        [[nodiscard]] std::uint64_t schema_version() const noexcept { return schema_version_; }

    private:
        [[nodiscard]] table_id create_table(const archetype_key& key) noexcept
        {
            const auto id = static_cast<table_id>(tables_.size());

            table t{};
            t.id  = id;
            t.key = key;

            t.columns.reserve(key.component_ids.size());
            for (const component_id cid : t.key.component_ids)
            {
                const auto& info = registry_.get_info(cid);
                t.columns.emplace_back(info);
            }

            t.finalize_schema();

            key_tables_.emplace(t.key, id);
            tables_.emplace_back(std::move(t));

            ++schema_version_;
            return id;
        }

    private:
        std::uint64_t schema_version_{ 0u };
        component_registry& registry_;
        std::unordered_map<archetype_key, table_id, archetype_hash> key_tables_{};
        std::vector<table> tables_{};
    };

#pragma endregion

    struct location
    {
        table_id tid{ INVALID_TABLE };
        std::uint32_t row{ 0u };
        [[nodiscard]] bool valid() const noexcept { return tid != INVALID_TABLE; }
    };

    template<typename... Cs>
    class _query;

    class world
    {
    public:
        world() noexcept
            : storage_(registry_)
        {
            empty_table_ = storage_.get_or_create_table(make_archetype_key({}));
        }

        template<typename... Cs>
        [[nodiscard]] _query<Cs...> query() noexcept
        {
            return _query<Cs...>(*this);
        }

        template<typename... Cs>
        void reserve(std::size_t count) noexcept
        {
            std::vector<component_id> ids{ registry_.get_id<Cs>()... };
            const table_id tid = storage_.get_or_create_table(make_archetype_key(std::move(ids)));
            storage_.get_table(tid).reserve(count);
            bump_version();
        }

        template<typename T>
        component_id register_component() noexcept
        {
            return registry_.add<T>();
        }

        [[nodiscard]] entity create_entity() noexcept
        {
            entity e = entities_.create_entity();
            ensure_location_capacity(e.index());

            table& t = storage_.get_table(empty_table_);
            const std::size_t row = t.add_row(e);

            locations_[e.index()] = { empty_table_, static_cast<std::uint32_t>(row) };

            bump_version();
            return e;
        }

        void destroy_entity(const entity e) noexcept
        {
            if (not entities_.alive(e)) return;

            bool removed = false;

            if (location& loc = locations_[e.index()]; loc.valid())
            {
                remove_row_dense_and_fixup(e);
                removed = true;
            }

            entities_.destroy_entity(e);

            if (removed) bump_version();
            else bump_version();
        }

        [[nodiscard]] bool alive(const entity e) const noexcept { return entities_.alive(e); }

        [[nodiscard]] location get_location(const entity e) const noexcept
        {
            if (not entities_.alive(e)) return {};
            const auto index = e.index();
            if (index >= locations_.size()) return {};
            return locations_[index];
        }

        template<typename T>
        [[nodiscard]] T& get_component(const entity e) noexcept
        {
            assert(alive(e) && "Entity is not alive!");
            const component_id cid = registry_.get_id<T>();

            const location loc = locations_[e.index()];
            assert(loc.valid() && "get_component: entity has no valid location!");

            table& t = storage_.get_table(loc.tid);
            assert(t.has(cid) && "get_component: entity archetype does not contain this component!");

            return *static_cast<T*>(t.get_component_ptr(loc.row, cid));
        }

        template<typename T>
        [[nodiscard]] const T& get_component(const entity e) const noexcept
        {
            assert(alive(e) && "Entity is not alive!");
            const component_id cid = registry_.get_id<T>();

            const location loc = locations_[e.index()];
            assert(loc.valid() && "get_component: entity has no valid location!");

            const table& t = storage_.get_table(loc.tid);
            assert(t.has(cid) && "get_component: entity archetype does not contain this component!");

            return *static_cast<const T*>(t.get_component_ptr(loc.row, cid));
        }

        template<typename T>
        [[nodiscard]] T* try_get_component(const entity e) noexcept
        {
            if (not alive(e)) return nullptr;

            const component_id cid = registry_.get_id<T>();
            const auto index = e.index();
            if (index >= locations_.size()) return nullptr;

            const location& loc = locations_[index];
            if (not loc.valid()) return nullptr;

            table& t = storage_.get_table(loc.tid);
            return t.has(cid) ? static_cast<T*>(t.get_component_ptr(loc.row, cid)) : nullptr;
        }

        template<typename T>
        [[nodiscard]] const T* try_get_component(const entity e) const noexcept
        {
            if (not alive(e)) return nullptr;

            const component_id cid = registry_.get_id<T>();
            const auto index = e.index();
            if (index >= locations_.size()) return nullptr;

            const location& loc = locations_[index];
            if (not loc.valid()) return nullptr;

            const table& t = storage_.get_table(loc.tid);
            return t.has(cid) ? static_cast<const T*>(t.get_component_ptr(loc.row, cid)) : nullptr;
        }

        template<typename T>
        void add_component(const entity e) noexcept { add_component<T>(e, nullptr); }

        template<typename T>
        void add_component(const entity e, const T& value) noexcept { add_component<T>(e, &value); }

        template<typename T>
        void remove_component(const entity e) noexcept
        {
            if (!alive(e)) return;

            const component_id cid = registry_.get_id<T>();
            location& loc = locations_[e.index()];
            assert(loc.valid());

            table& old_table = storage_.get_table(loc.tid);
            if (!old_table.has(cid)) return;

            std::vector<component_id> ids = old_table.key.component_ids;
            const auto it = std::ranges::lower_bound(ids, cid);
            assert(it != ids.end() && *it == cid);
            ids.erase(it);

            const table_id new_tid = get_table_for_signature(std::move(ids));
            (void)migrate_entity_to_table(e, new_tid);

            bump_version();
        }

        [[nodiscard]] component_registry& registry() noexcept { return registry_; }
        [[nodiscard]] world_storage&      storage () noexcept { return storage_;  }
        [[nodiscard]] entity_manager&     entities() noexcept { return entities_; }
        [[nodiscard]] std::uint64_t       version () const noexcept { return version_; }

    private:
        void bump_version() noexcept { ++version_; }

        template<typename T>
        void add_component(const entity e, const T* opt_value) noexcept
        {
            if (!alive(e)) return;

            const component_id cid = registry_.get_id<T>();
            location& loc = locations_[e.index()];
            assert(loc.valid());

            table& old_table = storage_.get_table(loc.tid);
            if (old_table.has(cid)) return;

            std::vector<component_id> ids = old_table.key.component_ids;
            ids.push_back(cid);

            const table_id new_tid = get_table_for_signature(std::move(ids));
            const std::uint32_t new_row = migrate_entity_to_table(e, new_tid);

            table& new_table = storage_.get_table(new_tid);
            if (opt_value) new_table.copy_construct_at<T>(new_row, cid, *opt_value);
            else           new_table.default_construct_at(new_row, cid);

            bump_version();
        }

        static void intersect_sorted_ids(
            const std::vector<component_id>& a,
            const std::vector<component_id>& b,
            auto&& fn)
        {
            std::size_t i = 0, j = 0;
            while (i < a.size() && j < b.size())
            {
                if (a[i] == b[j]) { fn(a[i]); ++i; ++j; }
                else if (a[i] < b[j]) ++i;
                else ++j;
            }
        }

        [[nodiscard]] std::uint32_t migrate_entity_to_table(const entity e, const table_id new_tid) noexcept
        {
            location& loc = locations_[e.index()];
            assert(loc.valid() && "migrate_entity_to_table: invalid location");

            const table_id old_tid = loc.tid;
            const std::uint32_t old_row = loc.row;

            table& old_table = storage_.get_table(old_tid);
            table& new_table = storage_.get_table(new_tid);

            const std::uint32_t new_row = static_cast<std::uint32_t>(new_table.add_row_uninitialized(e));

            intersect_sorted_ids(
                old_table.key.component_ids,
                new_table.key.component_ids,
                [&](component_id cid)
                {
                    column& src_col = old_table.get_column(cid);
                    column& dst_col = new_table.get_column(cid);

                    void* src = src_col.ptr_at_unsafe(old_row);
                    void* dst = dst_col.ptr_at_unsafe(new_row);

                    assert(src_col.info.move_ctor && src_col.info.dtor);
                    src_col.info.move_ctor(dst, src);
                    src_col.info.dtor(src);
                });

            for (component_id cid : old_table.key.component_ids)
            {
                if (new_table.has(cid)) continue;

                column& c = old_table.get_column(cid);
                void* obj = c.ptr_at_unsafe(old_row);

                assert(c.info.dtor);
                c.info.dtor(obj);
            }

            const entity swapped = old_table.remove_row_swap_nodestroy(old_row);

            if (swapped.valid())
            {
                location& swapped_loc = locations_[swapped.index()];
                swapped_loc.tid = old_tid;
                swapped_loc.row = old_row;
            }

            loc.tid = new_tid;
            loc.row = new_row;
            bump_version();

            return new_row;
        }

        void ensure_location_capacity(const fecs_id index) noexcept
        {
            if (index >= locations_.size())
                locations_.resize(static_cast<size_t>(index) + 1u);
        }

        void remove_row_dense_and_fixup(const entity e) noexcept
        {
            location& loc = locations_[e.index()];
            assert(loc.valid() && "remove_row_dense_and_fixup: entity has no valid location!");

            table& t = storage_.get_table(loc.tid);

            if (const entity swapped = t.remove_row_swap(loc.row); swapped.valid())
            {
                auto& [tid, row] = locations_[swapped.index()];
                tid = loc.tid;
                row = loc.row;
            }

            loc.tid = INVALID_TABLE;
            loc.row = 0u;

            bump_version();
        }

        [[nodiscard]] table_id get_table_for_signature(std::vector<component_id> ids) noexcept
        {
            archetype_key k{ std::move(ids) };
            return storage_.get_or_create_table(k);
        }

    private:
        entity_manager entities_{};
        component_registry registry_{};
        world_storage storage_;
        table_id empty_table_{ INVALID_TABLE };
        std::vector<location> locations_{};
        std::uint64_t version_{ 0u };
    };

#pragma region QUERY_EACH

    template<typename... Cs>
    class _query
    {
    public:
        struct pinned_block
        {
            std::tuple<Cs*...> arrays{};
            std::size_t n = 0;
        };

        [[nodiscard]] std::vector<pinned_block> pin()
        {
            refresh_if_needed();

            std::vector<pinned_block> out;
            out.reserve(matching_.size());

            for (const table_id tid : matching_)
            {
                table& t = world_->storage().get_table(tid);
                const std::size_t n = t.size();
                if (n == 0) continue;

                pinned_block b{};
                b.arrays = std::tuple{ t.template get_array<Cs>(world_->registry())... };
                b.n = n;

                out.emplace_back(b);
            }

            return out;
        }

        explicit _query(world& w) noexcept
            : world_(&w)
        {
            required_.reserve(sizeof...(Cs));
            (required_.push_back(world_->registry().get_id<Cs>()), ...);

            std::ranges::sort(required_);
            required_.erase(std::ranges::unique(required_).begin(), required_.end());
        }

        template<typename Fn>
        void each(Fn&& fn)
        {
            refresh_if_needed();

            for (const table_id tid : matching_)
            {
                table& t = world_->storage().get_table(tid);
                const std::size_t n = t.size();
                if (n == 0) continue;

                std::invoke(std::forward<Fn>(fn), t.template get_array<Cs>(world_->registry())..., n);
            }
        }

        template<typename Fn>
        void each_entity(Fn&& fn)
        {
            refresh_if_needed();

            for (const table_id tid : matching_)
            {
                table& t = world_->storage().get_table(tid);
                const std::size_t n = t.size();
                if (n == 0) continue;

                auto arrays = std::tuple{ t.template get_array<Cs>(world_->registry())... };

                for (std::size_t i = 0; i < n; ++i)
                {
                    std::apply([&](auto*... ptrs)
                    {
                        std::invoke(fn, t.entities[i], (ptrs[i])...);
                    }, arrays);
                }
            }
        }

        [[nodiscard]] std::size_t match_count()
        {
            refresh_if_needed();
            return matching_.size();
        }

    private:
        void refresh_if_needed()
        {
            const std::uint64_t v = world_->version();
            if (v == cached_version_) return;

            matching_.clear();

            const std::size_t tc = world_->storage().table_count();
            matching_.reserve(tc);

            for (table_id tid = 0; tid < tc; ++tid)
            {
                const table& t = world_->storage().get_table(tid);

                if (t.key.component_ids.size() < required_.size()) continue;

                if (contains_all_sorted(t.key.component_ids, required_))
                    matching_.push_back(tid);
            }

            cached_version_ = v;
        }

        [[nodiscard]] static bool contains_all_sorted(
            const std::vector<component_id>& haystack,
            const std::vector<component_id>& needles) noexcept
        {
            std::size_t i = 0, j = 0;
            while (i < haystack.size() && j < needles.size())
            {
                if (haystack[i] == needles[j]) { ++i; ++j; }
                else if (haystack[i] < needles[j]) ++i;
                else return false;
            }
            return j == needles.size();
        }

    private:
        world* world_{ nullptr };

        std::vector<component_id> required_{};
        std::vector<table_id> matching_{};
        std::uint64_t cached_version_{ std::numeric_limits<std::uint64_t>::max() };
    };

#pragma region RENDER_CACHE

    template<typename... Cs>
    class render_cache
    {
    public:
        struct block
        {
            std::tuple<Cs*...> arrays{};
            std::size_t n = 0;
        };

        void refresh(world& w)
        {
            if (!initialized_)
                init(w);

            const std::uint64_t v = w.version();
            if (v == cached_version_) return;

            build_matching(w);
            rebuild_blocks(w);
            cached_version_ = v;
        }

        [[nodiscard]] const std::vector<block>& blocks() const noexcept { return blocks_; }
        [[nodiscard]] bool empty() const noexcept { return blocks_.empty(); }
        [[nodiscard]] std::uint64_t version() const noexcept { return cached_version_; }

    private:
        void init(world& w)
        {
            required_.reserve(sizeof...(Cs));
            (required_.push_back(w.registry().get_id<Cs>()), ...);
            std::ranges::sort(required_);
            required_.erase(std::ranges::unique(required_).begin(), required_.end());
            initialized_ = true;
        }

        void build_matching(world& w)
        {
            matching_.clear();

            const std::size_t tc = w.storage().table_count();
            matching_.reserve(tc);

            for (table_id tid = 0; tid < tc; ++tid)
            {
                const table& t = w.storage().get_table(tid);
                if (t.key.component_ids.size() < required_.size()) continue;
                if (contains_all_sorted(t.key.component_ids, required_))
                    matching_.push_back(tid);
            }
        }

        void rebuild_blocks(world& w)
        {
            blocks_.clear();
            blocks_.reserve(matching_.size());

            for (const table_id tid : matching_)
            {
                table& t = w.storage().get_table(tid);
                const std::size_t n = t.size();
                if (n == 0) continue;

                block b{};
                b.arrays = std::tuple{ t.template get_array<Cs>(w.registry())... };
                b.n = n;
                blocks_.emplace_back(b);
            }
        }

        [[nodiscard]] static bool contains_all_sorted(
            const std::vector<component_id>& haystack,
            const std::vector<component_id>& needles) noexcept
        {
            std::size_t i = 0, j = 0;
            while (i < haystack.size() && j < needles.size())
            {
                if (haystack[i] == needles[j]) { ++i; ++j; }
                else if (haystack[i] < needles[j]) ++i;
                else return false;
            }
            return j == needles.size();
        }

    private:
        bool initialized_{ false };
        std::vector<component_id> required_{};
        std::vector<table_id> matching_{};
        std::vector<block> blocks_{};
        std::uint64_t cached_version_{ std::numeric_limits<std::uint64_t>::max() };
    };

#pragma endregion
#pragma endregion

} // namespace fecs

#endif // ECSTEMPLATE_CORE_H