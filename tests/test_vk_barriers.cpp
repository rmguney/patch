#include "engine/render/vk_barriers.h"

#include "test_common.h"

/* Barrier derivation of the synchronization2 helper (no GPU needed): the source scope and old
 * layout come from the tracked state, reads already made visible need no barrier. */

using patch::derive_image_barrier;
using patch::ImageState;
namespace use = patch::image_use;

static const VkPipelineStageFlags2 COMPUTE = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
static const VkPipelineStageFlags2 FRAGMENT = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

static bool same_state(const ImageState &a, const ImageState &b)
{
    return a.layout == b.layout && a.stages == b.stages && a.access == b.access;
}

TEST(first_use_waits_for_nothing)
{
    ImageState state{};
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::compute_write, false, &b));
    ASSERT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    ASSERT_EQ(b.newLayout, VK_IMAGE_LAYOUT_GENERAL);
    ASSERT_EQ(b.srcStageMask, VK_PIPELINE_STAGE_2_NONE);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_NONE);
    ASSERT_EQ(b.dstStageMask, COMPUTE);
    ASSERT_EQ(b.dstAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    ASSERT(same_state(state, use::compute_write));
    return 1;
}

TEST(write_then_read_makes_the_write_visible)
{
    ImageState state = use::compute_write;
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::sampled(COMPUTE | FRAGMENT), false, &b));
    ASSERT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_GENERAL);
    ASSERT_EQ(b.newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ASSERT_EQ(b.srcStageMask, COMPUTE);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    ASSERT_EQ(b.dstStageMask, COMPUTE | FRAGMENT);
    ASSERT_EQ(b.dstAccessMask, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return 1;
}

TEST(covered_read_needs_no_barrier)
{
    ImageState state = use::sampled(COMPUTE | FRAGMENT);
    VkImageMemoryBarrier2 b;
    ASSERT(!derive_image_barrier(state, use::sampled(FRAGMENT), false, &b));
    ASSERT(same_state(state, use::sampled(COMPUTE | FRAGMENT)));
    return 1;
}

TEST(new_reader_chains_after_earlier_readers)
{
    ImageState state = use::sampled(FRAGMENT);
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::sampled(COMPUTE), false, &b));
    ASSERT_EQ(b.oldLayout, b.newLayout);
    ASSERT_EQ(b.srcStageMask, FRAGMENT);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_NONE);
    ASSERT_EQ(b.dstStageMask, COMPUTE);
    /* The next writer has to wait for both readers */
    ASSERT_EQ(state.stages, COMPUTE | FRAGMENT);
    ASSERT(derive_image_barrier(state, use::compute_write, true, &b));
    ASSERT_EQ(b.srcStageMask, COMPUTE | FRAGMENT);
    return 1;
}

TEST(discard_after_reads_is_write_after_read)
{
    ImageState state = use::sampled(COMPUTE | FRAGMENT);
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::compute_write, true, &b));
    ASSERT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    ASSERT_EQ(b.srcStageMask, COMPUTE | FRAGMENT);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_NONE);
    return 1;
}

TEST(discard_after_write_still_orders_the_write)
{
    ImageState state = use::depth_attachment;
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::depth_attachment, true, &b));
    ASSERT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    ASSERT_EQ(b.srcStageMask, use::depth_attachment.stages);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    return 1;
}

TEST(write_after_write_in_one_layout)
{
    ImageState state = use::compute_write;
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::compute_write, false, &b));
    ASSERT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_GENERAL);
    ASSERT_EQ(b.newLayout, VK_IMAGE_LAYOUT_GENERAL);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    return 1;
}

TEST(present_after_attachment_writes)
{
    ImageState state = use::color_attachment;
    VkImageMemoryBarrier2 b;
    ASSERT(derive_image_barrier(state, use::present, false, &b));
    ASSERT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ASSERT_EQ(b.newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ASSERT_EQ(b.srcStageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    ASSERT_EQ(b.srcAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    ASSERT_EQ(b.dstStageMask, VK_PIPELINE_STAGE_2_NONE);
    return 1;
}

int main()
{
    printf("=== Barrier Derivation Tests (Non-GPU) ===\n\n");

    RUN_TEST(first_use_waits_for_nothing);
    RUN_TEST(write_then_read_makes_the_write_visible);
    RUN_TEST(covered_read_needs_no_barrier);
    RUN_TEST(new_reader_chains_after_earlier_readers);
    RUN_TEST(discard_after_reads_is_write_after_read);
    RUN_TEST(discard_after_write_still_orders_the_write);
    RUN_TEST(write_after_write_in_one_layout);
    RUN_TEST(present_after_attachment_writes);

    printf("\nResults: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
