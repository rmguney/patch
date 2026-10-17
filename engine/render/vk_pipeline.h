#ifndef PATCH_RENDER_VK_PIPELINE_H
#define PATCH_RENDER_VK_PIPELINE_H

#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace patch
{

    class VulkanContext;

    struct ShaderCode
    {
        const uint32_t *code;
        size_t size;
    };

    /* Binding i of a descriptor set layout */
    struct BindingDesc
    {
        VkDescriptorType type;
        VkShaderStageFlags stages;
    };

    bool create_set_layout(VkDevice device, std::initializer_list<BindingDesc> bindings, VkDescriptorSetLayout *out);

    /* push_size 0: no push constants */
    bool create_pipeline_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> set_layouts,
                                VkShaderStageFlags push_stages, uint32_t push_size, VkPipelineLayout *out);

    bool create_descriptor_pool(VkDevice device, uint32_t max_sets, std::initializer_list<VkDescriptorPoolSize> sizes,
                                VkDescriptorPool *out);

    /* count sets of one layout */
    bool allocate_sets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout, uint32_t count,
                       VkDescriptorSet *out);

    bool create_compute_pipeline(const VulkanContext &ctx, ShaderCode shader, VkPipelineLayout layout,
                                 VkPipeline *out);

    /* Graphics pipelines draw with dynamic rendering into the attachment formats named here;
     * viewport and scissor are dynamic, front faces counter-clockwise, one sample */
    struct GraphicsPipelineDesc
    {
        ShaderCode vertex;
        ShaderCode fragment;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        const VkFormat *color_formats = nullptr;
        uint32_t color_count = 0;
        VkFormat depth_format = VK_FORMAT_UNDEFINED;
        bool depth_test = false;
        bool depth_write = false;
        VkCompareOp depth_compare = VK_COMPARE_OP_NEVER;
        VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
        bool alpha_blend = false; /* colour: src * a + dst * (1 - a); alpha: src + dst * (1 - a) */
        const VkPipelineVertexInputStateCreateInfo *vertex_input = nullptr;
    };

    bool create_graphics_pipeline(const VulkanContext &ctx, const GraphicsPipelineDesc &desc, VkPipeline *out);

    /* Null-safe; reset the handle */
    void destroy_pipeline(VkDevice device, VkPipeline *pipeline);
    void destroy_pipeline_layout(VkDevice device, VkPipelineLayout *layout);
    void destroy_set_layout(VkDevice device, VkDescriptorSetLayout *layout);
    void destroy_descriptor_pool(VkDevice device, VkDescriptorPool *pool);
    void destroy_sampler(VkDevice device, VkSampler *sampler);

    /* Nearest texel and nearest mip, mips 0..max_lod */
    bool create_point_sampler(VkDevice device, VkSamplerAddressMode address_mode, float max_lod, VkSampler *out);

    /* Bilinear, mip 0 only */
    bool create_linear_sampler(VkDevice device, VkSamplerAddressMode address_mode, VkSampler *out);

    /* Collects descriptor writes and applies them in one vkUpdateDescriptorSets */
    class DescriptorWriter
    {
    public:
        explicit DescriptorWriter(VkDevice device) : device_(device) {}

        /* Combined image sampler, read in SHADER_READ_ONLY_OPTIMAL */
        DescriptorWriter &sampled(VkDescriptorSet set, uint32_t binding, VkSampler sampler, VkImageView view);
        /* Storage image, written in GENERAL */
        DescriptorWriter &storage(VkDescriptorSet set, uint32_t binding, VkImageView view);
        DescriptorWriter &buffer(VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkBuffer buffer,
                                 VkDeviceSize range = VK_WHOLE_SIZE);

        void update();

    private:
        VkWriteDescriptorSet &next(VkDescriptorSet set, uint32_t binding, VkDescriptorType type);

        static constexpr uint32_t MAX_WRITES = 32;

        VkDevice device_;
        VkWriteDescriptorSet writes_[MAX_WRITES];
        VkDescriptorImageInfo images_[MAX_WRITES];
        VkDescriptorBufferInfo buffers_[MAX_WRITES];
        uint32_t count_ = 0;
    };

}

#endif
