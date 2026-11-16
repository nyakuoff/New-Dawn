// Readers for the player's own Majora's Mask 3D (3DS) files: LzS compression, GAR archives and CMB models.
// A clean-room C++ port of tools/newdawn/mm3d/{binary,cmb,texture}.py (format facts and noclip.website's MIT notes;
// nothing from zelda3d-coop). Nothing read here is ever committed or shipped (ND-008, ND-013).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Mm3d {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // row-major, 4 bytes per pixel
};

// "LzS\x01" Okumura LZSS; data without the magic is passed through unchanged.
bool Inflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error);

// A GAR archive with its entries copied out by name ("model/soldier.cmb").
struct Gar {
    std::map<std::string, std::vector<uint8_t>> files;
};
bool ParseGar(const std::vector<uint8_t>& fileBytes, Gar& out, std::string& error);

struct Bone {
    int id = 0, parent = -1;
    float scale[3] = {}, rotation[3] = {}, translation[3] = {};
    double bind[16] = {}; // hierarchical bind matrix, row-major, column vectors (Rz*Ry*Rx*S, then translation)
};

struct Material {
    int texture[3] = { -1, -1, -1 };
    uint16_t wrapS[3] = {}, wrapT[3] = {};
    int uvSource = 0;          // texture coordinate set of unit 0
    float uvTransform[5] = {}; // scale x/y, rotation, translate x/y of unit 0
    int cull = 1;              // 0 none, 1 back, 2 front
    bool alphaTest = false;
    int blendMode = 0;
    uint8_t alphaRef = 0;
    uint8_t diffuse[4] = {}, ambient[4] = {}, emission[4] = {};
    uint8_t constants[6][4] = {};
    bool fragmentLighting = false, vertexLighting = false;
    int layer = 0;
    int combinerCount = 0;
    uint16_t combiners[6][20] = {};
    enum Alpha { Opaque, Cutout, Blend } alpha = Opaque;
};

struct Vertex {
    float pos[3] = {}, normal[3] = {}, uv[2] = {};
    int32_t bone[4] = { -1, -1, -1, -1 }; // CMB bone ids
    float weight[4] = {};
};

struct Draw {
    int material = 0;
    int skinning = 0; // 0 rigid, 1 per-vertex single bone, 2 smooth
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; // triangles
};

struct Texture {
    std::string name;
    int width = 0, height = 0;
    std::string format;
    Image image; // mip level 0 as RGBA8
};

struct Model {
    std::string name;
    std::vector<Bone> bones;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Draw> draws;
};

bool ParseCmb(const std::vector<uint8_t>& data, Model& out, std::string& error);

} // namespace Mm3d
