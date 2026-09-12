#ifndef TOS_BASE_CONFIG_H_
#define TOS_BASE_CONFIG_H_

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tos/base/result.h"
#include "tos/vendor/fkyaml/node.hpp"
#include "tos/vendor/nlohmann/json.hpp"

namespace tos {

/// Supported input formats for Config::Parse and ConfigStore::Reload.
enum class ConfigFormat {
    kJson,
    kYaml,
};

class LayeredConfig;

/// An immutable, shareable JSON/YAML configuration tree.
///
/// Config owns a shared immutable tree. Copies share that tree and are safe to
/// read concurrently. All lookup failures return Status; allocation failures
/// while parsing, copying strings, merging, or constructing error messages
/// propagate as exceptions. Paths use dots as separators; escape literal dots
/// and backslashes in keys as `\\.` and `\\\\` respectively.
class Config {
   public:
    /// Creates an empty object configuration named "<empty>". Allocation failures propagate.
    Config() : data_(std::make_shared<const Data>(Data{Json::object(), "<empty>"})) {}

    /// Copies share immutable storage and are safe to read concurrently.
    Config(const Config&) = default;
    /// Copies share immutable storage and are safe to read concurrently.
    Config& operator=(const Config&) = default;
    /// Keeps the source usable by sharing its immutable storage; does not throw.
    Config(Config&& other) noexcept : data_(other.data_) {}
    /// Keeps the source usable by sharing its immutable storage; does not throw.
    Config& operator=(Config&& other) noexcept {
        data_ = other.data_;
        return *this;
    }

    /// Creates a configuration from JSON or YAML text.
    ///
    /// The root must be an object. Expected parser and YAML-conversion failures
    /// return kInvalidArgument and include source_name. std::bad_alloc and other
    /// allocation exceptions propagate.
    static Result<Config> Parse(std::string_view text, ConfigFormat format,
                                std::string_view source_name = "<memory>") {
        const std::string_view source =
            source_name.empty() ? std::string_view("<memory>") : source_name;
        try {
            Json document;
            if (format == ConfigFormat::kJson) {
                document = Json::parse(text.begin(), text.end());
            } else if (format == ConfigFormat::kYaml) {
                const fkyaml::node yaml = fkyaml::node::deserialize(text.begin(), text.end());
                auto converted = ConvertYaml(yaml);
                if (!converted) {
                    return Error(StatusCode::kInvalidArgument, source,
                                 converted.status().message());
                }
                document = std::move(converted).value();
            } else {
                return Error(StatusCode::kInvalidArgument, source, "has an unsupported format");
            }

            if (!document.is_object()) {
                return Error(StatusCode::kInvalidArgument, source, "root value must be an object");
            }
            return Config(
                std::make_shared<const Data>(Data{std::move(document), std::string(source)}));
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const Json::exception& exception) {
            return Error(StatusCode::kInvalidArgument, source, exception.what());
        } catch (const fkyaml::exception& exception) {
            return Error(StatusCode::kInvalidArgument, source, exception.what());
        }
    }

    /// Returns whether path exists. Missing paths and invalid path syntax return false.
    bool Has(std::string_view path) const { return static_cast<bool>(Find(path)); }

    /// Returns an exact boolean value at path. No scalar conversions are performed.
    Result<bool> GetBool(std::string_view path) const {
        return GetExact<bool>(path, ConfigValueType::kBoolean);
    }

    /// Returns an exact signed integer value at path. No scalar conversions are performed.
    Result<std::int64_t> GetInt64(std::string_view path) const {
        return GetExact<std::int64_t>(path, ConfigValueType::kSignedInteger);
    }

    /// Returns a non-negative integer at path as uint64_t.
    /// Signed integer values are accepted only when non-negative and therefore
    /// losslessly representable as uint64_t; strings, floats, and booleans are not converted.
    Result<std::uint64_t> GetUint64(std::string_view path) const {
        auto node = Find(path);
        if (!node) {
            return std::move(node).status();
        }
        if (node.value()->is_number_unsigned()) {
            return node.value()->get<std::uint64_t>();
        }
        if (node.value()->is_number_integer() && node.value()->get<std::int64_t>() >= 0) {
            return static_cast<std::uint64_t>(node.value()->get<std::int64_t>());
        }
        return TypeError(path, "unsigned integer", TypeName(TypeOf(*node.value())));
    }

    /// Returns an exact floating-point value at path. Integer values are not converted.
    Result<double> GetDouble(std::string_view path) const {
        return GetExact<double>(path, ConfigValueType::kFloat);
    }

    /// Returns an exact string value at path. The returned string is independently owned.
    Result<std::string> GetString(std::string_view path) const {
        return GetExact<std::string>(path, ConfigValueType::kString);
    }

    /// Returns a new configuration by deeply overlaying object values from overlay.
    /// Arrays, nulls, scalars, and mismatched node types are replaced wholesale.
    /// Neither input changes; allocation exceptions while copying the tree propagate.
    Config Merge(const Config& overlay) const {
        Json merged = data_->document;
        MergeObjects(merged, overlay.data_->document);
        return Config(std::make_shared<const Data>(Data{std::move(merged), "<merged>"}));
    }

   private:
    enum class ConfigValueType {
        kNull,
        kBoolean,
        kSignedInteger,
        kUnsignedInteger,
        kFloat,
        kString,
        kArray,
        kObject,
    };

    using Json = ::nlohmann::json;

    struct Data {
        Json document;
        std::string source;
    };

    explicit Config(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

    static Status Error(StatusCode code, std::string_view source, std::string_view detail) {
        std::string message = "config ";
        message.append(source.data(), source.size());
        message.append(": ");
        message.append(detail.data(), detail.size());
        return Status(code, std::move(message));
    }

    static const char* TypeName(ConfigValueType type) noexcept {
        switch (type) {
            case ConfigValueType::kNull:
                return "null";
            case ConfigValueType::kBoolean:
                return "boolean";
            case ConfigValueType::kSignedInteger:
                return "signed integer";
            case ConfigValueType::kUnsignedInteger:
                return "unsigned integer";
            case ConfigValueType::kFloat:
                return "float";
            case ConfigValueType::kString:
                return "string";
            case ConfigValueType::kArray:
                return "array";
            case ConfigValueType::kObject:
                return "object";
        }
        return "unknown";
    }

    static ConfigValueType TypeOf(const Json& node) noexcept {
        if (node.is_null()) {
            return ConfigValueType::kNull;
        }
        if (node.is_boolean()) {
            return ConfigValueType::kBoolean;
        }
        if (node.is_number_integer()) {
            return ConfigValueType::kSignedInteger;
        }
        if (node.is_number_unsigned()) {
            return ConfigValueType::kUnsignedInteger;
        }
        if (node.is_number_float()) {
            return ConfigValueType::kFloat;
        }
        if (node.is_string()) {
            return ConfigValueType::kString;
        }
        if (node.is_array()) {
            return ConfigValueType::kArray;
        }
        return ConfigValueType::kObject;
    }

    static Result<std::vector<std::string>> ParsePath(std::string_view path,
                                                      std::string_view source) {
        if (path.empty()) {
            return Error(StatusCode::kInvalidArgument, source, "path must not be empty");
        }

        std::vector<std::string> parts;
        std::string part;
        bool escaped = false;
        for (const char character : path) {
            if (escaped) {
                if (character != '.' && character != '\\') {
                    return Error(StatusCode::kInvalidArgument, source,
                                 "path has an unsupported escape sequence");
                }
                part.push_back(character);
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '.') {
                if (part.empty()) {
                    return Error(StatusCode::kInvalidArgument, source,
                                 "path contains an empty segment");
                }
                parts.push_back(std::move(part));
                part.clear();
            } else {
                part.push_back(character);
            }
        }
        if (escaped) {
            return Error(StatusCode::kInvalidArgument, source, "path ends with an escape");
        }
        if (part.empty()) {
            return Error(StatusCode::kInvalidArgument, source, "path contains an empty segment");
        }
        parts.push_back(std::move(part));
        return parts;
    }

    static Result<std::size_t> ParseArrayIndex(std::string_view part, std::string_view source,
                                               std::string_view path) {
        if (part.empty()) {
            return Error(StatusCode::kInvalidArgument, source,
                         "path '" + std::string(path) + "' has an empty array index");
        }
        std::size_t value = 0;
        for (const char character : part) {
            if (!std::isdigit(static_cast<unsigned char>(character))) {
                return Error(StatusCode::kInvalidArgument, source,
                             "path '" + std::string(path) + "' has a non-numeric array index");
            }
            const unsigned digit = static_cast<unsigned>(character - '0');
            if (value > (static_cast<std::size_t>(-1) - digit) / 10) {
                return Error(StatusCode::kInvalidArgument, source,
                             "path '" + std::string(path) + "' has an out-of-range array index");
            }
            value = value * 10 + digit;
        }
        return value;
    }

    Result<const Json*> Find(std::string_view path) const {
        auto parts = ParsePath(path, data_->source);
        if (!parts) {
            return std::move(parts).status();
        }

        const Json* current = std::addressof(data_->document);
        for (const std::string& part : parts.value()) {
            if (current->is_object()) {
                const auto found = current->find(part);
                if (found == current->end()) {
                    return Error(StatusCode::kNotFound, data_->source,
                                 "path '" + std::string(path) + "' was not found");
                }
                current = std::addressof(*found);
                continue;
            }
            if (current->is_array()) {
                auto index = ParseArrayIndex(part, data_->source, path);
                if (!index) {
                    return std::move(index).status();
                }
                if (index.value() >= current->size()) {
                    return Error(StatusCode::kNotFound, data_->source,
                                 "path '" + std::string(path) + "' was not found");
                }
                current = std::addressof((*current)[index.value()]);
                continue;
            }
            return Error(StatusCode::kNotFound, data_->source,
                         "path '" + std::string(path) + "' was not found");
        }
        return current;
    }

    Status TypeError(std::string_view path, std::string_view expected,
                     std::string_view actual) const {
        std::string message = "path '";
        message.append(path.data(), path.size());
        message.append("' has type ");
        message.append(actual.data(), actual.size());
        message.append("; expected ");
        message.append(expected.data(), expected.size());
        return Error(StatusCode::kInvalidArgument, data_->source, message);
    }

    template <typename T>
    Result<T> GetExact(std::string_view path, ConfigValueType expected_type) const {
        auto node = Find(path);
        if (!node) {
            return std::move(node).status();
        }
        if (TypeOf(*node.value()) != expected_type) {
            return TypeError(path, TypeName(expected_type), TypeName(TypeOf(*node.value())));
        }
        return node.value()->get<T>();
    }

    static Result<Json> ConvertYaml(const fkyaml::node& node) {
        if (node.has_tag_name()) {
            return Status(StatusCode::kInvalidArgument, "YAML tags are not supported");
        }
        if (node.is_mapping()) {
            Json result = Json::object();
            for (const auto& entry : node.as_map()) {
                if (!entry.first.is_string()) {
                    return Status(StatusCode::kInvalidArgument,
                                  "YAML mapping keys must be strings");
                }
                auto child = ConvertYaml(entry.second);
                if (!child) {
                    return std::move(child).status();
                }
                result[entry.first.as_str()] = std::move(child).value();
            }
            return result;
        }
        if (node.is_sequence()) {
            Json result = Json::array();
            for (const auto& child_node : node.as_seq()) {
                auto child = ConvertYaml(child_node);
                if (!child) {
                    return std::move(child).status();
                }
                result.push_back(std::move(child).value());
            }
            return result;
        }
        if (node.is_null()) {
            return Json(nullptr);
        }
        if (node.is_boolean()) {
            return Json(node.as_bool());
        }
        if (node.is_integer()) {
            try {
                return Json(static_cast<std::int64_t>(node.as_int()));
            } catch (const fkyaml::type_error&) {
                return Json(node.as_uint());
            }
        }
        if (node.is_float_number()) {
            return Json(static_cast<double>(node.as_float()));
        }
        if (node.is_string()) {
            return Json(node.as_str());
        }
        return Status(StatusCode::kInvalidArgument, "YAML contains an unsupported node type");
    }

    static void MergeObjects(Json& base, const Json& overlay) {
        for (auto iterator = overlay.begin(); iterator != overlay.end(); ++iterator) {
            auto existing = base.find(iterator.key());
            if (existing != base.end() && existing->is_object() && iterator.value().is_object()) {
                MergeObjects(*existing, iterator.value());
            } else {
                base[iterator.key()] = iterator.value();
            }
        }
    }

    std::shared_ptr<const Data> data_;

    friend class ConfigStore;
    friend class LayeredConfig;
};

/// An input layer for LayeredConfig.
///
/// label is a caller-owned, stable identifier copied into LayeredConfig during
/// creation. It must be non-empty and unique within one LayeredConfig.
/// Smaller priority values take precedence; for equal priorities, the later
/// vector element takes precedence. config shares its immutable tree when
/// copied or moved. Constructing the string or Config can propagate exceptions.
struct ConfigLayer {
    std::string label;
    int priority = 0;
    Config config;
};

/// An immutable, shareable collection of prioritized Config layers.
///
/// LayeredConfig builds one complete Config snapshot by merging layers from
/// lowest to highest precedence. Both object-valued layers merge deeply; arrays,
/// nulls, scalars, and type mismatches replace the lower-precedence value. It
/// owns its layer labels and Config values, is safe for concurrent reads, and
/// has no mutation API. Publish a replacement Snapshot through ConfigStore when
/// a new layer set is needed. Allocation exceptions propagate.
class LayeredConfig {
   public:
    /// Creates a LayeredConfig from layers.
    ///
    /// An empty layer set produces an empty snapshot. Empty or duplicate labels
    /// return kInvalidArgument. Smaller priorities win; among equal priorities,
    /// the later input element wins. The input vector is consumed, and Config
    /// values continue to share their immutable trees. Allocation exceptions
    /// while validating, sorting, or merging propagate.
    static Result<LayeredConfig> Create(std::vector<ConfigLayer> layers) {
        std::unordered_set<std::string> labels;
        for (const ConfigLayer& layer : layers) {
            if (layer.label.empty()) {
                return Status(StatusCode::kInvalidArgument,
                              "layered config: layer label must not be empty");
            }
            if (!labels.insert(layer.label).second) {
                return Status(StatusCode::kInvalidArgument,
                              "layered config: duplicate layer label '" + layer.label + "'");
            }
        }

        // Merge from the lowest-priority layer to the highest-priority layer.
        // A stable ordering leaves equal-priority layers in input order, so the
        // later layer becomes the final overlay.
        std::stable_sort(layers.begin(), layers.end(),
                         [](const ConfigLayer& lhs, const ConfigLayer& rhs) {
                             return lhs.priority > rhs.priority;
                         });

        Config snapshot;
        for (const ConfigLayer& layer : layers) {
            snapshot = snapshot.Merge(layer.config);
        }
        return LayeredConfig(
            std::make_shared<const Data>(Data{std::move(layers), std::move(snapshot)}));
    }

    /// Copies share immutable state and are safe to read concurrently.
    LayeredConfig(const LayeredConfig&) = default;
    /// Copies share immutable state and are safe to read concurrently.
    LayeredConfig& operator=(const LayeredConfig&) = default;
    /// Keeps the source usable by sharing immutable state; does not throw.
    LayeredConfig(LayeredConfig&& other) noexcept : data_(other.data_) {}
    /// Keeps the source usable by sharing immutable state; does not throw.
    LayeredConfig& operator=(LayeredConfig&& other) noexcept {
        data_ = other.data_;
        return *this;
    }

    /// Returns the complete immutable configuration snapshot.
    ///
    /// The returned Config may outlive this LayeredConfig and is safe to read
    /// concurrently. Copying the shared state does not allocate.
    Config Snapshot() const { return data_->snapshot; }

    /// Returns the label of the highest-precedence layer that defines path.
    ///
    /// The path syntax and kInvalidArgument/kNotFound failures match Config.
    /// If path resolves to an object built from multiple layers, this returns
    /// the highest-precedence layer defining that object; it does not claim the
    /// complete subtree came from that layer. The returned label is independently
    /// owned. Allocation while copying it or constructing an error propagates.
    Result<std::string> SourceOf(std::string_view path) const {
        auto resolved = data_->snapshot.Find(path);
        if (!resolved) {
            return std::move(resolved).status();
        }

        for (auto iterator = data_->layers.rbegin(); iterator != data_->layers.rend(); ++iterator) {
            auto source = iterator->config.Find(path);
            if (source) {
                return iterator->label;
            }
            if (source.status().code() != StatusCode::kNotFound) {
                return std::move(source).status();
            }
        }
        return Status(StatusCode::kInternal, "layered config: resolved path has no defining layer");
    }

   private:
    struct Data {
        // Layers are stored from lowest to highest precedence.
        std::vector<ConfigLayer> layers;
        Config snapshot;
    };

    explicit LayeredConfig(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

    std::shared_ptr<const Data> data_;
};

/// A concurrently readable holder that atomically publishes complete Config snapshots.
///
/// Readers may call Snapshot concurrently with Reload and observe either the old
/// or the new complete tree. Multiple writers are allowed; the last atomic store
/// to execute wins. Reload translates expected parsing failures to Status and
/// preserves the previous snapshot on failure; allocation exceptions propagate.
class ConfigStore {
   public:
    /// Creates a store initialized with initial. Copies of Config share immutable storage.
    explicit ConfigStore(Config initial) : current_(std::move(initial.data_)) {}

    /// Returns an immutable configuration snapshot safe to retain across later reloads.
    Config Snapshot() const {
        return Config(std::atomic_load_explicit(&current_, std::memory_order_acquire));
    }

    /// Parses and atomically publishes a new configuration on success.
    /// Parse failures return Status and leave the previous snapshot unchanged.
    Status Reload(std::string_view text, ConfigFormat format,
                  std::string_view source_name = "<memory>") {
        auto parsed = Config::Parse(text, format, source_name);
        if (!parsed) {
            return std::move(parsed).status();
        }
        std::atomic_store_explicit(&current_, parsed.value().data_, std::memory_order_release);
        return Status::Ok();
    }

   private:
    std::shared_ptr<const Config::Data> current_;
};

}  // namespace tos

#endif  // TOS_BASE_CONFIG_H_
