#include "copy_to_swapchain.h"
#include "utils.h"

void Copy_To_Swapchain::create() {
    // set layout
    {
        VkDescriptorSetLayoutBinding layout_bindings[3] {};
        layout_bindings[0].binding          = 0;
        layout_bindings[0].descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLER;
        layout_bindings[0].descriptorCount  = 1;
        layout_bindings[0].stageFlags       = VK_SHADER_STAGE_COMPUTE_BIT;

        layout_bindings[1].binding          = 1;
        layout_bindings[1].descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        layout_bindings[1].descriptorCount  = 1;
        layout_bindings[1].stageFlags       = VK_SHADER_STAGE_COMPUTE_BIT;

        layout_bindings[2].binding          = 2;
        layout_bindings[2].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        layout_bindings[2].descriptorCount  = 1;
        layout_bindings[2].stageFlags       = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo create_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        create_info.bindingCount = (uint32_t)std::size(layout_bindings);
        create_info.pBindings = layout_bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &create_info, nullptr, &set_layout));
    }

    // pipeline layout
    {
        VkPushConstantRange range;
        range.stageFlags    = VK_SHADER_STAGE_COMPUTE_BIT;
        range.offset        = 0;
        range.size          = 8; // uint32 width + uint32 height

        VkPipelineLayoutCreateInfo create_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        create_info.setLayoutCount          = 1;
        create_info.pSetLayouts             = &set_layout;
        create_info.pushConstantRangeCount  = 1;
        create_info.pPushConstantRanges     = &range;

        VK_CHECK(vkCreatePipelineLayout(vk.device, &create_info, nullptr, &pipeline_layout));
    }

    // pipeline
    {
        VkShaderModule copy_shader = vk_load_spirv("spirv/copy_to_swapchain.comp.spv");

        VkPipelineShaderStageCreateInfo compute_stage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        compute_stage.stage    = VK_SHADER_STAGE_COMPUTE_BIT;
        compute_stage.module   = copy_shader;
        compute_stage.pName    = "main";

        VkComputePipelineCreateInfo create_info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        create_info.stage = compute_stage;
        create_info.layout = pipeline_layout;
        VK_CHECK(vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));

        vkDestroyShaderModule(vk.device, copy_shader, nullptr);
    }

    // point sampler
    {
        VkSamplerCreateInfo create_info { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        VK_CHECK(vkCreateSampler(vk.device, &create_info, nullptr, &point_sampler));
        vk_set_debug_name(point_sampler, "point_sampler");
    }
}

void Copy_To_Swapchain::destroy() {
    vkDestroyDescriptorSetLayout(vk.device, set_layout, nullptr);
    vkDestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
    vkDestroyPipeline(vk.device, pipeline, nullptr);
    vkDestroySampler(vk.device, point_sampler, nullptr);
    sets.clear();
}

void Copy_To_Swapchain::update_resolution_dependent_descriptors(VkImageView output_image_view) {
    if (sets.size() < vk.swapchain_info.images.size())
    {
        size_t n = vk.swapchain_info.images.size() - sets.size();
        for (size_t i = 0; i < n; i++)
        {
            VkDescriptorSetAllocateInfo alloc_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            alloc_info.descriptorPool     = vk.descriptor_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts        = &set_layout;

            VkDescriptorSet set;
            VK_CHECK(vkAllocateDescriptorSets(vk.device, &alloc_info, &set));
            sets.push_back(set);

            Descriptor_Writes(set).sampler(0, point_sampler);
        }
    }

    for (size_t i = 0; i < vk.swapchain_info.images.size(); i++) {
        Descriptor_Writes(sets[i])
            .sampled_image(1, output_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .storage_image(2, vk.swapchain_info.image_views[i]);
    }
}