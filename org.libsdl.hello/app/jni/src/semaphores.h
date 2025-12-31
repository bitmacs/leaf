#pragma once

#include "vk.h"
#include <cassert>
#include <unordered_set>

struct SemaphorePool {
    std::unordered_set<VkSemaphore> idle_semaphores;
    std::unordered_set<VkSemaphore> busy_semaphores;

    VkSemaphore acquire_semaphore(VkContext *context) {
        if (idle_semaphores.empty()) {
            VkSemaphoreCreateInfo semaphore_create_info = {};
            semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphore_create_info.flags = VK_SEMAPHORE_TYPE_BINARY;
            VkSemaphore semaphore;
            VkResult result = vkCreateSemaphore(context->device, &semaphore_create_info, nullptr, &semaphore);
            assert(result == VK_SUCCESS);
            idle_semaphores.insert(semaphore);
        }
        VkSemaphore semaphore = *idle_semaphores.begin();
        idle_semaphores.erase(semaphore);
        busy_semaphores.insert(semaphore);
        return semaphore;
    }

    void return_semaphore(VkSemaphore semaphore) {
        busy_semaphores.erase(semaphore);
        idle_semaphores.insert(semaphore);
    }

    void cleanup(VkContext *context) {
        for (auto &semaphore: idle_semaphores) {
            vkDestroySemaphore(context->device, semaphore, nullptr);
        }
        idle_semaphores.clear();
        for (auto &semaphore: busy_semaphores) {
            vkDestroySemaphore(context->device, semaphore, nullptr);
        }
        busy_semaphores.clear();
    }
};
