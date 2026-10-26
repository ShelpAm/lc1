// TinyGLTF is header-only in Conan; emit its parser and image writer once.
// Image decoding is provided by stb-image.cpp.
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
