#include "lc1/vertex.hpp"
#include <glm/gtx/hash.hpp>

namespace std {

size_t hash<lc1::Vertex>::operator()(lc1::Vertex const &vertex) const
{
    return std::hash<glm::vec3>{}(vertex.position) ^ (std::hash<glm::vec2>{}(vertex.uv) << 1) ^
           (std::hash<glm::vec3>{}(vertex.color) << 2) ^
           (std::hash<glm::vec3>{}(vertex.normal) << 3) ^
           (std::hash<glm::vec4>{}(vertex.tangent) << 4);
}

} // namespace std
