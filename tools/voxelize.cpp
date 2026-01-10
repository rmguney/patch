/*
 * voxelize.cpp - Build-time mesh to voxel conversion tool
 *
 * Converts OBJ mesh files to C source files containing VoxelShape descriptors.
 * Uses conservative triangle-AABB overlap testing for voxelization.
 *
 * Usage: voxelize input.obj output.c [options]
 *   --name <name>      Shape name (default: derived from filename)
 *   --resolution <n>   Target voxel resolution along longest axis (default: 16)
 *   --material <id>    Material ID for solid voxels (default: 1)
 *
 * Output: C source file with VoxelShape descriptor ready for content/ inclusion.
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/*
 * Default material ID for voxelization.
 * Must match MAT_STONE in content/materials.h (currently 1).
 * If material IDs are reorganized, update this constant.
 */
#define VOXELIZE_DEFAULT_MATERIAL 1

/* Vec3 - simple 3D vector */
struct Vec3
{
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3 &b) const { return Vec3(x + b.x, y + b.y, z + b.z); }
    Vec3 operator-(const Vec3 &b) const { return Vec3(x - b.x, y - b.y, z - b.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }

    float dot(const Vec3 &b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3 cross(const Vec3 &b) const
    {
        return Vec3(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const
    {
        float len = length();
        return len > 0.0001f ? Vec3(x / len, y / len, z / len) : Vec3();
    }

    static Vec3 min(const Vec3 &a, const Vec3 &b)
    {
        return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    }
    static Vec3 max(const Vec3 &a, const Vec3 &b)
    {
        return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    }
};

/* Triangle */
struct Triangle
{
    Vec3 v0, v1, v2;
};

/* AABB */
struct AABB
{
    Vec3 min_corner, max_corner;

    Vec3 center() const { return (min_corner + max_corner) * 0.5f; }
    Vec3 half_extents() const { return (max_corner - min_corner) * 0.5f; }
};

/* Mesh data */
struct Mesh
{
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    AABB bounds;
};

/* Voxel grid */
struct VoxelGrid
{
    int size_x, size_y, size_z;
    std::vector<uint8_t> voxels;
    float voxel_size;
    Vec3 origin;

    uint8_t &at(int x, int y, int z)
    {
        return voxels[x + y * size_x + z * size_x * size_y];
    }
    uint8_t at(int x, int y, int z) const
    {
        return voxels[x + y * size_x + z * size_x * size_y];
    }
};

/*
 * Triangle-AABB overlap test using Separating Axis Theorem.
 * Based on Tomas Akenine-Moller's algorithm.
 */
static bool triangle_aabb_overlap(const Triangle &tri, const AABB &box)
{
    Vec3 center = box.center();
    Vec3 half = box.half_extents();

    /* Move triangle to box center */
    Vec3 v0 = tri.v0 - center;
    Vec3 v1 = tri.v1 - center;
    Vec3 v2 = tri.v2 - center;

    /* Compute triangle edges */
    Vec3 e0 = v1 - v0;
    Vec3 e1 = v2 - v1;
    Vec3 e2 = v0 - v2;

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
    Vec3 normal = e0.cross(e1);
    float d = normal.dot(v0);
    float r = half.x * std::abs(normal.x) + half.y * std::abs(normal.y) +
              half.z * std::abs(normal.z);
    if (std::abs(d) > r)
        return false;

    /* Test 9 edge cross products (3 edges x 3 axes) */
    auto axis_test = [&](const Vec3 &axis) {
        float p0 = axis.dot(v0);
        float p1 = axis.dot(v1);
        float p2 = axis.dot(v2);
        float r = half.x * std::abs(axis.x) + half.y * std::abs(axis.y) +
                  half.z * std::abs(axis.z);
        float min_p = std::min({p0, p1, p2});
        float max_p = std::max({p0, p1, p2});
        return !(min_p > r || max_p < -r);
    };

    Vec3 axes[9] = {
        Vec3(0, -e0.z, e0.y), Vec3(0, -e1.z, e1.y), Vec3(0, -e2.z, e2.y),
        Vec3(e0.z, 0, -e0.x), Vec3(e1.z, 0, -e1.x), Vec3(e2.z, 0, -e2.x),
        Vec3(-e0.y, e0.x, 0), Vec3(-e1.y, e1.x, 0), Vec3(-e2.y, e2.x, 0),
    };

    for (int i = 0; i < 9; i++)
    {
        if (axes[i].length() > 0.0001f && !axis_test(axes[i]))
            return false;
    }

    return true;
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

    char line[512];
    mesh.bounds.min_corner = Vec3(1e10f, 1e10f, 1e10f);
    mesh.bounds.max_corner = Vec3(-1e10f, -1e10f, -1e10f);

    while (fgets(line, sizeof(line), f))
    {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        /* Vertex */
        if (line[0] == 'v' && line[1] == ' ')
        {
            Vec3 v;
            if (sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z) == 3)
            {
                mesh.vertices.push_back(v);
                mesh.bounds.min_corner = Vec3::min(mesh.bounds.min_corner, v);
                mesh.bounds.max_corner = Vec3::max(mesh.bounds.max_corner, v);
            }
        }
        /* Face */
        else if (line[0] == 'f' && line[1] == ' ')
        {
            /* Parse face indices (supports v, v/vt, v/vt/vn, v//vn) */
            std::vector<int> indices;
            char *ptr = line + 2;

            while (*ptr)
            {
                while (*ptr && std::isspace(*ptr))
                    ptr++;
                if (!*ptr)
                    break;

                int idx = 0;
                if (sscanf(ptr, "%d", &idx) == 1)
                {
                    /* OBJ indices are 1-based */
                    if (idx < 0)
                        idx = (int)mesh.vertices.size() + idx + 1;
                    indices.push_back(idx - 1);
                }

                /* Skip to next vertex */
                while (*ptr && !std::isspace(*ptr))
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
    Vec3 extent = mesh.bounds.max_corner - mesh.bounds.min_corner;
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

    /* Compute grid dimensions */
    grid.size_x = (int)std::ceil(extent.x / grid.voxel_size);
    grid.size_y = (int)std::ceil(extent.y / grid.voxel_size);
    grid.size_z = (int)std::ceil(extent.z / grid.voxel_size);

    /* Ensure at least 1 voxel per axis */
    grid.size_x = std::max(1, grid.size_x);
    grid.size_y = std::max(1, grid.size_y);
    grid.size_z = std::max(1, grid.size_z);

    /* Allocate voxel grid */
    grid.voxels.resize(grid.size_x * grid.size_y * grid.size_z, 0);

    printf("Voxelizing to %dx%dx%d grid (voxel size: %.4f)\n",
           grid.size_x, grid.size_y, grid.size_z, grid.voxel_size);

    /* For each voxel, test against all triangles */
    int solid_count = 0;
    for (int z = 0; z < grid.size_z; z++)
    {
        for (int y = 0; y < grid.size_y; y++)
        {
            for (int x = 0; x < grid.size_x; x++)
            {
                /* Compute voxel AABB */
                AABB voxel_box;
                voxel_box.min_corner = grid.origin + Vec3(
                                                         x * grid.voxel_size,
                                                         y * grid.voxel_size,
                                                         z * grid.voxel_size);
                voxel_box.max_corner = voxel_box.min_corner + Vec3(
                                                                  grid.voxel_size,
                                                                  grid.voxel_size,
                                                                  grid.voxel_size);

                /* Test against all triangles */
                for (const Triangle &tri : mesh.triangles)
                {
                    if (triangle_aabb_overlap(tri, voxel_box))
                    {
                        grid.at(x, y, z) = material;
                        solid_count++;
                        break;
                    }
                }
            }
        }
    }

    printf("Generated %d solid voxels\n", solid_count);
}

/*
 * Generate C source code for the voxel shape.
 */
static bool generate_c_code(const VoxelGrid &grid, const char *output_path,
                            const char *shape_name)
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
        if (!std::isalnum(c))
            c = '_';
    }

    /* Header comment */
    fprintf(f, "/* Auto-generated voxel shape - do not edit */\n");
    fprintf(f, "/* Source: %s */\n", shape_name);
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
    fprintf(f, "    .name = \"%s\",\n", shape_name);
    fprintf(f, "    .size_x = %d,\n", grid.size_x);
    fprintf(f, "    .size_y = %d,\n", grid.size_y);
    fprintf(f, "    .size_z = %d,\n", grid.size_z);
    fprintf(f, "    .voxels = k_%s_voxels,\n", c_name.c_str());
    fprintf(f, "    .solid_count = %d,\n", solid_count);
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
    fprintf(stderr, "  --resolution <n>    Voxel resolution (default: 16)\n");
    fprintf(stderr, "  --material <id>     Material ID (default: 1)\n");
}

int main(int argc, char *argv[])
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
    uint8_t material = VOXELIZE_DEFAULT_MATERIAL;

    /* Parse options */
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
        {
            shape_name = argv[++i];
        }
        else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc)
        {
            resolution = atoi(argv[++i]);
            if (resolution < 1)
                resolution = 1;
            if (resolution > 128)
                resolution = 128;
        }
        else if (strcmp(argv[i], "--material") == 0 && i + 1 < argc)
        {
            int mat_value = atoi(argv[++i]);
            if (mat_value < 0 || mat_value > 255)
            {
                fprintf(stderr, "Warning: material ID %d out of range [0,255], clamping\n", mat_value);
                mat_value = mat_value < 0 ? 0 : 255;
            }
            if (mat_value >= 64)
            {
                fprintf(stderr, "Note: material ID %d is high; content/materials.h defines ~23 materials\n", mat_value);
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

    if (!generate_c_code(grid, output_path, shape_name.c_str()))
    {
        fprintf(stderr, "Error: Failed to generate output\n");
        return 1;
    }

    printf("Done!\n");
    return 0;
}
