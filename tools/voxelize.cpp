/*
 * voxelize.cpp - Build-time mesh to voxel conversion tool
 *
 * Converts OBJ mesh files to C source files containing VoxelShape descriptors.
 * Uses conservative triangle-AABB overlap testing for voxelization.
 *
 * Usage: voxelize input.obj output.c [options]
 *   --name <name>      Shape name (default: derived from filename)
 *   --resolution <n>   Voxels along the longest axis, 1..VOBJ_GRID_SIZE (default: 16)
 *   --material <id>    Material ID for solid voxels, 1..255 (default: MAT_STONE)
 *
 * Output: C source file with a VoxelShape descriptor ready for content/shapes/.
 * The shape is a surface shell: voxels the mesh's triangles pass through.
 */

#include "content/materials.h"
#include "engine/voxel/voxel_object.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

/* Simple 3D vector (Vec3 is engine/core's plain C struct) */
namespace
{
struct Vec3f
{
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3f operator+(const Vec3f &b) const { return Vec3f(x + b.x, y + b.y, z + b.z); }
    Vec3f operator-(const Vec3f &b) const { return Vec3f(x - b.x, y - b.y, z - b.z); }
    Vec3f operator*(float s) const { return Vec3f(x * s, y * s, z * s); }

    float dot(const Vec3f &b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3f cross(const Vec3f &b) const
    {
        return Vec3f(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }

    static Vec3f min(const Vec3f &a, const Vec3f &b)
    {
        return Vec3f(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    }
    static Vec3f max(const Vec3f &a, const Vec3f &b)
    {
        return Vec3f(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    }
};

/* Triangle */
struct Triangle
{
    Vec3f v0, v1, v2;
};

/* AABB */
struct AABB
{
    Vec3f min_corner, max_corner;

    Vec3f center() const { return (min_corner + max_corner) * 0.5f; }
    Vec3f half_extents() const { return (max_corner - min_corner) * 0.5f; }
};

/* Mesh data */
struct Mesh
{
    std::vector<Vec3f> vertices;
    std::vector<Triangle> triangles;
    AABB bounds;
};

/* Voxel grid */
struct VoxelGrid
{
    int size_x, size_y, size_z;
    std::vector<uint8_t> voxels;
    float voxel_size;
    Vec3f origin;

    uint8_t &at(int x, int y, int z)
    {
        return voxels[x + y * size_x + z * size_x * size_y];
    }
};
} // namespace

/*
 * Triangle-AABB overlap test using Separating Axis Theorem.
 * Based on Tomas Akenine-Moller's algorithm.
 */
static bool triangle_aabb_overlap(const Triangle &tri, const AABB &box)
{
    Vec3f center = box.center();
    Vec3f half = box.half_extents();

    /* Move triangle to box center */
    Vec3f v0 = tri.v0 - center;
    Vec3f v1 = tri.v1 - center;
    Vec3f v2 = tri.v2 - center;

    /* Compute triangle edges */
    Vec3f e0 = v1 - v0;
    Vec3f e1 = v2 - v1;
    Vec3f e2 = v0 - v2;

    /* Test AABB axes (X, Y, Z) */
    float min_val, max_val;

    /* X-axis */
    min_val = std::min({v0.x, v1.x, v2.x});
    max_val = std::max({v0.x, v1.x, v2.x});
    if (min_val > half.x || max_val < -half.x)
        return false;

    /* Y-axis */
    min_val = std::min({v0.y, v1.y, v2.y});
    max_val = std::max({v0.y, v1.y, v2.y});
    if (min_val > half.y || max_val < -half.y)
        return false;

    /* Z-axis */
    min_val = std::min({v0.z, v1.z, v2.z});
    max_val = std::max({v0.z, v1.z, v2.z});
    if (min_val > half.z || max_val < -half.z)
        return false;

    /* Test triangle normal as separating axis */
    Vec3f normal = e0.cross(e1);
    float d = normal.dot(v0);
    float r = half.x * std::abs(normal.x) + half.y * std::abs(normal.y) +
              half.z * std::abs(normal.z);
    if (std::abs(d) > r)
        return false;

    /* Test 9 edge cross products (3 edges x 3 axes) */
    auto axis_test = [&](const Vec3f &axis) {
        float p0 = axis.dot(v0);
        float p1 = axis.dot(v1);
        float p2 = axis.dot(v2);
        float r = half.x * std::abs(axis.x) + half.y * std::abs(axis.y) +
                  half.z * std::abs(axis.z);
        float min_p = std::min({p0, p1, p2});
        float max_p = std::max({p0, p1, p2});
        return !(min_p > r || max_p < -r);
    };

    Vec3f axes[9] = {
        Vec3f(0, -e0.z, e0.y), Vec3f(0, -e1.z, e1.y), Vec3f(0, -e2.z, e2.y),
        Vec3f(e0.z, 0, -e0.x), Vec3f(e1.z, 0, -e1.x), Vec3f(e2.z, 0, -e2.x),
        Vec3f(-e0.y, e0.x, 0), Vec3f(-e1.y, e1.x, 0), Vec3f(-e2.y, e2.x, 0),
    };

    for (int i = 0; i < 9; i++)
    {
        if (axes[i].length() > 0.0001f && !axis_test(axes[i]))
            return false;
    }

    return true;
}

/* The next line of f, however long, without its line break; false at the end of the file
 * or on a read error */
static bool read_line(FILE *f, std::string &line)
{
    line.clear();
    char chunk[256];
    while (!feof(f) && !ferror(f) && fgets(chunk, sizeof(chunk), f))
    {
        line += chunk;
        if (line.back() == '\n')
        {
            line.pop_back();
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
    }
    return !line.empty();
}

/*
 * Load OBJ mesh file.
 * Simple parser supporting v (vertex) and f (face) commands.
 */
static bool load_obj(const char *path, Mesh &mesh)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "Error: Cannot open file %s\n", path);
        return false;
    }

    std::string text;
    mesh.bounds.min_corner = Vec3f(1e10f, 1e10f, 1e10f);
    mesh.bounds.max_corner = Vec3f(-1e10f, -1e10f, -1e10f);

    while (read_line(f, text))
    {
        const char *line = text.c_str();

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0')
            continue;

        /* Vertex */
        if (line[0] == 'v' && line[1] == ' ')
        {
            float c[3];
            const char *ptr = line + 2;
            int parsed = 0;
            for (; parsed < 3; parsed++)
            {
                char *end = nullptr;
                c[parsed] = std::strtof(ptr, &end);
                if (end == ptr)
                    break;
                ptr = end;
            }
            if (parsed == 3)
            {
                Vec3f v(c[0], c[1], c[2]);
                mesh.vertices.push_back(v);
                mesh.bounds.min_corner = Vec3f::min(mesh.bounds.min_corner, v);
                mesh.bounds.max_corner = Vec3f::max(mesh.bounds.max_corner, v);
            }
        }
        /* Face */
        else if (line[0] == 'f' && line[1] == ' ')
        {
            /* Parse face indices (supports v, v/vt, v/vt/vn, v//vn) */
            std::vector<int> indices;
            const char *ptr = line + 2;

            while (*ptr)
            {
                while (*ptr && std::isspace(static_cast<unsigned char>(*ptr)))
                    ptr++;
                if (!*ptr)
                    break;

                /* The vertex index (any /vt/vn after it is skipped below) */
                char *end = nullptr;
                long idx = std::strtol(ptr, &end, 10);
                if (end != ptr)
                {
                    /* OBJ indices are 1-based; negative ones count back from the last vertex */
                    if (idx < 0)
                        idx = (long)mesh.vertices.size() + idx + 1;
                    indices.push_back((int)idx - 1);
                }

                /* Skip to next vertex */
                while (*ptr && !std::isspace(static_cast<unsigned char>(*ptr)))
                    ptr++;
            }

            /* Triangulate face (fan triangulation) */
            for (size_t i = 2; i < indices.size(); i++)
            {
                Triangle tri;
                int i0 = indices[0];
                int i1 = indices[i - 1];
                int i2 = indices[i];

                if (i0 >= 0 && i0 < (int)mesh.vertices.size() &&
                    i1 >= 0 && i1 < (int)mesh.vertices.size() &&
                    i2 >= 0 && i2 < (int)mesh.vertices.size())
                {
                    tri.v0 = mesh.vertices[i0];
                    tri.v1 = mesh.vertices[i1];
                    tri.v2 = mesh.vertices[i2];
                    mesh.triangles.push_back(tri);
                }
            }
        }
    }

    fclose(f);

    printf("Loaded %zu vertices, %zu triangles\n", mesh.vertices.size(), mesh.triangles.size());
    printf("Bounds: (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)\n",
           mesh.bounds.min_corner.x, mesh.bounds.min_corner.y, mesh.bounds.min_corner.z,
           mesh.bounds.max_corner.x, mesh.bounds.max_corner.y, mesh.bounds.max_corner.z);

    return !mesh.triangles.empty();
}

/*
 * Voxelize mesh using conservative triangle-AABB overlap.
 */
static void voxelize(const Mesh &mesh, VoxelGrid &grid, int resolution, uint8_t material)
{
    Vec3f extent = mesh.bounds.max_corner - mesh.bounds.min_corner;
    float max_extent = std::max({extent.x, extent.y, extent.z});

    /* Compute voxel size to fit resolution along longest axis */
    grid.voxel_size = max_extent / (float)resolution;

    /* Guard against degenerate meshes producing near-zero voxel size */
    if (grid.voxel_size < 0.0001f)
    {
        fprintf(stderr, "Warning: mesh too small, clamping voxel_size to 0.0001\n");
        grid.voxel_size = 0.0001f;
    }

    grid.origin = mesh.bounds.min_corner;

    /* Grid dimensions: at least one voxel, at most resolution (extent / voxel_size can
     * round to just above it along the longest axis) */
    auto axis_size = [&](float axis_extent) {
        return std::clamp((int)std::ceil(axis_extent / grid.voxel_size), 1, resolution);
    };
    grid.size_x = axis_size(extent.x);
    grid.size_y = axis_size(extent.y);
    grid.size_z = axis_size(extent.z);

    /* Allocate voxel grid */
    grid.voxels.resize(grid.size_x * grid.size_y * grid.size_z, 0);

    printf("Voxelizing to %dx%dx%d grid (voxel size: %.4f)\n",
           grid.size_x, grid.size_y, grid.size_z, grid.voxel_size);

    /* Each triangle only tests the voxels around its own bounding box (one voxel of
     * margin keeps triangles that just touch a voxel face) */
    auto voxel_range = [&](float lo, float hi, float origin, int size, int *first, int *last) {
        *first = std::max(0, (int)std::floor((lo - origin) / grid.voxel_size) - 1);
        *last = std::min(size - 1, (int)std::floor((hi - origin) / grid.voxel_size) + 1);
    };

    for (const Triangle &tri : mesh.triangles)
    {
        Vec3f lo = Vec3f::min(Vec3f::min(tri.v0, tri.v1), tri.v2);
        Vec3f hi = Vec3f::max(Vec3f::max(tri.v0, tri.v1), tri.v2);
        int x0, x1, y0, y1, z0, z1;
        voxel_range(lo.x, hi.x, grid.origin.x, grid.size_x, &x0, &x1);
        voxel_range(lo.y, hi.y, grid.origin.y, grid.size_y, &y0, &y1);
        voxel_range(lo.z, hi.z, grid.origin.z, grid.size_z, &z0, &z1);

        for (int z = z0; z <= z1; z++)
        {
            for (int y = y0; y <= y1; y++)
            {
                for (int x = x0; x <= x1; x++)
                {
                    if (grid.at(x, y, z) != 0)
                        continue;

                    AABB voxel_box;
                    voxel_box.min_corner = grid.origin + Vec3f(x * grid.voxel_size,
                                                              y * grid.voxel_size,
                                                              z * grid.voxel_size);
                    voxel_box.max_corner = voxel_box.min_corner + Vec3f(grid.voxel_size,
                                                                       grid.voxel_size,
                                                                       grid.voxel_size);
                    if (triangle_aabb_overlap(tri, voxel_box))
                        grid.at(x, y, z) = material;
                }
            }
        }
    }

    int solid_count = (int)std::count_if(grid.voxels.begin(), grid.voxels.end(),
                                         [](uint8_t v) { return v != 0; });
    printf("Generated %d solid voxels\n", solid_count);
}

/*
 * Generate C source code for the voxel shape.
 */
static bool generate_c_code(const VoxelGrid &grid, const char *input_path,
                            const char *output_path, const char *shape_name)
{
    FILE *f = fopen(output_path, "w");
    if (!f)
    {
        fprintf(stderr, "Error: Cannot write to %s\n", output_path);
        return false;
    }

    /* Count solid voxels */
    int solid_count = 0;
    for (size_t i = 0; i < grid.voxels.size(); i++)
    {
        if (grid.voxels[i] != 0)
            solid_count++;
    }

    /* Create valid C identifier from name */
    std::string c_name = shape_name;
    for (char &c : c_name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    }

    /* Header comment */
    fprintf(f, "/* Auto-generated voxel shape - do not edit */\n");
    fprintf(f, "/* Source: %s */\n", input_path);
    fprintf(f, "/* Resolution: %dx%dx%d, Solid voxels: %d */\n\n",
            grid.size_x, grid.size_y, grid.size_z, solid_count);

    fprintf(f, "#include \"content/voxel_shapes.h\"\n\n");

    /* Voxel data array */
    fprintf(f, "static const uint8_t k_%s_voxels[%d] = {\n",
            c_name.c_str(), (int)grid.voxels.size());

    int items_per_line = 16;
    for (size_t i = 0; i < grid.voxels.size(); i++)
    {
        if (i % items_per_line == 0)
            fprintf(f, "    ");

        fprintf(f, "%3d,", grid.voxels[i]);

        if ((i + 1) % items_per_line == 0 || i == grid.voxels.size() - 1)
            fprintf(f, "\n");
    }
    fprintf(f, "};\n\n");

    /* Shape descriptor */
    fprintf(f, "const VoxelShape g_shape_%s = {\n", c_name.c_str());
    fprintf(f, "    .size_x = %d,\n", grid.size_x);
    fprintf(f, "    .size_y = %d,\n", grid.size_y);
    fprintf(f, "    .size_z = %d,\n", grid.size_z);
    fprintf(f, "    .voxels = k_%s_voxels,\n", c_name.c_str());
    fprintf(f, "};\n");

    fclose(f);
    printf("Generated %s\n", output_path);
    return true;
}

/*
 * Extract shape name from file path.
 */
static std::string extract_name(const char *path)
{
    std::string s(path);

    /* Remove directory path */
    size_t last_slash = s.find_last_of("/\\");
    if (last_slash != std::string::npos)
        s = s.substr(last_slash + 1);

    /* Remove extension */
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos)
        s = s.substr(0, dot);

    return s;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s input.obj output.c [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --name <name>       Shape name (default: from filename)\n");
    fprintf(stderr, "  --resolution <n>    Voxels along the longest axis, 1..%d (default: 16)\n", VOBJ_GRID_SIZE);
    fprintf(stderr, "  --material <id>     Material ID, 1..255 (default: %d)\n", MAT_STONE);
}

/* Whole-string decimal integer in [min_value, max_value] */
static bool parse_int(const char *text, int min_value, int max_value, int *out)
{
    char *end = nullptr;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < min_value || value > max_value)
        return false;
    *out = (int)value;
    return true;
}

static int run(int argc, char *argv[])
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];
    std::string shape_name = extract_name(input_path);
    int resolution = 16;
    uint8_t material = MAT_STONE;

    /* Parse options */
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
        {
            shape_name = argv[++i];
        }
        else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc)
        {
            /* The shapes become voxel objects, whose grid is VOBJ_GRID_SIZE voxels a side */
            const char *value = argv[++i];
            if (!parse_int(value, 1, VOBJ_GRID_SIZE, &resolution))
            {
                fprintf(stderr, "Error: --resolution %s: must be a whole number from 1 to %d "
                                "(VOBJ_GRID_SIZE, the voxel object grid; larger shapes cannot be spawned)\n",
                        value, VOBJ_GRID_SIZE);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--material") == 0 && i + 1 < argc)
        {
            const char *value = argv[++i];
            int mat_value = 0;
            if (!parse_int(value, 1, 255, &mat_value))
            {
                fprintf(stderr, "Error: --material %s: must be a material ID from 1 to 255 "
                                "(0 is air and would make an empty shape)\n",
                        value);
                return 1;
            }
            material = (uint8_t)mat_value;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("Voxelizing %s -> %s\n", input_path, output_path);
    printf("  Shape name: %s\n", shape_name.c_str());
    printf("  Resolution: %d\n", resolution);
    printf("  Material ID: %d\n", material);

    Mesh mesh;
    if (!load_obj(input_path, mesh))
    {
        fprintf(stderr, "Error: Failed to load mesh\n");
        return 1;
    }

    VoxelGrid grid;
    voxelize(mesh, grid, resolution, material);

    if (!generate_c_code(grid, input_path, output_path, shape_name.c_str()))
    {
        fprintf(stderr, "Error: Failed to generate output\n");
        return 1;
    }

    printf("Done!\n");
    return 0;
}

int main(int argc, char *argv[])
{
    /* std::vector and std::string report allocation failure by throwing */
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
