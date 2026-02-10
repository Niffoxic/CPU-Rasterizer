#ifndef DIRECTX12_JSON_LOADER_H
#define DIRECTX12_JSON_LOADER_H

#include <string>
#include <unordered_map>
#include <sstream>
#include <cstdint>

#include "file_system.h"

class JsonLoader
{
public:
    using ObjectType = std::unordered_map<std::string, JsonLoader>;

public:
     JsonLoader() = default;
    ~JsonLoader() = default;

    void Load(const std::string& filePath);
    void Save(const std::string& filePath);

    JsonLoader& operator=(const std::string& value);

    JsonLoader& operator=(const char* value)
    {
        m_value = value ? value : "";
        m_children.clear();
        return *this;
    }

    JsonLoader& operator=(int value)
    {
        m_value = std::to_string(value);
        m_children.clear();
        return *this;
    }

    JsonLoader& operator=(std::int64_t value)
    {
        m_value = std::to_string(value);
        m_children.clear();
        return *this;
    }

    JsonLoader& operator=(float value)
    {
        m_value = std::to_string(value);
        m_children.clear();
        return *this;
    }

    JsonLoader& operator=(double value)
    {
        m_value = std::to_string(value);
        m_children.clear();
        return *this;
    }

    JsonLoader& operator=(bool value)
    {
        m_value = value ? "true" : "false";
        m_children.clear();
        return *this;
    }

    //~ Child access

    const JsonLoader& operator[](const std::string& key) const;
    JsonLoader& operator[](const std::string& key)
    {
        return GetOrCreate(key);
    }

    //~ Rule of 5
    JsonLoader           (JsonLoader&&) noexcept = default;
    JsonLoader& operator=(JsonLoader&&) noexcept = default;

    JsonLoader           (const JsonLoader&) = default;
    JsonLoader& operator=(const JsonLoader&) = default;

    //~ Child management
    JsonLoader& GetOrCreate(const std::string& key);

    auto begin() { return m_children.begin(); }
    auto end() { return m_children.end(); }
    auto begin()  const { return m_children.begin(); }
    auto end()    const { return m_children.end(); }

    const std::string& GetValue() const { return m_value; }
    void               SetValue(const std::string& val) { m_value = val; }

    [[nodiscard]] bool Contains(const std::string& key) const;
    [[nodiscard]] bool Has(const std::string& key) const;

    //~ Serialization helpers
    std::string ToFormattedString(int indent = 0) const;
    void        FromStream(std::istream& input);

    //~ Typed access with optional defaults
    [[nodiscard]] float AsFloat(float defaultValue = 0.0f)   const;
    [[nodiscard]] int   AsInt(int   defaultValue = 0)      const;
    [[nodiscard]] std::uint32_t   AsUInt(std::uint32_t defaultValue = 0)      const;
    [[nodiscard]] bool  AsBool(bool  defaultValue = false)  const;

    [[nodiscard]] bool IsValid() const;
    void               Clear();

    // Object vs leaf convenience
    [[nodiscard]] bool IsObject() const { return !m_children.empty(); }
    [[nodiscard]] bool IsLeaf()   const { return m_children.empty(); }

private:
    void        Serialize(std::ostream& output, int indent) const;
    void        ParseObject(std::istream& input);
    static void SkipWhitespace(std::istream& input);
    static bool ConsumeChar(std::istream& input, char expected);
    static std::string ReadQuotedString(std::istream& input);
    static std::string EscapeString(const std::string& s);

private:
    std::string m_value;
    ObjectType  m_children;

    FileSystem m_fileSystem{};
};
#endif //DIRECTX12_JSON_LOADER_H