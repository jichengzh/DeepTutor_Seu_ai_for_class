#pragma once
/**
 * map_metadata_store.hpp
 *
 * C++ interface for the map metadata store.  The canonical implementation is
 * the Python SQLite backend in scripts/map_metadata_store.py.  This header
 * provides lightweight C++ data structures that mirror the Python dataclass so
 * that C++ nodes (e.g. SemanticMapMerger) can work with the same schema.
 *
 * For production use, consider wrapping the Python module via pybind11 or
 * providing a REST/gRPC interface; for this internship project, the C++ side
 * only reads/writes the JSON export produced by MapMetadataStore.export_to_json().
 */

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>   // header-only JSON — add as ament_vendor dep if needed

namespace module_3_mapping {

// ---------------------------------------------------------------------------
// Data structures (mirror scripts/map_metadata_store.py SemanticObject)
// ---------------------------------------------------------------------------

struct ObjectLabel {
  std::string category;
  double      confidence{0.0};
  int         source_id{0};
};

struct MapObject {
  int         id{-1};
  std::string map_name;
  double      x{0.0};
  double      y{0.0};
  double      z{0.0};
  ObjectLabel label;
  double      timestamp{0.0};
  std::string extra_json;  ///< any extra key/value pairs serialised as JSON
};

// ---------------------------------------------------------------------------
// Serialisation helpers
// ---------------------------------------------------------------------------

/// Deserialise a MapObject from a JSON object (as produced by export_to_json).
inline MapObject mapObjectFromJson(const nlohmann::json& j) {
  MapObject obj;
  obj.id        = j.value("id",        -1);
  obj.map_name  = j.value("map_name",  "");
  obj.x         = j.value("x",          0.0);
  obj.y         = j.value("y",          0.0);
  obj.z         = j.value("z",          0.0);
  obj.timestamp = j.value("timestamp",  0.0);
  obj.label.category   = j.value("category",   "unknown");
  obj.label.confidence = j.value("confidence",  0.0);
  obj.label.source_id  = j.value("source_id",   0);
  return obj;
}

/// Serialise a MapObject to JSON.
inline nlohmann::json mapObjectToJson(const MapObject& obj) {
  return nlohmann::json{
    {"id",         obj.id},
    {"map_name",   obj.map_name},
    {"x",          obj.x},
    {"y",          obj.y},
    {"z",          obj.z},
    {"timestamp",  obj.timestamp},
    {"category",   obj.label.category},
    {"confidence", obj.label.confidence},
    {"source_id",  obj.label.source_id},
  };
}

// ---------------------------------------------------------------------------
// MapMetadataStoreReader
//
// Read-only C++ wrapper that loads the JSON export written by the Python store.
// ---------------------------------------------------------------------------

class MapMetadataStoreReader {
public:
  /// Load all objects from a JSON file produced by MapMetadataStore.export_to_json().
  bool loadFromJson(const std::string& json_path);

  /// Return all loaded objects.
  const std::vector<MapObject>& getObjects() const { return objects_; }

  /// Find an object by id. Returns std::nullopt if not found.
  std::optional<MapObject> getById(int id) const;

  /// Return all objects belonging to a given map name.
  std::vector<MapObject> getByMapName(const std::string& map_name) const;

  /// Return all objects with confidence above threshold.
  std::vector<MapObject> filterByConfidence(double min_confidence) const;

  /// Number of loaded objects.
  std::size_t size() const { return objects_.size(); }

private:
  std::vector<MapObject> objects_;
};

} // namespace module_3_mapping
