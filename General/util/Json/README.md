# Shared nlohmann JSON

`Json.h` uses the existing `General/ThirdParty/json.hpp` (nlohmann/json 3.11.2).
`zzj::json::Json` is a type alias for `nlohmann::json`, with no substitute container or parser.

- `parseObject(std::string_view, maximumBytes)` uses nlohmann parsing, validates the object root, bounds input/depth and redacts parser input from errors.
- `readObjectFile(std::filesystem::path, maximumBytes)` bounds the initial size and actual read; it never creates a missing configuration.
- `JsonQtTypes.h` adds QString UTF-8 serialization, QByteArray output and Unicode file path overloads. It contains no Qt JSON value, parser or serializer.
- `ClientDevLib::Json` is an INTERFACE CMake target exposing the existing library header path; it does not download or copy a second JSON library.

Use native `Json::object()`, `Json::array()`, `at`, `value`, `get`, and `dump` throughout consumers. Optional values must still validate types where required by the application schema. Keep business policy and authorization checks in the application.
